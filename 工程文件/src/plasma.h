#ifndef RF_PLASMA_H
#define RF_PLASMA_H

#include "rf_model.h"
#include "vacuum.h"

#include <complex.h>
#include <stddef.h>

/* 指定实半径处的冷材料；密度[m^-3]、有效碰撞率[s^-1]，S/D/P 无量纲。 */
typedef struct RfPlasmaMaterial {
    double ne, ni, nu_e, nu_i;
    double complex S, D, P;
} RfPlasmaMaterial;

/* 固定 (f,m,kz) 的峰值复相量谱场；首次初始化为 {0}，拥有全部数组。 */
typedef struct RfPlasmaSolution {
    Project project; /* 驱动与几何输入快照 */
    Plasma plasma; /* 实际返回场的材料；极限模式可为末级正小量或正则端点 */
    size_t nr; /* 均匀显示样本数；内部 FEM 另外保留真实非均匀网格 */
    double *r; /* 显示半径[m] */
    double complex *electric; /* 分量主序 E_hat[V]，索引 component*nr+ir */
    double complex *current;  /* 相同排列的材料响应 J_hat[A/m]，不含规定天线源 */
    double complex *magnetic; /* component-major, units T m；解析旋度 */
    double *absorption;       /* W/m */
    double *total; /* 电场谱总幅值[V] */
    double *cumulative;    /* FEM 单元求积得到的 ∫r q_spec dr [W m] */
    double source_power;   /* dP_src/dkz [W m]，由独立源内积得到 */
    double absorbed_power; /* dP_abs/dkz=∫r q_spec dr [W m]；总功率另作谱积分 */
    double power_balance_error;
    double backward_error;
    double peak_electric;
    double collision_scale; /* 极限模式 eta=nu_reg/omega；正则零碰撞解为0；有限碰撞为1 */
    double radial_error, quadrature_error, continuation_error; /* 三项独立无量纲收敛诊断 */
    int limiting_steps, limiting_converged; /* 正小量级数与序列成功标志 */
    struct RfMaxwellSolution *fem;
    int complete;
    char message[256];
} RfPlasmaSolution;

/* 材料公式由物种动量方程推得；r=a 取内侧，r>a 返回真空。
 * 零碰撞模式在此返回正则材料解析极限；全波极限须经 rf_plasma_solve，
 * 不能把这里的零耗散系数代替奇异层的正小量吸收积分。 */
int rf_plasma_material(const Project *project,
    const Plasma *plasma,
    double radius,
    RfPlasmaMaterial *material,
    char *error,
    size_t error_size);
/* 非负粒子摩擦做功：E_hat[V] 输入得到 W/m；空间 E[V/m] 输入得到 W/m³。 */
double rf_plasma_absorption(const Project *project,
    const Plasma *plasma,
    const RfPlasmaMaterial *material,
    const double complex electric[3]);
/* -1: 材料计算错误；0: 无内部简单根；1: 找到 S0(rh)=0、S0'(rh)!=0。
 * 半径/斜率只描述局部材料；不替代全局源和边界的场求解。 */
int rf_plasma_resonance(const Project *project,
    const Plasma *plasma,
    double *radius,
    double *slope,
    char *error,
    size_t error_size);
/* 全局规定源 Maxwell 求解；模式 2 组织独立于输入碰撞参数的正小量族。
 * project/plasma 允许指向旧结果内部，失败清空结果，不保留半成品数组。 */
int rf_plasma_solve(const Project *project,
    const Plasma *plasma,
    RfPlasmaSolution *solution,
    char *error,
    size_t error_size);
/* 栈上对象首次须初始化为 {0}；重复释放安全，禁止浅复制独占数组。 */
void rf_plasma_solution_free(RfPlasmaSolution *solution);
/* 在原 FEM 上取任意半径指定电场谱分量[V]，无效请求返回复 NaN。 */
double complex rf_plasma_field(const RfPlasmaSolution *solution, double radius, int component);
/* 导出实际末级材料对应的复场、响应电流、功率与 eta 元数据。 */
int rf_plasma_export_csv(
    const RfPlasmaSolution *solution, const char *path, char *error, size_t error_size);

#endif
