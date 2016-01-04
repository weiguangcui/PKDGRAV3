#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <math.h>
#include <assert.h>
#include <errno.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#include <linux/fs.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef __linux__
#include <sys/resource.h>
#endif
#ifdef USE_ITT
#include "ittnotify.h"
#endif
#include "cudautil.h"
#include "pkd.h"
#include "ewald.h"
#include "walk.h"
#include "grav.h"
#include "mdl.h"
#include "tipsydefs.h"
#include "outtype.h"
#include "parameters.h"
#include "cosmo.h"

double pkdGetTimer(PKD pkd,int iTimer) {
    return(pkd->ti[iTimer].sec);
    }

double pkdGetSystemTimer(PKD pkd,int iTimer) {
    return(pkd->ti[iTimer].system_sec);
    }

double pkdGetWallClockTimer(PKD pkd,int iTimer) {
    return(pkd->ti[iTimer].wallclock_sec);
    }


void pkdClearTimer(PKD pkd,int iTimer) {
   int i;

    if (iTimer >= 0) {
	pkd->ti[iTimer].sec = 0.0;
	pkd->ti[iTimer].system_sec = 0.0;
	pkd->ti[iTimer].wallclock_sec = 0.0;
	pkd->ti[iTimer].iActive = 0;
	}
    else {
	for (i=0;i<MAX_TIMERS;++i) {
	    pkd->ti[i].sec = 0.0;
	    pkd->ti[i].system_sec = 0.0;
	    pkd->ti[i].wallclock_sec = 0.0;
	    pkd->ti[i].iActive = 0;
	    }
	}
    }


void pkdStartTimer(PKD pkd,int iTimer) {
    struct timeval tv;

    pkd->ti[iTimer].iActive++;

    if (pkd->ti[iTimer].iActive == 1) {
#ifdef _MSC_VER
        FILETIME ft;
        uint64_t clock;
	GetSystemTimeAsFileTime(&ft);
	clock = ft.dwHighDateTime;
	clock <<= 32;
	clock |= ft.dwLowDateTime;
	/* clock is in 100 nano-second units */
	pkd->ti[iTimer].wallclock_stamp = clock / 10000000.0;
#else
	gettimeofday(&tv,NULL);
	pkd->ti[iTimer].wallclock_stamp = tv.tv_sec + 1e-6*(double) tv.tv_usec;
#endif
	pkd->ti[iTimer].stamp = mdlCpuTimer(pkd->mdl);
#ifdef __linux__
	{
	    struct rusage ru;

	    getrusage(RUSAGE_SELF,&ru);
	    pkd->ti[iTimer].system_stamp = (double)ru.ru_stime.tv_sec + 1e-6*(double)ru.ru_stime.tv_usec;
	    }
#endif
	}
    }


void pkdStopTimer(PKD pkd,int iTimer) {
    double sec;
#ifdef _MSC_VER
    FILETIME ft;
    uint64_t clock;
#else
    struct timeval tv;
#endif
    sec = -pkd->ti[iTimer].stamp;
    pkd->ti[iTimer].stamp = mdlCpuTimer(pkd->mdl);
    sec += pkd->ti[iTimer].stamp;
    if (sec < 0.0) sec = 0.0;
    pkd->ti[iTimer].sec += sec;

    sec = -pkd->ti[iTimer].wallclock_stamp;

#ifdef _MSC_VER
    GetSystemTimeAsFileTime(&ft);
    clock = ft.dwHighDateTime;
    clock <<= 32;
    clock |= ft.dwLowDateTime;
    /* clock is in 100 nano-second units */
    pkd->ti[iTimer].wallclock_stamp = clock / 10000000.0;
#else
    gettimeofday(&tv,NULL);
    pkd->ti[iTimer].wallclock_stamp = tv.tv_sec + 1e-6*(double) tv.tv_usec;
#endif
    sec += pkd->ti[iTimer].wallclock_stamp;
    if (sec < 0.0) sec = 0.0;
    pkd->ti[iTimer].wallclock_sec += sec;

#ifdef __linux__
	{
	struct rusage ru;

	sec = -pkd->ti[iTimer].system_stamp;
	getrusage(RUSAGE_SELF,&ru);
	pkd->ti[iTimer].system_stamp = ((double)ru.ru_stime.tv_sec + 1e-6*(double)ru.ru_stime.tv_usec);
	sec += pkd->ti[iTimer].system_stamp;
	if (sec < 0.0) sec = 0.0;
	pkd->ti[iTimer].system_sec += sec;
	}
#endif
    pkd->ti[iTimer].iActive--;
    }

/* Add a NODE structure: assume double alignment */
static int pkdNodeAddStruct(PKD pkd,int n) {
    int iOffset = pkd->iTreeNodeSize;
    mdlassert( pkd->mdl, pkd->kdNodeListPRIVATE == NULL );
    mdlassert( pkd->mdl, (iOffset & (sizeof(double)-1)) == 0 );
    pkd->iTreeNodeSize += n;
    return iOffset;
    }
/* Add n doubles to the node structure */
static int pkdNodeAddDouble(PKD pkd,int n) {
    int iOffset = pkd->iTreeNodeSize;
    mdlassert( pkd->mdl, pkd->kdNodeListPRIVATE == NULL );
    mdlassert( pkd->mdl, (iOffset & (sizeof(double)-1)) == 0 );
    pkd->iTreeNodeSize += sizeof(double) * n;
    return iOffset;
    }
/* Add n floats to the node structure */
static int pkdNodeAddFloat(PKD pkd,int n) {
    int iOffset = pkd->iTreeNodeSize;
    mdlassert( pkd->mdl, pkd->kdNodeListPRIVATE == NULL );
    mdlassert( pkd->mdl, (iOffset & (sizeof(float)-1)) == 0 );
    pkd->iTreeNodeSize += sizeof(float) * n;
    return iOffset;
    }
/* Add n 64-bit integers to the node structure */
static int pkdNodeAddInt64(PKD pkd,int n) {
    int iOffset = pkd->iTreeNodeSize;
    mdlassert( pkd->mdl, pkd->kdNodeListPRIVATE == NULL );
    mdlassert( pkd->mdl, (iOffset & (sizeof(int64_t)-1)) == 0 );
    pkd->iTreeNodeSize += sizeof(int64_t) * n;
    return iOffset;
    }
/* Add n 32-bit integers to the node structure */
static int pkdNodeAddInt32(PKD pkd,int n) {
    int iOffset = pkd->iTreeNodeSize;
    mdlassert( pkd->mdl, pkd->kdNodeListPRIVATE == NULL );
    mdlassert( pkd->mdl, (iOffset & (sizeof(int32_t)-1)) == 0 );
    pkd->iTreeNodeSize += sizeof(int32_t) * n;
    return iOffset;
    }

/* Add a structure: assume double alignment */
static int pkdParticleAddStruct(PKD pkd,int n) {
    int iOffset = pkd->iParticleSize;
    mdlassert( pkd->mdl, pkd->pStorePRIVATE == NULL );
    mdlassert( pkd->mdl, (iOffset & (sizeof(double)-1)) == 0 );
    pkd->iParticleSize += n;
    return iOffset;
    }

/* Add n doubles to the particle structure */
static int pkdParticleAddDouble(PKD pkd,int n) {
    int iOffset = pkd->iParticleSize;
    mdlassert( pkd->mdl, pkd->pStorePRIVATE == NULL );
    mdlassert( pkd->mdl, (iOffset & (sizeof(double)-1)) == 0 );
    pkd->iParticleSize += sizeof(double) * n;
    return iOffset;
    }

/* Add n floats to the particle structure */
static int pkdParticleAddFloat(PKD pkd,int n) {
    int iOffset = pkd->iParticleSize;
    mdlassert( pkd->mdl, pkd->pStorePRIVATE == NULL );
    mdlassert( pkd->mdl, (iOffset & (sizeof(float)-1)) == 0 );
    pkd->iParticleSize += sizeof(float) * n;
    return iOffset;
    }

/* Add n 64-bit integers to the particle structure */
static int pkdParticleAddInt64(PKD pkd,int n) {
    int iOffset = pkd->iParticleSize;
    mdlassert( pkd->mdl, pkd->pStorePRIVATE == NULL );
    mdlassert( pkd->mdl, (iOffset & (sizeof(int64_t)-1)) == 0 );
    pkd->iParticleSize += sizeof(int64_t) * n;
    return iOffset;
    }

/* Add n 32-bit integers to the particle structure */
static int pkdParticleAddInt32(PKD pkd,int n) {
    int iOffset = pkd->iParticleSize;
    mdlassert( pkd->mdl, pkd->pStorePRIVATE == NULL );
    mdlassert( pkd->mdl, (iOffset & (sizeof(int32_t)-1)) == 0 );
    pkd->iParticleSize += sizeof(int32_t) * n;
    return iOffset;
    }

/* Add n 16-bit integers to the particle structure */
static int pkdParticleAddInt16(PKD pkd,int n) {
    int iOffset = pkd->iParticleSize;
    mdlassert( pkd->mdl, pkd->pStorePRIVATE == NULL );
    mdlassert( pkd->mdl, (iOffset & (sizeof(int16_t)-1)) == 0 );
    pkd->iParticleSize += sizeof(int16_t) * n;
    return iOffset;
    }

/* Extend the tree by adding more nodes */
void pkdExtendTree(PKD pkd) {
    if ( pkd->nTreeTiles >= (1<<pkd->nTreeBitsHi) ) {
	fprintf(stderr, "ERROR: insufficent nodes available in tree build"
	    "-- Increase nTreeBitsLo and/or nTreeBitsHi\n"
	    "nTreeBitsLo=%d nTreeBitsHi=%d\n",
	    pkd->nTreeBitsLo, pkd->nTreeBitsHi);
	assert( pkd->nTreeTiles < (1<<pkd->nTreeBitsHi) );
	}
    pkd->kdNodeListPRIVATE[pkd->nTreeTiles] = mdlMalloc(pkd->mdl,(1<<pkd->nTreeBitsLo)*pkd->iTreeNodeSize);
    mdlassert(pkd->mdl,pkd->kdNodeListPRIVATE[pkd->nTreeTiles] != NULL);
    ++pkd->nTreeTiles;
    pkd->nMaxNodes = (1<<pkd->nTreeBitsLo) * pkd->nTreeTiles;
    }

static void firstTouch(uint64_t n,char *p) {
    if (n>4096) n-= 4096;
    while(n>=4096) {
	*p = 0;
	p += 4096;
	n -= 4096;
	}
    }

void pkdInitialize(
    PKD *ppkd,MDL mdl,int nStore,uint64_t nMinTotalStore,uint64_t nMinEphemeral,
    int nBucket,int nGroup,int nTreeBitsLo, int nTreeBitsHi,
    int iCacheSize,int iWorkQueueSize,int iCUDAQueueSize,FLOAT *fPeriod,uint64_t nDark,uint64_t nGas,uint64_t nStar,
    uint64_t mMemoryModel, int bLightCone, int bLightConeParticles) {
    PKD pkd;
    PARTICLE *p;
    uint32_t pi;
    int j,ism;

#define RANDOM_SEED 1
    srand(RANDOM_SEED);

    pkd = (PKD)SIMD_malloc(sizeof(struct pkdContext));
    mdlassert(mdl,pkd != NULL);
    pkd->mdl = mdl;
    pkd->idSelf = mdlSelf(mdl);
    pkd->nThreads = mdlThreads(mdl);
    pkd->kdNodeListPRIVATE = NULL;
    pkd->pStorePRIVATE = NULL;
    pkd->nStore = nStore;
    pkd->nLocal = 0;
    pkd->nDark = nDark;
    pkd->nGas = nGas;
    pkd->nStar = nStar;
    pkd->nRejects = 0;
    for (j=0;j<3;++j) {
	pkd->fPeriod[j] = fPeriod[j];
	}

    pkd->uMinRungActive  = 0;
    pkd->uMaxRungActive  = 255;
    pkd->uRungVeryActive = 255;

    pkd->psGroupTable.nGroups = 0;
    pkd->psGroupTable.pGroup = NULL;


    /*
    ** Calculate the amount of memory (size) of each particle.  This is the
    ** size of a base particle (PARTICLE), plus any extra fields as defined
    ** by the current memory model.  Fields need to be added in order of
    ** descending size (i.e., doubles & int64 and then float & int32)
    */
    pkd->iParticleSize = sizeof(PARTICLE);
    pkd->iTreeNodeSize = sizeof(KDN);

#ifndef INTEGER_POSITION
    pkd->oPosition = pkdParticleAddDouble(pkd,3);
#endif
    if ( mMemoryModel & PKD_MODEL_PARTICLE_ID )
	pkd->oParticleID = pkdParticleAddInt64(pkd,1);
    else
	pkd->oParticleID = 0;

    pkd->oVelocity = 0;
    if ( mMemoryModel & PKD_MODEL_VELOCITY ) {
	if (sizeof(vel_t) == sizeof(double)) {
	    pkd->oVelocity = pkdParticleAddDouble(pkd,3);
	    }
	}
#ifdef INTEGER_POSITION
    pkd->oPosition = pkdParticleAddInt32(pkd,3);
#endif
    if ( mMemoryModel & PKD_MODEL_RELAXATION )
	pkd->oRelaxation = pkdParticleAddDouble(pkd,1);
    else
	pkd->oRelaxation = 0;

    if ( mMemoryModel & PKD_MODEL_SPH )
	pkd->oSph = pkdParticleAddStruct(pkd,sizeof(SPHFIELDS));
    else
	pkd->oSph = 0;

    if ( mMemoryModel & PKD_MODEL_STAR )
	pkd->oStar = pkdParticleAddStruct(pkd,sizeof(STARFIELDS));
    else
	pkd->oStar = 0;

    if ( mMemoryModel & PKD_MODEL_VELSMOOTH )
	pkd->oVelSmooth = pkdParticleAddStruct(pkd,sizeof(VELSMOOTH));
    else
	pkd->oVelSmooth = 0;
    if ( mMemoryModel & PKD_MODEL_VELOCITY ) {
	if (sizeof(vel_t) == sizeof(float)) {
	    pkd->oVelocity = pkdParticleAddFloat(pkd,3);
	    }
	}
    if ( mMemoryModel & PKD_MODEL_ACCELERATION )
	pkd->oAcceleration = pkdParticleAddFloat(pkd,3);
    else
	pkd->oAcceleration = 0;

    if ( mMemoryModel & PKD_MODEL_POTENTIAL )
	pkd->oPotential = pkdParticleAddFloat(pkd,1);
    else
	pkd->oPotential = 0;

    if ( mMemoryModel & PKD_MODEL_MASS )
	pkd->oMass = pkdParticleAddFloat(pkd,1);
    else
	pkd->oMass = 0;

    if ( mMemoryModel & PKD_MODEL_SOFTENING )
	pkd->oSoft = pkdParticleAddFloat(pkd,1);
    else
	pkd->oSoft = 0;

    if ( mMemoryModel & PKD_MODEL_SPH )
	pkd->oBall = pkdParticleAddFloat(pkd,1);
    else pkd->oBall = 0;
    if ( mMemoryModel & PKD_MODEL_SPH )
	pkd->oDensity = pkdParticleAddFloat(pkd,1);
    else pkd->oDensity = 0;

    if ( mMemoryModel & PKD_MODEL_GROUPS ) {
	pkd->oGroup = pkdParticleAddInt32(pkd,1);
	}
    else {
	pkd->oGroup = 0;
	}

    /*
    ** Tree node memory models
    */
    if ( mMemoryModel & PKD_MODEL_NODE_BND ) {
        pkd->oNodeBnd  = pkdNodeAddStruct(pkd,sizeof(BND));
    }
    else {
	pkd->oNodeBnd  = 0;
    }

    if ( mMemoryModel & PKD_MODEL_NODE_VBND ) {
	pkd->oNodeVBnd  = pkdNodeAddStruct(pkd,sizeof(BND));
    }
    else {
	pkd->oNodeVBnd = 0;
    }

    pkd->oNodeVelocity = 0;
    if ( (mMemoryModel & PKD_MODEL_NODE_VEL) && sizeof(vel_t) == sizeof(double))
	    pkd->oNodeVelocity = pkdNodeAddDouble(pkd,3);
    /*
    ** Three extra bounds are required by the fast gas SPH code.
    */
    if ( mMemoryModel & PKD_MODEL_NODE_SPHBNDS ) {
	pkd->oNodeSphBounds = pkdNodeAddStruct(pkd,sizeof(SPHBNDS));
    }
    else
	pkd->oNodeSphBounds = 0;

    if ( mMemoryModel & PKD_MODEL_NODE_MOMENT )
	pkd->oNodeMom = pkdNodeAddStruct(pkd,sizeof(FMOMR));
    else
	pkd->oNodeMom = 0;

    /* The acceleration is required for the new time step criteria */
    if ( mMemoryModel & PKD_MODEL_NODE_ACCEL )
	pkd->oNodeAcceleration = pkdNodeAddFloat(pkd,3);
    else
	pkd->oNodeAcceleration = 0;

    if ( (mMemoryModel & PKD_MODEL_NODE_VEL) && sizeof(vel_t) == sizeof(float))
	    pkd->oNodeVelocity = pkdNodeAddFloat(pkd,3);
    /*
    ** N.B.: Update pkdMaxNodeSize in pkd.h if you add fields.  We need to
    **       know the size of a node when setting up the pst.
    */
    assert(pkdNodeSize(pkd) > 0);
    if (pkdNodeSize(pkd) > pkdMaxNodeSize()) {
	fprintf(stderr, "Node size is too large. Node size=%llu, max node size=%llu\n", pkdNodeSize(pkd), pkdMaxNodeSize());
	}
    assert(pkdNodeSize(pkd)<=pkdMaxNodeSize());

    /* Align the particle size and the tree node, and store the tree node parameters */
    pkd->iParticleSize = (pkd->iParticleSize + sizeof(double) - 1 ) & ~(sizeof(double)-1);
    pkd->iTreeNodeSize = (pkd->iTreeNodeSize + sizeof(double) - 1 ) & ~(sizeof(double)-1);
    pkd->nTreeBitsLo = nTreeBitsLo;
    pkd->nTreeBitsHi = nTreeBitsHi;
    pkd->iTreeMask = (1<<pkd->nTreeBitsLo) - 1;


    /*
    ** We need to allocate one large chunk of memory for:
    **   (1) The particles, and,
    **   (2) The emphemeral storage,
    ** subject to the following constraints:
    **   (a) We must be able to store nStore+1 particles in both stores,
    **   (b) The total per-node ephemeral storage must be at least nMinEphemeral
    **   (c) The total size of the storage block must be at least nMinTotalStore
    **   (d) pStore must be page aligned on each thread
    **
    ** If (b) is not met then we increase the effective size of the ephemeral storage.
    ** If (c) is not met (even after increasing the ephemeral storage), then we increase
    ** the size of the block and use the left over for the first parts of the tree.
    **
    ** STILL TRUE?: We need one EXTRA storage location at the very end to use for
    ** calculating acceleration on arbitrary positions in space, for example
    ** determining the force on the sun. The easiest way to do this is to
    ** allocate one hidden particle, which won't interfere with the rest of
    ** the code (hopefully). pkd->pStore[pkd->nStore] is this particle.
    **
    ** IMPORTANT: There is a whole lot of pointer math here. If you mess with this
    **            you better be sure you get it right or really bad things will happen.
    */
    uint64_t nPageMask = sysconf(_SC_PAGESIZE)-1;
    uint64_t nBytesPerThread = ((nStore+1)*pkdParticleSize(pkd)+nPageMask) & ~nPageMask; // Constraint (d)
    uint64_t nBytesParticles = (uint64_t)mdlCores(pkd->mdl) * nBytesPerThread; // Constraint (a)
    uint64_t nBytesEphemeral = (uint64_t)mdlCores(pkd->mdl) * (nStore+1)*EPHEMERAL_BYTES; // Constraint (a)
    uint64_t nBytesTreeNodes = 0;
    if (nBytesEphemeral < nMinEphemeral) nBytesEphemeral = nMinEphemeral; // Constraint (b)
    if (nBytesParticles + nBytesEphemeral < nMinTotalStore) // Constraint (c)
	nBytesTreeNodes = nMinTotalStore - nBytesParticles - nBytesEphemeral;
    // Align to a even number of "tree tiles"
    uint64_t nTreeTileBytesPerNode = (1<<pkd->nTreeBitsLo)*pkd->iTreeNodeSize*mdlCores(pkd->mdl);
    uint64_t nTreeTiles = (uint64_t)ceil(1.0 * nBytesTreeNodes / nTreeTileBytesPerNode);
    nBytesTreeNodes = nTreeTiles * nTreeTileBytesPerNode;
    char *pParticles, *pEphemeral, *pTreeNodes;
    if (mdlCore(pkd->mdl)==0) {
	uint64_t nBytesTotal = nBytesParticles + nBytesEphemeral + nBytesTreeNodes;
	void *vParticles;
	if (posix_memalign(&vParticles,sysconf(_SC_PAGESIZE),nBytesTotal)) pParticles = NULL;
	else pParticles = vParticles;
//	pParticles = mdlMalloc(pkd->mdl,nBytesTotal);
	mdlassert(mdl,pParticles != NULL);
	pEphemeral = pParticles + nBytesParticles;
	pTreeNodes = pEphemeral + nBytesEphemeral;
	}
    else pParticles = pEphemeral = pTreeNodes = 0; // Ignore anyway in mdlSetArray() below
    pParticles = mdlSetArray(pkd->mdl,1,nBytesPerThread,pParticles);
    pEphemeral = mdlSetArray(pkd->mdl,1,nBytesEphemeral/mdlCores(pkd->mdl),pEphemeral);
    pTreeNodes = mdlSetArray(pkd->mdl,1,nBytesTreeNodes/mdlCores(pkd->mdl),pTreeNodes);
    firstTouch(nBytesParticles/mdlCores(pkd->mdl),pParticles);
    firstTouch(nBytesEphemeral/mdlCores(pkd->mdl),pEphemeral);
    firstTouch(nBytesTreeNodes/mdlCores(pkd->mdl),pTreeNodes);
    pkd->pStorePRIVATE = (PARTICLE *)pParticles;
    pkd->pLite = pEphemeral;
    /*
    ** Now we setup the node storage for the tree.  This storage is no longer
    ** continguous as the MDL now supports non-contiguous arrays.  We allocate
    ** a single "tile" for the tree.  If this is not sufficient, then additional
    ** tiles are allocated dynamically.  The default parameters allow for 2^32
    ** nodes total which is the integer limit anyway. We may use the extra storage
    ** from above if constraint (c) could not otherwise be met.
    */
    pkd->kdNodeListPRIVATE = mdlMalloc(pkd->mdl,(1<<pkd->nTreeBitsHi)*sizeof(KDN *));
    mdlassert(mdl,pkd->kdNodeListPRIVATE != NULL);
    if (nTreeTiles) {
	pkd->nTreeTilesReserved = nTreeTiles;
	pkd->nTreeTiles = nTreeTiles;
	for(j=0; j<nTreeTiles; ++j) {
	    pkd->kdNodeListPRIVATE[j] = pTreeNodes;
	    pTreeNodes += (1<<pkd->nTreeBitsLo)*pkd->iTreeNodeSize;
	    }
	}
    else {
	pkd->nTreeTilesReserved = 0;
	pkd->kdNodeListPRIVATE[0] = mdlMalloc(pkd->mdl,(1<<pkd->nTreeBitsLo)*pkd->iTreeNodeSize);
	mdlassert(mdl,pkd->kdNodeListPRIVATE[0] != NULL);
	pkd->nTreeTiles = 1;
	}
    pkd->nMaxNodes = (1<<pkd->nTreeBitsLo) * pkd->nTreeTiles;
    pkd->nNodes = 0;
    /*
    ** We also allocate a temporary particle used for swapping.  We need to do
    ** this now because the outside world can no longer know the size of a
    ** particle a priori.
    */
    pkd->pTempPRIVATE = malloc(pkdParticleSize(pkd));
    mdlassert(mdl,pkd->pTempPRIVATE != NULL);
    /*
    ** allocate enough space for light cone particle output
    */
    pkd->nLightCone = 0;
    pkd->pLightCone = NULL;
    if (bLightCone && bLightConeParticles) {
	pkd->pLightCone = malloc(nStore*sizeof(LIGHTCONEP));
	mdlassert(mdl,pkd->pLightCone != NULL);
	}

#ifdef MDL_CACHE_SIZE
    if ( iCacheSize > 0 ) mdlSetCacheSize(pkd->mdl,iCacheSize);
#endif
    // This is cheeserific - chooses the largest specified

#ifdef USE_CUDA
    mdlSetCudaBufferSize(pkd->mdl,MAX_EWALD_PARTICLES*sizeof(double)*4,MAX_EWALD_PARTICLES*sizeof(double)*5);
    mdlSetCudaBufferSize(pkd->mdl,PP_CUDA_MEMORY_LIMIT,PP_CUDA_MEMORY_LIMIT);
#endif
    mdlSetWorkQueueSize(pkd->mdl,iWorkQueueSize,iCUDAQueueSize);
    /*
    ** Initialize neighbor list pointer to NULL if present.
    */
    if (pkd->oSph) {
	for (pi=0;pi<(pkd->nStore+1);++pi) {
	    p = pkdParticle(pkd,pi);
	    *pkd_pNeighborList(pkd,p) = NULL;
	}
    }

    /*
    ** We support up to 256 classes
    */
    pkd->pClass = malloc(PKD_MAX_CLASSES*sizeof(PARTCLASS));
    mdlassert(mdl,pkd->pClass != NULL);
    for (j=0;j<PKD_MAX_CLASSES;j++) {
	pkd->pClass[j].fMass = pkd->pClass[j].fSoft = -1.0;
	pkd->pClass[j].eSpecies = FIO_SPECIES_LAST;
	}
    pkd->nClasses = 0;

    pkd->fSoftFix = -1.0;
    pkd->fSoftFac = 1.0;
    pkd->fSoftMax = HUGE;
    /*
    ** Ewald stuff!
    */
    pkd->ew.nMaxEwhLoop = 0;
    *ppkd = pkd;
    /*
    ** Tree walk stuff.
    */
    ilpInitialize(&pkd->ilp);
    ilcInitialize(&pkd->ilc);
    /*
    ** Allocate Checklist.
    */
    pkd->clFreeList.list = NULL;
    pkd->clFreeList.nRefs = 0;
    pkd->clFreeList.nTiles = 0;
    clInitialize(&pkd->cl,&pkd->clFreeList);
    clInitialize(&pkd->clNew,&pkd->clFreeList);
    /*
    ** Allocate the stack.
    */
    pkd->nMaxStack = 30;
    pkd->S = malloc(pkd->nMaxStack*sizeof(CSTACK));
    assert(pkd->S != NULL);
    for (ism=0;ism<pkd->nMaxStack;++ism) {
	clInitialize(&pkd->S[ism].cl,&pkd->clFreeList);
	}
    pkd->profileBins = NULL;
    pkd->groupBin = NULL;

    pkd->grid = NULL;
    pkd->gridData = NULL;

    pkd->tmpHopGroups = NULL;
    pkd->hopGroups = NULL;
    pkd->hopRootIndex = NULL;
    pkd->hopRoots = NULL;

#ifdef COOLING
    pkd->Cool = CoolInit();
#endif
    assert(pkdNodeSize(pkd) > 0);

#ifdef xNOxUSE_CUDA
	{
	int sizeILP = sizeof(ILP_BLK)*pkd->ilp->lst.nBlocksPerTile;
	int sizeILC = sizeof(ILC_BLK)*pkd->ilc->lst.nBlocksPerTile;
	pkd->cudaCtx = CUDA_initialize(mdlCore(pkd->mdl),
	    iCUDAQueueSize,
	    sizeILP>sizeILC ? sizeILP : sizeILC,
	    nGroup*sizeof(PINFOIN),
	    nGroup*sizeof(PINFOOUT)
	    * (pkd->ilp->lst.nBlocksPerTile>pkd->ilc->lst.nBlocksPerTile
		? pkd->ilp->lst.nBlocksPerTile : pkd->ilc->lst.nBlocksPerTile) );
	}
#endif
    }


void pkdFinish(PKD pkd) {
    PARTICLE *p;
    char **ppCList;
    uint32_t pi;
    int ism;
    int i;

#ifdef xNOxUSE_CUDA
    CUDA_finish(pkd->cudaCtx);
#endif

    if (pkd->kdNodeListPRIVATE) {
	/*
	** Close caching space and free up nodes.
	*/
	if (pkd->nNodes > 0)
	    mdlFinishCache(pkd->mdl,CID_CELL);
	for( i=pkd->nTreeTilesReserved; i<pkd->nTreeTiles; i++)
	    mdlFree(pkd->mdl,pkd->kdNodeListPRIVATE[i]);
	mdlFree(pkd->mdl,pkd->kdNodeListPRIVATE);
	}
    /*
    ** Free Interaction lists.
    */
    ilpFinish(pkd->ilp);
    ilcFinish(pkd->ilc);
    /*
    ** Free checklist.
    */
    clFinish(pkd->cl);
    clFinish(pkd->clNew);
    /*
    ** Free Stack.
    */
    for (ism=0;ism<pkd->nMaxStack;++ism) {
	clFinish(pkd->S[ism].cl);
	}
    free(pkd->S);
    if (pkd->ew.nMaxEwhLoop) {
	SIMD_free(pkd->ewt.hx.f);
	SIMD_free(pkd->ewt.hy.f);
	SIMD_free(pkd->ewt.hz.f);
	SIMD_free(pkd->ewt.hCfac.f);
	SIMD_free(pkd->ewt.hSfac.f);
	}

    free(pkd->pClass);
    /*
    ** Free any neighbor lists that were left hanging around.
    */
    if (pkd->oSph) {
	for (pi=0;pi<(pkd->nStore+1);++pi) {
	    p = pkdParticle(pkd,pi);
	    ppCList = pkd_pNeighborList(pkd,p);
	    if (*ppCList) {
		free(*ppCList);
		*ppCList = NULL;
	    }
	}
    }
    /* Only thread zero allocated this memory block  */
    mdlThreadBarrier(pkd->mdl);
    if (mdlCore(pkd->mdl)==0) mdlFree(pkd->mdl,pkd->pStorePRIVATE);
    free(pkd->pTempPRIVATE);
    if (pkd->pLightCone) free(pkd->pLightCone);
    csmFinish(pkd->param.csm);
    SIMD_free(pkd);
    }

size_t pkdClCount(PKD pkd) {
    size_t nCount = clCount(pkd->cl);
    int i;
    for(i=0; i<pkd->nMaxStack; ++i)
	nCount += clCount(pkd->S[i].cl);
    return nCount;
    }

size_t pkdClMemory(PKD pkd) {
    return clMemory(pkd->cl);
    }

size_t pkdIlpMemory(PKD pkd) {
    return ilpMemory(pkd->ilp);
    }

size_t pkdIlcMemory(PKD pkd) {
    return ilcMemory(pkd->ilc);
    }

size_t pkdTreeMemory(PKD pkd) {
    return pkd->nTreeTiles * (1<<pkd->nTreeBitsLo) * pkd->iTreeNodeSize;
    }

void pkdSetClass( PKD pkd, float fMass, float fSoft, FIO_SPECIES eSpecies, PARTICLE *p ) {
    int i;

    if ( pkd->oMass ) {
	float *pMass = pkdField(p,pkd->oMass);
	*pMass = fMass;
	fMass = 0.0;
	}
    if ( pkd->oSoft ) {
	float *pSoft = pkdField(p,pkd->oSoft);
	*pSoft = fSoft;
	fSoft = 0.0;
	}
    /* NOTE: The above can both be true, in which case a "zero" class is recorded */
    /* NOTE: Species is always part of the class table, so there will be at least one class per species */

    /* TODO: This is a linear search which is fine for a small number of classes */
    for ( i=0; i<pkd->nClasses; i++ )
	if ( pkd->pClass[i].fMass == fMass && pkd->pClass[i].fSoft == fSoft && pkd->pClass[i].eSpecies==eSpecies )
	    break;

    if ( i == pkd->nClasses ) {
	assert( pkd->nClasses < PKD_MAX_CLASSES );
	i = pkd->nClasses++;
	pkd->pClass[i].fSoft    = fSoft;
	pkd->pClass[i].fMass    = fMass;
	pkd->pClass[i].eSpecies = eSpecies;
	}
    p->iClass = i;
    }

int pkdGetClasses( PKD pkd, int nMax, PARTCLASS *pClass ) {
    int i;
    for ( i=0; i<pkd->nClasses; i++ )
	pClass[i] = pkd->pClass[i];
    return pkd->nClasses;
    }

void pkdSetClasses( PKD pkd, int n, PARTCLASS *pClass, int bUpdate ) {
    uint8_t map[PKD_MAX_CLASSES];
    PARTICLE *p;
    int i,j;

    if ( bUpdate && pkd->nClasses) {
	/* Build a map from the old class to the new class */
	assert( n >= pkd->nClasses );
	for ( i=0; i<pkd->nClasses; i++ ) {
	    for ( j=0; j<n; j++ )
		if ( pClass[j].fMass==pkd->pClass[i].fMass && pClass[j].fSoft==pkd->pClass[i].fSoft && pClass[j].eSpecies==pkd->pClass[i].eSpecies )
		    break;
	    assert(j<n);
	    map[i] = j;
	    }

	/* Now update the class with the new value */
	for (i=0;i<pkd->nLocal;++i) {
	    p = pkdParticle(pkd,i);
	    assert( p->iClass <= pkd->nClasses );
	    p->iClass = map[p->iClass];
	    }
	}

    /* Finally, set the new class table */
    for ( i=0; i<n; i++ ) pkd->pClass[i] = pClass[i];
    pkd->nClasses = n;
    }

void pkdSeek(PKD pkd,FILE *fp,uint64_t nStart,int bStandard,int bDoublePos) {
#ifndef HAVE_FSEEKO
    off_t MAX_OFFSET = 2147483640;
    int iErr;
#endif
    off_t lStart;

    /*
    ** Seek according to true XDR size structures when bStandard is true.
    ** This may be a bit dicey, but it should work as long
    ** as no one changes the tipsy binary format!
    */
    if (bStandard) lStart = 32;
    else lStart = sizeof(struct dump);
    if (nStart > pkd->nGas) {
	if (bStandard) lStart += pkd->nGas*(bDoublePos?60:48);
	else lStart += pkd->nGas*sizeof(struct gas_particle);
	nStart -= pkd->nGas;
	if (nStart > pkd->nDark) {
	    if (bStandard) lStart += pkd->nDark*(bDoublePos?48:36);
	    else lStart += pkd->nDark*sizeof(struct dark_particle);
	    nStart -= pkd->nDark;
	    if (bStandard) lStart += nStart*(bDoublePos?56:44);
	    else lStart += nStart*sizeof(struct star_particle);
	    }
	else {
	    if (bStandard) lStart += nStart*(bDoublePos?48:36);
	    else lStart += nStart*sizeof(struct dark_particle);
	    }
	}
    else {
	if (bStandard) lStart += nStart*(bDoublePos?60:48);
	else lStart += nStart*sizeof(struct gas_particle);
	}

#ifdef HAVE_FSEEKO
    fseeko(fp,lStart,SEEK_SET);
#else
    /*fseek fails for offsets >= 2**31; this is an ugly workaround;*/
    if (lStart > MAX_OFFSET) {
	iErr = fseek(fp,0,SEEK_SET);
	if (iErr) {
	    perror("pkdSeek failed");
	    exit(errno);
	    }
	while (lStart > MAX_OFFSET) {
	    fseek(fp,MAX_OFFSET,SEEK_CUR);
	    lStart -= MAX_OFFSET;
	    }
	iErr = fseek(fp,lStart,SEEK_CUR);
	if (iErr) {
	    perror("pkdSeek failed");
	    exit(errno);
	    }
	}
    else {
	iErr = fseek(fp,lStart,SEEK_SET);
	if (iErr) {
	    perror("pkdSeek failed");
	    exit(errno);
	    }
	}
#endif
    }

void pkdReadFIO(PKD pkd,FIO fio,uint64_t iFirst,int nLocal,double dvFac, double dTuFac) {
    int i,j;
    PARTICLE *p;
    STARFIELDS *pStar;
    SPHFIELDS *pSph;
    float *pPot, dummypot;
    double r[3];
    double vel[3];
    float fMass, fSoft,fDensity;
    FIO_SPECIES eSpecies;
    uint64_t iParticleID;

    mdlassert(pkd->mdl,fio != NULL);

#ifdef USE_ITT
    __itt_domain* domain = __itt_domain_create("MyTraces.MyDomain");
    __itt_string_handle* shMyTask = __itt_string_handle_create("Read");
     __itt_task_begin(domain, __itt_null, __itt_null, shMyTask);
#endif
    if (pkd->oStar) {
	/* Make sure star class established -- how do all procs know of these classes? How do we ensure they agree on the class identifiers? */
	p = pkdParticle(pkd,pkd->nLocal);
	pkdSetClass(pkd,0,0,FIO_SPECIES_STAR,p);
	}

    fioSeek(fio,iFirst,FIO_SPECIES_ALL);
    for (i=0;i<nLocal;++i) {
	p = pkdParticle(pkd,pkd->nLocal+i);
	/*
	** General initialization.
	*/
	p->uRung = p->uNewRung = 0;
	p->bSrcActive = p->bDstActive = p->bMarked = 1;
	pkdSetDensity(pkd,p,0.0);
	if (pkd->oBall) pkdSetBall(pkd,p,0.0);
	/*
	** Clear the accelerations so that the timestepping calculations do not
	** get funny uninitialized values!
	*/
	if ( pkd->oAcceleration ) {
	    float *a = pkdAccel(pkd,p);
	    for (j=0;j<3;++j) a[j] = 0.0;
	    }
	if ( pkd->oPotential) pPot = pkdPot(pkd,p);
	else pPot = &dummypot;

	/* Initialize SPH fields if present */
	if (pkd->oSph) {
	    pSph = pkdField(p,pkd->oSph);
	    pSph->u = pSph->uPred = pSph->uDot = pSph->c = pSph->divv = pSph->BalsaraSwitch
		= pSph->fMetals = pSph->diff = pSph->fMetalsPred = pSph->fMetalsDot = 0.0;
	    }
	else pSph = NULL;

	/* Initialize Star fields if present */
	if (pkd->oStar) {
	    pStar = pkdField(p,pkd->oStar);
	    pStar->fTimer = 0;
/*	    pStar->iGasOrder = IORDERMAX;*/
	    }
	else pStar = NULL;

	eSpecies = fioSpecies(fio);
	switch(eSpecies) {
	case FIO_SPECIES_SPH:
	    assert(pSph); /* JW: Could convert to dark ... */
	    assert(dTuFac>0.0);
	    fioReadSph(fio,&iParticleID,r,vel,&fMass,&fSoft,pPot,
			     &fDensity/*?*/,&pSph->u,&pSph->fMetals);
	    pkdSetDensity(pkd,p,fDensity);
	    pSph->u *= dTuFac; /* Can't do precise conversion until density known */
	    pSph->uPred = pSph->u;
	    pSph->fMetalsPred = pSph->fMetals;
	    pSph->vPred[0] = vel[0]*dvFac;
	    pSph->vPred[1] = vel[1]*dvFac;
	    pSph->vPred[2] = vel[2]*dvFac; /* density, divv, BalsaraSwitch, c set in smooth */
	    break;
	case FIO_SPECIES_DARK:
	    fioReadDark(fio,&iParticleID,r,vel,&fMass,&fSoft,pPot,&fDensity);
	    pkdSetDensity(pkd,p,fDensity);
	    break;
	case FIO_SPECIES_STAR:
	    assert(pStar && pSph);
	    fioReadStar(fio,&iParticleID,r,vel,&fMass,&fSoft,pPot,&fDensity,
			      &pSph->fMetals,&pStar->fTimer);
	    pkdSetDensity(pkd,p,fDensity);
	    pSph->vPred[0] = vel[0]*dvFac;
	    pSph->vPred[1] = vel[1]*dvFac;
	    pSph->vPred[2] = vel[2]*dvFac;
	    break;
	default:
	    fprintf(stderr,"Unsupported particle type: %d\n",eSpecies);
	    assert(0);
	    }

	for (j=0;j<3;++j) pkdSetPos(pkd,p,j,r[j]);
	if (pkd->oVelocity) {
	    for (j=0;j<3;++j) pkdVel(pkd,p)[j] = vel[j]*dvFac;
	    }

	p->iOrder = iFirst++;
	if (pkd->oParticleID) *pkdParticleID(pkd,p) = iParticleID;

	pkdSetClass(pkd,fMass,fSoft,eSpecies,p);
	}
    
    pkd->nLocal += nLocal;
    pkd->nActive += nLocal;

#ifdef USE_ITT
    __itt_task_end(domain);
#endif
    }

void pkdCalcBound(PKD pkd,BND *pbnd) {
    double r[3],dMin[3],dMax[3];
    PARTICLE *p;
    int i = 0;
    int j;

    mdlassert(pkd->mdl,pkd->nLocal > 0);
    p = pkdParticle(pkd,i);
    for (j=0;j<3;++j) {
	dMin[j] = dMax[j] = pkdPos(pkd,p,j);
	}
    for (++i;i<pkd->nLocal;++i) {
	p = pkdParticle(pkd,i);
	for (j=0;j<3;++j) r[j] = pkdPos(pkd,p,j);
	pkdMinMax(r,dMin,dMax);
	}
    for (j=0;j<3;++j) {
	pbnd->fCenter[j] = pkd->bnd.fCenter[j] = 0.5*(dMin[j] + dMax[j]);
	pbnd->fMax[j] = pkd->bnd.fMax[j] = 0.5*(dMax[j] - dMin[j]);
	}
    }

void pkdCalcVBound(PKD pkd,BND *pbnd) {
    double dMin[3],dMax[3];
    PARTICLE *p;
    vel_t *v;
    int i = 0;
    int j;

    mdlassert(pkd->mdl,pkd->nLocal > 0);
    p = pkdParticle(pkd,i);
    v = pkdVel(pkd,p);
    for (j=0;j<3;++j) {
	dMin[j] = v[j];
	dMax[j] = v[j];
	}
    for (++i;i<pkd->nLocal;++i) {
	p = pkdParticle(pkd,i);
	v = pkdVel(pkd,p);
	pkdMinMax(v,dMin,dMax);
	}
    for (j=0;j<3;++j) {
	pbnd->fCenter[j] = pkd->vbnd.fCenter[j] = 0.5*(dMin[j] + dMax[j]);
	pbnd->fMax[j] = pkd->vbnd.fMax[j] = 0.5*(dMax[j] - dMin[j]);
	}
    }


void pkdEnforcePeriodic(PKD pkd,BND *pbnd) {
    PARTICLE *p;
    double r;
    int i,j;

    for (i=0;i<pkd->nLocal;++i) {
	p = pkdParticle(pkd,i);
	for (j=0;j<3;++j) {
	    r = pkdPos(pkd,p,j);
	    if (r < pbnd->fCenter[j] - pbnd->fMax[j]) r += 2*pbnd->fMax[j];
	    else if (r >= pbnd->fCenter[j] + pbnd->fMax[j]) r -= 2*pbnd->fMax[j];
	    pkdSetPos(pkd,p,j,r);
	    /*
	    ** If it still doesn't lie in the "unit" cell then something has gone quite wrong with the 
	    ** simulation. Either we have a super fast particle or the initial condition is somehow not conforming
	    ** to the specified periodic box in a gross way.
	    */
	    mdlassert(pkd->mdl,((r >= pbnd->fCenter[j] - pbnd->fMax[j])&&
	    (r < pbnd->fCenter[j] + pbnd->fMax[j])));
	    }
	}
    }


/*
** x and y must have range [1,2) !
** returns key in the range [0,2^63-1]
*/
uint64_t hilbert2d(float x,float y) {
    uint64_t s = 0;
    uint32_t m,ux,uy,ut;

    union {
	float    f;
	uint32_t u;
	} punner;

    punner.f = x; ux = punner.u >> 2;
    punner.f = y; uy = punner.u >> 2;
   
    m = 0x00100000;

    while (m) {
	s = s << 2;
	if (ux&m) {
	    if (uy&m) {
		s |= 2;
		}
	    else {
		ut = ux;
		ux = ~uy;
		uy = ~ut;
		s |= 3;
		}
	    }
	else {
	    if (uy&m) {
		s |= 1;
		}
	    else {
		ut = ux;
		ux = uy;
		uy = ut;
		}
	    }
	m = m >> 1;
	}
    return s;
    }

/*
** x, y and z must have range [1,2) !
*/
uint64_t hilbert3d(float x,float y,float z) {
    uint64_t s = 0;
    uint32_t m,ux,uy,uz,ut;

    union {
	float    f;
	uint32_t u;
	} punner;

    punner.f = x; ux = punner.u >> 2;
    punner.f = y; uy = punner.u >> 2;
    punner.f = z; uz = punner.u >> 2;
    /* Or: ux = (uint32_t)((x-1.0f) * 0x00200000)*/

    m = 0x00100000;
    while (m) {
	s = s << 3;

	if (ux&m) {
	    if (uy&m) {
		if (uz&m) {
		    ut = ux;
		    ux = uy;
		    uy = ~uz;
		    uz = ~ut;
		    s |= 5;
		    }
		else {
		    ut = uz;
		    uz = ux;
		    ux = uy;
		    uy = ut;
		    s |= 2;
		    }
		}
	    else {
		ux = ~ux;
		uy = ~uy;
		if (uz&m) {
		    s |= 4;
		    }
		else {
		    s |= 3;
		    }
		}
	    }
	else {
	    if (uy&m) {
		if (uz&m) {
		    ut = ux;
		    ux = uy;
		    uy = ~uz;
		    uz = ~ut;
		    s |= 6;
		    }
		else {
		    ut = uz;
		    uz = ux;
		    ux = uy;
		    uy = ut;
		    s |= 1;
		    }
		}
	    else {
		if (uz&m) {
		    ut = uy;
		    uy = ux;
		    ux = ~uz;
		    uz = ~ut;
		    s |= 7;
		    }
		else {
		    ut = uy;
		    uy = ux;
		    ux = uz;
		    uz = ut;
		    s |= 0;
		    }
		}
	    }
	m = m >> 1;
	}
    return s;
    }

/*
** Partition particles between iFrom and iTo into those < fSplit and
** those >= to fSplit.  Find number and weight in each partition.
*/
int pkdWeight(PKD pkd,int d,FLOAT fSplit,int iSplitSide,int iFrom,int iTo,
	      int *pnLow,int *pnHigh,FLOAT *pfLow,FLOAT *pfHigh) {
    int i,iPart;
    FLOAT fLower,fUpper;

    /*
    ** First partition the memory about fSplit for particles iFrom to iTo.
    */
    if (iSplitSide) {
	iPart = pkdLowerPart(pkd,d,fSplit,iFrom,iTo);
	*pnLow = pkdLocal(pkd)-iPart;
	*pnHigh = iPart;
	}
    else {
	iPart = pkdUpperPart(pkd,d,fSplit,iFrom,iTo);
	*pnLow = iPart;
	*pnHigh = pkdLocal(pkd)-iPart;
	}
    /*
    ** Calculate the lower weight and upper weight BETWEEN the particles
    ** iFrom to iTo!
    */
    fLower = 0.0;
    for (i=iFrom;i<iPart;++i) {
	fLower += 1.0;
	}
    fUpper = 0.0;
    for (i=iPart;i<=iTo;++i) {
	fUpper += 1.0;
	}
    if (iSplitSide) {
	*pfLow = fUpper;
	*pfHigh = fLower;
	}
    else {
	*pfLow = fLower;
	*pfHigh = fUpper;
	}
    return(iPart);
    }


void pkdCountVA(PKD pkd,int d,FLOAT fSplit,int *pnLow,int *pnHigh) {
    PARTICLE *p;
    int i;

    *pnLow = 0;
    *pnHigh = 0;
    for (i=0;i<pkd->nLocal;++i) {
	p = pkdParticle(pkd,i);
	if (pkdIsVeryActive(pkd,p)) {
	    if (pkdPos(pkd,p,d) < fSplit) *pnLow += 1;
	    else *pnHigh += 1;
	    }
	}
    }

/*
** Partition particles between iFrom and iTo into those < fSplit and
** those >= to fSplit.  Find number and weight in each partition.
*/
int pkdWeightWrap(PKD pkd,int d,FLOAT fSplit,FLOAT fSplit2,int iSplitSide,int iVASplitSide,
		  int iFrom,int iTo,int *pnLow,int *pnHigh) {
    int iPart;

    /*
    ** First partition the memory about fSplit for particles iFrom to iTo.
    */
    if (!iSplitSide) {
	iPart = pkdLowerPartWrap(pkd,d,fSplit,fSplit2,iVASplitSide,iFrom,iTo);
	*pnLow = iPart;
	*pnHigh = pkdLocal(pkd)-iPart;
	}
    else {
	iPart = pkdUpperPartWrap(pkd,d,fSplit,fSplit2,iVASplitSide,iFrom,iTo);
	*pnHigh = iPart;
	*pnLow = pkdLocal(pkd)-iPart;
	}
    return(iPart);
    }


int pkdOrdWeight(PKD pkd,uint64_t iOrdSplit,int iSplitSide,int iFrom,int iTo,
		 int *pnLow,int *pnHigh) {
    int iPart;

    /*
    ** First partition the memory about fSplit for particles iFrom to iTo.
    */
    if (iSplitSide) {
	iPart = pkdLowerOrdPart(pkd,iOrdSplit,iFrom,iTo);
	*pnLow = pkdLocal(pkd)-iPart;
	*pnHigh = iPart;
	}
    else {
	iPart = pkdUpperOrdPart(pkd,iOrdSplit,iFrom,iTo);
	*pnLow = iPart;
	*pnHigh = pkdLocal(pkd)-iPart;
	}
    return(iPart);
    }


int pkdLowerPart(PKD pkd,int d,FLOAT fSplit,int i,int j) {
    PARTICLE *pi, *pj;
    pi = pkdParticle(pkd,i);
    pj = pkdParticle(pkd,j);
    PARTITION(pi<pj,pi<=pj,
	pi=pkdParticle(pkd,++i),pj=pkdParticle(pkd,--j),
	pkdSwapParticle(pkd,pi,pj),
	pkdPos(pkd,pi,d) >= fSplit,pkdPos(pkd,pj,d) < fSplit);
    return(i);
    }


int pkdUpperPart(PKD pkd,int d,FLOAT fSplit,int i,int j) {
    PARTICLE *pi, *pj;
    pi = pkdParticle(pkd,i);
    pj = pkdParticle(pkd,j);
    PARTITION(pi<pj,pi<=pj,
	pi=pkdParticle(pkd,++i),pj=pkdParticle(pkd,--j),
	pkdSwapParticle(pkd,pi,pj),
	pkdPos(pkd,pi,d) < fSplit,pkdPos(pkd,pj,d) >= fSplit);
    return(i);
    }


int pkdLowerPartWrap(PKD pkd,int d,FLOAT fSplit1,FLOAT fSplit2,int iVASplitSide,int i,int j) {
    PARTICLE *pi = pkdParticle(pkd,i);
    PARTICLE *pj = pkdParticle(pkd,j);

    if (fSplit1 > fSplit2) {
	if (iVASplitSide < 0) {
	    PARTITION(pi<pj,pi<=pj,
		pi=pkdParticle(pkd,++i),pj=pkdParticle(pkd,--j),
		pkdSwapParticle(pkd,pi,pj),
	    (pkdPos(pkd,pi,d) < fSplit2 || pkdPos(pkd,pi,d) >= fSplit1) &&
		       !pkdIsVeryActive(pkd,pi),
	    (pkdPos(pkd,pj,d) >= fSplit2 && pkdPos(pkd,pj,d) < fSplit1) ||
		       pkdIsVeryActive(pkd,pj));
	    }
	else if (iVASplitSide > 0) {
	    PARTITION(pi<pj,pi<=pj,
		pi=pkdParticle(pkd,++i),pj=pkdParticle(pkd,--j),
		pkdSwapParticle(pkd,pi,pj),
	    (pkdPos(pkd,pi,d) < fSplit2 || pkdPos(pkd,pi,d) >= fSplit1) ||
		pkdIsVeryActive(pkd,pi),
	    (pkdPos(pkd,pj,d) >= fSplit2 && pkdPos(pkd,pj,d) < fSplit1) &&
		!pkdIsVeryActive(pkd,pj));
	    }
	else {
	    PARTITION(pi<pj,pi<=pj,
		pi=pkdParticle(pkd,++i),pj=pkdParticle(pkd,--j),
		pkdSwapParticle(pkd,pi,pj),
	    (pkdPos(pkd,pi,d) < fSplit2 || pkdPos(pkd,pi,d) >= fSplit1),
	    (pkdPos(pkd,pj,d) >= fSplit2 && pkdPos(pkd,pj,d) < fSplit1));
	    }
	}
    else {
	if (iVASplitSide < 0) {
	    PARTITION(pi<pj,pi<=pj,
		pi=pkdParticle(pkd,++i),pj=pkdParticle(pkd,--j),
		pkdSwapParticle(pkd,pi,pj),
	    (pkdPos(pkd,pi,d) < fSplit2 && pkdPos(pkd,pi,d) >= fSplit1) &&
		!pkdIsVeryActive(pkd,pi),
	    (pkdPos(pkd,pj,d) >= fSplit2 || pkdPos(pkd,pj,d) < fSplit1) ||
		       pkdIsVeryActive(pkd,pj));
	    }
	else if (iVASplitSide > 0) {
	    PARTITION(pi<pj,pi<=pj,
		pi=pkdParticle(pkd,++i),pj=pkdParticle(pkd,--j),
		pkdSwapParticle(pkd,pi,pj),
	    (pkdPos(pkd,pi,d) < fSplit2 && pkdPos(pkd,pi,d) >= fSplit1) ||
		pkdIsVeryActive(pkd,pi),
	    (pkdPos(pkd,pj,d) >= fSplit2 || pkdPos(pkd,pj,d) < fSplit1) &&
		!pkdIsVeryActive(pkd,pj));
	    }
	else {
	    PARTITION(pi<pj,pi<=pj,
		pi=pkdParticle(pkd,++i),pj=pkdParticle(pkd,--j),
		pkdSwapParticle(pkd,pi,pj),
	    (pkdPos(pkd,pi,d) < fSplit2 && pkdPos(pkd,pi,d) >= fSplit1),
	    (pkdPos(pkd,pj,d) >= fSplit2 || pkdPos(pkd,pj,d) < fSplit1));
	    }
	}
    return(i);
    }


int pkdUpperPartWrap(PKD pkd,int d,FLOAT fSplit1,FLOAT fSplit2,int iVASplitSide,int i,int j) {
    PARTICLE *pi = pkdParticle(pkd,i);
    PARTICLE *pj = pkdParticle(pkd,j);

    if (fSplit1 > fSplit2) {
	if (iVASplitSide < 0) {
	    PARTITION(pi<pj,pi<=pj,
		pi=pkdParticle(pkd,++i),pj=pkdParticle(pkd,--j),
		pkdSwapParticle(pkd,pi,pj),
	    (pkdPos(pkd,pi,d) >= fSplit2 && pkdPos(pkd,pi,d) < fSplit1) ||
		pkdIsVeryActive(pkd,pi),
	    (pkdPos(pkd,pj,d) < fSplit2 || pkdPos(pkd,pj,d) >= fSplit1) &&
		!pkdIsVeryActive(pkd,pj));
	    }
	else if (iVASplitSide > 0) {
	    PARTITION(pi<pj,pi<=pj,
		pi=pkdParticle(pkd,++i),pj=pkdParticle(pkd,--j),
		pkdSwapParticle(pkd,pi,pj),
	    (pkdPos(pkd,pi,d) >= fSplit2 && pkdPos(pkd,pi,d) < fSplit1) &&
		!pkdIsVeryActive(pkd,pi),
	    (pkdPos(pkd,pj,d) < fSplit2 || pkdPos(pkd,pj,d) >= fSplit1) ||
		pkdIsVeryActive(pkd,pj));
	    }
	else {
	    PARTITION(pi<pj,pi<=pj,
		pi=pkdParticle(pkd,++i),pj=pkdParticle(pkd,--j),
		pkdSwapParticle(pkd,pi,pj),
	    (pkdPos(pkd,pi,d) >= fSplit2 && pkdPos(pkd,pi,d) < fSplit1),
	    (pkdPos(pkd,pj,d) < fSplit2 || pkdPos(pkd,pj,d) >= fSplit1));
	    }
	}
    else {
	if (iVASplitSide < 0) {
	    PARTITION(pi<pj,pi<=pj,
		pi=pkdParticle(pkd,++i),pj=pkdParticle(pkd,--j),
		pkdSwapParticle(pkd,pi,pj),
	    (pkdPos(pkd,pi,d) >= fSplit2 || pkdPos(pkd,pi,d) < fSplit1) ||
		pkdIsVeryActive(pkd,pi),
	    (pkdPos(pkd,pj,d) < fSplit2 && pkdPos(pkd,pj,d) >= fSplit1) &&
		!pkdIsVeryActive(pkd,pj));
	    }
	else if (iVASplitSide > 0) {
	    PARTITION(pi<pj,pi<=pj,
		pi=pkdParticle(pkd,++i),pj=pkdParticle(pkd,--j),
		pkdSwapParticle(pkd,pi,pj),
	    (pkdPos(pkd,pi,d) >= fSplit2 || pkdPos(pkd,pi,d) < fSplit1) &&
		!pkdIsVeryActive(pkd,pi),
	    (pkdPos(pkd,pj,d) < fSplit2 && pkdPos(pkd,pj,d) >= fSplit1) ||
		pkdIsVeryActive(pkd,pj));
	    }
	else {
	    PARTITION(pi<pj,pi<=pj,
		pi=pkdParticle(pkd,++i),pj=pkdParticle(pkd,--j),
		pkdSwapParticle(pkd,pi,pj),
	    (pkdPos(pkd,pi,d) >= fSplit2 || pkdPos(pkd,pi,d) < fSplit1),
	    (pkdPos(pkd,pj,d) < fSplit2 && pkdPos(pkd,pj,d) >= fSplit1));
	    }
	}
    return(i);
    }


int pkdLowerOrdPart(PKD pkd,uint64_t nOrdSplit,int i,int j) {
    PARTICLE *pi, *pj;
    pi = pkdParticle(pkd,i);
    pj = pkdParticle(pkd,j);
    PARTITION(pi<pj,pi<=pj,
	       pi=pkdParticle(pkd,++i),pj=pkdParticle(pkd,--j),
	       pkdSwapParticle(pkd,pi,pj),
	       pi->iOrder >= nOrdSplit,pj->iOrder < nOrdSplit);
    return(i);
    }


int pkdUpperOrdPart(PKD pkd,uint64_t nOrdSplit,int i,int j) {
    PARTICLE *pi, *pj;
    pi = pkdParticle(pkd,i);
    pj = pkdParticle(pkd,j);
    PARTITION(pi<pj,pi<=pj,
	       pi=pkdParticle(pkd,++i),pj=pkdParticle(pkd,--j),
	       pkdSwapParticle(pkd,pi,pj),
	       pi->iOrder < nOrdSplit,pj->iOrder >= nOrdSplit);
    return(i);
    }


int pkdActiveOrder(PKD pkd) {
    int i=0;
    int j=pkdLocal(pkd)-1;
    PARTICLE *pi, *pj;
    pi = pkdParticle(pkd,i);
    pj = pkdParticle(pkd,j);
    PARTITION(pi<pj,pi<=pj,
	       pi=pkdParticle(pkd,++i),pj=pkdParticle(pkd,--j),
	       pkdSwapParticle(pkd,pi,pj),
	       pkdIsActive(pkd,pi),!pkdIsActive(pkd,pj));
    return (pkd->nActive = i);
    }


int pkdColRejects(PKD pkd,int nSplit) {
    int iRejects,i;

    mdlassert(pkd->mdl,pkd->nRejects == 0);

    pkd->nRejects = pkdLocal(pkd) - nSplit;
    iRejects = pkdFreeStore(pkd) - pkd->nRejects;
    /*
    ** Move rejects to High memory.
    */
    if (pkdLocal(pkd) != pkdFreeStore(pkd)) {
	for (i=pkd->nRejects-1;i>=0;--i)
	    pkdCopyParticle(pkd,pkdParticle(pkd,iRejects+i),pkdParticle(pkd,nSplit+i));
	}
    pkd->nLocal = nSplit;
    return(pkd->nRejects);
    }


int pkdSwapRejects(PKD pkd,int idSwap) {
    size_t nBuf;
    size_t nOutBytes,nSndBytes,nRcvBytes;

    if (idSwap != -1) {
	nBuf = (pkdSwapSpace(pkd))*pkdParticleSize(pkd);
	nOutBytes = pkd->nRejects*pkdParticleSize(pkd);
	mdlassert(pkd->mdl,pkdLocal(pkd) + pkd->nRejects <= pkdFreeStore(pkd));
	mdlSwap(pkd->mdl,idSwap,nBuf,pkdParticle(pkd,pkdLocal(pkd)),
		nOutBytes,&nSndBytes,&nRcvBytes);
	pkd->nLocal += nRcvBytes/pkdParticleSize(pkd);
	pkd->nRejects -= nSndBytes/pkdParticleSize(pkd);
	}
    return(pkd->nRejects);
    }

void pkdSwapAll(PKD pkd, int idSwap) {
    size_t nBuf;
    size_t nOutBytes,nSndBytes,nRcvBytes;
    int i;
    int iBuf;

    /*
    ** Move particles to High memory.
    */
    iBuf = pkdSwapSpace(pkd);
    for (i=pkdLocal(pkd)-1;i>=0;--i)
	pkdCopyParticle(pkd,pkdParticle(pkd,iBuf+i),pkdParticle(pkd,i));
    nBuf = pkdFreeStore(pkd)*pkdParticleSize(pkd);
    nOutBytes = pkdLocal(pkd)*pkdParticleSize(pkd);
    mdlSwap(pkd->mdl,idSwap,nBuf,pkdParticleBase(pkd), nOutBytes,
	    &nSndBytes, &nRcvBytes);
    mdlassert(pkd->mdl,nSndBytes/pkdParticleSize(pkd) == pkdLocal(pkd));
    pkd->nLocal = nRcvBytes/pkdParticleSize(pkd);
    }

int pkdSwapSpace(PKD pkd) {
    return(pkdFreeStore(pkd) - pkdLocal(pkd));
    }


int pkdFreeStore(PKD pkd) {
    return(pkd->nStore);
    }

int pkdActive(PKD pkd) {
    return(pkd->nActive);
    }

int pkdInactive(PKD pkd) {
    return(pkd->nLocal - pkd->nActive);
    }

int pkdLocal(PKD pkd) {
    return(pkd->nLocal);
    }

int pkdNodes(PKD pkd) {
    return(pkd->nNodes);
    }

/*
** Returns a pointer to the i'th KDN in the tree.  Used for fetching
** cache element.  Normal code should call pkdTreeNode().
*/
void *pkdTreeNodeGetElement(void *vData,int i,int iDataSize) {
    PKD pkd = vData;
    return pkdTreeNode(pkd,i);
    }

int pkdNumSrcActive(PKD pkd,uint8_t uRungLo,uint8_t uRungHi) {
    int i, n;
    for (n=0,i=0;i<pkdLocal(pkd);++i)
	if ( pkdIsSrcActive(pkdParticle(pkd,i),uRungLo,uRungHi) ) n++;
    return n;
    }

int pkdNumDstActive(PKD pkd,uint8_t uRungLo,uint8_t uRungHi) {
    int i, n;
    for (n=0,i=0;i<pkdLocal(pkd);++i)
	if ( pkdIsDstActive(pkdParticle(pkd,i),uRungLo,uRungHi) ) n++;
    return n;
    }

int pkdColOrdRejects(PKD pkd,uint64_t nOrdSplit,int iSplitSide) {
    int nSplit;
    if (iSplitSide) nSplit = pkdLowerOrdPart(pkd,nOrdSplit,0,pkdLocal(pkd)-1);
    else nSplit = pkdUpperOrdPart(pkd,nOrdSplit,0,pkdLocal(pkd)-1);
    return pkdColRejects(pkd,nSplit);
    }

int cmpParticles(const void *pva,const void *pvb) {
    PARTICLE *pa = (PARTICLE *)pva;
    PARTICLE *pb = (PARTICLE *)pvb;

    return(pa->iOrder - pb->iOrder);
    }


void pkdLocalOrder(PKD pkd,uint64_t iMinOrder, uint64_t iMaxOrder) {
    int i;
    assert(pkd->nLocal == iMaxOrder - iMinOrder + 1);
    for (i=0;i<pkd->nLocal;++i) {
	PARTICLE *p1 = pkdParticle(pkd,i);
	assert(p1->iOrder >= iMinOrder && p1->iOrder <= iMaxOrder);
	while(p1->iOrder - iMinOrder !=  i) {
	    PARTICLE *p2 = pkdParticle(pkd,p1->iOrder-iMinOrder);
	    pkdSwapParticle(pkd,p1,p2);
	    }
	}
    /* Above replaces: qsort(pkdParticleBase(pkd),pkdLocal(pkd),pkdParticleSize(pkd),cmpParticles); */
    }

#define MAX_IO_BUFFER_SIZE (1024*1024*1024)
#define ASYNC_COUNT 10

/*
** This does DIRECT I/O to avoid swamping memory with I/O buffers. Ideally this
** would not be required, but buffered I/O causes it to CRASH during I/O on the Cray
** (or any Linux system probably) when the amount of available memory is quite low.
** The test was performed on Piz Daint and Piz Dora and the memory statistics at
** the time were:
** Piz Dora (Cray XC40):
**   free memory (GB): min=   2.522 @ 1195 avg=   2.797 of  2024 std-dev=   0.257
**      resident size: max=  57.671 @  129 avg=  57.415 of  2024 std-dev=   0.256
** Piz Daint (Cray XC30):
**   free memory (GB): min=   1.599 @21174 avg=   1.870 of 34300 std-dev=   0.029
**      resident size: max=  27.921 @17709 avg=  27.870 of 34300 std-dev=   0.013
*/

#undef HAVE_LIBAIO_H
#if defined(HAVE_LIBAIO_H) || defined(HAVE_AIO_H)

#ifdef HAVE_LIBAIO_H
#include <libaio.h>
typedef struct {
    struct iocb cb[2*ASYNC_COUNT+1];
    struct io_event events[2*ASYNC_COUNT+1];
    off_t iFilePosition;   /* File position */
    size_t nBufferSize;
    char *pSource;         /* Source of particles (in pStore) */
    size_t nBytes;         /* Number of bytes left to write */
    io_context_t ctx;
    int nPageSize;
    int nBuffers;
    int fd;
    } asyncInfo;
#else
#include <aio.h>
typedef struct {
    struct aiocb cb[2*ASYNC_COUNT+1];
    struct aiocb const * pcb[2*ASYNC_COUNT+1];
    void *pBuffer[2*ASYNC_COUNT];
    off_t iFilePosition;   /* File position */
    size_t nBufferSize;
    char *pSource;         /* Source of particles (in pStore) */
    size_t nBytes;         /* Number of bytes left to write */
    int nPageSize;
    int nBuffers;
    int fd;
    } asyncInfo;
#endif

static void queue_dio(asyncInfo *info,int i,int bWrite) {
    size_t nBytes = info->nBytes > info->nBufferSize ? info->nBufferSize : info->nBytes;
    int rc;

    /* Align buffer size for direct I/O. File will be truncated before closing if writing */
    nBytes = (nBytes+info->nPageSize-1) & ~(info->nPageSize-1);
#ifdef HAVE_LIBAIO_H
    struct iocb *pcb = &info->cb[i];
    if (bWrite) io_prep_pwrite(info->cb+i,info->fd,info->pSource,nBytes,info->iFilePosition);
    else        io_prep_pread(info->cb+i,info->fd,info->pSource,nBytes,info->iFilePosition);
    rc = io_submit(info->ctx,1,&pcb);
    if (rc<0) { perror("io_submit"); abort(); }
#else
    info->cb[i].aio_buf = info->pSource;
    info->cb[i].aio_offset = info->iFilePosition;
    info->cb[i].aio_nbytes = nBytes;
    if (bWrite) rc = aio_write(&info->cb[i]);
    else rc = aio_read(&info->cb[i]);
    if (rc) { perror("aio_write/read"); abort(); }
#endif
    info->iFilePosition += nBytes;
    if (nBytes < info->nBytes) {
	info->pSource += nBytes;
	info->nBytes -= nBytes;
	}
    else info->nBytes = 0;
    }

static void asyncCheckpoint(PKD pkd,const char *fname,int bWrite) {
    size_t nFileSize;
    asyncInfo info;
    int i, rc;

    if (bWrite) {
	info.fd = open(fname,O_DIRECT|O_CREAT|O_WRONLY|O_TRUNC,S_IRWXU|S_IRWXG);
	if (info.fd<0) { perror(fname); abort(); }
	nFileSize = pkdParticleSize(pkd) * pkd->nLocal;
	}
    else {
	struct stat s;
	info.fd = open(fname,O_DIRECT|O_RDONLY);
	if (info.fd<0) { perror(fname); abort(); }
	if ( fstat(info.fd,&s) != 0 ) { perror(fname); abort(); }
	nFileSize = s.st_size;
	pkd->nLocal = nFileSize / pkdParticleSize(pkd);
	}

    /* Align transfers to a page boundary - require for direct I/O */
    info.nPageSize = sysconf(_SC_PAGESIZE);

    /*
    ** Calculate buffer size and count. We want at least ASYNC_COUNT (or more)
    ** buffers each of which are multiples of DIRECT_IO_SIZE bytes long.
    */
    info.nBufferSize = nFileSize / ASYNC_COUNT;
    info.nBufferSize = 1 << (int)log2(info.nBufferSize); /* Prefer power of two */
    if (info.nBufferSize < info.nPageSize) info.nBufferSize = info.nPageSize;
    if (info.nBufferSize > MAX_IO_BUFFER_SIZE) info.nBufferSize = MAX_IO_BUFFER_SIZE;
    info.nBuffers = nFileSize / info.nBufferSize + 1;
    if (info.nBuffers > 2*ASYNC_COUNT+1) info.nBuffers = 2*ASYNC_COUNT+1;

#ifdef HAVE_LIBAIO_H
    info.ctx = 0;
    rc = io_setup(info.nBuffers, &info.ctx);
    if (rc<0) { perror("io_setup"); abort(); }
#else
    memset(&info.cb,0,sizeof(info.cb));
    for(i=0; i<info.nBuffers; ++i) {
	info.pcb[i] = info.cb + i;
	info.cb[i].aio_fildes = info.fd;
	info.cb[i].aio_offset = 0;
	info.cb[i].aio_buf = NULL;
	info.cb[i].aio_nbytes = 0;
	info.cb[i].aio_sigevent.sigev_notify = SIGEV_NONE;
	info.cb[i].aio_lio_opcode = LIO_NOP;
	}
#endif
    info.pSource = (char *)pkdParticleBase(pkd);
    info.nBytes = nFileSize;
    /* Queue as many operations as we have buffers */
    info.iFilePosition = 0;
    for(i=0; i<info.nBuffers && info.nBytes; ++i) queue_dio(&info,i,bWrite);
    info.nBuffers = i;

#ifdef HAVE_LIBAIO_H
    /* Main loop. Keep going until nothing left to do */
    int nInFlight = i;
    while (nInFlight) {
	int nEvent = io_getevents(info.ctx,1,info.nBuffers,info.events,NULL);
	if (nEvent<=0) { perror("aio_getevents"); abort(); }
	for(i=0; i<nEvent; ++i) {
	    ssize_t nWritten = info.events[i].res;
	    if (nWritten < 0 || nWritten >  info.events[i].obj->u.c.nbytes) {
		char szError[100];
		fprintf(stderr,"errno=%d nBytes=%lu: nWritten=%lu%s\n",
		    errno,info.events[i].obj->u.c.nbytes,
		    nWritten,
		    strerror((long)info.events[i].res));
		perror(szError);
		abort();
		}
	    else if (info.nBytes) {
		queue_dio(&info,info.events[i].obj-info.cb,bWrite);
		}
	    else --nInFlight;
	    }
	}
    rc = io_destroy(info.ctx);
    if (rc<0) { perror("io_destroy"); abort(); }
#else
    /* Main loop. Keep going until nothing left to do */
    int bDone;
    do {
	rc = aio_suspend(info.pcb,info.nBuffers,NULL);
	if (rc) { perror("aio_suspend"); abort(); }
	bDone = 1;
	for(i=0; i<info.nBuffers; ++i) {
	    if (info.pcb[i] == NULL) continue;
	    rc = aio_error(info.pcb[i]);
	    if (rc == EINPROGRESS) bDone = 0;
	    else if (rc == 0) {
		ssize_t nWritten = aio_return(&info.cb[i]);
		if (nWritten != info.cb[i].aio_nbytes) {
		    char szError[100];
		    sprintf(szError,"errno=%d nBytes=%llu nBytesWritten=%llu\n",
			errno,info.cb[i].aio_nbytes,nWritten);
		    perror(szError);
		    abort();
		    }
		if (info.nBytes) {
		    bDone = 0;
		    queue_dio(&info,i,bWrite);
		    }
		else info.pcb[i] = NULL;
		}
	    else { perror("aio_error"); abort(); }
	    }
	} while(!bDone);
#endif
    /* Record the actual file size */
    if (bWrite) ftruncate(info.fd,nFileSize);
    close(info.fd);
    }
#endif

#define WRITE_LIMIT (1024*1024*1024)
static void simpleCheckpoint(PKD pkd,const char *fname) {
    int fd = open(fname,O_CREAT|O_WRONLY|O_TRUNC,S_IRWXU|S_IRWXG);
    size_t nBytesToWrite = pkdParticleSize(pkd) * pkd->nLocal;
    char *pBuffer = (char *)pkdParticleBase(pkd);
    ssize_t nBytesWritten;
    if (fd<0) { perror(fname); abort(); }
    while(nBytesToWrite) {
	size_t nWrite = nBytesToWrite > WRITE_LIMIT ? WRITE_LIMIT : nBytesToWrite;
	nBytesWritten = write(fd,pBuffer,nWrite);
	if (nBytesWritten != nWrite) {
	    char szError[100];
	    sprintf(szError,"errno=%d nBytes=%llu nWrite=%llu\n",
		errno,nBytesWritten,nWrite);
	    perror(szError);
	    abort();
	    }
	pBuffer += nWrite;
	nBytesToWrite -= nWrite;
	}
    close(fd);
    }

void pkdCheckpoint(PKD pkd,const char *fname) {
#if defined(HAVE_LIBAIO_H) || defined(HAVE_AIO_H)
    asyncCheckpoint(pkd,fname,1);
#else
    simpleCheckpoint(pkd,fname);
#endif
    }

#define READ_LIMIT (1024*1024*1024)
static void simpleRestore(PKD pkd,const char *fname) {
    int fd = open(fname,O_RDONLY);
    if (fd<0) { perror(fname); abort(); }

    struct stat s;
    if ( fstat(fd,&s) != 0 ) { perror(fname); abort(); }

    size_t nBytesToRead = s.st_size;
    pkd->nLocal = nBytesToRead / pkdParticleSize(pkd);
    char *pBuffer = (char *)pkdParticleBase(pkd);
    ssize_t nBytesRead;
    while(nBytesToRead) {
	size_t nRead = nBytesToRead > READ_LIMIT ? READ_LIMIT : nBytesToRead;
	nBytesRead = read(fd,pBuffer,nRead);
	if (nBytesRead != nRead) {
	    char szError[100];
	    sprintf(szError,"errno=%d nBytes=%llu nRead=%llu\n",
		errno,nBytesRead,nRead);
	    perror(szError);
	    abort();
	    }
	pBuffer += nRead;
	nBytesToRead -= nRead;
	}
    close(fd);
    }

void pkdRestore(PKD pkd,const char *fname) {
#if defined(HAVE_LIBAIO_H) || defined(HAVE_LIBAIO_H)
    asyncCheckpoint(pkd,fname,0);
#else
    simpleRestore(pkd,fname);
#endif
    }

static void writeParticle(PKD pkd,FIO fio,double dvFac,BND *bnd,PARTICLE *p) {
    STARFIELDS *pStar;
    SPHFIELDS *pSph;
    float *pPot, dummypot;
    double v[3],r[3];
    float fMass, fSoft, fDensity;
    uint64_t iParticleID;
    int j;

    dummypot = 0.0;

    if ( pkd->oPotential) pPot = pkdPot(pkd,p);
    else pPot = &dummypot;
    if (pkd->oVelocity) {
	vel_t *pV = pkdVel(pkd,p);
	v[0] = pV[0] * dvFac;
	v[1] = pV[1] * dvFac;
	v[2] = pV[2] * dvFac;
	}
    else v[0] = v[1] = v[2] = 0.0;
 
    /* Initialize SPH fields if present */
    if (pkd->oSph) pSph = pkdField(p,pkd->oSph);
    else pSph = NULL;
    if (pkd->oStar) pStar = pkdField(p,pkd->oStar);
    else pStar = NULL;
    fMass = pkdMass(pkd,p);
    fSoft = pkdSoft0(pkd,p);
    if (pkd->oParticleID) iParticleID = *pkdParticleID(pkd,p);
    else iParticleID = p->iOrder;
    if (pkd->oDensity) fDensity = pkdDensity(pkd,p);
    else fDensity = 0.0;

    r[0] = pkdPos(pkd,p,0);
    r[1] = pkdPos(pkd,p,1);
    r[2] = pkdPos(pkd,p,2);
    /* Enforce periodic boundaries */
    for (j=0;j<3;++j) {
	if (r[j] < bnd->fCenter[j] - bnd->fMax[j]) r[j] += 2*bnd->fMax[j];
	else if (r[j] >= bnd->fCenter[j] + bnd->fMax[j]) r[j] -= 2*bnd->fMax[j];
	/*
	** If it still doesn't lie in the "unit" cell then something has gone quite wrong with the 
	** simulation. Either we have a super fast particle or the initial condition is somehow not conforming
	** to the specified periodic box in a gross way.
	*/
	mdlassert(pkd->mdl,((r[j] >= bnd->fCenter[j] - bnd->fMax[j])&&
		(r[j] < bnd->fCenter[j] + bnd->fMax[j])));
	
	}

    switch(pkdSpecies(pkd,p)) {
    case FIO_SPECIES_SPH:
	assert(pSph);
	assert(pkd->param.dTuFac>0.0);
	    {
	    double T;
#ifdef COOLING
	    COOLPARTICLE cp;
	    if (pkd->param.bGasCooling) {
		double E = pSph->u;
		CoolTempFromEnergyCode( pkd->Cool, 
		    &cp, &E, &T, p->fDensity, pSph->fMetals );
		}
	    else T = pSph->u/pkd->param.dTuFac;
#else
	    T = pSph->u/pkd->param.dTuFac;
#endif
	    fioWriteSph(fio,iParticleID,r,v,fMass,fSoft,*pPot,
		fDensity,T,pSph->fMetals);
	    }
	break;
    case FIO_SPECIES_DARK:
	fioWriteDark(fio,iParticleID,r,v,fMass,fSoft,*pPot,fDensity);
	break;
    case FIO_SPECIES_STAR:
	assert(pStar && pSph);
	fioWriteStar(fio,iParticleID,r,v,fMass,fSoft,*pPot,fDensity,
	    pSph->fMetals,pStar->fTimer);
	break;
    default:
	fprintf(stderr,"Unsupported particle type: %d\n",pkdSpecies(pkd,p));
	assert(0);
	}

    }

struct packWriteCtx {
    PKD pkd;
    FIO fio;
    BND *bnd;
    double dvFac;
    int iIndex;
    };

static int unpackWrite(void *vctx, int *id, size_t nSize, void *vBuff) {
    struct packWriteCtx *ctx = (struct packWriteCtx *)vctx;
    PKD pkd = ctx->pkd;
    PARTICLE *p = (PARTICLE *)vBuff;
    int n = nSize / pkdParticleSize(pkd);
    int i;
    assert( n*pkdParticleSize(pkd) == nSize);
    for(i=0; i<n; ++i) {
	writeParticle(pkd,ctx->fio,ctx->dvFac,ctx->bnd,pkdParticleGet(pkd,p,i));
	}
    return 1;
    }

void pkdWriteFromNode(PKD pkd,int iNode, FIO fio,double dvFac,BND *bnd) {
    struct packWriteCtx ctx;
    ctx.pkd = pkd;
    ctx.fio = fio;
    ctx.bnd = bnd;
    ctx.dvFac = dvFac;
    ctx.iIndex = 0;
#ifdef MPI_VERSION
    mdlRecv(pkd->mdl,iNode,unpackWrite,&ctx);
#endif
    }

static int packWrite(void *vctx, int *id, size_t nSize, void *vBuff) {
    struct packWriteCtx *ctx = (struct packWriteCtx *)vctx;
    PKD pkd = ctx->pkd;
    int nLeft = pkd->nLocal - ctx->iIndex;
    int n = nSize / pkdParticleSize(pkd);
    if ( n > nLeft ) n = nLeft;
    nSize = n*pkdParticleSize(pkd);
    memcpy(vBuff,pkdParticle(pkd,ctx->iIndex), nSize );
    ctx->iIndex += n;
    return nSize;
    }

/* Send all particled data to the specified node for writing */
void pkdWriteViaNode(PKD pkd, int iNode) {
    struct packWriteCtx ctx;
    ctx.pkd = pkd;
    ctx.fio = NULL;
    ctx.dvFac = 1.0;
    ctx.iIndex = 0;
#ifdef MPI_VERSION
    mdlSend(pkd->mdl,iNode,packWrite, &ctx);
#endif
    }

uint32_t pkdWriteFIO(PKD pkd,FIO fio,double dvFac,BND *bnd) {
    PARTICLE *p;
    int i;
    uint32_t nCount;
    nCount = 0;
    for (i=0;i<pkdLocal(pkd);++i) {
	p = pkdParticle(pkd,i);
	if ( !pkdIsSrcActive(p,0,MAX_RUNG) ) continue;  /* JW: Ack! */
	writeParticle(pkd,fio,dvFac,bnd,p);
	nCount++;
	}
    return nCount;
    }

void pkdSetSoft(PKD pkd,double dSoft) {
    pkd->fSoftFix = dSoft;
    }

void pkdPhysicalSoft(PKD pkd,double dSoftMax,double dFac,int bSoftMaxMul) {
    pkd->fSoftFac = dFac;
    pkd->fSoftMax = bSoftMaxMul ? HUGE : dSoftMax;
    }

void
pkdGravAll(PKD pkd,uint8_t uRungLo,uint8_t uRungHi,
    int bKickClose,int bKickOpen,double *dtClose,double *dtOpen,
    double dAccFac,double dTime,int nReps,int bPeriodic,
    int iOrder,int bEwald,int nGroup,int iRoot1, int iRoot2,
    double fEwCut,double fEwhCut,double dThetaMin,
    uint64_t *pnActive,
    double *pdPart,double *pdPartNumAccess,double *pdPartMissRatio,
    double *pdCell,double *pdCellNumAccess,double *pdCellMissRatio,
    double *pdFlop,uint64_t *pnRung) {

    double dActive;
    double dPartSum;
    double dCellSum;
    int i;

#ifdef USE_ITT
    __itt_domain* domain = __itt_domain_create("MyTraces.MyDomain");
    __itt_string_handle* shMyTask = __itt_string_handle_create("Gravity");
    __itt_task_begin(domain, __itt_null, __itt_null, shMyTask);
#endif

    /*
    ** Clear all the rung counters to be safe.
    */
    for (i=0;i<=IRUNGMAX;++i) pkd->nRung[i] = 0;
     
    pkdClearTimer(pkd,1);
#if defined(INSTRUMENT) && defined(HAVE_TICK_COUNTER)
    mdlTimeReset(pkd->mdl);
#endif

    /*
    ** Set up Ewald tables and stuff.
    */
    if (bPeriodic && bEwald) {
	pkdEwaldInit(pkd,nReps,fEwCut,fEwhCut);	/* ignored in Flop count! */
	}
    /*
    ** Start particle caching space (cell cache already active).
    */
    mdlROcache(pkd->mdl,CID_PARTICLE,NULL,pkdParticleBase(pkd),pkdParticleSize(pkd),
	       pkdLocal(pkd));
    /*
    ** Calculate newtonian gravity, including replicas if any.
    */
    *pdFlop = 0.0;
    dPartSum = 0.0;
    dCellSum = 0.0;
    pkdStartTimer(pkd,1);
    *pnActive = pkdGravWalk(pkd,uRungLo,uRungHi,bKickClose,bKickOpen,dtClose,dtOpen,dAccFac,dTime,nReps,bPeriodic && bEwald,nGroup,iRoot1,iRoot2,0,dThetaMin,pdFlop,&dPartSum,&dCellSum);
    pkdStopTimer(pkd,1);

    dActive = (double)(*pnActive);
    if (*pnActive) {
	*pdPart = dPartSum/dActive;
	*pdCell = dCellSum/dActive;
	}
    else {
	assert(dPartSum == 0 && dCellSum == 0);
	*pdPart = 0;  /* for the statistics we don't count this processor, see pstGravity(). */
	*pdCell = 0;
	}
    /*
    ** Get caching statistics.
    */
    if (*pnActive) {
	*pdCellNumAccess = mdlNumAccess(pkd->mdl,CID_CELL)/dActive;
	*pdPartNumAccess = mdlNumAccess(pkd->mdl,CID_PARTICLE)/dActive;
	}
    else {
	*pdCellNumAccess = 0;
	*pdPartNumAccess = 0;
	}
    *pdCellMissRatio = 100.0*mdlMissRatio(pkd->mdl,CID_CELL);      /* as a percentage */
    *pdPartMissRatio = 100.0*mdlMissRatio(pkd->mdl,CID_PARTICLE);  /* as a percentage */
    /*
    ** Output flops count in GFlops!
    */
    *pdFlop *= 1e-9;
    /*
    ** Stop particle caching space.
    */
    mdlFinishCache(pkd->mdl,CID_PARTICLE);

    for (i=0;i<=IRUNGMAX;++i) pnRung[i] = pkd->nRung[i];

#ifdef USE_ITT
    __itt_task_end(domain);
#endif
    }

/* This became easier; we already calculated these values on the fly */
void pkdCalcEandL(PKD pkd,double *T,double *U,double *Eth,double *L,double *F,double *W) {
    int i;

    *T = pkd->dEnergyT;
    *U = pkd->dEnergyU;
    *W = pkd->dEnergyW;
    for(i=0; i<3; ++i) {
	L[i] = pkd->dEnergyL[i];
	F[i] = pkd->dEnergyF[i];
	}
    if (pkd->oSph) {
	int n = pkdLocal(pkd);
	*Eth = 0.0;
	for (i=0;i<n;++i) {
	    PARTICLE *p = pkdParticle(pkd,i);
	    float fMass = pkdMass(pkd,p);
	    if (pkdIsGas(pkd,p)) *Eth += fMass*pkdSph(pkd,p)->u;
	    }
	}
    }


void pkdScaleVel(PKD pkd,double dvFac) {
    PARTICLE *p;
    vel_t *v;
    int i,j,n;
    n = pkdLocal(pkd);
    for (i=0;i<n;++i) {
	p = pkdParticle(pkd,i);
	v = pkdVel(pkd,p);
	for (j=0;j<3;++j) v[j] *= dvFac;
	}
    }

/*
** Drift particles whose Rung falls between uRungLo (large step) and uRungHi (small step) inclusive,
** and those whose destination activity flag is set.
**
** Note that the drift funtion no longer wraps the particles around the periodic "unit" cell. This is
** now done by Domain Decomposition only.
*/
void pkdDrift(PKD pkd,double dTime,double dDelta,double dDeltaVPred,double dDeltaUPred,uint8_t uRungLo,uint8_t uRungHi) {
    PARTICLE *p;
    vel_t *v;
    float *a;
    SPHFIELDS *sph;
    int i,j,n;
    double r1[3],r0[3],mr0,mr1,x,dMin[3],dMax[3];
    double dLightSpeed = dLightSpeedSim(pkd->param.dBoxSize);
    double dLookbackTime = pkd->dTimeRedshift0 - dTime;

    mdlDiag(pkd->mdl, "Into pkdDrift\n");
    assert(pkd->oVelocity);

    for (j=0;j<3;++j) {
	dMin[j] = pkd->bnd.fCenter[j] - pkd->bnd.fMax[j];
	dMax[j] = pkd->bnd.fCenter[j] + pkd->bnd.fMax[j];
	}
    n = pkdLocal(pkd);
    /*
    ** Update particle positions
    */
    if (pkd->param.bDoGas) {
	assert(pkd->oSph);
	assert(pkd->oAcceleration);
	for (i=0;i<n;++i) {
	    p = pkdParticle(pkd,i);
	    if (pkdIsRungRange(p,uRungLo,uRungHi)) {
		v = pkdVel(pkd,p);
		if (pkdIsGas(pkd,p)) {
		    a = pkdAccel(pkd,p);
		    sph = pkdSph(pkd,p);
		    for (j=0;j<3;++j) { /* NB: Pred quantities must be done before std. */
			sph->vPred[j] += a[j]*dDeltaVPred;
			}
		    sph->uPred += sph->uDot*dDeltaUPred;
		    sph->fMetalsPred += sph->fMetalsDot*dDeltaUPred;
		    }
		for (j=0;j<3;++j) {
		    pkdSetPos(pkd,p,j,r1[j] = pkdPos(pkd,p,j) + dDelta*v[j]);
		    }
		pkdMinMax(r1,dMin,dMax);
		}
	    }
	}
    /*
    ** If the light surface enters the unit box, then we can start generating light cone output. 
    */
    else if (pkd->param.bLightCone && dLookbackTime*dLightSpeed < 0.5) {
	for (i=0;i<n;++i) {
	    p = pkdParticle(pkd,i);
	    if (pkdIsRungRange(p,uRungLo,uRungHi)) {
		v = pkdVel(pkd,p);
		pkdGetPos1(pkd,p,r0);
		for (j=0;j<3;++j) r1[j] = r0[j] + dDelta*v[j];
		mr0 = sqrt(r0[0]*r0[0] + r0[1]*r0[1] + r0[2]*r0[2]);
		mr1 = sqrt(r1[0]*r1[0] + r1[1]*r1[1] + r1[2]*r1[2]);
		x = (dLightSpeed*dLookbackTime - mr0)/(dLightSpeed*dDelta - mr0 + mr1);
		if (x >=0 && x < 1) {
		    /*
		    ** Create a new light cone particle.
		    */
		    if (pkd->param.bLightConeParticles) {
			mdlassert(pkd->mdl,pkd->nLightCone < pkd->nStore);
			for (j=0;j<3;++j) {
			    pkd->pLightCone[pkd->nLightCone].pos[j] = (1-x)*r0[j] + x*r1[j]; 
			    }
			++pkd->nLightCone;
			}
		    }
		for (j=0;j<3;++j) pkdSetPos(pkd,p,j,r1[j]);
		pkdMinMax(r1,dMin,dMax);
		}
	    }
	}
    else {
	for (i=0;i<n;++i) {
	    p = pkdParticle(pkd,i);
	    if (pkdIsRungRange(p,uRungLo,uRungHi)) {
		v = pkdVel(pkd,p);
		pkdGetPos1(pkd,p,r0);
		for (j=0;j<3;++j) pkdSetPos(pkd,p,j,r1[j] = r0[j] + dDelta*v[j]);
		pkdMinMax(r1,dMin,dMax);
		}
	    }
	}
    for (j=0;j<3;++j) {
	pkd->bnd.fCenter[j] = 0.5*(dMin[j] + dMax[j]);
	pkd->bnd.fMax[j] = 0.5*(dMax[j] - dMin[j]);
	}
    mdlDiag(pkd->mdl, "Out of pkdDrift\n");
    }


void pkdGravityVeryActive(PKD pkd,uint8_t uRungLo,uint8_t uRungHi,double dTime,int bEwald,int nGroup,int nReps,
			  double dStep,double dTheta) {
    int nActive;
    double dFlop,dPartSum,dCellSum;

    /*
    ** Calculate newtonian gravity for the very active particles ONLY, including replicas if any.
    */
    dFlop = 0.0;
    dPartSum = 0.0;
    dCellSum = 0.0;
    nActive = pkdGravWalk(pkd,uRungLo,uRungHi,0,0,NULL,NULL,1.0,dTime,nReps,bEwald,nGroup,ROOT,0,VAROOT,dTheta,&dFlop,&dPartSum,&dCellSum);
    }


void pkdStepVeryActiveKDK(PKD pkd,uint8_t uRungLo,uint8_t uRungHi,double dStep, double dTime, double dDelta,
			  int iRung, int iKickRung, int iRungVeryActive,int iAdjust, double dThetaMin,
			  int *pnMaxRung, double aSunInact[], double adSunInact[], double dSunMass) {
    uint64_t nRungCount[256];
    double dDriftFac;

    if (iAdjust && (iRung < pkd->param.iMaxRung)) {

	/*
	** The following should be replaced with a single call which sets the rungs of all particles.
	*/
	pkdActiveRung(pkd, iRung, 1);
	if (pkd->param.bAccelStep) {
	    double a = csmTime2Exp(pkd->param.csm,dTime);
	    double dVelFac = 1.0/(a*a);
	    double dAccFac = 1.0/(a*a*a);
	    double dhMinOverSoft = 0;
	    pkdAccelStep(pkd,uRungLo,uRungHi,pkd->param.dEta, dVelFac,dAccFac,pkd->param.bDoGravity,
			 pkd->param.bEpsAccStep,dhMinOverSoft);
	    }
	*pnMaxRung = pkdUpdateRung(pkd,iRung,pkd->param.iMaxRung,
				   iRung,pkd->param.iMaxRung, nRungCount);


	if (pkd->param.bVDetails) {
	    printf("%*cAdjust at iRung: %d, nMaxRung:%d nRungCount[%d]=%lld\n",
		   2*iRung+2,' ',iRung,*pnMaxRung,*pnMaxRung,nRungCount[*pnMaxRung]);
	    }

	}
    if (iRung > iRungVeryActive) {	/* skip this if we are
					   entering for the first
					   time: Kick is taken care of
					   in master().
					*/
	if (pkd->param.bVDetails) {
	    printf("%*cVeryActive pkdKickOpen  at iRung: %d, 0.5*dDelta: %g\n",
		   2*iRung+2,' ',iRung,0.5*dDelta);
	    }
	pkdKickKDKOpen(pkd,dTime,0.5*dDelta,iRung,iRung);
	}
    if (*pnMaxRung > iRung) {
	/*
	** Recurse.
	*/
	pkdStepVeryActiveKDK(pkd,uRungLo,uRungHi,dStep,dTime,0.5*dDelta,iRung+1,iRung+1,iRungVeryActive,0,
			     dThetaMin,pnMaxRung,aSunInact,adSunInact,dSunMass);
	dStep += 1.0/(2 << iRung);
	dTime += 0.5*dDelta;

	pkdActiveRung(pkd,iRung,0);   /* is this needed? */

	pkdStepVeryActiveKDK(pkd,uRungLo,uRungHi,dStep,dTime,0.5*dDelta,iRung+1,iKickRung,iRungVeryActive,1,
			     dThetaMin,pnMaxRung,aSunInact,adSunInact,dSunMass);
	}
    else {
	if (pkd->param.bVDetails) {
	    printf("%*cVeryActive Drift at iRung: %d, drifting %d and higher with dDelta: %g\n",
		   2*iRung+2,' ',iRung,iRungVeryActive+1,dDelta);
	    }
	/*
	** We need to account for cosmological drift factor here!
	** Normally this is done at the MASTER level in msrDrift.
	** Note that for kicks we have written new "master-like" functions
	** KickOpen and KickClose which do this same job at PKD level.
	*/
	if (pkd->param.csm->bComove) {
	    dDriftFac = csmComoveDriftFac(pkd->param.csm,dTime,dDelta);
	    }
	else {
	    dDriftFac = dDelta;
	    }
	/*
	** This should drift *all* very actives!
	*/
	pkdDrift(pkd,dTime,dDriftFac,0,0,iRungVeryActive+1,MAX_RUNG);
	dTime += dDelta;
	dStep += 1.0/(1 << iRung);

	if (iKickRung > iRungVeryActive) {	/* skip this if we are
						   entering for the first
						   time: Kick is taken care of
						   in master().
						*/

	    if (pkd->param.bVDetails) {
		printf("%*cGravityVA: iRung %d Gravity for rungs %d to %d ... ",
		       2*iRung+2,' ',iRung,iKickRung,*pnMaxRung);
		}

	    pkdActiveRung(pkd,iKickRung,1);
//	    pkdVATreeBuild(pkd,pkd->param.nBucket);
	    pkdGravityVeryActive(pkd,uRungLo,uRungHi,dTime,pkd->param.bEwald && pkd->param.bPeriodic,pkd->param.nGroup,
				 pkd->param.nReplicas,dStep,dThetaMin);

	    }
	/*
	 * move time back to 1/2 step so that KickClose can integrate
	 * from 1/2 through the timestep to the end.
	 */
	dTime -= 0.5*dDelta;
	}
    if (iKickRung > iRungVeryActive) {	/* skip this if we are
						   entering for the first
						   time: Kick is taken care of
						   in master().
						*/
	if (pkd->param.bVDetails) {
	    printf("%*cVeryActive pkdKickClose at iRung: %d, 0.5*dDelta: %g\n",
		   2*iRung+2,' ',iRung,0.5*dDelta);
	    }
	pkdKickKDKClose(pkd,dTime,0.5*dDelta,iRung,iRung);
	}
    }


/*
 * Stripped down versions of routines from master.c
 */
void pkdKickKDKOpen(PKD pkd,double dTime,double dDelta,uint8_t uRungLo,uint8_t uRungHi) {
    if (pkd->param.csm->bComove) {
	dDelta = csmComoveKickFac(pkd->param.csm,dTime,dDelta);
    }
    pkdKick(pkd,dTime,dDelta,0,0,0,uRungLo,uRungHi);
    }

void pkdKickKDKClose(PKD pkd,double dTime,double dDelta,uint8_t uRungLo,uint8_t uRungHi) {
    if (pkd->param.csm->bComove) {
	dDelta = csmComoveKickFac(pkd->param.csm,dTime,dDelta);
    }
    pkdKick(pkd,dTime,dDelta,0,0,0,uRungLo,uRungHi);
    }


void pkdKick(PKD pkd,double dTime,double dDelta,double dDeltaVPred,double dDeltaU,double dDeltaUPred,uint8_t uRungLo,uint8_t uRungHi) {
    PARTICLE *p;
    vel_t *v;
    float *a;
    SPHFIELDS *sph;
    int i,j,n;

    assert(pkd->oVelocity);
    assert(pkd->oAcceleration);

    pkdClearTimer(pkd,1);
    pkdStartTimer(pkd,1);

    if (pkd->param.bDoGas) {
	assert(pkd->oSph);
	n = pkdLocal(pkd);
	for (i=0;i<n;++i) {
	    p = pkdParticle(pkd,i);
	    if (pkdIsRungRange(p,uRungLo,uRungHi)) {
		a = pkdAccel(pkd,p);
		v = pkdVel(pkd,p);
		if (pkdIsGas(pkd,p)) {
		    sph = pkdSph(pkd,p);
		    for (j=0;j<3;++j) { /* NB: Pred quantities must be done before std. */
			sph->vPred[j] = v[j] + a[j]*dDeltaVPred;
			}
		    sph->uPred = sph->u + sph->uDot*dDeltaUPred;
		    sph->u += sph->uDot*dDeltaU;
		    sph->fMetalsPred = sph->fMetals + sph->fMetalsDot*dDeltaUPred;
		    sph->fMetals += sph->fMetalsDot*dDeltaU;
		    }
		for (j=0;j<3;++j) {
		    v[j] += a[j]*dDelta;
		    }
		}
	    }
	}
    else {
	n = pkdLocal(pkd);
	for (i=0;i<n;++i) {
	    p = pkdParticle(pkd,i);
	    if (pkdIsRungRange(p,uRungLo,uRungHi)) {
		a = pkdAccel(pkd,p);
		v = pkdVel(pkd,p);
		for (j=0;j<3;++j) {
		    v[j] += a[j]*dDelta;
		    }
		}
	    }
	}


    pkdStopTimer(pkd,1);
    mdlDiag(pkd->mdl, "Done pkdkick\n");
    }

/* Kick the tree at iRoot. */
void pkdKickTree(PKD pkd,double dTime,double dDelta,double dDeltaVPred,double dDeltaU,double dDeltaUPred,int iRoot) {
    KDN *c;
    PARTICLE *p;
    vel_t *v;
    float *a;
    int i,j;

    /* Skip to local tree */
    c = pkdTreeNode(pkd,iRoot);
    while(c->bRemote) c = pkdTreeNode(pkd,iRoot = c->iLower);

    /* Now just kick all of the particles in the tree */
    for(i=c->pLower; i<=c->pUpper; ++i) {
	p = pkdParticle(pkd,i);
	a = pkdAccel(pkd,p);
	v = pkdVel(pkd,p);
	for (j=0;j<3;++j) {
	    v[j] += a[j]*dDelta;
	    a[j] = 0.0;
	    }
	}
    }

void pkdInitStep(PKD pkd, struct parameters *p, CSM csm) {
    pkd->param = *p;
    /*
    ** Need to be careful to correctly copy the cosmo
    ** parameters. This is very ugly!
    */
    csmInitialize(&pkd->param.csm);
    *pkd->param.csm = *csm;
    /*
    ** Also set up the time of redshift 0 which is needed for the 
    ** generation of light cone outputs.
    */
    pkd->dTimeRedshift0 = csmExp2Time(csm,1.0);
    }


void pkdSetRung(PKD pkd,uint8_t uRungLo, uint8_t uRungHi, uint8_t uRung) {
    PARTICLE *p;
    int i;

    for (i=0;i<pkdLocal(pkd);++i) {
	p = pkdParticle(pkd,i);
	if ( !pkdIsDstActive(p,uRungLo,uRungHi) ) continue;
	p->uRung = p->uNewRung = uRung;
	}
    }

void pkdZeroNewRung(PKD pkd,uint8_t uRungLo, uint8_t uRungHi, uint8_t uRung) {  /* JW: Ugly -- need to clean up */
    PARTICLE *p;
    int i;

    for (i=0;i<pkdLocal(pkd);++i) {
	p = pkdParticle(pkd,i);
	if ( !pkdIsActive(pkd,p) ) continue;
	p->uNewRung = 0;
	}
    }

void pkdActiveRung(PKD pkd, int iRung, int bGreater) {
    pkd->uMinRungActive = iRung;
    pkd->uMaxRungActive = bGreater ? 255 : iRung;
    }

void pkdCountRungs(PKD pkd,uint64_t *nRungs) {
    PARTICLE *p;
    int i;
    for (i=0;i<=MAX_RUNG;++i) nRungs[i] = 0;

    for (i=0;i<pkdLocal(pkd);++i) {
	p = pkdParticle(pkd,i);
	++nRungs[p->uRung];
	}
    }

void pkdAccelStep(PKD pkd, uint8_t uRungLo,uint8_t uRungHi,
		  double dEta,double dVelFac,double dAccFac,
		  int bDoGravity,int bEpsAcc,double dhMinOverSoft) {
    PARTICLE *p;
    float *a, *pPot;
    vel_t *v;
    int i,uNewRung;
    double vel;
    double acc;
    int j;
    double dT;
    FLOAT fSoft;

    assert(pkd->oVelocity);
    assert(pkd->oAcceleration);

    for (i=0;i<pkdLocal(pkd);++i) {
	p = pkdParticle(pkd,i);
	if (pkdIsActive(pkd,p)) {
	    v = pkdVel(pkd,p);
	    a = pkdAccel(pkd,p);
	    fSoft = pkdSoft(pkd,p);
	    vel = 0;
	    acc = 0;
	    for (j=0;j<3;j++) {
		vel += v[j]*v[j];
		acc += a[j]*a[j];
		}
	    mdlassert(pkd->mdl,vel >= 0);
	    vel = sqrt(vel)*dVelFac;
	    mdlassert(pkd->mdl,acc >= 0);
	    acc = sqrt(acc)*dAccFac;
	    dT = FLOAT_MAXVAL;
	    if (acc>0) {
		if (bEpsAcc) {
		    dT = dEta*sqrt(fSoft/acc);
		    }
		}
	    uNewRung = pkdDtToRung(dT,pkd->param.dDelta,pkd->param.iMaxRung);
	    if (uNewRung > p->uNewRung) p->uNewRung = uNewRung;
	    }
	}
    }


void pkdSphStep(PKD pkd, uint8_t uRungLo,uint8_t uRungHi,double dAccFac) {
    PARTICLE *p;
    float *a, uDot;
    int i,j,uNewRung;
    double acc;
    double dtNew;
    int u1,u2,u3;

    assert(pkd->oAcceleration);
    assert(pkd->oSph);

    for (i=0;i<pkdLocal(pkd);++i) {
	p = pkdParticle(pkd,i);
	if (pkdIsActive(pkd,p)) {
	    if (pkdIsGas(pkd,p)) {
		u1 = p->uNewRung;
		a = pkdAccel(pkd,p);
		acc = 0;
		for (j=0;j<3;j++) {
		    acc += a[j]*a[j];
		    }
		acc = sqrt(acc)*dAccFac;
		dtNew = FLOAT_MAXVAL;
		if (acc>0) dtNew = pkd->param.dEta*sqrt(pkdBall(pkd,p)/acc);
		u2 = pkdDtToRung(dtNew,pkd->param.dDelta,pkd->param.iMaxRung);
		uDot = *pkd_uDot(pkd,p);
		u3=0;
		if (uDot < 0) {
		    double dtemp = pkd->param.dEtaUDot*(*pkd_u(pkd,p))/fabs(uDot);
		    if (dtemp < dtNew) dtNew = dtemp;
		    u3 = pkdDtToRung(dtemp,pkd->param.dDelta,pkd->param.iMaxRung);
		    }
		uNewRung = pkdDtToRung(dtNew,pkd->param.dDelta,pkd->param.iMaxRung);
		if (uNewRung > p->uNewRung) p->uNewRung = uNewRung;
		if (!(p->iOrder%10000) || (p->uNewRung > 5 && !(p->iOrder%1000))) {
		    SPHFIELDS *sph = pkdSph(pkd,p);
#ifdef COOLING
		    double T, E = sph->u;
		    if (pkd->param.bGasIsothermal) T = E/pkd->param.dTuFac;
		    else {
			COOLPARTICLE cp;
			CoolTempFromEnergyCode( pkd->Cool, &cp, &E, &T, p->fDensity, sph->fMetals );
		    }

#else
		    /*T = E/pkd->param.dTuFac;*/
#endif
		    }
		}
	    }
	}
    }

void pkdStarForm(PKD pkd, double dRateCoeff, double dTMax, double dDenMin,
		 double dDelta, double dTime,
		 double dInitStarMass, double dESNPerStarMass, double dtCoolingShutoff,
		 double dtFeedbackDelay,  double dMassLossPerStarMass,    
		 double dZMassPerStarMass, double dMinGasMass,
		 int bdivv,
		 int *nFormed, /* number of stars formed */
		 double *dMassFormed,	/* mass of stars formed */
		 int *nDeleted) /* gas particles deleted */ {

    PARTICLE *p;
#ifdef COOLING
    COOLPARTICLE cp;
#endif
    SPHFIELDS *sph;
    double T, E, dmstar, dt, prob;
    PARTICLE *starp;
    int i;
    
    assert(pkd->oStar);
    assert(pkd->oSph);
    assert(pkd->oMass);

    *nFormed = 0;
    *nDeleted = 0;
    *dMassFormed = 0.0;
    starp = (PARTICLE *) malloc(pkdParticleSize(pkd));
    assert(starp != NULL);

    printf("pkdSF calc dTime %g\n",dTime);
    for (i=0;i<pkdLocal(pkd);++i) {
	p = pkdParticle(pkd,i);
	
	if (pkdIsActive(pkd,p) && pkdIsGas(pkd,p)) {
	    sph = pkdSph(pkd,p);
	    dt = pkd->param.dDelta/(1<<p->uRung); /* Actual Rung */
	    pkdStar(pkd,p)->totaltime += dt;
	    if (pkdDensity(pkd,p) < dDenMin || (bdivv && sph->divv >= 0.0)) continue;
	    E = sph->uPred;
#ifdef COOLING
	    if (pkd->param.bGasCooling) 
		CoolTempFromEnergyCode( pkd->Cool, &cp, &E, &T, p->fDensity, sph->fMetals );
	    else T=E/pkd->param.dTuFac;
#else
	    T=E/pkd->param.dTuFac;
#endif
	    if (T > dTMax) continue;
	    
            /* Note: Ramses allows for multiple stars per step -- but we have many particles
	      and he has one cell that may contain many times m_particle */
	    if (pkd->param.bGasCooling) {
		if (fabs(pkdStar(pkd,p)->totaltime-dTime) > 1e-3*dt) {
		    printf("total time error: %lu,  %g %g %g\n",p->iOrder,pkdStar(pkd,p)->totaltime,dTime,dt);
		    assert(0);
		    }
		}

	    dmstar = dRateCoeff*sqrt(pkdDensity(pkd,p))*pkdMass(pkd,p)*dt;
	    prob = 1.0 - exp(-dmstar/dInitStarMass); 
	    
	    /* Star formation event? */
	    if (rand()<RAND_MAX*prob) {
		float *starpMass = pkdField(starp,pkd->oMass);
		float *pMass = pkdField(p,pkd->oMass);
		pkdCopyParticle(pkd, starp, p);	/* grab copy */
		*pMass -= dInitStarMass;
		*starpMass = dInitStarMass;
/*		pkdStar(pkd,starp)->iGasOrder = p->iOrder;*/
		if (*pMass < 0) {
		    *starpMass += *pMass;
		    *pMass = 0;
		    }
	        if (*pMass < dMinGasMass) {
		    pkdDeleteParticle(pkd, p);
		    (*nDeleted)++;
		    }

                /* Time formed  
		   -- in principle it could have formed any time between dTime-dt and dTime 
		   so dTime-0.5*dt is good -- just check it's less that dtFB */
		if (dt < dtFeedbackDelay) pkdStar(pkd,starp)->fTimer = dTime-dt*.5;
		else pkdStar(pkd,starp)->fTimer = dTime-0.5*dtFeedbackDelay;
		pkdSph(pkd,starp)->u = 1; /* no FB yet */
		
		pkdSetClass(pkd,pkdMass(pkd,starp),pkdSoft(pkd,starp),FIO_SPECIES_STAR,starp); /* How do I make a new particle? -- this is bad it rewrites mass and soft for particle */
		/* JW: If class doesn't exist this is very bad -- what is the soft? 
		   For now force softening to exist to get around this */
		(*nFormed)++;
		*dMassFormed += *starpMass;
		pkdNewParticle(pkd, starp);    
		}
	    }
	}

    free(starp);
}


void pkdCooling(PKD pkd, double dTime, double z, int bUpdateState, int bUpdateTable, int bIterateDt, int bIsothermal )
    {
    PARTICLE *p;
    int i;
    SPHFIELDS *sph;
#ifdef COOLING
    COOLPARTICLE cp;  /* Dummy: Not yet fully implemented */
#endif
    double E,dt,ExternalHeating;
  
    pkdClearTimer(pkd,1);
    pkdStartTimer(pkd,1);
  
    assert(pkd->oSph);

    if (bIsothermal)  {
	for (i=0;i<pkdLocal(pkd);++i) {
	    p = pkdParticle(pkd,i);
	    pkdSph(pkd,p)->uDot = 0;
	    }
	}
    else {

#ifdef COOLING
	CoolSetTime( pkd->Cool, dTime, z, bUpdateTable );
#endif	
	if (bIterateDt) { /* Iterate Cooling & dt for each particle */
	    for (i=0;i<pkdLocal(pkd);++i) {
		p = pkdParticle(pkd,i);
		if (pkdIsActive(pkd,p) && pkdIsGas(pkd,p)) {
		    if (pkdStar(pkd,p)->fTimer > dTime) continue;
		    sph = pkdSph(pkd,p);
		    ExternalHeating = sph->uDot;
		    for (;;) {
			double uDot;
			
			E = sph->u;
			dt = pkd->param.dDelta/(1<<p->uNewRung); /* Rung Guess */
#ifdef COOLING
			CoolIntegrateEnergyCode(pkd->Cool, &cp, &E, ExternalHeating, p->fDensity, sph->fMetals, p->r, dt);
#endif
			uDot = (E-sph->u)/dt; 
			if (uDot < 0) {
			    double dtNew;
			    int uNewRung;
			    dtNew = pkd->param.dEtaUDot*sph->u/fabs(uDot);
			    uNewRung = pkdDtToRung(dtNew,pkd->param.dDelta,pkd->param.iMaxRung);
			    if (uNewRung > p->uNewRung) {
				p->uNewRung = uNewRung;
				continue;
				}
			    }
			sph->uDot = uDot;
			break;
			}
		    }
		}
	    }
	else {
	    for (i=0;i<pkdLocal(pkd);++i) {
		p = pkdParticle(pkd,i);
		if (pkdIsActive(pkd,p) && pkdIsGas(pkd,p)) {
		    if (pkdStar(pkd,p)->fTimer > dTime) {
			continue;
			}
		    sph = pkdSph(pkd,p);
		    ExternalHeating = sph->uDot;
		    E = sph->u;
		    dt = pkd->param.dDelta/(1<<p->uRung); /* Actual Rung */
#ifdef COOLING
		    CoolIntegrateEnergyCode(pkd->Cool, &cp, &E, ExternalHeating, p->fDensity, sph->fMetals, p->r, dt);
#endif
		    sph->uDot = (E-sph->u)/dt; /* To let us interpolate/extrapolate uPred */
		    }
		}
	    }
	}
    pkdStopTimer(pkd,1);
    }

void pkdCorrectEnergy(PKD pkd, double dTuFac, double z, double dTime, int iDirection )
    {
    PARTICLE *p;
    SPHFIELDS *sph;
    int i;
    double T,E;
#ifdef COOLING
    COOL *cl;
    COOLPARTICLE cp; /* Dummy for now */
#endif

#ifdef COOLING
    cl = pkd->Cool;
    CoolSetTime( cl, dTime, z, 1 );
#endif
    switch(iDirection)  {
    case CORRECTENERGY_IN:
#ifdef COOLING
	for(i=0;i<pkdLocal(pkd);++i) {
	    p = pkdParticle(pkd,i);
	    if (pkdIsGas(pkd,p)) {
		sph = pkdSph(pkd,p);
		T = sph->u/dTuFac;
		CoolEnergyCodeFromTemp( cl, &cp, &E, &T, p->fDensity, sph->fMetals );
		sph->u = E;
		sph->uPred = E;
		pkdStar(pkd,p)->totaltime = dTime;
		}
	    }
#endif
	break;
	/* Careful using this -- it permanenty converts the thermal energy */
    case CORRECTENERGY_OUT: 
#ifdef COOLING
	for(i=0;i<pkdLocal(pkd);++i) {
	    p = pkdParticle(pkd,i);
	    if (pkdIsGas(pkd,p)) {
		sph = pkdSph(pkd,p);
		E = sph->u;
		CoolTempFromEnergyCode( cl, &cp, &E, &T, p->fDensity, sph->fMetals );
		sph->u = T*dTuFac;
		sph->uPred = T*dTuFac;
		}
	    }
#endif
	break;
    case CORRECTENERGY_SPECIAL:
#ifdef COOLING
	for(i=0;i<pkdLocal(pkd);++i) {
	    p = pkdParticle(pkd,i);
	    if (pkdIsGas(pkd,p)) {
		sph = pkdSph(pkd,p);
		T = sph->u/dTuFac; 
		CoolInitEnergyCode( cl, &cp, &E, &T, p->fDensity, sph->fMetals );
		sph->u = E;
		sph->uPred = E;
		}
	    }
#endif
	break;
    default:
	assert(0);
	break;
	}
    }
    
void pkdDensityStep(PKD pkd, uint8_t uRungLo, uint8_t uRungHi, double dEta, double dRhoFac) {
    PARTICLE *p;
    int i;
    double dT;

    for (i=0;i<pkdLocal(pkd);++i) {
	p = pkdParticle(pkd,i);
	if (pkdIsActive(pkd,p)) {
	    dT = dEta/sqrt(pkdDensity(pkd,p)*dRhoFac);
	    p->uNewRung = pkdDtToRung(dT,pkd->param.dDelta,pkd->param.iMaxRung);
	    }
	}
    }

uint8_t pkdDtToRung(double dT, double dDelta, uint8_t uMaxRung) {
    double dRung;
    assert(dT>0.0);
    dRung = log(dDelta/dT) / M_LN2;
    dRung = (dRung > 0)?dRung:0;
    if (dRung > (double)uMaxRung) return(uMaxRung);
    else return((uint8_t)floor(dRung));
    }


void pkdUpdateRungByTree(PKD pkd,int iRoot,uint8_t uMinRung,int iMaxRung,
uint64_t *nRungCount) {
    KDN *c = pkdTreeNode(pkd,iRoot);
    int i;
    for (i=0;i<=iMaxRung;++i) nRungCount[i] = 0;
    for (i=c->pLower; i<=c->pUpper; ++i) {
	PARTICLE *p = pkdParticle(pkd,i);
	if ( p->uNewRung > iMaxRung ) p->uNewRung = iMaxRung;
	else if (p->uNewRung < uMinRung) p->uNewRung = uMinRung;
	if ( p->uNewRung > p->uRung ) ++p->uRung;
	else if ( p->uNewRung < p->uRung ) --p->uRung;
	nRungCount[p->uRung] += 1;
	}
    }


int pkdUpdateRung(PKD pkd,uint8_t uRungLo,uint8_t uRungHi,
		  uint8_t uRung,int iMaxRung,uint64_t *nRungCount) {
    PARTICLE *p;
    int i;
    int iTempRung;
    for (i=0;i<iMaxRung;++i) nRungCount[i] = 0;
    for (i=0;i<pkdLocal(pkd);++i) {
	p = pkdParticle(pkd,i);
	if ( pkdIsActive(pkd,p) ) {
	    if ( p->uNewRung > iMaxRung ) p->uNewRung = iMaxRung;
	    if ( p->uNewRung >= uRung ) p->uRung = p->uNewRung;
	    else if ( p->uRung > uRung) p->uRung = uRung;
	    }
	/*
	** Now produce a count of particles in rungs.
	*/
	nRungCount[p->uRung] += 1;
	}
    iTempRung = iMaxRung;
    while (nRungCount[iTempRung] == 0 && iTempRung > 0) --iTempRung;
    return iTempRung;
    }

void pkdDeleteParticle(PKD pkd, PARTICLE *p) {
    /* p->iOrder = -2 - p->iOrder; JW: Not needed -- just preserve iOrder */
    pkdSetClass(pkd,pkdMass(pkd,p),pkdSoft(pkd,p),FIO_SPECIES_LAST,p); /* Special "DELETED" class == FIO_SPECIES_LAST */
    }

void pkdNewParticle(PKD pkd, PARTICLE *p) {
    PARTICLE *newp;

    mdlassert(pkd->mdl,pkd->nLocal < pkd->nStore);
    newp = pkdParticle(pkd,pkd->nLocal);
    pkdCopyParticle(pkd,newp,p);
    newp->iOrder = IORDERMAX;
    pkd->nLocal++;
    }

void pkdColNParts(PKD pkd, int *pnNew, int *nDeltaGas, int *nDeltaDark,
		  int *nDeltaStar) {
    int pi, pj;
    int nNew;
    int ndGas;
    int ndDark;
    int ndStar;
    int newnLocal;
    PARTICLE *p;

    nNew = 0;
    ndGas = 0;
    ndDark = 0;
    ndStar = 0;
    newnLocal = pkdLocal(pkd);
    for (pi = 0, pj = 0; pi < pkdLocal(pkd); pi++) {
	p = pkdParticle(pkd,pi);
	if (pj < pi)
	    pkdCopyParticle(pkd,pkdParticle(pkd,pj),p);
	if (pkdIsNew(pkd,p)) {
	    ++pj;
	    ++nNew;
	    if (pkdIsGas(pkd, p))
		++ndGas;
	    else if (pkdIsDark(pkd, p))
		++ndDark;
	    else if (pkdIsStar(pkd, p))
		++ndStar;
	    else
		mdlassert(pkd->mdl,0);
	    if (pkdIsActive(pkd,p))
		++pkd->nActive;
	    continue;
	    }
	else if (pkdIsDeleted(pkd,p)) {
	    --newnLocal; /* no idea about type now -- type info lost */
	    --ndGas; /* JW: Hack: assume only gas deleted fix this! */
/*	    if (pkdIsGas(pkd, p))
		--ndGas;
	    else if (pkdIsDark(pkd, p))
		--ndDark;
	    else if (pkdIsStar(pkd, p))
		--ndStar;
	    else
	    mdlassert(pkd->mdl,0);*/
	    if (pkdIsActive(pkd,p))
		--pkd->nActive;
	    }
	else {
	    ++pj;
	    }
	}

    *pnNew = nNew;
    *nDeltaGas = ndGas;
    *nDeltaDark = ndDark;
    *nDeltaStar = ndStar;
    pkd->nLocal = newnLocal;
    }

void pkdNewOrder(PKD pkd,int nStart) {
    PARTICLE *p;
    int pi;

    for (pi=0;pi<pkdLocal(pkd);pi++) {
	p = pkdParticle(pkd,pi);
	if (p->iOrder == IORDERMAX) {
	    p->iOrder = nStart++;
	    }
	}
    }

void
pkdGetNParts(PKD pkd, struct outGetNParts *out )
{
    int pi;
    int n;
    int nGas;
    int nDark;
    int nStar;
    total_t iMaxOrder;
    total_t iOrder;
    PARTICLE *p;
    
    n = 0;
    nGas = 0;
    nDark = 0;
    nStar = 0;
    iMaxOrder = 0;
    for(pi = 0; pi < pkdLocal(pkd); pi++) {
	p = pkdParticle(pkd,pi);
	iOrder = p->iOrder;
	if (iOrder>iMaxOrder) iMaxOrder = iOrder;
	n++;
	if(pkdIsGas(pkd, p)) {
	    ++nGas;
	    }
	else if(pkdIsDark(pkd, p)) {
	    ++nDark;
	    }
	else if(pkdIsStar(pkd, p)) {
	    ++nStar;
	    }
	}
    
    out->n  = n;
    out->nGas = nGas;
    out->nDark = nDark;
    out->nStar = nStar;
    out->nMaxOrder = iMaxOrder;
}


void pkdSetNParts(PKD pkd,int nGas,int nDark,int nStar) {
    pkd->nGas = nGas;
    pkd->nDark = nDark;
    pkd->nStar = nStar;
    }


void pkdSetRungVeryActive(PKD pkd, int iRung) {
    /* Remember, the first very active particle is at iRungVeryActive + 1 */
    pkd->uRungVeryActive = iRung;
    }

int pkdIsGas(PKD pkd,PARTICLE *p) {
    return pkdSpecies(pkd,p) == FIO_SPECIES_SPH;
    }

int pkdIsDark(PKD pkd,PARTICLE *p) {
    return pkdSpecies(pkd,p) == FIO_SPECIES_DARK;
    }

int pkdIsStar(PKD pkd,PARTICLE *p) {
    return pkdSpecies(pkd,p) == FIO_SPECIES_STAR;
    }

void pkdInitRelaxation(PKD pkd) {
    PARTICLE *p;
    double *pRelax;
    int i;

    assert(pkd->oRelaxation);
    for (i=0;i<pkdLocal(pkd);++i) {
	p = pkdParticle(pkd,i);
	pRelax = pkdField(p,pkd->oRelaxation);
	*pRelax = 0.0;
	}
    }

double pkdTotalMass(PKD pkd) {
    PARTICLE *p;
    double m;
    int i,n;

    m = 0.0;
    n = pkdLocal(pkd);
    for( i=0; i<n; i++ ) {
	p = pkdParticle(pkd,i);
	if ( !pkdIsSrcActive(p,0,MAX_RUNG) ) continue;
	m += pkdMass(pkd,p);
	}
    return m;
    }

/*
** This function checks the predicate and returns a new value based on the flags.
** setIfTrue:    >0 -> return true if the predicate is true
**               <0 -> "clear if true"
** clearIfFalse: >0 -> return false if the predicate is false
**               <0 -> "set if false"
** A value of zero for either results in no action for the "IfTrue" or "IfFalse" flags.
** Conflicting options (e.g., setIfTrue and setIfFalse) result in a toggle.
*/
static inline int isSelected( int predicate, int setIfTrue, int clearIfFalse, int value ) {
    int s = (predicate&(setIfTrue>0)) | (~predicate&(clearIfFalse<0));
    int c = (predicate&(setIfTrue<0)) | (~predicate&(clearIfFalse>0));
    return (~s&~c&value) | (s&~(c&value));
    }

int pkdSelSrcAll(PKD pkd) {
    int i;
    int n=pkdLocal(pkd);
    for( i=0; i<n; i++ ) pkdParticle(pkd,i)->bSrcActive = 1;
    return n;
    }
int pkdSelDstAll(PKD pkd) {
    int i;
    int n=pkdLocal(pkd);
    for( i=0; i<n; i++ ) pkdParticle(pkd,i)->bDstActive = 1;
    return n;
    }

int pkdSelSrcGas(PKD pkd) {
    int i;
    int n=pkdLocal(pkd);
    PARTICLE *p;
    for( i=0; i<n; i++ ) {
	p=pkdParticle(pkd,i);
	if (pkdIsGas(pkd,p)) p->bSrcActive = 1; else p->bSrcActive = 0;
	}
    return n;
    }

int pkdSelDstGas(PKD pkd) {
    int i;
    int n=pkdLocal(pkd);
    PARTICLE *p;
    for( i=0; i<n; i++ ) {
	p=pkdParticle(pkd,i);
	if (pkdIsGas(pkd,p)) p->bDstActive = 1; else p->bDstActive = 0;
	}
    return n;
    }

int pkdSelSrcStar(PKD pkd) {
    int i;
    int n=pkdLocal(pkd);
    PARTICLE *p;
    for( i=0; i<n; i++ ) {
	p=pkdParticle(pkd,i);
	if (pkdIsStar(pkd,p)) p->bSrcActive = 1; else p->bSrcActive = 0;
	}
    return n;
    }

int pkdSelDstStar(PKD pkd, int bFB, double dTimeFB) {
    int i;
    int n=pkdLocal(pkd);
    PARTICLE *p;

    if (bFB) {
	for( i=0; i<n; i++ ) {
	    p=pkdParticle(pkd,i);
	    if (pkdIsStar(pkd,p) && pkdIsActive(pkd,p)) {
		double dtp = pkd->param.dDelta/(1<<p->uRung);
		if (dTimeFB-dtp < *pkd_Timer(pkd,p)) p->bDstActive = 1; 
		else p->bDstActive = 0;
		}
	    else p->bDstActive = 0;
	    }
	}
    else {
	for( i=0; i<n; i++ ) {
	    p=pkdParticle(pkd,i);
	    if (pkdIsStar(pkd,p)) p->bDstActive = 1; else p->bDstActive = 0;
	    }
	}
    return n;
    }

int pkdSelSrcDeleted(PKD pkd) {
    int i;
    int n=pkdLocal(pkd);
    PARTICLE *p;
    for( i=0; i<n; i++ ) {
	p=pkdParticle(pkd,i);
	if (pkdIsDeleted(pkd,p)) p->bSrcActive = 1; else p->bSrcActive = 0;
	}
    return n;
    }

int pkdSelDstDeleted(PKD pkd) {
    int i;
    int n=pkdLocal(pkd);
    PARTICLE *p;
    for( i=0; i<n; i++ ) {
	p=pkdParticle(pkd,i);
	if (pkdIsDeleted(pkd,p)) p->bDstActive = 1; else p->bDstActive = 0;
	}
    return n;
    }

int pkdSelSrcMass(PKD pkd,double dMinMass, double dMaxMass, int setIfTrue, int clearIfFalse ) {
    PARTICLE *p;
    double m;
    int i,n,nSelected;

    n = pkdLocal(pkd);
    nSelected = 0;
    for( i=0; i<n; i++ ) {
	p = pkdParticle(pkd,i);
	m = pkdMass(pkd,p);
	p->bSrcActive = isSelected((m >= dMinMass && m <=dMaxMass),setIfTrue,clearIfFalse,p->bSrcActive);
	if ( p->bSrcActive ) nSelected++;
	}
    return nSelected;
    }

int pkdSelDstMass(PKD pkd,double dMinMass, double dMaxMass, int setIfTrue, int clearIfFalse ) {
    PARTICLE *p;
    double m;
    int i,n,nSelected;

    n = pkdLocal(pkd);
    nSelected = 0;
    for( i=0; i<n; i++ ) {
	p = pkdParticle(pkd,i);
	m = pkdMass(pkd,p);
	p->bDstActive = isSelected((m >= dMinMass && m <=dMaxMass),setIfTrue,clearIfFalse,p->bDstActive);
	if ( p->bDstActive ) nSelected++;
	}
    return nSelected;
    }

int pkdSelSrcById(PKD pkd,uint64_t idStart, uint64_t idEnd, int setIfTrue, int clearIfFalse ) {
    PARTICLE *p;
    int i,n,nSelected;

    n = pkdLocal(pkd);
    nSelected = 0;
    for( i=0; i<n; i++ ) {
	p = pkdParticle(pkd,i);
	p->bSrcActive = isSelected((p->iOrder >= idStart && p->iOrder <= idEnd),setIfTrue,clearIfFalse,p->bSrcActive);
	if ( p->bSrcActive ) nSelected++;
	}
    return nSelected;
    }

int pkdSelDstById(PKD pkd,uint64_t idStart, uint64_t idEnd, int setIfTrue, int clearIfFalse ) {
    PARTICLE *p;
    int i,n,nSelected;

    n = pkdLocal(pkd);
    nSelected = 0;
    for( i=0; i<n; i++ ) {
	p = pkdParticle(pkd,i);
	p->bDstActive = isSelected((p->iOrder >= idStart && p->iOrder <= idEnd),setIfTrue,clearIfFalse,p->bDstActive);
	if ( p->bDstActive ) nSelected++;
	}
    return nSelected;
    }


int pkdSelSrcPhaseDensity(PKD pkd,double dMinDensity, double dMaxDensity, int setIfTrue, int clearIfFalse ) {
    PARTICLE *p;
    VELSMOOTH *pvel;
    float density;
    int i,n,nSelected;

    n = pkdLocal(pkd);
    nSelected = 0;
    for( i=0; i<n; i++ ) {
	p = pkdParticle(pkd,i);
	pvel = pkdField(p,pkd->oVelSmooth);
	density = pkdDensity(pkd,p) * pow(pvel->veldisp2,-1.5);
	p->bSrcActive = isSelected((density >= dMinDensity && density <=dMaxDensity),setIfTrue,clearIfFalse,p->bSrcActive);
	if ( p->bSrcActive ) nSelected++;
	}
    return nSelected;
    }

int pkdSelDstPhaseDensity(PKD pkd,double dMinDensity, double dMaxDensity, int setIfTrue, int clearIfFalse ) {
    PARTICLE *p;
    VELSMOOTH *pvel;
    float density;
    int i,n,nSelected;

    n = pkdLocal(pkd);
    nSelected = 0;
    for( i=0; i<n; i++ ) {
	p = pkdParticle(pkd,i);
	pvel = pkdField(p,pkd->oVelSmooth);
	density = pkdDensity(pkd,p) * pow(pvel->veldisp2,-1.5);
	p->bDstActive = isSelected((density >= dMinDensity && density <=dMaxDensity),setIfTrue,clearIfFalse,p->bDstActive);
	if ( p->bDstActive ) nSelected++;
	}
    return nSelected;
    }

int pkdSelSrcBox(PKD pkd,double *dCenter, double *dSize, int setIfTrue, int clearIfFalse ) {
    PARTICLE *p;
    int i,j,n,nSelected;
    int predicate;

    n = pkdLocal(pkd);
    nSelected = 0;
    for( i=0; i<n; i++ ) {
	p = pkdParticle(pkd,i);
	predicate = 1;
	for(j=0; j<3; j++ ) {
	    double dx = dCenter[j] - pkdPos(pkd,p,j);
	    predicate = predicate && dx < dSize[j] && dx >= -dSize[j];
	    }
	p->bSrcActive = isSelected(predicate,setIfTrue,clearIfFalse,p->bSrcActive);
	if ( p->bSrcActive ) nSelected++;
	}
    return nSelected;
    }

int pkdSelDstBox(PKD pkd,double *dCenter, double *dSize, int setIfTrue, int clearIfFalse ) {
    PARTICLE *p;
    int i,j,n,nSelected;
    int predicate;

    n = pkdLocal(pkd);
    nSelected = 0;
    for( i=0; i<n; i++ ) {
	p = pkdParticle(pkd,i);
	predicate = 1;
	for(j=0; j<3; j++ )
	    predicate = predicate && fabs(dCenter[j] - pkdPos(pkd,p,j)) <= dSize[j];
	p->bDstActive = isSelected(predicate,setIfTrue,clearIfFalse,p->bDstActive);
	if ( p->bDstActive ) nSelected++;
	}
    return nSelected;
    }

int pkdSelSrcSphere(PKD pkd,double *r, double dRadius, int setIfTrue, int clearIfFalse ) {
    PARTICLE *p;
    double d2,dx,dy,dz,dRadius2;
    int i,n,nSelected;

    n = pkdLocal(pkd);
    nSelected = 0;
    dRadius2 = dRadius*dRadius;
    for( i=0; i<n; i++ ) {
	p = pkdParticle(pkd,i);
	dx = r[0] - pkdPos(pkd,p,0);
	dy = r[1] - pkdPos(pkd,p,1);
	dz = r[2] - pkdPos(pkd,p,2);

	d2 = dx*dx + dy*dy + dz*dz;
	p->bSrcActive = isSelected((d2<=dRadius2),setIfTrue,clearIfFalse,p->bSrcActive);
	if ( p->bSrcActive ) nSelected++;
	}
    return nSelected;
    }

int pkdSelDstSphere(PKD pkd,double *r, double dRadius, int setIfTrue, int clearIfFalse ) {
    PARTICLE *p;
    double d2,dx,dy,dz,dRadius2;
    int i,n,nSelected;

    n = pkdLocal(pkd);
    nSelected = 0;
    dRadius2 = dRadius*dRadius;
    for( i=0; i<n; i++ ) {
	p = pkdParticle(pkd,i);
	dx = r[0] - pkdPos(pkd,p,0);
	dy = r[1] - pkdPos(pkd,p,1);
	dz = r[2] - pkdPos(pkd,p,2);

	d2 = dx*dx + dy*dy + dz*dz;
	p->bDstActive = isSelected((d2<=dRadius2),setIfTrue,clearIfFalse,p->bSrcActive);
	if ( p->bDstActive ) nSelected++;
	}
    return nSelected;
    }

/*
** Select all particles that fall inside a cylinder between two points P1 and P2
** with radius dRadius.
*/
int pkdSelSrcCylinder(PKD pkd,double *dP1, double *dP2, double dRadius,
		      int setIfTrue, int clearIfFalse ) {
    PARTICLE *p;
    double dCyl[3], dPart[3];
    double dLength2, dRadius2, dL2;
    double pdotr;
    int i,j,n,nSelected;
    int predicate;

    dRadius2 = dRadius*dRadius;
    dLength2 = 0.0;
    for( j=0;j<3;j++ ) {
	dCyl[j] = dP2[j] - dP1[j];
	dLength2 += dCyl[j] * dCyl[j];
	}
    n = pkdLocal(pkd);
    nSelected = 0;
    for( i=0; i<n; i++ ) {
	p = pkdParticle(pkd,i);
	pdotr = 0.0;
	for( j=0;j<3;j++ ) {
	    dPart[j] = pkdPos(pkd,p,j) - dP1[j];
	    pdotr += dPart[j] * dCyl[j];
	    }

	if ( pdotr < 0.0 || pdotr > dLength2 ) predicate = 0;
	else {
	    dL2 = dPart[0]*dPart[0] + dPart[1]*dPart[1] + dPart[2]*dPart[2] - pdotr*pdotr/dLength2;
	    predicate = (dL2 <= dRadius2);
	    }
	p->bSrcActive = isSelected(predicate,setIfTrue,clearIfFalse,p->bSrcActive);
	if ( p->bSrcActive ) nSelected++;
	}
    return nSelected;
    }

/*
** Select all particles that fall inside a cylinder between two points P1 and P2
** with radius dRadius.
*/
int pkdSelDstCylinder(PKD pkd,double *dP1, double *dP2, double dRadius,
		      int setIfTrue, int clearIfFalse ) {
    PARTICLE *p;
    double dCyl[3], dPart[3];
    double dLength2, dRadius2, dL2;
    double pdotr;
    int i,j,n,nSelected;
    int predicate;

    dRadius2 = dRadius*dRadius;
    dLength2 = 0.0;
    for( j=0;j<3;j++ ) {
	dCyl[j] = dP2[j] - dP1[j];
	dLength2 += dCyl[j] * dCyl[j];
	}

    n = pkdLocal(pkd);
    nSelected = 0;
    for( i=0; i<n; i++ ) {
	p = pkdParticle(pkd,i);
	pdotr = 0.0;
	for( j=0;j<3;j++ ) {
	    dPart[j] = pkdPos(pkd,p,j) - dP1[j];
	    pdotr += dPart[j] * dCyl[j];
	    }

	if ( pdotr < 0.0 || pdotr > dLength2 ) predicate = 0;
	else {
	    dL2 = dPart[0]*dPart[0] + dPart[1]*dPart[1] + dPart[2]*dPart[2] - pdotr*pdotr/dLength2;
	    predicate = (dL2 <= dRadius2);
	    }
	p->bDstActive = isSelected(predicate,setIfTrue,clearIfFalse,p->bSrcActive);
	if ( p->bDstActive ) nSelected++;
	}
    return nSelected;
    }

int pkdSelSrcGroup(PKD pkd, int iGroup) {
    int i;
    int n=pkdLocal(pkd);
    PARTICLE *p;
    for( i=0; i<n; i++ ) {
	p=pkdParticle(pkd,i);
	p->bSrcActive = *pkdGroup(pkd,p)==iGroup;
	}
    return n;
    }

int pkdSelDstGroup(PKD pkd, int iGroup) {
    int i;
    int n=pkdLocal(pkd);
    PARTICLE *p;
    for( i=0; i<n; i++ ) {
	p=pkdParticle(pkd,i);
	p->bDstActive = *pkdGroup(pkd,p)==iGroup;
	}
    return n;
    }

/*
**  Find the source particle with the deepest potential
*/
int pkdDeepestPot(PKD pkd, uint8_t uRungLo, uint8_t uRungHi,
    double *r, float *fPot) {
    int i,n,nChecked;
    PARTICLE *p, *pLocal;
    float *pPot, *pPotLocal;

    assert(pkd->oPotential);

    n = pkdLocal(pkd);
    pLocal = pkdParticle(pkd,0);
    pPotLocal = pkdPot(pkd,pLocal);
    nChecked = 0;
    for (i=0;i<n;++i) {
	p = pkdParticle(pkd,i);
	if (pkdIsSrcActive(p,uRungLo,uRungHi)) {
	    nChecked++;
	    pPot = pkdPot(pkd,p);
	    if ( *pPot < *pPotLocal ) {
		pLocal = p;
		pPotLocal = pkdPot(pkd,pLocal);
		}
	    }
	}
    r[0] = pkdPos(pkd,pLocal,0);
    r[1] = pkdPos(pkd,pLocal,1);
    r[2] = pkdPos(pkd,pLocal,2);
    *fPot= *pPotLocal;
    return nChecked;
    }

void pkdOutPsGroup(PKD pkd,char *pszFileName,int iType)
{
    FILE *fp;
    int i,j,nout,lStart;

    if (iType == OUT_PSGROUP_STATS) {
	fp = fopen(pszFileName,"a+");
	assert(fp != NULL);
	struct psGroup *gd = pkd->psGroupTable.pGroup;

	for (i=1;i<pkd->psGroupTable.nGroups;++i)
	{
	    if (gd[i].iPid != pkd->idSelf) continue;
	    fprintf(fp,"%d",gd[i].iGlobalId);
	    fprintf(fp," %10llu",gd[i].nTotal);
	    fprintf(fp," %12.8e",gd[i].fMass);
	    fprintf(fp," %12.8e",gd[i].fRMSRadius);
	    fprintf(fp," %12.8e",gd[i].r[0]);
	    fprintf(fp," %12.8e",gd[i].r[1]);
	    fprintf(fp," %12.8e",gd[i].r[2]);
	    fprintf(fp," %12.8e",gd[i].v[0]);
	    fprintf(fp," %12.8e",gd[i].v[1]);
	    fprintf(fp," %12.8e",gd[i].v[2]);
#if 0
	    fprintf(fp,"%.11g ",pkd->groupData[i].rcom[0]);
	    fprintf(fp,"%.11g ",pkd->groupData[i].rcom[1]);
	    fprintf(fp,"%.11g ",pkd->groupData[i].rcom[2]);
	    fprintf(fp,"%.11g ",pkd->groupData[i].r[0]);
	    fprintf(fp,"%.11g ",pkd->groupData[i].r[1]);
	    fprintf(fp,"%.11g ",pkd->groupData[i].r[2]);
	    fprintf(fp,"%.8g ",dvFac*pkd->groupData[i].v[0]);
	    fprintf(fp,"%.8g ",dvFac*pkd->groupData[i].v[1]);
	    fprintf(fp,"%.8g ",dvFac*pkd->groupData[i].v[2]);
#endif
	    fprintf(fp,"\n");
	}
	if (fclose(fp) == EOF)
	{
	    perror("pkdOutGroup: could not close file");
	    exit(1);
	}
    }
    else
	assert(0);
}

