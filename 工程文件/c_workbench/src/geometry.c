/* PDF 第一章：有限宽度、零厚度、规定电流天线。
 * 1--12 的闭合回路及 13--17 的给定电流使用同一 Fourier 源积分。
 * 开曲线或变相位电流的散度对应源电荷，由连续性方程确定。
 * 不联立求解导体内部电流、端口电压或馈电网络。 */
#include "geometry.h"
#include "rf_quadrature.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.141592653589793238462643383279502884
#endif

static int geometry_fail(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size != 0U) {
        (void)snprintf(error, error_size, "%s", message);
    }
    return 0;
}

static int geometry_fail_errno(char *error, size_t error_size, const char *prefix)
{
    if (error != NULL && error_size != 0U) {
        (void)snprintf(error, error_size, "%s: %s", prefix, strerror(errno));
    }
    return 0;
}

/* 拥有 segments 的结果对象须先置零或初始化；重复构建会释放旧路径。 */
void geometry_init(Geometry *geometry)
{
    if (geometry != NULL) {
        *geometry = (Geometry){ 0 };
    }
}

void geometry_free(Geometry *geometry)
{
    if (geometry != NULL) {
        free(geometry->segments);
        *geometry = (Geometry){ 0 };
    }
}

void geometry_release(Geometry *geometry)
{
    geometry_free(geometry);
}

size_t geometry_count(const Geometry *geometry)
{
    return geometry != NULL ? geometry->segment_count : 0U;
}

size_t geometry_segment_count(const Geometry *geometry)
{
    return geometry_count(geometry);
}

static int geometry_reserve(Geometry *geometry, size_t needed, char *error, size_t error_size)
{
    size_t capacity;
    GeometrySegment *segments;

    if (needed <= geometry->segment_capacity) {
        return 1;
    }
    capacity = geometry->segment_capacity != 0U ? geometry->segment_capacity : 64U;
    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2U) {
            return geometry_fail(error, error_size, "geometry segment count overflow");
        }
        capacity *= 2U;
    }
    if (capacity > (size_t)-1 / sizeof(*segments)) {
        return geometry_fail(error, error_size, "geometry allocation size overflow");
    }
    segments = (GeometrySegment *)realloc(geometry->segments, capacity * sizeof(*segments));
    if (segments == NULL) {
        return geometry_fail(error, error_size, "out of memory for geometry segments");
    }
    geometry->segments = segments;
    geometry->segment_capacity = capacity;
    return 1;
}

/* 存储一条有向电流段；只省略几何退化段，支路幅值与相位不能合并丢失。 */
static int geometry_push(Geometry *geometry,
    const double a[3],
    const double b[3],
    double phase,
    double amplitude,
    int group,
    int cylindrical,
    char *error,
    size_t error_size)
{
    GeometrySegment *segment;
    size_t index;

    if (geometry == NULL || a == NULL || b == NULL) {
        return geometry_fail(error, error_size, "geometry or segment endpoint is null");
    }
    if (!isfinite(a[0]) || !isfinite(a[1]) || !isfinite(a[2]) || !isfinite(b[0]) ||
        !isfinite(b[1]) || !isfinite(b[2]) || !isfinite(phase) || !isfinite(amplitude)) {
        return geometry_fail(error, error_size, "geometry segment contains a non-finite value");
    }
    if (fabs(a[0] - b[0]) < 1.0e-14 && fabs(a[1] - b[1]) < 1.0e-14 && fabs(a[2] - b[2]) < 1.0e-14) {
        return 1;
    }
    index = geometry->segment_count;
    if (!geometry_reserve(geometry, index + 1U, error, error_size)) {
        return 0;
    }
    segment = &geometry->segments[index];
    (void)memcpy(segment->a, a, sizeof(segment->a));
    (void)memcpy(segment->b, b, sizeof(segment->b));
    segment->phase = phase;
    segment->amplitude = amplitude;
    segment->phase_advance = 0.;
    segment->group = group;
    segment->cylindrical = cylindrical != 0 ? 1 : 0;
    segment->side_a[0] = segment->side_a[1] = segment->side_a[2] = 0.0;
    segment->side_b[0] = segment->side_b[1] = segment->side_b[2] = 0.0;
    geometry->segment_count = index + 1U;
    return 1;
}

static void point_on_cylinder(
    double radius, double phi, double z, double azimuth, double axial_offset, double point[3])
{
    const double angle = phi + azimuth;
    point[0] = radius * cos(angle);
    point[1] = radius * sin(angle);
    point[2] = z + axial_offset;
}

/* 几何和有限带宽等流线严格共用 PDF 第一章的规定电流。
 * source 的局部方向由端点顺序决定；global_phase/current_direction 只在积分时乘一次。
 * 边界图中的 vessel length 是显示窗口，轴向 Maxwell 模型没有端盖。 */
static void geometry_set_metadata(const Project *p, Geometry *g, int kind)
{
    g->band_width = p->band_width;
    g->reference_radius = p->antenna_radius;
    g->band_samples = p->band_samples;
    g->band_kind = kind;
}
static int valid_geometry(const Project *p, char *error, size_t n)
{
    if (!project_validate(p, error, n))
        return 0;
    double opening = p->width / p->antenna_radius;
    if (p->type == RF_ANT_SADDLE || p->type == RF_ANT_SINGLE_SADDLE)
        opening = p->arc_angle_deg * M_PI / 180.;
    if (p->type == RF_ANT_STRAP || p->type == RF_ANT_ARRAY || p->type == RF_ANT_COMB ||
        p->type == RF_ANT_SADDLE || p->type == RF_ANT_SINGLE_SADDLE) {
        if (p->band_width >= fmin(opening * p->antenna_radius, p->antenna_length) ||
            opening + p->band_width / p->antenna_radius >= 2. * M_PI)
            return geometry_fail(
                error, n, "矩形带宽必须小于回路两腿间距和轴向长度，外侧带边不得绕满一周");
    }
    if (p->type == RF_ANT_LINE || p->type == RF_ANT_PARTIAL_LOOP || p->type == RF_ANT_HALF_NAGOYA ||
        p->type == RF_ANT_HALF_ARRAY) {
        if (!(p->antenna_radius < p->return_radius && p->return_radius < p->radius))
            return geometry_fail(error, n, "回流半径必须满足 b < c < R");
    }
    return 1;
}
/* 圆柱曲线的 r,phi,z 对路径参数线性；保存局部宽度导数以便源积分还原
 * 正确的斜切转角。细分仅服务显示和相位积分，圆弧不会当作 Cartesian 弦。 */
static int append_curve(const Project *p,
    Geometry *g,
    double r0,
    double ph0,
    double z0,
    double r1,
    double ph1,
    double z1,
    const double wa[3],
    const double wb[3],
    double phase,
    double amp,
    int group,
    char *error,
    size_t n)
{
    int steps = (int)ceil(fmax(8., fabs(ph1 - ph0) * 32.));
    for (int j = 0; j < steps; ++j) {
        double a[3], b[3], ta = (double)j / steps, tb = (double)(j + 1) / steps;
        point_on_cylinder(r0 + (r1 - r0) * ta,
            ph0 + (ph1 - ph0) * ta,
            z0 + (z1 - z0) * ta,
            p->azimuth_deg * M_PI / 180.,
            p->axial_offset,
            a);
        point_on_cylinder(r0 + (r1 - r0) * tb,
            ph0 + (ph1 - ph0) * tb,
            z0 + (z1 - z0) * tb,
            p->azimuth_deg * M_PI / 180.,
            p->axial_offset,
            b);
        size_t old = g->segment_count;
        if (!geometry_push(g, a, b, phase, amp, group, 1, error, n))
            return 0;
        if (g->segment_count != old)
            for (int k = 0; k < 3; ++k) {
                g->segments[old].side_a[k] = wa[k] + (wb[k] - wa[k]) * ta;
                g->segments[old].side_b[k] = wa[k] + (wb[k] - wa[k]) * tb;
            }
    }
    return 1;
}
static int rectangle(const Project *p,
    Geometry *g,
    double centre,
    double alpha,
    double phase,
    double amp,
    int group,
    char *error,
    size_t n)
{
    double ph[4] = {
        centre - alpha / 2., centre - alpha / 2., centre + alpha / 2., centre + alpha / 2.
    };
    double zz[4] = { -p->antenna_length / 2.,
        p->antenna_length / 2.,
        p->antenna_length / 2.,
        -p->antenna_length / 2. };
    double normal[4][3] = { { 0, -1. / p->antenna_radius, -1 },
        { 0, -1. / p->antenna_radius, 1 },
        { 0, 1. / p->antenna_radius, 1 },
        { 0, 1. / p->antenna_radius, -1 } };
    for (int e = 0; e < 4; ++e) {
        int next = (e + 1) % 4;
        if (!append_curve(p,
                g,
                p->antenna_radius,
                ph[e],
                zz[e],
                p->antenna_radius,
                ph[next],
                zz[next],
                normal[e],
                normal[next],
                phase,
                amp,
                group,
                error,
                n))
            return 0;
    }
    return 1;
}
/* PDF §1.3.5、1.3.8：两个闭合 I/2 回路，共享腿叠加为 I，端部四条
 * 半圆各承载 I/2。eta=±1 的两个回路流向相同，不能错误地取相反电流。 */
static int helix_paths(const Project *p, Geometry *g, double twist, char *error, size_t n)
{
    double b = p->antenna_radius, L = p->antenna_length, A = hypot(L, b * twist);
    for (int eta = -1; eta <= 1; eta += 2) {
        double ph[5] = { 0, twist, twist + eta * M_PI, eta * M_PI, 0 };
        double zz[5] = { -L / 2., L / 2., L / 2., -L / 2., -L / 2. };
        double up = -(A - eta * b * twist) / (b * L), down = (A + eta * b * twist) / (b * L);
        double normal[5][3] = {
            { 0, up, eta }, { 0, up, eta }, { 0, down, eta }, { 0, down, eta }, { 0, up, eta }
        };
        for (int e = 0; e < 4; ++e)
            if (!append_curve(p,
                    g,
                    b,
                    ph[e],
                    zz[e],
                    b,
                    ph[e + 1],
                    zz[e + 1],
                    normal[e],
                    normal[e + 1],
                    0.,
                    .5,
                    0,
                    error,
                    n))
                return 0;
    }
    return 1;
}
static int partial_paths(
    const Project *p, Geometry *g, double centre, double phase, int group, char *error, size_t n)
{
    double b = p->antenna_radius, c = p->return_radius, half = p->arc_angle_deg * M_PI / 360.;
    double rr[5] = { b, b, c, c, b },
           ph[5] = { centre - half, centre + half, centre + half, centre - half, centre - half };
    const double width[3] = { 0, 0, 1 };
    for (int e = 0; e < 4; ++e)
        if (!append_curve(p,
                g,
                rr[e],
                ph[e],
                0,
                rr[e + 1],
                ph[e + 1],
                0,
                width,
                width,
                phase,
                1.,
                group,
                error,
                n))
            return 0;
    return 1;
}

/* Additional ideal current sources. No port, capacitance, substrate or
 * transmission-line solve is hidden in these builders. Planar currents are
 * axially averaged over band_width; cage/meander currents are rotated by u/b. */
static double complex angular_current(const Project *p, double phi)
{
    double a = p->source_mode * phi;
    return p->current_pattern ? cexp(I*a) : cos(a);
}
static int prescribed_paths(const Project *p, Geometry *g, char *error, size_t n)
{
    double b=p->antenna_radius, L=p->antenna_length;
    const double axial[3]={0,0,1}, angular[3]={0,1./b,0};
    if (p->type == RF_ANT_AXIAL_LOOPS) {
        for(int j=0;j<p->elements;++j) {
            double z=(j-(p->elements-1)/2.)*p->spacing;
            if(!append_curve(p,g,b,0,z,b,2*M_PI,z,axial,axial,
                j*p->phase_step*M_PI/180.,1.,j,error,n)) return 0;
        }
    } else if(p->type == RF_ANT_PLANAR_SPIRAL) {
        return append_curve(p,g,p->inner_radius,0,0,b,
            p->winding_direction*2*M_PI*p->turns,0,axial,axial,0,1,0,error,n);
    } else if(p->type == RF_ANT_SEGMENTED_RINGS) {
        for(int j=0;j<p->elements;++j) {
            double r=p->elements==1?p->inner_radius:
                p->inner_radius+(b-p->inner_radius)*j/(p->elements-1.);
            int sectors=j==0?2:4;
            double half=M_PI/sectors-p->gap_angle_deg*M_PI/360.;
            for(int q=0;q<sectors;++q) {
                double phi=2*M_PI*q/sectors;
                double complex a=angular_current(p,phi);
                if(!append_curve(p,g,r,phi-half,0,r,phi+half,0,axial,axial,
                    carg(a),cabs(a),4*j+q,error,n)) return 0;
            }
        }
    } else if(p->type == RF_ANT_BIRDCAGE) {
        double dphi=2*M_PI/p->elements;
        double complex denom=1.-cexp(-I*p->source_mode*dphi);
        for(int j=0;j<p->elements;++j) {
            double phi=j*dphi;
            double complex leg=angular_current(p,phi);
            double complex ring=cexp(I*p->source_mode*phi)/denom;
            if(!p->current_pattern) ring=creal(ring);
            if(!append_curve(p,g,b,phi,-L/2,b,phi,L/2,angular,angular,
                    carg(leg),cabs(leg),j,error,n) ||
               !append_curve(p,g,b,phi,L/2,b,phi+dphi,L/2,angular,angular,
                    carg(ring),cabs(ring),p->elements+j,error,n) ||
               !append_curve(p,g,b,phi,-L/2,b,phi+dphi,-L/2,angular,angular,
                    carg(-ring),cabs(ring),2*p->elements+j,error,n)) return 0;
        }
    } else if(p->type == RF_ANT_MEANDER) {
        double half=p->width/(2*b), distance=0.;
        for(int j=0;j<p->elements;++j) {
            double z=(j-(p->elements-1)/2.)*p->spacing;
            double ph0=(j%2?1.:-1.)*half,ph1=-ph0;
            for(int edge=0;edge<(j+1<p->elements?2:1);++edge) {
                double aphi=edge?ph1:ph0, az=z;
                double bphi=ph1, bz=z+(edge?p->spacing:0.);
                size_t first=g->segment_count;
                double length=edge?p->spacing:p->width;
                if(!append_curve(p,g,b,aphi,az,b,bphi,bz,angular,angular,
                    0,1,j,error,n)) return 0;
                size_t count=g->segment_count-first;
                for(size_t k=0;k<count;++k) {
                    GeometrySegment *s=&g->segments[first+k];
                    s->phase=p->current_phase_gradient*(distance+length*k/count);
                    s->phase_advance=p->current_phase_gradient*length/count;
                }
                distance+=length;
            }
        }
    }
    return 1;
}
int geometry_build(const Project *p, Geometry *g, char *error, size_t n)
{
    if (!g)
        return geometry_fail(error, n, "geometry is null");
    if (!valid_geometry(p, error, n))
        return 0;
    geometry_free(g);
    geometry_set_metadata(p, g, 0);
    double b = p->antenna_radius, L = p->antenna_length, alpha = p->width / b;
    int ok = 1;
    switch (p->type) {
    case RF_ANT_AXIAL_LOOPS: case RF_ANT_PLANAR_SPIRAL: case RF_ANT_SEGMENTED_RINGS:
    case RF_ANT_BIRDCAGE: case RF_ANT_MEANDER:
        ok = prescribed_paths(p,g,error,n);
        break;
    case RF_ANT_LOOP: {
        const double normal[3] = { 0, 0, 1 };
        ok = append_curve(p, g, b, 0, 0, b, 2 * M_PI, 0, normal, normal, 0, 1, 0, error, n);
        break;
    }
    case RF_ANT_HELIX:
        ok = helix_paths(p, g, p->winding_direction * 2 * M_PI * p->turns, error, n);
        break;
    case RF_ANT_NAGOYA:
        ok = helix_paths(p, g, 0., error, n);
        break;
    case RF_ANT_STRAP:
        ok = rectangle(p, g, 0, alpha, 0, 1, 0, error, n);
        break;
    case RF_ANT_ARRAY:
    case RF_ANT_COMB:
        for (int j = 0; j < p->elements && ok; ++j)
            ok = rectangle(p,
                g,
                (j - (p->elements - 1) / 2.) * p->spacing / b,
                alpha,
                j * p->phase_step * M_PI / 180.,
                1,
                j,
                error,
                n);
        break;
    case RF_ANT_SADDLE:
        alpha = p->arc_angle_deg * M_PI / 180.;
        ok = rectangle(p, g, 0, alpha, 0, -1, 0, error, n) &&
             rectangle(p, g, M_PI, alpha, 0, 1, 1, error, n);
        break;
    case RF_ANT_SINGLE_SADDLE:
        ok = rectangle(p, g, 0, p->arc_angle_deg * M_PI / 180., 0, -1, 0, error, n);
        break;
    case RF_ANT_PARTIAL_LOOP:
        ok = partial_paths(p, g, 0, 0, 0, error, n);
        break;
    case RF_ANT_HALF_ARRAY:
        for (int j = 0; j < p->elements && ok; ++j)
            ok = partial_paths(
                p, g, 2 * M_PI * j / p->elements, j * p->phase_step * M_PI / 180., j, error, n);
        break;
    case RF_ANT_HALF_NAGOYA: {
        double rr[5] = { b, b, p->return_radius, p->return_radius, b };
        double zz[5] = { -L / 2., L / 2., L / 2., -L / 2., -L / 2. };
        const double normal[3] = { 0, 1. / b, 0 };
        for (int j = 0; j < 4 && ok; ++j)
            ok = append_curve(
                p, g, rr[j], 0, zz[j], rr[j + 1], 0, zz[j + 1], normal, normal, 0, 1, 0, error, n);
        break;
    }
    case RF_ANT_LINE: {
        /* PDF §1.3.10：Cartesian 直弦 + 外圆弧，沿 z 展宽。 */
        double c = p->return_radius, half = sqrt(c * c - b * b), beta = acos(b / c);
        double a[3], z[3], theta = p->azimuth_deg * M_PI / 180.;
        a[0] = b * cos(theta) + half * sin(theta);
        a[1] = b * sin(theta) - half * cos(theta);
        a[2] = p->axial_offset;
        z[0] = b * cos(theta) - half * sin(theta);
        z[1] = b * sin(theta) + half * cos(theta);
        z[2] = p->axial_offset;
        ok = geometry_push(g, a, z, 0, 1, 0, 0, error, n);
        if (ok)
            g->segments[0].side_a[2] = g->segments[0].side_b[2] = 1.;
        const double normal[3] = { 0, 0, 1 };
        if (ok)
            ok = append_curve(p, g, c, beta, 0, c, -beta, 0, normal, normal, 0, 1, 0, error, n);
        break;
    }
    default:
        ok = geometry_fail(error, n, "unsupported antenna template");
    }
    if (!ok)
        geometry_free(g);
    return ok;
}
int geometry_build_loop(const Project *p, Geometry *g, char *error, size_t n)
{
    if (!p || p->type != RF_ANT_LOOP)
        return geometry_fail(error, n, "loop builder requires loop project");
    return geometry_build(p, g, error, n);
}
int geometry_build_helix(const Project *p, Geometry *g, char *error, size_t n)
{
    if (!p || p->type != RF_ANT_HELIX)
        return geometry_fail(error, n, "helix builder requires helix project");
    return geometry_build(p, g, error, n);
}

int geometry_export_obj(const Geometry *geometry, const char *path, char *error, size_t error_size)
{
    FILE *stream;
    size_t i;

    if (geometry == NULL || path == NULL || *path == '\0') {
        return geometry_fail(error, error_size, "geometry or OBJ path is invalid");
    }
    stream = fopen(path, "w");
    if (stream == NULL) {
        return geometry_fail_errno(error, error_size, "cannot open OBJ file");
    }
    if (fprintf(stream, "# SI metres; parameterized antenna current paths\n") < 0) {
        (void)fclose(stream);
        (void)remove(path);
        return geometry_fail(error, error_size, "cannot write OBJ header");
    }
    for (i = 0U; i < geometry->segment_count; ++i) {
        const GeometrySegment *segment = &geometry->segments[i];
        size_t vertex = 2U * i + 1U;
        if (fprintf(stream,
                "# rf_current %.17g %.17g %d\n"
                "# rf_phase_advance %.17g\n"
                "v %.12g %.12g %.12g\n"
                "v %.12g %.12g %.12g\n"
                "l %zu %zu\n",
                segment->phase,
                segment->amplitude,
                segment->group,
                segment->phase_advance,
                segment->a[0],
                segment->a[1],
                segment->a[2],
                segment->b[0],
                segment->b[1],
                segment->b[2],
                vertex,
                vertex + 1U) < 0) {
            (void)fclose(stream);
            (void)remove(path);
            return geometry_fail(error, error_size, "cannot write OBJ geometry");
        }
    }
    if (fclose(stream) != 0) {
        (void)remove(path);
        return geometry_fail(error, error_size, "cannot finalize OBJ file");
    }
    return 1;
}

/* PDF (1.6)、(2.29)：r dr 与源的 1/r 抵消后，只积分 I dl/(2pi)。
 * 此入口同时服务展示谱和场的弱式加载，杜绝两套源算法或半径合并。
 * 常半径段的相位精确积分；跨单元馈线先按径向节点切分，再作 Gauss 求积。 */
static double source_sinc(double x)
{
    if (fabs(x) < 1e-4) {
        double q = x * x;
        return 1. - q / 6. + q * q / 120. - q * q * q / 5040.;
    }
    return sin(x) / x;
}
static int compare_double(const void *a, const void *b)
{
    return (*(const double *)a > *(const double *)b) - (*(const double *)a < *(const double *)b);
}
int geometry_integrate_source(const Project *p,
    const Geometry *g,
    const double *nodes,
    size_t nn,
    RfSourceVisitor visit,
    void *context,
    char *error,
    size_t n)
{
    if (!p || !g || !visit || !g->segments || !g->segment_count || (nn && !nodes))
        return geometry_fail(error, n, "invalid source integration arguments");
    if (!isfinite(g->band_width) || g->band_width < 0. || g->band_samples > 32)
        return geometry_fail(error, n, "invalid finite-width source rule");
    if (!project_validate(p, error, n))
        return 0;
    double gx[32], gw[32], wx[32], ww[32];
    size_t nw = g->band_width > 0. ? (size_t)fmax(16., g->band_samples) : 1;
    if (!rf_gauss_legendre(16, gx, gw) || !rf_gauss_legendre(nw, wx, ww))
        return geometry_fail(error, n, "Gauss source rule failed");
    size_t capacity = 2 * nn + 16;
    double *cuts = malloc(capacity * sizeof(*cuts));
    if (!cuts)
        return geometry_fail(error, n, "out of memory for radial source cuts");
    for (size_t segment = 0; segment < g->segment_count; ++segment) {
        const GeometrySegment *s = &g->segments[segment];
        double ra = hypot(s->a[0], s->a[1]), rb = hypot(s->b[0], s->b[1]);
        double pa = atan2(s->a[1], s->a[0]);
        double dphi = remainder(atan2(s->b[1], s->b[0]) - pa, 2 * M_PI);
        /* 高 |kz|、宽带或高 m 时，固定宽度点数会混叠。按端点最大相位
         * 梯度细分 u，再在每段使用同一个 Gauss 规则；各宽度权重和为 1。 */
        double width_phase = 0.;
        for (int end = 0; end < 2; ++end) {
            const double *side = end ? s->side_b : s->side_a;
            double gradient = s->cylindrical ? fabs(p->selected_m * side[1]) + fabs(p->kz * side[2])
                                             : fabs(p->kz * side[2]);
            width_phase = fmax(width_phase, g->band_width * gradient);
        }
        if (!isfinite(width_phase) || width_phase > 1e7) {
            free(cuts);
            return geometry_fail(error, n, "带宽相位变化超过可分辨的源积分范围");
        }
        size_t width_pieces = (size_t)fmax(1., ceil(width_phase / (2 * M_PI)));
        for (size_t iw = 0; iw < nw * width_pieces; ++iw) {
            size_t part = iw / nw, qw = iw % nw;
            double u = g->band_width * ((part + (wx[qw] + 1.) / 2.) / width_pieces - .5);
            double width_weight = ww[qw] / (2. * width_pieces);
            double complex current =
                p->current * p->current_direction * s->amplitude * width_weight *
                cexp(I * (s->phase + p->global_phase_deg * M_PI / 180.)) / (2 * M_PI);
            double a[3], d[3];
            if (s->cylindrical) {
                a[0] = ra + u * s->side_a[0];
                a[1] = pa + u * s->side_a[1];
                a[2] = s->a[2] + u * s->side_a[2];
                d[0] = rb - ra + u * (s->side_b[0] - s->side_a[0]);
                d[1] = dphi + u * (s->side_b[1] - s->side_a[1]);
                d[2] = s->b[2] - s->a[2] + u * (s->side_b[2] - s->side_a[2]);
                if (fabs(d[0]) <= 1e-13 * fmax(ra, rb)) {
                    double complex weight = current * cexp(I*s->phase_advance/2.) *
                                            cexp(-I * (p->selected_m * (a[1] + d[1] / 2.) +
                                                          p->kz * (a[2] + d[2] / 2.))) *
                                            source_sinc((p->selected_m * d[1] + p->kz * d[2] - s->phase_advance) / 2.);
                    double complex dl[3] = { 0., weight * a[0] * d[1], weight * d[2] };
                    if (!visit(context, a[0], dl))
                        goto invalid;
                    continue;
                }
            } else
                for (int k = 0; k < 3; ++k) {
                    a[k] = s->a[k] + u * s->side_a[k];
                    d[k] = s->b[k] - s->a[k] + u * (s->side_b[k] - s->side_a[k]);
                }
            size_t count = 0;
            cuts[count++] = 0.;
            cuts[count++] = 1.;
            if (s->cylindrical) {
                for (size_t j = 0; j < nn; ++j) {
                    double t = (nodes[j] - a[0]) / d[0];
                    if (t > 0 && t < 1)
                        cuts[count++] = t;
                }
            } else {
                double aa = d[0] * d[0] + d[1] * d[1], bb = 2 * (a[0] * d[0] + a[1] * d[1]);
                if (aa > 0.) {
                    double turn = -bb / (2 * aa);
                    if (turn > 0 && turn < 1)
                        cuts[count++] = turn;
                    for (size_t j = 0; j < nn; ++j) {
                        double cc = a[0] * a[0] + a[1] * a[1] - nodes[j] * nodes[j],
                               disc = bb * bb - 4 * aa * cc;
                        if (disc >= 0)
                            for (int sign = -1; sign <= 1; sign += 2) {
                                double t = (-bb + sign * sqrt(disc)) / (2 * aa);
                                if (t > 0 && t < 1)
                                    cuts[count++] = t;
                            }
                    }
                }
            }
            qsort(cuts, count, sizeof(*cuts), compare_double);
            for (size_t j = 1; j < count; ++j) {
                double left = cuts[j - 1], right = cuts[j];
                if (right - left < 1e-14)
                    continue;
                /* 每个子区间的相位变化控制到约 pi；Gauss 点负责剩余平滑因子。 */
                double span = (fabs(p->kz * d[2]) + fabs(s->phase_advance)) * (right - left) +
                              abs(p->selected_m) * M_PI * (right - left);
                size_t pieces = (size_t)fmax(1., ceil(span / M_PI));
                for (size_t part = 0; part < pieces; ++part)
                    for (size_t q = 0; q < 16; ++q) {
                        double dt = (right - left) / pieces;
                        double t = left + dt * (part + (gx[q] + 1.) / 2.), radius, phi, z,
                               dlreal[3];
                        if (s->cylindrical) {
                            radius = a[0] + d[0] * t;
                            phi = a[1] + d[1] * t;
                            z = a[2] + d[2] * t;
                            dlreal[0] = d[0];
                            dlreal[1] = radius * d[1];
                            dlreal[2] = d[2];
                        } else {
                            double x = a[0] + d[0] * t, y = a[1] + d[1] * t;
                            radius = hypot(x, y);
                            phi = atan2(y, x);
                            z = a[2] + d[2] * t;
                            if (radius <= 0.)
                                goto invalid;
                            dlreal[0] = (x * d[0] + y * d[1]) / radius;
                            dlreal[1] = (-y * d[0] + x * d[1]) / radius;
                            dlreal[2] = d[2];
                        }
                        double complex weight = current * cexp(I*s->phase_advance*t) *
                                                cexp(-I * (p->selected_m * phi + p->kz * z)) * dt *
                                                gw[q] / 2.;
                        double complex dl[3] = {
                            weight * dlreal[0], weight * dlreal[1], weight * dlreal[2]
                        };
                        if (!visit(context, radius, dl))
                            goto invalid;
                    }
            }
        }
    }
    free(cuts);
    return 1;
invalid:
    free(cuts);
    return geometry_fail(
        error, n, "source is outside the radial domain or its weighted integral is non-finite");
}
static int spectrum_visit(void *context, double radius, const double complex dl[3])
{
    double complex *out = context;
    (void)radius;
    for (int k = 0; k < 3; ++k)
        out[k] += dl[k];
    return 1;
}
int geometry_current_spectrum(
    const Project *p, const Geometry *g, double complex out[3], char *error, size_t n)
{
    if (!out)
        return geometry_fail(error, n, "spectrum is null");
    out[0] = out[1] = out[2] = 0.;
    if (!geometry_integrate_source(p, g, NULL, 0, spectrum_visit, out, error, n))
        return 0;
    for (int k = 0; k < 3; ++k) {
        out[k] /= p->antenna_radius;
        if (!isfinite(creal(out[k])) || !isfinite(cimag(out[k])))
            return geometry_fail(error, n, "current spectrum is non-finite");
    }
    return 1;
}

/* 逐节点累加流出减流入的复电流，再用支路幅值和归一化；这是拓扑检查，
 * 不等同于 Maxwell 连续性方程或独立的能量守恒验证。复杂度 O(N²)。 */
double geometry_current_balance(const Geometry *g)
{
    size_t i, j;
    double worst = 0.;
    if (g == NULL || g->segment_count == 0U)
        return INFINITY;
    for (i = 0; i < 2U * g->segment_count; ++i) {
        const GeometrySegment *si = &g->segments[i / 2U];
        const double *pi = (i % 2U) ? si->b : si->a;
        double complex sum = 0.;
        double norm = 0.;
        for (j = 0; j < 2U * g->segment_count; ++j) {
            const GeometrySegment *sj = &g->segments[j / 2U];
            const double *pj = (j % 2U) ? sj->b : sj->a;
            if (hypot(hypot(pi[0] - pj[0], pi[1] - pj[1]), pi[2] - pj[2]) <= 1e-9) {
                double complex current = sj->amplitude *
                    cexp(I * (sj->phase + (j % 2U ? sj->phase_advance : 0.)));
                if (j % 2U)
                    current = -current;
                sum += current;
                norm += cabs(current);
            }
        }
        if (norm > 0.)
            worst = fmax(worst, cabs(sum) / norm);
    }
    return worst;
}
