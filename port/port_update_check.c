#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "port_update_check.h"
#include "port_version.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TMC_RELEASES_URL "https://github.com/MatheoVignaud/tmc/releases"
#define TMC_WIDEN2(value) L##value
#define TMC_WIDEN(value) TMC_WIDEN2(value)

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#endif

static void FreeString(char* value) {
    free(value);
}

static bool AppendBytes(char** buffer, size_t* size, size_t* capacity, const char* bytes, size_t byteCount) {
    if (*size + byteCount + 1 > *capacity) {
        size_t newCapacity = *capacity ? *capacity * 2 : 8192;
        while (*size + byteCount + 1 > newCapacity) {
            newCapacity *= 2;
        }

        char* newBuffer = (char*)realloc(*buffer, newCapacity);
        if (!newBuffer) {
            return false;
        }

        *buffer = newBuffer;
        *capacity = newCapacity;
    }

    memcpy(*buffer + *size, bytes, byteCount);
    *size += byteCount;
    (*buffer)[*size] = '\0';
    return true;
}

static char* DuplicateRange(const char* begin, const char* end) {
    size_t length = (size_t)(end - begin);
    char* result = (char*)malloc(length + 1);
    if (!result) {
        return NULL;
    }

    memcpy(result, begin, length);
    result[length] = '\0';
    return result;
}

static char* ExtractJsonString(const char* json, const char* key) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char* pos = strstr(json, pattern);
    if (!pos) {
        return NULL;
    }

    pos += strlen(pattern);
    while (*pos && isspace((unsigned char)*pos)) {
        pos++;
    }
    if (*pos != ':') {
        return NULL;
    }
    pos++;
    while (*pos && isspace((unsigned char)*pos)) {
        pos++;
    }
    if (*pos != '"') {
        return NULL;
    }
    pos++;

    const char* valueStart = pos;
    while (*pos) {
        if (*pos == '\\' && pos[1]) {
            pos += 2;
            continue;
        }
        if (*pos == '"') {
            return DuplicateRange(valueStart, pos);
        }
        pos++;
    }

    return NULL;
}

static bool ParseVersionNumbers(const char* version, int parts[3]) {
    const char* p = version;
    if (*p == 'v' || *p == 'V') {
        p++;
    }

    for (int i = 0; i < 3; i++) {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }

        int value = 0;
        while (isdigit((unsigned char)*p)) {
            value = value * 10 + (*p - '0');
            p++;
        }
        parts[i] = value;

        if (i < 2) {
            if (*p != '.') {
                return false;
            }
            p++;
        }
    }

    return true;
}

static bool IsNewerVersion(const char* latestTag, const char* currentVersion) {
    int latest[3] = { 0, 0, 0 };
    int current[3] = { 0, 0, 0 };

    if (!ParseVersionNumbers(latestTag, latest) || !ParseVersionNumbers(currentVersion, current)) {
        return false;
    }

    for (int i = 0; i < 3; i++) {
        if (latest[i] > current[i]) {
            return true;
        }
        if (latest[i] < current[i]) {
            return false;
        }
    }

    return false;
}

#ifdef _WIN32
static char* FetchLatestReleaseJson(void) {
    char* response = NULL;
    size_t responseSize = 0;
    size_t responseCapacity = 0;

    HINTERNET session = WinHttpOpen(L"tmc_pc/" TMC_WIDEN(TMC_PC_VERSION),
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS,
                                    0);
    if (!session) {
        return NULL;
    }

    WinHttpSetTimeouts(session, 3000, 3000, 3000, 3000);

    HINTERNET connect = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return NULL;
    }

    HINTERNET request = WinHttpOpenRequest(connect,
                                           L"GET",
                                           L"/repos/MatheoVignaud/tmc/releases?per_page=1",
                                           NULL,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return NULL;
    }

    static const wchar_t headers[] =
        L"Accept: application/vnd.github+json\r\n"
        L"User-Agent: tmc_pc\r\n";

    BOOL ok = WinHttpSendRequest(request,
                                 headers,
                                 (DWORD)-1L,
                                 WINHTTP_NO_REQUEST_DATA,
                                 0,
                                 0,
                                 0) &&
              WinHttpReceiveResponse(request, NULL);

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (ok) {
        ok = WinHttpQueryHeaders(request,
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX,
                                 &statusCode,
                                 &statusSize,
                                 WINHTTP_NO_HEADER_INDEX) &&
             statusCode == 200;
    }

    while (ok) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
            break;
        }

        char* chunk = (char*)malloc(available);
        if (!chunk) {
            ok = FALSE;
            break;
        }

        DWORD bytesRead = 0;
        if (!WinHttpReadData(request, chunk, available, &bytesRead)) {
            free(chunk);
            ok = FALSE;
            break;
        }

        ok = AppendBytes(&response, &responseSize, &responseCapacity, chunk, bytesRead);
        free(chunk);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if (!ok || responseSize == 0) {
        FreeString(response);
        return NULL;
    }

    return response;
}
#else
static char* ReadCommandOutput(const char* command) {
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        return NULL;
    }

    char* response = NULL;
    size_t responseSize = 0;
    size_t responseCapacity = 0;
    char chunk[4096];

    while (fgets(chunk, sizeof(chunk), pipe)) {
        if (!AppendBytes(&response, &responseSize, &responseCapacity, chunk, strlen(chunk))) {
            FreeString(response);
            pclose(pipe);
            return NULL;
        }
    }

    int status = pclose(pipe);
    if (status != 0 || responseSize == 0) {
        FreeString(response);
        return NULL;
    }

    return response;
}

static char* FetchLatestReleaseJson(void) {
    char* response = ReadCommandOutput(
        "curl -fsSL --max-time 3 -H 'Accept: application/vnd.github+json' "
        "-H 'User-Agent: tmc_pc' 'https://api.github.com/repos/MatheoVignaud/tmc/releases?per_page=1' 2>/dev/null");
    if (response) {
        return response;
    }

    return ReadCommandOutput(
        "wget -q -T 3 --header='Accept: application/vnd.github+json' "
        "--header='User-Agent: tmc_pc' -O - 'https://api.github.com/repos/MatheoVignaud/tmc/releases?per_page=1' 2>/dev/null");
}
#endif

static void ShowUpdateDialog(SDL_Window* window, const char* latestTag, const char* releaseUrl) {
    char message[512];
    snprintf(message,
             sizeof(message),
             "A new version of TMC PC is available.\n\n"
             "Installed version: %s\n"
             "Latest version: %s\n\n"
             "Open the GitHub release page?",
             TMC_PC_VERSION,
             latestTag);

    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Later" },
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "View release" },
    };
    const SDL_MessageBoxData data = {
        SDL_MESSAGEBOX_INFORMATION,
        window,
        "Update available",
        message,
        (int)(sizeof(buttons) / sizeof(buttons[0])),
        buttons,
        NULL,
    };

    int buttonId = 0;
    if (SDL_ShowMessageBox(&data, &buttonId) && buttonId == 1) {
        SDL_OpenURL(releaseUrl && releaseUrl[0] ? releaseUrl : TMC_RELEASES_URL);
    }
}

void Port_CheckForUpdates(SDL_Window* window) {
    /* Skip the (blocking, network) update check in automated/headless runs:
     * TMC_AUTOPLAY drives CI/smoke/repro harnesses with no user to see a
     * dialog, and TMC_NO_UPDATE_CHECK is an explicit opt-out. */
    const char* autoplay = getenv("TMC_AUTOPLAY");
    const char* noupdate = getenv("TMC_NO_UPDATE_CHECK");
    if ((autoplay && autoplay[0] && autoplay[0] != '0') ||
        (noupdate && noupdate[0] && noupdate[0] != '0')) {
        return;
    }

    char* json = FetchLatestReleaseJson();
    if (!json) {
        return;
    }

    char* latestTag = ExtractJsonString(json, "tag_name");
    char* releaseUrl = ExtractJsonString(json, "html_url");

    if (latestTag && IsNewerVersion(latestTag, TMC_PC_VERSION)) {
        ShowUpdateDialog(window, latestTag, releaseUrl ? releaseUrl : TMC_RELEASES_URL);
    }

    FreeString(releaseUrl);
    FreeString(latestTag);
    FreeString(json);
}
