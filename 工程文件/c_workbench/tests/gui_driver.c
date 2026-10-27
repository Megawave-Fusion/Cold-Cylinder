/* Exercise the same embedded UI and native command bridge as the application. */
#include "../src/rf_tk_gui.c"
#include <assert.h>

static void check_derived_fields(void)
{
    double r[5] = { 0., 0.1, 0.2, 0.3, 0.4 };
    double complex e[15] = { 0 }, b[15] = { 0 };
    size_t i;
    double mu = RF_MU0;
    /* 这里只检查绘图的能流公式；解析旋度由核心 Maxwell 参考测试覆盖。 */
    memset(e, 0, sizeof(e));
    memset(b, 0, sizeof(b));
    for (i = 0; i < 5; ++i) {
        e[i] = 1.;
        b[5 + i] = 2.;
    }
    assert(fabs(axial_flux(r, e, b, 5) - 3.14159265358979323846 * r[4] * r[4] / mu) < 1e-8);
    /* Interleaved total-field fixture: ||E||=13, ||B||=2, including complex phase.
     * Magnetic nodes stay undefined, while electric nodes give a genuine zero. */
    e[0] = 3. + 4. * I;
    e[1] = 0.;
    e[2] = 12.;
    b[0] = 0.;
    b[1] = 2. * I;
    b[2] = 0.;
    RfFullSolution total = { .electric = e, .magnetic = b };
    assert(fabs(full_value(&total, 0, 6, 0) - 6.5 * mu) < 1e-15);
    b[1] = 0.;
    assert(isnan(full_value(&total, 0, 6, 0)));
    b[1] = 1e-100;
    assert(fabs(full_value(&total, 0, 6, 0) / (13e100 * mu) - 1.) < 1e-15);
    e[0] = e[2] = 0.;
    assert(full_value(&total, 0, 6, 0) == 0.);
}

int main(int argc, char **argv)
{
    Tcl_Interp *ip;
    int ok;
    if (argc != 2)
        return 2;
    check_derived_fields();
    Tcl_FindExecutable(argv[0]);
    ip = Tcl_CreateInterp();
    configure_runtime(ip);
#ifdef RF_BUNDLED_TK
    /* Tk's macOS console uses a fresh interpreter, not the one configured
     * above. It must initialize from bundled resources after /tmp is cleaned. */
    Tcl_Interp *secondary = Tcl_CreateInterp();
    assert(Tcl_Init(secondary) == TCL_OK);
    const char *library = Tcl_GetVar(secondary, "tcl_library", TCL_GLOBAL_ONLY);
    const char *configured = Tcl_GetVar2(ip, "env", "TCL_LIBRARY", TCL_GLOBAL_ONLY);
    assert(library && configured && strcmp(library, configured) == 0);
    assert(Tcl_GetVar2(ip, "env", "TK_LIBRARY", TCL_GLOBAL_ONLY));
    Tcl_DeleteInterp(secondary);
#endif
    workbench_default(&rf_state);
    ok = Tcl_Init(ip) == TCL_OK && Tk_Init(ip) == TCL_OK && init_ui(ip) == TCL_OK;
    if (ok)
        ok = Tcl_EvalFile(ip, argv[1]) == TCL_OK;
    if (!ok)
        fprintf(stderr,
            "%s\n",
            Tcl_GetVar(ip, "errorInfo", TCL_GLOBAL_ONLY)
                ? Tcl_GetVar(ip, "errorInfo", TCL_GLOBAL_ONLY)
                : Tcl_GetStringResult(ip));
    else
        fprintf(stderr,
            "GUI PASS: three panes, live geometry, unified vacuum/plasma menus, plasma scans, "
            "persistence, cancellation, snapshots, exports, resizing\n");
    Tcl_Eval(ip, "destroy .");
    Tcl_DeleteInterp(ip);
    Tcl_Finalize();
    return ok ? 0 : 1;
}
