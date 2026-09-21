package com.example.tagmeow;

// 同步模块的错误码

// core 的 sync 三件套（sync_basic / sync_client / sync_server）用 std::error_code 做每层的
// 失败回传 判断一律写成 if (ec) 和 ec == std::errc::xxx Java 没有这种东西 这里把 core 真正
// 用到的那几个 std::errc 取值一一搬过来 保证两端对「同一次失败」的判定完全一致 ——
// 尤其是下载回调里靠错误码区分的几个语义分支：
//   NO_MESSAGE_AVAILABLE 服务器发送队列为空（**正常结束** 只是本次没有文件可下）
//   OPERATION_IN_PROGRESS 已有下载会话在进行（上层重复点击）
//   RESULT_OUT_OF_RANGE   server_index 越界
//   CONNECTION_ABORTED    连接中断（core 部分6 之后：EOF 但没收到会话结束控制帧）

// NONE 等价于 core 里的 ec.clear() 也就是「没有错误」

public enum SyncError {

    // 无错误（core: ec.clear()）
    NONE,

    // core: errc::operation_canceled —— 被上层 stop()/disconnect() 打断
    OPERATION_CANCELED,

    // core: errc::operation_in_progress —— 上一次下载还没结束
    OPERATION_IN_PROGRESS,

    // core: errc::result_out_of_range —— server_index 越界
    RESULT_OUT_OF_RANGE,

    // core: errc::no_message_available —— 服务器发送队列为空 本次没有文件可下载
    NO_MESSAGE_AVAILABLE,

    // core: errc::protocol_error —— 回复字节既不是 '1' 也不是 '0'
    PROTOCOL_ERROR,

    // core: errc::timed_out
    TIMED_OUT,

    // core: errc::not_connected
    NOT_CONNECTED,

    // core: errc::invalid_argument —— 报文缺字段 / JSON 解析失败
    INVALID_ARGUMENT,

    // core: errc::message_size —— 超过 UDP 载荷上限或单帧长度上限
    MESSAGE_SIZE,

    // core: errc::not_a_directory
    NOT_A_DIRECTORY,

    // core: errc::io_error
    IO_ERROR,

    // core: errc::permission_denied
    PERMISSION_DENIED,

    // core: errc::no_such_file_or_directory
    NO_SUCH_FILE_OR_DIRECTORY,

    // core: errc::not_enough_memory
    NOT_ENOUGH_MEMORY,

    // core: errc::connection_aborted —— 连接中断
    // core 部分6 起：EOF 不再等于「服务器正常收尾」 没收到会话结束控制帧的断开一律算中断
    CONNECTION_ABORTED,

    // 对端正常断开（TCP 层面的 EOF 信号 不再等同于会话正常结束）
    EOF;

    // 与 core 里的 if (ec) 等价
    public boolean isError() {
        return this != NONE;
    }
}
