#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <math.h>
#include <assert.h>
#include "pkd.h"
#include "walk.h"
#include "grav.h"
#include "floattype.h"
#include "ewald.h"
#include "moments.h"
#ifndef NO_TIMING
#include <sys/time.h>
#endif



typedef struct CheckElt {
    int iCell;
    int id;
    FLOAT rOffset[3];
    } CELT;

typedef struct CheckStack {
    int nPart;
    int nCell;
    int nPartBucket;
    int nCheck;
    CELT *Check;
    LOCR L;
    } CSTACK;

#define WALK_MINMULTIPOLE	3

/*
** Returns total number of active particles for which gravity was calculated.
*/
int pkdGravWalk(PKD pkd,int nReps,int bEwald,int bVeryActive,double fEwCut,
		double *pdFlop,double *pdPartSum,double *pdCellSum)
    {
    PARTICLE *p = pkd->pStore;
    PARTICLE *pRemote;
    KDN *c;
    KDN *pkdc;
    CSTACK *S;
    CELT *Check;
    LOCR L;
    ILP *ilp;
    ILC *ilc;
    ILPB *ilpb;
    double fWeight;
    double tempI;
    FLOAT dMin,dMax,min2,max2,d2,h2;
    FLOAT rCheck[3];
    FLOAT rOffset[3];
    FLOAT xParent,yParent,zParent;
    FLOAT dx[3],dir;
    int iStack,ism;
    int ix,iy,iz,bRep;
    int nMaxCheck,nCheck;
    int iCell,iSib,iCheckCell;
    int i,ii,j,n,id,pj,nActive,nTotActive;
    int iOpen;
    int nPart,nMaxPart;
    int nCell,nMaxCell;
    int nPartBucket,nMaxPartBucket;
#ifndef NO_TIMING
    struct timeval tv1, tv2;
    struct timezone tz;
#endif


    /*
    ** If we are doing the very active gravity then check that there is a very active tree!
    */
    if (bVeryActive) {
	assert(pkd->nVeryActive != 0);
	assert(pkd->nVeryActive == pkd->kdNodes[VAROOT].pUpper - pkd->kdNodes[VAROOT].pLower + 1);
	}	
    /*
    ** Initially we set our cell pointer to 
    ** point to the top tree.
    */
    c = pkd->kdTop;
    /*
    ** Allocate initial interaction lists.
    */
    nTotActive = 0;
    nPart = 0;
    nMaxPart = 500;
    ilp = malloc(nMaxPart*sizeof(ILP));
    assert(ilp != NULL);
    nCell = 0;
    nMaxCell = 500;
    ilc = malloc(nMaxCell*sizeof(ILC));
    assert(ilc != NULL);
    nPartBucket = 0;
    nMaxPartBucket = 500;
    ilpb = malloc(nMaxPartBucket*sizeof(ILPB));
    assert(ilpb != NULL);
    /*
    ** Allocate Checklist.
    */
    nMaxCheck = 2*nReps+1;
    nMaxCheck = nMaxCheck*nMaxCheck*nMaxCheck;	/* all replicas */
    iCell = pkd->iTopRoot;
    while ((iCell = c[iCell].iParent)) ++nMaxCheck; /* all top tree siblings */
    nMaxCheck = (nMaxCheck>1000)?nMaxCheck:1000;
    Check = malloc(nMaxCheck*sizeof(CELT));
    assert(Check != NULL);
    nCheck = 0;
    /*
    ** Allocate the stack.
    */
    S = malloc(pkd->nMaxDepth*sizeof(CSTACK));
    assert(S != NULL);
    iStack = -1;
    for (ism=0;ism<pkd->nMaxDepth;++ism) {
	S[ism].Check = malloc(nMaxCheck*sizeof(CELT));
	assert(S[ism].Check != NULL);
	}
    /*
    ** Clear local expansion.
    */
    momClearLocr(&L);
    /*
    ** First we add any replicas of the entire box
    ** to the Checklist.
    */
    for (ix=-nReps;ix<=nReps;++ix) {
	rOffset[0] = ix*pkd->fPeriod[0];
	for (iy=-nReps;iy<=nReps;++iy) {
	    rOffset[1] = iy*pkd->fPeriod[1];
	    for (iz=-nReps;iz<=nReps;++iz) {
		rOffset[2] = iz*pkd->fPeriod[2];
		bRep = ix || iy || iz;
		if (bRep || bVeryActive) {
		    Check[nCheck].iCell = ROOT;
		    /* If leaf of top tree, use root of
		       local tree.
		    */
		    if (c[ROOT].iLower) {
			Check[nCheck].id = -1;
			}
		    else {
			Check[nCheck].id = c[ROOT].pLower;
			}				    
		    for (j=0;j<3;++j) Check[nCheck].rOffset[j] = rOffset[j];
		    ++nCheck;
		    }
		if (bRep && bVeryActive) {
		    /*
		    ** Add the images of the very active tree to the checklist.
		    */
		    Check[nCheck].iCell = VAROOT;
		    Check[nCheck].id = mdlSelf(pkd->mdl);
		    for (j=0;j<3;++j) Check[nCheck].rOffset[j] = rOffset[j];
		    ++nCheck;
		    }
		}
	    }
	}
    if (!bVeryActive) {
	iCell = pkd->iTopRoot;
	iSib = SIBLING(iCell);
	while (iSib) {
	    if (c[iSib].iLower) {
		Check[nCheck].iCell = iSib;
		Check[nCheck].id = -1;
		}
	    else {
		/* If leaf of top tree, use root of local tree */
		Check[nCheck].iCell = ROOT;
		Check[nCheck].id = c[iSib].pLower;
		}
	    for (j=0;j<3;++j) Check[nCheck].rOffset[j] = 0.0;
	    ++nCheck;
	    iCell = c[iCell].iParent;
	    iSib = SIBLING(iCell);
	    }
	}
    /*
    ** Now switch the cell pointer to point to 
    ** the local tree.
    */
    c = pkd->kdNodes;
    if (bVeryActive) iCell = VAROOT;
    else iCell = ROOT;
    while (1) {
	while (1) {
	    /*
	    ** Process the Checklist.
	    */
	    ii = 0;
	    for (i=0;i<nCheck;++i) {
		id = Check[i].id;
		if (id == pkd->idSelf) {
		    pkdc = &pkd->kdNodes[Check[i].iCell];
		    n = pkdc->pUpper - pkdc->pLower + 1;
		    }
		else if (id < 0) {
		    pkdc = &pkd->kdTop[Check[i].iCell];
		    assert(pkdc->iLower != 0);
		    n = WALK_MINMULTIPOLE;  /* See check below */
		    }
		else {
		    pkdc = mdlAquire(pkd->mdl,CID_CELL,Check[i].iCell,id);
		    n = pkdc->pUpper - pkdc->pLower + 1;
		    }
		for (j=0;j<3;++j) rCheck[j] = pkdc->r[j] + Check[i].rOffset[j];
		/*
		** If this cell is not a bucket calculate the distance
		** between the center of masses of this cell and the check
		** list cell (d2). If this distance is larger than the sum of 
		** opening radii of the two cells (they are well 
		** seperated) then we accept the interaction.
		** If max2 <= CeckCell.fOpen2, then this Checkcell must be
		** opened since no subcell of the current cell will ever be 
		** to accept the Checkcell as an interaction. If neither 
		** condition is met then we need to keep this cell on the 
		** checklist.
		*/
		min2 = 0;	
		max2 = 0;
		d2 = 0;
		for (j=0;j<3;++j) {
		  dMin = fabs(rCheck[j] - c[iCell].bnd.fCenter[j]);
		  dMax = dMin + c[iCell].bnd.fMax[j];
		  dMin -= c[iCell].bnd.fMax[j];
		  if (dMin > 0) min2 += dMin*dMin;
		  max2 += dMax*dMax;
		  dx[j] = rCheck[j] - c[iCell].r[j]; 
		  d2 += dx[j]*dx[j];
		}
		/*
		** First test to see if we for sure open the checkcell. iOpen = 1;
		*/
		if (((c[iCell].iLower)?max2:min2) <= pkdc->fOpen2 || n < WALK_MINMULTIPOLE) iOpen = 1;
		/*
		** Second test to see if the checkcell can be accepted as a local expansion. iOpen = -1;
		*/
		else if (d2 > (sqrt(pkdc->fOpen2) + sqrt(c[iCell].fOpen2))*(sqrt(pkdc->fOpen2) + sqrt(c[iCell].fOpen2))) iOpen = -1;
		/*
		** Third test applies if the current cell is already a bucket, then test if it is an acceptable multipole (P-C). iOpen = -2;
		*/
		else if (!c[iCell].iLower && min2 > pkdc->fOpen2) iOpen = -2;
		/*
		** Otherwise we can't make a decision at this level in the tree and the cell must remain on the checklist. iOpen = 0;
		*/
		else iOpen = 0;
		if (iOpen == -1 || iOpen == -2) {
		  /*
		  ** While we accept the interaction on the grounds of the opening criterion
		  ** we still might have the case that the softening is larger than the opening
		  ** radius. In such cases we need to be quite careful since we can still use a 
		  ** softened monopole (like a softened P-P interaction).
		  */
#ifdef SOFTLINEAR
		  h2 = sqrt(pkdc->fSoft2) + sqrt(c[iCell].fSoft2);
		  h2 *= h2;
		  if (d2 < h2) iOpen = (c[iCell].iLower)?0:-3;
#endif
#ifdef SOFTSQUARE
		  h2 = 2*(pkdc->fSoft2 + c[iCell].fSoft2);
		  if (d2 < h2) iOpen = (c[iCell].iLower)?0:-3;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
		  /*
		  ** This is an asymmetric case as far as the softening is concerned.
		  */
		  h2 = 4*pkdc->fSoft2;
		  if (max2 < h2) iOpen = -3; 
		  else if (min2 < h2) iOpen = (c[iCell].iLower)?0:-3;
		}
#endif
		if (!c[iCell].iLower) assert(iOpen != 0);
/*
  printf("   i:%6d iCheck:%6d id:%2d iOpen:%2d\n",i,Check[i].iCell,id,iOpen);
*/
		if (iOpen > 0) {
		    /*
		    ** Contained! (or intersected in the case of reaching the bucket)
		    */
		    iCheckCell = pkdc->iLower;
		    if (iCheckCell) {
			/*
			** Open the cell.
			** Here we ASSUME that the children of 
			** pkdc are all in sequential memory order!
			** (the new tree build assures this)
			** (also true for the top tree)
			** We could do a prefetch here for non-local
			** cells.
			*/
			if (nCheck + 2 > nMaxCheck) {
			    nMaxCheck += 1000;
			    Check = realloc(Check,nMaxCheck*sizeof(CELT));
			    assert(Check != NULL);
			    for (ism=0;ism<pkd->nMaxDepth;++ism) {
				S[ism].Check = realloc(S[ism].Check,nMaxCheck*sizeof(CELT));
				assert(S[ism].Check != NULL);
				}
			    }
			Check[nCheck] = Check[i];
			Check[nCheck+1] = Check[i];
			/*
			** If we are opening a leaf of the top tree
			** we need to correctly set the processor id.
			** (this is a bit tricky)
			*/
			if (id < 0 ) {
			    if(pkd->kdTop[iCheckCell].pLower >= 0) {
				Check[nCheck].iCell = ROOT;
				Check[nCheck].id = pkd->kdTop[iCheckCell].pLower;
				}
			    else {
				Check[nCheck].iCell = iCheckCell;
				}
			    if(pkd->kdTop[iCheckCell+1].pLower >= 0) {
				Check[nCheck+1].iCell = ROOT;
				Check[nCheck+1].id = pkd->kdTop[iCheckCell+1].pLower;
				}
			    else {
				Check[nCheck+1].iCell = iCheckCell+1;
				}

			    }
			else {
			    Check[nCheck].iCell = iCheckCell;
			    Check[nCheck+1].iCell = iCheckCell+1;
			    }
			nCheck += 2;
			}
		    else {
			/*
			** Now I am trying to open a bucket, which means I place particles on the ilp
			** interaction list.
			*/
			if (id == pkd->idSelf) {
			    /*
			    ** Local Bucket Interaction.
			    ** Interact += Pacticles(pkdc);
			    */
			    if (nPart + n > nMaxPart) {
				nMaxPart += 500 + n;
				ilp = realloc(ilp,nMaxPart*sizeof(ILP));
				assert(ilp != NULL);	
				}
			    for (pj=pkdc->pLower;pj<=pkdc->pUpper;++pj) {
				ilp[nPart].iOrder = p[pj].iOrder;
				ilp[nPart].m = p[pj].fMass;
				ilp[nPart].x = p[pj].r[0] + Check[i].rOffset[0];
				ilp[nPart].y = p[pj].r[1] + Check[i].rOffset[1];
				ilp[nPart].z = p[pj].r[2] + Check[i].rOffset[2];
				ilp[nPart].vx = p[pj].v[0]; 
				ilp[nPart].vy = p[pj].v[1];
				ilp[nPart].vz = p[pj].v[2];
#ifdef SOFTLINEAR
				ilp[nPart].h = p[pj].fSoft;
#endif
#ifdef SOFTSQUARE
				ilp[nPart].twoh2 = 2*p[pj].fSoft*p[pj].fSoft;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
				ilp[nPart].fourh2 = 4*p[pj].fSoft*p[pj].fSoft;
#endif
				++nPart;
				}
			    }
			else {
			    /*
			    ** Remote Bucket Interaction.
			    ** Interact += Pacticles(pkdc);
			    */
			    if (nPart + n > nMaxPart) {
				nMaxPart += 500 + n;
				ilp = realloc(ilp,nMaxPart*sizeof(ILP));
				assert(ilp != NULL);	
				}
			    for (pj=pkdc->pLower;pj<=pkdc->pUpper;++pj) {
				pRemote = mdlAquire(pkd->mdl,CID_PARTICLE,pj,id);
				ilp[nPart].iOrder = pRemote->iOrder;
				ilp[nPart].m = pRemote->fMass;
				ilp[nPart].x = pRemote->r[0] + Check[i].rOffset[0];
				ilp[nPart].y = pRemote->r[1] + Check[i].rOffset[1];
				ilp[nPart].z = pRemote->r[2] + Check[i].rOffset[2];
				ilp[nPart].vx = pRemote->v[0]; 
				ilp[nPart].vy = pRemote->v[1];
				ilp[nPart].vz = pRemote->v[2];
#ifdef SOFTLINEAR
				ilp[nPart].h = pRemote->fSoft;
#endif
#ifdef SOFTSQUARE
				ilp[nPart].twoh2 = 2*pRemote->fSoft*pRemote->fSoft;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
				ilp[nPart].fourh2 = 4*pRemote->fSoft*pRemote->fSoft;
#endif
				++nPart;
				mdlRelease(pkd->mdl,CID_PARTICLE,pRemote);
				}
			    }
			/*
			** ...and we need to consider it in the timestepping part so place this cell
			** onto the particle-bucket list. This list is not used for force evaluation 
			** though.
			*/
			if (nPartBucket == nMaxPartBucket) {
			    nMaxPartBucket += 500;
			    ilpb = realloc(ilpb,nMaxPartBucket*sizeof(ILPB));
			    assert(ilpb != NULL);
			    }
			ilpb[nPartBucket].x = rCheck[0];
			ilpb[nPartBucket].y = rCheck[1];
			ilpb[nPartBucket].z = rCheck[2];
			ilpb[nPartBucket].m = pkdc->mom.m;   /* we really only need the mass here */
#ifdef SOFTLINEAR
		        ilpb[nPartBucket].h = sqrt(pkdc->fSoft2);
#endif
#ifdef SOFTSQUARE
		        ilpb[nPartBucket].twoh2 = 2*pkdc->fSoft2;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
		        ilpb[nPartBucket].fourh2 = 4*pkdc->fSoft2;
#endif
			++nPartBucket;
			}  /* end of opening a bucket */
		    }
		else if (iOpen == -1) {
		  /*
		  ** Local expansion accepted!
		  ** LocrAddMomr(locr,pkdc);
		  */
		  dir = 1.0/sqrt(d2);
		  t1 = -dir;
		  t2 = -3*dir;
		  t3r = -5;
		  t4r = -7;
		  momGenLocrAddMomr(&L,&pkdc->mom,dir,-dir,t1,t2,t3r,t4r,dx[0],dx[1],dx[2]);
		}
		else if (iOpen == -2) {
		    /*
		    ** No intersection, accept multipole!
		    ** Interact += Moment(pkdc);
		    */
		    if (nCell == nMaxCell) {
			nMaxCell += 500;
			ilc = realloc(ilc,nMaxCell*sizeof(ILC));
			assert(ilc != NULL);
			}
		    ilc[nCell].x = rCheck[0];
		    ilc[nCell].y = rCheck[1];
		    ilc[nCell].z = rCheck[2];
		    ilc[nCell].mom = pkdc->mom;
		    ++nCell;
		    }
		else if (iOpen == -3) {
		    /*
		    ** We accept this multipole from the opening criterion, but it is a softened
		    ** interaction, so we need to treat is as a softened monopole by putting it
		    ** on the particle interaction list.
		    */
		    if (nPart == nMaxPart) {
			nMaxPart += 500;
			ilp = realloc(ilp,nMaxPart*sizeof(ILP));
			assert(ilp != NULL);	
			}
		    ilp[nPart].iOrder = -1; /* set iOrder to negative value for time step criterion */
		    ilp[nPart].m = pkdc->mom.m;
		    ilp[nPart].x = rCheck[0];
		    ilp[nPart].y = rCheck[1];
		    ilp[nPart].z = rCheck[2];
		    ilp[nPart].vx = pkdc->v[0];
		    ilp[nPart].vy = pkdc->v[1];
		    ilp[nPart].vz = pkdc->v[2];
#ifdef SOFTLINEAR
		    ilp[nPart].h = sqrt(pkdc->fSoft2);
#endif
#ifdef SOFTSQUARE
		    ilp[nPart].twoh2 = 2*pkdc->fSoft2;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
		    ilp[nPart].fourh2 = 4*pkdc->fSoft2;
#endif
		    ++nPart;
		    /*
		    ** ...and we need to consider it in the timestepping part so place this cell
		    ** onto the particle-bucket list. This list is not used for force evaluation 
		    ** though.
		    */
		    if (nPartBucket == nMaxPartBucket) {
			nMaxPartBucket += 500;
			ilpb = realloc(ilpb,nMaxPartBucket*sizeof(ILPB));
			assert(ilpb != NULL);
			}
		    ilpb[nPartBucket].x = rCheck[0];
		    ilpb[nPartBucket].y = rCheck[1];
		    ilpb[nPartBucket].z = rCheck[2];
		    ilpb[nPartBucket].m = pkdc->mom.m;   /* we really only need the mass here */
#ifdef SOFTLINEAR
		    ilpb[nPartBucket].h = sqrt(pkdc->fSoft2);
#endif
#ifdef SOFTSQUARE
		    ilpb[nPartBucket].twoh2 = 2*pkdc->fSoft2;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
		    ilpb[nPartBucket].fourh2 = 4*pkdc->fSoft2;
#endif
		    ++nPartBucket;
		    }
		else {
		    Check[ii++] = Check[i];
		    }
		if (id >= 0 && id != pkd->idSelf) {
		    mdlRelease(pkd->mdl,CID_CELL,pkdc);
		    }
		}
	    nCheck = ii;
	    /*
	    ** Done processing of the Checklist.
	    ** Now prepare to proceed to the next deeper
	    ** level of the tree.
	    */
	    if (!c[iCell].iLower) break;
	    xParent = c[iCell].r[0];
	    yParent = c[iCell].r[1];
	    zParent = c[iCell].r[2];
	    iCell = c[iCell].iLower;
	    /*
	    ** Make sure all the check lists are long enough to handle 1 more cell.
	    */
	    if (nCheck == nMaxCheck) {
		nMaxCheck += 1000;
		Check = realloc(Check,nMaxCheck*sizeof(CELT));
		assert(Check != NULL);
		for (ism=0;ism<pkd->nMaxDepth;++ism) {
		    S[ism].Check = realloc(S[ism].Check,nMaxCheck*sizeof(CELT));
		    assert(S[ism].Check != NULL);
		    }
		}
	    /*
	    ** Check iCell is active. We eventually want to just to a 
	    ** rung check here when we start using tree repair, but 
	    ** for now this is just as good.
	    */
	    if (c[iCell].iActive & TYPE_ACTIVE) {
		/*
		** iCell is active, continue processing it.
		** Put the sibling onto the checklist.
		*/
		Check[nCheck].iCell = iCell + 1;
		Check[nCheck].id = pkd->idSelf;
		for (j=0;j<3;++j) Check[nCheck].rOffset[j] = 0.0;
		++nCheck;
		/*
		** Test whether the sibling is active as well.
		** If not we don't push it onto the stack, but we
		** have to be careful to not pop the stack when we
		** hit the sibling. See the goto InactiveAscend below
		** for how this is done.
		*/
		if (c[iCell+1].iActive & TYPE_ACTIVE) {
		    /*
		    ** Sibling is active as well.
		    ** Push Checklist for the sibling onto the stack
		    ** before proceeding deeper in the tree.
		    */
		    ++iStack;
		    S[iStack].nPart = nPart;
		    S[iStack].nCell = nCell;
		    S[iStack].nPartBucket = nPartBucket;
		    S[iStack].nCheck = nCheck;
		    for (i=0;i<nCheck;++i) S[iStack].Check[i] = Check[i];
		    S[iStack].Check[nCheck-1].iCell = iCell;
		    S[iStack].L = L;
		    momShiftLocr(&S[iStack].L,
				 c[iCell+1].r[0] - xParent,
				 c[iCell+1].r[1] - yParent,
				 c[iCell+1].r[2] - zParent);
		    }
		}
	    else {
		/*
		** Skip iCell, but add it to the Checklist.
		** No need to push anything onto the stack here.
		*/
		Check[nCheck].iCell = iCell;
		Check[nCheck].id = pkd->idSelf;
		for (j=0;j<3;++j) Check[nCheck].rOffset[j] = 0.0;
		++nCheck;
		/*
		** Move onto processing the sibling.
		*/
		++iCell;
		}
	    momShiftLocr(&L,c[iCell].r[0] - xParent,
			 c[iCell].r[1] - yParent,
			 c[iCell].r[2] - zParent);
	    }
	/*
	** Now the interaction list should be complete and the 
	** Checklist should be empty! Calculate gravity on this
	** Bucket!
	*/
	assert(nCheck == 0);
	/*
	** We no longer add *this bucket to any interaction list, this is now done with an 
	** N(N-1)/2 loop in pkdBucketInteract().
	*/
	pkdc = &c[iCell];
	/*
	** Now calculate gravity on this bucket!
	*/

#ifndef NO_TIMING
	gettimeofday(&tv1, &tz);
#endif

	tempI = *pdFlop;
	nActive = pkdGravInteract(pkd,pkdc,&L,ilp,nPart,ilc,nCell,ilpb,nPartBucket,pdFlop);
	/*
	** Note that if Ewald is being performed we need to factor this
	** constant cost into the load balancing weights.
	*/
	if (bEwald) {
	    *pdFlop += pkdBucketEwald(pkd,&pkd->kdNodes[iCell],nReps,fEwCut,4);
	    }
	if (nActive) {
	    fWeight = (*pdFlop-tempI)/nActive;

#ifndef NO_TIMING
	    gettimeofday(&tv2, &tz);
	    tv2.tv_sec -= tv1.tv_sec;
	    if ( tv2.tv_usec < tv1.tv_usec ) {
		tv2.tv_sec--;
		tv2.tv_usec += 1000000;
	    }
	    tv2.tv_usec -= tv1.tv_usec;
	    fWeight = tv2.tv_sec + (float)tv2.tv_usec/1000000.0;
	    if ( fWeight == 0.0 ) fWeight = 0.000001;
	    fWeight /= nActive;
#endif
	    pkdBucketWeight(pkd,iCell,fWeight);
/*
  printf("%6d nPart:%5d nCell:%5d\n",iCell,nPart,nCell);
*/
	    *pdPartSum += nActive*nPart;
	    *pdCellSum += nActive*nCell;
	    nTotActive += nActive;
	    }

	while (iCell & 1) {
	InactiveAscend:
	    iCell = c[iCell].iParent;
	    if (!iCell) {
		/*
		** Make sure stack is empty and free its storage.
		*/
		assert(iStack == -1);
		for (ism=0;ism<pkd->nMaxDepth;++ism) {
		    free(S[ism].Check);
		    }
		free(S);
		/*
		** Free checklist storage.
		*/
		free(Check);
		/*
		** Free interaction lists.
		*/
		free(ilp);
		free(ilc);
		free(ilpb);
		return(nTotActive);
		}
	    }
	++iCell;
	if (!(c[iCell].iActive & TYPE_ACTIVE)) goto InactiveAscend;
	/*
	** Pop the Checklist from the top of the stack,
	** also getting the state of the interaction list.
	*/
	nPart = S[iStack].nPart;
	nCell = S[iStack].nCell;
	nPartBucket = S[iStack].nPartBucket;
	nCheck = S[iStack].nCheck;
	for (i=0;i<nCheck;++i) Check[i] = S[iStack].Check[i];
	L = S[iStack].L;
	--iStack;
	}
    }

void pkdLocalWalk(PKD pkd,int iParticle,int bRep,FLOAT rOffset[3],
		  ILC **pilc, int *pnCell, int *nMaxCell, ILP **pilp, int *pnPart, int *nMaxPart, ILPB **pilpb, int *pnPartBucket, int *nMaxPartBucket)
    {
    PARTICLE *p, *ppart;
    KDN *pkdn;
    int iCell,n,pj,iOpen, i;
    FLOAT x[3];
    FLOAT d2;
    int nCell = *pnCell;
    int nPart = *pnPart;
    int nPartBucket = *pnPartBucket;
    ILC *ilc = *pilc;
    ILP *ilp = *pilp;
    ILPB *ilpb = *pilpb;

    p = pkd->pStore;
    ppart = &p[iParticle];
    iCell = ROOT;
    while (iCell != -1) {
	pkdn = &pkd->kdNodes[iCell];
	for(i = 0; i < 3; i++)
	    x[i] = pkdn->r[i] + rOffset[i];
	d2 = 0.0;
	for(i = 0; i < 3; i++)
	    d2 += (ppart->r[i] - x[i])*(ppart->r[i] - x[i]);
	
	if(d2 > pkdn->fOpen2
	   && (pkdn->pUpper - pkdn->pLower + 1) >= WALK_MINMULTIPOLE) {
#ifdef SOFTLINEAR
	    h2 = sqrt(pkdn->fSoft2) + ppart->fSoft;
	    h2 *= h2;
	    if (d2 > h2) iOpen = -1;
	    else iOpen = -2; /* means we treat this cell as a softened monopole */
#endif
#ifdef SOFTSQUARE
	    h2 = 2*(pkdn->fSoft2 + ppart->fSoft*ppart->fSoft);
	    if (d2 > h2) iOpen = -1;
	    else iOpen = -2; /* means we treat this cell as a softened monopole */
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
	    if (d2 > 4*pkdn->fSoft2) iOpen = -1;
	    else iOpen = -2; /* means we treat this cell as a softened monopole */
#endif
	    }
	else iOpen = 1;
	    
	if (iOpen > 0) {
	    if (pkdn->iLower != 0) {
		/*
		** Open cell.
		*/
		iCell = pkdn->iLower;
		}
	    else {
		/*
		** Bucket Interaction.
		*/
		n = pkdn->pUpper - pkdn->pLower + 1;
		if (nPart + n > *nMaxPart) {
		    *nMaxPart += *nMaxPart + n;
		    ilp = realloc(ilp,*nMaxPart*sizeof(ILP));
		    assert(ilp != NULL);
		    }
		for (pj=pkdn->pLower;pj<=pkdn->pUpper;++pj,++nPart) {
		    ilp[nPart].iOrder = p[pj].iOrder;
		    ilp[nPart].x = p[pj].r[0] + rOffset[0];
		    ilp[nPart].y = p[pj].r[1] + rOffset[1];
		    ilp[nPart].z = p[pj].r[2] + rOffset[2];
		    ilp[nPart].vx = p[pj].v[0];
		    ilp[nPart].vy = p[pj].v[1];
		    ilp[nPart].vz = p[pj].v[2];
		    ilp[nPart].m = p[pj].fMass;
#ifdef SOFTLINEAR
		    ilp[nPart].h = p[pj].fSoft;
#endif
#ifdef SOFTSQUARE
		    ilp[nPart].twoh2 = 2*p[pj].fSoft*p[pj].fSoft;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
		    ilp[nPart].fourh2 = 4*p[pj].fSoft*p[pj].fSoft;
#endif
		    }
		/*
		** ...and we need to consider it in the timestepping part so place this cell
		** onto the particle-bucket list. This list is not used for force evaluation 
		** though.
		*/
		if (nPartBucket == *nMaxPartBucket) {
		    *nMaxPartBucket += 500;
		    ilpb = realloc(ilpb,*nMaxPartBucket*sizeof(ILPB));
		    assert(ilpb != NULL);
		    }
		ilpb[nPartBucket].x = x[0];
		ilpb[nPartBucket].y = x[1];
		ilpb[nPartBucket].z = x[2];
		ilpb[nPartBucket].m = pkdn->mom.m;
#ifdef SOFTLINEAR
		ilpb[nPartBucket].h = sqrt(pkdn->fSoft2);
#endif
#ifdef SOFTSQUARE
		ilpb[nPartBucket].twoh2 = 2*pkdn->fSoft2;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
		ilpb[nPartBucket].fourh2 = 4*pkdn->fSoft2;
#endif
		++nPartBucket;

		while(iCell & 1) {
		    iCell = pkdn->iParent;
		    pkdn = &pkd->kdNodes[iCell];
		    }
		if(iCell == 0) break;
		iCell++; /* N.B. siblings are consecutive in memory */
		}
	    }
	else if (iOpen == -1) {
	    /*
	    ** Cell-Bucket interaction accepted.
	    */
	    if (nCell == *nMaxCell) {
		*nMaxCell *= 2;
		ilc = realloc(ilc,*nMaxCell*sizeof(ILC));
		assert(ilc != NULL);
		}
	    ilc[nCell].x = x[0];
	    ilc[nCell].y = x[1];
	    ilc[nCell].z = x[2];
	    ilc[nCell].mom = pkdn->mom;
	    ++nCell;
	    while(iCell & 1) {
		iCell = pkdn->iParent;
		pkdn = &pkd->kdNodes[iCell];
		}
	    if(iCell == 0) break;
	    iCell++; /* N.B. siblings are consecutive in memory */
	    }
	else if (iOpen == -2) {
	    if (nPart == *nMaxPart) {
		*nMaxPart += *nMaxPart ;
		ilp = realloc(ilp,*nMaxPart*sizeof(ILP));
		assert(ilp != NULL);
		}
	    ilp[nPart].iOrder = -1; /* set iOrder to negative value for time step criterion */
	    ilp[nPart].x = x[0];
	    ilp[nPart].y = x[1];
	    ilp[nPart].z = x[2];
	    ilp[nPart].vx = pkdn->v[0];
	    ilp[nPart].vy = pkdn->v[1];
	    ilp[nPart].vz = pkdn->v[2];
	    ilp[nPart].m = pkdn->mom.m;
#ifdef SOFTLINEAR
	    ilp[nPart].h = sqrt(pkdn->fSoft2);
#endif
#ifdef SOFTSQUARE
	    ilp[nPart].twoh2 = 2*pkdn->fSoft2;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
	    ilp[nPart].fourh2 = 4*pkdn->fSoft2;
#endif
	    ++nPart;
	    /*
	    ** ...and we need to consider it in the timestepping part so place this cell
	    ** onto the particle-bucket list. This list is not used for force evaluation 
	    ** though.
	    */
	    if (nPartBucket == *nMaxPartBucket) {
		*nMaxPartBucket += 500;
		ilpb = realloc(ilpb,*nMaxPartBucket*sizeof(ILPB));
		assert(ilpb != NULL);
		}
	    ilpb[nPartBucket].x = x[0];
	    ilpb[nPartBucket].y = x[1];
	    ilpb[nPartBucket].z = x[2];
	    ilpb[nPartBucket].m = pkdn->mom.m;   /* we really only need the mass here */
#ifdef SOFTLINEAR
	    ilpb[nPartBucket].h = sqrt(pkdn->fSoft2);
#endif
#ifdef SOFTSQUARE
	    ilpb[nPartBucket].twoh2 = 2*pkdn->fSoft2;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
	    ilpb[nPartBucket].fourh2 = 4*pkdn->fSoft2;
#endif
	    ++nPartBucket;
	    while(iCell & 1) {
		iCell = pkdn->iParent;
		pkdn = &pkd->kdNodes[iCell];
		}
	    if(iCell == 0) break;
	    iCell++; /* N.B. siblings are consecutive in memory */
	    }
	}
    /*
     * Pass out any changes in the lists
     */
    *pnPart = nPart;
    *pnCell = nCell;
    *pnPartBucket = nPartBucket;
    *pilc = ilc;
    *pilp = ilp;
    *pilpb = ilpb;
    }

void pkdRemoteWalk(PKD pkd,int iParticle,int id,FLOAT rOffset[3],
		   ILC **pilc, int *pnCell, int *nMaxCell, ILP **pilp, int *pnPart, int *nMaxPart, ILPB **pilpb, int *pnPartBucket, int *nMaxPartBucket)
    {
    PARTICLE *p,*ppart;
    KDN *pkdn;
    int iCell,n,j,iOpen, i;
    FLOAT x[3], d2;
    int nCell = *pnCell;
    int nPart = *pnPart;
    int nPartBucket = *pnPartBucket;
    ILC *ilc = *pilc;
    ILP *ilp = *pilp;
    ILPB *ilpb = *pilpb;

    assert(id != pkd->idSelf);
    ppart = &pkd->pStore[iParticle];
    iCell = ROOT;
    while (iCell != -1) {
	pkdn = mdlAquire(pkd->mdl,CID_CELL,iCell,id);
	for(i = 0; i < 3; i++)
	    x[i] = pkdn->r[i] + rOffset[i];
	d2 = 0.0;
	for(i = 0; i < 3; i++)
	    d2 += (ppart->r[i] - x[i])*(ppart->r[i] - x[i]);
	
	if(d2 > pkdn->fOpen2
	   && (pkdn->pUpper - pkdn->pLower + 1) >= WALK_MINMULTIPOLE) {
#ifdef SOFTLINEAR
	    h2 = sqrt(pkdn->fSoft2) + ppart->fSoft;
	    h2 *= h2;
	    if (d2 > h2) iOpen = -1;
	    else iOpen = -2; /* means we treat this cell as a softened monopole */
#endif
#ifdef SOFTSQUARE
	    h2 = 2*(pkdn->fSoft2 + ppart->fSoft*ppart->fSoft);
	    if (d2 > h2) iOpen = -1;
	    else iOpen = -2; /* means we treat this cell as a softened monopole */
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
	    if (d2 > 4*pkdn->fSoft2) iOpen = -1;
	    else iOpen = -2; /* means we treat this cell as a softened monopole */
#endif
	    }
	else iOpen = 1;
	if (iOpen > 0) {
	    if (pkdn->iLower != 0) {
		/*
		** Open cell.
		*/
		iCell = pkdn->iLower;
		mdlRelease(pkd->mdl,CID_CELL,pkdn);
		}
	    else {
		/*
		** Bucket-Bucket Interaction.
		*/
		n = pkdn->pUpper - pkdn->pLower + 1;
		if (nPart + n > *nMaxPart) {
		    *nMaxPart += *nMaxPart + n;
		    ilp = realloc(ilp,*nMaxPart*sizeof(ILP));
		    assert(ilp != NULL);
		    }
		for (j=0;j<n;++j,++nPart) {
		    p = mdlAquire(pkd->mdl,CID_PARTICLE,pkdn->pLower+j,id);
		    ilp[nPart].iOrder = p->iOrder;
		    ilp[nPart].x = p->r[0] + rOffset[0];
		    ilp[nPart].y = p->r[1] + rOffset[1];
		    ilp[nPart].z = p->r[2] + rOffset[2];
		    ilp[nPart].vx = p->v[0];
		    ilp[nPart].vy = p->v[1];
		    ilp[nPart].vz = p->v[2];
		    ilp[nPart].m = p->fMass;
#ifdef SOFTLINEAR
		    ilp[nPart].h = p->fSoft;
#endif
#ifdef SOFTSQUARE
		    ilp[nPart].twoh2 = 2*p->fSoft*p->fSoft;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
		    ilp[nPart].fourh2 = 4*p->fSoft*p->fSoft;
#endif
		    mdlRelease(pkd->mdl,CID_PARTICLE,p);
		    }

		/*
		** ...and we need to consider it in the timestepping part so place this cell
		** onto the particle-bucket list. This list is not used for force evaluation 
		** though.
		*/
		if (nPartBucket == *nMaxPartBucket) {
		    *nMaxPartBucket += 500;
		    ilpb = realloc(ilpb,*nMaxPartBucket*sizeof(ILPB));
		    assert(ilpb != NULL);
		    }
		ilpb[nPartBucket].x = x[0];
		ilpb[nPartBucket].y = x[1];
		ilpb[nPartBucket].z = x[2];
		ilpb[nPartBucket].m = pkdn->mom.m;   /* we really only need the mass here */
#ifdef SOFTLINEAR
		ilpb[nPartBucket].h = sqrt(pkdn->fSoft2);
#endif
#ifdef SOFTSQUARE
		ilpb[nPartBucket].twoh2 = 2*pkdn->fSoft2;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
		ilpb[nPartBucket].fourh2 = 4*pkdn->fSoft2;
#endif
		++nPartBucket;

		while(iCell & 1) {
		    iCell = pkdn->iParent;
		    mdlRelease(pkd->mdl,CID_CELL,pkdn);
		    pkdn = mdlAquire(pkd->mdl,CID_CELL,iCell,id);
		    }
		mdlRelease(pkd->mdl,CID_CELL,pkdn);
		if(iCell == 0) break;
		iCell++; /* N.B. siblings are consecutive in memory */
		}
	    }
	else if (iOpen == -1) {
	    /*
	    ** Cell-Bucket interaction accepted.
	    */
	    if (nCell == *nMaxCell) {
		*nMaxCell *= 2;
		ilc = realloc(ilc,*nMaxCell*sizeof(ILC));
		assert(ilc != NULL);
		}
	    ilc[nCell].x = x[0];
	    ilc[nCell].y = x[1];
	    ilc[nCell].z = x[2];
	    ilc[nCell].mom = pkdn->mom;
	    ++nCell;
	    while(iCell & 1) {
		iCell = pkdn->iParent;
		mdlRelease(pkd->mdl,CID_CELL,pkdn);
		pkdn = mdlAquire(pkd->mdl,CID_CELL,iCell,id);
		}
	    mdlRelease(pkd->mdl,CID_CELL,pkdn);
	    if(iCell == 0) break;
	    iCell++; /* N.B. siblings are consecutive in memory */
	    }
	else if (iOpen == -2) {
	    if (nPart == *nMaxPart) {
		*nMaxPart += *nMaxPart ;
		ilp = realloc(ilp,*nMaxPart*sizeof(ILP));
		assert(ilp != NULL);
		}
	    ilp[nPart].iOrder = -1; /* set iOrder to negative value for time step criterion */
	    ilp[nPart].x = x[0];
	    ilp[nPart].y = x[1];
	    ilp[nPart].z = x[2];
	    ilp[nPart].vx = pkdn->v[0];
	    ilp[nPart].vy = pkdn->v[1];
	    ilp[nPart].vz = pkdn->v[2];
	    ilp[nPart].m = pkdn->mom.m;
#ifdef SOFTLINEAR
	    ilp[nPart].h = sqrt(pkdn->fSoft2);
#endif
#ifdef SOFTSQUARE
	    ilp[nPart].twoh2 = 2*pkdn->fSoft2;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
	    ilp[nPart].fourh2 = 4*pkdn->fSoft2;
#endif
	    ++nPart;
	    /*
	    ** ...and we need to consider it in the timestepping part so place this cell
	    ** onto the particle-bucket list. This list is not used for force evaluation 
	    ** though.
	    */
	    if (nPartBucket == *nMaxPartBucket) {
		*nMaxPartBucket += 500;
		ilpb = realloc(ilpb,*nMaxPartBucket*sizeof(ILPB));
		assert(ilpb != NULL);
		}
	    ilpb[nPartBucket].x = x[0];
	    ilpb[nPartBucket].y = x[1];
	    ilpb[nPartBucket].z = x[2];
	    ilpb[nPartBucket].m = pkdn->mom.m;   /* we really only need the mass here */
#ifdef SOFTLINEAR
	    ilpb[nPartBucket].h = sqrt(pkdn->fSoft2);
#endif
#ifdef SOFTSQUARE
	    ilpb[nPartBucket].twoh2 = 2*pkdn->fSoft2;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
	    ilpb[nPartBucket].fourh2 = 4*pkdn->fSoft2;
#endif
	    ++nPartBucket;
	    while(iCell & 1) {
		iCell = pkdn->iParent;
		mdlRelease(pkd->mdl,CID_CELL,pkdn);
		pkdn = mdlAquire(pkd->mdl,CID_CELL,iCell,id);
		}
	    mdlRelease(pkd->mdl,CID_CELL,pkdn);
	    if(iCell == 0) break;
	    iCell++; /* N.B. siblings are consecutive in memory */
	    }
	}
    /*
    **  Pass out any changes in the lists
    */
    *pnPart = nPart;
    *pnCell = nCell;
    *pnPartBucket = nPartBucket;
    *pilc = ilc;
    *pilp = ilp;
    *pilpb = ilpb;
    }

void pkdParticleWalk(PKD pkd,int iParticle,int nReps, ILP **pilp, int *pnPart, int *pnMaxPart, ILC **pilc, int *pnCell, int *pnMaxCell, ILPB **pilpb, int *pnPartBucket, int *pnMaxPartBucket)
    {
    KDN *pkdn;
    int iCell,id,ix,iy,iz,bRep,iOpen, i;
    FLOAT x[3], d2, rOffset[3];
    ILP *ilp = *pilp;
    ILC *ilc = *pilc;
    ILPB *ilpb = *pilpb;
    PARTICLE *ppart;
	
    int nPart = 0;
    int nMaxPart = *pnMaxPart;
    int nCell = 0;
    int nMaxCell = *pnMaxCell;
    int nPartBucket = 0;
    int nMaxPartBucket = *pnMaxPartBucket;

    ppart = &pkd->pStore[iParticle];

    for (ix=-nReps;ix<=nReps;++ix) {
	if (ix && pkd->fPeriod[0] >= FLOAT_MAXVAL) continue;
	rOffset[0] = ix*pkd->fPeriod[0];
	for (iy=-nReps;iy<=nReps;++iy) {
	    if (iy && pkd->fPeriod[1] >= FLOAT_MAXVAL) continue;
	    rOffset[1] = iy*pkd->fPeriod[1];
	    for (iz=-nReps;iz<=nReps;++iz) {
		if (iz && pkd->fPeriod[2] >= FLOAT_MAXVAL) continue;
		rOffset[2] = iz*pkd->fPeriod[2];
		bRep = ix || iy || iz;
		/*
		** Walk the top tree first, finding local trees to
		** continue walking.
		*/
		iCell = ROOT;
		while (1) {
		    id = pkd->kdTop[iCell].pLower;
		    if (id == pkd->idSelf) {
			pkdLocalWalk(pkd,iParticle,bRep,rOffset, &ilc, &nCell, &nMaxCell, &ilp, &nPart, &nMaxPart, &ilpb, &nPartBucket, &nMaxPartBucket);
			SETNEXT(iCell);
			if (iCell == ROOT) break;
			}
		    else if (id >= 0) {
			pkdRemoteWalk(pkd,iParticle,id,rOffset, &ilc, &nCell, &nMaxCell, &ilp, &nPart, &nMaxPart, &ilpb, &nPartBucket, &nMaxPartBucket);
			SETNEXT(iCell);
			if (iCell == ROOT) break;
			}
		    else {
			pkdn = &pkd->kdTop[iCell];
			for(i = 0; i < 3; i++)
			    x[i] = pkdn->r[i] + rOffset[i];
			d2 = 0.0;
			for(i = 0; i < 3; i++)
			    d2 += (ppart->r[i] - x[i])*(ppart->r[i] - x[i]);

			if(d2 > pkdn->fOpen2
			   && (pkdn->pUpper - pkdn->pLower + 1) >= WALK_MINMULTIPOLE) {
#ifdef SOFTLINEAR
			    h2 = sqrt(pkdn->fSoft2) + ppart->fSoft;
			    h2 *= h2;
			    if (d2 > h2) iOpen = -1;
			    else iOpen = -2; /* means we treat this cell as a softened monopole */
#endif
#ifdef SOFTSQUARE
			    h2 = 2*(pkdn->fSoft2 + ppart->fSoft*ppart->fSoft);
			    if (d2 > h2) iOpen = -1;
			    else iOpen = -2; /* means we treat this cell as a softened monopole */
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
			    if (d2 > 4*pkdn->fSoft2) iOpen = -1;
			    else iOpen = -2; /* means we treat this cell as a softened monopole */
#endif
			    }
			else iOpen = 1;
			
			if (iOpen > 0) {
			    /*
			    ** Open top cell.
			    */
			    iCell = LOWER(iCell);
			    }
			else if (iOpen == -1) {
			    /*
			    ** Cell-Bucket interaction accepted.
			    ** Can use Newtonian Cell interaction.
			    */
			    if (nCell == nMaxCell) {
				nMaxCell *= 2;
				ilc = realloc(ilc, nMaxCell*sizeof(ILC));
				assert(ilc != NULL);
				}
			    ilc[nCell].x = x[0];
			    ilc[nCell].y = x[1];
			    ilc[nCell].z = x[2];
			    ilc[nCell].mom = pkdn->mom;
			    ++nCell;
			    SETNEXT(iCell);
			    if (iCell == ROOT) break;
			    }
			else if (iOpen == -2) {
			    if (nPart == nMaxPart) {
				nMaxPart += nMaxPart ;
				ilp = realloc(ilp,nMaxPart*sizeof(ILP));
				assert(ilp != NULL);
				}
			    ilp[nPart].iOrder = -1; /* set iOrder to negative value for time step criterion */
			    ilp[nPart].x = x[0];
			    ilp[nPart].y = x[1];
			    ilp[nPart].z = x[2];
			    ilp[nPart].vx = pkdn->v[0];
			    ilp[nPart].vy = pkdn->v[1];
			    ilp[nPart].vz = pkdn->v[2];
			    ilp[nPart].m = pkdn->mom.m;
#ifdef SOFTLINEAR
			    ilp[nPart].h = sqrt(pkdn->fSoft2);
#endif
#ifdef SOFTSQUARE
			    ilp[nPart].twoh2 = 2*pkdn->fSoft2;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
			    ilp[nPart].fourh2 = 4*pkdn->fSoft2;
#endif
			    ++nPart;
			    /*
			    ** ...and we need to consider it in the timestepping part so place this cell
			    ** onto the particle-bucket list. This list is not used for force evaluation 
			    ** though.
			    */
			    if (nPartBucket == nMaxPartBucket) {
				nMaxPartBucket += 500;
				ilpb = realloc(ilpb,nMaxPartBucket*sizeof(ILPB));
				assert(ilpb != NULL);
				}
			    ilpb[nPartBucket].x = x[0];
			    ilpb[nPartBucket].y = x[1];
			    ilpb[nPartBucket].z = x[2];
			    ilpb[nPartBucket].m = pkdn->mom.m;   /* we really only need the mass here */
#ifdef SOFTLINEAR
			    ilpb[nPartBucket].h = sqrt(pkdn->fSoft2);
#endif
#ifdef SOFTSQUARE
			    ilpb[nPartBucket].twoh2 = 2*pkdn->fSoft2;
#endif
#if !defined(SOFTLINEAR) && !defined(SOFTSQUARE)
			    ilpb[nPartBucket].fourh2 = 4*pkdn->fSoft2;
#endif
			    ++nPartBucket;
			    SETNEXT(iCell);
			    if (iCell == ROOT) break;
			    }
			}
		    } /* of Top tree walk */
		} /* of iz */
	    } /* of iy */
	} /* of ix */
    *pilp = ilp;
    *pnPart = nPart;
    *pnMaxCell = nMaxCell;
    *pilc = ilc;
    *pnCell = nCell;
    *pnMaxPart = nMaxPart;
    *pilpb = ilpb;
    *pnPartBucket = nPartBucket;
    *pnMaxPartBucket = nMaxPartBucket;
    }
