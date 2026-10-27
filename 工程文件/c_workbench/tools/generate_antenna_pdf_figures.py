#!/usr/bin/env python3
"""Refresh embedded antenna figures and the audit table in the single TeX source.

Run rf-antenna-test with the four output paths in tmp/pdfs/data before this script.
Only Python's standard library is required. Coordinates are SI metres.
"""
import csv
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DOC = ROOT / '天线电流谱文档'
DATA = ROOT / 'tmp/pdfs/data'
MAIN = DOC / 'Finite_width_antenna_current_spectra.tex'
GENERATED = {}
ROWS = list(csv.DictReader((DATA / 'double_saddle_geometry.csv').open()))
AUDIT = list(csv.DictReader((DATA / 'antenna_checks.csv').open()))
assert [int(r['number']) for r in AUDIT] == list(range(1, 18))

B, LENGTH, WIDTH, ALPHA = .44, .70, .04, 2*math.pi/3
YAW, ELEV = math.radians(65), math.radians(22)


def project(x, y, z):
    q = x * math.sin(YAW) + y * math.cos(YAW)
    return (x * math.cos(YAW) - y * math.sin(YAW),
            z * math.cos(ELEV) - q * math.sin(ELEV),
            q * math.cos(ELEV) + z * math.sin(ELEV))


def xyz(phi, z):
    return project(B * math.cos(phi), B * math.sin(phi), z)


def boundary(row, end, u):
    x, y, z = (float(row[end + k]) for k in 'xyz')
    side = 's' + end + '_'
    r = math.hypot(x, y) + u * float(row[side + 'r'])
    phi = math.atan2(y, x) + u * float(row[side + 'phi'])
    z += u * float(row[side + 'z'])
    return project(r * math.cos(phi), r * math.sin(phi), z)


def point(p):
    return f'({p[0]:.6f},{p[1]:.6f})'


colors = ['copper', 'teal']
facets = []
for row in ROWS:
    group = int(row['group'])
    assert group in (0, 1)
    assert float(row['amplitude']) == (-1 if group == 0 else 1)
    assert float(row['phase']) == 0
    vertices = [boundary(row, 'a', -WIDTH / 2), boundary(row, 'b', -WIDTH / 2),
                boundary(row, 'b', WIDTH / 2), boundary(row, 'a', WIDTH / 2)]
    assert all(abs(math.hypot(float(row[k+'x']), float(row[k+'y'])) - B) < 1e-12
               for k in ('a', 'b'))
    facets.append((sum(p[2] for p in vertices) / 4, group, vertices))

tex = [r'% Generated from the current C Geometry, catalogue number 7.',
       r'\begin{minipage}[t]{.49\linewidth}\centering\vspace{0pt}',
       r'\begin{tikzpicture}[x=5.2cm,y=5.2cm,font=\small,>=Latex]',
       r'\path[use as bounding box] (-.60,-.60) rectangle (.60,.60);',
       r'\draw[gray!55,densely dashed,->] (0,-.53)--(0,.53) node[above] {$z$};']
for _, group, vertices in sorted(facets):
    color = colors[group]
    tex.append(f'\\path[fill={color}!82,draw={color}!82,line width=.06pt] ' +
               ' -- '.join(map(point, vertices)) + r' -- cycle;')

# Reference current at current_direction=+1, phase=0. Arrows follow the signed
# amplitudes from the kernel, including reversed loop A and anti-driven loop B.
for group, center in enumerate((0., math.pi)):
    sign = (-1, 1)[group]
    for offset, original in ((-ALPHA/2, 1), (ALPHA/2, -1)):
        direction = original * sign
        p, q = xyz(center + offset, -.11*direction), xyz(center + offset, .11*direction)
        tex.append(r'\draw[navy,line width=.7pt,-{Latex[length=2mm,width=1.3mm]}] '+point(p)+' -- '+point(q)+';')
    for z, original in ((LENGTH/2, 1), (-LENGTH/2, -1)):
        direction = original * sign
        p, q = xyz(center-.20*direction, z), xyz(center+.20*direction, z)
        tex.append(r'\draw[navy,line width=.7pt,-{Latex[length=2mm,width=1.3mm]}] '+point(p)+' -- '+point(q)+';')
tex += [r'\node[navy,anchor=west] at (.32,-.48) {A};',
        r'\node[navy,anchor=east] at (-.32,.48) {B};',
        r'\end{tikzpicture}',
        r'\par\small 三维有限带宽闭合回路',
        r'\end{minipage}\hfill',
        r'\begin{minipage}[t]{.49\linewidth}\centering\vspace{0pt}',
        r'\begin{tikzpicture}[x=2.8cm,y=4.8cm,font=\small,>=Latex]',
        r'\path[use as bounding box] (-.65,-.65) rectangle (1.90,.65);']

# A cylinder unwrapped into (b phi,z). Two separate loops, rather than two
# complete end rings: the centers are pi apart in angle and b*pi in arc length.
for group, center in enumerate((0., B*math.pi)):
    color = colors[group]
    lo, hi = center-B*ALPHA/2, center+B*ALPHA/2
    tex.append(f'\\path[fill={color}!82,even odd rule] '
               f'({lo-WIDTH/2:.6f},{-LENGTH/2-WIDTH/2:.6f}) rectangle '
               f'({hi+WIDTH/2:.6f},{LENGTH/2+WIDTH/2:.6f}) '
               f'({lo+WIDTH/2:.6f},{-LENGTH/2+WIDTH/2:.6f}) rectangle '
               f'({hi-WIDTH/2:.6f},{LENGTH/2-WIDTH/2:.6f});')
    drive = -1 if group == 0 else 1
    for x, original in ((lo, 1), (hi, -1)):
        direction = original*drive
        tex.append(f'\\draw[navy,line width=.7pt,-{{Latex[length=2mm]}}] '
                   f'({x:.6f},{-.12*direction:.6f}) -- ({x:.6f},{.12*direction:.6f});')
    for z, original in ((LENGTH/2, 1), (-LENGTH/2, -1)):
        direction = original*drive
        tex.append(f'\\draw[navy,line width=.7pt,-{{Latex[length=2mm]}}] '
                   f'({center-.11*direction:.6f},{z:.6f}) -- ({center+.11*direction:.6f},{z:.6f});')
    tex.append(f'\\draw[navy,thin,<->] ({lo:.6f},.44)--({hi:.6f},.44) node[midway,above] {{$b\\alpha$}};')
    name, current = ('A', 'I') if group == 0 else ('B', '-I')
    tex.append(f'\\node[navy] at ({center:.6f},-.49) {{{name}: ${current}$}};')

tex += [f'\\draw[navy,thin,<->] (0,.60)--({B*math.pi:.6f},.60) node[midway,fill=white] {{$b\\pi$}};',
        r'\draw[navy,thin,<->] (-.60,-.35)--(-.60,.35) node[midway,left] {$L$};',
        r'\end{tikzpicture}',
        r'\par\small 圆柱展开面：横向为 $b\phi$，竖向为 $z$',
        r'\end{minipage}',
        r'\par\smallskip\small A、B 的中心角相差 $\pi$，相同鞍形参考方向下 $I_A=I$、$I_B=-I$。',
        r'\par\small $b=0.44$ m，$L=0.70$ m，$L_{\text{宽}}=0.04$ m，$\alpha=120^\circ=2\pi/3$ rad；箭头为零整体相位、正方向时的瞬时参考电流。']
GENERATED['double_saddle'] = '\n'.join(tex)+'\n'

labels = ['单带', '多带阵列', '梳状阵列', '全匝圆环', '双臂螺旋', '单鞍',
          '双鞍形', 'Nagoya III', '直带与回流', '部分匝', '半 Nagoya', '半匝阵列',
          '轴向多环', '平面螺旋', '分段多环', '鸟笼电流', '蛇形电流']
table = [r'% Generated from the current seventeen-model numerical audit.',
         r'\begin{center}\small',
         r'\begin{tabular}{@{}r l r r r@{}}\toprule',
         r'编号 & 天线 & $\max|\Delta K|/I_0$ & $\eta_{\rm FEM}$ & $\eta_P$\\\midrule']


def scientific(x):
    mantissa, exponent = f'{float(x):.2e}'.split('e')
    return f'${mantissa}\\times10^{{{int(exponent)}}}$'


for row, name in zip(AUDIT, labels):
    linear = max(float(row['vacuum_backward_error']), float(row['plasma_backward_error']))
    table.append(f"{row['number']} & {name} & {scientific(row['source_error_over_I0'])} & "
                 f"{scientific(linear)} & {scientific(row['power_balance_error'])}\\\\")
table += [r'\bottomrule\end{tabular}', r'\end{center}']
GENERATED['antenna_checks'] = '\n'.join(table)+'\n'

# The new drawings and curves are exported by the same C source integrator.
NEW = list(csv.DictReader((DATA/'prescribed_current_geometry.csv').open()))
SPECTRA = list(csv.DictReader((DATA/'prescribed_current_spectra.csv').open()))
for kind in range(13,18):
    rows=[r for r in NEW if int(r['type'])==kind]
    def projection(row,end):
        x,y,z=(float(row[end+k]) for k in 'xyz')
        if kind in (14,15): return (x,y,0.)
        if kind==17:
            return (.44*math.atan2(y,x),z,0.)
        return project(x,y,z)
    pts=[projection(r,e) for r in rows for e in ('a','b')]
    xmin,xmax=min(v[0] for v in pts),max(v[0] for v in pts)
    ymin,ymax=min(v[1] for v in pts),max(v[1] for v in pts)
    scale=min(5.7/max(xmax-xmin,.01),4.9/max(ymax-ymin,.01))
    cx,cy=(xmin+xmax)/2,(ymin+ymax)/2
    def pos(p): return f'({(p[0]-cx)*scale:.5f},{(p[1]-cy)*scale:.5f})'
    lines=[r'% Current geometry exported by C; arrows show instantaneous real current at t=0.',
           r'\begin{minipage}[t]{.48\linewidth}\centering\vspace{0pt}',
           r'\begin{tikzpicture}[x=1cm,y=1cm,>=Latex,font=\small]',
           r'\path[use as bounding box] (-3.15,-2.85) rectangle (3.15,2.85);']
    # Arrow selection is based on the strongest visible segments per branch,
    # so vanishing cos(q phi) legs are not misleadingly marked as carrying I0.
    candidates={}
    for r in sorted(rows,key=lambda r: sum(projection(r,e)[2] for e in ('a','b'))):
        p,q=projection(r,'a'),projection(r,'b')
        amp=float(r['amplitude']); phase=float(r['phase'])+float(r['phase_advance'])/2
        real=amp*math.cos(phase)
        color='copper' if real>=0 else 'teal'
        if amp<1e-10: color='gray!30'
        lines.append(f'\\draw[{color},line width=1.15pt] {pos(p)}--{pos(q)};')
        key=int(r['group'])
        candidates.setdefault(key,[]).append((r,p,q,real))
    for group,parts in candidates.items():
        # Place at most three arrowheads along each uninterrupted path/group.
        for index in sorted(set((len(parts)//5,len(parts)//2,4*len(parts)//5))):
            r,p,q,real=parts[index]
            if abs(real)<.12: continue
            delta=(q[0]-p[0],q[1]-p[1]);norm=math.hypot(*delta)
            if norm<1e-10: continue
            mid=((p[0]+q[0])/2,(p[1]+q[1])/2,0.)
            unit=(delta[0]/norm,delta[1]/norm); h=.07/scale*(1 if real>0 else -1)
            a=(mid[0]-h*unit[0],mid[1]-h*unit[1],0.)
            b=(mid[0]+h*unit[0],mid[1]+h*unit[1],0.)
            lines.append(r'\draw[navy,line width=.65pt,-{Latex[length=1.8mm]}] '+pos(a)+'--'+pos(b)+';')
    desc={13:'轴向双环：两环参考电流反相',14:'平面螺旋：内外端点保留',15:'四个半径：内环两段，外环各四段',16:'鸟笼：腿与端环电流满足节点守恒',17:'蛇形圆柱展开面：横向为 $b\phi$，竖向为 $z$'}[kind]
    lines += [r'\end{tikzpicture}',r'\par\small '+desc,
              r'\end{minipage}\hfill',
              r'\begin{minipage}[t]{.50\linewidth}\centering\vspace{0pt}',
              r'\resizebox{\linewidth}{!}{%',
              r'\begin{tikzpicture}',
              r'\begin{axis}[width=\linewidth,height=6.1cm,grid=major,',
              r'xlabel={$k_z$ (rad/m)},ylabel={$|K^{\rm eff}_\nu|$ (A)},',
              r'tick label style={font=\small},label style={font=\small},',
              r'legend style={font=\small,at={(.5,1.02)},anchor=south,legend columns=3},',
              r'xmin=-40,xmax=40,enlarge y limits=.08]']
    data=[r for r in SPECTRA if int(r['type'])==kind]
    for component,color,label in [('Kr','violet','r'),('Kphi','copper',r'\phi'),('Kz','teal','z')]:
        coords=' '.join(f"({r['kz']},{float(r[component]):.12g})" for r in data)
        lines.append(f'\\addplot[{color},thick,no marks] coordinates '+'{'+coords+'};')
        lines.append(r'\addlegendentry{$'+label+r'$}')
    lines += [r'\end{axis}\end{tikzpicture}}',
              r'\par\small 展示汇总谱，$m='+data[0]['m']+r'$；场加载保留各源半径。',
              r'\end{minipage}']
    GENERATED[f'prescribed_current_{kind}'] = '\n'.join(lines)+'\n'

# Keep all LaTeX in MAIN. Validate every boundary before replacing the file;
# the human-written chapters outside these seven generated regions stay intact.
original = MAIN.read_text(encoding='utf-8')
updated = original
for name, content in GENERATED.items():
    begin = f'% BEGIN GENERATED: {name}\n'
    end = f'% END GENERATED: {name}\n'
    if updated.count(begin) != 1 or updated.count(end) != 1:
        raise ValueError(f'Missing or duplicate generated region: {name}')
    first = updated.index(begin) + len(begin)
    last = updated.index(end)
    if last < first:
        raise ValueError(f'Reversed generated region: {name}')
    updated = updated[:first] + content + updated[last:]
if updated != original:
    temporary = MAIN.with_suffix('.tex.tmp')
    temporary.write_text(updated, encoding='utf-8')
    temporary.replace(MAIN)
print('Refreshed seven embedded figure/table regions in the single LaTeX source.')
