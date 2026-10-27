#include "spatial.h"
#include "plasma.h"
#include "geometry.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#define REQUIRE(x)                                                                                 \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %d: %s (%s)\n", __LINE__, #x, error);                            \
            abort();                                                                               \
        }                                                                                          \
    } while (0)
static char error[512];
static double rel(double a, double b)
{
    return fabs(a - b) / fmax(1e-100, fmax(fabs(a), fabs(b)));
}
static void parseval_translation(void)
{
    Project p;
    Plasma c;
    project_default(&p);
    plasma_default(&c);
    p.type = RF_ANT_HELIX;
    p.selected_m = 1;
    p.turns = .5;
    p.radial_cells = 64;
    c.density = 3e14;
    c.magnetic_field = .002;
    c.electron_collision = 1e7;
    c.ion_collision = 1e5;
    double period = 2 * RF_PI / (24. / 9);
    RfSpatialConfig g = { -12, 12, -period / 2, period / 2, 9, 17, 65, 0 };
    RfSpatialSolution a = { 0 }, b = { 0 };
    REQUIRE(rf_spatial_solve(&p, &c, &g, &a, error, sizeof(error)));
    REQUIRE(a.spectral_power > 0 && a.power_balance_error < 1e-8 && a.radial_check_error < 1e-9);
    /* An independent z integration over one entire DFT period must obey Parseval,
     * including both the inverse 1/(2pi) and azimuthal 2pi factors. */
    REQUIRE(rel(a.window_power, a.spectral_power) < 1e-9);
    REQUIRE(rel(creal(a.impedance), 2 * a.source_power / (p.current * p.current)) < 1e-13);
    /* Local material work is checked against the separately implemented positive
     * species loss, after inverse transforming all complex vector components. */
    for (size_t j = 0; j < g.nr * g.nz; j++) {
        double complex work = 0;
        for (size_t comp = 0; comp < 3; comp++)
            work += a.current[3 * j + comp] * conj(a.electric[3 * j + comp]);
        REQUIRE(fabs(.5 * creal(work) - a.absorption[j]) < 1e-9 * fmax(1e-20, a.absorption[j]));
    }
    /* Independently reconstruct B at one point directly from the original
     * modal curls. A ratio of spectral magnitudes would fail this check. */
    size_t probe_r = 7, probe_z = 19, probe = probe_z * g.nr + probe_r;
    double complex expected_b[3] = { 0 }, expected_phi = 0.;
    for (size_t k = 0; k < g.nk; k++) {
        Project mode = p;
        mode.kz = a.k[k];
        RfMaxwellSolution fem = { 0 };
        double complex E[3], B[3];
        REQUIRE(rf_maxwell_solve(&mode, &c, &fem, error, sizeof(error)));
        REQUIRE(rf_maxwell_evaluate(&fem, a.r[probe_r], 0, E, B));
        REQUIRE(rf_maxwell_prepare_potential(&fem, error, sizeof(error)));
        double complex phi;
        REQUIRE(rf_maxwell_evaluate_potential(&fem, a.r[probe_r], 0, &phi, NULL));
        expected_phi += phi * cexp(I * a.k[k] * a.z[probe_z]) * a.dk / (2 * RF_PI);
        for (size_t comp = 0; comp < 3; comp++)
            expected_b[comp] += B[comp] * cexp(I * a.k[k] * a.z[probe_z]) * a.dk / (2 * RF_PI);
        rf_maxwell_free(&fem);
    }
    REQUIRE(cabs(expected_phi - a.potential[probe]) < 1e-12 * fmax(1e-25, cabs(expected_phi)));
    REQUIRE(a.potential_backward_error < 1e-8);
    double en = 0, bn = 0;
    for (size_t comp = 0; comp < 3; comp++) {
        REQUIRE(cabs(expected_b[comp] - a.magnetic[3 * probe + comp]) <
            1e-12 * fmax(1e-30, cabs(expected_b[comp])));
        en = hypot(en, cabs(a.electric[3 * probe + comp]));
        bn = hypot(bn, cabs(expected_b[comp]));
    }
    REQUIRE(rel(a.wave_impedance[probe], RF_MU0 * en / bn) < 1e-12);
    /* Dense waveform mode uses identical modal physics/weights while skipping
     * unused power quadrature; shared z=0 samples must match the full solver. */
    RfSpatialSolution dense = { 0 };
    RfSpatialConfig dense_grid = g;
    dense_grid.nz = 401;
    REQUIRE(rf_spatial_begin_potential(&p, &c, &dense_grid, &dense, error, sizeof(error)));
    while (!dense.complete)
        REQUIRE(rf_spatial_step(&dense, error, sizeof(error)));
    REQUIRE(dense.potential_only && !dense.electric && !dense.absorption && dense.potential);
    for (size_t r = 0; r < g.nr; ++r)
        REQUIRE(cabs(dense.potential[200 * g.nr + r] - a.potential[32 * g.nr + r]) <
            1e-10 * fmax(1e-20, cabs(a.potential[32 * g.nr + r])));
    rf_spatial_free(&dense);
    double dz = (g.zmax - g.zmin) / (g.nz - 1);
    p.axial_offset = dz;
    p.global_phase_deg = 37;
    p.current = 2;
    REQUIRE(rf_spatial_solve(&p, &c, &g, &b, error, sizeof(error)));
    REQUIRE(cabs(a.impedance - b.impedance) < 1e-9 * cabs(a.impedance));
    REQUIRE(rel(b.spectral_power, 4 * a.spectral_power) < 1e-10);
    double worst = 0, peak = 0;
    for (size_t z = 1; z < g.nz; z++)
        for (size_t r = 0; r < g.nr; r++) {
            double expected = 4 * a.absorption[(z - 1) * g.nr + r];
            double complex expected_potential =
                2. * cexp(I * 37. * RF_PI / 180.) * a.potential[(z - 1) * g.nr + r];
            REQUIRE(cabs(b.potential[z * g.nr + r] - expected_potential) <
                1e-8 * fmax(1e-15, cabs(expected_potential)));
            peak = fmax(peak, expected);
            worst = fmax(worst, fabs(b.absorption[z * g.nr + r] - expected));
            double za = a.wave_impedance[(z - 1) * g.nr + r];
            double zb = b.wave_impedance[z * g.nr + r];
            if (isfinite(za) && isfinite(zb))
                REQUIRE(rel(za, zb) < 1e-8);
            else
                REQUIRE(!isfinite(za) && !isfinite(zb));
        }
    REQUIRE(worst < 1e-9 * peak);
    /* A smaller observation window has less integrated absorption, while the
     * infinite-medium band impedance is independent of that display window. */
    double full = b.window_power;
    g.zmin = -period / 8;
    g.zmax = period / 8;
    REQUIRE(rf_spatial_solve(&p, &c, &g, &b, error, sizeof(error)));
    REQUIRE(b.window_power < full && b.window_power > 0);
    REQUIRE(cabs(a.impedance - b.impedance) < 1e-9 * cabs(a.impedance));
    printf("spatial Parseval %.3g; source/absorption %.3g; translation defect %.3g\n",
        rel(a.window_power, a.spectral_power),
        a.power_balance_error,
        worst / peak);
    rf_spatial_free(&a);
    rf_spatial_free(&a);
    rf_spatial_free(&b);
}
static void impedance_source_work(void)
{
    /* Independent external-current work on a thin loop: Z_hat=-a conj(Ephi Kphi*)/I^2.
     * This checks the engineering reactance sign and source normalization, rather
     * than defining resistance from the already calculated absorbed power. */
    Project p;
    project_default(&p);
    p.selected_m = 0;
    p.band_width = 0;
    p.band_samples = 1;
    p.frequency_mhz = 10;
    p.kz = 4;
    p.radial_cells = 128;
    RfMaxwellSolution s = { 0 };
    Geometry geometry = { 0 };
    double complex K[3], E[3], B[3];
    REQUIRE(rf_maxwell_solve(&p, NULL, &s, error, sizeof(error)));
    REQUIRE(geometry_build(&p, &geometry, error, sizeof(error)));
    REQUIRE(geometry_current_spectrum(&p, &geometry, K, error, sizeof(error)));
    REQUIRE(rf_maxwell_evaluate(&s, p.antenna_radius, 0, E, B));
    double complex expected = -p.antenna_radius * conj(E[1] * conj(K[1])) / (p.current * p.current);
    REQUIRE(cabs(expected - s.load_impedance) < 1e-9 * cabs(expected));
    REQUIRE(cimag(s.load_impedance) > 0);
    REQUIRE(fabs(creal(s.load_impedance)) < 1e-9 * cabs(s.load_impedance));
    geometry_free(&geometry);
    rf_maxwell_free(&s);
}
static void failures(void)
{
    WorkbenchProject w;
    workbench_default(&w);
    w.project.radial_cells = 64;
    w.nk = 3;
    RfSpatialConfig g = rf_spatial_config(&w);
    g.kmin = -3;
    g.kmax = 3;
    g.zmin = -.5;
    g.zmax = .5;
    g.nr = 17;
    g.nz = 17;
    RfSpatialSolution s = { 0 };
    w.plasma.solve_mode = RF_PLASMA_LIMITING_ABSORPTION;
    REQUIRE(!rf_spatial_begin(&w.project, &w.plasma, &g, &s, error, sizeof(error)) && !s.work);
    w.plasma.solve_mode = RF_PLASMA_FINITE_COLLISION;
    g.zmax = 100;
    REQUIRE(!rf_spatial_begin(&w.project, &w.plasma, &g, &s, error, sizeof(error)));
    g.zmax = .5;
    g.nz = 3;
    g.kmin = -20;
    g.kmax = 20;
    REQUIRE(!rf_spatial_begin(&w.project, &w.plasma, &g, &s, error, sizeof(error)));
    g.kmin = 0;
    g.kmax = 1e-320;
    g.nz = 17;
    REQUIRE(!rf_spatial_begin(&w.project, &w.plasma, &g, &s, error, sizeof(error)));
    g.kmin = -3;
    g.kmax = 3;
    REQUIRE(rf_spatial_begin(&w.project, &w.plasma, &g, &s, error, sizeof(error)));
    REQUIRE(rf_spatial_step(&s, error, sizeof(error)) && !s.complete);
    /* Aliased restart/cancel must not leak or use freed configuration. */
    REQUIRE(rf_spatial_begin(&s.project, &s.plasma, &s.config, &s, error, sizeof(error)));
    rf_spatial_free(&s);
    REQUIRE(!rf_spatial_step(&s, error, sizeof(error)));
    w.project.type = RF_ANT_STRAP;
    w.project.width = .001;
    REQUIRE(rf_spatial_begin(&w.project, &w.plasma, &g, &s, error, sizeof(error)));
    REQUIRE(!rf_spatial_step(&s, error, sizeof(error)) && !s.complete && !s.work && !s.absorption);
}
int main(void)
{
    impedance_source_work();
    parseval_translation();
    failures();
    puts("SPATIAL PASS: independent source impedance/sign, coherent fields/material work, "
         "Parseval, translation, current scaling, window independence and failure ownership");
    return 0;
}
