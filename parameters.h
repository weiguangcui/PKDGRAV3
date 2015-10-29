#ifndef PARAMETERS_HINCLUDED
#define PARAMETERS_HINCLUDED

#include "cosmo.h"

/*
** Don't even think about putting a pointer in here!!
*/
struct parameters {
    /*
    ** Parameters for PKDGRAV.
    */
    int nThreads;
    int bDiag;
    int bDedicatedMPI;
    int bSharedMPI;
    int bNoGrav;
    int bOverwrite;
    int bVWarnings;
    int bVStart;
    int bVStep;
    int bVRungStat;
    int bVDetails;
    int bPeriodic;
    int bRestart;
    int bParaRead;
    int bParaWrite;
    int nParaRead;
    int nParaWrite;
    int bStandard;
    int iCompress;
    int bHDF5;
    int bDoublePos;
    int bDoubleVel;
    int bCenterOfMassExpand;
    int bGravStep;
    int bEpsAccStep;
    int bAccelStep; /* true if bEpsAccStep */
    int bDensityStep;
    int iTimeStepCrit;
    int nPartRhoLoc;
    int nPartColl;
    int nTruncateRung;
    int bDoDensity;
#ifdef USE_PNG
    int nPNGResolution;
#endif
    int bDoRungOutput;
    int bDoRungDestOutput;
    int bDoGravity;
    int bAarsethStep;
    int nBucket;
    int nGroup;
    int n2min;
    int iOutInterval;
    int iCheckInterval;
    int iLogInterval;
    int iOrder;
    int bEwald;
    int iEwOrder;
    int nReplicas;
    int iStartStep;
    int nSteps;
    int nSmooth;
    int iMaxRung;
    int nRungVeryActive;
    int nPartVeryActive;
    int nTreeBitsLo;
    int nTreeBitsHi;
    int iWallRunTime;
    int iSignalSeconds;
    int bPhysicalSoft;
    int bSoftMaxMul;
    int nSoftNbr;
    int bSoftByType;
    int bDoSoftOutput;
    int bDoAccOutput;
    int bDoPotOutput;
    int iCacheSize;
    int iWorkQueueSize;
    int iCUDAQueueSize;
    int bAddDelete;
    /* BEGIN Gas Parameters */
    int bDoGas;
    int bGasAdiabatic;
    int bGasIsothermal;
    int bGasCooling;
    int bInitTFromCooling;
    int bStarForm;
    int bFeedback;
    int iViscosityLimiter;
    int iDiffusion;
    int iRungCoolTableUpdate;
    int bHSDKD;
    int bNewKDK;
    double dEtaCourant;
    double dEtaUDot;
    double dConstAlpha;
    double dConstBeta;
    double dConstGamma;
    double dMeanMolWeight;
    double dGasConst;
    double dTuFac;
    double dMsolUnit;
    double dKpcUnit;
    double ddHonHLimit;
    double dKBoltzUnit;
    double dGmPerCcUnit;
    double dComovingGmPerCcUnit;
    double dErgPerGmUnit;
    double dSecUnit;
    double dKmPerSecUnit;
    double dhMinOverSoft;
    double dMetalDiffusionCoeff;
    double dThermalDiffusionCoeff;
#ifdef COOLING
    COOLPARAM CoolParam;
#endif
    /* StarForm and Feedback */
    double SFdEfficiency;
    double SFdTMax;
    double SFdPhysDenMin;
    double SFdComovingDenMin;
    double SFdESNPerStarMass;
    double SFdtCoolingShutoff;

    double SFdtFeedbackDelay;
    double SFdMassLossPerStarMass;
    double SFdZMassPerStarMass;
    double SFdInitStarMass;
    double SFdMinGasMass;
    double SFdvFB;
    int SFbdivv;
    
    /* END Gas Parameters */
    double dEta;
    double dExtraStore;
    double dSoft;
    double dSoftMax;
    double dDelta;
    double dEwCut;
    double dEwhCut;
    double dTheta;
    double dTheta2;
    double daSwitchTheta;
    double dPeriod;
    double dxPeriod;
    double dyPeriod;
    double dzPeriod;
    double dPreFacRhoLoc;
    double dFacExcludePart;
    double dEccFacMax;
    CSM csm;
    double dRedTo;
    double dRedFrom;
    double dCentMass;
    char achDigitMask[256];
    char achInFile[256];
    char achOutName[256];
    char achOutPath[256];
    char achIoPath[256];
    char achDataSubPath[256];
    char achOutTypes[256];
    char achCheckTypes[256];
#ifdef USE_PYTHON
    char achScriptFile[256];
#endif
    char achTfFile[256];
    double dGrowDeltaM;
    double dGrowStartT;
    double dGrowEndT;
    double dFracNoDomainDecomp;
    double dFracNoDomainRootFind;
    double dFracNoDomainDimChoice;
    /*
    ** Additional parameters for group finding.
    */
    int	bFindPSGroups;
    int	bFindGroups;
    int bFindHopGroups;
    int	nMinMembers;
    double dHopTau;
    double dTau;
    double dVTau;
    int bTauAbs;
    int	nBins;
    int	iCenterType;
    double binFactor;
    double fMinRadius;
    int bLogBins;
    int	bTraceRelaxation;

    /* IC Generation */
    int bWriteIC;
    double h;
    double dBoxSize;
    int nGrid;
    int iSeed;

#ifdef MDL_FFTW
    int nGridPk;
#endif

    int nDomainRungs;
    int iDomainMethod;

    /*
    ** Memory models.  Other parameters can force these to be set.
    */
    int bMemParticleID;
    int bMemAcceleration;
    int bMemVelocity;
    int bMemPotential;
    int bMemGroups;
    int bMemMass;
    int bMemSoft;
    int bMemRelaxation;
    int bMemVelSmooth;
    int bMemPsMetric;
    int bMemNewDD ;
    int bMemNodeMoment;
    int bMemNodeAcceleration;
    int bMemNodeVelocity;
    int bMemNodeSphBounds;
    int bMemNodeBnd;
    int bMemNodeVBnd;
    };

#endif
