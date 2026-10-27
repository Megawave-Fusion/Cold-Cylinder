/* Material, source, linear-algebra and persistence regression checks. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "geometry.h"
#include "plasma.h"
#include "project_io.h"
#include "vacuum.h"
#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define PI 3.14159265358979323846
static char error[256];
static void near(double complex got, double complex expected, double tolerance)
{
    assert(cabs(got - expected) <= tolerance * (1. + cabs(expected)));
}
static void spectrum(const Project *p, double complex out[3])
{
    Geometry g = { 0 };
    assert(geometry_build(p, &g, error, sizeof(error)));
    assert(geometry_current_spectrum(p, &g, out, error, sizeof(error)));
    geometry_free(&g);
}
static void linear_tests(void)
{
    RfComplexMatrix a = { 0 };
    RfComplexBandMatrix b = { 0 };
    double complex rhs[3], out[3];
    double eta;
    assert(rf_complex_matrix_init(&a, 3, 3));
    assert(rf_complex_band_init(&b, 3, 1, 1));
    /* A zero first diagonal forces Thomas to use partial pivoting. */
    const double complex matrix[9] = { 0, 2 + I, 0, 1, 3, 1 - I, 0, 1, 4 };
    const double complex expected[3] = { 1 + I, -2 + I, .5 - I };
    for (size_t i = 0; i < 3; ++i) {
        rhs[i] = 0;
        for (size_t j = 0; j < 3; ++j) {
            assert(rf_complex_matrix_set(&a, i, j, matrix[3 * i + j]));
            if (matrix[3 * i + j] != 0)
                assert(rf_complex_band_set(&b, i, j, matrix[3 * i + j]));
            rhs[i] += matrix[3 * i + j] * expected[j];
        }
    }
    assert(rf_complex_solve(&a, rhs, out, &eta) == RF_LINEAR_OK && eta < 1e-14);
    for (size_t i = 0; i < 3; ++i)
        near(out[i], expected[i], 1e-13);
    assert(rf_complex_band_solve(&b, rhs, rhs, &eta) == RF_LINEAR_OK && eta < 1e-14);
    for (size_t i = 0; i < 3; ++i)
        near(rhs[i], expected[i], 1e-13);
    for (size_t i = 0; i < 3; ++i) {
        rhs[i] = 0;
        for (size_t j = 0; j < 3; ++j)
            rhs[i] += matrix[3 * i + j] * expected[j];
    }
    assert(rf_complex_solve(&a, rhs, rhs, &eta) == RF_LINEAR_OK && eta < 1e-14);
    rf_complex_matrix_zero(&a);
    assert(rf_complex_solve(&a, rhs, out, &eta) == RF_LINEAR_SINGULAR);
    assert(!rf_complex_band_set(&b, 0, 2, 1));
    assert(!rf_complex_matrix_set(&a, 0, 0, NAN));
    rf_complex_band_free(&b);
    rf_complex_matrix_free(&a);
}
static void geometry_tests(void)
{
    Project p;
    double complex base[3], changed[3];
    project_default(&p);
    p.selected_m = 0; /* Analytic uniform-loop fixture. */
    p.kz = 0;
    p.current = 2.5;
    spectrum(&p, base);
    near(base[1], p.current, 1e-13);
    near(base[0], 0, 1e-13);
    near(base[2], 0, 1e-13);
    p.antenna_radius = .25;
    spectrum(&p, changed);
    near(changed[1], base[1], 1e-13);
    p.selected_m = 2;
    spectrum(&p, changed);
    near(changed[1], 0, 1e-12);
    for (int type = RF_ANT_STRAP; type <= RF_ANT_MEANDER; ++type) {
        Geometry g = { 0 };
        project_default(&p);
        p.type = type;
        p.kz = 3;
        p.selected_m = 1;
        assert(geometry_build(&p, &g, error, sizeof(error)));
        if(type<=RF_ANT_AXIAL_LOOPS || type==RF_ANT_BIRDCAGE)
            assert(geometry_current_balance(&g) < 1e-10);
        assert(geometry_current_spectrum(&p, &g, base, error, sizeof(error)));
        geometry_free(&g);
        p.azimuth_deg = 23;
        p.axial_offset = .08;
        p.global_phase_deg = 17;
        spectrum(&p, changed);
        double complex phase = cexp(I * (17 * PI / 180. - 23 * PI / 180. - p.kz * .08));
        for (int k = 0; k < 3; ++k)
            near(changed[k], phase * base[k], 1e-10);
        p.current_direction = -1;
        spectrum(&p, base);
        for (int k = 0; k < 3; ++k)
            near(base[k], -changed[k], 1e-11);
    }
    project_default(&p);
    p.type = RF_ANT_STRAP;
    p.selected_m = 1;
    p.kz = 3;
    p.width = .001;
    p.band_width = 0.;
    spectrum(&p, base);
    p.width = .002;
    spectrum(&p, changed);
    assert(cabs(changed[2] - base[2]) > 1e-5); /* 不得把窄回路开角截到任意下限。 */
    /* 两条相隔 π 的螺旋腿，轴向源可独立积分：端部半环没有 dz 贡献。 */
    project_default(&p);
    p.type = RF_ANT_HELIX;
    p.turns = .5;
    p.selected_m = 1;
    p.kz = 0;
    p.band_width = 0.;
    spectrum(&p, base);
    near(base[2], -I * 2. * p.current * p.antenna_length / (PI * PI * p.antenna_radius), 1e-12);
    /* Analytic straight axial segment at a high kz, independent of quadrature. */
    project_default(&p);
    p.kz = 1999;
    p.selected_m = 0;
    GeometrySegment segment = {
        .a = { .44, 0, 0 }, .b = { .44, 0, .2 }, .amplitude = 1, .cylindrical = 1
    };
    Geometry g = { .segments = &segment, .segment_count = 1 };
    assert(geometry_current_spectrum(&p, &g, base, error, sizeof(error)));
    double x = p.kz * .1;
    near(base[2], p.current / (2 * PI * p.antenna_radius) * .2 * cexp(-I * x) * sin(x) / x, 1e-13);
    assert(!geometry_build(&p, NULL, error, sizeof(error)));
    p.turns = -1;
    assert(!project_validate(&p, error, sizeof(error)));
    project_default(&p);
    p.type = RF_ANT_LINE;
    p.antenna_radius = .005;
    p.band_width = .001;
    assert(project_validate(&p, error, sizeof(error))); /* 直弦不用隐藏的矩形 width。 */
}
static void vacuum_tests(void)
{
    Project p;
    RfVacuumSolution a = { 0 }, b = { 0 };
    RfVacuumScan scan = { 0 };
    double complex source[3];
    project_default(&p);
    p.selected_m = 0; /* Nonzero loop field for the scaling check. */
    assert(rf_vacuum_solve(&p, &a, error, sizeof(error)));
    spectrum(&p, source);
    for (int k = 0; k < 3; ++k) {
        near(a.source[k], source[k], 1e-13);
        near(a.electric[(k + 1) * a.nr - 1], 0, 1e-13);
    }
    assert(a.backward_error < 1e-12);
    p.current *= 3;
    assert(rf_vacuum_solve(&p, &b, error, sizeof(error)));
    for (size_t i = 0; i < 3 * a.nr; ++i)
        near(b.electric[i], 3 * a.electric[i], 1e-10);
    assert(rf_vacuum_scan(&p, -5, 5, 5, 0, &scan, error, sizeof(error)));
    assert(scan.electric == NULL);
    for (size_t i = 0; i < 5; ++i) {
        p.kz = scan.k[i];
        spectrum(&p, source);
        assert(scan.valid[i]);
        assert(isnan(scan.total[i * scan.nr]) && isnan(scan.backward_error[i]));
        for (int k = 0; k < 3; ++k)
            near(scan.current[3 * i + k], source[k], 1e-13);
    }
    rf_vacuum_scan_free(&scan);
    p.type = RF_ANT_STRAP;
    p.width = .001;
    p.band_width = .02;
    assert(!rf_vacuum_solve(&p, &b, error, sizeof(error)));
    assert(rf_vacuum_scan(&p, -5, 5, 3, 1, &scan, error, sizeof(error)));
    for (size_t i = 0; i < 3; ++i)
        assert(!scan.valid[i] && scan.reason[i] && isnan(scan.total[i * scan.nr]) &&
               isnan(creal(scan.current[3 * i])));
    rf_vacuum_scan_free(&scan);
    rf_vacuum_solution_free(&a);
    rf_vacuum_solution_free(&b);
}
static void plasma_tests(void)
{
    Project p;
    Plasma c;
    RfPlasmaMaterial m, other;
    RfVacuumSolution vac = { 0 };
    RfPlasmaSolution s = { 0 };
    project_default(&p);
    plasma_default(&c);
    p.type = RF_ANT_HELIX;
    p.selected_m = 1;
    c.profile = RF_PLASMA_UNIFORM;
    assert(rf_vacuum_solve(&p, &vac, error, sizeof(error)));
    c.density = 0;
    assert(rf_plasma_solve(&p, &c, &s, error, sizeof(error)));
    for (size_t i = 0; i < 3 * s.nr; ++i)
        near(s.electric[i], vac.electric[i], 1e-12);
    assert(s.absorbed_power == 0 && isfinite(s.source_power) && s.power_balance_error < 1e-8);
    c.density = 1e13;
    c.magnetic_field = 0;
    c.electron_collision = 1e5;
    c.ion_collision = 1e3;
    assert(rf_plasma_solve(&p, &c, &s, error, sizeof(error)));
    size_t inside = 30;
    assert(rf_plasma_material(&p, &c, s.r[inside], &m, error, sizeof(error)));
    near(m.S, m.P, 1e-13);
    near(m.D, 0, 1e-13);
    assert(cabs(vac.electric[2 * s.nr + inside]) > 1e-14);
    assert(s.power_balance_error < 1e-6 && s.backward_error < 1e-12);
    for (size_t i = 0; i < s.nr; ++i) {
        double complex product = 0;
        for (int k = 0; k < 3; ++k)
            product += s.current[k * s.nr + i] * conj(s.electric[k * s.nr + i]);
        double expected = .5 * creal(product);
        assert(s.absorption[i] >= 0);
        assert(fabs(expected - s.absorption[i]) < 1e-10 * fmax(1., s.absorption[i]));
    }
    assert(s.absorbed_power > 0);
    double power = s.absorbed_power;
    p.current = 2;
    assert(rf_plasma_solve(&p, &c, &s, error, sizeof(error)));
    assert(fabs(s.absorbed_power / power - 4) < 1e-10);
    c.magnetic_field = .02;
    assert(rf_plasma_material(&p, &c, .1, &m, error, sizeof(error)));
    /* 独立按两物种动量方程求速度，不使用 S/D/P 公式构造预期电流。 */
    {
        double complex efield[3] = { 1. + .2 * I, -.4 + .7 * I, .3 - .8 * I }, fluid[3] = { 0 };
        double omega = 2. * PI * p.frequency_mhz * 1e6, eps0 = 8.8541878128e-12;
        for (int species = 0; species < 2; ++species) {
            double density = species ? m.ni : m.ne,
                   charge = species ? c.ion_charge * 1.602176634e-19 : -1.602176634e-19;
            double mass = species ? c.ion_mass_u * 1.66053906660e-27 : 9.1093837015e-31;
            double gyro = charge * c.magnetic_field / mass, nu = species ? m.nu_i : m.nu_e;
            double complex u = nu - I * omega, den = u * u + gyro * gyro;
            fluid[0] += density * charge * charge / mass * (u * efield[0] + gyro * efield[1]) / den;
            fluid[1] +=
                density * charge * charge / mass * (-gyro * efield[0] + u * efield[1]) / den;
            fluid[2] += density * charge * charge / mass * efield[2] / u;
        }
        near(-I * omega * eps0 * ((m.S - 1.) * efield[0] - I * m.D * efield[1]), fluid[0], 1e-12);
        near(-I * omega * eps0 * (I * m.D * efield[0] + (m.S - 1.) * efield[1]), fluid[1], 1e-12);
        near(-I * omega * eps0 * (m.P - 1.) * efield[2], fluid[2], 1e-12);
    }
    c.magnetic_field = -.02;
    assert(rf_plasma_material(&p, &c, .1, &other, error, sizeof(error)));
    near(m.S, other.S, 1e-13);
    near(m.P, other.P, 1e-13);
    near(m.D, -other.D, 1e-13);
    /* 有限正碰撞的回旋响应有限，不能被辅助零碰撞材料定位误拒绝。 */
    c.magnetic_field = (2 * PI * p.frequency_mhz * 1e6) * 9.1093837015e-31 / 1.602176634e-19;
    c.electron_collision = 1e7;
    c.ion_collision = 1e5;
    assert(rf_plasma_solve(&p, &c, &s, error, sizeof(error)));
    assert(s.power_balance_error < 1e-6);
    assert(rf_plasma_solve(&s.project, &s.plasma, &s, error, sizeof(error)));
    c.electron_collision_edge_ratio = 0;
    c.ion_collision_edge_ratio = 0;
    assert(rf_plasma_material(&p, &c, c.radius + 1e-14, &other, error, sizeof(error)));
    assert(other.nu_e == 0 && other.nu_i == 0);
    c.solve_mode = RF_PLASMA_DIRECT_RESONANCE;
    c.magnetic_field = 0;
    assert(rf_plasma_solve(&p, &c, &s, error, sizeof(error)));
    assert(s.absorbed_power == 0);
    /* Uniform epsilon=0 is a lossless degenerate region, not a local-inverse gain cap. */
    double w = 2 * PI * p.frequency_mhz * 1e6, e = 1.602176634e-19, eps = 8.8541878128e-12;
    c.density =
        w * w * eps /
        (e * e * (1. / 9.1093837015e-31 + c.ion_charge / (c.ion_mass_u * 1.66053906660e-27)));
    assert(!rf_plasma_solve(&p, &c, &s, error, sizeof(error)));
    assert(!s.complete && isnan(s.absorbed_power));
    rf_plasma_solution_free(&s);
    rf_vacuum_solution_free(&vac);
}
static void persistence_tests(void)
{
    char dir[] = "/tmp/rf-core-test-XXXXXX", path[256], config[256];
    WorkbenchProject w, saved;
    assert(mkdtemp(dir));
    workbench_default(&w);
    w.project.current = 7;
    w.project.global_phase_deg = -37.25;
    w.plasma.density = 2.3e14;
    w.plasma.solve_mode = RF_PLASMA_LIMITING_ABSORPTION;
    w.options.m_min = -2;
    w.options.m_max = 3;
    w.options.phi = .75;
    w.kmin = -23;
    w.kmax = 31;
    w.nk = 17;
    w.ray.r0=.17;w.ray.z0=.8;w.ray.phi0_deg=35;w.ray.direction=-1;
    w.ray.tolerance=3e-7;w.ray.max_step=.003;w.ray.max_path=7;w.ray.max_steps=1234;
    w.project.inner_radius=.17;w.project.gap_angle_deg=17.;w.project.source_mode=2;
    w.project.current_pattern=1;w.project.current_phase_gradient=-12.3;
    saved = w;
    assert(!workbench_load("/does/not/exist", &w, error, sizeof(error)));
    assert(w.project.current == 7);
    assert(!project_parse_text("frequency_mhz=abc\n", &w.project, error, sizeof(error)));
    assert(w.project.current == 7);
    assert(!project_parse_text(
        "frequency_mhz=1\nfrequency_mhz=2\nfrequency_mhz=3\nfrequency_mhz=4\nfrequency_mhz="
        "5\nfrequency_mhz=6\nfrequency_mhz=7\nfrequency_mhz=8\nfrequency_mhz=9\nfrequency_mhz=10\n",
        &w.project,
        error,
        sizeof(error)));
    snprintf(path, sizeof(path), "%s/case.rfproj", dir);
    snprintf(config, sizeof(config), "%s/case.plasmaconfig", dir);
    assert(project_save(path, &saved.project, error, sizeof(error)));
    assert(plasma_config_save(config, &saved.plasma, &saved.options, error, sizeof(error)));
    assert(workbench_load(config, &w, error, sizeof(error)));
    assert(w.plasma_enabled && w.project.current == 7);
    assert(w.project.inner_radius==.17 && w.project.gap_angle_deg==17. &&
        w.project.source_mode==2 && w.project.current_pattern==1 &&
        w.project.current_phase_gradient==-12.3);
    assert(w.plasma.solve_mode == RF_PLASMA_LIMITING_ABSORPTION && w.options.phi == .75);

    /* 统一格式、CRLF 和末行无换行都须保留非默认设置。 */
    assert(workbench_save(path, &saved, error, sizeof(error)));
    char text[8192], crlf[16384];
    FILE *stream = fopen(path, "rb");
    assert(stream);
    size_t count = fread(text, 1, sizeof(text) - 1, stream);
    assert(!ferror(stream) && feof(stream));
    fclose(stream);
    text[count] = '\0';
    char old_numbering[8192];
    strcpy(old_numbering, text);
    char *old_version = strstr(old_numbering, "project v8;");
    assert(old_version);
    old_version[9] = '6';
    Project retained = w.project;
    assert(!project_parse_text(old_numbering, &w.project, error, sizeof(error)));
    assert(!memcmp(&retained, &w.project, sizeof(retained)));
    /* The v7-to-v8 reorder changes IDs, never the physical antenna. New v8
     * projects must not be migrated a second time when reopened. */
    const int migrated_types[3] = {RF_ANT_SADDLE,RF_ANT_NAGOYA,RF_ANT_SINGLE_SADDLE};
    for(int old_type=6;old_type<=8;++old_type) {
        strcpy(old_numbering,text);
        old_version=strstr(old_numbering,"project v8;");assert(old_version);
        old_version[9]='7';
        char *type_field=strstr(old_numbering,"\ntype=");assert(type_field && type_field[7]=='\n');
        type_field[6]=(char)('0'+old_type);
        Project migrated;
        assert(project_parse_text(old_numbering,&migrated,error,sizeof(error)));
        assert(migrated.type==migrated_types[old_type-6]);
        assert(migrated.arc_angle_deg==saved.project.arc_angle_deg);
        old_version[9]='8';
        assert(project_parse_text(old_numbering,&migrated,error,sizeof(error)));
        assert(migrated.type==old_type);
    }
    size_t length = 0;
    for (size_t i = 0; i + 1 < count; ++i) {
        if (text[i] == '\n')
            crlf[length++] = '\r';
        crlf[length++] = text[i];
    }
    stream = fopen(path, "wb");
    assert(stream && fwrite(crlf, 1, length, stream) == length);
    assert(fclose(stream) == 0);
    assert(workbench_load(path, &w, error, sizeof(error)));
    assert(!w.plasma_enabled && w.project.global_phase_deg == -37.25);
    assert(w.plasma.density == 2.3e14 && w.plasma.solve_mode == RF_PLASMA_LIMITING_ABSORPTION);
    assert(w.options.m_min == -2 && w.options.m_max == 3 && w.options.phi == .75);
    assert(w.kmin == -23 && w.kmax == 31 && w.nk == 17);
    assert(!memcmp(&w.ray,&saved.ray,sizeof(w.ray)));
    stream=fopen(path,"ab");assert(stream);
    fputs("\n# ray.r0=.2\n",stream);assert(!fclose(stream));
    assert(!workbench_load(path,&w,error,sizeof(error)));
    assert(!memcmp(&w.ray,&saved.ray,sizeof(w.ray)));

    /* kz 的旧别名仍需参与同一个查重集合，失败后不提交部分状态。 */
    strcat(text, "spectrum_k_probe=12\n");
    Project unchanged = w.project;
    assert(!project_parse_text(text, &w.project, error, sizeof(error)));
    assert(!memcmp(&unchanged, &w.project, sizeof(unchanged)));

    /* 旧配套配置缺少 solve_mode 时保持有限碰撞默认，而非读取旧内存。 */
    stream = fopen(config, "rb");
    assert(stream);
    count = fread(text, 1, sizeof(text) - 1, stream);
    assert(!ferror(stream) && feof(stream));
    fclose(stream);
    text[count] = '\0';
    char *mode = strstr(text, "solve_mode=");
    assert(mode);
    char *next = strchr(mode, '\n');
    assert(next);
    memmove(mode, next + 1, strlen(next + 1) + 1);
    assert(plasma_config_parse_text(text, &w.plasma, &w.options, error, sizeof(error)));
    assert(w.plasma.solve_mode == RF_PLASMA_FINITE_COLLISION && w.options.phi == .75);

    /* 改名失败也须关闭并移除临时文件，目标目录和原工程仍完整。 */
    assert(!workbench_save(dir, &saved, error, sizeof(error)));
    char temporary[512];
    snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", dir, (long)getpid());
    assert(access(temporary, F_OK) != 0 && access(path, F_OK) == 0);
    unlink(config);
    unlink(path);
    rmdir(dir);
}
int main(void)
{
    linear_tests();
    geometry_tests();
    vacuum_tests();
    plasma_tests();
    persistence_tests();
    puts("CORE PASS: linear algebra, analytic spectra, 17 template geometries/phases, Maxwell "
         "scaling, plasma material/passivity/poles, source-only scans, failure states and "
         "persistence");
    return 0;
}
