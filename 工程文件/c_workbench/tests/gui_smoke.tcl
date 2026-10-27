set failures {}
proc bgerror {message} {lappend ::failures $::errorInfo}
proc assert {expression message} {if {![uplevel 1 [list expr $expression]]} {error $message}}
proc await_job {} {
    set deadline [expr {[clock milliseconds]+20000}]
    while {$::rf::busy} {
        update
        if {[clock milliseconds]>$deadline} {error {计算超时}}
    }
    update
    assert {$::failures eq {}} "后台错误：$::failures"
}
proc button_names {parent} {
    set names {}
    foreach button [winfo children $parent] {lappend names [$button cget -text]}
    return $names
}
proc select_mode {mode} {
    set index [lsearch -exact $::rf::modes $mode]
    assert {$index>=0} "不存在的计算分类：$mode"
    .navigation.tabs.b$index invoke
    assert {$::rf::mode eq $mode} "分类按钮没有切换模块：$mode"
}
proc choose_chart {name} {
    set index [lsearch -exact $::rf::chart_names $name]
    assert {$index>=0} "不存在的图示按钮：$name"
    .body.main.view.b$index invoke
    assert {$::rf::chart_name eq $name && [.body.main.view.b$index instate selected]} "图示按钮没有选中：$name"
}
proc assert_button_bounds {} {
    set widths {};set baseline {};set height {};set previous_end -2
    foreach button [winfo children .navigation.tabs] {
        if {$baseline eq {}} {set baseline [winfo y $button];set height [winfo height $button]}
        assert {[winfo y $button]==$baseline && [winfo height $button]==$height} {顶部七栏未保持固定一排等高}
        assert {[winfo x $button]-$previous_end>=2} {固定栏目的分割线间距不足或栏目重叠}
        assert {[font measure RFCategory [$button cget -text]]+16<=[winfo width $button]} {固定栏目文字被裁切或字号未保留}
        lappend widths [winfo width $button]
        set previous_end [expr {[winfo x $button]+[winfo width $button]}]
    }
    assert {[tcl::mathfunc::max {*}$widths]-[tcl::mathfunc::min {*}$widths]<=2} {顶部七栏没有等宽分割}
    assert {[winfo y .navigation]>=[winfo y .header]+[winfo height .header] && [winfo y .body]>=[winfo y .navigation]+[winfo height .navigation]} {固定栏目没有位于顶部工具栏与三栏工作区之间}
    foreach parent {.navigation.tabs .body.main.view} {
        foreach button [winfo children $parent] {
            assert {[winfo class $button] eq {TRadiobutton}} {选项仍然使用下拉菜单}
            assert {[winfo ismapped $button]} "选项按钮没有显示：[$button cget -text]"
            assert {[winfo x $button]>=0 && [winfo x $button]+[winfo width $button]<=[winfo width $parent]} {横向按钮超出可用宽度}
            assert {[winfo y $button]>=0 && [winfo y $button]+[winfo height $button]<=[winfo height $parent]} {换行按钮被裁切}
            set c [expr {[info exists ::rf::option_canvases($parent)] ? $::rf::option_canvases($parent) : $parent}]
            set x [expr {[winfo rootx $button]+[winfo width $button]/2}]
            set y [expr {[winfo rooty $button]+[winfo height $button]/2}]
            if {$y>=[winfo rooty $c] && $y<[winfo rooty $c]+[winfo height $c]} {
                assert {[winfo containing $x $y] eq $button} {图示选项被滚动画布遮住}
            }
        }
    }
}
proc assert_x_axis_spacing {} {
    set c .body.main.plot
    set description "$::rf::mode / $::rf::chart_name"
    set ticks [$c find withtag x_tick];set labels [$c find withtag x_label]
    assert {[llength $ticks]>=2 && [llength $labels]==1} "$description 缺少横坐标刻度或轴说明"
    set tick_box [$c bbox x_tick];set label_box [$c bbox x_label]
    assert {[lindex $tick_box 3]+6<=[lindex $label_box 1]} "$description 横坐标数字与说明重叠：$tick_box / $label_box"
    assert {[lindex $label_box 0]>=0 && [lindex $label_box 2]<=[winfo width $c] && [lindex $label_box 3]<=[winfo height $c]-4} "$description 横坐标说明被裁切：$label_box"
    set end -Inf
    foreach id $ticks {
        set box [$c bbox $id]
        assert {[lindex $box 0]>=0 && [lindex $box 2]<=[winfo width $c] && [lindex $box 0]>=$end+6} "$description 横坐标数字互相重叠或超出窗口：$box"
        set end [lindex $box 2]
    }
    if {[llength [$c find withtag state_legend]]} {
        set box [$c bbox state_legend]
        assert {[lindex $box 1]>=[lindex $label_box 3]+6 && [lindex $box 3]<=[winfo height $c]-4} "$description 状态图例与横坐标说明重叠或被裁切：$box"
    }
    assert {[lindex $::rf::plot_bounds 3]>[lindex $::rf::plot_bounds 1]+12} "$description 绘图区高度不足：$::rf::plot_bounds"
}
proc assert_sidebar_layout {} {
    set selected [.body.side.tabs select]
    set field_column {}
    array set tab_widths {}
    for {set x 0} {$x<[winfo width .body.side.tabs]} {incr x} {
        if {![catch {.body.side.tabs index @$x,10} index]} {incr tab_widths($index)}
    }
    foreach page [.body.side.tabs tabs] {
        set index [.body.side.tabs index $page]
        set text [.body.side.tabs tab $page -text]
        assert {[info exists tab_widths($index)] && $tab_widths($index)>=[font measure RFSidebar $text]+20} "参数页签文字被裁切：$text"
    }
    foreach page [.body.side.tabs tabs] {
        .body.side.tabs select $page
        update idletasks
        set form $page.c.form
        assert {[winfo reqwidth $form]<=[winfo width $page.c]} {放大字体后参数内容超出左栏}
        foreach child [winfo children $form] {
            if {[winfo manager $child] ne {grid}} {continue}
            assert {[winfo x $child]+[winfo width $child]<=[winfo width $form]} {参数文字或输入框被横向裁切}
        }
        foreach key [array names ::rf::widgets] {
            set widget $::rf::widgets($key)
            if {[winfo parent $widget] ne $form || [winfo manager $widget] ne {grid}} {continue}
            set label $::rf::row_labels($key)
            assert {[$label cget -anchor] eq {w} && [$label cget -justify] eq {left}} {参数名称没有左对齐}
            set column [list [winfo x $widget] [winfo width $widget]]
            if {$field_column eq {}} {set field_column $column}
            assert {$column eq $field_column} {左侧数值框没有统一列位置和宽度}
            assert {[winfo width $label]>=[winfo reqwidth $label] && [winfo x $label]+[winfo width $label]<=[winfo x $widget]} {参数名称、单位与输入值重叠}
            assert {[winfo x $widget]-[winfo x $label]-[winfo width $label]<=6} {参数名称与数值之间空白过大}
            if {[info exists ::rf::selectors($key)]} {
                foreach text [lindex $::rf::selectors($key) 1] {
                    assert {[font measure [$widget cget -font] $text]+22<=[winfo width $widget]} "下拉选项文字被裁切：$text"
                }
            }
        }
    }
    .body.side.tabs select $selected
    update idletasks
}
proc scene_count {tag} {
    set n 0
    foreach primitive $::rf::model_scene {if {[lindex $primitive 5] eq $tag} {incr n}}
    return $n
}
# Check actual alpha composition, shared triangle edges, and painter ordering.
image create photo test_alpha
rf_render_model test_alpha 20 20 {
    {polygon {0 0 20 0 20 20} #0000ff 0.5 0 test}
    {polygon {0 0 20 20 0 20} #0000ff 0.5 0 test}
} #ffffff
foreach point {{3 12} {12 3} {10 10}} {
    assert {[test_alpha get {*}$point] eq {128 128 255}} {半透明面片混合或接缝错误}
}
rf_render_model test_alpha 20 20 {
    {line {0 10 20 10} #ff0000 1 4 antenna}
    {polygon {0 0 20 0 20 20 0 20} #0000ff 0.5 0 shell}
} #ffffff
assert {[test_alpha get 10 10] eq {128 0 128}} {透明圆柱遮住了内部天线}
assert {[catch {rf_render_model test_alpha 20 20 {{polygon {0 0 20 0 20 20} #0000ff 2 0 test}} #ffffff}]} {无效透明度未被拒绝}
image delete test_alpha
proc calculate {mode} {select_mode $mode;.header.run invoke;await_job}
update
assert {[winfo class .navigation.tabs] eq {TFrame} && ![winfo exists .body.main.view.choice]} {计算分类或图示仍使用下拉菜单}
set category_font [ttk::style lookup [.navigation.tabs.b0 cget -style] -font]
set chart_font [ttk::style lookup [.body.main.view.b0 cget -style] -font]
assert {[font actual $chart_font -size]>=18 && [font actual $category_font -size]>=20 && [font actual $category_font -size]>[font actual $chart_font -size] && [font actual $category_font -weight] eq {bold}} {第一排分类字号未加大加粗，无法区分两排选项}
assert_button_bounds
foreach parent {.navigation.tabs .body.main.view} {
    set previous -1
    foreach button [winfo children $parent] {
        assert {[winfo x $button]>=0} {真空选项没有按可用宽度排列}
        set previous [winfo x $button]
    }
}
assert {[.navigation.tabs.b0 instate selected] && [.body.main.view.b0 instate selected]} {默认分类或图示没有选中状态}
assert {[winfo width .body.side]==360} {参数栏宽度不正确}
assert {[dict get [grid info .body.model] -column]==1 && [dict get [grid info .body.main] -column]==2} {模型与结果未按三栏排列}
assert {[string match {*无限长冷等离子体工作台} [wm title .]]} {窗口名称没有更新}
assert {[scene_count antenna]>0} {启动时中间未生成天线模型}
lassign $::rf::selectors(type) antenna_numbers antenna_names
assert {$antenna_numbers eq {1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17}} {天线编号没有连续排到 17}
assert {[lrange $antenna_names 5 7] eq {{6 单鞍} {7 双鞍形} {8 Nagoya III}}} {单鞍未排在双鞍前面或名称不正确}
assert {![string match {*孔径*} $antenna_names]} {重复孔径模型仍在目录中}
# New sources expose only the dimensions and prescribed-current controls they use.
set initial_type $::rf::cfg(type)
set initial_angle $::rf::cfg(arc_angle_deg)
set ::rf::cfg(type) 17
set ::rf::cfg(arc_angle_deg) 180
set ::rf::choice(type) {7 双鞍形}
::rf::choose type
assert {$::rf::cfg(type)==7 && $::rf::cfg(arc_angle_deg)==120} {切换到双鞍形沿用了其他模板的180度张角}
set ::rf::cfg(type) $initial_type
set ::rf::cfg(arc_angle_deg) $initial_angle
foreach kind {13 14 15 16 17} {
    set native [rf_preview [dict replace [rf_state] type $kind]]
    assert {[dict get $native count]>0 && [llength [dict get $native segments]]>0} {新增天线未生成几何}
    set ::rf::cfg(type) $kind
    ::rf::update_controls
    set pattern_visible [expr {[winfo manager $::rf::widgets(current_pattern)] eq {grid}}]
    assert {$pattern_visible==($kind in {15 16})} {新增电流分布控件显示错误}
    assert {([winfo manager $::rf::widgets(current_phase_gradient)] eq {grid})==($kind==17)} {沿线相位梯度控件显示错误}
    assert {([winfo manager $::rf::widgets(inner_radius)] eq {grid})==($kind in {14 15})} {平面源内半径控件显示错误}
}
set ::rf::cfg(type) $initial_type
::rf::update_controls
set saddle_preview [rf_preview [dict replace [rf_state] type 7 arc_angle_deg 120]]
set signs {}
foreach segment [dict get $saddle_preview segments] {
    assert {[llength $segment]==9} {预览没有保留支路相位和有符号电流}
    lappend signs [lindex $segment 8]
}
set signed_values [lsort -real -unique $signs]
assert {[llength $signed_values]==2 && [lindex $signed_values 0]==-1 && [lindex $signed_values 1]==1} {双鞍形两副回路的反相电流信息丢失}
assert {[scene_count plasma_shell]==0} {真空模式显示了等离子体区域}
assert {![winfo exists .body.side.medium] && [llength $::rf::modes]==7} {启动时仍有介质开关或合并后的栏目数不正确}
assert {[button_names .navigation.tabs] eq {真空单模场 真空波数谱 等离子体单模场 等离子体波数谱 等离子体总场 电势波形 色散与射线}} {七个栏目的新名称或顺序不正确}
assert {[ttk::style lookup TNotebook.Tab -font {selected}] eq {RFSidebar} && [ttk::style lookup TNotebook.Tab -font {!selected}] eq {RFSidebar}} {参数页签字体没有统一}
foreach style {Param.TLabel ParamGroup.TLabel Sidebar.TCheckbutton Advanced.TCheckbutton Sidebar.Subtle.TLabel} {
    assert {[font actual [ttk::style lookup $style -font] -size]>=18} "左侧参数字号没有调大：$style"
}
foreach key [array names ::rf::widgets] {
    set widget $::rf::widgets($key)
    set label $::rf::row_labels($key)
    assert {[font actual [$widget cget -font] -size]>=20} "参数输入值字号没有调大：$key"
    assert {[font actual [$label cget -font] -size]>=20} "参数名称仍被默认小字体覆盖：$key"
    assert {[$label cget -font] eq [$widget cget -font]} "参数名称和数值字体不一致：$key"
}
set tab_titles {}
foreach page [.body.side.tabs tabs] {lappend tab_titles [.body.side.tabs tab $page -text]}
assert {$tab_titles eq {天线 介质 扫描 射线追踪}} {扫描与射线追踪没有分为独立页签}
foreach key {kmin kmax nk} {
    assert {[winfo parent $::rf::widgets($key)] eq {.body.side.tabs.scan.c.form}} {扫描参数丢失或放错页签}
}
foreach key {space.m_min space.m_max space.phi space.zmin space.zmax space.nr space.nz} {
    assert {[winfo parent $::rf::widgets($key)] eq {.body.side.tabs.scan.c.form}} {全模式求和或观察截面参数缺失}
}
foreach key [array names ::rf::widgets ray.*] {
    assert {[winfo parent $::rf::widgets($key)] eq {.body.side.tabs.ray.c.form}} {射线参数仍然混在扫描页}
}
assert_sidebar_layout
assert {![winfo ismapped $::rf::widgets(turns)] && ![winfo ismapped $::rf::widgets(elements)]} {单环天线仍显示不适用的阵列或螺旋参数}
assert {![winfo ismapped $::rf::widgets(global_phase_deg)]} {高级参数未默认收起}
set ::rf::advanced(antenna) 1
::rf::update_controls
update idletasks
assert {[winfo ismapped $::rf::widgets(global_phase_deg)]} {无法展开高级参数}
assert_sidebar_layout
# Trackpad/wheel gestures must neither move the parameter form nor change a
# dropdown's value. The explicit scrollbar still provides access to lower rows.
set parameter_canvas .body.side.tabs.antenna.c
set parameter_scroll [.body.side.tabs.antenna.s cget -command]
{*}$parameter_scroll moveto 0
update idletasks
set parameter_top [$parameter_canvas yview]
{*}$parameter_scroll moveto 0.25
update idletasks
set parameter_position [$parameter_canvas yview]
assert {[lindex $parameter_position 0]>[lindex $parameter_top 0]} {参数滚动条无法访问下方参数}
set parameter_cfg [array get ::rf::cfg]
set parameter_choice [array get ::rf::choice]
set parameter_revision $::rf::revision
foreach target [list .body.side.tabs .body.side.tabs.antenna.s $parameter_canvas $parameter_canvas.form $::rf::row_labels(frequency_mhz) $::rf::widgets(frequency_mhz) $::rf::widgets(type)] {
    foreach delta {-120 -1 0 1 120} {
        event generate $target <MouseWheel> -delta $delta -when now
    }
    foreach button {4 5} {event generate $target <Button-$button> -when now}
    update
    assert {[$parameter_canvas yview] eq $parameter_position} "触控板仍然移动左侧参数：$target"
    assert {[array get ::rf::cfg] eq $parameter_cfg && [array get ::rf::choice] eq $parameter_choice && $::rf::revision==$parameter_revision} "触控板意外修改了参数：$target"
}
::rf::wheel $parameter_canvas 1000
assert {[$parameter_canvas yview] eq $parameter_position} {全局滚轮处理仍然移动左侧参数}
{*}$parameter_scroll moveto 0
set ::rf::advanced(antenna) 0
::rf::update_controls
set p [winfo parent $::rf::widgets(frequency_mhz)]
assert {[winfo height $::rf::widgets(frequency_mhz)]<48} {大字体参数行过高}
set committed [rf_state]
set ::rf::cfg(antenna_radius) 0.4
::rf::refresh_model
assert {[dict get $::rf::model snapshot antenna_radius]==0.4} {未保存的尺寸未更新模型}
assert {[rf_state] eq $committed} {模型预览修改了已提交的计算参数}
set good_model $::rf::model
set ::rf::cfg(antenna_radius) 2
::rf::refresh_model
assert {$::rf::model eq $good_model} {输入无效参数后丢失了有效模型}
set ::rf::cfg(antenna_radius) 0.44
::rf::refresh_model
set before [rf_state]
assert {[catch {rf_state {antenna_radius 2}} msg]} {无效半径应被拒绝}
assert {[rf_state] eq $before} {校验失败后项目被修改}
assert {[catch {rf_state {radial_cells 12.5}} msg]} {非整数网格应被拒绝}
assert {[catch {rf_state {frequency_mhz NaN}} msg]} {非有限参数应被拒绝}
assert {[catch {rf_state {kmin 40 kmax -40}} msg]} {反向扫描范围应被拒绝}
# GUI traces, bridge, actual field arrays and chart must agree.
# This analytic loop fixture explicitly uses m=0, independently of the UI default.
set ::rf::cfg(selected_m) 0
set ::rf::cfg(current) 2
set ::rf::cfg(nk) 7
calculate vacuum
set r $::rf::cache(vacuum)
set f [open /tmp/rf-ui-verified.svg w];fconfigure $f -encoding utf-8;puts $f [::rf::svg_text];close $f
set c [lindex [dict get $r charts] 0]
assert {[llength [dict get $r charts]]==3} {真空单模场图示目录不正确}
assert {[llength [dict get $c x]]==257} {径向采样不完整}
set rows [split [string trim [dict get $r csv]] \n]
assert {[llength $rows]==258} {复数 CSV 行数不完整}
set plotted [dict get [lindex [dict get $c series] 0] y]
set csv_value [lindex [split [lindex $rows 51] ,] 7]
assert {abs([lindex $plotted 50]-$csv_value)<1e-10} {图表与 CSV 不是同一份计算数据}
# A magnetic or flux view must not keep the electric peak's V unit.
choose_chart 磁场幅值
assert {[string match {* T m} $::rf::metric2]} {磁场峰值提示仍然使用电场单位}
set magnetic_chart [lindex $::rf::plots 1]
set magnetic_peak 0.
foreach value [dict get [lindex [dict get $magnetic_chart series] 0] y] {set magnetic_peak [expr {max($magnetic_peak,$value)}]}
assert {[regexp {峰值 ([^ ]+)} $::rf::metric2 match shown] && abs($shown-$magnetic_peak)<1e-3*max(1e-30,$magnetic_peak)} {磁场峰值提示没有读取磁场数据}
choose_chart 轴向能流密度
assert {[string match {最大绝对值 * W} $::rf::metric2]} {模态谱场能流提示使用了错误单位}
update
lassign $::rf::plot_bounds left top right bottom xmin xmax ymin ymax
::rf::inspect [expr {($left+$right)/2}] [expr {($top+$bottom)/2}]
assert {[string first {半径 r (m)} $::rf::hover]>=0 && [string match {* W} $::rf::hover]} {曲线悬停缺少坐标或场量单位}
choose_chart 电场幅值
set peak [dict get $r peak]
set ::rf::cfg(current) 4
assert {[string match *参数已修改* $::rf::snapshot_note]} {修改参数没有标记结果快照}
calculate vacuum
assert {abs([dict get $::rf::cache(vacuum) peak]/$peak-2)<1e-8} {界面修改的电流没有进入求解器}
# Saving/loading restores edited fields, and failed loads preserve state.
set scratch [file join /tmp rf-ui-test-[pid]]
file mkdir $scratch
rf_state {space.zmin -2 space.zmax 2 space.nz 193 space.nr 65 space.m_min -2 space.m_max 3 space.phi .37 ray.r0 .18 ray.z0 .6 ray.phi0_deg 30 ray.tolerance 0.000002 ray.max_step .003 ray.max_path 2 ray.direction -1 ray.max_steps 2345}
rf_save $scratch/project.rfproj
rf_state {current 1}
rf_open $scratch/project.rfproj
assert {[dict get [rf_state] space.m_min]==-2 && [dict get [rf_state] space.m_max]==3 && [dict get [rf_state] space.phi]==.37} {全模式参数未随项目保存}
assert {[dict get [rf_state] space.nz]==193 && [dict get [rf_state] space.nr]==65} {空间设置未保存}
assert {[dict get [rf_state] ray.r0]==.18 && [dict get [rf_state] ray.direction]==-1 && [dict get [rf_state] ray.max_steps]==2345} {射线发射与积分设置未保存}
assert {[dict get [rf_state] current]==4} {项目读写丢失修改}
set before [rf_state]
assert {[catch {rf_open $scratch/missing.rfproj}]} {加载不存在的文件应失败}
assert {[rf_state] eq $before} {加载失败破坏了已有项目}
::rf::pull
# Incremental scans produce actual source and field data, and can be cancelled.
calculate scan
set source_chart [lindex [dict get $::rf::cache(scan) charts] 1]
assert {[dict get $source_chart title] eq {电流谱幅值} && [llength [dict get $source_chart x]]==7} {电流谱未移入真空波数谱或采样不一致}
set source_index 0
foreach k [dict get $source_chart x] {
    set native_source [rf_compute source $k]
    for {set c 0} {$c<3} {incr c} {
        set actual [lindex [dict get [lindex [dict get $source_chart series] $c] y] $source_index]
        assert {$actual==[lindex [dict get $native_source values] $c]} {合并后的源谱与原始源谱不一致}
    }
    incr source_index
}
set csv_source [::rf::chart_csv $source_chart {}]
assert {[lindex [split $csv_source \n] 0] eq {kz_rad_per_m,Kr_re,Kr_im,Kphi_re,Kphi_im,Kz_re,Kz_im,error}} {电流谱 CSV 缺失复数分量}
set f [open /tmp/rf-ui-source.csv w];puts -nonewline $f $csv_source;close $f
set scan_titles {}
foreach chart [dict get $::rf::cache(scan) charts] {lappend scan_titles [dict get $chart title]}
set expected_scan_titles {电场幅值 电流谱幅值 轴向能流谱 总电场谱热图 磁场谱热图}
assert {$scan_titles eq $expected_scan_titles} {真空扫描图示目录不正确，或仍有电场分量热图}
assert {[button_names .body.main.view] eq $expected_scan_titles} {真空扫描选项仍有已移除的热图}
assert {[::rf::expected_charts scan] eq $expected_scan_titles} {真空扫描预设图示目录不正确}
# An invalid spectral slice must remain missing, with an explicit export reason.
set ::test_all_vacuum_fail 0
rename rf_compute rf_native_compute
proc rf_compute {args} {
    if {[lindex $args 0] eq {sample} && ($::test_all_vacuum_fail || abs([lindex $args 1])<1e-9)} {error {test invalid spectral slice}}
    return [uplevel 1 [linsert $args 0 rf_native_compute]]
}
calculate scan
set chart [lindex [dict get $::rf::cache(scan) charts] 0]
assert {[lindex [dict get [lindex [dict get $chart series] 0] y] 3] eq {}} {失败切片被替换为零}
assert {[string first {test invalid spectral slice} [dict get $::rf::cache(scan) csv]]>=0} {CSV 未保留失败原因}
set heat [lindex [dict get $::rf::cache(scan) charts] 3]
assert {[lindex [dict get $heat matrix] 3] eq {}} {谱热图把失败切片替换为零}
choose_chart [dict get $heat title]
update
assert {[llength [.body.main.plot find withtag heat_cell]]>100} {谱热图未绘制}
set heat_csv [::rf::chart_csv $heat {}]
assert {[string first {kz_rad_per_m,r_m,value} $heat_csv]==0} {谱热图 CSV 无网格列}
assert {[string first {test invalid spectral slice} $heat_csv]>=0} {谱热图 CSV 丢失失败原因}
set heat_svg [::rf::svg_text]
assert {[string first {<rect} $heat_svg]>=0} {谱热图 SVG 没有颜色网格}
set f [open /tmp/rf-ui-heatmap.svg w];fconfigure $f -encoding utf-8;puts $f $heat_svg;close $f
set source_with_field_gap [lindex [dict get $::rf::cache(scan) charts] 1]
foreach line [dict get $source_with_field_gap series] {assert {[lindex [dict get $line y] 3] ne {}} {场失败点抹掉了独立的源谱}}
set ::test_all_vacuum_fail 1
choose_chart 电流谱幅值
calculate scan
assert {$::rf::chart_name eq {电流谱幅值} && [llength [.body.main.plot find all]]>10} {所有场失败时无法查看有效源谱}
foreach line [dict get [lindex [dict get $::rf::cache(scan) charts] 1] series] {assert {[lsearch -exact [dict get $line y] {}]<0} {所有场失败时源谱丢失}}
choose_chart 总电场谱热图
update
assert {[llength [.body.main.plot find withtag heat_cell]]==0} {所有场失败时生成了虚假热图数据}
unset ::test_all_vacuum_fail
rename rf_compute {}
rename rf_native_compute rf_compute
set saved $::rf::cache(scan)
assert {[dict get $saved count]==7} {波数扫描采样错误}
set ::rf::cfg(nk) 401
::rf::run
foreach button [winfo children .navigation.tabs] {assert {[$button instate disabled]} {计算中分类按钮未禁用}}
after 5 ::rf::stop
await_job
foreach button [winfo children .navigation.tabs] {assert {![$button instate disabled]} {停止后分类按钮没有恢复}}
assert {!$::rf::busy && $::rf::cache(scan) eq $saved} {停止计算破坏了上一份完整结果}
# Vacuum and plasma categories stay in the same catalogue; selecting a view
# derives the medium without modifying physical inputs or invalidating caches.
assert {![winfo exists .body.side.medium]} {仍然显示启用等离子体开关}
set catalogue [button_names .navigation.tabs]
assert {$::rf::modes eq {vacuum scan plasma plasma_scan full waveform branches}} {合并后的计算栏目不正确}
set revision_before_switch $::rf::revision
set source_before_switch $::rf::cache(scan)
select_mode plasma_scan
assert {$::rf::cfg(plasma_enabled)==1} {选择等离子体栏目没有自动切换模型}
assert {{等离子体单模场} in $catalogue && {单模式空间吸收与沉积} ni $catalogue && {单模式场与电流} ni $catalogue && {spatial} ni $::rf::modes && {deposition} ni $::rf::modes} {单模式栏目未合并或名称不正确}
assert {{plasma_current} ni $::rf::modes && {空间吸收与阻抗} ni $catalogue} {仍显示已移除的旧栏目}
assert {{真空波数谱} in $catalogue && {天线电流谱} ni $catalogue && {source} ni $::rf::modes && {material} ni $::rf::modes && {材料与介电张量} ni $catalogue} {统一栏目缺少真空选项或恢复了已删材料图}
select_mode scan
choose_chart 电流谱幅值
assert {$::rf::cfg(plasma_enabled)==0 && [button_names .navigation.tabs] eq $catalogue} {选择真空栏目后等离子体栏目消失或模型未切换}
assert {[button_names .body.main.view] eq $expected_scan_titles && $::rf::cache(scan) eq $source_before_switch} {切换栏目丢失了源谱快照}
assert {![$::rf::widgets(plasma.density) instate disabled]} {移除开关后等离子体参数仍被锁定}
select_mode plasma
assert {$::rf::cfg(plasma_enabled)==1 && $::rf::revision==$revision_before_switch} {切换栏目修改了物理参数版本}
assert {[button_names .body.main.view] eq {电场幅值 磁场幅值 轴向能流密度 电流幅值 局部吸收 径向壳层沉积 波阻抗幅值}} {等离子体单模场图示目录不正确}
select_mode plasma_scan
::rf::refresh_model
assert {[dict get $::rf::model snapshot plasma_enabled]==1} {中间模型没有同步等离子体区域}
assert {[scene_count plasma_shell]>100} {等离子体圆柱缺少面填充}
foreach primitive $::rf::model_scene {
    if {[lindex $primitive 5] in {plasma_shell vessel_shell}} {
        assert {[lindex $primitive 0] eq {polygon} && [lindex $primitive 3]>0 && [lindex $primitive 3]<0.5} {圆柱未使用半透明面填充}
    }
}
assert {[scene_count vessel_shell]>500} {外层圆柱侧面未填充}
# Axial opacity is constant; both cylinders cross the top and bottom of the
# viewport rather than fading away or acquiring artificial end caps.
foreach tag {vessel_shell plasma_shell} alpha {.10 .24} {
    set ymin Inf;set ymax -Inf
    foreach primitive $::rf::model_scene {
        if {[lindex $primitive 5] ne $tag} {continue}
        assert {[lindex $primitive 3]==$alpha} {圆柱侧面仍有轴向渐隐}
        foreach {x y} [lindex $primitive 1] {set ymin [expr {min($ymin,$y)}];set ymax [expr {max($ymax,$y)}]}
    }
    assert {$ymin<0 && $ymax>[image height ::rf::model_image]} {圆柱没有覆盖截断后的显示区}
}
assert {abs([image height ::rf::model_image]/double([winfo height .body.model.canvas])-.64)<.002} {中间模型未在原百分之八十高度上再缩短百分之二十}
set model_image_y [lindex [.body.model.canvas coords [lindex [.body.model.canvas find withtag model_render] 0]] 1]
assert {abs(2*$model_image_y+[image height ::rf::model_image]-[winfo height .body.model.canvas])<1e-9} {中间模型上下留白不对称}
set infinity_text {}
foreach id [.body.model.canvas find withtag infinite_label] {lappend infinity_text [.body.model.canvas itemcget $id -text]}
assert {$infinity_text eq {+∞ −∞}} {模型两端没有标明正负无穷}
assert {[scene_count vessel_shell_continuation]==0 && [scene_count plasma_shell_continuation]==0} {圆柱仍然显示渐隐延长线}
# Radius changes must change displayed width, not trigger horizontal autofit.
set original_radius $::rf::cfg(radius)
set vessel_widths {}
foreach factor {1.0 0.9} {
    set ::rf::cfg(radius) [expr {$original_radius*$factor}]
    ::rf::refresh_model
    set xmin Inf;set xmax -Inf
    foreach primitive $::rf::model_scene {
        if {[lindex $primitive 5] ne {vessel_shell}} {continue}
        foreach {x y} [lindex $primitive 1] {set xmin [expr {min($xmin,$x)}];set xmax [expr {max($xmax,$x)}]}
    }
    lappend vessel_widths [expr {$xmax-$xmin}]
}
assert {abs([lindex $vessel_widths 1]/[lindex $vessel_widths 0]-.9)<1e-10} {容器宽度仍然被自动铺满，未按半径参数绘制}
set ::rf::cfg(radius) $original_radius
::rf::refresh_model
::rf::model_image write /tmp/rf-ui-translucent-model.png -format png
set ::rf::show_vessel 0
::rf::redraw_model
assert {[scene_count vessel_shell]==0 && [scene_count plasma_shell]>500} {隐藏容器影响了等离子体显示}
set ::rf::show_vessel 1
::rf::redraw_model
# Reproduce the reported default m=0 double-saddle failure through the UI bridge.
set before_templates [rf_state]
rf_state {type 7 selected_m 0 plasma_enabled 1}
set cancelled [rf_compute plasma]
assert {[dict get $cancelled error]<1e-8 && [dict get $cancelled peak]<1e-8} {对称相消模式仍误报求解失败}
rf_state {selected_m 1}
set excited [rf_compute plasma]
assert {[dict get $excited peak]>1e-4} {双鞍形的非零模数没有激发场}
rf_state $before_templates
set middle_model $::rf::model
calculate plasma
assert {[button_names .body.main.view] eq {电场幅值 磁场幅值 轴向能流密度 电流幅值 局部吸收 径向壳层沉积 波阻抗幅值}} {单模式合并后图示缺失、重复或仍有电流相位、实部、虚部}
choose_chart 电流幅值
assert {[string match {* A/m} $::rf::metric2]} {电流幅值仍显示电场单位}
set plasma_row [split [lindex [split [string trim [dict get $::rf::cache(plasma) csv]] \n] 51] ,]
foreach name {电流幅值 磁场幅值} columns {{7 8 9 10 11 12} {16 17 18 19 20 21}} {
    choose_chart $name
    set chart [lindex $::rf::plots [lsearch -exact $::rf::chart_names $name]]
    set labels {}
    foreach line [dict get $chart series] {lappend labels [dict get $line label]}
    assert {$labels eq {总幅值 {r 分量} {φ 分量} {z 分量}}} {磁场或电流幅值缺少总幅值曲线}
    set norm2 0.0
    foreach col $columns {set value [lindex $plasma_row $col];set norm2 [expr {$norm2+$value*$value}]}
    set actual [lindex [dict get [lindex [dict get $chart series] 0] y] 50]
    assert {abs($actual-sqrt($norm2))<=1e-12*max(1e-100,sqrt($norm2))} {总幅值与复数场或电流数据不一致}
}
choose_chart 电场幅值
assert {[string match {* V} $::rf::metric2]} {电场幅值未恢复电场单位}
set material_result [rf_compute material]
set material_csv [dict get $material_result csv]
assert {[string first {ne_m3} $material_csv]>=0} {材料 CSV 缺少电子密度}
foreach mode {plasma} {
    select_mode $mode
    foreach name [button_names .body.main.view] {
        choose_chart $name
        update
        assert {[llength [.body.main.plot find all]]>10} {结果曲线未绘制}
        set svg [::rf::svg_text]
        assert {[string first {<polyline} $svg]>=0} {SVG 未包含数据曲线}
    }
}
set ::rf::cfg(nk) 7
select_mode plasma_scan
set expected_plasma_scan_titles {电场幅值 吸收谱 负载阻抗谱 轴向能流谱 总电场谱热图 磁场谱热图 局部吸收谱热图}
assert {[button_names .body.main.view] eq $expected_plasma_scan_titles} {等离子体扫描预设菜单仍有电场分量热图}
calculate plasma_scan
set plasma_scan_titles {}
foreach chart [dict get $::rf::cache(plasma_scan) charts] {lappend plasma_scan_titles [dict get $chart title]}
assert {$plasma_scan_titles eq $expected_plasma_scan_titles} {等离子体波数谱图示目录不正确，或仍有电场分量热图}
# Impedance now comes from the same endpoint-inclusive scan as absorption.
set impedance_chart [lindex [dict get $::rf::cache(plasma_scan) charts] 2]
assert {[dict get $impedance_chart title] eq {负载阻抗谱} && [llength [dict get $impedance_chart series]]==2} {扫描阻抗谱缺少 R/X 曲线}
set scan_csv_rows [split [string trim [dict get $::rf::cache(plasma_scan) csv]] \n]
assert {[lindex $scan_csv_rows 0] eq {kz_rad_per_m,valid,peak_total_V,absorbed_W_m,axial_flux_W_m2,R_ohm_m,X_ohm_m,error}} {扫描 CSV 缺少阻抗列}
set sample_index 0
foreach k [dict get $impedance_chart x] {
    set native [rf_compute plasma_sample $k]
    set raw [split [lindex $scan_csv_rows [expr {$sample_index+1}]] ,]
    assert {[llength $raw]==8} {扫描 CSV 列数错误}
    foreach component {0 1} key {spectral_resistance spectral_reactance} col {5 6} {
        set value [lindex [dict get [lindex [dict get $impedance_chart series] $component] y] $sample_index]
        assert {$value==[dict get $native $key] && abs($value-[lindex $raw $col])<1e-12*max(1,abs($value))} {扫描阻抗与同波数求解结果或 CSV 不一致}
    }
    incr sample_index
}
choose_chart 负载阻抗谱
update
assert {[string first {Ω m} [::rf::svg_text]]>=0 && [string first {<polyline} [::rf::svg_text]]>=0} {扫描阻抗图未绘制或单位错误}
# Failed samples must leave both R and X missing, with aligned CSV columns.
rename rf_compute rf_native_compute
proc rf_compute {args} {
    if {[lindex $args 0] eq {plasma_sample} && abs([lindex $args 1]-$::rf::cfg(kmin))<1e-9} {error {test invalid impedance slice}}
    return [uplevel 1 [linsert $args 0 rf_native_compute]]
}
calculate plasma_scan
set failed_chart [lindex [dict get $::rf::cache(plasma_scan) charts] 2]
foreach line [dict get $failed_chart series] {assert {[lindex [dict get $line y] 0] eq {}} {失败阻抗切片被替换成零}}
set failed_row [split [lindex [split [dict get $::rf::cache(plasma_scan) csv] \n] 1] ,]
assert {[llength $failed_row]==8 && [lindex $failed_row 5] eq {} && [lindex $failed_row 6] eq {} && [string first {test invalid impedance slice} [lindex $failed_row 7]]>=0} {失败阻抗 CSV 列错位或错误原因丢失}
rename rf_compute {}
rename rf_native_compute rf_compute
calculate plasma_scan
# The new limiting mode is available through the same native UI bridge.
set material_state [rf_state]
rf_state {plasma.solve_mode 2 plasma.density 2e14 plasma.magnetic_field .002}
set limit_result [rf_compute plasma]
assert {[string match *延拓已收敛* [dict get $limit_result note]]} {极限吸收未使用收敛的正碰撞序列}
assert {[dict get $limit_result collision_scale]>0 && [dict get $limit_result collision_scale]<1} {极限吸收没有输出实际碰撞缩放}
rf_state $material_state
set one [rf_compute plasma_sample $::rf::cfg(kmin)]
set expected [dict get $one peak]
set chart [lindex [dict get $::rf::cache(plasma_scan) charts] 0]
assert {[lindex [dict get [lindex [dict get $chart series] 0] y] 0]==$expected} {等离子体扫描未调用等离子体求解器}
assert {[dict get $::rf::model segments] eq [dict get $middle_model segments]} {计算结果切换影响了中间模型}
# Full branches use the same fixed f,m,kz data in all curves and exports.
set pre_branches [rf_state]
rf_open [file join [file dirname [info script]] .. .. examples 全分支螺旋波示例.rfproj]
::rf::pull
select_mode branches
update
assert_button_bounds
assert {{色散与射线} in [button_names .navigation.tabs] && {分支分析} ni [button_names .navigation.tabs] && {射线追踪} ni [button_names .navigation.tabs]} {色散与射线未合并}
assert {[button_names .body.main.view] eq [::rf::expected_charts branches]} {合并后的分支与射线目录不正确}
::rf::wheel .body.main.view.b5 1000
update idletasks
assert {[lindex [.body.main.choices.c yview] 1]==1.0} {放大选项后无法滚动到最后一张图}
.body.main.choices.c yview moveto 0
calculate branches
set branch_result $::rf::cache(branches)
assert {[llength [dict get $branch_result charts]]==8} {合并后应保留八张分支与射线图}
assert {[dict get $branch_result ray_count]==4} {弱碰撞示例没有双分支双向射线}
foreach result [list $branch_result] {
    foreach chart [dict get $result charts] {
        assert {[string first {图例波型按发射半径} [dict get $chart note]]>=0} {波型图例未说明参考位置}
        foreach line [dict get $chart series] {
            set label [dict get $line label]
            assert {[string first [expr {[string index $label 0] eq {A} ? {helicon 型} : {TG 型}}] $label]>=0} {实际参考算例的 H/TG 图例识别错误}
            assert {[llength [dict get $line wave_names]]==[llength [dict get $line y]]} {局部波型未与曲线采样对齐}
        }
    }
}
assert {[button_names .body.main.view] eq {径向波数实部 径向波数虚部 分支传播状态 群速度 几何光学适用性 射线轴向投影 射线方位角 射线功率衰减}} {合并后的八张图不正确或仍保留已删除图}
set branch_csv [dict get $branch_result csv]
set rows [split [string trim $branch_csv] \n]
assert {[llength $rows]==2061} {全分支 CSV 没有保留全部四个根}
set chart [lindex [dict get $branch_result charts] 0]
assert {[::rf::chart_csv $chart [dict get $branch_result csv]] eq [dict get $branch_result csv]} {合并后分支图没有导出径向根表}
set value [lindex [dict get [lindex [dict get $chart series] 0] y] 1]
assert {abs($value-[lindex [split [lindex $rows 1] ,] 9])<1e-10} {分支曲线与导出不一致}
# The combined velocity plot keeps every branch/component value and sign.
set velocity_chart [lindex [dict get $branch_result charts] [lsearch -exact $::rf::chart_names 群速度]]
assert {[dict get $velocity_chart title] eq {群速度} && [llength [dict get $velocity_chart series]]==12} {合并群速度图未保留四支各三个分量}
set columns [split [lindex $rows 0] ,]
assert {{root_gap} in $columns && {polarization_residual} in $columns && {longitudinal_fraction} in $columns} {移除诊断图时误删了极化或数值诊断数据}
foreach name {wave_type wave_match_error wave_reason} {assert {$name in $columns} {CSV 缺少波型识别及依据}}
set valid_column [lsearch -exact $columns group_valid]
set row_index 0
foreach row [lrange $rows 1 end] {
    set values [split $row ,]
    set branch [expr {$row_index%4}];set sample [expr {$row_index/4}]
    foreach component {0 1 2} key {vg_r_m_s vg_phi_m_s vg_z_m_s} {
        set line [lindex [dict get $velocity_chart series] [expr {3*$branch+$component}]]
        assert {[dict get $line branch]==$branch && [dict get $line component]==$component} {群速度分支或分量标记不正确}
        set plotted [lindex [dict get $line y] [expr {2*$sample+1}]]
        if {[lindex $values $valid_column]} {
            set expected [lindex $values [lsearch -exact $columns $key]]
            assert {abs($plotted-$expected)<1e-12*max(1,abs($expected))} {合并群速度改变了数值或符号}
        } else {assert {$plotted eq {}} {无效群速度被填成了零}}
    }
    incr row_index
}
choose_chart 群速度
assert {[string first {helicon 型} [.body.main.legend.b0 cget -text]]>=0 && [string first {TG 型} [.body.main.legend.b2 cget -text]]>=0} {群速度图例没有直接显示物理波型}
assert {[llength [winfo children .body.main.legend]]==7} {群速度图例未提供三个分量和四个分支选择}
.body.main.legend.v1 invoke
.body.main.legend.b2 invoke
for {set j 0} {$j<12} {incr j} {
    assert {$::rf::enabled($j)==($j%3!=1 && $j/3!=2)} {群速度分量或分支勾选没有生效}
}
.body.main.legend.v1 invoke
.body.main.legend.b2 invoke
update
assert {[string first {stroke-dasharray} [::rf::svg_text]]>=0} {群速度 SVG 丢失分量线型}
assert {[string first {velocity-legend} [::rf::svg_text]]>=0} {群速度 SVG 没有独立排布图例}
set f [open /tmp/rf-ui-merged-group-velocity.svg w];fconfigure $f -encoding utf-8;puts $f [::rf::svg_text];close $f
wm geometry . 1200x700
update
assert {[winfo height .body.main.plot]>150} {合并群速度图例挤压绘图区}
foreach name [button_names .body.main.view] {
    choose_chart $name;update
    assert {[llength [.body.main.plot find all]]>10} {分支图示没有绘制}
    if {$name eq {分支传播状态}} {
        assert {[llength [.body.main.plot find withtag branch_state_cell]]>=8 && [llength [.body.main.plot find withtag branch_state_cell]]<40} {状态色带未合并相邻同类区域}
        assert {[string first {颜色分类} [::rf::svg_text]]>=0} {状态 SVG 缺少分类含义}
    } else {assert {[string first {<polyline} [::rf::svg_text]]>=0} {分支 SVG 没有实际曲线}}
}
foreach size {1200x700 1420x820} {
    wm geometry . $size;update
    assert_button_bounds
    assert {[winfo height .body.main.plot]>150} {全分支说明文字挤压绘图区}
}
# Scrolling a plot used to move its canvas origin, clipping axes and curves.
set origin [.body.main.plot canvasy 0]
::rf::wheel .body.main.plot 12
assert {[.body.main.plot canvasy 0]==$origin} {结果图仍被全局滚轮滚出画布}
.body.main.plot yview scroll 3 units
::rf::redraw
assert {[.body.main.plot canvasy 0]==0} {重绘没有复位结果坐标原点}
choose_chart 分支传播状态;update
set status_chart [lindex $::rf::plots [lsearch -exact [button_names .body.main.view] 分支传播状态]]
assert {[dict get $status_chart kind] eq {branch_status}} {状态仍被当成连续数值曲线}
lassign $::rf::plot_bounds left top right bottom xmin xmax ymin ymax
::rf::inspect_branch_status $status_chart [expr {$left+(.15-$xmin)/($xmax-$xmin)*($right-$left)}] [expr {$top+($bottom-$top)/8.}]
assert {[string first {局部：helicon 型} $::rf::hover]>=0} {分支状态悬停没有显示局部物理波型}
set f [open /tmp/rf-ui-branch-status.svg w];fconfigure $f -encoding utf-8;puts $f [::rf::svg_text];close $f
foreach name {射线轴向投影 射线方位角 射线功率衰减} {
    choose_chart $name;update
    assert {[string first {<polyline} [::rf::svg_text]]>=0} {拆分后的射线图缺少曲线}
}
choose_chart 射线轴向投影;update
assert {$::rf::metric1 eq {4 条参考射线}} {合并后射线图仍显示径向采样数}
set ray_chart [lindex $::rf::plots [lsearch -exact [button_names .body.main.view] 射线轴向投影]]
assert {[dict get $ray_chart xlabel] eq {轴向位置 z (m)}} {射线图没有采用轴向位置作为横轴}
assert {[string first {helicon 型} [.body.main.legend.c0 cget -text]]>=0 && [string first {TG 型} [.body.main.legend.c2 cget -text]]>=0} {射线图没有直接标注 H/TG 波型}
assert {[string first {helicon 型} [::rf::svg_text]]>=0 && [string first {TG 型} [::rf::svg_text]]>=0} {导出的射线图丢失物理波型标注}
assert {[llength [.body.main.plot find withtag ray_direction]]==4} {射线图缺少四条能量行进箭头}
assert {[string first {<polygon} [::rf::svg_text]]>=0} {射线 SVG 丢失方向箭头}
foreach line [dict get $ray_chart series] {
    assert {[llength [dict get $line x]]==[llength [dict get $line y]]} {射线独立横坐标不完整}
    foreach r [dict get $line y] {if {$r ne {}} {assert {$r>0 && $r<=0.3} {射线图把 z 数值误画成半径}}}
}
set ray_rows [split [string trim [dict get $branch_result ray_csv]] \n]
foreach column {kr_integrated_rad_m kr_reference_re_rad_m dispersion_residual kr_correction_rad_m} {
    assert {$column in [split [lindex $ray_rows 0] ,]} {移除图示时误删了射线波数或约束误差数据}
}
foreach line [dict get $ray_chart series] {
    assert {[lindex [dict get $line x] 0]==[dict get $branch_result snapshot ray.z0]} {射线没有从指定 z 发射}
    assert {[lindex [dict get $line y] 0]==[dict get $branch_result snapshot ray.r0]} {射线没有从指定半径发射}
    assert {[dict get $line stop] ne {}} {射线缺失终止原因}
}
assert {[::rf::chart_csv $ray_chart {}] eq [dict get $branch_result ray_csv]} {射线导出仍使用径向根表}
set f [open /tmp/rf-ui-ray-projection.svg w];fconfigure $f -encoding utf-8;puts $f [::rf::svg_text];close $f
set f [open /tmp/rf-ui-branches.svg w];fconfigure $f -encoding utf-8;puts $f [::rf::svg_text];close $f
# At the same r0 a nonuniform A branch can fail WKB while B remains usable.
# Refusals must be visible even when some other rays were launched successfully.
set ::rf::cfg(plasma.profile) 1
set ::rf::cfg(plasma.edge_ratio) .2
set ::rf::cfg(ray.r0) .2
calculate branches
assert {[dict get $::rf::cache(branches) ray_count]==2} {非均匀发射没有按具体起点筛选分支}
assert {[string first {未发射} $::rf::caption]>=0} {部分根被拒绝发射时未显示原因}
wm geometry . 1200x700;update
assert {[winfo height .body.main.plot]>150} "拒绝发射说明挤压了绘图区：plot=[winfo width .body.main.plot]x[winfo height .body.main.plot]，caption=[winfo height .body.main.caption]，tools=[winfo height .body.main.tools]"
::rf::wheel .body.main.caption 1000
assert {[lindex [.body.main.notes.c yview] 1]==1.0} {长说明末尾无法滚动查看}
assert {[.body.main.plot canvasy 0]==0} {说明区滚动移动了结果图}
.body.main.notes.c yview moveto 0
set ::rf::cfg(kz) 11
assert {[string match *参数已修改* $::rf::snapshot_note]} {分支快照未标记过期}
rf_state $pre_branches
::rf::pull
# All seven single-mode charts use one fixed (m,kz) and one result snapshot.
set pre_space [rf_state]
foreach {key value} {radial_cells 64 nk 5 kmin -6 kmax 6 space.zmin -.5 space.zmax .5 space.nr 17 space.nz 17 plasma.solve_mode 0} {set ::rf::cfg($key) $value}
select_mode waveform
assert {[button_names .body.main.view] eq {电势虚部波形堆叠}} {波形栏目未改为电势虚部}
select_mode plasma
assert {[button_names .body.main.view] eq [::rf::expected_charts plasma]} {等离子体单模场计算前目录不正确}
choose_chart 径向壳层沉积
calculate plasma
assert {$::rf::chart_name eq {径向壳层沉积}} {单模式计算后没有保留预选图示}
assert {[button_names .body.main.view] eq [::rf::expected_charts plasma]} {等离子体单模场计算后目录不一致}
assert {{累计吸收} ni $::rf::chart_names && {累计吸收比例} ni $::rf::chart_names && {窗口累计吸收} ni $::rf::chart_names} {仍显示已删除的累计吸收图}
set space $::rf::cache(plasma)
assert {[llength [dict get $space charts]]==7 && {轴向吸收分布} ni $::rf::chart_names && {窗口径向沉积} ni $::rf::chart_names} {单模式栏目未去重或仍含波数积分空间图}
assert {[dict get $space absorbed]>0 && [dict get $space power_balance_error]<1e-8} {单模式吸收谱量或功率守恒错误}
set rows [split [string trim [dict get $space csv]] \n]
foreach chart [dict get $space charts] {
    assert {![dict exists $chart kind] && [dict get $chart xlabel] eq {半径 r (m)}} {单模式栏目仍显示 r-z 热图}
}
assert {[dict get [lindex [dict get $space charts] 4] ylabel] eq {局部吸收谱量 (W/m)}} {单模式吸收被误标为空间功率密度}
assert {[dict get [lindex [dict get $space charts] 5] ylabel] eq {d²P / (dr dkz) (W)}} {单模式沉积归一化错误}
# The impedance is a ratio of the original single-(m,kz) complex E/B norms.
set zw_chart [lindex [dict get $space charts] 6]
assert {[dict get $zw_chart title] eq {波阻抗幅值} && [::rf::chart_value_unit $zw_chart] eq {Ω}} {单模式波阻抗标题/单位错误}
set zw_rows [split [string trim [::rf::chart_csv $zw_chart {}]] \n]
assert {[lindex $zw_rows 0] eq {r_m,selected_m,kz_rad_per_m,Z_wave_ohm,valid}} {单模式波阻抗 CSV 缺少模态或有效性}
set index 1
foreach zw [dict get [lindex [dict get $zw_chart series] 0] y] {
        set raw [split [lindex $rows $index] ,]
        set en2 0.;set bn2 0.
        foreach col {1 2 3 4 5 6} {set en2 [expr {$en2+pow([lindex $raw $col],2)}]}
        foreach col {16 17 18 19 20 21} {set bn2 [expr {$bn2+pow([lindex $raw $col],2)}]}
        if {$bn2>0} {
            set expected [expr {1.25663706212e-6*sqrt($en2/$bn2)}]
            assert {abs($zw-$expected)<1e-9*max(1,abs($expected))} {单模式波阻抗没有使用真实复数 E/B 总幅值}
        } else {assert {$zw eq {}} {磁场节点被人为截断成有限阻抗}}
        set impedance_raw [split [lindex $zw_rows $index] ,]
        assert {[lindex $impedance_raw 1]==[dict get $space snapshot selected_m] && [lindex $impedance_raw 2]==[dict get $space snapshot kz]} {波阻抗 CSV 模态不对应求解快照}
        incr index
}
choose_chart 波阻抗幅值
update
assert {[llength [.body.main.plot find withtag heat_cell]]==0 && [string first {<polyline} [::rf::svg_text]]>=0 && [string first {Ω} [::rf::svg_text]]>=0} {单模式波阻抗径向曲线或 SVG 未绘制}
set f [open /tmp/rf-ui-wave-impedance.svg w];fconfigure $f -encoding utf-8;puts $f [::rf::svg_text];close $f
# Scanning limits and m-sum limits cannot affect a fixed spectral point.
foreach {key value} {kmin -8 kmax 8 nk 7 space.m_min -2 space.m_max 2} {set ::rf::cfg($key) $value}
calculate plasma
assert {[dict get $::rf::cache(plasma) charts] eq [dict get $space charts] && [dict get $::rf::cache(plasma) absorbed]==[dict get $space absorbed]} {等离子体单模场仍依赖扫描带宽或 m 求和范围}
set fixed_kz $::rf::cfg(kz)
set ::rf::cfg(kz) [expr {$fixed_kz+1}]
calculate plasma
assert {[dict get $::rf::cache(plasma) csv] ne [dict get $space csv]} {修改指定 kz 未重新求单模式场}
set ::rf::cfg(kz) $fixed_kz
foreach {key value} {kmin -6 kmax 6 nk 5} {set ::rf::cfg($key) $value}
calculate plasma
set space $::rf::cache(plasma)
# Waveforms still reconstruct the k band and must not overwrite single-mode results.
calculate waveform
set waves $::rf::cache(waveform)
assert {[llength [dict get $waves charts]]==1 && $::rf::cache(plasma) eq $space} {电势波形未去掉电场分量或覆盖了等离子体单模场}
assert {[dict get $waves potential_backward_error]<1e-8} {电势泊松求解残差超限}
set rows [split [string trim [dict get $waves csv]] \n]
assert {[llength $rows]==6818 && [lindex $rows 0] eq {z_m,r_m,Phi_Coulomb_re_V,Phi_Coulomb_im_V,selected_m,potential_backward_error}} {加密波形 CSV 的单位/行数错误}
assert {[dict get $waves snapshot space.nz]==401 && $::rf::cfg(space.nz)==401} {波形没有实际加密采样或参数与快照不一致}
# The one waveform uses Im Phi from the reconstructed scalar potential,
# not Im E, |Phi| or the previous real-electric-field samples.
assert {[lrange [split [lindex $rows 0] ,] 2 3] eq {Phi_Coulomb_re_V Phi_Coulomb_im_V}} {电势 CSV 未标明库仑规范和 V 单位}
set wave [lindex [dict get $waves charts] 0]
    assert {[dict get $wave kind] eq {waterfall}} {波形未使用堆叠图}
    assert {[dict get $wave quantity] eq {Im Φ} && [dict get $wave unit] eq {V}} {电势虚部量名或单位错误}
    assert {[llength [dict get $wave z]]==401 && [llength [dict get $wave r]]==17} {波形网格未加密}
    set row 1
    foreach slice [dict get $wave matrix] {
        foreach e $slice {
            set raw [lindex [split [lindex $rows $row] ,] 3]
            assert {abs($e-$raw)<1e-10*max(1,abs($raw))} {波形未读取复电势虚部或丢失正负号}
            incr row
        }
    }
    assert {[::rf::chart_csv $wave {}] eq [dict get $waves csv]} {波形 CSV 导出了显示位移而非真实电势}
foreach name [button_names .body.main.view] {
    choose_chart $name
    update
    assert {[llength [.body.main.plot find all]]>10} {空间分布图没有绘制}
    assert {[string first {<svg} [::rf::svg_text]]>=0} {空间 SVG 导出失败}
}
select_mode waveform
assert {[button_names .body.main.view] eq [::rf::expected_charts waveform]} {独立波形栏目计算后目录不正确}
choose_chart 电势虚部波形堆叠
update
assert {[llength [.body.main.plot find withtag waterfall_trace]]==401} {堆叠波形仍被截为稀疏线条}
set gain_revision $::rf::revision
set base_gain $::rf::waterfall_gain
.body.main.legend.g2 invoke
assert {abs($::rf::waterfall_gain-2*$base_gain)<1e-12*max(1,abs($base_gain)) && $::rf::revision==$gain_revision && $::rf::cache(waveform) eq $waves} {起伏倍率没有只改变显示或破坏了原始结果}
.body.main.legend.g1 invoke
assert {[string first {Im Φ (V)} [::rf::svg_text]]>=0 && [string first {m/V} [::rf::svg_text]]>=0 && [string first {起伏不是轨迹} [::rf::svg_text]]>=0} {电势波形 SVG 缺少虚部、单位或显示说明}
set f [open /tmp/rf-ui-waterfall.svg w];fconfigure $f -encoding utf-8;puts $f [::rf::svg_text];close $f
# Check a known signed fixture: a tenfold weaker slice must stay tenfold weaker
# on screen; a field of zero must be exactly on its own baseline.
set fixture [dict create kind waterfall quantity {Im Φ} unit V r {0 .15 .3} z {-1 0 1} matrix {{1 -2 0} {.1 -.2 0} {0 0 0}}]
set c .body.main.plot
$c delete all
::rf::draw_waterfall $c $fixture [winfo width $c] [winfo height $c]
set a [$c coords [lindex [$c find withtag waterfall_z_0] 0]]
set b [$c coords [lindex [$c find withtag waterfall_z_1] 0]]
assert {abs(([lindex $a 1]-[lindex $a 5])/([lindex $b 1]-[lindex $b 5])-10)<1e-10} {逐条归一化破坏了波包包络}
assert {[lindex $a 3]>[lindex $a 5] && [lindex $a 1]<[lindex $a 5]} {波形电势虚部的正负绘制颠倒}
::rf::inspect_waterfall $fixture [lindex $a 2] [lindex $a 3]
assert {[string first {z基线=-1 m} $::rf::hover]>=0 && [string first {Im Φ=-2 V} $::rf::hover]>=0} {悬停把波形显示高度当成物理 z 或误用电场单位}
dict set fixture matrix {{0 0 0} {0 0 0} {0 0 0}}
$c delete all
::rf::draw_waterfall $c $fixture [winfo width $c] [winfo height $c]
assert {$::rf::waterfall_gain==0} {零场波形增益无效}
foreach id [$c find withtag waterfall_trace] {
    set xy [$c coords $id]
    assert {[lindex $xy 1]==[lindex $xy 3] && [lindex $xy 3]==[lindex $xy 5]} {零场产生了虚假波动}
}
# Nonuniform coordinates must use physical cell boundaries and actual
# nearest samples, rather than dividing the viewport into equal index bins.
set grid_fixture [dict create kind heatmap unit V xkey z_m xsymbol z xunit m xlabel {轴向位置 z (m)} x {0 .1 1} r {0 .05 .3} matrix {{10 20 30} {40 50 60} {70 80 90}}]
$c delete all
::rf::draw_heatmap $c $grid_fixture 600 400
lassign $::rf::plot_bounds left top right bottom xmin xmax rmin rmax
set first [$c coords [lindex [$c find withtag heat_cell] 0]]
assert {abs([lindex $first 2]-$left-.05*($right-$left))<1e-10 && abs([lindex $first 3]-[lindex $first 1]-.025/.3*($bottom-$top))<1e-10} {热图坐标仍按下标等分，未对齐真实采样位置}
::rf::inspect_heat $grid_fixture [expr {$left+.32*($right-$left)}] [expr {$bottom-.09/.3*($bottom-$top)}]
assert {$::rf::hover eq {z=0.1 m · r=0.05 m · 50 V}} {非均匀热图悬停位置或单位错误}
assert {[llength [$c find withtag colorbar_unit]]==1} {热图未独立标注色标单位}
set grid_csv [::rf::chart_csv $grid_fixture {}]
assert {[lindex [split $grid_csv \n] 0] eq {z_m,r_m,value,error,unit} && [string first {0,.05,20,,V} $grid_csv]>=0} {通用热图 CSV 缺少场量单位}
# Dense drawing retains up to 501 original rows and both endpoints. Increasing
# density cannot shrink the globally normalized oscillation height.
dict set fixture matrix {{1 -2 0} {.1 -.2 0} {0 0 0}}
set sparse_gain [dict get [::rf::waterfall_layout $fixture] gain]
set zs {};for {set j 0} {$j<=600} {incr j} {lappend zs [expr {$j/300.-1}]}
dict set fixture z $zs
dict set fixture matrix [lrepeat 601 {1 -2 0}]
set layout [::rf::waterfall_layout $fixture]
assert {[llength [dict get $layout indices]]==501 && [lindex [dict get $layout indices] end]==600} {密集波形显示抽样丢失端点或超过上限}
assert {abs([dict get $layout gain]-$sparse_gain)<1e-12} {加密堆叠线后波形起伏被压小}
# Narrow charts wrap headers as individual text items so SVG has the same lines.
$c delete all
::rf::draw_waterfall $c $fixture 360 400
foreach id [$c find withtag waterfall_heading] {
    assert {[lindex [$c bbox $id] 2]<=342} {窄窗口的波形图标题或增益说明被裁切}
}
select_mode plasma
choose_chart 局部吸收
update
assert {[string first {半径 r (m)} [::rf::svg_text]]>=0} {单模式 SVG 缺少径向横轴}
set f [open /tmp/rf-ui-spatial.svg w];fconfigure $f -encoding utf-8;puts $f [::rf::svg_text];close $f
select_mode waveform
.header.run invoke
assert {$::rf::busy} {空间任务未分步运行}
after 5 ::rf::stop
await_job
assert {$::rf::cache(plasma) eq $space && $::rf::cache(waveform) eq $waves} {取消波形计算后丢失了完整结果}
select_mode waveform
choose_chart 电势虚部波形堆叠
calculate waveform
assert {$::rf::chart_name eq {电势虚部波形堆叠} && [button_names .body.main.view] eq [::rf::expected_charts waveform]} {波形栏目无法独立发起计算或未保留预选图示}
assert {$::rf::cache(plasma) eq $space} {从波形栏目计算覆盖了等离子体单模场}
assert {[catch {rf_spatial step}]} {取消任务后仍能推进旧任务}
# Begin captures a fixed state even if another client changes the committed project.
rf_spatial begin
rf_state {current 7}
set part [rf_spatial step]
assert {![dict get $part complete]} {空间任务意外同步完成}
rf_spatial cancel
# Keep the following full-field export fixture small; dense waveform sampling
# has already been checked independently above.
set ::rf::cfg(space.nz) 17
# New full-mode category comes immediately after the plasma scan.
set index [lsearch -exact $::rf::modes plasma_scan]
assert {[lindex $::rf::modes [expr {$index+1}]] eq {full} && [lindex $::rf::mode_names [expr {$index+1}]] eq {等离子体总场}} {总场与吸收栏目名称或位置不正确}
foreach {key value} {space.m_min -1 space.m_max 1 space.phi .37} {set ::rf::cfg($key) $value}
select_mode full
assert {[button_names .body.main.view] eq {电场幅值 磁场幅值 轴向能流密度 电流幅值 局部吸收 径向壳层沉积 波阻抗幅值}} {总场与吸收没有补齐七张图}
choose_chart 电流幅值
calculate full
set total $::rf::cache(full)
assert {$::rf::chart_name eq {电流幅值} && [llength [dict get $total charts]]==7} {总场计算后丢失预选图示或未补齐七图}
assert {[dict get $total window_power]>0 && [dict get $total power_balance_error]<1e-8} {全模式未计算吸收或供功不守恒}
assert {[dict get $total snapshot space.phi]==.37 && [dict get $total snapshot space.m_min]==-1 && [dict get $total snapshot space.m_max]==1} {全模式快照范围不正确}
set full_rows [split [string trim [dict get $total csv]] \n]
assert {[llength $full_rows]==290 && [llength [split [lindex $full_rows 0] ,]]==31} {全模式 CSV 网格或列数不正确}
foreach title {电场幅值 磁场幅值 电流幅值} chart_index {0 1 3} offset {3 9 15} unit {V/m T A/m²} {
    choose_chart $title
    set chart [lindex [dict get $total charts] $chart_index]
    assert {[dict get $chart unit] eq $unit && [dict keys [dict get $chart components]] eq {总幅值 {r 分量} {φ 分量} {z 分量}}} {全模式分量选项或单位不正确}
    for {set comp 0} {$comp<4} {incr comp} {
        .body.main.legend.c$comp invoke
        set selected [lindex $::rf::plots $chart_index]
        set row 1
        foreach slice [dict get $selected matrix] {
            foreach value $slice {
                set csv_row [split [lindex $full_rows $row] ,]
                set norm2 0.
                if {$comp==0} {set components {0 1 2}} else {set components [list [expr {$comp-1}]]}
                foreach c $components {
                    set re [lindex $csv_row [expr {$offset+2*$c}]];set im [lindex $csv_row [expr {$offset+2*$c+1}]]
                    set norm2 [expr {$norm2+$re*$re+$im*$im}]
                }
                assert {abs($value-sqrt($norm2))<1e-10*max(1e-20,sqrt($norm2))} {全模式分量图未使用相干重建后的复数场}
                incr row
            }
        }
    }
}
# The total-field wave impedance must be the ratio of the coherent vector
# norms exported in the original E/B CSV, including m/k interference.
choose_chart 波阻抗幅值
set total_zw_chart [lindex [dict get $total charts] 6]
assert {[dict get $total_zw_chart unit] eq {Ω} && ![dict exists $total_zw_chart components]} {总场波阻抗单位或显示方式错误}
set total_zw_rows [split [string trim [::rf::chart_csv $total_zw_chart {}]] \n]
assert {[llength $total_zw_rows]==[llength $full_rows] && [lindex $total_zw_rows 0] eq {z_m,r_m,phi_rad,Z_wave_ohm,valid,m_min,m_max,kmin_rad_m,kmax_rad_m}} {总场波阻抗 CSV 缺少位置、定义或求和范围}
set row 1
foreach slice [dict get $total_zw_chart matrix] {
    foreach zw $slice {
        set raw [split [lindex $full_rows $row] ,]
        set zw_raw [split [lindex $total_zw_rows $row] ,]
        set en2 0.;set bn2 0.
        foreach col {3 4 5 6 7 8} {set en2 [expr {$en2+pow([lindex $raw $col],2)}]}
        foreach col {9 10 11 12 13 14} {set bn2 [expr {$bn2+pow([lindex $raw $col],2)}]}
        if {$bn2>0} {
            set expected [expr {1.25663706212e-6*sqrt($en2/$bn2)}]
            assert {$zw ne {} && abs($zw-$expected)<1e-10*max(1,abs($expected))} {总场波阻抗未按相干总 E/B 计算}
            assert {[lindex $zw_raw 4]==1 && abs([lindex $zw_raw 3]-$zw)<1e-12*max(1,abs($zw))} {总场波阻抗 CSV 数值与热图不一致}
        } else {
            assert {$zw eq {} && [lindex $zw_raw 3] eq {} && [lindex $zw_raw 4]==0} {磁场节点被人为加了分母或填成有限阻抗}
        }
        foreach col {0 1 2} {assert {[lindex $zw_raw $col]==[lindex $raw $col]} {波阻抗位置或 φ 与总场不一致}}
        foreach col {5 6 7 8} source_col {27 28 29 30} {assert {[lindex $zw_raw $col]==[lindex $raw $source_col]} {波阻抗求和范围与总场不一致}}
        incr row
    }
}
update
assert {[string first {Ω} [::rf::svg_text]]>=0} {总场波阻抗 SVG 丢失单位}
set f [open /tmp/rf-ui-full-wave-impedance.svg w];fconfigure $f -encoding utf-8;puts $f [::rf::svg_text];close $f
foreach name [button_names .body.main.view] {
    choose_chart $name
    update
    assert {[llength [.body.main.plot find withtag heat_cell]]>0 && [string first {<svg} [::rf::svg_text]]>=0} {全模式热图或 SVG 未绘制}
}
choose_chart 轴向能流密度
assert {[dict get [lindex $::rf::plots 2] signed]} {全模式能流图没有保留方向符号}
set f [open /tmp/rf-ui-full-flux.svg w];fconfigure $f -encoding utf-8;puts $f [::rf::svg_text];close $f
choose_chart 局部吸收
set f [open /tmp/rf-ui-full-absorption.svg w];fconfigure $f -encoding utf-8;puts $f [::rf::svg_text];close $f
# A signed fixture must render negative, zero and positive with distinct colors.
set signed_fixture [dict create kind heatmap signed 1 unit W/m² x {-1 0 1} r {0 .3} matrix {{-2 -2} {0 0} {2 2}}]
.body.main.plot delete all
::rf::draw_heatmap .body.main.plot $signed_fixture 600 400
set cells [.body.main.plot find withtag heat_cell]
assert {[.body.main.plot itemcget [lindex $cells 0] -fill] eq {#2563b4} && [.body.main.plot itemcget [lindex $cells 2] -fill] eq {#ffffff} && [.body.main.plot itemcget [lindex $cells 4] -fill] eq {#b91c1c}} {负能流被截成零或正负颜色错误}
.header.run invoke
after 5 ::rf::stop
await_job
assert {$::rf::cache(full) eq $total && [catch {rf_full step}]} {停止全模式任务后丢失旧结果或仍能推进任务}
# The native job's snapshot remains fixed across outside state edits.
rf_full begin
rf_state {space.phi 1.2}
while {1} {set part [rf_full step];if {[dict get $part complete]} {break}}
assert {[dict get $part snapshot space.phi]==.37} {全模式计算未冻结输入参数}
rf_full cancel
rf_state $pre_space
::rf::pull
select_mode plasma_scan
set snapshot [dict get $::rf::model snapshot]
set ::rf::cfg(antenna_radius) 0.4
::rf::apply
set before [rf_state]
rf_export_obj $snapshot $scratch/antenna.obj
assert {[rf_state] eq $before} {导出快照修改了当前项目}
assert {[file size $scratch/antenna.obj]>100} {OBJ 导出为空}
set plasma_cache $::rf::cache(plasma)
select_mode scan
assert {$::rf::cfg(plasma_enabled)==0 && [button_names .navigation.tabs] eq $catalogue} {真空与等离子体栏目没有保持并列}
::rf::refresh_model
assert {[dict get $::rf::model snapshot plasma_enabled]==0 && [scene_count plasma_shell]==0} {选择真空栏目后模型未清除等离子体区域}
select_mode plasma
::rf::refresh_model
assert {[scene_count plasma_shell]>100 && $::rf::cache(plasma) eq $plasma_cache} {切回等离子体栏目后模型或旧结果丢失}
select_mode scan
set a $::rf::angle
::rf::model_drag_start 20 20
::rf::model_drag_move 70 40
assert {$::rf::angle!=$a} {模型拖动未改变视角}
::rf::zoom_model 1.1
assert {$::rf::zoom>1} {模型缩放没有生效}
::rf::reset_model
# Check all 38 actual charts at the supported minimum and a larger window.
foreach size {1200x700 1440x950} {
    wm geometry . $size
    update
    set checked 0
    foreach mode {vacuum scan plasma plasma_scan full waveform branches} {
        select_mode $mode
        foreach title [button_names .body.main.view] {
            choose_chart $title
            update
            assert_x_axis_spacing
            incr checked
        }
    }
    assert {$checked==38} {横坐标布局检查未覆盖全部 38 张图}
}
# Left/right anchored endpoint ticks must share the middle tick baseline
# in SVG as well as on the canvas.
set c .body.main.plot
$c delete all
::rf::draw_x_axis $c 60 380 100 0 100 {半径 r (m)}
set axis_svg [::rf::svg_text]
set baseline {}
foreach value {0 25 50 75 100} {
    set pattern [format {<text[^>]* y="([^"]+)"[^>]*>%s</text>} $value]
    assert {[regexp $pattern $axis_svg match y]} {SVG 丢失横坐标数字}
    if {$baseline eq {}} {set baseline $y}
    assert {$y==$baseline} {SVG 端点刻度与中间数字不在同一行}
}
# Missing result views clear previous legends. Minimum and large windows fit.
select_mode scan
assert {[llength $::rf::plots]==5 && [button_names .body.main.view] eq $expected_scan_titles} {切回真空后扫描缓存图示不正确}
unset ::rf::cache(scan)
select_mode scan
assert {[button_names .body.main.view] eq $expected_scan_titles} {未计算时真空扫描目录缺少电流谱幅值}
assert {[winfo children .body.main.legend] eq {}} {未计算页面残留上一个模块的图例}
foreach size {1200x700 1440x950} {
    wm geometry . $size
    update
    assert_button_bounds
    assert_sidebar_layout
    assert {[winfo width .body.main.plot]>300 && [winfo height .body.main.plot]>150} {缩放后绘图区被挤压}
    assert {[winfo width .body.model.canvas]>220 && [winfo height .body.model.canvas]>200} {缩放后中间模型被挤压}
    assert {[winfo y .body.model.canvas]==0 && [winfo height .body.model.canvas]==[winfo height .body.model]} {中间画布没有顶到上下边框}
    assert {[winfo x .body.model.canvas]==0 && [winfo width .body.model.canvas]==[winfo width .body.model]} {中间画布没有铺满中栏}
    assert {[winfo x .body.model.tools]>=0 && [winfo y .body.model.tools]+[winfo height .body.model.tools]<=[winfo height .body.model]} {模型悬浮按钮超出视图区}
    assert {[winfo x .body.side]<[winfo x .body.model] && [winfo x .body.model]<[winfo x .body.main]} {三栏顺序错误}
    set top_y [winfo y .header.open]
    set top_height [winfo height .header.open]
    set previous_right 0
    foreach name {open save more run stop export svg} {
        set w .header.$name
        assert {[winfo y $w]==$top_y && [winfo height $w]==$top_height} {顶部操作按钮没有横向等高对齐}
        assert {[winfo x $w]>=$previous_right} {顶部左右操作组出现重叠}
        set previous_right [expr {[winfo x $w]+[winfo width $w]}]
    }
    assert {$previous_right<=[winfo width .header]} {顶部工具栏超出窗口}
}
assert {$::failures eq {}} "后台错误：$::failures"
puts {GUI PASS: three panes, live geometry, unified vacuum/plasma views, native plasma scan, validation, persistence, cancellation, CSV/SVG/OBJ and resizing}
puts "Test artifacts: $scratch"
