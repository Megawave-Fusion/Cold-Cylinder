#ifndef RF_PLOT_H
#define RF_PLOT_H

#include "vacuum.h"
#include "plasma.h"

int rf_vacuum_export_svg(
    const RfVacuumSolution *solution, const char *path, char *error, size_t error_size);
int rf_plasma_export_svg(
    const RfPlasmaSolution *solution, const char *path, char *error, size_t error_size);

#endif
