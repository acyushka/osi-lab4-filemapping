#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "shared.h"

static void close_if_valid(HANDLE h) {
    if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

int main(void) {
    char filename[MAX_PATH];
    printf("Enter file name: ");
    fflush(stdout);
    if (!fgets(filename, sizeof(filename), stdin)) {
        fprintf(stderr, "failed to read file name\n");
        return 1;
    }
    filename[strcspn(filename, "\r\n")] = 0;

    //Открываем входной файл (наследуемый)
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ,
                               &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        print_error("CreateFileA(input)");
        return 1;
    }

    // Создаём отображение в память
    HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, &sa,
                                     PAGE_READWRITE, 0, sizeof(SharedBuf),
                                     SHARED_NAME);
    if (!hMap) {
        print_error("CreateFileMappingA");
        close_if_valid(hFile);
        return 1;
    }

    SharedBuf *shared = (SharedBuf *)MapViewOfFile(
            hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBuf));
    if (!shared) {
        print_error("MapViewOfFile");
        close_if_valid(hMap);
        close_if_valid(hFile);
        return 1;
    }
    ZeroMemory(shared, sizeof(*shared));

    // Создаём два именованных события
    HANDLE evData = CreateEventA(&sa, FALSE, FALSE, EVENT_DATA);
    HANDLE evAck  = CreateEventA(&sa, FALSE, FALSE, EVENT_ACK);
    if (!evData || !evAck) {
        print_error("CreateEventA");
        if (evData) CloseHandle(evData);
        if (evAck)  CloseHandle(evAck);
        UnmapViewOfFile(shared);
        close_if_valid(hMap);
        close_if_valid(hFile);
        return 1;
    }

    // Запускаем дочерний процесс
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = hFile;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, "child.exe", NULL, NULL, TRUE,
                        0, NULL, NULL, &si, &pi)) {
        print_error("CreateProcessA");
        CloseHandle(evData);
        CloseHandle(evAck);
        UnmapViewOfFile(shared);
        close_if_valid(hMap);
        close_if_valid(hFile);
        return 1;
    }

    CloseHandle(hFile);

    // Цикл чтения из общей памяти
    BOOL done = FALSE;
    int had_error = 0;

    while (!done) {
        HANDLE waits[2] = { evData, pi.hProcess };
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (w == WAIT_FAILED) {
            print_error("WaitForMultipleObjects");
            had_error = 1;
            break;
        }

        if (w == WAIT_OBJECT_0) {
            // есть новые данные
            if (shared->status == 1) {
                fprintf(stderr, "%s\n", shared->message);
                had_error = 1;
                done = TRUE;
            } else {
                printf("%g\n", shared->result);
                fflush(stdout);
            }
            SetEvent(evAck);
        } else if (w == WAIT_OBJECT_0 + 1) {
            // ребёнок завершился — больше данных не будет
            done = TRUE;
        }
    }

    // Дожидаемся завершения
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    if (!GetExitCodeProcess(pi.hProcess, &code)) {
        print_error("GetExitCodeProcess");
    }

    // Освобождаем ресурсы
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(evData);
    CloseHandle(evAck);
    UnmapViewOfFile(shared);
    close_if_valid(hMap);

    if (had_error || code != 0) {
        fprintf(stderr, "child terminated with code %lu\n", code);
        return 1;
    }
    return 0;
}
