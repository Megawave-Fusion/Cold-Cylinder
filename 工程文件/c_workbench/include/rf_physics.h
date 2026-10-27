#ifndef RF_PHYSICS_H
#define RF_PHYSICS_H

/* 全内核共用的 SI 常量。c0²=1/(mu0 epsilon0) 必须在数值上成立，
 * 否则传播方程与材料做功会出现约 5e-10 的固定功率偏差。
 * 保留项目原有 epsilon0 数值，以此和精确光速确定一致的 mu0。 */
#define RF_PI 3.141592653589793238462643383279502884
#define RF_C0 299792458.0
#define RF_EPS0 8.8541878128e-12
#define RF_MU0 (1.0 / (RF_EPS0 * RF_C0 * RF_C0))
#define RF_E_CHARGE 1.602176634e-19
#define RF_ME 9.1093837015e-31
#define RF_AMU 1.66053906660e-27

#endif
