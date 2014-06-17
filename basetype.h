#ifndef WORK_HINCLUDED
#define WORK_HINCLUDED

#define IORDERBITS 41
#define IORDERMAX ((((uint64_t) 1)<<IORDERBITS)-1)

typedef struct particle {
    /*-----Base-Particle-Data----*/
    uint64_t iOrder     :  IORDERBITS;
    uint8_t  bMarked    :  1;
    uint8_t  uNewRung   :  6;
    uint8_t  uRung      :  6;
    uint8_t  bSrcActive :  1;
    uint8_t  bDstActive :  1;
    uint8_t  iClass     :  8;
    double r[3];
    /*-----Used-for-Smooth-------*/
    float fBall;
    float fDensity;
    /* a, fPot, v, pGroup, pBin moved to memory models */

#ifdef PLANETS
    /* (collision stuff) */
    int iOrgIdx;		/* for tracking of mergers, aggregates etc. */
    FLOAT w[3];			/* spin vector */
    int iColor;			/* handy color tag */
    int iColflag;	        /* handy collision tag 1 for c1, 2 for c2*/
    uint64_t iOrderCol;              /* iOrder of colliding oponent.*/
    FLOAT dtCol;
    /* end (collision stuff) */
#ifdef SYMBA
    FLOAT rb[3]; /* position before drift */
    FLOAT vb[3]; /* velocity before drift */
    FLOAT drmin; /* minimum distance from neighbors normalized by Hill*/
    FLOAT drmin2; /* min. dis. during drift */
    uint64_t iOrder_VA[5]; /* iOrder's of particles within 3 hill radius*/
    int   i_VA[5];    /* pointers of particles */
    int   n_VA;       /* number of particles */
    double  hill_VA[5]; /* mutual hill radius calculated in grav.c */
    double a_VA[3];          /* accralation due to close encounters */
    /* int   iKickRung; */
#endif
#endif/* PLANETS */
    } PARTICLE;

typedef struct {
    float r[3];
    float v[3];
    float a[3];
    float fMass;
    float fSoft;
    float fSmooth2;
    } PINFOIN;

typedef struct {
    float a[3];
    float fPot;
    float dirsum, normsum;
    float rhopmax;
    } PINFOOUT;


/*
** Accumulates the work for a set of particles
*/
typedef struct {
    PARTICLE **pPart;
    PINFOIN *pInfoIn;
    PINFOOUT *pInfoOut;
    float dRhoFac;
    int nP;
    int nRefs;
    void *ctx;
    int bGravStep;
#ifdef USE_CUDA
    void *cudaCtx;
    void *gpu_memory;
#endif
    } workParticle;

/*
** One tile of PP interactions
*/
typedef struct {
    PINFOOUT *pInfoOut;
    ILP ilp;
    ILPTILE tile;
    workParticle *work;
    int i;
    uint16_t nBlocks;
    uint16_t nInLast;
#ifdef USE_CUDA
    void *gpu_memory;
    int nCudaBlks;
#endif
    } workPP;

typedef struct {
    PINFOOUT *pInfoOut;
    ILC ilc;
    ILCTILE tile;
    workParticle *work;
    int i;
    uint16_t nBlocks;
    uint16_t nInLast;
#ifdef USE_CUDA
    void *gpu_memory;
#endif
    } workPC;



#endif
