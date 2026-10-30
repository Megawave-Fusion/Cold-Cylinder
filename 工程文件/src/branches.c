/* 全分支局部分析：峰值相量 exp(i m phi+i kz z-i omega t)。
 * 实际复波数来自所选碰撞模型；实射线采用无碰撞参考色散，仅接受弱损耗、
 * 非退化、径向传播及 WKB 合格的部分。不把局部根当成全局模态功率。 */
#include "rf_platform.h"
#include "branches.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef CMPLX
/* 部分 Windows CRT 的 complex.h 缺少 C11 构造宏。复数按 C11 具有两个
 * double 的表示，直接复制实/虚部，避免用 re+I*im 使 NaN/Inf 污染另一分量。 */
static double complex complex_parts(double real, double imaginary)
{
    double parts[2] = { real, imaginary };
    double complex value;
    _Static_assert(sizeof(value) == sizeof(parts), "C11 complex representation required");
    memcpy(&value, parts, sizeof(value));
    return value;
}
#define CMPLX(real, imaginary) complex_parts((double)(real), (double)(imaginary))
#endif

/* 统一诊断错误出口；允许调用方不接收错误文本。 */
static int fail(char *e, size_t n, const char *msg)
{
    if (e && n) snprintf(e, n, "%s", msg);
    return 0;
}
/* 实部、虚部都有限才视作可表示的局部根，避免将材料极点填成零。 */
static int finite_complex(double complex z)
{
    return isfinite(creal(z)) && isfinite(cimag(z));
}
/* 复矢量的 Hermitian 范数，用 hypot 避免分量平方造成不必要的溢出。 */
static double norm3(const double complex v[3])
{
    return hypot(hypot(cabs(v[0]), cabs(v[1])), cabs(v[2]));
}
/* 双线性复叉乘，不隐含共轭；用于波矩阵零空间及局部 Maxwell 几何关系。 */
static void cross(const double complex a[3], const double complex b[3], double complex v[3])
{
    for (int j=0;j<3;j++) v[j]=a[(j+1)%3]*b[(j+2)%3]-a[(j+2)%3]*b[(j+1)%3];
}
/* 以 1 为参考尺度的归一化复根距离；只用于无量纲 Q 的匹配和分支分离诊断。 */
static double distance(double complex a, double complex b)
{
    return cabs(a-b)/(1.+cabs(a)+cabs(b));
}
/* 稳定二次求根：先算模较大的分子，再由根积恢复小根；S=0 保留有限线性根。 */
static int roots(const Project *p, const Plasma *c, double r, RfPlasmaMaterial *mat,
    double complex q[2], char *error, size_t n)
{
    q[0]=q[1]=CMPLX(NAN,NAN);
    if (!rf_plasma_material(p,c,r,mat,error,n)) return 0;
    double k0=2.*RF_PI*p->frequency_mhz*1e6/RF_C0;
    double nz2=(p->kz/k0)*(p->kz/k0);
    if (mat->S==1. && mat->P==1. && mat->D==0.) {
        q[0]=q[1]=1.-nz2; return 1; /* exact vacuum double root */
    }
    double complex b=(mat->S+mat->P)*(mat->S-nz2)-mat->D*mat->D;
    double complex cc=mat->P*((mat->S-nz2)*(mat->S-nz2)-mat->D*mat->D);
    double scale=fmax(cabs(mat->S),fmax(cabs(b),cabs(cc)));
    if (!isfinite(scale) || scale==0.) return 1;
    double complex a=mat->S/scale, bb=b/scale, v=cc/scale;
    if (cabs(a)==0.) {
        if (cabs(bb)>0.) q[0]=v/bb;
        return 1;
    }
    double complex d=csqrt(bb*bb-4.*a*v);
    double complex t=cabs(bb+d)>=cabs(bb-d) ? .5*(bb+d) : .5*(bb-d);
    if (cabs(t)==0.) q[0]=q[1]=0.;
    else { q[0]=t/a; q[1]=v/t; }
    if (!finite_complex(q[0])) q[0]=CMPLX(NAN,NAN);
    if (!finite_complex(q[1])) q[1]=CMPLX(NAN,NAN);
    return 1;
}
/* 完整冷色散多项式 F(Q,N)；omega 改变时按固定物理 kr、kphi、kz 重算 Q、N。 */
static double complex dispersion(const Project *p, const RfPlasmaMaterial *a,
    double omega, double kr, double kphi)
{
    double k0=omega/RF_C0, Q=(kr*kr+kphi*kphi)/(k0*k0), N=(p->kz/k0)*(p->kz/k0);
    return a->S*Q*Q-((a->S+a->P)*(a->S-N)-a->D*a->D)*Q+
        a->P*((a->S-N)*(a->S-N)-a->D*a->D);
}
/* 由缩放后的波矩阵零空间取单位极化，并固定最大分量为正实相位。
 * 真空重根另造两个独立横向基；结果无量纲，不携带天线耦合幅度。 */
static int polarization(const Project *p, const RfPlasmaMaterial *a, double r,
    double complex kr, int branch, int vacuum, RfBranchPoint *v)
{
    double k0=2.*RF_PI*p->frequency_mhz*1e6/RF_C0;
    double complex k[3]={kr/k0,p->selected_m/(r*k0),p->kz/k0};
    double complex eps[3][3]={{a->S,-I*a->D,0},{I*a->D,a->S,0},{0,0,a->P}};
    double complex M[3][3], e[3]={0}, trial[3];
    double complex k2=k[0]*k[0]+k[1]*k[1]+k[2]*k[2];
    double scale=0., best=0.;
    for (int j=0;j<3;j++) for(int l=0;l<3;l++) {
        M[j][l]=eps[j][l]+k[j]*k[l]-(j==l?k2:0.);
        scale=fmax(scale,cabs(M[j][l]));
    }
    if (!(scale>0.) || !isfinite(scale)) return 0;
    for (int j=0;j<3;j++) for(int l=0;l<3;l++) M[j][l]/=scale;
    if (vacuum) {
        /* 重根的两个独立横向极化；无唯一 H/TG 标号。 */
        double complex axis[3]={0};
        int least=0;
        for(int j=1;j<3;j++) if(cabs(k[j])<cabs(k[least])) least=j;
        axis[least]=1.; cross(k,axis,e);
        if(branch) {
            /* 倏逝真空根的 k 为复数，k×e 可能仍与 e 共线。
             * k×conj(e) 同时满足 k·e2=0 和 e†e2=0；实 k 时退化为原实基。 */
            double complex conjugate[3];
            for (int j=0;j<3;j++) conjugate[j]=conj(e[j]);
            cross(k,conjugate,trial); memcpy(e,trial,sizeof(e));
        }
        best=norm3(e);
    } else {
        for(int j=0;j<3;j++) {
            cross(M[j],M[(j+1)%3],trial);
            double len=norm3(trial);
            if(len>best) {best=len;memcpy(e,trial,sizeof(e));}
        }
    }
    if (!(best>1e-12)) return 0;
    for(int j=0;j<3;j++) e[j]/=best;
    int pivot=0;
    for(int j=1;j<3;j++) if(cabs(e[j])>cabs(e[pivot])) pivot=j;
    double complex phase=conj(e[pivot])/cabs(e[pivot]);
    for(int j=0;j<3;j++) v->electric[j]=e[j]*phase;
    double complex residual[3]={0};
    double mn=0.;
    for(int j=0;j<3;j++) for(int l=0;l<3;l++) {
        residual[j]+=M[j][l]*v->electric[l]; mn+=pow(cabs(M[j][l]),2);
    }
    v->polarization_error=norm3(residual)/sqrt(mn);
    /* 与实相位波矢平行的电场份额，不是 E_z 份额。 */
    double kn=hypot(hypot(creal(kr),p->selected_m/r),p->kz);
    if(kn>0.) v->longitudinal_fraction=pow(cabs(creal(kr)*v->electric[0]+
        p->selected_m/r*v->electric[1]+p->kz*v->electric[2])/kn,2);
    return isfinite(v->polarization_error) && v->polarization_error<1e-6;
}
/* 按 vg=-F_k/F_omega 求三分量群速度（m/s）。频率差分同时包含材料频散
 * 和 Q、N 的链式项；两档步长须一致且不能跨过回旋极点。 */
static int group_velocity(const Project *p, const Plasma *cold, double r, double kr,
    const RfPlasmaMaterial *mat, int vacuum, double vg[3])
{
    double omega=2.*RF_PI*p->frequency_mhz*1e6, k0=omega/RF_C0;
    double kp=p->selected_m/r;
    if(vacuum) {
        vg[0]=RF_C0*RF_C0*kr/omega;
        vg[1]=RF_C0*RF_C0*kp/omega;
        vg[2]=RF_C0*RF_C0*p->kz/omega;
        return 1;
    }
    double Q=(kr*kr+kp*kp)/(k0*k0), N=(p->kz/k0)*(p->kz/k0);
    double S=creal(mat->S), D=creal(mat->D), P=creal(mat->P);
    double fq=2*S*Q-((S+P)*(S-N)-D*D), fn=(S+P)*Q-2*P*(S-N);
    double step=2e-5;
    /* Keep both centred differences on the same side of all cold material poles. */
    double gyro[2]={RF_E_CHARGE*fabs(cold->magnetic_field)/RF_ME,
        cold->ion_charge*RF_E_CHARGE*fabs(cold->magnetic_field)/(cold->ion_mass_u*RF_AMU)};
    for(int j=0;j<2;j++) step=fmin(step,.1*fabs(omega-gyro[j])/omega);
    if(step<1e-10) return 0;
    double deriv[2];
    for(int level=0;level<2;level++) {
        double h=step/(level+1);
        Project lo=*p,hi=*p;
        lo.frequency_mhz*=1-h;hi.frequency_mhz*=1+h;
        RfPlasmaMaterial ml,mh;char err[160];
        if(!rf_plasma_material(&lo,cold,r,&ml,err,sizeof(err)) ||
           !rf_plasma_material(&hi,cold,r,&mh,err,sizeof(err))) return 0;
        double fl=creal(dispersion(p,&ml,omega*(1-h),kr,kp));
        double fh=creal(dispersion(p,&mh,omega*(1+h),kr,kp));
        if(!isfinite(fl)||!isfinite(fh)||fabs(fh-fl)<1e-12*fmax(1.,fabs(fl)+fabs(fh))) return 0;
        deriv[level]=(fh-fl)/(2*h*omega);
    }
    double fw=deriv[1];
    if(!isfinite(fw)||fabs(deriv[0]-fw)>1e-3*fabs(fw)) return 0;
    vg[0]=-2*kr*fq/(k0*k0*fw);vg[1]=-2*kp*fq/(k0*k0*fw);
    vg[2]=-2*p->kz*fn/(k0*k0*fw);
    return isfinite(vg[0]) && isfinite(vg[1]) && isfinite(vg[2]);
}
/* 无效量统一设 NaN、有效性标志置零；缺失分支不会伪装成零波数或零场。 */
static void empty_point(RfBranchPoint *v)
{
    memset(v,0,sizeof(*v));
    v->q=v->kr=v->q_reference=v->kr_reference=CMPLX(NAN,NAN);
    for(int j=0;j<3;j++) {v->electric[j]=CMPLX(NAN,NAN);v->vg[j]=NAN;}
    v->longitudinal_fraction=v->polarization_error=v->damping_ratio=v->reference_shift=NAN;
    v->wkb=v->root_gap=NAN;
    v->ray_z=v->ray_phi=v->ray_time=v->ray_optical_depth=NAN;
    v->wave_match_error=NAN;
    snprintf(v->wave_reason,sizeof(v->wave_reason),"local root unavailable");
    v->state=RF_BRANCH_SINGULAR;
}
/* 将离散传播状态转成稳定 CSV 标识；状态与是否已发射射线分开保存。 */
const char *rf_branch_state_name(int state)
{
    static const char *names[]={"propagating","evanescent","strong_damping",
        "degenerate","singular","material_error"};
    return state>=0 && state<6?names[state]:"invalid";
}
/* 波型是局部物理识别标签，不是 A/B 连续编号，也不决定射线发射资格。 */
const char *rf_branch_wave_name(int type)
{
    static const char *names[]={"unidentified","vacuum","helicon_like","TG_like","mixed",
        "icrf_fast_like","icrf_slow_like"};
    return type>=0 && type<7?names[type]:"unidentified";
}
/* Cold ICRF naming by analytic continuation P -> P/t, t:0+ -> 1, at fixed
 * S,D,N=n_parallel^2. This does not alter the physical roots or trace rays.
 * t*S*Q^2-[P*(S-N)+t*(S*(S-N)-D^2)]*Q+P*((S-N)^2-D^2)=0.
 * The finite t->0 root is the fast wave; the divergent root is the slow wave.
 * Check the whole quadratic discriminant on [0,1], rather than sorting |Q|.
 * Evanescent continuation is a wave identity, not permission to trace a ray.
 * Slow here is the cold ICRF branch, not a finite-temperature ion-sound mode. */
static void identify_icrf(const Project *p,const RfPlasmaMaterial *reference,RfBranchPoint *v)
{
    if(v->state==RF_BRANCH_DEGENERATE ||
       fabs(cimag(v->q_reference))>1e-7*fmax(1.,cabs(v->q_reference))) {
        v->wave_type=RF_WAVE_MIXED;
        snprintf(v->wave_reason,sizeof(v->wave_reason),"ICRF reference roots merge or become complex");
        return;
    }
    if(v->reference_shift>.1 || v->state==RF_BRANCH_DAMPED || !v->polarization_valid) {
        snprintf(v->wave_reason,sizeof(v->wave_reason),"ICRF loss/reference shift too large or polarization unresolved");
        return;
    }
    double k0=2*RF_PI*p->frequency_mhz*1e6/RF_C0;
    double N=pow(p->kz/k0,2), S=creal(reference->S),D=creal(reference->D),P=creal(reference->P);
    if(!(P<0.)) {
        snprintf(v->wave_reason,sizeof(v->wave_reason),"ICRF P-limit naming requires negative parallel dielectric P");
        return;
    }
    /* Q scales with the dielectric elements; nondimensionalize before
       forming cubic coefficients to avoid unnecessary overflow. */
    double scale=fmax(1.,fmax(N,fmax(fabs(P),fmax(fabs(S),fabs(D)))));
    S/=scale;D/=scale;P/=scale;N/=scale;
    double X=S-N,H=S*X-D*D,G=(X-D)*(X+D),b0=P*X,C=P*G;
    if(fabs(X)<=1e-8*fmax(DBL_MIN,fmax(fabs(S),N))) {
        v->wave_type=RF_WAVE_MIXED;
        snprintf(v->wave_reason,sizeof(v->wave_reason),"ICRF S-n_parallel^2 too small for separate P-limit names");
        return;
    }
    /* Delta(t)=(b0+t*H)^2-4*t*S*C. Its only possible interior
       minimum is tested explicitly, including relative root separation. */
    double times[3]={0.,1.,-1.};
    if(H!=0.)times[2]=(2*S*C-b0*H)/(H*H);
    for(int j=0;j<3;j++)if(times[j]>=0. && times[j]<=1.) {
        double t=times[j],b=b0+t*H,a=t*S,disc=b*b-4*a*C;
        double norm=b*b+4*fabs(a*C);
        if(!isfinite(disc) || disc<=1e-10*fmax(DBL_MIN,norm)) {
            v->wave_type=RF_WAVE_MIXED;
            snprintf(v->wave_reason,sizeof(v->wave_reason),"ICRF P-continuation crosses complex or near-merged roots");
            return;
        }
    }
    double b=b0+H,d=copysign(sqrt(b*b-4*S*C),b0),qf,qs;
    if(S==0.) {qf=C/b;qs=NAN;}
    else {
        /* Choose cancellation-free expressions without changing which
           sign of sqrt(Delta) was selected at the finite P-limit root. */
        qf=fabs(b+d)>=fabs(b-d) ? 2*C/(b+d) : (b-d)/(2*S);
        qs=fabs(b-d)>=fabs(b+d) ? 2*C/(b-d) : (b+d)/(2*S);
    }
    qf*=scale;qs*=scale;
    double fast=isfinite(qf)?distance(v->q_reference,qf):INFINITY;
    double slow=isfinite(qs)?distance(v->q_reference,qs):INFINITY;
    v->wave_match_error=fmin(fast,slow);
    if(v->wave_match_error>1e-6 || fabs(fast-slow)<1e-6) {
        snprintf(v->wave_reason,sizeof(v->wave_reason),"ICRF root does not uniquely match P-continuation");
        return;
    }
    v->wave_type=fast<slow?RF_WAVE_ICRF_FAST:RF_WAVE_ICRF_SLOW;
    snprintf(v->wave_reason,sizeof(v->wave_reason),"ICRF %s P-limit continuation; propagation checked separately",
        fast<slow?"fast":"slow");
}
/* Identify only well separated, weak-loss roots in the dense helicon ordering.
 * Electron-MHD reference (Chen & Arnush, Phys. Plasmas 4, 3411, 1997):
 * delta*beta^2 - |kz|*beta + kw^2 = 0, delta=omega/|Omega_e|,
 * kw^2=delta*omega_pe^2/c^2. beta is TOTAL wavenumber, not kr.
 * The lower root continues to the inertia-free helicon; the upper is TG-like.
 * Thresholds below are conservative identification heuristics, not new cutoffs
 * in the full two-fluid solver. Never infer wave identity from A/B or F_L alone. */
static void identify_wave(const Project *p,const Plasma *c,const RfPlasmaMaterial *mat,
    const RfPlasmaMaterial *reference,int vacuum,RfBranchPoint *v)
{
    if(vacuum || mat->ne==0.) {
        v->wave_type=RF_WAVE_VACUUM;
        snprintf(v->wave_reason,sizeof(v->wave_reason),"vacuum; no helicon/TG identification");
        return;
    }
    if(!v->valid || !finite_complex(v->q_reference))return;
    double omega=2*RF_PI*p->frequency_mhz*1e6;
    double ce=RF_E_CHARGE*fabs(c->magnetic_field)/RF_ME;
    double ci=c->ion_charge*RF_E_CHARGE*fabs(c->magnetic_field)/(c->ion_mass_u*RF_AMU);
    double pe2=mat->ne*RF_E_CHARGE*RF_E_CHARGE/(RF_EPS0*RF_ME);
    double lh=ce>0?sqrt(ci*ce/(1+ce*ce/pe2)):0.;
    /* A software naming window for the fundamental and first few ion
       harmonics, not a cutoff in the full cold-plasma dispersion solver. */
    if(ce>0. && omega<=5*ci && omega<=.1*ce) {
        identify_icrf(p,reference,v);
        return;
    }
    if(!(ce>0 && omega<=.1*ce && omega>=10*ci && omega>=3*lh && pe2>=9*ce*ce) || p->kz==0.) {
        snprintf(v->wave_reason,sizeof(v->wave_reason),"outside conservative dense helicon frequency ordering");
        return;
    }
    double delta=omega/ce, kw2=delta*pe2/(RF_C0*RF_C0), kz=fabs(p->kz);
    double disc=1-4*delta*kw2/(kz*kz);
    if(disc<=.05 || v->state==RF_BRANCH_DEGENERATE ||
       fabs(cimag(v->q_reference))>1e-7*fmax(1.,cabs(v->q_reference))) {
        v->wave_type=RF_WAVE_MIXED;
        snprintf(v->wave_reason,sizeof(v->wave_reason),"H/TG reference roots coalesce or become complex");
        return;
    }
    if(v->damping_ratio>.1 || v->reference_shift>.1) {
        snprintf(v->wave_reason,sizeof(v->wave_reason),"loss too strong for weak-loss wave identification");
        return;
    }
    double k0=omega/RF_C0, beta2=k0*k0*creal(v->q_reference)+kz*kz;
    if(creal(v->q_reference)<=0 || beta2<=0 || !v->polarization_valid) {
        snprintf(v->wave_reason,sizeof(v->wave_reason),"no resolved real perpendicular reference wave/polarization");
        return;
    }
    double beta=sqrt(beta2), factor=1+sqrt(disc);
    double beta_h=2*kw2/(kz*factor), beta_tg=kz*factor/(2*delta);
    double h=fabs(log(beta/beta_h)), tg=fabs(log(beta/beta_tg));
    int helicon=h<tg;
    v->wave_match_error=fabs(beta/(helicon?beta_h:beta_tg)-1);
    if(v->wave_match_error>.2 || fabs(h-tg)<log(1.25)) {
        snprintf(v->wave_reason,sizeof(v->wave_reason),"full root does not uniquely match H/TG reference within 20 percent");
        return;
    }
    if(!helicon && !(v->longitudinal_fraction>=.8)) {
        snprintf(v->wave_reason,sizeof(v->wave_reason),"upper reference root lacks quasi-electrostatic polarization");
        return;
    }
    v->wave_type=helicon?RF_WAVE_HELICON:RF_WAVE_TG;
    snprintf(v->wave_reason,sizeof(v->wave_reason),"%s reference match; diagnostic label only",
        helicon?"lower inertia-free helicon":"upper TG with F_L >= 0.8");
}
/* 释放径向诊断表和四条独立自适应轨迹，随后恢复可重复求解的空状态。 */
void rf_branches_free(RfBranchSolution *s)
{
    if(!s) return;
    free(s->r);free(s->region);free(s->points);
    for(int k=0;k<4;k++)free(s->rays[k].points);
    memset(s,0,sizeof(*s));
}
/* 极化重叠辅助根延续；相位规范不影响此量。 */
static double overlap(const RfBranchPoint *a,const RfBranchPoint *b)
{
    if(!a->polarization_valid || !b->polarization_valid) return 0.;
    double complex t=0.;for(int j=0;j<3;j++) t+=conj(a->electric[j])*b->electric[j];
    return pow(cabs(t),2);
}
/* 从一个 Q 根构造指定符号的 kr、极化及无损参考，分开标记传播、群速度
 * 和射线资格；强损耗/倏逝/退化并不通过删除根来隐藏。 */
static void analyze_point(const Project *p,const Plasma *cold,double r,int vacuum,
    const RfPlasmaMaterial *mat,const RfPlasmaMaterial *cm,double complex q,
    double complex cq,double gap,int branch,int sign,RfBranchPoint *v)
{
    empty_point(v);v->q=q;v->q_reference=cq;v->root_gap=gap;
    if(!finite_complex(q)) {snprintf(v->reason,sizeof(v->reason),"nonfinite/infinite dispersion root");return;}
    double k0=2.*RF_PI*p->frequency_mhz*1e6/RF_C0, kp=p->selected_m/r;
    v->kr=(sign?-1.:1.)*csqrt(k0*k0*q-kp*kp);
    if(!finite_complex(v->kr)) return;
    v->valid=1;v->polarization_valid=polarization(p,mat,r,v->kr,branch,vacuum,v);
    v->damping_ratio=fabs(cimag(v->kr))/fmax(fabs(creal(v->kr)),1e-30*k0);
    v->reference_shift=finite_complex(cq)?distance(q,cq):INFINITY;
    double complex coldkr=csqrt(k0*k0*cq-kp*kp);
    v->kr_reference=(sign?-1.:1.)*coldkr;
    if(!vacuum && gap<1e-6) {
        v->state=RF_BRANCH_DEGENERATE;snprintf(v->reason,sizeof(v->reason),"coalescing roots; isolated-branch ray invalid");
    } else if(!finite_complex(cq)) {
        v->state=RF_BRANCH_SINGULAR;snprintf(v->reason,sizeof(v->reason),"collisionless reference unavailable");
    } else if(fabs(cimag(coldkr))>1e-7*fmax(k0,cabs(coldkr)) || cabs(coldkr)<1e-8*k0) {
        v->state=RF_BRANCH_EVANESCENT;snprintf(v->reason,sizeof(v->reason),"radial evanescence or turning point");
    } else if(v->damping_ratio>.1 || v->reference_shift>.1) {
        v->state=RF_BRANCH_DAMPED;snprintf(v->reason,sizeof(v->reason),"weak-loss ray approximation fails");
    } else {
        v->state=RF_BRANCH_PROPAGATING;
        v->group_valid=group_velocity(p,cold,r,(sign?-1.:1.)*creal(coldkr),cm,vacuum,v->vg);
        if(!v->group_valid) snprintf(v->reason,sizeof(v->reason),"group derivative singular");
    }
    identify_wave(p,cold,mat,cm,vacuum,v);
}
/* 固定 f,m,kz，以物理路径长度为自变量。状态为 (r,z,phi,kr,t,tau)；
 * kr 先独立积分，再投影到同一简单参考根，不能每步直接用根代替积分。 */
typedef struct {
    const Project *p;
    const Plasma *c;
    Plasma cold;
    RfPlasmaMaterial center;
    RfBranchPoint anchor;
    int mode, region;
    char reason[200];
} RayContext;

/* 固定 (omega,m,kz,kr) 的径向偏导 F_r：包括 kphi=m/r 的几何项与密度剖面项。 */
static double ray_dr(const RayContext *ctx,double r,double kr,const RfPlasmaMaterial *a)
{
    const Project *p=ctx->p;const Plasma *c=ctx->c;
    double k0=2*RF_PI*p->frequency_mhz*1e6/RF_C0, kp=p->selected_m/r;
    if(ctx->region) return -2*kp*kp/r;
    double S=creal(a->S),D=creal(a->D),P=creal(a->P);
    double Q=(kr*kr+kp*kp)/(k0*k0),N=pow(p->kz/k0,2);
    double fq=2*S*Q-((S+P)*(S-N)-D*D);
    double fs=Q*Q-(2*S+P-N)*Q+2*P*(S-N);
    double fd=2*D*(Q-P),fp=-(S-N)*Q+(S-N)*(S-N)-D*D;
    double df=0;
    if(c->profile==RF_PLASMA_PARABOLIC) df=-2*(1-c->edge_ratio)*r/(c->radius*c->radius);
    else if(c->profile==RF_PLASMA_GAUSSIAN) {
        double logedge=log(fmax(c->edge_ratio,1e-300));
        df=exp(logedge*pow(r/c->radius,2))*logedge*2*r/(c->radius*c->radius);
    }
    return fq*(-2*kp*kp/(r*k0*k0)) + df*(fs*creal(ctx->center.S-1)+
        fd*creal(ctx->center.D)+fp*creal(ctx->center.P-1));
}
/* F 对物理径向波数 kr 的偏导；真空用简单标量色散避免双重行列式根的导数为零。 */
static double ray_dkr(const RayContext *ctx,double r,double kr,const RfPlasmaMaterial *a)
{
    if(ctx->region)return 2*kr;
    double k0=2*RF_PI*ctx->p->frequency_mhz*1e6/RF_C0;
    double Q=(kr*kr+pow(ctx->p->selected_m/r,2))/(k0*k0),N=pow(ctx->p->kz/k0,2);
    return 2*kr/(k0*k0)*creal(2*a->S*Q-((a->S+a->P)*(a->S-N)-a->D*a->D));
}
/* 用多项式各项绝对值之和归一化约束残差；验收前的原始积分 kr 保留供导出。 */
static double ray_residual(const RayContext *ctx,double r,double kr,const RfPlasmaMaterial *a)
{
    double omega=2*RF_PI*ctx->p->frequency_mhz*1e6,k0=omega/RF_C0,kp=ctx->p->selected_m/r;
    if(ctx->region)return fabs(kr*kr+kp*kp+ctx->p->kz*ctx->p->kz-k0*k0)/(k0*k0+kr*kr+kp*kp+ctx->p->kz*ctx->p->kz);
    double Q=(kr*kr+kp*kp)/(k0*k0),N=pow(ctx->p->kz/k0,2);
    double aa=creal(a->S)*Q*Q;
    double bb=creal((a->S+a->P)*(a->S-N)-a->D*a->D)*Q;
    double cc=creal(a->P*((a->S-N)*(a->S-N)-a->D*a->D));
    return fabs(aa-bb+cc)/fmax(DBL_MIN,fabs(aa)+fabs(bb)+fabs(cc));
}
/* 相对最后接受点匹配实际根、参考根和极化；积分途中不按大小重新标号。
 * 径向 WKB 在精确半径处由隐函数导数求得，不依赖绘图径向网格。 */
static int ray_local(RayContext *ctx,double r,RfBranchPoint *v,RfPlasmaMaterial *cm)
{
    double axis=fmax(1e-12,ctx->p->radius*1e-10);
    if(r<=axis || r>=ctx->p->radius ||
       (!ctx->region && r>ctx->c->radius) || (ctx->region && ctx->c->density>0 && r<=ctx->c->radius)) {
        snprintf(ctx->reason,sizeof(ctx->reason),"radial material/axis boundary");return 0;
    }
    RfPlasmaMaterial mat;double complex q[2],cq[2];
    if(!roots(ctx->p,ctx->c,r,&mat,q,ctx->reason,sizeof(ctx->reason)) ||
       !roots(ctx->p,&ctx->cold,r,cm,cq,ctx->reason,sizeof(ctx->reason)))return 0;
    if(distance(q[0],cq[0])+distance(q[1],cq[1])>
       distance(q[0],cq[1])+distance(q[1],cq[0])) {
        double complex swap=cq[0];cq[0]=cq[1];cq[1]=swap;
    }
    RfBranchPoint candidate[2];double score[2];
    for(int b=0;b<2;b++) {
        analyze_point(ctx->p,&ctx->cold,r,ctx->region,&mat,cm,q[b],cq[b],distance(q[0],q[1]),
            ctx->mode/2,ctx->mode%2,&candidate[b]);
        score[b]=distance(candidate[b].q_reference,ctx->anchor.q_reference)+
            distance(candidate[b].q,ctx->anchor.q);
        if(candidate[b].polarization_valid && ctx->anchor.polarization_valid)
            score[b]+=.1*(1-overlap(&candidate[b],&ctx->anchor));
    }
    *v=candidate[score[1]<score[0]];
    if(v->state!=RF_BRANCH_PROPAGATING || !v->group_valid || !v->polarization_valid) {
        snprintf(ctx->reason,sizeof(ctx->reason),"%s",v->reason[0]?v->reason:"local branch unavailable");return 0;
    }
    double kr=creal(v->kr_reference),dk=ray_dkr(ctx,r,kr,cm),dr=ray_dr(ctx,r,kr,cm);
    if(!isfinite(dk) || fabs(dk)<DBL_MIN) {snprintf(ctx->reason,sizeof(ctx->reason),"Hamilton derivative singular");return 0;}
    v->wkb=fmax(fabs(dr/dk)/(kr*kr),1/(fabs(kr)*r));
    if(!isfinite(v->wkb) || v->wkb>.1) {
        snprintf(ctx->reason,sizeof(ctx->reason),"WKB/turning/axis indicator exceeds 0.1");return 0;
    }
    double speed=hypot(hypot(v->vg[0],v->vg[1]),v->vg[2]);
    if(fabs(v->vg[0])<1e-8*speed) {
        snprintf(ctx->reason,sizeof(ctx->reason),"radial group turning point");return 0;
    }
    if(cimag(v->kr)*v->vg[0]<-1e-8*fmax(1.,cabs(v->kr)*speed)) {
        snprintf(ctx->reason,sizeof(ctx->reason),"spatial damping and reference energy direction disagree");return 0;
    }
    v->ray_valid=1;v->reason[0]=0;return 1;
}
/* 路径长度 s（m）下的六状态右端：r,z,phi,kr,t,tau。
 * tau'=2 Im(kr) dr/ds 保留方向符号，向内传播不能对两项分别取绝对值。 */
static int ray_rhs(RayContext *ctx,const double y[6],double f[6])
{
    RfBranchPoint local;RfPlasmaMaterial cm;
    if(!ray_local(ctx,y[0],&local,&cm))return 0;
    double vg[3];
    if(!group_velocity(ctx->p,&ctx->cold,y[0],y[3],&cm,ctx->region,vg)) {
        snprintf(ctx->reason,sizeof(ctx->reason),"frequency derivative unavailable");return 0;
    }
    double speed=hypot(hypot(vg[0],vg[1]),vg[2]);
    double dk=ray_dkr(ctx,y[0],y[3],&cm),dr=ray_dr(ctx,y[0],y[3],&cm);
    if(!isfinite(speed) || speed<=0 || fabs(dk)<DBL_MIN)return 0;
    f[0]=vg[0]/speed;f[1]=vg[2]/speed;f[2]=vg[1]/(y[0]*speed);
    /* -Dr/Dkr * vg_r = Dr/Domega, with all derivatives at fixed m,kz. */
    f[3]=-dr/dk*f[0];f[4]=1/speed;f[5]=2*cimag(local.kr)*f[0];
    for(int j=0;j<6;j++)if(!isfinite(f[j]))return 0;
    return 1;
}
/* 固定当前已接受锚点作四阶 RK 试步；所有子步均重新匹配/筛选局部根，不提交状态。 */
static int ray_rk4(RayContext *ctx,const double y[6],double h,double out[6])
{
    double k[4][6],trial[6];
    if(!ray_rhs(ctx,y,k[0]))return 0;
    for(int stage=1;stage<4;stage++) {
        double factor=stage==3?1.:.5;
        for(int j=0;j<6;j++)trial[j]=y[j]+h*factor*k[stage-1][j];
        if(!ray_rhs(ctx,trial,k[stage]))return 0;
    }
    for(int j=0;j<6;j++)out[j]=y[j]+h*(k[0][j]+2*k[1][j]+2*k[2][j]+k[3][j])/6;
    return 1;
}
/* 轨迹点数组按倍增扩容；保存完整局部诊断，绘图无需从径向表插值轨迹。 */
static int ray_append(RfBranchRay *ray,const RfRayPoint *point)
{
    if(ray->count==ray->capacity) {
        size_t cap=ray->capacity?2*ray->capacity:128;
        RfRayPoint *p=realloc(ray->points,cap*sizeof(*p));if(!p)return 0;
        ray->points=p;ray->capacity=cap;
    }
    ray->points[ray->count++]=*point;return 1;
}
/* 在用户共同起点重新求根，分别推进四个相位候选。拒步不改已接受状态，
 * 碰到界面、转向或 WKB 失效时停止，不自动制造反射、透射或模式转换。 */
static int trace_from_launch(RfBranchSolution *s,char *error,size_t n)
{
    const RfRayConfig *config=&s->ray_config;
    if(!rf_ray_config_validate(config,error,n))return 0;
    double r0=config->r0,eps=fmax(1e-12,s->project.radius*1e-10);
    if(r0>=s->project.radius || (s->plasma.density>0 && fabs(r0-s->plasma.radius)<eps))
        return fail(error,n,"ray r0 must lie strictly inside one radial material region");
    /* Label the launch roots by continuation from the radial diagnostic table. */
    size_t index=0;double nearest=INFINITY;
    int region=r0>s->plasma.radius || s->plasma.density==0;
    for(size_t i=0;i<s->nr;i++)if(s->region[i]==region && fabs(s->r[i]-r0)<nearest) {
        nearest=fabs(s->r[i]-r0);index=i;
    }
    for(int mode=0;mode<4;mode++) {
        RfBranchRay *ray=&s->rays[mode];ray->mode=mode;
        RayContext ctx={.p=&s->project,.c=&s->plasma,.cold=s->plasma,
            .anchor=s->points[4*index+mode],.mode=mode,.region=region};
        ctx.cold.solve_mode=RF_PLASMA_LOSSLESS_REFERENCE;
        if(!region && !rf_plasma_material(ctx.p,&ctx.cold,0,&ctx.center,ctx.reason,sizeof(ctx.reason))) {
            snprintf(ray->stop,sizeof(ray->stop),"launch refused: %s",ctx.reason);continue;
        }
        RfRayPoint point={.r=r0};RfPlasmaMaterial cm;
        empty_point(&point.local);
        int launch_ok=ray_local(&ctx,r0,&point.local,&cm);
        ray->launch_wave_type=point.local.wave_type;
        if(!launch_ok) {
            snprintf(ray->stop,sizeof(ray->stop),"launch refused: %.170s",ctx.reason);continue;
        }
        if(config->direction && point.local.vg[0]*config->direction<=0) {
            snprintf(ray->stop,sizeof(ray->stop),"energy direction not selected");continue;
        }
        point.local.ray_id=(int)++s->ray_count;
        double y[6]={r0,config->z0,config->phi0_deg*RF_PI/180,creal(point.local.kr_reference),0,0};
        point.kr_integrated=y[3];point.local.ray_z=y[1];point.local.ray_phi=y[2];
        point.local.ray_time=point.local.ray_optical_depth=0;
        point.dispersion_error=ray_residual(&ctx,r0,y[3],&cm);
        if(!ray_append(ray,&point))return fail(error,n,"ray allocation failed");
        ctx.anchor=point.local;
        double path=0,h=fmin(config->max_step,config->max_path);
        double minstep=fmax(1e-12*s->project.radius,config->max_step*1e-8);
        double speed0=hypot(hypot(point.local.vg[0],point.local.vg[1]),point.local.vg[2]);
        size_t attempts=0;
        while(ray->count<=(size_t)config->max_steps && attempts++<20*(size_t)config->max_steps) {
            double f[6],full[6],half[6],fine[6];
            if(!ray_rhs(&ctx,y,f)) {snprintf(ray->stop,sizeof(ray->stop),"%s",ctx.reason);break;}
            double inner=region?(s->plasma.density>0?s->plasma.radius:eps):eps;
            double outer=region?s->project.radius:s->plasma.radius;
            double distance=f[0]>0?outer-y[0]:y[0]-inner;
            if(distance<=s->project.radius*1e-8) {
                snprintf(ray->stop,sizeof(ray->stop),"%s",f[0]>0?
                    (region?"PEC wall reached; no reflection coefficients":"plasma/vacuum interface; no transmission coefficients"):
                    (region&&s->plasma.density>0?"vacuum/plasma interface; no transmission coefficients":"axis analysis boundary"));break;
            }
            if(config->max_path-path<=config->max_path*1e-12) {
                snprintf(ray->stop,sizeof(ray->stop),"maximum path length reached (not an axial endplate)");break;
            }
            h=fmin(h,fmin(config->max_path-path,.85*distance/fmax(1e-12,fabs(f[0]))));
            if(h<minstep) {snprintf(ray->stop,sizeof(ray->stop),"step limit: %.175s",ctx.reason[0]?ctx.reason:"event resolution");break;}
            int ok=ray_rk4(&ctx,y,h,full) && ray_rk4(&ctx,y,.5*h,half) && ray_rk4(&ctx,half,.5*h,fine);
            RfBranchPoint local;RfPlasmaMaterial material;
            if(ok)ok=ray_local(&ctx,fine[0],&local,&material);
            double err=INFINITY,projection=0;
            if(ok) {
                double L=s->project.radius,k0=2*RF_PI*s->project.frequency_mhz*1e6/RF_C0;
                double scale[6]={L,L+fabs(fine[1]-config->z0),1+fabs(fine[2]-config->phi0_deg*RF_PI/180),
                    k0+fabs(fine[3]),L/speed0+fine[4],1+fabs(fine[5])};
                err=0;
                for(int j=0;j<6;j++)err=fmax(err,fabs(fine[j]-full[j])/(15*config->tolerance*scale[j]));
                projection=creal(local.kr_reference)-fine[3];
                err=fmax(err,fabs(projection)/(config->tolerance*scale[3]));
            }
            if(!ok || err>1) {ray->rejected++;h*=ok?fmax(.1,.8*pow(err,-.2)):.5;continue;}
            point=(RfRayPoint){.r=fine[0],.path=path+h,.kr_integrated=fine[3],
                .dispersion_error=ray_residual(&ctx,fine[0],fine[3],&material),.correction=projection,.local=local};
            point.local.ray_id=(int)s->ray_count;
            point.local.ray_z=fine[1];point.local.ray_phi=fine[2];
            point.local.ray_time=fine[4];point.local.ray_optical_depth=fine[5];
            if(fine[5]<y[5]-1e-10) {snprintf(ray->stop,sizeof(ray->stop),"passive power increased; damping sign rejected");break;}
            if(!ray_append(ray,&point))return fail(error,n,"ray allocation failed");
            memcpy(y,fine,sizeof(y));y[3]=creal(local.kr_reference);
            path=point.path;ctx.anchor=local;ctx.reason[0]=0;
            if(y[5]>-log(1e-8)) {snprintf(ray->stop,sizeof(ray->stop),"remaining power fraction below 1e-8");break;}
            h=fmin(config->max_step,h*fmin(2.,fmax(.5,.9*pow(fmax(err,1e-10),-.2))));
        }
        if(!ray->stop[0])snprintf(ray->stop,sizeof(ray->stop),"maximum integration steps reached");
        snprintf(ray->points[ray->count-1].local.ray_stop,sizeof(point.local.ray_stop),"%s",ray->stop);
        s->ray_points+=ray->count;
    }
    return 1;
}

/* 单次事务：先建立材料各侧的径向诊断，再从显式起点追踪；成功后才替换旧结果。
 * 四解的存储顺序固定为 A+、A-、B+、B-，A/B 可随半径作连续匹配。 */
int rf_branches_solve_config(const Project *p,const Plasma *c,const RfRayConfig *config,RfBranchSolution *s,char *error,size_t n)
{
    if(!p || !c || !s) return fail(error,n,"null branch analysis input");
    if(!project_validate(p,error,n)||!plasma_validate(c,error,n)) return 0;
    if(c->radius>=p->radius) return fail(error,n,"plasma radius must be inside vessel");
    RfBranchSolution out={0};out.project=*p;out.plasma=*c;
    if(config)out.ray_config=*config;else rf_ray_config_default(&out.ray_config);
    size_t cells=(size_t)p->radial_cells;
    /* 轴心不作平面波替代；中心点从半个显示步长起。材料界面两侧分开。 */
    out.nr=cells+3;
    out.r=calloc(out.nr,sizeof(double));out.region=calloc(out.nr,sizeof(int));
    out.points=calloc(out.nr*4,sizeof(RfBranchPoint));
    if(!out.r||!out.region||!out.points){rf_branches_free(&out);return fail(error,n,"branch allocation failed");}
    size_t nr=0;
    double dr=p->radius/cells, edge_eps=fmin(dr*1e-5,(p->radius-c->radius)*.25);
    for(size_t j=0;j<cells;j++) {
        double r=(j+.5)*dr;
        if(fabs(r-c->radius)>edge_eps) out.r[nr++]=r;
    }
    out.r[nr++]=c->radius;out.r[nr++]=c->radius+edge_eps;out.r[nr++]=p->radius;
    for(size_t j=1;j<nr;j++) {double r=out.r[j];size_t k=j;while(k&&out.r[k-1]>r){out.r[k]=out.r[k-1];k--;}out.r[k]=r;}
    out.nr=nr;
    Plasma cold=*c;cold.solve_mode=RF_PLASMA_LOSSLESS_REFERENCE;
    for(size_t i=0;i<nr;i++) {
        double r=out.r[i];out.region[i]=r>c->radius||c->density==0.;
        int vacuum=out.region[i]||c->density==0.;
        RfPlasmaMaterial mat={0}, cm={0};double complex q[2],cq[2];char why[160]={0};
        int ok=roots(p,c,r,&mat,q,why,sizeof(why));
        int coldok=roots(p,&cold,r,&cm,cq,NULL,0);
        if(!coldok) cq[0]=cq[1]=CMPLX(NAN,NAN);
        if(!ok) {
            for(int k=0;k<4;k++) {RfBranchPoint *v=&out.points[4*i+k];empty_point(v);v->state=RF_BRANCH_MATERIAL_ERROR;snprintf(v->reason,sizeof(v->reason),"%s",why);}
            continue;
        }
        if(cabs(q[0])>cabs(q[1])) {double complex t=q[0];q[0]=q[1];q[1]=t;}
        if(distance(q[0],cq[0])+distance(q[1],cq[1])>
           distance(q[0],cq[1])+distance(q[1],cq[0])) {double complex t=cq[0];cq[0]=cq[1];cq[1]=t;}
        double gap=distance(q[0],q[1]);
        for(int b=0;b<2;b++) for(int sign=0;sign<2;sign++)
            analyze_point(p,&cold,r,vacuum,&mat,&cm,q[b],cq[b],gap,b,sign,&out.points[4*i+2*b+sign]);
        if(i && out.region[i]==out.region[i-1] && !vacuum) {
            RfBranchPoint *a=&out.points[4*(i-1)],*v=&out.points[4*i];
            if(a[0].valid&&a[2].valid&&v[0].valid&&v[2].valid) {
                double same=distance(a[0].q,v[0].q)+distance(a[2].q,v[2].q)+.1*(2-overlap(a,v)-overlap(a+2,v+2));
                double swap=distance(a[0].q,v[2].q)+distance(a[2].q,v[0].q)+.1*(2-overlap(a,v+2)-overlap(a+2,v));
                if(swap<same) for(int sign=0;sign<2;sign++){RfBranchPoint t=v[sign];v[sign]=v[2+sign];v[2+sign]=t;}
            }
        }
        for(int k=0;k<4;k++) out.valid_points+=out.points[4*i+k].valid;
    }
    /* |dkr/dr|/|kr|² 和圆柱曲率 1/(|kr|r)；接近界面不跨侧差分。 */
    for(size_t i=0;i<nr;i++) for(int k=0;k<4;k++) {
        RfBranchPoint *v=&out.points[4*i+k];
        if(!v->group_valid) continue;
        size_t lo=i,hi=i;
        if(i && out.region[i-1]==out.region[i]) lo=i-1;
        if(i+1<nr && out.region[i+1]==out.region[i]) hi=i+1;
        RfBranchPoint *a=&out.points[4*lo+k],*b=&out.points[4*hi+k];
        if(hi==lo||!a->group_valid||!b->group_valid) {snprintf(v->reason,sizeof(v->reason),"neighbour unavailable for WKB check");continue;}
        double kr=fabs(creal(v->kr));
        v->wkb=fmax(cabs(b->kr-a->kr)/(out.r[hi]-out.r[lo])/(kr*kr),1/(kr*out.r[i]));
        double speed=hypot(hypot(v->vg[0],v->vg[1]),v->vg[2]);
        if(v->wkb>.1) snprintf(v->reason,sizeof(v->reason),"WKB/curvature indicator exceeds 0.1");
        else if(fabs(v->vg[0])<1e-6*speed) snprintf(v->reason,sizeof(v->reason),"radial group velocity vanishes");
        else if(cimag(v->kr)*v->vg[0]<-1e-8*fmax(1.,cabs(v->kr)*speed)) snprintf(v->reason,sizeof(v->reason),"spatial damping and reference energy direction disagree");
        else if(!v->polarization_valid) snprintf(v->reason,sizeof(v->reason),"polarization unresolved");
        else {v->ray_valid=1;v->reason[0]=0;}
    }
    /* 两个相位方向必须同时可用，保证双向轨迹使用同一连续区间。 */
    for(size_t i=0;i<nr;i++)for(int b=0;b<2;b++){
        RfBranchPoint *v=&out.points[4*i+2*b];int usable=v[0].ray_valid&&v[1].ray_valid;
        v[0].ray_valid=v[1].ray_valid=usable;
    }
    if(!trace_from_launch(&out,error,n)){rf_branches_free(&out);return 0;}
    snprintf(out.note,sizeof(out.note),
        "固定 f=%.6g MHz，m=%d，kz=%.6g rad/m · 两支局部色散 × ±kr · %zu/%zu 个有限根，%zu 条参考射线。\n"
        "A/B 是连续编号；helicon/TG 型及 ICRF 快/慢波型是局部参考识别，待判定不等于不能传播。\n"
        "复波数采用当前材料响应；群速度来自无碰撞参考，仅弱损耗/非退化传播段有效。WKB 上限 0.1。\n"
        "指定位置发射；路径参数自适应 RK4，固定 f,m,kz，参考实根约束校正。界面/转向/WKB 失效处停止。\n"
        "轴心不采样；轨迹是局部 WKB 解释，不是天线激励场。归一化极化无 V/m 幅值；不含热/动理学分支。%s",
        p->frequency_mhz,p->selected_m,p->kz,out.valid_points,4*nr,out.ray_count,
        c->solve_mode==RF_PLASMA_ZERO_COLLISION_LIMIT?" 当前为零碰撞极限：采用局部正则材料的解析极限，不使用输入碰撞率；不计算奇异层穿越吸收。":"");
    size_t used=strlen(out.note);
    snprintf(out.note+used,sizeof(out.note)-used,"\n起点 r=%.6g m，z=%.6g m，φ=%.6g°；容差 %.2g，最大步长 %.3g m，路径上限 %.4g m。功率为 P/P0，不含天线耦合。",
        out.ray_config.r0,out.ray_config.z0,out.ray_config.phi0_deg,out.ray_config.tolerance,out.ray_config.max_step,out.ray_config.max_path);
    for(int k=0;k<4;k++){used=strlen(out.note);snprintf(out.note+used,sizeof(out.note)-used,"\n%c(%skr)：%zu 点，拒步 %zu；%s",'A'+k/2,k%2?"−":"+",out.rays[k].count,out.rays[k].rejected,out.rays[k].stop);}
    rf_branches_free(s);*s=out;return 1;
}
/* 仅供原 C 接口的默认发射配置；GUI/CLI 使用显式配置入口，不覆盖用户起点。 */
int rf_branches_solve(const Project *p,const Plasma *c,RfBranchSolution *s,char *error,size_t n)
{
    if(!p||!c||!s)return fail(error,n,"null branch analysis input");
    RfRayConfig config;rf_ray_config_default(&config);
    config.r0=.65*c->radius;
    return rf_branches_solve_config(p,c,&config,s,error,n);
}
/* 径向表保留根、有效性、单位、参考及拒绝原因；旧 ray_* 列保持缺失轨迹值。 */
const char *rf_branches_csv_header(void)
{
    return "r_m,region,branch,kr_sign,valid,state,ray_eligible,Q_re,Q_im,kr_re_rad_m,kr_im_rad_m,"
        "damping_ratio,reference_shift,root_gap,E_r_re,E_r_im,E_phi_re,E_phi_im,E_z_re,E_z_im,"
        "longitudinal_fraction,polarization_residual,vg_r_m_s,vg_phi_m_s,vg_z_m_s,WKB,ray_id,"
        "ray_z_m,ray_phi_rad,ray_time_s,power_optical_depth,reason,ray_stop,polarization_valid,group_valid,frequency_mhz,m,kz_rad_m,Q_reference_re,Q_reference_im,kr_reference_re_rad_m,kr_reference_im_rad_m,wave_type,wave_match_error,wave_reason\n";
}
/* 输出一个径向样本的一个相位候选；调用方须保证 i<nr、0<=mode<4。 */
void rf_branches_csv_row(const RfBranchSolution *s,size_t i,int mode,char *buffer,size_t n)
{
    const RfBranchPoint *v=&s->points[4*i+mode];
    snprintf(buffer,n,"%.17g,%s,%c,%d,%d,%s,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,"
        "%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%d,"
        "%.17g,%.17g,%.17g,%.17g,\"%s\",\"%s\",%d,%d,%.17g,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%s,%.17g,\"%s\"\n",
        s->r[i],s->region[i]?"vacuum":"plasma",'A'+mode/2,mode%2?-1:1,v->valid,
        rf_branch_state_name(v->state),v->ray_valid,creal(v->q),cimag(v->q),creal(v->kr),cimag(v->kr),
        v->damping_ratio,v->reference_shift,v->root_gap,creal(v->electric[0]),cimag(v->electric[0]),
        creal(v->electric[1]),cimag(v->electric[1]),creal(v->electric[2]),cimag(v->electric[2]),
        v->longitudinal_fraction,v->polarization_error,v->vg[0],v->vg[1],v->vg[2],v->wkb,v->ray_id,
        v->ray_z,v->ray_phi,v->ray_time,v->ray_optical_depth,v->reason,v->ray_stop,
        v->polarization_valid,v->group_valid,s->project.frequency_mhz,s->project.selected_m,s->project.kz,
        creal(v->q_reference),cimag(v->q_reference),creal(v->kr_reference),cimag(v->kr_reference),
        rf_branch_wave_name(v->wave_type),v->wave_match_error,v->wave_reason);
}
/* 沿程表按每条射线的接受次序输出；P/P0 为不含聚焦和天线耦合的耗散因子。 */
const char *rf_rays_csv_header(void)
{
    return "ray_id,branch,kr_sign,r_m,ray_z_m,ray_phi_rad,ray_time_s,path_m,kr_integrated_rad_m,kr_reference_re_rad_m,kr_re_rad_m,kr_im_rad_m,Q_reference_re,Q_re,Q_im,vg_r_m_s,vg_phi_m_s,vg_z_m_s,E_r_re,E_r_im,E_phi_re,E_phi_im,E_z_re,E_z_im,polarization_residual,WKB,ray_eligible,power_optical_depth,power_fraction,dispersion_residual,kr_correction_rad_m,rejected_steps,frequency_mhz,m,kz_rad_m,region,ray_stop,wave_type,wave_match_error,wave_reason\n";
}
/* 输出一条轨迹的接受点；调用方须保证 mode 和 i 均在对应结果数组范围内。 */
void rf_rays_csv_row(const RfBranchSolution *s,int mode,size_t i,char *buffer,size_t n)
{
    const RfBranchRay *ray=&s->rays[mode];const RfRayPoint *p=&ray->points[i];
    const RfBranchPoint *v=&p->local;
    snprintf(buffer,n,"%d,%c,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,"
        "%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%d,%.17g,%.17g,%.17g,%.17g,%zu,%.17g,%d,%.17g,%s,\"%s\",%s,%.17g,\"%s\"\n",
        v->ray_id,'A'+mode/2,mode%2?-1:1,p->r,v->ray_z,v->ray_phi,v->ray_time,p->path,
        p->kr_integrated,creal(v->kr_reference),creal(v->kr),cimag(v->kr),creal(v->q_reference),creal(v->q),cimag(v->q),
        v->vg[0],v->vg[1],v->vg[2],creal(v->electric[0]),cimag(v->electric[0]),
        creal(v->electric[1]),cimag(v->electric[1]),creal(v->electric[2]),cimag(v->electric[2]),
        v->polarization_error,v->wkb,v->ray_valid,v->ray_optical_depth,exp(-v->ray_optical_depth),
        p->dispersion_error,p->correction,ray->rejected,s->project.frequency_mhz,s->project.selected_m,s->project.kz,
        (p->r>s->plasma.radius||s->plasma.density==0)?"vacuum":"plasma",v->ray_stop,
        rf_branch_wave_name(v->wave_type),v->wave_match_error,v->wave_reason);
}
/* 统一径向/沿程文件导出并检查写入及关闭错误；失败不会报告成完整数值结果。 */
static int export_csv(const RfBranchSolution *s,const char *path,int rays,char *error,size_t n)
{
    if(!s||!s->points||!path) return fail(error,n,"no branch results/path");
    FILE *f=rf_fopen_utf8(path,"w");if(!f)return fail(error,n,"cannot open branch CSV");
    fputs(rays?rf_rays_csv_header():rf_branches_csv_header(),f);char row[3000];
    if(rays) {
        for(int k=0;k<4;k++)for(size_t i=0;i<s->rays[k].count;i++) {
            rf_rays_csv_row(s,k,i,row,sizeof(row));fputs(row,f);
        }
    } else for(size_t i=0;i<s->nr;i++)for(int k=0;k<4;k++){
        rf_branches_csv_row(s,i,k,row,sizeof(row));fputs(row,f);
    }
    int bad=ferror(f);if(fclose(f))bad=1;
    return bad?fail(error,n,"branch CSV write failed"):1;
}
/* 导出完整径向诊断，保留无效根及其原因，不只导出能传播的候选。 */
int rf_branches_export_csv(const RfBranchSolution *s,const char *path,char *error,size_t n)
{return export_csv(s,path,0,error,n);}
/* 只导出实际发射的轨迹；零条射线时仍有合法表头，不伪造数据行。 */
int rf_branches_export_rays(const RfBranchSolution *s,const char *path,char *error,size_t n)
{return export_csv(s,path,1,error,n);}
