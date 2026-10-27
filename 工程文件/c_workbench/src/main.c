#include "project_io.h"
#include "geometry.h"
#include "vacuum.h"
#include "plasma.h"
#include "plot.h"
#include "spatial.h"
#include "branches.h"

#ifdef RF_HAVE_TK
#include "rf_tk_gui.h"
#endif

#include <complex.h>
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_json_string(const char *value)
{
    const unsigned char *p = (const unsigned char *)(value != NULL ? value : "");
    putchar('"');
    for (; *p; ++p) {
        if (*p == '"' || *p == '\\') {
            putchar('\\');
            putchar(*p);
        } else if (*p == '\n')
            fputs("\\n", stdout);
        else if (*p == '\r')
            fputs("\\r", stdout);
        else if (*p == '\t')
            fputs("\\t", stdout);
        else
            putchar(*p);
    }
    putchar('"');
}

static int load_state(const char *path, WorkbenchProject *w, char *error, size_t error_size)
{
    if (path != NULL)
        return workbench_load(path, w, error, error_size);
    workbench_default(w);
    return workbench_state_validate(w, error, error_size);
}

/* 命令行与项目文件同样严格解析，不能把 abc 当成 0、把 NaN 当成未传参数。 */
static int cli_real(const char *text, double *out)
{
    char *end;
    double value;
    errno = 0;
    value = strtod(text, &end);
    if (end == text || *end || errno == ERANGE || !isfinite(value)) {
        fprintf(stderr, "无效数值: %s\n", text);
        return 0;
    }
    *out = value;
    return 1;
}
static int cli_int(const char *text, int *out)
{
    double value;
    if (!cli_real(text, &value))
        return 0;
    if (value < INT_MIN || value > INT_MAX || floor(value) != value) {
        fprintf(stderr, "无效整数: %s\n", text);
        return 0;
    }
    *out = (int)value;
    return 1;
}
static int check_command(const WorkbenchProject *w)
{
    printf("{\"language\":\"C\",\"standard\":\"C11\",\"project_valid\":true,\"plasma_valid\":true,"
           "\"options_valid\":true,\"plasma_enabled\":%s,\"antenna_type\":%d,\"frequency_mhz\":%."
           "17g,\"nk\":%d,\"radial_cells\":%d}\n",
        w->plasma_enabled ? "true" : "false",
        w->project.type,
        w->project.frequency_mhz,
        w->nk,
        w->project.radial_cells);
    return 0;
}

static void csv_string(FILE *f, const char *text)
{
    fputc('"', f);
    for (; *text; ++text) {
        if (*text == '"')
            fputc('"', f);
        fputc(*text, f);
    }
    fputc('"', f);
}
/* 一行对应一个 (kz,r) 节点；source-only 每个 kz 一行且不伪造场/残差。 */
static int scan_csv(const RfVacuumScan *s, const char *path)
{
    FILE *f;
    size_t i, j;
    int ok = 1;
    if (!path || !*path)
        return 1;
    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "无法写入 %s: %s\n", path, strerror(errno));
        return 0;
    }
    if (fputs("kz_rad_per_m,valid,source_r_re,source_r_im,source_phi_re,source_phi_im,source_z_re,"
              "source_z_im,backward_error,r_m,Er_re,Er_im,Ephi_re,Ephi_im,Ez_re,Ez_im,total_V,"
              "error\n",
            f) < 0)
        ok = 0;
    for (i = 0; i < s->nk; ++i)
        for (j = 0; j < (s->electric ? s->nr : 1U); ++j) {
            if (fprintf(f,
                    "%.17g,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,",
                    s->k[i],
                    s->valid[i],
                    creal(s->current[3 * i]),
                    cimag(s->current[3 * i]),
                    creal(s->current[3 * i + 1]),
                    cimag(s->current[3 * i + 1]),
                    creal(s->current[3 * i + 2]),
                    cimag(s->current[3 * i + 2]),
                    s->backward_error[i]) < 0)
                ok = 0;
            if (s->electric) {
                const double complex *e = s->electric + 3 * i * s->nr;
                if (fprintf(f,
                        "%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,",
                        s->project.radius * j / (s->nr - 1U),
                        creal(e[j]),
                        cimag(e[j]),
                        creal(e[s->nr + j]),
                        cimag(e[s->nr + j]),
                        creal(e[2 * s->nr + j]),
                        cimag(e[2 * s->nr + j]),
                        s->total[i * s->nr + j]) < 0)
                    ok = 0;
            } else if (fputs(",,,,,,,,", f) < 0)
                ok = 0;
            csv_string(f, s->reason[i] ? s->reason[i] : "");
            fputc('\n', f);
        }
    if (ferror(f))
        ok = 0;
    if (fclose(f))
        ok = 0;
    if (!ok)
        fprintf(stderr, "扫描 CSV 写入失败: %s\n", path);
    return ok;
}

static int geometry_command(const WorkbenchProject *w, const char *obj)
{
    Geometry g = { 0 };
    char error[256] = { 0 };
    geometry_init(&g);
    if (!geometry_build(&w->project, &g, error, sizeof(error))) {
        fprintf(stderr, "几何生成失败: %s\n", error);
        geometry_free(&g);
        return 1;
    }
    if (obj != NULL && !geometry_export_obj(&g, obj, error, sizeof(error))) {
        fprintf(stderr, "OBJ 导出失败: %s\n", error);
        geometry_free(&g);
        return 1;
    }
    printf("{\"geometry_valid\":true,\"segments\":%zu,\"current_balance\":%.17g,\"band_width\":%."
           "17g,\"obj\":",
        geometry_count(&g),
        geometry_current_balance(&g),
        g.band_width);
    print_json_string(obj != NULL ? obj : "");
    puts("}");
    geometry_free(&g);
    return 0;
}

/* 命令行解析只产生选项；载入、覆盖校验和各计算命令分别执行。 */
typedef struct {
    int check, gui, do_vacuum, do_scan, do_plasma, do_geometry;
    int fields, save_default, help, do_spatial, do_branches;
    const char *project_path, *plasma_path, *output, *obj, *save_path, *spectrum, *rays;
    double kmin, kmax, density, magnetic_field, frequency, kz, zmin, zmax;
    int nk, radial_cells, m_override, solve_mode, nz, nr;
    RfRayConfig ray;
    int has_ray;
} CliOptions;

static int parse_arguments(int argc, char **argv, CliOptions *options)
{
    *options = (CliOptions){ .fields = 1,
        .kmin = NAN,
        .kmax = NAN,
        .density = NAN,
        .magnetic_field = NAN,
        .frequency = NAN,
        .kz = NAN,
        .zmin = NAN,
        .zmax = NAN,
        .m_override = INT_MIN,
        .solve_mode = -1 };
    options->ray=(RfRayConfig){.r0=NAN,.z0=NAN,.phi0_deg=NAN,.tolerance=NAN,
        .max_step=NAN,.max_path=NAN,.direction=INT_MIN,.max_steps=0};
    int i;
    if (argc == 1) {
        const char *program = strrchr(argv[0], '/');
        if (!strcmp(program ? program + 1 : argv[0], "CylinderRF"))
            options->gui = 1;
    }
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--check"))
            options->check = 1;
        else if (!strcmp(argv[i], "--gui"))
            options->gui = 1;
        else if (!strcmp(argv[i], "--vacuum") || !strcmp(argv[i], "--run"))
            options->do_vacuum = 1;
        else if (!strcmp(argv[i], "--spatial") || !strcmp(argv[i], "--impedance")) {
            options->do_spatial = options->do_plasma = 1;
        } else if (!strcmp(argv[i], "--branches"))
            options->do_branches = options->do_plasma = 1;
        else if (!strcmp(argv[i], "--rays")) {
            if (++i >= argc) { fputs("--rays 需要 CSV 路径\n", stderr); return 2; }
            options->rays = argv[i];
        } else if (!strncmp(argv[i], "--ray-", 6)) {
            const char *flag=argv[i];options->has_ray=1;
            if(++i>=argc)return 2;
            double *target=NULL;
            if(!strcmp(flag,"--ray-r"))target=&options->ray.r0;
            else if(!strcmp(flag,"--ray-z"))target=&options->ray.z0;
            else if(!strcmp(flag,"--ray-phi"))target=&options->ray.phi0_deg;
            else if(!strcmp(flag,"--ray-tol"))target=&options->ray.tolerance;
            else if(!strcmp(flag,"--ray-step"))target=&options->ray.max_step;
            else if(!strcmp(flag,"--ray-length"))target=&options->ray.max_path;
            else if(!strcmp(flag,"--ray-direction")) {
                if(!strcmp(argv[i],"both"))options->ray.direction=0;
                else if(!strcmp(argv[i],"inward"))options->ray.direction=-1;
                else if(!strcmp(argv[i],"outward"))options->ray.direction=1;
                else return 2;
            } else if(!strcmp(flag,"--ray-max-steps")) {
                if(!cli_int(argv[i],&options->ray.max_steps) || options->ray.max_steps<10)return 2;
            } else return 2;
            if(target && !cli_real(argv[i],target))return 2;
        } else if (!strcmp(argv[i], "--plasma"))
            options->do_plasma = 1;
        else if (!strcmp(argv[i], "--plasma-resonance")) {
            options->do_plasma = 1;
            options->solve_mode = RF_PLASMA_LIMITING_ABSORPTION;
        } else if (!strcmp(argv[i], "--plasma-suite") || !strcmp(argv[i], "--plasma-scan")) {
            fputs("该批处理命令尚未提供；单点 Maxwell 求解请使用 "
                  "--plasma，波数扫描可使用图形界面。\n",
                stderr);
            return 2;
        } else if (!strcmp(argv[i], "--geometry"))
            options->do_geometry = 1;
        else if (!strcmp(argv[i], "--scan")) {
            options->do_vacuum = 1;
            options->do_scan = 1;
        } else if (!strcmp(argv[i], "--source-scan")) {
            options->do_vacuum = 1;
            options->do_scan = 1;
            options->fields = 0;
        } else if (!strcmp(argv[i], "--source-only"))
            options->fields = 0;
        else if (!strcmp(argv[i], "--save-default")) {
            options->save_default = 1;
            if (++i >= argc) {
                fputs("--save-default 需要路径\n", stderr);
                return 2;
            }
            options->save_path = argv[i];
        } else if (!strcmp(argv[i], "--project")) {
            if (++i >= argc) {
                fputs("--project 需要路径\n", stderr);
                return 2;
            }
            options->project_path = argv[i];
        } else if (!strcmp(argv[i], "--plasma-config")) {
            if (++i >= argc) {
                fputs("--plasma-config 需要路径\n", stderr);
                return 2;
            }
            options->plasma_path = argv[i];
        } else if (!strcmp(argv[i], "--output") || !strcmp(argv[i], "--csv")) {
            if (++i >= argc) {
                fputs("--output 需要路径\n", stderr);
                return 2;
            }
            options->output = argv[i];
        } else if (!strcmp(argv[i], "--obj")) {
            if (++i >= argc) {
                fputs("--obj 需要路径\n", stderr);
                return 2;
            }
            options->obj = argv[i];
            options->do_geometry = 1;
        } else if (!strcmp(argv[i], "--spectrum")) {
            if (++i >= argc)
                return 2;
            options->spectrum = argv[i];
        } else if (!strcmp(argv[i], "--zmin")) {
            if (++i >= argc || !cli_real(argv[i], &options->zmin))
                return 2;
        } else if (!strcmp(argv[i], "--zmax")) {
            if (++i >= argc || !cli_real(argv[i], &options->zmax))
                return 2;
        } else if (!strcmp(argv[i], "--zpoints")) {
            if (++i >= argc || !cli_int(argv[i], &options->nz) || options->nz < 3 ||
                options->nz > 1001)
                return 2;
        } else if (!strcmp(argv[i], "--rpoints")) {
            if (++i >= argc || !cli_int(argv[i], &options->nr) || options->nr < 17 ||
                options->nr > 2049)
                return 2;
        } else if (!strcmp(argv[i], "--kmin")) {
            if (++i >= argc || !cli_real(argv[i], &options->kmin))
                return 2;
        } else if (!strcmp(argv[i], "--kmax")) {
            if (++i >= argc || !cli_real(argv[i], &options->kmax))
                return 2;
        } else if (!strcmp(argv[i], "--kpoints") || !strcmp(argv[i], "--nk")) {
            if (++i >= argc || !cli_int(argv[i], &options->nk) || options->nk < 3 ||
                options->nk > 401)
                return 2;
        } else if (!strcmp(argv[i], "--density")) {
            if (++i >= argc || !cli_real(argv[i], &options->density))
                return 2;
        } else if (!strcmp(argv[i], "--field")) {
            if (++i >= argc || !cli_real(argv[i], &options->magnetic_field))
                return 2;
        } else if (!strcmp(argv[i], "--frequency")) {
            if (++i >= argc || !cli_real(argv[i], &options->frequency))
                return 2;
        } else if (!strcmp(argv[i], "--kz")) {
            if (++i >= argc || !cli_real(argv[i], &options->kz))
                return 2;
        } else if (!strcmp(argv[i], "--m")) {
            if (++i >= argc || !cli_int(argv[i], &options->m_override) ||
                options->m_override < -16 || options->m_override > 16)
                return 2;
        } else if (!strcmp(argv[i], "--radial-cells")) {
            if (++i >= argc || !cli_int(argv[i], &options->radial_cells) ||
                options->radial_cells < 64 || options->radial_cells > 2048)
                return 2;
        } else if (!strcmp(argv[i], "--plasma-method")) {
            if (++i >= argc)
                return 2;
            if (!strcmp(argv[i], "collisionless"))
                options->solve_mode = RF_PLASMA_DIRECT_RESONANCE;
            else if (!strcmp(argv[i], "finite"))
                options->solve_mode = RF_PLASMA_FINITE_COLLISION;
            else if (!strcmp(argv[i], "limiting") || !strcmp(argv[i], "resonance"))
                options->solve_mode = RF_PLASMA_LIMITING_ABSORPTION;
            else {
                fputs("--plasma-method 支持 finite、collisionless 或 limiting。\n", stderr);
                return 2;
            }
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            puts("用法: rf-workbench --gui | --check [--project PATH] | --vacuum [--project PATH] "
                 "[--output CSV] | --scan [--kmin K0 --kmax K1 --kpoints N] | --plasma [--project "
                 "PATH] [--density N --field B --frequency MHz --m M --kz K] [--plasma-method "
                 "finite|collisionless|limiting] [--output CSV] | --geometry --obj PATH | "
                 "--save-default PATH | --spatial (or --impedance) [--kmin K0 --kmax K1 --nk N] "
                 "[--zmin Z0 --zmax Z1 --zpoints NZ --rpoints NR] [--output MAP.csv] [--spectrum "
                 "Z.csv] | --branches [--frequency MHz --m M --kz K] [--output branches.csv] "
                 "[--rays rays.csv] [--ray-r R --ray-z Z --ray-phi DEG] [--ray-direction both|inward|outward] [--ray-tol T --ray-step M --ray-length M --ray-max-steps N]");
            options->help = 1;
            return 0;
        } else {
            fprintf(stderr, "未知参数: %s\n", argv[i]);
            return 2;
        }
    }
    return 0;
}

static int has_overrides(const CliOptions *options)
{
    return options->plasma_path || isfinite(options->kmin) || isfinite(options->kmax) ||
           isfinite(options->density) || isfinite(options->magnetic_field) ||
           isfinite(options->frequency) || isfinite(options->kz) || options->nk ||
           options->radial_cells || options->nz || options->nr || isfinite(options->zmin) ||
           isfinite(options->zmax) || options->do_spatial || options->do_branches || options->rays || options->spectrum ||
           options->m_override != INT_MIN || options->solve_mode >= 0 || options->has_ray;
}

static int load_command_state(const CliOptions *options, WorkbenchProject *w)
{
    char error[256] = { 0 };
    if (!load_state(options->project_path, w, error, sizeof(error))) {
        fprintf(stderr, "项目加载失败: %s\n", error);
        return 0;
    }
    if (options->plasma_path &&
        !plasma_config_load(options->plasma_path, &w->plasma, &w->options, error, sizeof(error))) {
        fprintf(stderr, "等离子体配置加载失败: %s\n", error);
        return 0;
    }
    if (isfinite(options->kmin))
        w->kmin = options->kmin;
    if (isfinite(options->kmax))
        w->kmax = options->kmax;
    if (options->nk)
        w->nk = options->nk;
    if (isfinite(options->density))
        w->plasma.density = options->density;
    if (isfinite(options->magnetic_field))
        w->plasma.magnetic_field = options->magnetic_field;
    if (isfinite(options->frequency))
        w->project.frequency_mhz = options->frequency;
    if (isfinite(options->kz))
        w->project.kz = options->kz;
    if (options->m_override != INT_MIN)
        w->project.selected_m = options->m_override;
    if (options->radial_cells)
        w->project.radial_cells = options->radial_cells;
    if (options->solve_mode >= 0)
        w->plasma.solve_mode = options->solve_mode;
    if (isfinite(options->zmin))
        w->options.zmin = options->zmin;
    if (isfinite(options->zmax))
        w->options.zmax = options->zmax;
    if (options->nz)
        w->options.nz = options->nz;
    if (options->nr)
        w->options.nr = options->nr;
    if(isfinite(options->ray.r0))w->ray.r0=options->ray.r0;
    if(isfinite(options->ray.z0))w->ray.z0=options->ray.z0;
    if(isfinite(options->ray.phi0_deg))w->ray.phi0_deg=options->ray.phi0_deg;
    if(isfinite(options->ray.tolerance))w->ray.tolerance=options->ray.tolerance;
    if(isfinite(options->ray.max_step))w->ray.max_step=options->ray.max_step;
    if(isfinite(options->ray.max_path))w->ray.max_path=options->ray.max_path;
    if(options->ray.direction!=INT_MIN)w->ray.direction=options->ray.direction;
    if(options->ray.max_steps)w->ray.max_steps=options->ray.max_steps;
    if (options->do_plasma)
        w->plasma_enabled = 1;
    if (!workbench_state_validate(w, error, sizeof(error))) {
        fprintf(stderr, "参数校验失败: %s\n", error);
        return 0;
    }
    return 1;
}

static int svg_path(const char *path)
{
    size_t length = strlen(path);
    return length >= 4 && !strcmp(path + length - 4, ".svg");
}

static int branches_command(const WorkbenchProject *w, const CliOptions *options)
{
    RfBranchSolution s = {0};
    char error[256] = {0};
    if (options->output && svg_path(options->output)) {
        fputs("分支 CLI 使用 CSV；SVG 曲线可在图形界面导出。\n", stderr);
        return 1;
    }
    if (!rf_branches_solve_config(&w->project, &w->plasma, &w->ray, &s, error, sizeof(error))) {
        fprintf(stderr, "分支分析失败: %s\n", error);
        return 1;
    }
    int ok = (!options->output || rf_branches_export_csv(&s, options->output, error, sizeof(error))) &&
        (!options->rays || rf_branches_export_rays(&s, options->rays, error, sizeof(error)));
    if (ok) printf("%s\n", s.note);
    else fprintf(stderr, "分支导出失败: %s\n", error);
    rf_branches_free(&s);
    return !ok;
}

static int plasma_command(const WorkbenchProject *w, const char *output)
{
    RfPlasmaSolution solution = { 0 };
    char error[256] = { 0 };
    if (!rf_plasma_solve(&w->project, &w->plasma, &solution, error, sizeof(error))) {
        fprintf(stderr, "等离子体求解失败: %s\n", error);
        return 1;
    }
    int ok = 1;
    if (output)
        ok = svg_path(output) ? rf_plasma_export_svg(&solution, output, error, sizeof(error))
                              : rf_plasma_export_csv(&solution, output, error, sizeof(error));
    if (ok)
        printf("%s\n负载阻抗谱 dZ/dk_z = %.9g%+.9gi Ω m（工程 exp(+iωt)，当前 m、k_z）\n",
            solution.message,
            creal(solution.fem->load_impedance),
            cimag(solution.fem->load_impedance));
    else
        fprintf(stderr, "等离子体导出失败: %s\n", error);
    rf_plasma_solution_free(&solution);
    return !ok;
}

static int vacuum_command(const WorkbenchProject *w, const char *output)
{
    RfVacuumSolution solution = { 0 };
    char error[256] = { 0 };
    if (!rf_vacuum_solve(&w->project, &solution, error, sizeof(error))) {
        fprintf(stderr, "真空单点求解失败: %s\n", error);
        return 1;
    }
    int ok = 1;
    if (output)
        ok = svg_path(output) ? rf_vacuum_export_svg(&solution, output, error, sizeof(error))
                              : rf_vacuum_export_csv(&solution, output, error, sizeof(error));
    if (ok)
        printf("%s\n负载阻抗谱 dZ/dk_z = %.9g%+.9gi Ω m（工程 exp(+iωt)，当前 m、k_z）\n",
            solution.message,
            creal(solution.fem->load_impedance),
            cimag(solution.fem->load_impedance));
    else if (*error)
        fprintf(stderr, "真空导出失败: %s\n", error);
    rf_vacuum_solution_free(&solution);
    return !ok;
}

static int spatial_command(const WorkbenchProject *w, const CliOptions *options)
{
    RfSpatialSolution s = { 0 };
    RfSpatialConfig config = rf_spatial_config(w);
    char error[512] = { 0 };
    if (options->output && svg_path(options->output)) {
        fputs("空间 CLI 输出使用 .csv；SVG 热图可在图形界面导出。\n", stderr);
        return 1;
    }
    if (!rf_spatial_solve(&w->project, &w->plasma, &config, &s, error, sizeof(error))) {
        fprintf(stderr, "空间与阻抗求解失败: %s\n", error);
        return 1;
    }
    int ok =
        (!options->output || rf_spatial_export_csv(&s, options->output, error, sizeof(error))) &&
        (!options->spectrum ||
            rf_spatial_export_spectrum(&s, options->spectrum, error, sizeof(error)));
    if (ok)
        printf("{\"selected_m\":%d,\"nk\":%zu,\"nr\":%zu,\"nz\":%zu,"
               "\"window_power_W\":%.17g,\"band_absorbed_W\":%.17g,\"band_source_W\":%.17g,"
               "\"R_ohm\":%.17g,\"X_ohm\":%.17g,\"period_m\":%.17g,"
               "\"power_balance_error\":%.17g,\"radial_check_error\":%.17g,"
               "\"edge_ratio\":%.17g,\"band_edge_fraction\":%.17g,\"impedance_edge_fraction\":%."
               "17g}\n",
            s.project.selected_m,
            config.nk,
            config.nr,
            config.nz,
            s.window_power,
            s.spectral_power,
            s.source_power,
            creal(s.impedance),
            cimag(s.impedance),
            s.period,
            s.power_balance_error,
            s.radial_check_error,
            s.edge_ratio,
            s.band_edge_fraction,
            s.impedance_edge_fraction);
    else
        fprintf(stderr, "空间导出失败: %s\n", error);
    rf_spatial_free(&s);
    return !ok;
}

static int scan_command(const WorkbenchProject *w, int fields, const char *output)
{
    RfVacuumScan scan = { 0 };
    char error[256] = { 0 };
    if (!rf_vacuum_scan(
            &w->project, w->kmin, w->kmax, (size_t)w->nk, fields, &scan, error, sizeof(error))) {
        fprintf(stderr, "真空扫描失败: %s\n", error);
        return 1;
    }
    if (output && !scan_csv(&scan, output)) {
        rf_vacuum_scan_free(&scan);
        return 1;
    }
    size_t valid = 0;
    for (size_t i = 0; i < scan.nk; ++i)
        valid += (size_t)scan.valid[i];
    printf("真空扫描完成 · %zu 点 · 有效 %zu 点\n", scan.nk, valid);
    rf_vacuum_scan_free(&scan);
    return valid == 0;
}

static int save_default_command(const char *path)
{
    WorkbenchProject w;
    char error[256] = { 0 };
    workbench_default(&w);
    if (workbench_save(path, &w, error, sizeof(error)))
        return 0;
    fprintf(stderr, "保存失败: %s\n", error);
    return 1;
}

int main(int argc, char **argv)
{
    CliOptions options;
    int status = parse_arguments(argc, argv, &options);
    if (status || options.help)
        return status;
    if (options.rays && !options.do_branches) {
        fputs("--rays 必须与 --branches 一起使用。\n", stderr); return 2;
    }
    if(options.has_ray && !options.do_branches) {
        fputs("--ray-* 必须与 --branches 一起使用。\n",stderr);return 2;
    }
    if (options.save_default)
        return save_default_command(options.save_path);
    if (options.gui) {
        if (has_overrides(&options)) {
            fputs("GUI 参数请使用 --project 载入，或在左侧修改；数值覆盖选项仅用于 CLI 计算。\n",
                stderr);
            return 2;
        }
#ifdef RF_HAVE_TK
        return rf_tk_gui_run(argc, argv);
#else
        fputs("当前构建未启用 Tcl/Tk GUI。请安装 Tcl/Tk 开发文件，或设置 TCL_CFLAGS/TCL_LIBS。\n",
            stderr);
        return 2;
#endif
    }
    if (!options.check && !options.do_vacuum && !options.do_plasma && !options.do_geometry) {
        fputs("请使用 --gui、--check、--vacuum、--plasma 或 --geometry。\n", stderr);
        return 2;
    }
    WorkbenchProject w;
    if (!load_command_state(&options, &w))
        return 1;
    /* 保持已有命令组合的优先顺序：参数检查、几何、等离子体、真空/扫描。 */
    if (options.check)
        return check_command(&w);
    if (options.do_geometry)
        return geometry_command(&w, options.obj);
    if (options.do_branches)
        return branches_command(&w, &options);
    if (options.do_spatial)
        return spatial_command(&w, &options);
    if (options.do_plasma)
        return plasma_command(&w, options.output);
    if (options.do_scan)
        return scan_command(&w, options.fields, options.output);
    return vacuum_command(&w, options.output);
}
