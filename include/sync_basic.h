#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <string>
#include <vector>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <system_error>
#include <nlohmann/json.hpp>
#include <asio.hpp>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#endif

#define UDP_DEFAULT_PORT 11451
#define UDP_DEFAULT_MAGIC "0x114514"

// 模块三 同步收发基础层 —— 设计约束（全部同步 不重试）
// 1. 所有接口均为同步阻塞实现 无异步回调
// 2. BroadcastSender::send 只单次发送，重复广播由调用方循环控制，不重试；
// 3. BroadcastReceiver::scan 阻塞收集“所有”合法广播后返回：
//    持续接收直到出现安静期（quiet_timeout 内无任何报文）或达到 max_servers /
//    total_timeout 按 ip:port 去重 非法报文与魔术字不匹配仅跳过 不中断扫描
// 4. TcpConnection 单条连接 4 字节长度前缀成帧 不自动重连
//    接收端对长度前缀设上限 防止恶意报文导致超大内存分配

struct FileHeader
{
    std::string parent_dir_;  // 相对父目录
    std::string file_name_;   // 文件名
    std::uint64_t file_size_; // 文件总大小
};

struct UDPMessage
{
    std::string name_;       // 服务器名称
    std::string ip_;         // IP 地址
    std::uint16_t port_;     // TCP 端口
    std::string magic_word_; // 广播报文魔术字 接收端过滤用，对应 json 字段 "magic_word"
};

struct ServerInfo
{
    std::string name_;   // 服务器名称
    std::string ip_;     // IP 地址
    std::uint16_t port_; // TCP 端口
};

// UDP 广播发送器：单次发送 重复广播由调用方循环控制 不重试无确认
class BroadcastSender
{
public:
    // broadcast_addr：广播目标地址 默认 255.255.255.255 多网卡时可传子网定向广播地址
    explicit BroadcastSender(asio::io_context &io, uint16_t port, const std::string &broadcast_addr = "255.255.255.255");
    ~BroadcastSender();

    BroadcastSender(const BroadcastSender &) = delete;
    BroadcastSender &operator=(const BroadcastSender &) = delete;

    // 向局域网广播一条报文（单次）报文超过 UDP 载荷上限时 ec = errc::message_size
    void send(const UDPMessage &msg, std::error_code &ec);

private:
    asio::ip::udp::socket socket_;
    asio::ip::udp::endpoint endpoint_;

    // UDP 单包最大载荷（65536 - 20 IP头 - 8 UDP头）
    static constexpr std::size_t kMaxDatagramSize = 65507;
};

// UDP 广播接收器 scan() 阻塞收集所有合法广播后返回
//   已知限制(设计如此 未改动): 接收器必须绑定 UDP_DEFAULT_PORT(11451) 才能收到服务端发往该端口的广播。
//   同一台机器同时运行多个实例时该端口会被多个接收器同时占用:
//   Windows 下重复绑定(SO_REUSEADDR)后广播报文只投递给其中一个接收者(通常先占用的实例)
//   后启动实例的 scan() 收不到任何报文(或绑定失败) 表现为"客户端搜索不到设备"
//   关闭先启动的实例释放端口后, 后启动实例才能正常扫描
//   跨机器(局域网不同主机)测试无此限制 同机多实例自测需错开运行或改用不同机器/虚拟机
class BroadcastReceiver
{
public:
    // magic_word    : 过滤魔术字 默认与 UDP_DEFAULT_MAGIC 一致
    // quiet_timeout : 安静期 scan 内最后一次收到报文后等待该时长判定扫描结束
    explicit BroadcastReceiver(asio::io_context &io, uint16_t port, std::string magic_word = UDP_DEFAULT_MAGIC, std::chrono::milliseconds quiet_timeout = std::chrono::milliseconds(300));
    ~BroadcastReceiver();

    BroadcastReceiver(const BroadcastReceiver &) = delete;
    BroadcastReceiver &operator=(const BroadcastReceiver &) = delete;

    // 收集所有合法广播 按 ip:port 去重 后返回。
    // 结束条件（任一）：出现安静期 / 收集满 max_servers 台 / 超过 total_timeout。
    // 安静期结束视为正常结束 ec 清零 无服务器也返回空列表而非错误
    // 套接字级错误通过 ec 上报 此时返回已收集的部分结果
    std::vector<ServerInfo> scan(std::error_code &ec, std::size_t max_servers = 128, std::chrono::milliseconds total_timeout = std::chrono::milliseconds(2000));

private:
    asio::ip::udp::socket socket_;
    std::string magic_word_;
    std::chrono::milliseconds quiet_timeout_;

    static bool parseUDPMessage(const std::string &data, UDPMessage &out_msg);
    // SO_RCVTIMEO 到期类错误 无包可收判定：Windows 为 WSAETIMEDOUT，POSIX 为 EAGAIN/EWOULDBLOCK
    static bool isReceiveQuietEnd(const std::error_code &ec);
    static std::string serverKey(const ServerInfo &s);
};

// TCP 消息连接：4 字节大端长度前缀成帧 单条连接不重连
class TcpConnection
{
public:
    explicit TcpConnection(asio::ip::tcp::socket socket);
    ~TcpConnection();

    TcpConnection(TcpConnection &&) = default;
    TcpConnection &operator=(TcpConnection &&) = default;

    // 发送文件头 JSON 不含文件数据
    void sendHeader(const FileHeader &header, std::error_code &ec);
    // 发送文件数据块 从 offset 开始读取最多 chunk_size 字节并发送
    // bytes_sent 可空 回传实际发送字节数 0 表示已到文件末尾 正常结束 ec 清零
    // 调用方应据此停止发送 避免对端死等剩余字节
    void sendFileData(const std::filesystem::path &file_path_utf8, uint64_t offset, size_t chunk_size, std::error_code &ec, size_t *bytes_sent = nullptr);
    // 接收文件头 严格校验必需字段
    FileHeader receiveHeader(std::error_code &ec);
    // 接收文件数据到指定路径 内部循环接收直到 file_size 字节
    void receiveFileTo(const std::filesystem::path &save_path_utf8, uint64_t file_size, std::error_code &ec);
    // 发送单个控制字节 无长度前缀 用于客户端 1/0 回复协议
    void sendByte(char value, std::error_code &ec);
    // 接收单个控制字节 最多等待 timeout 超时无数据返回 false 且 ec = errc::timed_out
    // 读到 1 字节返回 true 并回填 value 套接字错误返回 false 并置 ec
    bool receiveByte(char &value, std::error_code &ec, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));
    // 设置套接字收发超时 毫秒 0 = 无限 使文件传输级阻塞 I/O 有界
    void setTimeouts(int send_timeout_ms, int recv_timeout_ms, std::error_code &ec);
    // 打断阻塞中的接收（原子标志 线程安全） 正在等待数据的接收将在下一个轮询周期返回
    // ec = errc::operation_canceled 用于上层主动断开时唤醒接收线程（不直接操作套接字 无竞态）
    void interrupt();

    void close(std::error_code &ec);
    asio::ip::tcp::socket &socket();

private:
    asio::ip::tcp::socket socket_;
    std::atomic<bool> interrupted_{false}; // interrupt() 置位 打断轮询接收

    // 单帧（文件头 JSON）最大长度 防御恶意/损坏的长度前缀导致超大内存分配
    static constexpr uint32_t kMaxFrameSize = 64 * 1024 * 1024;

    void sendMessage(const std::vector<char> &data, std::error_code &ec);
    std::vector<char> receiveMessage(std::error_code &ec);
    // 轮询式完整读取：可被 interrupt() 打断 空闲超时（SO_RCVTIMEO 到期）继续等待而非报错
    bool readInterruptible(void *data, size_t size, size_t &bytes_read, std::error_code &ec);
};
