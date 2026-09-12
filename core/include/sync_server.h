#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <queue>
#include <vector>
#include <filesystem>
#include <system_error>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

#include "sync_basic.h"

// 一对一连接模式（与 sync_client.h 对齐）
// 工作流程
// 1. start() 启动工作线程 无客户端时：周期广播一次消息 + 轮询接受连接（非阻塞 accept + 短休眠）
//    两者在同一线程交替执行 保证 stop() 可打断
// 2. 客户端连接成功：停止广播，从队列取目录逐个发送其中所有文件
//    每个文件：发送文件头 -> 等待客户端回复 1 字节（'1' = 发送数据，'0' = 跳过）
//    会话期间 enqueueDirectory() 推入的目录会继续按序发送
// 3. 队列为空：等待 empty_queue_wait 分钟（期间有新目录继续发送）超时仍为空才断开客户端
//    清空队列并重新开始广播
// 4. stop()/析构：停止工作线程并清理

class SyncServer
{
public:
    // 构造时固定广播相关信息（UDP 端口与魔术字）与空队列等待时长
    // empty_queue_wait：会话中队列为空时等待新目录入队的时长 超时仍为空才断开客户端
    SyncServer(std::uint16_t port = UDP_DEFAULT_PORT, std::string magic_word = UDP_DEFAULT_MAGIC, std::chrono::minutes empty_queue_wait = std::chrono::minutes(5));
    ~SyncServer();

    // 禁止拷贝
    SyncServer(const SyncServer &) = delete;
    SyncServer &operator=(const SyncServer &) = delete;

    // 启动工作线程：绑定 TCP 端口（port == 0 时由系统分配，广播广告实际端口）
    // 返回 false 表示启动失败（ec 说明） 成功后工作线程运行
    // 工作线程回调（勿在回调内做阻塞或 UI 操作）：
    //   cb(true, errc::no_message_available) —— 会话中发送队列为空 上层可趁等待期继续入队
    //   cb(true, {})                           —— 会话正常结束（等待超时后断开客户端）
    //   cb(false, ec)                          —— 会话出错/被中断
    bool start(std::string server_name, std::uint16_t port, std::error_code &ec, std::function<void(bool, std::error_code)> cb = {});
    // 停止工作线程并清理（幂等）
    void stop(std::error_code &ec);

    // 选择要发送的目录 推入队列（客户端自行校验进度）
    void enqueueDirectory(const std::filesystem::path &dir);
    // 获取待发送目录队列的快照（线程安全 返回拷贝 可安全迭代）
    std::vector<std::filesystem::path> getTaskQueue() const;
    // 断开当前客户端并清空队列（随后重新开始广播）：
    // 置位断开请求并唤醒工作线程 由工作线程在下一个有界阻塞点完成断开
    // （不直接操作 client_ 避免与工作线程的指针竞态）
    void disconnect(std::error_code &ec);
    // 手动指定对外广告的 IP（默认 start 时按本机首个可用接口自动解析
    // 多网卡/VPN 环境自动解析错误时可覆盖 须在 start() 前调用）
    void setAdvertiseIP(const std::string &ip);

    // 线程安全
    std::string getLastError() const;

private:
    asio::io_context io_;
    asio::ip::tcp::acceptor acceptor_;
    std::string magic_word_;
    std::uint16_t udp_port_;
    std::string server_name_;
    std::uint16_t server_port_;             // 实际绑定的 TCP 端口（广播广告用）
    std::string advertise_ip_;              // 对外广告的 IP（start 时按接口解析 可 setAdvertiseIP 覆盖）
    std::chrono::minutes empty_queue_wait_; // 空队列等待时长（构造时固定）

    std::unique_ptr<BroadcastSender> broadcaster_;
    std::unique_ptr<TcpConnection> client_;

    std::queue<std::filesystem::path> task_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable cv_;

    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> client_connected_{false};
    std::atomic<bool> broadcasting_{false};
    std::atomic<bool> started_{false};
    std::atomic<bool> disconnect_requested_{false}; // disconnect() 置位 工作线程消费
    mutable std::mutex error_mutex_;
    mutable std::string error_string_;

    // 工作线程主循环（状态机：广播 + 轮询 accept <-> 会话发送）
    // 所有阻塞 I/O 均有界（套接字超时）保证 stop()/disconnect() 可打断并安全 join
    void workerLoop(std::function<void(bool, std::error_code)> cb);
    // 广播一次消息（单次 循环由 workerLoop 控制）
    bool broadcastMessage(std::error_code &ec);
    // 尝试接受一个客户端连接（非阻塞轮询 无连接返回 false）
    bool tryAccept(std::error_code &ec);
    // 发送一个目录：每个文件 发送文件头 -> 等待客户端回复 -> 决定是否发送数据
    bool sendDirectory(const std::filesystem::path &dir, std::error_code &ec);
    // 等待客户端回复一个字节（'1' = 发送 '0' = 跳过；其他值视为协议错误）
    bool waitForClientReply(std::error_code &ec, bool &shouldSend);
    // 断开并清理客户端连接（workerLoop / stop 共用）
    void closeClient(std::error_code &ec);
    // 线程安全设置错误信息
    void setError(const std::string &msg);
    // 获取本机 IP（多网卡时取首个非回环 v4 地址 必要时调用方可自行指定）
    std::string getLocalIP() const;

    // 便携休眠（避免依赖 std::this_thread::sleep_for 的工具链差异）
    static void sleepMillis(long long ms);
    // 递归收集目录下所有普通文件 排序保证发送顺序确定
    static void collectFiles(const std::filesystem::path &root, std::vector<std::filesystem::path> &out);
    // 按接口枚举解析本机首个可用 IPv4 地址及其子网定向广播地址
    // （Linux 规范：getifaddrs Windows 用等效的 GetAdaptersAddresses）
    static bool getInterfaceInfo(std::string &ip, std::string &broadcast, std::error_code &ec);
};
