package com.client;

import android.Manifest;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import java.util.ArrayList;
import java.util.List;

public class MainActivity extends AppCompatActivity {

    private EditText mEtIp, mEtPort;
    private TextView mTvStatus;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        mEtIp     = findViewById(R.id.et_ip);
        mEtPort   = findViewById(R.id.et_port);
        mTvStatus = findViewById(R.id.tv_status);
        Button btnStart = findViewById(R.id.btn_start);
        Button btnStop  = findViewById(R.id.btn_stop);
        Button btnAccessibility = findViewById(R.id.btn_accessibility);

        // 加载保存的配置
        SharedPreferences prefs = getSharedPreferences("config", MODE_PRIVATE);
        mEtIp.setText(prefs.getString("server_ip", "192.168.1.100"));
        mEtPort.setText(String.valueOf(prefs.getInt("server_port", 8080)));

        btnStart.setOnClickListener(v -> startService());
        btnStop.setOnClickListener(v  -> stopService());
        btnAccessibility.setOnClickListener(v -> openAccessibilitySettings());

        requestPermissions();
    }

    private void startService() {
        String ip   = mEtIp.getText().toString().trim();
        int    port = 8080;
        try { port = Integer.parseInt(mEtPort.getText().toString().trim()); }
        catch (NumberFormatException ignored) {}

        // 保存配置
        getSharedPreferences("config", MODE_PRIVATE).edit()
                .putString("server_ip", ip).putInt("server_port", port).apply();

        Intent intent = new Intent(this, ClientService.class);
        intent.putExtra("ip", ip);
        intent.putExtra("port", port);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            startForegroundService(intent);
        else
            startService(intent);

        mTvStatus.setText("状态: 运行中");
        Toast.makeText(this, "服务已启动", Toast.LENGTH_SHORT).show();
    }

    private void stopService() {
        stopService(new Intent(this, ClientService.class));
        mTvStatus.setText("状态: 已停止");
        Toast.makeText(this, "服务已停止", Toast.LENGTH_SHORT).show();
    }

    private void openAccessibilitySettings() {
        startActivity(new Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS));
        Toast.makeText(this, "请找到并开启"键盘监听服务"", Toast.LENGTH_LONG).show();
    }

    private void requestPermissions() {
        List<String> perms = new ArrayList<>();
        perms.add(Manifest.permission.CAMERA);
        perms.add(Manifest.permission.INTERNET);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            perms.add(Manifest.permission.READ_MEDIA_IMAGES);
            perms.add(Manifest.permission.POST_NOTIFICATIONS);
        } else {
            perms.add(Manifest.permission.READ_EXTERNAL_STORAGE);
        }

        List<String> toRequest = new ArrayList<>();
        for (String p : perms) {
            if (checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED)
                toRequest.add(p);
        }
        if (!toRequest.isEmpty())
            ActivityCompat.requestPermissions(this, toRequest.toArray(new String[0]), 100);
    }
}
