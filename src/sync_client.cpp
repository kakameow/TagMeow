#include "sync_client.h"

#include <cerrno>
#include <fstream>
#include <sstream>
#include <unordered_set>

#ifndef _WIN32
#include <poll.h>
#endif

SyncClient::SyncClient(std::uint16_t port, std::string magic_word, const std::filesystem::path &download_path)
    : port_(port), magic_word_(std::move(magic_word)), download_path_(download_path), records_path_(download_path / "records.json")
{
    receiver_ = std::make_unique<BroadcastReceiver>(io_, port_, magic_word_);

    std::error_code ec;
    if (!loadRecords(ec))
    {
        setError("[tip] failed to load the download cache" + ec.message());
    }

    running_ = true;
    worker_thread_ = std::thread(&SyncClient::workerLoop, this);
}

SyncClient::~SyncClient()
{
    running_ = false;
    if (conn_)
    {
        conn_->interrupt(); // 打断阻塞中的接收（原子标志 无竞态） 保证 join 有界返回
    }
    cv_.notify_all();
    if (worker_thread_.joinable())
    {
        worker_thread_.join();
    }

    std::error_code ec;
    if (conn_)
    {
        conn_->close(ec);
        conn_.reset();
    }
}

std::vector<ServerInfo> SyncClient::scanServers(size_t num_attempts)
{
    std::vector<ServerInfo> result;
    std::unordered_set<std::string> seen;
    std::error_code ec;

    for (size_t i = 0; i < num_attempts; ++i)
    {
        auto servers = receiver_->scan(ec);
        if (ec)
        {
            setError("[warning] download failed: " + ec.message());
            break;
        }
        for (auto &s : servers)
        {
            if (seen.insert(serverKey(s)).second)
            {
                result.push_back(std::move(s));
            }
        }
    }

    servers_ = result;
    return result;
}

void SyncClient::startDownload(std::size_t server_index, std::function<void(bool, std::error_code)> cb)
{
    if (!running_)
    {
        if (cb)
        {
            cb(false, std::make_error_code(std::errc::operation_canceled));
        }
        return;
    }

    bool expected = false;
    if (!is_busy_.compare_exchange_strong(expected, true))
    {
        if (cb)
        {
            cb(false, std::make_error_code(std::errc::operation_in_progress));
        }
        return;
    }

    if (server_index >= servers_.size())
    {
        is_busy_ = false;
        if (cb)
        {
            cb(false, std::make_error_code(std::errc::result_out_of_range));
        }
        return;
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push([this, server_index, cb]() { doDownload(server_index, cb); });
    }
    cv_.notify_one();
}

void SyncClient::disconnect()
{
    // 会话级手动断开(与服务端 disconnect_requested_ 模式一致):
    // 置位请求 + interrupt() 打断阻塞中的接收 由下载会话在下一个有界阻塞点退出并回调失败
    disconnect_requested_ = true;
    if (conn_)
    {
        conn_->interrupt();
    }
    cv_.notify_all();
}

void SyncClient::clearDownloadRecords()
{
    {
        std::lock_guard<std::mutex> lock(records_mutex_);
        download_records_.clear();
    }

    std::error_code ec;
    std::filesystem::remove(records_path_, ec);
    if (ec)
    {
        setError("[warning] failed to clear the download cache: " + ec.message());
    }
}

bool SyncClient::isDownloading() const
{
    return is_busy_.load();
}

const std::filesystem::path &SyncClient::getDownloadPath() const
{
    return download_path_;
}

const std::vector<ServerInfo> &SyncClient::getServers() const
{
    return servers_;
}

std::string SyncClient::getLastError() const
{
    std::lock_guard<std::mutex> lock(error_mutex_);
    return error_string_;
}

void SyncClient::workerLoop()
{
    while (running_)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_.wait(lock, [this](){ return !running_ || !task_queue_.empty(); });
            if (!running_)
            {
                break;
            }
            task = std::move(task_queue_.front());
            task_queue_.pop();
        }

        try
        {
            task();
        }
        catch (const std::exception &e)
        {
            setError(std::string("[warning] task exception: ") + e.what());
            is_busy_ = false;
        }
        catch (...)
        {
            setError("[warning] task exception");
            is_busy_ = false;
        }
    }
}

void SyncClient::doDownload(std::size_t index, std::function<void(bool, std::error_code)> cb)
{
    std::error_code ec;
    bool success = false;
    disconnect_requested_ = false; // 新下载会话: 消费可能残留的断开请求(断开后再次下载可正常进行)

    try
    {
        if (index >= servers_.size())
        {
            ec = std::make_error_code(std::errc::result_out_of_range);
        }
        else
        {
            const ServerInfo &server = servers_[index];
            if (syncConnect(server, ec))
            {
                // 成功时 ec 可能为清空（完成 ≥1 文件）或 no_message_available（服务器队列为空）
                success = syncReceiveAll(ec);
            }
        }
    }
    catch (const std::exception &e)
    {
        ec = std::make_error_code(std::errc::io_error);
        setError(std::string("[warning] download exception: ") + e.what());
    }

    // 会话结束：关闭连接
    if (conn_)
    {
        std::error_code close_ec;
        conn_->close(close_ec);
        conn_.reset();
    }

    is_busy_ = false;
    if (cb)
    {
        try
        {
            cb(success, ec);
        }
        catch (...)
        {
        }
    }
}

bool SyncClient::syncConnect(const ServerInfo &server, std::error_code &ec)
{
    ec.clear();
    conn_.reset();

    asio::ip::tcp::endpoint ep;
    asio::error_code addr_ec;
    auto addr = asio::ip::make_address(server.ip_, addr_ec);
    if (addr_ec)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    ep = asio::ip::tcp::endpoint(addr, server.port_);

    asio::ip::tcp::socket sock(io_);
    if (!connectWithTimeout(sock, ep, std::chrono::milliseconds(5000), ec))
    {
        return false;
    }

    auto conn = std::make_unique<TcpConnection>(std::move(sock));
    std::error_code to_ec;
    // 发送超时 30s 有界；接收超时 500ms 作为轮询粒度：
    // 空闲（服务器空队列等待）由 readInterruptible 继续等待 不报错；interrupt() 可在 ≤500ms 内打断
    conn->setTimeouts(30000, 500, to_ec);
    conn_ = std::move(conn);
    return true;
}

bool SyncClient::syncReceiveAll(std::error_code &ec)
{
    ec.clear();
    size_t received_count = 0;

    while (running_ && !disconnect_requested_)
    {
        FileHeader header = conn_->receiveHeader(ec);
        if (ec)
        {
            if (ec == asio::error::eof)
            {
                ec.clear();
                if (received_count > 0)
                {
                    // 服务器发送完毕正常断开
                    return true;
                }
                // 无文件可下载（服务器发送队列为空）：正常完成 以 ec 提示上层
                ec = std::make_error_code(std::errc::no_message_available);
                return true;
            }
            return false;
        }

        if (header.file_name_.empty())
        {
            ec = std::make_error_code(std::errc::protocol_error);
            return false;
        }

        if (isFileAlreadyExists(header))
        {
             // '0' 跳过
            if (!sendReply(false, ec))
            {
                return false;
            }
            continue;
        }

        // '1' 发送文件数据
        if (!sendReply(true, ec)) 
        {
            return false;
        }

        std::filesystem::path save_path = buildSavePath(download_path_, header.parent_dir_, header.file_name_);
        conn_->receiveFileTo(save_path, header.file_size_, ec);
        if (ec)
        {
            return false;
        }

        DownloadRecord rec;
        rec.parent_dir_ = header.parent_dir_;
        rec.file_name_ = header.file_name_;
        rec.file_size_ = header.file_size_;
        {
            std::lock_guard<std::mutex> lock(records_mutex_);
            download_records_.push_back(rec);
        }

        std::error_code save_ec;
        saveRecords(save_ec);
        if (save_ec)
        {
            setError("[warning] download record save failed: " + save_ec.message());
        }
        ++received_count;
    }

    ec = std::make_error_code(std::errc::operation_canceled);
    return false;
}

bool SyncClient::isFileAlreadyExists(const FileHeader &header) const
{
    std::lock_guard<std::mutex> lock(records_mutex_);
    for (const auto &rec : download_records_)
    {
        if (rec.parent_dir_ == header.parent_dir_ && rec.file_name_ == header.file_name_ && rec.file_size_ == header.file_size_)
        {
            return true;
        }
    }
    return false;
}

bool SyncClient::sendReply(bool should_send, std::error_code &ec)
{
    ec.clear();
    if (!conn_)
    {
        ec = std::make_error_code(std::errc::not_connected);
        return false;
    }
    conn_->sendByte(should_send ? '1' : '0', ec);
    return !ec;
}

bool SyncClient::loadRecords(std::error_code &ec)
{
    ec.clear();

    std::lock_guard<std::mutex> lock(records_mutex_);
    download_records_.clear();

    std::ifstream in(records_path_, std::ios::binary);
    if (!in)
    {
        // 尚无记录文件：视为空记录
        return true;
    }

    try
    {
        nlohmann::json j = nlohmann::json::parse(in);
        if (!j.is_array())
        {
            ec = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
        for (const auto &item : j)
        {
            DownloadRecord rec;
            rec.parent_dir_ = item.value("parent_dir", "");
            rec.file_name_ = item.value("file_name", "");
            rec.file_size_ = item.value("file_size", 0ULL);
            download_records_.push_back(std::move(rec));
        }
        return true;
    }
    catch (...)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
}

bool SyncClient::saveRecords(std::error_code &ec)
{
    ec.clear();
    try
    {
        std::lock_guard<std::mutex> lock(records_mutex_);

        std::error_code mkdir_ec;
        std::filesystem::create_directories(download_path_, mkdir_ec);
        if (mkdir_ec)
        {
            ec = mkdir_ec;
            return false;
        }

        nlohmann::json j = nlohmann::json::array();
        for (const auto &rec : download_records_)
        {
            nlohmann::json item;
            item["parent_dir"] = rec.parent_dir_;
            item["file_name"] = rec.file_name_;
            item["file_size"] = rec.file_size_;
            j.push_back(std::move(item));
        }

        std::ofstream out(records_path_, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            ec = std::make_error_code(std::errc::permission_denied);
            return false;
        }
        out << j.dump(2);
        out.flush();
        if (!out)
        {
            ec = std::make_error_code(std::errc::io_error);
            return false;
        }
        return true;
    }
    catch (...)
    {
        ec = std::make_error_code(std::errc::io_error);
        return false;
    }
}

void SyncClient::setError(const std::string &msg)
{
    std::lock_guard<std::mutex> lock(error_mutex_);
    error_string_ = msg;
}

std::string SyncClient::serverKey(const ServerInfo &s)
{
    return s.ip_ + ":" + std::to_string(s.port_);
}

bool SyncClient::connectWithTimeout(asio::ip::tcp::socket &sock, const asio::ip::tcp::endpoint &ep, std::chrono::milliseconds timeout, std::error_code &ec)
{
    ec.clear();

    std::error_code open_ec;
    sock.open(asio::ip::tcp::v4(), open_ec);
    if (open_ec)
    {
        ec = open_ec;
        return false;
    }

    sock.non_blocking(true);
    sock.connect(ep, ec);
    if (!ec)
    {
        sock.non_blocking(false);
        return true;
    }
    if (ec != asio::error::would_block && ec != asio::error::in_progress)
    {
        return false;
    }
    ec.clear();

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

#ifdef _WIN32
    fd_set write_set;
    FD_ZERO(&write_set);
    FD_SET(sock.native_handle(), &write_set);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int rc = ::select(0, nullptr, &write_set, nullptr, &tv);
#else
    struct pollfd pfd;
    pfd.fd = sock.native_handle();
    pfd.events = POLLOUT;
    pfd.revents = 0;
    int rc = ::poll(&pfd, 1, timeout_ms);
#endif
    if (rc <= 0)
    {
        if (rc == 0)
        {
            ec = std::make_error_code(std::errc::timed_out);
        }
        else
        {
#ifdef _WIN32
            // Windows 上 select 失败不设置 errno，需用 WSAGetLastError
            ec = std::error_code(WSAGetLastError(), std::system_category());
#else
            ec = std::error_code(errno, std::generic_category());
#endif
        }
        return false;
    }

    // 连接结果：select 可写不代表成功，需读 SO_ERROR
    int so_error = 0;
#ifdef _WIN32
    int opt_len = sizeof(so_error);
    getsockopt(sock.native_handle(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&so_error), &opt_len);
#else
    socklen_t opt_len = sizeof(so_error);
    getsockopt(sock.native_handle(), SOL_SOCKET, SO_ERROR, &so_error, &opt_len);
#endif
    if (so_error != 0)
    {
#ifdef _WIN32
        // Windows 的 SO_ERROR 返回 WSA 错误码（100xx），需用系统错误类别
        ec = std::error_code(so_error, std::system_category());
#else
        ec = std::error_code(so_error, std::generic_category());
#endif
        return false;
    }

    sock.non_blocking(false);
    return true;
}

std::filesystem::path SyncClient::buildSavePath(const std::filesystem::path &download_path, const std::string &parent_dir, const std::string &file_name)
{
    std::string normalized = parent_dir;
    for (auto &ch : normalized)
    {
        if (ch == '\\')
        {
            ch = '/';
        }
    }

    std::filesystem::path rel;
    std::stringstream ss(normalized);
    std::string comp;
    while (std::getline(ss, comp, '/'))
    {
        if (comp.empty() || comp == "." || comp == "..")
        {
            continue;
        }
        if (comp.size() == 2 && comp[1] == ':')
        {
            continue; // 盘符
        }
        rel /= std::filesystem::u8path(comp);
    }

    // 文件名不得包含路径分隔符
    std::string safe_name = file_name;
    for (auto &ch : safe_name)
    {
        if (ch == '/' || ch == '\\')
        {
            ch = '_';
        }
    }
    rel /= std::filesystem::u8path(safe_name);

    return download_path / rel;
}
