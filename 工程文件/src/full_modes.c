#include "full_modes.h"
#include "plasma.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    RfSpatialSolution single;
    int m;
    size_t next_r, final_r;
} FullWork;
/* 全模式任务的可选诊断出口；不假定调用者一定提供错误缓冲。 */
static int fail(char *e, size_t n, const char *message)
{
    if (e && n)
        snprintf(e, n, "%s", message);
    return 0;
}
/* 提取模式求和范围及观察方位角phi（rad）；k积分规则复用单m配置。 */
RfFullConfig rf_full_config(const WorkbenchProject *w)
{
    if (!w)
        return (RfFullConfig){ 0 };
    return (
        RfFullConfig){ rf_spatial_config(w), w->options.m_min, w->options.m_max, w->options.phi };
}
/* 释放当前m的单模任务及进度状态，不释放已经累加的公开总场数组。 */
static void free_work(RfFullSolution *s)
{
    FullWork *w = s->work;
    if (!w)
        return;
    rf_spatial_free(&w->single);
    free(w);
    s->work = NULL;
}
/* 销毁拥有数组的全模式结果；清零后既可安全重复调用，也可重新开始任务。 */
void rf_full_free(RfFullSolution *s)
{
    if (!s)
        return;
    free_work(s);
    free(s->r);
    free(s->z);
    free(s->electric);
    free(s->magnetic);
    free(s->current);
    free(s->absorption);
    free(s->axial_flux);
    free(s->shell);
    memset(s, 0, sizeof(*s));
}
/* 仅替换本次单模求解的selected_m，保持同一频率、材料和k/z网格。 */
static int begin_mode(RfFullSolution *s, char *e, size_t n)
{
    FullWork *w = s->work;
    Project p = s->project;
    p.selected_m = w->m;
    w->next_r = 0;
    return rf_spatial_begin(&p, &s->plasma, &s->config.space, &w->single, e, n);
}
/* 验证模式/方位角并启动第一个m；所有模式在整个容器半径上采样。
 * 输入可引用旧结果中的成员，因此必须在释放旧结果前复制。 */
int rf_full_begin(
    const Project *p, const Plasma *c, const RfFullConfig *g, RfFullSolution *s, char *e, size_t n)
{
    if (!p || !c || !g || !s)
        return fail(e, n, "全模式求解缺少参数");
    Project pp = *p;
    Plasma cc = *c;
    RfFullConfig gg = *g;
    rf_full_free(s);
    if (gg.m_min < -16 || gg.m_max > 16 || gg.m_min > gg.m_max || !isfinite(gg.phi))
        return fail(e, n, "全模式 m 范围必须在 [-16,16] 内，且下限不大于上限；φ 必须有限");
    s->project = pp;
    s->plasma = cc;
    s->config = gg;
    s->config.space.field_radius = pp.radius;
    FullWork *w = calloc(1, sizeof(*w));
    s->work = w;
    if (!w)
        goto memory;
    w->m = gg.m_min;
    if (!begin_mode(s, e, n)) {
        rf_full_free(s);
        return 0;
    }
    size_t nr = gg.space.nr, nz = gg.space.nz, count = nr * nz;
#define ALLOC(member, count, type)                                                                 \
    s->member = calloc((count), sizeof(type));                                                     \
    if (!s->member)                                                                                \
    goto memory
    ALLOC(r, nr, double);
    ALLOC(z, nz, double);
    ALLOC(electric, 3 * count, double complex);
    ALLOC(magnetic, 3 * count, double complex);
    ALLOC(current, 3 * count, double complex);
    ALLOC(absorption, count, double);
    ALLOC(axial_flux, count, double);
    ALLOC(shell, count, double);
#undef ALLOC
    memcpy(s->r, w->single.r, nr * sizeof(double));
    memcpy(s->z, w->single.z, nz * sizeof(double));
    return 1;
memory:
    rf_full_free(s);
    return fail(e, n, "全模式场内存分配失败");
}
/* 依次完成一个m，乘exp(i*m*phi)累加复场，然后释放该m的FEM切片；
 * shell先作完整方位角积分，跨m项正交消失；局部q(phi)则在总场求和后计算。 */
static int advance(RfFullSolution *s, char *e, size_t n)
{
    FullWork *w = s->work;
    const RfFullConfig *g = &s->config;
    size_t nr = g->space.nr, nz = g->space.nz;
    int modes = g->m_max - g->m_min + 1;
    if (w->m <= g->m_max) {
        if (!w->single.complete) {
            if (!rf_spatial_step(&w->single, e, n)) {
                char message[512];
                snprintf(message, sizeof(message), "m=%d: %s", w->m,
                    e && n && *e ? e : "空间求解失败");
                return fail(e, n, message);
            }
            s->progress = .95 * (w->m - g->m_min + .9 * w->single.progress) / modes;
            return 1;
        }
        if (w->next_r < nr) {
            size_t r = w->next_r++;
            /* 方位角以2π为周期；先约化再乘m，避免有限大角度乘m后溢出。 */
            double complex phase = cexp(I * (w->m * remainder(g->phi, 2 * RF_PI)));
            for (size_t z = 0; z < nz; z++) {
                size_t j = z * nr + r;
                for (size_t comp = 0; comp < 3; comp++) {
                    s->electric[3 * j + comp] += phase * w->single.electric[3 * j + comp];
                    s->magnetic[3 * j + comp] += phase * w->single.magnetic[3 * j + comp];
                    s->current[3 * j + comp] += phase * w->single.current[3 * j + comp];
                }
                /* Exact angular orthogonality eliminates cross-m terms here,
                 * while all cross-k terms remain in each coherent q_m(r,z). */
                s->shell[j] += 2 * RF_PI * s->r[r] * w->single.absorption[j];
            }
            s->progress = .95 * (w->m - g->m_min + .9 + .1 * w->next_r / nr) / modes;
            return 1;
        }
        s->window_power += w->single.window_power;
        s->spectral_power += w->single.spectral_power;
        s->impedance += w->single.impedance;
        s->power_balance_error = fmax(s->power_balance_error, w->single.power_balance_error);
        s->radial_check_error = fmax(s->radial_check_error, w->single.radial_check_error);
        s->band_edge_fraction = fmax(s->band_edge_fraction, w->single.band_edge_fraction);
        s->impedance_edge_fraction =
            fmax(s->impedance_edge_fraction, w->single.impedance_edge_fraction);
        s->edge_ratio = fmax(s->edge_ratio, w->single.edge_ratio);
        rf_spatial_free(&w->single);
        w->m++;
        if (w->m <= g->m_max && !begin_mode(s, e, n))
            return 0;
        return 1;
    }
    if (w->final_r < nr) {
        size_t r = w->final_r++;
        RfPlasmaMaterial mat;
        if (!rf_plasma_material(&s->project, &s->plasma, s->r[r], &mat, e, n))
            return 0;
        for (size_t z = 0; z < nz; z++) {
            size_t j = z * nr + r;
            const double complex *E = s->electric + 3 * j, *B = s->magnetic + 3 * j;
            s->absorption[j] = rf_plasma_absorption(&s->project, &s->plasma, &mat, E);
            s->axial_flux[j] = .5 * creal(E[0] * conj(B[1]) - E[1] * conj(B[0])) / RF_MU0;
            if (!isfinite(s->absorption[j]) || !isfinite(s->axial_flux[j]))
                return fail(e, n, "全模式吸收或能流非有限值");
            for (size_t comp = 0; comp < 3; comp++)
                if (!isfinite(cabs(E[comp])) || !isfinite(cabs(B[comp])) ||
                    !isfinite(cabs(s->current[3 * j + comp])))
                    return fail(e, n, "全模式场或电流非有限值");
        }
        s->progress = .95 + .05 * w->final_r / nr;
        return 1;
    }
    s->complete = 1;
    s->progress = 1;
    free_work(s);
    return 1;
}
/* 单次推进有界工作量；失败后清理全部部分结果，禁止将未完成的和当成总场。 */
int rf_full_step(RfFullSolution *s, char *e, size_t n)
{
    if (!s || (!s->work && !s->complete))
        return fail(e, n, "没有全模式计算任务");
    if (s->complete)
        return 1;
    if (advance(s, e, n))
        return 1;
    rf_full_free(s);
    return 0;
}
/* 以同步循环运行相同的增量任务，供独立回归与无界面调用使用。 */
int rf_full_solve(
    const Project *p, const Plasma *c, const RfFullConfig *g, RfFullSolution *s, char *e, size_t n)
{
    if (!rf_full_begin(p, c, g, s, e, n))
        return 0;
    while (!s->complete)
        if (!rf_full_step(s, e, n))
            return 0;
    return 1;
}
