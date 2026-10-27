#include "rf_tk_gui.h"
#include "project_io.h"
#include "geometry.h"
#include "vacuum.h"
#include "plasma.h"
#include "spatial.h"
#include "full_modes.h"
#include "branches.h"

#include <complex.h>
#include <math.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <tcl.h>
#include <tk.h>

#include "rf_gui_script.h"

static WorkbenchProject rf_state;

typedef struct Field {
    const char *key, *label;
    size_t offset;
    int integer;
    double min, max;
} Field;
#define PF(k, l, t, a, b)                                                                          \
    { #k, l, offsetof(WorkbenchProject, project) + offsetof(Project, k), t, a, b }
#define CF(k, l, t, a, b)                                                                          \
    { "plasma." #k, l, offsetof(WorkbenchProject, plasma) + offsetof(Plasma, k), t, a, b }
#define OF(k, l, t, a, b)                                                                          \
    { "space." #k, l, offsetof(WorkbenchProject, options) + offsetof(Options, k), t, a, b }
#define WF(k, l, t, a, b) { #k, l, offsetof(WorkbenchProject, k), t, a, b }
#define RY(k, l, t, a, b) \
    { "ray." #k, l, offsetof(WorkbenchProject, ray) + offsetof(RfRayConfig, k), t, a, b }
static const Field fields[] = {
    PF(type, "天线类型", 1, 1, 17),
    PF(elements, "阵列单元数", 1, 1, 32),
    PF(current_direction, "电流方向", 1, -1, 1),
    PF(winding_direction, "绕向", 1, -1, 1),
    PF(global_phase_deg, "整体相位", 0, -1e6, 1e6),
    PF(selected_m, "角向模数 m", 1, -16, 16),
    PF(kz, "轴向波数", 0, -2000, 2000),
    PF(radial_cells, "径向网格数", 1, 64, 2048),
    PF(radius, "容器半径", 0, 0.0200001, 100),
    PF(length, "显示长度", 0, 0.0400001, 200),
    PF(frequency_mhz, "频率", 0, 1e-12, 10000),
    PF(current, "峰值电流", 0, 1e-12, 1e6),
    PF(phase_step, "阵列相位差", 0, -1e6, 1e6),
    PF(antenna_radius, "天线半径", 0, 1e-9, 100),
    PF(antenna_length, "天线长度", 0, 1e-9, 200),
    PF(width, "天线宽度", 0, 1e-9, 100),
    PF(spacing, "单元间距", 0, 0, 200),
    PF(turns, "匝数", 0, 0.001, 100),
    PF(azimuth_deg, "方位角", 0, -1e6, 1e6),
    PF(axial_offset, "轴向偏移", 0, -100, 100),
    PF(arc_angle_deg, "圆弧角度", 0, 0.001, 360),
    PF(return_radius, "回流半径", 0, 1e-9, 100),
    PF(band_width, "电流带宽", 0, 0, 100),
    PF(band_samples, "带宽采样数", 1, 1, 32),
    PF(inner_radius, "内半径", 0, 1e-9, 100),
    PF(gap_angle_deg, "分段间隙角", 0, 0.001, 89.999),
    PF(source_mode, "电流谐波 q", 1, 0, 16),
    PF(current_pattern, "电流分布", 1, 0, 1),
    PF(current_phase_gradient, "沿线相位梯度", 0, -2000, 2000),
    CF(radius, "等离子体半径", 0, 1e-9, 100),
    CF(density, "中心电子密度", 0, 0, 1e25),
    CF(edge_ratio, "边缘密度比例", 0, 0, 1),
    CF(magnetic_field, "背景磁场", 0, -100, 100),
    CF(electron_collision, "电子碰撞频率", 0, 0, 1e16),
    CF(ion_collision, "离子碰撞频率", 0, 0, 1e16),
    CF(electron_collision_edge_ratio, "电子边缘碰撞比", 0, 0, 1e6),
    CF(ion_collision_edge_ratio, "离子边缘碰撞比", 0, 0, 1e6),
    CF(ion_mass_u, "离子质量", 0, 0.1, 300),
    CF(ion_charge, "离子电荷数", 1, 1, 92),
    CF(profile, "密度分布", 1, 0, 2),
    CF(solve_mode, "材料响应", 1, 0, 2),
    WF(plasma_enabled, "启用等离子体", 1, 0, 1),
    WF(kmin, "波数下限", 0, -2000, 2000),
    WF(kmax, "波数上限", 0, -2000, 2000),
    WF(nk, "波数采样数", 1, 3, 401),
    OF(m_min, "求和 m 下限", 1, -16, 16),
    OF(m_max, "求和 m 上限", 1, -16, 16),
    OF(phi, "观察方位 φ", 0, -1e6, 1e6),
    OF(zmin, "观察 z 下限", 0, -1000, 1000),
    OF(zmax, "观察 z 上限", 0, -1000, 1000),
    OF(nz, "z 显示点数", 1, 3, 1001),
    OF(nr, "r 显示点数", 1, 17, 2049),
    RY(r0,"射线发射半径",0,1e-9,100),
    RY(z0,"射线发射 z",0,-1000,1000),
    RY(phi0_deg,"射线发射方位角",0,-1e6,1e6),
    RY(direction,"射线能量方向",1,-1,1),
    RY(tolerance,"射线相对容差",0,1e-9,1e-2),
    RY(max_step,"射线最大步长",0,1e-8,100),
    RY(max_path,"射线最大路径",0,1e-6,1e6),
    RY(max_steps,"射线最大步数",1,10,100000),
};

static void put(Tcl_Interp *ip, Tcl_Obj *dict, const char *key, Tcl_Obj *value)
{
    Tcl_DictObjPut(ip, dict, Tcl_NewStringObj(key, -1), value);
}
static void str(Tcl_Interp *ip, Tcl_Obj *dict, const char *key, const char *value)
{
    put(ip, dict, key, Tcl_NewStringObj(value, -1));
}
static void num(Tcl_Interp *ip, Tcl_Obj *dict, const char *key, double value)
{
    put(ip, dict, key, Tcl_NewDoubleObj(value));
}
static int failure(Tcl_Interp *ip, const char *message)
{
    Tcl_SetObjResult(ip, Tcl_NewStringObj(message, -1));
    return TCL_ERROR;
}
static int field_error(Tcl_Interp *ip, const char *key, const char *message)
{
    Tcl_SetErrorCode(ip, "RF", "FIELD", key, NULL);
    return failure(ip, message);
}

static Tcl_Obj *state_dict(Tcl_Interp *ip)
{
    size_t i;
    Tcl_Obj *d = Tcl_NewDictObj();
    for (i = 0; i < sizeof(fields) / sizeof(*fields); ++i) {
        const Field *f = &fields[i];
        const char *p = (const char *)&rf_state + f->offset;
        put(ip,
            d,
            f->key,
            f->integer ? Tcl_NewIntObj(*(const int *)p) : Tcl_NewDoubleObj(*(const double *)p));
    }
    return d;
}

/* Apply to a copy: failed edits and failed file loads never damage the project. */
static int apply_state(Tcl_Interp *ip, Tcl_Obj *dict)
{
    WorkbenchProject next = rf_state;
    size_t i;
    char error[256];
    for (i = 0; i < sizeof(fields) / sizeof(*fields); ++i) {
        const Field *f = &fields[i];
        Tcl_Obj *v = NULL;
        double value;
        int integer = 0;
        Tcl_Obj *key = Tcl_NewStringObj(f->key, -1);
        int found;
        Tcl_IncrRefCount(key);
        found = Tcl_DictObjGet(ip, dict, key, &v);
        Tcl_DecrRefCount(key);
        if (found != TCL_OK)
            return TCL_ERROR;
        if (!v)
            continue;
        if ((f->integer ? Tcl_GetIntFromObj(ip, v, &integer)
                        : Tcl_GetDoubleFromObj(ip, v, &value)) != TCL_OK) {
            snprintf(
                error, sizeof(error), "%s：请输入%s。", f->label, f->integer ? "整数" : "有效数字");
            return field_error(ip, f->key, error);
        }
        if (f->integer)
            value = integer;
        if (!isfinite(value) || value < f->min || value > f->max) {
            snprintf(error, sizeof(error), "%s：范围应为 %.8g 至 %.8g。", f->label, f->min, f->max);
            return field_error(ip, f->key, error);
        }
        if (f->integer)
            *(int *)((char *)&next + f->offset) = integer;
        else
            *(double *)((char *)&next + f->offset) = value;
    }
    if (next.project.current_direction == 0)
        return field_error(ip, "current_direction", "电流方向应为 +1 或 -1。");
    if (next.project.winding_direction == 0)
        return field_error(ip, "winding_direction", "绕向应为 +1 或 -1。");
    if (next.project.antenna_radius >= next.project.radius)
        return field_error(ip, "antenna_radius", "天线半径必须小于容器半径。");
    if (next.project.band_width >= next.project.antenna_radius)
        return field_error(ip, "band_width", "电流带宽必须小于天线半径。");
    if (fabs(next.project.axial_offset) > next.project.length / 2)
        return field_error(ip, "axial_offset", "轴向偏移不能超过显示长度的一半。");
    if (next.plasma_enabled && next.plasma.radius >= next.project.antenna_radius)
        return field_error(ip, "plasma.radius", "等离子体半径必须小于天线半径。");
    if (next.options.m_min > next.options.m_max)
        return field_error(ip, "space.m_max", "m 上限不能小于下限。");
    if (next.options.zmin >= next.options.zmax)
        return field_error(ip, "space.zmax", "观察上限必须大于下限。");
    if (next.kmin >= next.kmax)
        return field_error(ip, "kmax", "波数上限必须大于下限。");
    if (next.plasma.profile == RF_PLASMA_GAUSSIAN && next.plasma.edge_ratio <= 0)
        return field_error(ip, "plasma.edge_ratio", "高斯分布的边缘密度比例必须大于零。");
    if (!workbench_state_validate(&next, error, sizeof(error)))
        return failure(ip, error);
    rf_state = next;
    return TCL_OK;
}
static int state_command(ClientData cd, Tcl_Interp *ip, int argc, Tcl_Obj *const argv[])
{
    (void)cd;
    if (argc > 2) {
        Tcl_WrongNumArgs(ip, 1, argv, "?参数字典?");
        return TCL_ERROR;
    }
    if (argc == 2 && apply_state(ip, argv[1]) != TCL_OK)
        return TCL_ERROR;
    Tcl_SetObjResult(ip, state_dict(ip));
    return TCL_OK;
}
static int file_command(ClientData cd, Tcl_Interp *ip, int argc, Tcl_Obj *const argv[])
{
    char error[256];
    WorkbenchProject next;
    if (argc != 2) {
        Tcl_WrongNumArgs(ip, 1, argv, "路径");
        return TCL_ERROR;
    }
    if (cd) {
        if (!workbench_save(Tcl_GetString(argv[1]), &rf_state, error, sizeof(error)))
            return failure(ip, error);
    } else {
        if (!workbench_load(Tcl_GetString(argv[1]), &next, error, sizeof(error)))
            return failure(ip, error);
        rf_state = next;
    }
    Tcl_SetObjResult(ip, state_dict(ip));
    return TCL_OK;
}

static Tcl_Obj *numbers(const double *values, size_t n)
{
    size_t i;
    Tcl_Obj *a = Tcl_NewListObj(0, NULL);
    for (i = 0; i < n; ++i)
        Tcl_ListObjAppendElement(NULL, a, Tcl_NewDoubleObj(values[i]));
    return a;
}
static Tcl_Obj *series(Tcl_Interp *ip, const char *label, const char *color, Tcl_Obj *values)
{
    Tcl_Obj *s = Tcl_NewDictObj();
    str(ip, s, "label", label);
    str(ip, s, "color", color);
    put(ip, s, "y", values);
    return s;
}
static Tcl_Obj *complex_values(const double complex *v, size_t n, int part)
{
    size_t i;
    Tcl_Obj *a = Tcl_NewListObj(0, NULL);
    for (i = 0; i < n; ++i) {
        double value = part == 1   ? carg(v[i]) * 180 / 3.14159265358979323846
                       : part == 2 ? creal(v[i])
                       : part == 3 ? cimag(v[i])
                                   : cabs(v[i]);
        Tcl_ListObjAppendElement(NULL, a, Tcl_NewDoubleObj(value));
    }
    return a;
}
static Tcl_Obj *chart(Tcl_Interp *ip,
    const char *title,
    const char *xlabel,
    const char *ylabel,
    Tcl_Obj *x,
    Tcl_Obj *lines)
{
    Tcl_Obj *c = Tcl_NewDictObj();
    str(ip, c, "title", title);
    str(ip, c, "xlabel", xlabel);
    str(ip, c, "ylabel", ylabel);
    put(ip, c, "x", x);
    put(ip, c, "series", lines);
    return c;
}
static Tcl_Obj *field_chart(Tcl_Interp *ip,
    const char *title,
    const char *unit,
    const double *r,
    const double complex *v,
    const double *total,
    size_t n,
    int phase)
{
    static const char *names[] = { "r 分量", "φ 分量", "z 分量" };
    static const char *colors[] = { "#2563eb", "#d97706", "#059669" };
    size_t k;
    Tcl_Obj *lines = Tcl_NewListObj(0, NULL);
    if (!phase) {
        Tcl_Obj *magnitude;
        if (total) {
            magnitude = numbers(total, n);
        } else {
            magnitude = Tcl_NewListObj(0, NULL);
            for (size_t i = 0; i < n; ++i) {
                double norm = hypot(hypot(cabs(v[i]), cabs(v[n + i])), cabs(v[2 * n + i]));
                Tcl_ListObjAppendElement(ip, magnitude, Tcl_NewDoubleObj(norm));
            }
        }
        Tcl_ListObjAppendElement(ip, lines, series(ip, "总幅值", "#172b4d", magnitude));
    }
    for (k = 0; k < 3; ++k)
        Tcl_ListObjAppendElement(
            ip, lines, series(ip, names[k], colors[k], complex_values(v + k * n, n, phase)));
    return chart(ip, title, "半径 r (m)", unit, numbers(r, n), lines);
}
static void csv_row(Tcl_Obj *csv, const double *v, size_t n)
{
    size_t j;
    char b[64];
    for (j = 0; j < n; ++j) {
        snprintf(b, sizeof(b), "%s%.17g", j ? "," : "", v[j]);
        Tcl_AppendToObj(csv, b, -1);
    }
    Tcl_AppendToObj(csv, "\n", 1);
}
#include "rf_gui_fields.h"
/* Tcl 结果容器统一由命令入口持有；各计算函数只填充数据，不管理界面状态。 */
typedef struct {
    Tcl_Obj *result;
    Tcl_Obj *charts;
    Tcl_Obj *csv;
} ComputeOutput;

static int compute_geometry(Tcl_Interp *ip, const Project *p, int preview, ComputeOutput *out)
{
    Geometry g = { 0 };
    char error[256];
    if (!geometry_build(p, &g, error, sizeof(error))) {
        geometry_free(&g);
        return failure(ip, error);
    }
    Tcl_Obj *segments = Tcl_NewListObj(0, NULL);
    for (size_t i = 0; i < g.segment_count; ++i) {
        const GeometrySegment *segment = &g.segments[i];
        double v[9] = { segment->a[0],
            segment->a[1],
            segment->a[2],
            segment->b[0],
            segment->b[1],
            segment->b[2],
            segment->group,
            segment->phase + segment->phase_advance/2.,
            segment->amplitude };
        Tcl_ListObjAppendElement(ip, segments, numbers(v, 9));
    }
    put(ip, out->result, "segments", segments);
    num(ip, out->result, "count", g.segment_count);
    if (!preview)
        num(ip, out->result, "balance", geometry_current_balance(&g));
    num(ip, out->result, "radius", p->radius);
    num(ip, out->result, "length", p->length);
    str(ip,
        out->result,
        "note",
        "几何预览使用计算内核的有向电流路径；可旋转视角并导出 OBJ 中心线。");
    geometry_free(&g);
    return TCL_OK;
}

static int compute_source(Tcl_Interp *ip, const Project *p, ComputeOutput *out)
{
    Geometry g = { 0 };
    double complex source[3];
    char error[256];
    int ok = geometry_build(p, &g, error, sizeof(error)) &&
             geometry_current_spectrum(p, &g, source, error, sizeof(error));
    geometry_free(&g);
    if (!ok)
        return failure(ip, error);
    put(ip, out->result, "values", complex_values(source, 3, 0));
    put(ip, out->result, "phase", complex_values(source, 3, 1));
    put(ip, out->result, "real", complex_values(source, 3, 2));
    put(ip, out->result, "imag", complex_values(source, 3, 3));
    double row[7] = { p->kz,
        creal(source[0]),
        cimag(source[0]),
        creal(source[1]),
        cimag(source[1]),
        creal(source[2]),
        cimag(source[2]) };
    csv_row(out->csv, row, 7);
    return TCL_OK;
}

static int compute_vacuum(Tcl_Interp *ip, const Project *p, int sample, ComputeOutput *out)
{
    RfVacuumSolution s = { 0 };
    char error[256];
    if (!rf_vacuum_solve(p, &s, error, sizeof(error)))
        return failure(ip, error);
    /* 磁场样本已归求解结果所有；图表读取即可，无需再分配和复制三分量数组。 */
    const double complex *b = s.magnetic;
    double flux = axial_flux(s.r, s.electric, b, s.nr);
    num(ip, out->result, "peak", s.peak_electric);
    num(ip, out->result, "error", s.backward_error);
    num(ip, out->result, "count", s.nr);
    num(ip, out->result, "spectral_resistance", creal(s.fem->load_impedance));
    num(ip, out->result, "spectral_reactance", cimag(s.fem->load_impedance));
    num(ip, out->result, "flux", flux);
    if (sample) {
        double row[5] = { p->kz, 1, s.peak_electric, s.backward_error, flux };
        csv_row(out->csv, row, 5);
        add_scan_slice(ip, out->result, s.r, s.electric, b, s.total, NULL, s.nr);
    } else {
        add_field_charts(ip, out->charts, s.r, s.electric, s.total, b, s.nr);
        Tcl_AppendToObj(out->csv,
            "r_m,Er_re,Er_im,Ephi_re,Ephi_im,Ez_re,Ez_im,total_V,Br_re,Bphi_re,Bz_re,"
            "Br_im,Bphi_im,Bz_im,axial_flux_density_W,R_ohm_m,X_ohm_m\n",
            -1);
        for (size_t i = 0; i < s.nr; ++i) {
            double row[17] = { s.r[i],
                creal(s.electric[i]),
                cimag(s.electric[i]),
                creal(s.electric[s.nr + i]),
                cimag(s.electric[s.nr + i]),
                creal(s.electric[2 * s.nr + i]),
                cimag(s.electric[2 * s.nr + i]),
                s.total[i],
                creal(b[i]),
                creal(b[s.nr + i]),
                creal(b[2 * s.nr + i]),
                cimag(b[i]),
                cimag(b[s.nr + i]),
                cimag(b[2 * s.nr + i]),
                axial_flux_density(s.electric, b, s.nr, i),
                creal(s.fem->load_impedance),
                cimag(s.fem->load_impedance) };
            csv_row(out->csv, row, 17);
        }
    }
    char note[640];
    snprintf(note,
        sizeof(note),
        "%s\n负载阻抗谱 dZ/dk_z = %.6g%+.6gi Ω m（工程 exp(+iωt)，当前 m、k_z）。",
        s.message,
        creal(s.fem->load_impedance),
        cimag(s.fem->load_impedance));
    str(ip, out->result, "note", note);
    rf_vacuum_solution_free(&s);
    return TCL_OK;
}

static void add_plasma_charts(Tcl_Interp *ip, const RfPlasmaSolution *s, ComputeOutput *out)
{
    const double complex *b = s->magnetic;
    add_field_charts(ip, out->charts, s->r, s->electric, s->total, b, s->nr);
    add_chart(ip,
        out->charts,
        "current",
        field_chart(ip, "电流幅值", "电流谱幅值 (A/m)", s->r, s->current, NULL, s->nr, 0));

    Tcl_Obj *shell = Tcl_NewListObj(0, NULL), *wave_impedance = Tcl_NewListObj(0, NULL);
    Tcl_Obj *impedance_csv = Tcl_NewStringObj("r_m,selected_m,kz_rad_per_m,Z_wave_ohm,valid\n", -1);
    Tcl_AppendToObj(out->csv,
        "r_m,Er_re,Er_im,Ephi_re,Ephi_im,Ez_re,Ez_im,Jr_re,Jr_im,Jphi_re,Jphi_im,Jz_re,"
        "Jz_im,absorption_W_per_m,total_V,cumulative_W_m,Br_re,Bphi_re,Bz_re,Br_im,"
        "Bphi_im,Bz_im,axial_flux_density_W,R_ohm_m,X_ohm_m\n",
        -1);
    for (size_t i = 0; i < s->nr; ++i) {
        double row[25] = { s->r[i],
            creal(s->electric[i]),
            cimag(s->electric[i]),
            creal(s->electric[s->nr + i]),
            cimag(s->electric[s->nr + i]),
            creal(s->electric[2 * s->nr + i]),
            cimag(s->electric[2 * s->nr + i]),
            creal(s->current[i]),
            cimag(s->current[i]),
            creal(s->current[s->nr + i]),
            cimag(s->current[s->nr + i]),
            creal(s->current[2 * s->nr + i]),
            cimag(s->current[2 * s->nr + i]),
            s->absorption[i],
            s->total[i],
            s->cumulative[i],
            creal(b[i]),
            creal(b[s->nr + i]),
            creal(b[2 * s->nr + i]),
            cimag(b[i]),
            cimag(b[s->nr + i]),
            cimag(b[2 * s->nr + i]),
            axial_flux_density(s->electric, b, s->nr, i),
            creal(s->fem->load_impedance),
            cimag(s->fem->load_impedance) };
        csv_row(out->csv, row, 25);
        Tcl_ListObjAppendElement(ip, shell, Tcl_NewDoubleObj(s->r[i] * s->absorption[i]));
        /* The common exp(i*m*phi+i*kz*z) phase cancels in this norm ratio.
         * Use exactly the same single-(m,kz) E/B samples as the field charts. */
        double en = 0., bn = 0.;
        for (size_t component = 0; component < 3; ++component) {
            en = hypot(en, cabs(s->electric[component * s->nr + i]));
            bn = hypot(bn, cabs(b[component * s->nr + i]));
        }
        double zw = bn > 0. ? RF_MU0 * en / bn : NAN;
        int valid = isfinite(zw);
        Tcl_ListObjAppendElement(ip, wave_impedance,
            valid ? Tcl_NewDoubleObj(zw) : Tcl_NewStringObj("", 0));
        Tcl_Obj *record = valid
            ? Tcl_ObjPrintf("%.17g,%d,%.17g,%.17g,1\n", s->r[i], s->fem->project.selected_m,
                  s->fem->project.kz, zw)
            : Tcl_ObjPrintf("%.17g,%d,%.17g,,0\n", s->r[i], s->fem->project.selected_m,
                  s->fem->project.kz);
        Tcl_AppendObjToObj(impedance_csv, record);
        Tcl_DecrRefCount(record);
    }
    const struct {
        const char *title, *label, *unit, *color;
        Tcl_Obj *values;
    } deposition_views[] = {
        { "局部吸收", "局部吸收", "局部吸收谱量 (W/m)", "#d97706", numbers(s->absorption, s->nr) },
        { "径向壳层沉积", "径向壳层", "d²P / (dr dkz) (W)", "#7c3aed", shell },
    };
    for (size_t i = 0; i < sizeof(deposition_views) / sizeof(deposition_views[0]); ++i) {
        Tcl_Obj *lines = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(ip,
            lines,
            series(ip,
                deposition_views[i].label,
                deposition_views[i].color,
                deposition_views[i].values));
        Tcl_Obj *c = chart(ip, deposition_views[i].title, "半径 r (m)",
            deposition_views[i].unit, numbers(s->r, s->nr), lines);
        str(ip, c, "note", i == 0
            ? "固定当前 (m,k_z) 的局部吸收谱量 q_hat=½Re(J_hat·E_hat*)，单位 W/m；按同一个模态谱电场计算。"
            : "固定当前 (m,k_z) 的径向壳层沉积谱量 r q_hat=d²P/(dr dk_z)，单位 W；对 dr 积分得到 dP/dk_z（W m）。");
        add_chart(ip, out->charts, "deposition", c);
    }
    Tcl_Obj *lines = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(ip, lines, series(ip, "波阻抗幅值", "#2563eb", wave_impedance));
    Tcl_Obj *c = chart(ip, "波阻抗幅值", "半径 r (m)", "波阻抗幅值 (Ω)",
        numbers(s->r, s->nr), lines);
    str(ip, c, "note", "固定当前 (m,k_z)，Z_w=μ₀‖E_hat‖/‖B_hat‖（Ω）。使用单模式复数场的矢量范数；磁场为零或比值不可表示处留空。该幅值比不含相位，也不是天线端口负载阻抗。");
    put(ip, c, "csv", impedance_csv);
    add_chart(ip, out->charts, "wave_impedance", c);
}

static int compute_plasma(
    Tcl_Interp *ip, const Project *p, const Plasma *plasma, int sample, ComputeOutput *out)
{
    RfPlasmaSolution s = { 0 };
    char error[256];
    if (!rf_plasma_solve(p, plasma, &s, error, sizeof(error)))
        return failure(ip, error);
    double flux = axial_flux(s.r, s.electric, s.magnetic, s.nr);
    num(ip, out->result, "peak", s.peak_electric);
    num(ip, out->result, "count", s.nr);
    num(ip, out->result, "absorbed", s.absorbed_power);
    num(ip, out->result, "source_power", s.source_power);
    num(ip, out->result, "spectral_resistance", creal(s.fem->load_impedance));
    num(ip, out->result, "spectral_reactance", cimag(s.fem->load_impedance));
    num(ip, out->result, "power_balance_error", s.power_balance_error);
    num(ip, out->result, "error", s.backward_error);
    num(ip, out->result, "collision_scale", s.collision_scale);
    num(ip, out->result, "flux", flux);
    if (sample) {
        double row[7] = { p->kz, 1, s.peak_electric, s.absorbed_power, flux,
            creal(s.fem->load_impedance), cimag(s.fem->load_impedance) };
        csv_row(out->csv, row, 7);
        add_scan_slice(ip, out->result, s.r, s.electric, s.magnetic, s.total, s.absorption, s.nr);
    } else
        add_plasma_charts(ip, &s, out);
    char note[640];
    snprintf(note,
        sizeof(note),
        "%s\n负载阻抗谱 dZ/dk_z = %.6g%+.6gi Ω m（工程 exp(+iωt)，当前 m、k_z）。",
        s.message,
        creal(s.fem->load_impedance),
        cimag(s.fem->load_impedance));
    str(ip, out->result, "note", note);
    rf_plasma_solution_free(&s);
    return TCL_OK;
}

#include "rf_gui_branches.h"

static int compute_command(ClientData cd, Tcl_Interp *ip, int argc, Tcl_Obj *const argv[])
{
    if (argc < 2 || argc > 3) {
        Tcl_WrongNumArgs(ip, 1, argv, "vacuum|plasma|branches|source|sample|plasma_sample|geometry ?k_z?");
        return TCL_ERROR;
    }
    Project p = rf_state.project;
    if (argc == 3 && Tcl_GetDoubleFromObj(ip, argv[2], &p.kz) != TCL_OK)
        return TCL_ERROR;
    ComputeOutput out = { Tcl_NewDictObj(), Tcl_NewListObj(0, NULL), Tcl_NewObj() };
    Tcl_IncrRefCount(out.result);
    Tcl_IncrRefCount(out.charts);
    Tcl_IncrRefCount(out.csv);
    const char *mode = Tcl_GetString(argv[1]);
    int status;
    if (!strcmp(mode, "geometry"))
        status = compute_geometry(ip, &p, cd != NULL, &out);
    else if (!strcmp(mode, "source"))
        status = compute_source(ip, &p, &out);
    else if (!strcmp(mode, "vacuum") || !strcmp(mode, "sample"))
        status = compute_vacuum(ip, &p, !strcmp(mode, "sample"), &out);
    else if (!strcmp(mode, "branches"))
        status = rf_state.plasma_enabled ? compute_branches(ip, &p, &rf_state.plasma, &out)
                                         : failure(ip, "请先启用等离子体。");
    else if (!strcmp(mode, "material")) {
        char error[256];
        if (!rf_state.plasma_enabled)
            status = failure(ip, "请先启用等离子体。");
        else if (!add_material_charts(
                     ip, out.charts, out.csv, &p, &rf_state.plasma, error, sizeof(error)))
            status = failure(ip, error);
        else {
            num(ip, out.result, "count", p.radial_cells + 1);
            str(ip,
                out.result,
                "note",
                "材料剖面来自冷等离子体张量；等离子体区域外为真空介电响应。");
            status = TCL_OK;
        }
    } else if (!strcmp(mode, "plasma") || !strcmp(mode, "plasma_sample")) {
        status =
            rf_state.plasma_enabled
                ? compute_plasma(ip, &p, &rf_state.plasma, !strcmp(mode, "plasma_sample"), &out)
                : failure(ip, "请先在“介质”页启用等离子体。");
    } else
        status = failure(ip, "未知计算模块。");
    if (status == TCL_OK) {
        put(ip, out.result, "charts", out.charts);
        put(ip, out.result, "csv", out.csv);
        put(ip, out.result, "snapshot", state_dict(ip));
        Tcl_SetObjResult(ip, out.result);
    }
    Tcl_DecrRefCount(out.result);
    Tcl_DecrRefCount(out.charts);
    Tcl_DecrRefCount(out.csv);
    return status;
}
/* Preview unsaved form values without committing them or solving any fields. */
static int preview_command(ClientData cd, Tcl_Interp *ip, int argc, Tcl_Obj *const argv[])
{
    WorkbenchProject saved = rf_state;
    Tcl_Obj *args[2];
    int status;
    (void)cd;
    if (argc != 2) {
        Tcl_WrongNumArgs(ip, 1, argv, "参数字典");
        return TCL_ERROR;
    }
    if (apply_state(ip, argv[1]) != TCL_OK)
        return TCL_ERROR;
    args[0] = Tcl_NewStringObj("rf_compute", -1);
    args[1] = Tcl_NewStringObj("geometry", -1);
    Tcl_IncrRefCount(args[0]);
    Tcl_IncrRefCount(args[1]);
    status = compute_command((ClientData)1, ip, 2, args);
    rf_state = saved;
    Tcl_DecrRefCount(args[0]);
    Tcl_DecrRefCount(args[1]);
    return status;
}
static int export_obj_command(ClientData cd, Tcl_Interp *ip, int argc, Tcl_Obj *const argv[])
{
    WorkbenchProject saved = rf_state;
    Geometry g = { 0 };
    char error[256];
    int ok;
    (void)cd;
    if (argc != 3) {
        Tcl_WrongNumArgs(ip, 1, argv, "参数快照 路径");
        return TCL_ERROR;
    }
    if (apply_state(ip, argv[1]) != TCL_OK)
        return TCL_ERROR;
    ok = geometry_build(&rf_state.project, &g, error, sizeof(error));
    rf_state = saved;
    if (ok)
        ok = geometry_export_obj(&g, Tcl_GetString(argv[2]), error, sizeof(error));
    geometry_free(&g);
    return ok ? TCL_OK : failure(ip, error);
}

#include "rf_gui_render.h"

#include "rf_gui_spatial.h"
#include "rf_gui_full.h"

static int init_ui(Tcl_Interp *ip)
{
    Tcl_DString script;
    size_t i;
    int status;
    Tcl_CreateObjCommand(ip, "rf_state", state_command, NULL, NULL);
    Tcl_CreateObjCommand(ip, "rf_open", file_command, NULL, NULL);
    Tcl_CreateObjCommand(ip, "rf_save", file_command, (ClientData)1, NULL);
    Tcl_CreateObjCommand(ip, "rf_compute", compute_command, NULL, NULL);
    Tcl_CreateObjCommand(ip, "rf_spatial", spatial_command, &spatial_job, spatial_delete);
    Tcl_CreateObjCommand(ip, "rf_full", full_command, &full_job, full_delete);
    Tcl_CreateObjCommand(ip, "rf_preview", preview_command, NULL, NULL);
    Tcl_CreateObjCommand(ip, "rf_render_model", render_model_command, NULL, NULL);
    Tcl_CreateObjCommand(ip, "rf_export_obj", export_obj_command, NULL, NULL);
    Tcl_DStringInit(&script);
    for (i = 0; gui_script[i]; ++i)
        Tcl_DStringAppend(&script, gui_script[i], -1);
    status = Tcl_EvalEx(ip, Tcl_DStringValue(&script), Tcl_DStringLength(&script), TCL_EVAL_GLOBAL);
    Tcl_DStringFree(&script);
    return status;
}
static void configure_runtime(Tcl_Interp *ip)
{
#ifdef RF_BUNDLED_TK
    const char *exe = Tcl_GetNameOfExecutable(), *slash = exe ? strrchr(exe, '/') : NULL;
    if (slash) {
        Tcl_DString path;
        int base = (int)(slash - exe);
        FILE *probe;
        Tcl_DStringInit(&path);
        Tcl_DStringAppend(&path, exe, base);
        Tcl_DStringAppend(&path, "/runtime/lib/tcl8.6/init.tcl", -1);
        probe = fopen(Tcl_DStringValue(&path), "r");
        Tcl_DStringSetLength(&path, base);
        if (probe) {
            fclose(probe);
            Tcl_DStringAppend(&path, "/runtime/lib", -1);
        } else
            Tcl_DStringAppend(&path, "/../Resources/TclTk/lib", -1);
        base = Tcl_DStringLength(&path);
        Tcl_DStringAppend(&path, "/tcl8.6", -1);
        Tcl_SetVar(ip, "tcl_library", Tcl_DStringValue(&path), TCL_GLOBAL_ONLY);
        /* macOS Tk creates a second interpreter for its console. Per-interpreter
         * tcl_library/tk_library variables do not reach it: without environment
         * paths it falls back to the now-removed runtime build directory. Tcl's
         * env array updates the process environment and all new interpreters. */
        Tcl_SetVar2(ip, "env", "TCL_LIBRARY", Tcl_DStringValue(&path), TCL_GLOBAL_ONLY);
        Tcl_DStringSetLength(&path, base);
        Tcl_DStringAppend(&path, "/tk8.6", -1);
        Tcl_SetVar(ip, "tk_library", Tcl_DStringValue(&path), TCL_GLOBAL_ONLY);
        Tcl_SetVar2(ip, "env", "TK_LIBRARY", Tcl_DStringValue(&path), TCL_GLOBAL_ONLY);
        Tcl_DStringFree(&path);
    }
#else
    (void)ip;
#endif
}
int rf_tk_gui_run(int argc, char **argv)
{
    Tcl_Interp *ip;
    int i, status;
    const char *path = NULL;
    char error[256] = { 0 };
    workbench_default(&rf_state);
    for (i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "--project"))
            path = argv[i + 1];
    if (path) {
        WorkbenchProject next;
        if (workbench_load(path, &next, error, sizeof(error)))
            rf_state = next;
    }
    Tcl_FindExecutable(argv[0]);
    ip = Tcl_CreateInterp();
    if (!ip)
        return 1;
    configure_runtime(ip);
    if (Tcl_Init(ip) != TCL_OK || Tk_Init(ip) != TCL_OK || init_ui(ip) != TCL_OK) {
        fprintf(stderr,
            "GUI 初始化失败: %s\n",
            Tcl_GetVar(ip, "errorInfo", TCL_GLOBAL_ONLY)
                ? Tcl_GetVar(ip, "errorInfo", TCL_GLOBAL_ONLY)
                : Tcl_GetStringResult(ip));
        Tcl_DeleteInterp(ip);
        Tcl_Finalize();
        return 1;
    }
    if (path && !error[0])
        Tcl_SetVar(ip, "::rf::project_path", path, TCL_GLOBAL_ONLY);
    if (error[0]) {
        Tcl_SetVar(ip, "::rf::status", error, TCL_GLOBAL_ONLY);
        Tcl_Eval(ip,
            "after idle {tk_messageBox -icon error -title {项目加载失败} -message $::rf::status}");
    }
    Tcl_Eval(ip, "::rf::update_title");
    Tk_MainLoop();
    status = 0;
    Tcl_DeleteInterp(ip);
    Tcl_Finalize();
    return status;
}
