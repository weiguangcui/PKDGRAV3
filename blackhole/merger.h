#include "smooth/smooth.h"
#include "pkd.h"

#ifdef __cplusplus
extern "C" {
#endif
void smBHmerger(PARTICLE *p,float fBall,int nSmooth,NN *nnList,SMF *smf) ;
void combBHmerger(void *vpkd, void *p1,void *p2) ;
int smReSmoothBHNode(SMX smx,SMF *smf, int iSmoothType) ;
void buildCandidateMergerList(SMX smx, SMF *smf, KDN *node, BND bnd_node,
                              int *nCnt_tot, double r[3], double fBall2, int ix, int iy, int iz);
void pkdRepositionBH(PKD pkd);
#ifdef __cplusplus
}
#endif
