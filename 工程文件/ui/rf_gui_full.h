/* Native incremental owner: one modal spatial solution is retained at a time. */
typedef struct {
    RfFullSolution solution;
    Tcl_Obj *snapshot;
} FullJob;
static FullJob full_job;
/* 释放当前多模式任务与持有的快照；已经复制到 Tcl 的完整结果继续有效。 */
static void full_delete(ClientData data)
{
    FullJob *job = data;
    rf_full_free(&job->solution);
    if (job->snapshot)
        Tcl_DecrRefCount(job->snapshot);
    job->snapshot = NULL;
}
/* field=0/1/3 对应 E/B/J 幅值，2/4/5/6 为能流/吸收/壳层沉积/波阻抗；component=0 表示总模。 */
static double full_value(const RfFullSolution *s, size_t j, int field, int component)
{
    if (field == 2)
        return s->axial_flux[j];
    if (field == 4)
        return s->absorption[j];
    if (field == 5)
        return s->shell[j];
    if (field == 6) {
        /* Apply the norm ratio only after coherent m/k reconstruction.
         * A magnetic node is undefined; do not floor or clip the denominator. */
        const double complex *E = s->electric + 3 * j, *B = s->magnetic + 3 * j;
        double en = hypot(hypot(cabs(E[0]), cabs(E[1])), cabs(E[2]));
        double bn = hypot(hypot(cabs(B[0]), cabs(B[1])), cabs(B[2]));
        double zw = bn > 0. ? RF_MU0 * en / bn : NAN;
        return isfinite(zw) ? zw : NAN;
    }
    const double complex *v = (field == 0      ? s->electric
                                  : field == 1 ? s->magnetic
                                               : s->current) +
                              3 * j;
    if (component)
        return cabs(v[component - 1]);
    return hypot(hypot(cabs(v[0]), cabs(v[1])), cabs(v[2]));
}
/* 按 z-major 顺序输出 r-z 样本；不可定义的阻抗节点留空，不能填零冒充零阻抗。 */
static Tcl_Obj *full_matrix(Tcl_Interp *ip, const RfFullSolution *s, int field, int component)
{
    Tcl_Obj *matrix = Tcl_NewListObj(0, NULL);
    for (size_t z = 0; z < s->config.space.nz; z++) {
        Tcl_Obj *row = Tcl_NewListObj(0, NULL);
        for (size_t r = 0; r < s->config.space.nr; r++) {
            double value = full_value(s, z * s->config.space.nr + r, field, component);
            Tcl_ListObjAppendElement(ip,
                row,
                isfinite(value) ? Tcl_NewDoubleObj(value) : Tcl_NewStringObj("", 0));
        }
        Tcl_ListObjAppendElement(ip, matrix, row);
    }
    return matrix;
}
/* 总场使用空间单位；局部 q 保留模式干涉，壳层沉积先对整圆周积分，二者含义不同。 */
static Tcl_Obj *full_result(Tcl_Interp *ip, const RfFullSolution *s, Tcl_Obj *snapshot)
{
    Tcl_Obj *result = Tcl_NewDictObj(), *charts = Tcl_NewListObj(0, NULL);
    Tcl_Obj *csv = Tcl_NewStringObj(
        "z_m,r_m,phi_rad,Er_re_V_m,Er_im_V_m,Ephi_re_V_m,Ephi_im_V_m,Ez_re_V_m,Ez_im_V_m,"
        "Br_re_T,Br_im_T,Bphi_re_T,Bphi_im_T,Bz_re_T,Bz_im_T,Jr_re_A_m2,Jr_im_A_m2,"
        "Jphi_re_A_m2,Jphi_im_A_m2,Jz_re_A_m2,Jz_im_A_m2,E_total_V_m,B_total_T,J_total_A_m2,"
        "Sz_W_m2,q_W_m3,shell_d2P_dr_dz_W_m2,m_min,m_max,kmin_rad_m,kmax_rad_m\n",
        -1);
    Tcl_Obj *impedance_csv = Tcl_NewStringObj(
        "z_m,r_m,phi_rad,Z_wave_ohm,valid,m_min,m_max,kmin_rad_m,kmax_rad_m\n", -1);
    size_t nr = s->config.space.nr, nz = s->config.space.nz;
    for (size_t z = 0; z < nz; z++)
        for (size_t r = 0; r < nr; r++) {
            size_t j = z * nr + r;
            double row[31] = { s->z[z], s->r[r], s->config.phi };
            const double complex *v[] = {
                s->electric + 3 * j, s->magnetic + 3 * j, s->current + 3 * j
            };
            for (int group = 0; group < 3; group++)
                for (int c = 0; c < 3; c++) {
                    row[3 + 6 * group + 2 * c] = creal(v[group][c]);
                    row[4 + 6 * group + 2 * c] = cimag(v[group][c]);
                }
            row[21] = full_value(s, j, 0, 0);
            row[22] = full_value(s, j, 1, 0);
            row[23] = full_value(s, j, 3, 0);
            row[24] = s->axial_flux[j];
            row[25] = s->absorption[j];
            row[26] = s->shell[j];
            row[27] = s->config.m_min;
            row[28] = s->config.m_max;
            row[29] = s->config.space.kmin;
            row[30] = s->config.space.kmax;
            csv_row(csv, row, 31);
            double zw = full_value(s, j, 6, 0);
            Tcl_Obj *record = isfinite(zw)
                ? Tcl_ObjPrintf("%.17g,%.17g,%.17g,%.17g,1,%d,%d,%.17g,%.17g\n",
                      s->z[z], s->r[r], s->config.phi, zw, s->config.m_min, s->config.m_max,
                      s->config.space.kmin, s->config.space.kmax)
                : Tcl_ObjPrintf("%.17g,%.17g,%.17g,,0,%d,%d,%.17g,%.17g\n",
                      s->z[z], s->r[r], s->config.phi, s->config.m_min, s->config.m_max,
                      s->config.space.kmin, s->config.space.kmax);
            Tcl_AppendObjToObj(impedance_csv, record);
            Tcl_DecrRefCount(record);
        }
    static const char *titles[] = {
        "电场幅值", "磁场幅值", "轴向能流密度", "电流幅值", "局部吸收", "径向壳层沉积", "波阻抗幅值"
    };
    static const char *units[] = { "V/m", "T", "W/m²", "A/m²", "W/m³", "W/m²", "Ω" };
    for (int field = 0; field < 7; field++) {
        Tcl_Obj *view = Tcl_NewDictObj();
        str(ip, view, "title", titles[field]);
        str(ip, view, "kind", "heatmap");
        str(ip, view, "unit", units[field]);
        str(ip, view, "xlabel", "轴向位置 z (m)");
        str(ip, view, "xkey", "z_m");
        str(ip, view, "xsymbol", "z");
        str(ip, view, "xunit", "m");
        put(ip, view, "x", numbers(s->z, nz));
        put(ip, view, "r", numbers(s->r, nr));
        put(ip, view, "csv", field == 6 ? impedance_csv : csv);
        Tcl_Obj *matrix = full_matrix(ip, s, field, 0);
        put(ip, view, "matrix", matrix);
        if (field == 0 || field == 1 || field == 3) {
            Tcl_Obj *parts = Tcl_NewDictObj();
            put(ip, parts, "总幅值", matrix);
            static const char *labels[] = { "r 分量", "φ 分量", "z 分量" };
            for (int c = 1; c <= 3; c++)
                put(ip, parts, labels[c - 1], full_matrix(ip, s, field, c));
            put(ip, view, "components", parts);
            str(ip,
                view,
                "note",
                "复数场先对 k_z 积分、对 m "
                "求和，再取幅值；可切换总幅值或单个分量的幅值。保留全部模式间干涉，使用物理空间单位"
                "。");
        } else if (field == 2) {
            put(ip, view, "signed", Tcl_NewBooleanObj(1));
            str(ip,
                view,
                "note",
                "S_z=½Re(E_r B_φ*−E_φ B_r*)/μ₀，由相干重建后的总场计算；正值沿 +z，负值沿 "
                "−z，保留模式间干涉。");
        } else if (field == 4) {
            str(ip,
                view,
                "note",
                "所选 φ 截面的 q=½Re(J_pl·E*)，单位 W/m³；由总复场计算并保留不同 m、k_z "
                "的交叉项。真空区为零。");
        } else if (field == 5) {
            str(ip,
                view,
                "note",
                "圆柱壳层沉积密度 d²P/(dr dz)=r∫₀²πq(r,φ,z)dφ=2πrΣ_m q_m(r,z)，单位 W/m²。每个 q_m "
                "保留 k_z 干涉；圆周积分消去不同 m 的交叉项。此图与所选 φ 无关，再沿 z、r "
                "积分才得到窗口功率 W。");
        } else {
            str(ip,
                view,
                "note",
                "所选 φ 截面的波阻抗幅值 Z_w=μ₀‖E_total‖/‖B_total‖（Ω）。先对 m、k_z "
                "相干叠加复电场和复磁场，再计算范数比，保留模式间干涉。磁场为零或比值不可表示处留空；"
                "不含阻抗相位，也不是天线端口负载阻抗。");
        }
        Tcl_ListObjAppendElement(ip, charts, view);
    }
    put(ip, result, "charts", charts);
    put(ip, result, "csv", csv);
    put(ip, result, "snapshot", snapshot);
    put(ip, result, "complete", Tcl_NewBooleanObj(1));
    num(ip, result, "count", nr * nz);
    num(ip, result, "window_power", s->window_power);
    num(ip, result, "band_power", s->spectral_power);
    num(ip, result, "power_balance_error", s->power_balance_error);
    num(ip, result, "radial_check_error", s->radial_check_error);
    char note[1600];
    snprintf(note,
        sizeof(note),
        "全模式重建 · m=[%d,%d]（有限求和），k_z=[%g,%g] rad/m（%zu 个中点），φ=%.6g rad。\n"
        "场、局部吸收与波阻抗是当前 φ 截面；壳层图作完整圆周积分。全圆周窗口吸收 %.6g W，带内谱积分 %.6g "
        "W。\n"
        "各 m 最差指标：供功平衡 %.2g，径向求积 %.2g，谱边缘吸收/阻抗占比 %.3g/%.3g，窗口端部/峰值 "
        "%.3g。\n"
        "沿 z 无界，观察窗口不是端板；请扩大 m、k_z 范围并加密采样检查收敛。",
        s->config.m_min,
        s->config.m_max,
        s->config.space.kmin,
        s->config.space.kmax,
        s->config.space.nk,
        s->config.phi,
        s->window_power,
        s->spectral_power,
        s->power_balance_error,
        s->radial_check_error,
        s->band_edge_fraction,
        s->impedance_edge_fraction,
        s->edge_ratio);
    str(ip, result, "note", note);
    return result;
}
/* 原生多 m 任务的事务边界：先冻结参数，逐步推进，完成后转成独立 Tcl 结果再释放 C 解。 */
static int full_command(ClientData data, Tcl_Interp *ip, int argc, Tcl_Obj *const argv[])
{
    FullJob *job = data;
    if (argc != 2) {
        Tcl_WrongNumArgs(ip, 1, argv, "begin|step|cancel");
        return TCL_ERROR;
    }
    const char *mode = Tcl_GetString(argv[1]);
    if (!strcmp(mode, "cancel")) {
        full_delete(data);
        return TCL_OK;
    }
    char error[512] = { 0 };
    if (!strcmp(mode, "begin")) {
        full_delete(data);
        if (!rf_state.plasma_enabled)
            return failure(ip, "请选择等离子体栏目后计算。");
        RfFullConfig config = rf_full_config(&rf_state);
        if (!rf_full_begin(
                &rf_state.project, &rf_state.plasma, &config, &job->solution, error, sizeof(error)))
            return failure(ip, error);
        job->snapshot = state_dict(ip);
        Tcl_IncrRefCount(job->snapshot);
        return TCL_OK;
    }
    if (strcmp(mode, "step"))
        return failure(ip, "未知全模式求解操作。");
    if (!rf_full_step(&job->solution, error, sizeof(error))) {
        full_delete(data);
        return failure(ip, error);
    }
    if (job->solution.complete) {
        Tcl_SetObjResult(ip, full_result(ip, &job->solution, job->snapshot));
        full_delete(data);
    } else {
        Tcl_Obj *result = Tcl_NewDictObj();
        put(ip, result, "complete", Tcl_NewBooleanObj(0));
        num(ip, result, "progress", job->solution.progress * 100);
        Tcl_SetObjResult(ip, result);
    }
    return TCL_OK;
}
