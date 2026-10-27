#include "full_modes.h"
#include "plasma.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#define REQUIRE(x)                                                                                 \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %d: %s (%s)\n", __LINE__, #x, error);                            \
            abort();                                                                               \
        }                                                                                          \
    } while (0)
static char error[512];
static double relative(double a, double b)
{
    return fabs(a - b) / fmax(1e-100, fmax(fabs(a), fabs(b)));
}
int main(void)
{
    Project p;
    Plasma c;
    project_default(&p);
    plasma_default(&c);
    p.type = RF_ANT_HELIX;
    p.turns = .5;
    p.radial_cells = 64;
    c.density = 3e14;
    c.magnetic_field = .002;
    c.electron_collision = 1e7;
    c.ion_collision = 1e5;
    double period = 2 * RF_PI / 4.;
    RfFullConfig g = { { -6, 6, -period / 2, period / 2, 3, 17, 33, 0 }, -1, 1, .37 };
    RfFullSolution full = { 0 }, other = { 0 };
    REQUIRE(rf_full_solve(&p, &c, &g, &full, error, sizeof(error)));
    REQUIRE(full.complete && full.work == NULL && full.r[16] == p.radius);
    REQUIRE(relative(full.window_power, full.spectral_power) < 1e-9);
    REQUIRE(full.power_balance_error < 1e-8 && full.radial_check_error < 1e-9);
    RfSpatialSolution modes[3] = { { 0 } };
    double power = 0;
    for (int m = -1; m <= 1; m++) {
        Project single = p;
        single.selected_m = m;
        RfSpatialConfig grid = g.space;
        grid.field_radius = p.radius;
        REQUIRE(rf_spatial_solve(&single, &c, &grid, &modes[m + 1], error, sizeof(error)));
        power += modes[m + 1].window_power;
    }
    REQUIRE(relative(power, full.window_power) < 1e-12);
    double worst = 0, peak = 0, interference = 0, qpeak = 0;
    for (size_t r = 0; r < g.space.nr; r++) {
        RfPlasmaMaterial mat;
        REQUIRE(rf_plasma_material(&p, &c, full.r[r], &mat, error, sizeof(error)));
        for (size_t z = 0; z < g.space.nz; z++) {
            size_t j = z * g.space.nr + r;
            double complex E[3] = { 0 }, B[3] = { 0 }, J[3] = { 0 };
            double incoherent_q = 0;
            for (int m = -1; m <= 1; m++) {
                const RfSpatialSolution *v = &modes[m + 1];
                double complex phase = cexp(I * m * g.phi);
                incoherent_q += v->absorption[j];
                for (int comp = 0; comp < 3; comp++) {
                    E[comp] += phase * v->electric[3 * j + comp];
                    B[comp] += phase * v->magnetic[3 * j + comp];
                    J[comp] += phase * v->current[3 * j + comp];
                }
            }
            double complex work = 0;
            for (int comp = 0; comp < 3; comp++) {
                worst = fmax(worst, cabs(E[comp] - full.electric[3 * j + comp]));
                peak = fmax(peak, cabs(E[comp]));
                REQUIRE(cabs(B[comp] - full.magnetic[3 * j + comp]) <
                        1e-12 * fmax(1e-30, cabs(B[comp])));
                REQUIRE(cabs(J[comp] - full.current[3 * j + comp]) <
                        1e-12 * fmax(1e-30, cabs(J[comp])));
                work += J[comp] * conj(E[comp]);
            }
            double q = .5 * creal(work);
            REQUIRE(fabs(q - full.absorption[j]) < 1e-9 * fmax(1e-20, fabs(q)));
            double flux = .5 * creal(E[0] * conj(B[1]) - E[1] * conj(B[0])) / RF_MU0;
            REQUIRE(fabs(flux - full.axial_flux[j]) < 1e-12 * fmax(1e-20, fabs(flux)));
            interference = fmax(interference, fabs(q - incoherent_q));
            qpeak = fmax(qpeak, fabs(q));
            if (full.r[r] > c.radius)
                REQUIRE(full.absorption[j] == 0 && full.shell[j] == 0);
            /* Independent seven-angle quadrature of coherent total E. With
             * m=-1..1 this exactly integrates every angular cross term. */
            double angular_q = 0;
            for (int phi_index = 0; phi_index < 7; phi_index++) {
                double phi = 2 * RF_PI * phi_index / 7;
                double complex field[3] = { 0 };
                for (int m = -1; m <= 1; m++)
                    for (int comp = 0; comp < 3; comp++)
                        field[comp] += cexp(I * m * phi) * modes[m + 1].electric[3 * j + comp];
                angular_q += rf_plasma_absorption(&p, &c, &mat, field) * 2 * RF_PI / 7;
            }
            REQUIRE(fabs(full.shell[j] - full.r[r] * angular_q) <
                    1e-11 * fmax(1e-20, fabs(full.shell[j])));
        }
    }
    REQUIRE(worst < 1e-12 * peak && interference > 1e-3 * qpeak);
    /* Shift observation phi while rotating the source by the same angle:
     * cylindrical E/B/J and all scalar maps must be invariant. */
    p.azimuth_deg += .41 * 180 / RF_PI;
    g.phi += .41;
    REQUIRE(rf_full_solve(&p, &c, &g, &other, error, sizeof(error)));
    double rotation_error = 0;
    for (size_t j = 0; j < g.space.nr * g.space.nz; j++) {
        rotation_error = fmax(rotation_error, fabs(full.absorption[j] - other.absorption[j]));
        REQUIRE(fabs(full.shell[j] - other.shell[j]) < 1e-8 * fmax(1e-20, fabs(full.shell[j])));
    }
    REQUIRE(rotation_error < 1e-8 * qpeak);
    /* Degenerate range is exactly a one-m coherent reconstruction. */
    g.m_min = g.m_max = 1;
    g.phi = 0;
    p.azimuth_deg = 0;
    REQUIRE(rf_full_solve(&p, &c, &g, &other, error, sizeof(error)));
    for (size_t j = 0; j < g.space.nr * g.space.nz; j++)
        REQUIRE(relative(other.absorption[j], modes[2].absorption[j]) < 1e-12 ||
                other.absorption[j] == 0);
    g.m_min = 2;
    g.m_max = 1;
    REQUIRE(
        !rf_full_begin(&p, &c, &g, &other, error, sizeof(error)) && !other.work && !other.electric);
    g.m_min = -1;
    REQUIRE(rf_full_begin(&p, &c, &g, &other, error, sizeof(error)));
    REQUIRE(rf_full_step(&other, error, sizeof(error)));
    rf_full_free(&other);
    REQUIRE(!rf_full_step(&other, error, sizeof(error)));
    for (int m = 0; m < 3; m++)
        rf_spatial_free(&modes[m]);
    rf_full_free(&full);
    puts("FULL PASS: coherent m/k fields, magnetic/current phases, positive material work, signed "
         "flux, angular shell quadrature, Parseval, rotational covariance, one-m limit and "
         "cancellation ownership");
    return 0;
}
