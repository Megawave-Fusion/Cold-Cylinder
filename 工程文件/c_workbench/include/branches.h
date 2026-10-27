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
/* Conservative local identification, independent of the A/B continuation ID. */
enum RfBranchWaveType {
    RF_WAVE_UNIDENTIFIED, RF_WAVE_VACUUM, RF_WAVE_HELICON, RF_WAVE_TG, RF_WAVE_MIXED
};
typedef struct {
    double complex q, kr, q_reference, kr_reference, electric[3];
    double vg[3], longitudinal_fraction, polarization_error;
    double damping_ratio, reference_shift, wkb, root_gap;
    double ray_z, ray_phi, ray_time, ray_optical_depth;
    int valid, polarization_valid, group_valid, ray_valid, ray_id, state;
    int wave_type;
    double wave_match_error;
    char wave_reason[128];
    char reason[160], ray_stop[200];
} RfBranchPoint;
typedef struct {
    double r, path, kr_integrated, dispersion_error, correction;
    RfBranchPoint local; /* actual root, reference group, z/phi/time/tau */
} RfRayPoint;
typedef struct {
    RfRayPoint *points;
    size_t count, capacity, rejected;
    int mode;
    int launch_wave_type;
    char stop[200]; /* also records refusal to launch */
} RfBranchRay;
typedef struct {
    Project project;
    Plasma plasma;
    size_t nr;
    double *r;
    int *region; /* 0 plasma, 1 vacuum; 界面两侧单独采样，禁止跨界积分 */
    RfBranchPoint *points; /* [4*i + 2*branch + sign], sign=0:+kr, 1:-kr */
    RfRayConfig ray_config;
    RfBranchRay rays[4]; /* independent adaptive paths; never radial-grid interpolation */
    size_t valid_points, ray_points, ray_count;
    char note[3200];
} RfBranchSolution;
void rf_branches_free(RfBranchSolution *s);
int rf_branches_solve(const Project *p, const Plasma *c, RfBranchSolution *s,
    char *error, size_t n);
int rf_branches_solve_config(const Project *p,const Plasma *c,const RfRayConfig *config,
    RfBranchSolution *s,char *error,size_t n);
const char *rf_branch_state_name(int state);
const char *rf_branch_wave_name(int type);
const char *rf_branches_csv_header(void);
void rf_branches_csv_row(const RfBranchSolution *s, size_t i, int mode,
    char *buffer, size_t n);
int rf_branches_export_csv(const RfBranchSolution *s, const char *path, char *error, size_t n);
int rf_branches_export_rays(const RfBranchSolution *s, const char *path, char *error, size_t n);
const char *rf_rays_csv_header(void);
void rf_rays_csv_row(const RfBranchSolution *s,int mode,size_t i,char *buffer,size_t n);
#endif
