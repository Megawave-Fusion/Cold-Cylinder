#ifndef RF_PLOT_H
#define RF_PLOT_H

#include "vacuum.h"
#include "plasma.h"

/* CLI幅值曲线导出，横轴r[m]、纵轴|E_hat|[V]；仅接受已完成结果。
 * 全部写入和关闭检查成功才返回1，error为空或长度0时仍返回正确状态码。 */
int rf_vacuum_export_svg(
    const RfVacuumSolution *solution, const char *path, char *error, size_t error_size);
int rf_plasma_export_svg(
    const RfPlasmaSolution *solution, const char *path, char *error, size_t error_size);

#endif
