#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <system_error>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

#include "sync_basic.h"

// 工作流程（与 sync_server.h 对齐）
// 1. 上层调用 scanServers() 同步扫描局域网 阻塞返回去重后的服务器列表
// 2. 调用 startDownload(server_index, cb) 发起一次下载会话（异步立即返回）
//    连接服务器后客户端不再需要上层操作 会话内被动接收 结束/失败/断开时回调
//    - 每个文件：接收文件头 -> 与本地“成功下载记录”比对：
//        * 无记录：发送 1 字节 '1' 通知服务器发送文件 接收文件数据 成功后写入记录
//        * 已有成功记录 发送 1 字节 '0' 通知服务器跳过该文件
//    - 特殊情况 服务器发送队列为空时由服务器主动断开连接
//      回调以 success=true、ec=errc::no_message_available 提示上层“发送队列为空”（本次无文件可下载）

// 成功下载记录（身份 = parent_dir + file_name + file_size）
struct DownloadRecord
{
    std::string parent_dir_;  // 相对父目录
    std::string file_name_;   // 文件名
    std::uint64_t file_size_; // 文件大小 完整性校验
};

class SyncClient
{
public:
    // 默认下载路径构造时固定 下载记录持久化到 download_path_/records.json
    explicit SyncClient(std::uint16_t port = UDP_DEFAULT_PORT, std::string magic_word = UDP_DEFAULT_MAGIC, const std::filesystem::path &download_path = "./download");
    ~SyncClient();

    SyncClient(const SyncClient &) = delete;
    SyncClient &operator=(const SyncClient &) = delete;

    // 同步扫描局域网服务器 阻塞返回去重后的服务器列表。
    // num_attempts：扫描轮数（每轮内部有总超时） 结果跨轮合并去重 1 为单轮
    // 注意：下载进行中不要调用本函数（servers_ 会被替换）
    std::vector<ServerInfo> scanServers(size_t num_attempts = 1);

    // 异步下载会话：立即返回 会话结束（下载完成/服务器队列为空/失败）时在工作线程回调
    // - isDownloading() 为 true 时重复调用：立即回调失败（ec = errc::operation_in_progress）
    // - server_index 越界：立即回调失败（ec = errc::result_out_of_range）
    // - 回调语义：
    //     success=true  ec 清空                         —— 至少完成 1 个文件后服务器正常断开
    //     success=true  ec=errc::no_message_available   —— 服务器发送队列为空 本次无文件可下载（正常完成）
    //     success=false ec=其他                         —— 连接失败/中途断开/协议错误
    void startDownload(std::size_t server_index, std::function<void(bool success, std::error_code ec)> cb);

    // 断开当前连接并终止下载会话（工作线程在下一个有界阻塞点退出并回调失败）
    void disconnect();

    // 清除下载缓存记录（records.json）：下次下载将重新下载全部文件（不删除已下载的文件本身）
    // 线程安全：下载进行中调用也不会产生数据竞争
    void clearDownloadRecords();

    // 状态查询（线程安全）
    bool isDownloading() const;
    const std::filesystem::path &getDownloadPath() const;
    const std::vector<ServerInfo> &getServers() const;
    std::string getLastError() const;

private:
    asio::io_context io_;
    std::uint16_t port_;                  // UDP 广播端口
    std::string magic_word_;              // 广播过滤魔术字
    std::filesystem::path download_path_; // 固定下载路径
    std::vector<ServerInfo> servers_;

    std::unique_ptr<BroadcastReceiver> receiver_;
    std::unique_ptr<TcpConnection> conn_;

    // 成功下载记录 持久化到 records_path_
    std::vector<DownloadRecord> download_records_;
    std::filesystem::path records_path_;
    mutable std::mutex records_mutex_; // 保护 download_records_ 与记录文件

    // 线程管理
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> is_busy_{false};

    std::queue<std::function<void()>> task_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable cv_;
    mutable std::mutex error_mutex_;
    mutable std::string error_string_;

    // 后台工作线程主循环
    void workerLoop();
    // 执行下载会话（在工作线程中运行）
    void doDownload(std::size_t index, std::function<void(bool success, std::error_code ec)> cb);
    // 连接服务器（同步内部调用）
    bool syncConnect(const ServerInfo &server, std::error_code &ec);
    // 接收整个会话：文件头 -> 比对记录 -> 回复 -> 接收数据 循环；
    // 服务器发送队列为空主动断开时返回 errc::no_message_available
    bool syncReceiveAll(std::error_code &ec);
    // 检查文件是否已有成功下载记录（记录是跳过判定的唯一依据）
    // 清除记录后（clearDownloadRecords）下次下载将重新下载全部文件
    bool isFileAlreadyExists(const FileHeader &header) const;
    // 发送回复字节：true -> '1'(发送文件)，false -> '0'(跳过)
    bool sendReply(bool should_send, std::error_code &ec);
    // 下载记录读写（加载失败不致命 仅记录错误信息）
    bool loadRecords(std::error_code &ec);
    bool saveRecords(std::error_code &ec);
    // 线程安全设置错误信息
    void setError(const std::string &msg);
    static std::string serverKey(const ServerInfo &s);
    // 带超时的阻塞式连接：非阻塞 connect + select/poll 等待可写 再检查 SO_ERROR
    static bool connectWithTimeout(asio::ip::tcp::socket &sock, const asio::ip::tcp::endpoint &ep, std::chrono::milliseconds timeout, std::error_code &ec);
    // 安全拼接保存路径：只允许相对组件 丢弃 .. 与盘符 防止目录穿越
    static std::filesystem::path buildSavePath(const std::filesystem::path &download_path, const std::string &parent_dir, const std::string &file_name);
};
