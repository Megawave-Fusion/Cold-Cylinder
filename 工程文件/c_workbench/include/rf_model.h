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
    RF_PLASMA_FINITE_COLLISION = 0,
    RF_PLASMA_DIRECT_RESONANCE = 1,   /* 兼容旧项目编号；无碰撞非共振边值问题 */
    RF_PLASMA_LIMITING_ABSORPTION = 2 /* PDF 第四章：正碰撞族及嵌套收敛检查 */
};

enum rf_sweep_kind { RF_SWEEP_DENSITY = 0, RF_SWEEP_FIELD = 1, RF_SWEEP_FREQUENCY = 2 };

enum rf_comparison_kind { RF_COMPARE_WIDTH = 0, RF_COMPARE_ANTENNA = 1 };

typedef struct Project {
    int type;
    int elements;
    int current_direction;
    int winding_direction;
    double global_phase_deg;
    int selected_m;
    double kz;
    int radial_cells;
    double radius;
    double length;
    double frequency_mhz;
    double current;
    double phase_step;
    double antenna_radius;
    double antenna_length;
    double width;
    double spacing; /* m；相邻矩形闭合单元中心的圆周弧长间距 */
    double turns;
    double azimuth_deg;
    double axial_offset;
    double arc_angle_deg;
    double return_radius;
    double band_width;
    int band_samples;
    double inner_radius;          /* m; planar spiral / concentric rings */
    double gap_angle_deg;         /* angular gaps in segmented rings */
    int source_mode;              /* prescribed current harmonic q, distinct from selected_m */
    int current_pattern;          /* 0: cos(q phi), 1: exp(i q phi) */
    double current_phase_gradient; /* rad/m along the meander centre line */
} Project;

typedef struct Plasma {
    double radius;
    double density;
    double edge_ratio;
    double magnetic_field;
    double electron_collision;
    double ion_collision;
    double electron_collision_edge_ratio;
    double ion_collision_edge_ratio;
    double ion_mass_u;
    int ion_charge;
    int profile;
    int solve_mode;
} Plasma;

typedef struct Options {
    int scans;
    int space;
    int sweeps;
    int nk;
    int m_min;
    int m_max;
    int nz;
    int nr;
    double kmin;
    double kmax;
    double zmin;
    double zmax;
    double phi;
    int sweep_kind;
    int sweep_points;
    int comparison_kind;
    int comparison_points;
    double sweep_min;
    double sweep_max;
    double comparison_min;
    double comparison_max;
} Options;

/* Fixed f,m,kz rays in the existing radially stratified cylinder. */
typedef struct RfRayConfig {
    double r0, z0, phi0_deg;
    double tolerance, max_step, max_path;
    int direction; /* -1 inward energy, 0 both, +1 outward energy */
    int max_steps;
} RfRayConfig;

/* Complete saved workbench state.  These fields are stored in one .rfproj
 * file; keeping the aggregate in C makes the GUI and
 * command line use exactly the same project representation. */
typedef struct WorkbenchProject {
    Project project;
    Plasma plasma;
    Options options;
    RfRayConfig ray;
    int plasma_enabled;
    double kmin;
    double kmax;
    int nk;
} WorkbenchProject;

/* Populate a structure with a safe baseline. */
void project_default(Project *project);
void plasma_default(Plasma *plasma);
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
