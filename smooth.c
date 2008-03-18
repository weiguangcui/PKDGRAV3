#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <math.h>
#include <limits.h>
#include <assert.h>
#include "smooth.h"
#include "pkd.h"
#include "smoothfcn.h"

int smInitialize(SMX *psmx,PKD pkd,SMF *smf,int nSmooth,int bGasOnly,
		 int bPeriodic,int bSymmetric,int iSmoothType, 
		 int eParticleTypes,
		 double dfBall2OverSoft2 ) {
    SMX smx;
    void (*initParticle)(void *) = NULL;
    void (*init)(void *) = NULL;
    void (*comb)(void *,void *) = NULL;
    int pi;
    int nTree;
    int iTopDepth;

    smx = malloc(sizeof(struct smContext));
    assert(smx != NULL);
    smx->pkd = pkd;
    if (smf != NULL) smf->pkd = pkd;
    smx->nSmooth = nSmooth;
    smx->bGasOnly = bGasOnly;
    smx->bPeriodic = bPeriodic;
    smx->eParticleTypes = eParticleTypes;

    switch (iSmoothType) {
    case SMX_NULL:
	smx->fcnSmooth = NullSmooth;
	initParticle = NULL; /* Original Particle */
	init = NULL; /* Cached copies */
	comb = NULL;
	smx->fcnPost = NULL;
	break;
    case SMX_DENSITY:
	smx->fcnSmooth = bSymmetric?DensitySym:Density;
	initParticle = initDensity; /* Original Particle */
	init = initDensity; /* Cached copies */
	comb = combDensity;
	smx->fcnPost = NULL;
	break;
    case SMX_MARKDENSITY:
	smx->fcnSmooth = bSymmetric?MarkDensitySym:MarkDensity;
	initParticle = initParticleMarkDensity; /* Original Particle */
	init = initMarkDensity; /* Cached copies */
	comb = combMarkDensity;
	smx->fcnPost = NULL;
	break;
    case SMX_MARKIIDENSITY:
	smx->fcnSmooth = bSymmetric?MarkIIDensitySym:MarkIIDensity;
	initParticle = initParticleMarkIIDensity; /* Original Particle */
	init = initMarkIIDensity; /* Cached copies */
	comb = combMarkIIDensity;
	smx->fcnPost = NULL;
	break;
    case SMX_MARK:
	smx->fcnSmooth = NULL;
	initParticle = NULL;
	init = initMark;
	comb = combMark;
	smx->fcnPost = NULL;
	break;
    case SMX_FOF:
	assert(bSymmetric == 0);
	smx->fcnSmooth = NULL;
	initParticle = NULL;
	init = NULL;
	comb = NULL;
	smx->fcnPost = NULL;
	break;
#ifdef RELAXATION
    case SMX_RELAXATION:
	assert(bSymmetric == 0);
	smx->fcnSmooth = AddRelaxation;
	initParticle = NULL;
	init = NULL;
	comb = NULL;
	smx->fcnPost = NULL;
	break;
#endif /* RELAXATION */
#ifdef SYMBA
    case SMX_SYMBA:
	assert(bSymmetric == 0);
	smx->fcnSmooth = DrmininDrift;
	initParticle = NULL;
	init = NULL;
	comb = NULL;
	smx->fcnPost = NULL;
	break;
#endif /* SYMBA */

    default:
	assert(0);
	}
    /*
    ** Initialize the ACTIVE particles in the tree.
    ** There are other particles in the tree -- just not active.
    */
    nTree = pkd->kdNodes[ROOT].pUpper + 1;
    if (initParticle != NULL) {
	for (pi=0;pi<nTree;++pi) {
	    if (TYPETest(&(pkd->pStore[pi]),smx->eParticleTypes)) {
		initParticle(&pkd->pStore[pi]);
	    }
	}
    }
    /*
    ** Start particle caching space (cell cache is already active).
    */
    if (bSymmetric) {
	mdlCOcache(pkd->mdl,CID_PARTICLE,pkd->pStore,sizeof(PARTICLE),
		   nTree,init,comb);
	}
    else {
	mdlROcache(pkd->mdl,CID_PARTICLE,pkd->pStore,sizeof(PARTICLE),
		   nTree);
	}
    /*
    ** Allocate Nearest-Neighbor List.
    ** And also the associated list bRemote flags.
    */
    smx->nnListSize = 0;
    smx->nnListMax = NNLIST_INCREMENT;
    smx->nnList = malloc(smx->nnListMax*sizeof(NN));
    assert(smx->nnList != NULL);
    smx->nnbRemote = malloc(smx->nnListMax*sizeof(int));
    assert(smx->nnbRemote != NULL);
    /*
    ** Allocate priority queue.
    */
    smx->pq = malloc(nSmooth*sizeof(PQ));
    assert(smx->pq != NULL);
    PQ_INIT(smx->pq,nSmooth);
    /*
    ** Allocate special stacks for searching.
    ** There is a mistake here, since I use these stacks for the remote trees as well.
    ** This can be easily fixed, but a hack for now.
    */
    smx->S = malloc(1024*sizeof(int));
    assert(smx->S != NULL);
    smx->Smin = malloc(1024*sizeof(FLOAT));
    assert(smx->Smin != NULL);
    /*
    ** Allocate special stacks for searching within the top tree.
    ** Calculate the number of levels in the top tree. 
    */
    iTopDepth = 1+(int)ceil(log((double)smx->pkd->nThreads)/log(2.0));
    smx->ST = malloc(iTopDepth*sizeof(int));
    assert(smx->ST != NULL);
    smx->SminT = malloc(iTopDepth*sizeof(FLOAT));
    assert(smx->SminT != NULL);
    *psmx = smx;	
    return(1);
    }


void smFinish(SMX smx,SMF *smf)
    {
    PKD pkd = smx->pkd;
    int pi;
    char achOut[128];

    /*
     * Output statistics.
     */
    sprintf(achOut, "Cell Accesses: %g\n",
	    mdlNumAccess(smx->pkd->mdl,CID_CELL));
    mdlDiag(smx->pkd->mdl, achOut);
    sprintf(achOut, "    Miss ratio: %g\n",
	    mdlMissRatio(smx->pkd->mdl,CID_CELL));
    mdlDiag(smx->pkd->mdl, achOut);
    sprintf(achOut, "    Min ratio: %g\n",
	    mdlMinRatio(smx->pkd->mdl,CID_CELL));
    mdlDiag(smx->pkd->mdl, achOut);
    sprintf(achOut, "    Coll ratio: %g\n",
	    mdlCollRatio(smx->pkd->mdl,CID_CELL));
    mdlDiag(smx->pkd->mdl, achOut);
    sprintf(achOut, "Particle Accesses: %g\n",
	    mdlNumAccess(smx->pkd->mdl,CID_PARTICLE));
    mdlDiag(smx->pkd->mdl, achOut);
    sprintf(achOut, "    Miss ratio: %g\n",
	    mdlMissRatio(smx->pkd->mdl,CID_PARTICLE));
    mdlDiag(smx->pkd->mdl, achOut);
    sprintf(achOut, "    Min ratio: %g\n",
	    mdlMinRatio(smx->pkd->mdl,CID_PARTICLE));
    mdlDiag(smx->pkd->mdl, achOut);
    sprintf(achOut, "    Coll ratio: %g\n",
	    mdlCollRatio(smx->pkd->mdl,CID_PARTICLE));
    mdlDiag(smx->pkd->mdl, achOut);
    /*
    ** Stop particle caching space.
    */
    mdlFinishCache(smx->pkd->mdl,CID_PARTICLE);
    /*
    ** Now do any post calculations, these ususlly involve some sort of
    ** normalizations of the smoothed quantities, usually division by
    ** the local density! Do NOT put kernel normalizations in here as
    ** these do not depend purely on local properties in the case of
    ** "Gather-Scatter" kernel.
    */
    if (smx->fcnPost != NULL) {
	for (pi=0;pi<pkd->nLocal;++pi) {
	    if (TYPETest(&(pkd->pStore[pi]),smx->eParticleTypes)) {
		smx->fcnPost(&pkd->pStore[pi],smf);
		}
	    }
	}
    /*
    ** Free up context storage.
    */
    free(smx->S);
    free(smx->Smin);
    free(smx->ST);
    free(smx->SminT);
    free(smx->pq);
    free(smx->nnList);
    free(smx->nnbRemote);
    free(smx);
    }


/*
** This function performs a local nearest neighbor search.
** Note that this function cannot be called for a periodic
** replica of the local domain, that can be done with the
** pqSearchRemote function setting id == idSelf.
*/
PQ *pqSearchLocal(SMX smx,PARTICLE *pi,FLOAT r[3],int *pbDone) 
    {
    PARTICLE *p = smx->pkd->pStore;
    KDN *c = smx->pkd->kdNodes;
    PQ *pq;
    FLOAT dx,dy,dz,dMin,min1,min2,fDist2;
    FLOAT *Smin = smx->Smin;
    int *S = smx->S;
    int i,j,n,pj,pWant,pEnd,iCell,iParent;
    int sp = 0;
    int sm = 0;

    *pbDone = 1;	/* assume that we will complete the search */
    assert(smx->nQueue == 0);
    pq = smx->pq;
    /*
    ** Decide where the first containment test needs to
    ** be performed. If no particle is specfied then we
    ** don't perform containment tests except at the 
    ** root, so that the pbDone flag can be correctly
    ** set.
    */
    if (pi) {
	iCell = pi->iBucket;
	while (iCell != ROOT) {
#ifdef GASOLINE
	    if (smx->bGasOnly) n = c[iCell].nGas;
	    else n = c[iCell].pUpper - c[iCell].pLower + 1;
#else
	    n = c[iCell].pUpper - c[iCell].pLower + 1;
#endif
	    if (n < smx->nSmooth) iCell = c[iCell].iParent;
	    else break;
	    }
	S[sp] = iCell;
	iCell = pi->iBucket;
	}
    else {
	iCell = ROOT;
	S[sp] = iCell;
	}
    /*
    ** Start of PRIOQ Loading loop.
    */
    while (1) {
	/*
	** Descend to bucket via the closest cell at each level.
	*/
	while (c[iCell].iLower) {
	    iCell = c[iCell].iLower;
	    MINDIST(c[iCell].bnd,r,min1);
	    ++iCell;
	    MINDIST(c[iCell].bnd,r,min2);
	    if (min1 < min2) {
		Smin[sm++] = min2;
		--iCell;
		}
	    else {
		Smin[sm++] = min1;
		}
#ifdef GASOLINE
	    if (smx->bGasOnly && c[iCell].nGas == 0) goto LoadNotContained;
#endif
	    }
	pWant = c[iCell].pLower + smx->nSmooth - smx->nQueue - 1;
#ifdef GASOLINE
	if (smx->bGasOnly) pEnd = c[iCell].pLower + c[iCell].nGas - 1;
	else pEnd = c[iCell].pUpper;
#else
	pEnd = c[iCell].pUpper;
#endif
	if (pWant > pEnd) {
	    for (pj=c[iCell].pLower;pj<=pEnd;++pj) {
		dx = r[0] - p[pj].r[0];
		dy = r[1] - p[pj].r[1];
		dz = r[2] - p[pj].r[2];
		pq[smx->nQueue].pPart = &p[pj];
		pq[smx->nQueue].fDist2 = dx*dx + dy*dy + dz*dz;
		++smx->nQueue;
		}
	    }
	else {
	    for (pj=c[iCell].pLower;pj<=pWant;++pj) {
		dx = r[0] - p[pj].r[0];
		dy = r[1] - p[pj].r[1];
		dz = r[2] - p[pj].r[2];
		pq[smx->nQueue].pPart = &p[pj];
		pq[smx->nQueue].fDist2 = dx*dx + dy*dy + dz*dz;
		++smx->nQueue;
		}
	    PQ_BUILD(pq,smx->nSmooth,pq);
	    for (;pj<=pEnd;++pj) {
		dx = r[0] - p[pj].r[0];
		dy = r[1] - p[pj].r[1];
		dz = r[2] - p[pj].r[2];
		fDist2 = dx*dx + dy*dy + dz*dz;
		if (fDist2 < pq->fDist2) {
		    pq->pPart = &p[pj];
		    pq->fDist2 = fDist2;
		    PQ_REPLACE(pq);
		    }
		}
	    goto NoIntersect;  /* done loading phase */
	    }
#ifdef GASOLINE
    LoadNoIntersect:
#endif
	while (iCell == S[sp]) {
	    if (!sp) {
		*pbDone = 0;
		/*
		** Set dx,dy and dz and bRemote before leaving this
		** function.
		*/
		for (i=0;i<smx->nQueue;++i) {
		    pq[i].dx = r[0] - pq[i].pPart->r[0];
		    pq[i].dy = r[1] - pq[i].pPart->r[1];
		    pq[i].dz = r[2] - pq[i].pPart->r[2];
		    pq[i].bRemote = 0;
		    }
		return NULL;		/* EXIT, could not load enough particles! */
		}
	    --sp;
	    iCell = c[iCell].iParent;
	    }
#ifdef GASOLINE
    LoadNotContained:
#endif
	iCell ^= 1;
	if (sm) --sm;
#ifdef GASOLINE
	if (smx->bGasOnly && c[iCell].nGas == 0) {
	    iCell = c[iCell].iParent;
	    goto LoadNoIntersect;   			
	    }
#endif
	S[++sp] = iCell;
	}
    /*
    ** Start of PRIOQ searching loop.
    */
    while (1) {
	/*
	** Descend to bucket via the closest cell at each level.
	*/
	while (c[iCell].iLower) {
	    iCell = c[iCell].iLower;
#ifdef GASOLINE
	    if (smx->bGasOnly) {
		if (c[iCell].nGas == 0) min1 = pq->fDist2;
		else {
		    MINDIST(c[iCell].bnd,r,min1);
		    }
		++iCell;
		if (c[iCell].nGas == 0) min2 = pq->fDist2;
		else {
		    MINDIST(c[iCell].bnd,r,min2);
		    }
		}
	    else {
		MINDIST(c[iCell].bnd,r,min1);
		++iCell;
		MINDIST(c[iCell].bnd,r,min2);
		}
#else
	    MINDIST(c[iCell].bnd,r,min1);
	    ++iCell;
	    MINDIST(c[iCell].bnd,r,min2);
#endif
	    if (min1 < min2) {
		Smin[sm++] = min2;
		--iCell;
		if (min1 >= pq->fDist2) goto NotContained;
		}
	    else {
		Smin[sm++] = min1;
		if (min2 >= pq->fDist2) goto NotContained;
		}
	    }
#ifdef GASOLINE
	if (smx->bGasOnly) pEnd = c[iCell].pLower + c[iCell].nGas - 1;
	else pEnd = c[iCell].pUpper;
#else
	pEnd = c[iCell].pUpper;
#endif
	for (pj=c[iCell].pLower;pj<=pEnd;++pj) {
	    dx = r[0] - p[pj].r[0];
	    dy = r[1] - p[pj].r[1];
	    dz = r[2] - p[pj].r[2];
	    fDist2 = dx*dx + dy*dy + dz*dz;
	    if (fDist2 < pq->fDist2) {
		pq->pPart = &p[pj];
		pq->fDist2 = fDist2;
		PQ_REPLACE(pq);
		}
	    }
    NoIntersect:
	while (iCell == S[sp]) {
	    if (sp) {
		--sp;
		iCell = c[iCell].iParent;
		}
	    else {
		/*
		** Containment Test! 
		*/
		for (j=0;j<3;++j) {
		    dMin = c[iCell].bnd.fMax[j] - 
			fabs(c[iCell].bnd.fCenter[j] - r[j]);
		    if (dMin*dMin < pq->fDist2 || dMin < 0) {
			iParent = c[iCell].iParent;
			if (!iParent) {
			    *pbDone = 0;		/* EXIT, not contained! */
			    break;
			    }
			S[sp] = iParent;
			goto NotContained;
			}
		    }
		/*
		** Set dx,dy and dz and bRemote before leaving this
		** function.
		*/
		for (i=0;i<smx->nQueue;++i) {
		    smx->pq[i].dx = r[0] - smx->pq[i].pPart->r[0];
		    smx->pq[i].dy = r[1] - smx->pq[i].pPart->r[1];
		    smx->pq[i].dz = r[2] - smx->pq[i].pPart->r[2];
		    smx->pq[i].bRemote = 0;
		    }
		return pq;
		}
	    }
    NotContained:
	iCell ^= 1;		
	/*
	** Intersection Test. (ball-test)
	*/
	if (sm) min2 = Smin[--sm];
	else {
#ifdef GASOLINE
	    if (smx->bGasOnly && c[iCell].nGas == 0) {
		iCell = c[iCell].iParent;
		goto NoIntersect;
		}
#endif
	    MINDIST(c[iCell].bnd,r,min2);
	    }
	if (min2 >= pq->fDist2) {
	    iCell = c[iCell].iParent;
	    goto NoIntersect;
	    }
	S[++sp] = iCell;
	}
    }



PQ *pqSearchRemote(SMX smx,PQ *pq,int id,FLOAT r[3]) 
    {
    MDL mdl = smx->pkd->mdl;
    KDN *c = smx->pkd->kdNodes;
    PARTICLE *p;
    KDN *pkdn,*pkdu;
    FLOAT dx,dy,dz,min1,min2,fDist2;
    FLOAT *Smin = smx->Smin;
    int *S = smx->S;
    int pj,pWant,pEnd,iCell;
    int sp = 0;
    int sm = 0;
    int idSelf = smx->pkd->idSelf;

    iCell = ROOT;
    S[sp] = iCell;
    if (id == idSelf) pkdn = &c[iCell];
    else pkdn = mdlAquire(mdl,CID_CELL,iCell,id);
    if (smx->nQueue == smx->nSmooth) goto StartSearch;
    /*
    ** Start of PRIOQ Loading loop.
    */
    while (1) {
	/*
	** Descend to bucket via the closest cell at each level.
	*/
	while (pkdn->iLower) {
	    iCell = pkdn->iLower;
	    if (id == idSelf) pkdn = &c[iCell];
	    else {
		mdlRelease(mdl,CID_CELL,pkdn);
		pkdn = mdlAquire(mdl,CID_CELL,iCell,id);
		}
	    MINDIST(pkdn->bnd,r,min1);
	    ++iCell;
	    if (id == idSelf) pkdu = &c[iCell];
	    else pkdu = mdlAquire(mdl,CID_CELL,iCell,id);
	    MINDIST(pkdu->bnd,r,min2);
	    if (min1 < min2) {
		Smin[sm++] = min2;
		if (id != idSelf) mdlRelease(mdl,CID_CELL,pkdu);
		--iCell;
		}
	    else {
		Smin[sm++] = min1;
		if (id != idSelf) mdlRelease(mdl,CID_CELL,pkdn);
		pkdn = pkdu;
		}
#ifdef GASOLINE
	    if (smx->bGasOnly && pkdn->nGas == 0) goto LoadNotContained;
#endif
	    }
	pWant = pkdn->pLower + smx->nSmooth - smx->nQueue - 1;
#ifdef GASOLINE
	if (smx->bGasOnly) pEnd = pkdn->pLower + pkdn->nGas - 1;
	else pEnd = pkdn->pUpper;
#else
	pEnd = pkdn->pUpper;
#endif
	if (pWant > pEnd) {
	    for (pj=pkdn->pLower;pj<=pEnd;++pj) {
		if (id == idSelf) {
		    p = &smx->pkd->pStore[pj];
		    pq[smx->nQueue].bRemote = 0;
		    }
		else {
		    p = mdlAquire(mdl,CID_PARTICLE,pj,id);
		    pq[smx->nQueue].bRemote = 1;
		    }
		dx = r[0] - p->r[0];
		dy = r[1] - p->r[1];
		dz = r[2] - p->r[2];
		pq[smx->nQueue].pPart = p;
		pq[smx->nQueue].fDist2 = dx*dx + dy*dy + dz*dz;
		pq[smx->nQueue].dx = dx;
		pq[smx->nQueue].dy = dy;
		pq[smx->nQueue].dz = dz;
		++smx->nQueue;
		}
	    }
	else {
	    for (pj=pkdn->pLower;pj<=pWant;++pj) {
		if (id == idSelf) {
		    p = &smx->pkd->pStore[pj];
		    pq[smx->nQueue].bRemote = 0;
		    }
		else {
		    p = mdlAquire(mdl,CID_PARTICLE,pj,id);
		    pq[smx->nQueue].bRemote = 1;
		    }
		dx = r[0] - p->r[0];
		dy = r[1] - p->r[1];
		dz = r[2] - p->r[2];
		pq[smx->nQueue].pPart = p;
		pq[smx->nQueue].fDist2 = dx*dx + dy*dy + dz*dz;
		pq[smx->nQueue].dx = dx;
		pq[smx->nQueue].dy = dy;
		pq[smx->nQueue].dz = dz;
		++smx->nQueue;
		}
	    PQ_BUILD(pq,smx->nSmooth,pq);
	    for (;pj<=pEnd;++pj) {
		if (id == idSelf) p = &smx->pkd->pStore[pj];
		else p = mdlAquire(mdl,CID_PARTICLE,pj,id);
		dx = r[0] - p->r[0];
		dy = r[1] - p->r[1];
		dz = r[2] - p->r[2];
		fDist2 = dx*dx + dy*dy + dz*dz;
		if (fDist2 < pq->fDist2) {
		    if (pq->bRemote) mdlRelease(mdl,CID_PARTICLE,pq->pPart);
		    if (id == idSelf) pq->bRemote = 0;
		    else pq->bRemote = 1;
		    pq->pPart = p;
		    pq->fDist2 = fDist2;
		    pq->dx = dx;
		    pq->dy = dy;
		    pq->dz = dz;
		    PQ_REPLACE(pq);
		    }
		else if (id != idSelf) mdlRelease(mdl,CID_PARTICLE,p);
		}
	    goto NoIntersect;  /* done loading phase */
	    }
#ifdef GASOLINE
    LoadNoIntersect:
#endif
	while (iCell == S[sp]) {
	    if (!sp) {
		if (id != idSelf) mdlRelease(mdl,CID_CELL,pkdn);
		return NULL;		/* EXIT, could not load enough particles! */
		}
	    --sp;
	    iCell = pkdn->iParent;
	    if (id == idSelf) pkdn = &c[iCell];
	    else {
		mdlRelease(mdl,CID_CELL,pkdn);
		pkdn = mdlAquire(mdl,CID_CELL,iCell,id);
		}
	    }
#ifdef GASOLINE
    LoadNotContained:
#endif
	iCell ^= 1;
	if (id == idSelf) pkdn = &c[iCell];
	else {
	    mdlRelease(mdl,CID_CELL,pkdn);
	    pkdn = mdlAquire(mdl,CID_CELL,iCell,id);
	    }
	if (sm) --sm;
#ifdef GASOLINE
	if (smx->bGasOnly && pkdn->nGas == 0) {
	    iCell = pkdn->iParent;
	    if (id == idSelf) pkdn = &c[iCell];
	    else {
		mdlRelease(mdl,CID_CELL,pkdn);
		pkdn = mdlAquire(mdl,CID_CELL,iCell,id);
		}
	    goto LoadNoIntersect;   			
	    }
#endif
	S[++sp] = iCell;
	}
    StartSearch:
    /*
    ** Start of PRIOQ searching loop.
    */
    while (1) {
	/*
	** Descend to bucket via the closest cell at each level.
	*/
	while (pkdn->iLower) {
	    iCell = pkdn->iLower;
	    if (id == idSelf) pkdn = &c[iCell];
	    else {
		mdlRelease(mdl,CID_CELL,pkdn);
		pkdn = mdlAquire(mdl,CID_CELL,iCell,id);
		}
#ifdef GASOLINE
	    if (smx->bGasOnly) {
		if (pkdn->nGas == 0) min1 = pq->fDist2;
		else {
		    MINDIST(pkdn->bnd,r,min1);
		    }
		++iCell;
		if (id == idSelf) pkdu = &c[iCell];
		else pkdu = mdlAquire(mdl,CID_CELL,iCell,id);
		if (pkdu->nGas == 0) min2 = pq->fDist2;
		else {
		    MINDIST(pkdu->bnd,r,min2);
		    }
		}
	    else {
		MINDIST(pkdn->bnd,r,min1);
		++iCell;
		if (id == idSelf) pkdu = &c[iCell];
		else pkdu = mdlAquire(mdl,CID_CELL,iCell,id);
		MINDIST(pkdu->bnd,r,min2);
		}
#else
	    MINDIST(pkdn->bnd,r,min1);
	    ++iCell;
	    if (id == idSelf) pkdu = &c[iCell];
	    else pkdu = mdlAquire(mdl,CID_CELL,iCell,id);
	    MINDIST(pkdu->bnd,r,min2);
#endif
	    if (min1 < min2) {
		Smin[sm++] = min2;
		--iCell;
		if (id != idSelf) mdlRelease(mdl,CID_CELL,pkdu);
		if (min1 >= pq->fDist2) goto NotContained;
		}
	    else {
		Smin[sm++] = min1;
		if (id != idSelf) mdlRelease(mdl,CID_CELL,pkdn);
		pkdn = pkdu;
		if (min2 >= pq->fDist2) goto NotContained;
		}
	    }
#ifdef GASOLINE
	if (smx->bGasOnly) pEnd = pkdn->pLower + pkdn->nGas - 1;
	else pEnd = pkdn->pUpper;
#else
	pEnd = pkdn->pUpper;
#endif
	for (pj=pkdn->pLower;pj<=pEnd;++pj) {
	    if (id == idSelf) p = &smx->pkd->pStore[pj];
	    else p = mdlAquire(mdl,CID_PARTICLE,pj,id);
	    dx = r[0] - p->r[0];
	    dy = r[1] - p->r[1];
	    dz = r[2] - p->r[2];
	    fDist2 = dx*dx + dy*dy + dz*dz;
	    if (fDist2 < pq->fDist2) {
		if (pq->bRemote) mdlRelease(mdl,CID_PARTICLE,pq->pPart);
		if (id == idSelf) pq->bRemote = 0;
		else pq->bRemote = 1;
		pq->pPart = p;
		pq->fDist2 = fDist2;
		pq->dx = dx;
		pq->dy = dy;
		pq->dz = dz;
		PQ_REPLACE(pq);
		}
	    else if (id != idSelf) mdlRelease(mdl,CID_PARTICLE,p);
	    }
    NoIntersect:
	while (iCell == S[sp]) {
	    if (!sp) {
		if (id != idSelf) mdlRelease(mdl,CID_CELL,pkdn);
		return pq;
		}
	    --sp;
	    iCell = pkdn->iParent;
	    if (id == idSelf) pkdn = &c[iCell];
	    else {
		mdlRelease(mdl,CID_CELL,pkdn);
		pkdn = mdlAquire(mdl,CID_CELL,iCell,id);
		}
	    }
    NotContained:
	iCell ^= 1;		
	if (id == idSelf) pkdn = &c[iCell];
	else {
	    mdlRelease(mdl,CID_CELL,pkdn);
	    pkdn = mdlAquire(mdl,CID_CELL,iCell,id);
	    }
	/*
	** Intersection Test. (ball-test)
	*/
	if (sm) min2 = Smin[--sm];
	else {
#ifdef GASOLINE
	    if (smx->bGasOnly && pkdn->nGas == 0) {
		iCell = pkdn->iParent;
		if (id == idSelf) pkdn = &c[iCell];
		else {
		    mdlRelease(mdl,CID_CELL,pkdn);
		    pkdn = mdlAquire(mdl,CID_CELL,iCell,id);
		    }
		goto NoIntersect;
		}
#endif
	    MINDIST(pkdn->bnd,r,min2);
	    }
	if (min2 >= pq->fDist2) {
	    iCell = pkdn->iParent;
	    if (id == idSelf) pkdn = &c[iCell];
	    else {
		mdlRelease(mdl,CID_CELL,pkdn);
		pkdn = mdlAquire(mdl,CID_CELL,iCell,id);
		}
	    goto NoIntersect;
	    }
	S[++sp] = iCell;
	}
    }


PQ *pqSearch(SMX smx,PQ *pq,PARTICLE *pi,FLOAT r[3],int bReplica,int *pbDone) {
    KDN *c = smx->pkd->kdTop;
    int idSelf = smx->pkd->idSelf;
    FLOAT *Smin = smx->SminT;
    int *S = smx->ST;
    FLOAT dMin,min1,min2;
    int j,iCell,id,iParent;
    int sp = 0;
    int sm = 0;

    *pbDone = 0;
    if (bReplica) iCell = ROOT;
    else {
	iCell = smx->pkd->iTopRoot;
	assert(c[iCell].pLower == idSelf);
	}
    if (iCell != ROOT) S[sp] = c[iCell].iParent;
    else S[sp] = iCell;	
    if (smx->nQueue == smx->nSmooth) goto StartSearch;
    /*
    ** Start of PRIOQ Loading loop.
    */
    while (1) {
	/*
	** Descend to bucket via the closest cell at each level.
	*/
	while (c[iCell].iLower) {
	    iCell = c[iCell].iLower;
	    MINDIST(c[iCell].bnd,r,min1);
	    ++iCell;
	    MINDIST(c[iCell].bnd,r,min2);
	    if (min1 < min2) {
		Smin[sm++] = min2;
		--iCell;
		}
	    else {
		Smin[sm++] = min1;
		}
#ifdef GASOLINE
	    if (smx->bGasOnly && c[iCell].nGas == 0) goto LoadNotContained;
#endif
	    }
	id = c[iCell].pLower;	/* this is the thread id in LTT */
	if (bReplica || id != idSelf) {
	    pq = pqSearchRemote(smx,pq,id,r);
	    }
	else {
	    pq = pqSearchLocal(smx,pi,r,pbDone);
	    if (*pbDone) return pq;	/* early exit */
	    }
	if (smx->nQueue == smx->nSmooth) goto NoIntersect;  /* done loading phase */
#ifdef GASOLINE
    LoadNoIntersect:
#endif
	while (iCell == S[sp]) {
	    if (!sp) {
		return NULL;		/* EXIT, could not load enough particles! */
		}
	    --sp;
	    iCell = c[iCell].iParent;
	    }
#ifdef GASOLINE
    LoadNotContained:
#endif
	iCell ^= 1;
	if (sm) --sm;
#ifdef GASOLINE
	if (smx->bGasOnly && c[iCell].nGas == 0) {
	    iCell = c[iCell].iParent;
	    goto LoadNoIntersect;   			
	    }
#endif
	S[++sp] = iCell;
	}
    /*
    ** Start of PRIOQ searching loop.
    */
    StartSearch:
    while (1) {
	/*
	** Descend to bucket via the closest cell at each level.
	*/
	while (c[iCell].iLower) {
	    iCell = c[iCell].iLower;
#ifdef GASOLINE
	    if (smx->bGasOnly) {
		if (c[iCell].nGas == 0) min1 = pq->fDist2;
		else {
		    MINDIST(c[iCell].bnd,r,min1);
		    }
		++iCell;
		if (c[iCell].nGas == 0) min2 = pq->fDist2;
		else {
		    MINDIST(c[iCell].bnd,r,min2);
		    }
		}
	    else {
		MINDIST(c[iCell].bnd,r,min1);
		++iCell;
		MINDIST(c[iCell].bnd,r,min2);
		}
#else
	    MINDIST(c[iCell].bnd,r,min1);
	    ++iCell;
	    MINDIST(c[iCell].bnd,r,min2);
#endif
	    if (min1 < min2) {
		Smin[sm++] = min2;
		--iCell;
		if (min1 >= pq->fDist2) goto NotContained;
		}
	    else {
		Smin[sm++] = min1;
		if (min2 >= pq->fDist2) goto NotContained;
		}
	    }
	id = c[iCell].pLower;	/* this is the thread id in LTT */
	pq = pqSearchRemote(smx,pq,id,r);
    NoIntersect:
	while (iCell == S[sp]) {
	    if (sp) {
		--sp;
		iCell = c[iCell].iParent;
		}
	    else if (!bReplica) {
		/*
		** Containment Test! 
		*/
		for (j=0;j<3;++j) {
		    dMin = c[iCell].bnd.fMax[j] - 
			fabs(c[iCell].bnd.fCenter[j] - r[j]);
		    if (dMin*dMin < pq->fDist2 || dMin < 0) {
			iParent = c[iCell].iParent;
			if (!iParent) {
			    *pbDone = 0;
			    return pq;
			    }
			S[sp] = iParent;
			goto NotContained;
			}
		    }
		*pbDone = 1;
		return pq;
		}
	    else return pq;
	    }
    NotContained:
	iCell ^= 1;		
	/*
	** Intersection Test. (ball-test)
	*/
	if (sm) min2 = Smin[--sm];
	else {
#ifdef GASOLINE
	    if (smx->bGasOnly && c[iCell].nGas == 0) {
		iCell = c[iCell].iParent;
		goto NoIntersect;
		}
#endif
	    MINDIST(c[iCell].bnd,r,min2);
	    }
	if (min2 >= pq->fDist2) {
	    iCell = c[iCell].iParent;
	    goto NoIntersect;
	    }
	S[++sp] = iCell;
	}
    }


void smSmooth(SMX smx,SMF *smf)
    {
    PKD pkd = smx->pkd;
    PARTICLE *p = pkd->pStore;
    PQ *pq;
    FLOAT r[3],fBall;
    int iStart[3],iEnd[3];
    int pi,i,j,bDone;
    int ix,iy,iz;
   
    for (pi=0;pi<pkd->nLocal;++pi) {
	if (!TYPETest(&(p[pi]),smx->eParticleTypes)) continue;
	pq = NULL;
	smx->nQueue = 0;
	pq = pqSearch(smx,pq,&p[pi],p[pi].r,0,&bDone);
	/*
	** Search in replica boxes if it is required.
	*/
	if (!bDone && smx->bPeriodic) {
	    /*
	    ** Note for implementing SLIDING PATCH, the offsets for particles are
	    ** negative here, reflecting the relative +ve offset of the simulation
	    ** volume.
	    */
	    fBall = sqrt(pq->fDist2);
	    for (j=0;j<3;++j) {
		iStart[j] = floor((p[pi].r[j] - fBall)/pkd->fPeriod[j] + 0.5);
		iEnd[j] = floor((p[pi].r[j] + fBall)/pkd->fPeriod[j] + 0.5);
		}
	    for (ix=iStart[0];ix<=iEnd[0];++ix) {
		r[0] = p[pi].r[0] - ix*pkd->fPeriod[0];
		for (iy=iStart[1];iy<=iEnd[1];++iy) {
		    r[1] = p[pi].r[1] - iy*pkd->fPeriod[1];
		    for (iz=iStart[2];iz<=iEnd[2];++iz) {
			r[2] = p[pi].r[2] - iz*pkd->fPeriod[2];
			if (ix || iy || iz) {
			    pq = pqSearch(smx,pq,&p[pi],r,1,&bDone);
			    }
			}
		    }	
		}
	    }
	/*
	** Should we ever get tripped by this assert, it means that 
	** for some reason there are less particles in the box (and 
	** replicas) of the desired type than are requested by 
	** nSmooth! We die on this condition at the moment, but maybe
	** there are sensible cases to be dealt with here.
	*/
	assert(pq != NULL);
	/*
	** Maybe should give a warning if the search radius is not conained
	** within the replica volume.
	*/
	
	p[pi].fBall = sqrt(pq->fDist2);
	for (i=0;i<smx->nSmooth;++i) {
	    smx->nnList[i].pPart = smx->pq[i].pPart;
	    smx->nnList[i].fDist2 = smx->pq[i].fDist2;			
	    smx->nnList[i].dx = smx->pq[i].dx;
	    smx->nnList[i].dy = smx->pq[i].dy;
	    smx->nnList[i].dz = smx->pq[i].dz;
	    }
	
	/*
	** Apply smooth funtion to the neighbor list.
	*/
	smx->fcnSmooth(&p[pi],smx->nSmooth,smx->nnList,smf);
	/*
	** Call mdlCacheCheck to make sure we are making progress!
	*/
	mdlCacheCheck(pkd->mdl);
	/*
	** Release aquired pointers.
	*/
	for (i=0;i<smx->nSmooth;++i) {
	    if (smx->pq[i].bRemote) {
		mdlRelease(pkd->mdl,CID_PARTICLE,smx->pq[i].pPart);
		}
	    }
	}
    }


void smGatherLocal(SMX smx,FLOAT fBall2,FLOAT r[3])
    {
    KDN *c = smx->pkd->kdNodes;
    PARTICLE *p = smx->pkd->pStore;
    FLOAT min2,dx,dy,dz,fDist2;
    int *S = smx->S;
    int sp = 0;
    int iCell,pj,nCnt,pEnd;
    int idSelf;

    idSelf = smx->pkd->idSelf;
    nCnt = smx->nnListSize;
    iCell = ROOT;
    while (1) {
#ifdef GASOLINE
	if (smx->bGasOnly && c[iCell].nGas == 0) {
	    goto NoIntersect;
	    }
#endif
	MINDIST(c[iCell].bnd,r,min2);
	if (min2 > fBall2) {
	    goto NoIntersect;
	    }
	/*
	** We have an intersection to test.
	*/
	if (c[iCell].iLower) {
	    iCell = c[iCell].iLower;
	    S[sp++] = iCell+1;
	    continue;
	    }
	else {
#ifdef GASOLINE
	    if (smx->bGasOnly) pEnd = c[iCell].pLower + c[iCell].nGas - 1;
	    else pEnd = c[iCell].pUpper;
#else
	    pEnd = c[iCell].pUpper;
#endif
	    for (pj=c[iCell].pLower;pj<=pEnd;++pj) {
		dx = r[0] - p[pj].r[0];
		dy = r[1] - p[pj].r[1];
		dz = r[2] - p[pj].r[2];
		fDist2 = dx*dx + dy*dy + dz*dz;
		if (fDist2 <= fBall2) {
		    if(nCnt >= smx->nnListMax) {
			smx->nnListMax += NNLIST_INCREMENT;
			smx->nnList = realloc(smx->nnList,smx->nnListMax*sizeof(NN));
			assert(smx->nnList != NULL);
			smx->nnbRemote = realloc(smx->nnbRemote,smx->nnListMax*sizeof(int));
			assert(smx->nnbRemote != NULL);
			}
		    smx->nnList[nCnt].fDist2 = fDist2;
		    smx->nnList[nCnt].dx = dx;
		    smx->nnList[nCnt].dy = dy;
		    smx->nnList[nCnt].dz = dz;
		    smx->nnList[nCnt].pPart = &p[pj];
		    smx->nnList[nCnt].iPid = idSelf;
		    smx->nnList[nCnt].iIndex = pj;
		    smx->nnbRemote[nCnt] = 0;
		    ++nCnt;
		    }
		}
	    }
    NoIntersect:
	if (sp) iCell = S[--sp];
	else break;
	}
    smx->nnListSize = nCnt;
    }


void smGatherRemote(SMX smx,FLOAT fBall2,FLOAT r[3],int id)
    {
    MDL mdl = smx->pkd->mdl;
    KDN *pkdn;
    PARTICLE *pp;
    FLOAT min2,dx,dy,dz,fDist2;
    int *S = smx->S;
    int sp = 0;
    int pj,nCnt,pEnd;
    int iCell;

    assert(id != smx->pkd->idSelf);
    nCnt = smx->nnListSize;
    iCell = ROOT;
    pkdn = mdlAquire(mdl,CID_CELL,iCell,id);
    while (1) {
#ifdef GASOLINE
	if (smx->bGasOnly && pkdn->nGas == 0) {
	    goto NoIntersect;
	    }
#endif
	MINDIST(pkdn->bnd,r,min2);
	if (min2 > fBall2) {
	    goto NoIntersect;
	    }
	/*
	** We have an intersection to test.
	*/
	if (pkdn->iLower) {
	    iCell = pkdn->iLower;
	    S[sp++] = iCell+1;
	    mdlRelease(mdl,CID_CELL,pkdn);
	    pkdn = mdlAquire(mdl,CID_CELL,iCell,id);
	    continue;
	    }
	else {
#ifdef GASOLINE
	    if (smx->bGasOnly) pEnd = pkdn->pLower + pkdn->nGas - 1;
	    else pEnd = pkdn->pUpper;
#else
	    pEnd = pkdn->pUpper;
#endif
	    for (pj=pkdn->pLower;pj<=pEnd;++pj) {
		pp = mdlAquire(mdl,CID_PARTICLE,pj,id);
		dx = r[0] - pp->r[0];
		dy = r[1] - pp->r[1];
		dz = r[2] - pp->r[2];
		fDist2 = dx*dx + dy*dy + dz*dz;
		if (fDist2 <= fBall2) {
		    if(nCnt >= smx->nnListMax) {
			smx->nnListMax += NNLIST_INCREMENT;
			smx->nnList = realloc(smx->nnList,smx->nnListMax*sizeof(NN));
			assert(smx->nnList != NULL);
			smx->nnbRemote = realloc(smx->nnbRemote,smx->nnListMax*sizeof(int));
			assert(smx->nnbRemote != NULL);
			}
		    smx->nnList[nCnt].fDist2 = fDist2;
		    smx->nnList[nCnt].dx = dx;
		    smx->nnList[nCnt].dy = dy;
		    smx->nnList[nCnt].dz = dz;
		    smx->nnList[nCnt].pPart = pp;
		    smx->nnList[nCnt].iPid = id;
		    smx->nnList[nCnt].iIndex = pj;
		    smx->nnbRemote[nCnt] = 1;
		    ++nCnt;
		    }
		}
	    }
    NoIntersect:
	if (sp) {
	    iCell = S[--sp];
	    mdlRelease(mdl,CID_CELL,pkdn);
	    pkdn = mdlAquire(mdl,CID_CELL,iCell,id);
	    }
	else break;
	}
    mdlRelease(mdl,CID_CELL,pkdn);
    smx->nnListSize = nCnt;
    }


void smGather(SMX smx,FLOAT fBall2,FLOAT r[3])
    {
    KDN *c = smx->pkd->kdTop;
    int idSelf = smx->pkd->idSelf;
    int *S = smx->ST;
    FLOAT min2;
    int iCell,id;
    int sp = 0;

    iCell = ROOT;
    while (1) {
#ifdef GASOLINE
	if (smx->bGasOnly && c[iCell].nGas == 0) {
	    goto NoIntersect;
	    }
#endif
	MINDIST(c[iCell].bnd,r,min2);
	if (min2 > fBall2) {
	    goto NoIntersect;
	    }
	/*
	** We have an intersection to test.
	*/
	if (c[iCell].iLower) {
	    iCell = c[iCell].iLower;
	    S[sp++] = iCell+1;
	    continue;
	    }
	else {
	    id = c[iCell].pLower; /* this is the thread id in LTT */
	    if (id != idSelf) {
		smGatherRemote(smx,fBall2,r,id);
		}
	    else {
		smGatherLocal(smx,fBall2,r);
		}
	    }
    NoIntersect:
	if (sp) iCell = S[--sp];
	else break;
	}
    }


void smReSmooth(SMX smx,SMF *smf)
    {
    PKD pkd = smx->pkd;
    PARTICLE *p = pkd->pStore;
    FLOAT r[3],fBall;
    int iStart[3],iEnd[3];
    int pi,i,j;
    int ix,iy,iz;

    for (pi=0;pi<pkd->nLocal;++pi) {
	if (!TYPETest(&(p[pi]),smx->eParticleTypes)) continue;
	smx->nnListSize = 0;
	/*
	** Note for implementing SLIDING PATCH, the offsets for particles are
	** negative here, reflecting the relative +ve offset of the simulation
	** volume.
	*/
	fBall = p[pi].fBall;
	if (smx->bPeriodic) {
	    for (j=0;j<3;++j) {
		iStart[j] = floor((p[pi].r[j] - fBall)/pkd->fPeriod[j] + 0.5);
		iEnd[j] = floor((p[pi].r[j] + fBall)/pkd->fPeriod[j] + 0.5);
		}
	    for (ix=iStart[0];ix<=iEnd[0];++ix) {
		r[0] = p[pi].r[0] - ix*pkd->fPeriod[0];
		for (iy=iStart[1];iy<=iEnd[1];++iy) {
		    r[1] = p[pi].r[1] - iy*pkd->fPeriod[1];
		    for (iz=iStart[2];iz<=iEnd[2];++iz) {
			r[2] = p[pi].r[2] - iz*pkd->fPeriod[2];
			smGather(smx,fBall*fBall,r);
			}
		    }
		}
	    }
	else {
	    smGather(smx,fBall*fBall,p[pi].r);
	    }
	/*
	** Apply smooth funtion to the neighbor list.
	*/
	smx->fcnSmooth(&p[pi],smx->nnListSize,smx->nnList,smf);
	/*
	** Release aquired pointers.
	*/
	for (i=0;i<smx->nnListSize;++i) {
	    if (smx->nnbRemote[i]) {
		mdlRelease(pkd->mdl,CID_PARTICLE,smx->nnList[i].pPart);
		}
	    }
	}
    }


void smFof(SMX smx,int nFOFsDone,SMF *smf)
{	
 
  PKD pkd = smx->pkd;
  MDL mdl = smx->pkd->mdl;
  PARTICLE *p = smx->pkd->pStore;
  FOFRM* rm;
  FOFPG* protoGroup;		
  FOFBIN *bin;

  int pi,pn,pnn,nCnt,i,j,k,nRmCnt, iRmIndex,nRmListSize;
  int nFifo, iHead, iTail, iMaxGroups, iGroup;
  int *Fifo;
  int iStart[3],iEnd[3];
  int ix,iy,iz;

  FLOAT r[3],l[3],relpos[3],lx,ly,lz,fBall,fBall2Max,rho;
  FLOAT fMass;
  int nTree,cnt,tmp;
  cnt = 0;
  if (smx->bPeriodic) {
    lx = pkd->fPeriod[0];
    ly = pkd->fPeriod[1];
    lz = pkd->fPeriod[2];
  }
  else {
    lx = FLOAT_MAXVAL;
    ly = FLOAT_MAXVAL;
    lz = FLOAT_MAXVAL;
  }
  l[0] = lx;
  l[1] = ly;
  l[2] = lz;
	
  iHead = 0;
  iTail = 0;
  tmp = pkd->nDark+pkd->nGas+pkd->nStar;

  iGroup = 0; 
  nTree = pkd->kdNodes[ROOT].pUpper + 1;
  iMaxGroups = nTree+1;
  nRmListSize = nTree;
  nFifo = nTree;
  Fifo = (int *)malloc(nFifo*sizeof(int));
  assert(Fifo != NULL);
 
  protoGroup = (FOFPG *)malloc(iMaxGroups*sizeof(FOFPG));
  /*used something smaller than FOFGD here to reduce memory usage*/
  assert(protoGroup != NULL);

  rm = (FOFRM *)malloc(nRmListSize*sizeof(FOFRM));
  assert(rm != NULL);
 
  iRmIndex = 0;
  pkd->nGroups = 0;
  pkd->nMaxRm = 0;
  fBall2Max = 0.0;

  if( nFOFsDone > 0){
    mdlROcache(mdl,CID_BIN,pkd->groupBin,sizeof(FOFBIN),pkd->nBins);
    if(pkd->idSelf != 0){
      for(i=0; i< pkd->nBins; i++){
	bin = mdlAquire(mdl,CID_BIN,i,0);
	mdlRelease(mdl,CID_BIN,bin);
	pkd->groupBin[i] = *bin;	
      }
    }	
    mdlFinishCache(mdl,CID_BIN);
    for (pn=0;pn<nTree;pn++) {
#ifdef PARTICLE_HAS_MASS
      fMass = p[pn].fMass;
#else
      fMass = pkd->pClass[p[pn].iClass].fMass;
#endif
      if( p[pn].pBin >= 0 ){
	for(j = 0; j < 3; j++)	{
	  relpos[j] = corrPos(pkd->groupBin[p[pn].pBin].com[j], p[pn].r[j], l[j]) 
	    - pkd->groupBin[p[pn].pBin].com[j];
	}
	rho = pkd->groupBin[p[pn].pBin].fDensity;
	if(rho > p[pn].fDensity) rho =  p[pn].fDensity;
	p[pn].fBall = pow(fMass/(rho*smf->fContrast),2.0/3.0);			
	/* set velocity linking lenght in case of a phase space FOF */
	p[pn].fBallv2 = pkd->groupBin[p[pn].pBin].v2[0]+ 
	  pkd->groupBin[p[pn].pBin].v2[1]+ pkd->groupBin[p[pn].pBin].v2[2];
	p[pn].fBallv2 *= 2.0;
	p[pn].fBallv2 *= pow(smf->fContrast,-2.0/3.0);
	if(p[pn].fBall > smf->dTau2*pow(fMass/ smf->fContrast,2.0/3.0) )
	  p[pn].fBall = smf->dTau2*pow(fMass/ smf->fContrast,2.0/3.0);
      } else {
	p[pn].fBall = 0.0;
      }
      if(p[pn].fBall > fBall2Max)fBall2Max = p[pn].fBall;
      p[pn].pGroup = 0;
    }	
  } else {
    for (pn=0;pn<nTree;pn++) {
#ifdef PARTICLE_HAS_MASS
      fMass = p[pn].fMass;
#else
      fMass = pkd->pClass[p[pn].iClass].fMass;
#endif
      p[pn].pBin = p[pn].pGroup; /* temp. store old groupIDs for doing the links*/
      p[pn].pGroup = 0;
      if(smf->bTauAbs) {
	p[pn].fBall = smf->dTau2;
	if(smf->dTau2 > pow(fMass/smf->Delta,0.6666) ) /*enforces at least virial density for linking*/
	  p[pn].fBall = pow(fMass/smf->Delta,0.6666);
	p[pn].fBallv2 = smf->dVTau2;
      } else {
	p[pn].fBall = smf->dTau2*pow(fMass,0.6666);
	p[pn].fBallv2 = -1.0; /* No phase space FOF in this case */
      }
      if(p[pn].fBall > fBall2Max)fBall2Max = p[pn].fBall;
    }	
    /* Have to restart particle chache, since we will need 
     * the updated p[pn].fBall now */
    mdlFinishCache(mdl,CID_PARTICLE);
    mdlROcache(mdl,CID_PARTICLE,p,sizeof(PARTICLE),nTree);	
  }
  
  /* Starting FOF search now... */
  for (pn=0;pn<nTree;pn++) {
    if (p[pn].pGroup ) continue;
    iGroup++;
    assert(iGroup < iMaxGroups);
    protoGroup[iGroup].nMembers = 0;
    protoGroup[iGroup].iId = iGroup;		
    nRmCnt = 0;	
    /*
    ** Mark particle and add it to the do-fifo
    */
    p[pn].pGroup = iGroup;
    Fifo[iTail] = pn; iTail++;
    if(iTail == nFifo) iTail = 0;
    while(iHead != iTail) {
      pi = Fifo[iHead];iHead++;
      if(iHead == nFifo) iHead=0;
      /*
      ** Do a Ball Gather at the radius p[pi].fBall
      */
      smx->nnListSize =0;
      fBall = sqrt(p[pi].fBall);
      if (smx->bPeriodic) {
	for (j=0;j<3;++j) {
	  iStart[j] = floor((p[pi].r[j] - fBall)/pkd->fPeriod[j] + 0.5);
	  iEnd[j] = floor((p[pi].r[j] + fBall)/pkd->fPeriod[j] + 0.5);
	}
	for (ix=iStart[0];ix<=iEnd[0];++ix) {
	  r[0] = p[pi].r[0] - ix*pkd->fPeriod[0];
	  for (iy=iStart[1];iy<=iEnd[1];++iy) {
	    r[1] = p[pi].r[1] - iy*pkd->fPeriod[1];
	    for (iz=iStart[2];iz<=iEnd[2];++iz) {
	      r[2] = p[pi].r[2] - iz*pkd->fPeriod[2];
	      smGather(smx,p[pi].fBall,r);
	    }
	  }
	}
      }
      else {
	smGather(smx,p[pi].fBall,p[pi].r);
      }
      nCnt = smx->nnListSize;
      for(pnn=0;pnn<nCnt;++pnn ){
	if(smx->nnbRemote[pnn] == 0){
	  /* Do not add particles that are already in a group*/
	  if (smx->nnList[pnn].pPart->pGroup) continue;
	  /* Check phase space distance */
	  if (p[pi].fBallv2 > 0.0){
	    if(phase_dist(p[pi],*smx->nnList[pnn].pPart,smf->H) > 1.0) continue;
	  }
	  /* 
	  **  Mark particle and add it to the do-fifo
	  */
	  smx->nnList[pnn].pPart->pGroup = iGroup;
	  Fifo[iTail] = smx->nnList[pnn].iIndex;iTail++;
	  if(iTail == nFifo) iTail = 0;
	} else {	 /* Nonlocal neighbors: */
	  /* Make remote member linking symmetric by using smaller linking lenght if different: */
	  if (p[pi].fBallv2 > 0.0) { /* Check phase space distance */
	    if(phase_dist(p[pi],*smx->nnList[pnn].pPart,smf->H) > 1.0 || 
	       phase_dist(*smx->nnList[pnn].pPart,p[pi],smf->H) > 1.0) continue;
	  } else { /* real space distance */
	    if(smx->nnList[pnn].fDist2 > smx->nnList[pnn].pPart->fBall) continue;
	  }
		
	  /* Add to RM list if new */ 
	  if(iRmIndex==nRmListSize){
	    nRmListSize = nRmListSize*2;
	    rm = (FOFRM *) realloc(rm,(nRmListSize)*sizeof(FOFRM)); 
	    assert(rm != NULL);
	  }
	  rm[iRmIndex].iIndex = smx->nnList[pnn].iIndex ;
	  rm[iRmIndex].iPid = smx->nnList[pnn].iPid;						 				  
	  if(bsearch (rm+iRmIndex,rm+iRmIndex-nRmCnt,nRmCnt, sizeof(FOFRM),CmpRMs) == NULL ){ 
	    nRmCnt++;	
	    iRmIndex++;
	    qsort(rm+iRmIndex-nRmCnt,nRmCnt,sizeof(FOFRM),CmpRMs);
	  }
	}					
      }
    }	
    /* FIFO done for this group, add remote Members to the group data before doing next group: */
    protoGroup[iGroup].iFirstRm = iRmIndex - nRmCnt;
    protoGroup[iGroup].nRemoteMembers = nRmCnt;
    if( nRmCnt > pkd->nMaxRm ) pkd->nMaxRm = nRmCnt;
  }
  free(Fifo);
 
  /*
  ** Now we can already reject small groups if they are local 
  */
  for (pn=0; pn<nTree ; pn++){
    if(p[pn].pGroup >=0 && p[pn].pGroup < iMaxGroups)
      ++(protoGroup[p[pn].pGroup].nMembers);
    else
      printf("ERROR: idSelf=%i , p[pn].pGroup=%i too large. iMaxGroups=%i \n",pkd->idSelf,p[pn].pGroup,iMaxGroups);
  } 	
  /*
  ** Create a remapping and give unique local Ids !
  */
  iMaxGroups = iGroup;
  iGroup= 1+ pkd->idSelf;
  pkd->nGroups = 0;
  protoGroup[0].iId = tmp;
  for (i=1;i <= iMaxGroups ;i++) {
    protoGroup[i].iId = iGroup;
    if(protoGroup[i].nMembers < smf->nMinMembers && protoGroup[i].nRemoteMembers == 0 ) {
      protoGroup[i].iId = tmp;
    }	
    else {
      iGroup += pkd->nThreads;
      ++pkd->nGroups;
    }		
  }	 
  /*
  ** Update the particle groups ids.
  */
  for (pi=0; pi<nTree ; pi++) {
    p[pi].pGroup = protoGroup[p[pi].pGroup].iId;
  }
  /*
  ** Allocate memory for group data
  */
  pkd->groupData = (FOFGD *) malloc((1+pkd->nGroups)*sizeof(FOFGD)); 
  assert(pkd->groupData != NULL);
  k=1;
  for (i=0 ; i < pkd->nGroups;i++){
    while(protoGroup[k].iId == tmp) k++; 
    pkd->groupData[i].iGlobalId = protoGroup[k].iId;
    pkd->groupData[i].iLocalId = protoGroup[k].iId;
    pkd->groupData[i].nLocal = protoGroup[k].nMembers;
    pkd->groupData[i].iFirstRm = protoGroup[k].iFirstRm;
    pkd->groupData[i].nRemoteMembers = protoGroup[k].nRemoteMembers;
    k++;
    pkd->groupData[i].bMyGroup = 1;
    pkd->groupData[i].fMass = 0.0;
    pkd->groupData[i].fStarMass = 0.0;
    pkd->groupData[i].fGasMass = 0.0;
    pkd->groupData[i].fAvgDens = 0.0;
    pkd->groupData[i].fVelDisp = 0.0;
    for(j=0;j<3;j++){		
      pkd->groupData[i].fVelSigma2[j] = 0.0;
      pkd->groupData[i].r[j] = 0.0;
      pkd->groupData[i].v[j] = 0.0;
      pkd->groupData[i].rmax[j] = -l[j];
      pkd->groupData[i].rmin[j] = l[j];
    }
    pkd->groupData[i].fDeltaR2 = 0.0;
    pkd->groupData[i].potmin = -1.0;
    pkd->groupData[i].denmax = -1.0;
    pkd->groupData[i].vcircMax = 0.0;
    pkd->groupData[i].rvcircMax = 0.0;
    pkd->groupData[i].rvir = 0.0;
    pkd->groupData[i].Mvir = 0.0;
    pkd->groupData[i].rhoBG = 0.0;
    pkd->groupData[i].lambda = 0.0;
    if(pkd->groupData[i].nRemoteMembers == 0) cnt++;
  }
  free(protoGroup);
  rm  = (FOFRM *) realloc(rm,(1+iRmIndex)*sizeof(FOFRM)); 
  pkd->remoteMember = rm; 
  pkd->nRm = iRmIndex;
  /*
  ** Calculate local group properties
  */
  for (pi=0;pi<nTree ;++pi) {
#ifdef PARTICLE_HAS_MASS
    fMass = p[pi].fMass;
#else
    fMass = pkd->pClass[p[pi].iClass].fMass;
#endif
    if(p[pi].pGroup != tmp){	
      i = (p[pi].pGroup - 1 - pkd->idSelf)/pkd->nThreads;
      if(TYPETest(&p[pi],TYPE_GAS) ) 
	pkd->groupData[i].fGasMass += fMass;
      if(TYPETest(&p[pi],TYPE_STAR) ) 
	pkd->groupData[i].fStarMass += fMass;
      pkd->groupData[i].fVelDisp += (p[pi].v[0]*p[pi].v[0]+p[pi].v[1]*p[pi].v[1]+p[pi].v[2]*p[pi].v[2])*fMass;
      for(j=0;j<3;j++){
	pkd->groupData[i].fVelSigma2[j] += ( p[pi].v[j]*p[pi].v[j] )*fMass;
	if(pkd->groupData[i].fMass > 0.0) 
	  r[j] = corrPos(pkd->groupData[i].r[j]/pkd->groupData[i].fMass, p[pi].r[j], l[j]);
	else  r[j] = p[pi].r[j]; 
	pkd->groupData[i].r[j] += r[j]*fMass;
	pkd->groupData[i].fDeltaR2 +=  r[j]* r[j]*fMass;
	if(r[j] > pkd->groupData[i].rmax[j]) pkd->groupData[i].rmax[j] = r[j];
	if(r[j] < pkd->groupData[i].rmin[j]) pkd->groupData[i].rmin[j] = r[j];
	pkd->groupData[i].v[j] += p[pi].v[j]*fMass;
      }
      pkd->groupData[i].fMass += fMass;
      /* use absolute values of particle potential, sign of pot has changed in pkdgrav2*/
      if(fabs(p[pi].fPot) > pkd->groupData[i].potmin){
	pkd->groupData[i].potmin = fabs(p[pi].fPot);
	for(j=0;j<3;j++)pkd->groupData[i].rpotmin[j]=r[j];
      }
      if( p[pi].fDensity > pkd->groupData[i].denmax){
	pkd->groupData[i].denmax = p[pi].fDensity;
	for(j=0;j<3;j++)pkd->groupData[i].rdenmax[j]=r[j];
      }
      if(nFOFsDone > 0){
	  assert(0); /* p[pn] ??  Not p[pi] ?? */
	  //rho = p[pn].fMass/(pow(p[pn].fBall ,3.0/2.0)*smf->fContrast);
	if(rho > pkd->groupData[i].rhoBG)pkd->groupData[i].rhoBG = rho;
      } else { 
	pkd->groupData[i].rhoBG = 1.0;
      }
    }
  }
  for (i=0; i<pkd->nGroups;++i){
    pkd->groupData[i].fRadius = 0.0;
    pkd->groupData[i].fRadius += pkd->groupData[i].rmax[0]-pkd->groupData[i].rmin[0];
    pkd->groupData[i].fRadius += pkd->groupData[i].rmax[1]-pkd->groupData[i].rmin[1];
    pkd->groupData[i].fRadius += pkd->groupData[i].rmax[2]-pkd->groupData[i].rmin[2];
    pkd->groupData[i].fRadius /= 6.0;
    if(pkd->groupData[i].nLocal > 1 ){
      pkd->groupData[i].fAvgDens = pkd->groupData[i].fMass*0.238732414/pkd->groupData[i].fRadius/pkd->groupData[i].fRadius/pkd->groupData[i].fRadius; /*assuming spherical*/		
    } else
      pkd->groupData[i].fAvgDens = 0.0;
  }	
}  


FLOAT corrPos(FLOAT com, FLOAT r,FLOAT l){
  if(com > 0.2*l && r< -0.2*l) return r + l;
  else if(com < - 0.2*l && r > 0.2*l) return r - l;
  else return r;
}
int CmpParticleGroupIds(const void *v1,const void *v2)
{
  PARTICLE *p1 = (PARTICLE *)v1;
  PARTICLE *p2 = (PARTICLE *)v2;
  return(p1->pGroup - p2->pGroup);
}
FLOAT phase_dist(PARTICLE pa,PARTICLE pb, double H)
{ 
  int i;
  FLOAT dx,dv;
  dx=0.0;
  dv=0.0;
  for(i=0; i<3; i++) dx += pow(pa.r[i]-pb.r[i],2);
  dx /= pa.fBall;
  for(i=0; i<3; i++) dv += pow(pa.v[i]-pb.v[i]+H*(pa.r[i]-pb.r[i]),2); 
  dv /= pa.fBallv2; 
  dx = dx+dv;
  return dx;
}
int CmpRMs(const void *v1,const void *v2)
{
  FOFRM *rm1 = (FOFRM *)v1;
  FOFRM *rm2 = (FOFRM *)v2;
  if(rm1->iPid != rm1->iPid) return(rm1->iPid - rm2->iPid);
  else return(rm1->iIndex - rm2->iIndex);
}
int CmpProtoGroups(const void *v1,const void *v2)
{ 
  FOFPG *g1 = (FOFPG *)v1;
  FOFPG *g2 = (FOFPG *)v2;
  return(g1->iId - g2->iId);
}
int CmpGroups(const void *v1,const void *v2)
{
  FOFGD *g1 = (FOFGD *)v1;
  FOFGD *g2 = (FOFGD *)v2;
  /* use maxima of fabs(potential) to order groups*/
  if( g1->potmin > g2->potmin )  
    return 1;
  else
    return -1;
}
int smGroupMerge(SMF *smf,int bPeriodic)
{
  PKD pkd = smf->pkd;
  MDL mdl = smf->pkd->mdl;
  PARTICLE *p = smf->pkd->pStore;
  PARTICLE *pPart;
  FLOAT l[3], r,min,max,corr;
  int pi,id,i,j,k,index,listSize, sgListSize, lsgListSize;
  int nLSubGroups,nSubGroups,nMyGroups;
  int iHead, iTail, nFifo,tmp, nTree;
  FILE * pFile; /* files for parallel output of group ids, links and densities*/
  FILE * lFile;
  FILE * dFile;
  char filename [30];
  
  FOFGD *sG;
  FOFRM *rmFifo;
  FOFRM *remoteRM;
  FOFRM rm;
  FOFGD **subGroup; /* Array of group data pointers */
  FOFGD **lSubGroup; /* Array of group data pointers */

  if (bPeriodic) {
    for(j=0;j<3;j++) l[j] = pkd->fPeriod[j];
  }
  else {
    for(j=0;j<3;j++) l[j] = FLOAT_MAXVAL;
  }

  tmp = pkd->nDark+pkd->nGas+pkd->nStar;
  nTree = pkd->kdNodes[ROOT].pUpper + 1;
  nFifo = 30*pkd->nMaxRm + 1;
  sgListSize = 10*pkd->nThreads;
  lsgListSize = 10*pkd->nThreads;
	
  subGroup = (FOFGD **)malloc(sgListSize*sizeof(FOFGD *));
  assert(subGroup != NULL);

  lSubGroup = (FOFGD **)malloc(lsgListSize*sizeof(FOFGD *));
  assert(lSubGroup != NULL);
  rmFifo = (FOFRM *)malloc(nFifo*sizeof(FOFRM));	
  assert(rmFifo != NULL);	

  /*		
  ** Start RO particle cache.
  */
  mdlROcache(mdl,CID_PARTICLE,p,sizeof(PARTICLE), nTree);
  /*
  ** Start CO group data cache.
  */
  mdlCOcache(mdl,CID_GROUP,pkd->groupData,sizeof(FOFGD), pkd->nGroups,initGroupMerge,combGroupMerge);
  /*			
  ** Start RO remote member cache.
  */
  mdlROcache(mdl,CID_RM,pkd->remoteMember,sizeof(FOFRM),pkd->nRm);
	
  for (i=0; i < pkd->nGroups ;i++){
    nSubGroups = 0;
    nLSubGroups = 0;
    iHead = 0;
    iTail = 0;
    pkd->groupData[i].nTotal = pkd->groupData[i].nLocal;   
    if(pkd->groupData[i].bMyGroup == 0)	goto NextGroup;
    if(pkd->groupData[i].nRemoteMembers && pkd->groupData[i].bMyGroup){		
			
      /* Add all remote members to the Fifo: */		
      for (j=pkd->groupData[i].iFirstRm; j < pkd->groupData[i].iFirstRm + pkd->groupData[i].nRemoteMembers ;j++)
	rmFifo[iTail++] = pkd->remoteMember[j];
      while(iHead != iTail){
	rm = rmFifo[iHead++];
	if(iHead == nFifo) iHead = 0;
	if(rm.iPid == pkd->idSelf){
	  pPart = pkd->pStore + rm.iIndex;
	  /* Local: Have I got this group already? If RM not in a group, ignore it */
	  if(pPart->pGroup == pkd->groupData[i].iLocalId || pPart->pGroup == tmp
	     || pPart->pGroup == 0 ) goto NextRemoteMember;	
	  for (k=0; k < nLSubGroups ;k++){
	    if(lSubGroup[k]->iLocalId == pPart->pGroup){
	      goto NextRemoteMember;
	    }
	  }
	  /* Local: New subgroup found, add to list: */	
	  sG = pkd->groupData + (pPart->pGroup - 1 - pkd->idSelf)/pkd->nThreads;
	  if(nLSubGroups >= lsgListSize){
	    lsgListSize *= 1.5;
	    lSubGroup = (FOFGD **)realloc(lSubGroup, lsgListSize*sizeof(FOFGD *));
	    assert(lSubGroup != NULL);						
	  }
	  lSubGroup[nLSubGroups++] = sG;
	  if(sG->fAvgDens > pkd->groupData[i].fAvgDens) {
	    pkd->groupData[i].bMyGroup = 0;
	    goto NextGroup;	
	  }
	  if(sG->iLocalId < pkd->groupData[i].iGlobalId)pkd->groupData[i].iGlobalId = sG->iLocalId; 
	  pkd->groupData[i].nTotal += sG->nLocal;
	  /* Add all its remote members to the Fifo: */		
	  for (j=sG->iFirstRm; j< sG->iFirstRm + sG->nRemoteMembers ;j++){					
	    rmFifo[iTail++] = pkd->remoteMember[j];
	    if(iTail == nFifo) iTail = 0;
	  }
	} else {
	  pPart = mdlAquire(mdl,CID_PARTICLE,rm.iIndex,rm.iPid);					
	  mdlRelease(mdl,CID_PARTICLE,pPart);
	  /* Remote: ignore if not in a group */	
	  if(pPart->pGroup == tmp){
	    goto NextRemoteMember;
	  }
	  /* Remote: Have I got this group already? */	
	  for (k=0; k < nSubGroups ;++k){
	    if(pPart->pGroup == subGroup[k]->iLocalId){
	      goto NextRemoteMember;						
	    }
	  }	
	  /* Remote: New subgroup found, add to list: */	
	  index = (pPart->pGroup - 1 - rm.iPid)/pkd->nThreads ;
	  sG = mdlAquire(mdl,CID_GROUP,index,rm.iPid);
	  if(nSubGroups >= sgListSize){
	    sgListSize *= 1.5;
	    subGroup = (FOFGD **)realloc(subGroup, sgListSize*sizeof(FOFGD *));
	    assert(subGroup != NULL);						
	  }
	  subGroup[nSubGroups++] = sG;
	  if(sG->fAvgDens > pkd->groupData[i].fAvgDens) {
	    pkd->groupData[i].bMyGroup = 0;
	    goto NextGroup;
	  }
	  if(sG->iLocalId < pkd->groupData[i].iGlobalId)pkd->groupData[i].iGlobalId = sG->iLocalId; 
	  pkd->groupData[i].nTotal += sG->nLocal;
	  /* Add all its remote members to the Fifo: */		
	  for (j=sG->iFirstRm; j < sG->iFirstRm + sG->nRemoteMembers ;j++){
	    remoteRM = mdlAquire(mdl,CID_RM,j,rm.iPid);		
	    rmFifo[iTail++] = *remoteRM;
	    if(iTail == nFifo) iTail = 0;
	    mdlRelease(mdl,CID_RM,remoteRM);
	  }
	}
      NextRemoteMember:
	if(0){
	}	
      }
      if(pkd->groupData[i].nTotal < smf->nMinMembers){ 
	/* 
	** Nonlocal group too small:
	*/
	pkd->groupData[i].iGlobalId = 0;
	pkd->groupData[i].bMyGroup = 0;
	for (k=0;k < nSubGroups;++k) {
	  subGroup[k]->iGlobalId = 0;
	  subGroup[k]->bMyGroup = 0;
	}	
	for (k=0;k < nLSubGroups;++k) {
	  lSubGroup[k]->iGlobalId = 0;
	  lSubGroup[k]->bMyGroup = 0;
	}				
      } else {		
	/*
	** Nonlocal group big enough: calculate properties
	*/
	for (k=0;k < nSubGroups;++k) {
	  sG = subGroup[k];
	  sG->iGlobalId = pkd->groupData[i].iGlobalId;			 
	  sG->bMyGroup = 0;
	  if( sG->denmax > pkd->groupData[i].denmax){
	    pkd->groupData[i].denmax = sG->denmax;
	    for(j=0;j<3;j++)pkd->groupData[i].rdenmax[j]=
			      corrPos(pkd->groupData[i].r[j]/pkd->groupData[i].fMass, sG->rdenmax[j], l[j]);
	  }
	  if( sG->potmin > pkd->groupData[i].potmin){
	    pkd->groupData[i].potmin = sG->potmin;
	    for(j=0;j<3;j++)pkd->groupData[i].rpotmin[j]=
			      corrPos(pkd->groupData[i].r[j]/pkd->groupData[i].fMass, sG->rpotmin[j], l[j]);
	  }
	  for(j=0;j<3;j++){
	    r = corrPos(pkd->groupData[i].r[j]/pkd->groupData[i].fMass, sG->r[j]/sG->fMass, l[j]);
	    pkd->groupData[i].r[j] += r*sG->fMass;
	    max = corrPos(pkd->groupData[i].r[j]/pkd->groupData[i].fMass, sG->rmax[j], l[j]);
	    if(max > pkd->groupData[i].rmax[j]) pkd->groupData[i].rmax[j] = max;
	    min = corrPos(pkd->groupData[i].r[j]/pkd->groupData[i].fMass, sG->rmin[j], l[j]);
	    if(min < pkd->groupData[i].rmin[j]) pkd->groupData[i].rmin[j] = min;
	    pkd->groupData[i].fVelSigma2[j] += sG->fVelSigma2[j];
	    pkd->groupData[i].v[j] += sG->v[j];
	    corr = r - sG->r[j]/sG->fMass;
	    pkd->groupData[i].fDeltaR2 += 2.0*corr*sG->r[j] + sG->fMass*corr*corr;
	  }
	  pkd->groupData[i].fStarMass += sG->fStarMass;
	  pkd->groupData[i].fGasMass += sG->fGasMass;
	  pkd->groupData[i].fVelDisp += sG->fVelDisp;
	  pkd->groupData[i].fDeltaR2 += sG->fDeltaR2;
	  pkd->groupData[i].fMass += sG->fMass;				
	}	
	for(k=0;k < nLSubGroups;++k) {

	  sG = lSubGroup[k];
	  sG->iGlobalId = pkd->groupData[i].iGlobalId;			
	  sG->bMyGroup = 0;
	  if( sG->denmax > pkd->groupData[i].denmax){
	    pkd->groupData[i].denmax = sG->denmax;
	    for(j=0;j<3;j++)pkd->groupData[i].rdenmax[j]=
			      corrPos(pkd->groupData[i].r[j]/pkd->groupData[i].fMass, sG->rdenmax[j], l[j]);
	  }
	  if( sG->potmin > pkd->groupData[i].potmin){
	    pkd->groupData[i].potmin = sG->potmin;
	    for(j=0;j<3;j++)pkd->groupData[i].rpotmin[j]=
			      corrPos(pkd->groupData[i].r[j]/pkd->groupData[i].fMass, sG->rpotmin[j], l[j]);
	  }
	  for(j=0;j<3;j++){
	    r = corrPos(pkd->groupData[i].r[j]/pkd->groupData[i].fMass, sG->r[j]/sG->fMass, l[j]);
	    pkd->groupData[i].r[j] += r*sG->fMass;
	    max = corrPos(pkd->groupData[i].r[j]/pkd->groupData[i].fMass, sG->rmax[j], l[j]);
	    if(max > pkd->groupData[i].rmax[j]) pkd->groupData[i].rmax[j] = max;
	    min = corrPos(pkd->groupData[i].r[j]/pkd->groupData[i].fMass, sG->rmin[j], l[j]);
	    if(min < pkd->groupData[i].rmin[j]) pkd->groupData[i].rmin[j] = min;
	    pkd->groupData[i].fVelSigma2[j] += sG->fVelSigma2[j];
	    pkd->groupData[i].v[j] += sG->v[j];
	    corr = r - sG->r[j]/sG->fMass;
	    pkd->groupData[i].fDeltaR2 += 2.0*corr*sG->r[j] + sG->fMass*corr*corr;
	  }
	  pkd->groupData[i].fStarMass += sG->fStarMass;
	  pkd->groupData[i].fGasMass += sG->fGasMass;
	  pkd->groupData[i].fVelDisp += sG->fVelDisp;
	  pkd->groupData[i].fDeltaR2 += sG->fDeltaR2;
	  pkd->groupData[i].fMass += sG->fMass;
	}	
      }
    NextGroup:
      /*
      ** Release non-local pointers.
      */
      for (k=0;k < nSubGroups;k++) {
	mdlRelease(mdl,CID_GROUP,subGroup[k]);
      } 
    } 
  }
  mdlFinishCache(mdl,CID_PARTICLE);			
  mdlFinishCache(mdl,CID_GROUP);
  mdlFinishCache(mdl,CID_RM);
  
  free(subGroup);
  free(lSubGroup);
  free(rmFifo);
  free(pkd->remoteMember);
  pkd->nRm = 0;
  
  /*  Update and write the groups ids of the local particles */	
  sprintf(filename,"tmpgrids/p%i.a%le.grids",pkd->idSelf,smf->a); 
  pFile = fopen(filename,"a"); 
  sprintf(filename,"tmplinks/p%i.a%le.links",pkd->idSelf,smf->a);
  lFile = fopen(filename,"a");
  sprintf(filename,"tmpdens/p%i.a%le.dens",pkd->idSelf,smf->a);
    dFile = fopen(filename,"a");
  for (pi=0;pi<nTree ;pi++){		
    index = (p[pi].pGroup - 1 - pkd->idSelf)/pkd->nThreads ;
    if(index >= 0 && index < pkd->nGroups )
      p[pi].pGroup = pkd->groupData[index].iGlobalId;
    else p[pi].pGroup = 0;
    fprintf(dFile, "%lu %.8g\n",  p[pi].iOrder, p[pi].fDensity);
    if(p[pi].pGroup) fprintf(pFile, "%lu %i\n",  p[pi].iOrder, p[pi].pGroup);
    if(p[pi].pBin && p[pi].pGroup)fprintf(lFile, "%i %i\n", p[pi].pGroup, p[pi].pBin); 
  }
  fclose(pFile);fclose(lFile);fclose(dFile);
  
  /* Move real groups to low memory and normalize their properties. */	
  nMyGroups=0;
  for (i=0; i< pkd->nGroups;i++){
    if(pkd->groupData[i].bMyGroup && pkd->groupData[i].iGlobalId != 0){
      for(j=0;j<3;j++){
	pkd->groupData[i].r[j] /= pkd->groupData[i].fMass;
	if(pkd->groupData[i].r[j] > 0.5*l[j]) pkd->groupData[i].r[j] -= l[j];
	if(pkd->groupData[i].r[j] < -0.5*l[j]) pkd->groupData[i].r[j] += l[j];	 
	pkd->groupData[i].v[j] /= pkd->groupData[i].fMass;
	pkd->groupData[i].fVelSigma2[j] = 
	  pkd->groupData[i].fVelSigma2[j]/pkd->groupData[i].fMass - pkd->groupData[i].v[j]*pkd->groupData[i].v[j];
	pkd->groupData[i].fVelSigma2[j] = pow(pkd->groupData[i].fVelSigma2[j], 0.5);
      }	
      pkd->groupData[i].fRadius = 0.0;
      pkd->groupData[i].fRadius += pkd->groupData[i].rmax[0]-pkd->groupData[i].rmin[0];
      pkd->groupData[i].fRadius += pkd->groupData[i].rmax[1]-pkd->groupData[i].rmin[1];
      pkd->groupData[i].fRadius += pkd->groupData[i].rmax[2]-pkd->groupData[i].rmin[2];
      pkd->groupData[i].fRadius /= 6.0;
      pkd->groupData[i].fVelDisp = 
	pkd->groupData[i].fVelDisp/pkd->groupData[i].fMass - pkd->groupData[i].v[0]*pkd->groupData[i].v[0]
	- pkd->groupData[i].v[1]*pkd->groupData[i].v[1]- pkd->groupData[i].v[2]*pkd->groupData[i].v[2];
      pkd->groupData[i].fVelDisp = pow(pkd->groupData[i].fVelDisp, 0.5);
      pkd->groupData[i].fDeltaR2 /= pkd->groupData[i].fMass; 
      pkd->groupData[i].fDeltaR2 -= pkd->groupData[i].r[0]*pkd->groupData[i].r[0]
	+ pkd->groupData[i].r[1]*pkd->groupData[i].r[1] + pkd->groupData[i].r[2]*pkd->groupData[i].r[2];
      pkd->groupData[i].fDeltaR2 = pow(pkd->groupData[i].fDeltaR2, 0.5);
      pkd->groupData[i].fAvgDens = 0.5*pkd->groupData[i].fMass
	*0.238732414/pow(pkd->groupData[i].fDeltaR2,3.0);/*using half mass radius and assuming spherical halos*/
      pkd->groupData[nMyGroups] = pkd->groupData[i];
      nMyGroups++;	
    }
  }	
  if(nMyGroups == pkd->nGroups){
    pkd->groupData = (FOFGD *) realloc(pkd->groupData,(nMyGroups+1)*sizeof(FOFGD));
    assert(pkd->groupData != NULL);
  }
  pkd->groupData[nMyGroups].bMyGroup = 0;
  /* Start RO group data cache and master reads and saves all the group data. */
  mdlROcache(mdl,CID_GROUP,pkd->groupData,sizeof(FOFGD), nMyGroups + 1);
  if(pkd->idSelf == 0) {
    listSize = pkd->nThreads*(pkd->nGroups+1);
    pkd->groupData = (FOFGD *) realloc(pkd->groupData,listSize*sizeof(FOFGD));
    assert(pkd->groupData != NULL);
    for(id=1; id < pkd->nThreads; id++){
      index = 0;
      while(1){
	sG = mdlAquire(mdl,CID_GROUP,index,id);
	mdlRelease(mdl,CID_GROUP,sG);
	if(sG->bMyGroup != 0){
	  if(nMyGroups >= listSize-1){
	    listSize *= 2.0; 
	    pkd->groupData = (FOFGD *) realloc(pkd->groupData, listSize*sizeof(FOFGD)); 
	    assert(pkd->groupData != NULL);
	  }
	  pkd->groupData[nMyGroups] = *sG;
	  index++;
	  nMyGroups++;
	} else {
	  break;
	}
      }
    }
    pkd->groupData[nMyGroups].bMyGroup = 0;
  }
  mdlFinishCache(mdl,CID_GROUP);
  if(pkd->idSelf != 0){
    nMyGroups = 0;
  } else {
    /*
    ** Master orders groupData
    */	
    qsort(pkd->groupData,nMyGroups,sizeof(FOFGD), CmpGroups);
  }
  pkd->nGroups = nMyGroups;
  return nMyGroups;
}

int smGroupProfiles(SMX smx, SMF *smf,int bPeriodic, int nTotalGroups,int bLogBins, int nFOFsDone)
{
  PKD pkd = smf->pkd;
  MDL mdl = smf->pkd->mdl;
  PARTICLE *p = smf->pkd->pStore;
  double dx2;
  FLOAT l[3],L[3],r[3],relvel[3],com[3],V,Rprev,Vprev,Mprev,vcirc,vcircMax,rvcircMax,M,R,binFactor;
  FLOAT rvir,Mvir,Delta,fBall,lastbin,minSoft,rho,rhoinner,fMass,fSoft;
  int pn,i,j,k,iBin,nBins,maxId,nTree,index,nCnt,pnn;
  int* iGroupIndex;
  int iStart[3],iEnd[3];
  int ix,iy,iz;
  FOFGD *gdp;
  FOFBIN *bin;

  if(nTotalGroups==0) return 0;

  Delta = smf->Delta; /* density contrast over critial density within rvir  */
  binFactor = smf->binFactor; /* to assure that the bins always reach out to the tidal/virial radius */
 
  M=0.0;
  R=0.0;
  if (bPeriodic) {
    for(j=0;j<3;j++) l[j] = pkd->fPeriod[j];
  }
  else {
    for(j=0;j<3;j++) l[j] = FLOAT_MAXVAL;
  }
  nTree = pkd->kdNodes[ROOT].pUpper + 1;
  /*			
  ** the smallest softening of all particles sets the innermost bin radius:
  */	
  minSoft=1.0;
  for(pn=0;pn<nTree;pn++) {
#ifdef PARTICLE_HAS_MASS
    fSoft = p[pn].fSoft;
#else
    fSoft = pkd->pClass[p[pn].iClass].fSoft;
#endif
    if(fSoft<minSoft)minSoft=fSoft;
  }
  /*
  ** Start RO group data cache and read all if you are not master
  */
  if(pkd->idSelf != 0){
    pkd->groupData = (FOFGD *) realloc(pkd->groupData,nTotalGroups*sizeof(FOFGD));
    assert(pkd->groupData != NULL);
  }
  mdlROcache(mdl,CID_GROUP,pkd->groupData,sizeof(FOFGD),pkd->nGroups);
  if(pkd->idSelf != 0){
    for(i=0; i< nTotalGroups; i++){
      gdp = mdlAquire(mdl,CID_GROUP,i,0);
      mdlRelease(mdl,CID_GROUP,gdp);
      pkd->groupData[i] = *gdp;			
    }
  }	
  mdlFinishCache(mdl,CID_GROUP);
  /*
  ** Calculate the number of bins and allocate memory for them.
  */
  maxId = 0;
  nBins = 0;
  for(i=0; i< nTotalGroups; i++){
    if( pkd->groupData[i].nTotal >= smf->nMinProfile ){ /* use RM field for bin pointers now...*/ 
      pkd->groupData[i].nRemoteMembers = smf->nBins; 
    }	else pkd->groupData[i].nRemoteMembers = 0;
    pkd->groupData[i].iFirstRm = nBins;
    nBins += pkd->groupData[i].nRemoteMembers;
    if(pkd->groupData[i].iGlobalId > maxId ) maxId = pkd->groupData[i].iGlobalId;
  }
  pkd->groupBin = (FOFBIN *) malloc( (nBins+1)*sizeof(FOFBIN) );
  assert(pkd->groupBin != NULL);
  /*
  ** Create a map group id -> index of group in the pkd->groupData array.
  */
  iGroupIndex = (int *) malloc( (maxId+1)*sizeof(int));
  assert(iGroupIndex != NULL);
  for(i=0; i< maxId+1; i++)iGroupIndex[i]= -1;
  for(i=0; i< nTotalGroups; i++){
    iGroupIndex[pkd->groupData[i].iGlobalId] = i;        
  }
  /*			
  ** Initalize bin array
  */	
  iBin = 0;
  for (i=0; i< nTotalGroups; i++) {
    if(bLogBins == 2){ /* logarithmic bins with same fixed non-comoving size for all groups */
      lastbin = binFactor/smf->a; /* NB: lastbin is actually the first bin in this case */
    }else {
      /* estimate virial radius, assuming isothermal shperes */
      lastbin = pow(pkd->groupData[i].fAvgDens,0.5)*pkd->groupData[i].fDeltaR2*binFactor;
      for (k=i+1; k < nTotalGroups; k++) { 
	/* if a larger group is nearby limit lastbin to 0.75 of its distance*/
/* 	if(pkd->groupData[k].fAvgDens*pkd->groupData[k].fAvgDens*pkd->groupData[k].fMass > */
/* 	   pkd->groupData[i].fAvgDens*pkd->groupData[i].fAvgDens*pkd->groupData[i].fMass){ */
	  dx2=0.0;for(j = 0; j < 3; j++)dx2+=pow(pkd->groupData[k].r[j] - pkd->groupData[i].r[j],2.0);
	  dx2=0.75*sqrt(dx2);
	  if(lastbin > dx2)lastbin = dx2;
/* 	} */
      }
    }
    for(j=0; j < pkd->groupData[i].nRemoteMembers; j++){
      assert(iBin < nBins);
      pkd->groupBin[iBin].iId = pkd->groupData[i].iGlobalId;
      pkd->groupBin[iBin].nMembers = 0;
      if(bLogBins == 1){/* logarithmic bins */
	dx2 = pkd->groupData[i].nRemoteMembers-(j+1);
	pkd->groupBin[iBin].fRadius = minSoft*pow(lastbin/minSoft,((float) j)/( (float)pkd->groupData[i].nRemoteMembers-1.0));
      }else if(bLogBins == 2){/* logarithmic bins with same fixed non-comoving size for all groups */
	pkd->groupBin[iBin].fRadius = lastbin*pow(10.0, 0.1*j);
      } else { /* linear bins */
	dx2 = j+1;
	pkd->groupBin[iBin].fRadius = lastbin*dx2/pkd->groupData[i].nRemoteMembers;  
      } 
      pkd->groupBin[iBin].fMassInBin = 0.0;
      pkd->groupBin[iBin].fMassEnclosed = 0.0;
      pkd->groupBin[iBin].v2[0] = 0.0;
      pkd->groupBin[iBin].v2[1] = 0.0;
      pkd->groupBin[iBin].v2[2] = 0.0;
      pkd->groupBin[iBin].L[0] = 0.0;
      pkd->groupBin[iBin].L[1] = 0.0;
      pkd->groupBin[iBin].L[2] = 0.0;	
/*  	Shapes are not implemented yet: */
/*       pkd->groupBin[iBin].a = 0.0;	 */
/*       pkd->groupBin[iBin].b = 0.0;	 */
/*       pkd->groupBin[iBin].c = 0.0;	 */
/*       pkd->groupBin[iBin].phi = 0.0;	 */
/*       pkd->groupBin[iBin].theta = 0.0;	 */
/*       pkd->groupBin[iBin].psi = 0.0;	 */
      iBin++;
    } 
  }
  /*			
  ** Add local particles to their correspondig bins
  */	
  for(pn=0;pn<nTree;pn++) {
    p[pn].pBin = -1;
  }
  for (index=0; index< nTotalGroups; index++){ 
    if(pkd->groupData[index].nRemoteMembers > 0){
      k = pkd->groupData[index].iFirstRm + pkd->groupData[index].nRemoteMembers -1;
      for(j = 0; j < 3; j++){
	if(nFOFsDone == 0 && smf->bUsePotmin==1){
	  com[j] = pkd->groupData[index].rpotmin[j];
	} else if (smf->bUsePotmin == 2){	
	  com[j] =  pkd->groupData[index].rdenmax[j];	
	} else {		
	  com[j] = pkd->groupData[index].r[j];						
	}
      }
      smx->nnListSize = 0;
      fBall =pkd->groupBin[k].fRadius;
      if (bPeriodic) {
	for (j=0;j<3;++j) {
	  iStart[j] = floor((com[j] - fBall)/pkd->fPeriod[j] + 0.5);
	  iEnd[j] = floor((com[j] + fBall)/pkd->fPeriod[j] + 0.5);
	}
	for (ix=iStart[0];ix<=iEnd[0];++ix) {
	  r[0] = com[0] - ix*pkd->fPeriod[0];
	  for (iy=iStart[1];iy<=iEnd[1];++iy) {
	    r[1] = com[1] - iy*pkd->fPeriod[1];
	    for (iz=iStart[2];iz<=iEnd[2];++iz) {
	      r[2] = com[2] - iz*pkd->fPeriod[2];
	      smGatherLocal(smx,fBall*fBall,r);
	    }
	  }
	}
      }
      else {
	smGatherLocal(smx,fBall*fBall,com);
      }
      nCnt = smx->nnListSize;
      for(pnn=0;pnn<nCnt;++pnn ){
	dx2=0.0;
	for(j = 0; j < 3; j++){
	  r[j] = corrPos(com[j], smx->nnList[pnn].pPart->r[j], l[j])	- com[j];
	  relvel[j] = smx->nnList[pnn].pPart->v[j] - pkd->groupData[index].v[j];
	  dx2 += pow(r[j],2);
	} 
	iBin = pkd->groupData[index].iFirstRm;
	k = 0;
	while(dx2 > pkd->groupBin[iBin+k].fRadius*pkd->groupBin[iBin+k].fRadius){
	  k++;
	  if( k == pkd->groupData[index].nRemoteMembers) goto nextParticle;
	}
	assert(iBin+k < nBins);
#ifdef PARTICLE_HAS_MASS
	fMass = smx->nnList[pnn].pPart->fMass;
#else
	fMass = pkd->pClass[smx->nnList[pnn].pPart->iClass].fMass;
#endif
	pkd->groupBin[iBin+k].nMembers++;
	pkd->groupBin[iBin+k].fMassInBin += fMass;
	pkd->groupBin[iBin+k].v2[0] += fMass*pow(relvel[0],2.0);
	pkd->groupBin[iBin+k].v2[1] += fMass*pow(relvel[1],2.0);
	pkd->groupBin[iBin+k].v2[2] += fMass*pow(relvel[2],2.0);
	pkd->groupBin[iBin+k].com[0] = com[0];
	pkd->groupBin[iBin+k].com[1] = com[1];
	pkd->groupBin[iBin+k].com[2] = com[2];
	pkd->groupBin[iBin+k].L[0] += fMass*(r[1]*relvel[2] - r[2]*relvel[1]);
	pkd->groupBin[iBin+k].L[1] += fMass*(r[2]*relvel[0] - r[0]*relvel[2]);
	pkd->groupBin[iBin+k].L[2] += fMass*(r[0]*relvel[1] - r[1]*relvel[0]);	
	smx->nnList[pnn].pPart->pBin = iBin+k;
      nextParticle:
	;
      }		
    }
  }
  /*			
  ** Start CO group profiles cache.
  */	 
  mdlCOcache(mdl,CID_BIN,pkd->groupBin,sizeof(FOFBIN),nBins,initGroupBins,combGroupBins);
  if(pkd->idSelf != 0) { 
    for(i=0; i< nBins; i++){ 
      if(pkd->groupBin[i].fMassInBin > 0.0){
	bin = mdlAquire(mdl,CID_BIN,i,0);	   
	*bin = pkd->groupBin[i];  	
	mdlRelease(mdl,CID_BIN,bin);    
      }  	
    }
  }
  mdlFinishCache(mdl,CID_BIN);
  
  /*			
  ** Caculate densities, vcirc max, rvir:
  */	
  if(pkd->idSelf == 0){
    Rprev = 0.0;
    Vprev = 0.0;
    Mprev = 0.0;
    vcircMax = 0.0;
    rvcircMax = 0.0;
    rvir = 0.0;
    Mvir = 0.0;
    for(k=0;k<3;k++) L[k] = 0.0;
    for (i=0; i< nBins; i++) {
      if( i > 0 && pkd->groupBin[i].iId != j ){ 
	/* i.e. a new group profile starts with this bin: */
	Rprev = 0.0;
	Vprev = 0.0;
	Mprev = 0.0;
	pkd->groupData[iGroupIndex[j]].vcircMax = vcircMax;
	pkd->groupData[iGroupIndex[j]].rvcircMax = rvcircMax;
	pkd->groupData[iGroupIndex[j]].rvir = rvir;
	pkd->groupData[iGroupIndex[j]].Mvir = Mvir;
	pkd->groupData[iGroupIndex[j]].lambda = 
	  pow((L[0]*L[0]+L[1]*L[1]+L[2]*L[2])/2.0,0.5)/(M*pow(M*R,0.5) );
	vcircMax = 0.0;
	rvcircMax = 0.0;
	rvir = 0.0;
	Mvir = 0.0;
	for(k=0;k<3;k++) L[k] = 0.0;
      }
      j = pkd->groupBin[i].iId;
      V = 4.1887902048*pow(pkd->groupBin[i].fRadius,3.0);
      pkd->groupBin[i].fDensity = pkd->groupBin[i].fMassInBin/(V-Vprev);
      pkd->groupBin[i].fMassEnclosed = pkd->groupBin[i].fMassInBin + Mprev;
      for(k=0;k<3;k++)pkd->groupBin[i].v2[k] /= pkd->groupBin[i].fMassInBin;
      /* calculate the virial mass and radius: */
      rho = pkd->groupBin[i].fMassEnclosed/V;
      if(Vprev > 0.0)  rhoinner = pkd->groupBin[i-1].fMassEnclosed/Vprev;
      else rhoinner=0.0;
      if(rhoinner > Delta){ 
	if(rho < Delta){
	  /* interpolate between bins assuming isothermal spheres: */
	  rvir = Rprev + (pkd->groupBin[i].fRadius-Rprev)/(rho-rhoinner)*(Delta-rhoinner);
	  Mvir = Mprev + (pkd->groupBin[i].fMassEnclosed-Mprev)/(rho-rhoinner)*(Delta-rhoinner);
	} else {
	  rvir = pkd->groupBin[i].fRadius;
	  Mvir = pkd->groupBin[i].fMassEnclosed;
	}
      }
      Rprev = pkd->groupBin[i].fRadius;
      Vprev = V;
      Mprev = pkd->groupBin[i].fMassEnclosed;
     /* calculate peak circular velocity: */
      vcirc = pow(pkd->groupBin[i].fMassEnclosed/pkd->groupBin[i].fRadius,0.5);
      if( vcirc >  vcircMax 
	  && vcirc > 5.0*2.046*pow(pkd->groupData[iGroupIndex[j]].rhoBG,0.5)*pkd->groupBin[i].fRadius){ 
	if(rvir < pkd->groupBin[i].fRadius){
	}else{
	  vcircMax = vcirc;
	  rvcircMax = pkd->groupBin[i].fRadius;
	}
      }	
      if(! (pkd->groupBin[i].fRadius > rvcircMax) ){
	for(k=0;k<3;k++) L[k] += pkd->groupBin[i].L[k];
	M = pkd->groupBin[i].fMassEnclosed;
	R = pkd->groupBin[i].fRadius;
      }
    }
    pkd->groupData[iGroupIndex[j]].vcircMax = vcircMax;
    pkd->groupData[iGroupIndex[j]].rvcircMax = rvcircMax;
    pkd->groupData[iGroupIndex[j]].rvir = rvir;
    pkd->groupData[iGroupIndex[j]].Mvir = Mvir;
    pkd->groupData[iGroupIndex[j]].lambda = 
      pow((L[0]*L[0]+L[1]*L[1]+L[2]*L[2])/2.0,0.5)/(M*pow(M*R,0.5) );
  }
  free(iGroupIndex);
  if(pkd->idSelf != 0){
    free(pkd->groupData);
    free(pkd->groupBin);
  }	
  pkd->nBins =  nBins;
  return nBins;
}	
