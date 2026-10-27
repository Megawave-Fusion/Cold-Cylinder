#ifndef RF_GEOMETRY_H
#define RF_GEOMETRY_H

/*
 * Lightweight, dependency-free antenna geometry used by the C workbench.
 * Coordinates are SI metres.  A geometry owns a dynamic array of centre-line
 * segments; callers must release it with geometry_free().
 */

#include "rf_model.h"

#include <stddef.h>
#include <complex.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GeometrySegment {
    double a[3];
    double b[3];
    double phase;     /* rad，支路相位，不含整体驱动相位 */
    double amplitude; /* 无量纲，乘峰值电流及整体 current_direction */
    double phase_advance; /* rad; additional linear current phase from a to b */
    int group;
    int cylindrical;
    /* 等流线端点随跨宽坐标 u [m] 的变化率。
     * cylindrical=1 时为 (dr/du,dphi/du,dz/du)，其余为 Cartesian 导数。
     * 一整条闭合回路共享同一 u，转角和回流不能分别丢弃宽度修正。 */
    double side_a[3];
    double side_b[3];
} GeometrySegment;

typedef struct Geometry {
    GeometrySegment *segments;
    size_t segment_count;
    size_t segment_capacity;
    double band_width; /* m；按 du/band_width 平均完整闭合等流线 */
    double reference_radius;
    int band_samples; /* 宽度 Gauss 求积的最低点数，内核至少使用 16 点 */
    int band_kind;
} Geometry;

void geometry_init(Geometry *geometry);
void geometry_free(Geometry *geometry);
void geometry_release(Geometry *geometry);

size_t geometry_count(const Geometry *geometry);
size_t geometry_segment_count(const Geometry *geometry);

/* Dedicated high-resolution loop/helix builders. */
int geometry_build_loop(const Project *project, Geometry *geometry, char *error, size_t error_size);
int geometry_build_helix(
    const Project *project, Geometry *geometry, char *error, size_t error_size);

/* Dispatch the seventeen prescribed-current antenna families. */
int geometry_build(const Project *project, Geometry *geometry, char *error, size_t error_size);

/* Export centre lines as a Wavefront OBJ line list. */
int geometry_export_obj(const Geometry *geometry, const char *path, char *error, size_t error_size);

/* Fourier current spectrum K_eff=(r,phi,z) in amperes for a selected mode. */
int geometry_current_spectrum(const Project *project,
    const Geometry *geometry,
    double complex spectrum[3],
    char *error,
    size_t error_size);

double geometry_current_balance(const Geometry *geometry);

/* 完整有向源的统一积分入口。weighted_dl 已含 I exp(-i phase)/(2pi)、
 * 宽度权重和有向线元，单位 A m；调用者可加载 FEM 或计算本征模投影。
 * 径向节点用于把穿过多个单元的馈线分段，避免跨不连续基函数求积。
 * 不将 K_eff 压回参考半径；多半径源的位置在每个回调中保持不变。 */
typedef int (*RfSourceVisitor)(void *context, double radius, const double complex weighted_dl[3]);
int geometry_integrate_source(const Project *project,
    const Geometry *geometry,
    const double *radial_nodes,
    size_t node_count,
    RfSourceVisitor visitor,
    void *context,
    char *error,
    size_t error_size);

#ifdef __cplusplus
}
#endif

#endif /* RF_GEOMETRY_H */
