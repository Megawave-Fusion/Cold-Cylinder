#ifndef RF_VACUUM_H
#define RF_VACUUM_H

/* C11 复数线性代数与真空单谱接口，共用 rf_model.h 的单位和峰值相量规范。
 * Maxwell 相容有限元、材料/源界面与 PEC 条件统一由 maxwell.c 实现；
 * 这里的稠密矩阵主要供独立参考核对，生产求场使用带状系统。
 */

#include "rf_model.h"

#include <complex.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RfLinearStatus {
    RF_LINEAR_OK = 0,
    RF_LINEAR_INVALID = 1,
    RF_LINEAR_OUT_OF_MEMORY = 2,
    RF_LINEAR_SINGULAR = 3,
    RF_LINEAR_NONFINITE = 4
} RfLinearStatus;

/* 稠密参考矩阵按行存储，values 有 rows*cols 个复数。
 * init 用于新对象或 free 后的对象，不用于覆盖尚未释放的分配。 */
typedef struct RfComplexMatrix {
    size_t rows;
    size_t cols;
    double complex *values;
} RfComplexMatrix;

int rf_complex_matrix_init(RfComplexMatrix *matrix, size_t rows, size_t cols);
void rf_complex_matrix_free(RfComplexMatrix *matrix);
void rf_complex_matrix_zero(RfComplexMatrix *matrix);
double complex rf_complex_matrix_get(const RfComplexMatrix *matrix, size_t row, size_t col);
int rf_complex_matrix_set(RfComplexMatrix *matrix, size_t row, size_t col, double complex value);

/* 一般复数带状矩阵按列压缩，每列 lower+upper+1 项，对角偏移为 upper。
 * 带外读零，带外 set/add 被拒绝；结构独占 values，不能浅复制后重复释放。 */
typedef struct RfComplexBandMatrix {
    size_t n;
    size_t lower;
    size_t upper;
    double complex *values;
} RfComplexBandMatrix;

int rf_complex_band_init(RfComplexBandMatrix *matrix, size_t n, size_t lower, size_t upper);
void rf_complex_band_free(RfComplexBandMatrix *matrix);
double complex rf_complex_band_get(const RfComplexBandMatrix *matrix, size_t row, size_t col);
int rf_complex_band_set(RfComplexBandMatrix *matrix, size_t row, size_t col, double complex value);
int rf_complex_band_add(RfComplexBandMatrix *matrix, size_t row, size_t col, double complex value);

/* 解 A*x=b，保留原 A。rhs、solution 各有 n 项，允许二者完全相同，
 * 不支持部分重叠；backward_error 可为 NULL，否则返回原系统的归一化
 * 残差，不是物理误差，也不是条件数估计。非有限中间量会明确返回失败。 */
RfLinearStatus rf_complex_solve(const RfComplexMatrix *matrix,
    const double complex *rhs,
    double complex *solution,
    double *backward_error);
RfLinearStatus rf_complex_band_solve(const RfComplexBandMatrix *matrix,
    const double complex *rhs,
    double complex *solution,
    double *backward_error);
/* 原始稠密 A、b 的绝对无穷范数残差；无效或非有限请求返回 NaN。 */
double rf_complex_residual_inf(
    const RfComplexMatrix *matrix, const double complex *solution, const double complex *rhs);

/* 真空单谱结果，初始为 {0}；electric[component*nr+radial_index]。
 * 显示样本与内部 FEM 都由此对象独占，free 会同时清理。 */
typedef struct RfVacuumSolution {
    Project project;
    size_t nr; /* 均匀显示/导出径向节点数，非 FEM 自由度数 */
    double *r; /* 显示半径[m] */
    double complex *electric; /* E_hat[V]，分量按 r/phi/z 排列 */
    double complex *magnetic; /* 从 FEM 解析旋度得到，T m */
    double *total; /* sqrt(sum |E_hat_c|²)[V] */
    double complex source[3]; /* 参考半径等效规定源谱[A]，仅用于显示 */
    int complete;
    double backward_error;
    double source_power;  /* dP_src/dkz[W m]；无损非极点仅有有功舍入残余 */
    double peak_electric; /* 内部有限元网格的峰值估计，不依赖显示降采样 */
    struct RfMaxwellSolution *fem; /* 内部非均匀网格与系数，独占拥有 */
    char message[192];
    unsigned long magic;
} RfVacuumSolution;

/* 首次调用前初始化为 {0}；free 与重复求解会清理旧结果。 */
void rf_vacuum_solution_free(RfVacuumSolution *solution);
/* 同时生成内部 FEM 与均匀显示样本，允许 project=&solution->project。 */
int rf_vacuum_solve(
    const Project *project, RfVacuumSolution *solution, char *error, size_t error_size);
/* 任意半径的原 FEM 电场谱[V]；component=0/1/2 对应 r/phi/z。 */
double complex rf_vacuum_field(const RfVacuumSolution *solution, double radius, int component);
/* 输出原复数 E/B、源供功和谱负载阻抗；写入失败返回诊断。 */
int rf_vacuum_export_csv(
    const RfVacuumSolution *solution, const char *path, char *error, size_t error_size);

/* 含端点的均匀 kz 扫描，初始为 {0}。complete 只表示已遍历；
 * 每个切片是否成功由 valid/reason 判断，失败数据为 NaN。 */
typedef struct RfVacuumScan {
    Project project;
    size_t nk; /* 轴向波数切片数 */
    size_t nr; /* 每片的均匀径向样本数 */
    double kmin; /* 扫描下限[rad/m] */
    double kmax; /* 扫描上限[rad/m] */
    double *k; /* kz[rad/m]，nk 个含端点样本 */
    int *valid; /* 每个 kz 的独立成功标志 */
    double *total; /* 每个 kz 的 nr 个电场谱总幅值[V]；未解/失败为 NaN */
    double complex *current; /* K_eff[A]，索引 3*ik+component */
    double complex *electric; /* E_hat[V]，索引 (3*ik+component)*nr+ir */
    double *backward_error; /* 每个成功切片的原 Maxwell 系统残差 */
    char **reason; /* 独占的逐切片失败原因，成功切片为 NULL */
    int complete;
    unsigned long magic;
} RfVacuumScan;

/* 释放扫描数组与逐切片原因字符串，允许重复调用。 */
void rf_vacuum_scan_free(RfVacuumScan *scan);
/* fields=0 只算规定源谱，不因场的真空极点丢失有效源；
 * fields=1 另存完整复场。输入可借用 scan->project，失败切片不填零。 */
int rf_vacuum_scan(const Project *project,
    double kmin,
    double kmax,
    size_t nk,
    int fields,
    RfVacuumScan *scan,
    char *error,
    size_t error_size);

#ifdef __cplusplus
}
#endif

#endif /* RF_VACUUM_H */
