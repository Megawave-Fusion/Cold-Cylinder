#ifndef RF_GEOMETRY_H
#define RF_GEOMETRY_H

/*
 * C 工作台的给定电流几何；端点坐标统一为 Cartesian SI 米。
 * Geometry 拥有中心线路径数组，须以 {0} 或 geometry_init 首次初始化，
 * 最后调用 geometry_free。持有数组时不可作浅拷贝后分别释放。
 */

#include "rf_model.h"

#include <stddef.h>
#include <complex.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GeometrySegment {
    double a[3]; /* 起点 (x,y,z)，m；与 cylindrical 标志无关 */
    double b[3]; /* 终点 (x,y,z)，m；端点顺序决定正向线元 */
    double phase;     /* rad，支路相位，不含整体驱动相位 */
    double amplitude; /* 无量纲，乘峰值电流及整体 current_direction */
    double phase_advance; /* rad，沿本段从 a 到 b 的线性电流相位增量 */
    int group;           /* 支路分组，仅作显示/审计，不额外改变电流 */
    int cylindrical;     /* 1: 按 r,phi,z 插值曲线；0: Cartesian 直线 */
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
    double reference_radius; /* m，仅为几何/谱展示的参考半径，不替代源的实际位置 */
    int band_samples; /* 宽度 Gauss 求积的最低点数，内核至少使用 16 点 */
    int band_kind;
} Geometry;

/* 首次初始化不释放旧内存；重复构建或销毁由 build/free 负责。 */
void geometry_init(Geometry *geometry);
void geometry_free(Geometry *geometry);

/* 中心线段数与带宽求积节点数不同。空对象返回 0。 */
size_t geometry_count(const Geometry *geometry);

/* 按模板编号构造十七类给定电流源；不解馈电或随负载自洽的导体电流。 */
int geometry_build(const Project *project, Geometry *geometry, char *error, size_t error_size);

/* Wavefront OBJ 仅导出中心线；有限宽度表面由 side_a/side_b 参数恢复。 */
int geometry_export_obj(const Geometry *geometry, const char *path, char *error, size_t error_size);

/* 固定 (m,kz) 的 K_eff=(r,phi,z)，单位 A；复数相位保留，不先取幅值。
 * 对多半径源，这是按参考半径归一化的谱矩，不是单一面上的真实 K。 */
int geometry_current_spectrum(const Project *project,
    const Geometry *geometry,
    double complex spectrum[3],
    char *error,
    size_t error_size);

/* 中心线节点电流不平衡的最大相对值；开口或变相位源可非零，并非求解失败。 */
double geometry_current_balance(const Geometry *geometry);

/* 完整有向源的统一积分入口。weighted_dl 已含峰值电流、驱动相位
 * exp(+i phase)、Fourier 因子 exp[-i(m phi+kz z)]/(2pi)、宽度权重
 * 和有向线元，单位 A m；调用者可加载 FEM 或计算本征模投影。
 * 径向节点须为有限非负值，用于把穿过多个单元的径向连接段分段，
 * 避免跨不连续基函数求积。visitor 返回 0 会立即中止积分并返回失败。
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
