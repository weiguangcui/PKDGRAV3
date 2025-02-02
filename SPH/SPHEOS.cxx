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

#include "SPHEOS.h"

float SPHEOSPCTofRhoU(PKD pkd, float rho, float u, float *c, float *T, int iMat, SPHOptions *SPHoptions) {
    float P = 0.0f;
    if (iMat == 0 && SPHoptions->useBuiltinIdeal) {
        if (SPHoptions->useIsentropic) u = SPHEOSconvertEntropicFunctiontoInternalEnergy(rho, u, iMat, SPHoptions);
        if (T) *T = u / SPHoptions->TuFac;
        *c = sqrtf(SPHoptions->gamma * (SPHoptions->gamma - 1.0f) * u);
        P = (SPHoptions->gamma - 1.0f) * rho * u;
    }
    else {
#ifdef HAVE_EOSLIB_H
        double ctmp = 0.0;
        double Ttmp = 0.0;
        P = (float)EOSPCTofRhoU(pkd->materials[iMat],rho,u,&ctmp,&Ttmp);
        *c = (float)ctmp;
        if (T) *T = (float)Ttmp;
        if (P < 0.0f) P = 0.0f;
        if (*c < pkd->materials[iMat]->minSoundSpeed) *c = (float)pkd->materials[iMat]->minSoundSpeed;
#endif
    }
    return P;
}

float SPHEOSUofRhoT(PKD pkd, float rho, float T, int iMat, SPHOptions *SPHoptions) {
    float u = 0.0f;
    if (iMat == 0 && SPHoptions->useBuiltinIdeal) {
        u = T * SPHoptions->TuFac;
        if (SPHoptions->useIsentropic) u = SPHEOSconvertEntropicFunctiontoInternalEnergy(rho, u, iMat, SPHoptions);
    }
    else {
#ifdef HAVE_EOSLIB_H
        u = (float)EOSUofRhoT(pkd->materials[iMat],rho,T);
#endif
    }
    return u;
}

float SPHEOSTofRhoU(PKD pkd, float rho, float u, int iMat, SPHOptions *SPHoptions) {
    float T = 0.0f;
    if (iMat == 0 && SPHoptions->useBuiltinIdeal) {
        if (SPHoptions->useIsentropic) u = SPHEOSconvertEntropicFunctiontoInternalEnergy(rho, u, iMat, SPHoptions);
        T = u / SPHoptions->TuFac;
    }
    else {
#ifdef HAVE_EOSLIB_H
        T = (float)EOSTofRhoU(pkd->materials[iMat],rho,u);
#endif
    }
    return T;
}

float SPHEOSPofRhoT(PKD pkd, float rho, float T, int iMat, SPHOptions *SPHoptions) {
    float P = 0.0f;
    if (iMat == 0 && SPHoptions->useBuiltinIdeal) {
        float u = T * SPHoptions->TuFac;
        P = (SPHoptions->gamma - 1.0f) * rho * u;
    }
    else {
#ifdef HAVE_EOSLIB_H
        P = (float)EOSPofRhoT(pkd->materials[iMat],rho,T);
        if (P < 0.0f) P = 0.0f;
#endif
    }
    return P;
}

float SPHEOSRhoofPT(PKD pkd, float P, float T, int iMat, SPHOptions *SPHoptions) {
    float rho = 0.0f;
    if (iMat == 0 && SPHoptions->useBuiltinIdeal) {
        float u = T * SPHoptions->TuFac;
        rho = P / (u * (SPHoptions->gamma - 1.0f));
    }
    else {
#ifdef HAVE_EOSLIB_H
        rho = (float)EOSRhoofPT(pkd->materials[iMat],P,T);
#endif
    }
    return rho;
}

float SPHEOSIsentropic(PKD pkd, float rho1, float u1, float rho2, int iMat, SPHOptions *SPHoptions) {
    float u2 = 0.0f;
    if (iMat == 0 && SPHoptions->useBuiltinIdeal) {
        u2 = u1;
    }
    else {
#ifdef HAVE_EOSLIB_H
        u2 = (float)EOSIsentropic(pkd->materials[iMat], rho1, u1, rho2);
#endif
    }
    return u2;
}

float SPHEOSGammaofRhoT(PKD pkd, float rho, float T, int iMat, SPHOptions *SPHoptions) {
    float Gamma = 0.0f;
    if (iMat == 0 && SPHoptions->useBuiltinIdeal) {
        // Builtin ideal gas does not have a shear modulus and will have zero yield strength.
        Gamma = 0.0f;
    }
    else {
#ifdef HAVE_EOSLIB_H
        Gamma = (float)EOSGammaofRhoT(pkd->materials[iMat], rho, T);
#endif
    }
    return Gamma;
}

void SPHEOSApplyStrengthLimiter(PKD pkd, float rho, float u, int iMat, float *Sxx, float *Syy, float *Sxy, float *Sxz, float *Syz, SPHOptions *SPHoptions) {
    if (iMat == 0 && SPHoptions->useBuiltinIdeal) {
        *Sxx = 0.0f;
        *Syy = 0.0f;
        *Sxy = 0.0f;
        *Sxz = 0.0f;
        *Syz = 0.0f;
    }
    else {
#ifdef HAVE_EOSLIB_H
        int yieldStrengthModel = EOSYieldModel(pkd->materials[iMat]);
        if (yieldStrengthModel > 0) {
            double J2 = (*Sxx) * (*Sxx) + (*Sxx) * (*Syy) + (*Sxy) * (*Sxy) + (*Sxz) * (*Sxz) + (*Syy) * (*Syy) + (*Syz) * (*Syz);
            if (fabs(J2) > 0.0) {
                double Y = EOSYieldStrength(pkd->materials[iMat], rho, u);
                double f = std::min(Y / sqrt(J2), 1.0);
                *Sxx *= f;
                *Syy *= f;
                *Sxy *= f;
                *Sxz *= f;
                *Syz *= f;
            }
        }
#endif
    }
}