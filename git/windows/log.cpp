#include "log.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

Log::Log(const std::filesystem::path log_path)
{
    if (!loadLog(log_path))
    {
        //
        return;
    }

    std::error_code ec;
    auto size = std::filesystem::file_size(log_path_, ec);
    if (!ec && size >= 1048576)
    {
        clearLog();
    }
}

Log::~Log()
{
    l_ofs_.close();
}

bool Log::loadLog(const std::filesystem::path& log_path)
{
    std::error_code ec;

    auto parent = log_path.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, ec))
    {
        std::filesystem::create_directories(parent, ec);
        if (ec)
        {
            //
            return false;
        }
    }

    l_ofs_.open(log_path, std::ios::out | std::ios::app);
    if (!l_ofs_.is_open())
    {
        //
        return false;
    }

    log_path_ = log_path;
    return true;
}

std::string Log::timeStamp()
{
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << "]";
    return oss.str();
}


void Log::write(const std::string &class_name, const std::string &message)
{
    if (message.empty())
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mtx_);
    if (!l_ofs_.is_open())
    {
        return;
    }

    l_ofs_ << timeStamp() << " " << class_name << ": " << message << "\n";
    l_ofs_.flush();
}

// 清空日志：关闭后以截断方式重开
void Log::clearLog()
{
    std::lock_guard<std::mutex> lock(mtx_);

    if (l_ofs_.is_open())
    {
        l_ofs_.close();
    }

    if (!log_path_.empty())
    {
        l_ofs_.open(log_path_, std::ios::out | std::ios::trunc);
    }
}