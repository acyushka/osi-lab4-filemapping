#ifndef SHARED_H
#define SHARED_H

#include <windows.h>

#define SHARED_NAME  "Local\\Lab4SharedBuf"
#define EVENT_DATA   "Local\\Lab4EventData"
#define EVENT_ACK    "Local\\Lab4EventAck"

typedef struct {
    LONG   line_no;
    LONG   status;            // 0 — ok, 1 — division by zero
    double result;
    char   message[256];
} SharedBuf;

#endif