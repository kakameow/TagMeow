package com.example.tagmeow;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InterfaceAddress;
import java.net.NetworkInterface;
import java.net.SocketException;
import java.util.Collections;
import java.util.Enumeration;
import java.util.List;

// 同步基础层的常量与线协议数据结构

// 设计约束（同 core）：
// 1. 全部同步阻塞实现 没有异步回调
// 2. 广播只单次发送 重复广播由调用方循环控制 不重试无确认
// 3. 单条 TCP 连接 4 字节大端长度前缀成帧 不自动重连

public final class SyncBasic {

    // UDP 广播端口（core: #define UDP_DEFAULT_PORT 11451）
    public static final int UDP_DEFAULT_PORT = 11451;

    // 广播报文魔术字（core: #define UDP_DEFAULT_MAGIC "0x114514"）
    public static final String UDP_DEFAULT_MAGIC = "0x114514";

    // UDP 单包最大载荷（core: 65536 - 20 IP 头 - 8 UDP 头）
    public static final int MAX_DATAGRAM_SIZE = 65507;

    // 单帧（文件头 JSON）最大长度 防御恶意/损坏的长度前缀导致超大内存分配
    public static final int MAX_FRAME_SIZE = 64 * 1024 * 1024;

    // 安静期默认时长：scan 内最后一次收到报文后等这么久没有新报文就判定扫描结束
    public static final int DEFAULT_QUIET_TIMEOUT_MS = 300;

    // 文件数据分块大小（core 收发两侧都是 64 * 1024）
    public static final int FILE_CHUNK_SIZE = 64 * 1024;

    // 扫描参数（core: max_servers = 128, total_timeout = 2000ms）
    public static final int MAX_SERVERS = 128;
    public static final int SCAN_TOTAL_TIMEOUT_MS = 2000;

    // 会话中回复字节的轮询粒度（core: waitForClientReply 500ms）
    public static final int REPLY_POLL_MS = 500;

    private SyncBasic() {
    }

    // 文件头（TCP 上每个文件先发这一帧）
    // core: struct FileHeader { parent_dir_, file_name_, file_size_, last_in_dir_, session_end_ }
    public static final class FileHeader {

        // 相对父目录 以入队目录自身的名字为首段
        public String parent_dir = "";
        // 文件名
        public String file_name = "";
        // 文件总大小（字节）
        public long file_size = 0L;
        // 本文件是所在任务(入队目录)的最后一个文件（可选字段 旧端不识别时按 false 处理）
        // 接收端据此在一个任务的全部文件处理完后给出任务完成提示
        public boolean last_in_dir = false;
        // 会话结束控制帧标记（不携带文件信息 仅服务端正常收尾时发送）
        // TCP EOF 不能区分正常断开与网络中断 接收端以该标记判定正常结束
        public boolean session_end = false;
    }

    // 任务(入队目录)级完成报告：一个目录下的文件全部发送/接收完毕时由工作线程通知上层
    // core: struct TaskReport { name_, file_count_, byte_count_ }
    public static final class TaskReport {

        // 任务名（入队目录名）
        public String name = "";
        // 文件数
        public int file_count = 0;
        // 字节数（接收端跳过的文件按 0 计）
        public long byte_count = 0L;
    }

    // UDP 广播报文
    // core: struct UDPMessage { name_, ip_, port_, magic_word_ }
    public static final class UDPMessage {

        // 服务器名称
        public String name = "";
        // 对外广告的 IP
        public String ip = "";
        // 服务器实际监听的 TCP 端口
        public int port = 0;
        // 魔术字 接收端过滤用
        public String magic_word = "";
    }

    // 扫描到的服务器
    // core: struct ServerInfo { name_, ip_, port_ }
    public static final class ServerInfo {

        public String name = "";
        public String ip = "";
        public int port = 0;

        // 去重键 与 core 的 serverKey() 一致：ip:port
        public String key() {
            return ip + ":" + port;
        }
    }

    // 本机接口信息
    // 对应 core 的 SyncServer::getInterfaceInfo（ip + 子网定向广播地址）
    public static final class InterfaceInfo {

        public String ip = "";
        public String broadcast = "";
    }

    // 按接口枚举本机首个可用 IPv4 地址及其子网定向广播地址
    public static InterfaceInfo findInterfaceInfo() {

        InterfaceInfo point_to_point = null;
        InterfaceInfo any_broadcast = null;
        InterfaceInfo preferred = null;

        for (NetworkInterface nic : listInterfaces()) {
            if (!isUsable(nic)) {
                continue;
            }

            for (InterfaceAddress address : nic.getInterfaceAddresses()) {
                InetAddress ip = address.getAddress();

                if (!(ip instanceof Inet4Address) || ip.isLoopbackAddress() || ip.isLinkLocalAddress()) {
                    continue;
                }

                InetAddress broadcast = address.getBroadcast();

                if (broadcast == null) {
                    if (point_to_point == null) {
                        point_to_point = infoOf(ip, "255.255.255.255");
                    }

                    continue;
                }

                InterfaceInfo info = infoOf(ip, broadcast.getHostAddress());

                if (any_broadcast == null) {
                    any_broadcast = info;
                }

                if (preferred == null && isPreferredName(nic.getName())) {
                    preferred = info;
                }
            }
        }

        if (preferred != null) {
            return preferred;
        }

        if (any_broadcast != null) {
            return any_broadcast;
        }

        return point_to_point;
    }

    // 本机用于广告的 IP（对应 core 的 getLocalIP 兜底 127.0.0.1）
    public static String localIp() {

        InterfaceInfo info = findInterfaceInfo();

        return info == null || info.ip.isEmpty() ? "127.0.0.1" : info.ip;
    }

    private static List<NetworkInterface> listInterfaces() {

        try {
            Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();

            return interfaces == null ? Collections.<NetworkInterface>emptyList() : Collections.list(interfaces);
        } catch (SocketException error) {
            return Collections.<NetworkInterface>emptyList();
        }
    }

    private static boolean isUsable(NetworkInterface nic) {

        try {
            return nic.isUp() && !nic.isLoopback();
        } catch (SocketException error) {
            return false;
        }
    }

    // Wi-Fi / 以太网 / 热点优先 这三类是局域网发现唯一有意义的承载
    private static boolean isPreferredName(String name) {

        if (name == null) {
            return false;
        }

        String lower = name.toLowerCase();

        return lower.startsWith("wlan") || lower.startsWith("eth") || lower.startsWith("ap");
    }

    private static InterfaceInfo infoOf(InetAddress ip, String broadcast) {

        InterfaceInfo info = new InterfaceInfo();
        info.ip = ip.getHostAddress();
        info.broadcast = broadcast;

        return info;
    }
}
