#ifndef OPCODES_H
#define OPCODES_H

#include "cpu/instruction.h"

#define OPTYPE struct decoded_instruction*
OPTYPE op_trace_end(struct decoded_instruction* i);
OPTYPE op_ud_exception(struct decoded_instruction* i);
OPTYPE op_fatal_error(struct decoded_instruction* i);
OPTYPE op_nop(struct decoded_instruction* i);

// Data transfer
OPTYPE op_mov_r32i32(struct decoded_instruction* i);

// Stack
OPTYPE op_push_r16(struct decoded_instruction* i);
OPTYPE op_push_i16(struct decoded_instruction* i);
OPTYPE op_push_e16(struct decoded_instruction* i);
OPTYPE op_push_r32(struct decoded_instruction* i);
OPTYPE op_push_i32(struct decoded_instruction* i);
OPTYPE op_push_e32(struct decoded_instruction* i);
OPTYPE op_pop_r16(struct decoded_instruction* i);
OPTYPE op_pop_r32(struct decoded_instruction* i);
OPTYPE op_pop_e16(struct decoded_instruction* i);
OPTYPE op_pop_e32(struct decoded_instruction* i);

OPTYPE op_pop_s16(struct decoded_instruction* i);
OPTYPE op_pop_s32(struct decoded_instruction* i);
OPTYPE op_push_s16(struct decoded_instruction* i);
OPTYPE op_push_s32(struct decoded_instruction* i);

OPTYPE op_pusha(struct decoded_instruction* i);
OPTYPE op_pushad(struct decoded_instruction* i);
OPTYPE op_popa(struct decoded_instruction* i);
OPTYPE op_popad(struct decoded_instruction* i);

// Arithmetic
OPTYPE op_arith_r8r8(struct decoded_instruction* i);
OPTYPE op_arith_r8i8(struct decoded_instruction* i);
OPTYPE op_arith_r8e8(struct decoded_instruction* i);
OPTYPE op_arith_e8r8(struct decoded_instruction* i);
OPTYPE op_arith_e8i8(struct decoded_instruction* i);
OPTYPE op_arith_r16r16(struct decoded_instruction* i);
OPTYPE op_arith_r16i16(struct decoded_instruction* i);
OPTYPE op_arith_r16e16(struct decoded_instruction* i);
OPTYPE op_arith_e16r16(struct decoded_instruction* i);
OPTYPE op_arith_e16i16(struct decoded_instruction* i);
OPTYPE op_arith_r32r32(struct decoded_instruction* i);
OPTYPE op_arith_r32i32(struct decoded_instruction* i);
OPTYPE op_arith_r32e32(struct decoded_instruction* i);
OPTYPE op_arith_e32r32(struct decoded_instruction* i);
OPTYPE op_arith_e32i32(struct decoded_instruction* i);

OPTYPE op_shift_r8cl(struct decoded_instruction* i);
OPTYPE op_shift_r8i8(struct decoded_instruction* i);
OPTYPE op_shift_e8cl(struct decoded_instruction* i);
OPTYPE op_shift_e8i8(struct decoded_instruction* i);
OPTYPE op_shift_r16cl(struct decoded_instruction* i);
OPTYPE op_shift_r16i16(struct decoded_instruction* i);
OPTYPE op_shift_e16cl(struct decoded_instruction* i);
OPTYPE op_shift_e16i16(struct decoded_instruction* i);
OPTYPE op_shift_r32cl(struct decoded_instruction* i);
OPTYPE op_shift_r32i32(struct decoded_instruction* i);
OPTYPE op_shift_e32cl(struct decoded_instruction* i);
OPTYPE op_shift_e32i32(struct decoded_instruction* i);

OPTYPE op_cmp_e8r8(struct decoded_instruction* i);
OPTYPE op_cmp_r8r8(struct decoded_instruction* i);
OPTYPE op_cmp_r8e8(struct decoded_instruction* i);
OPTYPE op_cmp_r8i8(struct decoded_instruction* i);
OPTYPE op_cmp_e8i8(struct decoded_instruction* i);
OPTYPE op_cmp_e16r16(struct decoded_instruction* i);
OPTYPE op_cmp_r16r16(struct decoded_instruction* i);
OPTYPE op_cmp_r16e16(struct decoded_instruction* i);
OPTYPE op_cmp_r16i16(struct decoded_instruction* i);
OPTYPE op_cmp_e16i16(struct decoded_instruction* i);
OPTYPE op_cmp_e32r32(struct decoded_instruction* i);
OPTYPE op_cmp_r32r32(struct decoded_instruction* i);
OPTYPE op_cmp_r32e32(struct decoded_instruction* i);
OPTYPE op_cmp_r32i32(struct decoded_instruction* i);
OPTYPE op_cmp_e32i32(struct decoded_instruction* i);

OPTYPE op_test_e8r8(struct decoded_instruction* i);
OPTYPE op_test_r8r8(struct decoded_instruction* i);
OPTYPE op_test_r8e8(struct decoded_instruction* i);
OPTYPE op_test_r8i8(struct decoded_instruction* i);
OPTYPE op_test_e8i8(struct decoded_instruction* i);
OPTYPE op_test_e16r16(struct decoded_instruction* i);
OPTYPE op_test_r16r16(struct decoded_instruction* i);
OPTYPE op_test_r16e16(struct decoded_instruction* i);
OPTYPE op_test_r16i16(struct decoded_instruction* i);
OPTYPE op_test_e16i16(struct decoded_instruction* i);
OPTYPE op_test_e32r32(struct decoded_instruction* i);
OPTYPE op_test_r32r32(struct decoded_instruction* i);
OPTYPE op_test_r32e32(struct decoded_instruction* i);
OPTYPE op_test_r32i32(struct decoded_instruction* i);
OPTYPE op_test_e32i32(struct decoded_instruction* i);

OPTYPE op_inc_r8(struct decoded_instruction* i);
OPTYPE op_inc_e8(struct decoded_instruction* i);
OPTYPE op_inc_r16(struct decoded_instruction* i);
OPTYPE op_inc_e16(struct decoded_instruction* i);
OPTYPE op_inc_r32(struct decoded_instruction* i);
OPTYPE op_inc_e32(struct decoded_instruction* i);
OPTYPE op_dec_r8(struct decoded_instruction* i);
OPTYPE op_dec_e8(struct decoded_instruction* i);
OPTYPE op_dec_r16(struct decoded_instruction* i);
OPTYPE op_dec_e16(struct decoded_instruction* i);
OPTYPE op_dec_r32(struct decoded_instruction* i);
OPTYPE op_dec_e32(struct decoded_instruction* i);

OPTYPE op_not_r8(struct decoded_instruction* i);
OPTYPE op_not_e8(struct decoded_instruction* i);
OPTYPE op_not_r16(struct decoded_instruction* i);
OPTYPE op_not_e16(struct decoded_instruction* i);
OPTYPE op_not_r32(struct decoded_instruction* i);
OPTYPE op_not_e32(struct decoded_instruction* i);
OPTYPE op_neg_r8(struct decoded_instruction* i);
OPTYPE op_neg_e8(struct decoded_instruction* i);
OPTYPE op_neg_r16(struct decoded_instruction* i);
OPTYPE op_neg_e16(struct decoded_instruction* i);
OPTYPE op_neg_r32(struct decoded_instruction* i);
OPTYPE op_neg_e32(struct decoded_instruction* i);

OPTYPE op_muldiv_r8(struct decoded_instruction* i);
OPTYPE op_muldiv_e8(struct decoded_instruction* i);
OPTYPE op_muldiv_r16(struct decoded_instruction* i);
OPTYPE op_muldiv_e16(struct decoded_instruction* i);
OPTYPE op_muldiv_r32(struct decoded_instruction* i);
OPTYPE op_muldiv_e32(struct decoded_instruction* i);
OPTYPE op_imul_r16r16i16(struct decoded_instruction* i);
OPTYPE op_imul_r16e16i16(struct decoded_instruction* i);
OPTYPE op_imul_r32r32i32(struct decoded_instruction* i);
OPTYPE op_imul_r32e32i32(struct decoded_instruction* i);
OPTYPE op_imul_r16r16(struct decoded_instruction* i);
OPTYPE op_imul_r32r32(struct decoded_instruction* i);
OPTYPE op_imul_r16e16(struct decoded_instruction* i);
OPTYPE op_imul_r32e32(struct decoded_instruction* i);

OPTYPE op_shrd_r16r16i8(struct decoded_instruction* i);
OPTYPE op_shrd_r32r32i8(struct decoded_instruction* i);
OPTYPE op_shrd_r16r16cl(struct decoded_instruction* i);
OPTYPE op_shrd_r32r32cl(struct decoded_instruction* i);
OPTYPE op_shrd_e16r16i8(struct decoded_instruction* i);
OPTYPE op_shrd_e32r32i8(struct decoded_instruction* i);
OPTYPE op_shrd_e16r16cl(struct decoded_instruction* i);
OPTYPE op_shrd_e32r32cl(struct decoded_instruction* i);
OPTYPE op_shld_r16r16i8(struct decoded_instruction* i);
OPTYPE op_shld_r32r32i8(struct decoded_instruction* i);
OPTYPE op_shld_r16r16cl(struct decoded_instruction* i);
OPTYPE op_shld_r32r32cl(struct decoded_instruction* i);
OPTYPE op_shld_e16r16i8(struct decoded_instruction* i);
OPTYPE op_shld_e32r32i8(struct decoded_instruction* i);
OPTYPE op_shld_e16r16cl(struct decoded_instruction* i);
OPTYPE op_shld_e32r32cl(struct decoded_instruction* i);

// Control flow
OPTYPE op_jmpf(struct decoded_instruction* i);
OPTYPE op_jmpf_e16(struct decoded_instruction* i);
OPTYPE op_jmpf_e32(struct decoded_instruction* i);
OPTYPE op_jmp_r16(struct decoded_instruction* i);
OPTYPE op_jmp_r32(struct decoded_instruction* i);
OPTYPE op_jmp_e16(struct decoded_instruction* i);
OPTYPE op_jmp_e32(struct decoded_instruction* i);
OPTYPE op_jmp_rel32(struct decoded_instruction* i);
OPTYPE op_jmp_rel16(struct decoded_instruction* i);
OPTYPE op_callf16_ap(struct decoded_instruction* i);
OPTYPE op_callf32_ap(struct decoded_instruction* i);
OPTYPE op_callf_e16(struct decoded_instruction* i);
OPTYPE op_callf_e32(struct decoded_instruction* i);
OPTYPE op_loop_rel16(struct decoded_instruction* i);
OPTYPE op_loop_rel32(struct decoded_instruction* i);
OPTYPE op_loopz_rel16(struct decoded_instruction* i);
OPTYPE op_loopz_rel32(struct decoded_instruction* i);
OPTYPE op_loopnz_rel16(struct decoded_instruction* i);
OPTYPE op_loopnz_rel32(struct decoded_instruction* i);
OPTYPE op_jecxz_rel16(struct decoded_instruction* i);
OPTYPE op_jecxz_rel32(struct decoded_instruction* i);

// <<< BEGIN AUTOGENERATE "jcc" >>>
// Auto-generated on Mon Sep 16 2019 23:27:23 GMT-0700 (PDT)
OPTYPE op_jo16(struct decoded_instruction* i);
OPTYPE op_jo32(struct decoded_instruction* i);
OPTYPE op_jno16(struct decoded_instruction* i);
OPTYPE op_jno32(struct decoded_instruction* i);
OPTYPE op_jb16(struct decoded_instruction* i);
OPTYPE op_jb32(struct decoded_instruction* i);
OPTYPE op_jnb16(struct decoded_instruction* i);
OPTYPE op_jnb32(struct decoded_instruction* i);
OPTYPE op_jz16(struct decoded_instruction* i);
OPTYPE op_jz32(struct decoded_instruction* i);
OPTYPE op_jnz16(struct decoded_instruction* i);
OPTYPE op_jnz32(struct decoded_instruction* i);
OPTYPE op_jbe16(struct decoded_instruction* i);
OPTYPE op_jbe32(struct decoded_instruction* i);
OPTYPE op_jnbe16(struct decoded_instruction* i);
OPTYPE op_jnbe32(struct decoded_instruction* i);
OPTYPE op_js16(struct decoded_instruction* i);
OPTYPE op_js32(struct decoded_instruction* i);
OPTYPE op_jns16(struct decoded_instruction* i);
OPTYPE op_jns32(struct decoded_instruction* i);
OPTYPE op_jp16(struct decoded_instruction* i);
OPTYPE op_jp32(struct decoded_instruction* i);
OPTYPE op_jnp16(struct decoded_instruction* i);
OPTYPE op_jnp32(struct decoded_instruction* i);
OPTYPE op_jl16(struct decoded_instruction* i);
OPTYPE op_jl32(struct decoded_instruction* i);
OPTYPE op_jnl16(struct decoded_instruction* i);
OPTYPE op_jnl32(struct decoded_instruction* i);
OPTYPE op_jle16(struct decoded_instruction* i);
OPTYPE op_jle32(struct decoded_instruction* i);
OPTYPE op_jnle16(struct decoded_instruction* i);
OPTYPE op_jnle32(struct decoded_instruction* i);
// <<< END AUTOGENERATE "jcc" >>>

OPTYPE op_call_j16(struct decoded_instruction* i);
OPTYPE op_call_j32(struct decoded_instruction* i);
OPTYPE op_call_r16(struct decoded_instruction* i);
OPTYPE op_call_r32(struct decoded_instruction* i);
OPTYPE op_call_e16(struct decoded_instruction* i);
OPTYPE op_call_e32(struct decoded_instruction* i);
OPTYPE op_ret16(struct decoded_instruction* i);
OPTYPE op_ret32(struct decoded_instruction* i);
OPTYPE op_ret16_iw(struct decoded_instruction* i);
OPTYPE op_ret32_iw(struct decoded_instruction* i);
OPTYPE op_int(struct decoded_instruction* i);
OPTYPE op_into(struct decoded_instruction* i);
OPTYPE op_retf16(struct decoded_instruction* i);
OPTYPE op_retf32(struct decoded_instruction* i);
OPTYPE op_iret16(struct decoded_instruction* i);
OPTYPE op_iret32(struct decoded_instruction* i);

// I/O
OPTYPE op_out_i8al(struct decoded_instruction* i);
OPTYPE op_out_i8ax(struct decoded_instruction* i);
OPTYPE op_out_i8eax(struct decoded_instruction* i);
OPTYPE op_in_i8al(struct decoded_instruction* i);
OPTYPE op_in_i8ax(struct decoded_instruction* i);
OPTYPE op_in_i8eax(struct decoded_instruction* i);
OPTYPE op_out_dxal(struct decoded_instruction* i);
OPTYPE op_out_dxax(struct decoded_instruction* i);
OPTYPE op_out_dxeax(struct decoded_instruction* i);
OPTYPE op_in_dxal(struct decoded_instruction* i);
OPTYPE op_in_dxax(struct decoded_instruction* i);
OPTYPE op_in_dxeax(struct decoded_instruction* i);

// Data transfer
OPTYPE op_mov_r8i8(struct decoded_instruction* i);
OPTYPE op_mov_r16i16(struct decoded_instruction* i);
OPTYPE op_mov_r32i32(struct decoded_instruction* i);
OPTYPE op_mov_r8r8(struct decoded_instruction* i);
OPTYPE op_mov_r8e8(struct decoded_instruction* i);
OPTYPE op_mov_e8r8(struct decoded_instruction* i);
OPTYPE op_mov_e8i8(struct decoded_instruction* i);
OPTYPE op_mov_r16r16(struct decoded_instruction* i);
OPTYPE op_mov_r16e16(struct decoded_instruction* i);
OPTYPE op_mov_e16r16(struct decoded_instruction* i);
OPTYPE op_mov_e16i16(struct decoded_instruction* i);
OPTYPE op_mov_r32r32(struct decoded_instruction* i);
OPTYPE op_mov_r32e32(struct decoded_instruction* i);
OPTYPE op_mov_e32r32(struct decoded_instruction* i);
OPTYPE op_mov_e32i32(struct decoded_instruction* i);

OPTYPE op_mov_s16r16(struct decoded_instruction* i);
OPTYPE op_mov_s16e16(struct decoded_instruction* i);
OPTYPE op_mov_e16s16(struct decoded_instruction* i);
OPTYPE op_mov_r16s16(struct decoded_instruction* i);
OPTYPE op_mov_r32s16(struct decoded_instruction* i);

OPTYPE op_cmov_r16e16(struct decoded_instruction* i);
OPTYPE op_cmov_r16r16(struct decoded_instruction* i);
OPTYPE op_cmov_r32e32(struct decoded_instruction* i);
OPTYPE op_cmov_r32r32(struct decoded_instruction* i);
OPTYPE op_setcc_e8(struct decoded_instruction* i);
OPTYPE op_setcc_r8(struct decoded_instruction* i);
//TODO figure out what is going wrong 
OPTYPE op_lea_r16e16(struct decoded_instruction* i);
OPTYPE op_lea_r32e32(struct decoded_instruction* i);

OPTYPE op_mov_eaxm32(struct decoded_instruction* i);
OPTYPE op_mov_axm16(struct decoded_instruction* i);
OPTYPE op_mov_alm8(struct decoded_instruction* i);
OPTYPE op_mov_m32eax(struct decoded_instruction* i);
OPTYPE op_mov_m16ax(struct decoded_instruction* i);
OPTYPE op_mov_m8al(struct decoded_instruction* i);

OPTYPE op_lds_r16e16(struct decoded_instruction* i);
OPTYPE op_lds_r32e32(struct decoded_instruction* i);
OPTYPE op_les_r16e16(struct decoded_instruction* i);
OPTYPE op_les_r32e32(struct decoded_instruction* i);
OPTYPE op_lss_r16e16(struct decoded_instruction* i);
OPTYPE op_lss_r32e32(struct decoded_instruction* i);
OPTYPE op_lfs_r16e16(struct decoded_instruction* i);
OPTYPE op_lfs_r32e32(struct decoded_instruction* i);
OPTYPE op_lgs_r16e16(struct decoded_instruction* i);
OPTYPE op_lgs_r32e32(struct decoded_instruction* i);

OPTYPE op_movzx_r16r8(struct decoded_instruction* i);
OPTYPE op_movzx_r32r8(struct decoded_instruction* i);
OPTYPE op_movzx_r16e8(struct decoded_instruction* i);
OPTYPE op_movzx_r32e8(struct decoded_instruction* i);
OPTYPE op_movzx_r32r16(struct decoded_instruction* i);
OPTYPE op_movzx_r32e16(struct decoded_instruction* i);
OPTYPE op_movsx_r16r8(struct decoded_instruction* i);
OPTYPE op_movsx_r32r8(struct decoded_instruction* i);
OPTYPE op_movsx_r16e8(struct decoded_instruction* i);
OPTYPE op_movsx_r32e8(struct decoded_instruction* i);
OPTYPE op_movsx_r32r16(struct decoded_instruction* i);
OPTYPE op_movsx_r32e16(struct decoded_instruction* i);

OPTYPE op_xchg_r8r8(struct decoded_instruction* i);
OPTYPE op_xchg_r16r16(struct decoded_instruction* i);
OPTYPE op_xchg_r32r32(struct decoded_instruction* i);
OPTYPE op_xchg_r8e8(struct decoded_instruction* i);
OPTYPE op_xchg_r16e16(struct decoded_instruction* i);
OPTYPE op_xchg_r32e32(struct decoded_instruction* i);

OPTYPE op_cmpxchg_r8r8(struct decoded_instruction* i);
OPTYPE op_cmpxchg_e8r8(struct decoded_instruction* i);
OPTYPE op_cmpxchg_r16r16(struct decoded_instruction* i);
OPTYPE op_cmpxchg_e16r16(struct decoded_instruction* i);
OPTYPE op_cmpxchg_r32r32(struct decoded_instruction* i);
OPTYPE op_cmpxchg_e32r32(struct decoded_instruction* i);
OPTYPE op_cmpxchg8b_e32(struct decoded_instruction* i);

OPTYPE op_xadd_r8r8(struct decoded_instruction* i);
OPTYPE op_xadd_r8e8(struct decoded_instruction* i);
OPTYPE op_xadd_r16r16(struct decoded_instruction* i);
OPTYPE op_xadd_r16e16(struct decoded_instruction* i);
OPTYPE op_xadd_r32r32(struct decoded_instruction* i);
OPTYPE op_xadd_r32e32(struct decoded_instruction* i);

OPTYPE op_bound_r16e16(struct decoded_instruction* i);
OPTYPE op_bound_r32e32(struct decoded_instruction* i);

OPTYPE op_xlat16(struct decoded_instruction* i);
OPTYPE op_xlat32(struct decoded_instruction* i);

OPTYPE op_bswap_r16(struct decoded_instruction* i);
OPTYPE op_bswap_r32(struct decoded_instruction* i);

// BCD
OPTYPE op_daa(struct decoded_instruction* i);
OPTYPE op_das(struct decoded_instruction* i);
OPTYPE op_aaa(struct decoded_instruction* i);
OPTYPE op_aas(struct decoded_instruction* i);
OPTYPE op_aam(struct decoded_instruction* i);
OPTYPE op_aad(struct decoded_instruction* i);

// Bit test
// <<< BEGIN AUTOGENERATE "bit" >>>
// Auto-generated on Thu Oct 03 2019 14:47:14 GMT-0700 (PDT)
OPTYPE op_bt_r16(struct decoded_instruction* i);
OPTYPE op_bts_r16(struct decoded_instruction* i);
OPTYPE op_btc_r16(struct decoded_instruction* i);
OPTYPE op_btr_r16(struct decoded_instruction* i);
OPTYPE op_bt_r32(struct decoded_instruction* i);
OPTYPE op_bts_r32(struct decoded_instruction* i);
OPTYPE op_btc_r32(struct decoded_instruction* i);
OPTYPE op_btr_r32(struct decoded_instruction* i);
OPTYPE op_bt_e16(struct decoded_instruction* i);
OPTYPE op_bt_e32(struct decoded_instruction* i);
OPTYPE op_bts_e16(struct decoded_instruction* i);
OPTYPE op_btc_e16(struct decoded_instruction* i);
OPTYPE op_btr_e16(struct decoded_instruction* i);
OPTYPE op_bts_e32(struct decoded_instruction* i);
OPTYPE op_btc_e32(struct decoded_instruction* i);
OPTYPE op_btr_e32(struct decoded_instruction* i);

// <<< END AUTOGENERATE "bit" >>>

OPTYPE op_bsf_r16r16(struct decoded_instruction* i);
OPTYPE op_bsf_r16e16(struct decoded_instruction* i);
OPTYPE op_bsf_r32r32(struct decoded_instruction* i);
OPTYPE op_bsf_r32e32(struct decoded_instruction* i);
OPTYPE op_bsr_r16r16(struct decoded_instruction* i);
OPTYPE op_bsr_r16e16(struct decoded_instruction* i);
OPTYPE op_bsr_r32r32(struct decoded_instruction* i);
OPTYPE op_bsr_r32e32(struct decoded_instruction* i);

// Miscellaneous
OPTYPE op_cli(struct decoded_instruction* i);
OPTYPE op_sti(struct decoded_instruction* i);
OPTYPE op_cmc(struct decoded_instruction* i);
OPTYPE op_clc(struct decoded_instruction* i);
OPTYPE op_stc(struct decoded_instruction* i);
OPTYPE op_cld(struct decoded_instruction* i);
OPTYPE op_std(struct decoded_instruction* i);
OPTYPE op_hlt(struct decoded_instruction* i);
OPTYPE op_cpuid (struct decoded_instruction* i);
OPTYPE op_rdmsr (struct decoded_instruction* i);
OPTYPE op_wrmsr (struct decoded_instruction* i);
OPTYPE op_rdtsc(struct decoded_instruction* i);
OPTYPE op_pushf (struct decoded_instruction* i);
OPTYPE op_pushfd (struct decoded_instruction* i);
OPTYPE op_popf (struct decoded_instruction* i);
OPTYPE op_popfd (struct decoded_instruction* i);
OPTYPE op_cbw(struct decoded_instruction* i);
OPTYPE op_cwde(struct decoded_instruction* i);
OPTYPE op_cwd(struct decoded_instruction* i);
OPTYPE op_cdq(struct decoded_instruction* i);
OPTYPE op_lahf(struct decoded_instruction* i);
OPTYPE op_sahf(struct decoded_instruction* i);
OPTYPE op_enter16(struct decoded_instruction* i);
OPTYPE op_enter32(struct decoded_instruction* i);
OPTYPE op_leave16(struct decoded_instruction* i);
OPTYPE op_leave32(struct decoded_instruction* i);

// Protected mode opcodes
OPTYPE op_sgdt_e32(struct decoded_instruction* i);
OPTYPE op_sidt_e32(struct decoded_instruction* i);
OPTYPE op_lgdt_e16(struct decoded_instruction* i);
OPTYPE op_lgdt_e32(struct decoded_instruction* i);
OPTYPE op_lidt_e16(struct decoded_instruction* i);
OPTYPE op_lidt_e32(struct decoded_instruction* i);
OPTYPE op_str_sldt_e16(struct decoded_instruction* i);
OPTYPE op_str_sldt_r16(struct decoded_instruction* i);

OPTYPE op_smsw_r16(struct decoded_instruction* i);
OPTYPE op_smsw_r32(struct decoded_instruction* i);
OPTYPE op_smsw_e16(struct decoded_instruction* i);
OPTYPE op_lmsw_r16(struct decoded_instruction* i);
OPTYPE op_lmsw_e16(struct decoded_instruction* i);
OPTYPE op_invlpg_e8(struct decoded_instruction* i);

OPTYPE op_mov_r32cr(struct decoded_instruction* i);
OPTYPE op_mov_crr32(struct decoded_instruction* i);
OPTYPE op_mov_r32dr(struct decoded_instruction* i);
OPTYPE op_mov_drr32(struct decoded_instruction* i);

OPTYPE op_ltr_e16(struct decoded_instruction* i);
OPTYPE op_ltr_r16(struct decoded_instruction* i);
OPTYPE op_lldt_e16(struct decoded_instruction* i);
OPTYPE op_lldt_r16(struct decoded_instruction* i);

OPTYPE op_lar_r16e16(struct decoded_instruction* i);
OPTYPE op_lar_r16r16(struct decoded_instruction* i);
OPTYPE op_lar_r32e32(struct decoded_instruction* i);
OPTYPE op_lar_r32r32(struct decoded_instruction* i);
OPTYPE op_lsl_r16e16(struct decoded_instruction* i);
OPTYPE op_lsl_r16r16(struct decoded_instruction* i);
OPTYPE op_lsl_r32e32(struct decoded_instruction* i);
OPTYPE op_lsl_r32r32(struct decoded_instruction* i);

OPTYPE op_arpl_e16(struct decoded_instruction* i);
OPTYPE op_arpl_r16(struct decoded_instruction* i);
OPTYPE op_verr_e16(struct decoded_instruction* i);
OPTYPE op_verr_r16(struct decoded_instruction* i);
OPTYPE op_verw_e16(struct decoded_instruction* i);
OPTYPE op_verw_r16(struct decoded_instruction* i);
OPTYPE op_clts(struct decoded_instruction* i);
OPTYPE op_wbinvd(struct decoded_instruction* i);
OPTYPE op_prefetchh(struct decoded_instruction* i);

OPTYPE op_sysenter(struct decoded_instruction* i);
OPTYPE op_sysexit(struct decoded_instruction* i);
OPTYPE op_ldmxcsr(struct decoded_instruction* i);
OPTYPE op_stmxcsr(struct decoded_instruction* i);

// FPU
OPTYPE op_fpu_mem(struct decoded_instruction* i);
OPTYPE op_fpu_reg(struct decoded_instruction* i);
OPTYPE op_fwait(struct decoded_instruction* i);

OPTYPE op_mfence(struct decoded_instruction* i);
OPTYPE op_fxsave(struct decoded_instruction* i);
OPTYPE op_fxrstor(struct decoded_instruction* i);

// SSE
OPTYPE op_sse_10_17(struct decoded_instruction* i);
OPTYPE op_sse_28_2F(struct decoded_instruction* i);
OPTYPE op_sse_38(struct decoded_instruction* i);
OPTYPE op_sse_6638(struct decoded_instruction* i);
OPTYPE op_sse_50_57(struct decoded_instruction* i);
OPTYPE op_sse_58_5F(struct decoded_instruction* i);
OPTYPE op_sse_60_67(struct decoded_instruction* i);
OPTYPE op_sse_68_6F(struct decoded_instruction* i);
OPTYPE op_sse_70_76(struct decoded_instruction* i);
OPTYPE op_sse_7C_7D(struct decoded_instruction* i);
OPTYPE op_sse_7E_7F(struct decoded_instruction* i);
OPTYPE op_sse_C2_C6(struct decoded_instruction* i);
OPTYPE op_sse_D0_D7(struct decoded_instruction* i);
OPTYPE op_sse_D8_DF(struct decoded_instruction* i);
OPTYPE op_sse_E0_E7(struct decoded_instruction* i);
OPTYPE op_sse_E8_EF(struct decoded_instruction* i);
OPTYPE op_sse_F1_F7(struct decoded_instruction* i);
OPTYPE op_sse_F8_FE(struct decoded_instruction* i);

// SSE
OPTYPE op_ldmxcsr(struct decoded_instruction* i);
OPTYPE op_stmxcsr(struct decoded_instruction* i);
OPTYPE op_mfence(struct decoded_instruction* i);
OPTYPE op_fxsave(struct decoded_instruction* i);
OPTYPE op_fxrstor(struct decoded_instruction* i);

// MMX
OPTYPE op_emms(struct decoded_instruction* i);

// <<< BEGIN AUTOGENERATE "string" >>>
OPTYPE op_movsb16(struct decoded_instruction* i);
OPTYPE op_movsb32(struct decoded_instruction* i);
OPTYPE op_movsw16(struct decoded_instruction* i);
OPTYPE op_movsw32(struct decoded_instruction* i);
OPTYPE op_movsd16(struct decoded_instruction* i);
OPTYPE op_movsd32(struct decoded_instruction* i);
OPTYPE op_stosb16(struct decoded_instruction* i);
OPTYPE op_stosb32(struct decoded_instruction* i);
OPTYPE op_stosw16(struct decoded_instruction* i);
OPTYPE op_stosw32(struct decoded_instruction* i);
OPTYPE op_stosd16(struct decoded_instruction* i);
OPTYPE op_stosd32(struct decoded_instruction* i);
OPTYPE op_scasb16(struct decoded_instruction* i);
OPTYPE op_scasb32(struct decoded_instruction* i);
OPTYPE op_scasw16(struct decoded_instruction* i);
OPTYPE op_scasw32(struct decoded_instruction* i);
OPTYPE op_scasd16(struct decoded_instruction* i);
OPTYPE op_scasd32(struct decoded_instruction* i);
OPTYPE op_insb16(struct decoded_instruction* i);
OPTYPE op_insb32(struct decoded_instruction* i);
OPTYPE op_insw16(struct decoded_instruction* i);
OPTYPE op_insw32(struct decoded_instruction* i);
OPTYPE op_insd16(struct decoded_instruction* i);
OPTYPE op_insd32(struct decoded_instruction* i);
OPTYPE op_outsb16(struct decoded_instruction* i);
OPTYPE op_outsb32(struct decoded_instruction* i);
OPTYPE op_outsw16(struct decoded_instruction* i);
OPTYPE op_outsw32(struct decoded_instruction* i);
OPTYPE op_outsd16(struct decoded_instruction* i);
OPTYPE op_outsd32(struct decoded_instruction* i);
OPTYPE op_cmpsb16(struct decoded_instruction* i);
OPTYPE op_cmpsb32(struct decoded_instruction* i);
OPTYPE op_cmpsw16(struct decoded_instruction* i);
OPTYPE op_cmpsw32(struct decoded_instruction* i);
OPTYPE op_cmpsd16(struct decoded_instruction* i);
OPTYPE op_cmpsd32(struct decoded_instruction* i);
OPTYPE op_lodsb16(struct decoded_instruction* i);
OPTYPE op_lodsb32(struct decoded_instruction* i);
OPTYPE op_lodsw16(struct decoded_instruction* i);
OPTYPE op_lodsw32(struct decoded_instruction* i);
OPTYPE op_lodsd16(struct decoded_instruction* i);
OPTYPE op_lodsd32(struct decoded_instruction* i);

// <<< END AUTOGENERATE "string" >>>

OPTYPE op_lodsb16(struct decoded_instruction* i);
OPTYPE op_lodsb32(struct decoded_instruction* i);
OPTYPE op_lodsw16(struct decoded_instruction* i);
OPTYPE op_lodsw32(struct decoded_instruction* i);
OPTYPE op_lodsd16(struct decoded_instruction* i);
OPTYPE op_lodsd32(struct decoded_instruction* i);

// Flags update methods
enum {
    ADD8,
    ADD16,
    ADD32,
    BIT, // or, and, xor
    ADC8,
    ADC16,
    ADC32,
    SBB8,
    SBB16,
    SBB32,
    SUB8,
    SUB16,
    SUB32,
    SHL8,
    SHL16,
    SHL32,
    SHR8,
    SHR16,
    SHR32,
    SAR8,
    SAR16,
    SAR32,
    SHLD16,
    SHLD32,
    SHRD16,
    SHRD32,
    MUL,
    INC8,
    INC16,
    INC32,
    DEC8,
    DEC16,
    DEC32,
    EFLAGS_FULL_UPDATE
};

#endif