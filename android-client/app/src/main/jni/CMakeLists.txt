cmake_minimum_required(VERSION 3.22)
project(client)

add_library(client SHARED
    client_jni.c
    socket_client.c
)

target_include_directories(client PRIVATE include)

target_link_libraries(client
    android
    camera2ndk
    mediandk
    log
    pthread
)
