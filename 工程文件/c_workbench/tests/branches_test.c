#include "branches.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static double determinant_residual(const Project *p,const RfPlasmaMaterial *a,double r,
    const RfBranchPoint *v)
{
    double k0=2*RF_PI*p->frequency_mhz*1e6/RF_C0;
    double complex k[3]={v->kr/k0,p->selected_m/(r*k0),p->kz/k0};
    double complex ep[3][3]={{a->S,-I*a->D,0},{I*a->D,a->S,0},{0,0,a->P}};
    double complex M[3][3],k2=k[0]*k[0]+k[1]*k[1]+k[2]*k[2];
    double scale=0;
    for(int j=0;j<3;j++)for(int l=0;l<3;l++){
        M[j][l]=ep[j][l]+k[j]*k[l]-(j==l?k2:0);scale=fmax(scale,cabs(M[j][l]));
    }
    for(int j=0;j<3;j++)for(int l=0;l<3;l++)M[j][l]/=scale;
    double complex d=M[0][0]*(M[1][1]*M[2][2]-M[1][2]*M[2][1])-
        M[0][1]*(M[1][0]*M[2][2]-M[1][2]*M[2][0])+
        M[0][2]*(M[1][0]*M[2][1]-M[1][1]*M[2][0]);
    return cabs(d);
}
/* Independent Brillouin energy velocity from E,B and d(omega epsilon)/domega.
 * This checks the group derivative including anisotropy and electron/ion dispersion. */
static void energy_velocity(const Project *p,const Plasma *c,double r,
    const RfBranchPoint *v,double velocity[3])
{
    double omega=2*RF_PI*p->frequency_mhz*1e6;
    double complex k[3]={v->kr,p->selected_m/r,p->kz}, B[3];
    for(int j=0;j<3;j++)B[j]=(k[(j+1)%3]*v->electric[(j+2)%3]-
        k[(j+2)%3]*v->electric[(j+1)%3])/omega;
    double h=1e-5;Project hi=*p,lo=*p;hi.frequency_mhz*=1+h;lo.frequency_mhz*=1-h;
    RfPlasmaMaterial a,b;char err[128];
    assert(rf_plasma_material(&hi,c,r,&a,err,sizeof(err)));
    assert(rf_plasma_material(&lo,c,r,&b,err,sizeof(err)));
    double complex ds=((1+h)*a.S-(1-h)*b.S)/(2*h);
    double complex dd=((1+h)*a.D-(1-h)*b.D)/(2*h);
    double complex dp=((1+h)*a.P-(1-h)*b.P)/(2*h);
    double complex response[3]={ds*v->electric[0]-I*dd*v->electric[1],
        I*dd*v->electric[0]+ds*v->electric[1],dp*v->electric[2]};
    double W=0;
    for(int j=0;j<3;j++)W+=.25*(RF_EPS0*creal(conj(v->electric[j])*response[j])+pow(cabs(B[j]),2)/RF_MU0);
    for(int j=0;j<3;j++)velocity[j]=.5*creal(v->electric[(j+1)%3]*conj(B[(j+2)%3])-
        v->electric[(j+2)%3]*conj(B[(j+1)%3]))/RF_MU0/W;
}
/* In a uniform anisotropic medium, Cartesian group velocity is constant.
 * This analytic path is independent of the cylindrical Hamilton integrator. */
static double uniform_ray_error(const RfBranchSolution *s)
{
    double worst=0;
    for(int k=0;k<4;k++) {
        const RfBranchRay *ray=&s->rays[k];if(!ray->count)continue;
        const RfRayPoint *seed=&ray->points[0];
        for(size_t i=0;i<ray->count;i++) {
            const RfRayPoint *p=&ray->points[i];double t=p->local.ray_time;
            double x=seed->r+seed->local.vg[0]*t,y=seed->local.vg[1]*t;
            double r=hypot(x,y),z=seed->local.ray_z+seed->local.vg[2]*t;
            double phi=seed->local.ray_phi+atan2(y,x);
            worst=fmax(worst,fabs(p->r-r));
            worst=fmax(worst,fabs(p->local.ray_z-z));
            worst=fmax(worst,seed->r*fabs(p->local.ray_phi-phi));
            assert(p->dispersion_error<1e-4);
            assert(p->local.polarization_error<1e-8);
            if(i)assert(t>ray->points[i-1].local.ray_time && p->path>ray->points[i-1].path);
        }
    }
    return worst;
}
int main(void)
{
    Project p;Plasma c;project_default(&p);plasma_default(&c);
    p.radial_cells=256;p.frequency_mhz=1000;p.kz=1;p.selected_m=0;c.density=0;
    RfBranchSolution s={0};char error[256];
    RfRayConfig config;rf_ray_config_default(&config);config.r0=.55;config.max_path=.5;
    assert(rf_branches_solve_config(&p,&c,&config,&s,error,sizeof(error)));
    assert(s.ray_count>0);
    assert(uniform_ray_error(&s)<1e-7);
    double omega=2*RF_PI*p.frequency_mhz*1e6,k0=omega/RF_C0;
    for(size_t i=0;i<s.nr;i++)for(int k=0;k<4;k++){
        RfBranchPoint *v=&s.points[4*i+k];assert(v->valid&&v->group_valid&&v->polarization_valid);
        assert(v->wave_type==RF_WAVE_VACUUM);
        assert(fabs(creal(v->kr*v->kr)-(k0*k0-p.kz*p.kz))<1e-7);
        assert(fabs(hypot(hypot(v->vg[0],v->vg[1]),v->vg[2])/RF_C0-1)<1e-10);
        assert(v->polarization_error<1e-12);
        if(v->ray_id) {assert(v->ray_time>=0);assert(fabs(v->ray_z-v->vg[2]*v->ray_time)<1e-8);}
    }
    rf_branches_free(&s);
    p.frequency_mhz=13.56;p.kz=10;p.selected_m=1;
    c.density=1e18;c.magnetic_field=.02;c.profile=RF_PLASMA_UNIFORM;
    c.electron_collision=c.ion_collision=0;c.solve_mode=RF_PLASMA_FINITE_COLLISION;
    assert(rf_branches_solve(&p,&c,&s,error,sizeof(error)));
    size_t groups=0,pols=0;
    for(size_t i=0;i<s.nr;i++)for(int k=0;k<4;k++){
        RfBranchPoint *v=&s.points[4*i+k];RfPlasmaMaterial mat;
        assert(rf_plasma_material(&p,&c,s.r[i],&mat,error,sizeof(error)));
        if(v->valid)assert(determinant_residual(&p,&mat,s.r[i],v)<1e-9);
        if(v->polarization_valid){assert(v->polarization_error<1e-9);pols++;}
        if(v->group_valid && !s.region[i]) {
            double vel[3];energy_velocity(&p,&c,s.r[i],v,vel);
            double scale=hypot(hypot(vel[0],vel[1]),vel[2]);
            for(int j=0;j<3;j++)assert(fabs(vel[j]-v->vg[j])<1e-5*scale);
            groups++;
            if(s.r[i]>.1) {
                assert(v->wave_type==(k/2?RF_WAVE_TG:RF_WAVE_HELICON));
                assert(v->wave_match_error<.03);
                /* Both branches can be almost longitudinal: F_L alone does
                   not distinguish helicon from TG in this actual fixture. */
                assert(v->longitudinal_fraction>.99);
            }
        }
    }
    assert(groups>100&&pols>100&&s.ray_count>=4);
    /* +/- kr has opposite radial energy flow but same axial/azimuthal velocity. */
    for(size_t i=0;i<s.nr;i++)for(int b=0;b<2;b++){
        RfBranchPoint *v=&s.points[4*i+2*b];
        if(v[0].group_valid&&v[1].group_valid){
            assert(fabs(v[0].vg[0]+v[1].vg[0])<1e-8);
            assert(fabs(v[0].vg[2]-v[1].vg[2])<1e-8);
        }
    }
    rf_branches_free(&s);
    /* Tighten step/tolerance independently of the radial diagnostic sampling. */
    double errors[3];
    for(int level=0;level<3;level++) {
        p.radial_cells=128;
        config.r0=.2;config.z0=.7;config.phi0_deg=25;config.max_path=3;
        config.max_step=.8/pow(2.,level);config.tolerance=1e-3/pow(100.,level);
        assert(rf_branches_solve_config(&p,&c,&config,&s,error,sizeof(error)));
        assert(s.ray_count==4);
        for(int k=0;k<4;k++) {
            const RfRayPoint *start=&s.rays[k].points[0];
            assert(start->r==config.r0 && start->local.ray_z==config.z0);
            assert(fabs(start->local.ray_phi-config.phi0_deg*RF_PI/180)<1e-15);
        }
        errors[level]=uniform_ray_error(&s);rf_branches_free(&s);
    }
    assert(errors[2]<errors[0]*.1 && errors[2]<1e-6);
    printf("adaptive Hamilton ray analytic errors: %.3g %.3g %.3g m\n",errors[0],errors[1],errors[2]);
    p.radial_cells=256;
    /* Genuine weak collisions: passive attenuation along both energy directions. */
    c.electron_collision=1e5;c.ion_collision=100;
    assert(rf_branches_solve(&p,&c,&s,error,sizeof(error)));
    assert(s.ray_count>=4);
    for(int k=0;k<4;k++)for(size_t i=0;i<s.rays[k].count;i++) {
        const RfRayPoint *v=&s.rays[k].points[i];
        assert(v->local.ray_optical_depth>=-1e-12);
        if(i)assert(v->local.ray_optical_depth>=s.rays[k].points[i-1].local.ray_optical_depth-1e-10);
    }
    rf_branches_free(&s);
    c.electron_collision=1e10;
    assert(rf_branches_solve(&p,&c,&s,error,sizeof(error)));
    int damped=0;for(size_t i=0;i<s.nr;i++)for(int k=0;k<4;k++){
        RfBranchPoint *v=&s.points[4*i+k];if(!s.region[i]) {
            assert(!v->ray_id);damped+=v->state==RF_BRANCH_DAMPED;
            assert(v->wave_type!=RF_WAVE_HELICON && v->wave_type!=RF_WAVE_TG);
        }}
    assert(damped>0);rf_branches_free(&s);
    /* A near-coalescing pair has no reliable separate H/TG label even if
       the full cold dispersion still returns two finite roots. */
    double delta=2*RF_PI*p.frequency_mhz*1e6*RF_ME/(RF_E_CHARGE*fabs(c.magnetic_field));
    double density=c.density;
    c.density=.999*p.kz*p.kz*RF_C0*RF_C0/(4*delta*delta)*RF_EPS0*RF_ME/(RF_E_CHARGE*RF_E_CHARGE);
    c.electron_collision=c.ion_collision=0;
    assert(rf_branches_solve(&p,&c,&s,error,sizeof(error)));
    for(size_t i=0;i<s.nr;i++)if(!s.region[i])for(int k=0;k<4;k++)
        assert(s.points[4*i+k].wave_type==RF_WAVE_MIXED);
    rf_branches_free(&s);c.density=density;
    /* Same full dispersion covers sub-/super-ion-cyclotron and helicon frequencies. */
    double frequencies[]={.003,.005,.01,.03,1.,13.56};
    c.electron_collision=1e3;c.ion_collision=1.;
    for(size_t f=0;f<sizeof(frequencies)/sizeof(*frequencies);f++) {
        p.frequency_mhz=frequencies[f];
        assert(rf_branches_solve(&p,&c,&s,error,sizeof(error)));
        for(size_t i=0;i<s.nr;i++)for(int k=0;k<4;k++) {
            const RfBranchPoint *v=&s.points[4*i+k];RfPlasmaMaterial mat;
            if(frequencies[f]<1.)assert(v->wave_type!=RF_WAVE_HELICON && v->wave_type!=RF_WAVE_TG);
            assert(rf_plasma_material(&p,&c,s.r[i],&mat,error,sizeof(error)));
            if(v->valid)assert(determinant_residual(&p,&mat,s.r[i],v)<1e-8);
        }
        rf_branches_free(&s);
    }
    /* Exact material pole yields annotated missing points, no fabricated wave/ray. */
    c.electron_collision=0;c.ion_collision=0;
    p.frequency_mhz=RF_E_CHARGE*fabs(c.magnetic_field)/(RF_ME*2*RF_PI*1e6);
    assert(rf_branches_solve(&p,&c,&s,error,sizeof(error)));
    for(size_t i=0;i<s.nr;i++)if(!s.region[i])for(int k=0;k<4;k++){
        assert(!s.points[4*i+k].valid);assert(s.points[4*i+k].state==RF_BRANCH_MATERIAL_ERROR);
    }
    rf_branches_free(&s);
    p.frequency_mhz=13.56;c.electron_collision=1e5;c.ion_collision=100;
    /* Nonuniform paths must not depend on the radial diagnostic grid. Shifts
       in z/phi change only the origin because the background is invariant. */
    c.profile=RF_PLASMA_PARABOLIC;c.edge_ratio=.2;
    rf_ray_config_default(&config);config.r0=.15;config.max_path=.5;
    double endpoint[4][4];
    for(int level=0;level<2;level++) {
        p.radial_cells=level?512:128;config.z0=level?2:0;config.phi0_deg=level?60:0;
        assert(rf_branches_solve_config(&p,&c,&config,&s,error,sizeof(error)));
        assert(s.ray_count==4);
        for(int k=0;k<4;k++) {
            const RfBranchRay *ray=&s.rays[k];assert(ray->count>3);
            const RfRayPoint *last=&ray->points[ray->count-1];
            double values[4]={last->r,last->local.ray_z-config.z0,
                last->local.ray_phi-config.phi0_deg*RF_PI/180,last->local.ray_optical_depth};
            for(int j=0;j<4;j++)if(level)assert(fabs(values[j]-endpoint[k][j])<1e-7);else endpoint[k][j]=values[j];
            assert(cabs(last->local.q_reference-ray->points[0].local.q_reference)>1e-6);
            for(size_t j=0;j<ray->count;j++)assert(ray->points[j].dispersion_error<1e-4);
        }
        rf_branches_free(&s);
    }
    c.profile=RF_PLASMA_UNIFORM;
    rf_ray_config_default(&config);config.r0=.2;config.max_path=.4;config.direction=-1;
    assert(rf_branches_solve_config(&p,&c,&config,&s,error,sizeof(error)));
    assert(s.ray_count==2);
    for(int k=0;k<4;k++)if(s.rays[k].count)assert(s.rays[k].points[0].local.vg[0]<0);
    rf_branches_free(&s);
    config.direction=0;config.r0=.0001;
    assert(rf_branches_solve_config(&p,&c,&config,&s,error,sizeof(error)));
    assert(s.ray_count==0);
    for(int k=0;k<4;k++)assert(strstr(s.rays[k].stop,"launch refused"));
    rf_branches_free(&s);
    config.r0=p.radius;
    assert(!rf_branches_solve_config(&p,&c,&config,&s,error,sizeof(error)));
    config.r0=.2;config.tolerance=NAN;
    assert(!rf_branches_solve_config(&p,&c,&config,&s,error,sizeof(error)));
    rf_ray_config_default(&config);config.r0=.55;config.direction=1;
    c.density=0;p.frequency_mhz=10000;p.selected_m=0;p.kz=1;
    assert(rf_branches_solve_config(&p,&c,&config,&s,error,sizeof(error)));
    assert(s.ray_count==2);
    for(int k=0;k<4;k++)if(s.rays[k].count) {
        const RfBranchRay *ray=&s.rays[k];
        assert(strstr(ray->stop,"PEC wall"));
        assert(fabs(ray->points[ray->count-1].r-p.radius)<1e-7*p.radius);
    }
    rf_branches_free(&s);
    puts("branches PASS: vacuum, full Maxwell determinant, Brillouin energy velocity, both directions, weak/strong damping and material pole");
    return 0;
}
