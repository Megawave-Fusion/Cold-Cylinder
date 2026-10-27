/* 径向全矢量 Maxwell 内核，对应根目录 PDF 第 2、3 章。
 *
 * 约定：exp(i*m*phi+i*kz*z-i*omega*t)，峰值复相量；omega 是实数。
 * 方程：curl curl E_hat-k0² epsilon_r(r) E_hat=i omega mu0 J_ant_hat。
 * Fourier 只解耦不同 (m,kz)。每个模态内保留三个电场分量的全部耦合。
 *
 * 弱式：∫ r [(curl v)*·curl E-k0² v*·epsilon_r E] dr
 *       =i omega mu0 ∫ r v*·J_ant dr。
 * 采用 PDF (2.33)–(2.41) 的相容矢量基：Er 单元独立，r Ephi 和 Ez 连续。
 * 轴心首单元用模式幂次基；外壁只约束 Ephi/Ez。介质和源面为网格节点。
 * 不求逆局部介电张量，不添加散度罚项，不添加人工虚部回避物理极点。
 *
 * 材料吸收与源供功分别从体积分和复源内积计算。单位：E_hat[V]，
 * B_hat[T m]，J_hat[A/m]，q_spec[W/m]，dP/dkz[W m]。
 * 按本 PDF 的 Fourier 归一化，功率径向积分没有额外的 2*pi。
 */
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE
#include "maxwell.h"
#include "geometry.h"
#include "vacuum.h"
#include "plasma.h"
#include "rf_quadrature.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.141592653589793238462643383279502884
#define MU0 RF_MU0
#define C0 299792458.0

static int fail(char *error, size_t n, const char *message)
{
    if (error && n)
        snprintf(error, n, "%s", message);
    return 0;
}
static int finite_complex(double complex z)
{
    return isfinite(creal(z)) && isfinite(cimag(z));
}
void rf_maxwell_free(RfMaxwellSolution *s)
{
    if (!s)
        return;
    RfMaxwellSolution old = *s;
    *s = (RfMaxwellSolution){ 0 };
    free(old.r);
    free(old.coefficients);
    free(old.potential);
    free(old.cumulative);
}
static int order_double(const void *a, const void *b)
{
    return (*(const double *)a > *(const double *)b) - (*(const double *)a < *(const double *)b);
}

typedef struct MeshBuilder {
    double *r;
    size_t count, capacity;
} MeshBuilder;
static int mesh_point(MeshBuilder *b, double r, double R)
{
    if (!isfinite(r) || r < 0 || r > R)
        return 1; /* 共振邻域超出内域的候选点不加入。 */
    if (b->count == b->capacity) {
        size_t capacity = b->capacity ? 2 * b->capacity : 512;
        double *next = realloc(b->r, capacity * sizeof(*next));
        if (!next)
            return 0;
        b->r = next;
        b->capacity = capacity;
    }
    b->r[b->count++] = r;
    return 1;
}
/* 当前单调密度剖面、常量 B0 至多有一个内部 S0 简单根。
 * 正碰撞层宽 ell=Im S/|S0'|，PDF (4.20)。在窄层内放置密集点，
 * 向外按几何级数接回普通网格，避免固定均匀网格漏掉集中吸收。
 * 无碰撞简单根不能通过一个有限网格假装得到分布解，必须走正碰撞延拓。 */
static int resonance_mesh(const Project *p, const Plasma *c, MeshBuilder *b, char *error, size_t n)
{
    if (!c || c->density == 0.)
        return 1;
    Plasma collisionless = *c;
    collisionless.solve_mode = RF_PLASMA_DIRECT_RESONANCE;
    RfPlasmaMaterial ma, mb;
    int lossless = c->solve_mode == RF_PLASMA_DIRECT_RESONANCE ||
                   (c->electron_collision == 0. && c->ion_collision == 0.);
    char local_error[256];
    if (!rf_plasma_material(p, &collisionless, 0, &ma, local_error, sizeof(local_error)) ||
        !rf_plasma_material(p, &collisionless, c->radius, &mb, local_error, sizeof(local_error))) {
        /* 有限碰撞的回旋响应可为有限数；这里只能跳过 S0 简单根预定位，
         * 不能因为辅助的零碰撞材料奇异而拒绝实际的正碰撞方程。 */
        return lossless ? fail(error, n, local_error) : 1;
    }
    double f0 = creal(ma.S), f1 = creal(mb.S);
    if (c->profile == RF_PLASMA_UNIFORM) {
        if (lossless && fabs(f0) < 64 * DBL_EPSILON * fmax(1., cabs(ma.D)))
            return fail(
                error, n, "无碰撞 S=0 整段退化；需指定正碰撞并检查极限，不能直接作普通有限元求解");
        return 1;
    }
    if (lossless && (fabs(f0) < 64 * DBL_EPSILON || fabs(f1) < 64 * DBL_EPSILON))
        return fail(error, n, "S0 零点与轴心或材料边界重合；不能套用内部简单共振求解");
    double root, slope;
    int kind = rf_plasma_resonance(p, c, &root, &slope, error, n);
    if (kind < 0)
        return 0;
    if (!kind)
        return 1;
    if (lossless)
        return fail(error,
            n,
            "检测到内部 S0=0 共振层；无碰撞场不是普通平方可积函数，请使用正碰撞或极限吸收延拓");
    RfPlasmaMaterial at_root;
    if (!rf_plasma_material(p, c, root, &at_root, error, n))
        return 0;
    double ell = fabs(cimag(at_root.S) / slope), h = p->radius / p->radial_cells;
    if (!isfinite(ell) || ell <= 64 * DBL_EPSILON * p->radius)
        return fail(error, n, "共振层宽已低于径向浮点分辨率；需增加物理碰撞或使用局部奇异基匹配");
    if (ell >= h)
        return 1;
    if (!mesh_point(b, root, p->radius))
        return 0;
    /* 随基础网格同时加密层内网格，保证径向收敛检查也细化窄层。 */
    int divisions = p->radial_cells / 16;
    if (divisions < 4)
        divisions = 4;
    double step = ell / divisions;
    for (int j = 1; j <= 16 * divisions; ++j)
        for (int sign = -1; sign <= 1; sign += 2)
            if (!mesh_point(b, root + sign * j * step, p->radius))
                return 0;
    for (double offset = 16. * ell; offset < 4. * h; offset *= 1.35)
        for (int sign = -1; sign <= 1; sign += 2)
            if (!mesh_point(b, root + sign * offset, p->radius))
                return 0;
    return 1;
}
/* 冷等离子体同时包含 H/TG 两支；仅分辨共振层宽还不足以分辨 TG
 * 的短径向波长。局部均匀色散式为 S Q²-B Q+C=0，Q=(kperp/k0)²，
 * B=(S+P)(S-nz²)-D²，C=P[(S-nz²)²-D²]。用两根的最大模限制单元
 * 相位/衰减增量。该预加密只控制分辨率，最终精度仍由加密比较判断。 */
static double radial_wavenumber(const Project *p, const Plasma *c, double r)
{
    double k0 = 2 * PI * p->frequency_mhz * 1e6 / C0;
    if (!c || c->density == 0. || r > c->radius)
        return sqrt(fabs(k0 * k0 - p->kz * p->kz));
    RfPlasmaMaterial mat;
    char error[128];
    if (!rf_plasma_material(p, c, r, &mat, error, sizeof(error)))
        return NAN;
    double nz2 = pow(p->kz / k0, 2);
    double complex b = (mat.S + mat.P) * (mat.S - nz2) - mat.D * mat.D;
    double complex cc = mat.P * ((mat.S - nz2) * (mat.S - nz2) - mat.D * mat.D);
    if (cabs(mat.S) < 1e-280)
        return NAN;
    double complex disc = csqrt(b * b - 4. * mat.S * cc);
    double complex q1 = (b + disc) / (2. * mat.S), q2 = (b - disc) / (2. * mat.S);
    return k0 * sqrt(fmax(cabs(q1), cabs(q2)));
}
static int resolve_waves(
    const Project *p, const Plasma *c, MeshBuilder *b, size_t original_count, char *error, size_t n)
{
    double *original = malloc(original_count * sizeof(*original));
    if (!original)
        return fail(error, n, "out of memory for wave-resolution mesh");
    memcpy(original, b->r, original_count * sizeof(*original));
    b->count = original_count;
    double target = .2 * 256. / p->radial_cells;
    for (size_t j = 1; j < original_count; ++j) {
        double left = original[j - 1], h = original[j] - left, maxwave = 0.;
        for (int q = 1; q <= 3; ++q) {
            double wave = radial_wavenumber(p, c, left + h * q / 4.);
            if (!isfinite(wave)) {
                free(original);
                return fail(error, n, "local cold-plasma radial dispersion is non-finite");
            }
            maxwave = fmax(maxwave, wave);
        }
        double required = ceil(h * maxwave / target);
        if (required > 131072 || b->count + required > 131072) {
            free(original);
            return fail(
                error, n, "所需 Maxwell 径向分辨率超过 131072 节点；当前参数下未返回欠分辨的场");
        }
        size_t pieces = (size_t)fmax(1., required);
        for (size_t k = 1; k < pieces; ++k)
            if (!mesh_point(b, left + h * k / pieces, p->radius)) {
                free(original);
                return fail(error, n, "out of memory while resolving radial waves");
            }
    }
    free(original);
    return 1;
}
static void sort_mesh_points(MeshBuilder *mesh, double radius)
{
    qsort(mesh->r, mesh->count, sizeof(*mesh->r), order_double);
    size_t count = 0;
    for (size_t i = 0; i < mesh->count; ++i)
        if (!count || mesh->r[i] - mesh->r[count - 1] > 32 * DBL_EPSILON * radius)
            mesh->r[count++] = mesh->r[i];
    mesh->count = count;
}
static int build_mesh(
    const Project *p, const Plasma *c, RfMaxwellSolution *s, char *error, size_t n)
{
    MeshBuilder b = { 0 };
    for (int j = 0; j <= p->radial_cells; ++j)
        if (!mesh_point(&b, p->radius * j / p->radial_cells, p->radius))
            goto memory;
    if (!mesh_point(&b, p->antenna_radius, p->radius))
        goto memory;
    if (p->type == RF_ANT_LINE || p->type == RF_ANT_PARTIAL_LOOP || p->type == RF_ANT_HALF_NAGOYA ||
        p->type == RF_ANT_HALF_ARRAY)
        if (!mesh_point(&b, p->return_radius, p->radius))
            goto memory;
    if (p->type == RF_ANT_PLANAR_SPIRAL || p->type == RF_ANT_SEGMENTED_RINGS) {
        if (!mesh_point(&b, p->inner_radius, p->radius)) goto memory;
        if (p->type == RF_ANT_SEGMENTED_RINGS)
            for (int j=1;j<p->elements;++j)
                if (!mesh_point(&b, p->inner_radius +
                    (p->antenna_radius-p->inner_radius)*j/(p->elements-1.), p->radius))
                    goto memory;
    }
    if (c && c->density > 0. && !mesh_point(&b, c->radius, p->radius))
        goto memory;
    if (!resonance_mesh(p, c, &b, error, n)) {
        free(b.r);
        return 0;
    }
    sort_mesh_points(&b, p->radius);
    if (!resolve_waves(p, c, &b, b.count, error, n)) {
        free(b.r);
        return 0;
    }
    sort_mesh_points(&b, p->radius);
    s->r = b.r;
    s->nodes = b.count;
    return 1;
memory:
    free(b.r);
    return fail(error, n, "out of memory for Maxwell radial mesh");
}

/* 五个局部自由度顺序：(D_j,P_j,Z_j,P_{j+1},Z_{j+1})。
 * curl 基解析计算，特别在轴心先消去相反的 1/r 项，绝不以小半径截断。 */
static void basis(const RfMaxwellSolution *s,
    size_t element,
    double r,
    size_t index[5],
    double complex v[5][3],
    double complex curl[5][3])
{
    double a = s->r[element], h = s->r[element + 1] - a, x = (r - a) / h, k = s->project.kz;
    int m = s->project.selected_m, l = abs(m);
    index[0] = 3 * element + 2;
    index[1] = 3 * element;
    index[2] = 3 * element + 1;
    index[3] = 3 * element + 3;
    index[4] = 3 * element + 4;
    memset(v, 0, 15 * sizeof(double complex));
    memset(curl, 0, 15 * sizeof(double complex));
    if (element > 0) {
        v[0][0] = 1.;
        v[1][1] = a * (1. - x) / r;
        v[2][2] = 1. - x;
        v[3][1] = s->r[element + 1] * x / r;
        v[4][2] = x;
        for (int q = 0; q < 5; ++q) {
            curl[q][0] = I * m * v[q][2] / r - I * k * v[q][1];
            curl[q][1] = I * k * v[q][0];
            curl[q][2] = -I * m * v[q][0] / r;
        }
        curl[2][1] += 1. / h;
        curl[4][1] -= 1. / h;
        curl[1][2] -= a / (h * r);
        curl[3][2] += s->r[element + 1] / (h * r);
    } else if (m == 0) {
        v[0][0] = x;
        v[3][1] = x;
        v[2][2] = 1. - x * x;
        v[4][2] = x * x;
        curl[0][1] = I * k * x;
        curl[3][0] = -I * k * x;
        curl[3][2] = 2. / h;
        curl[2][1] = 2. * x / h;
        curl[4][1] = -2. * x / h;
    } else {
        double low = pow(x, l - 1), high = pow(x, l + 1), longitudinal = pow(x, l);
        v[0][0] = high;
        v[3][0] = -I * (double)l / m * low;
        v[3][1] = low;
        v[4][2] = longitudinal;
        curl[0][1] = I * k * high;
        curl[0][2] = -I * m * longitudinal / h;
        curl[3][0] = -I * k * low;
        curl[3][1] = k * (double)l / m * low;
        curl[4][0] = I * m * low / h;
        curl[4][1] = -l * low / h;
    }
}
static size_t find_element(const RfMaxwellSolution *s, double r, int side)
{
    size_t left = 0, right = s->nodes - 1;
    while (left + 1 < right) {
        size_t middle = (left + right) / 2;
        if (s->r[middle] < r || (side > 0 && s->r[middle] == r))
            left = middle;
        else
            right = middle;
    }
    return left;
}
static void evaluate_element(
    const RfMaxwellSolution *s, size_t element, double r, double complex e[3], double complex b[3])
{
    size_t ix[5];
    double complex v[5][3], curl[5][3];
    double omega = 2. * PI * s->project.frequency_mhz * 1e6;
    basis(s, element, r, ix, v, curl);
    for (int k = 0; k < 3; ++k) {
        e[k] = b[k] = 0.;
        for (int q = 0; q < 5; ++q) {
            e[k] += s->coefficients[ix[q]] * v[q][k];
            b[k] += s->coefficients[ix[q]] * curl[q][k] / (I * omega);
        }
    }
}
int rf_maxwell_evaluate(
    const RfMaxwellSolution *s, double r, int side, double complex e[3], double complex b[3])
{
    if (!s || !s->complete || !e || !b || !isfinite(r) || r < 0 || r > s->project.radius)
        return 0;
    evaluate_element(s, find_element(s, r, side), r, e, b);
    return 1;
}

static int material(
    const RfMaxwellSolution *s, double r, RfPlasmaMaterial *m, char *error, size_t n)
{
    if (s->has_plasma)
        return rf_plasma_material(&s->project, &s->plasma, r, m, error, n);
    memset(m, 0, sizeof(*m));
    m->S = m->P = 1.;
    return 1;
}
static int assemble(const RfMaxwellSolution *s, RfComplexBandMatrix *a, char *error, size_t n)
{
    double x[64] = { 0 }, w[64] = { 0 }, k0 = 2. * PI * s->project.frequency_mhz * 1e6 / C0;
    size_t nq = s->quadrature_order;
    if (!rf_gauss_legendre(nq, x, w))
        return fail(error, n, "invalid Maxwell quadrature rule");
    for (size_t element = 0; element + 1 < s->nodes; ++element) {
        double h = s->r[element + 1] - s->r[element];
        for (size_t q = 0; q < nq; ++q) {
            double r = s->r[element] + (x[q] + 1.) * h / 2., weight = r * h * w[q] / 2.;
            size_t ix[5];
            double complex v[5][3], curl[5][3];
            RfPlasmaMaterial m;
            basis(s, element, r, ix, v, curl);
            if (!material(s, r, &m, error, n))
                return 0;
            for (int row = 0; row < 5; ++row)
                for (int col = 0; col < 5; ++col) {
                    double complex mass =
                        conj(v[row][0]) * (m.S * v[col][0] - I * m.D * v[col][1]) +
                        conj(v[row][1]) * (I * m.D * v[col][0] + m.S * v[col][1]) +
                        conj(v[row][2]) * m.P * v[col][2];
                    double complex stiffness = 0.;
                    for (int k = 0; k < 3; ++k)
                        stiffness += conj(curl[row][k]) * curl[col][k];
                    if (!rf_complex_band_add(
                            a, ix[row], ix[col], weight * (stiffness - k0 * k0 * mass)))
                        return fail(error, n, "non-finite Maxwell element matrix");
                }
        }
    }
    return 1;
}

typedef struct SourceLoad {
    RfMaxwellSolution *solution;
    double complex *rhs;
    double norm;
} SourceLoad;
static int load_source(void *context, double r, const double complex dl[3])
{
    SourceLoad *load = context;
    RfMaxwellSolution *s = load->solution;
    if (!(r > 0 && r < s->project.radius))
        return 0;
    size_t element = find_element(s, r, 0), ix[5];
    double complex v[5][3], curl[5][3];
    basis(s, element, r, ix, v, curl);
    double omega = 2. * PI * s->project.frequency_mhz * 1e6;
    for (int j = 0; j < 5; ++j) {
        double complex dot = 0.;
        for (int k = 0; k < 3; ++k)
            dot += conj(v[j][k]) * dl[k];
        double complex value = I * omega * MU0 * dot;
        load->rhs[ix[j]] += value;
        load->norm += cabs(value);
    }
    for (int k = 0; k < 3; ++k)
        s->source[k] += dl[k] / s->project.antenna_radius;
    return isfinite(load->norm);
}
/* 连续无损真空圆管的极点，PDF §2.4.6。不能靠网格误差把极点变成一个
 * 很高却有限的谱峰。只有源对本征电场投影非零时才拒绝受迫稳态解。 */
typedef struct PoleProjection {
    int m, te;
    double q, k;
    double complex value;
    double norm;
} PoleProjection;
static int project_pole(void *context, double r, const double complex dl[3])
{
    PoleProjection *p = context;
    int l = abs(p->m);
    double x = p->q * r;
    double j = jn(l, x), derivative = (l / x) * j - jn(l + 1, x);
    double complex e[3];
    if (p->te) {
        e[0] = -I * p->m * j / r;
        e[1] = p->q * derivative;
        e[2] = 0.;
    } else {
        e[0] = I * p->k * p->q * derivative;
        e[1] = -p->k * p->m * j / r;
        e[2] = p->q * p->q * j;
    }
    for (int k = 0; k < 3; ++k) {
        double complex term = conj(e[k]) * dl[k];
        p->value += term;
        p->norm += cabs(term);
    }
    return 1;
}
static int vacuum_pole(const Project *p, const Geometry *g, char *error, size_t n)
{
    double k0 = 2 * PI * p->frequency_mhz * 1e6 / C0, q2 = k0 * k0 - p->kz * p->kz;
    if (q2 <= 0)
        return 1;
    double q = sqrt(q2), x = q * p->radius;
    int l = abs(p->selected_m);
    if (x < 1e-5)
        return 1;
    double j = jn(l, x), derivative = (l / x) * j - jn(l + 1, x), scale = hypot(j, derivative);
    if (scale == 0.)
        return 1;
    for (int te = 0; te < 2; ++te)
        if (fabs(te ? derivative : j) < 1e-10 * scale) {
            PoleProjection projection = { .m = p->selected_m, .te = te, .q = q, .k = p->kz };
            if (!geometry_integrate_source(p, g, NULL, 0, project_pole, &projection, error, n))
                return 0;
            if (cabs(projection.value) > 1e-9 * projection.norm)
                return fail(error,
                    n,
                    te ? "受激无损 TE 谱极点：该 (m,kz,omega) 没有有限稳态解"
                       : "受激无损 TM 谱极点：该 (m,kz,omega) 没有有限稳态解");
        }
    return 1;
}
static void constrain(RfComplexBandMatrix *a, double complex *rhs, size_t index)
{
    size_t first = index > 4 ? index - 4 : 0, last = (size_t)fmin(a->n - 1, index + 4);
    for (size_t j = first; j <= last; ++j) {
        (void)rf_complex_band_set(a, index, j, 0.);
        (void)rf_complex_band_set(a, j, index, 0.);
    }
    (void)rf_complex_band_set(a, index, index, 1.);
    rhs[index] = 0.;
}
/* 体吸收在单元内部对解析有限元场积分。不能用显示网格的梯形积分替代：
 * 显示采样可能既没落在材料界面，也没有分辨窄共振层。 */
static double absorption_interval(
    const RfMaxwellSolution *s, size_t element, double left, double right)
{
    if (!s->has_plasma || s->plasma.density == 0. || right <= left)
        return 0.;
    double gx[64] = { 0 }, gw[64] = { 0 }, value = 0.;
    if (!rf_gauss_legendre(s->quadrature_order, gx, gw))
        return NAN;
    for (size_t q = 0; q < s->quadrature_order; ++q) {
        double r = (left + right) / 2. + (right - left) * gx[q] / 2.;
        double complex e[3], b[3];
        RfPlasmaMaterial m;
        char error[128];
        evaluate_element(s, element, r, e, b);
        if (!material(s, r, &m, error, sizeof(error)))
            return NAN;
        double local = rf_plasma_absorption(&s->project, &s->plasma, &m, e);
        value += r * local * (right - left) * gw[q] / 2.;
    }
    return value;
}
double rf_maxwell_cumulative(const RfMaxwellSolution *s, double r)
{
    if (!s || !s->complete || !isfinite(r) || r < 0 || r > s->project.radius)
        return NAN;
    size_t element = find_element(s, r, 0);
    return s->cumulative[element] + absorption_interval(s, element, s->r[element], r);
}
/* 只负责对角平衡与带状求解；原矩阵和源向量留给独立残差及供功检查。 */
static int solve_scaled_system(const RfComplexBandMatrix *matrix,
    const double complex *rhs,
    double complex *coefficients,
    char *error,
    size_t n)
{
    size_t dofs = matrix->n;
    double norm_rhs = 0.;
    for (size_t i = 0; i < dofs; ++i)
        norm_rhs = fmax(norm_rhs, cabs(rhs[i]));
    /* Only an exactly zero assembled RHS admits this zero-field shortcut.
     * Symmetry can leave roundoff-sized loads: skipping their solve and then
     * checking Ax=b against that nonzero b would falsely report eta=1. */
    if (norm_rhs == 0.)
        return 1;
    RfComplexBandMatrix scaled = { 0 };
    double *scale = calloc(dofs, sizeof(*scale));
    double complex *scaled_rhs = calloc(dofs, sizeof(*scaled_rhs));
    double complex *scaled_answer = calloc(dofs, sizeof(*scaled_answer));
    int ok = 0;
    if (!scale || !scaled_rhs || !scaled_answer || !rf_complex_band_init(&scaled, dofs, 4, 4)) {
        fail(error, n, "out of memory for Maxwell system");
        goto done;
    }
    /* 对称对角平衡 A_s=D A D，b_s=D b，最终 u=D y。
     * 这只是数值变量缩放，不修改物理方程、源功或介电系数。 */
    for (size_t i = 0; i < dofs; ++i) {
        double row = 0.;
        size_t first = i > 4 ? i - 4 : 0, last = (size_t)fmin(dofs - 1, i + 4);
        for (size_t j = first; j <= last; ++j)
            row += cabs(rf_complex_band_get(matrix, i, j));
        scale[i] = 1. / sqrt(fmax(row, DBL_MIN));
        scaled_rhs[i] = scale[i] * rhs[i];
    }
    for (size_t i = 0; i < dofs; ++i) {
        size_t first = i > 4 ? i - 4 : 0, last = (size_t)fmin(dofs - 1, i + 4);
        for (size_t j = first; j <= last; ++j)
            (void)rf_complex_band_set(
                &scaled, i, j, scale[i] * rf_complex_band_get(matrix, i, j) * scale[j]);
    }
    double scaled_error;
    RfLinearStatus status =
        rf_complex_band_solve(&scaled, scaled_rhs, scaled_answer, &scaled_error);
    if (status != RF_LINEAR_OK) {
        fail(error, n, "Maxwell 复数带状系统奇异或消元失败");
        goto done;
    }
    for (size_t i = 0; i < dofs; ++i)
        coefficients[i] = scale[i] * scaled_answer[i];
    ok = 1;
done:
    free(scale);
    free(scaled_rhs);
    free(scaled_answer);
    rf_complex_band_free(&scaled);
    return ok;
}

/* Scalar H1 basis whose gradients lie in the compatible Maxwell space.
 * The first element uses r^|m| (m!=0) or {1-r²/h²,r²/h²} (m=0).
 * All axis limits are analytic; no small-radius or small-k regularization. */
static void potential_basis(const RfMaxwellSolution *s, size_t element, double r,
    double value[2], double complex gradient[2][3])
{
    double a = s->r[element], h = s->r[element + 1] - a, x = (r - a) / h;
    int m = s->project.selected_m, l = abs(m);
    double k = s->project.kz;
    memset(gradient, 0, 6 * sizeof(double complex));
    if (element > 0) {
        value[0] = 1. - x;
        value[1] = x;
        gradient[0][0] = -1. / h;
        gradient[1][0] = 1. / h;
        for (int q = 0; q < 2; ++q) {
            gradient[q][1] = I * m * value[q] / r;
            gradient[q][2] = I * k * value[q];
        }
    } else if (m == 0) {
        value[0] = 1. - x * x;
        value[1] = x * x;
        gradient[0][0] = -2. * x / h;
        gradient[1][0] = 2. * x / h;
        gradient[0][2] = I * k * value[0];
        gradient[1][2] = I * k * value[1];
    } else {
        value[0] = 0.;
        value[1] = pow(x, l);
        double low = pow(x, l - 1) / h;
        gradient[1][0] = l * low;
        gradient[1][1] = I * m * low;
        gradient[1][2] = I * k * value[1];
    }
}
int rf_maxwell_prepare_potential(RfMaxwellSolution *s, char *error, size_t n)
{
    if (!s || !s->complete || s->nodes < 2 || !s->coefficients)
        return fail(error, n, "电势提取需要完整 Maxwell 场");
    if (s->potential)
        return 1;
    RfComplexBandMatrix matrix = { 0 };
    double complex *rhs = calloc(s->nodes, sizeof(*rhs));
    double complex *answer = calloc(s->nodes, sizeof(*answer));
    int ok = 0;
    if (!rhs || !answer || !rf_complex_band_init(&matrix, s->nodes, 1, 1)) {
        fail(error, n, "电势泊松系统内存不足");
        goto done;
    }
    double gx[64], gw[64];
    if (!rf_gauss_legendre(s->quadrature_order, gx, gw)) {
        fail(error, n, "电势求积阶数无效");
        goto done;
    }
    /* ∫ r grad(v)*·grad(Phi) dr = -∫ r grad(v)*·E dr.
     * This includes interface charge through the weak RHS; never differentiate
     * E samples across dielectric interfaces or antenna sheets. */
    for (size_t element = 0; element + 1 < s->nodes; ++element) {
        double a = s->r[element], h = s->r[element + 1] - a;
        for (size_t q = 0; q < s->quadrature_order; ++q) {
            double r = a + (gx[q] + 1.) * h / 2., weight = r * h * gw[q] / 2.;
            double value[2];
            double complex gradient[2][3], E[3], B[3];
            potential_basis(s, element, r, value, gradient);
            evaluate_element(s, element, r, E, B);
            for (int row = 0; row < 2; ++row) {
                for (int c = 0; c < 3; ++c)
                    rhs[element + row] -= weight * conj(gradient[row][c]) * E[c];
                for (int col = 0; col < 2; ++col) {
                    double complex entry = 0.;
                    for (int c = 0; c < 3; ++c)
                        entry += conj(gradient[row][c]) * gradient[col][c];
                    if (!rf_complex_band_add(&matrix, element + row, element + col,
                            weight * entry)) {
                        fail(error, n, "电势泊松矩阵非有限值");
                        goto done;
                    }
                }
            }
        }
    }
    if (s->project.selected_m != 0)
        constrain(&matrix, rhs, 0);
    constrain(&matrix, rhs, s->nodes - 1); /* grounded reference Phi(R)=0 */
    if (!solve_scaled_system(&matrix, rhs, answer, error, n))
        goto done;
    double worst = 0.;
    for (size_t row = 0; row < s->nodes; ++row) {
        double complex residual = -rhs[row];
        double scale = cabs(rhs[row]);
        size_t first = row ? row - 1 : 0, last = row + 1 < s->nodes ? row + 1 : row;
        for (size_t col = first; col <= last; ++col) {
            double complex term = rf_complex_band_get(&matrix, row, col) * answer[col];
            residual += term;
            scale += cabs(term);
        }
        if (!finite_complex(answer[row])) {
            fail(error, n, "电势泊松解非有限值");
            goto done;
        }
        worst = fmax(worst, cabs(residual) / fmax(scale, DBL_MIN));
    }
    if (worst > 1e-8) {
        fail(error, n, "电势泊松解后向误差超限");
        goto done;
    }
    s->potential_backward_error = worst;
    s->potential = answer;
    answer = NULL;
    ok = 1;
done:
    free(rhs);
    free(answer);
    rf_complex_band_free(&matrix);
    return ok;
}
int rf_maxwell_evaluate_potential(const RfMaxwellSolution *s, double r, int side,
    double complex *potential, double complex longitudinal[3])
{
    if (!s || !s->complete || !s->potential || !potential || !isfinite(r) ||
        r < 0. || r > s->project.radius)
        return 0;
    size_t element = find_element(s, r, side);
    double value[2];
    double complex gradient[2][3];
    potential_basis(s, element, r, value, gradient);
    *potential = 0.;
    if (longitudinal)
        memset(longitudinal, 0, 3 * sizeof(*longitudinal));
    for (int q = 0; q < 2; ++q) {
        *potential += s->potential[element + q] * value[q];
        if (longitudinal)
            for (int c = 0; c < 3; ++c)
                longitudinal[c] -= s->potential[element + q] * gradient[q][c];
    }
    return finite_complex(*potential);
}

/* 原始受约束系统的残差和复源内积；不使用缩放矩阵评估物理质量。 */
static int check_system(RfMaxwellSolution *s,
    const RfComplexBandMatrix *matrix,
    const double complex *rhs,
    double complex *source_inner_product,
    char *error,
    size_t n)
{
    size_t dofs = matrix->n;
    const Project *p = &s->project;
    double residual = 0., denominator = 0.;
    double complex input = 0.;
    for (size_t i = 0; i < dofs; ++i) {
        double complex value = -rhs[i];
        double row_scale = cabs(rhs[i]);
        size_t first = i > 4 ? i - 4 : 0, last = (size_t)fmin(dofs - 1, i + 4);
        for (size_t j = first; j <= last; ++j) {
            double complex term = rf_complex_band_get(matrix, i, j) * s->coefficients[j];
            value += term;
            row_scale += cabs(term);
        }
        if (!finite_complex(value)) {
            fail(error, n, "Maxwell solution is non-finite");
            return 0;
        }
        residual = fmax(residual, cabs(value));
        denominator = fmax(denominator, row_scale);
        input += conj(s->coefficients[i]) * rhs[i];
    }
    s->backward_error = residual / fmax(DBL_MIN, denominator);
    if (s->backward_error > 1e-8 || !isfinite(s->backward_error)) {
        fail(error, n, "Maxwell 原系统后向误差超过 1e-8");
        return 0;
    }
    s->source_power = -cimag(input) / (2. * 2. * PI * p->frequency_mhz * 1e6 * MU0);
    /* Conjugate the exp(-i omega t) complex power to report engineering X.
     * Peak-current normalization: Re Z_hat = 2 P_src_hat / I_peak^2. */
    s->load_impedance =
        I * input / (2. * PI * p->frequency_mhz * 1e6 * MU0 * p->current * p->current);
    *source_inner_product = input;
    return 1;
}

/* 在内部单元上计算峰值与累计耗散，再比较独立得到的材料吸收和源供功。 */
static int integrate_power(RfMaxwellSolution *s, double complex input, char *error, size_t n)
{
    const Project *p = &s->project;
    for (size_t j = 0; j + 1 < s->nodes; ++j) {
        /* 峰值在内部网格估计，避免窄共振层被均匀显示采样漏掉。
         * 界面端点从左右两个单元分别取迹，保留法向场的物理跳变。 */
        for (int q = 0; q <= 4; ++q) {
            double r = s->r[j] + (s->r[j + 1] - s->r[j]) * q / 4.;
            double complex e[3], b[3];
            evaluate_element(s, j, r, e, b);
            double magnitude = 0.;
            for (int k = 0; k < 3; ++k)
                magnitude += pow(cabs(e[k]), 2);
            s->peak_electric = fmax(s->peak_electric, sqrt(magnitude));
        }
        double value = absorption_interval(s, j, s->r[j], s->r[j + 1]);
        if (!isfinite(value) || value < 0.) {
            fail(error, n, "Maxwell material absorption is non-finite or negative");
            return 0;
        }
        s->cumulative[j + 1] = s->cumulative[j] + value;
    }
    s->absorbed_power = s->cumulative[s->nodes - 1];
    /* 无损时有功供功只剩舍入误差，按复功率自然尺度归一；有可分辨吸收时
     * 用有功尺度。保留原始供功，不令 source_power=absorbed_power 制造假守恒。 */
    double apparent = cabs(input) / (2. * 2. * PI * p->frequency_mhz * 1e6 * MU0);
    double power_scale = fmax(fabs(s->source_power), s->absorbed_power);
    if (power_scale < 256 * DBL_EPSILON * apparent)
        power_scale = apparent;
    s->power_balance_error = fabs(s->source_power - s->absorbed_power) / fmax(DBL_MIN, power_scale);
    return 1;
}

int rf_maxwell_solve_order(const Project *p,
    const Plasma *c,
    size_t quadrature_order,
    RfMaxwellSolution *s,
    char *error,
    size_t n)
{
    if (!p || !s)
        return fail(error, n, "Maxwell project or destination is null");
    /* 允许用旧结果的 project/plasma 重新求解同一个结果对象。 */
    Project input_project = *p;
    Plasma input_plasma = { 0 };
    int has_plasma = c != NULL;
    if (c)
        input_plasma = *c;
    rf_maxwell_free(s);
    /* 新解在独立的零初始化对象内构造，成功后才转移所有权。旧解清理
     * 与新解失败路径不会混用同一组数组，也便于重复求解和内存审查。 */
    RfMaxwellSolution *destination = s, built = { 0 };
    s = &built;
    p = &input_project;
    c = has_plasma ? &input_plasma : NULL;
    if (!project_validate(p, error, n) || (c && !plasma_validate(c, error, n)))
        return 0;
    if (c && !(c->radius < p->antenna_radius && p->antenna_radius < p->radius))
        return fail(
            error, n, "plasma radius must be smaller than antenna radius and vessel radius");
    if (quadrature_order < 4 || quadrature_order > 64)
        return fail(error, n, "Maxwell quadrature order must be 4..64");
    s->project = *p;
    s->has_plasma = c != NULL;
    if (c)
        s->plasma = *c;
    s->quadrature_order = quadrature_order;
    Geometry geometry = { 0 };
    RfComplexBandMatrix matrix = { 0 };
    double complex *rhs = NULL;
    int ok = 0;
    if (!geometry_build(p, &geometry, error, n))
        goto done;
    if ((!c || c->density == 0.) && !vacuum_pole(p, &geometry, error, n))
        goto done;
    if (!build_mesh(p, c, s, error, n))
        goto done;

    size_t dofs = 3 * (s->nodes - 1) + 2;
    s->coefficients = calloc(dofs, sizeof(*s->coefficients));
    s->cumulative = calloc(s->nodes, sizeof(*s->cumulative));
    rhs = calloc(dofs, sizeof(*rhs));
    if (!s->coefficients || !s->cumulative || !rhs || !rf_complex_band_init(&matrix, dofs, 4, 4)) {
        fail(error, n, "out of memory for Maxwell system");
        goto done;
    }
    if (!assemble(s, &matrix, error, n))
        goto done;
    SourceLoad load = { .solution = s, .rhs = rhs };
    if (!geometry_integrate_source(p, &geometry, s->r, s->nodes, load_source, &load, error, n))
        goto done;

    /* 轴心占位 P0 必须消去；m!=0 还消去 Z0。首单元正则基保留非零横向
     * 轴上场。PEC 只约束切向场，壁面法向自由度 D_(N-1) 始终保留。 */
    constrain(&matrix, rhs, 0);
    if (p->selected_m != 0)
        constrain(&matrix, rhs, 1);
    constrain(&matrix, rhs, 3 * (s->nodes - 1));
    constrain(&matrix, rhs, 3 * (s->nodes - 1) + 1);

    double complex input;
    if (!solve_scaled_system(&matrix, rhs, s->coefficients, error, n) ||
        !check_system(s, &matrix, rhs, &input, error, n) || !integrate_power(s, input, error, n))
        goto done;
    s->complete = 1;
    ok = 1;
done:
    free(rhs);
    rf_complex_band_free(&matrix);
    geometry_free(&geometry);
    if (!ok)
        rf_maxwell_free(s);
    else
        *destination = *s;
    return ok;
}
int rf_maxwell_solve(const Project *p, const Plasma *c, RfMaxwellSolution *s, char *error, size_t n)
{
    /* |m|=16 首单元质量项最高为 r^35；32 点规则避免低阶求积低估该项。 */
    return rf_maxwell_solve_order(p, c, p && abs(p->selected_m) > 12 ? 32 : 16, s, error, n);
}
