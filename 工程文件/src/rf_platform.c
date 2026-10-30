#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "rf_platform.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <wchar.h>
#else
#include <unistd.h>
#endif

/* 拒绝无效字符串，而不将 NULL 传进 CRT 的无效参数处理器。 */
static int valid_text(const char *text)
{
    if (text && *text)
        return 1;
    errno = EINVAL;
    return 0;
}

#ifdef _WIN32
/* Win32 失败保留为常见的文件 errno，供现有 strerror 诊断使用。
 * 不使用 ANSI 文件 API；未知系统错误保守报告为 I/O 失败。 */
static void set_windows_errno(DWORD error)
{
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        errno = ENOENT;
        break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        errno = EACCES;
        break;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
        errno = EEXIST;
        break;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
        errno = ENOSPC;
        break;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        errno = ENOMEM;
        break;
    case ERROR_TOO_MANY_OPEN_FILES:
        errno = EMFILE;
        break;
    case ERROR_FILENAME_EXCED_RANGE:
    case ERROR_BUFFER_OVERFLOW:
        errno = ENAMETOOLONG;
        break;
    case ERROR_NO_UNICODE_TRANSLATION:
        errno = EILSEQ;
        break;
    case ERROR_NOT_SAME_DEVICE:
        errno = EXDEV;
        break;
    case ERROR_DIR_NOT_EMPTY:
        errno = ENOTEMPTY;
        break;
    case ERROR_WRITE_PROTECT:
        errno = EROFS;
        break;
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
    case ERROR_INVALID_PARAMETER:
        errno = EINVAL;
        break;
    default:
        errno = EIO;
        break;
    }
}

/* 严格 UTF-8 解码，包含 NUL 的长度先验证 int 和分配上限。
 * 非法字节不作替换，避免静默读写另一个名称的文件；内存归调用者 free。 */
static wchar_t *utf8_to_wide(const char *text)
{
    if (!valid_text(text))
        return NULL;
    size_t bytes = strlen(text);
    if (bytes >= INT_MAX) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)bytes + 1, NULL, 0);
    if (!count) {
        set_windows_errno(GetLastError());
        return NULL;
    }
    if (count > 32767 || (size_t)count > SIZE_MAX / sizeof(wchar_t)) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    wchar_t *wide = malloc((size_t)count * sizeof(*wide));
    if (!wide) {
        errno = ENOMEM;
        return NULL;
    }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)bytes + 1, wide, count)) {
        DWORD error = GetLastError();
        free(wide);
        set_windows_errno(error);
        return NULL;
    }
    return wide;
}
#endif

/* 打开现有或新文件；模式控制内容的文本/二进制转换，与路径编码无关。 */
FILE *rf_fopen_utf8(const char *path, const char *mode)
{
    if (!valid_text(path) || !valid_text(mode))
        return NULL;
#ifdef _WIN32
    wchar_t *wide_path = utf8_to_wide(path);
    if (!wide_path)
        return NULL;
    wchar_t *wide_mode = utf8_to_wide(mode);
    if (!wide_mode) {
        int saved = errno;
        free(wide_path);
        errno = saved;
        return NULL;
    }
    FILE *stream = _wfopen(wide_path, wide_mode);
    int saved = errno;
    free(wide_path);
    free(wide_mode);
    errno = saved;
    return stream;
#else
    return fopen(path, mode);
#endif
}

/* 删除文件时也保持路径编码一致；不吞掉失败，返回约定与 remove 相同。 */
int rf_remove_utf8(const char *path)
{
    if (!valid_text(path))
        return -1;
#ifdef _WIN32
    wchar_t *wide = utf8_to_wide(path);
    if (!wide)
        return -1;
    int status = _wremove(wide), saved = errno;
    free(wide);
    errno = saved;
    return status;
#else
    return remove(path);
#endif
}

/* basename/配套配置探测也经过宽字符路径边界，不在中文目录中换错文件。 */
int rf_access_exists_utf8(const char *path)
{
    if (!valid_text(path))
        return -1;
#ifdef _WIN32
    wchar_t *wide = utf8_to_wide(path);
    if (!wide)
        return -1;
    int status = _waccess(wide, 0), saved = errno;
    free(wide);
    errno = saved;
    return status;
#else
    return access(path, F_OK);
#endif
}

/* 覆盖提交不先 unlink 旧目标；Windows 不采用不能覆盖目标的 CRT rename。
 * 同文件系统提交失败时保留旧目标，临时文件的清理由调用者决定。 */
int rf_replace_file_utf8(const char *temporary, const char *destination)
{
    if (!valid_text(temporary) || !valid_text(destination))
        return -1;
#ifdef _WIN32
    wchar_t *wide_temporary = utf8_to_wide(temporary);
    if (!wide_temporary)
        return -1;
    wchar_t *wide_destination = utf8_to_wide(destination);
    if (!wide_destination) {
        int saved = errno;
        free(wide_temporary);
        errno = saved;
        return -1;
    }
    BOOL moved = MoveFileExW(wide_temporary, wide_destination,
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    DWORD error = moved ? ERROR_SUCCESS : GetLastError();
    free(wide_temporary);
    free(wide_destination);
    if (!moved)
        set_windows_errno(error);
    return moved ? 0 : -1;
#else
    return rename(temporary, destination);
#endif
}

/* 先构造自有候选路径再写输出，因此 destination 与输出缓冲可重叠。
 * 名称唯一性由操作系统独占创建保证，后缀只用于减少碰撞，不作密码学承诺。 */
FILE *rf_open_temporary_utf8(const char *destination, char *temporary, size_t capacity)
{
    if (!valid_text(destination) || !temporary || !capacity) {
        if (temporary && capacity)
            temporary[0] = '\0';
        errno = EINVAL;
        return NULL;
    }
#ifdef _WIN32
    const size_t suffix_length = 21; /* .tmp. 和 16 位十六进制标识 */
#else
    const size_t suffix_length = sizeof(".tmp.XXXXXX") - 1;
#endif
    size_t length = strlen(destination);
    if (length > SIZE_MAX - suffix_length - 1 || length + suffix_length + 1 > capacity) {
        temporary[0] = '\0';
        errno = ENAMETOOLONG;
        return NULL;
    }
    size_t total = length + suffix_length + 1;
    char *candidate = malloc(total);
    if (!candidate) {
        temporary[0] = '\0';
        errno = ENOMEM;
        return NULL;
    }
    memcpy(candidate, destination, length);
#ifdef _WIN32
    static volatile LONG serial;
    LARGE_INTEGER counter;
    if (!QueryPerformanceCounter(&counter))
        counter.QuadPart = (LONGLONG)GetTickCount();
    uint64_t nonce = (uint64_t)counter.QuadPart ^ ((uint64_t)GetCurrentProcessId() << 32) ^
        (uint32_t)InterlockedIncrement(&serial);
    temporary[0] = '\0';
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        snprintf(candidate + length, total - length, ".tmp.%016llx", (unsigned long long)nonce);
        wchar_t *wide = utf8_to_wide(candidate);
        if (!wide)
            break;
        HANDLE handle = CreateFileW(wide, GENERIC_READ | GENERIC_WRITE, 0, NULL,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            free(wide);
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
                nonce += UINT64_C(0x9e3779b97f4a7c15);
                errno = EEXIST;
                continue;
            }
            set_windows_errno(error);
            break;
        }
        /* _open_osfhandle 成功后由描述符接管 HANDLE；_fdopen 再接管描述符。 */
        int fd = _open_osfhandle((intptr_t)handle, _O_RDWR | _O_BINARY);
        if (fd < 0) {
            int saved = errno;
            CloseHandle(handle);
            DeleteFileW(wide);
            free(wide);
            free(candidate);
            errno = saved;
            return NULL;
        }
        FILE *stream = _fdopen(fd, "wb");
        if (!stream) {
            int saved = errno;
            _close(fd);
            DeleteFileW(wide);
            free(wide);
            free(candidate);
            errno = saved;
            return NULL;
        }
        free(wide);
        memcpy(temporary, candidate, total);
        free(candidate);
        return stream;
    }
    int saved = errno;
    free(candidate);
    errno = saved;
    return NULL;
#else
    memcpy(candidate + length, ".tmp.XXXXXX", suffix_length + 1);
    temporary[0] = '\0';
    int fd = mkstemp(candidate);
    if (fd < 0) {
        int saved = errno;
        free(candidate);
        errno = saved;
        return NULL;
    }
    FILE *stream = fdopen(fd, "wb");
    if (!stream) {
        int saved = errno;
        close(fd);
        remove(candidate);
        free(candidate);
        errno = saved;
        return NULL;
    }
    memcpy(temporary, candidate, total);
    free(candidate);
    return stream;
#endif
}
