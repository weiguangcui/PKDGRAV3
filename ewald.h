#ifndef EWALD_HINCLUDED
#define EWALD_HINCLUDED

#include "pkd.h"

int pkdParticleEwald(PKD pkd,PARTICLE *p);
int pkdBucketEwald(PKD pkd,KDN *pkdn);
void pkdEwaldInit(PKD pkd,int nReps,double fEwCut,double fhCut);

#endif
