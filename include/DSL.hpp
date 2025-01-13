#pragma once

#include <cstdio>

#define catchNullptr(POINTER, RETURN_IN_CASE_OF_FAILURE) {                                                         \
    if ((POINTER) == nullptr) {                                                                                     \
        FILE *logFile = fopen("./logs/logFile.txt", "a");                                                            \
        fprintf(logFile, "%s pointer at %s at %s(%d) is NULL\n", #POINTER, __PRETTY_FUNCTION__, __FILE__, __LINE__);  \
        fclose(logFile);                                                                                               \
        return RETURN_IN_CASE_OF_FAILURE;                                                                               \
    }                                                                                                                    \
}

#define EXECUTE_ERROR(ERROR) {                                                                                                   \
    if (ERROR) {                                                                                                                  \
        FILE *logFile = fopen("./logs/logFile.txt", "a");                                                                          \
        fprintf(logFile, "%d error at %s at %s(%d) exceeded!\n", ERROR, __PRETTY_FUNCTION__, __FILE__, __LINE__);                   \
        fclose(logFile);                                                                                                             \
    }                                                                                                                                 \
}

#define MESSAGE(msg, ...) {                                                                                                  \
    FILE *logFile = fopen("./logs/logFile.txt", "a");                                                                         \
    fprintf (logFile, "MSG:" msg ", from %s at %s(%d)\n", __VA_ARGS__, __PRETTY_FUNCTION__, __FILE__, __LINE__);               \
    fclose(logFile);                                                                                                            \
}

#define MSG(msg) {                                                                                                      \
    FILE *logFile = fopen("./logs/logFile.txt", "a");                                                                    \
    fprintf (logFile, "MSG:" msg ", from %s at %s(%d)\n", __PRETTY_FUNCTION__, __FILE__, __LINE__);                       \
    fclose(logFile);                                                                                                       \
}

#define MESSAGE_CLEAR() {                                                                                               \
    FILE *logFile = fopen("./logs/logFile.txt", "w");                                                                     \
    fclose(logFile);                                                                                                       \
}

#define SQR(a) ((a) * (a))