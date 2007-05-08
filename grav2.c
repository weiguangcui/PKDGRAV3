#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <stdlib.h>
#include <malloc.h>
#include <stddef.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include "pkd.h"
#include "moments.h"
#include "grav.h"

#define SQRT1(d2,dir)\
    {\
    dir = 1/sqrt(d2);\
    } 

#define CAcceptAngle 0.75

#define ECCFACMAX 10000

#define NMAXPLD 16

double softmassweight(double m1,double fourh12,double m2,double fourh22){
    return((m1+m2)*(fourh12*fourh22)/(fourh22*m1+fourh12*m2));
    }

void HEAPheapstruct(int n, int k, HEAPSTRUCT ra[]) {
    int l,j,ir,i;
    HEAPSTRUCT rra;

    l=(n>>1)+1;
    ir=n;

    while (1) {
	if (l > 1) {
	    rra = ra[--l-1];
	    }
	else {
	    rra = ra[ir-1];
	    ra[ir-1] = ra[0];
	    if (--ir == n-k) {
		ra[0] = rra;
		return;
		}
	    }
	i = l;
	j = (l<<1);
	while (j <= ir) {
	    if (j < ir && ra[j-1].rhoenc < ra[j].rhoenc) ++j; /* k largest elements at the end */
	    if (rra.rhoenc < ra[j-1].rhoenc) {
		ra[i-1] = ra[j-1];
		j += (i=j);
		}
	    else j = ir+1;
	    }
	ra[i-1] = rra;
	}
    }

void HEAPrholocal(int n, int k, RHOLOCAL ra[]) {
    int l,j,ir,i;
    RHOLOCAL rra;

    l=(n>>1)+1;
    ir=n;

    while (1) {
	if (l > 1) {
	    rra = ra[--l-1];
	    }
	else {
	    rra = ra[ir-1];
	    ra[ir-1] = ra[0];
	    if (--ir == n-k) {
		ra[0] = rra;
		return;
		}
	    }
	i = l;
	j = (l<<1);
	while (j <= ir) {
	    if (j < ir && ra[j-1].d2 > ra[j].d2) ++j; /* k smallest elements at the end */
	    if (rra.d2 > ra[j-1].d2) {
		ra[i-1] = ra[j-1];
		j += (i=j);
		}
	    else j = ir+1;
	    }
	ra[i-1] = rra;
	}
    }



/*
** This version of grav.c does all the operations inline, including 
** v_sqrt's and such.
** Returns nActive.
*/
int pkdGravInteract(PKD pkd,KDN *pBucket,LOCR *pLoc,ILP *ilp,int nPart,ILC *ilc,int nCell,ILPB *ilpb,int nPartBucket,double *pdFlop) {
    PARTICLE *p = pkd->pStore;
    PARTICLE *pi,*pj;
    KDN *pkdn = pBucket;
    const double onethird = 1.0/3.0;
    momFloat ax,ay,az,fPot;
    double x,y,z,d2,dir,dir2,g2,g3,g4;
    double xx,xy,xz,yy,yz,zz;
    double xxx,xxz,yyy,yyz,xxy,xyy,xyz;
    double tx,ty,tz;
    double fourh2;
    double rhocadd,rhocaddlocal,rholoc,rhopmax,rhopmaxlocal,costheta;
    double vx,vy,vz,v2,mu,Etot,L2,ecc,eccfac;
#ifdef SOFTSQUARE
    double ptwoh2;
#endif
#ifdef USE_SIMD_MOMR
    int nCellILC;
#endif
    int i,j,k,l,nN,nC,nSP,nSC,nSCmin,nSPB,nSPBmin,na,nia,nSoft,nActive;
    /*
    ** time-step lists
    */
    RHOENC *rhoenc;
    RHOLOCAL *rholocal;	
    HEAPSTRUCT *heapstruct;
    int *jmax;
    /*
    ** Determine particle numbers and sizes of time-step lists
    */
    nC = nCell+nPartBucket;                     /* total number of cells */
    nN = nPart+pkdn->pUpper-pkdn->pLower;       /* total number of neighbouring particles (without particle itself) */
    nSC  = (int) (0.005*nCell);                 /* estimate of number of cells to check for maximum */
    nSPB = (int) (0.005*nPartBucket);           /* estimate of number of particle-buckets to check for maximum */
    nSCmin  = (nCell < 2)?nCell:2;              /* minimum number of cells to check for maximum */
    nSPBmin = (nPartBucket < 1)?nPartBucket:1;  /* minimum number of particle-buckets to check for maximum */
    nSC  = (nSC < nSCmin)?nSCmin:nSC;           /* final number of cells to check for maximum */
    nSPB = (nSPB < nSPBmin)?nSPBmin:nSPB;       /* final number of particle-buckets to check for maximum */
    /*
    ** Allocate time-step lists
    */
    rhoenc = malloc(nC*sizeof(RHOENC));
    assert(rhoenc != NULL);
    rholocal = malloc(nN*sizeof(RHOLOCAL));
    assert(rholocal != NULL);
    heapstruct = malloc(nC*sizeof(HEAPSTRUCT));
    assert(heapstruct != NULL);
    jmax = malloc((nSC+nSPB)*sizeof(int));
    assert(jmax != NULL);

#ifdef USE_SIMD_MOMR
    nCellILC = nCell;
    momPadSIMDMomr( &nCellILC, ilc );
#endif


    /*
    ** Now process the two interaction lists for each active particle.
    */
    nActive = 0;
    nSoft = 0;
    for (i=pkdn->pLower;i<=pkdn->pUpper;++i) {
	if (!TYPEQueryACTIVE(&p[i])) continue;
	++nActive;
	fPot = 0;
	ax = 0;
	ay = 0;
	az = 0;
	rhocadd = 0;
	rholoc = 0;
	rhopmax = 0;

	/*
	** Evaluate local expansion.
	*/
	x = p[i].r[0] - pkdn->r[0];
	y = p[i].r[1] - pkdn->r[1];
	z = p[i].r[2] - pkdn->r[2];
	momEvalLocr(pLoc,x,y,z,&fPot,&ax,&ay,&az);

#ifdef USE_SIMD_MOMR
	momEvalSIMDMomr( nCellILC, ilc, p[i].r, &ax, &ay, &az, &fPot );
#else
	for (j=0;j<nCell;++j) {
	    x = p[i].r[0] - ilc[j].x;
	    y = p[i].r[1] - ilc[j].y;
	    z = p[i].r[2] - ilc[j].z;
	    d2 = x*x + y*y + z*z;
	    SQRT1(d2,dir);
	    dir2 = dir*dir;
	    g2 = 3*dir*dir2*dir2;
	    g3 = 5*g2*dir2;
	    g4 = 7*g3*dir2;
	    /*
	    ** Calculate the funky distance terms.
	    */
	    xx = 0.5*x*x;
	    xy = x*y;
	    xz = x*z;
	    yy = 0.5*y*y;
	    yz = y*z;
	    zz = 0.5*z*z;
	    xxx = x*(onethird*xx - zz);
	    xxz = z*(xx - onethird*zz);
	    yyy = y*(onethird*yy - zz);
	    yyz = z*(yy - onethird*zz);
	    xx -= zz;
	    yy -= zz;
	    xxy = y*xx;
	    xyy = x*yy;
	    xyz = xy*z;
	    /*
	    ** Now calculate the interaction up to Hexadecapole order.
	    */
	    tx = g4*(ilc[j].mom.xxxx*xxx + ilc[j].mom.xyyy*yyy + ilc[j].mom.xxxy*xxy + ilc[j].mom.xxxz*xxz + ilc[j].mom.xxyy*xyy + ilc[j].mom.xxyz*xyz + ilc[j].mom.xyyz*yyz);
	    ty = g4*(ilc[j].mom.xyyy*xyy + ilc[j].mom.xxxy*xxx + ilc[j].mom.yyyy*yyy + ilc[j].mom.yyyz*yyz + ilc[j].mom.xxyy*xxy + ilc[j].mom.xxyz*xxz + ilc[j].mom.xyyz*xyz);
	    tz = g4*(-ilc[j].mom.xxxx*xxz - (ilc[j].mom.xyyy + ilc[j].mom.xxxy)*xyz - ilc[j].mom.yyyy*yyz + ilc[j].mom.xxxz*xxx + ilc[j].mom.yyyz*yyy - ilc[j].mom.xxyy*(xxz + yyz) + ilc[j].mom.xxyz*xxy + ilc[j].mom.xyyz*xyy);
	    g4 = 0.25*(tx*x + ty*y + tz*z);
	    xxx = g3*(ilc[j].mom.xxx*xx + ilc[j].mom.xyy*yy + ilc[j].mom.xxy*xy + ilc[j].mom.xxz*xz + ilc[j].mom.xyz*yz);
	    xxy = g3*(ilc[j].mom.xyy*xy + ilc[j].mom.xxy*xx + ilc[j].mom.yyy*yy + ilc[j].mom.yyz*yz + ilc[j].mom.xyz*xz);
	    xxz = g3*(-(ilc[j].mom.xxx + ilc[j].mom.xyy)*xz - (ilc[j].mom.xxy + ilc[j].mom.yyy)*yz + ilc[j].mom.xxz*xx + ilc[j].mom.yyz*yy + ilc[j].mom.xyz*xy);
	    g3 = onethird*(xxx*x + xxy*y + xxz*z);
	    xx = g2*(ilc[j].mom.xx*x + ilc[j].mom.xy*y + ilc[j].mom.xz*z);
	    xy = g2*(ilc[j].mom.yy*y + ilc[j].mom.xy*x + ilc[j].mom.yz*z);
	    xz = g2*(-(ilc[j].mom.xx + ilc[j].mom.yy)*z + ilc[j].mom.xz*x + ilc[j].mom.yz*y);
	    g2 = 0.5*(xx*x + xy*y + xz*z);
	    rhoenc[j].index = j;
	    rhoenc[j].x = x;
	    rhoenc[j].y = y;
	    rhoenc[j].z = z;
	    rhoenc[j].dir = dir;
	    dir *= ilc[j].mom.m;
	    dir2 *= dir + 5*g2 + 7*g3 + 9*g4;
	    rhoenc[j].rhoenc = dir2;
	    fPot -= dir + g2 + g3 + g4;
	    ax += xx + xxx + tx - x*dir2;
	    ay += xy + xxy + ty - y*dir2;
	    az += xz + xxz + tz - z*dir2;
	    } /* end of cell list gravity loop */
#endif
	
	if(pkd->param.bGravStep) {
	    /*
	    ** Add particle-bucket stuff
	    */
	    for(j=nCell,k=0;j<nC;++j,++k) {
		rhoenc[j].index = j;
		rhoenc[j].x = p[i].r[0] - ilpb[k].x;
		rhoenc[j].y = p[i].r[1] - ilpb[k].y;
		rhoenc[j].z = p[i].r[2] - ilpb[k].z;
		d2 = rhoenc[j].x*rhoenc[j].x + rhoenc[j].y*rhoenc[j].y + rhoenc[j].z*rhoenc[j].z; 
#ifdef SOFTSQUARE
		ptwoh2 = 2*p[i].fSoft*p[i].fSoft;
		fourh2 = ptwoh2 + ilpb[k].twoh2;
#endif
#ifdef SOFTLINEAR
		fourh2 = p[i].fSoft + ilpb[k].h;
		fourh2 *= fourh2;
#endif		       
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE) 
		/* fourh2 = ilpb[k].fourh2; old softening */
		fourh2 = softmassweight(p[i].fMass,4*p[i].fSoft*p[i].fSoft,ilpb[k].m,ilpb[k].fourh2);
#endif
		if (d2 > fourh2) {
		    SQRT1(d2,dir);
		    dir2 = dir*dir*dir;
		    }
		else {
		    /*
		    ** This uses the Dehnen K1 kernel function now, it's fast!
		    */
		    SQRT1(fourh2,dir);
		    dir2 = dir*dir;
		    d2 *= dir2;
		    dir2 *= dir;
		    d2 = 1 - d2;
		    dir *= 1.0 + d2*(0.5 + d2*(3.0/8.0 + d2*(45.0/32.0)));
		    dir2 *= 1.0 + d2*(1.5 + d2*(135.0/16.0));
		    }
		rhoenc[j].dir = dir;
		rhoenc[j].rhoenc = ilpb[k].m*dir2;
		}
	    /*
	    ** Determine the nSC maximum cells in the cell list
	    */
	    if (nCell > 0) {
		for (j=0;j<nCell;++j) {
		    heapstruct[j].index = rhoenc[j].index;
		    heapstruct[j].rhoenc = rhoenc[j].rhoenc;
		    }
		HEAPheapstruct(nCell,nSC,heapstruct);
		for (j=0;j<nSC;++j) {
		    jmax[j] = heapstruct[nCell-1-j].index;
		    }
		}
	    /*
	    ** Determine the nSPB maximum cells in the particle-bucket list
	    */
	    if (nPartBucket > 0) {
		for (j=0;j<nPartBucket;++j) {
		    heapstruct[j].index = rhoenc[nCell+j].index;
		    heapstruct[j].rhoenc = rhoenc[nCell+j].rhoenc;
		    }
		HEAPheapstruct(nPartBucket,nSPB,heapstruct);
		for (j=0;j<nSPB;++j) {
		    jmax[nSC+j] = heapstruct[nPartBucket-1-j].index;
		    }
		}
	    /*
	    ** Determine the maximum of the rhocadd values
	    */
	    if (nC > 0) {
		for (j=0;j<(nSC+nSPB);++j) {
		    l = jmax[j];
		    rhocaddlocal = 0;
		    for (k=0;k<nC;++k) {
			costheta = (rhoenc[l].x*rhoenc[k].x + rhoenc[l].y*rhoenc[k].y + rhoenc[l].z*rhoenc[k].z)*rhoenc[l].dir*rhoenc[k].dir;
			if (costheta > CAcceptAngle && 2*rhoenc[k].dir > rhoenc[l].dir) rhocaddlocal += rhoenc[k].rhoenc;
			}
		    rhocadd = (rhocaddlocal > rhocadd)?rhocaddlocal:rhocadd;
		    }
		}
	    assert(rhocadd >= 0);
	    } /* end of cell & particle-bucket list time-step loop */
	
	mdlCacheCheck(pkd->mdl);
	
#ifdef SOFTSQUARE
	ptwoh2 = 2*p[i].fSoft*p[i].fSoft;
#endif
	for (j=0;j<nPart;++j) {
	    x = p[i].r[0] - ilp[j].x;
	    y = p[i].r[1] - ilp[j].y;
	    z = p[i].r[2] - ilp[j].z;
	    d2 = x*x + y*y + z*z;
#ifdef SOFTSQUARE
	    fourh2 = ptwoh2 + ilp[j].twoh2;
#endif
#ifdef SOFTLINEAR
	    fourh2 = p[i].fSoft + ilp[j].h;
	    fourh2 *= fourh2;
#endif		       
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE) 
	    /* fourh2 = ilp[j].fourh2; old softening */
	    fourh2 = softmassweight(p[i].fMass,4*p[i].fSoft*p[i].fSoft,ilp[j].m,ilp[j].fourh2);
#endif
	    rholocal[j].m = ilp[j].m;	
	    rholocal[j].d2 = d2;
	    if (d2 > fourh2) {
		SQRT1(d2,dir);
		dir2 = dir*dir*dir;
		}
	    else {
		/*
		** This uses the Dehnen K1 kernel function now, it's fast!
		*/
		SQRT1(fourh2,dir);
		dir2 = dir*dir;
		d2 *= dir2;
		dir2 *= dir;
		d2 = 1 - d2;
		dir *= 1.0 + d2*(0.5 + d2*(3.0/8.0 + d2*(45.0/32.0)));
		dir2 *= 1.0 + d2*(1.5 + d2*(135.0/16.0));
		++nSoft;
		}
	    /*
	    ** Eccentricity correction
	    */
	    if(pkd->param.bGravStep && pkd->param.iTimeStepCrit > 0 && 
	       (ilp[j].iOrder < pkd->param.nPColl || p[i].iOrder < pkd->param.nPColl)) {
		vx = p[i].v[0] - ilp[j].vx;
		vy = p[i].v[1] - ilp[j].vy;
		vz = p[i].v[2] - ilp[j].vz;
		v2 = vx*vx + vy*vy + vz*vz;
		mu = p[i].fMass*ilp[j].m/(p[i].fMass + ilp[j].m);
		Etot = 0.5*mu*v2 - p[i].fMass*ilp[j].m*dir;
		L2 = (y*vz - z*vy)*(y*vz - z*vy) + (z*vx - x*vz)*(z*vx - x*vz) + (x*vy - y*vx)*(x*vy - y*vx);
		L2 *= mu*mu;
		ecc = 1+2*Etot*L2/(mu*p[i].fMass*ilp[j].m*p[i].fMass*ilp[j].m);
		ecc = (ecc <= 0)?0:sqrt(ecc);
		rhopmaxlocal = (p[i].fMass+ilp[j].m)*dir2;
		eccfac = (2*ecc + 1)/fabs(1-ecc);
		eccfac = (eccfac > ECCFACMAX)?ECCFACMAX:eccfac;
		if(pkd->param.iTimeStepCrit == 2) eccfac = 1;
		rhopmaxlocal *= eccfac; 
		rhopmax = (rhopmaxlocal > rhopmax)?rhopmaxlocal:rhopmax;
/*  		printf("PP iOrder: %d iOrder %d mu: %g Etot: %g L2: %g ecc: %g eccfac: %g\n",p[i].iOrder,p[j].iOrder,mu,Etot,L2,ecc,eccfac); */
		}
	    dir2 *= ilp[j].m;
	    fPot -= ilp[j].m*dir;
	    ax -= x*dir2;
	    ay -= y*dir2;
	    az -= z*dir2;
	    } /* end of particle list gravity loop */
	p[i].fPot += fPot;
	p[i].a[0] += ax;
	p[i].a[1] += ay;
	p[i].a[2] += az;
	
	if(pkd->param.bGravStep) {
	    /*
	    ** Add bucket particles to the array rholocal as well!
	    ** Not including yourself!!
	    */
	    k = nPart;
	    for (j=pkdn->pLower;j<=pkdn->pUpper;++j) {
		if(p[i].iOrder == p[j].iOrder) continue;
		x = p[i].r[0] - p[j].r[0];
		y = p[i].r[1] - p[j].r[1];
		z = p[i].r[2] - p[j].r[2];
		d2 = x*x + y*y + z*z;
		rholocal[k].m = p[j].fMass;
		rholocal[k].d2 = d2;
		k += 1;
		}
	    /*
	    ** Add local density from particle list to mean field value.
	    ** Calculate local density only in the case of more than 1 neighbouring particle! 
	    */
	    if (nN > 1) {
		nSP = (nN < NMAXPLD)?nN:NMAXPLD;
		HEAPrholocal(nN,nSP,rholocal);
		SQRT1(rholocal[nN-nSP].d2,dir);
		dir *= dir;
		for (j=(nN - nSP);j<nN;++j) {
		    d2 = rholocal[j].d2*dir;
		    d2 =(1-d2)*(1-d2)*(1-d2)*(1-d2)*(1-d2);
		    rholoc += d2*rholocal[j].m; 
		    }
		rholoc = 9009/1024*M_1_PI*rholoc*dir*sqrt(dir); /* F5 Kernel */
		}
	    assert(rholoc >= 0);
	    rhocadd += rholoc;
	    p[i].dtGrav = (rhocadd > p[i].dtGrav)?rhocadd:p[i].dtGrav;
	    /*
	    ** Mean field value from cells including local density is now set.
	    ** Check if particle-particle interaction with nPColl particles (e.g. black holes) dominate!
	    */
	    if(pkd->param.iTimeStepCrit > 0) {
		p[i].dtGrav = (rhopmax > p[i].dtGrav)?rhopmax:p[i].dtGrav;
		}
	    } /* end of particle-particle list time-step loop */
	} /* end of i-loop cells & particles */
    /*
    ** Free time-step lists
    */ 
    free(rhoenc);
    free(rholocal);
    free(heapstruct);
    free(jmax);
    /*
    ** Perform intra-bucket interactions
    */
    na = 0;
    nia = 0;
    for (i=pkdn->pLower;i<=pkdn->pUpper;++i) {
	if (TYPEQueryACTIVE(&p[i])) pkd->piActive[na++] = &p[i];
	else pkd->piInactive[nia++] = &p[i];
	}
    /*
    ** Active-Active Interactions.
    */
    for (i=0;i<na-1;++i) {
	fPot = 0;
	ax = 0;
	ay = 0;
	az = 0;
	pi = pkd->piActive[i];
#ifdef SOFTSQUARE
	ptwoh2 = 2*pi->fSoft*pi->fSoft;
#endif
	for (j=i+1;j<na;++j) {
	    pj = pkd->piActive[j];
	    x = pi->r[0] - pj->r[0];
	    y = pi->r[1] - pj->r[1];
	    z = pi->r[2] - pj->r[2];
	    d2 = x*x + y*y + z*z;
#ifdef SOFTSQUARE
	    fourh2 = ptwoh2 + 2*pj->fSoft*pj->fSoft;
#endif
#ifdef SOFTLINEAR
	    fourh2 = pi->fSoft + pj->fSoft;
	    fourh2*= fourh2;
#endif		       
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE) 
	    /* fourh2 = 4*pj->fSoft*pj->fSoft; old softening */
	    fourh2 = softmassweight(pi->fMass,4*pi->fSoft*pi->fSoft,pj->fMass,4*pj->fSoft*pj->fSoft);
#endif
	    if (d2 > fourh2) {
		SQRT1(d2,dir);
		dir2 = dir*dir*dir;
		}
	    else {
		/*
		** This uses the Dehnen K1 kernel function now, it's fast!
		** (no more lookup tables)
		*/
		SQRT1(fourh2,dir);
		dir2 = dir*dir;
		d2 *= dir2;
		dir2 *= dir;
		d2 = 1 - d2;
		dir *= 1.0 + d2*(0.5 + d2*(3.0/8.0 + d2*(45.0/32.0)));
		dir2 *= 1.0 + d2*(1.5 + d2*(135.0/16.0));
		++nSoft;
		}
	    fPot += dir*pj->fMass;
	    ax += x*dir2*pj->fMass;
	    ay += y*dir2*pj->fMass;
	    az += z*dir2*pj->fMass;
	    pj->fPot -= dir*pi->fMass;
	    pj->a[0] += x*dir2*pi->fMass;
	    pj->a[1] += y*dir2*pi->fMass;
	    pj->a[2] += z*dir2*pi->fMass;
	    /*
	    ** Eccentricity correction
	    */
	    if(pkd->param.bGravStep && pkd->param.iTimeStepCrit > 0 && 
	       (pj->iOrder < pkd->param.nPColl || pi->iOrder < pkd->param.nPColl)) {
		vx = pi->v[0] - pj->v[0];
		vy = pi->v[1] - pj->v[1];
		vz = pi->v[2] - pj->v[2];
		v2 = vx*vx + vy*vy + vz*vz;
		mu = pi->fMass*pj->fMass/(pi->fMass + pj->fMass);
		Etot = 0.5*mu*v2 - pi->fMass*pj->fMass*dir;
		L2 = (y*vz - z*vy)*(y*vz - z*vy) + (z*vx - x*vz)*(z*vx - x*vz) + (x*vy - y*vx)*(x*vy - y*vx);
		L2 *= mu*mu;
		ecc = 1+2*Etot*L2/(mu*pi->fMass*pj->fMass*pi->fMass*pj->fMass);
		ecc = (ecc <= 0)?0:sqrt(ecc);
		rhopmaxlocal = (pi->fMass+pj->fMass)*dir2;
		eccfac = (2*ecc + 1)/fabs(1-ecc); /* scaling of error at pericentre */
		eccfac = (eccfac > ECCFACMAX)?ECCFACMAX:eccfac;
		if(pkd->param.iTimeStepCrit == 2) eccfac = 1;
		rhopmaxlocal *= eccfac; 
/*  		printf("Active-Active iOrderA: %d IOrderA: %d mu: %g Etot: %g L2: %g ecc: %g eccfac: %g\n",pi->iOrder,pj->iOrder,mu,Etot,L2,ecc,eccfac); */
		pi->dtGrav = (rhopmaxlocal > pi->dtGrav)?rhopmaxlocal:pi->dtGrav;
		pj->dtGrav = (rhopmaxlocal > pj->dtGrav)?rhopmaxlocal:pj->dtGrav; 
		}
	    } /* end of j-loop */
	pi->fPot -= fPot;
	pi->a[0] -= ax;
	pi->a[1] -= ay;
	pi->a[2] -= az;
	} /* end of i-loop active-active */
    /*
    ** Active-Inactive interactions.
    */
    for (i=0;i<na;++i) {
	fPot = 0;
	ax = 0;
	ay = 0;
	az = 0;
	pi = pkd->piActive[i];
#ifdef SOFTSQUARE
	ptwoh2 = 2*pi->fSoft*pi->fSoft;
#endif
	for (j=0;j<nia;++j) {
	    pj = pkd->piInactive[j];
	    x = pi->r[0] - pj->r[0];
	    y = pi->r[1] - pj->r[1];
	    z = pi->r[2] - pj->r[2];
	    d2 = x*x + y*y + z*z;
#ifdef SOFTSQUARE
	    fourh2 = ptwoh2 + 2*pj->fSoft*pj->fSoft;
#endif
#ifdef SOFTLINEAR
	    fourh2 = pi->fSoft + pj->fSoft;
	    fourh2*= fourh2;
#endif		       
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE) 
	    /* fourh2 = 4*pj->fSoft*pj->fSoft; old softening */
	    fourh2 = softmassweight(pi->fMass,4*pi->fSoft*pi->fSoft,pj->fMass,4*pj->fSoft*pj->fSoft);
#endif
	    if (d2 > fourh2) {
		SQRT1(d2,dir);
		dir2 = dir*dir*dir;
		}
	    else {
		/*
		** This uses the Dehnen K1 kernel function now, it's fast!
		** (no more lookup tables)
		*/
		SQRT1(fourh2,dir);
		dir2 = dir*dir;
		d2 *= dir2;
		dir2 *= dir;
		d2 = 1 - d2;
		dir *= 1.0 + d2*(0.5 + d2*(3.0/8.0 + d2*(45.0/32.0)));
		dir2 *= 1.0 + d2*(1.5 + d2*(135.0/16.0));
		++nSoft;
		}
	    fPot += dir*pj->fMass;
	    ax += x*dir2*pj->fMass;
	    ay += y*dir2*pj->fMass;
	    az += z*dir2*pj->fMass;
	    /*
	    ** Eccentricity correction
	    */
	    if(pkd->param.bGravStep && pkd->param.iTimeStepCrit > 0 && 
	       (pj->iOrder < pkd->param.nPColl || pi->iOrder < pkd->param.nPColl)) {
		vx = pi->v[0] - pj->v[0];
		vy = pi->v[1] - pj->v[1];
		vz = pi->v[2] - pj->v[2];
		v2 = vx*vx + vy*vy + vz*vz;
		mu = pi->fMass*pj->fMass/(pi->fMass + pj->fMass);
		Etot = 0.5*mu*v2 - pi->fMass*pj->fMass*dir;
		L2 = (y*vz - z*vy)*(y*vz - z*vy) + (z*vx - x*vz)*(z*vx - x*vz) + (x*vy - y*vx)*(x*vy - y*vx);
		L2 *= mu*mu;
		ecc = 1+2*Etot*L2/(mu*pi->fMass*pj->fMass*pi->fMass*pj->fMass);
		ecc = (ecc <= 0)?0:sqrt(ecc);
		rhopmaxlocal = (pi->fMass+pj->fMass)*dir2;
		eccfac = (2*ecc + 1)/fabs(1-ecc); /* scaling of error at pericentre */
		eccfac = (eccfac > ECCFACMAX)?ECCFACMAX:eccfac;
		if(pkd->param.iTimeStepCrit == 2) eccfac = 1;
		rhopmaxlocal *= eccfac; 
/*  		printf("Active-Inactive iOrderA: %d iOrderI: %d mu: %g Etot: %g L2: %g ecc: %g eccfac: %g\n",pi->iOrder,pj->iOrder,mu,Etot,L2,ecc,eccfac); */
		pi->dtGrav = (rhopmaxlocal > pi->dtGrav)?rhopmaxlocal:pi->dtGrav;
		}
	    } /* end of j-loop */
	pi->fPot -= fPot;
	pi->a[0] -= ax;
	pi->a[1] -= ay;
	pi->a[2] -= az;
	} /* end of i-loop active-inactive */
    *pdFlop += nActive*(nPart*40 + nCell*200) + nSoft*15;
    return(nActive);
    }
