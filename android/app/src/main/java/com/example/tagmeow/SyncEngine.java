package com.example.tagmeow;

import android.content.Context;

import java.io.File;

// 同步引擎的进程级持有者

// Activity 真正退出时（isFinishing）由 UI 调 closeAll() 释放套接字与工作线程
// 只是想换下载目录时调 client() 传新路径即可（路径不同会重建 client）

final class SyncEngine {

    private static SyncServer server;
    private static SyncClient client;
    private static String client_path = "";

    private SyncEngine() {
    }

    // 惰性创建服务端（只创建一次 重复调用返回同一个）
    static synchronized SyncServer server(int port, String magic_word, int empty_queue_wait_minutes) {

        if (server == null) {
            server = new SyncServer(port, magic_word, empty_queue_wait_minutes);
        }

        return server;
    }

    static synchronized SyncServer server() {
        return server;
    }

    static synchronized boolean hasServer() {
        return server != null;
    }

    // 惰性创建客户端 下载路径与上次不同时重建（core 的下载路径构造时固定）
    static synchronized SyncClient client(Context context, int port, String magic_word, File download_path) throws SyncException {

        String path = download_path.getAbsolutePath();

        if (client != null && !path.equals(client_path)) {
            // 换目录连 records.json 的位置一起换：下载记录跟着下载目录走（core 的 records_path_）
            client.close();
            client = null;
        }

        if (client == null) {
            client = new SyncClient(context, port, magic_word, download_path);
            client_path = path;
        }

        return client;
    }

    static synchronized SyncClient client() {
        return client;
    }

    static synchronized boolean hasClient() {
        return client != null;
    }

    static synchronized void closeServer() {

        if (server != null) {
            server.stop();
            server = null;
        }
    }

    static synchronized void closeClient() {

        if (client != null) {
            client.close();
            client = null;
            client_path = "";
        }
    }

    static synchronized void closeAll() {

        closeServer();
        closeClient();
    }
}
