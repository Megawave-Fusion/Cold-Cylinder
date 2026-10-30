/* 图表直接使用 Maxwell 有限元的 E/B 样本；磁场已由基函数解析旋度求得。
 * 不跨材料界面或源面对节点场差分。下面只计算峰值相量的能流与显示量。 */
/* 峰值相量周期平均 Poynting 谱：Sz=Re(Er Bφ*−Eφ Br*)/(2μ0)，保留正负。 */
/* 固定 (m,kz) 的谱场叉积取周期平均，保留正负方向；结果单位 W 而非 W/m²。 */
static double axial_flux_density(
    const double complex *e, const double complex *b, size_t n, size_t i)
{
    return 0.5 * creal(e[i] * conj(b[n + i]) - e[n + i] * conj(b[i])) / RF_MU0;
}
/* 横截面 2π∫r Sz dr，复合梯形积分；固定 (m,kz) 的谱单位为 W m²。 */
/* 只对径向谱能流作 2πr dr 截面积分，结果为 W m²；尚未作 kz 相干重建。 */
static double axial_flux(
    const double *r, const double complex *e, const double complex *b, size_t n)
{
    size_t i;
    double value = 0.;
    for (i = 1; i < n; ++i)
        value +=
            3.14159265358979323846 * (r[i] - r[i - 1]) *
            (r[i] * axial_flux_density(e, b, n, i) + r[i - 1] * axial_flux_density(e, b, n, i - 1));
    return value;
}
/* family 只标记图示来源，不改变场归一化或数值内容。 */
static void add_chart(Tcl_Interp *ip, Tcl_Obj *charts, const char *family, Tcl_Obj *c)
{
    str(ip, c, "family", family);
    Tcl_ListObjAppendElement(ip, charts, c);
}
/* 直接使用解析旋度得到的磁场，避免跨材料界面对电场显示样本求差分。 */
static void add_field_charts(Tcl_Interp *ip,
    Tcl_Obj *charts,
    const double *r,
    const double complex *e,
    const double *total,
    const double complex *b,
    size_t n)
{
    size_t i;
    Tcl_Obj *lines, *values[3], *c;
    add_chart(
        ip, charts, "field", field_chart(ip, "电场幅值", "电场谱幅值 (V)", r, e, total, n));
    c = field_chart(ip, "磁场幅值", "磁场谱幅值 (T m)", r, b, NULL, n);
    str(ip, c, "note", "磁场由 Maxwell 有限元基的解析旋度按 B=∇×E/(iω) 重建，界面取内侧迹。");
    add_chart(ip, charts, "field", c);
    values[0] = Tcl_NewListObj(0, NULL);
    for (i = 0; i < n; ++i)
        Tcl_ListObjAppendElement(ip, values[0], Tcl_NewDoubleObj(axial_flux_density(e, b, n, i)));
    lines = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(ip, lines, series(ip, "轴向能流", "#7c3aed", values[0]));
    c = chart(ip, "轴向能流密度", "半径 r (m)", "模态谱场能流量 (W)", numbers(r, n), lines);
    str(ip, c, "note", "由固定 (m,kz) 的谱场按 ½Re(E_hat×B_hat*)/μ0 构造，单位 W，保留方向符号；它不是空间能流密度 W/m²。再作 2π∫r dr 截面积分得到 W m² 的谱场量。");
    add_chart(ip, charts, "field", c);
}
/* Scan heatmaps retain up to 129 radial nodes per slice; reported peaks, flux,
 * absorption and native radial CSVs still use the complete solver grid. */
/* 显示热图最多抽样 129 个半径点；峰值、功率与完整 CSV 不使用这个降采样网格。 */
static void add_scan_slice(Tcl_Interp *ip,
    Tcl_Obj *result,
    const double *r,
    const double complex *b,
    const double *total,
    const double *absorption,
    size_t n)
{
    size_t i, j, count = n < 129 ? n : 129;
    Tcl_Obj *heat = Tcl_NewDictObj(), *rr = Tcl_NewListObj(0, NULL), *v[3];
    for (j = 0; j < 3; ++j)
        v[j] = Tcl_NewListObj(0, NULL);
    for (i = 0; i < count; ++i) {
        /* 正常求解至少有 65 个节点；辅助接口仍定义单节点退化输入，避免除零。 */
        size_t q = count > 1 ? i * (n - 1) / (count - 1) : 0;
        double bm = 0.;
        Tcl_ListObjAppendElement(ip, rr, Tcl_NewDoubleObj(r[q]));
        Tcl_ListObjAppendElement(ip, v[0], Tcl_NewDoubleObj(total[q]));
        for (j = 0; j < 3; ++j) {
            bm += pow(cabs(b[j * n + q]), 2);
        }
        Tcl_ListObjAppendElement(ip, v[1], Tcl_NewDoubleObj(sqrt(bm)));
        Tcl_ListObjAppendElement(ip, v[2], Tcl_NewDoubleObj(absorption ? absorption[q] : 0.));
    }
    for (j = 0; j < 3; ++j)
        put(ip, heat, (const char *[]){ "total", "B", "absorption" }[j], v[j]);
    put(ip, result, "heat", heat);
    put(ip, result, "heat_r", rr);
}
