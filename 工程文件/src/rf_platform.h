#ifndef RF_PLATFORM_H
#define RF_PLATFORM_H

#include <stddef.h>
#include <stdio.h>

/* 跨平台文件边界：调用者以 UTF-8 传递路径，文件内容不在此转换。
 * Windows 严格转换为 UTF-16 后调用宽字符接口；POSIX 保留原路径字节。
 * NULL、空路径或空模式均失败并设置 errno；不接管输入字符串。 */
FILE *rf_fopen_utf8(const char *path, const char *mode);
int rf_remove_utf8(const char *path);

/* 与 access(path,F_OK) 一致：0 表示存在，-1 表示不存在或访问失败；
 * 调用者可用 errno==ENOENT 区分缺失，不把无权限当作可忽略的缺失。 */
int rf_access_exists_utf8(const char *path);

/* 将已经关闭的同目录临时文件提交为目标，允许覆盖已有普通文件。
 * 返回 0 表示成功，-1 表示失败并设置 errno；失败不预先删除目标或临时文件。
 * 源、目标应为不同路径且位于同一文件系统；不提供跨卷复制回退。 */
int rf_replace_file_utf8(const char *temporary, const char *destination);

/* 在目标同目录独占创建唯一临时文件，以二进制写入模式返回 FILE*。
 * 成功才将实际临时路径写入 temporary；其容量必须包含路径和追加后缀及 NUL。
 * 失败返回 NULL、设置 errno，并在缓冲有效时清空 temporary；创建后包装失败
 * 会关闭并移除本次文件。调用者负责 fclose，随后提交或移除此临时文件。 */
FILE *rf_open_temporary_utf8(const char *destination, char *temporary, size_t capacity);

#endif
