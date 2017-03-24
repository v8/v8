// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/instruction-scheduler.h"

namespace v8 {
namespace internal {
namespace compiler {

bool InstructionScheduler::SchedulerSupported() { return true; }


int InstructionScheduler::GetTargetInstructionFlags(
    const Instruction* instr) const {
  switch (instr->arch_opcode()) {
    case kArmAdd:
    case kArmAnd:
    case kArmBic:
    case kArmClz:
    case kArmCmp:
    case kArmCmn:
    case kArmTst:
    case kArmTeq:
    case kArmOrr:
    case kArmEor:
    case kArmSub:
    case kArmRsb:
    case kArmMul:
    case kArmMla:
    case kArmMls:
    case kArmSmmul:
    case kArmSmull:
    case kArmSmmla:
    case kArmUmull:
    case kArmSdiv:
    case kArmUdiv:
    case kArmMov:
    case kArmMvn:
    case kArmBfc:
    case kArmUbfx:
    case kArmSbfx:
    case kArmSxtb:
    case kArmSxth:
    case kArmSxtab:
    case kArmSxtah:
    case kArmUxtb:
    case kArmUxth:
    case kArmUxtab:
    case kArmUxtah:
    case kArmRbit:
    case kArmAddPair:
    case kArmSubPair:
    case kArmMulPair:
    case kArmLslPair:
    case kArmLsrPair:
    case kArmAsrPair:
    case kArmVcmpF32:
    case kArmVaddF32:
    case kArmVsubF32:
    case kArmVmulF32:
    case kArmVmlaF32:
    case kArmVmlsF32:
    case kArmVdivF32:
    case kArmVabsF32:
    case kArmVnegF32:
    case kArmVsqrtF32:
    case kArmVcmpF64:
    case kArmVaddF64:
    case kArmVsubF64:
    case kArmVmulF64:
    case kArmVmlaF64:
    case kArmVmlsF64:
    case kArmVdivF64:
    case kArmVmodF64:
    case kArmVabsF64:
    case kArmVnegF64:
    case kArmVsqrtF64:
    case kArmVrintmF32:
    case kArmVrintmF64:
    case kArmVrintpF32:
    case kArmVrintpF64:
    case kArmVrintzF32:
    case kArmVrintzF64:
    case kArmVrintaF64:
    case kArmVrintnF32:
    case kArmVrintnF64:
    case kArmVcvtF32F64:
    case kArmVcvtF64F32:
    case kArmVcvtF32S32:
    case kArmVcvtF32U32:
    case kArmVcvtF64S32:
    case kArmVcvtF64U32:
    case kArmVcvtS32F32:
    case kArmVcvtU32F32:
    case kArmVcvtS32F64:
    case kArmVcvtU32F64:
    case kArmVmovU32F32:
    case kArmVmovF32U32:
    case kArmVmovLowU32F64:
    case kArmVmovLowF64U32:
    case kArmVmovHighU32F64:
    case kArmVmovHighF64U32:
    case kArmVmovF64U32U32:
    case kArmVmovU32U32F64:
    case kArmFloat32Max:
    case kArmFloat64Max:
    case kArmFloat32Min:
    case kArmFloat64Min:
    case kArmFloat64SilenceNaN:
    case kArmFloat32x4Splat:
    case kArmFloat32x4ExtractLane:
    case kArmFloat32x4ReplaceLane:
    case kArmFloat32x4FromInt32x4:
    case kArmFloat32x4FromUint32x4:
    case kArmFloat32x4Abs:
    case kArmFloat32x4Neg:
    case kArmFloat32x4RecipApprox:
    case kArmFloat32x4RecipSqrtApprox:
    case kArmFloat32x4Add:
    case kArmFloat32x4Sub:
    case kArmFloat32x4Mul:
    case kArmFloat32x4Min:
    case kArmFloat32x4Max:
    case kArmFloat32x4RecipRefine:
    case kArmFloat32x4RecipSqrtRefine:
    case kArmFloat32x4Equal:
    case kArmFloat32x4NotEqual:
    case kArmFloat32x4LessThan:
    case kArmFloat32x4LessThanOrEqual:
    case kArmInt32x4Splat:
    case kArmInt32x4ExtractLane:
    case kArmInt32x4ReplaceLane:
    case kArmInt32x4FromFloat32x4:
    case kArmUint32x4FromFloat32x4:
    case kArmInt32x4Neg:
    case kArmInt32x4ShiftLeftByScalar:
    case kArmInt32x4ShiftRightByScalar:
    case kArmInt32x4Add:
    case kArmInt32x4Sub:
    case kArmInt32x4Mul:
    case kArmInt32x4Min:
    case kArmInt32x4Max:
    case kArmInt32x4Equal:
    case kArmInt32x4NotEqual:
    case kArmInt32x4LessThan:
    case kArmInt32x4LessThanOrEqual:
    case kArmUint32x4ShiftRightByScalar:
    case kArmUint32x4Min:
    case kArmUint32x4Max:
    case kArmUint32x4LessThan:
    case kArmUint32x4LessThanOrEqual:
    case kArmInt16x8Splat:
    case kArmInt16x8ExtractLane:
    case kArmInt16x8ReplaceLane:
    case kArmInt16x8Neg:
    case kArmInt16x8ShiftLeftByScalar:
    case kArmInt16x8ShiftRightByScalar:
    case kArmInt16x8Add:
    case kArmInt16x8AddSaturate:
    case kArmInt16x8Sub:
    case kArmInt16x8SubSaturate:
    case kArmInt16x8Mul:
    case kArmInt16x8Min:
    case kArmInt16x8Max:
    case kArmInt16x8Equal:
    case kArmInt16x8NotEqual:
    case kArmInt16x8LessThan:
    case kArmInt16x8LessThanOrEqual:
    case kArmUint16x8ShiftRightByScalar:
    case kArmUint16x8AddSaturate:
    case kArmUint16x8SubSaturate:
    case kArmUint16x8Min:
    case kArmUint16x8Max:
    case kArmUint16x8LessThan:
    case kArmUint16x8LessThanOrEqual:
    case kArmInt8x16Splat:
    case kArmInt8x16ExtractLane:
    case kArmInt8x16ReplaceLane:
    case kArmInt8x16Neg:
    case kArmInt8x16ShiftLeftByScalar:
    case kArmInt8x16ShiftRightByScalar:
    case kArmInt8x16Add:
    case kArmInt8x16AddSaturate:
    case kArmInt8x16Sub:
    case kArmInt8x16SubSaturate:
    case kArmInt8x16Mul:
    case kArmInt8x16Min:
    case kArmInt8x16Max:
    case kArmInt8x16Equal:
    case kArmInt8x16NotEqual:
    case kArmInt8x16LessThan:
    case kArmInt8x16LessThanOrEqual:
    case kArmUint8x16ShiftRightByScalar:
    case kArmUint8x16AddSaturate:
    case kArmUint8x16SubSaturate:
    case kArmUint8x16Min:
    case kArmUint8x16Max:
    case kArmUint8x16LessThan:
    case kArmUint8x16LessThanOrEqual:
    case kArmSimd128Zero:
    case kArmSimd128And:
    case kArmSimd128Or:
    case kArmSimd128Xor:
    case kArmSimd128Not:
    case kArmSimd128Select:
    case kArmSimd1x4AnyTrue:
    case kArmSimd1x4AllTrue:
    case kArmSimd1x8AnyTrue:
    case kArmSimd1x8AllTrue:
    case kArmSimd1x16AnyTrue:
    case kArmSimd1x16AllTrue:
      return kNoOpcodeFlags;

    case kArmVldrF32:
    case kArmVldrF64:
    case kArmVld1F64:
    case kArmVld1S128:
    case kArmLdrb:
    case kArmLdrsb:
    case kArmLdrh:
    case kArmLdrsh:
    case kArmLdr:
      return kIsLoadOperation;

    case kArmVstrF32:
    case kArmVstrF64:
    case kArmVst1F64:
    case kArmVst1S128:
    case kArmStrb:
    case kArmStrh:
    case kArmStr:
    case kArmPush:
    case kArmPoke:
      return kHasSideEffect;

#define CASE(Name) case k##Name:
    COMMON_ARCH_OPCODE_LIST(CASE)
#undef CASE
      // Already covered in architecture independent code.
      UNREACHABLE();
  }

  UNREACHABLE();
  return kNoOpcodeFlags;
}


int InstructionScheduler::GetInstructionLatency(const Instruction* instr) {
  // TODO(all): Add instruction cost modeling.
  return 1;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
