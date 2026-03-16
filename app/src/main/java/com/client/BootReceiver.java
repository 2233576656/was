package com.client;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Build;

/**
 * 开机自启：设备重启后自动启动 ClientService
 */
public class BootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        if (Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) {
            SharedPreferences prefs = context.getSharedPreferences("config", Context.MODE_PRIVATE);
            String ip   = prefs.getString("server_ip", "192.168.1.100");
            int    port = prefs.getInt("server_port", 8080);

            Intent service = new Intent(context, ClientService.class);
            service.putExtra("ip", ip);
            service.putExtra("port", port);

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                context.startForegroundService(service);
            else
                context.startService(service);
        }
    }
}
