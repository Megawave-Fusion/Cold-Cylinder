#ifndef RF_BRANCHES_H
#define RF_BRANCHES_H
#include "plasma.h"
#include <stdio.h>

/* 固定 omega,m,kz 的局部 WKB 诊断，不是圆柱全局本征模分解。
 * 每个 r 有两支 Q 和各自 ±kr；electric 为单位范数局部极化，非天线场。
 * vg 是匹配的无碰撞参考分支的群速度，仅 weak-loss simple roots 有效。 */
enum RfBranchState {
    RF_BRANCH_PROPAGATING, RF_BRANCH_EVANESCENT, RF_BRANCH_DAMPED,
    RF_BRANCH_DEGENERATE, RF_BRANCH_SINGULAR, RF_BRANCH_MATERIAL_ERROR
};
/* 保守局部命名，与 A/B 连续编号及传播资格分开；不把命名窗口当物理截止。 */
enum RfBranchWaveType {
    RF_WAVE_UNIDENTIFIED, RF_WAVE_VACUUM, RF_WAVE_HELICON, RF_WAVE_TG, RF_WAVE_MIXED,
    RF_WAVE_ICRF_FAST, RF_WAVE_ICRF_SLOW
};
typedef struct {
    double complex q, kr, q_reference, kr_reference, electric[3];
    /* q/reference 为 Q=(k_perp/k0)^2（无量纲），kr/reference 为 rad/m；
     * electric 是单位 Hermitian 范数极化（无量纲），不是实际 E[V/m]。 */
    double vg[3], longitudinal_fraction, polarization_error; /* m/s；后两项无量纲 */
    double damping_ratio, reference_shift, wkb, root_gap;   /* 无量纲诊断 */
    double ray_z, ray_phi, ray_time, ray_optical_depth; /* m,rad,s,无量纲 tau */
    int valid, polarization_valid, group_valid, ray_valid, ray_id, state;
    int wave_type;
    double wave_match_error; /* 无量纲；H/TG 比较 beta，ICRF 比较完整参考 Q */
    char wave_reason[128];
    char reason[160], ray_stop[200];
} RfBranchPoint;
typedef struct {
    double r, path, kr_integrated, dispersion_error, correction;
    /* r/path 为 m，kr_integrated/correction 为 rad/m，残差无量纲。
     * 原始积分波数和投影校正均保留，不能只导出已校正的零残差。 */
    RfBranchPoint local; /* 实际根、参考群速度、z/phi/time/tau */
} RfRayPoint;
typedef struct {
    RfRayPoint *points;
    size_t count, capacity, rejected;
    int mode; /* 0:A+, 1:A-, 2:B+, 3:B-；符号为相位 kr，不是能流方向 */
    int launch_wave_type;
    char stop[200]; /* 含拒绝发射原因；不发射时 points 为空、count=0 */
} RfBranchRay;
typedef struct {
    Project project;
    Plasma plasma;
    size_t nr;
    double *r;
    int *region; /* 0 plasma, 1 vacuum; 界面两侧单独采样，禁止跨界积分 */
    RfBranchPoint *points; /* [4*i + 2*branch + sign], sign=0:+kr, 1:-kr */
    RfRayConfig ray_config;
    RfBranchRay rays[4]; /* 独立自适应轨迹，不从径向诊断样本插值得到 */
    size_t valid_points, ray_points, ray_count;
    char note[3200];
} RfBranchSolution;
/* 拥有数组的结果须先置 {0}；成功求解替换旧对象，失败保留调用者旧结果。 */
void rf_branches_free(RfBranchSolution *s);
/* C 接口默认起点 r0=0.65a；用户界面使用下方显式配置入口。 */
int rf_branches_solve(const Project *p, const Plasma *c, RfBranchSolution *s,
    char *error, size_t n);
int rf_branches_solve_config(const Project *p,const Plasma *c,const RfRayConfig *config,
    RfBranchSolution *s,char *error,size_t n);
const char *rf_branch_state_name(int state);
const char *rf_branch_wave_name(int type);
/* 两类 CSV 共用求解快照，但径向表与沿程表有独立单位、行次序和失败标志。
 * *_csv_row 的索引须合法；buffer 至少 3000 字节时可完整容纳当前一行。 */
const char *rf_branches_csv_header(void);
void rf_branches_csv_row(const RfBranchSolution *s, size_t i, int mode,
    char *buffer, size_t n);
int rf_branches_export_csv(const RfBranchSolution *s, const char *path, char *error, size_t n);
int rf_branches_export_rays(const RfBranchSolution *s, const char *path, char *error, size_t n);
const char *rf_rays_csv_header(void);
void rf_rays_csv_row(const RfBranchSolution *s,int mode,size_t i,char *buffer,size_t n);
#endif
