/* Independent source formulas for the seventeen numbered antenna models.
 * The reference path integrals below never call the geometry builder.
 * argv[1]: optional numerical audit CSV; argv[2]: saddle mesh CSV; argv[3]: new centre lines; argv[4]: new spectra CSV.
 */
#include "geometry.h"
#include "maxwell.h"
#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.1415926535897932384626433832795
static char error[256];
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s; %s\n",__LINE__,#x,error); exit(1); } } while (0)
static const char *names[] = {"", "strap", "array", "comb", "loop", "helix", "single_saddle",
    "double_saddle", "Nagoya_III", "line_return", "partial_turn", "half_Nagoya", "half_array", "axial_loops", "planar_spiral", "segmented_rings", "birdcage", "meander"};

static double sinc(double x)
{
    return fabs(x) < 1e-7 ? 1.-x*x/6.+x*x*x*x/120. : sin(x)/x;
}

static Project catalogue(int type)
{
    Project p; project_default(&p);
    p.type=type; p.current=1.; p.frequency_mhz=13.56;
    p.width=.18; p.band_width=.04; p.selected_m=(type==RF_ANT_LOOP || type==RF_ANT_AXIAL_LOOPS)?0:1;
    p.kz=10.; p.arc_angle_deg=180.;
    if(type==RF_ANT_ARRAY || type==RF_ANT_COMB) p.width=.12;
    if(type==RF_ANT_COMB) {p.elements=6;p.phase_step=75.;}
    if(type==RF_ANT_HELIX) {p.turns=.5;p.winding_direction=-1;p.azimuth_deg=90.;}
    if(type==RF_ANT_SADDLE) p.arc_angle_deg=120.;
    if(type==RF_ANT_SINGLE_SADDLE) p.arc_angle_deg=120.;
    if(type==RF_ANT_AXIAL_LOOPS) {p.elements=2;p.phase_step=180.;}
    if(type==RF_ANT_PLANAR_SPIRAL) p.turns=4.;
    if(type==RF_ANT_SEGMENTED_RINGS) p.inner_radius=.14;
    if(type==RF_ANT_BIRDCAGE) p.elements=8;
    if(type==RF_ANT_MEANDER) {p.elements=7;p.width=.5;p.spacing=.1;p.current_phase_gradient=8.;}
    return p;
}

/* Independent composite Simpson average of the exact thin rectangle spectrum.
 * Written with sinc, so m=0 and kz=0 use their analytic continuous limits. */
static void rectangle_reference(const Project *p, double alpha, double complex k[3])
{
    const int n=1024;
    k[0]=k[1]=k[2]=0.;
    for(int j=0;j<=n;++j) {
        double u=p->band_width*((double)j/n-.5), a=alpha+2*u/p->antenna_radius;
        double len=p->antenna_length+2*u, weight=(j==0||j==n)?1.:(j%2?4.:2.);
        k[1]+=weight*(-I*a/PI*sinc(p->selected_m*a/2.)*sin(p->kz*len/2.));
        k[2]+=weight*(I*len/(PI*p->antenna_radius)*sin(p->selected_m*a/2.)*sinc(p->kz*len/2.));
    }
    k[1]/=3*n;k[2]/=3*n;
}

static void reference(const Project *p, double complex k[3])
{
    double b=p->antenna_radius,c=p->return_radius,L=p->antenna_length,w=p->band_width;
    int m=p->selected_m;
    double z=p->kz,a=p->arc_angle_deg*PI/180.;
    double complex factor=1.;
    k[0]=k[1]=k[2]=0.;
    switch(p->type) {
    case RF_ANT_STRAP: case RF_ANT_ARRAY: case RF_ANT_COMB:
        rectangle_reference(p,p->width/b,k);
        if(p->type!=RF_ANT_STRAP) {
            factor=0.;
            for(int e=0;e<p->elements;++e)
                factor+=cexp(I*(e*p->phase_step*PI/180.-m*(e-(p->elements-1)/2.)*p->spacing/b));
        }
        break;
    case RF_ANT_LOOP:
        k[1]=m==0?sinc(z*w/2.):0.;
        break;
    case RF_ANT_HELIX: case RF_ANT_NAGOYA: {
        double t=p->type==RF_ANT_NAGOYA?0.:p->winding_direction*2*PI*p->turns;
        double A=hypot(L,b*t),g=.5*(sinc((z+m*t/L-m*A/(b*L))*w/2.)+
            sinc((z+m*t/L+m*A/(b*L))*w/2.));
        if(m%2) {
            k[2]=L/(PI*b)*cexp(-I*m*t/2.)*sinc((z*L+m*t)/2.)*g;
            k[1]=-b*z/m*k[2];
        }
        break;
    }
    case RF_ANT_SADDLE: case RF_ANT_SINGLE_SADDLE:
        rectangle_reference(p,a,k);
        factor=p->type==RF_ANT_SINGLE_SADDLE?-1.:-(m%2?2.:0.);
        break;
    case RF_ANT_LINE: {
        const int n=8192;
        double h=sqrt(c*c-b*b),beta=acos(b/c);
        for(int j=0;j<=n;++j) {
            double y=h*(2.*j/n-1.),r=hypot(b,y),weight=(j==0||j==n)?1.:(j%2?4.:2.);
            double complex phase=cexp(-I*m*atan2(y,b));
            k[0]+=weight*y/r*phase;
            k[1]+=weight*b/r*phase;
        }
        k[0]*=2*h/(3*n); k[1]*=2*h/(3*n);
        k[1]-=2*c*beta*sinc(m*beta);
        factor=sinc(z*w/2.)/(2*PI*b);
        break;
    }
    case RF_ANT_PARTIAL_LOOP: case RF_ANT_HALF_ARRAY:
        k[0]=-I*(c-b)/(PI*b)*sin(m*a/2.)*sinc(z*w/2.);
        k[1]=a*(b-c)/(2*PI*b)*sinc(m*a/2.)*sinc(z*w/2.);
        if(p->type==RF_ANT_HALF_ARRAY) {
            factor=0.;
            for(int e=0;e<p->elements;++e)
                factor+=cexp(I*e*(p->phase_step*PI/180.-2*PI*m/p->elements));
        }
        break;
    case RF_ANT_HALF_NAGOYA:
        k[0]=-I*(c-b)/(PI*b)*sin(z*L/2.)*sinc(m*w/(2*b));
        break;
    case RF_ANT_AXIAL_LOOPS:
        if(m==0) {
            for(int j=0;j<p->elements;++j)
                k[1]+=cexp(I*j*p->phase_step*PI/180.-I*z*(j-(p->elements-1)/2.)*p->spacing);
            k[1]*=sinc(z*w/2.);
        }
        break;
    case RF_ANT_PLANAR_SPIRAL: {
        /* Continuous Archimedean curve, independent of segment construction. */
        const int n=8192;
        double dr=b-p->inner_radius, twist=p->winding_direction*2*PI*p->turns;
        for(int j=0;j<=n;++j) {
            double t=(double)j/n, r=p->inner_radius+dr*t;
            double weight=(j==0||j==n)?1.:(j%2?4.:2.);
            double complex phase=cexp(-I*m*twist*t);
            k[0]+=weight*dr*phase;
            k[1]+=weight*r*twist*phase;
        }
        factor=sinc(z*w/2.)/(3*n*2*PI*b);
        break;
    }
    case RF_ANT_SEGMENTED_RINGS:
        for(int j=0;j<p->elements;++j) {
            double r=p->elements==1?p->inner_radius:p->inner_radius+(b-p->inner_radius)*j/(p->elements-1.);
            int ns=j==0?2:4;
            double span=2*PI/ns-p->gap_angle_deg*PI/180.;
            for(int h=0;h<ns;++h) {
                double phi=2*PI*h/ns;
                double complex drive=p->current_pattern?cexp(I*p->source_mode*phi):cos(p->source_mode*phi);
                k[1]+=r*span*drive*cexp(-I*m*phi)*sinc(m*span/2.);
            }
        }
        factor=sinc(z*w/2.)/(2*PI*b);
        break;
    case RF_ANT_BIRDCAGE: {
        double step=2*PI/p->elements;
        for(int j=0;j<p->elements;++j) {
            double phi=j*step;
            double complex g=p->current_pattern?cexp(I*p->source_mode*phi):cos(p->source_mode*phi);
            double complex t=cexp(I*p->source_mode*phi)/(1.-cexp(-I*p->source_mode*step));
            if(!p->current_pattern)t=creal(t);
            k[2]+=L*g*cexp(-I*m*phi)*sinc(z*L/2.)/b;
            k[1]+=-2*I*sin(z*L/2.)*step*t*cexp(-I*m*(phi+step/2.))*sinc(m*step/2.);
        }
        factor=sinc(m*w/(2*b))/(2*PI);
        break;
    }
    case RF_ANT_MEANDER: {
        double distance=0., beta=p->current_phase_gradient,half=p->width/(2*b);
        for(int j=0;j<p->elements;++j) {
            double sign=j%2?-1.:1., zz=(j-(p->elements-1)/2.)*p->spacing;
            double dp=sign*p->width/b;
            k[1]+=dp*cexp(I*beta*(distance+p->width/2.)-I*z*zz)*sinc((m*dp-beta*p->width)/2.);
            distance+=p->width;
            if(j+1<p->elements) {
                k[2]+=p->spacing/b*cexp(I*beta*(distance+p->spacing/2.)-I*(m*sign*half+z*(zz+p->spacing/2.)))*sinc((z-beta)*p->spacing/2.);
                distance+=p->spacing;
            }
        }
        factor=sinc(m*w/(2*b))/(2*PI);
        break;
    }
    default: CHECK(0);
    }
    factor*=p->current*p->current_direction*cexp(I*(p->global_phase_deg*PI/180.-
        m*p->azimuth_deg*PI/180.-z*p->axial_offset));
    for(int j=0;j<3;++j) k[j]*=factor;
}

static double source_check(Project p)
{
    double worst=0.;
    for(int move=0;move<2;++move) for(int iw=0;iw<2;++iw)
    for(int m=-3;m<=3;++m) for(int ik=-1;ik<=1;++ik) {
        Project q=p;
        q.selected_m=m;q.kz=10.*ik;q.band_width=iw*.04;
        if(move) {q.current_direction=-1;q.global_phase_deg=17;q.azimuth_deg+=23;q.axial_offset=.08;}
        Geometry g={0}; double complex actual[3],expected[3];
        CHECK(geometry_build(&q,&g,error,sizeof error));
        if(q.type<=RF_ANT_AXIAL_LOOPS || q.type==RF_ANT_BIRDCAGE)
            CHECK(geometry_current_balance(&g)<1e-10);
        CHECK(geometry_current_spectrum(&q,&g,actual,error,sizeof error));
        reference(&q,expected);
        for(int j=0;j<3;++j) {
            double delta=cabs(actual[j]-expected[j])/q.current;
            worst=fmax(worst,delta);
            if(delta>1e-9) fprintf(stderr,"source number=%d m=%d k=%g w=%g component=%d error=%g\n",q.type,m,q.kz,q.band_width,j,delta);
            CHECK(delta<1e-9);
        }
        geometry_free(&g);
    }
    return worst;
}

static void dump_saddle(const char *path)
{
    FILE *f=fopen(path,"w");CHECK(f);
    fputs("ax,ay,az,bx,by,bz,sa_r,sa_phi,sa_z,sb_r,sb_phi,sb_z,amplitude,phase,group\n",f);
    Project p=catalogue(RF_ANT_SADDLE);Geometry g={0};
    CHECK(geometry_build(&p,&g,error,sizeof error));
    for(size_t i=0;i<g.segment_count;++i) {
        GeometrySegment *s=g.segments+i;
        for(int j=0;j<3;++j)fprintf(f,"%.17g,",s->a[j]);
        for(int j=0;j<3;++j)fprintf(f,"%.17g,",s->b[j]);
        for(int j=0;j<3;++j)fprintf(f,"%.17g,",s->side_a[j]);
        for(int j=0;j<3;++j)fprintf(f,"%.17g,",s->side_b[j]);
        fprintf(f,"%.17g,%.17g,%d\n",s->amplitude,s->phase,s->group);
    }
    geometry_free(&g);CHECK(!fclose(f));
}

typedef struct {int m;double k;double complex sum;} ChargeCheck;
static int charge_visit(void *context,double r,const double complex dl[3])
{
    ChargeCheck *q=context;
    /* Test potential psi=r^2 exp[-i(m phi+kz)], differentiated analytically. */
    q->sum+=2*r*dl[0]-I*q->m*r*dl[1]-I*q->k*r*r*dl[2];
    return 1;
}
static double complex charge_reference(const Project *p)
{
    double b=p->antenna_radius,m=p->selected_m,k=p->kz;
    double complex result=0.;
    double width=sinc(k*p->band_width/2.);
    if(p->type==RF_ANT_PLANAR_SPIRAL) {
        result=b*b*cexp(-I*m*p->winding_direction*2*PI*p->turns)-p->inner_radius*p->inner_radius;
    } else if(p->type==RF_ANT_SEGMENTED_RINGS) {
        for(int j=0;j<p->elements;++j) {
            int ns=j?4:2;double half=PI/ns-p->gap_angle_deg*PI/360.;
            double r=p->elements==1?p->inner_radius:p->inner_radius+(b-p->inner_radius)*j/(p->elements-1.);
            for(int h=0;h<ns;++h) {
                double phi=2*PI*h/ns;
                double complex current=p->current_pattern?cexp(I*p->source_mode*phi):cos(p->source_mode*phi);
                result+=r*r*current*(cexp(-I*m*(phi+half))-cexp(-I*m*(phi-half)));
            }
        }
    } else if(p->type==RF_ANT_MEANDER) {
        width=sinc(m*p->band_width/(2*b));
        double beta=p->current_phase_gradient,half=p->width/(2*b),N=p->elements;
        double total=N*p->width+(N-1)*p->spacing;
        double endphi=p->elements%2?half:-half;
        result=b*b*(cexp(I*beta*total-I*(m*endphi+k*(N-1)*p->spacing/2.))-
            cexp(-I*(-m*half-k*(N-1)*p->spacing/2.)));
        double distance=0.;double complex integral=0.;
        for(int j=0;j<p->elements;++j) {
            double sign=j%2?-1.:1.,z=(j-(N-1)/2.)*p->spacing;
            integral+=p->width*cexp(I*beta*(distance+p->width/2.)-I*k*z)*
                sinc((m*sign*p->width/b-beta*p->width)/2.);
            distance+=p->width;
            if(j+1<p->elements) {
                integral+=p->spacing*cexp(I*beta*(distance+p->spacing/2.)-
                    I*(m*sign*half+k*(z+p->spacing/2.)))*sinc((k-beta)*p->spacing/2.);
                distance+=p->spacing;
            }
        }
        result-=I*beta*b*b*integral;
    }
    return result*width*p->current*p->current_direction/(2*PI)*
        cexp(I*(p->global_phase_deg*PI/180.-m*p->azimuth_deg*PI/180.-k*p->axial_offset));
}
static void charge_checks(void)
{
    for(int type=RF_ANT_AXIAL_LOOPS;type<=RF_ANT_MEANDER;++type)
    for(int m=-3;m<=3;++m) for(int ik=-1;ik<=1;++ik) for(int wide=0;wide<2;++wide) {
        Project p=catalogue(type);p.selected_m=m;p.kz=10*ik;p.band_width=.04*wide;
        p.azimuth_deg=23.;p.axial_offset=.08;p.global_phase_deg=17.;p.current_direction=-1;
        Geometry g={0};CHECK(geometry_build(&p,&g,error,sizeof error));
        ChargeCheck q={.m=m,.k=p.kz};
        CHECK(geometry_integrate_source(&p,&g,NULL,0,charge_visit,&q,error,sizeof error));
        CHECK(cabs(q.sum-charge_reference(&p))<1e-10);
        geometry_free(&g);
    }
}

int main(int argc,char **argv)
{
    FILE *csv=argc>1?fopen(argv[1],"w"):NULL;
    if(argc>1) CHECK(csv);
    if(csv)fputs("number,name,m,kz_rad_m,source_error_over_I0,vacuum_backward_error,plasma_backward_error,power_balance_error,absorbed_power_W_m,radial_nodes\n",csv);
    double worst_source=0.,worst_linear=0.,worst_power=0.;
    for(int type=RF_ANT_STRAP;type<=RF_ANT_MEANDER;++type) {
        Project p=catalogue(type);Plasma plasma;plasma_default(&plasma);
        double source_error=source_check(p);
        RfMaxwellSolution vacuum={0},loaded={0};
        CHECK(rf_maxwell_solve(&p,NULL,&vacuum,error,sizeof error));
        CHECK(rf_maxwell_solve(&p,&plasma,&loaded,error,sizeof error));
        CHECK(vacuum.complete && loaded.complete);
        CHECK(vacuum.backward_error<1e-8 && loaded.backward_error<1e-8);
        CHECK(loaded.absorbed_power>0. && loaded.power_balance_error<1e-6);
        worst_source=fmax(worst_source,source_error);
        worst_linear=fmax(worst_linear,fmax(vacuum.backward_error,loaded.backward_error));
        worst_power=fmax(worst_power,loaded.power_balance_error);
        if(csv)fprintf(csv,"%d,%s,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%zu\n",
            type,names[type],p.selected_m,p.kz,source_error,vacuum.backward_error,
            loaded.backward_error,loaded.power_balance_error,loaded.absorbed_power,loaded.nodes);
        rf_maxwell_free(&vacuum);rf_maxwell_free(&loaded);
    }
    if(csv)CHECK(!fclose(csv));
    /* At alpha=pi a double saddle has coincident legs. Its prescribed source
     * equals twice a 90-degree-rotated Nagoya, including finite bandwidth. */
    for(int m=-3;m<=3;++m) for(int ik=-1;ik<=1;++ik) {
        Project p=catalogue(RF_ANT_SADDLE);p.arc_angle_deg=180.;p.selected_m=m;p.kz=10.*ik;
        Geometry g={0};double complex actual[3],expected[3];
        CHECK(geometry_build(&p,&g,error,sizeof error));
        CHECK(geometry_current_spectrum(&p,&g,actual,error,sizeof error));
        Project q=p;q.type=RF_ANT_NAGOYA;q.azimuth_deg+=90.;q.current*=2.;reference(&q,expected);
        for(int j=0;j<3;++j)CHECK(cabs(actual[j]-expected[j])<1e-10);
        geometry_free(&g);
    }
    /* Rotating current distributions are separate from the selected Fourier m. */
    for(int type=RF_ANT_SEGMENTED_RINGS;type<=RF_ANT_BIRDCAGE;++type) {
        Project p=catalogue(type);p.current_pattern=1;p.source_mode=2;
        worst_source=fmax(worst_source,source_check(p));
    }
    if(argc>2)dump_saddle(argv[2]);
    if(argc>3) {
        FILE *f=fopen(argv[3],"w");CHECK(f);
        fputs("type,ax,ay,az,bx,by,bz,amplitude,phase,phase_advance,group\n",f);
        for(int type=RF_ANT_AXIAL_LOOPS;type<=RF_ANT_MEANDER;++type) {
            Project p=catalogue(type); Geometry g={0};
            CHECK(geometry_build(&p,&g,error,sizeof error));
            for(size_t i=0;i<g.segment_count;++i) {
                GeometrySegment *s=g.segments+i;
                fprintf(f,"%d,",type);
                for(int j=0;j<3;++j)fprintf(f,"%.17g,",s->a[j]);
                for(int j=0;j<3;++j)fprintf(f,"%.17g,",s->b[j]);
                fprintf(f,"%.17g,%.17g,%.17g,%d\n",s->amplitude,s->phase,s->phase_advance,s->group);
            }
            geometry_free(&g);
        }
        CHECK(!fclose(f));
    }
    if(argc>4) {
        FILE *f=fopen(argv[4],"w");CHECK(f);
        fputs("type,m,kz,Kr,Kphi,Kz\n",f);
        for(int type=RF_ANT_AXIAL_LOOPS;type<=RF_ANT_MEANDER;++type) {
            Project p=catalogue(type);Geometry g={0};
            CHECK(geometry_build(&p,&g,error,sizeof error));
            for(int j=0;j<=80;++j) {
                double complex k[3];p.kz=j-40.;
                CHECK(geometry_current_spectrum(&p,&g,k,error,sizeof error));
                fprintf(f,"%d,%d,%.17g,%.17g,%.17g,%.17g\n",type,p.selected_m,p.kz,cabs(k[0]),cabs(k[1]),cabs(k[2]));
            }
            geometry_free(&g);
        }
        CHECK(!fclose(f));
    }
    charge_checks();
    printf("ANTENNA PASS: 17 types, 1596 independent source checks, 34 vacuum/plasma solves\n");
    printf("maximum source error/I0 %.12g; backward error %.12g; power balance error %.12g\n",
        worst_source,worst_linear,worst_power);
    return 0;
}
