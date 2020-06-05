// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/macro-assembler.h"
#include "src/compiler/backend/instruction-scheduler.h"

namespace v8 {
namespace internal {
namespace compiler {

bool InstructionScheduler::SchedulerSupported() { return true; }

int InstructionScheduler::GetTargetInstructionFlags(
    const Instruction* instr) const {
  switch (instr->arch_opcode()) {
    case kRiscvAbsD:
    case kRiscvAbsS:
    case kRiscvAdd:
    case kRiscvAddD:
    case kRiscvAddS:
    case kRiscvAnd:
    case kRiscvAnd32:
    case kRiscvAssertEqual:
    case kRiscvBitcastDL:
    case kRiscvBitcastLD:
    case kRiscvByteSwap32:
    case kRiscvByteSwap64:
    case kRiscvCeilWD:
    case kRiscvCeilWS:
    case kRiscvClz:
    case kRiscvCmp:
    case kRiscvCmpD:
    case kRiscvCmpS:
    case kRiscvCtz:
    case kRiscvCvtDL:
    case kRiscvCvtDS:
    case kRiscvCvtDUl:
    case kRiscvCvtDUw:
    case kRiscvCvtDW:
    case kRiscvCvtSD:
    case kRiscvCvtSL:
    case kRiscvCvtSUl:
    case kRiscvCvtSUw:
    case kRiscvCvtSW:
    case kRiscvDMulHigh:
    case kRiscvMulHighU:
    case kRiscvDadd:
    case kRiscvDaddOvf:
    case kRiscvDclz:
    case kRiscvDctz:
    case kRiscvDdiv:
    case kRiscvDdivU:
    case kRiscvDext:
    case kRiscvDins:
    case kRiscvDiv:
    case kRiscvDivD:
    case kRiscvDivS:
    case kRiscvDivU:
    case kRiscvDlsa:
    case kRiscvDmod:
    case kRiscvDmodU:
    case kRiscvDmul:
    case kRiscvDpopcnt:
    case kRiscvDror:
    case kRiscvDsar:
    case kRiscvDshl:
    case kRiscvDshr:
    case kRiscvDsub:
    case kRiscvDsubOvf:
    case kRiscvExt:
    case kRiscvF64x2Abs:
    case kRiscvF64x2Neg:
    case kRiscvF64x2Sqrt:
    case kRiscvF64x2Add:
    case kRiscvF64x2Sub:
    case kRiscvF64x2Mul:
    case kRiscvF64x2Div:
    case kRiscvF64x2Min:
    case kRiscvF64x2Max:
    case kRiscvF64x2Eq:
    case kRiscvF64x2Ne:
    case kRiscvF64x2Lt:
    case kRiscvF64x2Le:
    case kRiscvI64x2Add:
    case kRiscvI64x2Sub:
    case kRiscvI64x2Mul:
    case kRiscvI64x2Neg:
    case kRiscvI64x2Shl:
    case kRiscvI64x2ShrS:
    case kRiscvI64x2ShrU:
    case kRiscvF32x4Abs:
    case kRiscvF32x4Add:
    case kRiscvF32x4AddHoriz:
    case kRiscvF32x4Eq:
    case kRiscvF32x4ExtractLane:
    case kRiscvF32x4Lt:
    case kRiscvF32x4Le:
    case kRiscvF32x4Max:
    case kRiscvF32x4Min:
    case kRiscvF32x4Mul:
    case kRiscvF32x4Div:
    case kRiscvF32x4Ne:
    case kRiscvF32x4Neg:
    case kRiscvF32x4Sqrt:
    case kRiscvF32x4RecipApprox:
    case kRiscvF32x4RecipSqrtApprox:
    case kRiscvF32x4ReplaceLane:
    case kRiscvF32x4SConvertI32x4:
    case kRiscvF32x4Splat:
    case kRiscvF32x4Sub:
    case kRiscvF32x4UConvertI32x4:
    case kRiscvF64x2Splat:
    case kRiscvF64x2ExtractLane:
    case kRiscvF64x2ReplaceLane:
    case kRiscvFloat32Max:
    case kRiscvFloat32Min:
    case kRiscvFloat32RoundDown:
    case kRiscvFloat32RoundTiesEven:
    case kRiscvFloat32RoundTruncate:
    case kRiscvFloat32RoundUp:
    case kRiscvFloat64ExtractLowWord32:
    case kRiscvFloat64ExtractHighWord32:
    case kRiscvFloat64InsertLowWord32:
    case kRiscvFloat64InsertHighWord32:
    case kRiscvFloat64Max:
    case kRiscvFloat64Min:
    case kRiscvFloat64RoundDown:
    case kRiscvFloat64RoundTiesEven:
    case kRiscvFloat64RoundTruncate:
    case kRiscvFloat64RoundUp:
    case kRiscvFloat64SilenceNaN:
    case kRiscvFloorWD:
    case kRiscvFloorWS:
    case kRiscvI16x8Add:
    case kRiscvI16x8AddHoriz:
    case kRiscvI16x8AddSaturateS:
    case kRiscvI16x8AddSaturateU:
    case kRiscvI16x8Eq:
    case kRiscvI16x8ExtractLaneU:
    case kRiscvI16x8ExtractLaneS:
    case kRiscvI16x8GeS:
    case kRiscvI16x8GeU:
    case kRiscvI16x8GtS:
    case kRiscvI16x8GtU:
    case kRiscvI16x8MaxS:
    case kRiscvI16x8MaxU:
    case kRiscvI16x8MinS:
    case kRiscvI16x8MinU:
    case kRiscvI16x8Mul:
    case kRiscvI16x8Ne:
    case kRiscvI16x8Neg:
    case kRiscvI16x8ReplaceLane:
    case kRiscvI8x16SConvertI16x8:
    case kRiscvI16x8SConvertI32x4:
    case kRiscvI16x8SConvertI8x16High:
    case kRiscvI16x8SConvertI8x16Low:
    case kRiscvI16x8Shl:
    case kRiscvI16x8ShrS:
    case kRiscvI16x8ShrU:
    case kRiscvI16x8Splat:
    case kRiscvI16x8Sub:
    case kRiscvI16x8SubSaturateS:
    case kRiscvI16x8SubSaturateU:
    case kRiscvI8x16UConvertI16x8:
    case kRiscvI16x8UConvertI32x4:
    case kRiscvI16x8UConvertI8x16High:
    case kRiscvI16x8UConvertI8x16Low:
    case kRiscvI16x8RoundingAverageU:
    case kRiscvI32x4Add:
    case kRiscvI32x4AddHoriz:
    case kRiscvI32x4Eq:
    case kRiscvI32x4ExtractLane:
    case kRiscvI32x4GeS:
    case kRiscvI32x4GeU:
    case kRiscvI32x4GtS:
    case kRiscvI32x4GtU:
    case kRiscvI32x4MaxS:
    case kRiscvI32x4MaxU:
    case kRiscvI32x4MinS:
    case kRiscvI32x4MinU:
    case kRiscvI32x4Mul:
    case kRiscvI32x4Ne:
    case kRiscvI32x4Neg:
    case kRiscvI32x4ReplaceLane:
    case kRiscvI32x4SConvertF32x4:
    case kRiscvI32x4SConvertI16x8High:
    case kRiscvI32x4SConvertI16x8Low:
    case kRiscvI32x4Shl:
    case kRiscvI32x4ShrS:
    case kRiscvI32x4ShrU:
    case kRiscvI32x4Splat:
    case kRiscvI32x4Sub:
    case kRiscvI32x4UConvertF32x4:
    case kRiscvI32x4UConvertI16x8High:
    case kRiscvI32x4UConvertI16x8Low:
    case kRiscvI8x16Add:
    case kRiscvI8x16AddSaturateS:
    case kRiscvI8x16AddSaturateU:
    case kRiscvI8x16Eq:
    case kRiscvI8x16ExtractLaneU:
    case kRiscvI8x16ExtractLaneS:
    case kRiscvI8x16GeS:
    case kRiscvI8x16GeU:
    case kRiscvI8x16GtS:
    case kRiscvI8x16GtU:
    case kRiscvI8x16MaxS:
    case kRiscvI8x16MaxU:
    case kRiscvI8x16MinS:
    case kRiscvI8x16MinU:
    case kRiscvI8x16Mul:
    case kRiscvI8x16Ne:
    case kRiscvI8x16Neg:
    case kRiscvI8x16ReplaceLane:
    case kRiscvI8x16Shl:
    case kRiscvI8x16ShrS:
    case kRiscvI8x16ShrU:
    case kRiscvI8x16Splat:
    case kRiscvI8x16Sub:
    case kRiscvI8x16SubSaturateS:
    case kRiscvI8x16SubSaturateU:
    case kRiscvI8x16RoundingAverageU:
    case kRiscvIns:
    case kRiscvLsa:
    case kRiscvMaxD:
    case kRiscvMaxS:
    case kRiscvMinD:
    case kRiscvMinS:
    case kRiscvMod:
    case kRiscvModU:
    case kRiscvMov:
    case kRiscvMul:
    case kRiscvMulD:
    case kRiscvMulHigh:
    case kRiscvMulOvf:
    case kRiscvMulS:
    case kRiscvNegD:
    case kRiscvNegS:
    case kRiscvNor:
    case kRiscvNor32:
    case kRiscvOr:
    case kRiscvOr32:
    case kRiscvPopcnt:
    case kRiscvRor:
    case kRiscvRoundWD:
    case kRiscvRoundWS:
    case kRiscvS128And:
    case kRiscvS128Or:
    case kRiscvS128Not:
    case kRiscvS128Select:
    case kRiscvS128Xor:
    case kRiscvS128Zero:
    case kRiscvS16x8InterleaveEven:
    case kRiscvS16x8InterleaveOdd:
    case kRiscvS16x8InterleaveLeft:
    case kRiscvS16x8InterleaveRight:
    case kRiscvS16x8PackEven:
    case kRiscvS16x8PackOdd:
    case kRiscvS16x2Reverse:
    case kRiscvS16x4Reverse:
    case kRiscvS1x16AllTrue:
    case kRiscvS1x16AnyTrue:
    case kRiscvS1x4AllTrue:
    case kRiscvS1x4AnyTrue:
    case kRiscvS1x8AllTrue:
    case kRiscvS1x8AnyTrue:
    case kRiscvS32x4InterleaveEven:
    case kRiscvS32x4InterleaveOdd:
    case kRiscvS32x4InterleaveLeft:
    case kRiscvS32x4InterleaveRight:
    case kRiscvS32x4PackEven:
    case kRiscvS32x4PackOdd:
    case kRiscvS32x4Shuffle:
    case kRiscvS8x16Concat:
    case kRiscvS8x16InterleaveEven:
    case kRiscvS8x16InterleaveOdd:
    case kRiscvS8x16InterleaveLeft:
    case kRiscvS8x16InterleaveRight:
    case kRiscvS8x16PackEven:
    case kRiscvS8x16PackOdd:
    case kRiscvS8x2Reverse:
    case kRiscvS8x4Reverse:
    case kRiscvS8x8Reverse:
    case kRiscvS8x16Shuffle:
    case kRiscvS8x16Swizzle:
    case kRiscvSar:
    case kRiscvSeb:
    case kRiscvSeh:
    case kRiscvShl:
    case kRiscvShr:
    case kRiscvSqrtD:
    case kRiscvSqrtS:
    case kRiscvSub:
    case kRiscvSubD:
    case kRiscvSubS:
    case kRiscvTruncLD:
    case kRiscvTruncLS:
    case kRiscvTruncUlD:
    case kRiscvTruncUlS:
    case kRiscvTruncUwD:
    case kRiscvTruncUwS:
    case kRiscvTruncWD:
    case kRiscvTruncWS:
    case kRiscvTst:
    case kRiscvXor:
    case kRiscvXor32:
      return kNoOpcodeFlags;

    case kRiscvLb:
    case kRiscvLbu:
    case kRiscvLd:
    case kRiscvLdc1:
    case kRiscvLh:
    case kRiscvLhu:
    case kRiscvLw:
    case kRiscvLwc1:
    case kRiscvLwu:
    case kRiscvMsaLd:
    case kRiscvPeek:
    case kRiscvUld:
    case kRiscvUldc1:
    case kRiscvUlh:
    case kRiscvUlhu:
    case kRiscvUlw:
    case kRiscvUlwu:
    case kRiscvUlwc1:
    case kRiscvS8x16LoadSplat:
    case kRiscvS16x8LoadSplat:
    case kRiscvS32x4LoadSplat:
    case kRiscvS64x2LoadSplat:
    case kRiscvI16x8Load8x8S:
    case kRiscvI16x8Load8x8U:
    case kRiscvI32x4Load16x4S:
    case kRiscvI32x4Load16x4U:
    case kRiscvI64x2Load32x2S:
    case kRiscvI64x2Load32x2U:
    case kRiscvWord64AtomicLoadUint8:
    case kRiscvWord64AtomicLoadUint16:
    case kRiscvWord64AtomicLoadUint32:
    case kRiscvWord64AtomicLoadUint64:

      return kIsLoadOperation;

    case kRiscvModD:
    case kRiscvModS:
    case kRiscvMsaSt:
    case kRiscvPush:
    case kRiscvSb:
    case kRiscvSd:
    case kRiscvSdc1:
    case kRiscvSh:
    case kRiscvStackClaim:
    case kRiscvStoreToStackSlot:
    case kRiscvSw:
    case kRiscvSwc1:
    case kRiscvUsd:
    case kRiscvUsdc1:
    case kRiscvUsh:
    case kRiscvUsw:
    case kRiscvUswc1:
    case kRiscvSync:
    case kRiscvWord64AtomicStoreWord8:
    case kRiscvWord64AtomicStoreWord16:
    case kRiscvWord64AtomicStoreWord32:
    case kRiscvWord64AtomicStoreWord64:
    case kRiscvWord64AtomicAddUint8:
    case kRiscvWord64AtomicAddUint16:
    case kRiscvWord64AtomicAddUint32:
    case kRiscvWord64AtomicAddUint64:
    case kRiscvWord64AtomicSubUint8:
    case kRiscvWord64AtomicSubUint16:
    case kRiscvWord64AtomicSubUint32:
    case kRiscvWord64AtomicSubUint64:
    case kRiscvWord64AtomicAndUint8:
    case kRiscvWord64AtomicAndUint16:
    case kRiscvWord64AtomicAndUint32:
    case kRiscvWord64AtomicAndUint64:
    case kRiscvWord64AtomicOrUint8:
    case kRiscvWord64AtomicOrUint16:
    case kRiscvWord64AtomicOrUint32:
    case kRiscvWord64AtomicOrUint64:
    case kRiscvWord64AtomicXorUint8:
    case kRiscvWord64AtomicXorUint16:
    case kRiscvWord64AtomicXorUint32:
    case kRiscvWord64AtomicXorUint64:
    case kRiscvWord64AtomicExchangeUint8:
    case kRiscvWord64AtomicExchangeUint16:
    case kRiscvWord64AtomicExchangeUint32:
    case kRiscvWord64AtomicExchangeUint64:
    case kRiscvWord64AtomicCompareExchangeUint8:
    case kRiscvWord64AtomicCompareExchangeUint16:
    case kRiscvWord64AtomicCompareExchangeUint32:
    case kRiscvWord64AtomicCompareExchangeUint64:
      return kHasSideEffect;

#define CASE(Name) case k##Name:
      COMMON_ARCH_OPCODE_LIST(CASE)
#undef CASE
      // Already covered in architecture independent code.
      UNREACHABLE();
  }

  UNREACHABLE();
}

enum Latency {
  BRANCH = 4,  // Estimated max.
  RINT_S = 4,  // Estimated.
  RINT_D = 4,  // Estimated.

  MULT = 4,
  MULTU = 4,
  DMULT = 4,
  DMULTU = 4,

  MUL = 7,
  DMUL = 7,
  MUH = 7,
  MUHU = 7,
  DMUH = 7,
  DMUHU = 7,

  DIV = 50,  // Min:11 Max:50
  DDIV = 50,
  DIVU = 50,
  DDIVU = 50,

  ABS_S = 4,
  ABS_D = 4,
  NEG_S = 4,
  NEG_D = 4,
  ADD_S = 4,
  ADD_D = 4,
  SUB_S = 4,
  SUB_D = 4,
  MAX_S = 4,  // Estimated.
  MIN_S = 4,
  MAX_D = 4,  // Estimated.
  MIN_D = 4,
  C_cond_S = 4,
  C_cond_D = 4,
  MUL_S = 4,

  MADD_S = 4,
  MSUB_S = 4,
  NMADD_S = 4,
  NMSUB_S = 4,

  CABS_cond_S = 4,
  CABS_cond_D = 4,

  CVT_D_S = 4,
  CVT_PS_PW = 4,

  CVT_S_W = 4,
  CVT_S_L = 4,
  CVT_D_W = 4,
  CVT_D_L = 4,

  CVT_S_D = 4,

  CVT_W_S = 4,
  CVT_W_D = 4,
  CVT_L_S = 4,
  CVT_L_D = 4,

  CEIL_W_S = 4,
  CEIL_W_D = 4,
  CEIL_L_S = 4,
  CEIL_L_D = 4,

  FLOOR_W_S = 4,
  FLOOR_W_D = 4,
  FLOOR_L_S = 4,
  FLOOR_L_D = 4,

  ROUND_W_S = 4,
  ROUND_W_D = 4,
  ROUND_L_S = 4,
  ROUND_L_D = 4,

  TRUNC_W_S = 4,
  TRUNC_W_D = 4,
  TRUNC_L_S = 4,
  TRUNC_L_D = 4,

  MOV_S = 4,
  MOV_D = 4,

  MOVF_S = 4,
  MOVF_D = 4,

  MOVN_S = 4,
  MOVN_D = 4,

  MOVT_S = 4,
  MOVT_D = 4,

  MOVZ_S = 4,
  MOVZ_D = 4,

  MUL_D = 5,
  MADD_D = 5,
  MSUB_D = 5,
  NMADD_D = 5,
  NMSUB_D = 5,

  RECIP_S = 13,
  RECIP_D = 26,

  RSQRT_S = 17,
  RSQRT_D = 36,

  DIV_S = 17,
  SQRT_S = 17,

  DIV_D = 32,
  SQRT_D = 32,

  MTC1 = 4,
  MTHC1 = 4,
  DMTC1 = 4,
  LWC1 = 4,
  LDC1 = 4,

  MFC1 = 1,
  MFHC1 = 1,
  DMFC1 = 1,
  MFHI = 1,
  MFLO = 1,
  SWC1 = 1,
  SDC1 = 1,
};

int DadduLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return 1;
  } else {
    return 2;  // Estimated max.
  }
}

int DsubuLatency(bool is_operand_register = true) {
  return DadduLatency(is_operand_register);
}

int AndLatency(bool is_operand_register = true) {
  return DadduLatency(is_operand_register);
}

int OrLatency(bool is_operand_register = true) {
  return DadduLatency(is_operand_register);
}

int NorLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return 1;
  } else {
    return 2;  // Estimated max.
  }
}

int XorLatency(bool is_operand_register = true) {
  return DadduLatency(is_operand_register);
}

int MulLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return Latency::MUL;
  } else {
    return Latency::MUL + 1;
  }
}

int DmulLatency(bool is_operand_register = true) {
  int latency = Latency::DMULT + Latency::MFLO;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int MulhLatency(bool is_operand_register = true) {
  int latency = Latency::MULT + Latency::MFHI;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int MulhuLatency(bool is_operand_register = true) {
  int latency = Latency::MULTU + Latency::MFHI;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int DMulhLatency(bool is_operand_register = true) {
  int latency = Latency::DMULT + Latency::MFHI;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int DivLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return Latency::DIV;
  } else {
    return Latency::DIV + 1;
  }
}

int DivuLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return Latency::DIVU;
  } else {
    return Latency::DIVU + 1;
  }
}

int DdivLatency(bool is_operand_register = true) {
  int latency = Latency::DDIV + Latency::MFLO;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int DdivuLatency(bool is_operand_register = true) {
  int latency = Latency::DDIVU + Latency::MFLO;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int ModLatency(bool is_operand_register = true) {
  int latency = Latency::DIV + Latency::MFHI;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int ModuLatency(bool is_operand_register = true) {
  int latency = Latency::DIVU + Latency::MFHI;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int DmodLatency(bool is_operand_register = true) {
  int latency = Latency::DDIV + Latency::MFHI;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int DmoduLatency(bool is_operand_register = true) {
  int latency = Latency::DDIV + Latency::MFHI;
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int MovzLatency() { return 1; }

int MovnLatency() { return 1; }

int DlsaLatency() {
  // Estimated max.
  return DadduLatency() + 1;
}

int CallLatency() {
  // Estimated.
  return DadduLatency(false) + Latency::BRANCH + 5;
}

int JumpLatency() {
  // Estimated max.
  return 1 + DadduLatency() + Latency::BRANCH + 2;
}

int SmiUntagLatency() { return 1; }

int PrepareForTailCallLatency() {
  // Estimated max.
  return 2 * (DlsaLatency() + DadduLatency(false)) + 2 + Latency::BRANCH +
         Latency::BRANCH + 2 * DsubuLatency(false) + 2 + Latency::BRANCH + 1;
}

int AssemblePopArgumentsAdoptFrameLatency() {
  return 1 + Latency::BRANCH + 1 + SmiUntagLatency() +
         PrepareForTailCallLatency();
}

int AssertLatency() { return 1; }

int PrepareCallCFunctionLatency() {
  int frame_alignment = TurboAssembler::ActivationFrameAlignment();
  if (frame_alignment > kSystemPointerSize) {
    return 1 + DsubuLatency(false) + AndLatency(false) + 1;
  } else {
    return DsubuLatency(false);
  }
}

int AdjustBaseAndOffsetLatency() {
  return 3;  // Estimated max.
}

int AlignedMemoryLatency() { return AdjustBaseAndOffsetLatency() + 1; }

int UlhuLatency() {
  return AdjustBaseAndOffsetLatency() + 2 * AlignedMemoryLatency() + 2;
}

int UlwLatency() {
  // Estimated max.
  return AdjustBaseAndOffsetLatency() + 3;
}

int UlwuLatency() { return UlwLatency() + 1; }

int UldLatency() {
  // Estimated max.
  return AdjustBaseAndOffsetLatency() + 3;
}

int Ulwc1Latency() { return UlwLatency() + Latency::MTC1; }

int Uldc1Latency() { return UldLatency() + Latency::DMTC1; }

int UshLatency() {
  // Estimated max.
  return AdjustBaseAndOffsetLatency() + 2 + 2 * AlignedMemoryLatency();
}

int UswLatency() { return AdjustBaseAndOffsetLatency() + 2; }

int UsdLatency() { return AdjustBaseAndOffsetLatency() + 2; }

int Uswc1Latency() { return Latency::MFC1 + UswLatency(); }

int Usdc1Latency() { return Latency::DMFC1 + UsdLatency(); }

int Lwc1Latency() { return AdjustBaseAndOffsetLatency() + Latency::LWC1; }

int Swc1Latency() { return AdjustBaseAndOffsetLatency() + Latency::SWC1; }

int Sdc1Latency() { return AdjustBaseAndOffsetLatency() + Latency::SDC1; }

int Ldc1Latency() { return AdjustBaseAndOffsetLatency() + Latency::LDC1; }

int MultiPushLatency() {
  int latency = DsubuLatency(false);
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    latency++;
  }
  return latency;
}

int MultiPushFPULatency() {
  int latency = DsubuLatency(false);
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    latency += Sdc1Latency();
  }
  return latency;
}

int PushCallerSavedLatency(SaveFPRegsMode fp_mode) {
  int latency = MultiPushLatency();
  if (fp_mode == kSaveFPRegs) {
    latency += MultiPushFPULatency();
  }
  return latency;
}

int MultiPopLatency() {
  int latency = DadduLatency(false);
  for (int16_t i = 0; i < kNumRegisters; i++) {
    latency++;
  }
  return latency;
}

int MultiPopFPULatency() {
  int latency = DadduLatency(false);
  for (int16_t i = 0; i < kNumRegisters; i++) {
    latency += Ldc1Latency();
  }
  return latency;
}

int PopCallerSavedLatency(SaveFPRegsMode fp_mode) {
  int latency = MultiPopLatency();
  if (fp_mode == kSaveFPRegs) {
    latency += MultiPopFPULatency();
  }
  return latency;
}

int CallCFunctionHelperLatency() {
  // Estimated.
  int latency = AndLatency(false) + Latency::BRANCH + 2 + CallLatency();
  if (base::OS::ActivationFrameAlignment() > kSystemPointerSize) {
    latency++;
  } else {
    latency += DadduLatency(false);
  }
  return latency;
}

int CallCFunctionLatency() { return 1 + CallCFunctionHelperLatency(); }

int AssembleArchJumpLatency() {
  // Estimated max.
  return Latency::BRANCH;
}

int AssembleArchLookupSwitchLatency(const Instruction* instr) {
  int latency = 0;
  for (size_t index = 2; index < instr->InputCount(); index += 2) {
    latency += 1 + Latency::BRANCH;
  }
  return latency + AssembleArchJumpLatency();
}

int GenerateSwitchTableLatency() {
  int latency = 6;
  latency += 2;
  return latency;
}

int AssembleArchTableSwitchLatency() {
  return Latency::BRANCH + GenerateSwitchTableLatency();
}

int DropAndRetLatency() {
  // Estimated max.
  return DadduLatency(false) + JumpLatency();
}

int AssemblerReturnLatency() {
  // Estimated max.
  return DadduLatency(false) + MultiPopLatency() + MultiPopFPULatency() +
         Latency::BRANCH + DadduLatency() + 1 + DropAndRetLatency();
}

int TryInlineTruncateDoubleToILatency() {
  return 2 + Latency::TRUNC_W_D + Latency::MFC1 + 2 + AndLatency(false) +
         Latency::BRANCH;
}

int CallStubDelayedLatency() { return 1 + CallLatency(); }

int TruncateDoubleToIDelayedLatency() {
  // TODO(riscv): This no longer reflects how TruncateDoubleToI is called.
  return TryInlineTruncateDoubleToILatency() + 1 + DsubuLatency(false) +
         Sdc1Latency() + CallStubDelayedLatency() + DadduLatency(false) + 1;
}

int CheckPageFlagLatency() {
  return AndLatency(false) + AlignedMemoryLatency() + AndLatency(false) +
         Latency::BRANCH;
}

int SltuLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return 1;
  } else {
    return 2;  // Estimated max.
  }
}

int BranchShortHelperLatency() {
  return SltuLatency() + 2;  // Estimated max.
}

int BranchShortLatency() { return BranchShortHelperLatency(); }

int MoveLatency() { return 1; }

int MovToFloatParametersLatency() { return 2 * MoveLatency(); }

int MovFromFloatResultLatency() { return MoveLatency(); }

int DaddOverflowLatency() {
  // Estimated max.
  return 6;
}

int DsubOverflowLatency() {
  // Estimated max.
  return 6;
}

int MulOverflowLatency() {
  // Estimated max.
  return MulLatency() + MulhLatency() + 2;
}

int DclzLatency() { return 1; }

int CtzLatency() {
  return DadduLatency(false) + XorLatency() + AndLatency() + DclzLatency() + 1 +
         DsubuLatency();
}

int DctzLatency() {
  return DadduLatency(false) + XorLatency() + AndLatency() + 1 + DsubuLatency();
}

int PopcntLatency() {
  return 2 + AndLatency() + DsubuLatency() + 1 + AndLatency() + 1 +
         AndLatency() + DadduLatency() + 1 + DadduLatency() + 1 + AndLatency() +
         1 + MulLatency() + 1;
}

int DpopcntLatency() {
  return 2 + AndLatency() + DsubuLatency() + 1 + AndLatency() + 1 +
         AndLatency() + DadduLatency() + 1 + DadduLatency() + 1 + AndLatency() +
         1 + DmulLatency() + 1;
}

int CompareFLatency() { return Latency::C_cond_S; }

int CompareF32Latency() { return CompareFLatency(); }

int CompareF64Latency() { return CompareFLatency(); }

int CompareIsNanFLatency() { return CompareFLatency(); }

int CompareIsNanF32Latency() { return CompareIsNanFLatency(); }

int CompareIsNanF64Latency() { return CompareIsNanFLatency(); }

int NegsLatency() {
  // Estimated.
  return CompareIsNanF32Latency() + 2 * Latency::BRANCH + Latency::NEG_S +
         Latency::MFC1 + 1 + XorLatency() + Latency::MTC1;
}

int NegdLatency() {
  // Estimated.
  return CompareIsNanF64Latency() + 2 * Latency::BRANCH + Latency::NEG_D +
         Latency::DMFC1 + 1 + XorLatency() + Latency::DMTC1;
}

int Float64RoundLatency() {
  // For ceil_l_d, floor_l_d, round_l_d, trunc_l_d latency is 4.
  return Latency::DMFC1 + 1 + Latency::BRANCH + Latency::MOV_D + 4 +
         Latency::DMFC1 + Latency::BRANCH + Latency::CVT_D_L + 2 +
         Latency::MTHC1;
}

int Float32RoundLatency() {
  // For ceil_w_s, floor_w_s, round_w_s, trunc_w_s latency is 4.
  return Latency::MFC1 + 1 + Latency::BRANCH + Latency::MOV_S + 4 +
         Latency::MFC1 + Latency::BRANCH + Latency::CVT_S_W + 2 + Latency::MTC1;
}

int Float32MaxLatency() {
  // Estimated max.
  int latency = CompareIsNanF32Latency() + Latency::BRANCH;
  return latency + 5 * Latency::BRANCH + 2 * CompareF32Latency() +
         Latency::MFC1 + 1 + Latency::MOV_S;
}

int Float64MaxLatency() {
  // Estimated max.
  int latency = CompareIsNanF64Latency() + Latency::BRANCH;
  return latency + 5 * Latency::BRANCH + 2 * CompareF64Latency() +
         Latency::DMFC1 + Latency::MOV_D;
}

int Float32MinLatency() {
  // Estimated max.
  int latency = CompareIsNanF32Latency() + Latency::BRANCH;
  return latency + 5 * Latency::BRANCH + 2 * CompareF32Latency() +
         Latency::MFC1 + 1 + Latency::MOV_S;
}

int Float64MinLatency() {
  // Estimated max.
  int latency = CompareIsNanF64Latency() + Latency::BRANCH;
  return latency + 5 * Latency::BRANCH + 2 * CompareF32Latency() +
         Latency::DMFC1 + Latency::MOV_D;
}

int TruncLSLatency(bool load_status) {
  int latency = Latency::TRUNC_L_S + Latency::DMFC1;
  if (load_status) {
    latency += SltuLatency() + 7;
  }
  return latency;
}

int TruncLDLatency(bool load_status) {
  int latency = Latency::TRUNC_L_D + Latency::DMFC1;
  if (load_status) {
    latency += SltuLatency() + 7;
  }
  return latency;
}

int TruncUlSLatency() {
  // Estimated max.
  return 2 * CompareF32Latency() + CompareIsNanF32Latency() +
         4 * Latency::BRANCH + Latency::SUB_S + 2 * Latency::TRUNC_L_S +
         3 * Latency::DMFC1 + OrLatency() + Latency::MTC1 + Latency::MOV_S +
         SltuLatency() + 4;
}

int TruncUlDLatency() {
  // Estimated max.
  return 2 * CompareF64Latency() + CompareIsNanF64Latency() +
         4 * Latency::BRANCH + Latency::SUB_D + 2 * Latency::TRUNC_L_D +
         3 * Latency::DMFC1 + OrLatency() + Latency::DMTC1 + Latency::MOV_D +
         SltuLatency() + 4;
}

int PushLatency() { return DadduLatency() + AlignedMemoryLatency(); }

int ByteSwapSignedLatency() { return 2; }

int LlLatency(int offset) {
  bool is_one_instruction = is_int12(offset);
  if (is_one_instruction) {
    return 1;
  } else {
    return 3;
  }
}

int ExtractBitsLatency(bool sign_extend, int size) {
  int latency = 2;
  if (sign_extend) {
    switch (size) {
      case 8:
      case 16:
      case 32:
        latency += 1;
        break;
      default:
        UNREACHABLE();
    }
  }
  return latency;
}

int InsertBitsLatency() { return 2 + DsubuLatency(false) + 2; }

int ScLatency(int offset) { return 3; }

int Word32AtomicExchangeLatency(bool sign_extend, int size) {
  return DadduLatency(false) + 1 + DsubuLatency() + 2 + LlLatency(0) +
         ExtractBitsLatency(sign_extend, size) + InsertBitsLatency() +
         ScLatency(0) + BranchShortLatency() + 1;
}

int Word32AtomicCompareExchangeLatency(bool sign_extend, int size) {
  return 2 + DsubuLatency() + 2 + LlLatency(0) +
         ExtractBitsLatency(sign_extend, size) + InsertBitsLatency() +
         ScLatency(0) + BranchShortLatency() + 1;
}

int InstructionScheduler::GetInstructionLatency(const Instruction* instr) {
  // FIXME(RISCV): Verify these latencies for RISC-V (currently using MIPS numbers)
  switch (instr->arch_opcode()) {
    case kArchCallCodeObject:
    case kArchCallWasmFunction:
      return CallLatency();
    case kArchTailCallCodeObjectFromJSFunction:
    case kArchTailCallCodeObject: {
      int latency = 0;
      if (instr->arch_opcode() == kArchTailCallCodeObjectFromJSFunction) {
        latency = AssemblePopArgumentsAdoptFrameLatency();
      }
      return latency + JumpLatency();
    }
    case kArchTailCallWasm:
    case kArchTailCallAddress:
      return JumpLatency();
    case kArchCallJSFunction: {
      int latency = 0;
      if (FLAG_debug_code) {
        latency = 1 + AssertLatency();
      }
      return latency + 1 + DadduLatency(false) + CallLatency();
    }
    case kArchPrepareCallCFunction:
      return PrepareCallCFunctionLatency();
    case kArchSaveCallerRegisters: {
      auto fp_mode =
          static_cast<SaveFPRegsMode>(MiscField::decode(instr->opcode()));
      return PushCallerSavedLatency(fp_mode);
    }
    case kArchRestoreCallerRegisters: {
      auto fp_mode =
          static_cast<SaveFPRegsMode>(MiscField::decode(instr->opcode()));
      return PopCallerSavedLatency(fp_mode);
    }
    case kArchPrepareTailCall:
      return 2;
    case kArchCallCFunction:
      return CallCFunctionLatency();
    case kArchJmp:
      return AssembleArchJumpLatency();
    case kArchLookupSwitch:
      return AssembleArchLookupSwitchLatency(instr);
    case kArchTableSwitch:
      return AssembleArchTableSwitchLatency();
    case kArchAbortCSAAssert:
      return CallLatency() + 1;
    case kArchDebugBreak:
      return 1;
    case kArchComment:
    case kArchNop:
    case kArchThrowTerminator:
    case kArchDeoptimize:
      return 0;
    case kArchRet:
      return AssemblerReturnLatency();
    case kArchFramePointer:
      return 1;
    case kArchParentFramePointer:
      // Estimated max.
      return AlignedMemoryLatency();
    case kArchTruncateDoubleToI:
      return TruncateDoubleToIDelayedLatency();
    case kArchStoreWithWriteBarrier:
      return DadduLatency() + 1 + CheckPageFlagLatency();
    case kArchStackSlot:
      // Estimated max.
      return DadduLatency(false) + AndLatency(false) + AssertLatency() +
             DadduLatency(false) + AndLatency(false) + BranchShortLatency() +
             1 + DsubuLatency() + DadduLatency();
    case kArchWordPoisonOnSpeculation:
      return AndLatency();
    case kIeee754Float64Acos:
    case kIeee754Float64Acosh:
    case kIeee754Float64Asin:
    case kIeee754Float64Asinh:
    case kIeee754Float64Atan:
    case kIeee754Float64Atanh:
    case kIeee754Float64Atan2:
    case kIeee754Float64Cos:
    case kIeee754Float64Cosh:
    case kIeee754Float64Cbrt:
    case kIeee754Float64Exp:
    case kIeee754Float64Expm1:
    case kIeee754Float64Log:
    case kIeee754Float64Log1p:
    case kIeee754Float64Log10:
    case kIeee754Float64Log2:
    case kIeee754Float64Pow:
    case kIeee754Float64Sin:
    case kIeee754Float64Sinh:
    case kIeee754Float64Tan:
    case kIeee754Float64Tanh:
      return PrepareCallCFunctionLatency() + MovToFloatParametersLatency() +
             CallCFunctionLatency() + MovFromFloatResultLatency();
    case kRiscvAdd:
    case kRiscvDadd:
      return DadduLatency(instr->InputAt(1)->IsRegister());
    case kRiscvDaddOvf:
      return DaddOverflowLatency();
    case kRiscvSub:
    case kRiscvDsub:
      return DsubuLatency(instr->InputAt(1)->IsRegister());
    case kRiscvDsubOvf:
      return DsubOverflowLatency();
    case kRiscvMul:
      return MulLatency();
    case kRiscvMulOvf:
      return MulOverflowLatency();
    case kRiscvMulHigh:
      return MulhLatency();
    case kRiscvMulHighU:
      return MulhuLatency();
    case kRiscvDMulHigh:
      return DMulhLatency();
    case kRiscvDiv: {
      int latency = DivLatency(instr->InputAt(1)->IsRegister());
      return latency + MovzLatency();
    }
    case kRiscvDivU: {
      int latency = DivuLatency(instr->InputAt(1)->IsRegister());
      return latency + MovzLatency();
    }
    case kRiscvMod:
      return ModLatency();
    case kRiscvModU:
      return ModuLatency();
    case kRiscvDmul:
      return DmulLatency();
    case kRiscvDdiv: {
      int latency = DdivLatency();
      return latency + MovzLatency();
    }
    case kRiscvDdivU: {
      int latency = DdivuLatency();
      return latency + MovzLatency();
    }
    case kRiscvDmod:
      return DmodLatency();
    case kRiscvDmodU:
      return DmoduLatency();
    case kRiscvDlsa:
    case kRiscvLsa:
      return DlsaLatency();
    case kRiscvAnd:
      return AndLatency(instr->InputAt(1)->IsRegister());
    case kRiscvAnd32: {
      bool is_operand_register = instr->InputAt(1)->IsRegister();
      int latency = AndLatency(is_operand_register);
      if (is_operand_register) {
        return latency + 2;
      } else {
        return latency + 1;
      }
    }
    case kRiscvOr:
      return OrLatency(instr->InputAt(1)->IsRegister());
    case kRiscvOr32: {
      bool is_operand_register = instr->InputAt(1)->IsRegister();
      int latency = OrLatency(is_operand_register);
      if (is_operand_register) {
        return latency + 2;
      } else {
        return latency + 1;
      }
    }
    case kRiscvNor:
      return NorLatency(instr->InputAt(1)->IsRegister());
    case kRiscvNor32: {
      bool is_operand_register = instr->InputAt(1)->IsRegister();
      int latency = NorLatency(is_operand_register);
      if (is_operand_register) {
        return latency + 2;
      } else {
        return latency + 1;
      }
    }
    case kRiscvXor:
      return XorLatency(instr->InputAt(1)->IsRegister());
    case kRiscvXor32: {
      bool is_operand_register = instr->InputAt(1)->IsRegister();
      int latency = XorLatency(is_operand_register);
      if (is_operand_register) {
        return latency + 2;
      } else {
        return latency + 1;
      }
    }
    case kRiscvClz:
    case kRiscvDclz:
      return DclzLatency();
    case kRiscvCtz:
      return CtzLatency();
    case kRiscvDctz:
      return DctzLatency();
    case kRiscvPopcnt:
      return PopcntLatency();
    case kRiscvDpopcnt:
      return DpopcntLatency();
    case kRiscvShl:
      return 1;
    case kRiscvShr:
    case kRiscvSar:
      return 2;
    case kRiscvExt:
    case kRiscvIns:
    case kRiscvDext:
    case kRiscvDins:
    case kRiscvDshl:
    case kRiscvDshr:
    case kRiscvDsar:
    case kRiscvRor:
    case kRiscvDror:
      return 1;
    case kRiscvTst:
      return AndLatency(instr->InputAt(1)->IsRegister());
    case kRiscvMov:
      return 1;
    case kRiscvCmpS:
      return MoveLatency() + CompareF32Latency();
    case kRiscvAddS:
      return Latency::ADD_S;
    case kRiscvSubS:
      return Latency::SUB_S;
    case kRiscvMulS:
      return Latency::MUL_S;
    case kRiscvDivS:
      return Latency::DIV_S;
    case kRiscvModS:
      return PrepareCallCFunctionLatency() + MovToFloatParametersLatency() +
             CallCFunctionLatency() + MovFromFloatResultLatency();
    case kRiscvAbsS:
      return Latency::ABS_S;
    case kRiscvNegS:
      return NegdLatency();
    case kRiscvSqrtS:
      return Latency::SQRT_S;
    case kRiscvMaxS:
      return Latency::MAX_S;
    case kRiscvMinS:
      return Latency::MIN_S;
    case kRiscvCmpD:
      return MoveLatency() + CompareF64Latency();
    case kRiscvAddD:
      return Latency::ADD_D;
    case kRiscvSubD:
      return Latency::SUB_D;
    case kRiscvMulD:
      return Latency::MUL_D;
    case kRiscvDivD:
      return Latency::DIV_D;
    case kRiscvModD:
      return PrepareCallCFunctionLatency() + MovToFloatParametersLatency() +
             CallCFunctionLatency() + MovFromFloatResultLatency();
    case kRiscvAbsD:
      return Latency::ABS_D;
    case kRiscvNegD:
      return NegdLatency();
    case kRiscvSqrtD:
      return Latency::SQRT_D;
    case kRiscvMaxD:
      return Latency::MAX_D;
    case kRiscvMinD:
      return Latency::MIN_D;
    case kRiscvFloat64RoundDown:
    case kRiscvFloat64RoundTruncate:
    case kRiscvFloat64RoundUp:
    case kRiscvFloat64RoundTiesEven:
      return Float64RoundLatency();
    case kRiscvFloat32RoundDown:
    case kRiscvFloat32RoundTruncate:
    case kRiscvFloat32RoundUp:
    case kRiscvFloat32RoundTiesEven:
      return Float32RoundLatency();
    case kRiscvFloat32Max:
      return Float32MaxLatency();
    case kRiscvFloat64Max:
      return Float64MaxLatency();
    case kRiscvFloat32Min:
      return Float32MinLatency();
    case kRiscvFloat64Min:
      return Float64MinLatency();
    case kRiscvFloat64SilenceNaN:
      return Latency::SUB_D;
    case kRiscvCvtSD:
      return Latency::CVT_S_D;
    case kRiscvCvtDS:
      return Latency::CVT_D_S;
    case kRiscvCvtDW:
      return Latency::MTC1 + Latency::CVT_D_W;
    case kRiscvCvtSW:
      return Latency::MTC1 + Latency::CVT_S_W;
    case kRiscvCvtSUw:
      return 1 + Latency::DMTC1 + Latency::CVT_S_L;
    case kRiscvCvtSL:
      return Latency::DMTC1 + Latency::CVT_S_L;
    case kRiscvCvtDL:
      return Latency::DMTC1 + Latency::CVT_D_L;
    case kRiscvCvtDUw:
      return 1 + Latency::DMTC1 + Latency::CVT_D_L;
    case kRiscvCvtDUl:
      return 2 * Latency::BRANCH + 3 + 2 * Latency::DMTC1 +
             2 * Latency::CVT_D_L + Latency::ADD_D;
    case kRiscvCvtSUl:
      return 2 * Latency::BRANCH + 3 + 2 * Latency::DMTC1 +
             2 * Latency::CVT_S_L + Latency::ADD_S;
    case kRiscvFloorWD:
      return Latency::FLOOR_W_D + Latency::MFC1;
    case kRiscvCeilWD:
      return Latency::CEIL_W_D + Latency::MFC1;
    case kRiscvRoundWD:
      return Latency::ROUND_W_D + Latency::MFC1;
    case kRiscvTruncWD:
      return Latency::TRUNC_W_D + Latency::MFC1;
    case kRiscvFloorWS:
      return Latency::FLOOR_W_S + Latency::MFC1;
    case kRiscvCeilWS:
      return Latency::CEIL_W_S + Latency::MFC1;
    case kRiscvRoundWS:
      return Latency::ROUND_W_S + Latency::MFC1;
    case kRiscvTruncWS:
      return Latency::TRUNC_W_S + Latency::MFC1 + 2 + MovnLatency();
    case kRiscvTruncLS:
      return TruncLSLatency(instr->OutputCount() > 1);
    case kRiscvTruncLD:
      return TruncLDLatency(instr->OutputCount() > 1);
    case kRiscvTruncUwD:
      // Estimated max.
      return CompareF64Latency() + 2 * Latency::BRANCH +
             2 * Latency::TRUNC_W_D + Latency::SUB_D + OrLatency() +
             Latency::MTC1 + Latency::MFC1 + Latency::MTHC1 + 1;
    case kRiscvTruncUwS:
      // Estimated max.
      return CompareF32Latency() + 2 * Latency::BRANCH +
             2 * Latency::TRUNC_W_S + Latency::SUB_S + OrLatency() +
             Latency::MTC1 + 2 * Latency::MFC1 + 2 + MovzLatency();
    case kRiscvTruncUlS:
      return TruncUlSLatency();
    case kRiscvTruncUlD:
      return TruncUlDLatency();
    case kRiscvBitcastDL:
      return Latency::DMFC1;
    case kRiscvBitcastLD:
      return Latency::DMTC1;
    case kRiscvFloat64ExtractLowWord32:
      return Latency::MFC1;
    case kRiscvFloat64InsertLowWord32:
      return Latency::MFHC1 + Latency::MTC1 + Latency::MTHC1;
    case kRiscvFloat64ExtractHighWord32:
      return Latency::MFHC1;
    case kRiscvFloat64InsertHighWord32:
      return Latency::MTHC1;
    case kRiscvSeb:
    case kRiscvSeh:
      return 1;
    case kRiscvLbu:
    case kRiscvLb:
    case kRiscvLhu:
    case kRiscvLh:
    case kRiscvLwu:
    case kRiscvLw:
    case kRiscvLd:
    case kRiscvSb:
    case kRiscvSh:
    case kRiscvSw:
    case kRiscvSd:
      return AlignedMemoryLatency();
    case kRiscvLwc1:
      return Lwc1Latency();
    case kRiscvLdc1:
      return Ldc1Latency();
    case kRiscvSwc1:
      return Swc1Latency();
    case kRiscvSdc1:
      return Sdc1Latency();
    case kRiscvUlhu:
    case kRiscvUlh:
      return UlhuLatency();
    case kRiscvUlwu:
      return UlwuLatency();
    case kRiscvUlw:
      return UlwLatency();
    case kRiscvUld:
      return UldLatency();
    case kRiscvUlwc1:
      return Ulwc1Latency();
    case kRiscvUldc1:
      return Uldc1Latency();
    case kRiscvUsh:
      return UshLatency();
    case kRiscvUsw:
      return UswLatency();
    case kRiscvUsd:
      return UsdLatency();
    case kRiscvUswc1:
      return Uswc1Latency();
    case kRiscvUsdc1:
      return Usdc1Latency();
    case kRiscvPush: {
      int latency = 0;
      if (instr->InputAt(0)->IsFPRegister()) {
        latency = Sdc1Latency() + DsubuLatency(false);
      } else {
        latency = PushLatency();
      }
      return latency;
    }
    case kRiscvPeek: {
      int latency = 0;
      if (instr->OutputAt(0)->IsFPRegister()) {
        auto op = LocationOperand::cast(instr->OutputAt(0));
        switch (op->representation()) {
          case MachineRepresentation::kFloat64:
            latency = Ldc1Latency();
            break;
          case MachineRepresentation::kFloat32:
            latency = Latency::LWC1;
            break;
          default:
            UNREACHABLE();
        }
      } else {
        latency = AlignedMemoryLatency();
      }
      return latency;
    }
    case kRiscvStackClaim:
      return DsubuLatency(false);
    case kRiscvStoreToStackSlot: {
      int latency = 0;
      if (instr->InputAt(0)->IsFPRegister()) {
        if (instr->InputAt(0)->IsSimd128Register()) {
          latency = 1;  // Estimated value.
        } else {
          latency = Sdc1Latency();
        }
      } else {
        latency = AlignedMemoryLatency();
      }
      return latency;
    }
    case kRiscvByteSwap64:
      return ByteSwapSignedLatency();
    case kRiscvByteSwap32:
      return ByteSwapSignedLatency();
    case kWord32AtomicLoadInt8:
    case kWord32AtomicLoadUint8:
    case kWord32AtomicLoadInt16:
    case kWord32AtomicLoadUint16:
    case kWord32AtomicLoadWord32:
      return 2;
    case kWord32AtomicStoreWord8:
    case kWord32AtomicStoreWord16:
    case kWord32AtomicStoreWord32:
      return 3;
    case kWord32AtomicExchangeInt8:
      return Word32AtomicExchangeLatency(true, 8);
    case kWord32AtomicExchangeUint8:
      return Word32AtomicExchangeLatency(false, 8);
    case kWord32AtomicExchangeInt16:
      return Word32AtomicExchangeLatency(true, 16);
    case kWord32AtomicExchangeUint16:
      return Word32AtomicExchangeLatency(false, 16);
    case kWord32AtomicExchangeWord32:
      return 2 + LlLatency(0) + 1 + ScLatency(0) + BranchShortLatency() + 1;
    case kWord32AtomicCompareExchangeInt8:
      return Word32AtomicCompareExchangeLatency(true, 8);
    case kWord32AtomicCompareExchangeUint8:
      return Word32AtomicCompareExchangeLatency(false, 8);
    case kWord32AtomicCompareExchangeInt16:
      return Word32AtomicCompareExchangeLatency(true, 16);
    case kWord32AtomicCompareExchangeUint16:
      return Word32AtomicCompareExchangeLatency(false, 16);
    case kWord32AtomicCompareExchangeWord32:
      return 3 + LlLatency(0) + BranchShortLatency() + 1 + ScLatency(0) +
             BranchShortLatency() + 1;
    case kRiscvAssertEqual:
      return AssertLatency();
    default:
      return 1;
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
