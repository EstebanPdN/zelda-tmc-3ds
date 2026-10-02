/*
 * port_android_log.c — route stdout/stderr into logcat on Android.
 *
 * The whole port logs with fprintf(stderr, ...) (AGENTS.md convention), which
 * Android silently discards: an app's fd 1/2 point at /dev/null. This bridge
 * dup2()s both onto a pipe and pumps complete lines into
 * __android_log_write(tag "tmc"), so `adb logcat -s tmc` shows exactly what a
 * desktop terminal would. No-op stub elsewhere.
 */

#ifdef __ANDROID__

#include <android/log.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int sPipe[2] = { -1, -1 };

static void* LogPumpThread(void* arg) {
    (void)arg;
    char buf[1024];
    size_t fill = 0;
    for (;;) {
        ssize_t n = read(sPipe[0], buf + fill, sizeof(buf) - 1 - fill);
        if (n <= 0) {
            break;
        }
        fill += (size_t)n;
        buf[fill] = '\0';
        /* Emit complete lines; keep any trailing partial line buffered. */
        char* start = buf;
        for (;;) {
            char* nl = strchr(start, '\n');
            if (!nl) {
                break;
            }
            *nl = '\0';
            if (*start) {
                __android_log_write(ANDROID_LOG_INFO, "tmc", start);
            }
            start = nl + 1;
        }
        fill = strlen(start);
        memmove(buf, start, fill + 1);
        if (fill >= sizeof(buf) - 2) { /* pathological long line: flush it */
            __android_log_write(ANDROID_LOG_INFO, "tmc", buf);
            fill = 0;
        }
    }
    return NULL;
}

void Port_AndroidLog_Init(void) {
    if (sPipe[0] >= 0) {
        return;
    }
    if (pipe(sPipe) != 0) {
        return;
    }
    /* Line-buffer stdio so fprintf lands in the pipe promptly. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    dup2(sPipe[1], STDOUT_FILENO);
    dup2(sPipe[1], STDERR_FILENO);
    pthread_t t;
    if (pthread_create(&t, NULL, LogPumpThread, NULL) == 0) {
        pthread_detach(t);
    }
    fprintf(stderr, "[android] stderr -> logcat bridge active\n");
}

#else /* !__ANDROID__ */

void Port_AndroidLog_Init(void) {
}

#endif
