package com.example.tagmeow;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketException;
import java.net.UnknownHostException;
import java.nio.charset.StandardCharsets;

// UDP 广播发送器

// - send() 只单次发送 不重试无确认 重复广播由调用方（SyncServer::workerLoop）循环控制
// - 报文超过 UDP 载荷上限时不发 返回 MESSAGE_SIZE（core 返回 errc::message_size）
// - 单包发送失败不抛异常 用返回的错误码表达 避免广播失败把整个服务器拖垮

// 报文格式与 core 完全相同（nlohmann 的键序是排序的，这里也按 name/ip/magic_word/port 的
// JSON 对象发出，接收端只认字段不认顺序）：
//   {"name": ..., "ip": ..., "port": ..., "magic_word": ...}

public final class BroadcastSender {

    private final DatagramSocket socket;
    private final InetAddress target;
    private final int port;

    private String error_string = "";

    // broadcast_addr：广播目标地址
    //   默认 255.255.255.255 多网卡时应由 SyncServer 传入子网定向广播地址
    public BroadcastSender(int port, String broadcast_addr) throws SyncException {

        this.port = port;

        InetAddress address;

        try {
            address = InetAddress.getByName(broadcast_addr == null || broadcast_addr.isEmpty() ? "255.255.255.255" : broadcast_addr);
        } catch (UnknownHostException error) {
            throw new SyncException(SyncError.INVALID_ARGUMENT, "bad broadcast address: " + broadcast_addr, error);
        }

        try {
            // 绑到端口 0：只发不收 不占用 UDP_DEFAULT_PORT
            socket = new DatagramSocket(0);
            socket.setBroadcast(true);
        } catch (SocketException error) {
            throw new SyncException(SyncError.IO_ERROR, "cannot open the broadcast socket", error);
        }

        this.target = address;
    }

    // 单次发送一条广播报文
    public SyncError send(SyncBasic.UDPMessage msg) {

        error_string = "";

        byte[] payload;

        try {
            JSONObject json = new JSONObject();
            json.put("name", msg.name);
            json.put("ip", msg.ip);
            json.put("port", msg.port);
            json.put("magic_word", msg.magic_word);

            payload = json.toString().getBytes(StandardCharsets.UTF_8);
        } catch (JSONException error) {
            error_string = "[warning] cannot build the broadcast message: " + error;

            return SyncError.INVALID_ARGUMENT;
        }

        // 避免 send 返回 EMSGSIZE 或触发分片
        if (payload.length > SyncBasic.MAX_DATAGRAM_SIZE) {
            error_string = "[warning] broadcast message is too large: " + payload.length;

            return SyncError.MESSAGE_SIZE;
        }

        try {
            socket.send(new DatagramPacket(payload, payload.length, target, port));

            return SyncError.NONE;
        } catch (IOException error) {
            error_string = "[warning] broadcast failed: " + error;

            return SyncError.IO_ERROR;
        }
    }

    public String getLastError() {
        return error_string;
    }

    public void close() {

        if (socket != null && !socket.isClosed()) {
            socket.close();
        }
    }
}
