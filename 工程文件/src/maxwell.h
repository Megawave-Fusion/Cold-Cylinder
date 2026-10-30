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
    Project project; /* 本次离散解的驱动、几何与基础网格参数快照 */
    Plasma plasma;   /* 本次实际材料；正小量末级不等同于原界面输入 */
    int has_plasma;  /* 0: 真空；1: 使用 plasma 中的实际冷材料 */
    size_t nodes;    /* 包含界面和局部加密后的真实径向节点数 */
    double *r;       /* 严格递增径向节点[m]，首尾为 0 和 R */
    double complex *coefficients; /* P_j,Z_j,D_j，PDF (2.38) */
    double complex *potential;    /* 可选库仑标量电势谱节点[V m]，Phi(R)=0 */
    double potential_backward_error; /* 独立 Poisson 投影原系统的无量纲后向误差 */
    double *cumulative;           /* ∫_0^r rho q_spec d rho [W m] */
    double complex source[3];     /* 只用于展示的 K_eff [A] */
    double backward_error; /* 原 Maxwell 约束系统的无量纲后向误差 */
    double source_power;           /* dP_src/dkz [W m] */
    double complex load_impedance; /* dZ/dkz [ohm m], engineering exp(+i omega t), X>0 inductive */
    double absorbed_power;         /* dP_abs/dkz [W m]，无额外 2pi */
    double power_balance_error; /* 独立源供功与材料吸收的归一化差 */
    double peak_electric; /* 内部各单元双侧端点及内部点上的谱场峰值估计 [V] */
    size_t quadrature_order; /* 本次组装/材料功率共用的 Gauss 点数 */
    int complete; /* 全部组装、线性解、场恢复和功率步骤成功才为 1 */
} RfMaxwellSolution;

/* 清空独占结果，允许重复释放；首次使用对象应初始化为 {0}。 */
void rf_maxwell_free(RfMaxwellSolution *solution);
/* 固定材料的单谱底层求解：plasma=NULL 为真空。
 * 零碰撞极限序列由 rf_plasma_solve 组织，此接口不自动延拓。
 * 失败会清空 solution；project/plasma 允许指向旧 solution 内部。 */
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
/* 原内部 FEM 网格上的径向累计吸收谱[W m]，不是显示点梯形积分。 */
double rf_maxwell_cumulative(const RfMaxwellSolution *solution, double radius);
/* 纵向 Helmholtz 投影：Laplacian Phi=-div E，Phi(R)=0，轴心正则。
 * E=-grad Phi+i omega A，div A=0；不除以 kz，故 kz=0 也可用。
 * 这是不修改原 Maxwell 场或吸收的独立诊断。 */
int rf_maxwell_prepare_potential(RfMaxwellSolution *, char *, size_t);
/* 返回 Phi_hat[V m] 与可选的纵向电场谱[V]；side 的取侧约定同上。 */
int rf_maxwell_evaluate_potential(const RfMaxwellSolution *, double radius, int side,
    double complex *potential, double complex longitudinal_electric[3]);

#endif
