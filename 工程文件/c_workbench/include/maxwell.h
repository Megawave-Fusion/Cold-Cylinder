#ifndef RF_MAXWELL_H
#define RF_MAXWELL_H

#include "rf_model.h"
#include <complex.h>
#include <stddef.h>

/* PDF 第 2、3 章的共同径向 Maxwell 边值问题。
 * E_hat [V]，B_hat [T m]，相量 exp(i m phi+i kz z-i omega t)。
 * 内部使用界面对齐的非均匀网格，不能把输出的均匀显示采样当成自由度。
 * 初始化为 {0}；同一对象不可以浅复制后分别释放。 */
typedef struct RfMaxwellSolution {
    Project project;
    Plasma plasma;
    int has_plasma;
    size_t nodes;
    double *r;
    double complex *coefficients; /* P_j,Z_j,D_j，PDF (2.38) */
    double complex *potential;    /* optional Coulomb scalar potential nodes [V m], Phi(R)=0 */
    double potential_backward_error;
    double *cumulative;           /* ∫_0^r rho q_spec d rho [W m] */
    double complex source[3];     /* 只用于展示的 K_eff [A] */
    double backward_error;
    double source_power;           /* dP_src/dkz [W m] */
    double complex load_impedance; /* dZ/dkz [ohm m], engineering exp(+i omega t), X>0 inductive */
    double absorbed_power;         /* dP_abs/dkz [W m]，无额外 2pi */
    double power_balance_error;
    double peak_electric; /* 内部各单元双侧端点及内部点上的谱场峰值估计 [V] */
    size_t quadrature_order;
    int complete;
} RfMaxwellSolution;

void rf_maxwell_free(RfMaxwellSolution *solution);
int rf_maxwell_solve(const Project *project,
    const Plasma *plasma,
    RfMaxwellSolution *solution,
    char *error,
    size_t error_size);
/* 独立改变单元求积阶数，用于求积收敛核对，不改变物理模型。 */
int rf_maxwell_solve_order(const Project *project,
    const Plasma *plasma,
    size_t order,
    RfMaxwellSolution *solution,
    char *error,
    size_t error_size);
/* side<0 取内侧迹，side>0 取外侧迹；side=0 约定取内侧。
 * 磁场直接由有限元基的解析旋度重建，绝不跨界面对采样场差分。 */
int rf_maxwell_evaluate(const RfMaxwellSolution *solution,
    double radius,
    int side,
    double complex electric[3],
    double complex magnetic[3]);
double rf_maxwell_cumulative(const RfMaxwellSolution *solution, double radius);
/* Longitudinal Helmholtz projection: Laplacian Phi=-div E, Phi(R)=0,
 * regular axis. E=-grad Phi+i*omega*A, div A=0. No division by kz.
 * This diagnostic leaves the full Maxwell field and absorption unchanged. */
int rf_maxwell_prepare_potential(RfMaxwellSolution *, char *, size_t);
int rf_maxwell_evaluate_potential(const RfMaxwellSolution *, double radius, int side,
    double complex *potential, double complex longitudinal_electric[3]);

#endif
