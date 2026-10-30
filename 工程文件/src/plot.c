#include "rf_platform.h"
#include "plot.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* 创建CLI静态曲线画布；仅写已验证的数值，文本标题由内部固定字符串提供。 */
static int begin_svg(FILE **out, const char *path, char *error, size_t n)
{
    FILE *f = rf_fopen_utf8(path, "w");
    if (!f) {
        if (error && n)
            snprintf(error, n, "cannot write '%s': %s", path, strerror(errno));
        return 0;
    }
    fputs("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 900 520\"><rect width=\"100%\" "
          "height=\"100%\" fill=\"white\"/><g stroke=\"#555\" fill=\"none\"><path d=\"M75 "
          "62V455H865\"/></g>\n",
        f);
    *out = f;
    return 1;
}
/* 补齐闭合标签并检查缓冲写入和fclose，写入失败不能报告导出成功。 */
static int end_svg(FILE *f, char *error, size_t n)
{
    if (fputs("</svg>\n", f) < 0 || ferror(f)) {
        fclose(f);
        if (error && n)
            snprintf(error, n, "cannot write SVG");
        return 0;
    }
    if (fclose(f) != 0) {
        if (error && n)
            snprintf(error, n, "cannot finalize SVG");
        return 0;
    }
    return 1;
}
/* 按SVG路径语法追加一个画布坐标，首点使用M，后续点使用L。 */
static void path_point(FILE *f, double x, double y, int first)
{
    fprintf(f, "%s%.3f %.3f", first ? "M" : " L", x, y);
}

/* 真空和等离子体共用坐标、幅值缩放和 SVG 曲线路径，仅标题与配色不同。 */
/* 将真实半径区间映射到画布，幅值按当前最大值缩放；图中单位仍是r[m]与E_hat[V]。
 * 全零场只改变显示刻度的分母，不改变导出的物理数据。 */
static int export_field_svg(const double *r,
    const double *total,
    size_t count,
    const char *title,
    const char *color,
    const char *path,
    char *error,
    size_t n)
{
    double maximum = 0.;
    for (size_t i = 0; i < count; ++i)
        if (isfinite(total[i]))
            maximum = fmax(maximum, total[i]);
    if (maximum <= 0.)
        maximum = 1.;
    FILE *stream;
    if (!begin_svg(&stream, path, error, n))
        return 0;
    fprintf(stream,
        "<text x=\"75\" y=\"22\" font-family=\"sans-serif\" font-size=\"18\">%s</text>"
        "<text x=\"75\" y=\"48\" font-family=\"sans-serif\" font-size=\"14\">|E_hat| "
        "[V]</text><path "
        "stroke=\"%s\" fill=\"none\" stroke-width=\"2\" d=\"",
        title,
        color);
    for (size_t i = 0; i < count; ++i)
        path_point(stream,
            75 + 790 * (r[i] - r[0]) / (r[count - 1] - r[0]),
            455 - 393 * total[i] / maximum,
            i == 0);
    fputs("\"/>\n", stream);
    for (int i = 0; i <= 4; i++) {
        double x = 75 + 790 * i / 4., y = 455 - 393 * i / 4.;
        fprintf(stream,
            "<text x=\"%.3f\" y=\"477\" text-anchor=\"middle\" font-size=\"12\">%.4g</text>"
            "<text x=\"65\" y=\"%.3f\" text-anchor=\"end\" font-size=\"12\">%.4g</text>\n",
            x,
            r[0] + (r[count - 1] - r[0]) * i / 4.,
            y + 4,
            maximum * i / 4.);
    }
    fputs("<text x=\"470\" y=\"505\" text-anchor=\"middle\">r [m]</text>\n", stream);
    return end_svg(stream, error, n);
}

/* 只接受完整真空结果与有效路径，复用统一坐标和单位绘制幅值曲线。 */
int rf_vacuum_export_svg(const RfVacuumSolution *s, const char *path, char *error, size_t n)
{
    if (!s || !s->complete || !s->r || !s->total || s->nr < 2 || !path || !*path) {
        if (error && n)
            snprintf(error, n, "vacuum result or SVG path is invalid");
        return 0;
    }
    return export_field_svg(s->r, s->total, s->nr, "真空径向电场谱", "#2367a8", path, error, n);
}

/* 只接受完整等离子体结果，保持与真空图相同的谱幅值单位和轴向约定。 */
int rf_plasma_export_svg(const RfPlasmaSolution *s, const char *path, char *error, size_t n)
{
    if (!s || !s->complete || !s->r || !s->total || s->nr < 2 || !path || !*path) {
        if (error && n)
            snprintf(error, n, "plasma result or SVG path is invalid");
        return 0;
    }
    return export_field_svg(
        s->r, s->total, s->nr, "冷等离子体径向电场谱", "#c4513a", path, error, n);
}
