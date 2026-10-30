#ifndef RF_TK_GUI_H
#define RF_TK_GUI_H

/* 启动可选的 Tcl/Tk 前端并运行事件循环，返回进程退出码。
 * 参数只读取 --project；项目先校验再提交。关闭解释器会取消并释放未完成任务。 */
int rf_tk_gui_run(int argc, char **argv);

#endif /* RF_TK_GUI_H */
