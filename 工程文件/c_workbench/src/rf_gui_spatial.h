/* Incremental job ownership belongs to the Tcl command, including cancellation. */
typedef struct {
    RfSpatialSolution solution;
    Tcl_Obj *snapshot;
} SpatialJob;
static SpatialJob spatial_job;
static void spatial_delete(ClientData data)
{
    SpatialJob *job = data;
    rf_spatial_free(&job->solution);
    if (job->snapshot)
        Tcl_DecrRefCount(job->snapshot);
    job->snapshot = NULL;
}
static Tcl_Obj *potential_result(Tcl_Interp *ip, const RfSpatialSolution *s, Tcl_Obj *snapshot)
{
    Tcl_Obj *result = Tcl_NewDictObj(), *charts = Tcl_NewListObj(0, NULL);
    Tcl_Obj *wave = Tcl_NewDictObj(), *matrix = Tcl_NewListObj(0, NULL);
    Tcl_Obj *csv = Tcl_NewStringObj(
        "z_m,r_m,Phi_Coulomb_re_V,Phi_Coulomb_im_V,selected_m,potential_backward_error\n", -1);
    size_t nr = s->config.nr, nz = s->config.nz;
    for (size_t z = 0; z < nz; ++z) {
        Tcl_Obj *row = Tcl_NewListObj(0, NULL);
        for (size_t r = 0; r < nr; ++r) {
            double complex phi = s->potential[z * nr + r];
            double values[] = { s->z[z], s->r[r], creal(phi), cimag(phi),
                s->project.selected_m, s->potential_backward_error };
            csv_row(csv, values, 6);
            Tcl_ListObjAppendElement(ip, row, Tcl_NewDoubleObj(cimag(phi)));
        }
        Tcl_ListObjAppendElement(ip, matrix, row);
    }
    str(ip, wave, "kind", "waterfall");
    str(ip, wave, "title", "电势虚部波形堆叠");
    str(ip, wave, "quantity", "Im Φ");
    str(ip, wave, "unit", "V");
    str(ip, wave, "note", "固定 φ=0，显示 Im Φ（V）。库仑规范：∇²Φ=−∇·E，外壁 Φ(R)=0、轴心正则。横轴 r，纵轴为 z 基线；所有曲线共用增益。起伏倍率只改变显示；CSV 保留原始复电势。");
    put(ip, wave, "r", numbers(s->r, nr));
    put(ip, wave, "z", numbers(s->z, nz));
    put(ip, wave, "matrix", matrix);
    put(ip, wave, "csv", csv);
    Tcl_ListObjAppendElement(ip, charts, wave);
    put(ip, result, "charts", charts);
    put(ip, result, "csv", csv);
    put(ip, result, "snapshot", snapshot);
    put(ip, result, "complete", Tcl_NewBooleanObj(1));
    num(ip, result, "count", nr * nz);
    num(ip, result, "potential_backward_error", s->potential_backward_error);
    str(ip, result, "note", "单 m、k_z 带电势相干重建；全部切片直接由原始模态电势计算。");
    return result;
}
static Tcl_Obj *spatial_line(Tcl_Interp *ip,
    const char *title,
    const char *xlabel,
    const char *unit,
    const double *x,
    const double *y,
    size_t count)
{
    Tcl_Obj *lines = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(ip, lines, series(ip, title, "#2563eb", numbers(y, count)));
    return chart(ip, title, xlabel, unit, numbers(x, count), lines);
}
static Tcl_Obj *spatial_result(Tcl_Interp *ip, const RfSpatialSolution *s, Tcl_Obj *snapshot)
{
    if (s->potential_only)
        return potential_result(ip, s, snapshot);
    size_t nr = s->config.nr, nz = s->config.nz, nk = s->config.nk;
    Tcl_Obj *result = Tcl_NewDictObj(), *charts = Tcl_NewListObj(0, NULL), *heat = Tcl_NewDictObj();
    Tcl_Obj *matrix = Tcl_NewListObj(0, NULL), *imp_matrix = Tcl_NewListObj(0, NULL),
            *imp_csv = Tcl_NewStringObj("z_m,r_m,Z_wave_ohm,valid\n", -1),
            *csv = Tcl_NewStringObj(
                "z_m,r_m,q_W_m3,dP_dz_W_m,dP_dr_window_W_m,cumulative_window_W,Er_re_V_m,Er_im_V_m,"
                "Ephi_re_V_m,Ephi_im_V_m,Ez_re_V_m,Ez_im_V_m,Jr_re_A_m2,Jr_im_A_m2,Jphi_re_A_m2,"
                "Jphi_im_A_m2,Jz_re_A_m2,Jz_im_A_m2,Br_re_T,Br_im_T,Bphi_re_T,Bphi_im_T,Bz_re_T,Bz_im_T,Phi_Coulomb_re_V,Phi_Coulomb_im_V\n",
                -1);
    for (size_t z = 0; z < nz; z++) {
        Tcl_ListObjAppendElement(ip, matrix, numbers(s->absorption + z * nr, nr));
        Tcl_Obj *imp_row = Tcl_NewListObj(0, NULL);
        for (size_t r = 0; r < nr; r++) {
            size_t j = z * nr + r;
            double row[26] = { s->z[z],
                s->r[r],
                s->absorption[j],
                s->axial_absorption[z],
                s->radial_absorption[r],
                s->cumulative[z] };
            for (size_t c = 0; c < 3; c++) {
                row[6 + 2 * c] = creal(s->electric[3 * j + c]);
                row[7 + 2 * c] = cimag(s->electric[3 * j + c]);
                row[12 + 2 * c] = creal(s->current[3 * j + c]);
                row[13 + 2 * c] = cimag(s->current[3 * j + c]);
                row[18 + 2 * c] = creal(s->magnetic[3 * j + c]);
                row[19 + 2 * c] = cimag(s->magnetic[3 * j + c]);
            }
            row[24] = creal(s->potential[j]);
            row[25] = cimag(s->potential[j]);
            csv_row(csv, row, 26);
            double zw = s->wave_impedance[j];
            Tcl_ListObjAppendElement(ip, imp_row,
                isfinite(zw) ? Tcl_NewDoubleObj(zw) : Tcl_NewStringObj("", 0));
            Tcl_Obj *record = isfinite(zw)
                ? Tcl_ObjPrintf("%.17g,%.17g,%.17g,1\n", s->z[z], s->r[r], zw)
                : Tcl_ObjPrintf("%.17g,%.17g,,0\n", s->z[z], s->r[r]);
            Tcl_AppendObjToObj(imp_csv, record);
            Tcl_DecrRefCount(record);
        }
        Tcl_ListObjAppendElement(ip, imp_matrix, imp_row);
    }
    str(ip, heat, "kind", "heatmap");
    str(ip, heat, "title", "空间吸收 q(r,z)");
    str(ip, heat, "unit", "W/m³");
    str(ip, heat, "xlabel", "轴向位置 z (m)");
    str(ip, heat, "xkey", "z_m");
    str(ip, heat, "xsymbol", "z");
    str(ip, heat, "xunit", "m");
    put(ip, heat, "x", numbers(s->z, nz));
    put(ip, heat, "r", numbers(s->r, nr));
    put(ip, heat, "matrix", matrix);
    put(ip, heat, "csv", csv);
    Tcl_ListObjAppendElement(ip, charts, heat);
    Tcl_Obj *wave_imp = Tcl_NewDictObj();
    str(ip, wave_imp, "kind", "heatmap");
    str(ip, wave_imp, "title", "空间波阻抗 |E|/|H| (r,z)");
    str(ip, wave_imp, "unit", "Ω");
    str(ip, wave_imp, "xlabel", "轴向位置 z (m)");
    str(ip, wave_imp, "xkey", "z_m");
    str(ip, wave_imp, "xsymbol", "z");
    str(ip, wave_imp, "xunit", "m");
    str(ip, wave_imp, "note", "局部波阻抗幅值 Z_w=μ₀√Σ|E_c|²/√Σ|B_c|²（Ω）；E、B 均先作复数相干重建，B 来自解析旋度。表示当前总场的幅值比，不是天线端口负载阻抗，也不表示单一分支的复阻抗；磁场为零或比值不可表示处留空。");
    put(ip, wave_imp, "x", numbers(s->z, nz));
    put(ip, wave_imp, "r", numbers(s->r, nr));
    put(ip, wave_imp, "matrix", imp_matrix);
    put(ip, wave_imp, "csv", imp_csv);
    Tcl_ListObjAppendElement(ip, charts, wave_imp);
    Tcl_ListObjAppendElement(ip,
        charts,
        spatial_line(
            ip, "轴向吸收分布", "轴向位置 z (m)", "dP/dz (W/m)", s->z, s->axial_absorption, nz));
    Tcl_ListObjAppendElement(ip,
        charts,
        spatial_line(
            ip, "窗口径向沉积", "半径 r (m)", "dP/dr (W/m)", s->r, s->radial_absorption, nr));
    /* Imaginary scalar potential, not a renamed electric-field component. */
    Tcl_Obj *wave = Tcl_NewDictObj(), *field = Tcl_NewListObj(0, NULL);
    for (size_t z = 0; z < nz; z++) {
        Tcl_Obj *row = Tcl_NewListObj(0, NULL);
        for (size_t r = 0; r < nr; r++)
            Tcl_ListObjAppendElement(ip, row,
                Tcl_NewDoubleObj(cimag(s->potential[z * nr + r])));
        Tcl_ListObjAppendElement(ip, field, row);
    }
    str(ip, wave, "kind", "waterfall");
    str(ip, wave, "title", "电势虚部波形堆叠");
    str(ip, wave, "quantity", "Im Φ");
    str(ip, wave, "unit", "V");
    str(ip, wave, "note", "固定 φ=0，显示 Im Φ（V）。库仑规范：∇²Φ=−∇·E，外壁 Φ(R)=0、轴心正则；由全电磁场提取纵向电势。横轴 r，纵轴为 z 基线；所有曲线共用增益，起伏不是轨迹。CSV 保留复电势，不是电场虚部。");
    put(ip, wave, "r", numbers(s->r, nr));
    put(ip, wave, "z", numbers(s->z, nz));
    put(ip, wave, "matrix", field);
    put(ip, wave, "csv", csv);
    Tcl_ListObjAppendElement(ip, charts, wave);
    put(ip, result, "charts", charts);
    put(ip, result, "csv", csv);
    put(ip, result, "snapshot", snapshot);
    num(ip, result, "count", nr * nz);
    num(ip, result, "window_power", s->window_power);
    num(ip, result, "band_power", s->spectral_power);
    num(ip, result, "resistance", creal(s->impedance));
    num(ip, result, "reactance", cimag(s->impedance));
    num(ip, result, "impedance_edge_fraction", s->impedance_edge_fraction);
    num(ip, result, "band_edge_fraction", s->band_edge_fraction);
    num(ip, result, "period", s->period);
    num(ip, result, "edge_ratio", s->edge_ratio);
    num(ip, result, "power_balance_error", s->power_balance_error);
    num(ip, result, "radial_check_error", s->radial_check_error);
    num(ip, result, "potential_backward_error", s->potential_backward_error);
    char note[1400];
    snprintf(note,
        sizeof(note),
        "无限长基准 · 仅 m=%d · k=[%g,%g] rad/m，%zu 个中点。窗口吸收 %.5g W；谱积分 %.5g W。\n"
        "给定电流的带内负载贡献 Z_m=%.5g%+.5gi Ω；工程 exp(+iωt)，X>0 为感性；不含馈线/金属损耗。\n"
        "离散重复周期 %.5g m；窗口端部/峰值 %.3g，谱边缘吸收/阻抗占比 %.3g/%.3g；功率平衡误差 "
        "%.2g。\n"
        "窗口不是端板；请增大波数范围、加密波数并扩大观察窗口检查收敛。%s",
        s->project.selected_m,
        s->config.kmin,
        s->config.kmax,
        nk,
        s->window_power,
        s->spectral_power,
        creal(s->impedance),
        cimag(s->impedance),
        s->period,
        s->edge_ratio,
        s->band_edge_fraction,
        s->impedance_edge_fraction,
        s->power_balance_error,
        s->edge_ratio > .01 ? " 端部吸收尚未衰减，需检查周期重复污染。" : "");
    str(ip, result, "note", note);
    put(ip, result, "complete", Tcl_NewIntObj(1));
    return result;
}
static int spatial_command(ClientData data, Tcl_Interp *ip, int argc, Tcl_Obj *const argv[])
{
    SpatialJob *job = data;
    char error[512] = { 0 };
    if (argc != 2) {
        Tcl_WrongNumArgs(ip, 1, argv, "begin|step|cancel");
        return TCL_ERROR;
    }
    const char *mode = Tcl_GetString(argv[1]);
    if (!strcmp(mode, "cancel")) {
        spatial_delete(data);
        return TCL_OK;
    }
    if (!strcmp(mode, "begin")) {
        spatial_delete(data);
        if (!rf_state.plasma_enabled)
            return failure(ip, "请先启用等离子体。");
        RfSpatialConfig config = rf_spatial_config(&rf_state);
        if (!rf_spatial_begin_potential(
                &rf_state.project, &rf_state.plasma, &config, &job->solution, error, sizeof(error)))
            return failure(ip, error);
        job->snapshot = state_dict(ip);
        Tcl_IncrRefCount(job->snapshot);
        return TCL_OK;
    }
    if (strcmp(mode, "step"))
        return failure(ip, "未知空间求解操作。");
    if (!rf_spatial_step(&job->solution, error, sizeof(error))) {
        spatial_delete(data);
        return failure(ip, error);
    }
    if (job->solution.complete) {
        Tcl_SetObjResult(ip, spatial_result(ip, &job->solution, job->snapshot));
        spatial_delete(data);
    } else {
        Tcl_Obj *result = Tcl_NewDictObj();
        put(ip, result, "complete", Tcl_NewIntObj(0));
        num(ip, result, "progress", 100 * job->solution.progress);
        Tcl_SetObjResult(ip, result);
    }
    return TCL_OK;
}
