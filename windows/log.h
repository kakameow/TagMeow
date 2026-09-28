#ifndef LOG_H
#define LOG_H

#pragma once

#include <fstream>
#include <filesystem>
#include <string>
#include <mutex>

class Log {
public:
    explicit Log(const std::filesystem::path log_path = "./config/log.txt");
    ~Log();

    // 禁止拷贝
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    bool loadLog(const std::filesystem::path& log_path);

    // 文件尾追加一条记录
    void write(const std::string &class_name, const std::string &message);

    // 清空日志
    void clearLog();

private:
    // 一条记录的时间前缀 "[YYYY-MM-DD HH:MM:SS]"
    static std::string timeStamp();

    std::filesystem::path log_path_;
    std::ofstream l_ofs_;
    std::mutex mtx_;
};

#endif // LOG_H
