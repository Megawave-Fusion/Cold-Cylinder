#ifndef RF_QUADRATURE_H
#define RF_QUADRATURE_H

/* Gauss–Legendre 求积，节点/权重位于 [-1,1]。只依赖 C11 libm。
 * 积分次序可以独立于径向网格加密；不能靠把网格加密当成求积收敛检查。 */
#include <math.h>
#include <stddef.h>
/* 构造 1..64 点 Gauss 规则。x、w 分别至少有 n 项；只利用根的对称性
 * 求一半节点，Newton 迭代失败就返回 0，绝不留下可冒充有效规则的成功标志。 */
static inline int rf_gauss_legendre(size_t n, double *x, double *w)
{
    const double pi = 3.141592653589793238462643383279502884;
    if (!n || n > 64 || !x || !w)
        return 0;
    for (size_t j = 0; j < (n + 1) / 2; ++j) {
        /* Legendre 根的渐近余弦初值；所有角度为弧度。 */
        double z = cos(pi * ((double)j + .75) / ((double)n + .5)), derivative = 0.;
        int converged = 0;
        for (int iteration = 0; iteration < 64; ++iteration) {
            /* 三项递推同时保留 P_n 与 P_(n-1)，再用解析导数求 Newton 步长。 */
            double p = 1., previous = 0.;
            for (size_t k = 1; k <= n; ++k) {
                double next = ((2. * k - 1.) * z * p - (k - 1.) * previous) / k;
                previous = p;
                p = next;
            }
            derivative = n * (z * p - previous) / (z * z - 1.);
            double step = p / derivative;
            z -= step;
            if (fabs(step) < 4e-16) {
                converged = 1;
                break;
            }
        }
        if (!converged)
            return 0;
        /* 在更新后的根重新计算导数，使对称节点具有一致的权重。 */
        double p = 1., previous = 0.;
        for (size_t k = 1; k <= n; ++k) {
            double next = ((2. * k - 1.) * z * p - (k - 1.) * previous) / k;
            previous = p;
            p = next;
        }
        derivative = n * (z * p - previous) / (z * z - 1.);
        x[j] = -z;
        x[n - 1 - j] = z;
        w[j] = w[n - 1 - j] = 2. / ((1. - z * z) * derivative * derivative);
    }
    return 1;
}
#endif
