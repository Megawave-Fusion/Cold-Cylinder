/* 独立的物理参考，不以有限元装配公式重复构造预期结果。
 * 真空：Bessel Green 解；等离子体：四切向场 RK4 射击及边界匹配。
 * 所有比较都使用复数场（保留相位），并单独检查源、轴心、PEC 和功率。 */
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE
#include "maxwell.h"
#include "vacuum.h"
#include "plasma.h"
#include "geometry.h"
#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PI 3.14159265358979323846
#define MU (4e-7 * PI)
#define C 299792458.0
static char error[256];
#define REQUIRE(x)                                                                                 \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL line %d: %s (%s)\n", __LINE__, #x, error);                       \
            abort();                                                                               \
        }                                                                                          \
    } while (0)
static double sq(double complex z)
{
    return creal(z * conj(z));
}
static void near(double complex a, double complex b, double relative, double absolute)
{
    if (cabs(a - b) > absolute + relative * cabs(b)) {
        fprintf(stderr,
            "got %.12g%+.12gi expected %.12g%+.12gi\n",
            creal(a),
            cimag(a),
            creal(b),
            cimag(b));
        abort();
    }
}
static void source(const Project *p, double complex k[3])
{
    Geometry g = { 0 };
    REQUIRE(geometry_build(p, &g, error, sizeof(error)));
    REQUIRE(geometry_current_spectrum(p, &g, k, error, sizeof(error)));
    geometry_free(&g);
}

/* 中等实自变量的独立 I_n 幂级数和 K_n 积分，PDF §2.4.2。
 * 不调用 Maxwell 代码、有限元基或线性求解器。 */
static double in(int n, double x)
{
    double term = 1.;
    for (int j = 1; j <= n; ++j)
        term *= x / (2 * j);
    double value = term;
    for (int j = 1; j < 200; ++j) {
        term *= x * x / (4. * j * (j + n));
        value += term;
        if (fabs(term) < 1e-16 * fabs(value))
            break;
    }
    return value;
}
static double kn(int n, double x)
{
    double h = 12. / 6000., sum = 0.;
    for (int j = 0; j <= 6000; ++j) {
        double t = j * h, value = exp(-x * cosh(t)) * cosh(n * t);
        sum += (j == 0 || j == 6000 ? 1 : (j % 2 ? 4 : 2)) * value;
    }
    return sum * h / 3.;
}
static void functions(int n, double r, double q2, double *f, double *fp, double *g, double *gp)
{
    double t = sqrt(fabs(q2)), x = t * r;
    if (q2 > 0) {
        *f = jn(n, x);
        *fp = t * ((double)n / x * (*f) - jn(n + 1, x));
        *g = yn(n, x);
        *gp = t * ((double)n / x * (*g) - yn(n + 1, x));
    } else {
        *f = in(n, x);
        *fp = t * (n ? .5 * (in(n - 1, x) + in(n + 1, x)) : in(1, x));
        *g = kn(n, x);
        *gp = -t * (n ? .5 * (kn(n - 1, x) + kn(n + 1, x)) : kn(1, x));
    }
}
static void green(int n,
    int neumann,
    double r,
    double b,
    double R,
    double q2,
    double *value,
    double *dr,
    double *db,
    double *drdb)
{
    double fw, dfw, gw, dgw, fr, dfr, gr, dgr, fb, dfb, gb, dgb;
    functions(n, R, q2, &fw, &dfw, &gw, &dgw);
    functions(n, r, q2, &fr, &dfr, &gr, &dgr);
    functions(n, b, q2, &fb, &dfb, &gb, &dgb);
    double ratio = neumann ? dgw / dfw : gw / fw;
    gr -= ratio * fr;
    dgr -= ratio * dfr;
    gb -= ratio * fb;
    dgb -= ratio * dfb;
    double norm = q2 > 0 ? -PI / 2. : 1.;
    if (r < b) {
        *value = norm * fr * gb;
        *dr = norm * dfr * gb;
        *db = norm * fr * dgb;
        *drdb = norm * dfr * dgb;
    } else {
        *value = norm * fb * gr;
        *dr = norm * fb * dgr;
        *db = norm * dfb * gr;
        *drdb = norm * dfb * dgr;
    }
}
static void vacuum_reference(
    const Project *p, const double complex K[3], double r, double complex e[3])
{
    double omega = 2 * PI * p->frequency_mhz * 1e6, q2 = pow(omega / C, 2) - p->kz * p->kz;
    double gd, gdr, gdb, gdrdb, gn, gnr, gnb, gnrb;
    green(abs(p->selected_m), 0, r, p->antenna_radius, p->radius, q2, &gd, &gdr, &gdb, &gdrdb);
    green(abs(p->selected_m), 1, r, p->antenna_radius, p->radius, q2, &gn, &gnr, &gnb, &gnrb);
    double complex ez = I * omega * MU * p->antenna_radius * K[2] * gd;
    double complex ezp = I * omega * MU * p->antenna_radius * K[2] * gdr;
    double complex bz = -MU * p->antenna_radius * K[1] * gnb,
                   bzp = -MU * p->antenna_radius * K[1] * gnrb;
    e[0] = (I * p->kz * ezp - omega * p->selected_m * bz / r) / q2;
    e[1] = (-p->kz * p->selected_m * ez / r - I * omega * bzp) / q2;
    e[2] = ez;
}
static double vacuum_error(Project *p)
{
    RfVacuumSolution sol = { 0 };
    double complex K[3];
    source(p, K);
    REQUIRE(rf_vacuum_solve(p, &sol, error, sizeof(error)));
    double err = 0., norm = 0.;
    const double probes[] = { .0713, .1831, .2979, .3917, .5113, .5791 };
    for (size_t j = 0; j < sizeof(probes) / sizeof(*probes); ++j) {
        double complex expected[3];
        vacuum_reference(p, K, probes[j], expected);
        for (int k = 0; k < 3; ++k) {
            double complex actual = rf_vacuum_field(&sol, probes[j], k);
            err += sq(actual - expected[k]);
            norm += sq(expected[k]);
        }
    }
    REQUIRE(sol.backward_error < 1e-12);
    double complex e[3], b[3];
    REQUIRE(rf_maxwell_evaluate(sol.fem, p->radius, 0, e, b));
    near(e[1], 0, 0, 1e-10);
    near(e[2], 0, 0, 1e-10);
    REQUIRE(rf_maxwell_evaluate(sol.fem, 0, 0, e, b));
    if (abs(p->selected_m) == 1) {
        near(e[0], -I * e[1] / p->selected_m, 1e-12, 1e-12);
        near(e[2], 0, 0, 1e-13);
    }
    if (p->selected_m == 0) {
        near(e[0], 0, 0, 1e-13);
        near(e[1], 0, 0, 1e-13);
    }
    rf_vacuum_solution_free(&sol);
    return sqrt(err / fmax(norm, 1e-300));
}

/* 独立一阶系统使用 U=(Ephi,Ez,cBphi,cBz)，其来源是 Maxwell 的六个
 * 分量关系，完全绕开 curl-curl 弱式、有限元装配和带状消元。 */
static void ode(const Project *p,
    const Plasma *c,
    int inside,
    double r,
    const double complex u[4],
    double complex out[4])
{
    RfPlasmaMaterial mat = { .S = 1, .P = 1 };
    if (inside)
        REQUIRE(rf_plasma_material(p, c, r, &mat, error, sizeof(error)));
    double omega = 2 * PI * p->frequency_mhz * 1e6, k = p->kz, m = p->selected_m;
    double complex er = (I * mat.D * u[0] + C / omega * (k * u[2] - m * u[3] / r)) / mat.S;
    double complex cbr = C / omega * (m * u[1] / r - k * u[0]);
    out[0] = -u[0] / r + I * m * er / r + I * omega / C * u[3];
    out[1] = I * k * er - I * omega / C * u[2];
    out[2] = -u[2] / r + I * m * cbr / r - I * omega / C * mat.P * u[1];
    out[3] = I * k * cbr - omega / C * mat.D * er + I * omega / C * mat.S * u[0];
}
static void propagate(
    const Project *p, const Plasma *c, int inside, double a, double b, double complex u[4])
{
    int count = (int)ceil((b - a) / 1e-5);
    if (count < 1)
        return;
    double h = (b - a) / count;
    for (int j = 0; j < count; ++j) {
        double r = a + j * h;
        double complex k1[4], k2[4], k3[4], k4[4], temp[4];
        ode(p, c, inside, r, u, k1);
        for (int d = 0; d < 4; ++d)
            temp[d] = u[d] + h * k1[d] / 2.;
        ode(p, c, inside, r + h / 2., temp, k2);
        for (int d = 0; d < 4; ++d)
            temp[d] = u[d] + h * k2[d] / 2.;
        ode(p, c, inside, r + h / 2., temp, k3);
        for (int d = 0; d < 4; ++d)
            temp[d] = u[d] + h * k3[d];
        ode(p, c, inside, r + h, temp, k4);
        for (int d = 0; d < 4; ++d)
            u[d] += h * (k1[d] + 2 * k2[d] + 2 * k3[d] + k4[d]) / 6.;
    }
}
static void regular(const Project *p, const Plasma *c, int branch, double r, double complex u[4])
{
    double small = 1e-6 * p->radius, k0 = 2 * PI * p->frequency_mhz * 1e6 / C, m = p->selected_m;
    RfPlasmaMaterial mat;
    REQUIRE(rf_plasma_material(p, c, 0, &mat, error, sizeof(error)));
    double a = branch == 0, b = branch == 1;
    u[0] = a;
    u[1] = small * (p->kz * a / m - I * k0 * b);
    u[2] = b;
    u[3] = small * (p->kz * b / m + I * k0 * (mat.S + mat.D / m) * a);
    propagate(p, c, 1, small, fmin(c->radius, r), u);
    if (r > c->radius)
        propagate(p, c, 0, c->radius, r, u);
}
static void plasma_reference(
    const Project *p, const Plasma *c, const double complex K[3], double r, double complex e[3])
{
    double complex one[4], two[4], jump[4] = { 0, 0, C * MU * K[2], -C * MU * K[1] };
    regular(p, c, 0, p->radius, one);
    regular(p, c, 1, p->radius, two);
    propagate(p, c, 0, p->antenna_radius, p->radius, jump);
    double complex det = one[0] * two[1] - one[1] * two[0];
    double complex a = (-jump[0] * two[1] + jump[1] * two[0]) / det;
    double complex b = (-one[0] * jump[1] + one[1] * jump[0]) / det;
    regular(p, c, 0, r, one);
    regular(p, c, 1, r, two);
    for (int d = 0; d < 4; ++d)
        one[d] = a * one[d] + b * two[d];
    if (r > p->antenna_radius) {
        jump[0] = jump[1] = 0.;
        jump[2] = C * MU * K[2];
        jump[3] = -C * MU * K[1];
        propagate(p, c, 0, p->antenna_radius, r, jump);
        for (int d = 0; d < 4; ++d)
            one[d] += jump[d];
    }
    RfPlasmaMaterial mat = { .S = 1, .P = 1 };
    if (r < c->radius)
        REQUIRE(rf_plasma_material(p, c, r, &mat, error, sizeof(error)));
    double omega = 2 * PI * p->frequency_mhz * 1e6;
    e[0] = (I * mat.D * one[0] + C / omega * (p->kz * one[2] - p->selected_m * one[3] / r)) / mat.S;
    e[1] = one[0];
    e[2] = one[1];
}
static void plasma_test(void)
{
    Project p;
    Plasma c;
    project_default(&p);
    plasma_default(&c);
    p.type = RF_ANT_HELIX;
    p.turns = .5;
    p.selected_m = 1;
    p.kz = 3.;
    p.radial_cells = 512;
    c.density = 3e14;
    c.magnetic_field = .002;
    c.electron_collision = 1e7;
    c.ion_collision = 1e5;
    c.edge_ratio = .5;
    RfPlasmaSolution sol = { 0 };
    double complex K[3];
    source(&p, K);
    REQUIRE(rf_plasma_solve(&p, &c, &sol, error, sizeof(error)));
    double err = 0., norm = 0.;
    const double probes[] = { .0831, .1719, .2813, .3517, .5113 };
    for (size_t j = 0; j < sizeof(probes) / sizeof(*probes); ++j) {
        double complex expected[3];
        plasma_reference(&p, &c, K, probes[j], expected);
        for (int k = 0; k < 3; ++k) {
            err += sq(rf_plasma_field(&sol, probes[j], k) - expected[k]);
            norm += sq(expected[k]);
        }
    }
    printf("magnetized nonuniform plasma / independent 4-ODE: relative E error %.6g, power balance "
           "%.3g\n",
        sqrt(err / norm),
        sol.power_balance_error);
    REQUIRE(sqrt(err / norm) < .005);
    REQUIRE(sol.power_balance_error < 1e-6);
    REQUIRE(sol.absorbed_power > 0);
    near(sol.source_power, sol.absorbed_power, 1e-6, 1e-10);
    near(sol.cumulative[sol.nr - 1], sol.absorbed_power, 1e-13, 1e-14);
    double complex em[3], ep[3], bm[3], bp[3];
    REQUIRE(rf_maxwell_evaluate(sol.fem, c.radius, -1, em, bm));
    REQUIRE(rf_maxwell_evaluate(sol.fem, c.radius, 1, ep, bp));
    near(em[1], ep[1], 1e-12, 1e-12);
    near(em[2], ep[2], 1e-12, 1e-12);
    near(bm[0], bp[0], 1e-12, 1e-16);
    double flux = .5 * creal(em[1] * conj(bm[2]) - em[2] * conj(bm[1])) / RF_MU0;
    double defect = fabs(sol.absorbed_power + c.radius * flux) / sol.absorbed_power;
    printf("independent radial Poynting/material integral defect %.6g\n", defect);
    REQUIRE(defect < .02);
    double power = sol.absorbed_power;
    p.current = 2;
    REQUIRE(rf_plasma_solve(&p, &c, &sol, error, sizeof(error)));
    near(sol.absorbed_power, 4 * power, 1e-9, 1e-12);
    c.density = 0;
    RfVacuumSolution vac = { 0 };
    REQUIRE(rf_vacuum_solve(&p, &vac, error, sizeof(error)));
    REQUIRE(rf_plasma_solve(&p, &c, &sol, error, sizeof(error)));
    for (size_t i = 0; i < 3 * sol.nr; ++i)
        near(sol.electric[i], vac.electric[i], 1e-11, 1e-11);
    REQUIRE(sol.absorbed_power == 0.);
    rf_vacuum_solution_free(&vac);
    rf_plasma_solution_free(&sol);
}
static void source_and_pole_tests(void)
{
    Project p;
    project_default(&p);
    p.type = RF_ANT_HELIX;
    p.turns = .5;
    p.selected_m = 1;
    p.kz = 0.;
    p.band_width = 0.;
    double complex k[3];
    source(&p, k);
    near(k[2], -I * 2 * p.current * p.antenna_length / (PI * PI * p.antenna_radius), 1e-12, 1e-13);
    /* 单环 Kphi=I sinc(kz W/2)，带宽必须进入数值源。 */
    project_default(&p);
    p.selected_m = 0;
    p.kz = 38.;
    source(&p, k);
    near(k[1], sin(p.kz * p.band_width / 2.) / (p.kz * p.band_width / 2.), 1e-13, 1e-13);
    RfVacuumSolution vac = { 0 };
    p.kz = 0.;
    p.band_width = 0.;
    /* m=0 TE 第一根 J1(x)=0，单环在 b 处投影非零。 */
    p.frequency_mhz = C * 3.8317059702075125 / (2 * PI * p.radius * 1e6);
    REQUIRE(!rf_vacuum_solve(&p, &vac, error, sizeof(error)));
    REQUIRE(strstr(error, "TE"));
    rf_vacuum_solution_free(&vac);
    /* 半 Nagoya 汇总 Kz=0，但位于不同半径的轴向源不能从右端删除。 */
    project_default(&p);
    p.type = RF_ANT_HALF_NAGOYA;
    p.selected_m = 1;
    p.kz = 3.;
    source(&p, k);
    near(k[2], 0, 0, 1e-12);
    REQUIRE(rf_vacuum_solve(&p, &vac, error, sizeof(error)));
    REQUIRE(cabs(rf_vacuum_field(&vac, .2, 2)) > 1e-6);
    rf_vacuum_solution_free(&vac);
}
/* 对闭合规定电流，任意模态标量势的梯度都应与源正交。
 * 这项测试同时覆盖转角、真实带宽、径向馈线和多半径回流。 */
typedef struct GradientCheck {
    int m;
    double k;
    double complex sum;
    double norm;
} GradientCheck;
static int gradient_source(void *context, double r, const double complex dl[3])
{
    GradientCheck *c = context;
    double complex gradient[3] = { 2 * r, -I * c->m * r, -I * c->k * r * r };
    for (int k = 0; k < 3; ++k) {
        double complex value = gradient[k] * dl[k];
        c->sum += value;
        c->norm += cabs(value);
    }
    return 1;
}
static void conservation_tests(void)
{
    Project p;
    project_default(&p);
    p.kz = 11.3;
    p.selected_m = -3;
    for (int type = RF_ANT_STRAP; type <= RF_ANT_HALF_ARRAY; ++type) {
        p.type = type;
        Geometry g = { 0 };
        REQUIRE(geometry_build(&p, &g, error, sizeof(error)));
        GradientCheck check = { .m = p.selected_m, .k = p.kz };
        REQUIRE(geometry_integrate_source(
            &p, &g, NULL, 0, gradient_source, &check, error, sizeof(error)));
        REQUIRE(cabs(check.sum) < 2e-12 * fmax(check.norm, 1e-20));
        geometry_free(&g);
    }
    /* 轴向带宽在高波数也不得混叠。 */
    project_default(&p);
    p.selected_m = 0;
    p.kz = 1999.;
    p.band_width = .2;
    double complex k[3];
    source(&p, k);
    near(k[1], sin(p.kz * p.band_width / 2.) / (p.kz * p.band_width / 2.), 1e-11, 1e-12);
    /* 真空的实际 PEC 法向电场通常非零，不能再被设成 Dirichlet。 */
    project_default(&p);
    p.type = RF_ANT_HELIX;
    p.selected_m = 1;
    RfVacuumSolution v = { 0 };
    REQUIRE(rf_vacuum_solve(&p, &v, error, sizeof(error)));
    REQUIRE(cabs(v.electric[v.nr - 1]) > 1e-5);
    REQUIRE(rf_vacuum_solve(&v.project, &v, error, sizeof(error)));
    REQUIRE(rf_maxwell_solve(&v.fem->project, NULL, v.fem, error, sizeof(error)));
    rf_vacuum_solution_free(&v);
    /* 带内部分选主元必须在扩展的上带中保留填充，和稠密参考相同。 */
    enum { N = 37 };
    RfComplexMatrix dense = { 0 };
    RfComplexBandMatrix band = { 0 };
    double complex rhs[N], a[N], b[N];
    double eta;
    REQUIRE(rf_complex_matrix_init(&dense, N, N));
    REQUIRE(rf_complex_band_init(&band, N, 4, 3));
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            if (i - j <= 4 && j - i <= 3) {
                double complex x =
                    (i == j ? 1. : 0.) + sin(1.3 * i + .7 * j) + I * cos(.41 * i - .9 * j);
                if (i == j && i % 4 == 0)
                    x = 0.;
                REQUIRE(rf_complex_matrix_set(&dense, i, j, x));
                REQUIRE(rf_complex_band_set(&band, i, j, x));
            }
    for (int i = 0; i < N; ++i)
        rhs[i] = sin(i + .3) + I * cos(i - .2);
    REQUIRE(rf_complex_solve(&dense, rhs, a, &eta) == RF_LINEAR_OK);
    REQUIRE(rf_complex_band_solve(&band, rhs, b, &eta) == RF_LINEAR_OK);
    REQUIRE(eta < 1e-13);
    for (int i = 0; i < N; ++i)
        near(b[i], a[i], 1e-10, 1e-10);
    rf_complex_matrix_free(&dense);
    rf_complex_band_free(&band);
}
static void limiting_test(void)
{
    Project p;
    Plasma c;
    project_default(&p);
    p.selected_m = 0;
    plasma_default(&c);
    p.frequency_mhz = 100.;
    c.density = 2e14;
    c.magnetic_field = .002;
    c.solve_mode = RF_PLASMA_LIMITING_ABSORPTION;
    RfPlasmaSolution s = { 0 };
    double root, slope;
    REQUIRE(rf_plasma_resonance(&p, &c, &root, &slope, error, sizeof(error)) == 1);
    REQUIRE(rf_plasma_solve(&p, &c, &s, error, sizeof(error)));
    REQUIRE(s.limiting_converged && s.limiting_steps >= 3 && s.collision_scale < 1.);
    REQUIRE(s.radial_error < 3e-3 && s.quadrature_error < 3e-4 && s.continuation_error < 3e-3);
    REQUIRE(s.absorbed_power > 0. && s.power_balance_error < 1e-6);
    /* 正碰撞场形成的 S Er 趋向局部极点分子。以此独立核对 PDF (4.60)
     * 的集中层吸收系数；有限 lambda 下只要求随延拓接近，不能声称恒等。 */
    double complex e[3], b[3];
    RfPlasmaMaterial m;
    REQUIRE(rf_maxwell_evaluate(s.fem, root, 0, e, b));
    REQUIRE(rf_plasma_material(&p, &s.plasma, root, &m, error, sizeof(error)));
    double omega = 2 * PI * p.frequency_mhz * 1e6;
    double bnorm = PI * PI * (omega / C) / fabs(slope) * sq(m.S * e[0]);
    double predicted = 8.8541878128e-12 * C * root / (2 * PI) * bnorm;
    printf("limiting absorption: lambda %.4g, P %.9g, singular-coefficient estimate %.9g\n",
        s.collision_scale,
        s.absorbed_power,
        predicted);
    REQUIRE(fabs(predicted - s.absorbed_power) / s.absorbed_power < .015);
    Plasma zero = c;
    zero.solve_mode = RF_PLASMA_DIRECT_RESONANCE;
    REQUIRE(!rf_plasma_solve(&p, &zero, &s, error, sizeof(error)));
    REQUIRE(!s.complete && strstr(error, "S0"));
    rf_plasma_solution_free(&s);
}

/* Regression for symmetry-cancelled modes: a nonzero roundoff RHS must be
 * solved as assembled, rather than skipped and later checked against E=0. */
static void template_mode_regression(void)
{
    for (int m = -1; m <= 1; m++) {
        for (int type = RF_ANT_STRAP; type <= RF_ANT_HALF_ARRAY; type++) {
            Project p;
            Plasma c;
            project_default(&p);
            plasma_default(&c);
            p.type = type;
            p.selected_m = m;
            for (int medium = 0; medium < 2; medium++) {
                RfMaxwellSolution solution = { 0 };
                REQUIRE(rf_maxwell_solve(&p, medium ? &c : NULL, &solution, error, sizeof(error)));
                REQUIRE(solution.complete && solution.backward_error < 1e-8);
                REQUIRE(isfinite(solution.peak_electric));
                if ((m == 0 && (type == RF_ANT_ARRAY || type == RF_ANT_COMB ||
                               type == RF_ANT_HELIX || type == RF_ANT_SADDLE ||
                               type == RF_ANT_NAGOYA || type == RF_ANT_HALF_ARRAY)) ||
                    (m != 0 && type == RF_ANT_LOOP))
                    REQUIRE(solution.peak_electric < 1e-8);
                else if (m >= 0)
                    REQUIRE(solution.peak_electric > 1e-4);
                rf_maxwell_free(&solution);
            }
        }
    }
    puts(
        "template regression: all 12 antennas, m=-1/0/1, vacuum/plasma, including cancelled modes");
}

static void potential_projection(void)
{
    /* Manufacture E=-grad Phi directly in the compatible vector space, with
     * arbitrary complex phase, a grounded wall and exact axis behavior. The
     * Poisson extraction must recover Phi, including m=kz=0 without division. */
    for (int m = -2; m <= 2; ++m)
        for (int ik = -1; ik <= 1; ++ik) {
            RfMaxwellSolution s = { 0 };
            project_default(&s.project);
            s.project.radius = 1.;
            s.project.selected_m = m;
            s.project.kz = 3. * ik;
            s.nodes = 17;
            s.quadrature_order = 16;
            s.complete = 1;
            s.r = calloc(s.nodes, sizeof(*s.r));
            s.coefficients = calloc(3 * (s.nodes - 1) + 2, sizeof(*s.coefficients));
            REQUIRE(s.r && s.coefficients);
            double complex phi[17], amplitude = .7 - 1.3 * I;
            for (size_t j = 0; j < s.nodes; ++j) {
                s.r[j] = pow(j / 16., 1.3);
                phi[j] = amplitude * pow(s.r[j], abs(m)) * (1. - s.r[j]);
                s.coefficients[3 * j + 1] = -I * s.project.kz * phi[j];
                if (j)
                    s.coefficients[3 * j] = -I * m * phi[j] / s.r[j];
            }
            for (size_t j = 0; j + 1 < s.nodes; ++j) {
                double h = s.r[j + 1] - s.r[j];
                s.coefficients[3 * j + 2] = j ? -(phi[j + 1] - phi[j]) / h
                    : m ? 0. : -2. * (phi[1] - phi[0]) / h;
            }
            REQUIRE(rf_maxwell_prepare_potential(&s, error, sizeof(error)));
            REQUIRE(s.potential_backward_error < 1e-12);
            for (size_t j = 0; j < s.nodes; ++j) {
                double complex actual, EL[3], E[3], B[3];
                REQUIRE(rf_maxwell_evaluate_potential(&s, s.r[j], 0, &actual, EL));
                near(actual, phi[j], 1e-10, 1e-11);
                REQUIRE(rf_maxwell_evaluate(&s, s.r[j], 0, E, B));
                for (int c = 0; c < 3; ++c)
                    near(EL[c], E[c], 1e-10, 1e-10);
            }
            REQUIRE(s.potential[s.nodes - 1] == 0.);
            rf_maxwell_free(&s);
            REQUIRE(!s.potential);
        }
    /* Pure axisymmetric azimuthal field has zero divergence. Its nonzero
     * electric field must not be relabeled as a scalar potential. */
    RfMaxwellSolution transverse = { 0 };
    project_default(&transverse.project);
    transverse.project.radius = 1.;
    transverse.project.selected_m = 0;
    transverse.project.kz = 0.;
    transverse.nodes = 3;
    transverse.quadrature_order = 16;
    transverse.complete = 1;
    transverse.r = calloc(3, sizeof(double));
    transverse.coefficients = calloc(8, sizeof(double complex));
    REQUIRE(transverse.r && transverse.coefficients);
    transverse.r[1] = .5;
    transverse.r[2] = 1.;
    transverse.coefficients[3] = 1. + I;
    REQUIRE(rf_maxwell_prepare_potential(&transverse, error, sizeof(error)));
    for (size_t j = 0; j < 3; ++j)
        REQUIRE(transverse.potential[j] == 0.);
    rf_maxwell_free(&transverse);
    puts("potential: exact gradient recovery, complex phase, grounded wall, regular axis, kz=0 and transverse rejection");
}
int main(void)
{
    potential_projection();
    Project p;
    project_default(&p);
    p.band_width = .01;
    for (int type = 0; type < 2; ++type)
        for (int sign = -1; sign <= 1; sign += 2) {
            p.type = type ? RF_ANT_HELIX : RF_ANT_LOOP;
            p.turns = .5;
            p.selected_m = type ? sign : 0;
            p.frequency_mhz = type ? 200. : 100.;
            p.kz = type ? 1.3 : 10.;
            p.radial_cells = 128;
            double coarse = vacuum_error(&p);
            p.radial_cells = 512;
            double fine = vacuum_error(&p);
            printf("vacuum %s m=%d / Bessel: coarse %.6g fine %.6g\n",
                type ? "helix" : "loop",
                p.selected_m,
                coarse,
                fine);
            REQUIRE(fine < .002);
            REQUIRE(fine < .7 * coarse);
            if (!type)
                break;
        }
    template_mode_regression();
    source_and_pole_tests();
    conservation_tests();
    plasma_test();
    limiting_test();
    puts("MAXWELL PASS: analytic vacuum, modal axis/PEC, true sources/poles, independent plasma "
         "ODE, interface traces, power and zero-density limit");
    return 0;
}
