#include "rf_model.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* 参数校验统一诊断出口；允许error为空或长度为0。 */
static int fail(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size != 0U) {
        (void)snprintf(error, error_size, "%s", message);
    }
    return 0;
}

/* 数值域守卫，先排除NaN/Inf，避免比较表达式把非法浮点数放行。 */
static int finite_value(double value)
{
    return isfinite(value) != 0;
}

/* 建立17类给定电流模型共用的有效基线，频率用MHz、其余长度用m。 */
void project_default(Project *p)
{
    if (p == NULL) {
        return;
    }
    *p = (Project){ .type = RF_ANT_LOOP,
        .elements = 4,
        .current_direction = 1,
        .winding_direction = 1,
        .global_phase_deg = 0.0,
        .selected_m = 1,
        .kz = 10.0,
        .radial_cells = 256,
        .radius = 0.60,
        .length = 1.60,
        .frequency_mhz = 100.0,
        .current = 1.0,
        .phase_step = 90.0,
        .antenna_radius = 0.44,
        .antenna_length = 0.70,
        .width = 0.06,
        .spacing = 0.18,
        .turns = 1.0,
        .azimuth_deg = 0.0,
        .axial_offset = 0.0,
        .arc_angle_deg = 120.0,
        .return_radius = 0.55,
        .band_width = 0.02,
        .band_samples = 1,
        .inner_radius = 0.12,
        .gap_angle_deg = 12.,
        .source_mode = 1,
        .current_pattern = 0,
        .current_phase_gradient = 0. };
}

/* 默认冷电子+单离子材料；碰撞率为独立输入的有效阻尼，不按密度推算。 */
void plasma_default(Plasma *p)
{
    if (p == NULL) {
        return;
    }
    *p = (Plasma){ .radius = 0.30,
        .density = 1.0e16,
        .edge_ratio = 0.20,
        .magnetic_field = 0.02,
        .electron_collision = 5.0e6,
        .ion_collision = 1.0e4,
        .electron_collision_edge_ratio = 1.0,
        .ion_collision_edge_ratio = 1.0,
        .ion_mass_u = 39.948,
        .ion_charge = 1,
        .profile = RF_PLASMA_PARABOLIC,
        .solve_mode = RF_PLASMA_FINITE_COLLISION };
}

/* 仅在外部状态边界迁移旧直接无损选项；保留碰撞输入供切回有限碰撞使用。
 * 内部解析无损参考调用不经过此函数，以免改成需要数值延拓的全波请求。 */
void plasma_normalize_legacy_mode(Plasma *p)
{
    if (p && p->solve_mode == RF_PLASMA_LOSSLESS_REFERENCE) {
        p->solve_mode = RF_PLASMA_ZERO_COLLISION_LIMIT;
    }
}

/* 初始化模式范围、空间采样和旧格式兼容元数据；当前公共k扫描另存于WorkbenchProject。 */
void options_default(Options *o)
{
    if (o == NULL) {
        return;
    }
    *o = (Options){ .nk = 81,
        .m_min = 0,
        .m_max = 0,
        .nz = 161,
        .nr = 129,
        .kmin = -80.0,
        .kmax = 80.0,
        .zmin = -1.5,
        .zmax = 1.5,
        .phi = 0.0 };
}

/* 默认射线从一个显式物理位置发射；步长/路径长度单位m，积分相对容差无量纲。 */
void rf_ray_config_default(RfRayConfig *r)
{
    if(r) *r=(RfRayConfig){.r0=.2,.z0=0,.phi0_deg=0,.tolerance=1e-5,
        .max_step=.005,.max_path=5,.direction=0,.max_steps=10000};
}
/* 验证有限起点、方向和积分预算；起点是否恰在材料界面由实际发射器进一步检查。 */
int rf_ray_config_validate(const RfRayConfig *r,char *error,size_t n)
{
    if(!r || !isfinite(r->r0) || !isfinite(r->z0) || !isfinite(r->phi0_deg) ||
       !isfinite(r->tolerance) || !isfinite(r->max_step) || !isfinite(r->max_path) ||
       r->r0<=0 || r->r0>100 || fabs(r->z0)>1000 || fabs(r->phi0_deg)>1e6 ||
       r->tolerance<1e-9 || r->tolerance>1e-2 || r->max_step<1e-8 || r->max_step>100 ||
       r->max_path<1e-6 || r->max_path>1e6 || r->direction<-1 || r->direction>1 ||
       r->max_steps<10 || r->max_steps>100000)
        return fail(error,n,"invalid ray launch/integration parameters");
    return 1;
}
/* 组合几何、材料、空间与射线默认值；首次选择真空栏目，但保留等离子体设置。 */
void workbench_default(WorkbenchProject *w)
{
    if (w == NULL) {
        return;
    }
    project_default(&w->project);
    plasma_default(&w->plasma);
    options_default(&w->options);
    rf_ray_config_default(&w->ray);
    /* The GUI defaults to a vacuum project; plasma options remain ready for
     * loading when the user clicks the medium toggle. */
    w->options.m_min = w->project.selected_m;
    w->options.m_max = w->project.selected_m;
    w->plasma_enabled = 0;
    w->kmin = -40.0;
    w->kmax = 40.0;
    w->nk = 121;
}

/* 先检查数值/整数域，再执行模板专属几何约束；
 * 当前模型规定天线电流，轴向显示长度不是端板边界，也不涉及端口自洽馈电。 */
int project_validate(const Project *p, char *error, size_t error_size)
{
    if (p == NULL) {
        return fail(error, error_size, "project is null");
    }
    if (p->type < RF_ANT_STRAP || p->type > RF_ANT_MEANDER) {
        return fail(error, error_size, "antenna type is invalid");
    }
    if (p->elements < 1 || p->elements > 32 || p->radial_cells < 64 || p->radial_cells > 2048 ||
        p->band_samples < 1 || p->band_samples > 32) {
        return fail(error, error_size, "integer project range is invalid");
    }
    if (p->current_direction != -1 && p->current_direction != 1) {
        return fail(error, error_size, "current_direction must be -1 or 1");
    }
    if (p->winding_direction != -1 && p->winding_direction != 1) {
        return fail(error, error_size, "winding_direction must be -1 or 1");
    }
    if (p->selected_m < -16 || p->selected_m > 16) {
        return fail(error, error_size, "selected_m must be in -16..16");
    }
    if (!finite_value(p->global_phase_deg) || !finite_value(p->kz) || !finite_value(p->radius) ||
        !finite_value(p->length) || !finite_value(p->frequency_mhz) || !finite_value(p->current) ||
        !finite_value(p->phase_step) || !finite_value(p->antenna_radius) ||
        !finite_value(p->antenna_length) || !finite_value(p->width) || !finite_value(p->spacing) ||
        !finite_value(p->turns) || !finite_value(p->azimuth_deg) ||
        !finite_value(p->axial_offset) || !finite_value(p->arc_angle_deg) ||
        !finite_value(p->return_radius) || !finite_value(p->band_width)) {
        return fail(error, error_size, "project contains a non-finite value");
    }
    if (!(p->radius > 0.02 && p->radius <= 100.0) || !(p->length > 0.04 && p->length <= 200.0) ||
        !(p->frequency_mhz > 0.0 && p->frequency_mhz <= 10000.0) ||
        !(p->current > 0.0 && p->current <= 1.0e6) ||
        !(p->antenna_radius > 0.0 && p->antenna_radius < p->radius) ||
        !(p->band_width >= 0.0 && p->band_width < p->antenna_radius) || fabs(p->kz) > 2000.0 ||
        fabs(p->global_phase_deg) > 1.0e6 || fabs(p->azimuth_deg) > 1.0e6) {
        return fail(error, error_size, "project value is outside its valid range");
    }
    /* 所有长度/相位都先检查数值域；回流半径的容器约束只在使用该参数的模板生效。 */
    if (!(p->antenna_length > 0. && p->antenna_length <= 200.) ||
        !(p->width > 0. && p->width <= 100.) || !(p->spacing >= 0. && p->spacing <= 200.) ||
        !(p->turns >= 0.001 && p->turns <= 100.) || fabs(p->phase_step) > 1e6 ||
        !(p->arc_angle_deg > 0. && p->arc_angle_deg <= 360.) ||
        !(p->return_radius > 0. && p->return_radius <= 100.))
        return fail(error, error_size, "antenna dimensions or phase are outside their valid range");
    if ((p->type == RF_ANT_STRAP || p->type == RF_ANT_ARRAY || p->type == RF_ANT_COMB) &&
        p->width > 6.283185307179586 * p->antenna_radius)
        return fail(error, error_size, "antenna arc width exceeds circumference");
    if ((p->type == RF_ANT_LINE || p->type == RF_ANT_PARTIAL_LOOP ||
            p->type == RF_ANT_HALF_NAGOYA || p->type == RF_ANT_HALF_ARRAY) &&
        !(p->antenna_radius < p->return_radius && p->return_radius < p->radius))
        return fail(error, error_size, "return conductor must lie inside vessel");
    if (!isfinite(p->inner_radius) || !isfinite(p->gap_angle_deg) ||
        !isfinite(p->current_phase_gradient) || p->inner_radius <= 0. ||
        p->gap_angle_deg <= 0. || p->gap_angle_deg >= 90. ||
        fabs(p->current_phase_gradient) > 2000. || p->source_mode < 0 || p->source_mode > 16 ||
        p->current_pattern < 0 || p->current_pattern > 1)
        return fail(error, error_size, "invalid prescribed-current parameters");
    if ((p->type == RF_ANT_PLANAR_SPIRAL || p->type == RF_ANT_SEGMENTED_RINGS) &&
        p->inner_radius >= p->antenna_radius)
        return fail(error, error_size, "平面源必须满足 0 < 内半径 < 外半径 < 容器半径");
    if (p->type == RF_ANT_SEGMENTED_RINGS && p->elements > 8)
        return fail(error, error_size, "分段环的径向环数应为 1 到 8");
    if ((p->type == RF_ANT_AXIAL_LOOPS || p->type == RF_ANT_MEANDER) &&
        (p->elements < 2 || p->spacing <= 0.))
        return fail(error, error_size, "多环或蛇形源至少需要两个单元，轴向间距必须为正");
    if (p->type == RF_ANT_BIRDCAGE &&
        (p->elements < 3 || p->source_mode < 1 || p->source_mode >= p->elements))
        return fail(error, error_size, "鸟笼需 N>=3 且 1<=电流谐波 q<N，以保证端环电流闭合");
    if (p->type == RF_ANT_MEANDER && p->width >= 2.*3.14159265358979323846*p->antenna_radius)
        return fail(error, error_size, "蛇形横向弧长必须小于圆周长");
    return 1;
}

/* 检查单离子材料和碰撞剖面输入；Gaussian边缘比必须为正，零密度仍允许真空极限。 */
int plasma_validate(const Plasma *p, char *error, size_t error_size)
{
    if (p == NULL) {
        return fail(error, error_size, "plasma is null");
    }
    if (!finite_value(p->radius) || !finite_value(p->density) || !finite_value(p->edge_ratio) ||
        !finite_value(p->magnetic_field) || !finite_value(p->electron_collision) ||
        !finite_value(p->ion_collision) || !finite_value(p->electron_collision_edge_ratio) ||
        !finite_value(p->ion_collision_edge_ratio) || !finite_value(p->ion_mass_u)) {
        return fail(error, error_size, "plasma contains a non-finite value");
    }
    if (!(p->radius > 0.0) || !(p->density >= 0.0 && p->density <= 1.0e25) ||
        !(p->edge_ratio >= 0.0 && p->edge_ratio <= 1.0) || !(fabs(p->magnetic_field) <= 100.0) ||
        !(p->electron_collision >= 0.0 && p->electron_collision <= 1.0e16) ||
        !(p->ion_collision >= 0.0 && p->ion_collision <= 1.0e16) ||
        !(p->electron_collision_edge_ratio >= 0.0 && p->electron_collision_edge_ratio <= 1.0e6) ||
        !(p->ion_collision_edge_ratio >= 0.0 && p->ion_collision_edge_ratio <= 1.0e6) ||
        !(p->ion_mass_u >= 0.1 && p->ion_mass_u <= 300.0) || p->ion_charge < 1 ||
        p->ion_charge > 92 || p->profile < RF_PLASMA_UNIFORM || p->profile > RF_PLASMA_GAUSSIAN ||
        p->solve_mode < RF_PLASMA_FINITE_COLLISION ||
        p->solve_mode > RF_PLASMA_ZERO_COLLISION_LIMIT) {
        return fail(error, error_size, "plasma value is outside its valid range");
    }
    if (p->profile == RF_PLASMA_GAUSSIAN && p->edge_ratio == 0.0) {
        return fail(error, error_size, "Gaussian edge_ratio must be greater than zero");
    }
    return 1;
}

/* 检查当前采样与模式范围；物理窗口的周期/解析度约束由空间求解入口检查。 */
int options_validate(const Options *o, char *error, size_t error_size)
{
    if (o == NULL) {
        return fail(error, error_size, "options is null");
    }
    if (!finite_value(o->kmin) || !finite_value(o->kmax) || !finite_value(o->zmin) ||
        !finite_value(o->zmax) || !finite_value(o->phi)) {
        return fail(error, error_size, "options contains a non-finite value");
    }
    if (o->m_min < -16 || o->m_max > 16 ||
        o->m_min > o->m_max || o->nk < 3 || o->nk > 4097 || o->nr < 17 || o->nr > 2049 ||
        o->nz < 3 || o->nz > 1001 || !(o->kmin < o->kmax) || o->kmin < -2000.0 ||
        o->kmax > 2000.0 || !(o->zmin < o->zmax)) {
        return fail(error, error_size, "options value is outside its valid range");
    }
    return 1;
}

/* 联合校验工程状态；选用等离子体时要求其半径小于参考天线半径和容器半径。 */
int workbench_validate(const Project *project,
    const Plasma *plasma,
    const Options *options,
    int plasma_enabled,
    char *error,
    size_t error_size)
{
    if (plasma_enabled != 0 && plasma_enabled != 1) {
        return fail(error, error_size, "plasma_enabled must be 0 or 1");
    }
    if (!project_validate(project, error, error_size) ||
        !plasma_validate(plasma, error, error_size) ||
        !options_validate(options, error, error_size)) {
        return 0;
    }
    if (plasma_enabled &&
        !(plasma->radius < project->antenna_radius && project->antenna_radius < project->radius)) {
        return fail(error,
            error_size,
            "plasma radius must be smaller than antenna radius and vessel radius");
    }
    return 1;
}

/* 校验保存/CLI/GUI共享的完整状态，并验证公共k扫描与显式射线配置。 */
int workbench_state_validate(const WorkbenchProject *w, char *error, size_t error_size)
{
    if (w == NULL) {
        return fail(error, error_size, "workbench is null");
    }
    if (!rf_ray_config_validate(&w->ray,error,error_size)) return 0;
    if (!workbench_validate(
            &w->project, &w->plasma, &w->options, w->plasma_enabled, error, error_size)) {
        return 0;
    }
    if (!finite_value(w->kmin) || !finite_value(w->kmax) || !(w->kmin < w->kmax) ||
        w->kmin < -2000.0 || w->kmax > 2000.0 || w->nk < 3 || w->nk > 401) {
        return fail(error, error_size, "vacuum scan range is invalid");
    }
    return 1;
}
