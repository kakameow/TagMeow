#include "sync_basic.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <unordered_set>


// 最近一次套接字调用的错误码（Windows: WSAGetLastError POSIX: errno）
std::error_code lastSocketError()
{
#ifdef _WIN32
    return std::error_code(WSAGetLastError(), std::system_category());
#else
    return std::error_code(errno, std::generic_category());
#endif
}

// SO_RCVTIMEO 到期类错误（超时无数据）判定：Windows 为 WSAETIMEDOUT，POSIX 为 EAGAIN/EWOULDBLOCK
bool isQuietTimeout(const std::error_code &ec)
{
    if (!ec)
    {
        return false;
    }
    if (ec == asio::error::would_block || ec == asio::error::try_again || ec == asio::error::timed_out)
    {
        return true;
    }
#ifdef _WIN32
    if (ec.value() == WSAETIMEDOUT)
    {
        return true;
    }
#else
    if (ec.value() == EAGAIN || ec.value() == EWOULDBLOCK || ec.value() == ETIMEDOUT)
    {
        return true;
    }
#endif
    return false;
}

BroadcastSender::BroadcastSender(asio::io_context &io, uint16_t port, const std::string &broadcast_addr)
    : socket_(io, asio::ip::udp::endpoint(asio::ip::udp::v4(), 0))
{
    socket_.set_option(asio::socket_base::broadcast(true));

    // 默认全局广播 255.255.255.255 多网卡环境下可指定子网定向广播地址
    asio::ip::address_v4 target = asio::ip::address_v4::broadcast();
    std::error_code ec;
    asio::ip::address addr = asio::ip::make_address(broadcast_addr, ec);
    if (!ec && addr.is_v4())
    {
        target = addr.to_v4();
    }
    endpoint_ = asio::ip::udp::endpoint(target, port);
}

BroadcastSender::~BroadcastSender()
{
}

void BroadcastSender::send(const UDPMessage &msg, std::error_code &ec)
{
    ec.clear();

    nlohmann::json j;
    j["name"] = msg.name_;
    j["ip"] = msg.ip_;
    j["port"] = msg.port_;
    j["magic_word"] = msg.magic_word_;
    std::string data = j.dump();

    // UDP 单包载荷上限 避免 send_to 返回 EMSGSIZE 或触发分片
    if (data.size() > kMaxDatagramSize)
    {
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    socket_.send_to(asio::buffer(data), endpoint_, 0, ec);
}

BroadcastReceiver::BroadcastReceiver(asio::io_context &io, uint16_t port, std::string magic_word, std::chrono::milliseconds quiet_timeout)
        : socket_(io), magic_word_(std::move(magic_word)), quiet_timeout_(quiet_timeout)
{
    socket_.open(asio::ip::udp::v4());
    socket_.set_option(asio::socket_base::reuse_address(true));

#ifndef _WIN32
#ifdef SO_REUSEPORT
    // Linux 上 SO_REUSEADDR 不允许同端口重复绑定 需 SO_REUSEPORT 才能同机多实例
    // （Windows 的 SO_REUSEADDR 语义已允许重复绑定 行为对齐）
    int one = 1;
    setsockopt(socket_.native_handle(), SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif
#endif

    socket_.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), port));

    long long ms = quiet_timeout_.count();
    if (ms <= 0)
    {
        ms = 1;
    }

    if (ms > 0x7FFFFFFFLL)
    {
        ms = 0x7FFFFFFFLL;
    }
    int timeout_ms = static_cast<int>(ms);

#ifdef _WIN32
    setsockopt(socket_.native_handle(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout_ms), sizeof(timeout_ms));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(socket_.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

BroadcastReceiver::~BroadcastReceiver()
{
}

std::vector<ServerInfo> BroadcastReceiver::scan(std::error_code &ec, std::size_t max_servers, std::chrono::milliseconds total_timeout)
{
    ec.clear();

    std::vector<ServerInfo> servers;
    std::unordered_set<std::string> seen;
    char buffer[65536];

    const auto dead_line = std::chrono::steady_clock::now() + total_timeout;

    while (servers.size() < max_servers)
    {
        if (std::chrono::steady_clock::now() >= dead_line)
        {
            break;
        }

        asio::ip::udp::endpoint sender_endpoint;
        std::error_code recv_ec;
        size_t length = socket_.receive_from(asio::buffer(buffer), sender_endpoint, 0, recv_ec);

        if (recv_ec)
        {
            // 安静期（超时无包）→ 扫描正常结束
            if (isReceiveQuietEnd(recv_ec))
            {
                break;
            }
            // 单个报文超长被截断：跳过继续收，不中断整个扫描
            if (recv_ec == asio::error::message_size)
            {
                continue;
            }
            ec = recv_ec;
            break;
        }

        if (length == 0)
        {
            continue;
        }

        std::string data(buffer, length);
        UDPMessage msg;
        if (!parseUDPMessage(data, msg))
        {
            continue;
        }
        if (msg.magic_word_ != magic_word_)
        {
            continue;
        }

        ServerInfo info;
        info.name_ = msg.name_;
        info.ip_ = msg.ip_;
        info.port_ = msg.port_;

        if (seen.insert(serverKey(info)).second)
        {
            servers.push_back(std::move(info));
        }
    }

    return servers;
}

bool BroadcastReceiver::parseUDPMessage(const std::string &data, UDPMessage &out_msg)
{
    try
    {
        nlohmann::json j = nlohmann::json::parse(data);
        if (!j.contains("name") || !j.contains("ip") || !j.contains("port") || !j.contains("magic_word"))
        {
            return false;
        }

        out_msg.name_ = j["name"].get<std::string>();
        out_msg.ip_ = j["ip"].get<std::string>();
        out_msg.port_ = j["port"].get<uint16_t>();
        out_msg.magic_word_ = j["magic_word"].get<std::string>();

        return true;
    }
    catch (...)
    {
        return false;
    }
}

// Windows 上 SO_RCVTIMEO 到期返回 WSAETIMEDOUT(10060)；
// Linux 上 recvfrom 超时返回 EAGAIN/EWOULDBLOCK（部分平台返回 ETIMEDOUT）。
bool BroadcastReceiver::isReceiveQuietEnd(const std::error_code &ec)
{
    if (!ec)
    {
        return false;
    }
    if (ec == asio::error::would_block || ec == asio::error::try_again || ec == asio::error::timed_out)
    {
        return true;
    }
#ifdef _WIN32
    if (ec.value() == WSAETIMEDOUT)
    {
        return true;
    }
#else
    if (ec.value() == EAGAIN || ec.value() == EWOULDBLOCK || ec.value() == ETIMEDOUT)
    {
        return true;
    }
#endif
    return false;
}

std::string BroadcastReceiver::serverKey(const ServerInfo &s)
{
    return s.ip_ + ":" + std::to_string(s.port_);
}

TcpConnection::TcpConnection(asio::ip::tcp::socket socket) : socket_(std::move(socket))
{
}

TcpConnection::~TcpConnection()
{
}

void TcpConnection::sendHeader(const FileHeader &header, std::error_code &ec)
{
    nlohmann::json j;
    j["parent_dir"] = header.parent_dir_;
    j["file_name"] = header.file_name_;
    j["file_size"] = header.file_size_;
    std::string json_str = j.dump();

    std::vector<char> data(json_str.begin(), json_str.end());
    sendMessage(data, ec);
}

void TcpConnection::sendFileData(const std::filesystem::path &file_path_utf8, uint64_t offset, size_t chunk_size, std::error_code &ec, size_t *bytes_sent)
{
    if (bytes_sent)
    {
        *bytes_sent = 0;
    }

    std::ifstream file(file_path_utf8, std::ios::binary);
    if (!file)
    {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return;
    }

    file.seekg(offset, std::ios::beg);
    if (!file)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return;
    }

    std::vector<char> buffer(chunk_size);
    file.read(buffer.data(), chunk_size);
    std::streamsize bytes_read = file.gcount();

    if (bytes_read <= 0)
    {
        // 0 字节：已到文件末尾（正常）或读失败
        if (file.bad() || (file.fail() && !file.eof()))
        {
            ec = std::make_error_code(std::errc::io_error);
        }
        // 正常 EOF：不发送任何字节 bytes_sent 保持 0 调用方据此停止
        else
        {
            ec.clear();
        }
        return;
    }

    // 发送实际读取的字节数 文件剩余不足 chunk_size 时只发剩余部分
    asio::write(socket_, asio::buffer(buffer.data(), bytes_read), ec);
    if (!ec && bytes_sent)
    {
        *bytes_sent = static_cast<size_t>(bytes_read);
    }
}

FileHeader TcpConnection::receiveHeader(std::error_code &ec)
{
    std::vector<char> data = receiveMessage(ec);
    if (ec)
    {
        return {};
    }

    std::string json_str(data.begin(), data.end());
    try
    {
        nlohmann::json j = nlohmann::json::parse(json_str);
        // 与 parseUDPMessage 一致：严格校验必需字段 缺失即协议错误
        if (!j.contains("parent_dir") || !j.contains("file_name") || !j.contains("file_size"))
        {
            ec = std::make_error_code(std::errc::invalid_argument);
            return {};
        }

        FileHeader header;
        header.parent_dir_ = j["parent_dir"].get<std::string>();
        header.file_name_ = j["file_name"].get<std::string>();
        header.file_size_ = j["file_size"].get<uint64_t>();
        return header;
    }
    catch (const std::exception &)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return {};
    }
}

void TcpConnection::receiveFileTo(const std::filesystem::path &save_path, uint64_t file_size, std::error_code &ec)
{
    // 创建保存目录（无父目录时跳过 如相对当前目录的文件名）
    if (!save_path.parent_path().empty())
    {
        std::error_code mkdir_ec;
        std::filesystem::create_directories(save_path.parent_path(), mkdir_ec);
        if (mkdir_ec)
        {
            ec = mkdir_ec;
            return;
        }
    }

    std::ofstream file(save_path, std::ios::binary);
    if (!file)
    {
        ec = std::make_error_code(std::errc::permission_denied);
        return;
    }

    const size_t buffer_size = 64 * 1024;
    std::vector<char> buffer(buffer_size);
    uint64_t received = 0;

    while (received < file_size)
    {
        size_t to_read = static_cast<size_t>(std::min<uint64_t>(buffer_size, file_size - received));
        size_t bytes = 0;
        if (!readInterruptible(buffer.data(), to_read, bytes, ec))
        {
            if (bytes > 0)
            {
                file.write(buffer.data(), bytes);
            }
            return;
        }

        file.write(buffer.data(), bytes);
        if (!file)
        {
            ec = std::make_error_code(std::errc::io_error);
            return;
        }
        received += bytes;
    }
}

void TcpConnection::sendByte(char value, std::error_code &ec)
{
    ec.clear();
    asio::write(socket_, asio::buffer(&value, 1), ec);
}

bool TcpConnection::receiveByte(char &value, std::error_code &ec, std::chrono::milliseconds timeout)
{
    ec.clear();

    if (!socket_.is_open())
    {
        ec = std::make_error_code(std::errc::not_connected);
        return false;
    }

    long long ms = timeout.count();
    if (ms <= 0)
    {
        ms = 1;
    }
    if (ms > 0x7FFFFFFFLL)
    {
        ms = 0x7FFFFFFFLL;
    }
    int timeout_ms = static_cast<int>(ms);

    // 临时设置 SO_RCVTIMEO 读取后恢复 避免影响后续文件数据传输
    // 设置失败则直接返回错误 避免无界阻塞
#ifdef _WIN32
    int old_ms = 0;
    int old_len = sizeof(old_ms);
    getsockopt(socket_.native_handle(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&old_ms), &old_len);
    if (setsockopt(socket_.native_handle(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout_ms), sizeof(timeout_ms)) != 0)
    {
        ec = lastSocketError();
        return false;
    }
#else
    struct timeval old_tv;
    socklen_t old_len = sizeof(old_tv);
    getsockopt(socket_.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &old_tv, &old_len);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(socket_.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0)
    {
        ec = lastSocketError();
        return false;
    }
#endif

    size_t n = asio::read(socket_, asio::buffer(&value, 1), ec);

#ifdef _WIN32
    setsockopt(socket_.native_handle(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&old_ms), sizeof(old_ms));
#else
    setsockopt(socket_.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &old_tv, sizeof(old_tv));
#endif

    if (ec)
    {
        // 超时无数据统一为 errc::timed_out 语义
        if (ec == asio::error::would_block || ec == asio::error::try_again || ec == asio::error::timed_out)
        {
            ec = std::make_error_code(std::errc::timed_out);
        }
        return false;
    }

    return n == 1;
}

void TcpConnection::interrupt()
{
    interrupted_ = true;
}

bool TcpConnection::readInterruptible(void *data, size_t size, size_t &bytes_read, std::error_code &ec)
{
    ec.clear();
    bytes_read = 0;

    char *buf = static_cast<char *>(data);
    while (bytes_read < size)
    {
        if (interrupted_.load())
        {
            ec = std::make_error_code(std::errc::operation_canceled);
            return false;
        }

        size_t n = asio::read(socket_, asio::buffer(buf + bytes_read, size - bytes_read), ec);
        bytes_read += n;
        if (ec)
        {
            if (isQuietTimeout(ec))
            {
                continue; // 空闲（超时无数据）：继续等待 不视为错误
            }
            return false; // EOF / 连接错误
        }
    }
    return true;
}

void TcpConnection::setTimeouts(int send_timeout_ms, int recv_timeout_ms, std::error_code &ec)
{
    ec.clear();

    if (!socket_.is_open())
    {
        ec = std::make_error_code(std::errc::not_connected);
        return;
    }

#ifdef _WIN32
    if (setsockopt(socket_.native_handle(), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&send_timeout_ms), sizeof(send_timeout_ms)) != 0)
    {
        ec = lastSocketError();
        return;
    }
    if (setsockopt(socket_.native_handle(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&recv_timeout_ms), sizeof(recv_timeout_ms)) != 0)
    {
        ec = lastSocketError();
        return;
    }
#else
    struct timeval send_tv;
    send_tv.tv_sec = send_timeout_ms / 1000;
    send_tv.tv_usec = (send_timeout_ms % 1000) * 1000;
    if (setsockopt(socket_.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &send_tv, sizeof(send_tv)) != 0)
    {
        ec = lastSocketError();
        return;
    }

    struct timeval recv_tv;
    recv_tv.tv_sec = recv_timeout_ms / 1000;
    recv_tv.tv_usec = (recv_timeout_ms % 1000) * 1000;
    if (setsockopt(socket_.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &recv_tv, sizeof(recv_tv)) != 0)
    {
        ec = lastSocketError();
        return;
    }
#endif
}

void TcpConnection::close(std::error_code &ec)
{
    ec.clear();
    if (socket_.is_open())
    {
        socket_.close(ec);
    }
}

asio::ip::tcp::socket &TcpConnection::socket()
{
    return socket_;
}

void TcpConnection::sendMessage(const std::vector<char> &data, std::error_code &ec)
{
    ec.clear();

    if (data.size() > kMaxFrameSize)
    {
        ec = std::make_error_code(std::errc::message_size);
        return;
    }

    // 构造长度前缀（大端）
    uint32_t len = static_cast<uint32_t>(data.size());
    uint32_t len_net = htonl(len);
    std::vector<char> header(4);
    std::memcpy(header.data(), &len_net, 4);

    // 发送头部 + 数据 使用 gather-write
    std::vector<asio::const_buffer> buffers;
    buffers.push_back(asio::buffer(header));
    buffers.push_back(asio::buffer(data));
    asio::write(socket_, buffers, ec);
}

std::vector<char> TcpConnection::receiveMessage(std::error_code &ec)
{
    ec.clear();

    // 读取长度前缀（轮询式 可被 interrupt() 打断 空闲等待不报错）
    uint32_t len_net = 0;
    size_t n = 0;
    if (!readInterruptible(&len_net, sizeof(len_net), n, ec))
    {
        return {};
    }

    uint32_t len = ntohl(len_net);
    if (len > kMaxFrameSize)
    {
        // 防御恶意/损坏的长度前缀导致超大内存分配
        ec = std::make_error_code(std::errc::message_size);
        return {};
    }

    std::vector<char> data(len);

    if (len > 0)
    {
        size_t n2 = 0;
        if (!readInterruptible(data.data(), len, n2, ec))
        {
            return {};
        }
    }

    return data;
}
