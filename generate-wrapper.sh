#!/bin/bash
# 生成 Gradle Wrapper 文件

echo "下载 Gradle Wrapper JAR..."
mkdir -p gradle/wrapper
curl -L -o gradle/wrapper/gradle-wrapper.jar \
  https://raw.githubusercontent.com/gradle/gradle/v8.2.0/gradle/wrapper/gradle-wrapper.jar

echo "设置 gradlew 执行权限..."
chmod +x gradlew

echo "完成！现在可以提交这些文件到 Git："
echo "  git add gradlew gradle/wrapper/"
echo "  git commit -m 'Add Gradle wrapper'"
echo "  git push"
