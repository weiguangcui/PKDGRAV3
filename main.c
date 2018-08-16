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

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "pkd_config.h"
#endif

#ifdef ENABLE_FE
#include <fenv.h>
#endif
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include "mdl.h"
#ifdef USE_BT
#include "bt.h"
#endif
#include "master.h"
#include "outtype.h"
#include "smoothfcn.h"
#ifdef USE_PYTHON
#include "pkdpython.h"
#endif

void * main_ch(MDL mdl) {
    PST pst;
    LCL lcl;

#ifndef _MSC_VER
	/* a USR1 signal indicates that the queue wants us to exit */
    timeGlobalSignalTime = 0;
    signal(SIGUSR1,NULL);
    signal(SIGUSR1,USR1_handler);

    /* a USR2 signal indicates that we should write an output when convenient */
    bGlobalOutput = 0;
    signal(SIGUSR2,NULL);
    signal(SIGUSR2,USR2_handler);
#endif

    lcl.pkd = NULL;
    pstInitialize(&pst,mdl,&lcl);

    pstAddServices(pst,mdl);

    mdlHandler(mdl);

    pstFinish(pst);
    return NULL;
    }

/*
** This is invoked instead of main_ch for the "master" process.
*/
void * master_ch(MDL mdl) {
    int argc = mdl->base.argc;
    char **argv = mdl->base.argv;
    MSR msr;
    FILE *fpLog = NULL;
    char achFile[256];			/*DEBUG use MAXPATHLEN here (& elsewhere)? -- DCR*/
    double dTime;
    double E=0,T=0,U=0,Eth=0,L[3]={0,0,0},F[3]={0,0,0},W=0;
    double dMultiEff=0;
    long lSec=0;
    int i,iStep,iStartStep,iSec=0,iStop=0;
    uint64_t nActive;
    int bKickOpen=0;
    int bDoOpeningKick=0;
    int bDoCheckpoint=0;
    int bDoOutput=0;
    int64_t nRungs[MAX_RUNG+1];
    uint8_t uRungMax;
    double diStep;
    double ddTime;
    int bRestore;

    printf("%s\n", PACKAGE_STRING );

    bRestore = msrInitialize(&msr,mdl,argc,argv);
    msr->lStart=time(0);

    /*
    ** Establish safety lock.
    */
    if (!msrGetLock(msr)) {
	msrFinish(msr);
	return NULL;
	}

    /* a USR1 signal indicates that the queue wants us to exit */
#ifndef _MSC_VER
    timeGlobalSignalTime = 0;
    signal(SIGUSR1,NULL);
    signal(SIGUSR1,USR1_handler);

    /* a USR2 signal indicates that we should write an output when convenient */
    bGlobalOutput = 0;
    signal(SIGUSR2,NULL);
    signal(SIGUSR2,USR2_handler);
#endif

    /*
    ** Output the host names to make troubleshooting easier
    */
    msrHostname(msr);

    /* Restore from a previous checkpoint */
    if (bRestore) {
	dTime = msrRestore(msr);
	iStartStep = msr->iCheckpointStep;
	msrInitStep(msr);
	if (prmSpecified(msr->prm,"dSoft")) msrSetSoft(msr,msrSoft(msr));
	iStep = msrSteps(msr); /* 0=analysis, >1=simulate, <0=python */
	uRungMax = msr->iCurrMaxRung;
	}

    /* Generate the initial particle distribution */
    else if (prmSpecified(msr->prm,"nGrid")) {
	iStartStep = msr->param.iStartStep; /* Should be zero */
#ifdef MDL_FFTW
	dTime = msrGenerateIC(msr); /* May change nSteps/dDelta */
	if ( msr->param.bWriteIC ) {
	    msrBuildIoName(msr,achFile,0);
	    msrWrite( msr,achFile,dTime,msr->param.bWriteIC-1);
	    }
#else
	printf("To generate initial conditions, compile with FFTW\n");
	msrFinish(msr);
	return NULL;
#endif
	msrInitStep(msr);
	if (prmSpecified(msr->prm,"dSoft")) msrSetSoft(msr,msrSoft(msr));
	iStep = msrSteps(msr); /* 0=analysis, >1=simulate, <0=python */
	}

    /* Read in a binary file */
    else if ( msr->param.achInFile[0] ) {
	iStartStep = msr->param.iStartStep; /* Should be zero */
	dTime = msrRead(msr,msr->param.achInFile); /* May change nSteps/dDelta */
	msrInitStep(msr);
	if (msr->param.bAddDelete) msrGetNParts(msr);
	if (prmSpecified(msr->prm,"dRedFrom")) {
	    double aOld, aNew;
	    aOld = csmTime2Exp(msr->param.csm,dTime);
	    aNew = 1.0 / (1.0 + msr->param.dRedFrom);
	    dTime = msrAdjustTime(msr,aOld,aNew);
	    /* Seriously, we shouldn't need to send parameters *again*.
	       When we remove sending parameters, we should remove this. */
	    msrInitStep(msr);
	    }
	if (prmSpecified(msr->prm,"dSoft")) msrSetSoft(msr,msrSoft(msr));
	iStep = msrSteps(msr); /* 0=analysis, >1=simulate, <0=python */
	}
#ifdef USE_PYTHON
    else if ( msr->param.achScriptFile[0] ) {
	iStep = -1;
	}
#endif
    else {
	printf("No input file specified\n");
	msrFinish(msr);
	return NULL;
	}

    /* Adjust theta for gravity calculations. */
    if (msrComove(msr)) msrSwitchTheta(msr,dTime);

    /* Analysis mode */
    if (iStep==0) {
#ifdef MDL_FFTW
	int bDoPk = msr->param.nGridPk>0;
#else
	int bDoPk = 0;
#endif
	msrInflate(msr,0);
	if (msrDoGravity(msr) ||msrDoGas(msr) || bDoPk || msr->param.bFindGroups) {
	    msrActiveRung(msr,0,1); /* Activate all particles */
	    msrDomainDecomp(msr,0,0,0);
	    msrUpdateSoft(msr,dTime);
	    msrBuildTree(msr,dTime,msr->param.bEwald);
	    msrOutputOrbits(msr,iStartStep,dTime);
#ifdef MDL_FFTW
	    if (bDoPk) {
		msrOutputPk(msr,iStartStep,dTime);
		}
#endif
	    if (msr->param.bFindGroups) {
		msrNewFof(msr,csmTime2Exp(msr->param.csm,dTime));
		}
	    if (msrDoGravity(msr)) {
		msrGravity(msr,0,MAX_RUNG,ROOT,0,dTime,iStartStep,0,0,
		    msr->param.bEwald,msr->param.nGroup,&iSec,&nActive);
		msrMemStatus(msr);
		if (msr->param.bGravStep) {
		    msrBuildTree(msr,dTime,msr->param.bEwald);
		    msrGravity(msr,0,MAX_RUNG,ROOT,0,dTime,iStartStep,0,0,
			msr->param.bEwald,msr->param.nGroup,&iSec,&nActive);
		    msrMemStatus(msr);
		    }
		}
	    if (msr->param.bFindGroups) {
		msrGroupStats(msr);
		}
		
	    if (msrDoGas(msr)) {
		/* Initialize SPH, Cooling and SF/FB and gas time step */
		msrCoolSetup(msr,dTime);
		/* Fix dTuFac conversion of T in InitSPH */
		msrInitSph(msr,dTime);
		}
		   
	    msrUpdateRung(msr,0); /* set rungs for output */
	    }
	msrOutput(msr,0,dTime,0);  /* JW: Will trash gas density */
	}

#ifdef USE_PYTHON
    else if ( iStep < 0 ) {
	PPY ppy;
	ppyInitialize(&ppy,msr,dTime);
	msr->prm->script_argv[0] = msr->param.achScriptFile;
	ppyRunScript(ppy,msr->prm->script_argc,msr->prm->script_argv);
	ppyFinish(ppy);
	}
#endif

    /* Simulation mode */
    else {
	/*
	** Now we have all the parameters for the simulation we can make a
	** log file entry.
	*/
	if (msrLogInterval(msr)) {
	    sprintf(achFile,"%s.log",msrOutName(msr));
	    fpLog = fopen(achFile,"a");
	    assert(fpLog != NULL);
	    setbuf(fpLog,(char *) NULL); /* no buffering */
	    /*
	    ** Include a comment at the start of the log file showing the
	    ** command line options.
	    */
	    fprintf(fpLog,"# ");
	    for (i=0;i<argc;++i) fprintf(fpLog,"%s ",argv[i]);
	    fprintf(fpLog,"\n");
	    msrLogParams(msr,fpLog);
	    }

	if (msr->param.bLightCone && msrComove(msr)) {
	    printf("LightCone output will begin at z=%.10g\n",
		1.0/csmComoveLookbackTime2Exp(msr->param.csm,1.0 / dLightSpeedSim(msr->param.dBoxSize)) - 1.0 );;
	    }

	/*
	** Build tree, activating all particles first (just in case).
	*/
	msrInflate(msr,iStartStep);
	msrActiveRung(msr,0,1); /* Activate all particles */
	msrDomainDecomp(msr,0,0,0);
	msrUpdateSoft(msr,dTime);
	msrBuildTree(msr,dTime,msr->param.bEwald);
	msrOutputOrbits(msr,iStartStep,dTime);
#ifdef MDL_FFTW
	if (msr->param.nGridPk>0) {
	    msrOutputPk(msr,iStartStep,dTime);
	    }
#endif
	if (msrDoGravity(msr)) {
	    if (msr->param.bNewKDK) {
		msrLightConeOpen(msr,iStartStep + 1);
		bKickOpen = 1;
		}
	    else bKickOpen = 0;

            /* Compute the neutrino grids before doing gravity */
            if (msr->param.csm->val.classData.bNeutrinos){
                msrSetNuGrid(msr,dTime, msr->param.nGridNu);
                if (msr->param.bDoNuPkOutput)
                    msrOutputNuPk(msr, iStartStep, dTime);
            }
	    uRungMax = msrGravity(msr,0,MAX_RUNG,ROOT,0,dTime,iStartStep,0,bKickOpen,msr->param.bEwald,msr->param.nGroup,&iSec,&nActive);
	    msrMemStatus(msr);
	    if (msr->param.bGravStep) {
		assert(msr->param.bNewKDK == 0);    /* for now! */
		msrBuildTree(msr,dTime,msr->param.bEwald);
		msrGravity(msr,0,MAX_RUNG,ROOT,0,dTime,iStartStep,0,0,msr->param.bEwald,msr->param.nGroup,&iSec,&nActive);
		msrMemStatus(msr);
		}
	    }
	if (msrDoGas(msr)) {
	    /* Initialize SPH, Cooling and SF/FB and gas time step */
	    msrCoolSetup(msr,dTime);
	    /* Fix dTuFac conversion of T in InitSPH */
	    msrInitSph(msr,dTime);
	    }

	msrCalcEandL(msr,MSR_INIT_E,dTime,&E,&T,&U,&Eth,L,F,&W);
	dMultiEff = 1.0;
	if (msrLogInterval(msr)) {
		(void) fprintf(fpLog,"%e %e %.16e %e %e %e %.16e %.16e %.16e "
			       "%.16e %.16e %.16e %.16e %i %e\n",dTime,
			       1.0/csmTime2Exp(msr->param.csm,dTime)-1.0,
			       E,T,U,Eth,L[0],L[1],L[2],F[0],F[1],F[2],W,iSec,dMultiEff);
	    }
	if ( msr->param.bTraceRelaxation) {
	    msrInitRelaxation(msr);
	    }

	bDoOpeningKick = 0;
	for (iStep=iStartStep+1;iStep<=msrSteps(msr)&&!iStop;++iStep) {
	    if (msrComove(msr)) msrSwitchTheta(msr,dTime);
	    dMultiEff = 0.0;
	    lSec = time(0);
	    if (msr->param.bNewKDK) {
		diStep = (double)(iStep-1);
		ddTime = dTime;
		if (bDoOpeningKick) {
		    bDoOpeningKick = 0; /* clear the opening kicking flag */
                    msrLightConeOpen(msr,iStep);  /* open the lightcone */
		    uRungMax = msrGravity(msr,0,MAX_RUNG,ROOT,0,ddTime,diStep,0,1,msr->param.bEwald,msr->param.nGroup,&iSec,&nActive);
                    /* Set the Neutrino grids */
                    if (msr->param.csm->val.classData.bNeutrinos){
		        msrSetNuGrid(msr, dTime, msr->param.nGridNu);
                        if (msr->param.bDoNuPkOutput)
                            msrOutputNuPk(msr, iStartStep, dTime);
                        }
		    }
		msrNewTopStepKDK(msr,0,0,&diStep,&ddTime,&uRungMax,&iSec,&bDoCheckpoint,&bDoOutput);
		bDoOpeningKick = bDoCheckpoint || bDoOutput;
		}
	    else {
		msrTopStepKDK(msr,iStep-1,dTime,
		    msrDelta(msr),0,0,msrMaxRung(msr),1,
		    &dMultiEff,&iSec);
		}
	    dTime += msrDelta(msr);
	    lSec = time(0) - lSec;
	    msrMemStatus(msr);

	    msrOutputOrbits(msr,iStep,dTime);
#ifdef MDL_FFTW
	    if (msr->param.iPkInterval && iStep%msr->param.iPkInterval == 0) {
		msrOutputPk(msr,iStep,dTime);
		}
#endif
	    /*
	    ** Output a log file line if requested.
	    ** Note: no extra gravity calculation required.
	    */
	    if (msrLogInterval(msr) && iStep%msrLogInterval(msr) == 0) {
		msrCalcEandL(msr,MSR_STEP_E,dTime,&E,&T,&U,&Eth,L,F,&W);
		(void) fprintf(fpLog,"%e %e %.16e %e %e %e %.16e %.16e "
			       "%.16e %.16e %.16e %.16e %.16e %li %e\n",dTime,
			       1.0/csmTime2Exp(msr->param.csm,dTime)-1.0,
			       E,T,U,Eth,L[0],L[1],L[2],F[0],F[1],F[2],W,lSec,dMultiEff);
		}
	    if ( msr->param.bTraceRelaxation) {
		msrActiveRung(msr,0,1); /* Activate all particles */
		msrDomainDecomp(msr,0,0,0);
		msrBuildTree(msr,dTime,0);
		msrRelaxation(msr,dTime,msrDelta(msr),SMX_RELAXATION,0);
		}
	    if (!msr->param.bNewKDK) {
		msrCheckForOutput(msr,iStep,dTime,&bDoCheckpoint,&bDoOutput);
		}
	    if (bDoCheckpoint) {
		msrCheckpoint(msr,iStep,dTime);
		bDoCheckpoint = 0;
		}
	    if (bDoOutput) {
		msrOutput(msr,iStep,dTime,0);
		bDoOutput = 0;
		if (msr->param.bNewKDK) {
		    msrDomainDecomp(msr,0,0,0);
		    msrBuildTree(msr,dTime,msr->param.bEwald);
		    }
		}
	    }
	if (msrLogInterval(msr)) (void) fclose(fpLog);
	}
    printf("Done all, just finishing up now with msrFinish()\n");
    msrFinish(msr);
    return NULL;
    }

int main(int argc,char **argv) {
#ifdef USE_BT
    bt_initialize();
#endif
#ifdef ENABLE_FE
    feenableexcept(FE_INVALID|FE_DIVBYZERO|FE_OVERFLOW);
#endif
#ifndef CCC
    /* no stdout buffering */
    setbuf(stdout,(char *) NULL);
#endif

    mdlLaunch(argc,argv,master_ch,main_ch);

    return 0;
    }
