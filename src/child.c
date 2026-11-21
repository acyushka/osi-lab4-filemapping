#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "shared.h"

int main(void) {
    // Открываем отображение, созданное родителем
    HANDLE hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, SHARED_NAME);
    if (!hMap) {
        print_error("OpenFileMappingA");
        return 1;
    }

    SharedBuf *shared = (SharedBuf *)MapViewOfFile(
            hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBuf));
    if (!shared) {
        print_error("MapViewOfFile");
        CloseHandle(hMap);
        return 1;
    }

    // Открываем события по имени
    HANDLE evData = OpenEventA(EVENT_MODIFY_STATE, FALSE, EVENT_DATA);
    HANDLE evAck  = OpenEventA(SYNCHRONIZE,       FALSE, EVENT_ACK);
    if (!evData || !evAck) {
        print_error("OpenEventA");
        if (evData) CloseHandle(evData);
        if (evAck)  CloseHandle(evAck);
        UnmapViewOfFile(shared);
        CloseHandle(hMap);
        return 1;
    }

    // Читаем stdin (файл), разбираем команды
    char line[4096];
    LONG line_no = 0;
    int  rc = 0;

    while (fgets(line, sizeof(line), stdin)) {
        const char *p = line;
        float acc;
        int off = 0;

        if (sscanf(p, "%f%n", &acc, &off) != 1) continue;
        p += off;

        int div_by_zero = 0;
        for (;;) {
            float b;
            int n = 0;
            if (sscanf(p, "%f%n", &b, &n) != 1) break;
            p += n;
            if (b == 0.0f) { div_by_zero = 1; break; }
            acc /= b;
        }

        // Заполняем общий буфер
        shared->line_no = ++line_no;
        if (div_by_zero) {
            shared->status = 1;
            shared->result = 0;
            strncpy(shared->message, "division by zero",
                    sizeof(shared->message) - 1);
            shared->message[sizeof(shared->message) - 1] = 0;
            SetEvent(evData);
            rc = 1;
            break;   // по заданию — дочерний процесс завершается
        } else {
            shared->status = 0;
            shared->result = acc;
            shared->message[0] = 0;
        }

        //Сообщаем родителю и ждём подтверждения
        if (!SetEvent(evData)) {
            print_error("SetEvent(evData)");
            rc = 2;
            break;
        }
        DWORD w = WaitForSingleObject(evAck, INFINITE);
        if (w != WAIT_OBJECT_0) {
            print_error("WaitForSingleObject(evAck)");
            rc = 2;
            break;
        }
    }

    if (ferror(stdin) && rc == 0) {
        perror("fgets");
        rc = 2;
    }

    // Освобождаем ресурсы
    CloseHandle(evData);
    CloseHandle(evAck);
    UnmapViewOfFile(shared);
    CloseHandle(hMap);
    return rc;
}