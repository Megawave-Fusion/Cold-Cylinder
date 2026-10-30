#include "project_io.h"
#include "rf_platform.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 字段名称、类型和保存顺序只在这里定义一次，读取、查重与写入共用。
 * offsetof 指向相应结构中的真实 int/double 成员，不经浮点转换保存整数。 */
typedef struct {
    const char *name;
    size_t offset;
    int integer;
} StoredField;

#define INT_FIELD(type, member) { #member, offsetof(type, member), 1 }
#define REAL_FIELD(type, member) { #member, offsetof(type, member), 0 }
#define COUNT(items) (sizeof(items) / sizeof((items)[0]))

static const StoredField project_fields[] = {
    INT_FIELD(Project, type),
    INT_FIELD(Project, elements),
    INT_FIELD(Project, current_direction),
    INT_FIELD(Project, winding_direction),
    REAL_FIELD(Project, global_phase_deg),
    INT_FIELD(Project, selected_m),
    REAL_FIELD(Project, kz),
    INT_FIELD(Project, radial_cells),
    REAL_FIELD(Project, radius),
    REAL_FIELD(Project, length),
    REAL_FIELD(Project, frequency_mhz),
    REAL_FIELD(Project, current),
    REAL_FIELD(Project, phase_step),
    REAL_FIELD(Project, antenna_radius),
    REAL_FIELD(Project, antenna_length),
    REAL_FIELD(Project, width),
    REAL_FIELD(Project, spacing),
    REAL_FIELD(Project, turns),
    REAL_FIELD(Project, azimuth_deg),
    REAL_FIELD(Project, axial_offset),
    REAL_FIELD(Project, arc_angle_deg),
    REAL_FIELD(Project, return_radius),
    REAL_FIELD(Project, band_width),
    INT_FIELD(Project, band_samples),
    REAL_FIELD(Project, inner_radius),
    REAL_FIELD(Project, gap_angle_deg),
    INT_FIELD(Project, source_mode),
    INT_FIELD(Project, current_pattern),
    REAL_FIELD(Project, current_phase_gradient),
};
static const StoredField plasma_fields[] = {
    REAL_FIELD(Plasma, radius),
    REAL_FIELD(Plasma, density),
    REAL_FIELD(Plasma, edge_ratio),
    REAL_FIELD(Plasma, magnetic_field),
    REAL_FIELD(Plasma, electron_collision),
    REAL_FIELD(Plasma, ion_collision),
    REAL_FIELD(Plasma, electron_collision_edge_ratio),
    REAL_FIELD(Plasma, ion_collision_edge_ratio),
    REAL_FIELD(Plasma, ion_mass_u),
    INT_FIELD(Plasma, ion_charge),
    INT_FIELD(Plasma, profile),
    INT_FIELD(Plasma, solve_mode),
};
static const StoredField ray_fields[] = {
    REAL_FIELD(RfRayConfig, r0), REAL_FIELD(RfRayConfig, z0), REAL_FIELD(RfRayConfig, phi0_deg),
    REAL_FIELD(RfRayConfig, tolerance), REAL_FIELD(RfRayConfig, max_step),
    REAL_FIELD(RfRayConfig, max_path), INT_FIELD(RfRayConfig, direction), INT_FIELD(RfRayConfig, max_steps),
};
static const StoredField option_fields[] = {
    INT_FIELD(Options, nk),
    INT_FIELD(Options, m_min),
    INT_FIELD(Options, m_max),
    INT_FIELD(Options, nz),
    INT_FIELD(Options, nr),
    REAL_FIELD(Options, kmin),
    REAL_FIELD(Options, kmax),
    REAL_FIELD(Options, zmin),
    REAL_FIELD(Options, zmax),
    REAL_FIELD(Options, phi),
};
/* Old files may contain settings for the never-implemented batch/comparison
 * UI. Validate their syntax on input, but keep them out of live state/output. */
static const struct {
    const char *name;
    int integer;
} obsolete_options[] = {
    { "scans", 1 }, { "space", 1 }, { "sweeps", 1 },
    { "sweep_kind", 1 }, { "sweep_points", 1 },
    { "comparison_kind", 1 }, { "comparison_points", 1 },
    { "sweep_min", 0 }, { "sweep_max", 0 },
    { "comparison_min", 0 }, { "comparison_max", 0 },
};
#undef INT_FIELD
#undef REAL_FIELD

/* 写入有长度约束的UTF-8诊断；失败路径统一返回0且允许无诊断缓冲。 */
static int io_fail(char *error, size_t size, const char *message)
{
    if (error && size)
        snprintf(error, size, "%s", message);
    return 0;
}

/* 在解析器自有的可修改副本上去除首尾空白，兼容LF/CRLF及末行无换行。 */
static char *trim(char *text)
{
    while (*text && strchr(" \t\r\n", *text))
        ++text;
    char *end = text + strlen(text);
    while (end > text && strchr(" \t\r\n", end[-1]))
        *--end = '\0';
    return text;
}

/* 严格解析有限double，拒绝无数字、溢出/下溢、NaN/Inf和尾部非空白内容。 */
static int parse_number(const char *text, double *value)
{
    char *end;
    errno = 0;
    double parsed = strtod(text, &end);
    if (end == text || errno == ERANGE || !isfinite(parsed) || *trim(end))
        return 0;
    *value = parsed;
    return 1;
}

/* 先验证数值确为整数且在int范围内，再转换；不能用截断掩盖小数或溢出。 */
static int parse_integer(const char *text, int *value)
{
    double number;
    if (!parse_number(text, &number) || number < INT_MIN || number > INT_MAX ||
        floor(number) != number)
        return 0;
    *value = (int)number;
    return 1;
}

/* 通过统一字段表定位名字；表项同时定义类型和真实结构偏移，避免读写规则分叉。 */
static const StoredField *find_field(const StoredField *fields, size_t count, const char *key)
{
    for (size_t i = 0; i < count; ++i)
        if (!strcmp(fields[i].name, key))
            return &fields[i];
    return NULL;
}

/* 仅在已找到的字段偏移写入正确类型；object指向候选状态而非调用者的已提交状态。 */
static int read_field(void *object, const StoredField *field, const char *value)
{
    char *target = (char *)object + field->offset;
    return field->integer ? parse_integer(value, (int *)target)
                          : parse_number(value, (double *)target);
}

/* 按照字段表写出确定顺序；double使用17位有效数字保证往返保真，整数不用浮点中转。 */
static int write_fields(
    FILE *stream, const void *object, const StoredField *fields, size_t count, const char *prefix)
{
    for (size_t i = 0; i < count; ++i) {
        const char *value = (const char *)object + fields[i].offset;
        int written =
            fields[i].integer
                ? fprintf(stream, "%s%s=%d\n", prefix, fields[i].name, *(const int *)value)
                : fprintf(stream, "%s%s=%.17g\n", prefix, fields[i].name, *(const double *)value);
        if (written < 0)
            return 0;
    }
    return 1;
}

/* 一次复制整段文本，再原地逐行切分，避免每行 malloc/free。
 * next_line 不修改调用者的原始文本，也保留空行以准确报告行号。 */
/* 为一次解析分配唯一文本副本；原始输入与已有工程不会被逐行切分操作修改。 */
static char *copy_text(const char *text, char *error, size_t size)
{
    char *copy = malloc(strlen(text) + 1);
    if (copy)
        strcpy(copy, text);
    else
        io_fail(error, size, "out of memory while parsing file");
    return copy;
}

/* 原地切出下一行并保留空行的计数语义；cursor为空表示已经到达文本末尾。 */
static char *next_line(char **cursor)
{
    if (!*cursor || !**cursor)
        return NULL;
    char *line = *cursor;
    char *end = strchr(line, '\n');
    if (end) {
        *end = '\0';
        *cursor = end + 1;
    } else
        *cursor = NULL;
    return trim(line);
}

/* 完整读取二进制字节后补C字符串终止符，拒绝短读与内嵌NUL，
 * 防止损坏文件的后半段参数被静默忽略；成功时out的内存归调用者释放。 */
static int read_file_text(const char *path, char **out, char *error, size_t size)
{
    if (!path || !*path || !out)
        return io_fail(error, size, "file path is invalid");
    FILE *stream = rf_fopen_utf8(path, "rb");
    if (!stream) {
        if (error && size)
            snprintf(error, size, "cannot open '%s': %s", path, strerror(errno));
        return 0;
    }
    long length;
    if (fseek(stream, 0, SEEK_END) || (length = ftell(stream)) < 0 || fseek(stream, 0, SEEK_SET)) {
        fclose(stream);
        return io_fail(error, size, "cannot determine file size");
    }
    char *text = malloc((size_t)length + 1);
    if (!text) {
        fclose(stream);
        return io_fail(error, size, "out of memory while reading file");
    }
    size_t count = fread(text, 1, (size_t)length, stream);
    fclose(stream);
    if (count != (size_t)length) {
        free(text);
        return io_fail(error, size, "cannot read file");
    }
    /* 文本解析以 NUL 结尾；文件中的内嵌 NUL 不能静默截断后半段参数。 */
    if (memchr(text, '\0', count)) {
        free(text);
        return io_fail(error, size, "project/configuration file contains an embedded NUL byte");
    }
    text[count] = '\0';
    *out = text;
    return 1;
}

/* 所有保存入口都先写临时文件，完整关闭后再改名；失败时保留原文件。 */
/* 平台层在目标同目录独占创建；Windows 中文路径和覆盖提交不经过 ANSI CRT。 */
static FILE *open_temporary(const char *path, char temporary[4096], char *error, size_t size)
{
    FILE *stream = rf_open_temporary_utf8(path, temporary, 4096);
    if (!stream) {
        int saved_errno = errno;
        if (error && size)
            snprintf(error, size, "cannot write '%s': %s", path, strerror(saved_errno));
        errno = saved_errno;
    }
    return stream;
}

/* 关闭并检查全部写入后才提交替换；失败仅移除本次临时文件，保持原工程不变。 */
static int finish_save(
    FILE *stream, const char *temporary, const char *path, int written, char *error, size_t size)
{
    int closed = fclose(stream) == 0;
    if (!written || !closed) {
        int saved_errno = errno ? errno : EIO;
        rf_remove_utf8(temporary);
        io_fail(error, size, "cannot write file");
        errno = saved_errno;
        return 0;
    }
    if (rf_replace_file_utf8(temporary, path)) {
        int saved_errno = errno;
        rf_remove_utf8(temporary);
        if (error && size)
            snprintf(error, size, "cannot commit '%s': %s", path, strerror(saved_errno));
        errno = saved_errno;
        return 0;
    }
    return 1;
}

/* 在默认候选参数上读取v8/v7几何及驱动字段，检测已知字段重复与非法值；
 * v7旧编号只映射6/7/8，以保留单鞍、双鞍和Nagoya的实际几何含义。 */
static int project_parse_candidate(const char *text, Project *project, char *error, size_t size)
{
    if (!text || !project)
        return io_fail(error, size, "project text or destination is null");
    int previous_order = !strncmp(text, "# Cylinder RF project v7;", 25) ||
        !strncmp(text, "# Cylinder RF Workbench project v7;", 35);
    if (!previous_order && strncmp(text, "# Cylinder RF project v8;", 25) &&
        strncmp(text, "# Cylinder RF Workbench project v8;", 35))
        return io_fail(error, size, "requires project v8 (or v7 with automatic antenna renumbering)");
    char *storage = copy_text(text, error, size);
    if (!storage)
        return 0;
    char *cursor = storage, *line;
    unsigned char seen[COUNT(project_fields)] = { 0 };
    size_t recognized = 0;
    unsigned long line_number = 0;
    int ok = 0;
    project_default(project);
    while ((line = next_line(&cursor))) {
        ++line_number;
        if (!*line || *line == '#')
            continue;
        char *equals = strchr(line, '=');
        if (!equals)
            continue; /* 旧工程中的非参数行仍可忽略。 */
        *equals = '\0';
        char *key = trim(line);
        const char *canonical = !strcmp(key, "spectrum_k_probe") ? "kz" : key;
        const StoredField *field = find_field(project_fields, COUNT(project_fields), canonical);
        if (!field)
            continue; /* 保持旧格式对额外几何/显示字段的兼容。 */
        if (!read_field(project, field, trim(equals + 1))) {
            if (error && size)
                snprintf(error,
                    size,
                    "invalid value for project key '%s' on line %lu",
                    key,
                    line_number);
            goto done;
        }
        size_t index = (size_t)(field - project_fields);
        if (seen[index]) {
            io_fail(error, size, "duplicate project key");
            goto done;
        }
        seen[index] = 1;
        ++recognized;
    }
    if (recognized < 10) {
        io_fail(error, size, "file is not a valid RF project");
        goto done;
    }
    /* v7: 6 double saddle, 7 Nagoya, 8 single saddle. Preserve the physical
     * antenna when loading earlier projects after the catalogue reorder. */
    if (previous_order) {
        if (project->type == 6) project->type = RF_ANT_SADDLE;
        else if (project->type == 7) project->type = RF_ANT_NAGOYA;
        else if (project->type == 8) project->type = RF_ANT_SINGLE_SADDLE;
    }
    ok = project_validate(project, error, size);
done:
    free(storage);
    return ok;
}

/* 候选状态通过全部校验才提交；解析失败不损坏调用者的有效工程。 */
/* 事务式几何解析：候选状态完全通过校验后才赋给调用者，失败不提交部分值。 */
int project_parse_text(const char *text, Project *project, char *error, size_t size)
{
    Project candidate;
    if (!project)
        return io_fail(error, size, "project destination is null");
    if (!project_parse_candidate(text, &candidate, error, size))
        return 0;
    *project = candidate;
    return 1;
}


/* 保存仅含几何/驱动的工程格式；主工作台完整状态使用workbench_save。 */
int project_save(const char *path, const Project *project, char *error, size_t size)
{
    if (!path || !*path || !project)
        return io_fail(error, size, "project path or source is invalid");
    if (!project_validate(project, error, size))
        return 0;
    char temporary[4096];
    FILE *stream = open_temporary(path, temporary, error, size);
    if (!stream)
        return 0;
    int ok = fputs("# Cylinder RF project v8; C11; antenna numbering 1-17; SI units, MHz and degrees explicitly named\n",
                 stream) >= 0 &&
             write_fields(stream, project, project_fields, COUNT(project_fields), "");
    return finish_save(stream, temporary, path, ok, error, size);
}

/* 统一读取独立材料配置与工程内的# plasma.字段；两者共用查重、类型、完整性校验。
 * 已废弃选项只验证语法后丢弃，旧无损模式在对外状态边界迁移为零碰撞极限。 */
static int parse_plasma_lines(const char *text,
    Plasma *plasma,
    Options *options,
    const char *prefix,
    int require_all,
    char *error,
    size_t size)
{
    if (!text || !plasma || !options)
        return io_fail(error, size, "plasma text or destination is null");
    char *storage = copy_text(text, error, size);
    if (!storage)
        return 0;
    char *cursor = storage, *line;
    unsigned char seen[COUNT(plasma_fields) + COUNT(option_fields) + COUNT(obsolete_options)] = { 0 };
    unsigned long line_number = 0;
    size_t prefix_length = prefix ? strlen(prefix) : 0;
    int ok = 0;
    plasma_default(plasma); /* 旧配置没有 solve_mode 时沿用有限碰撞。 */
    options_default(options);
    while ((line = next_line(&cursor))) {
        ++line_number;
        if (prefix) {
            if (strncmp(line, prefix, prefix_length))
                continue;
            line = trim(line + prefix_length);
        } else if (*line == '#')
            continue;
        if (!*line)
            continue;
        char *equals = strchr(line, '=');
        if (!equals) {
            if (error && size)
                snprintf(error, size, "invalid plasma line %lu", line_number);
            goto done;
        }
        *equals = '\0';
        char *key = trim(line);
        void *target = plasma;
        const StoredField *field = find_field(plasma_fields, COUNT(plasma_fields), key);
        size_t index;
        if (field)
            index = (size_t)(field - plasma_fields);
        else {
            field = find_field(option_fields, COUNT(option_fields), key);
            target = options;
            index = field ? COUNT(plasma_fields) + (size_t)(field - option_fields) : 0;
        }
        if (!field) {
            size_t old;
            for (old = 0; old < COUNT(obsolete_options); ++old)
                if (!strcmp(key, obsolete_options[old].name))
                    break;
            if (old < COUNT(obsolete_options)) {
                index = COUNT(plasma_fields) + COUNT(option_fields) + old;
                int integer;
                double real;
                if (seen[index] || !(obsolete_options[old].integer
                        ? parse_integer(trim(equals + 1), &integer)
                        : parse_number(trim(equals + 1), &real))) {
                    io_fail(error, size, "invalid or duplicate obsolete plasma option");
                    goto done;
                }
                seen[index] = 1;
                continue;
            }
        }
        if (!field || seen[index]) {
            if (error && size)
                snprintf(error,
                    size,
                    "invalid or duplicate plasma key '%s' on line %lu",
                    key,
                    line_number);
            goto done;
        }
        seen[index] = 1;
        if (!read_field(target, field, trim(equals + 1))) {
            if (error && size)
                snprintf(error, size, "invalid plasma key '%s' on line %lu", key, line_number);
            goto done;
        }
    }
    if (require_all) {
        for (size_t i = 0; i < COUNT(plasma_fields); ++i)
            if (!seen[i] && strcmp(plasma_fields[i].name, "solve_mode")) {
                io_fail(error, size, "plasma configuration is incomplete");
                goto done;
            }
        for (size_t i = 0; i < COUNT(option_fields); ++i)
            if (!seen[COUNT(plasma_fields) + i]) {
                io_fail(error, size, "plasma configuration is incomplete");
                goto done;
            }
    }
    ok = plasma_validate(plasma, error, size) && options_validate(options, error, size);
    if (ok)
        plasma_normalize_legacy_mode(plasma);
done:
    free(storage);
    return ok;
}

/* 用局部材料/采样候选值解析，全部成功才同时提交两个输出结构。 */
int plasma_config_parse_text(
    const char *text, Plasma *plasma, Options *options, char *error, size_t size)
{
    Plasma candidate;
    Options candidate_options;
    if (!plasma || !options)
        return io_fail(error, size, "plasma destination is null");
    if (!parse_plasma_lines(text, &candidate, &candidate_options, NULL, 1, error, size))
        return 0;
    *plasma = candidate;
    *options = candidate_options;
    return 1;
}

/* 读取材料配置并复用文本事务解析；无论成功失败均释放文件文本。 */
int plasma_config_load(const char *path, Plasma *plasma, Options *options, char *error, size_t size)
{
    char *text;
    if (!read_file_text(path, &text, error, size))
        return 0;
    int ok = plasma_config_parse_text(text, plasma, options, error, size);
    free(text);
    return ok;
}

/* 验证输入后在局部副本规范化旧模式；保存不修改调用者的有限碰撞输入值。 */
int plasma_config_save(
    const char *path, const Plasma *plasma, const Options *options, char *error, size_t size)
{
    if (!path || !*path || !plasma || !options)
        return io_fail(error, size, "plasma path or source is invalid");
    if (!plasma_validate(plasma, error, size) || !options_validate(options, error, size))
        return 0;
    Plasma normalized = *plasma;
    plasma_normalize_legacy_mode(&normalized);
    plasma = &normalized;
    char temporary[4096];
    FILE *stream = open_temporary(path, temporary, error, size);
    if (!stream)
        return 0;
    int ok = fputs("# CylinderRF cold plasma v1; SI units; ion_mass_u in u; phi in radians\n",
                 stream) >= 0 &&
             write_fields(stream, plasma, plasma_fields, COUNT(plasma_fields), "") &&
             write_fields(stream, options, option_fields, COUNT(option_fields), "");
    return finish_save(stream, temporary, path, ok, error, size);
}

/* 解析统一工程版本、介质提示和公共k扫描设置；只有元数据完整才视作统一工程。 */
static int unified_metadata(
    const char *text, WorkbenchProject *w, int *has_unified, char *error, size_t size)
{
    char *storage = copy_text(text, error, size);
    if (!storage)
        return 0;
    char *cursor = storage, *line;
    int version = 0, enabled = 0, got_version = 0, got_enabled = 0;
    int got_kmin = 0, got_kmax = 0, got_nk = 0;
    while ((line = next_line(&cursor))) {
        if (strncmp(line, "# ", 2))
            continue;
        char *key = line + 2;
        char *equals = strchr(key, '=');
        if (!equals)
            continue;
        *equals = '\0';
        key = trim(key);
        char *value = trim(equals + 1);
        int valid = 1;
        if (!strcmp(key, "workbench_version")) {
            valid = !got_version && parse_integer(value, &version);
            got_version = 1;
        } else if (!strcmp(key, "workbench_plasma_enabled")) {
            valid = !got_enabled && parse_integer(value, &enabled);
            got_enabled = 1;
        } else if (!strcmp(key, "vacuum_kmin")) {
            valid = !got_kmin && parse_number(value, &w->kmin);
            got_kmin = 1;
        } else if (!strcmp(key, "vacuum_kmax")) {
            valid = !got_kmax && parse_number(value, &w->kmax);
            got_kmax = 1;
        } else if (!strcmp(key, "vacuum_nk")) {
            valid = !got_nk && parse_integer(value, &w->nk);
            got_nk = 1;
        }
        if (!valid) {
            if (error && size)
                snprintf(error, size, "invalid %s", key);
            free(storage);
            return 0;
        }
    }
    free(storage);
    *has_unified = got_version || got_enabled || got_kmin || got_kmax || got_nk;
    if (*has_unified && !(got_version && got_enabled && got_kmin && got_kmax && got_nk))
        return io_fail(error, size, "workbench project metadata is incomplete");
    if (*has_unified && version != 1)
        return io_fail(error, size, "unsupported workbench project version");
    if (*has_unified && enabled != 0 && enabled != 1)
        return io_fail(error, size, "workbench plasma flag is invalid");
    if (*has_unified)
        w->plasma_enabled = enabled;
    return 1;
}

/* 检测带材料字段却缺统一工程版本的损坏文件，避免错误地按纯几何默认值加载。 */
static int has_plasma_prefix(const char *text)
{
    for (const char *line = text; line && *line;) {
        if (!strncmp(line + strspn(line, " \t\r"), "# plasma.", 9))
            return 1;
        const char *end = strchr(line, '\n');
        line = end ? end + 1 : NULL;
    }
    return 0;
}

/* Optional block: old projects keep deterministic defaults. Unknown or duplicate
 * ray keys are errors, so a misspelled launcher never silently changes a run. */
/* 读取显式发射与积分配置；不含该块的旧工程保留默认值，已知键重复/非法则失败。 */
static int parse_ray_lines(const char *text,RfRayConfig *ray,char *error,size_t n)
{
    char *storage=copy_text(text,error,n);if(!storage)return 0;
    char *cursor=storage,*line;unsigned char seen[COUNT(ray_fields)]={0};int ok=1;
    while((line=next_line(&cursor))) {
        if(strncmp(line,"# ray.",6))continue;
        char *key=line+6,*eq=strchr(key,'=');
        if(!eq){ok=io_fail(error,n,"invalid ray project field");break;}
        *eq=0;key=trim(key);
        const StoredField *field=find_field(ray_fields,COUNT(ray_fields),key);
        if(!field || seen[field-ray_fields] || !read_field(ray,field,trim(eq+1))) {
            ok=io_fail(error,n,"unknown, duplicate or invalid ray project field");break;
        }
        seen[field-ray_fields]=1;
    }
    free(storage);return ok;
}
/* 先解析工程，再读取内嵌材料或旧配套配置；只有不存在的basename才尝试.rfproj。
 * 最终对耦合几何/材料/扫描/射线状态统一校验。 */
static int workbench_load_candidate(const char *path, WorkbenchProject *w, char *error, size_t size)
{
    if (!path || !*path || !w)
        return io_fail(error, size, "workbench path or destination is invalid");
    workbench_default(w);
    size_t length = strlen(path);
    int direct_config = length >= 13 && !strcmp(path + length - 13, ".plasmaconfig");
    size_t project_length = direct_config ? length - 13 : length;
    char project_path[4096], sidecar[4096];
    if (project_length >= sizeof(project_path))
        return io_fail(error, size, "workbench path is too long");
    memcpy(project_path, path, project_length);
    project_path[project_length] = '\0';
    char *text;
    if (!read_file_text(project_path, &text, error, size)) {
        /* 只为不存在的 basename 尝试 .rfproj；已有文件损坏/无权读时不能换文件。 */
        if (rf_access_exists_utf8(project_path) == 0 || errno != ENOENT)
            return 0;
        /* 兼容裸 basename 和 basename.plasmaconfig 配对 basename.rfproj。 */
        if (project_length + 7 >= sizeof(project_path))
            return 0;
        strcat(project_path, ".rfproj");
        if (!read_file_text(project_path, &text, error, size))
            return 0;
    }
    int unified = 0;
    int ok = project_parse_text(text, &w->project, error, size) &&
             unified_metadata(text, w, &unified, error, size) &&
             parse_ray_lines(text,&w->ray,error,size);
    if (ok && unified)
        ok = parse_plasma_lines(text, &w->plasma, &w->options, "# plasma.", 1, error, size);
    if (ok && !unified && has_plasma_prefix(text))
        ok = io_fail(error, size, "workbench project is missing version metadata");
    free(text);
    if (!ok)
        return 0;
    /* 用户显式选择配置时必须读取它；统一工程只是其余状态的底板，
     * 不能因工程已有材料块就忽略指定配置，更不能忽略缺失/损坏配置。 */
    if (direct_config) {
        if (!plasma_config_load(path, &w->plasma, &w->options, error, size))
            return 0;
        w->plasma_enabled = 1;
        return workbench_state_validate(w, error, size);
    }
    if (unified)
        return workbench_state_validate(w, error, size);
    int written = direct_config
                      ? snprintf(sidecar, sizeof(sidecar), "%s", path)
                      : snprintf(sidecar, sizeof(sidecar), "%s.plasmaconfig", project_path);
    if (written >= (int)sizeof(sidecar))
        return io_fail(error, size, "workbench path is too long");
    if (rf_access_exists_utf8(sidecar) == 0) {
        if (!plasma_config_load(sidecar, &w->plasma, &w->options, error, size))
            return 0;
        w->plasma_enabled = 1;
    }
    return workbench_state_validate(w, error, size);
}

/* 对外完整工程加载采用候选事务，读文件、解析或校验失败均保留原状态。 */
int workbench_load(const char *path, WorkbenchProject *w, char *error, size_t size)
{
    WorkbenchProject candidate;
    if (!w)
        return io_fail(error, size, "workbench destination is null");
    if (!workbench_load_candidate(path, &candidate, error, size))
        return 0;
    *w = candidate;
    return 1;
}

/* 以当前格式保存全部有效状态和显式射线起点；规范化仅作用于局部副本。 */
int workbench_save(const char *path, const WorkbenchProject *w, char *error, size_t size)
{
    if (!path || !*path || !w)
        return io_fail(error, size, "workbench path or source is invalid");
    if (!workbench_state_validate(w, error, size))
        return 0;
    WorkbenchProject normalized = *w;
    plasma_normalize_legacy_mode(&normalized.plasma);
    w = &normalized;
    char temporary[4096];
    FILE *stream = open_temporary(path, temporary, error, size);
    if (!stream)
        return 0;
    int ok =
        fputs(
            "# Cylinder RF Workbench project v8; C11; antenna numbering 1-17; SI units, MHz and degrees explicitly named\n",
            stream) >= 0 &&
        write_fields(stream, &w->project, project_fields, COUNT(project_fields), "") &&
        fprintf(stream,
            "# workbench_version=1\n# workbench_plasma_enabled=%d\n# vacuum_kmin=%.17g\n# "
            "vacuum_kmax=%.17g\n# vacuum_nk=%d\n",
            w->plasma_enabled,
            w->kmin,
            w->kmax,
            w->nk) >= 0 &&
        write_fields(stream, &w->plasma, plasma_fields, COUNT(plasma_fields), "# plasma.") &&
        write_fields(stream, &w->options, option_fields, COUNT(option_fields), "# plasma.") &&
        write_fields(stream, &w->ray, ray_fields, COUNT(ray_fields), "# ray.");
    return finish_save(stream, temporary, path, ok, error, size);
}
