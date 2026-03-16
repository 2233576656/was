package com.client;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Build;
import android.os.IBinder;
import androidx.annotation.Nullable;

public class ClientService extends Service {

    static { System.loadLibrary("client"); }

    private static final String CHANNEL_ID = "client_channel";
    private static final int    NOTIF_ID   = 1;

    private Thread mConnectThread;
    private volatile boolean mRunning = false;

    public native int  nativeConnect(String ip, int port);
    public native void nativeDisconnect();

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String ip   = "192.168.1.100"; // 默认 IP，可通过 Intent 传入
        int    port = 8080;

        if (intent != null) {
            ip   = intent.getStringExtra("ip")   != null ? intent.getStringExtra("ip")   : ip;
            port = intent.getIntExtra("port", 8080);
        }

        // 读取保存的配置
        SharedPreferences prefs = getSharedPreferences("config", MODE_PRIVATE);
        ip   = prefs.getString("server_ip",   ip);
        port = prefs.getInt("server_port", port);

        startForeground(NOTIF_ID, buildNotification("连接中..."));
        startConnect(ip, port);
        return START_STICKY; // 被杀死后自动重启
    }

    private void startConnect(final String ip, final int port) {
        if (mRunning) return;
        mRunning = true;

        final String finalIp = ip;
        final int finalPort  = port;

        mConnectThread = new Thread(() -> {
            while (mRunning) {
                updateNotification("连接到 " + finalIp + ":" + finalPort);
                int ret = nativeConnect(finalIp, finalPort);
                if (!mRunning) break;
                // 断线后 5 秒重连
                updateNotification("断线，5秒后重连...");
                try { Thread.sleep(5000); } catch (InterruptedException e) { break; }
            }
        });
        mConnectThread.setDaemon(true);
        mConnectThread.start();
    }

    @Override
    public void onDestroy() {
        mRunning = false;
        nativeDisconnect();
        if (mConnectThread != null) mConnectThread.interrupt();
        super.onDestroy();
    }

    @Nullable
    @Override
    public IBinder onBind(Intent intent) { return null; }

    /* ── 通知 ─────────────────────────────────────────── */
    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel ch = new NotificationChannel(
                    CHANNEL_ID, "后台服务", NotificationManager.IMPORTANCE_LOW);
            getSystemService(NotificationManager.class).createNotificationChannel(ch);
        }
    }

    private Notification buildNotification(String text) {
        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);
        return builder.setContentTitle("系统服务").setContentText(text)
                .setSmallIcon(android.R.drawable.ic_dialog_info).build();
    }

    private void updateNotification(String text) {
        NotificationManager nm = getSystemService(NotificationManager.class);
        nm.notify(NOTIF_ID, buildNotification(text));
    }
}
