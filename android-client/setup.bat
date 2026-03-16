@echo off
chcp 65001 >nul
echo [1/3] 检查 Java...
java -version 2>nul
if errorlevel 1 (
    echo ERROR: 未找到 Java，请先安装 JDK 17
    echo 下载地址: https://adoptium.net/
    pause & exit /b 1
)

echo [2/3] 下载 Gradle Wrapper...
powershell -Command "Invoke-WebRequest -Uri 'https://services.gradle.org/distributions/gradle-8.2-bin.zip' -OutFile 'gradle-8.2-bin.zip'"
powershell -Command "Expand-Archive -Path 'gradle-8.2-bin.zip' -DestinationPath 'gradle-dist' -Force"
set GRADLE_BIN=gradle-dist\gradle-8.2\bin\gradle.bat

echo [3/3] 生成 gradlew...
%GRADLE_BIN% wrapper
del gradle-8.2-bin.zip
rmdir /s /q gradle-dist

echo 完成！现在可以运行 build.bat 打包
pause
