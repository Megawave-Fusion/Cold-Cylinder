#include "collisions.h"
#include "rf_quadrature.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int collision_error(char *error, size_t size, const char *message)
{
    if (error && size)
        snprintf(error, size, "%s", message);
    return 0;
}

/* Phelps/Petrovic电子弹性动量转移拟合，能量数值用eV，结果用m^2。
 * 参见Phelps & Petrovic, PSST 8 (1999) R21；公式亦收录于
 * ANU Transport Properties of Charged Particles, Appendix B, eq.(B.1)。
 * 这里只估算弹性动量摩擦，未把激发/电离反应率混作碰撞阻尼。 */
double rf_argon_electron_cross_section(double e)
{
    if (!(e >= 0.) || !isfinite(e))
        return NAN;
    double low = 6. / pow(1. + e / .1 + pow(e / .6, 2.), 3.3);
    double high = 1.1 * pow(e, 1.4) /
        ((1. + pow(e / 15., 1.2)) * sqrt(1. + pow(e / 5.5, 2.5) + pow(e / 60., 4.1)));
    return 1e-20 * (fabs(low - high) + .05 / pow(1. + e / 10., 2.) +
        .01 * pow(e, 3.) / (1. + pow(e / 12., 6.)));
}

/* Phelps, JAP 76 (1994) 747；Vrhovac et al., EPJD 21 (2002) 335,
 * eq.(3.1): Q1=1.15e-18 E_lab^-0.1 (1+0.015/E_lab)^0.6。
 * Ar+与Ar同质量，因此E_lab=2 E_cm；这里显式转换，避免差一个2。
 * Q1是总动量转移截面：Q_iso+2 Q_backward，已经含电荷交换。 */
double rf_argon_ion_cross_section(double ecm)
{
    if (!(ecm > 0.) || !isfinite(ecm))
        return NAN;
    double elab = 2. * ecm;
    return 1.15e-18 * pow(elab, -.1) * pow(1. + .015 / elab, .6);
}

/* Maxwellian相对速度，Theta=mu(kT_a/m_a+kT_n/m_n)。线性化小漂移摩擦：
 * K_a=(mu/m_a)*4/(3 sqrt(pi))*sqrt(2 Theta/mu)
 *     * integral_0^infty x^2 exp(-x) sigma_mt(Theta*x) dx。
 * 这不是简单碰撞计数<sigma*v>；常数截面的弛豫率含4/3的漂移权重。
 * 温度参数均匀，系数在每个线程缓存；径向求积仍逐点计算nn与nu_ei。 */
typedef struct {
    double x[32], w[32], key[7], electron, ion;
    int ready, valid, model;
} CollisionCache;
static _Thread_local CollisionCache rate_cache;

static double integral_rule(CollisionCache *cache, double lo, double hi, double theta_ev, int ion)
{
    double mid = .5 * (lo + hi), half = .5 * (hi - lo), result = 0.;
    for (int j = 0; j < 32; ++j) {
        double x = mid + half * cache->x[j];
        double sigma = ion ? rf_argon_ion_cross_section(theta_ev * x)
                           : rf_argon_electron_cross_section(theta_ev * x);
        result += cache->w[j] * x * x * exp(-x) * (sigma / 1e-18);
    }
    return result * half;
}

/* 分段Gauss并递归比较二分结果；零端点的离子幂次奇异被x^2权重消去。
 * 以1e-18 m^2缩放控制误差，防止把SI量级误当作绝对零。 */
static double integrate(CollisionCache *cache, double lo, double hi, double theta_ev,
    int ion, double coarse, int depth, int *ok)
{
    double mid = .5 * (lo + hi);
    double left = integral_rule(cache, lo, mid, theta_ev, ion);
    double right = integral_rule(cache, mid, hi, theta_ev, ion);
    if (fabs(left + right - coarse) <= 1e-10 + 1e-8 * fabs(left + right))
        return left + right;
    if (!depth) {
        *ok = 0;
        return 0.;
    }
    return integrate(cache, lo, mid, theta_ev, ion, left, depth - 1, ok) +
        integrate(cache, mid, hi, theta_ev, ion, right, depth - 1, ok);
}

static double coefficient(CollisionCache *cache, double mass, double neutral_mass,
    double temperature_j, double gas_j, int ion, int *ok)
{
    double mu = mass * neutral_mass / (mass + neutral_mass);
    double theta = mu * (temperature_j / mass + gas_j / neutral_mass);
    double prefactor = mu / mass * 4. / (3. * sqrt(RF_PI)) * sqrt(2. * theta / mu);
    /* exp(-64)后的截断远小于积分容差；拟合截面在此无指数增长。 */
    const double bounds[] = {0., .0625, .25, .5, 1., 2., 4., 8., 16., 32., 64.};
    double integral = 0.;
    for (int j = 0; j < 10; ++j) {
        double coarse = integral_rule(cache, bounds[j], bounds[j + 1], theta / RF_E_CHARGE, ion);
        integral += integrate(cache, bounds[j], bounds[j + 1], theta / RF_E_CHARGE, ion, coarse, 18, ok);
    }
    return prefactor * integral * 1e-18;
}

static int thermal_coefficients(const Plasma *p, double *electron, double *ion)
{
    double mn = (p->neutral_model == RF_NEUTRAL_ARGON ? 39.948 : p->neutral_mass_u) * RF_AMU;
    double mi = p->ion_mass_u * RF_AMU;
    double key[] = {p->electron_temperature, p->ion_temperature, p->gas_temperature,
        p->ion_mass_u, mn, p->electron_neutral_cross_section, p->ion_neutral_cross_section};
    CollisionCache *cache = &rate_cache;
    if (cache->valid && cache->model == p->neutral_model && !memcmp(key, cache->key, sizeof(key))) {
        *electron = cache->electron;
        *ion = cache->ion;
        return 1;
    }
    double ke, ki;
    if (p->neutral_model == RF_NEUTRAL_CUSTOM) {
        /* 常数sigma的解析积分 integral x²e^-x dx=2，独立于数值求积。 */
        double se = RF_E_CHARGE * p->electron_temperature / RF_ME + RF_KB * p->gas_temperature / mn;
        double si = RF_E_CHARGE * p->ion_temperature / mi + RF_KB * p->gas_temperature / mn;
        ke = mn / (RF_ME + mn) * 8. / (3. * sqrt(RF_PI)) * sqrt(2. * se) * p->electron_neutral_cross_section;
        ki = mn / (mi + mn) * 8. / (3. * sqrt(RF_PI)) * sqrt(2. * si) * p->ion_neutral_cross_section;
    } else {
        if (!cache->ready) {
            if (!rf_gauss_legendre(32, cache->x, cache->w))
                return 0;
            cache->ready = 1;
        }
        int ok = 1;
        ke = coefficient(cache, RF_ME, mn, RF_E_CHARGE * p->electron_temperature,
            RF_KB * p->gas_temperature, 0, &ok);
        ki = coefficient(cache, mi, mn, RF_E_CHARGE * p->ion_temperature,
            RF_KB * p->gas_temperature, 1, &ok);
        if (!ok)
            return 0;
    }
    if (!isfinite(ke) || !isfinite(ki) || ke < 0. || ki < 0.)
        return 0;
    memcpy(cache->key, key, sizeof(key));
    cache->model = p->neutral_model;
    cache->electron = *electron = ke;
    cache->ion = *ion = ki;
    cache->valid = 1;
    return 1;
}

int rf_collision_rates(const Plasma *p, double radius, double ne,
    RfCollisionRates *out, char *error, size_t size)
{
    if (!p || !out || !isfinite(radius) || radius < 0. || !isfinite(ne) || ne < 0.)
        return collision_error(error, size, "碰撞率计算的半径或局部密度无效");
    *out = (RfCollisionRates){0};
    if (p->solve_mode != RF_PLASMA_FINITE_COLLISION || radius > p->radius)
        return 1;
    if (p->collision_source == RF_COLLISION_DIRECT) {
        double x = fmin(1., radius / p->radius);
        out->electron_total = out->electron_neutral = p->electron_collision *
            (1. + (p->electron_collision_edge_ratio - 1.) * x * x);
        out->ion_total = out->ion_neutral = p->ion_collision *
            (1. + (p->ion_collision_edge_ratio - 1.) * x * x);
        return 1;
    }
    if (!thermal_coefficients(p, &out->electron_rate_coefficient, &out->ion_rate_coefficient))
        return collision_error(error, size, "温压碰撞系数的Maxwell积分未收敛");
    out->neutral_density = p->neutral_pressure / (RF_KB * p->gas_temperature);
    out->electron_neutral = out->neutral_density * out->electron_rate_coefficient;
    out->ion_neutral = out->neutral_density * out->ion_rate_coefficient;
    if (p->coulomb_collisions && ne > 0.) {
        if (RF_ME * p->ion_temperature > .01 * p->ion_mass_u * RF_AMU * p->electron_temperature)
            return collision_error(error, size, "库仑近似要求离子热速度远小于电子热速度：Ti/Mi ≤ 0.01 Te/me");
        /* 弱耦合、非相对论Maxwellian电子，vTe=sqrt(2 kTe/me)。
         * lnLambda=ln(lambda_De/max(b90,lambda_dB))；不暗中钳成正数。
         * Te是eV、ne是m^-3，采用SI展开的Braginskii动量弛豫率。 */
        double t = RF_E_CHARGE * p->electron_temperature;
        double speed = sqrt(2. * t / RF_ME);
        /* 对数域避免极低密度时ne*e²下溢；不改变Debye截断约定。 */
        double log_debye = .5 * (log(RF_EPS0) + log(t) - log(ne) - 2. * log(RF_E_CHARGE));
        double b90 = p->ion_charge * RF_E_CHARGE * RF_E_CHARGE / (4. * RF_PI * RF_EPS0 * RF_ME * speed * speed);
        double quantum = RF_HBAR / (2. * RF_ME * speed);
        out->coulomb_log = log_debye - log(fmax(b90, quantum));
        if (!(out->coulomb_log >= 2.) || !isfinite(out->coulomb_log))
            return collision_error(error, size, "库仑模型要求弱耦合 lnΛ≥2；请核对密度、Te或关闭电子离子摩擦");
        double e2 = RF_E_CHARGE * RF_E_CHARGE / (4. * RF_PI * RF_EPS0);
        double prefactor = 4. * sqrt(2. * RF_PI) / 3. * e2 * e2 / (sqrt(RF_ME) * pow(t, 1.5));
        out->electron_ion = ne * (p->ion_charge * prefactor * out->coulomb_log);
        /* me ne nu_ei = mi ni nu_ie；ni=ne/Z，保持总摩擦动量守恒。 */
        out->ion_electron = RF_ME * p->ion_charge / (p->ion_mass_u * RF_AMU) * out->electron_ion;
    }
    out->electron_total = out->electron_neutral + out->electron_ion;
    out->ion_total = out->ion_neutral + out->ion_electron;
    if (!isfinite(out->electron_total) || !isfinite(out->ion_total) ||
        out->electron_total > 1e16 || out->ion_total > 1e16)
        return collision_error(error, size, "计算碰撞率超出有限有效范围 [0,1e16] s⁻¹");
    return 1;
}
