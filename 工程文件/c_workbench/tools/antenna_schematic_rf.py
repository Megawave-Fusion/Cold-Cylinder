"""Scientific structure schematics for two RF plasma launchers.

The parent figure supplies Chinese font configuration and panel titles.
These drawings show topology and layers; dimensions and fold counts are schematic.
"""

import numpy as np
from matplotlib.patches import FancyArrowPatch, Polygon


INK = '#20384b'
MUTED = '#607184'
COPPER = '#bd6a29'
METAL = '#697785'
BLUE = '#2b72ae'
GREEN = '#21765e'


def _polygon(ax, points, facecolor='none', edgecolor=INK, **kwargs):
    patch = Polygon(points, closed=True, facecolor=facecolor,
                    edgecolor=edgecolor, **kwargs)
    ax.add_patch(patch)
    return patch


def _leader(ax, text, xy, xytext, *, ha='center', color=INK, fontsize=18):
    return ax.annotate(
        text, xy=xy, xytext=xytext, ha=ha, va='center',
        fontsize=fontsize, color=color,
        arrowprops=dict(arrowstyle='-', color=MUTED, lw=1.2,
                        shrinkA=5, shrinkB=3,
                        connectionstyle='arc3,rad=0'),
        zorder=30,
    )


def draw_meander(ax):
    """Draw one continuous covered meander microstrip over a ground plane.

    Based on Matsuno et al., PFR 1, 009 (2006), Figs. 1--2.
    The return conductor is the separate ground plane, not an invented
    copper bridge closing the top microstrip into a loop.
    """
    ax.axis('off')
    ax.set_aspect('equal')
    ax.set_xlim(-1.65, 1.85)
    ax.set_ylim(-1.3, 1.3)

    def project(u, v, h):
        return np.array([u + 0.42 * v, 0.48 * v + h])

    def plane(h, u0=-1.12, u1=1.12, v0=-0.52, v1=0.52):
        return np.array([project(u0, v0, h), project(u1, v0, h),
                         project(u1, v1, h), project(u0, v1, h)])

    # Full ground plane, with a thin visible edge.
    _polygon(ax, plane(-0.31), facecolor='#c2c9cf', edgecolor=METAL,
             linewidth=1.5, zorder=1)
    edge = [project(-1.12, -0.52, -0.31),
            project(1.12, -0.52, -0.31),
            project(1.12, -0.52, -0.36),
            project(-1.12, -0.52, -0.36)]
    _polygon(ax, edge, facecolor=METAL, edgecolor=METAL, zorder=2)

    # Dielectric slab below the copper trace.
    substrate_top, substrate_bottom = 0.015, -0.25
    _polygon(ax, [project(-1.12, -0.52, substrate_top),
                  project(1.12, -0.52, substrate_top),
                  project(1.12, -0.52, substrate_bottom),
                  project(-1.12, -0.52, substrate_bottom)],
             facecolor='#e3d8b6', edgecolor='#b8ad8c',
             linewidth=1.2, zorder=3)
    _polygon(ax, [project(1.12, -0.52, substrate_top),
                  project(1.12, 0.52, substrate_top),
                  project(1.12, 0.52, substrate_bottom),
                  project(1.12, -0.52, substrate_bottom)],
             facecolor='#e8dfc4', edgecolor='#b8ad8c',
             linewidth=1.2, zorder=3)
    _polygon(ax, plane(substrate_top), facecolor='#f4eedb',
             edgecolor='#b8ad8c', linewidth=1.2, zorder=4)

    # Seven long legs, joined alternately at opposite ends: a single trace.
    us = np.linspace(-0.96, 0.96, 7)
    v = -0.41
    uv = [(-1.15, v), (us[0], v)]
    for index, u in enumerate(us):
        v = -v
        uv.append((u, v))
        if index < len(us) - 1:
            uv.append((us[index + 1], v))
    uv.append((1.15, v))
    trace = np.array([project(u, vv, 0.045) for u, vv in uv])
    ax.plot(trace[:, 0], trace[:, 1], color=COPPER, linewidth=6,
            solid_capstyle='butt', solid_joinstyle='miter', zorder=7)
    ax.scatter(trace[[0, -1], 0], trace[[0, -1], 1], s=26,
               facecolor='white', edgecolor=COPPER, linewidth=1.6, zorder=9)

    # Transparent cover, raised enough to make its separate layer legible.
    cover_h = 0.29
    _polygon(ax, plane(cover_h), facecolor='#d9edf5', edgecolor=BLUE,
             linewidth=1.2, alpha=0.30, zorder=10)
    for u, vv in [(-1.12, -0.52), (1.12, -0.52), (1.12, 0.52)]:
        a, b = project(u, vv, substrate_top), project(u, vv, cover_h)
        ax.plot([a[0], b[0]], [a[1], b[1]], color=BLUE,
                lw=1, alpha=0.45, linestyle=(0, (3, 3)), zorder=11)

    # Phase progression is along the array periodicity, parallel to B0.
    ax.add_patch(FancyArrowPatch((-0.85, 0.84), (0.50, 0.84),
                                arrowstyle='-|>', mutation_scale=18,
                                linewidth=1.8, color=GREEN, zorder=20))
    ax.text(-0.18, 1.02, r'$B_0\;\parallel\;p$', fontsize=20,
            color=GREEN, ha='center', va='center')

    _leader(ax, 'RF 输入', trace[0], (-1.30, 0.20))
    _leader(ax, '末端', trace[-1], (1.54, -0.02))
    _leader(ax, '介质覆层', project(0.82, 0.50, cover_h), (1.18, 1.04))
    _leader(ax, '介质基板', project(0.55, -0.52, -0.11), (0.68, -0.94))
    _leader(ax, '接地背板', project(-0.70, -0.52, -0.34), (-0.93, -1.06))


def draw_folded_waveguide(ax):
    """Draw a cutaway metal folded cavity and its polarizing aperture face.

    The folds lie in the transverse cross-section. They are metal vanes,
    not a meandering wire or the direction followed by the power flow.
    Conceptual geometry follows ORNL's folded-waveguide coupler and
    Torii et al., JPFR Series 5, 310--313 (2002), Fig. 1.
    """
    ax.axis('off')
    ax.set_aspect('equal')
    ax.set_xlim(-1.55, 2.35)
    ax.set_ylim(-1.3, 1.3)

    def project(u, v, w):
        # w is the waveguide axis, from the shorted back toward the mouth.
        return np.array([u + 0.86 * w - 0.35, v - 0.30 * w + 0.15])

    u0, u1, v0, v1 = -0.78, 0.78, -0.49, 0.49
    back, front = 0.0, 1.0

    # Back shorting wall and visible outer sides; the top is cut away.
    _polygon(ax, [project(u0, v0, back), project(u1, v0, back),
                  project(u1, v1, back), project(u0, v1, back)],
             facecolor='#e7ebee', edgecolor=METAL, linewidth=1.5, zorder=1)
    _polygon(ax, [project(u0, v0, back), project(u1, v0, back),
                  project(u1, v0, front), project(u0, v0, front)],
             facecolor='#c1c9d0', edgecolor=METAL, linewidth=1.5, zorder=2)
    _polygon(ax, [project(u0, v0, back), project(u0, v1, back),
                  project(u0, v1, front), project(u0, v0, front)],
             facecolor='#d0d6dc', edgecolor=METAL, linewidth=1.5, zorder=3)
    _polygon(ax, [project(u1, v0, back), project(u1, v1, back),
                  project(u1, v1, front), project(u1, v0, front)],
             facecolor='#e1e6ea', edgecolor=METAL, linewidth=1.5, zorder=3)

    # Alternating fins extend from opposite transverse cavity walls.
    # All are extruded along w; the air/vacuum between them is connected.
    for index, u in enumerate(np.linspace(-0.56, 0.56, 5)):
        if index % 2 == 0:
            bottom, top = v0, 0.27
        else:
            bottom, top = -0.27, v1
        width = 0.045
        _polygon(ax, [project(u, bottom, back), project(u, top, back),
                      project(u, top, front), project(u, bottom, front)],
                 facecolor='#aeb9c2', edgecolor=METAL,
                 linewidth=1.0, zorder=5 + index)
        _polygon(ax, [project(u - width / 2, bottom, front),
                      project(u + width / 2, bottom, front),
                      project(u + width / 2, top, front),
                      project(u - width / 2, top, front)],
                 facecolor=METAL, edgecolor=METAL,
                 linewidth=0.8, zorder=12)

    # The outer mouth rim. Dashed upper longitudinal edges indicate cutaway.
    mouth = [project(u0, v0, front), project(u1, v0, front),
             project(u1, v1, front), project(u0, v1, front)]
    _polygon(ax, mouth, edgecolor=METAL, linewidth=2.2, zorder=14)
    for u in (u0, u1):
        a, b = project(u, v1, back), project(u, v1, front)
        ax.plot([a[0], b[0]], [a[1], b[1]], color=METAL,
                lw=1.2, linestyle=(0, (4, 3)), zorder=15)

    # A slightly separated mouth plate makes the apertures easy to see.
    # Bars represent the polarizing metal; white gaps are true apertures.
    face_w = 1.18
    frame = [project(u0 - 0.035, v0 - 0.035, face_w),
             project(u1 + 0.035, v0 - 0.035, face_w),
             project(u1 + 0.035, v1 + 0.035, face_w),
             project(u0 - 0.035, v1 + 0.035, face_w)]
    _polygon(ax, frame, facecolor='#f7f9fa', edgecolor=METAL,
             linewidth=2.0, alpha=0.94, zorder=17)
    # Open rectangles are cut-outs in a continuous plate, rather than wires.
    for lower, upper in [(-0.39, -0.19), (-0.10, 0.10), (0.19, 0.39)]:
        _polygon(ax, [project(-0.66, lower, face_w),
                      project(0.66, lower, face_w),
                      project(0.66, upper, face_w),
                      project(-0.66, upper, face_w)],
                 facecolor='white', edgecolor=METAL,
                 linewidth=1.0, zorder=18)

    # Side coaxial feed: a short hollow feedthrough, not another antenna loop.
    feed_end = project(u0, -0.14, 0.35)
    feed_start = feed_end + np.array([-0.50, 0.0])
    ax.plot([feed_start[0], feed_end[0]],
            [feed_start[1], feed_end[1]], color=METAL,
            linewidth=9, solid_capstyle='butt', zorder=19)
    ax.plot([feed_start[0], feed_end[0]],
            [feed_start[1], feed_end[1]], color='white',
            linewidth=4, solid_capstyle='butt', zorder=20)
    ax.plot([feed_start[0], feed_end[0] + 0.06],
            [feed_start[1], feed_end[1]], color=COPPER,
            linewidth=1.9, solid_capstyle='butt', zorder=21)

    # Power leaves the face along +w. It does not follow the folded section.
    axis_start = project(0.0, 0.0, face_w + 0.02)
    axis_end = project(0.0, 0.0, 2.53)
    ax.add_patch(FancyArrowPatch(axis_start, axis_end, arrowstyle='-|>',
                                mutation_scale=23, color=BLUE,
                                linewidth=3.0, zorder=25))
    ax.text(1.88, -0.29, r'$P_{\rm RF}$', color=BLUE, fontsize=20,
            ha='center', va='center', zorder=26)
    ax.text(2.12, -0.82, '等离子体', color=BLUE, fontsize=18,
            ha='center', va='center', zorder=26)

    _leader(ax, '侧面馈电', feed_start + np.array([0.18, 0]),
            (-1.05, -0.93))
    _leader(ax, '金属折叠腔', project(-0.42, 0.27, 0.32),
            (-0.55, 1.03))
    _leader(ax, '极化板', project(0.55, 0.45, face_w), (1.47, 0.90))
    _leader(ax, '耦合开口', project(0.49, -0.29, face_w), (0.73, -1.10))
