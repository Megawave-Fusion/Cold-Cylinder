#ifndef RF_SPATIAL_H
#define RF_SPATIAL_H
#include "maxwell.h"
/* Infinite, z-uniform, finite-collision baseline, ONE selected azimuthal mode.
 * Midpoint k quadrature: E(z)=sum E_hat(k) exp(ikz) dk/(2pi).
 * q=Re(J.E*)/2 after coherent reconstruction; P=sum P_hat dk.
 * Initialize {0}; owning objects must not be shallow copied. */
typedef struct {
    double kmin, kmax, zmin, zmax;
    size_t nk, nr, nz;
    double field_radius; /* 0: plasma radius; otherwise display fields out to this radius */
} RfSpatialConfig;
typedef struct {
    Project project;
    Plasma plasma;
    RfSpatialConfig config;
    double *r, *z, *k;
    double *absorption;                 /* z-major [iz*nr+ir], W/m^3; zero outside plasma */
    double complex *electric, *current; /* [(iz*nr+ir)*3+c], V/m, A/m^2 */
    double complex *magnetic;           /* same ordering, T; coherent curl-derived field */
    double complex *potential;          /* z-major Coulomb scalar potential [V], Phi(R)=0 */
    double potential_backward_error;    /* worst scalar-projection solve over k */
    double *wave_impedance;             /* |E|/|H| = mu0 |E|/|B|, ohm; NAN if undefined */
    double *axial_absorption;           /* 2pi integral r q dr, W/m */
    double *radial_absorption;          /* 2pi r integral_window q dz, W/m */
    double *cumulative;                 /* axial cumulative absorption within window, W */
    double *power_spectrum;             /* W m */
    double complex *impedance_spectrum; /* ohm m, engineering exp(+i wt) */
    double dk, period, spectral_power, source_power, window_power;
    double complex impedance; /* ohm, selected m and finite k band */
    double radial_check_error, power_balance_error, edge_ratio, band_edge_fraction;
    double impedance_edge_fraction; /* outer 5% at each end / integral |Z_hat| */
    double progress;
    int potential_only; /* waveform job: only r/z/k/potential and spectral diagnostics are valid */
    int complete;
    void *work;
} RfSpatialSolution;
/* 当前共用k设置取state.nk/kmin/kmax，非旧Options兼容字段；NULL返回零配置。 */
RfSpatialConfig rf_spatial_config(const WorkbenchProject *state);
int rf_spatial_begin(
    const Project *, const Plasma *, const RfSpatialConfig *, RfSpatialSolution *, char *, size_t);
/* Same modal Maxwell/Coulomb solves and Fourier weights, without unused
 * E/B/J observation arrays or high-order spatial power quadrature. */
int rf_spatial_begin_potential(
    const Project *, const Plasma *, const RfSpatialConfig *, RfSpatialSolution *, char *, size_t);
/* 每次只推进有界工作量；可在两次step间取消。失败后清理部分结果。
 * 连续范围即使合法，实际r/z/k节点若在double下不能分辨，也会明确拒绝。 */
int rf_spatial_step(RfSpatialSolution *, char *, size_t);
int rf_spatial_solve(
    const Project *, const Plasma *, const RfSpatialConfig *, RfSpatialSolution *, char *, size_t);
void rf_spatial_free(RfSpatialSolution *);
int rf_spatial_export_csv(const RfSpatialSolution *, const char *, char *, size_t);
int rf_spatial_export_spectrum(const RfSpatialSolution *, const char *, char *, size_t);
#endif
