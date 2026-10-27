/* 图表直接使用 Maxwell 有限元的 E/B 样本；磁场已由基函数解析旋度求得。
 * 不跨材料界面或源面对节点场差分。下面只计算峰值相量的能流与显示量。 */
/* 峰值相量周期平均 Poynting 谱：Sz=Re(Er Bφ*−Eφ Br*)/(2μ0)，保留正负。 */
static double axial_flux_density(
    const double complex *e, const double complex *b, size_t n, size_t i)
{
    return 0.5 * creal(e[i] * conj(b[n + i]) - e[n + i] * conj(b[i])) / RF_MU0;
}
/* 横截面 2π∫r Sz dr，复合梯形积分；固定 (m,kz) 的谱单位为 W m²。 */
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
static void add_chart(Tcl_Interp *ip, Tcl_Obj *charts, const char *family, Tcl_Obj *c)
{
    str(ip, c, "family", family);
    Tcl_ListObjAppendElement(ip, charts, c);
}
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
        ip, charts, "field", field_chart(ip, "电场幅值", "电场谱幅值 (V)", r, e, total, n, 0));
    c = field_chart(ip, "磁场幅值", "磁场谱幅值 (T m)", r, b, NULL, n, 0);
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
static void add_scan_slice(Tcl_Interp *ip,
    Tcl_Obj *result,
    const double *r,
    const double complex *e,
    const double complex *b,
    const double *total,
    const double *absorption,
    size_t n)
{
    size_t i, j, count = n < 129 ? n : 129;
    Tcl_Obj *heat = Tcl_NewDictObj(), *rr = Tcl_NewListObj(0, NULL), *v[6];
    for (j = 0; j < 6; ++j)
        v[j] = Tcl_NewListObj(0, NULL);
    for (i = 0; i < count; ++i) {
        size_t q = i * (n - 1) / (count - 1);
        double bm = 0.;
        Tcl_ListObjAppendElement(ip, rr, Tcl_NewDoubleObj(r[q]));
        Tcl_ListObjAppendElement(ip, v[0], Tcl_NewDoubleObj(total[q]));
        for (j = 0; j < 3; ++j) {
            Tcl_ListObjAppendElement(ip, v[j + 1], Tcl_NewDoubleObj(cabs(e[j * n + q])));
            bm += pow(cabs(b[j * n + q]), 2);
        }
        Tcl_ListObjAppendElement(ip, v[4], Tcl_NewDoubleObj(sqrt(bm)));
        Tcl_ListObjAppendElement(ip, v[5], Tcl_NewDoubleObj(absorption ? absorption[q] : 0.));
    }
    for (j = 0; j < 6; ++j)
        put(ip, heat, (const char *[]){ "total", "Er", "Ephi", "Ez", "B", "absorption" }[j], v[j]);
    put(ip, result, "heat", heat);
    put(ip, result, "heat_r", rr);
}
/* 材料剖面直接取 rf_plasma_material，不通过求场器反推，边界约定与核心一致。 */
static int add_material_charts(Tcl_Interp *ip,
    Tcl_Obj *charts,
    Tcl_Obj *csv,
    const Project *p,
    const Plasma *plasma,
    char *error,
    size_t error_size)
{
    size_t i, j, n = (size_t)p->radial_cells + 1;
    double h = p->radius / p->radial_cells;
    Tcl_Obj *r = Tcl_NewListObj(0, NULL), *v[10], *lines;
    Tcl_IncrRefCount(r);
    for (j = 0; j < 10; ++j) {
        v[j] = Tcl_NewListObj(0, NULL);
        Tcl_IncrRefCount(v[j]);
    }
    Tcl_AppendToObj(csv, "r_m,ne_m3,ni_m3,nu_e_s1,nu_i_s1,S_re,S_im,D_re,D_im,P_re,P_im\n", -1);
    for (i = 0; i < n; ++i) {
        RfPlasmaMaterial m;
        double x = i * h, row[11];
        if (!rf_plasma_material(p, plasma, x, &m, error, error_size)) {
            Tcl_DecrRefCount(r);
            for (j = 0; j < 10; ++j)
                Tcl_DecrRefCount(v[j]);
            return 0;
        }
        row[0] = x;
        row[1] = m.ne;
        row[2] = m.ni;
        row[3] = m.nu_e;
        row[4] = m.nu_i;
        row[5] = creal(m.S);
        row[6] = cimag(m.S);
        row[7] = creal(m.D);
        row[8] = cimag(m.D);
        row[9] = creal(m.P);
        row[10] = cimag(m.P);
        Tcl_ListObjAppendElement(ip, r, Tcl_NewDoubleObj(x));
        for (j = 0; j < 10; ++j)
            Tcl_ListObjAppendElement(ip, v[j], Tcl_NewDoubleObj(row[j + 1]));
        csv_row(csv, row, 11);
    }
    for (i = 0; i < 4; ++i) {
        lines = Tcl_NewListObj(0, NULL);
        if (i < 2) {
            for (j = 0; j < 2; ++j)
                Tcl_ListObjAppendElement(ip,
                    lines,
                    series(ip,
                        i == 0 ? (j == 0 ? "电子" : "离子") : (j == 0 ? "νe" : "νi"),
                        j == 0 ? "#2563eb" : "#d97706",
                        v[2 * i + j]));
        } else {
            for (j = 0; j < 3; ++j)
                Tcl_ListObjAppendElement(ip,
                    lines,
                    series(ip,
                        (const char *[]){ "S", "D", "P" }[j],
                        (const char *[]){ "#2563eb", "#d97706", "#059669" }[j],
                        v[4 + 2 * j + (i == 3)]));
        }
        add_chart(ip,
            charts,
            "material",
            chart(ip,
                (const char *[]){
                    "粒子密度剖面", "碰撞频率剖面", "介电张量实部", "介电张量虚部" }[i],
                "半径 r (m)",
                (const char *[]){ "粒子密度 (m⁻³)", "碰撞频率 (s⁻¹)", "Re(εr)", "Im(εr)" }[i],
                r,
                lines));
    }
    Tcl_DecrRefCount(r);
    for (j = 0; j < 10; ++j)
        Tcl_DecrRefCount(v[j]);
    return 1;
}
