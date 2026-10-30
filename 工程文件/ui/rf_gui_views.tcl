# Render and catalogue helpers, embedded before the main UI script.
namespace eval ::rf {}
# 优先使用图表明确单位，只有一维旧格式才从纵轴末尾括号读取。
proc ::rf::chart_value_unit {chart} {
    if {[dict exists $chart unit]} {return [dict get $chart unit]}
    if {[dict exists $chart ylabel] && [regexp {\(([^()]*)\)$} [dict get $chart ylabel] match unit]} {return $unit}
    return {无量纲}
}
# All renderers reserve the actual tick and wrapped label heights, rather
# than a fixed footer that becomes too small when RFSmall is enlarged.
# 用真实字体和换行高度预留横轴区域，防止刻度数字与轴说明重叠。
proc ::rf::x_axis_layout {c left right height label {extra_height 0}} {
    set id [$c create text 0 0 -anchor nw -font RFSmall -width [expr {max(80,$right-$left)}] -text $label]
    lassign [$c bbox $id] x0 y0 x1 y1
    $c delete $id
    set tick_height [expr {[font metrics RFSmall -linespace]+2}]
    set label_height [expr {$y1-$y0}]
    set bottom [expr {$height-8-$extra_height-$label_height-8-$tick_height-8}]
    return [list $bottom [expr {$bottom+8}] [expr {$bottom+8+$tick_height+8}] $label_height]
}
# 逐步减少刻度数量以适应可用宽度；首尾刻度向内锚定，避免裁切。
proc ::rf::x_axis_ticks {left right xmin xmax} {
    for {set intervals 4} {$intervals>=1} {incr intervals -1} {
        set ticks {};set edge -Inf;set fits 1
        for {set i 0} {$i<=$intervals} {incr i} {
            set x [expr {$left+($right-$left)*$i/double($intervals)}]
            set text [fmt [expr {$xmin+($xmax-$xmin)*$i/double($intervals)}]]
            set width [expr {[font measure RFSmall $text]+2}]
            set anchor [expr {$i==0 ? {nw} : ($i==$intervals ? {ne} : {n})}]
            set start [expr {$i==0 ? $x-1 : ($i==$intervals ? $x-$width+1 : $x-$width/2.)}]
            if {$start<$edge+8} {set fits 0}
            set edge [expr {$start+$width}]
            lappend ticks [list $x $text $anchor]
        }
        if {$fits || $intervals==1} {return $ticks}
    }
}
# 所有图共用相同刻度/说明基线，返回文字最下边缘供分类图例继续排布。
proc ::rf::draw_x_axis {c left right bottom xmin xmax label} {
    set tick_y [expr {$bottom+8}]
    set label_y [expr {$tick_y+[font metrics RFSmall -linespace]+2+8}]
    foreach tick [x_axis_ticks $left $right $xmin $xmax] {
        lassign $tick x text anchor
        $c create text $x $tick_y -text $text -anchor $anchor -font RFSmall -fill #64748b -tags x_tick
    }
    set id [$c create text [expr {($left+$right)/2.}] $label_y -anchor n -font RFSmall -fill #475569 -width [expr {max(80,$right-$left)}] -text $label -tags x_label]
    return [lindex [$c bbox $id] 3]
}
# 计算前后的图名契约一致，用户可先选择图示再计算。
proc ::rf::expected_charts {mode} {
    switch -- $mode {
        branches {return {径向波数实部 径向波数虚部 分支传播状态 群速度 几何光学适用性 射线轴向投影 射线方位角 射线功率衰减}}
        waveform {return {电势虚部波形堆叠}}
        vacuum {return {电场幅值 磁场幅值 轴向能流密度}}
        plasma {return {电场幅值 磁场幅值 轴向能流密度 电流幅值 局部吸收 径向壳层沉积 波阻抗幅值}}
        full {return {电场幅值 磁场幅值 轴向能流密度 电流幅值 局部吸收 径向壳层沉积 波阻抗幅值}}
        scan {return {电场幅值 电流谱幅值 轴向能流谱 总电场谱热图 磁场谱热图}}
        plasma_scan {return {电场幅值 吸收谱 负载阻抗谱 轴向能流谱 总电场谱热图 磁场谱热图 局部吸收谱热图}}
    }
}
# Rounded, antialiased nine-slice surfaces retain native ttk input/focus
# behavior and the same large text as the existing option controls.
# 只生成视觉圆角边界；实际交互仍由原生 ttk 控件负责。
proc ::rf::rounded_option_polygon {size radius inset} {
    set points {}
    foreach {cx cy start} [list $radius $radius 180 [expr {$size-$radius}] $radius 270 [expr {$size-$radius}] [expr {$size-$radius}] 0 $radius [expr {$size-$radius}] 90] {
        for {set step 0} {$step<=12} {incr step} {
            set angle [expr {($start+7.5*$step)*acos(-1)/180}]
            lappend points [expr {$cx+($radius-$inset)*cos($angle)}] [expr {$cy+($radius-$inset)*sin($angle)}]
        }
    }
    return $points
}
# 预生成不同原生控件状态的抗锯齿底图，避免手工模拟点击与键盘焦点。
proc ::rf::option_surface {element category} {
    set outer [rounded_option_polygon 32 9 0]
    set inner [rounded_option_polygon 32 9 1]
    set palette {
        normal #ffffff #dbe3ed
        active #f0f6ff #a7c7f5
        focus #ffffff #2563eb
        pressed #e1edff #7aa8ee
        disabled #f8fafc #e8edf3
        selected #eaf2ff #7aa8ee
        selected_active #dceaff #5d96e5
    }
    if {$category} {
        set palette [lreplace $palette end-5 end selected #2563eb #2563eb selected_active #1d4ed8 #1d4ed8]
    }
    foreach {state fill border} $palette {
        set name ::rf::${element}_${state}
        image create photo $name
        set layers {}
        foreach polygon [list $outer $inner] color [list $border $fill] {
            lassign [lrange $polygon end-1 end] px py
            foreach {x y} $polygon {
                lappend layers [list polygon [list 16 16 $px $py $x $y] $color 1 0 surface]
                set px $x;set py $y
            }
        }
        rf_render_model $name 32 32 $layers #ffffff
        set img($state) $name
    }
    ttk::style element create $element image [list $img(normal) disabled $img(disabled) {selected active} $img(selected_active) selected $img(selected) pressed $img(pressed) active $img(active) focus $img(focus)] -border 10 -padding 0 -sticky news
}
# 选项内容完全可见时移除滚动条，超出时恢复，不改变内容的原始尺寸。
proc ::rf::option_scrollbar {scrollbar first last} {
    $scrollbar set $first $last
    if {$first<=0 && $last>=1} {grid remove $scrollbar} else {grid $scrollbar}
}
# Rectangular tab surfaces keep the seven top-level categories visually joined.
# The dark frame between cells supplies continuous two-pixel separators.
# 七个一级栏目共享矩形表面，由外框提供连续分割线。
proc ::rf::category_tab_surface {} {
    foreach {state fill bottom} {
        normal #f8fafc #f8fafc
        active #e8f0ff #93b4ee
        selected #2563eb #1e40af
        selected_active #1d4ed8 #1e3a8a
        disabled #eef2f6 #eef2f6
    } {
        set name ::rf::category_tab_$state
        image create photo $name -width 8 -height 8
        $name put $fill -to 0 0 8 8
        $name put $bottom -to 0 5 8 8
        set img($state) $name
    }
    ttk::style element create CategoryTab.surface image [list $img(normal) disabled $img(disabled) {selected active} $img(selected_active) selected $img(selected) active $img(active) focus $img(active)] -border {1 1 1 3} -padding 0 -sticky news
}
# 按固定顺序构造七个原生单选栏目，不因计算缓存是否存在而增删。
proc ::rf::category_tabs {parent labels} {
    foreach child [winfo children $parent] {destroy $child}
    set i 0
    foreach label $labels {
        ttk::radiobutton $parent.b$i -text $label -value $label -variable ::rf::selected_mode -command ::rf::select_mode -style FixedCategory.TRadiobutton
        grid $parent.b$i -row 0 -column $i -sticky nsew -padx 1 -pady 1
        grid columnconfigure $parent $i -weight 1
        incr i
    }
    fit_category_tabs [expr {[winfo width .body.main]-24}]
}
# 宽度不足时整体隐藏栏目，加宽后恢复，保留完整名称和固定字号。
proc ::rf::fit_category_tabs {width} {
    set bar .body.main.tools.categories
    if {![winfo exists $bar.tabs]} {return}
    set needed 4
    foreach button [winfo children $bar.tabs] {incr needed [expr {[winfo reqwidth $button]+2}]}
    if {$width>=$needed} {
        grid $bar
    } else {
        grid remove $bar
    }
}
# Keep enlarged options scrollable while preserving the result canvas.
# 只给二级选项创建有限高度滚动容器，避免挤占结果绘图区。
proc ::rf::scroll_options {parent container cap} {
    ttk::frame $container -style White.TFrame
    canvas $container.c -background #ffffff -highlightthickness 0 -width 1 -height 1
    ttk::scrollbar $container.s -orient vertical -command [list $container.c yview]
    $container.c configure -yscrollcommand [list ::rf::option_scrollbar $container.s]
    grid $container.c -row 0 -column 0 -sticky ew
    grid $container.s -row 0 -column 1 -sticky ns
    grid columnconfigure $container 0 -weight 1
    $container.c create window 0 0 -anchor nw -window $parent -tags options
    raise $parent $container
    set ::rf::option_canvases($parent) $container.c
    set ::rf::option_caps($parent) $cap
    bind $container.c <Configure> [list ::rf::size_options $parent %w]
}
# 选项容器宽度变化时重新排布按钮，画布内部窗口同步使用新宽度。
proc ::rf::size_options {parent width} {
    set c $::rf::option_canvases($parent)
    $c itemconfigure options -width $width
    layout_option_buttons $parent $width
}
# 重建当前图示的原生单选按钮，并保留 Tcl 变量驱动的选中状态。
proc ::rf::option_buttons {parent labels variable command {style Option.TRadiobutton}} {
    foreach child [winfo children $parent] {destroy $child}
    set i 0
    foreach label $labels {
        ttk::radiobutton $parent.b$i -text $label -value $label -variable $variable -command $command -style $style
        incr i
    }
    bind $parent <Configure> {::rf::layout_option_buttons %W %w}
    layout_option_buttons $parent [winfo width $parent]
    if {[info exists ::rf::option_canvases($parent)]} {$::rf::option_canvases($parent) yview moveto 0}
}
# Place buttons by their actual requested sizes, without grid column alignment
# adding gaps or making one row overflow because another row has longer labels.
# 按实际请求宽度逐行放置，行高取本行最大值，不用统一网格制造大空白。
proc ::rf::layout_option_buttons {parent width} {
    if {![winfo exists $parent] || $width<40} {return}
    set x 0;set y 0;set row_height 0;set row_ends {}
    foreach button [winfo children $parent] {
        set w [expr {min($width,[winfo reqwidth $button])}]
        set h [winfo reqheight $button]
        if {$x>0 && $x+$w>$width} {
            lappend row_ends [expr {$y+$row_height}]
            set x 0;incr y [expr {$row_height+6}];set row_height 0
        }
        place $button -x $x -y $y -width $w -height $h
        incr x [expr {$w+8}]
        set row_height [expr {max($row_height,$h)}]
    }
    set height [expr {max(1,$y+$row_height)}]
    lappend row_ends $height
    if {[winfo reqheight $parent]!=$height} {$parent configure -height $height}
    if {[info exists ::rf::option_canvases($parent)]} {
        set c $::rf::option_canvases($parent)
        set cap [expr {[winfo height .]<760 ? 50 : $::rf::option_caps($parent)}]
        set visible [lindex $row_ends 0]
        foreach end $row_ends {if {$end<=$cap} {set visible $end}}
        $c configure -height $visible -scrollregion [list 0 0 $width $height]
    }
}
# 只有旧选择不存在于新目录时才回退到第一张图，计算后保留用户预选。
proc ::rf::chart_choices {names} {
    set ::rf::chart_names $names
    if {[lsearch -exact $names $::rf::chart_name]<0} {set ::rf::chart_name [lindex $names 0]}
    option_buttons .body.main.view $names ::rf::chart_name ::rf::select_chart
}
# r 在 φ=0 射线上，φ 为绕 +z 的正向角；小轴与模型使用同一相机投影。
proc ::rf::model_cylindrical_axes {c left bottom} {
    # r is the radial ray at phi=0. Positive phi is counterclockwise viewed
    # from +z; project its circular arc with the same camera as the model.
    foreach xyz {{0 0 1} {1 0 0}} label {z r} color {#2563eb #ef4444} {
        lassign [project_point {*}$xyz] x y
        set ex [expr {38*$x}];set ey [expr {38*$y}]
        $c create line 0 0 $ex $ey -arrow last -fill $color -width 1.5 -tags axes
        set norm [expr {hypot($x,$y)}]
        if {$norm<.01} {set x 1.;set y 0.;set norm 1.}
        set id [$c create text [expr {$ex+12*$x/$norm}] [expr {$ey+12*$y/$norm}] -text $label -fill $color -font RFSmall -tags {axes axis_label}]
        # Near an edge-on view the projected r and z directions may coincide.
        # Keep their labels distinct while preserving the projected arrows.
        foreach previous [$c find withtag axis_label] {
            if {$previous==$id} {continue}
            for {set j 0} {$j<8} {incr j} {
                lassign [$c bbox $previous] ax ay bx by
                lassign [$c bbox $id] xx yy xxx yyy
                if {$xxx+4<$ax || $xx>$bx+4 || $yyy+4<$ay || $yy>$by+4} {break}
                $c move $id 16 0
            }
        }
    }
    set arc {};set xmin Inf;set xmax -Inf;set ymax -Inf
    for {set j 0} {$j<=32} {incr j} {
        set phi [expr {125*acos(-1)/180.*$j/32.}]
        lassign [project_point [expr {cos($phi)}] [expr {sin($phi)}] 0] x y
        set x [expr {24*$x}];set y [expr {24*$y}]
        lappend arc $x $y
        set xmin [expr {min($xmin,$x)}];set xmax [expr {max($xmax,$x)}];set ymax [expr {max($ymax,$y)}]
    }
    $c create line 0 0 {*}[lrange $arc end-1 end] -fill #94a3b8 -dash {2 3} -width 1 -tags axes
    $c create line {*}$arc -arrow last -arrowshape {6 7 3} -fill #059669 -width 1.5 -tags {axes axis_phi_arc}
    set id [$c create text [expr {($xmin+$xmax)/2.}] [expr {$ymax+14}] -text φ -fill #059669 -font RFSmall -tags {axes axis_label}]
    foreach previous [$c find withtag axis_label] {
        if {$previous==$id} {continue}
        for {set j 0} {$j<8} {incr j} {
            lassign [$c bbox $previous] ax ay bx by
            lassign [$c bbox $id] xx yy xxx yyy
            if {$xxx+4<$ax || $xx>$bx+4 || $yyy+4<$ay || $yy>$by+4} {break}
            $c move $id 0 16
        }
    }
    $c create oval -1.5 -1.5 1.5 1.5 -fill #64748b -outline {} -tags axes
    lassign [$c bbox axes] x0 y0 x1 y1
    $c move axes [expr {$left-$x0}] [expr {$bottom-$y1}]
}
# 返回沿相机深度方向的米制坐标，仅用于远到近的画家排序。
proc ::rf::model_depth {x y z} {
    set a [expr {$::rf::angle*acos(-1)/180}];set e [expr {$::rf::elevation*acos(-1)/180}]
    return [expr {cos($e)*($x*sin($a)+$y*cos($a))+sin($e)*$z}]
}
# 把三维面片转换为像素多边形并记录平均深度，不改变物理顶点。
proc ::rf::model_face {layers_var vertices color alpha tag cx cy scale} {
    upvar 1 $layers_var layers
    set points {};set depth 0.;set n 0
    foreach {x y z} $vertices {
        lassign [project_point $x $y $z] sx sy
        lappend points [expr {$cx+$scale*$sx}] [expr {$cy+$scale*$sy}]
        set depth [expr {$depth+[model_depth $x $y $z]}];incr n
    }
    lappend layers [list [expr {$depth/$n}] [list polygon $points $color $alpha 0 $tag]]
}
# 线元和箭头共享一个深度层；电流箭头采用 PDF 的深蓝色，方向由调用方决定。
proc ::rf::model_line {layers_var vertices color alpha width tag cx cy scale {arrow none}} {
    upvar 1 $layers_var layers
    lassign $vertices x y z xx yy zz
    set depth [expr {([model_depth $x $y $z]+[model_depth $xx $yy $zz])/2}]
    lassign [project_point $x $y $z] sx sy
    lassign [project_point $xx $yy $zz] ex ey
    set sx [expr {$cx+$scale*$sx}];set sy [expr {$cy+$scale*$sy}]
    set ex [expr {$cx+$scale*$ex}];set ey [expr {$cy+$scale*$ey}]
    lappend layers [list $depth [list line [list $sx $sy $ex $ey] $color $alpha $width $tag]]
    if {$arrow ne {none}} {
        if {$arrow eq {first}} {set tx $sx;set ty $sy;set dx [expr {$sx-$ex}];set dy [expr {$sy-$ey}]} else {set tx $ex;set ty $ey;set dx [expr {$ex-$sx}];set dy [expr {$ey-$sy}]}
        set len [expr {hypot($dx,$dy)}]
        if {$len>0.01} {
            set dx [expr {$dx/$len}];set dy [expr {$dy/$len}]
            # PDF navy = RGB(32,62,83). Keep the source path's own color;
            # only the arrow shaft and head use the contrasting dark color.
            set arrow_color #203e53
            set shaft [list [expr {$tx-13*$dx}] [expr {$ty-13*$dy}] $tx $ty]
            lappend layers [list $depth [list line $shaft $arrow_color 1.0 1.35 $tag]]
            set points [list $tx $ty [expr {$tx-9*$dx+3*$dy}] [expr {$ty-9*$dy-3*$dx}] [expr {$tx-9*$dx-3*$dy}] [expr {$ty-9*$dy+3*$dx}]]
            lappend layers [list $depth [list polygon $points $arrow_color 1.0 0 $tag]]
        }
    }
}
# 只绘制轴向延伸的圆柱侧面；视窗截断不绘制端盖或人为淡化边界。
proc ::rf::model_shell {layers_var radius length color alpha tag cx cy scale} {
    upvar 1 $layers_var layers
    # Uniform lateral surfaces extend beyond the viewport and are clipped by
    # the renderer. No fading, end rims or end plates are introduced.
    for {set j 0} {$j<64} {incr j} {
        set phi [expr {2*acos(-1)*$j/64}];set next [expr {2*acos(-1)*($j+1)/64}]
        set x [expr {$radius*cos($phi)}];set y [expr {$radius*sin($phi)}]
        set xx [expr {$radius*cos($next)}];set yy [expr {$radius*sin($next)}]
        for {set iz 0} {$iz<48} {incr iz} {
            set u [expr {-0.5+$iz/48.}];set uu [expr {$u+1./48}]
            set z [expr {$length*$u}];set zz [expr {$length*$uu}]
            model_face layers [list $x $y $z $xx $yy $z $xx $yy $zz $x $y $zz] $color $alpha $tag $cx $cy $scale
        }
    }
    foreach phi [list [expr {-$::rf::angle*acos(-1)/180}] [expr {acos(-1)-$::rf::angle*acos(-1)/180}]] {
        set x [expr {$radius*cos($phi)}];set y [expr {$radius*sin($phi)}]
        for {set iz 0} {$iz<48} {incr iz} {
            set u [expr {-0.5+$iz/48.}];set uu [expr {$u+1./48}]
            model_line layers [list $x $y [expr {$length*$u}] $x $y [expr {$length*$uu}]] $color .40 0.9 ${tag}_edge $cx $cy $scale
        }

    }
}
# 颜色区分规定电流支路，近零幅度显示淡色而不伪造电流。
proc ::rf::model_current_color {group amplitude} {
    if {abs($amplitude)<1e-10} {return #cbd5e1}
    return [lindex {#ed8b18 #dc4b36 #e5b625 #a855f7} [expr {int($group)%4}]]
}
# 按真实带面边界构造图元后排序，像素合成仅影响显示，不回写几何。
proc ::rf::draw_model_solids {c r snap cx cy scale length image_height image_y} {
    set layers {}
    if {$::rf::show_vessel} {
        model_shell layers [dict get $r radius] $length #729caf 0.10 vessel_shell $cx $cy $scale
    }
    if {[dict get $snap plasma_enabled]} {
        model_shell layers [dict get $snap plasma.radius] $length #287ce5 0.24 plasma_shell $cx $cy $scale
    }
    foreach face [dict get $r band_faces] {
        lassign [lrange $face 12 14] group phase amplitude
        model_face layers [lrange $face 0 11] [model_current_color $group $amplitude] 1.0 antenna $cx $cy $scale
    }
    foreach line [dict get $r band_lines] {
        lassign [lrange $line 6 8] group phase amplitude
        model_line layers [lrange $line 0 5] [model_current_color $group $amplitude] 1.0 1.2 antenna $cx $cy $scale
    }
    # A zero-width current is a line. Subpixel bands get a thin visibility
    # guide; finite band boundaries above always retain their physical size.
    set guide [expr {[dict get $snap band_width]*$scale<1.2 && [dict get $r count]<5000}]
    set i 0
    foreach seg [dict get $r segments] {
        lassign $seg x y z xx yy zz group phase amplitude
        set color [model_current_color $group $amplitude]
        set real_current [expr {[dict get $snap current_direction]*$amplitude*cos($phase+[dict get $snap global_phase_deg]*acos(-1)/180.)}]
        set direction [expr {$real_current<0 ? {first} : {last}}]
        set arrow [expr {$i%32==16 && abs($real_current)>1e-10 ? $direction : {none}}]
        if {[dict get $snap band_width]==0. || $guide} {
            model_line layers [list $x $y $z $xx $yy $zz] $color 1.0 1.2 antenna $cx $cy $scale $arrow
        } elseif {$arrow ne {none}} {
            model_line layers [list $x $y $z $xx $yy $zz] $color 1.0 1.0 antenna_current $cx $cy $scale $arrow
        }
        incr i
    }
    set ::rf::model_scene {}
    foreach layer [lsort -real -index 0 $layers] {lappend ::rf::model_scene [lindex $layer 1]}
    if {[lsearch -exact [image names] ::rf::model_image]<0} {image create photo ::rf::model_image}
    rf_render_model ::rf::model_image [winfo width $c] $image_height $::rf::model_scene [$c cget -background]
    $c create image 0 $image_y -anchor nw -image ::rf::model_image -tags model_render

}
# 正幅值热图使用固定连续色带，输入是已经归一化的显示值。
proc ::rf::heat_color {value} {
    set stops {{30 41 95} {37 99 180} {21 167 171} {114 204 109} {253 225 72}}
    set t [expr {max(0.,min(1.,$value))*4}];set lo [expr {min(3,int($t))}];set f [expr {$t-$lo}]
    set rgb {}
    foreach a [lindex $stops $lo] b [lindex $stops [expr {$lo+1}]] {lappend rgb [expr {int($a*(1-$f)+$b*$f)}]}
    return [format {#%02x%02x%02x} {*}$rgb]
}
# 有符号能流使用蓝/白/红对称色带，白色严格对应零。
proc ::rf::signed_heat_color {value} {
    set t [expr {max(0.,min(1.,$value))}]
    set a [expr {$t<0.5 ? {37 99 180} : {255 255 255}}]
    set b [expr {$t<0.5 ? {255 255 255} : {185 28 28}}]
    set f [expr {$t<0.5 ? 2*$t : 2*$t-1}]
    set rgb {}
    foreach x $a y $b {lappend rgb [expr {int(round($x*(1-$f)+$y*$f))}]}
    return [format {#%02x%02x%02x} {*}$rgb]
}
# 正幅值用四个数量级的对数色标，带符号量用对称线性色标；空样本不着色。
proc ::rf::draw_heatmap {c chart w h} {
    set xs [dict get $chart x];set rs [dict get $chart r];set matrix [dict get $chart matrix]
    set nk [llength $xs];set nr [llength $rs]
    set max 0.
    set signed [expr {[dict exists $chart signed] && [dict get $chart signed]}]
    foreach row $matrix {foreach y $row {if {$y ne {}} {set max [expr {max($max,$signed ? abs($y) : $y)}]}}}
    # Four decades of dynamic range show weak structure without log(0).
    set floor [expr {$max>0 ? $max*1e-4 : 1.}]
    set left 74;set top 42;set right [expr {$w-88}]
    set xlabel [expr {[dict exists $chart xlabel] ? [dict get $chart xlabel] : {轴向波数 k_z (rad/m)}}]
    lassign [x_axis_layout $c $left $right $h $xlabel] bottom
    set ::rf::plot_bounds [list $left $top $right $bottom [lindex $xs 0] [lindex $xs end] [lindex $rs 0] [lindex $rs end]]
    set dx [expr {max(1,int(ceil($nk/140.0)))}];set dr [expr {max(1,int(ceil($nr/80.0)))}]
    for {set i 0} {$i<$nk} {incr i $dx} {
        set ix [expr {min($nk-1,$i+$dx/2)}];set row [lindex $matrix $ix]
        # Never paint across an invalid slice when display columns are binned.
        for {set q $i} {$q<min($nk,$i+$dx)} {incr q} {if {[lindex $matrix $q] eq {}} {set row {};break}}
        for {set j 0} {$j<$nr} {incr j $dr} {
            if {$row eq {}} {continue}
            set y [lindex $row [expr {min($nr-1,$j+$dr/2)}]]
            if {$y eq {}} {continue}
            set t [expr {$signed ? ($max>0 ? .5+.5*$y/$max : .5) : ($max>0 ? (log10(max($y,$floor))-log10($floor))/4. : 0.)}]
            lassign [sample_cell_edges $xs $i $dx] xa xb
            lassign [sample_cell_edges $rs $j $dr] ra rb
            set x0 [expr {$left+($xa-[lindex $xs 0])/([lindex $xs end]-[lindex $xs 0])*($right-$left)}]
            set x1 [expr {$left+($xb-[lindex $xs 0])/([lindex $xs end]-[lindex $xs 0])*($right-$left)}]
            set y0 [expr {$bottom-($ra-[lindex $rs 0])/([lindex $rs end]-[lindex $rs 0])*($bottom-$top)}]
            set y1 [expr {$bottom-($rb-[lindex $rs 0])/([lindex $rs end]-[lindex $rs 0])*($bottom-$top)}]
            $c create rectangle $x0 $y1 $x1 $y0 -fill [expr {$signed ? [signed_heat_color $t] : [heat_color $t]}] -outline {} -tags heat_cell
        }
    }
    for {set j 0} {$j<64} {incr j} {
        set y0 [expr {$bottom-($bottom-$top)*$j/64.}];set y1 [expr {$bottom-($bottom-$top)*($j+1)/64.}]
        set t [expr {$j/63.}]
        $c create rectangle [expr {$right+16}] $y1 [expr {$right+28}] $y0 -fill [expr {$signed ? [signed_heat_color $t] : [heat_color $t]}] -outline {}
    }
    $c create rectangle $left $top $right $bottom -outline #cbd5e1
    for {set i 0} {$i<=4} {incr i} {
        set y [expr {$bottom-($bottom-$top)*$i/4.}]
        set rv [expr {[lindex $rs 0]+([lindex $rs end]-[lindex $rs 0])*$i/4.}]
        $c create text [expr {$left-10}] $y -text [fmt $rv] -anchor e -font RFSmall -fill #64748b
        $c create text [expr {$right+34}] $y -text [fmt [expr {$signed ? $max*($i/2.-1) : ($max>0 ? $floor*pow(10,$i) : 0.)}]] -anchor w -font RFSmall -fill #64748b
    }
    $c create text $left 16 -text {半径 r (m)} -anchor w -font RFSmall -fill #475569
    $c create text [expr {$right+16}] 16 -text [dict get $chart unit] -anchor w -font RFSmall -fill #475569 -tags colorbar_unit
    draw_x_axis $c $left $right $bottom [lindex $xs 0] [lindex $xs end] $xlabel
}
# 按相邻真实采样坐标的中点确定格子边界，不能假设非均匀坐标等间距。
proc ::rf::sample_cell_edges {values start count} {
    set end [expr {min([llength $values],$start+$count)-1}]
    set lo [lindex $values $start];set hi [lindex $values $end]
    if {$start>0} {set lo [expr {([lindex $values [expr {$start-1}]]+$lo)/2.}]}
    if {$end<[llength $values]-1} {set hi [expr {($hi+[lindex $values [expr {$end+1}]])/2.}]}
    return [list $lo $hi]
}
# 悬停返回最近原始网格值与实际坐标，不读取颜色插值后的显示值。
proc ::rf::inspect_heat {chart px py} {
    lassign $::rf::plot_bounds left top right bottom xmin xmax rmin rmax
    if {$px<$left || $px>$right || $py<$top || $py>$bottom} {set ::rf::hover {};return}
    set xs [dict get $chart x];set rs [dict get $chart r]
    set i [nearest_sample $xs [expr {$xmin+($px-$left)/double($right-$left)*($xmax-$xmin)}]]
    set j [nearest_sample $rs [expr {$rmin+($bottom-$py)/double($bottom-$top)*($rmax-$rmin)}]]
    set row [lindex [dict get $chart matrix] $i]
    set value [lindex $row $j]
    set symbol [expr {[dict exists $chart xsymbol] ? [dict get $chart xsymbol] : {k_z}}]
    set xunit [expr {[dict exists $chart xunit] ? [dict get $chart xunit] : ($symbol eq {z} ? {m} : {rad/m})}]
    set ::rf::hover "$symbol=[fmt [lindex $xs $i]] $xunit · r=[fmt [lindex $rs $j]] m · [expr {$value eq {} ? {无效采样点} : [fmt $value]}] [dict get $chart unit]"
}
# A waterfall is a set of signed radial potential slices, not a ray plot. The
# matrix always remains in V. One gain for the entire matrix preserves the
# relative envelope; in particular, never normalize individual z slices.
# 全部 z 切片共用一个 m/V 增益，保留相对包络；显示起伏不是物理轨迹。
proc ::rf::waterfall_layout {chart} {
    set zs [dict get $chart z];set n [llength $zs];set count [expr {min(501,$n)}]
    set indices {}
    for {set j 0} {$j<$count} {incr j} {
        lappend indices [expr {int(round(double($j)*($n-1)/($count-1)))}]
    }
    set peak 0.
    foreach row [dict get $chart matrix] {foreach e $row {set peak [expr {max($peak,abs($e))}]}}
    set span [expr {[lindex $zs end]-[lindex $zs 0]}]
    # A fixed fraction of the window preserves visible oscillation height
    # when the trace density is raised. One multiplier applies to all slices.
    set amplitude [expr {.06*$span*$::rf::waterfall_scale}]
    set gain [expr {$peak>0 ? $amplitude/$peak : 0.}]
    return [dict create indices $indices peak $peak gain $gain pad [expr {$amplitude*1.15}]]
}
# Separate canvas items keep narrow-window wrapping identical in SVG exports.
# 中文标题逐字符分行成独立画布项，使窄窗口导出仍保留同样的行序。
proc ::rf::waterfall_heading {c x y width text color} {
    set line {}
    foreach char [split $text {}] {
        if {$line ne {} && [font measure RFSmall "$line$char"]>$width} {
            set id [$c create text $x $y -anchor nw -font RFSmall -fill $color -text $line -tags waterfall_heading]
            set y [expr {[lindex [$c bbox $id] 3]+1}]
            set line {}
        }
        append line $char
    }
    if {$line ne {}} {
        set id [$c create text $x $y -anchor nw -font RFSmall -fill $color -text $line -tags waterfall_heading]
        set y [lindex [$c bbox $id] 3]
    }
    return $y
}
# Im Φ 的矩阵单位保持 V，纵向仅将共同增益乘值叠加到真实 z 基线上。
proc ::rf::draw_waterfall {c chart w h} {
    set rs [dict get $chart r];set zs [dict get $chart z];set matrix [dict get $chart matrix]
    set layout [waterfall_layout $chart];set gain [dict get $layout gain]
    set indices [dict get $layout indices];set pad [dict get $layout pad]
    set rmin [lindex $rs 0];set rmax [lindex $rs end]
    set zmin [expr {[lindex $zs 0]-$pad}];set zmax [expr {[lindex $zs end]+$pad}]
    set left 76;set right [expr {$w-20}]
    lassign [x_axis_layout $c $left $right $h {半径 r (m)}] bottom
    set ::rf::waterfall_gain $gain
    set ::rf::waterfall_indices $indices
    set field_unit [dict get $chart unit]
    set y [waterfall_heading $c $left 5 [expr {$right-$left}] "轴向基线 z (m) · [dict get $chart quantity] ($field_unit) · φ=0" #475569]
    set y [waterfall_heading $c $left [expr {$y+5}] [expr {$right-$left}] "[llength $indices]/[llength $zs] 条 · 增益 [fmt $gain] m/$field_unit · 起伏不是轨迹" #64748b]
    set top [expr {$y+12}]
    set ::rf::plot_bounds [list $left $top $right $bottom $rmin $rmax $zmin $zmax]
    for {set j 0} {$j<=4} {incr j} {
        set z [expr {[lindex $zs 0]+([lindex $zs end]-[lindex $zs 0])*$j/4.}]
        set y [expr {$bottom-($z-$zmin)/($zmax-$zmin)*($bottom-$top)}]
        $c create text [expr {$left-10}] $y -anchor e -font RFSmall -fill #64748b -text [fmt $z]
    }
    foreach tick [x_axis_ticks $left $right $rmin $rmax] {
        set x [lindex $tick 0]
        $c create line $x $top $x $bottom -fill #f1f5f9
    }
    set j 0
    foreach index $indices {
        set z [lindex $zs $index]
        set baseline [expr {$bottom-($z-$zmin)/($zmax-$zmin)*($bottom-$top)}]
        # Sparse baselines help identify the reference height without creating
        # an extra grey line under every almost-zero field trace.
        set highlight [expr {$j%25==0 || $j==[llength $indices]-1}]
        if {$highlight} {$c create line $left $baseline $right $baseline -fill #e2e8f0 -tags waterfall_baseline}
        set points {}
        foreach r $rs e [lindex $matrix $index] {
            lappend points [expr {$left+($r-$rmin)/($rmax-$rmin)*($right-$left)}] [expr {$baseline-$gain*$e/($zmax-$zmin)*($bottom-$top)}]
        }
        $c create line $points -fill [expr {$highlight ? {#2563eb} : {#475569}}] -width [expr {$highlight ? .9 : .45}] -tags [list waterfall_trace waterfall_z_$index]
        incr j
    }
    $c create line $left $top $left $bottom $right $bottom -fill #cbd5e1
    draw_x_axis $c $left $right $bottom $rmin $rmax {半径 r (m)}
}
# 按坐标距离选择最近样本，适用于非均匀自适应采样。
proc ::rf::nearest_sample {values target} {
    set best 0;set distance Inf;set j 0
    foreach value $values {
        set delta [expr {abs($value-$target)}]
        if {$delta<$distance} {set best $j;set distance $delta}
        incr j
    }
    return $best
}
# 在指针半径处寻找最近显示曲线，报告真实 z 基线及原始电势，而非显示偏移。
proc ::rf::inspect_waterfall {chart px py} {
    lassign $::rf::plot_bounds left top right bottom rmin rmax zmin zmax
    set rs [dict get $chart r];set zs [dict get $chart z]
    set ir [nearest_sample $rs [expr {$rmin+($px-$left)/double($right-$left)*($rmax-$rmin)}]]
    # Select the nearest displayed trace at the pointer's radius, then report
    # its actual z baseline, not the vertically displaced display coordinate.
    set best Inf;set iz 0
    foreach index $::rf::waterfall_indices {
        set e [lindex [dict get $chart matrix] $index $ir]
        set y [expr {$bottom-([lindex $zs $index]+$::rf::waterfall_gain*$e-$zmin)/($zmax-$zmin)*($bottom-$top)}]
        if {abs($y-$py)<$best} {set best [expr {abs($y-$py)}];set iz $index;set yy $y}
    }
    set e [lindex [dict get $chart matrix] $iz $ir]
    set xx [expr {$left+([lindex $rs $ir]-$rmin)/($rmax-$rmin)*($right-$left)}]
    .body.main.plot create line $xx $top $xx $bottom -fill #94a3b8 -dash {3 3} -tags cursor
    .body.main.plot create line [expr {$xx-4}] $yy [expr {$xx+4}] $yy -fill #dc2626 -width 3 -tags cursor
    set ::rf::hover "z基线=[fmt [lindex $zs $iz]] m · r=[fmt [lindex $rs $ir]] m · [dict get $chart quantity]=[fmt $e] [dict get $chart unit] · φ=0"
}
# 优先使用原生完整复数 CSV；通用热图导出保留失败切片及其原因。
proc ::rf::chart_csv {chart fallback} {
    if {[dict exists $chart csv]} {return [dict get $chart csv]}
    if {![dict exists $chart kind] || [dict get $chart kind] ne {heatmap}} {return $fallback}
    set xkey [expr {[dict exists $chart xkey] ? [dict get $chart xkey] : {kz_rad_per_m}}]
    set text "$xkey,r_m,value,error,unit\n";set rs [dict get $chart r]
    set unit [dict get $chart unit]
    foreach x [dict get $chart x] row [dict get $chart matrix] {
        if {$row eq {}} {
            set reason [expr {[dict exists $chart errors $x] ? [dict get $chart errors $x] : {invalid slice}}]
            set reason [string map [list \" \"\"] $reason]
            foreach r $rs {append text "$x,$r,,\"$reason\",$unit\n"}
            continue
        }
        foreach r $rs y $row {append text "$x,$r,$y,,$unit\n"}
    }
    return $text
}

# Categorical dispersion states are intervals, never interpolated y coordinates.
# 顺序与核心 RF_BRANCH_* 枚举一致；这是离散分类而不是连续物理量。
proc ::rf::branch_state_labels {} {return {传播候选 倏逝/转向 强阻尼 根退化 奇异根 材料极点}}
# 每类状态固定颜色，跨图切换不重新按数值大小生成渐变。
proc ::rf::branch_state_colors {} {return {#bbf7d0 #e2e8f0 #fed7aa #ddd6fe #fecaca #fda4af}}
# 每个已勾选分支占一条状态带，相邻同类区间可合并但不作类别插值。
proc ::rf::draw_branch_status {c chart w h} {
    set xs [dict get $chart x];set xmin [lindex $xs 0];set xmax [lindex $xs end]
    set labels [branch_state_labels];set colors [branch_state_colors]
    set legend_width 0
    foreach label $labels {set legend_width [expr {max($legend_width,[font measure RFSmall $label]+32)}]}
    set cols [expr {max(1,min(3,int(($w-100)/$legend_width)))}]
    set legend_rows [expr {int(ceil(6./$cols))}]
    set legend_height [expr {[font metrics RFSmall -linespace]+6}]
    set left 82;set right [expr {$w-18}];set top 34
    lassign [x_axis_layout $c $left $right $h {半径 r (m)} [expr {8+$legend_rows*$legend_height}]] bottom
    set ::rf::status_rows {};set j 0
    foreach s [dict get $chart series] {if {$::rf::enabled($j)} {lappend ::rf::status_rows $s};incr j}
    set count [llength $::rf::status_rows]
    if {!$count} {$c create text [expr {$w/2}] [expr {$h/2}] -text {请勾选要查看的分支} -fill #64748b;unset -nocomplain ::rf::plot_bounds;return}
    set ::rf::plot_bounds [list $left $top $right $bottom $xmin $xmax 0 $count]
    $c create text $left 12 -anchor w -text {各分支在不同半径的状态（颜色分类）} -font RFSmall -fill #475569
    set lane [expr {($bottom-$top)/double($count)}];set row 0
    foreach s $::rf::status_rows {
        set y0 [expr {$top+$lane*$row+5}];set y1 [expr {$top+$lane*($row+1)-5}]
        set row_label [expr {[dict exists $s short_label] ? [dict get $s short_label] : [dict get $s label]}]
        $c create text [expr {$left-10}] [expr {($y0+$y1)/2}] -anchor e -text $row_label -font RFSmall -fill #475569
        set ys [dict get $s y];set n [llength $xs]
        # Paired x samples permit a discontinuity without joining two materials.
        set run_state {};set run_lo 0.;set run_hi 0.
        for {set i 1} {$i<$n} {incr i 2} {
            set state [lindex $ys $i];if {$state eq {}} {continue};set state [expr {int($state)}]
            set x [lindex $xs $i]
            set lo [expr {$i>1 ? ($x+[lindex $xs [expr {$i-2}]])/2 : $xmin}]
            set hi [expr {$i+2<$n ? ($x+[lindex $xs [expr {$i+2}]])/2 : $xmax}]
            if {$run_state ne {} && $state!=$run_state} {
                branch_state_cell $c $left $right $xmin $xmax $run_lo $run_hi $y0 $y1 [lindex $colors $run_state]
                set run_state {}
            }
            if {$run_state eq {}} {set run_state $state;set run_lo $lo}
            set run_hi $hi
        }
        if {$run_state ne {}} {branch_state_cell $c $left $right $xmin $xmax $run_lo $run_hi $y0 $y1 [lindex $colors $run_state]}
        $c create rectangle $left $y0 $right $y1 -outline #cbd5e1
        incr row
    }
    set label_bottom [draw_x_axis $c $left $right $bottom $xmin $xmax {半径 r (m)}]
    for {set i 0} {$i<6} {incr i} {
        set x [expr {$left+($i%$cols)*$legend_width}];set y [expr {$label_bottom+8+($i/$cols)*$legend_height+$legend_height/2.}]
        $c create rectangle $x [expr {$y-5}] [expr {$x+12}] [expr {$y+5}] -fill [lindex $colors $i] -outline #cbd5e1
        $c create text [expr {$x+17}] $y -anchor w -text [lindex $labels $i] -font RFSmall -fill #475569 -tags state_legend
    }
}
# 把一个真实半径区间映射成色块，不把状态编号当成纵向坐标。
proc ::rf::branch_state_cell {c left right xmin xmax lo hi y0 y1 color} {
    set px [expr {$left+($lo-$xmin)/($xmax-$xmin)*($right-$left)}]
    set qx [expr {$left+($hi-$xmin)/($xmax-$xmin)*($right-$left)}]
    $c create rectangle $px $y0 $qx $y1 -fill $color -outline {} -tags branch_state_cell
}
# 只在当前显示的分支行内寻找最近径向分类，并补充同点波型。
proc ::rf::inspect_branch_status {chart px py} {
    lassign $::rf::plot_bounds left top right bottom xmin xmax dummy count
    set row [expr {min($count-1,int(($py-$top)/double($bottom-$top)*$count))}]
    set s [lindex $::rf::status_rows $row];set xs [dict get $chart x]
    set target [expr {$xmin+($px-$left)/double($right-$left)*($xmax-$xmin)}]
    set index 1;set nearest Inf
    for {set i 1} {$i<[llength $xs]} {incr i 2} {set d [expr {abs([lindex $xs $i]-$target)}];if {$d<$nearest} {set nearest $d;set index $i}}
    set state [expr {int([lindex [dict get $s y] $index])}]
    set ::rf::hover "[dict get $s label] · r=[fmt [lindex $xs $index]] m · [lindex [branch_state_labels] $state]（不是波形）"
    if {[dict exists $s wave_names]} {append ::rf::hover " · 局部：[lindex [dict get $s wave_names] $index]"}
}
# 每支射线先按横坐标选择最近样本，再用屏幕高度决定要补充哪一支的终止原因。
proc ::rf::inspect_ray_projection {chart px py} {
    lassign $::rf::plot_bounds left top right bottom xmin xmax ymin ymax
    set target [expr {$xmin+($px-$left)/double($right-$left)*($xmax-$xmin)}]
    set symbol [expr {[dict get $chart title] eq {射线轴向投影} ? {z} : {s}}]
    set text "$symbol ≈ [fmt $target] m";set j 0;set best Inf;set selected {}
    foreach s [dict get $chart series] {
        if {$::rf::enabled($j)} {
            set nearest Inf;set index -1;set i 0
            foreach x [dict get $s x] {if {$x ne {}} {set d [expr {abs($x-$target)}];if {$d<$nearest} {set nearest $d;set index $i}};incr i}
            if {$index>=0} {
                set value [lindex [dict get $s y] $index]
                append text " · [dict get $s label]：[fmt $value] [chart_value_unit $chart]"
                if {[dict exists $s wave_names]} {append text "（局部：[lindex [dict get $s wave_names] $index]）"}
                set y [expr {$bottom-($value-$ymin)/($ymax-$ymin)*($bottom-$top)}]
                if {abs($y-$py)<$best} {set best [expr {abs($y-$py)}];set selected $s}
            }
        }
        incr j
    }
    if {$selected ne {} && [dict exists $selected stop]} {append text " · [dict get $selected label]终止：[dict get $selected stop]"}
    set ::rf::hover $text
}
