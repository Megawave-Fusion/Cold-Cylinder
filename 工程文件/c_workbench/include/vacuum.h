#ifndef RF_VACUUM_H
#define RF_VACUUM_H

/*
 * Small, dependency-free numerical layer used by the C workbench.
 *
 * The C workbench uses the same matrix and source conventions as the project
 * model, exposed through a self-contained C11 implementation.  The
 * vacuum solver uses the coupled cylindrical Maxwell weak form and compatible
 * vector finite elements described in the project PDF. See src/maxwell.c.
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

/* Row-major dense complex matrix.  Values has rows*cols elements. */
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
int rf_complex_matrix_add(RfComplexMatrix *matrix, size_t row, size_t col, double complex value);

/*
 * General complex band matrix.  Storage is LAPACK-like: each column has
 * lower+upper+1 entries and the diagonal is at offset upper.  Access outside
 * the declared band is rejected by set/add and reads as zero.
 */
typedef struct RfComplexBandMatrix {
    size_t n;
    size_t lower;
    size_t upper;
    double complex *values;
} RfComplexBandMatrix;

int rf_complex_band_init(RfComplexBandMatrix *matrix, size_t n, size_t lower, size_t upper);
void rf_complex_band_free(RfComplexBandMatrix *matrix);
void rf_complex_band_zero(RfComplexBandMatrix *matrix);
double complex rf_complex_band_get(const RfComplexBandMatrix *matrix, size_t row, size_t col);
int rf_complex_band_set(RfComplexBandMatrix *matrix, size_t row, size_t col, double complex value);
int rf_complex_band_add(RfComplexBandMatrix *matrix, size_t row, size_t col, double complex value);

/* Solve A*x=b; A is unchanged. rhs and solution have n entries and may alias.
 * backward_error is the residual of the original system, not a physical error. */
RfLinearStatus rf_complex_solve(const RfComplexMatrix *matrix,
    const double complex *rhs,
    double complex *solution,
    double *backward_error);
RfLinearStatus rf_complex_band_solve(const RfComplexBandMatrix *matrix,
    const double complex *rhs,
    double complex *solution,
    double *backward_error);
double rf_complex_residual_inf(
    const RfComplexMatrix *matrix, const double complex *solution, const double complex *rhs);

/* A compact vacuum field result (electric[component*nr + radial_index]). */
typedef struct RfVacuumSolution {
    Project project;
    size_t nr;
    double *r;
    double complex *electric;
    double complex *magnetic; /* 从 FEM 解析旋度得到，T m */
    double *total;
    double complex source[3];
    int complete;
    double backward_error;
    double source_power;  /* dP_src/dkz，W m；无损非极点时实部接近零 */
    double peak_electric; /* 内部有限元网格的峰值估计，不依赖显示降采样 */
    struct RfMaxwellSolution *fem; /* 内部非均匀网格与系数，独占拥有 */
    char message[192];
    unsigned long magic;
} RfVacuumSolution;

/* Initialize stack-allocated result objects before the first solve. */
void rf_vacuum_solution_init(RfVacuumSolution *solution);
void rf_vacuum_solution_free(RfVacuumSolution *solution);
int rf_vacuum_solve(
    const Project *project, RfVacuumSolution *solution, char *error, size_t error_size);
double complex rf_vacuum_field(const RfVacuumSolution *solution, double radius, int component);
int rf_vacuum_export_csv(
    const RfVacuumSolution *solution, const char *path, char *error, size_t error_size);

/* Uniform k_z spectrum scan.  electric is optional and can be omitted. */
typedef struct RfVacuumScan {
    Project project;
    size_t nk;
    size_t nr;
    double kmin;
    double kmax;
    double *k;
    int *valid;
    double *total;
    double complex *current;
    double complex *electric;
    double *backward_error;
    char **reason;
    int complete;
    unsigned long magic;
} RfVacuumScan;

void rf_vacuum_scan_init(RfVacuumScan *scan);
void rf_vacuum_scan_free(RfVacuumScan *scan);
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
