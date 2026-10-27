#ifndef PROJECT_IO_H
#define PROJECT_IO_H

#include "rf_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Read/write the plain-text .rfproj format used by the existing workbench. */
int project_parse_text(const char *text, Project *project, char *error, size_t error_size);
int project_load(const char *path, Project *project, char *error, size_t error_size);
int project_save(const char *path, const Project *project, char *error, size_t error_size);

/* Plasma sidecar and unified workbench project persistence. */
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
