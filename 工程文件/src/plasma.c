/* 冷电子 + 单离子介电张量、响应电流与碰撞吸收。
 * 运动方程、张量符号和峰值相量对应 PDF §3.1；全局求场共用 maxwell.c。
 * B0 沿 z，Omega_s=q_s B0/M_s 保留电荷符号。无端口电流反馈和热输运。 */
#include "rf_platform.h"
#include "plasma.h"
#include "maxwell.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef RF_PI
#define RF_PI 3.141592653589793238462643383279502884
#endif

/* 诊断缓冲可省略，容量为零时也不解引用；返回值统一为失败 0。 */
static int fail(char *error, size_t n, const char *message)
{
    if (error != NULL && n != 0U)
        (void)snprintf(error, n, "%s", message);
    return 0;
}

/* 以 x=(r/a)² 给出密度比 n(r)/n0：均匀、抛物线、或满足边缘比的高斯。
 * 该函数只用于等离子体内部；边界外材料函数明确返回真空 ε=I。 */
static double profile_factor(const Plasma *p, double radius)
{
    double x = radius / p->radius;
    if (x < 0.0)
        x = 0.0;
    if (x > 1.0)
        x = 1.0;
    x *= x;
    if (p->profile == RF_PLASMA_UNIFORM)
        return 1.0;
    if (p->profile == RF_PLASMA_GAUSSIAN)
        /* 材料入口已验证 Gaussian 的 edge_ratio>0；不能再以隐藏下限
         * 截断合法的小边缘比，否则实际密度剖面会偏离用户输入。 */
        return exp(log(p->edge_ratio) * x);
    return 1.0 - (1.0 - p->edge_ratio) * x;
}

/* 从物种动量方程 (ν_s−iω)v_s=(q_s/M_s)(E+v_s×B0) 得到材料张量。
 * 准中性：ni=ne/Z；Ω_s=q_s B0/M_s 必须保留电荷符号，电子 Ωe<0（B0>0）。
 * ω_tilde=ω+iν，ν_s(r)=ν_s0[1+(edge_ratio_s−1)(r/a)²]。
 * S=1−Σ wp² ω_tilde/[ω(ω_tilde²−Ω²)]，
 * D=Σ wp² Ω/[ω(ω_tilde²−Ω²)]，P=1−Σ wp²/(ω ω_tilde)。
 * 极点不返回有限假值；radius=a 算内侧材料，外侧为真空。
 */
int rf_plasma_material(const Project *project,
    const Plasma *plasma,
    double radius,
    RfPlasmaMaterial *m,
    char *error,
    size_t error_size)
{
    const double eps = 1e-12 * fmax(plasma != NULL ? plasma->radius : 1.0, 1e-6);
    double omega, ne, ni, nue, nui;
    double complex S = 1.0, D = 0.0, P = 1.0;
    int species;
    if (project == NULL || plasma == NULL || m == NULL || !isfinite(radius) || radius < 0.0)
        return fail(error, error_size, "invalid plasma material request");
    if (!project_validate(project, error, error_size) ||
        !plasma_validate(plasma, error, error_size))
        return 0;
    if (radius > project->radius + eps)
        return fail(error, error_size, "material radius outside vessel");
    if (radius <= plasma->radius) {
        double shape = profile_factor(plasma, radius);
        double x = fmin(1., radius / plasma->radius); /* 容差范围内按边界值，避免负碰撞率。 */
        ne = plasma->density * shape;
        ni = ne / (double)plasma->ion_charge;
        /* 极限请求在局部材料接口中返回正则系数的解析极限。全波场
         * 则在下方显式构造有限正小量材料；模式 2 不使用输入碰撞率。 */
        nue = plasma->solve_mode != RF_PLASMA_FINITE_COLLISION
                  ? 0.0
                  : plasma->electron_collision *
                        (1.0 + (plasma->electron_collision_edge_ratio - 1.0) * x * x);
        nui =
            plasma->solve_mode != RF_PLASMA_FINITE_COLLISION
                ? 0.0
                : plasma->ion_collision * (1.0 + (plasma->ion_collision_edge_ratio - 1.0) * x * x);
    } else {
        ne = ni = nue = nui = 0.0;
    }
    omega = 2.0 * RF_PI * project->frequency_mhz * 1e6;
    for (species = 0; species < 2; ++species) {
        double density = species == 0 ? ne : ni;
        double charge = species == 0 ? -RF_E_CHARGE : plasma->ion_charge * RF_E_CHARGE;
        double mass = species == 0 ? RF_ME : plasma->ion_mass_u * RF_AMU;
        double nu = species == 0 ? nue : nui;
        double gyro = charge * plasma->magnetic_field / mass;
        double complex frequency = omega + I * nu;
        double complex denominator = (frequency - gyro) * (frequency + gyro);
        double wp2;
        if (density == 0.0)
            continue;
        if ((nu == 0. && fabs(omega - fabs(gyro)) <= 8. * DBL_EPSILON * fmax(omega, fabs(gyro))) ||
            cabs(denominator) == 0.0 || !isfinite(creal(denominator)) ||
            !isfinite(cimag(denominator)))
            return fail(error, error_size, "介电张量在回旋极点无有效有限值");
        wp2 = density * charge * charge / (RF_EPS0 * mass);
        S -= wp2 * frequency / (omega * denominator);
        D += wp2 * gyro / (omega * denominator);
        P -= wp2 / (omega * frequency);
    }
    if (!isfinite(creal(S)) || !isfinite(cimag(S)) || !isfinite(creal(D)) || !isfinite(cimag(D)) ||
        !isfinite(creal(P)) || !isfinite(cimag(P)))
        return fail(error, error_size, "介电张量不是有限值");
    m->ne = ne;
    m->ni = ni;
    m->nu_e = nue;
    m->nu_i = nui;
    m->S = S;
    m->D = D;
    m->P = P;
    return 1;
}

/* PDF 第四章的材料分类：只定位连续材料内部的简单 S0 零点。
 * 不把回旋极点、界面跳变或无损全局本征值冒充这一类共振。 */
int rf_plasma_resonance(
    const Project *p, const Plasma *c, double *radius, double *slope, char *error, size_t n)
{
    if (!p || !c || !radius || !slope) {
        fail(error, n, "invalid resonance request");
        return -1;
    }
    *radius = *slope = NAN;
    if (c->density == 0. || c->profile == RF_PLASMA_UNIFORM)
        return 0;
    Plasma zero = *c;
    zero.solve_mode = RF_PLASMA_LOSSLESS_REFERENCE;
    RfPlasmaMaterial centre, edge;
    if (!rf_plasma_material(p, &zero, 0, &centre, error, n) ||
        !rf_plasma_material(p, &zero, c->radius, &edge, error, n))
        return -1;
    double f0 = creal(centre.S), f1 = creal(edge.S);
    if (f0 * f1 >= 0.)
        return 0;
    double left = 0., right = c->radius;
    for (int j = 0; j < 80; ++j) {
        double mid = (left + right) / 2.;
        RfPlasmaMaterial m;
        if (!rf_plasma_material(p, &zero, mid, &m, error, n))
            return -1;
        if (f0 * creal(m.S) <= 0.)
            right = mid;
        else
            left = mid;
    }
    *radius = (left + right) / 2.;
    if (c->profile == RF_PLASMA_PARABOLIC)
        *slope = (1. - f0) * 2 * (1. - c->edge_ratio) * (*radius) / (c->radius * c->radius);
    else
        *slope = -(1. - f0) * 2 * log(c->edge_ratio) * (*radius) / (c->radius * c->radius) *
                 exp(log(c->edge_ratio) * (*radius) * (*radius) / (c->radius * c->radius));
    if (!isfinite(*slope) || *slope == 0.) {
        fail(error, n, "degenerate S0 root is not a simple resonance");
        return -1;
    }
    return 1;
}

/* 对象须先初始化为 {0}；释放时不能保留上次的功率元数据。 */
static void reset(RfPlasmaSolution *s)
{
    if (s == NULL)
        return;
    RfPlasmaSolution old = *s;
    *s = (RfPlasmaSolution){
        .source_power = NAN, .absorbed_power = NAN, .power_balance_error = NAN
    };
    free(old.r);
    free(old.electric);
    free(old.current);
    free(old.absorption);
    free(old.total);
    free(old.magnetic);
    free(old.cumulative);
    if (old.fem) {
        rf_maxwell_free(old.fem);
        free(old.fem);
    }
}


/* 释放一个单谱场结果，包含内部非均匀 FEM 与外部显示样本；允许重复调用。 */
void rf_plasma_solution_free(RfPlasmaSolution *s)
{
    reset(s);
}

/* 直接由碰撞耗散计算 q，避免 Re(J·E*) 在弱碰撞时相消为负。
 * q=Σ n q_s² ν_s/(4M_s) [ |Er+iEφ|²/((ω−Ω)²+ν²)
 *                         +|Er−iEφ|²/((ω+Ω)²+ν²)+2|Ez|²/(ω²+ν²) ]。
 * 每项非负，ν=0 的物种严格贡献 0；没有 fmax(0,...) 掩盖负功率。
 * 输入 E_hat[V] 时返回谱量 W/m，输入空间 E[V/m] 时返回 W/m³。
 */
double rf_plasma_absorption(
    const Project *p, const Plasma *c, const RfPlasmaMaterial *m, const double complex e[3])
{
    if (!p || !c || !m || !e)
        return NAN;
    double omega = 2. * RF_PI * p->frequency_mhz * 1e6, q = 0.;
    for (int species = 0; species < 2; ++species) {
        double density = species == 0 ? m->ne : m->ni, nu = species == 0 ? m->nu_e : m->nu_i;
        double charge = species == 0 ? -RF_E_CHARGE : c->ion_charge * RF_E_CHARGE;
        double mass = species == 0 ? RF_ME : c->ion_mass_u * RF_AMU;
        double gyro = charge * c->magnetic_field / mass;
        if (density == 0. || nu == 0.)
            continue;
        q += density * charge * charge * nu / (4. * mass) *
             (pow(cabs(e[0] + I * e[1]), 2) / (pow(omega - gyro, 2) + nu * nu) +
                 pow(cabs(e[0] - I * e[1]), 2) / (pow(omega + gyro, 2) + nu * nu) +
                 2. * pow(cabs(e[2]), 2) / (omega * omega + nu * nu));
    }
    return q;
}

/* 比较两次独立求解：材料积分功率 + 远离共振层的复 E、cB。
 * 功率相等不能单独证明场正确，故同时检查保留相位的层外场。
 * 六个固定探针是一项数值诊断，不是对全域误差的严格上界。 */
static double field_power_difference(
    const RfMaxwellSolution *a, const RfMaxwellSolution *b, double root)
{
    double err = 0., norm = 0.;
    double probes[6] = { .13 * a->plasma.radius,
        .37 * a->plasma.radius,
        .73 * a->plasma.radius,
        .91 * a->plasma.radius,
        (a->plasma.radius + a->project.antenna_radius) / 2.,
        (a->project.antenna_radius + a->project.radius) / 2. };
    for (int j = 0; j < 6; ++j) {
        /* 固定层外探针不随 lambda 移动；避免恰好落在材料共振根上。 */
        if (fabs(probes[j] - root) < .02 * a->plasma.radius)
            continue;
        double complex ea[3], eb[3], ba[3], bb[3];
        if (!rf_maxwell_evaluate(a, probes[j], 0, ea, ba) ||
            !rf_maxwell_evaluate(b, probes[j], 0, eb, bb))
            return INFINITY;
        for (int k = 0; k < 3; ++k) {
            err += pow(cabs(ea[k] - eb[k]), 2) + pow(RF_C0 * cabs(ba[k] - bb[k]), 2);
            norm += pow(cabs(eb[k]), 2) + pow(RF_C0 * cabs(bb[k]), 2);
        }
    }
    double power_scale = fmax(a->absorbed_power, b->absorbed_power);
    /* 正则无损极限下 P_abs 趋零，其相邻级相对变化仍可为 O(1)。
     * 因而以复源功尺度控制绝对变化；S0 共振层仍按吸收功率作相对控制。 */
    if (!isfinite(root))
        power_scale = fmax(power_scale, .5 * a->project.current * a->project.current *
            fmax(cabs(a->load_impedance), cabs(b->load_impedance)));
    double power = fabs(a->absorbed_power - b->absorbed_power) / fmax(1e-100, power_scale);
    return fmax(power, sqrt(err / fmax(norm, 1e-300)));
}
/* 显式转移内部网格和场系数的所有权；移走后源对象恢复为空。 */
static void move_field(RfMaxwellSolution *target, RfMaxwellSolution *source)
{
    rf_maxwell_free(target);
    *target = *source;
    *source = (RfMaxwellSolution){ 0 };
}

/* 固定一个正碰撞级：先径向加密，再独立改变 Gauss 阶数。
 * 常规 m 比较 16/32 点，高 |m| 比较 32/64 点；只有两项都通过，
 * 才把本级场交给外层碰撞序列。 */
static int resolve_collision_level(const Project *p,
    const Plasma *physical,
    double root,
    double eta,
    RfPlasmaSolution *result,
    RfMaxwellSolution *resolved,
    char *error,
    size_t n)
{
    const double radial_tolerance = 3e-3, quadrature_tolerance = 3e-4;
    Project coarse_project = *p;
    coarse_project.radial_cells = p->radial_cells / 2;
    if (coarse_project.radial_cells < 64)
        coarse_project.radial_cells = 64;
    RfMaxwellSolution coarse = { 0 }, fine = { 0 }, quadrature = { 0 };
    int ok = 0;
    if (!rf_maxwell_solve(&coarse_project, physical, &coarse, error, n))
        goto done;
    for (;;) {
        Project fine_project = coarse_project;
        fine_project.radial_cells = 2 * coarse_project.radial_cells;
        if (fine_project.radial_cells < p->radial_cells)
            fine_project.radial_cells = p->radial_cells;
        if (fine_project.radial_cells > 2048)
            fine_project.radial_cells = 2048;
        if (!rf_maxwell_solve(&fine_project, physical, &fine, error, n))
            goto done;
        result->radial_error = field_power_difference(&coarse, &fine, root);
        rf_maxwell_free(&coarse);
        if (result->radial_error <= radial_tolerance)
            break;
        if (fine_project.radial_cells == 2048) {
            if (error && n)
                snprintf(error,
                    n,
                    "零碰撞极限径向加密未收敛：eta=%.3g，基础单元 %d，变化 %.3g",
                    eta,
                    fine_project.radial_cells,
                    result->radial_error);
            goto done;
        }
        move_field(&coarse, &fine);
        coarse_project = fine_project;
    }
    /* 高 |m| 的默认求解本来就是 32 点；此时必须继续到 64 点，
     * 不能比较两次相同的 32 点计算后宣称完成了独立求积检查。 */
    size_t check_order = fine.quadrature_order < 32 ? 32 : 64;
    if (!rf_maxwell_solve_order(&fine.project, physical, check_order, &quadrature, error, n))
        goto done;
    result->quadrature_error = field_power_difference(&fine, &quadrature, root);
    if (result->quadrature_error > quadrature_tolerance) {
        if (error && n)
            snprintf(error,
                n,
                "零碰撞极限求积未收敛：eta=%.3g，变化 %.3g；未继续减小正小量",
                eta,
                result->quadrature_error);
        goto done;
    }
    move_field(resolved, &quadrature);
    ok = 1;
done:
    rf_maxwell_free(&coarse);
    rf_maxwell_free(&fine);
    rf_maxwell_free(&quadrature);
    return ok;
}

/* PDF §4.12：各正碰撞级先完成径向/求积检查，外层只负责缩小碰撞和比较。
 * 这是普通场数值延拓，并非把 nu=0 的 Dirac 场离散成一个有限尖峰。
 * 每个 (m,kz) 的极限分别检查；完整总功率还需外层的模式/波数收敛。
 * 达不到容差时返回失败，绝不静默返回最后一级未收敛结果。 */
static int limiting_field(
    const Project *p, const Plasma *reference, RfPlasmaSolution *result, char *error, size_t n)
{
    double root, slope;
    int kind = rf_plasma_resonance(p, reference, &root, &slope, error, n);
    if (kind < 0)
        return 0;
    Plasma zero = *reference;
    zero.solve_mode = RF_PLASMA_LOSSLESS_REFERENCE;
    RfPlasmaMaterial at_root;
    if (kind && !rf_plasma_material(p, &zero, root, &at_root, error, n))
        return 0;
    if (kind && (cabs(at_root.D) <= 64 * DBL_EPSILON || cabs(at_root.P) <= 64 * DBL_EPSILON))
        return fail(
            error, n, "该 S0 零点为 D0 或 P0 同时退化的情形，超出 PDF 内部简单混合共振的延拓假设");

    RfMaxwellSolution previous = { 0 };
    int stable = 0;
    const double limit_tolerance = 3e-3;
    const double omega = 2. * RF_PI * p->frequency_mhz * 1e6;
    for (int level = 0; level < 18; ++level) {
        /* 两物种均使用空间均匀的数值正小量 nu=eta*omega，eta→0+。
         * 此族不取决于任何输入碰撞率或碰撞边缘比。 */
        double eta = ldexp(1e-2, -level);
        Plasma physical = *reference;
        physical.solve_mode = RF_PLASMA_FINITE_COLLISION;
        physical.electron_collision = physical.ion_collision = eta * omega;
        physical.electron_collision_edge_ratio = physical.ion_collision_edge_ratio = 1.;
        RfMaxwellSolution current = { 0 };
        if (!resolve_collision_level(p, &physical, root, eta, result, &current, error, n)) {
            rf_maxwell_free(&previous);
            return 0;
        }
        result->continuation_error =
            previous.complete ? field_power_difference(&previous, &current, root) : INFINITY;
        if (result->continuation_error < limit_tolerance)
            ++stable;
        else
            stable = 0;
        if (stable >= 2) {
            if (!kind) {
                /* 正小量族通过后，再在独立加密的无损网格上核对正则解析端点；
                 * S0=0 共振层没有此普通函数端点，不能走这个分支。 */
                RfMaxwellSolution endpoint = { 0 };
                if (!resolve_collision_level(&current.project, &zero, NAN, 0.,
                        result, &endpoint, error, n)) {
                    rf_maxwell_free(&current);
                    rf_maxwell_free(&previous);
                    return 0;
                }
                double endpoint_error = field_power_difference(&current, &endpoint, NAN);
                result->continuation_error = fmax(result->continuation_error, endpoint_error);
                if (endpoint_error > limit_tolerance) {
                    rf_maxwell_free(&endpoint);
                    move_field(&previous, &current);
                    continue;
                }
                move_field(&current, &endpoint);
            }
            move_field(result->fem, &current);
            rf_maxwell_free(&previous);
            result->collision_scale = kind ? eta : 0.;
            result->limiting_steps = level + 1;
            result->limiting_converged = 1;
            return 1;
        }
        move_field(&previous, &current);
    }
    rf_maxwell_free(&previous);
    return fail(error, n, "18 级正小量趋零序列尚未满足零碰撞极限收敛条件，未返回未收敛结果");
}

/* 求解包含完整 epsilon_r 的 Maxwell 弱式；不先解真空、也不局部乘 epsilon^-1。
 * 在同一个 FEM 场上恢复响应电流；体吸收与源供功已在核心独立积分。
 * 输出数组只服务显示/导出，累计功率由内部单元求积获得。 */
int rf_plasma_solve(const Project *p, const Plasma *c, RfPlasmaSolution *s, char *error, size_t n)
{
    if (!s)
        return fail(error, n, "plasma destination is null");
    Project input_project = { 0 };
    Plasma input_plasma = { 0 };
    if (p)
        input_project = *p;
    if (c)
        input_plasma = *c;
    int inputs_present = p && c;
    reset(s);
    RfPlasmaSolution *destination = s, built = { 0 };
    s = &built;
    if (!inputs_present)
        return fail(error, n, "plasma project or material is null");
    p = &input_project;
    c = &input_plasma;
    if (!project_validate(p, error, n) || !plasma_validate(c, error, n))
        return 0;
    s->fem = calloc(1, sizeof(*s->fem));
    if (!s->fem)
        return fail(error, n, "out of memory for plasma Maxwell result");
    s->radial_error = s->quadrature_error = s->continuation_error = NAN;
    s->collision_scale = c->solve_mode == RF_PLASMA_LOSSLESS_REFERENCE ? 0. : 1.;
    int solved = c->solve_mode == RF_PLASMA_ZERO_COLLISION_LIMIT
                     ? limiting_field(p, c, s, error, n)
                     : rf_maxwell_solve(p, c, s->fem, error, n);
    if (!solved) {
        reset(s);
        return 0;
    }
    const Plasma *actual = &s->fem->plasma;
    s->nr = (size_t)p->radial_cells + 1;
    s->project = *p;
    s->plasma = *actual;
    s->r = malloc(s->nr * sizeof(*s->r));
    s->electric = calloc(3 * s->nr, sizeof(*s->electric));
    s->magnetic = calloc(3 * s->nr, sizeof(*s->magnetic));
    s->current = calloc(3 * s->nr, sizeof(*s->current));
    s->absorption = calloc(s->nr, sizeof(*s->absorption));
    s->total = calloc(s->nr, sizeof(*s->total));
    s->cumulative = calloc(s->nr, sizeof(*s->cumulative));
    if (!s->r || !s->electric || !s->magnetic || !s->current || !s->absorption || !s->total ||
        !s->cumulative) {
        reset(s);
        return fail(error, n, "out of memory for plasma field samples");
    }
    const double omega = 2. * RF_PI * p->frequency_mhz * 1e6;
    for (size_t i = 0; i < s->nr; ++i) {
        double complex e[3], b[3], j[3];
        RfPlasmaMaterial m;
        s->r[i] = p->radius * i / (s->nr - 1);
        if (!rf_maxwell_evaluate(s->fem, s->r[i], 0, e, b) ||
            !rf_plasma_material(p, actual, s->r[i], &m, error, n)) {
            reset(s);
            return 0;
        }
        j[0] = -I * omega * RF_EPS0 * ((m.S - 1.) * e[0] - I * m.D * e[1]);
        j[1] = -I * omega * RF_EPS0 * (I * m.D * e[0] + (m.S - 1.) * e[1]);
        j[2] = -I * omega * RF_EPS0 * (m.P - 1.) * e[2];
        for (int k = 0; k < 3; ++k) {
            if (!isfinite(creal(j[k])) || !isfinite(cimag(j[k]))) {
                reset(s);
                return fail(error, n, "non-finite plasma response current");
            }
            s->electric[k * s->nr + i] = e[k];
            s->magnetic[k * s->nr + i] = b[k];
            s->current[k * s->nr + i] = j[k];
            s->total[i] += pow(cabs(e[k]), 2);
        }
        s->total[i] = sqrt(s->total[i]);
        s->absorption[i] = rf_plasma_absorption(p, actual, &m, e);
        s->cumulative[i] = rf_maxwell_cumulative(s->fem, s->r[i]);
    }
    s->source_power = s->fem->source_power;
    s->absorbed_power = s->fem->absorbed_power;
    s->peak_electric = s->fem->peak_electric;
    s->power_balance_error = s->fem->power_balance_error;
    s->backward_error = s->fem->backward_error;
    s->complete = 1;
    snprintf(s->message,
        sizeof(s->message),
        "冷等离子体 Maxwell 矢量有限元 · m=%d · kz=%.6g · %zu 单元 · dP/dkz=%.6g W m · "
        "功率误差 %.2g",
        p->selected_m,
        p->kz,
        s->fem->nodes - 1,
        s->absorbed_power,
        s->power_balance_error);
    if (c->solve_mode == RF_PLASMA_ZERO_COLLISION_LIMIT)
        snprintf(s->message,
            sizeof(s->message),
            "零碰撞极限已收敛 · %d 级 · eta=%.4g · dP/dkz=%.6g W m · 网格/求积/延拓 "
            "%.2g/%.2g/%.2g",
            s->limiting_steps,
            s->collision_scale,
            s->absorbed_power,
            s->radial_error,
            s->quadrature_error,
            s->continuation_error);
    *destination = *s; /* 转移拥有数组及内部 FEM；局部 built 不再释放。 */
    return 1;
}
/* 在任意容器内半径用原 FEM 基恢复指定电场谱分量[V]；无效请求返回复 NaN。 */
double complex rf_plasma_field(const RfPlasmaSolution *s, double radius, int component)
{
    double complex e[3], b[3];
    if (!s || !s->complete || component < 0 || component > 2 ||
        !rf_maxwell_evaluate(s->fem, radius, 0, e, b))
        return NAN + NAN * I;
    return e[component];
}

/* 导出完整复数谱场与非负体吸收谱，保留单位及 double 往返精度。 */
int rf_plasma_export_csv(
    const RfPlasmaSolution *s, const char *path, char *error, size_t error_size)
{
    FILE *f;
    size_t i;
    if (!s || !s->complete || !path || !*path)
        return fail(error, error_size, "plasma result or path is invalid");
    f = rf_fopen_utf8(path, "w");
    if (!f) {
        if (error && error_size)
            snprintf(error, error_size, "cannot write '%s': %s", path, strerror(errno));
        return 0;
    }
    if (fprintf(f,
            "r_m,Er_re,Er_im,Ephi_re,Ephi_im,Ez_re,Ez_im,Jr_re,Jr_im,Jphi_re,Jphi_im,Jz_re,Jz_"
            "im,absorption_W_per_m,total_V,Br_re,Br_im,Bphi_re,Bphi_im,Bz_re,Bz_im,cumulative_"
            "W_m,source_power_W_m,absorbed_power_W_m,backward_error,power_balance_error,"
            "collision_scale,R_ohm_m,X_ohm_m\n") < 0)
        goto bad;
    for (i = 0; i < s->nr; ++i) {
        double row[29] = { s->r[i],
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
            creal(s->magnetic[i]),
            cimag(s->magnetic[i]),
            creal(s->magnetic[s->nr + i]),
            cimag(s->magnetic[s->nr + i]),
            creal(s->magnetic[2 * s->nr + i]),
            cimag(s->magnetic[2 * s->nr + i]),
            s->cumulative[i],
            s->source_power,
            s->absorbed_power,
            s->backward_error,
            s->power_balance_error,
            s->collision_scale,
            creal(s->fem->load_impedance),
            cimag(s->fem->load_impedance) };
        for (size_t col = 0; col < 29; ++col)
            if (fprintf(f, col ? ",%.17g" : "%.17g", row[col]) < 0)
                goto bad;
        if (fputc('\n', f) == EOF)
            goto bad;
    }
    if (fclose(f) != 0)
        return fail(error, error_size, "cannot finalize plasma CSV");
    return 1;
bad:
    (void)fclose(f);
    (void)rf_remove_utf8(path);
    return fail(error, error_size, "cannot write plasma CSV");
}
