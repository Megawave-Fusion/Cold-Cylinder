/* Alpha-composited geometry for Tk's canvas, whose polygons have no RGBA fill.
 * Painter-sorted facets and antenna segments share one supersampled image. */
typedef struct ModelPrimitive {
    double xy[16], alpha, width;
    unsigned char rgb[3];
    int count, line;
} ModelPrimitive;

/* 输入 alpha 已验证为 [0,1]；像素采用不透明背景上的逐层颜色混合。 */
static void model_blend(unsigned char *pixel, const ModelPrimitive *p)
{
    int k;
    for (k = 0; k < 3; ++k)
        pixel[k] = (unsigned char)lround(p->rgb[k] * p->alpha + pixel[k] * (1. - p->alpha));
}
/* 扫描线使用半开边规则，避免相邻三角形公共边被重复混色；坐标单位为超采样像素。 */
static void model_polygon(unsigned char *pixels, int width, int height, const ModelPrimitive *p)
{
    double ymin = height, ymax = 0.;
    int y, j;
    for (j = 0; j < p->count; ++j) {
        ymin = fmin(ymin, p->xy[2 * j + 1]);
        ymax = fmax(ymax, p->xy[2 * j + 1]);
    }
    for (y = (int)fmax(0., ceil(ymin - .5)); y < (int)fmin(height, ceil(ymax - .5)); ++y) {
        double intersections[8], py = y + .5;
        int n = 0, i, x;
        for (i = 0, j = p->count - 1; i < p->count; j = i++) {
            double yi = p->xy[2 * i + 1], yj = p->xy[2 * j + 1];
            /* Half-open edges assign each shared edge to exactly one face. */
            if ((yi <= py && yj > py) || (yj <= py && yi > py))
                intersections[n++] =
                    p->xy[2 * i] + (py - yi) * (p->xy[2 * j] - p->xy[2 * i]) / (yj - yi);
        }
        for (i = 1; i < n; ++i) {
            double v = intersections[i];
            j = i;
            while (j && intersections[j - 1] > v) {
                intersections[j] = intersections[j - 1];
                --j;
            }
            intersections[j] = v;
        }
        for (i = 0; i + 1 < n; i += 2)
            for (x = (int)fmax(0., ceil(intersections[i] - .5));
                 x < (int)fmin(width, ceil(intersections[i + 1] - .5));
                 ++x)
                model_blend(pixels + 4 * ((size_t)y * width + x), p);
    }
}
/* 按像素中心到有限线段的距离覆盖圆头线，计算范围先裁剪到画布。 */
static void model_line(unsigned char *pixels, int width, int height, const ModelPrimitive *p)
{
    double x0 = p->xy[0], y0 = p->xy[1], dx = p->xy[2] - x0, dy = p->xy[3] - y0;
    double len = dx * dx + dy * dy, r = p->width / 2.;
    int x, y;
    int left = (int)fmax(0., floor(fmin(x0, p->xy[2]) - r)),
        right = (int)fmin(width, ceil(fmax(x0, p->xy[2]) + r));
    int top = (int)fmax(0., floor(fmin(y0, p->xy[3]) - r)),
        bottom = (int)fmin(height, ceil(fmax(y0, p->xy[3]) + r));
    for (y = top; y < bottom; ++y)
        for (x = left; x < right; ++x) {
            double t =
                len > 0 ? fmax(0., fmin(1., ((x + .5 - x0) * dx + (y + .5 - y0) * dy) / len)) : 0.;
            double px = x + .5 - x0 - t * dx, py = y + .5 - y0 - t * dy;
            if (px * px + py * py <= r * r)
                model_blend(pixels + 4 * ((size_t)y * width + x), p);
        }
}
/* 先验证图元和尺寸，再用 1×/2× 超采样绘制；Tk 复制像素后立即释放 C 缓冲区。 */
static int render_model_command(ClientData cd, Tcl_Interp *ip, int argc, Tcl_Obj *const argv[])
{
    int width, height, count, i, j, sample, sw, sh, status = TCL_ERROR;
    size_t size;
    Tcl_Obj **items;
    ModelPrimitive *primitives = NULL;
    unsigned char *pixels = NULL, *small = NULL;
    Tk_PhotoHandle photo;
    Tk_PhotoImageBlock block;
    unsigned int bg[3];
    (void)cd;
    if (argc != 6) {
        Tcl_WrongNumArgs(ip, 1, argv, "photo width height primitives background");
        return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(ip, argv[2], &width) != TCL_OK ||
        Tcl_GetIntFromObj(ip, argv[3], &height) != TCL_OK)
        return TCL_ERROR;
    if (width < 1 || height < 1 || width > 4096 || height > 4096)
        return failure(ip, "模型画布尺寸无效。");
    photo = Tk_FindPhoto(ip, Tcl_GetString(argv[1]));
    if (!photo)
        return failure(ip, "模型图像不存在。");
    if (sscanf(Tcl_GetString(argv[5]), "#%2x%2x%2x", &bg[0], &bg[1], &bg[2]) != 3)
        return failure(ip, "模型背景颜色无效。");
    if (Tcl_ListObjGetElements(ip, argv[4], &count, &items) != TCL_OK)
        return TCL_ERROR;
    if (count > 100000)
        return failure(ip, "模型面片过多。");
    primitives = calloc((size_t)count + 1, sizeof(*primitives));
    if (!primitives)
        return failure(ip, "模型图像内存不足。");
    sample = width <= 1600 && height <= 1600 ? 2 : 1;
    sw = width * sample;
    sh = height * sample;
    for (i = 0; i < count; ++i) {
        Tcl_Obj **fields, **coords;
        int n, nc;
        unsigned int rgb[3];
        ModelPrimitive *p = primitives + i;
        if (Tcl_ListObjGetElements(ip, items[i], &n, &fields) != TCL_OK)
            goto done;
        if (n != 6) {
            failure(ip, "模型图元格式无效。");
            goto done;
        }
        p->line = strcmp(Tcl_GetString(fields[0]), "line") == 0;
        if (!p->line && strcmp(Tcl_GetString(fields[0]), "polygon")) {
            failure(ip, "模型图元类型无效。");
            goto done;
        }
        if (Tcl_ListObjGetElements(ip, fields[1], &nc, &coords) != TCL_OK)
            goto done;
        if ((p->line && nc != 4) || (!p->line && (nc < 6 || nc > 16 || nc % 2))) {
            failure(ip, "模型图元坐标无效。");
            goto done;
        }
        p->count = nc / 2;
        for (j = 0; j < nc; ++j) {
            if (Tcl_GetDoubleFromObj(ip, coords[j], &p->xy[j]) != TCL_OK)
                goto done;
            if (!isfinite(p->xy[j]) || fabs(p->xy[j]) > 1e7) {
                failure(ip, "模型图元坐标超出范围。");
                goto done;
            }
            p->xy[j] *= sample;
        }
        if (sscanf(Tcl_GetString(fields[2]), "#%2x%2x%2x", &rgb[0], &rgb[1], &rgb[2]) != 3) {
            failure(ip, "模型颜色无效。");
            goto done;
        }
        for (j = 0; j < 3; ++j)
            p->rgb[j] = (unsigned char)rgb[j];
        if (Tcl_GetDoubleFromObj(ip, fields[3], &p->alpha) != TCL_OK ||
            Tcl_GetDoubleFromObj(ip, fields[4], &p->width) != TCL_OK)
            goto done;
        if (!isfinite(p->alpha) || p->alpha < 0 || p->alpha > 1 || !isfinite(p->width) ||
            p->width < 0 || p->width > 100) {
            failure(ip, "模型透明度或线宽无效。");
            goto done;
        }
        p->width *= sample;
    }
    size = (size_t)sw * sh * 4;
    pixels = malloc(size);
    small = malloc((size_t)width * height * 4);
    if (!pixels || !small) {
        failure(ip, "模型图像内存不足。");
        goto done;
    }
    for (size_t k = 0; k < size; k += 4) {
        for (j = 0; j < 3; ++j)
            pixels[k + j] = (unsigned char)bg[j];
        pixels[k + 3] = 255;
    }
    for (i = 0; i < count; ++i) {
        if (primitives[i].line)
            model_line(pixels, sw, sh, primitives + i);
        else
            model_polygon(pixels, sw, sh, primitives + i);
    }
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            unsigned char *out = small + 4 * ((size_t)y * width + x);
            for (j = 0; j < 3; ++j) {
                unsigned sum = 0;
                for (int yy = 0; yy < sample; ++yy)
                    for (int xx = 0; xx < sample; ++xx)
                        sum += pixels[4 * ((size_t)(y * sample + yy) * sw + x * sample + xx) + j];
                out[j] = (unsigned char)(sum / (sample * sample));
            }
            out[3] = 255;
        }
    block.pixelPtr = small;
    block.width = width;
    block.height = height;
    block.pitch = width * 4;
    block.pixelSize = 4;
    block.offset[0] = 0;
    block.offset[1] = 1;
    block.offset[2] = 2;
    block.offset[3] = 3;
    status = Tk_PhotoSetSize(ip, photo, width, height);
    if (status == TCL_OK)
        status = Tk_PhotoPutBlock(ip, photo, &block, 0, 0, width, height, TK_PHOTO_COMPOSITE_SET);
done:
    free(primitives);
    free(pixels);
    free(small);
    return status;
}
