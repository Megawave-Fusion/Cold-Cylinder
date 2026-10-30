#ifndef PROJECT_IO_H
#define PROJECT_IO_H

#include "rf_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 几何/驱动文本格式：所有double以SI单位存储，频率MHz、角度deg字段例外。
 * 解析失败不修改输出；字符串输入须正常以NUL终止，文件输入会另查内嵌NUL。 */
int project_parse_text(const char *text, Project *project, char *error, size_t error_size);
int project_save(const char *path, const Project *project, char *error, size_t error_size);

/* 材料配置与统一工程的事务读写；可选error缓冲允许NULL或长度0。
 * 显式选择.plasmaconfig必须确实读取并验证该配置；默认副本不会代替损坏文件。
 * 保存经同目录独占临时文件+关闭检查+rename提交，失败保持原文件。 */
int plasma_config_parse_text(
    const char *text, Plasma *plasma, Options *options, char *error, size_t error_size);
int plasma_config_load(
    const char *path, Plasma *plasma, Options *options, char *error, size_t error_size);
int plasma_config_save(
    const char *path, const Plasma *plasma, const Options *options, char *error, size_t error_size);
int workbench_load(const char *path, WorkbenchProject *workbench, char *error, size_t error_size);
int workbench_save(
    const char *path, const WorkbenchProject *workbench, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif /* PROJECT_IO_H */
