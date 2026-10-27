#!/usr/bin/env python3
"""Draw six literature-supported candidate antenna configurations.

These are geometric/topological illustrations, not results from the current
Maxwell solver. Requires matplotlib and numpy; the production PDF build remains
independent of these optional drawing dependencies.
"""
from pathlib import Path
import math
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager
from matplotlib.patches import Circle, Rectangle, FancyArrowPatch, Polygon
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from antenna_schematic_rf import draw_meander, draw_folded_waveguide

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / '天线电流谱文档' / 'figures'
FONT_PATH = '/System/Library/Fonts/STHeiti Light.ttc'
font_manager.fontManager.addfont(FONT_PATH)
plt.rcParams.update({'font.family': font_manager.FontProperties(fname=FONT_PATH).get_name(),
                     'font.size': 18, 'axes.unicode_minus': False, 'svg.fonttype': 'path'})
INK, MUTED = '#20384b', '#607184'
COPPER, TEAL, METAL = '#bd6a29', '#29878c', '#697785'
BLUE, GREEN = '#2b72ae', '#21765e'


def projection(x, y, z):
    a, e = math.radians(33), math.radians(22)
    return x*math.cos(a)-y*math.sin(a), z*math.cos(e)-(x*math.sin(a)+y*math.cos(a))*math.sin(e)


def cyl(phi, z, r=.85):
    return projection(r*math.cos(phi), r*math.sin(phi), z)


def curve(ax, coords, color=COPPER, lw=3, ls='-', zorder=3, alpha=1):
    points=np.asarray(coords)
    ax.plot(points[:,0],points[:,1],color=color,lw=lw,ls=ls,
            solid_capstyle='round',zorder=zorder,alpha=alpha)


def label(ax, text, point, at, color=INK, ha='left', size=18):
    ax.annotate(text,xy=point,xytext=at,color=color,ha=ha,va='center',fontsize=size,
        arrowprops={'arrowstyle':'-','lw':1.05,'color':MUTED},linespacing=1.4,zorder=12)


def base(ax, limits=(-1.6,1.8,-1.3,1.3)):
    ax.set_aspect('equal');ax.axis('off')
    ax.set_xlim(*limits[:2]);ax.set_ylim(*limits[2:])


def tube(ax, radius=.65, lo=-.9, hi=.9):
    theta=np.linspace(0,2*math.pi,150)
    for z in (lo,hi):
        curve(ax,[cyl(t,z,radius) for t in theta],color=BLUE,lw=1,ls='--',zorder=0,alpha=.40)
    for phi in (math.radians(-33),math.radians(147)):
        curve(ax,[cyl(phi,lo,radius),cyl(phi,hi,radius)],color=BLUE,lw=1,ls='--',zorder=0,alpha=.40)


def current_port(ax, points, at, text, color):
    x,y=at;w,h=.42,.25
    ax.add_patch(Rectangle((x,y-h/2),w,h,fc='white',ec=MUTED,lw=1.15,zorder=8))
    for source,target in zip(points,[(x,y-.07),(x,y+.07)]):
        curve(ax,[source,target],color=color,lw=1.8,zorder=7)
    ax.text(x+w/2,y,text,ha='center',va='center',fontsize=15,color=INK,zorder=9)


def draw_axial_loops(ax):
    base(ax);tube(ax)
    for z,color,port,name in [(.50,COPPER,(1.05,.54),'RF 1'),(-.50,TEAL,(1.05,-.45),'RF 2')]:
        gap=.15
        curve(ax,[cyl(t,z) for t in np.linspace(gap,2*math.pi-gap,250)],color=color,lw=4)
        current_port(ax,[cyl(gap,z),cyl(-gap,z)],port,name,color)
    label(ax,'圆环 1',cyl(math.pi,.50),(-1.48,.80),size=19)
    label(ax,'圆环 2',cyl(math.pi,-.50),(-1.48,-.48),size=19)
    ax.add_patch(FancyArrowPatch((-1.25,-.83),(-1.25,.83),arrowstyle='->',
                                 mutation_scale=16,lw=1.3,color=MUTED))
    ax.text(-1.35,.99,'z',color=INK,fontsize=20)
    ax.text(.10,-1.17,'轴向间距、两路电流相位可调',ha='center',color=MUTED,fontsize=18)


def draw_planar_spiral(ax):
    base(ax,(-1.36,1.75,-1.28,1.28))
    ax.add_patch(Circle((0,0),1.02,fc='#edf4f8',ec='#9ab4c7',lw=1.1,zorder=0))
    # A generic four-turn flat spiral; both ends connect to the RF source.
    t=np.linspace(0,8*math.pi,1300);r=.13+.75*t/(8*math.pi)
    points=np.column_stack([r*np.cos(t),r*np.sin(t)])
    curve(ax,points,color=COPPER,lw=3.5,zorder=3)
    # The center lead is behind the plane, so crossings are not junctions.
    curve(ax,[(.13,0),(.13,-1.13),(1.27,-1.13),(1.27,-.46)],
          color=TEAL,lw=1.8,ls='--',zorder=4)
    curve(ax,[(.88,0),(1.18,0),(1.18,-.28),(1.27,-.28)],color=COPPER,lw=2.1,zorder=4)
    ax.add_patch(Rectangle((1.27,-.49),.40,.24,fc='white',ec=MUTED,lw=1.1,zorder=6))
    ax.text(1.47,-.37,'RF',ha='center',va='center',fontsize=16,color=INK,zorder=7)
    label(ax,'端面／介质窗',(-.72,.72),(-1.28,1.14),size=18)
    label(ax,'连续平面螺旋',(.40,.56),(.50,1.12),size=18)
    label(ax,'背面馈线',(.13,-.88),(.38,-.82),color=TEAL,size=18)


def draw_segmented_loops(ax):
    base(ax,(-1.35,1.8,-1.30,1.30))
    # Actual source example: innermost ring has TWO half-ring pieces; the
    # other three rings have FOUR quarter-ring pieces (Shinohara 2014 Fig.3).
    radii=(.28,.48,.68,.88)
    for radius,nparts in zip(radii,(2,4,4,4)):
        gap=.030/radius
        for j in range(nparts):
            start=2*math.pi*j/nparts+gap
            end=2*math.pi*(j+1)/nparts-gap
            ph=np.linspace(start,end,90)
            curve(ax,[(radius*math.cos(t),radius*math.sin(t)) for t in ph],lw=5.0)
            ax.scatter([radius*math.cos(start),radius*math.cos(end)],
                       [radius*math.sin(start),radius*math.sin(end)],s=10,color=COPPER,zorder=4)
    # Selected rear leads are illustrative only; no particular m/feed topology
    # is prescribed by this diagram.
    box=(1.15,-.65,.48,1.32)
    ax.add_patch(Rectangle(box[:2],box[2],box[3],fc='#f2f6f8',ec=MUTED,lw=1,zorder=0))
    ax.text(1.39,.01,'背面\n可重接\n端子',ha='center',va='center',fontsize=17,color=INK,zorder=5)
    for radius,y in zip(radii,(-.52,-.22,.22,.52)):
        phi=-.030/radius
        curve(ax,[(radius*math.cos(phi),radius*math.sin(phi)),(1.05,y),(1.15,y)],
              color=MUTED,lw=1.1,ls='--',zorder=1)
    label(ax,'绝缘间隙',(.67,0),(.55,1.13),size=18)
    label(ax,'四个不同半径',(-.78,-.40),(-1.25,-1.0),size=18)
    ax.text(.12,-1.22,'内环 2 段；外三环各 4 段',ha='center',color=MUTED,fontsize=18)


def draw_birdcage(ax):
    base(ax,(-1.5,1.7,-1.36,1.36));tube(ax,.63,-.93,.93)
    n=8;r=.84;lo,hi=-.76,.76
    elements=[]
    # Each leg is inductive. Each END-RING segment contains a series capacitor.
    for j in range(n):
        ph=2*math.pi*j/n
        x,y=r*math.cos(ph),r*math.sin(ph)
        depth=x*math.sin(math.radians(33))+y*math.cos(math.radians(33))
        elements.append((depth,[projection(x,y,lo),projection(x,y,hi)],COPPER,2.8))
    for z in (lo,hi):
        for j in range(n):
            mid=2*math.pi*(j+.5)/n
            for a,b in [(2*math.pi*j/n,mid-.045),(mid+.045,2*math.pi*(j+1)/n)]:
                ph=np.linspace(a,b,25)
                points=[cyl(t,z,r) for t in ph]
                depth=math.sin(mid+math.radians(33))
                elements.append((depth,points,COPPER,2.4))
            for ph in (mid-.045,mid+.045):
                points=[cyl(ph,z,r-.065),cyl(ph,z,r+.065)]
                elements.append((math.sin(mid+math.radians(33))+.01,points,INK,1.8))
    for _,points,color,width in sorted(elements,key=lambda x:x[0]):
        curve(ax,points,color=color,lw=width,zorder=3)
    label(ax,'轴向导体',cyl(.1,0,r),(1.00,-.10),size=19)
    label(ax,'环段电容 C',cyl(2*math.pi*(.5)/n,hi,r),(.68,1.14),size=19)
    ax.text(.05,-1.22,'高通拓扑示意；馈电与接缝从略',ha='center',color=MUTED,fontsize=18)


DRAWERS=[draw_axial_loops,draw_planar_spiral,draw_segmented_loops,draw_birdcage,
         draw_meander,draw_folded_waveguide]
TITLES=['A  轴向双环／多环','B  端面平面螺旋','C  端面多半径分段环',
        'D  鸟笼谐振天线','E  蛇形微带慢波天线','F  折叠波导孔径天线']
SOURCES=[
    'https://www.seas.ucla.edu/~ffchen/Publs/chen235R.pdf, Fig.5',
    'https://web.tuat.ac.jp/~sinohara/pub_data/97PPCF1479.pdf, Fig.1',
    'https://web.tuat.ac.jp/~sinohara/pub_data/14RSI093509.pdf, Fig.2-3',
    'https://arxiv.org/html/2404.05441v1, Fig.3',
    'https://www.jspf.or.jp/PFR/PDF/pfr2006_01-009.pdf, Fig.1-2',
    'https://www.jspf.or.jp/JPFRS/PDF/Vol5/jpfrs2002_05-310.pdf, Fig.1',
]


def make_figure(indices, stem, overview=False):
    rows=len(indices)//2
    fig,axes=plt.subplots(rows,2,figsize=(14,5.5*rows+.55),facecolor='white',squeeze=False)
    fig.subplots_adjust(left=.035,right=.97,bottom=.055 if rows==1 else .035,
                        top=.88 if rows==1 else .945,wspace=.08,hspace=.16)
    if overview:
        fig.suptitle('补充天线位型 · 文献结构示意',fontsize=25,color=INK,y=.985)
        fig.text(.5,.955,'A–F 为候选图号；接线简化，非计算结果',ha='center',color=MUTED,fontsize=17)
    for ax,i in zip(axes.flat,indices):
        DRAWERS[i](ax)
        ax.set_title(TITLES[i],fontsize=22,color=INK,pad=15,loc='left')
    desc='Original schematic drawings; not solver results. References: '+'; '.join(SOURCES[i] for i in indices)
    for ext in ('png','svg'):
        p=OUT/f'{stem}.{ext}'
        metadata={'Description':desc} if ext=='svg' else {'Description':desc}
        fig.savefig(p,dpi=160,facecolor='white',metadata=metadata)
        print(p)
    plt.close(fig)


if __name__=='__main__':
    OUT.mkdir(parents=True,exist_ok=True)
    make_figure(list(range(6)),'补充天线位型_六图总览',overview=True)
    for ids,name in [([0,1],'补充位型_AB_双环与平面螺旋'),
                     ([2,3],'补充位型_CD_分段环与鸟笼'),
                     ([4,5],'补充位型_EF_慢波与波导')]:
        make_figure(ids,name)
