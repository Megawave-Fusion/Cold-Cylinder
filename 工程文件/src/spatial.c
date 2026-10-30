#include "rf_platform.h"
#include "spatial.h"
#include "plasma.h"
#include "rf_quadrature.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

typedef struct {
    RfMaxwellSolution *modes;
    double complex *phases, *field, *magnetic, *potential, *modal;
    double *nodes;
    size_t next_k, next_r, next_cell, nodes_count, stored_nodes;
    double radial_power;
    double gx[16], gw[16];
} SpatialWork;
/* 统一写入可选诊断并返回失败；error_size=0时不读写调用者的缓冲。 */
static int fail(char *e, size_t n, const char *s)
{
    if (e && n)
        snprintf(e, n, "%s", s);
    return 0;
}
/* 比较两个同单位的功率；近零分母只用于数值诊断，不给物理吸收添加底值。 */
static double relative(double a, double b)
{
    return fabs(a - b) / fmax(1e-100, fmax(fabs(a), fabs(b)));
}
/* 从已提交工程提取单m的观察网格；实际k区间/点数取工作台公共扫描设置。
 * 空状态返回零配置，交给begin的参数验证明确拒绝。 */
RfSpatialConfig rf_spatial_config(const WorkbenchProject *w)
{
    if (!w)
        return (RfSpatialConfig){ 0 };
    return (RfSpatialConfig){ w->kmin,
        w->kmax,
        w->options.zmin,
        w->options.zmax,
        (size_t)w->nk,
        (size_t)w->options.nr,
        (size_t)w->options.nz, 0 };
}
/* 释放各k的原始FEM解及重建临时数组；公开结果数组由rf_spatial_free另行释放。 */
static void free_work(RfSpatialSolution *s)
{
    SpatialWork *w = s->work;
    if (!w)
        return;
    if (w->modes)
        for (size_t k = 0; k < s->config.nk; k++)
            rf_maxwell_free(w->modes + k);
    free(w->modes);
    free(w->phases);
    free(w->field);
    free(w->magnetic);
    free(w->potential);
    free(w->modal);
    free(w->nodes);
    free(w);
    s->work = NULL;
}
/* 取消、失败和正常释放共用此入口；结果必须先初始化为{0}，清理后可再次启动。 */
void rf_spatial_free(RfSpatialSolution *s)
{
    if (!s)
        return;
    free_work(s);
    free(s->r);
    free(s->z);
    free(s->k);
    free(s->absorption);
    free(s->electric);
    free(s->current);
    free(s->magnetic);
    free(s->potential);
    free(s->wave_impedance);
    free(s->axial_absorption);
    free(s->radial_absorption);
    free(s->cumulative);
    free(s->power_spectrum);
    free(s->impedance_spectrum);
    memset(s, 0, sizeof(*s));
}
/* 启动前先复制允许与旧结果别名的输入，再销毁旧结果。验证真实采样/周期约束，
 * 预分配所需数组；potential_only避免为电势图计算无关的场与吸收网格。 */
static int spatial_begin(const Project *p,
    const Plasma *c,
    const RfSpatialConfig *g,
    RfSpatialSolution *s,
    char *e,
    size_t n,
    int potential_only)
{
    if (!p || !c || !g || !s)
        return fail(e, n, "空间求解缺少参数");
    /* Permit beginning again with pointers into s, like the other owning solvers. */
    Project pp = *p;
    Plasma cc = *c;
    RfSpatialConfig gg = *g;
    p = &pp;
    c = &cc;
    g = &gg;
    rf_spatial_free(s);
    if (!project_validate(p, e, n) || !plasma_validate(c, e, n))
        return 0;
    if (c->radius >= p->radius)
        return fail(e, n, "等离子体半径必须小于容器半径");
    if (c->solve_mode != RF_PLASMA_FINITE_COLLISION || c->density <= 0 ||
        (c->electron_collision <= 0 && c->ion_collision <= 0))
        return fail(e, n, "空间重建要求正密度、选择‘有限碰撞’且至少一种碰撞率为正；暂不支持逐波数零碰撞极限的相干重建");
    if (!isfinite(g->kmin) || !isfinite(g->kmax) || !isfinite(g->zmin) || !isfinite(g->zmax) ||
        g->kmin >= g->kmax || g->zmin >= g->zmax || g->kmin < -2000 || g->kmax > 2000 ||
        g->nk < 3 || g->nk > 4097 || g->nr < 17 || g->nr > 2049 || g->nz < 3 || g->nz > 1001 ||
        g->nr * g->nz > 500000)
        return fail(e, n, "空间网格/波数范围无效（最多 500000 个显示点）");
    if (!isfinite(g->field_radius) || g->field_radius < 0 || g->field_radius > p->radius)
        return fail(e, n, "空间场显示半径必须在容器内");
    double dk = (g->kmax - g->kmin) / g->nk, period = 2 * RF_PI / dk;
    if (!(dk > 0) || !isfinite(period) || !isfinite(g->zmax - g->zmin))
        return fail(e, n, "空间/波数步长超出可表示范围");
    if (g->zmax - g->zmin > period * (1 + 1e-12))
        return fail(e, n, "观察窗口超过离散波数的重复周期 2π/Δk；请增加波数点数");
    if ((g->zmax - g->zmin) / (g->nz - 1) > RF_PI / (g->kmax - g->kmin) * (1 + 1e-12))
        return fail(e, n, "z 网格不足以解析吸收的干涉条纹；请增加 z 点数");
    s->project = *p;
    s->plasma = *c;
    s->config = *g;
    s->potential_only = potential_only;
    s->dk = dk;
    s->period = period;
    SpatialWork *w = calloc(1, sizeof(*w));
    s->work = w;
    if (!w)
        goto memory;
#define ALLOC(member, count, type)                                                                 \
    s->member = calloc((count), sizeof(type));                                                     \
    if (!s->member)                                                                                \
    goto memory
    ALLOC(r, g->nr, double);
    ALLOC(z, g->nz, double);
    ALLOC(k, g->nk, double);
    ALLOC(potential, g->nr * g->nz, double complex);
    if (!potential_only) {
        ALLOC(absorption, g->nr * g->nz, double);
        ALLOC(electric, 3 * g->nr * g->nz, double complex);
        ALLOC(current, 3 * g->nr * g->nz, double complex);
        ALLOC(magnetic, 3 * g->nr * g->nz, double complex);
        ALLOC(wave_impedance, g->nr * g->nz, double);
        ALLOC(axial_absorption, g->nz, double);
        ALLOC(radial_absorption, g->nr, double);
        ALLOC(cumulative, g->nz, double);
    }
    ALLOC(power_spectrum, g->nk, double);
    ALLOC(impedance_spectrum, g->nk, double complex);
#undef ALLOC
    w->modes = calloc(g->nk, sizeof(*w->modes));
    w->phases = calloc(g->nk * g->nz, sizeof(*w->phases));
    w->potential = calloc(g->nz, sizeof(*w->potential));
    if (!potential_only) {
        w->field = calloc(3 * g->nz, sizeof(*w->field));
        w->magnetic = calloc(3 * g->nz, sizeof(*w->magnetic));
        w->modal = calloc(3 * g->nk, sizeof(*w->modal));
        if (!w->field || !w->magnetic || !w->modal)
            goto memory;
    }
    if (!w->modes || !w->phases || !w->potential)
        goto memory;
    if (!rf_gauss_legendre(16, w->gx, w->gw)) {
        rf_spatial_free(s);
        return fail(e, n, "空间径向求积规则生成失败");
    }
    for (size_t r = 0; r < g->nr; r++) {
        s->r[r] = (g->field_radius > 0 ? g->field_radius : c->radius) * ((double)r / (g->nr - 1));
        if (r && !(s->r[r] > s->r[r-1])) {
            rf_spatial_free(s);
            return fail(e,n,"径向采样间隔小于double分辨率；请调整半径或采样点数");
        }
    }
    for (size_t z = 0; z < g->nz; z++) {
        double fraction = (double)z / (g->nz - 1);
        s->z[z] = (1.-fraction) * g->zmin + fraction * g->zmax;
        if (!isfinite(s->z[z]) || (z && !(s->z[z] > s->z[z-1]))) {
            rf_spatial_free(s);
            return fail(e,n,"轴向采样间隔小于double分辨率；请调整窗口中心或采样点数");
        }
    }
    for (size_t k = 0; k < g->nk; k++) {
        s->k[k] = g->kmin + (k + .5) * dk;
        /* 区间端点可区分不代表所有中点可区分；必须检查实际生成的double节点。 */
        if (!(s->k[k] > g->kmin && s->k[k] < g->kmax) || (k && !(s->k[k] > s->k[k-1]))) {
            rf_spatial_free(s);
            return fail(e,n,"波数中点小于double分辨率；请扩大波数区间或减少点数");
        }
        for (size_t z = 0; z < g->nz; z++) {
            double phase = s->k[k] * s->z[z];
            if (!isfinite(phase)) {
                rf_spatial_free(s);
                return fail(e,n,"Fourier相位非有限；请调整波数或轴向窗口中心");
            }
            w->phases[k * g->nz + z] = dk / (2 * RF_PI) * cexp(I * phase);
        }
    }
    return 1;
memory:
    rf_spatial_free(s);
    return fail(e, n, "空间重建内存分配失败");
}
/* 启动有限碰撞的单m全场重建：保留各k的复相位，最终在空间中计算二次量。 */
int rf_spatial_begin(const Project *p, const Plasma *c, const RfSpatialConfig *g,
    RfSpatialSolution *s, char *e, size_t n)
{
    return spatial_begin(p, c, g, s, e, n, 0);
}
/* 启动电势专用重建；仍解相同的Maxwell/Coulomb问题，输出Phi为V。 */
int rf_spatial_begin_potential(const Project *p, const Plasma *c, const RfSpatialConfig *g,
    RfSpatialSolution *s, char *e, size_t n)
{
    return spatial_begin(p, c, g, s, e, n, 1);
}
/* 公共径向节点排序器；节点来自已验证的有限FEM网格，不比较NaN。 */
static int compare_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
/* 合并全部k切片在等离子体内的真实节点，避免空间吸收求积跨过任何单元/界面；
 * 节点与总工作量上限在进一步分配、积分前检查。 */
static int prepare_nodes(RfSpatialSolution *s, char *e, size_t n)
{
    SpatialWork *w = s->work;
    size_t count = 2;
    for (size_t k = 0; k < s->config.nk; k++)
        count += w->modes[k].nodes;
    if (count > 1500000)
        return fail(e, n, "空间径向公共网格过大；请降低网格数或分批计算");
    w->nodes = malloc(count * sizeof(double));
    if (!w->nodes)
        return fail(e, n, "公共径向网格内存不足");
    count = 0;
    w->nodes[count++] = 0;
    w->nodes[count++] = s->plasma.radius;
    for (size_t k = 0; k < s->config.nk; k++)
        for (size_t r = 0; r < w->modes[k].nodes; r++) {
            double x = w->modes[k].r[r];
            if (x > 0 && x < s->plasma.radius)
                w->nodes[count++] = x;
        }
    qsort(w->nodes, count, sizeof(double), compare_double);
    size_t kept = 1;
    for (size_t r = 1; r < count; r++)
        if (w->nodes[r] - w->nodes[kept - 1] > 32 * DBL_EPSILON * s->project.radius)
            w->nodes[kept++] = w->nodes[r];
    w->nodes_count = kept;
    if ((double)kept * s->config.nk * s->config.nz * 16 > 1.5e9)
        return fail(e, n, "空间重建工作量过大；请减少波数/空间点或径向网格数");
    return 1;
}
/* E_hat is sampled from each original FEM mesh, never interpolated from display magnitudes. */
/* 固定真实半径r，从各自FEM网格恢复复E_hat/B_hat，再以dk/(2pi)相干求和；
 * 空间E/B单位V/m、T。spectral_q保留独立的sum dk*q_spec，用于径向功率交叉核对。 */
static int reconstruct(RfSpatialSolution *s, double r, double complex *potential,
    RfPlasmaMaterial *mat, double *spectral_q, char *e, size_t n)
{
    SpatialWork *w = s->work;
    size_t nk = s->config.nk, nz = s->config.nz;
    if (!rf_plasma_material(&s->project, &s->plasma, r, mat, e, n))
        return 0;
    memset(w->field, 0, 3 * nz * sizeof(*w->field));
    memset(w->magnetic, 0, 3 * nz * sizeof(*w->magnetic));
    if (potential)
        memset(potential, 0, nz * sizeof(*potential));
    *spectral_q = 0;
    for (size_t k = 0; k < nk; k++) {
        double complex b[3], *v = w->modal + 3 * k;
        if (!rf_maxwell_evaluate(w->modes + k, r, 0, v, b))
            return fail(e, n, "空间重建 FEM 取场失败");
        double complex phi = 0.;
        if (potential && !rf_maxwell_evaluate_potential(w->modes + k, r, 0, &phi, NULL))
            return fail(e, n, "空间重建电势取样失败");
        *spectral_q += s->dk * rf_plasma_absorption(&s->project, &s->plasma, mat, v);
        for (size_t z = 0; z < nz; z++) {
            double complex phase = w->phases[k * nz + z];
            if (potential)
                potential[z] += phi * phase;
            for (size_t c = 0; c < 3; c++) {
                w->field[3 * z + c] += v[c] * phase;
                w->magnetic[3 * z + c] += b[c] * phase;
            }
        }
    }
    /* 真空区吸收恒为0，不能只用吸收是否有限来代替复场有效性检查。 */
    for (size_t z = 0; z < nz; ++z)
        for (size_t c = 0; c < 3; ++c)
            if (!isfinite(cabs(w->field[3*z+c])) || !isfinite(cabs(w->magnetic[3*z+c])))
                return fail(e, n, "空间相干重建电场或磁场非有限值");
    if (!isfinite(*spectral_q))
        return fail(e, n, "空间重建的吸收谱积分非有限值");
    return 1;
}
/* 复合梯形积分；x必须严格递增，结果单位是x单位乘被积量单位。 */
static double trap(const double *x, const double *v, size_t n)
{
    double a = 0;
    for (size_t i = 1; i < n; i++)
        a += (x[i] - x[i - 1]) * (v[i] + v[i - 1]) / 2;
    return a;
}
/* 分步状态机：逐k求场→逐r重建→公共径向网格求积→功率诊断。
 * E/J/B先相干求和再形成q；Parseval谱功率与窗口功率分别保存，不相互覆盖。 */
static int advance(RfSpatialSolution *s, char *e, size_t n)
{
    SpatialWork *w = s->work;
    const RfSpatialConfig *g = &s->config;
    if (w->next_k < g->nk) {
        size_t k = w->next_k;
        Project p = s->project;
        p.kz = s->k[k];
        if (!rf_maxwell_solve(&p, &s->plasma, w->modes + k, e, n) ||
            !rf_maxwell_prepare_potential(w->modes + k, e, n)) {
            char detail[256];
            snprintf(detail, sizeof(detail), "kz=%.9g: %s", p.kz,
                e && n && *e ? e : "模态求解失败");
            return fail(e, n, detail);
        }
        s->potential_backward_error =
            fmax(s->potential_backward_error, w->modes[k].potential_backward_error);
        w->stored_nodes += w->modes[k].nodes;
        if (w->stored_nodes > 1500000)
            return fail(e, n, "空间谱场存储超过 1500000 个径向节点；请减少网格或波数点数");
        s->power_spectrum[k] = w->modes[k].absorbed_power;
        s->impedance_spectrum[k] = w->modes[k].load_impedance;
        s->spectral_power += s->dk * w->modes[k].absorbed_power;
        s->source_power += s->dk * w->modes[k].source_power;
        s->impedance += s->dk * w->modes[k].load_impedance;
        w->next_k++;
        s->progress = .45 * w->next_k / g->nk;
        if (w->next_k == g->nk && !s->potential_only && !prepare_nodes(s, e, n))
            return 0;
        return 1;
    }
    if (s->potential_only) {
        /* Evaluate the finite Fourier sum at every real observation point.
         * No interpolated traces and no unrelated absorption quadrature. */
        size_t r = w->next_r;
        memset(w->potential, 0, g->nz * sizeof(*w->potential));
        for (size_t k = 0; k < g->nk; ++k) {
            double complex phi;
            if (!rf_maxwell_evaluate_potential(w->modes + k, s->r[r], 0, &phi, NULL))
                return fail(e, n, "电势波形取样失败");
            for (size_t z = 0; z < g->nz; ++z)
                w->potential[z] += phi * w->phases[k * g->nz + z];
        }
        for (size_t z = 0; z < g->nz; ++z) {
            if (!isfinite(creal(w->potential[z])) || !isfinite(cimag(w->potential[z])))
                return fail(e, n, "电势波形非有限值");
            s->potential[z * g->nr + r] = w->potential[z];
        }
        w->next_r++;
        s->progress = .45 + .55 * w->next_r / g->nr;
        if (w->next_r == g->nr) {
            s->power_balance_error = relative(s->source_power, s->spectral_power);
            s->complete = 1;
            s->progress = 1.;
            free_work(s);
        }
        return 1;
    }
    RfPlasmaMaterial mat;
    double spec;
    if (w->next_r < g->nr) {
        size_t r = w->next_r;
        double radius = s->r[r];
        if (!reconstruct(s, radius, w->potential, &mat, &spec, e, n))
            return 0;
        double omega = 2 * RF_PI * s->project.frequency_mhz * 1e6;
        for (size_t z = 0; z < g->nz; z++) {
            size_t j = z * g->nr + r;
            double complex *v = w->field + 3 * z, *J = s->current + 3 * j;
            s->potential[j] = w->potential[z];
            if (!isfinite(creal(s->potential[j])) || !isfinite(cimag(s->potential[j])))
                return fail(e, n, "空间电势非有限值");
            memcpy(s->electric + 3 * j, v, 3 * sizeof(*v));
            memcpy(s->magnetic + 3 * j, w->magnetic + 3 * z, 3 * sizeof(*v));
            /* Ratio of vector norms after coherent reconstruction. This local
             * wave-impedance magnitude is not the antenna's complex port load.
             * A magnetic node stays undefined; never add an artificial floor. */
            double en = 0, bn = 0;
            for (size_t c = 0; c < 3; c++) {
                en = hypot(en, cabs(v[c]));
                bn = hypot(bn, cabs(s->magnetic[3 * j + c]));
            }
            double zw = bn > 0 ? RF_MU0 * en / bn : NAN;
            s->wave_impedance[j] = isfinite(zw) ? zw : NAN;
            J[0] = -I * omega * RF_EPS0 * ((mat.S - 1) * v[0] - I * mat.D * v[1]);
            J[1] = -I * omega * RF_EPS0 * (I * mat.D * v[0] + (mat.S - 1) * v[1]);
            J[2] = -I * omega * RF_EPS0 * (mat.P - 1) * v[2];
            s->absorption[j] = rf_plasma_absorption(&s->project, &s->plasma, &mat, v);
            if (!isfinite(s->absorption[j]))
                return fail(e, n, "空间吸收非有限值");
            if (z)
                s->radial_absorption[r] += 2 * RF_PI * radius * (s->z[z] - s->z[z - 1]) *
                                           (s->absorption[j] + s->absorption[(z - 1) * g->nr + r]) /
                                           2;
        }
        w->next_r++;
        s->progress = .45 + .15 * w->next_r / g->nr;
        return 1;
    }
    if (w->next_cell + 1 < w->nodes_count) {
        size_t cell = w->next_cell;
        double lo = w->nodes[cell], hi = w->nodes[cell + 1];
        for (size_t q = 0; q < 16; q++) {
            double r = (hi + lo) / 2 + (hi - lo) * w->gx[q] / 2,
                   weight = (hi - lo) * w->gw[q] / 2 * r;
            if (!reconstruct(s, r, NULL, &mat, &spec, e, n))
                return 0;
            w->radial_power += weight * spec;
            for (size_t z = 0; z < g->nz; z++)
                s->axial_absorption[z] +=
                    2 * RF_PI * weight *
                    rf_plasma_absorption(&s->project, &s->plasma, &mat, w->field + 3 * z);
        }
        w->next_cell++;
        s->progress = .6 + .4 * w->next_cell / (w->nodes_count - 1);
        return 1;
    }
    s->window_power = trap(s->z, s->axial_absorption, g->nz);
    for (size_t z = 1; z < g->nz; z++)
        s->cumulative[z] =
            s->cumulative[z - 1] +
            (s->z[z] - s->z[z - 1]) * (s->axial_absorption[z] + s->axial_absorption[z - 1]) / 2;
    double peak = 0;
    for (size_t z = 0; z < g->nz; z++)
        peak = fmax(peak, s->axial_absorption[z]);
    s->edge_ratio =
        fmax(s->axial_absorption[0], s->axial_absorption[g->nz - 1]) / fmax(1e-100, peak);
    size_t edge = (g->nk + 19) / 20;
    double tail = 0, ztail = 0, znorm = 0;
    for (size_t k = 0; k < g->nk; k++) {
        znorm += cabs(s->impedance_spectrum[k]);
        if (k < edge || k + edge >= g->nk) {
            tail += s->dk * s->power_spectrum[k];
            ztail += cabs(s->impedance_spectrum[k]);
        }
    }
    s->impedance_edge_fraction = ztail / fmax(1e-100, znorm);
    s->band_edge_fraction = tail / fmax(1e-100, s->spectral_power);
    s->radial_check_error = relative(w->radial_power, s->spectral_power);
    s->power_balance_error = relative(s->source_power, s->spectral_power);
    s->complete = 1;
    s->progress = 1;
    free_work(s);
    return 1;
}
/* 推进一个有界工作块，供GUI事件循环取消；任何失败都释放未完成任务。 */
int rf_spatial_step(RfSpatialSolution *s, char *e, size_t n)
{
    if (!s)
        return fail(e, n, "空间求解未初始化");
    if (s->complete)
        return 1;
    if (!s->work)
        return fail(e, n, "空间求解未启动或已经失败");
    if (advance(s, e, n))
        return 1;
    rf_spatial_free(s);
    return 0;
}
/* 同步包装同一begin/step算法，保证CLI与GUI使用相同公式及采样规则。 */
int rf_spatial_solve(const Project *p,
    const Plasma *c,
    const RfSpatialConfig *g,
    RfSpatialSolution *s,
    char *e,
    size_t n)
{
    if (!rf_spatial_begin(p, c, g, s, e, n))
        return 0;
    while (!s->complete)
        if (!rf_spatial_step(s, e, n))
            return 0;
    return 1;
}
/* 导出完整空间结果：场为V/m、T、A/m²，q为W/m³，功率为W；
 * 磁场零点的波阻抗留空并给valid=0，不伪造有限阻抗。 */
int rf_spatial_export_csv(const RfSpatialSolution *s, const char *path, char *e, size_t n)
{
    if (!s || !s->complete || !path || !*path)
        return fail(e, n, "没有完整的空间结果或有效导出路径");
    FILE *f = rf_fopen_utf8(path, "w");
    if (!f)
        return fail(e, n, "无法写入空间 CSV");
    if (s->potential_only) {
        fputs("z_m,r_m,Phi_Coulomb_re_V,Phi_Coulomb_im_V,selected_m,potential_backward_error\n", f);
        for (size_t z = 0; z < s->config.nz; ++z)
            for (size_t r = 0; r < s->config.nr; ++r) {
                double complex phi = s->potential[z * s->config.nr + r];
                fprintf(f, "%.17g,%.17g,%.17g,%.17g,%d,%.17g\n", s->z[z], s->r[r],
                    creal(phi), cimag(phi), s->project.selected_m, s->potential_backward_error);
            }
        int ok = !ferror(f);
        if (fclose(f))
            ok = 0;
        return ok ? 1 : fail(e, n, "电势 CSV 写入失败");
    }
    fputs("z_m,r_m,q_W_m3,dP_dz_W_m,dP_dr_window_W_m,cumulative_window_W,Er_re_V_m,Er_im_V_m,Ephi_"
          "re_V_m,Ephi_im_V_m,Ez_re_V_m,Ez_im_V_m,Jr_re_A_m2,Jr_im_A_m2,Jphi_re_A_m2,Jphi_im_A_m2,"
          "Jz_re_A_m2,Jz_im_A_m2,selected_m,window_power_W,band_absorbed_W,band_source_W,band_R_"
          "ohm,band_X_ohm,period_m,Br_re_T,Br_im_T,Bphi_re_T,Bphi_im_T,Bz_re_T,Bz_im_T,Z_wave_ohm,Z_wave_valid,Phi_Coulomb_re_V,Phi_Coulomb_im_V\n",
        f);
    for (size_t z = 0; z < s->config.nz; z++)
        for (size_t r = 0; r < s->config.nr; r++) {
            size_t j = z * s->config.nr + r;
            fprintf(f,
                "%.17g,%.17g,%.17g,%.17g,%.17g,%.17g",
                s->z[z],
                s->r[r],
                s->absorption[j],
                s->axial_absorption[z],
                s->radial_absorption[r],
                s->cumulative[z]);
            for (size_t c = 0; c < 3; c++)
                fprintf(f,
                    ",%.17g,%.17g",
                    creal(s->electric[3 * j + c]),
                    cimag(s->electric[3 * j + c]));
            for (size_t c = 0; c < 3; c++)
                fprintf(
                    f, ",%.17g,%.17g", creal(s->current[3 * j + c]), cimag(s->current[3 * j + c]));
            fprintf(f,
                ",%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g",
                s->project.selected_m,
                s->window_power,
                s->spectral_power,
                s->source_power,
                creal(s->impedance),
                cimag(s->impedance),
                s->period);
            for (size_t c = 0; c < 3; c++)
                fprintf(f, ",%.17g,%.17g", creal(s->magnetic[3 * j + c]),
                    cimag(s->magnetic[3 * j + c]));
            if (isfinite(s->wave_impedance[j]))
                fprintf(f, ",%.17g,1", s->wave_impedance[j]);
            else
                fputs(",,0", f);
            fprintf(f, ",%.17g,%.17g\n", creal(s->potential[j]), cimag(s->potential[j]));
        }
    int ok = !ferror(f);
    if (fclose(f))
        ok = 0;
    return ok ? 1 : fail(e, n, "空间 CSV 写入失败");
}
/* 逐k导出dP/dk和dZ/dk（W m、ohm m），并保留实际中点权重dk；
 * source_W_m由独立源阻抗的实部恢复，便于核对材料吸收。 */
int rf_spatial_export_spectrum(const RfSpatialSolution *s, const char *path, char *e, size_t n)
{
    if (!s || !s->complete || !path || !*path)
        return fail(e, n, "没有完整的空间结果或有效导出路径");
    FILE *f = rf_fopen_utf8(path, "w");
    if (!f)
        return fail(e, n, "无法写入阻抗谱 CSV");
    fputs("kz_rad_per_m,absorbed_W_m,source_W_m,R_ohm_m,X_ohm_m,dk_rad_per_m\n", f);
    for (size_t k = 0; k < s->config.nk; k++)
        fprintf(f,
            "%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n",
            s->k[k],
            s->power_spectrum[k],
            .5 * s->project.current * s->project.current * creal(s->impedance_spectrum[k]),
            creal(s->impedance_spectrum[k]),
            cimag(s->impedance_spectrum[k]),
            s->dk);
    int ok = !ferror(f);
    if (fclose(f))
        ok = 0;
    return ok ? 1 : fail(e, n, "阻抗谱 CSV 写入失败");
}
