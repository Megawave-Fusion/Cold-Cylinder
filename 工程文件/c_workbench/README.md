# C11 工作台开发说明

启动、界面操作和 CLI 示例见 [根目录 README](../../README.md)；物理公式与验证在 [求解器文档](../docs/Maxwell求解器实现与验证.md)，模板、参数和源谱在 [天线模型与电流源](../docs/天线模型与电流源.md)。

## 源码入口

| 文件 | 职责 |
|---|---|
| `src/rf_model.c`、`src/project_io.c` | 参数状态、校验和项目事务读写，兼容 `.rfproj` 与配套 `.plasmaconfig` |
| `src/geometry.c` | 12 类完整闭合带面源、Fourier 积分与 OBJ 中心线导出 |
| `src/maxwell.c` | 真空和冷等离子体共用的相容矢量有限元 |
| `src/vacuum.c` | 真空接口、复数线性代数、源谱与波数扫描 |
| `src/spatial.c`、`src/rf_gui_spatial.h` | 单 m 无限长复数场/电势重建、电势虚部堆叠、空间吸收与带内复阻抗、分步任务 |
| `src/full_modes.c`、`include/full_modes.h`、`src/rf_gui_full.h` | 多 m 相干场重建、全圆周壳层沉积、全模式 r-z 图与分量切换 |
| `src/branches.c`、`include/branches.h` | 固定 f,m,kz 局部分支与指定起点的自适应 Hamilton 射线，独立采样和 CSV |
| `src/plasma.c` | 冷流体材料张量、响应电流、功率与正碰撞延拓 |
| `src/main.c`、`src/plot.c` | CLI、CSV/SVG 导出 |
| `src/rf_tk_gui.c`、`src/rf_gui_fields.h` | Tcl 参数桥接、计算缓存及派生图示数据 |
| `src/rf_gui.tcl`、`src/rf_gui_views.tcl` | 三栏界面、扫描/空间任务调度及图表 |
| `src/rf_gui_render.h` | 透明模型混色与抗锯齿绘制 |
| `include/` | 公共接口、数据所有权约定、物理常量与求积规则 |
| `tests/`、`tools/` | 回归检查、Tcl 嵌入、运行库构建与应用打包 |

## 构建与生成文件

本目录和根目录均可执行 `make`、`make test`、`make full-test`、`make gui-test`、`make app`、`make clean`。数值核心只依赖 C11 标准库；GUI 依赖 Tcl/Tk。Python 仅用于 CLI 回归，GUI 回归需要可用图形会话。

macOS 优先使用 `runtime/` 中的 Tcl/Tk 8.6.18。`make runtime` 从固定版本与 SHA-256 校验和重建本机运行库；Linux 或自定义安装可设置 `TCL_CFLAGS` / `TCL_LIBS`，纯 CLI 使用 `make GUI=no`。`.build-config` 记录编译配置，切换选项时使对象文件重新构建。

`tools/embed_tcl.awk` 从两个 Tcl 源文件生成 `src/rf_gui_script.h`，由 Makefile 管理依赖，不手工修改生成头文件。`make clean` 删除对象文件、源码可执行文件、四个测试程序、生成头文件及配置缓存；再次构建会重新生成。

`make app` 将程序、示例、运行库与许可复制进根目录独立 `.app`。源码构建需要 `runtime/include` 和 `runtime/lib`，独立应用需要自己的运行库副本，两者各有用途。普通清理保留运行库与应用；更新源码后执行 `make app` 才会更新应用。

主 PDF 的全部章节、TikZ/PGFPlots 绘图、验证表及参考文献集中在 `天线电流谱文档/Finite_width_antenna_current_spectra.tex`，可单独复制并用 XeLaTeX/latexmk 编译，无需其他项目 `.tex` 或图片、CSV 文件。根目录 `make pdf` 会先重算天线验证数据，再由 `tools/generate_antenna_pdf_figures.py` 更新主文件中七个 `BEGIN/END GENERATED` 标记区，最后编译 PDF；标记区以外的正文保持原样。生成区的图表调整应修改生成脚本，避免下一次更新被覆盖。

文档目录只保留主 `.tex` 和 PDF。重算所需的四份 CSV 写到 `tmp/pdfs/data/`，LaTeX 编译中间文件写到 `tmp/pdfs/build/`，生成脚本从临时数据目录读取，不在文档目录创建分文件或数据目录。

## 代码组织与修改约定

`main.c` 将命令行解析、项目载入与覆盖校验、具体计算命令分开；`rf_tk_gui.c` 按几何、源谱、真空和等离子体分别构造结果，统一管理 Tcl 结果容器。图表直接读取求解器拥有的场样本，复制到 Tcl 数值后即可释放原解。

`project_io.c` 的三张 `StoredField` 表集中维护工程、材料和扫描字段，读取、查重与保存共用定义。新增持久化参数时同时考虑旧文件的默认值及字段完整性。文本逐行解析共用一份可修改副本；保存统一经临时文件关闭和改名提交。解析或写入失败须保留调用者状态及原文件。

`maxwell.c` 按装配与源加载、约束、平衡求解、原系统残差、功率积分分步执行；`plasma.c` 将固定碰撞级的网格/求积收敛与外层碰撞延拓分开。拥有内部网格和系数的结果只转移一次所有权，失败路径统一释放。修改这些流程时保留公式注释与独立功率检查，并运行核心、CLI 和 GUI 回归。

根目录 `.clang-format` 统一自有 C 源码、头文件和 C 测试的格式，使用四空格缩进，展开短条件和循环，保留物理注释。格式工具使用 clang-format 18；第三方 `runtime/` 和自动生成的 `rf_gui_script.h` 不参与格式整理。

## 固定 f,m,kz 的全分支传播分析

`branches.c` / `branches.h` 复用 `rf_plasma_material`，直接求完整冷介电张量波矩阵的两支 `Q=(kperp/k0)^2` 和各自 `±kr`，其中 `kr²=k0²Q-m²/r²`。A/B 是按根距离与极化重叠延续的编号，不预设所有频率下都同时存在 helicon、TG 和 ICRF 快/慢波四种物理波名。

图例保留 A/B，并标注发射半径 r0 处的 helicon 型、TG 型、混合/近合并、待判定或真空；悬停显示当前位置的局部判定。识别只添加诊断元数据，不改变求根、传播状态、群速度或轨迹。以 Chen & Arnush (1997) 的电子惯性参考关系 `delta*beta²-|kz|*beta+kw²=0` 匹配完整无碰撞根的总波数，其中 `delta=omega/|Omega_e|`、`kw²=delta*omega_pe²/c²`。采用保守窗口 `10|Omega_i|<=omega<=0.1|Omega_e|`、`omega>=3omega_LH`、`omega_pe>=3|Omega_e|`、`kz!=0`；近似 `omega_LH²=|Omega_i Omega_e|/(1+Omega_e²/omega_pe²)` 仅用于命名筛选。参考判别式大于 0.05，根弱损耗且参考 Q 为正实数、极化有效，才匹配两个实参考根；相对偏差须不超过 20%，对数距离的差须至少 ln(1.25)，TG 型另需纵向极化份额 >=0.8。纵向份额大并非 TG 的充分条件。门限是减少误标的算法选择，不是波存在边界；“待判定”不等于不能传播，“混合”也不表示已计算模式转换。径向与射线 CSV 末尾均追加 `wave_type,wave_match_error,wave_reason`。详细公式及参考算例见 PDF 第五章。

GUI 顶部工具栏下方固定一排七个等宽栏目：真空单模场、真空波数谱、等离子体单模场、等离子体波数谱、等离子体总场、电势波形、色散与射线。栏目之间使用深灰分割线，选中项蓝底白字，保持大字号，不换行或滚动；二级图示仍在右侧按宽度换行。最后的「色散与射线」使用现有频率、角向模数、轴向波数与径向网格输入。该栏目保留五张局部诊断图：径向波数实部、径向波数虚部、传播状态、合并群速度和 WKB，以及三张射线图：轴向投影、方位角、功率衰减。群速度可同时勾选三个分量与四个分支，实线/虚线/点线区分 r/φ/z。极化纵向份额、根间距、极化残差、射线径向波数和色散残差保留在内部计算与 CSV 中，不单独绘图。八张图共用一次求解的参数快照；分支图导出径向根表，射线图导出沿程表。两侧材料和独立发射区间在曲线上断开；CSV 保留所有四个解与缺失原因。

```sh
./rf-workbench --branches --project ../examples/全分支螺旋波示例.rfproj \
  --output /tmp/branches.csv --rays /tmp/rays.csv
```

实际复波数使用当前碰撞模型；实群速度来自匹配的无碰撞参考，仅在弱损耗、根分离及径向传播条件下有效。群速度频率差分避免跨回旋极点，并做两档步长检查。几何光学指标 `max(|dkr/dr|/|Re kr|², 1/(|Re kr| r)) <= 0.1` 后才追踪；轴心不采样。各候选根从指定的 r0,z0,φ0 发射，按所选能流方向而非相位符号积分，到界面、截止/转向、WKB 失效或强阻尼区停止。完整 CSV 的 `ray_stop` 记录终止原因；轨迹 CSV 按 ray_id 与时间排序。

不自动求界面反射、模式转换、天线耦合幅度或热/动理学分支。极限吸收模式在此使用输入参考碰撞，不执行延拓。归一化极化无量纲，`exp(-tau)` 是不含聚焦的相对射线管耗散因子。详细推导和实际结果图见项目 PDF 第五章。

`make branches-test` 用完整 Maxwell 行列式、真空解析色散和 Brillouin 能量速度独立验证根、极化及群速度，覆盖离子回旋与 helicon 频段、弱/强碰撞及材料极点；均匀各向异性解析轨迹检验径向采样收敛。


显示约定：`分支传播状态` 使用按分支排列的分类色带，不将离散状态编号画成连续曲线。`射线轴向投影` 以 z 为横轴、r 为纵轴，箭头按轨迹时间递增表示能流方向；它是能量传播路径，不是电场随位置振荡的波形。均匀剖面下波数、轴向群速度等诊断量可以近似恒定。新增 `全分支非均匀螺旋波示例.rfproj` 使用相同 f,m,kz 的抛物线密度，便于对比折射。左侧参数禁用触控板和滚轮滚动，保留滚动条拖动；结果图的坐标原点固定。
