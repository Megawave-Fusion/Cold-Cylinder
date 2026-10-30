/* Diagnostics and adaptive paths use separate native arrays and CSV schemas. */
/* 波型只用于诊断标签；待判定不代表该局部根不可传播。 */
static const char *branch_wave_label(int type)
{
    switch(type) {
    case RF_WAVE_VACUUM: return "真空";
    case RF_WAVE_HELICON: return "helicon 型";
    case RF_WAVE_TG: return "TG 型";
    case RF_WAVE_ICRF_FAST: return "ICRF 快波型";
    case RF_WAVE_ICRF_SLOW: return "ICRF 慢波型";
    case RF_WAVE_MIXED: return "混合/近合并";
    default: return "待判定";
    }
}
/* 把核心的发射拒绝原因概括为界面说明，完整英文原因为 CSV 的事实记录。 */
static const char *ray_refusal_label(const char *reason)
{
    if (strstr(reason, "WKB"))
        return "WKB/转向/近轴条件不满足";
    if (strstr(reason, "weak-loss"))
        return "损耗过强";
    if (strstr(reason, "evanescence"))
        return "径向倏逝或转向";
    if (strstr(reason, "coalescing"))
        return "色散根退化";
    if (strstr(reason, "direction not selected"))
        return "未选此能量方向";
    return "材料或传播条件不满足";
}
/* 无效波数或群速度转换为空白样本；分类状态与无量纲 WKB 不作幅度归一化。 */
static double branch_plot_value(const RfBranchPoint *v, int field)
{
    switch (field) {
    case 0:
        return v->valid ? creal(v->kr) : NAN;
    case 1:
        return v->valid ? cimag(v->kr) : NAN;
    case 2:
        return v->state;
    case 4:
    case 5:
    case 6:
        return v->group_valid ? v->vg[field - 4] : NAN;
    case 7:
        return v->wkb;
    }
    return NAN;
}
/* 一次求解共享 f,m,kz 快照；径向诊断表与各射线的自适应沿程表独立导出，不能互换横轴。 */
static int compute_branches(Tcl_Interp *ip, const Project *p, const Plasma *c, ComputeOutput *out)
{
    RfBranchSolution s = { 0 };
    char error[256];
    if (!rf_branches_solve_config(p, c, &rf_state.ray, &s, error, sizeof(error)))
        return failure(ip, error);
    Tcl_AppendToObj(out->csv, rf_branches_csv_header(), -1);
    char row[2400];
    for (size_t i = 0; i < s.nr; i++)
        for (int k = 0; k < 4; k++) {
            rf_branches_csv_row(&s, i, k, row, sizeof(row));
            Tcl_AppendToObj(out->csv, row, -1);
        }
    Tcl_Obj *ray_csv = Tcl_NewStringObj(rf_rays_csv_header(), -1);
    for (int k = 0; k < 4; k++)
        for (size_t i = 0; i < s.rays[k].count; i++) {
            rf_rays_csv_row(&s, k, i, row, sizeof(row));
            Tcl_AppendToObj(ray_csv, row, -1);
        }
    static const struct {
        const char *title, *unit;
        int field;
    } views[] = { { "径向波数实部", "Re(k_r) (rad/m)", 0 },
        { "径向波数虚部", "Im(k_r) (rad/m)", 1 },
        { "分支传播状态", "传播状态编号", 2 },
        { "群速度", "参考 v_g (m/s)", 4 },
        { "几何光学适用性", "WKB 指标（无量纲）", 7 },
        { "射线轴向投影", "半径 r (m)", 10 },
        { "射线方位角", "方位角 φ (rad)", 11 },
        { "射线功率衰减", "P/P0（无量纲）", 12 } };
    static const char *short_labels[] = { "A (+kr)", "A (−kr)", "B (+kr)", "B (−kr)" };
    char labels[4][128];
    for(int k=0;k<4;k++)
        snprintf(labels[k],sizeof(labels[k]),"%c · %s (%skr)",'A'+k/2,
            branch_wave_label(s.rays[k].launch_wave_type),k%2?"−":"+");
    static const char *colors[] = { "#2563eb", "#0891b2", "#d97706", "#a855f7" };
    for (size_t view_id = 0; view_id < sizeof(views) / sizeof(*views); view_id++) {
        int field = views[view_id].field;
        Tcl_Obj *xs = Tcl_NewListObj(0, NULL), *lines = Tcl_NewListObj(0, NULL);
        /* Duplicate x at each node permits gaps without interpolating across an interface
         * or connecting independently seeded ray intervals. */
        for (size_t i = 0; i < s.nr; i++)
            for (int j = 0; j < 2; j++)
                Tcl_ListObjAppendElement(ip, xs, Tcl_NewDoubleObj(s.r[i]));
        for (int k = 0; k < 4; k++) {
            for (int component = 0; component < (field == 4 ? 3 : 1); component++) {
                Tcl_Obj *ys = Tcl_NewListObj(0, NULL), *wave_names = Tcl_NewListObj(0, NULL),
                        *ray_x = Tcl_NewListObj(0, NULL),
                        *times = Tcl_NewListObj(0, NULL);
                Tcl_IncrRefCount(ray_x);
                Tcl_IncrRefCount(times);
                if (field < 10)
                    for (size_t i = 0; i < s.nr; i++) {
                        const RfBranchPoint *v = &s.points[4 * i + k];
                        double y = branch_plot_value(v, field == 4 ? 4 + component : field);
                        int gap = i && s.region[i] != s.region[i - 1];
                        for (int j = 0; j < 2; j++) {
                            int missing = !isfinite(y) || (j == 0 && gap);
                            Tcl_ListObjAppendElement(
                                ip, ys, missing ? Tcl_NewObj() : Tcl_NewDoubleObj(y));
                            Tcl_ListObjAppendElement(ip,wave_names,
                                Tcl_NewStringObj(branch_wave_label(v->wave_type),-1));
                        }
                    }
                else
                    for (size_t i = 0; i < s.rays[k].count; i++) {
                        const RfRayPoint *v = &s.rays[k].points[i];
                        double y = field == 10   ? v->r
                                   : field == 11 ? v->local.ray_phi
                                                 : exp(-v->local.ray_optical_depth);
                        Tcl_ListObjAppendElement(ip, ys, Tcl_NewDoubleObj(y));
                        Tcl_ListObjAppendElement(ip,wave_names,
                            Tcl_NewStringObj(branch_wave_label(v->local.wave_type),-1));
                        Tcl_ListObjAppendElement(
                            ip, ray_x, Tcl_NewDoubleObj(field == 10 ? v->local.ray_z : v->path));
                        Tcl_ListObjAppendElement(ip, times, Tcl_NewDoubleObj(v->local.ray_time));
                    }
                char label[160];
                static const char *component_names[] = { "r", "φ", "z" };
                if (field == 4)
                    snprintf(
                        label, sizeof(label), "%s · %s", labels[k], component_names[component]);
                else
                    snprintf(label, sizeof(label), "%s", labels[k]);
                Tcl_Obj *line = series(ip, label, colors[k], ys);
                str(ip,line,"short_label",short_labels[k]);
                put(ip,line,"wave_names",wave_names);
                if (field == 4) {
                    put(ip, line, "branch", Tcl_NewIntObj(k));
                    put(ip, line, "component", Tcl_NewIntObj(component));
                    str(ip, line, "branch_label", labels[k]);
                    str(ip, line, "dash", component == 0 ? "" : component == 1 ? "6 3" : "2 3");
                }
                if (field >= 10) {
                    put(ip, line, "x", ray_x);
                    str(ip, line, "stop", s.rays[k].stop);
                }
                if (field >= 10)
                    put(ip, line, "time", times);
                Tcl_ListObjAppendElement(ip, lines, line);
                Tcl_DecrRefCount(ray_x);
                Tcl_DecrRefCount(times);
            }
        }
        Tcl_Obj *view =
            chart(ip, views[view_id].title, "半径 r (m)", views[view_id].unit, xs, lines);
        str(ip, view, "family", field < 10 ? "branches" : "rays");
        const char *unit = field <= 1              ? "rad/m"
                           : field == 4              ? "m/s"
                           : field == 10             ? "m"
                           : field == 11             ? "rad"
                           : field == 2              ? "分类"
                                                     : "无量纲";
        str(ip, view, "unit", unit);
        if (field == 2) {
            str(ip, view, "kind", "branch_status");
            num(ip, view, "interface", c->radius);
        }
        if (field == 7)
            str(ip,
                view,
                "note",
                "WKB 指标无量纲，当前上限为 "
                "0.1；还需满足弱损耗、非退化、极化与群速度等条件才可追踪。");
        if (field == 4) {
            put(ip, view, "velocity_group", Tcl_NewBooleanObj(1));
            str(ip,
                view,
                "note",
                "同图显示四个径向根的三个参考群速度分量（m/s）。颜色区分 A/B 和 "
                "±kr；径向为实线、方位为虚线、轴向为点线。图例可分别勾选分量和分支；只在弱损耗参考"
                "群速度有效的区域显示，空白不表示速度为零。");
        }
        if (field >= 10) {
            put(ip, view, "parametric", Tcl_NewIntObj(1));
            str(ip, view, "xlabel", field == 10 ? "轴向位置 z (m)" : "沿程距离 s (m)");
            put(ip, view, "csv", ray_csv);
        }
        if (field <= 1)
            str(ip,
                view,
                "note",
                "这是局部波数随半径的曲线，不是空间波形。均匀密度/"
                "磁场下，远离轴心的波数可以近似为常数；传播路径见本栏的“射线轴向投影”。");
        if (field == 2)
            str(ip,
                view,
                "note",
                "颜色表示各半径处的离散分类，不是波形或射线。传播候选还需通过 "
                "WKB、极化和群速度检查才可追踪；排除原因见 CSV。");
        if (field >= 10)
            str(ip,
                view,
                "note",
                "各根从指定位置发射，箭头沿时间递增；其余射线图横轴为路径 "
                "s。悬停查看各支终止原因；CSV "
                "保留自适应采样、校正前残差和波矢校正量。无自动界面反射/转换；P/P0 "
                "不含天线耦合与聚焦。");
        Tcl_Obj *old_note=NULL,*note_key=Tcl_NewStringObj("note",-1);
        Tcl_IncrRefCount(note_key);
        Tcl_DictObjGet(ip,view,note_key,&old_note);
        Tcl_DecrRefCount(note_key);
        char wave_note[768];
        snprintf(wave_note,sizeof(wave_note),
            "图例波型按发射半径 r₀=%.5g m 识别；悬停查看局部波型。helicon/TG 型采用色散参考匹配，ICRF 快/慢波型按冷色散极限延续识别；"
            "待判定不表示不能传播；识别原因见 CSV。",s.ray_config.r0);
        Tcl_Obj *note=Tcl_NewStringObj(wave_note,-1);
        if(old_note) {Tcl_AppendToObj(note,"\n",1);Tcl_AppendToObj(note,Tcl_GetString(old_note),-1);}
        put(ip,view,"note",note);
        Tcl_ListObjAppendElement(ip, out->charts, view);
    }
    num(ip, out->result, "count", s.nr);
    num(ip, out->result, "ray_count", s.ray_count);
    num(ip, out->result, "ray_points", s.ray_points);
    num(ip, out->result, "valid_points", s.valid_points);
    char note[2400];
    snprintf(note,
        sizeof(note),
        "固定 f=%.6g MHz，m=%d，kz=%.6g rad/m；两支 × ±kr，%zu 条已发射射线。\n起点 r=%.5g "
        "m，z=%.5g m，φ=%.5g°；容差 %.2g，最大步长 %.3g m，路径上限 %.4g "
        "m（不是端板）。\n实轨迹采用弱损耗无碰撞参考；WKB≤0.1。当前材料给出复根及 "
        "P/P0；不含天线分支耦合功率。",
        p->frequency_mhz,
        p->selected_m,
        p->kz,
        s.ray_count,
        s.ray_config.r0,
        s.ray_config.z0,
        s.ray_config.phi0_deg,
        s.ray_config.tolerance,
        s.ray_config.max_step,
        s.ray_config.max_path);
    for (int k = 0; k < 4; k++)
        if (!s.rays[k].count) {
            size_t used = strlen(note);
            snprintf(note + used,
                sizeof(note) - used,
                "\n%s 未发射：%s",
                labels[k],
                ray_refusal_label(s.rays[k].stop));
        }
    str(ip, out->result, "note", note);
    put(ip, out->result, "ray_csv", ray_csv);
    rf_branches_free(&s);
    return TCL_OK;
}
