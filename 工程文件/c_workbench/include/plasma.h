#ifndef RF_PLASMA_H
#define RF_PLASMA_H

#include "rf_model.h"
#include "vacuum.h"

#include <complex.h>
#include <stddef.h>

typedef struct RfPlasmaMaterial {
    double ne, ni, nu_e, nu_i;
    double complex S, D, P;
} RfPlasmaMaterial;

typedef struct RfPlasmaSolution {
    Project project;
    Plasma plasma;
    size_t nr;
    double *r;
    double complex *electric; /* component-major, units V */
    double complex *current;  /* component-major, units A/m */
    double complex *magnetic; /* component-major, units T m；解析旋度 */
    double *absorption;       /* W/m */
    double *total;
    double *cumulative;    /* FEM 单元求积得到的 ∫r q_spec dr [W m] */
    double source_power;   /* dP_src/dkz [W m]，由独立源内积得到 */
    double absorbed_power; /* dP_abs/dkz=∫r q_spec dr [W m]；总功率另作谱积分 */
    double power_balance_error;
    double backward_error;
    double peak_electric;
    double collision_scale; /* 本次普通场实际碰撞/输入参考碰撞；常规模式为 1 */
    double radial_error, quadrature_error, continuation_error;
    int limiting_steps, limiting_converged;
    struct RfMaxwellSolution *fem;
    int complete;
    char message[256];
} RfPlasmaSolution;

/* 材料公式由物种动量方程推得；全局耦合求场见 src/maxwell.c。 */
int rf_plasma_material(const Project *project,
    const Plasma *plasma,
    double radius,
    RfPlasmaMaterial *material,
    char *error,
    size_t error_size);
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
int rf_plasma_solve(const Project *project,
    const Plasma *plasma,
    RfPlasmaSolution *solution,
    char *error,
    size_t error_size);
/* 栈上对象须 {0} 或先初始化；重复求解/释放必须遵守拥有数组的生命周期。 */
void rf_plasma_solution_init(RfPlasmaSolution *solution);
void rf_plasma_solution_free(RfPlasmaSolution *solution);
double complex rf_plasma_field(const RfPlasmaSolution *solution, double radius, int component);
int rf_plasma_export_csv(
    const RfPlasmaSolution *solution, const char *path, char *error, size_t error_size);

#endif
