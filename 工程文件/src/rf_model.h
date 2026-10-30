#ifndef RF_MODEL_H
#define RF_MODEL_H

/*
 * Small, dependency-free C11 data model for the RF workbench.
 *
 * Units follow the workbench: lengths are metres, current is peak
 * amperes, frequency_mhz is MHz, angles whose name ends in _deg are degrees,
 * and wave numbers are radians per metre.  The model is shared by the native
 * numerical solvers and the Tcl/Tk interface.
 */

#include <stddef.h>
#include "rf_physics.h"

#ifdef __cplusplus
extern "C" {
#endif

enum rf_antenna_type {
    RF_ANT_STRAP = 1,
    RF_ANT_ARRAY = 2,
    RF_ANT_COMB = 3,
    RF_ANT_LOOP = 4,
    RF_ANT_HELIX = 5,
    RF_ANT_SINGLE_SADDLE = 6,
    RF_ANT_SADDLE = 7,
    RF_ANT_NAGOYA = 8,
    RF_ANT_LINE = 9,
    RF_ANT_PARTIAL_LOOP = 10,
    RF_ANT_HALF_NAGOYA = 11,
    RF_ANT_HALF_ARRAY = 12,
    RF_ANT_AXIAL_LOOPS = 13,
    RF_ANT_PLANAR_SPIRAL = 14,
    RF_ANT_SEGMENTED_RINGS = 15,
    RF_ANT_BIRDCAGE = 16,
    RF_ANT_MEANDER = 17
};

enum rf_plasma_profile { RF_PLASMA_UNIFORM = 0, RF_PLASMA_PARABOLIC = 1, RF_PLASMA_GAUSSIAN = 2 };

enum rf_plasma_solve_mode {
    RF_PLASMA_FINITE_COLLISION = 0,  /* 有限碰撞：使用输入碰撞剖面 */
    RF_PLASMA_LOSSLESS_REFERENCE = 1, /* 内部解析无损参考，不作为界面求场模式 */
    RF_PLASMA_ZERO_COLLISION_LIMIT = 2 /* 自动正小量趋零，忽略输入碰撞参数 */
};

typedef struct Project {
    int type;                 /* 1..17，给定电流天线模板，不含馈线/端口反馈 */
    int elements;             /* 阵列、环、鸟笼腿或蛇形段数，含义由模板决定 */
    int current_direction;    /* ±1，整体有向电流符号 */
    int winding_direction;    /* ±1，螺旋绕向，独立于整体电流符号 */
    double global_phase_deg;  /* 整体驱动相位，deg；源相量乘exp(+i phase) */
    int selected_m;           /* 场与源的方位Fourier指标，-16..16，无量纲 */
    double kz;                /* 轴向Fourier波数，rad/m，保留正负号 */
    int radial_cells;         /* 基础径向单元数；实际网格还插入界面/共振/短波节点 */
    double radius;            /* 径向PEC容器半径R，m */
    double length;            /* 几何显示长度，m；轴向模型仍无限长，没有端盖 */
    double frequency_mhz;     /* 驱动频率，MHz；内核统一转成omega=2*pi*f */
    double current;           /* 给定峰值电流I0，A，非RMS值 */
    double phase_step;        /* 相邻阵列支路驱动相位差，deg */
    double antenna_radius;    /* 参考天线半径b，m；平面源用作外半径 */
    double antenna_length;    /* 有效模板的轴向源长度，m */
    double width;             /* 模板开口/横向弧长，m；区别于电流带跨宽band_width */
    double spacing;            /* m；矩形阵列为周向弧长，多环/蛇形为轴向间距 */
    double turns;             /* 螺旋匝数，无量纲 */
    double azimuth_deg;       /* 几何整体方位旋转，deg */
    double axial_offset;      /* 源的轴向平移，m；相位仍由实际源坐标参与积分 */
    double arc_angle_deg;     /* 鞍形/部分回路的开口角，deg */
    double return_radius;     /* 使用回流模板的回流半径c，m */
    double band_width;        /* 零厚度电流带的有限跨宽，m；积分对du/band_width平均 */
    int band_samples;         /* 带宽求积最低阶数；实际积分可提高阶数/分段数 */
    double inner_radius;          /* m; planar spiral / concentric rings */
    double gap_angle_deg;         /* angular gaps in segmented rings */
    int source_mode;              /* prescribed current harmonic q, distinct from selected_m */
    int current_pattern;          /* 0: cos(q phi), 1: exp(i q phi) */
    double current_phase_gradient; /* rad/m along the meander centre line */
} Project;

typedef struct Plasma {
    double radius;            /* 等离子体半径a，m；外侧介电响应为真空 */
    double density;           /* 轴心电子密度ne0，m^-3；单离子准中性ni=ne/Z */
    double edge_ratio;        /* ne(a-)/ne0，无量纲 */
    double magnetic_field;    /* 轴向静磁场B0，T，保留方向符号 */
    double electron_collision; /* 电子轴心有效碰撞率，s^-1，零碰撞极限不使用 */
    double ion_collision;      /* 离子轴心有效碰撞率，s^-1，零碰撞极限不使用 */
    double electron_collision_edge_ratio; /* nue(a-)/nue(0)，非负无量纲输入 */
    double ion_collision_edge_ratio;      /* nui(a-)/nui(0)，非负无量纲输入 */
    double ion_mass_u;         /* 单离子质量，原子质量单位u */
    int ion_charge;            /* 正整数电荷数Z；电子电荷的负号在材料函数中保留 */
    int profile;               /* 均匀、抛物线或Gaussian密度剖面枚举 */
    int solve_mode;            /* 有限碰撞/内部无损参考/零碰撞极限枚举 */
} Plasma;

typedef struct Options {
    int nk;                   /* 旧材料配置兼容字段；实际扫描取WorkbenchProject.nk */
    int m_min, m_max;          /* 全模式场的有限m求和范围，两端包含 */
    int nz, nr;                /* 实际空间z/r显示采样数，区别于FEM基础单元数 */
    double kmin, kmax;         /* 旧兼容元数据；实际扫描取WorkbenchProject.kmin/kmax */
    double zmin, zmax;         /* 观察窗口端点，m，不构成物理端板 */
    double phi;                /* 全模式空间截面方位角，rad */
} Options;

/* Fixed f,m,kz rays in the existing radially stratified cylinder. */
typedef struct RfRayConfig {
    double r0, z0, phi0_deg;   /* 发射半径/轴向位置[m]，方位角[deg] */
    double tolerance;          /* 路径RK误差和色散投影的相对验收容差 */
    double max_step, max_path; /* 路径参数的最大步长/最大长度，m */
    int direction; /* -1 inward energy, 0 both, +1 outward energy */
    int max_steps;             /* 自适应尝试步数上限，含拒绝步 */
} RfRayConfig;

/* Complete saved workbench state.  These fields are stored in one .rfproj
 * file; keeping the aggregate in C makes the GUI and
 * command line use exactly the same project representation. */
typedef struct WorkbenchProject {
    Project project;
    Plasma plasma;
    Options options;
    RfRayConfig ray;
    int plasma_enabled;        /* 介质分发/旧工程初始栏目提示，由当前栏目同步 */
    double kmin, kmax;         /* 当前真空/等离子体/空间计算共用的k区间，rad/m */
    int nk;                    /* 共用扫描点数；单谱仍使用project.kz */
} WorkbenchProject;

/* Populate a structure with a safe baseline. */
void project_default(Project *project);
void plasma_default(Plasma *plasma);
/* External state only: map old direct-lossless mode 1 to the causal limit.
 * Preserve the stored finite-collision values for switching back to mode 0.
 * Internal analytic references may still use mode 1. */
void plasma_normalize_legacy_mode(Plasma *plasma);
void options_default(Options *options);
void workbench_default(WorkbenchProject *workbench);
void rf_ray_config_default(RfRayConfig *config);
int rf_ray_config_validate(const RfRayConfig *config, char *error, size_t error_size);

/*
 * Return 1 when valid and 0 otherwise.  On failure, error receives a short
 * UTF-8 diagnostic (when error_size is non-zero).  Passing NULL is allowed.
 */
int project_validate(const Project *project, char *error, size_t error_size);
int plasma_validate(const Plasma *plasma, char *error, size_t error_size);
int options_validate(const Options *options, char *error, size_t error_size);
int workbench_state_validate(const WorkbenchProject *workbench, char *error, size_t error_size);

/* Validate the coupled values needed by the first workbench calculation. */
int workbench_validate(const Project *project,
    const Plasma *plasma,
    const Options *options,
    int plasma_enabled,
    char *error,
    size_t error_size);

#ifdef __cplusplus
}
#endif

#endif /* RF_MODEL_H */
