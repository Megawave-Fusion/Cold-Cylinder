# Native, resizable workbench. All plot coordinates come from C result objects.
catch {console hide}
namespace eval ::rf {
    variable cfg
    variable choice
    variable widgets
    variable row_labels
    variable advanced
    array set advanced {antenna 0 medium 0}
    variable selectors
    variable cache
    variable loading 1
    variable dirty 0
    variable revision 0
    variable busy 0
    variable job {}
    variable project_path {}
    variable status {就绪 · 设置参数后开始计算}
    variable mode vacuum
    variable chart_name {}
    variable chart_names {}
    variable heat_component 总幅值
    variable waterfall_scale 1
    variable progress 0
    variable angle 35
    variable elevation 22
    variable zoom 1.0
    variable model_view_height_m 5.0
    variable model {}
    variable model_job {}
    variable model_info {}
    variable model_status {正在生成天线模型…}
    variable model_type {}
    variable show_vessel 1
    variable enabled
    variable modes {vacuum scan}
    variable mode_names {真空单模场 真空波数谱}
    variable selected_mode 真空单模场
    variable plots {}
    variable current {}
    variable snapshot_note {}
    variable hover {}
    variable caption {}
    variable metric1 —
    variable metric2 —
    variable metric3 —
    variable fields_by_page
}

# 固定应用标题，不把工程文件名或未保存标记混进模型名称。
proc ::rf::update_title {} {
    wm title . {Cold-Cylinder 无限长圆柱体冷等离子体工作模型}
}
# 仅用户编辑递增版本；载入/格式化期间关闭跟踪，防止把程序回填误认为新参数。
proc ::rf::edited {args} {
    if {$::rf::loading} {return}
    set ::rf::dirty 1
    incr ::rf::revision
    update_title
    update_controls
    mark_snapshot
    schedule_model
}
# 从已提交 C 状态回填表单，紧凑数字格式必须保持同一个 double 数值。
proc ::rf::pull {} {
    variable cfg
    variable loading
    variable selectors
    variable choice
    set loading 1
    array set cfg [rf_state]
    # Keep large densities/collision rates readable in the narrow inputs.
    # Use compact notation only when it preserves the stored double exactly.
    foreach key [array names cfg] {
        if {![info exists selectors($key)] && (abs($cfg($key))>=1e6 || ($cfg($key)!=0 && abs($cfg($key))<1e-4))} {
            set compact [format %.12g $cfg($key)]
            if {double($compact)==double($cfg($key))} {set cfg($key) $compact}
        }
    }
    foreach key [array names selectors] {
        lassign $selectors($key) values labels
        set choice($key) [lindex $labels [lsearch -exact $values $cfg($key)]]
    }
    # Old project files retain a medium hint; restore a suitable initial view.
    if {$cfg(plasma_enabled) && $::rf::mode in {vacuum scan}} {set ::rf::mode plasma}
    if {!$cfg(plasma_enabled) && $::rf::mode ni {vacuum scan}} {set ::rf::mode vacuum}
    set loading 0
    update_controls
    schedule_model
}
# 把中文下拉标签映射到持久化枚举值；只在主动切换双鞍形时设默认角度。
proc ::rf::choose {key} {
    set previous_type $::rf::cfg(type)
    lassign $::rf::selectors($key) values labels
    set ::rf::cfg($key) [lindex $values [lsearch -exact $labels $::rf::choice($key)]]
    # The saddle's default must not inherit another family's 180-degree arc.
    # Loading a project uses pull(), so an explicitly saved custom angle is retained.
    if {$key eq {type} && $::rf::cfg(type)==7 && $previous_type!=7} {
        set ::rf::cfg(arc_angle_deg) 120.0
    }
}
# 校验失败时展开并定位有问题的输入框；成功后统一采用原生返回的规范参数。
proc ::rf::apply {} {
    if {[catch {rf_state [array get ::rf::cfg]} msg options]} {
        if {[dict exists $options -errorcode] && [lindex [dict get $options -errorcode] 0] eq {RF}} {
            set key [lindex [dict get $options -errorcode] 2]
            if {[info exists ::rf::widgets($key)]} {
                if {$key in {global_phase_deg azimuth_deg axial_offset band_samples length}} {set ::rf::advanced(antenna) 1}
                if {$key in {plasma.electron_collision_edge_ratio plasma.ion_collision_edge_ratio}} {set ::rf::advanced(medium) 1}
                update_controls
                set w $::rf::widgets($key)
                set page [winfo parent [winfo parent [winfo parent $w]]]
                if {[winfo exists $page]} {.body.side.tabs select $page}
                focus $w
                catch {$w selection range 0 end}
                # Scroll the invalid field into view, including advanced fields.
                set frame [winfo parent $w]
                set c [winfo parent $frame]
                update idletasks
                if {[winfo class $c] eq {Canvas}} {
                    $c yview moveto [expr {double([winfo y $w]-30)/max(1,[winfo height $frame])}]
                }
            }
        }
        set ::rf::status $msg
        tk_messageBox -parent . -icon error -title {请检查参数} -message $msg
        return 0
    }
    pull
    return 1
}
# 先提交表单，再写统一工程；取消路径选择不会清除 dirty 标志。
proc ::rf::save_project {{save_as 0}} {
    if {![apply]} {return 0}
    set p $::rf::project_path
    if {$save_as || $p eq {}} {
        set p [tk_getSaveFile -parent . -title {保存项目} -defaultextension .rfproj -filetypes {{{天线项目} {.rfproj}}}]
    }
    if {$p eq {}} {return 0}
    if {[catch {rf_save $p} msg]} {tk_messageBox -parent . -icon error -message $msg; return 0}
    set ::rf::project_path $p
    set ::rf::dirty 0
    set ::rf::status {项目已保存}
    update_title
    return 1
}
# 只在可能丢弃尚未保存的用户编辑时询问；取消必须阻止后续打开或关闭。
proc ::rf::may_discard {} {
    if {!$::rf::dirty} {return 1}
    set answer [tk_messageBox -parent . -type yesnocancel -icon question -title {保存修改} -message {是否保存当前项目的修改？}]
    if {$answer eq {cancel}} {return 0}
    if {$answer eq {yes}} {return [save_project]}
    return 1
}
# 载入成功后才替换工程路径和版本；旧计算结果保留为可识别的参数快照。
proc ::rf::open_project {{p {}}} {
    if {$::rf::busy} {return}
    if {$p eq {}} {set p [tk_getOpenFile -parent . -title {打开项目} -filetypes {{{天线项目} {.rfproj .plasmaconfig}} {{全部文件} {*}}}]}
    if {$p eq {} || ![may_discard]} {return}
    if {[catch {rf_open $p} msg]} {tk_messageBox -parent . -icon error -title {无法加载项目} -message $msg;return}
    set ::rf::project_path $p
    set ::rf::dirty 0
    incr ::rf::revision
    pull
    update_title
    mark_snapshot
    set ::rf::status {项目已加载 · 可在左侧修改参数}
}
# 关闭前处理未保存编辑，并撤销排队回调，避免窗口销毁后继续访问控件。
proc ::rf::close {} {
    if {![may_discard]} {return}
    if {$::rf::job ne {}} {after cancel $::rf::job}
    if {$::rf::model_job ne {}} {after cancel $::rf::model_job}
    destroy .
}

# Form pages move only through their scrollbars. Real widgets retain keyboard navigation.
# 每个参数页独立保存滚动范围；画布承载原生控件而不是绘制不可编辑的假输入框。
proc ::rf::page {key title} {
    set p .body.side.tabs.$key
    ttk::frame $p
    .body.side.tabs add $p -text $title
    canvas $p.c -background #ffffff -highlightthickness 0 -width 250
    ttk::scrollbar $p.s -orient vertical -command [list $p.c yview]
    $p.c configure -yscrollcommand [list $p.s set]
    grid $p.c -row 0 -column 0 -sticky nsew
    grid $p.s -row 0 -column 1 -sticky ns
    grid columnconfigure $p 0 -weight 1
    grid rowconfigure $p 0 -weight 1
    ttk::frame $p.c.form -padding {8 2 8 6} -style White.TFrame
    $p.c create window 0 0 -anchor nw -window $p.c.form -tags form
    bind $p.c <Configure> [list ::rf::size_form $p.c %w]
    bind $p.c.form <Configure> [list ::rf::scroll_form $p.c]
    set ::rf::fields_by_page($key) $p.c.form
    return $p.c.form
}
# 页面宽度跟随侧栏；按当前字体的两列真实请求宽度补足少量空间，不压缩字号。
proc ::rf::size_form {c width} {
    $c itemconfigure form -width $width
    if {$width>1 && [winfo exists .body.side]} {
        set missing [expr {[winfo reqwidth $c.form]-$width}]
        if {$missing>0} {.body.side configure -width [expr {[winfo width .body.side]+$missing}]}
    }
}
# 表单增删/隐藏参数后更新真实滚动范围，保证下方参数能经滚动条访问。
proc ::rf::scroll_form {c} {
    $c configure -scrollregion [$c bbox all]
    size_form $c [winfo width $c]
}
# 参数小组标题占满名称/数值列，不改变各输入框的统一对齐。
proc ::rf::group {p title} {
    set row [lindex [grid size $p] 1]
    ttk::label $p.group$row -text $title -style ParamGroup.TLabel -font RFParamGroup -padding {0 6 0 2}
    grid $p.group$row -row $row -column 0 -columnspan 3 -sticky w
}
# 统一字段布局和枚举映射；单位放在名称中，数值列不随名称长度漂移。
proc ::rf::field {p key title {unit {}} {values {}} {labels {}}} {
    variable widgets
    variable selectors
    variable row_labels
    variable parameter_width
    if {![info exists parameter_width]} {set parameter_width [expr {[font measure RFSidebar 00000000]+22}]}
    set row [lindex [grid size $p] 1]
    ttk::label $p.l$row -text "$title[expr {$unit eq {} ? {} : " ($unit)"}]" -style Param.TLabel -font RFSidebar -wraplength 145 -justify left -anchor w
    grid $p.l$row -row $row -column 0 -sticky ew -pady 1
    if {$values ne {}} {
        set selectors($key) [list $values $labels]
        foreach label $labels {set parameter_width [expr {max($parameter_width,[font measure RFSidebar $label]+22)}]}
        ttk::combobox $p.v$row -textvariable ::rf::choice($key) -values $labels -state readonly -width 8 -font RFSidebar -style Param.TCombobox
        bind $p.v$row <<ComboboxSelected>> [list ::rf::choose $key]
    } else {
        ttk::entry $p.v$row -textvariable ::rf::cfg($key) -width 8 -justify left -font RFValue -style Param.TEntry
    }
    grid $p.v$row -row $row -column 1 -sticky ew -padx {4 0} -pady 1
    # A compact, fixed label column keeps names close to the common value
    # column. Put surplus sidebar space after the fields, not between them.
    grid columnconfigure $p 0 -weight 0 -minsize 145
    grid columnconfigure $p 2 -weight 1
    foreach page [array names ::rf::fields_by_page] {
        grid columnconfigure $::rf::fields_by_page($page) 1 -weight 0 -minsize [expr {$parameter_width+4}]
    }
    set widgets($key) $p.v$row
    set row_labels($key) $p.l$row
}
# 高级字段只改变可见性；隐藏不删除已保存的物理参数。
proc ::rf::advanced_toggle {p page text} {
    set row [lindex [grid size $p] 1]
    ttk::checkbutton $p.advanced -text $text -variable ::rf::advanced($page) -command ::rf::update_controls -style Advanced.TCheckbutton
    grid $p.advanced -row $row -column 0 -columnspan 3 -sticky w -pady {8 3}
}
# 在控件类绑定之前拦截触控板事件，同时防止下拉框被滚轮意外改值。
proc ::rf::block_parameter_wheel {w} {
    # Run before widget/class bindings: a combobox otherwise changes its value
    # on a trackpad gesture even when the form itself does not scroll.
    if {{RFParameterNoWheel} ni [bindtags $w]} {
        bindtags $w [linsert [bindtags $w] 0 RFParameterNoWheel]
    }
    foreach child [winfo children $w] {block_parameter_wheel $child}
}
# 只允许结果选项和说明滚动；绘图原点及左侧参数页不受全局滚轮事件影响。
proc ::rf::wheel {w delta} {
    if {![winfo exists $w]} {return}
    if {$w eq {.body.main.caption}} {set w .body.main.notes.c}
    foreach parent [array names ::rf::option_canvases] {
        if {$w eq $parent || [string match "$parent.*" $w]} {set w $::rf::option_canvases($parent);break}
    }
    while {$w ne {.} && [winfo class $w] ne {Canvas}} {set w [winfo parent $w]}
    # Only result options and notes scroll. A wheel over the result plot must not move its
    # coordinate origin and silently clip the axes/data.
    if {$w eq {.body.main.notes.c} || $w in [lmap parent [array names ::rf::option_canvases] {set ::rf::option_canvases($parent)}]} {$w yview scroll $delta units}
}
# 由天线位型、碰撞模式和 busy 状态决定参数可见性/禁用状态，不修改求解方程。
proc ::rf::update_controls {} {
    variable widgets
    variable selectors
    variable cfg
    variable busy
    if {![winfo exists .body.side.tabs]} {return}
    sync_medium
    foreach key [array names widgets] {
        set w $widgets($key)
        set disabled $busy
        set visible 1
        if {$cfg(plasma.solve_mode)==2 && $key in {plasma.electron_collision plasma.ion_collision plasma.electron_collision_edge_ratio plasma.ion_collision_edge_ratio}} {set disabled 1}
        if {$key eq {elements} && $cfg(type) ni {2 3 12 13 15 16 17}} {set visible 0}
        if {$key eq {phase_step} && $cfg(type) ni {2 3 12 13}} {set visible 0}
        if {$key eq {spacing} && $cfg(type) ni {2 3 13 17}} {set visible 0}
        if {$key in {turns winding_direction} && $cfg(type) ni {5 14}} {set visible 0}
        if {$key eq {return_radius} && $cfg(type) ni {9 10 11 12}} {set visible 0}
        if {$key eq {arc_angle_deg} && $cfg(type) ni {6 7 10 12}} {set visible 0}
        if {$key eq {width} && $cfg(type) ni {1 2 3 17}} {set visible 0}
        if {$key eq {antenna_length} && $cfg(type) in {4 9 10 12 13 14 15 17}} {set visible 0}
        if {$key eq {inner_radius} && $cfg(type) ni {14 15}} {set visible 0}
        if {$key eq {gap_angle_deg} && $cfg(type)!=15} {set visible 0}
        if {$key in {source_mode current_pattern} && $cfg(type) ni {15 16}} {set visible 0}
        if {$key eq {current_phase_gradient} && $cfg(type)!=17} {set visible 0}
        if {$key in {global_phase_deg azimuth_deg axial_offset band_samples length} && !$::rf::advanced(antenna)} {set visible 0}
        if {$key in {plasma.electron_collision_edge_ratio plasma.ion_collision_edge_ratio} && !$::rf::advanced(medium)} {set visible 0}
        if {$visible} {grid $w $::rf::row_labels($key)} else {grid remove $w $::rf::row_labels($key)}
        if {$disabled} {$w state disabled} else {
            $w state !disabled
            if {[info exists selectors($key)]} {$w state readonly}
        }
    }
    # Labels describe the actual prescribed source parameters for each family.
    foreach {key text} {elements 阵列单元数 spacing {单元间距 (m)} width {天线宽度 (m)} antenna_radius {天线半径 (m)} band_width {电流带宽 (m)} current {峰值电流 (A)}} {
        $::rf::row_labels($key) configure -text $text
    }
    switch $cfg(type) {
        13 {
            $::rf::row_labels(elements) configure -text 环数
            $::rf::row_labels(spacing) configure -text {轴向环间距 (m)}
        }
        14 - 15 {
            $::rf::row_labels(antenna_radius) configure -text {外半径 (m)}
            if {$cfg(type)==15} {$::rf::row_labels(elements) configure -text 径向环数}
        }
        16 {
            $::rf::row_labels(elements) configure -text 轴向腿数
            $::rf::row_labels(current) configure -text {轴腿参考电流 (A)}
        }
        17 {
            $::rf::row_labels(elements) configure -text 横向段数
            $::rf::row_labels(width) configure -text {横向弧长 (m)}
            $::rf::row_labels(spacing) configure -text {轴向节距 (m)}
        }
    }
    if {$cfg(type) in {13 14 15}} {$::rf::row_labels(band_width) configure -text {轴向源展宽 (m)}}
    if {$cfg(type) in {16 17}} {$::rf::row_labels(band_width) configure -text {角向源展宽 (m)}}
    foreach w [concat {.header.open .header.save .header.more .body.model.tools.export} [winfo children .body.main.tools.categories.tabs]] {
        if {$busy} {$w state disabled} else {$w state !disabled}
    }
    if {$busy} {.header.run state disabled;.header.stop state !disabled} else {
        .header.run state !disabled;.header.stop state disabled
    }
    set exportable [expr {!$busy && [info exists ::rf::cache($::rf::mode)]}]
    foreach w {.header.export .header.svg} {
        if {$exportable} {$w state !disabled} else {$w state disabled}
    }
}
# The catalogue is always visible. The legacy medium flag is derived from
# the selected view, only for solver dispatch, project compatibility and model preview.
# 七栏目固定存在；旧介质标志由当前栏目派生，只用于计算分派与模型预览。
proc ::rf::sync_medium {} {
    set modes {vacuum scan plasma plasma_scan full waveform branches}
    set names {真空单模场 真空波数谱 等离子体单模场 等离子体波数谱 等离子体总场 电势波形 色散与射线}
    set rebuild [expr {$::rf::modes ne $modes}]
    set ::rf::modes $modes
    set ::rf::mode_names $names
    if {$::rf::mode ni $modes} {set ::rf::mode vacuum}
    set medium [expr {$::rf::mode ni {vacuum scan}}]
    if {$::rf::cfg(plasma_enabled)!=$medium} {
        set loading $::rf::loading
        set ::rf::loading 1
        set ::rf::cfg(plasma_enabled) $medium
        set ::rf::loading $loading
        schedule_model
    }
    set ::rf::selected_mode [lindex $names [lsearch -exact $modes $::rf::mode]]
    if {$rebuild} {
        category_tabs .body.main.tools.categories.tabs $names
        show_result
    }
}
# 只切换显示与介质分派，缓存仍按各栏目独立保存。
proc ::rf::select_mode {} {
    set i [lsearch -exact $::rf::mode_names $::rf::selected_mode]
    set ::rf::mode [lindex $::rf::modes $i]
    update_controls
    show_result
}
# 比较结果版本与参数版本；修改输入后仍显示上一份完整结果，但明确标注已过期。
proc ::rf::mark_snapshot {} {
    if {[info exists ::rf::cache($::rf::mode)]} {
        set r $::rf::cache($::rf::mode)
        set stale [expr {[dict get $r revision]!=$::rf::revision}]
        set ::rf::snapshot_note [expr {$stale ? {参数已修改 · 显示上次结果} : {结果对应当前参数}}]
    } else {set ::rf::snapshot_note {尚未计算}}
}
# 提交并冻结本次输入后启动任务；电势波形增加真实 z 样本，不插值制造密集曲线。
proc ::rf::run {} {
    if {$::rf::busy || ![apply]} {return}
    if {$::rf::mode eq {waveform}} {
        # Recompute real axial samples, rather than interpolating old traces.
        # Keep the existing native 500000-point observation-grid limit.
        set target [expr {min(401,500000/int($::rf::cfg(space.nr)))}]
        if {$::rf::cfg(space.nz)<$target} {
            set ::rf::cfg(space.nz) $target
            if {![apply]} {return}
        }
    }
    set ::rf::busy 1
    set ::rf::progress 0
    set ::rf::status {计算中…}
    update_controls
    set ::rf::job [after 1 ::rf::start]
}
# 统一结束 busy 状态并恢复控件，不能在这里清空上一份成功结果。
proc ::rf::finish {} {
    set ::rf::job {}
    set ::rf::busy 0
    update_controls
}
# 按模块启动同步单谱、逐波数扫描或原生分步任务，成功结果才进入缓存。
proc ::rf::start {} {
    set ::rf::job {}
    if {$::rf::mode eq {full}} {
        if {[catch {rf_full begin} result]} {
            set ::rf::status "计算失败：$result"
            finish
            tk_messageBox -parent . -icon error -title {计算失败} -message $result
            return
        }
        full_step
        return
    }
    if {$::rf::mode eq {waveform}} {
        if {[catch {rf_spatial begin} result]} {
            set ::rf::status "计算失败：$result"
            finish
            tk_messageBox -parent . -icon error -title {计算失败} -message $result
            return
        }
        spatial_step
        return
    }
    if {$::rf::mode in {scan plasma_scan}} {
        set ::rf::scan_i 0
        set ::rf::scan_x {}
        set ::rf::scan_y {{} {} {}}
        set ::rf::scan_impedance {{} {}}
        set ::rf::scan_source_y {{} {} {}}
        set ::rf::scan_source_csv "kz_rad_per_m,Kr_re,Kr_im,Kphi_re,Kphi_im,Kz_re,Kz_im,error\n"
        set ::rf::scan_source_errors {}
        set ::rf::scan_maps [dict create total {} B {} absorption {}]
        set ::rf::scan_r {}
        switch -- $::rf::mode {
            scan {set ::rf::scan_csv "kz_rad_per_m,valid,peak_total_V,backward_error,axial_flux_W_m2,error\n"}
            plasma_scan {set ::rf::scan_csv "kz_rad_per_m,valid,peak_total_V,absorbed_W_m,axial_flux_W_m2,R_ohm_m,X_ohm_m,error\n"}
        }
        set ::rf::scan_errors {}
        scan_step
        return
    }
    set command $::rf::mode
    if {[catch {rf_compute $command} result]} {
        set ::rf::status "计算失败：$result"
        finish
        tk_messageBox -parent . -icon error -title {计算失败} -message $result
        return
    }
    dict set result revision $::rf::revision
    set ::rf::cache($::rf::mode) $result
    set ::rf::progress 100
    set ::rf::status {计算完成}
    show_result
    finish
}
# One wave number per event-loop turn, so Stop and window redraw remain responsive.
# 每轮只求一个 kz，失败切片保留缺口与原因；源谱和场响应分别判定有效性。
proc ::rf::scan_step {} {
    variable scan_i
    variable scan_x
    variable scan_y
    variable scan_impedance
    variable scan_csv
    variable scan_errors
    variable scan_source_y
    variable scan_source_csv
    variable scan_source_errors
    variable scan_maps
    variable scan_r
    variable cfg
    set k [expr {$cfg(kmin)+($cfg(kmax)-$cfg(kmin))*$scan_i/double($cfg(nk)-1)}]
    set cmd [dict get {scan sample plasma_scan plasma_sample} $::rf::mode]
    lappend scan_x $k
    # The impressed source remains meaningful even at a vacuum field pole.
    # Evaluate it independently so a missing field slice cannot erase its data.
    if {$cmd eq {sample}} {
        if {[catch {rf_compute source $k} source]} {
            lappend scan_source_errors [list $k $source]
            set reason [string map [list \" \"\"] $source]
            append scan_source_csv "$k,,,,,,,\"$reason\"\n"
            foreach j {0 1 2} {lset scan_source_y $j [concat [lindex $scan_source_y $j] [list {}]]}
        } else {
            foreach j {0 1 2} {
                lset scan_source_y $j [concat [lindex $scan_source_y $j] [list [lindex [dict get $source values] $j]]]
            }
            append scan_source_csv [string trimright [dict get $source csv] \n] ",\n"
        }
    }
    if {[catch {rf_compute $cmd $k} one]} {
        lappend scan_errors [list $k $one]
        set reason [string map [list \" \"\"] $one]
        if {$cmd eq {plasma_sample}} {
            append scan_csv "$k,0,,,,,,\"$reason\"\n"
        } else {append scan_csv "$k,0,,,,\"$reason\"\n"}
        foreach j {0 1} {lset scan_impedance $j [concat [lindex $scan_impedance $j] [list {}]]}
        foreach j {0 1 2} {lset scan_y $j [concat [lindex $scan_y $j] [list {}]]}
        foreach key {total B absorption} {dict lappend scan_maps $key {}}
    } else {
        set ys [list [dict get $one peak] [expr {$cmd eq {plasma_sample} ? [dict get $one absorbed] : 0.}] [dict get $one flux]]
        foreach j {0 1 2} {lset scan_y $j [concat [lindex $scan_y $j] [list [lindex $ys $j]]]}
        if {$cmd eq {plasma_sample}} {
            foreach j {0 1} key {spectral_resistance spectral_reactance} {
                lset scan_impedance $j [concat [lindex $scan_impedance $j] [list [dict get $one $key]]]
            }
        }
        set scan_r [dict get $one heat_r]
        foreach key {total B absorption} {dict lappend scan_maps $key [dict get $one heat $key]}
        append scan_csv [string trimright [dict get $one csv] \n] ",\n"
    }
    incr scan_i
    set ::rf::progress [expr {100.0*$scan_i/$cfg(nk)}]
    set ::rf::status "计算中 · $scan_i / $cfg(nk) 个波数点"
    if {$scan_i<$cfg(nk)} {set ::rf::job [after 1 ::rf::scan_step];return}
    if {[llength $scan_errors]==$cfg(nk) && ($cmd ne {sample} || [llength $scan_source_errors]==$cfg(nk))} {
        set ::rf::status "扫描失败：全部波数点无有效解 · [lindex [lindex $scan_errors 0] 1]"
        finish
        return
    }
    set charts {}
    if {$scan_r eq {}} {
        # All field slices may be absent while the prescribed source is valid.
        for {set j 0} {$j<=16} {incr j} {lappend scan_r [expr {$cfg(radius)*$j/16.}]}
    }
    set line [dict create label 电场幅值 color #2563eb y [lindex $scan_y 0]]
    lappend charts [dict create title 电场幅值 xlabel {轴向波数 k_z (rad/m)} ylabel {电场谱幅值 (V)} x $scan_x series [list $line]]
    if {$cmd eq {sample}} {
        set lines {}
        foreach label {{r 分量} {φ 分量} {z 分量}} color {#2563eb #d97706 #059669} y $scan_source_y {
            lappend lines [dict create label $label color $color y $y]
        }
        lappend charts [dict create title 电流谱幅值 xlabel {轴向波数 k_z (rad/m)} ylabel {电流谱幅值 (A)} x $scan_x series $lines csv $scan_source_csv note {给定天线电流的源谱，与真空场响应共用 m 和 k_z 采样；不随负载自洽调整。场求解失败处仍单独计算源谱，源谱导出保留复数分量及失败原因。}]
    }
    if {$cmd eq {plasma_sample}} {
        set line [dict create label {截面积分吸收} color #d97706 y [lindex $scan_y 1]]
        lappend charts [dict create title 吸收谱 xlabel {轴向波数 k_z (rad/m)} ylabel {吸收谱 (W m)} x $scan_x series [list $line]]
        set lines {}
        foreach label {{R 谱} {X 谱}} color {#2563eb #d97706} y $scan_impedance {
            lappend lines [dict create label $label color $color y $y]
        }
        lappend charts [dict create title 负载阻抗谱 xlabel {轴向波数 k_z (rad/m)} ylabel {dZ/dk_z (Ω m)} x $scan_x series $lines note {与吸收谱共用波数采样；给定天线电流、当前 m 的负载阻抗谱贡献，工程 exp(+iωt)，X>0 为感性。对 k_z 积分得到带内阻抗 Ω；不含馈线与金属损耗。}]
    }
    set line [dict create label {轴向能流} color #7c3aed y [lindex $scan_y 2]]
    lappend charts [dict create title 轴向能流谱 xlabel {轴向波数 k_z (rad/m)} ylabel {截面谱场能流量 (W m²)} x $scan_x series [list $line] note {由模态谱场构造 ½Re(E_hat×B_hat*)/μ0，再作 2π∫r dr 截面积分，单位 W m²，保留方向。它不是实际空间截面功率 W；实际能流由相干重建后的场计算。}]
    if {$cmd eq {plasma_sample}} {
        set keys {total B absorption}
        set titles {总电场谱热图 磁场谱热图 局部吸收谱热图}
        set units {V {T m} W/m}
    } else {
        set keys {total B}
        set titles {总电场谱热图 磁场谱热图}
        set units {V {T m}}
    }
    set error_lookup {}
    foreach pair $scan_errors {dict set error_lookup [lindex $pair 0] [lindex $pair 1]}
    foreach key $keys title $titles unit $units {
        lappend charts [dict create kind heatmap title $title unit $unit xlabel {轴向波数 k_z (rad/m)} xsymbol k_z xunit rad/m x $scan_x r $scan_r matrix [dict get $scan_maps $key] errors $error_lookup]
    }
    set note "m=$cfg(selected_m) · $cfg(kmin) 至 $cfg(kmax) rad/m · [expr {$cfg(nk)-[llength $scan_errors]}] / $cfg(nk) 点有效"
    if {[llength $scan_errors]} {
        append note "\n失败切片留空，导出数据保留原因。"
        set e [lindex $scan_errors 0]
        append note " 首个 k_z=[fmt [lindex $e 0]]：[lindex $e 1]"
    }
    if {$cmd eq {sample}} {
        append note "\n源谱 [expr {$cfg(nk)-[llength $scan_source_errors]}] / $cfg(nk) 点有效；源谱与场响应分别保留失败点。"
    }
    set ::rf::cache($::rf::mode) [dict create charts $charts csv $scan_csv snapshot [rf_state] revision $::rf::revision note $note count $cfg(nk)]
    set ::rf::status "扫描完成 · [llength $scan_errors] 个失败点"
    show_result
    finish
}
# 推进多 m 相干重建；只有完整解才替换总场缓存，取消/失败保留前一次结果。
proc ::rf::full_step {} {
    set ::rf::job {}
    if {[catch {rf_full step} result]} {
        set ::rf::status "计算失败：$result"
        finish
        tk_messageBox -parent . -icon error -title {计算失败} -message $result
        return
    }
    if {![dict get $result complete]} {
        set ::rf::progress [dict get $result progress]
        set ::rf::status [format {全模式场与电流重建中… %.0f%%} $::rf::progress]
        set ::rf::job [after 1 ::rf::full_step]
        return
    }
    dict set result revision $::rf::revision
    set ::rf::cache(full) $result
    set ::rf::progress 100
    set ::rf::status {全模式计算完成 · 请检查 m 与波数截断收敛}
    show_result
    finish
}
# 推进单 m 电势的 kz 相干重建；显示 Im Φ 的真实空间单位 V。
proc ::rf::spatial_step {} {
    set ::rf::job {}
    if {[catch {rf_spatial step} result]} {
        set ::rf::status "计算失败：$result"
        finish
        tk_messageBox -parent . -icon error -title {计算失败} -message $result
        return
    }
    if {![dict get $result complete]} {
        set ::rf::progress [dict get $result progress]
        set ::rf::status [format {电势波形计算中… %.0f%%} $::rf::progress]
        set ::rf::job [after 1 ::rf::spatial_step]
        return
    }
    dict set result revision $::rf::revision
    set snap [dict get $result snapshot]
    dict set result note [format {单 m 电势相干重建 · f=%.6g MHz，m=%d，k=[%g,%g] rad/m，φ=0。显示 Im Φ（V），库仑规范、外壁 Φ=0；泊松后向误差 %.2g。请检查波数范围、步长和观察窗口收敛。} [dict get $snap frequency_mhz] [dict get $snap selected_m] [dict get $snap kmin] [dict get $snap kmax] [dict get $result potential_backward_error]]
    set ::rf::cache(waveform) $result
    set ::rf::progress 100
    set ::rf::status {电势波形计算完成 · 单 m、扫描 k_z 带相干重建}
    show_result
    finish
}
# 同时撤销 Tcl 排队回调并释放两类原生任务，停止不提交半成品结果。
proc ::rf::stop {} {
    if {!$::rf::busy} {return}
    if {$::rf::job ne {}} {after cancel $::rf::job}
    catch {rf_spatial cancel}
    catch {rf_full cancel}
    set ::rf::status {已停止 · 保留上一份完整结果}
    set ::rf::progress 0
    finish
}
# 从当前栏目缓存恢复图示目录、指标及快照；空栏目必须清掉上一栏目的图例。
proc ::rf::show_result {} {
    variable current
    set ::rf::hover {}
    set ::rf::metric1 —;set ::rf::metric2 —;set ::rf::metric3 —
    if {![info exists ::rf::cache($::rf::mode)]} {
        grid remove .body.main.metrics
        set current {}
        set ::rf::plots {}
        chart_choices [expected_charts $::rf::mode]
        set ::rf::caption {图表将显示实际计算数据。修改参数后，已有结果会保留为快照。}
        foreach w [winfo children .body.main.legend] {destroy $w}
        mark_snapshot
        redraw
        return
    }
    set current $::rf::cache($::rf::mode)
    grid .body.main.metrics
    set ::rf::plots [dict get $current charts]
    set names {}
    foreach c $::rf::plots {lappend names [dict get $c title]}
    chart_choices $names
    set ::rf::caption [dict get $current note]
    set ::rf::metric1 "[format %.0f [dict get $current count]] 点"
    set ::rf::metric2 [format {m = %s} [dict get $current snapshot selected_m]]
    set ::rf::metric3 [format {%g MHz} [dict get $current snapshot frequency_mhz]]
    if {[dict exists $current peak]} {set ::rf::metric2 [format {峰值 %.5g V} [dict get $current peak]]}
    if {[dict exists $current absorbed]} {set ::rf::metric3 [format {吸收 %.4g W m} [dict get $current absorbed]]}
    if {$::rf::mode eq {full}} {
        set snap [dict get $current snapshot]
        set ::rf::metric1 [format {m = %d…%d} [dict get $snap space.m_min] [dict get $snap space.m_max]]
        set ::rf::metric2 [format {φ = %.5g rad} [dict get $snap space.phi]]
        set ::rf::metric3 [format {窗口吸收 %.5g W} [dict get $current window_power]]
    }
    mark_snapshot
    select_chart
}
# 图例勾选只改变显示；热图分量和群速度分量均读取同一次原生求解的数据。
proc ::rf::select_chart {} {
    foreach key [array names ::rf::enabled] {unset ::rf::enabled($key)}
    foreach w [winfo children .body.main.legend] {destroy $w}
    set i [lsearch -exact $::rf::chart_names $::rf::chart_name]
    if {$i>=0 && $i<[llength $::rf::plots]} {
        set chart [lindex $::rf::plots $i]
        if {$::rf::mode eq {branches}} {
            if {[dict get $chart family] eq {rays}} {
                set ::rf::metric1 "[format %.0f [dict get $::rf::current ray_count]] 条参考射线"
            } else {set ::rf::metric1 "[format %.0f [dict get $::rf::current count]] 个径向点"}
        }
        if {$::rf::mode in {vacuum plasma}} {
            set title [dict get $chart title]
            set unit [chart_value_unit $chart]
            if {$title eq {电场幅值}} {
                set peak [dict get $::rf::current peak]
            } else {
                set peak 0.0
                foreach s [dict get $chart series] {foreach y [dict get $s y] {if {$y ne {}} {set peak [expr {max($peak,abs($y))}]}}}
            }
            set ::rf::metric2 [format {%s %.4g %s} [expr {$title eq {轴向能流密度} ? {最大绝对值} : {峰值}}] $peak $unit]
        }
        set ::rf::caption [dict get $::rf::current note]
        if {[dict exists $chart note]} {append ::rf::caption "\n" [dict get $chart note]}
        if {[dict exists $chart kind] && [dict get $chart kind] eq {heatmap}} {
            if {[dict exists $chart components]} {
                if {![dict exists $chart components $::rf::heat_component]} {set ::rf::heat_component 总幅值}
                dict set chart matrix [dict get $chart components $::rf::heat_component]
                lset ::rf::plots $i $chart
                set j 0
                foreach part [dict keys [dict get $chart components]] {
                    ttk::radiobutton .body.main.legend.c$j -text $part -value $part -variable ::rf::heat_component -command ::rf::select_chart -style Option.TRadiobutton
                    place .body.main.legend.c$j -x 0 -y 0
                    incr j
                }
                append ::rf::caption "\n当前显示：$::rf::heat_component。"
            }
            layout_option_buttons .body.main.legend [winfo width .body.main.legend]
            if {[dict exists $chart signed] && [dict get $chart signed]} {
                append ::rf::caption "\n颜色保留正负号：蓝色为负、白色为零、红色为正，使用对称线性色标。"
            } else {append ::rf::caption "\n颜色为对数幅值（4 个数量级）；径向与横轴显示会按画布分辨率抽样。"}
            redraw
            return
        }
        if {[dict exists $chart kind] && [dict get $chart kind] eq {waterfall}} {
            set j 0
            foreach value {0.5 1 2 4} {
                ttk::radiobutton .body.main.legend.g$j -text "起伏 ×$value" -value $value -variable ::rf::waterfall_scale -command ::rf::redraw -style Option.TRadiobutton
                place .body.main.legend.g$j -x 0 -y 0
                incr j
            }
            layout_option_buttons .body.main.legend [winfo width .body.main.legend]
            redraw
            return
        }
        if {[dict exists $chart velocity_group]} {
            set component 0
            foreach label {{径向 r} {方位 φ} {轴向 z}} {
                set ::rf::velocity_component($component) 1
                ttk::checkbutton .body.main.legend.v$component -text $label -variable ::rf::velocity_component($component) -command ::rf::velocity_selection -style Legend.TCheckbutton
                place .body.main.legend.v$component -x 0 -y 0
                incr component
            }
            set j 0
            foreach s [dict get $chart series] {
                set ::rf::enabled($j) 1
                if {[dict get $s component]==0} {
                    set branch [dict get $s branch]
                    set ::rf::velocity_branch($branch) 1
                    ttk::style configure Branch$branch.Legend.TCheckbutton -foreground [dict get $s color]
                    ttk::checkbutton .body.main.legend.b$branch -text [dict get $s branch_label] -variable ::rf::velocity_branch($branch) -command ::rf::velocity_selection -style Branch$branch.Legend.TCheckbutton
                    place .body.main.legend.b$branch -x 0 -y 0
                }
                incr j
            }
        } else {
            set j 0
            foreach s [dict get $chart series] {
                set ::rf::enabled($j) 1
                ttk::style configure Curve$j.Legend.TCheckbutton -foreground [dict get $s color]
                ttk::checkbutton .body.main.legend.c$j -text [dict get $s label] -variable ::rf::enabled($j) -command ::rf::redraw -style Curve$j.Legend.TCheckbutton
                place .body.main.legend.c$j -x 0 -y 0
                incr j
            }
        }
    }
    layout_option_buttons .body.main.legend [winfo width .body.main.legend]
    redraw
}
# 群速度可见性是分量开关与分支开关的交集，不重新计算速度。
proc ::rf::velocity_selection {} {
    set i [lsearch -exact $::rf::chart_names $::rf::chart_name]
    set j 0
    foreach line [dict get [lindex $::rf::plots $i] series] {
        set component [dict get $line component]
        set branch [dict get $line branch]
        set ::rf::enabled($j) [expr {$::rf::velocity_component($component) && $::rf::velocity_branch($branch)}]
        incr j
    }
    redraw
}
# 仅用于人眼显示的紧凑格式；导出与内部样本仍保留原始精度。
proc ::rf::fmt {v} {
    if {$v==0} {return 0}
    if {abs($v)<0.001 || abs($v)>=100000} {return [format %.2e $v]}
    return [format %.4g $v]
}
# 按当前可见曲线确定画布范围；缺失点断线，禁止跨材料界面或失败切片插值连接。
proc ::rf::redraw {} {
    set c .body.main.plot
    if {![winfo exists $c]} {return}
    $c delete all
    set w [winfo width $c];set h [winfo height $c]
    $c configure -scrollregion [list 0 0 $w $h]
    $c xview moveto 0;$c yview moveto 0
    if {$w<100 || $h<100} {return}
    if {$::rf::current eq {}} {
        $c create oval [expr {$w/2-28}] [expr {$h/2-80}] [expr {$w/2+28}] [expr {$h/2-24}] -outline #cbd5e1 -width 2
        $c create line [expr {$w/2-17}] [expr {$h/2-52}] [expr {$w/2+17}] [expr {$h/2-52}] -fill #94a3b8 -width 2
        $c create line [expr {$w/2}] [expr {$h/2-69}] [expr {$w/2}] [expr {$h/2-35}] -fill #94a3b8 -width 2
        $c create text [expr {$w/2}] [expr {$h/2+8}] -text {准备好后，开始计算} -font RFHeading -fill #334155
        $c create text [expr {$w/2}] [expr {$h/2+43}] -text "左侧设置参数 · 中间查看模型\n点击“开始计算”后在此查看结果" -justify center -font RFSmall -fill #64748b
        return
    }
    set index [lsearch -exact $::rf::chart_names $::rf::chart_name]
    set chart [lindex $::rf::plots $index]
    if {$chart eq {}} {return}
    if {[dict exists $chart kind] && [dict get $chart kind] eq {heatmap}} {draw_heatmap $c $chart $w $h;return}
    if {[dict exists $chart kind] && [dict get $chart kind] eq {waterfall}} {draw_waterfall $c $chart $w $h;return}
    if {[dict exists $chart kind] && [dict get $chart kind] eq {branch_status}} {draw_branch_status $c $chart $w $h;return}
    set xs [dict get $chart x]
    set xmin [lindex $xs 0];set xmax [lindex $xs end]
    if {[dict exists $chart parametric]} {
        set xmin 0.;set xmax 0.;set j 0
        foreach s [dict get $chart series] {
            if {$::rf::enabled($j)} {foreach x [dict get $s x] {if {$x ne {}} {set xmin [expr {min($xmin,$x)}];set xmax [expr {max($xmax,$x)}]}}}
            incr j
        }
    }
    if {$xmax==$xmin} {set xmax [expr {$xmin+1}]}
    set ymin 0.0;set ymax 0.0;set j 0
    foreach s [dict get $chart series] {
        if {$::rf::enabled($j)} {foreach y [dict get $s y] {if {$y ne {}} {set ymin [expr {min($ymin,$y)}];set ymax [expr {max($ymax,$y)}]}}}
        incr j
    }
    if {$ymin==$ymax} {set ymax [expr {$ymin+1}]}
    set pad [expr {($ymax-$ymin)*0.08}]
    set ymax [expr {$ymax+$pad}]
    if {$ymin<0} {set ymin [expr {$ymin-$pad}]}
    set left 76;set top 32;set right [expr {$w-18}]
    lassign [x_axis_layout $c $left $right $h [dict get $chart xlabel]] bottom
    set ::rf::plot_bounds [list $left $top $right $bottom $xmin $xmax $ymin $ymax]
    $c create text $left 12 -text [dict get $chart ylabel] -anchor w -fill #475569 -font RFSmall
    for {set i 0} {$i<=4} {incr i} {
        set y [expr {$bottom-($bottom-$top)*$i/4.0}]
        $c create line $left $y $right $y -fill #e8edf3
        $c create text [expr {$left-12}] $y -text [fmt [expr {$ymin+($ymax-$ymin)*$i/4.0}]] -anchor e -fill #64748b -font RFSmall
    }
    foreach tick [x_axis_ticks $left $right $xmin $xmax] {
        set x [lindex $tick 0]
        $c create line $x $top $x $bottom -fill #f1f5f9
    }
    $c create line $left $top $left $bottom $right $bottom -fill #cbd5e1
    draw_x_axis $c $left $right $bottom $xmin $xmax [dict get $chart xlabel]
    set j 0
    foreach s [dict get $chart series] {
        if {$::rf::enabled($j)} {
            set points {};set times {}
            set sx [expr {[dict exists $s x] ? [dict get $s x] : $xs}]
            set ts [expr {[dict exists $s time] ? [dict get $s time] : {}}]
            foreach x $sx y [dict get $s y] t $ts {
                if {$y eq {} || $x eq {}} {draw_line $c $points [dict get $s color] $times [expr {[dict exists $s dash] ? [dict get $s dash] : {}}];set points {};set times {};continue}
                lappend points [expr {$left+($x-$xmin)/($xmax-$xmin)*($right-$left)}] [expr {$bottom-($y-$ymin)/($ymax-$ymin)*($bottom-$top)}]
                if {$t ne {}} {lappend times $t}
            }
            draw_line $c $points [dict get $s color] $times [expr {[dict exists $s dash] ? [dict get $s dash] : {}}]
        }
        incr j
    }
}
# 连接同一连续有效区间；孤立样本画点，射线箭头按时间递增方向绘制。
proc ::rf::draw_line {c points color {times {}} {dash {}}} {
    if {[llength $points]>=4} {$c create line $points -fill $color -width 2 -capstyle round -joinstyle round -dash $dash}
    if {[llength $points]==2} {lassign $points x y;$c create oval [expr {$x-2}] [expr {$y-2}] [expr {$x+2}] [expr {$y+2}] -fill $color -outline $color}
    set n [llength $times]
    if {$n>=2 && [llength $points]==2*$n} {
        set lo [expr {max(0,$n/2-max(1,$n/12))}];set hi [expr {min($n-1,$n/2+max(1,$n/12))}]
        set arrow [lrange $points [expr {2*$lo}] [expr {2*$hi+1}]]
        if {[lindex $times $hi]<[lindex $times $lo]} {
            set reverse {};foreach {x y} $arrow {set reverse [linsert $reverse 0 $x $y]};set arrow $reverse
        }
        if {[lindex $times $hi]!=[lindex $times $lo]} {$c create line $arrow -fill $color -width 2 -arrow last -arrowshape {7 9 3} -tags ray_direction}
    }
}
# 把屏幕指针反算到当前图的原始样本；报告实际物理坐标、单位和失效状态。
proc ::rf::inspect {px py} {
    set c .body.main.plot
    $c delete cursor
    if {$::rf::current eq {} || ![info exists ::rf::plot_bounds]} {return}
    lassign $::rf::plot_bounds left top right bottom xmin xmax ymin ymax
    if {$px<$left || $px>$right || $py<$top || $py>$bottom} {set ::rf::hover {};return}
    set i [lsearch -exact $::rf::chart_names $::rf::chart_name]
    set chart [lindex $::rf::plots $i]
    if {[dict exists $chart kind] && [dict get $chart kind] eq {heatmap}} {inspect_heat $chart $px $py;return}
    if {[dict exists $chart kind] && [dict get $chart kind] eq {waterfall}} {inspect_waterfall $chart $px $py;return}
    if {[dict exists $chart kind] && [dict get $chart kind] eq {branch_status}} {inspect_branch_status $chart $px $py;return}
    if {[dict exists $chart parametric]} {inspect_ray_projection $chart $px $py;return}
    set xs [dict get $chart x]
    set target [expr {$xmin+double($px-$left)/($right-$left)*($xmax-$xmin)}]
    set index 0;set nearest Inf;set j 0
    foreach value $xs {set delta [expr {abs($value-$target)}];if {$delta<=$nearest} {set nearest $delta;set index $j};incr j}
    set x [lindex $xs $index]
    set xx [expr {$left+($x-$xmin)/($xmax-$xmin)*($right-$left)}]
    $c create line $xx $top $xx $bottom -fill #94a3b8 -dash {3 3} -tags cursor
    set text "[dict get $chart xlabel] = [fmt $x]"
    set unit [chart_value_unit $chart]
    set j 0
    foreach s [dict get $chart series] {
        if {$::rf::enabled($j)} {
            set value [lindex [dict get $s y] $index]
            append text "    [dict get $s label]：[expr {$value eq {} ? {无效点} : [fmt $value]}] $unit"
            if {[dict exists $s wave_names]} {append text "（局部：[lindex [dict get $s wave_names] $index]）"}
        }
        incr j
    }
    set ::rf::hover $text
}
# Geometry is a persistent middle pane, independent of result caches and solvers.
# 合并连续输入事件以减少重复预览，模型更新与求场缓存相互独立。
proc ::rf::schedule_model {} {
    if {![winfo exists .body.model.canvas]} {return}
    if {$::rf::model_job ne {}} {after cancel $::rf::model_job}
    set ::rf::model_status {正在更新模型…}
    set ::rf::model_job [after 180 ::rf::refresh_model]
}
# 未提交参数只用于预览；失败返回 0 并保留旧模型，调用者不能把旧模型当作新输入导出。
proc ::rf::refresh_model {} {
    if {$::rf::model_job ne {}} {after cancel $::rf::model_job}
    set ::rf::model_job {}
    if {[catch {rf_preview [array get ::rf::cfg]} result]} {
        set ::rf::model_status "参数待修正，保留上次有效模型。\n$result"
        redraw_model
        return 0
    }
    set ::rf::model $result
    set snap [dict get $result snapshot]
    set ::rf::model_type "$::rf::choice(type)天线 · 轴向无限长"
    set ::rf::model_info [format {天线半径 %g m · 容器半径 %g m
天线长度 %g m · 电流带宽 %g m} [dict get $snap antenna_radius] [dict get $snap radius] [dict get $snap antenna_length] [dict get $snap band_width]]
    switch [dict get $snap type] {
        13 {set detail [format {%d 环 · 轴向间距 %g m} [dict get $snap elements] [dict get $snap spacing]]}
        14 {set detail [format {内半径 %g m · %g 匝} [dict get $snap inner_radius] [dict get $snap turns]]}
        15 {set detail [format {%d 个半径 · 间隙 %g° · q=%d} [dict get $snap elements] [dict get $snap gap_angle_deg] [dict get $snap source_mode]]}
        16 {set detail [format {%d 腿 · 长度 %g m · q=%d} [dict get $snap elements] [dict get $snap antenna_length] [dict get $snap source_mode]]}
        17 {set detail [format {%d 横段 · 节距 %g m · β=%g rad/m} [dict get $snap elements] [dict get $snap spacing] [dict get $snap current_phase_gradient]]}
    }
    if {[dict get $snap type]>=13} {
        set ::rf::model_info [format {参考半径 %g m · 容器半径 %g m
%s} [dict get $snap antenna_radius] [dict get $snap radius] $detail]
    }
    set ::rf::model_status {拖动旋转 · 滚轮缩放}
    redraw_model
    return 1
}
# 将米制 Cartesian 坐标作相机旋转投影；缩放和像素平移由调用者处理。
proc ::rf::project_point {x y z} {
    set a [expr {$::rf::angle*acos(-1)/180}]
    set e [expr {$::rf::elevation*acos(-1)/180}]
    return [list [expr {$x*cos($a)-$y*sin($a)}] [expr {sin($e)*($x*sin($a)+$y*cos($a))-cos($e)*$z}]]
}
# 保存按下时的相机角度，拖动过程中始终以同一手势起点计算。
proc ::rf::model_drag_start {x y} {set ::rf::model_drag [list $x $y $::rf::angle $::rf::elevation]}
# 拖动只改变相机，不改变天线尺寸或源电流参数。
proc ::rf::model_drag_move {x y} {
    if {![info exists ::rf::model_drag]} {return}
    lassign $::rf::model_drag sx sy yaw pitch
    set ::rf::angle [expr {$yaw+($x-$sx)*0.6}]
    set ::rf::elevation [expr {max(-85,min(85,$pitch+($y-$sy)*0.4))}]
    redraw_model
}
# 恢复默认观察角和视觉缩放，不重置用户物理参数。
proc ::rf::reset_model {} {
    set ::rf::angle 35;set ::rf::elevation 22;set ::rf::zoom 1.0
    redraw_model
}
# 视觉缩放有界，防止模型消失或产生不可表示的投影坐标。
proc ::rf::zoom_model {factor} {
    set ::rf::zoom [expr {max(0.5,min(2.5,$::rf::zoom*$factor))}]
    redraw_model
}
# 柱体显示长度仅为视窗截取；±∞ 标记和 r/z/φ 小轴不引入物理端板。
proc ::rf::redraw_model {} {
    set c .body.model.canvas
    if {![winfo exists $c]} {return}
    $c delete all
    set w [winfo width $c];set h [winfo height $c]
    if {$w<50 || $h<50 || $::rf::model eq {}} {return}
    set r $::rf::model
    set radius [dict get $r radius]
    set snap [dict get $r snapshot]
    # Use one physical scale for x, y and z. The reference camera shows 5 m
    # per viewport height at unit zoom; pane width and vessel radius do not
    # rescale the geometry. Crop the axial drawing to 64% of the pane height.
    set e [expr {$::rf::elevation*acos(-1)/180}]
    set scale [expr {$::rf::zoom*$h/$::rf::model_view_height_m}]
    set image_height [expr {int(round(.64*$h))}]
    set image_y [expr {($h-$image_height)/2.0}]
    set cx [expr {$w/2.0}];set cy [expr {$image_height/2.0}]
    set length [expr {2*($cy+$scale*$radius*abs(sin($e))+4)/($scale*abs(cos($e)))}]
    draw_model_solids $c $r $snap $cx $cy $scale $length $image_height $image_y
    # Metadata overlays the drawing; it must not reserve space above or below
    # an axially unbounded cylinder. Canvas text leaves the background visible.
    set title [$c create text 12 12 -anchor nw -text $::rf::model_type -font RFGroup -fill #2563eb -width [expr {$w-24}] -tags model_overlay]
    set y [expr {[lindex [$c bbox $title] 3]+8}]
    set info [$c create text 12 $y -anchor nw -text $::rf::model_info -font RFSmall -fill #526174 -width [expr {$w-24}] -tags model_overlay]
    set y [expr {[lindex [$c bbox $info] 3]+8}];set x 12
    foreach text {天线 容器 等离子体} color {#cb7515 #729caf #287ce5} show [list 1 $::rf::show_vessel [dict get $snap plasma_enabled]] {
        if {!$show} {continue}
        set id [$c create text $x $y -anchor nw -text "● $text" -font RFSmall -fill $color -tags model_overlay]
        set x [expr {[lindex [$c bbox $id] 2]+12}]
    }
    set status_y [expr {$h-[winfo height .body.model.tools]-16}]
    set status [$c create text 12 $status_y -anchor sw -text $::rf::model_status -font RFSmall -fill #526174 -width [expr {$w-24}] -tags {model_overlay model_status}]
    # Stack the coordinate inset above the actual text bounds, including
    # multiline validation messages, and keep both above the floating buttons.
    model_cylindrical_axes $c 12 [expr {[lindex [$c bbox $status] 1]-12}]
    $c create text $cx [expr {$image_y-12}] -text {+∞} -font RFParam -fill #526174 -tags infinite_label
    $c create text $cx [expr {$image_y+$image_height+12}] -text {−∞} -font RFParam -fill #526174 -tags infinite_label
}
# 使用明确参数快照重新导出米制中心线，屏幕旋转和透明度不进入模型文件。
proc ::rf::export_model {} {
    if {$::rf::model eq {} || $::rf::busy} {return}
    if {![apply]} {return}
    if {![refresh_model]} {set ::rf::status $::rf::model_status;return}
    set p [tk_getSaveFile -parent . -title {导出天线模型} -defaultextension .obj -filetypes {{{OBJ 中心线} {.obj}}}]
    if {$p eq {}} {return}
    if {[catch {rf_export_obj [dict get $::rf::model snapshot] $p} msg]} {tk_messageBox -parent . -icon error -message $msg;return}
    set ::rf::status "模型已导出：[file tail $p]"
}
# 转义 SVG 文本内容；物理标签中的比较符号不能被解释成 XML 标签。
proc ::rf::xml {text} {return [string map {& &amp; < &lt; > &gt; \" &quot;} $text]}
# 导出当前画布及可见图例，使用相同样本、断点、线型和时间箭头。
proc ::rf::svg_text {} {
    set c .body.main.plot
    $c delete cursor
    set w [winfo width $c];set h [winfo height $c]
    set index [lsearch -exact $::rf::chart_names $::rf::chart_name]
    set chart [lindex $::rf::plots $index]
    set velocity_legend {}
    set branch_legend [expr {$::rf::mode eq {branches}}]
    set export_height $h
    if {[dict exists $chart velocity_group]} {
        foreach component {0 1 2} label {{径向 r · 实线} {方位 φ · 虚线} {轴向 z · 点线}} {
            if {$::rf::velocity_component($component)} {lappend velocity_legend [list $label #475569]}
        }
        foreach line [dict get $chart series] {
            if {[dict get $line component]==0 && $::rf::velocity_branch([dict get $line branch])} {
                lappend velocity_legend [list [dict get $line branch_label] [dict get $line color]]
            }
        }
    } elseif {$branch_legend} {
        set j 0
        foreach line [dict get $chart series] {
            if {$::rf::enabled($j)} {lappend velocity_legend [list [dict get $line label] [dict get $line color]]}
            incr j
        }
    }
    if {[llength $velocity_legend]} {
        set legend_width 180
        foreach entry $velocity_legend {set legend_width [expr {max($legend_width,[font measure RFSmall [lindex $entry 0]]+20)}]}
        set legend_columns [expr {max(1,int(($w-40)/$legend_width))}]
        set export_height [expr {$h+16+20*int(ceil([llength $velocity_legend]/double($legend_columns)))}]
    }
    set svg "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 $w $export_height\"><rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n"
    append svg "<title>[xml $::rf::chart_name]</title>\n"
    append svg "<desc>[xml [dict get $::rf::current snapshot]]</desc>\n"
    foreach id [$c find all] {
        set type [$c type $id];set coords [$c coords $id]
        switch -- $type {
            line {
                set points {}
                foreach {x y} $coords {lappend points "$x,$y"}
                set dash [$c itemcget $id -dash]
                set dash_attribute [expr {$dash eq {} ? {} : " stroke-dasharray=\"[join $dash ,]\""}]
                append svg "<polyline points=\"[join $points { }]\" fill=\"none\" stroke=\"[$c itemcget $id -fill]\" stroke-width=\"[$c itemcget $id -width]\"$dash_attribute/>\n"
                if {[$c itemcget $id -arrow] eq {last}} {
                    set end [expr {[llength $coords]-2}];set tx [lindex $coords $end];set ty [lindex $coords [expr {$end+1}]]
                    for {set j [expr {$end-2}]} {$j>=0} {incr j -2} {
                        set dx [expr {$tx-[lindex $coords $j]}];set dy [expr {$ty-[lindex $coords [expr {$j+1}]]}]
                        set len [expr {hypot($dx,$dy)}]
                        if {$len>1e-6} {
                            set dx [expr {$dx/$len}];set dy [expr {$dy/$len}]
                            set ax [expr {$tx-9*$dx+3*$dy}];set ay [expr {$ty-9*$dy-3*$dx}]
                            set bx [expr {$tx-9*$dx-3*$dy}];set by [expr {$ty-9*$dy+3*$dx}]
                            append svg "<polygon points=\"$tx,$ty $ax,$ay $bx,$by\" fill=\"[$c itemcget $id -fill]\"/>\n"
                            break
                        }
                    }
                }
            }
            text {
                lassign $coords x y
                set anchor [$c itemcget $id -anchor]
                set alignment middle
                if {$anchor in {w nw sw}} {set alignment start}
                if {$anchor in {e ne se}} {set alignment end}
                if {$anchor in {n nw ne}} {
                    set y [expr {$y+12}]
                } elseif {$anchor ni {s sw se}} {set y [expr {$y+4}]}
                append svg "<text x=\"$x\" y=\"$y\" text-anchor=\"$alignment\" font-family=\"sans-serif\" font-size=\"12\" fill=\"[$c itemcget $id -fill]\">[xml [$c itemcget $id -text]]</text>\n"
            }
            rectangle {
                lassign $coords x y xx yy
                set fill [$c itemcget $id -fill];if {$fill eq {}} {set fill none}
                set stroke [$c itemcget $id -outline];if {$stroke eq {}} {set stroke none}
                append svg "<rect x=\"$x\" y=\"$y\" width=\"[expr {$xx-$x}]\" height=\"[expr {$yy-$y}]\" fill=\"$fill\" stroke=\"$stroke\"/>\n"
            }
            oval {
                # 断点两侧只剩一个有效样本时 draw_line 画圆点；导出也须保留，
                # 不能因为该段没有可连接的线，就把有效计算结果从 SVG 中丢掉。
                lassign $coords x y xx yy
                set fill [$c itemcget $id -fill];if {$fill eq {}} {set fill none}
                set stroke [$c itemcget $id -outline];if {$stroke eq {}} {set stroke none}
                append svg "<ellipse cx=\"[expr {($x+$xx)/2.}]\" cy=\"[expr {($y+$yy)/2.}]\" rx=\"[expr {abs($xx-$x)/2.}]\" ry=\"[expr {abs($yy-$y)/2.}]\" fill=\"$fill\" stroke=\"$stroke\" stroke-width=\"[$c itemcget $id -width]\"/>\n"
            }
        }
    }
    # Preserve series names, which are native checkbuttons outside the canvas.
    if {[dict exists $chart velocity_group] || $branch_legend} {
        append svg "<g id=\"velocity-legend\">\n"
        set j 0
        foreach entry $velocity_legend {
            lassign $entry label color
            set x [expr {20+($j%$legend_columns)*$legend_width}]
            set y [expr {$h+20+($j/$legend_columns)*20}]
            append svg "<text x=\"$x\" y=\"$y\" font-family=\"sans-serif\" font-size=\"12\" fill=\"$color\">[xml $label]</text>\n"
            incr j
        }
        append svg "</g>\n"
    } elseif {$::rf::mode ne {geometry}} {
        set index [lsearch -exact $::rf::chart_names $::rf::chart_name]
        set chart [lindex $::rf::plots $index]
        set x 100;set j 0
        foreach s [expr {[dict exists $chart series] && (![dict exists $chart kind] || [dict get $chart kind] ne {branch_status}) ? [dict get $chart series] : {}}] {
            if {$::rf::enabled($j)} {
                append svg "<text x=\"$x\" y=\"38\" font-family=\"sans-serif\" font-size=\"12\" fill=\"[dict get $s color]\">[xml [dict get $s label]]</text>\n"
                incr x 120
            }
            incr j
        }
    }
    append svg {</svg>}
    return $svg
}
# CSV 保留复数/原始数值，SVG 保留当前视图；写入失败必须报告且关闭文件句柄。
proc ::rf::export_result {{kind csv}} {
    if {![info exists ::rf::cache($::rf::mode)] || $::rf::busy} {return}
    set r $::rf::cache($::rf::mode)
    if {$::rf::mode eq {geometry} && $kind eq {csv}} {set kind obj}
    set p [tk_getSaveFile -parent . -title {导出计算结果} -defaultextension .$kind -filetypes [list [list [string toupper $kind] .$kind]]]
    if {$p eq {}} {return}
    if {[catch {
        if {$kind eq {obj}} {rf_export_obj [dict get $r snapshot] $p} else {
            set index [lsearch -exact $::rf::chart_names $::rf::chart_name]
            set data [expr {$kind eq {svg} ? [svg_text] : [chart_csv [lindex $::rf::plots $index] [dict get $r csv]]}]
            set f [open $p w]
            if {[catch {fconfigure $f -encoding utf-8 -translation lf;puts -nonewline $f $data} msg]} {close $f;error $msg}
            close $f
        }
    } msg]} {tk_messageBox -parent . -icon error -message $msg;return}
    set ::rf::status "已导出：[file tail $p]"
}

# Restrained, neutral theme with one action color. Grid owns all window sizing.
wm title . {Cold-Cylinder 无限长圆柱体冷等离子体工作模型}
wm geometry . 1600x820
wm minsize . 1200 700
wm protocol . WM_DELETE_WINDOW ::rf::close
# 从当前系统真实字体族中选择，保持中文界面和数值输入各自清晰，缺字库时有明确回退。
proc ::rf::available_font {candidates} {
    set available [font families]
    foreach family $candidates {
        set index [lsearch -nocase -exact $available $family]
        if {$index>=0} {return [lindex $available $index]}
    }
    return [font actual TkDefaultFont -family]
}
if {[tk windowingsystem] eq {win32}} {
    set ::rf::font_family [::rf::available_font {{Microsoft YaHei UI} {Microsoft YaHei} DengXian}]
    set ::rf::value_font_family [::rf::available_font {{Segoe UI} Arial}]
} else {
    set ::rf::font_family [::rf::available_font {{PingFang SC}}]
    set ::rf::value_font_family [::rf::available_font {{Helvetica Neue} Arial}]
}
# 保留原字号层级；数值用20号的拉丁无衬线字体，中文选择框继续用20号界面字体。
font create RFBody -family $::rf::font_family -size 16
font create RFSmall -family $::rf::font_family -size 14
font create RFParam -family $::rf::font_family -size 18
font create RFSidebar -family $::rf::font_family -size 20
font create RFValue -family $::rf::value_font_family -size 20
font create RFCategory -family $::rf::font_family -size 16 -weight bold
font create RFParamGroup -family $::rf::font_family -size 18 -weight bold
font create RFHeading -family $::rf::font_family -size 22 -weight bold
font create RFGroup -family $::rf::font_family -size 16 -weight bold
option add *Font RFBody
option add *TCombobox*Listbox.font RFSidebar
ttk::style theme use clam
ttk::style configure . -font RFBody -background #f3f5f8 -foreground #24344b
ttk::style configure TFrame -background #f3f5f8
ttk::style configure White.TFrame -background #ffffff
ttk::style configure TLabel -background #f3f5f8
ttk::style configure White.TLabel -background #ffffff
ttk::style configure Subtle.TLabel -background #ffffff -foreground #718096 -font RFSmall
ttk::style configure Sidebar.Subtle.TLabel -font RFSidebar
ttk::style configure Heading.TLabel -background #ffffff -font RFHeading -foreground #172b4d
ttk::style configure Group.TLabel -background #ffffff -foreground #2563eb -font RFGroup
ttk::style configure Param.TLabel -background #ffffff -foreground #334155 -font RFSidebar
ttk::style configure ParamGroup.TLabel -background #ffffff -foreground #526174 -font RFParamGroup
ttk::style configure TButton -width 0 -padding {12 7} -background #ffffff -bordercolor #d8e0ea -lightcolor #ffffff -darkcolor #ffffff -relief flat
ttk::style configure Toolbar.TButton -font RFParam -padding {14 7}
::rf::option_surface Option.surface 0
::rf::option_surface Category.surface 1
::rf::category_tab_surface
ttk::style layout Option.TRadiobutton {Option.surface -sticky nswe -children {Button.padding -sticky nswe -children {Button.label -sticky nswe}}}
ttk::style layout Category.Option.TRadiobutton {Category.surface -sticky nswe -children {Button.padding -sticky nswe -children {Button.label -sticky nswe}}}
ttk::style configure Option.TRadiobutton -font RFParam -padding {14 6} -foreground #526174 -anchor center
ttk::style map Option.TRadiobutton -foreground {disabled #a0aaba selected #1d4ed8 active #1d4ed8}
ttk::style configure Category.Option.TRadiobutton -font RFCategory -padding {16 7} -foreground #475569 -anchor center
ttk::style map Category.Option.TRadiobutton -foreground {disabled #a0aaba selected #ffffff active #1d4ed8}
ttk::style configure CategoryBar.TFrame -background #64748b
ttk::style layout FixedCategory.TRadiobutton {CategoryTab.surface -sticky nswe -children {Button.padding -sticky nswe -children {Button.label -sticky nswe}}}
ttk::style configure FixedCategory.TRadiobutton -font RFCategory -padding {4 6} -foreground #334155 -anchor center
ttk::style map FixedCategory.TRadiobutton -foreground {disabled #94a3b8 selected #ffffff active #1d4ed8}
ttk::style layout Toolbar.TButton {Option.surface -sticky nswe -children {Button.padding -sticky nswe -children {Button.label -sticky nswe}}}
ttk::style configure Toolbar.TButton -foreground #475569 -anchor center
ttk::style map Toolbar.TButton -foreground {disabled #a0aaba active #1d4ed8}
ttk::style configure Model.Toolbar.TButton -padding {8 5}
ttk::style element create Action.surface image [list ::rf::Category.surface_selected disabled ::rf::Option.surface_disabled pressed ::rf::Category.surface_selected_active active ::rf::Category.surface_selected_active] -border 10 -padding 0 -sticky news
ttk::style layout Compact.Accent.TButton {Action.surface -sticky nswe -children {Button.padding -sticky nswe -children {Button.label -sticky nswe}}}
ttk::style layout Toolbar.TMenubutton {Option.surface -sticky nswe -children {Menubutton.padding -sticky nswe -children {Menubutton.indicator -side right -sticky {} Menubutton.label -side left -sticky nswe}}}
ttk::style configure Toolbar.TMenubutton -font RFParam -padding {14 7} -width 0 -background #ffffff -foreground #475569
ttk::style layout Vertical.TScrollbar {Vertical.Scrollbar.trough -sticky ns -children {Vertical.Scrollbar.thumb -expand 1 -sticky nsew}}
ttk::style configure Vertical.TScrollbar -width 9 -background #cbd5e1 -troughcolor #f8fafc -borderwidth 0
ttk::style map TButton -background {active #edf2f7 disabled #f1f3f6} -foreground {disabled #a0aaba}
ttk::style configure Accent.TButton -background #2563eb -foreground #ffffff -bordercolor #2563eb -lightcolor #2563eb -darkcolor #2563eb
ttk::style configure Compact.Accent.TButton -font RFParam -padding {14 7}
ttk::style map Accent.TButton -background {disabled #c4d2ee active #1d4ed8} -foreground {disabled #ffffff active #ffffff}
ttk::style map Compact.Accent.TButton -foreground {disabled #a0aaba active #ffffff}
ttk::style configure TEntry -fieldbackground #ffffff -bordercolor #dbe2ec -padding {5 2} -font RFBody
ttk::style configure TCombobox -fieldbackground #ffffff -padding {5 2} -bordercolor #dbe2ec -arrowsize 12 -font RFBody
ttk::style map TCombobox -fieldbackground {readonly #ffffff disabled #f3f5f8} -foreground {disabled #9aa5b5}
ttk::style configure Param.TEntry -font RFValue -padding {6 3}
ttk::style configure Param.TCombobox -font RFSidebar -padding {6 3} -arrowsize 12
ttk::style configure TNotebook -background #ffffff -borderwidth 0 -tabmargins {4 0 4 0}
ttk::style configure TNotebook.Tab -font RFSidebar -padding {10 6} -width 0 -anchor center -background #f0f0f0 -bordercolor #c6c6c6
# Clam increases the selected tab's padding by default; clear every size change.
ttk::style map TNotebook.Tab -font {selected RFSidebar !selected RFSidebar} -padding {selected {10 6} !selected {10 6}} -expand {selected {0 0 0 0} !selected {0 0 0 0}} -background {selected #d4d4d4 !selected #f0f0f0} -foreground {selected #303030 !selected #606060} -lightcolor {selected #d4d4d4 !selected #f0f0f0} -darkcolor {selected #d4d4d4 !selected #f0f0f0}
ttk::style configure Advanced.TCheckbutton -background #ffffff -foreground #64748b -font RFSidebar
ttk::style configure TCheckbutton -background #ffffff
ttk::style configure Param.TCheckbutton -font RFParam
ttk::style configure Sidebar.TCheckbutton -font RFSidebar
ttk::style configure Legend.TCheckbutton -background #ffffff -font RFSmall
ttk::style configure TProgressbar -background #2563eb -troughcolor #e9eef5 -borderwidth 0
. configure -background #f3f5f8

ttk::frame .header -style White.TFrame -padding {14 8}
grid .header -row 0 -column 0 -sticky ew
ttk::button .header.open -text {打开项目} -command ::rf::open_project -style Toolbar.TButton
ttk::button .header.save -text {保存} -command ::rf::save_project -style Toolbar.TButton
ttk::menubutton .header.more -text {更多} -menu .header.more.menu -style Toolbar.TMenubutton
menu .header.more.menu -tearoff 0
.header.more.menu add command -label {另存为…} -command {::rf::save_project 1}
.header.more.menu add separator
set example_dir [file normalize [file join [file dirname [info nameofexecutable]] .. examples]]
set bundled_examples [file normalize [file join [file dirname [info nameofexecutable]] .. Resources examples]]
if {[file isdirectory $bundled_examples]} {set example_dir $bundled_examples}
# Windows 便携包把 examples 放在 exe 同目录；源码与 macOS 的原路径继续作为回退。
set portable_examples [file normalize [file join [file dirname [info nameofexecutable]] examples]]
if {[file isdirectory $portable_examples]} {set example_dir $portable_examples}
foreach path [lsort [glob -nocomplain -directory $example_dir *.rfproj]] {
    .header.more.menu add command -label "示例：[file rootname [file tail $path]]" -command [list ::rf::open_project $path]
}
ttk::button .header.run -text {开始计算} -style Compact.Accent.TButton -command ::rf::run
ttk::button .header.stop -text {停止} -style Toolbar.TButton -command ::rf::stop
ttk::button .header.export -text {数据 CSV} -style Toolbar.TButton -command ::rf::export_result
ttk::button .header.svg -text {图 SVG} -style Toolbar.TButton -command {::rf::export_result svg}
foreach widget {open save more run stop export svg} column {0 1 2 4 5 6 7} {
    grid .header.$widget -row 0 -column $column -sticky ns -padx {0 8}
}
grid columnconfigure .header 3 -weight 1 -minsize 24

ttk::frame .body -padding {14 4 14 10}
grid .body -row 1 -column 0 -sticky nsew
grid columnconfigure . 0 -weight 1
grid rowconfigure . 1 -weight 1
grid columnconfigure .body 1 -weight 2 -minsize 250
grid columnconfigure .body 2 -weight 3 -minsize 470
grid rowconfigure .body 0 -weight 1

ttk::frame .body.side -style White.TFrame -width 360
grid .body.side -row 0 -column 0 -sticky ns -padx {0 12}
grid propagate .body.side 0
grid rowconfigure .body.side 1 -weight 1
grid columnconfigure .body.side 0 -weight 1
ttk::label .body.side.title -text {参数输入} -style ParamGroup.TLabel -font RFParamGroup -padding {10 8 10 3}
grid .body.side.title -row 0 -column 0 -sticky w
ttk::notebook .body.side.tabs
grid .body.side.tabs -row 1 -column 0 -sticky nsew
set p [::rf::page antenna 天线]
::rf::group $p {类型与驱动}
::rf::field $p type {天线类型} {} {1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17} {{1 单带} {2 多带阵列} {3 梳状阵列} {4 单环} {5 螺旋} {6 单鞍} {7 双鞍形} {8 Nagoya III} {9 含回流线电流} {10 部分匝} {11 半Nagoya} {12 半匝阵列} {13 轴向多环} {14 平面螺旋} {15 分段多环} {16 鸟笼电流} {17 蛇形电流}}
foreach {key title unit} {frequency_mhz 频率 MHz current 峰值电流 A selected_m {角向模数 m} {} kz {轴向波数 k_z} rad/m} {::rf::field $p $key $title $unit}
::rf::field $p current_direction 电流方向 {} {1 -1} {正向 反向}
::rf::group $p {尺寸与网格}
foreach {key title unit} {radius 容器半径 m antenna_radius 天线半径 m antenna_length 天线长度 m width 天线宽度 m band_width 电流带宽 m turns 匝数 {} elements 阵列单元数 {} phase_step 阵列相位差 ° spacing 单元间距 m arc_angle_deg 圆弧角度 ° return_radius 回流半径 m inner_radius 内半径 m gap_angle_deg 分段间隙角 ° source_mode {电流谐波 q} {} current_phase_gradient 沿线相位梯度 rad/m radial_cells 径向网格数 {}} {::rf::field $p $key $title $unit}
::rf::field $p current_pattern 电流分布 {} {0 1} {余弦驻波 旋转相位}
::rf::field $p winding_direction 螺旋绕向 {} {1 -1} {正向 反向}
::rf::advanced_toggle $p antenna {高级：相位、位置、采样}
foreach {key title unit} {global_phase_deg 整体相位 ° azimuth_deg 方位角 ° axial_offset 轴向偏移 m band_samples 带宽采样数 {}} {::rf::field $p $key $title $unit}
set p [::rf::page medium 介质]
::rf::group $p {冷等离子体}
foreach {key title unit} {plasma.radius 等离子体半径 m plasma.density 中心电子密度 m⁻³ plasma.magnetic_field 背景磁场 T} {::rf::field $p $key $title $unit}
::rf::field $p plasma.profile 密度分布 {} {0 1 2} {均匀 抛物线 高斯}
::rf::field $p plasma.edge_ratio 边缘密度比
::rf::group $p {碰撞与离子}
::rf::field $p plasma.solve_mode 材料响应 {} {0 2} {有限碰撞 零碰撞极限}
foreach {key title unit} {plasma.electron_collision 电子碰撞频率 s⁻¹ plasma.ion_collision 离子碰撞频率 s⁻¹ plasma.ion_mass_u 离子质量 u plasma.ion_charge 离子电荷数 {}} {::rf::field $p $key $title $unit}
::rf::advanced_toggle $p medium {高级参数 · 碰撞边缘比例}
foreach {key title unit} {plasma.electron_collision_edge_ratio 电子边缘碰撞比 {} plasma.ion_collision_edge_ratio 离子边缘碰撞比 {}} {::rf::field $p $key $title $unit}
set p [::rf::page scan 扫描]
::rf::group $p {轴向波数扫描}
foreach {key title unit} {kmin 波数下限 rad/m kmax 波数上限 rad/m nk 波数采样数 {}} {::rf::field $p $key $title $unit}
ttk::label $p.tip -text "电流谱与波数扫描共用此范围。\n角向模数 m 使用“天线”页设置。\n扫描时可停止，失败切片会留空。" -style Sidebar.Subtle.TLabel -font RFSidebar -wraplength 310 -padding {0 6}
grid $p.tip -column 0 -columnspan 3 -sticky ew
::rf::group $p {全模式求和与空间观察}
foreach {key title unit} {space.m_min {m 下限} {} space.m_max {m 上限} {} space.phi {方位 φ} rad space.zmin {z 下限} m space.zmax {z 上限} m space.nz {z 点数} {} space.nr {r 点数} {}} {::rf::field $p $key $title $unit}
ttk::label $p.full_tip -text {等离子体总场按这里的 m 范围求和、k_z 范围积分；φ 选择 r-z 截面。电势波形自动加密 z 采样，并可在图上调整起伏倍率。请检查模式和网格收敛。} -style Sidebar.Subtle.TLabel -font RFSidebar -wraplength 310 -padding {0 6}
grid $p.full_tip -column 0 -columnspan 3 -sticky ew
set p [::rf::page ray 射线追踪]
::rf::group $p {发射位置与方向}
foreach {key title unit} {ray.r0 发射半径 m ray.z0 发射位置z m ray.phi0_deg 发射方位角 °} {::rf::field $p $key $title $unit}
::rf::field $p ray.direction 能量方向 {} {0 -1 1} {双向 向内 向外}
::rf::group $p {积分与停止条件}
foreach {key title unit} {ray.tolerance 相对容差 {} ray.max_step 最大步长 m ray.max_path 最大路径 m ray.max_steps 最大步数 {}} {::rf::field $p $key $title $unit}
ttk::label $p.ray_tip -text {各候选分支从此位置发射。频率 f、角向模数 m 和轴向波数 kz 使用“天线”页设置。路径上限不是端板；功率显示 P/P0。} -style Sidebar.Subtle.TLabel -font RFSidebar -wraplength 310 -padding {0 6}
grid $p.ray_tip -column 0 -columnspan 3 -sticky ew

ttk::frame .body.model -style White.TFrame -padding 0 -width 350
grid .body.model -row 0 -column 1 -sticky nsew -padx {0 12}
grid propagate .body.model 0
grid columnconfigure .body.model 0 -weight 1
grid rowconfigure .body.model 0 -weight 1
canvas .body.model.canvas -background #f8fafc -highlightthickness 0 -borderwidth 0 -width 280 -height 300 -cursor hand2
grid .body.model.canvas -row 0 -column 0 -sticky nsew
bind .body.model.canvas <Configure> {::rf::redraw_model}
bind .body.model.canvas <ButtonPress-1> {::rf::model_drag_start %x %y}
bind .body.model.canvas <B1-Motion> {::rf::model_drag_move %x %y}
bind .body.model.canvas <MouseWheel> {::rf::zoom_model [expr {%D>0 ? 1.1 : 0.9}];break}
bind .body.model.canvas <Button-4> {::rf::zoom_model 1.1;break}
bind .body.model.canvas <Button-5> {::rf::zoom_model 0.9;break}
# Floating controls do not change the full-height drawing area.
ttk::frame .body.model.tools -style White.TFrame -padding 2
place .body.model.tools -relx 1 -rely 1 -anchor se -x -8 -y -8
ttk::button .body.model.tools.reset -text {复位} -command ::rf::reset_model -style Model.Toolbar.TButton
ttk::button .body.model.tools.export -text {导出模型} -command ::rf::export_model -style Model.Toolbar.TButton
ttk::checkbutton .body.model.vessel -text {显示容器} -variable ::rf::show_vessel -command ::rf::redraw_model -style Param.TCheckbutton
grid .body.model.vessel -in .body.model.tools -row 0 -column 0 -sticky w -padx {0 4}
grid .body.model.tools.reset -row 0 -column 1 -padx {0 4}
grid .body.model.tools.export -row 0 -column 2 -sticky e

ttk::frame .body.main -style White.TFrame -padding {12 12 12 8} -width 620
grid .body.main -row 0 -column 2 -sticky nsew
grid propagate .body.main 0
grid columnconfigure .body.main 0 -weight 1
grid rowconfigure .body.main 3 -weight 1
ttk::frame .body.main.tools -style White.TFrame
grid .body.main.tools -row 0 -column 0 -sticky ew
ttk::frame .body.main.tools.head -style White.TFrame
grid .body.main.tools.head -row 0 -column 0 -sticky ew
grid columnconfigure .body.main.tools.head 1 -weight 1
ttk::label .body.main.tools.heading -text {运算结果} -style Group.TLabel -font RFGroup
grid .body.main.tools.heading -in .body.main.tools.head -row 0 -column 0 -sticky w -padx {0 10}
ttk::frame .body.main.tools.categories -style CategoryBar.TFrame -padding 2
grid .body.main.tools.categories -row 1 -column 0 -sticky ew -pady {8 4}
ttk::frame .body.main.tools.categories.tabs -style CategoryBar.TFrame
grid .body.main.tools.categories.tabs -row 0 -column 0 -sticky ew
grid columnconfigure .body.main.tools.categories 0 -weight 1
grid columnconfigure .body.main.tools 0 -weight 1
ttk::label .body.main.snapshot -textvariable ::rf::snapshot_note -style Subtle.TLabel -font RFSmall
grid .body.main.snapshot -in .body.main.tools.head -row 0 -column 1 -sticky w
ttk::frame .body.main.metrics -style White.TFrame
grid .body.main.metrics -row 1 -column 0 -sticky ew
foreach i {1 2 3} {
    ttk::label .body.main.metrics.m$i -textvariable ::rf::metric$i -style Subtle.TLabel -font RFSmall -padding {0 0 10 2}
    grid .body.main.metrics.m$i -row 0 -column [expr {$i-1}] -sticky w
    grid columnconfigure .body.main.metrics [expr {$i-1}] -weight 1
}
ttk::frame .body.main.view -style White.TFrame -width 1 -height 26
::rf::scroll_options .body.main.view .body.main.choices 88
grid .body.main.choices -row 2 -column 0 -sticky ew -pady {6 6}
ttk::label .body.main.hover -textvariable ::rf::hover -style Subtle.TLabel -font RFSmall -padding {0 1} -wraplength 680
grid .body.main.hover -row 5 -column 0 -sticky ew
canvas .body.main.plot -background #ffffff -highlightthickness 0 -width 600 -height 280
grid .body.main.plot -row 3 -column 0 -sticky nsew
bind .body.main.plot <Configure> {::rf::redraw}
bind .body.main.plot <Motion> {::rf::inspect %x %y}
bind .body.main.plot <Leave> {set ::rf::hover {}; .body.main.plot delete cursor}
ttk::frame .body.main.legend -style White.TFrame -padding {0 2}
grid .body.main.legend -row 4 -column 0 -sticky ew
bind .body.main.legend <Configure> {::rf::layout_option_buttons %W %w}
# Long branch diagnostics remain readable without taking space from the plot.
ttk::frame .body.main.notes -style White.TFrame
grid .body.main.notes -row 6 -column 0 -sticky ew
grid columnconfigure .body.main.notes 0 -weight 1
canvas .body.main.notes.c -background #ffffff -highlightthickness 0 -width 1 -height 24
ttk::scrollbar .body.main.notes.s -orient vertical -command {.body.main.notes.c yview}
.body.main.notes.c configure -yscrollcommand {.body.main.notes.s set}
grid .body.main.notes.c -row 0 -column 0 -sticky ew
grid .body.main.notes.s -row 0 -column 1 -sticky ns
ttk::label .body.main.caption -textvariable ::rf::caption -style Subtle.TLabel -font RFSmall -wraplength 680 -padding {0 2}
.body.main.notes.c create window 0 0 -anchor nw -window .body.main.caption -tags caption
bind .body.main.notes.c <Configure> {
    .body.main.caption configure -wraplength [expr {max(100,%w)}]
    .body.main.notes.c itemconfigure caption -width %w
}
bind .body.main.caption <Configure> {
    .body.main.notes.c configure -scrollregion [.body.main.notes.c bbox all] -height [expr {min(64,[winfo reqheight .body.main.caption])}]
}
trace add variable ::rf::caption write {::apply {{args} {.body.main.notes.c yview moveto 0}}}
bind .body.main <Configure> {
    ::rf::fit_category_tabs [expr {%w-24}]
    .body.main.model configure -wraplength [expr {max(200,%w-24)}]
    .body.main.hover configure -wraplength [expr {max(200,%w-24)}]
    foreach parent [array names ::rf::option_canvases] {
        ::rf::size_options $parent [winfo width $::rf::option_canvases($parent)]
    }
}
ttk::label .body.main.model -text {模型：圆柱 Fourier 模态 · Maxwell 矢量有限元 · 冷等离子体介电张量。} -style Subtle.TLabel -font RFSmall -wraplength 680
grid .body.main.model -row 7 -column 0 -sticky ew

ttk::frame .footer -padding {20 4 20 12}
grid .footer -row 2 -column 0 -sticky ew
ttk::label .footer.status -textvariable ::rf::status -font RFSmall -width 50
ttk::progressbar .footer.progress -variable ::rf::progress -maximum 100 -length 180
grid .footer.status -row 0 -column 0 -sticky w
grid .footer.progress -row 0 -column 1 -sticky e
grid columnconfigure .footer 0 -weight 1
bind RFParameterNoWheel <MouseWheel> {break}
bind RFParameterNoWheel <Button-4> {break}
bind RFParameterNoWheel <Button-5> {break}
::rf::block_parameter_wheel .body.side
bind all <MouseWheel> {::rf::wheel %W [expr {%D>0 ? -3 : 3}]}
bind all <Button-4> {::rf::wheel %W -3}
bind all <Button-5> {::rf::wheel %W 3}
# Aqua 注册原生 Command 快捷键；所有平台继续保留已有 Control 快捷键。
if {[tk windowingsystem] eq {aqua}} {
    bind . <Command-o> {if {!$::rf::busy} {::rf::open_project}}
    bind . <Command-s> {if {!$::rf::busy} {::rf::save_project}}
    bind . <Command-Return> ::rf::run
}
bind . <Control-o> {if {!$::rf::busy} {::rf::open_project}}
bind . <Control-s> {if {!$::rf::busy} {::rf::save_project}}
bind . <Control-Return> ::rf::run
bind . <Escape> ::rf::stop
::rf::pull
trace add variable ::rf::cfg write ::rf::edited
set ::rf::loading 0
::rf::update_title
::rf::show_result
::rf::update_controls

::rf::refresh_model
