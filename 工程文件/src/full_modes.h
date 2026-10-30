#ifndef RF_FULL_MODES_H
#define RF_FULL_MODES_H
#include "spatial.h"

/* 给定phi截面的有限m相干和及k积分，相量exp(-i omega t)。
 * 所有拥有数组的结果必须先{0}；完成后释放临时谱场，仅保留公开结果数组。
 * 任意m失败使整个任务失败，不能把缺失模式补零后宣称得到完整总场。 */
typedef struct {
    RfSpatialConfig space;
    int m_min, m_max;
    double phi; /* radians */
} RfFullConfig;
typedef struct {
    Project project;
    Plasma plasma;
    RfFullConfig config;
    double *r, *z;
    double complex *electric, *magnetic, *current; /* z-major，[iz*nr+ir]*3+c；V/m、T、A/m² */
    double *absorption, *axial_flux, *shell;       /* W/m³、W/m²、W/m²，能流保留正负号 */
    /* shell=r integral_0^2pi q dphi=2pi*r*sum_m q_m, NOT 2pi*r*q(phi). */
    double window_power, spectral_power, power_balance_error;
    double band_edge_fraction, impedance_edge_fraction, edge_ratio, radial_check_error;
    double complex impedance;
    double progress;
    int complete;
    void *work;
} RfFullSolution;
/* 提取共用扫描与空间设置；NULL返回待校验的零配置。 */
RfFullConfig rf_full_config(const WorkbenchProject *);
int rf_full_begin(
    const Project *, const Plasma *, const RfFullConfig *, RfFullSolution *, char *, size_t);
int rf_full_step(RfFullSolution *, char *, size_t);
int rf_full_solve(
    const Project *, const Plasma *, const RfFullConfig *, RfFullSolution *, char *, size_t);
void rf_full_free(RfFullSolution *);
#endif
