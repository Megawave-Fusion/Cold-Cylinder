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
static int fail(char *e, size_t n, const char *message)
{
    if (e && n)
        snprintf(e, n, "%s", message);
    return 0;
}
RfFullConfig rf_full_config(const WorkbenchProject *w)
{
    return (
        RfFullConfig){ rf_spatial_config(w), w->options.m_min, w->options.m_max, w->options.phi };
}
static void free_work(RfFullSolution *s)
{
    FullWork *w = s->work;
    if (!w)
        return;
    rf_spatial_free(&w->single);
    free(w);
    s->work = NULL;
}
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
static int begin_mode(RfFullSolution *s, char *e, size_t n)
{
    FullWork *w = s->work;
    Project p = s->project;
    p.selected_m = w->m;
    w->next_r = 0;
    return rf_spatial_begin(&p, &s->plasma, &s->config.space, &w->single, e, n);
}
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
                snprintf(message, sizeof(message), "m=%d: %s", w->m, e ? e : "空间求解失败");
                return fail(e, n, message);
            }
            s->progress = .95 * (w->m - g->m_min + .9 * w->single.progress) / modes;
            return 1;
        }
        if (w->next_r < nr) {
            size_t r = w->next_r++;
            double complex phase = cexp(I * w->m * g->phi);
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
