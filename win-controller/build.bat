@echo off
chcp 65001 >nul
echo Building win_client.exe...
gcc win_client.c -o win_client.exe -lws2_32 -lgdi32 -lshell32 -O2
if errorlevel 1 (
    echo Build failed.
    pause
) else (
    echo Build OK: win_client.exe
    echo Usage: win_client.exe [server_ip] [port]
    echo Example: win_client.exe 192.168.1.100 8081
)
pause
