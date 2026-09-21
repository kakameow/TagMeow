package com.example.tagmeow;

import android.content.Context;
import android.net.wifi.WifiManager;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;
import java.net.SocketException;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

// UDP 广播接收器

// scan() 阻塞收集「所有」合法广播后返回：持续接收直到出现安静期（quiet_timeout 内无任何报文）
// 或收集满 max_servers 台或超过 total_timeout 按 ip:port 去重 非法报文与魔术字不匹配只跳过
// 不中断扫描 套接字级错误上报到 getLastError() 并返回已收集的部分结果

public final class BroadcastReceiver {

    // MulticastLock 的标签 只用于 logcat / dumpsys 观察
    private static final String LOCK_TAG = "tagmeow-sync";

    private final Context context;
    private final DatagramSocket socket;
    private final String magic_word;
    private final int quiet_timeout_ms;

    private String error_string = "";

    public BroadcastReceiver(Context context, int port, String magic_word, int quiet_timeout_ms) throws SyncException {

        this.context = context == null ? null : context.getApplicationContext();
        this.magic_word = magic_word == null ? SyncBasic.UDP_DEFAULT_MAGIC : magic_word;
        this.quiet_timeout_ms = quiet_timeout_ms <= 0 ? 1 : quiet_timeout_ms;

        try {
            socket = new DatagramSocket(null);
            socket.setReuseAddress(true);
            socket.setBroadcast(true);
            socket.bind(new InetSocketAddress(port));
            socket.setSoTimeout(this.quiet_timeout_ms);
        } catch (SocketException error) {
            throw new SyncException(SyncError.IO_ERROR, "cannot bind the UDP port " + port, error);
        }
    }

    // 收集所有合法广播 按 ip:port 去重后返回
    // 结束条件（任一）：出现安静期 / 收集满 max_servers 台 / 超过 total_timeout_ms
    // 安静期结束视为正常结束 没有服务器也返回空列表而不是错误
    public List<SyncBasic.ServerInfo> scan(int max_servers, int total_timeout_ms) {

        error_string = "";

        List<SyncBasic.ServerInfo> servers = new ArrayList<>();
        Set<String> seen = new LinkedHashSet<>();

        byte[] buffer = new byte[65536];
        DatagramPacket packet = new DatagramPacket(buffer, buffer.length);

        long dead_line = System.currentTimeMillis() + Math.max(total_timeout_ms, 1);

        WifiManager.MulticastLock lock = acquireMulticastLock();

        try {
            while (servers.size() < max_servers) {
                if (System.currentTimeMillis() >= dead_line) {
                    break;
                }

                // 每轮都要还原长度 否则下一轮只能收到不超过上一轮那么长的包
                packet.setLength(buffer.length);

                try {
                    socket.receive(packet);
                } catch (SocketTimeoutException quiet) {
                    break; // 安静期：扫描正常结束
                } catch (IOException error) {
                    // 套接字级错误：上报并返回已经收集到的部分结果
                    error_string = "[warning] scan failed: " + error;

                    break;
                }

                String text = new String(packet.getData(), packet.getOffset(), packet.getLength(), StandardCharsets.UTF_8);
                SyncBasic.UDPMessage msg = parseUDPMessage(text);

                if (msg == null || !magic_word.equals(msg.magic_word)) {
                    continue; // 非法报文 / 魔术字不匹配 只跳过
                }

                SyncBasic.ServerInfo info = new SyncBasic.ServerInfo();
                info.name = msg.name;
                info.ip = msg.ip;
                info.port = msg.port;

                if (seen.add(info.key())) {
                    servers.add(info);
                }
            }
        } finally {
            releaseMulticastLock(lock);
        }

        return servers;
    }

    public String getLastError() {
        return error_string;
    }

    public void close() {

        if (socket != null && !socket.isClosed()) {
            socket.close();
        }
    }

    // 严格校验必需字段 与 core 的 BroadcastReceiver::parseUDPMessage 一一对应
    // 解析失败返回 null（core 返回 false 并跳过）
    static SyncBasic.UDPMessage parseUDPMessage(String data) {

        try {
            JSONObject json = new JSONObject(data);

            if (!json.has("name") || !json.has("ip") || !json.has("port") || !json.has("magic_word")) {
                return null;
            }

            SyncBasic.UDPMessage msg = new SyncBasic.UDPMessage();
            msg.name = json.getString("name");
            msg.ip = json.getString("ip");
            msg.port = json.getInt("port");
            msg.magic_word = json.getString("magic_word");

            return msg;
        } catch (JSONException error) {
            return null;
        }
    }

    private WifiManager.MulticastLock acquireMulticastLock() {

        if (context == null) {
            return null;
        }

        try {
            WifiManager wifi = (WifiManager) context.getSystemService(Context.WIFI_SERVICE);

            if (wifi == null) {
                return null;
            }

            WifiManager.MulticastLock lock = wifi.createMulticastLock(LOCK_TAG);
            lock.setReferenceCounted(true);
            lock.acquire();

            return lock;
        } catch (Throwable error) {
            // 没给 CHANGE_WIFI_MULTICAST_STATE / 没有 Wi-Fi 硬件时都不能让扫描整体失败
            return null;
        }
    }

    private void releaseMulticastLock(WifiManager.MulticastLock lock) {

        if (lock == null) {
            return;
        }

        try {
            lock.release();
        } catch (Throwable error) {
        }
    }
}
