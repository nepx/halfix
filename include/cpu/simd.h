#ifndef SIMD_H
#define SIMD_H

#include "cpu/instruction.h"

// Opcode naming standard:
//  MG[d|q]: MMX register operand, encoded by the REG field
//  ME[d|q]: MMX register or memory operand, encoded by the R/M field
//  E[d|q]: GPR or memory operand, encoded by the R/M field
//  G[d|q]: GPR, encoded by the REG field
//  XG[d|q|o]: XMM register operand, encoded by the REG field
//  XE[d|q|o]: XMM register or memory operand, encoded by the R/M field
enum {
    // 0F 10
    MOVUPS_XGoXEo,
    MOVSS_XGdXEd,
    MOVSD_XGqXEq,

    // 0F 11
    MOVUPS_XEoXGo,
    MOVSS_XEdXGd,
    MOVSD_XEqXGq,
    
    // 0F 12
    MOVHLPS_XGqXEq,
    MOVLPS_XGqXEq,

    // 0F 13
    //MOVLPS_XEqXGq, // This appears to do exactly the same thing as MOVSD_XEqXGq
    
    // 0F 14
    UNPCKLPS_XGoXEq,
    UNPCKLPD_XGoXEo,
    
    // 0F 15
    UNPCKHPS_XGoXEq,
    UNPCKHPD_XGoXEo,

    // 0F 16
    MOVLHPS_XGqXEq,
    MOVHPS_XGqXEq,

    // 0F 17
    MOVHPS_XEqXGq
};

enum {
    MOVAPS_XGoXEo,
    MOVAPS_XEoXGo,
    CVTPI2PS_XGqMEq,
    CVTSI2SS_XGdEd,
    CVTPI2PD_XGoMEq,
    CVTSI2SD_XGqMEd,
    CVTPS2PI_MGqXEq,
    CVTSS2SI_GdXEd,
    CVTPD2PI_MGqXEo,
    CVTSD2SI_GdXEq,
    UCOMISS_XGdXEd,
    UCOMISD_XGqXEq
};

enum {
    // 0F 50
    MOVMSKPS_GdXEo,
    MOVMSKPD_GdXEo,

    // 0F 51
    SQRTPS_XGoXEo,
    SQRTSS_XGdXEd,
    SQRTPD_XGoXEo,
    SQRTSD_XGqXEq,
    
    // 0F 52
    RSQRTPS_XGoXEo,
    RSQRTSS_XGdXEd,

    // 0F 53
    RCPPS_XGoXEo,
    RCPSS_XGdXEd,

    // 0F 54
    ANDPS_XGoXEo,

    // 0F 55
    ANDNPS_XGoXEo,

    // 0F 56
    ORPS_XGoXEo,

    // 0F 57
    XORPS_XGoXEo
};

int cpu_sse_exception(void);

int cpu_emms(void);
int execute_0F10_17(struct decoded_instruction* i);
int execute_0F28_2F(struct decoded_instruction* i);

#endif