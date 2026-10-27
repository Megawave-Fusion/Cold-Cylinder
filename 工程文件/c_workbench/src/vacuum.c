/* 复数线性代数与真空 Maxwell 公共接口。
 * 全矢量径向有限元、轴心正则性、切向 PEC 与源加载在 maxwell.c 实现。 */
#include "vacuum.h"
#include "geometry.h"
#include "maxwell.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef RF_PI
#define RF_PI 3.141592653589793238462643383279502884
#endif

#define RF_VACUUM_SOLUTION_MAGIC 0x5256534fUL
#define RF_VACUUM_SCAN_MAGIC 0x52565343UL

static int fail_text(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size != 0U) {
        (void)snprintf(error, error_size, "%s", message);
    }
    return 0;
}

static int complex_finite(double complex value)
{
    return isfinite(creal(value)) && isfinite(cimag(value));
}

static int size_product_ok(size_t a, size_t b)
{
    return b == 0U || a <= SIZE_MAX / b;
}

static size_t dense_index(const RfComplexMatrix *matrix, size_t row, size_t col)
{
    return row * matrix->cols + col;
}

/* 稠密矩阵按行存储；先核对乘法溢出，再分配 rows×cols 个复数。 */
int rf_complex_matrix_init(RfComplexMatrix *matrix, size_t rows, size_t cols)
{
    if (matrix == NULL || !size_product_ok(rows, cols) ||
        (rows != 0U && cols != 0U && rows * cols > SIZE_MAX / sizeof(*matrix->values))) {
        return 0;
    }
    matrix->rows = rows;
    matrix->cols = cols;
    matrix->values = NULL;
    if (rows != 0U && cols != 0U) {
        matrix->values = (double complex *)calloc(rows * cols, sizeof(*matrix->values));
        if (matrix->values == NULL) {
            matrix->rows = matrix->cols = 0U;
            return 0;
        }
    }
    return 1;
}

void rf_complex_matrix_free(RfComplexMatrix *matrix)
{
    if (matrix == NULL) {
        return;
    }
    free(matrix->values);
    matrix->values = NULL;
    matrix->rows = matrix->cols = 0U;
}

void rf_complex_matrix_zero(RfComplexMatrix *matrix)
{
    if (matrix != NULL && matrix->values != NULL && matrix->rows != 0U && matrix->cols != 0U) {
        (void)memset(matrix->values, 0, matrix->rows * matrix->cols * sizeof(*matrix->values));
    }
}

double complex rf_complex_matrix_get(const RfComplexMatrix *matrix, size_t row, size_t col)
{
    if (matrix == NULL || matrix->values == NULL || row >= matrix->rows || col >= matrix->cols) {
        return 0.0 + 0.0 * I;
    }
    return matrix->values[dense_index(matrix, row, col)];
}

int rf_complex_matrix_set(RfComplexMatrix *matrix, size_t row, size_t col, double complex value)
{
    if (matrix == NULL || matrix->values == NULL || row >= matrix->rows || col >= matrix->cols ||
        !complex_finite(value)) {
        return 0;
    }
    matrix->values[dense_index(matrix, row, col)] = value;
    return 1;
}

int rf_complex_matrix_add(RfComplexMatrix *matrix, size_t row, size_t col, double complex value)
{
    if (matrix == NULL || matrix->values == NULL || row >= matrix->rows || col >= matrix->cols ||
        !complex_finite(value)) {
        return 0;
    }
    matrix->values[dense_index(matrix, row, col)] += value;
    return complex_finite(matrix->values[dense_index(matrix, row, col)]);
}

/* 带状矩阵按列压缩：A[row,col] ↦ col×(l+u+1)+u+row−col。带外读零、写拒绝。 */
static int band_offset(const RfComplexBandMatrix *matrix, size_t row, size_t col, size_t *index)
{
    size_t width;
    size_t diagonal;
    if (matrix == NULL || matrix->values == NULL || row >= matrix->n || col >= matrix->n ||
        row + matrix->upper < col || col + matrix->lower < row) {
        return 0;
    }
    width = matrix->lower + matrix->upper + 1U;
    diagonal = matrix->upper + row - col;
    if (diagonal >= width || !size_product_ok(col, width)) {
        return 0;
    }
    *index = col * width + diagonal;
    return 1;
}

/* 分配一般带状矩阵；上下带宽均包含在容量检查中。 */
int rf_complex_band_init(RfComplexBandMatrix *matrix, size_t n, size_t lower, size_t upper)
{
    size_t width;
    if (matrix == NULL || lower > SIZE_MAX - upper || lower + upper == SIZE_MAX ||
        (width = lower + upper + 1U) > SIZE_MAX / (n == 0U ? 1U : n) ||
        (n != 0U && width * n > SIZE_MAX / sizeof(*matrix->values))) {
        return 0;
    }
    matrix->n = n;
    matrix->lower = lower;
    matrix->upper = upper;
    matrix->values = NULL;
    if (n != 0U) {
        matrix->values = (double complex *)calloc(width * n, sizeof(*matrix->values));
        if (matrix->values == NULL) {
            matrix->n = matrix->lower = matrix->upper = 0U;
            return 0;
        }
    }
    return 1;
}

void rf_complex_band_free(RfComplexBandMatrix *matrix)
{
    if (matrix == NULL) {
        return;
    }
    free(matrix->values);
    matrix->values = NULL;
    matrix->n = matrix->lower = matrix->upper = 0U;
}

void rf_complex_band_zero(RfComplexBandMatrix *matrix)
{
    size_t width;
    if (matrix == NULL || matrix->values == NULL) {
        return;
    }
    width = matrix->lower + matrix->upper + 1U;
    (void)memset(matrix->values, 0, width * matrix->n * sizeof(*matrix->values));
}

double complex rf_complex_band_get(const RfComplexBandMatrix *matrix, size_t row, size_t col)
{
    size_t index;
    if (!band_offset(matrix, row, col, &index)) {
        return 0.0 + 0.0 * I;
    }
    return matrix->values[index];
}

int rf_complex_band_set(RfComplexBandMatrix *matrix, size_t row, size_t col, double complex value)
{
    size_t index;
    if (!band_offset(matrix, row, col, &index) || !complex_finite(value)) {
        return 0;
    }
    matrix->values[index] = value;
    return 1;
}

int rf_complex_band_add(RfComplexBandMatrix *matrix, size_t row, size_t col, double complex value)
{
    size_t index;
    if (!band_offset(matrix, row, col, &index) || !complex_finite(value)) {
        return 0;
    }
    matrix->values[index] += value;
    return complex_finite(matrix->values[index]);
}

/* 矩阵无穷范数为最大绝对行和，用于相对主元判据和归一化后向误差。 */
static double matrix_norm_inf(const RfComplexMatrix *matrix)
{
    size_t row, col;
    double norm = 0.0;
    if (matrix == NULL || matrix->rows != matrix->cols || matrix->values == NULL) {
        return NAN;
    }
    for (row = 0U; row < matrix->rows; ++row) {
        double sum = 0.0;
        for (col = 0U; col < matrix->cols; ++col) {
            double magnitude = cabs(matrix->values[dense_index(matrix, row, col)]);
            if (!isfinite(magnitude)) {
                return NAN;
            }
            sum += magnitude;
        }
        if (sum > norm) {
            norm = sum;
        }
    }
    return norm;
}

/* 以原始 A、b 重算 ||Ax−b||∞，不使用已消元矩阵；残差不是物理解误差。 */
double rf_complex_residual_inf(
    const RfComplexMatrix *matrix, const double complex *solution, const double complex *rhs)
{
    size_t row, col;
    double residual = 0.0;
    if (matrix == NULL || matrix->rows != matrix->cols || solution == NULL || rhs == NULL) {
        return NAN;
    }
    if (matrix->rows == 0U)
        return 0.0;
    if (matrix->values == NULL)
        return NAN;
    for (row = 0U; row < matrix->rows; ++row) {
        double complex value = 0.0 + 0.0 * I;
        for (col = 0U; col < matrix->cols; ++col) {
            value += matrix->values[dense_index(matrix, row, col)] * solution[col];
        }
        if (!complex_finite(value) || !complex_finite(rhs[row])) {
            return NAN;
        }
        value -= rhs[row];
        if (cabs(value) > residual) {
            residual = cabs(value);
        }
    }
    return residual;
}

/* 一般复数系统：复制 A、b，逐列部分选主元消元，再回代。
 * 奇异判据为 ε_machine×n×||A||∞；报告 η=||Ax−b||∞/(||A||∞||x||∞+||b||∞)。
 * 小后向误差只保证离散方程满足，不保证矩阵良态或物理模型正确。 */
RfLinearStatus rf_complex_solve(const RfComplexMatrix *matrix,
    const double complex *rhs,
    double complex *solution,
    double *backward_error)
{
    size_t n, i, j, k;
    double complex *work = NULL;
    double complex *vector = NULL;
    double norm_a;
    double norm_b = 0.0;
    double scale;
    if (backward_error != NULL) {
        *backward_error = NAN;
    }
    if (matrix == NULL || matrix->rows != matrix->cols || rhs == NULL || solution == NULL) {
        return RF_LINEAR_INVALID;
    }
    n = matrix->rows;
    /* rhs 与 solution 可以是同一缓冲区；残差必须用未被覆盖的原始 b。 */
    if (rhs == solution && n) {
        double complex *saved;
        if (n > SIZE_MAX / sizeof(*saved))
            return RF_LINEAR_OUT_OF_MEMORY;
        saved = malloc(n * sizeof(*saved));
        if (!saved)
            return RF_LINEAR_OUT_OF_MEMORY;
        memcpy(saved, rhs, n * sizeof(*saved));
        RfLinearStatus alias_status = rf_complex_solve(matrix, saved, solution, backward_error);
        free(saved);
        return alias_status;
    }
    if (n == 0U) {
        if (backward_error != NULL) {
            *backward_error = 0.0;
        }
        return RF_LINEAR_OK;
    }
    if (matrix->values == NULL)
        return RF_LINEAR_INVALID;
    if (!size_product_ok(n, n) || n * n > SIZE_MAX / sizeof(*work)) {
        return RF_LINEAR_OUT_OF_MEMORY;
    }
    work = (double complex *)malloc(n * n * sizeof(*work));
    vector = (double complex *)malloc(n * sizeof(*vector));
    if (work == NULL || vector == NULL) {
        free(work);
        free(vector);
        return RF_LINEAR_OUT_OF_MEMORY;
    }
    norm_a = matrix_norm_inf(matrix);
    if (!isfinite(norm_a)) {
        free(work);
        free(vector);
        return RF_LINEAR_NONFINITE;
    }
    (void)memcpy(work, matrix->values, n * n * sizeof(*work));
    for (i = 0U; i < n; ++i) {
        if (!complex_finite(rhs[i])) {
            free(work);
            free(vector);
            return RF_LINEAR_NONFINITE;
        }
        vector[i] = rhs[i];
        norm_b = fmax(norm_b, cabs(rhs[i]));
    }
    /* Partial pivoting is adequate for the modest radial systems used here. */
    for (k = 0U; k < n; ++k) {
        size_t pivot = k;
        double pivot_size = cabs(work[dense_index(matrix, k, k)]);
        for (i = k + 1U; i < n; ++i) {
            double candidate = cabs(work[i * n + k]);
            if (candidate > pivot_size) {
                pivot = i;
                pivot_size = candidate;
            }
        }
        if (!isfinite(pivot_size) ||
            pivot_size <= DBL_EPSILON * fmax(norm_a, DBL_MIN) * (double)n) {
            free(work);
            free(vector);
            return RF_LINEAR_SINGULAR;
        }
        if (pivot != k) {
            for (j = k; j < n; ++j) {
                double complex temporary = work[k * n + j];
                work[k * n + j] = work[pivot * n + j];
                work[pivot * n + j] = temporary;
            }
            {
                double complex temporary = vector[k];
                vector[k] = vector[pivot];
                vector[pivot] = temporary;
            }
        }
        for (i = k + 1U; i < n; ++i) {
            double complex factor = work[i * n + k] / work[k * n + k];
            work[i * n + k] = 0.0 + 0.0 * I;
            for (j = k + 1U; j < n; ++j) {
                work[i * n + j] -= factor * work[k * n + j];
            }
            vector[i] -= factor * vector[k];
        }
    }
    for (i = n; i-- > 0U;) {
        double complex value = vector[i];
        for (j = i + 1U; j < n; ++j) {
            value -= work[i * n + j] * solution[j];
        }
        solution[i] = value / work[i * n + i];
        if (!complex_finite(solution[i])) {
            free(work);
            free(vector);
            return RF_LINEAR_NONFINITE;
        }
    }
    if (backward_error != NULL) {
        double residual = rf_complex_residual_inf(matrix, solution, rhs);
        double solution_norm = 0.0;
        for (i = 0U; i < n; ++i) {
            solution_norm = fmax(solution_norm, cabs(solution[i]));
        }
        scale = norm_a * solution_norm + norm_b + DBL_MIN;
        *backward_error = residual / scale;
        if (!isfinite(*backward_error)) {
            free(work);
            free(vector);
            return RF_LINEAR_NONFINITE;
        }
    }
    free(work);
    free(vector);
    return RF_LINEAR_OK;
}

/* 一般复数带状消元，PDF §2.3.4。
 * 下带宽 l 的选主元最多使上带宽由 u 扩展为 u+l。每列只交换尚未
 * 消元的带内尾部，同时交换已前消元的右端，因此无需保存完整 L。
 * 时间 O(n*l*(u+l))，存储 O(n*(2l+u+1))；没有稠密回退或人工损耗。
 * 输入 A、b 始终保留，支持 rhs==solution，并用原系统独立重算后向误差。
 */
RfLinearStatus rf_complex_band_solve(const RfComplexBandMatrix *matrix,
    const double complex *rhs,
    double complex *solution,
    double *backward_error)
{
    RfComplexBandMatrix work = { 0 };
    double complex *vector = NULL;
    double norm_a = 0., norm_b = 0.;
    RfLinearStatus status = RF_LINEAR_OK;
    if (backward_error)
        *backward_error = NAN;
    if (!matrix || !rhs || !solution)
        return RF_LINEAR_INVALID;
    size_t n = matrix->n, l = matrix->lower, u = matrix->upper;
    if (rhs == solution && n) {
        if (n > SIZE_MAX / sizeof(double complex))
            return RF_LINEAR_OUT_OF_MEMORY;
        double complex *saved = malloc(n * sizeof(*saved));
        if (!saved)
            return RF_LINEAR_OUT_OF_MEMORY;
        memcpy(saved, rhs, n * sizeof(*saved));
        RfLinearStatus alias_status =
            rf_complex_band_solve(matrix, saved, solution, backward_error);
        free(saved);
        return alias_status;
    }
    if (!n) {
        if (backward_error)
            *backward_error = 0.;
        return RF_LINEAR_OK;
    }
    if (!matrix->values || l > SIZE_MAX - u)
        return RF_LINEAR_INVALID;
    if (!rf_complex_band_init(&work, n, l, u + l))
        return RF_LINEAR_OUT_OF_MEMORY;
    vector = malloc(n * sizeof(*vector));
    if (!vector) {
        rf_complex_band_free(&work);
        return RF_LINEAR_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < n; ++i) {
        double row_sum = 0.;
        if (!complex_finite(rhs[i])) {
            status = RF_LINEAR_NONFINITE;
            goto done;
        }
        vector[i] = rhs[i];
        norm_b = fmax(norm_b, cabs(rhs[i]));
        size_t first = i > l ? i - l : 0, last = fmin(n - 1, i + u);
        for (size_t j = first; j <= last; ++j) {
            double complex value = rf_complex_band_get(matrix, i, j);
            if (!complex_finite(value)) {
                status = RF_LINEAR_NONFINITE;
                goto done;
            }
            (void)rf_complex_band_set(&work, i, j, value);
            row_sum += cabs(value);
        }
        norm_a = fmax(norm_a, row_sum);
    }
    for (size_t k = 0; k < n; ++k) {
        size_t bottom = fmin(n - 1, k + l), right = fmin(n - 1, k + u + l), pivot = k;
        double magnitude = cabs(rf_complex_band_get(&work, k, k));
        for (size_t i = k + 1; i <= bottom; ++i) {
            double candidate = cabs(rf_complex_band_get(&work, i, k));
            if (candidate > magnitude) {
                magnitude = candidate;
                pivot = i;
            }
        }
        if (!isfinite(magnitude)) {
            status = RF_LINEAR_NONFINITE;
            goto done;
        }
        /* 不能用全矩阵范数乘 epsilon 截掉小但合法的 Maxwell 主元。
         * 奇异/病态还由原系统残差和连续谱极点检测分别判断。 */
        if (magnitude < 1e-280) {
            status = RF_LINEAR_SINGULAR;
            goto done;
        }
        if (pivot != k) {
            for (size_t j = k; j <= right; ++j) {
                double complex value = rf_complex_band_get(&work, k, j);
                (void)rf_complex_band_set(&work, k, j, rf_complex_band_get(&work, pivot, j));
                (void)rf_complex_band_set(&work, pivot, j, value);
            }
            double complex value = vector[k];
            vector[k] = vector[pivot];
            vector[pivot] = value;
        }
        double complex diagonal = rf_complex_band_get(&work, k, k);
        for (size_t i = k + 1; i <= bottom; ++i) {
            double complex factor = rf_complex_band_get(&work, i, k) / diagonal;
            (void)rf_complex_band_set(&work, i, k, 0.);
            for (size_t j = k + 1; j <= right; ++j) {
                double complex value =
                    rf_complex_band_get(&work, i, j) - factor * rf_complex_band_get(&work, k, j);
                if (!rf_complex_band_set(&work, i, j, value)) {
                    status = RF_LINEAR_NONFINITE;
                    goto done;
                }
            }
            vector[i] -= factor * vector[k];
        }
    }
    for (size_t i = n; i-- > 0;) {
        size_t right = fmin(n - 1, i + u + l);
        double complex value = vector[i];
        for (size_t j = i + 1; j <= right; ++j)
            value -= rf_complex_band_get(&work, i, j) * solution[j];
        solution[i] = value / rf_complex_band_get(&work, i, i);
        if (!complex_finite(solution[i])) {
            status = RF_LINEAR_NONFINITE;
            goto done;
        }
    }
    if (backward_error) {
        double residual = 0., norm_x = 0.;
        for (size_t i = 0; i < n; ++i)
            norm_x = fmax(norm_x, cabs(solution[i]));
        for (size_t i = 0; i < n; ++i) {
            /* vector 在前消元中被修改；rhs 可能与 solution 重叠，所以原始
             * 右端另从调用前备份中读取（见下方 alias 分支）。 */
            double complex value = -rhs[i];
            size_t first = i > l ? i - l : 0, last = fmin(n - 1, i + u);
            for (size_t j = first; j <= last; ++j)
                value += rf_complex_band_get(matrix, i, j) * solution[j];
            residual = fmax(residual, cabs(value));
        }
        *backward_error = residual / fmax(DBL_MIN, norm_a * norm_x + norm_b);
        if (!isfinite(*backward_error))
            status = RF_LINEAR_NONFINITE;
    }
done:
    free(vector);
    rf_complex_band_free(&work);
    return status;
}

/* 释放结果拥有的数组并清除 complete；调用前须初始化对象或使用 {0}。 */
static void vacuum_solution_reset(RfVacuumSolution *solution)
{
    if (solution == NULL) {
        return;
    }
    if (solution->magic == RF_VACUUM_SOLUTION_MAGIC) {
        free(solution->r);
        free(solution->electric);
        free(solution->total);
        free(solution->magnetic);
        if (solution->fem) {
            rf_maxwell_free(solution->fem);
            free(solution->fem);
        }
    }
    (void)memset(solution, 0, sizeof(*solution));
    solution->r = NULL;
    solution->electric = NULL;
    solution->total = NULL;
    solution->nr = 0U;
    solution->complete = 0;
    solution->backward_error = NAN;
    solution->message[0] = '\0';
    solution->magic = RF_VACUUM_SOLUTION_MAGIC;
}

void rf_vacuum_solution_init(RfVacuumSolution *solution)
{
    if (solution != NULL) {
        (void)memset(solution, 0, sizeof(*solution));
        solution->backward_error = NAN;
        solution->magic = RF_VACUUM_SOLUTION_MAGIC;
    }
}

void rf_vacuum_solution_free(RfVacuumSolution *solution)
{
    if (solution == NULL)
        return;
    vacuum_solution_reset(solution);
    solution->magic = RF_VACUUM_SOLUTION_MAGIC;
}

/* 输出仍按原界面的 nr=radial_cells+1 均匀采样；实际有限元自由度与
 * 界面对齐网格保存在 fem 中。数组采样绝不再被当成求解的离散未知量。 */
int rf_vacuum_solve(const Project *p, RfVacuumSolution *s, char *error, size_t n)
{
    if (!s)
        return fail_text(error, n, "vacuum destination is null");
    Project input_project = { 0 };
    int input_present = p != NULL;
    if (p)
        input_project = *p;
    vacuum_solution_reset(s);
    if (!input_present)
        return fail_text(error, n, "vacuum project is null");
    p = &input_project;
    if (!project_validate(p, error, n))
        return 0;
    s->fem = calloc(1, sizeof(*s->fem));
    if (!s->fem)
        return fail_text(error, n, "out of memory for vacuum Maxwell result");
    if (!rf_maxwell_solve(p, NULL, s->fem, error, n)) {
        vacuum_solution_reset(s);
        return 0;
    }
    s->nr = (size_t)p->radial_cells + 1;
    s->project = *p;
    s->r = malloc(s->nr * sizeof(*s->r));
    s->total = calloc(s->nr, sizeof(*s->total));
    s->electric = calloc(3 * s->nr, sizeof(*s->electric));
    s->magnetic = calloc(3 * s->nr, sizeof(*s->magnetic));
    if (!s->r || !s->total || !s->electric || !s->magnetic) {
        vacuum_solution_reset(s);
        return fail_text(error, n, "out of memory for vacuum field samples");
    }
    for (size_t i = 0; i < s->nr; ++i) {
        double complex e[3], b[3];
        s->r[i] = p->radius * i / (s->nr - 1);
        if (!rf_maxwell_evaluate(s->fem, s->r[i], 0, e, b)) {
            vacuum_solution_reset(s);
            return fail_text(error, n, "vacuum field recovery failed");
        }
        for (int k = 0; k < 3; ++k) {
            s->electric[k * s->nr + i] = e[k];
            s->magnetic[k * s->nr + i] = b[k];
            s->total[i] += pow(cabs(e[k]), 2);
        }
        s->total[i] = sqrt(s->total[i]);
    }
    memcpy(s->source, s->fem->source, sizeof(s->source));
    s->backward_error = s->fem->backward_error;
    s->source_power = s->fem->source_power;
    s->peak_electric = s->fem->peak_electric;
    s->complete = 1;
    snprintf(s->message,
        sizeof(s->message),
        "真空 Maxwell 矢量有限元 · m=%d · kz=%.6g · %zu 单元 · 残差 %.2g",
        p->selected_m,
        p->kz,
        s->fem->nodes - 1,
        s->backward_error);
    return 1;
}
/* 任意半径按原有限元基恢复，保留真实边界和源面两侧的场结构。 */
double complex rf_vacuum_field(const RfVacuumSolution *s, double r, int component)
{
    double complex e[3], b[3];
    if (!s || !s->complete || component < 0 || component > 2 ||
        !rf_maxwell_evaluate(s->fem, r, 0, e, b))
        return NAN + NAN * I;
    return e[component];
}

/* 导出完整复数谱场，%.17g 保留 double 往返精度；失败写入返回错误。 */
int rf_vacuum_export_csv(
    const RfVacuumSolution *solution, const char *path, char *error, size_t error_size)
{
    FILE *stream;
    size_t i;
    if (solution == NULL || !solution->complete || solution->r == NULL ||
        solution->electric == NULL || solution->total == NULL || path == NULL || *path == '\0') {
        return fail_text(error, error_size, "vacuum result or path is invalid");
    }
    stream = fopen(path, "w");
    if (stream == NULL) {
        if (error != NULL && error_size != 0U) {
            (void)snprintf(error, error_size, "cannot write '%s': %s", path, strerror(errno));
        }
        return 0;
    }
    if (fprintf(stream,
            "r_m,Er_re,Er_im,Ephi_re,Ephi_im,Ez_re,Ez_im,total_V,Br_re,Br_im,Bphi_re,Bphi_im,"
            "Bz_re,Bz_im,source_power_W_m,backward_error,R_ohm_m,X_ohm_m\n") < 0)
        goto write_error;
    for (i = 0; i < solution->nr; ++i) {
        double row[18] = { solution->r[i],
            creal(solution->electric[i]),
            cimag(solution->electric[i]),
            creal(solution->electric[solution->nr + i]),
            cimag(solution->electric[solution->nr + i]),
            creal(solution->electric[2 * solution->nr + i]),
            cimag(solution->electric[2 * solution->nr + i]),
            solution->total[i],
            creal(solution->magnetic[i]),
            cimag(solution->magnetic[i]),
            creal(solution->magnetic[solution->nr + i]),
            cimag(solution->magnetic[solution->nr + i]),
            creal(solution->magnetic[2 * solution->nr + i]),
            cimag(solution->magnetic[2 * solution->nr + i]),
            solution->source_power,
            solution->backward_error,
            creal(solution->fem->load_impedance),
            cimag(solution->fem->load_impedance) };
        for (size_t col = 0; col < 18; ++col)
            if (fprintf(stream, col ? ",%.17g" : "%.17g", row[col]) < 0)
                goto write_error;
        if (fputc('\n', stream) == EOF)
            goto write_error;
    }
    if (fclose(stream) != 0) {
        return fail_text(error, error_size, "cannot finalize vacuum CSV");
    }
    return 1;
write_error:
    (void)fclose(stream);
    (void)remove(path);
    return fail_text(error, error_size, "cannot write vacuum CSV");
}

/* 扫描对象独立拥有每个失败原因字符串；先释放子项，再释放各数组。 */
static void vacuum_scan_reset(RfVacuumScan *scan)
{
    size_t i;
    if (scan == NULL) {
        return;
    }
    if (scan->magic == RF_VACUUM_SCAN_MAGIC) {
        if (scan->reason != NULL) {
            for (i = 0U; i < scan->nk; ++i) {
                free(scan->reason[i]);
            }
        }
        free(scan->k);
        free(scan->valid);
        free(scan->total);
        free(scan->current);
        free(scan->electric);
        free(scan->backward_error);
        free(scan->reason);
    }
    (void)memset(scan, 0, sizeof(*scan));
    scan->k = NULL;
    scan->valid = NULL;
    scan->total = NULL;
    scan->current = NULL;
    scan->electric = NULL;
    scan->backward_error = NULL;
    scan->reason = NULL;
    scan->nk = scan->nr = 0U;
    scan->complete = 0;
    scan->magic = RF_VACUUM_SCAN_MAGIC;
}

void rf_vacuum_scan_init(RfVacuumScan *scan)
{
    if (scan != NULL) {
        (void)memset(scan, 0, sizeof(*scan));
        scan->magic = RF_VACUUM_SCAN_MAGIC;
    }
}

void rf_vacuum_scan_free(RfVacuumScan *scan)
{
    if (scan == NULL)
        return;
    vacuum_scan_reset(scan);
    scan->magic = RF_VACUUM_SCAN_MAGIC;
}

/* 均匀采样包含 kmin、kmax，逐波数独立求解。fields=0 只积分源，不进行求场。
 * complete 表示扫描已遍历，并不表示所有 valid 都为真；调用者必须逐点检查。 */
int rf_vacuum_scan(const Project *project,
    double kmin,
    double kmax,
    size_t nk,
    int fields,
    RfVacuumScan *scan,
    char *error,
    size_t error_size)
{
    size_t ik;
    size_t i;
    size_t nr;
    if (project == NULL || scan == NULL) {
        return fail_text(error, error_size, "vacuum scan project or destination is null");
    }
    if (!isfinite(kmin) || !isfinite(kmax) || !(kmin < kmax) || kmin < -2000.0 || kmax > 2000.0 ||
        nk < 3U || nk > 401U || (fields != 0 && fields != 1)) {
        return fail_text(
            error, error_size, "扫描需满足 -2000 ≤ k_min < k_max ≤ 2000，点数为 3–401 的整数");
    }
    if (!project_validate(project, error, error_size)) {
        return 0;
    }
    vacuum_scan_reset(scan);
    nr = (size_t)project->radial_cells + 1U;
    if (nk > SIZE_MAX / (3U * sizeof(*scan->current)) ||
        nk > SIZE_MAX / (nr * sizeof(*scan->total)) ||
        (fields && (nk > SIZE_MAX / (3U * nr * sizeof(*scan->electric))))) {
        return fail_text(error, error_size, "vacuum scan is too large");
    }
    scan->project = *project;
    scan->nk = nk;
    scan->nr = nr;
    scan->kmin = kmin;
    scan->kmax = kmax;
    scan->k = (double *)calloc(nk, sizeof(*scan->k));
    scan->valid = (int *)calloc(nk, sizeof(*scan->valid));
    scan->total = (double *)calloc(nk * nr, sizeof(*scan->total));
    scan->current = (double complex *)calloc(3U * nk, sizeof(*scan->current));
    scan->backward_error = (double *)calloc(nk, sizeof(*scan->backward_error));
    scan->reason = (char **)calloc(nk, sizeof(*scan->reason));
    scan->electric =
        fields ? (double complex *)calloc(3U * nk * nr, sizeof(*scan->electric)) : NULL;
    if (scan->k == NULL || scan->valid == NULL || scan->total == NULL || scan->current == NULL ||
        scan->backward_error == NULL || scan->reason == NULL ||
        (fields && scan->electric == NULL)) {
        vacuum_scan_reset(scan);
        return fail_text(error, error_size, "out of memory while allocating vacuum scan");
    }
    /* 未解或失败切片不能表现为真实零场；valid/reason 是每个切片的状态。 */
    for (i = 0; i < nk * nr; ++i)
        scan->total[i] = NAN;
    for (i = 0; i < 3U * nk; ++i)
        scan->current[i] = NAN + NAN * I;
    if (fields)
        for (i = 0; i < 3U * nk * nr; ++i)
            scan->electric[i] = NAN + NAN * I;
    for (i = 0; i < nk; ++i)
        scan->backward_error[i] = NAN;
    for (ik = 0U; ik < nk; ++ik) {
        Project probe = *project;
        RfVacuumSolution one = { 0 };
        double fraction = (double)ik / (double)(nk - 1U);
        char local_error[160] = { 0 };
        scan->k[ik] = kmin + (kmax - kmin) * fraction;
        probe.kz = scan->k[ik];
        int ok;
        if (fields) {
            ok = rf_vacuum_solve(&probe, &one, local_error, sizeof(local_error));
        } else {
            /* 只扫描源谱时无需解场；应与 GUI 的天线电流谱完全同源，
             * 也不能由于某个真空本征极点而丢失一个有效天线源。 */
            Geometry source_geometry = { 0 };
            ok = geometry_build(&probe, &source_geometry, local_error, sizeof(local_error)) &&
                 geometry_current_spectrum(
                     &probe, &source_geometry, one.source, local_error, sizeof(local_error));
            geometry_free(&source_geometry);
        }
        if (!ok) {
            scan->reason[ik] =
                (char *)malloc(strlen(local_error[0] != '\0' ? local_error : "真空求解失败") + 1U);
            if (scan->reason[ik] != NULL) {
                (void)strcpy(
                    scan->reason[ik], local_error[0] != '\0' ? local_error : "真空求解失败");
            }
            rf_vacuum_solution_free(&one);
            continue;
        }
        scan->valid[ik] = 1;
        scan->backward_error[ik] = fields ? one.backward_error : NAN;
        for (i = 0U; i < 3U; ++i) {
            scan->current[3U * ik + i] = one.source[i];
        }
        if (fields) {
            (void)memcpy(scan->total + ik * nr, one.total, nr * sizeof(*scan->total));
            for (i = 0U; i < 3U; ++i) {
                (void)memcpy(scan->electric + (3U * ik + i) * nr,
                    one.electric + i * nr,
                    nr * sizeof(*scan->electric));
            }
        }
        rf_vacuum_solution_free(&one);
    }
    scan->complete = 1;
    return 1;
}
