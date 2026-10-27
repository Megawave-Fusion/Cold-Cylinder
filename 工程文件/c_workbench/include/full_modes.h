#ifndef RF_FULL_MODES_H
#define RF_FULL_MODES_H
#include "spatial.h"

/* Finite m sum and coherent k integral at a chosen phi, exp(-i omega t).
 * Owning objects must start {0}; complete results have no remaining work. */
typedef struct {
    RfSpatialConfig space;
    int m_min, m_max;
    double phi; /* radians */
} RfFullConfig;
typedef struct {
    Project project;
    Plasma plasma;
    RfFullConfig config;
    double *r, *z;
    double complex *electric, *magnetic, *current; /* z-major, three cylindrical components */
    double *absorption, *axial_flux, *shell;       /* W/m3, W/m2, W/m2 */
    /* shell=r integral_0^2pi q dphi=2pi*r*sum_m q_m, NOT 2pi*r*q(phi). */
    double window_power, spectral_power, power_balance_error;
    double band_edge_fraction, impedance_edge_fraction, edge_ratio, radial_check_error;
    double complex impedance;
    double progress;
    int complete;
    void *work;
} RfFullSolution;
RfFullConfig rf_full_config(const WorkbenchProject *);
int rf_full_begin(
    const Project *, const Plasma *, const RfFullConfig *, RfFullSolution *, char *, size_t);
int rf_full_step(RfFullSolution *, char *, size_t);
int rf_full_solve(
    const Project *, const Plasma *, const RfFullConfig *, RfFullSolution *, char *, size_t);
void rf_full_free(RfFullSolution *);
#endif
