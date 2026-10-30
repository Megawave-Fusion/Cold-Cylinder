/* Potential-only waveform job; cancellation owns and frees the modal state. */
typedef struct {
    RfSpatialSolution solution;
    Tcl_Obj *snapshot;
} SpatialJob;
static SpatialJob spatial_job;
/* 任务取消、失败或解释器退出均调用此函数；快照和求解器状态只释放一次。 */
static void spatial_delete(ClientData data)
{
    SpatialJob *job = data;
    rf_spatial_free(&job->solution);
    if (job->snapshot)
        Tcl_DecrRefCount(job->snapshot);
    job->snapshot = NULL;
}
/* 矩阵按 z 行、r 列复制 Im Φ（V）；CSV 同时保留复电势与固定方位模数。 */
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


/* 每次 step 只推进有限工作量；任务在 begin 时冻结参数，后续界面修改不进入该解。 */
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
            return failure(ip, "请选择等离子体栏目后计算。");
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
        Tcl_SetObjResult(ip, potential_result(ip, &job->solution, job->snapshot));
        spatial_delete(data);
    } else {
        Tcl_Obj *result = Tcl_NewDictObj();
        put(ip, result, "complete", Tcl_NewIntObj(0));
        num(ip, result, "progress", 100 * job->solution.progress);
        Tcl_SetObjResult(ip, result);
    }
    return TCL_OK;
}
