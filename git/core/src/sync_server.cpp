#if defined(__linux__) && !defined(_GNU_SOURCE) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif

#include "sync_server.h"

#include <algorithm>
#include <cerrno>
#include <ctime>
#include <sstream>

#ifndef _WIN32
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#else
#include <ifdef.h>
#include <iphlpapi.h>
#endif

SyncServer::SyncServer(std::uint16_t port, std::string magic_word, std::chrono::minutes empty_queue_wait)
    : acceptor_(io_), magic_word_(std::move(magic_word)), udp_port_(port), empty_queue_wait_(empty_queue_wait), broadcaster_(nullptr), client_(nullptr)
{
}

SyncServer::~SyncServer()
{
    std::error_code ec;
    stop(ec);
}

bool SyncServer::start(std::string server_name, std::uint16_t port, std::error_code &ec, std::function<void(bool, std::error_code)> cb)
{
    ec.clear();

    if (started_)
    {
        ec = std::make_error_code(std::errc::operation_in_progress);
        setError("[tip] the server has started");
        return false;
    }

    // 打开并绑定 TCP 监听（port == 0 时系统分配，广播广告实际端口）
    acceptor_.open(asio::ip::tcp::v4(), ec);
    if (ec)
    {
        setError("[warning] failed to start listening: " + ec.message());
        return false;
    }
    acceptor_.set_option(asio::socket_base::reuse_address(true));
    acceptor_.bind(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port), ec);
    if (ec)
    {
        setError("[warning] failed to bing the port: " + ec.message());
        std::error_code close_ec;
        acceptor_.close(close_ec);
        return false;
    }
    server_port_ = acceptor_.local_endpoint().port();
    acceptor_.listen(asio::socket_base::max_listen_connections, ec);
    if (ec)
    {
        setError("[warning] failed to start listening:" + ec.message());
        std::error_code close_ec;
        acceptor_.close(close_ec);
        return false;
    }
    acceptor_.non_blocking(true); // 轮询 accept

    // 解析本机接口：广告 IP 与子网定向广播地址（多网卡/VPN 环境可用 setAdvertiseIP 覆盖）
    std::string broadcast_addr = "255.255.255.255";
    {
        std::error_code net_ec;
        std::string resolved_ip;
        if (!getInterfaceInfo(resolved_ip, broadcast_addr, net_ec) || resolved_ip.empty())
        {
            broadcast_addr = "255.255.255.255"; // 回退全局广播
        }
        if (advertise_ip_.empty())
        {
            advertise_ip_ = resolved_ip;
        }
    }

    server_name_ = std::move(server_name);
    broadcaster_ = std::make_unique<BroadcastSender>(io_, udp_port_, broadcast_addr);

    running_ = true;
    broadcasting_ = true;
    started_ = true;

    worker_thread_ = std::thread(&SyncServer::workerLoop, this, std::move(cb));
    return true;
}

void SyncServer::stop(std::error_code &ec)
{
    ec.clear();

    if (!started_)
    {
        return;
    }

    // 不直接操作 client_（与工作线程并发访问有竞态）仅置标志
    // 工作线程在下一个有界阻塞点（accept 轮询/回复超时/发送超时）退出并自行清理
    running_ = false;
    cv_.notify_all();

    // 防止在回调（工作线程）内调用 stop 时自 join 死锁
    if (worker_thread_.joinable() && worker_thread_.get_id() != std::this_thread::get_id())
    {
        worker_thread_.join();
    }

    started_ = false;

    if (acceptor_.is_open())
    {
        acceptor_.close(ec);
        ec.clear();
    }
    broadcaster_.reset();
}

void SyncServer::enqueueDirectory(const std::filesystem::path &dir)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    task_queue_.push(dir);
    cv_.notify_one();
}

std::vector<std::filesystem::path> SyncServer::getTaskQueue() const
{
    std::lock_guard<std::mutex> lock(queue_mutex_);

    std::vector<std::filesystem::path> snapshot;
    snapshot.reserve(task_queue_.size());

    std::queue<std::filesystem::path> copy = task_queue_;
    while (!copy.empty())
    {
        snapshot.push_back(copy.front());
        copy.pop();
    }
    return snapshot;
}

void SyncServer::disconnect(std::error_code &ec)
{
    ec.clear();

    // 置位断开请求并唤醒工作线程：由工作线程在下一个有界阻塞点完成断开与清队
    // 不直接操作 client_（与工作线程并发访问有竞态）
    disconnect_requested_ = true;
    cv_.notify_all();
}

std::string SyncServer::getLastError() const
{
    std::lock_guard<std::mutex> lock(error_mutex_);
    return error_string_;
}

void SyncServer::workerLoop(std::function<void(bool, std::error_code)> cb)
{
    while (running_)
    {
        if (client_connected_)
        {
            // 会话模式：按序发送队列中的目录

            // 上层请求断开：立即结束会话（回调说明被中断）
            if (disconnect_requested_)
            {
                disconnect_requested_ = false;
                if (cb)
                {
                    cb(false, std::make_error_code(std::errc::operation_canceled));
                }
                std::error_code close_ec;
                closeClient(close_ec);
                continue;
            }

            bool have_dir = false;
            std::filesystem::path dir;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                if (!task_queue_.empty())
                {
                    dir = task_queue_.front();
                    task_queue_.pop();
                    have_dir = true;
                }
            }

            if (!have_dir)
            {
                // 队列为空：通知上层“发送队列为空” 上层可趁等待期继续入队（回调在锁外执行）
                if (cb)
                {
                    cb(true, std::make_error_code(std::errc::no_message_available));
                }

                // 等待新目录入队（最长 empty_queue_wait_）期间有新目录则继续发送
                // 超时仍为空或被停止/请求断开则结束会话
                std::unique_lock<std::mutex> lock(queue_mutex_);
                cv_.wait_for(lock, empty_queue_wait_, [this]()
                {
                    return !running_ || !task_queue_.empty() || disconnect_requested_;
                });
                if (running_ && !disconnect_requested_ && !task_queue_.empty())
                {
                    dir = task_queue_.front();
                    task_queue_.pop();
                    have_dir = true;
                }
            }

            if (have_dir)
            {
                std::error_code send_ec;
                if (!sendDirectory(dir, send_ec))
                {
                    // 会话出错结束
                    if (cb)
                    {
                        cb(false, send_ec);
                    }
                    closeClient(send_ec);
                    continue;
                }
                continue;
            }

            // 队列为空且等待超时：会话正常结束 被停止或请求断开则视为中断
            if (cb)
            {
                if (running_ && !disconnect_requested_)
                {
                    cb(true, std::error_code());
                }
                else
                {
                    cb(false, std::make_error_code(std::errc::operation_canceled));
                }
            }
            disconnect_requested_ = false; // 消费断开请求（等待被断开请求唤醒时）
            std::error_code close_ec;
            closeClient(close_ec);
            continue;
        }

        // 无客户端：周期广播 + 轮询 accept
        if (disconnect_requested_)
        {
            disconnect_requested_ = false; // 无客户端时断开请求为空操作 丢弃残留标志
        }
        if (broadcaster_ && broadcasting_)
        {
            std::error_code b_ec;
            broadcastMessage(b_ec);
            if (b_ec)
            {
                setError("[Warning] Broadcast failed : " + b_ec.message());
            }
        }

        std::error_code a_ec;
        if (tryAccept(a_ec))
        {
            client_connected_ = true;
            broadcasting_ = false;
            continue;
        }
        if (a_ec)
        {
            setError("[Warning] Failed to accept connection: " + a_ec.message());
        }

        // 短暂休眠避免忙等 同时使 stop() 可及时返回
        sleepMillis(400);
    }

    // 工作线程退出：若会话被中断则通知回调
    if (cb && client_connected_)
    {
        cb(false, std::make_error_code(std::errc::operation_canceled));
    }
    std::error_code close_ec;
    closeClient(close_ec);
}

bool SyncServer::broadcastMessage(std::error_code &ec)
{
    ec.clear();

    if (!broadcaster_)
    {
        ec = std::make_error_code(std::errc::not_connected);
        return false;
    }

    UDPMessage msg;
    msg.name_ = server_name_;
    msg.ip_ = advertise_ip_.empty() ? getLocalIP() : advertise_ip_;
    msg.port_ = server_port_;
    msg.magic_word_ = magic_word_;

    broadcaster_->send(msg, ec);
    return !ec;
}

bool SyncServer::tryAccept(std::error_code &ec)
{
    ec.clear();

    if (!acceptor_.is_open())
    {
        ec = std::make_error_code(std::errc::not_connected);
        return false;
    }

    asio::ip::tcp::socket sock(io_);
    acceptor_.accept(sock, ec);
    if (!ec)
    {
        // 确保已接受套接字为阻塞模式（非阻塞 acceptor 的 accept 可能继承非阻塞）
        sock.non_blocking(false);

        auto conn = std::make_unique<TcpConnection>(std::move(sock));
        std::error_code to_ec;
        conn->setTimeouts(30000, 30000, to_ec); // 传输级阻塞 I/O 有界
        client_ = std::move(conn);
        return true;
    }
    if (ec == asio::error::would_block)
    {
        ec.clear();
    }
    return false;
}

bool SyncServer::sendDirectory(const std::filesystem::path &dir, std::error_code &ec)
{
    ec.clear();

    std::error_code check_ec;
    if (!std::filesystem::is_directory(dir, check_ec) || check_ec)
    {
        ec = std::make_error_code(std::errc::not_a_directory);
        return false;
    }

    std::vector<std::filesystem::path> files;
    collectFiles(dir, files);

    for (const auto &file_path : files)
    {
        if (!running_ || disconnect_requested_)
        {
            ec = std::make_error_code(std::errc::operation_canceled);
            return false;
        }

        // 计算相对父目录（UTF-8，'/' 分隔）：以入队目录自身名字为首段
        // 避免多个入队目录下同名文件在客户端相互覆盖（根目录文件 = 入队目录名）
        std::string parent_dir = dir.filename().u8string();
        {
            std::error_code rel_ec;
            auto rel = std::filesystem::relative(file_path.parent_path(), dir, rel_ec);
            if (!rel_ec)
            {
                for (const auto &comp : rel)
                {
                    if (comp == std::filesystem::path("."))
                    {
                        continue; // 根目录文件：相对路径为 "."
                    }
                    if (!parent_dir.empty())
                    {
                        parent_dir += '/';
                    }
                    parent_dir += comp.u8string();
                }
            }
        }

        std::error_code size_ec;
        auto file_size = std::filesystem::file_size(file_path, size_ec);
        if (size_ec)
        {
            ec = size_ec;
            return false;
        }

        FileHeader header;
        header.parent_dir_ = parent_dir;
        header.file_name_ = file_path.filename().u8string();
        header.file_size_ = file_size;

        client_->sendHeader(header, ec);
        if (ec)
        {
            return false;
        }

        bool should_send = false;
        if (!waitForClientReply(ec, should_send))
        {
            return false;
        }
        if (!should_send)
        {
            continue;
        }

        // 发送文件数据：严格按 header.file_size_ 字节发送
        uint64_t offset = 0;
        const size_t chunk_size = 64 * 1024;
        while (offset < header.file_size_)
        {
            if (!running_ || disconnect_requested_)
            {
                ec = std::make_error_code(std::errc::operation_canceled);
                return false;
            }
            size_t sent = 0;
            client_->sendFileData(file_path, offset, chunk_size, ec, &sent);
            if (ec)
            {
                return false;
            }
            if (sent == 0)
            {
                // 文件在发送过程中被截断：中止会话（客户端不会死等）
                ec = std::make_error_code(std::errc::io_error);
                return false;
            }
            offset += sent;
        }
    }

    return true;
}

bool SyncServer::waitForClientReply(std::error_code &ec, bool &shouldSend)
{
    ec.clear();

    char value = 0;
    while (running_ && !disconnect_requested_)
    {
        if (client_->receiveByte(value, ec, std::chrono::milliseconds(500)))
        {
            if (value == '1')
            {
                shouldSend = true;
                return true;
            }
            if (value == '0')
            {
                shouldSend = false;
                return true;
            }
            ec = std::make_error_code(std::errc::protocol_error);
            return false;
        }

        if (ec)
        {
            if (ec == std::errc::timed_out)
            {
                continue; // 未收到回复 继续等待（可被 running_ 打断）
            }
            return false;
        }
        return false;
    }

    ec = std::make_error_code(std::errc::operation_canceled);
    return false;
}

void SyncServer::closeClient(std::error_code &ec)
{
    ec.clear();

    if (client_)
    {
        client_->close(ec);
        client_.reset();
    }
    client_connected_ = false;
    broadcasting_ = true; // 会话结束（正常/断开/出错）后恢复广播，便于重新被发现

    // 清空待发送队列
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        std::queue<std::filesystem::path> empty;
        std::swap(task_queue_, empty);
    }
}

void SyncServer::setError(const std::string &msg)
{
    std::lock_guard<std::mutex> lock(error_mutex_);
    error_string_ = msg;
}

void SyncServer::setAdvertiseIP(const std::string &ip)
{
    advertise_ip_ = ip;
}

std::string SyncServer::getLocalIP() const
{
    // 优先按接口枚举（首个可用非回环 IPv4）
    std::string ip;
    std::string broadcast;
    std::error_code ec;
    if (getInterfaceInfo(ip, broadcast, ec) && !ip.empty())
    {
        return ip;
    }

    // 回退：hostname 解析
    try
    {
        asio::io_context io;
        asio::ip::tcp::resolver resolver(io);
        auto endpoints = resolver.resolve(asio::ip::host_name(), "");

        for (auto it = endpoints.begin(); it != endpoints.end(); it++)
        {
            auto addr = it->endpoint().address();
            if (addr.is_v4() && !addr.is_loopback() && !addr.is_multicast())
            {
                return addr.to_string();
            }
        }
        return "127.0.0.1";
    }
    catch (...)
    {
        return "127.0.0.1";
    }
}

void SyncServer::sleepMillis(long long ms)
{
    if (ms <= 0)
    {
        return;
    }
#ifdef _WIN32
    ::Sleep(static_cast<DWORD>(ms));
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    ::nanosleep(&ts, nullptr);
#endif
}

void SyncServer::collectFiles(const std::filesystem::path &root, std::vector<std::filesystem::path> &out)
{
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    std::filesystem::recursive_directory_iterator end;
    for (; it != end; it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        std::error_code fec;
        if (it->is_regular_file(fec) && !fec)
        {
            out.push_back(it->path());
        }
    }
    std::sort(out.begin(), out.end());
}

bool SyncServer::getInterfaceInfo(std::string &ip, std::string &broadcast, std::error_code &ec)
{
    ec.clear();
#ifdef _WIN32
    ULONG buf_len = 15000;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        IP_ADAPTER_ADDRESSES *adapters = static_cast<IP_ADAPTER_ADDRESSES *>(malloc(buf_len));
        if (adapters == nullptr)
        {
            ec = std::make_error_code(std::errc::not_enough_memory);
            return false;
        }
        ULONG rc = GetAdaptersAddresses(
            AF_INET,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_FRIENDLY_NAME | GAA_FLAG_INCLUDE_PREFIX,
            nullptr, adapters, &buf_len);
        if (rc == ERROR_BUFFER_OVERFLOW)
        {
            free(adapters);
            continue;
        }
        if (rc != NO_ERROR)
        {
            free(adapters);
            ec = std::error_code(rc, std::system_category());
            return false;
        }

        for (IP_ADAPTER_ADDRESSES *a = adapters; a != nullptr; a = a->Next)
        {
            if (a->OperStatus != IfOperStatusUp)
            {
                continue;
            }
            if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || a->IfType == IF_TYPE_TUNNEL)
            {
                continue;
            }
            for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u != nullptr; u = u->Next)
            {
                if (u->Address.lpSockaddr->sa_family != AF_INET)
                {
                    continue;
                }
                const sockaddr_in *sin = reinterpret_cast<const sockaddr_in *>(u->Address.lpSockaddr);
                uint32_t addr = ntohl(sin->sin_addr.s_addr);
                if ((addr >> 24) == 127)
                {
                    continue;
                }
                ULONG prefix = u->OnLinkPrefixLength;
                if (prefix > 32)
                {
                    continue; // 前缀无效（如 255）
                }
                uint32_t mask = 0xFFFFFFFFu;
                if (prefix < 32)
                {
                    mask = (prefix == 0) ? 0u : (0xFFFFFFFFu << (32 - prefix));
                }
                ip = asio::ip::address_v4(addr).to_string();
                broadcast = asio::ip::address_v4((addr & mask) | (~mask)).to_string();
                free(adapters);
                return true;
            }
        }
        free(adapters);
        break;
    }
    return false;
#else
    struct ifaddrs *ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0)
    {
        ec = std::error_code(errno, std::generic_category());
        return false;
    }

    bool found = false;
    for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET)
        {
            continue;
        }
        if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_LOOPBACK) != 0)
        {
            continue;
        }
        const sockaddr_in *sin = reinterpret_cast<const sockaddr_in *>(ifa->ifa_addr);
        uint32_t addr = ntohl(sin->sin_addr.s_addr);
        if ((addr >> 24) == 127)
        {
            continue;
        }
        uint32_t mask = 0xFFFFFFFFu;
        if (ifa->ifa_netmask != nullptr)
        {
            mask = ntohl(reinterpret_cast<const sockaddr_in *>(ifa->ifa_netmask)->sin_addr.s_addr);
        }
        ip = asio::ip::address_v4(addr).to_string();
        broadcast = asio::ip::address_v4((addr & mask) | (~mask)).to_string();
        found = true;
        break;
    }
    freeifaddrs(ifaddr);
    return found;
#endif
}