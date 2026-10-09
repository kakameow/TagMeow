#pragma once

#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// std::error_code::message() 在 Windows 上返回的是**系统 ANSI 代码页**（简体中文系统 = GBK）的文本
// 而本工程内部一律按 UTF-8 处理字符串 —— 直接把它拼进 error_string_ 会让上层显示成乱码
// 所以：凡是把 std::error_code 的说明文字拼进 error_string_ 的地方 一律用这个函数过一遍

inline std::string systemErrorText(const std::error_code &ec)
{
#ifdef _WIN32
    const std::string raw = ec.message();
    if (raw.empty())
    {
        return raw;
    }

    // 先按 ANSI 代码页解成宽字符 再转 UTF-8 两步任一失败就原样返回
    const int wide_len = MultiByteToWideChar(CP_ACP, 0, raw.c_str(), static_cast<int>(raw.size()), nullptr, 0);
    if (wide_len <= 0)
    {
        return raw;
    }

    std::wstring wide(static_cast<std::size_t>(wide_len), L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, raw.c_str(), static_cast<int>(raw.size()), wide.data(), wide_len) <= 0)
    {
        return raw;
    }

    const int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_len, nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 0)
    {
        return raw;
    }

    std::string utf8(static_cast<std::size_t>(utf8_len), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_len, utf8.data(), utf8_len, nullptr, nullptr) <= 0)
    {
        return raw;
    }

    return utf8;
#else
    // 其它平台的消息本来就是 UTF-8 / ASCII
    return ec.message();
#endif
}
