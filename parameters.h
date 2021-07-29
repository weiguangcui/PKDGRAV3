/*  This file is part of PKDGRAV3 (http://www.pkdgrav.org/).
 *  Copyright (c) 2001-2018 Joachim Stadel & Douglas Potter
 *
 *  PKDGRAV3 is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  PKDGRAV3 is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with PKDGRAV3.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PARAMETERS_HINCLUDED
#define PARAMETERS_HINCLUDED

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
    int iFofInterval;
    int iCheckInterval;
    int iLogInterval;
    int iPkInterval;
    int iOrder;
    int bEwald;
    int iEwOrder;
    int nReplicas;
    int iStartStep;
    int nSteps;
    int nSteps10;
    int nSmooth;
    int iMaxRung;
    int nRungVeryActive;
    int nPartVeryActive;
    int bDualTree;
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
    int bLightCone;
    int bLightConeParticles;
    int bInFileLC;
    double dRedshiftLCP;
    int nSideHealpix;
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
    int bNewKDK;
    int nDigits;
#define GET_PARTICLES_MAX 20 /* We have a nested loop, so don't increase this */
    int nOutputParticles;
    uint64_t iOutputParticles[GET_PARTICLES_MAX];
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
    double dErgUnit;
    double dSecUnit;
    double dKmPerSecUnit;
    double dhMinOverSoft;
    double dMetalDiffusionCoeff;
    double dThermalDiffusionCoeff;
    /* StarForm and Feedback */
    //IA: We keep most of them for compatibility reasons I guess? But should be cleaned! TODO
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
    double dMaxPhysicalSoft;
    double dDelta;
    double dEwCut;
    double dEwhCut;
    double dTheta;
    double dTheta2;
    double dTheta20;
    double dPeriod;
    double dxPeriod;
    double dyPeriod;
    double dzPeriod;
    double dPreFacRhoLoc;
    double dFacExcludePart;
    double dEccFacMax;
    double dRedTo;
    double dRedFrom;
    double dCentMass;
    char achInFile[256];
    char achOutName[256];
    char achOutPath[256];
    char achIoPath[256];
    char achCheckpointPath[256];
    char achDataSubPath[256];
    char achOutTypes[256];
    char achCheckTypes[256];
#ifdef USE_PYTHON
    char achScriptFile[256];
#endif
    char achTfFile[256];
    char achClassFilename[256];
    char achLinearSpecies[256];
    char achPowerSpecies[256];
    double dGrowDeltaM;
    double dGrowStartT;
    double dGrowEndT;
    double dFracDualTree;
    double dFracNoDomainDecomp;
    double dFracNoDomainRootFind;
    double dFracNoDomainDimChoice;
    /*
    ** Additional parameters for group finding.
    */
    int	bFindGroups;
    int bFindHopGroups;
    int	nMinMembers;
    double dHopTau;
    double dTau;
    int	nBins;
    int	iCenterType;
    double binFactor;
    double fMinRadius;
    int bLogBins;
    int	bTraceRelaxation;
    /*
    ** Parameters for group stats.
    */
    double dEnvironment0;
    double dEnvironment1;
    /* IC Generation */
    int bWriteIC;
    double h;
    double dBoxSize;
    int nGrid;
    int iSeed;
    int bFixedAmpIC;
    double dFixedAmpPhasePI;
    int b2LPT;
    int bICgas;

    /*
     * IA: Parameters for the meshless hydrodynamics
     */
    double dCFLacc;
    int bMeshlessHydro;
    int bFirstHydroLoop;
    int bConservativeReSmooth;
    int bIterativeSmoothingLength;
    int bWakeUpParticles;
    double dNeighborsStd0;
    double dNeighborsStd;

    /* 
     * IA: Parameter for fixing all particles to the same rung
     */
    int bGlobalDt;
    double dFixedDelta;
    double dMinDt;

#ifdef COOLING
    /*
     * IA: Cooling parameters
     */
    char strCoolingTables[256];
    double fH_reion_z;
    double fH_reion_eV_p_H;
    double fHe_reion_z_centre;
    double fHe_reion_z_sigma;
    double fHe_reion_eV_p_H;
    double fCa_over_Si_in_Solar;
    double fS_over_Si_in_Solar;
    double fT_CMB_0;

    /*
     * IA: Internal energy floor parameters
     */
    double dJeansFloorIndex;
    double dJeansFloorDen;
    double dJeansFlooru;
    double dCoolingFloorDen;
    double dCoolingFlooru;

    /*
     * IA: Initial abundances 
     */
    double dInitialHe;
    double dInitialC;
    double dInitialN;
    double dInitialO;
    double dInitialNe;
    double dInitialMg;
    double dInitialSi;
    double dInitialFe;
#endif
    double dInitialH;
#ifdef STAR_FORMATION
    /* IA: Star formation */
    double dSFMinOverDensity; 
    double dSFGasFraction;
    double dSFThresholdDen;
    double dSFThresholdu;
    double dSFindexKS;
    double dSFnormalizationKS;
#endif
#ifdef FEEDBACK
    double dFeedbackDelay;
    double dFeedbackEfficiency;
    double dFeedbackDu;
    double dNumberSNIIperMass;
#endif 
#ifdef BLACKHOLES
    int bMerger;
    int bPlaceBHSeed;
    int bAccretion;
    int bBHFeedback;
    double dAccretionAlpha;
    double dEddingtonFactor;
    double dBHRadiativeEff;
    double dBHFeedbackEff;
    double dBHFeedbackEcrit;
    double dBHSeedMass;
    double dMhaloMin;
#endif

#ifdef MDL_FFTW
    int nGridPk;
    int bPkInterlace;
    int iPkOrder;
    int nBinsPk;
    int nGridLin;
    int bDoLinPkOutput;
    int nBinsLinPk;
    int iDeltakInterval;
  double dDeltakRedshift;
#endif

    int iInflateStep;
    int nInflateReps;

    /*
    ** Memory models.  Other parameters can force these to be set.
    */
    int bMemIntegerPosition;
    int bMemUnordered;
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
    int bMemNodeMoment;
    int bMemNodeAcceleration;
    int bMemNodeVelocity;
    int bMemNodeSphBounds;
    int bMemNodeBnd;
    int bMemNodeVBnd;
    };

#endif
