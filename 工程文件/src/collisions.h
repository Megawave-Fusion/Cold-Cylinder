#ifndef RF_COLLISIONS_H
#define RF_COLLISIONS_H

#include "rf_model.h"

/* 所有频率为动量弛豫率[s^-1]。en/in 对静止中性背景，ei/ie 成对交换动量。
 * 不能把 nu_e/nu_i 的对角总和再次当作两个独立背景阻尼使用。 */
typedef struct RfCollisionRates {
    double neutral_density; /* m^-3 */
    double electron_neutral, ion_neutral, electron_ion, ion_electron;
    double electron_total, ion_total, coulomb_log;
    double electron_rate_coefficient, ion_rate_coefficient; /* m^3/s */
} RfCollisionRates;

/* 调用方先做plasma_validate；ne是已求得的局部电子密度，避免复制密度剖面。 */
int rf_collision_rates(const Plasma *plasma, double radius, double ne,
    RfCollisionRates *rates, char *error, size_t error_size);

/* 文献Ar动量转移截面[m^2]；电子能量是LAB eV，离子能量是COM eV。
 * 对离子已包含弹性和对称电荷交换的总动量转移，不能再另加CX截面。 */
double rf_argon_electron_cross_section(double energy_ev);
double rf_argon_ion_cross_section(double energy_cm_ev);

#endif
