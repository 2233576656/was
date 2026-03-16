@echo off
chcp 65001 >nul
echo ===== 开始打包 APK =====

if not exist "gradle\wrapper\gradle-wrapper.jar" (
    echo ERROR: 请先运行 setup.bat
    pause & exit /b 1
)

echo [1/2] 编译 Debug APK...
call gradlew.bat assembleDebug
if errorlevel 1 (
    echo ERROR: 编译失败，查看上方错误信息
    pause & exit /b 1
)

echo [2/2] 完成！
echo APK 路径: app\build\outputs\apk\debug\app-debug.apk
explorer app\build\outputs\apk\debug
pause
