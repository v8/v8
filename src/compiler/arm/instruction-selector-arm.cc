// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/instruction-selector-impl.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler-intrinsics.h"

namespace v8 {
namespace internal {
namespace compiler {

// Adds Arm-specific methods for generating InstructionOperands.
class ArmOperandGenerator V8_FINAL : public OperandGenerator {
 public:
  explicit ArmOperandGenerator(InstructionSelector* selector)
      : OperandGenerator(selector) {}

  InstructionOperand* UseOperand(Node* node, InstructionCode opcode) {
    if (CanBeImmediate(node, opcode)) {
      return UseImmediate(node);
    }
    return UseRegister(node);
  }

  bool CanBeImmediate(Node* node, InstructionCode opcode) {
    int32_t value;
    switch (node->opcode()) {
      case IrOpcode::kInt32Constant:
      case IrOpcode::kNumberConstant:
        value = ValueOf<int32_t>(node->op());
        break;
      default:
        return false;
    }
    switch (ArchOpcodeField::decode(opcode)) {
      case kArmAnd:
      case kArmMov:
      case kArmMvn:
      case kArmBic:
        return ImmediateFitsAddrMode1Instruction(value) ||
               ImmediateFitsAddrMode1Instruction(~value);

      case kArmAdd:
      case kArmSub:
      case kArmCmp:
      case kArmCmn:
        return ImmediateFitsAddrMode1Instruction(value) ||
               ImmediateFitsAddrMode1Instruction(-value);

      case kArmTst:
      case kArmTeq:
      case kArmOrr:
      case kArmEor:
      case kArmRsb:
        return ImmediateFitsAddrMode1Instruction(value);

      case kArmFloat64Load:
      case kArmFloat64Store:
        return value >= -1020 && value <= 1020 && (value % 4) == 0;

      case kArmLoadWord8:
      case kArmStoreWord8:
      case kArmLoadWord32:
      case kArmStoreWord32:
      case kArmStoreWriteBarrier:
        return value >= -4095 && value <= 4095;

      case kArmLoadWord16:
      case kArmStoreWord16:
        return value >= -255 && value <= 255;

      case kArchJmp:
      case kArchNop:
      case kArchRet:
      case kArchDeoptimize:
      case kArmMul:
      case kArmMla:
      case kArmMls:
      case kArmSdiv:
      case kArmUdiv:
      case kArmBfc:
      case kArmUbfx:
      case kArmCallCodeObject:
      case kArmCallJSFunction:
      case kArmCallAddress:
      case kArmPush:
      case kArmDrop:
      case kArmVcmpF64:
      case kArmVaddF64:
      case kArmVsubF64:
      case kArmVmulF64:
      case kArmVmlaF64:
      case kArmVmlsF64:
      case kArmVdivF64:
      case kArmVmodF64:
      case kArmVnegF64:
      case kArmVcvtF64S32:
      case kArmVcvtF64U32:
      case kArmVcvtS32F64:
      case kArmVcvtU32F64:
        return false;
    }
    UNREACHABLE();
    return false;
  }

 private:
  bool ImmediateFitsAddrMode1Instruction(int32_t imm) const {
    return Assembler::ImmediateFitsAddrMode1Instruction(imm);
  }
};


static void VisitRRRFloat64(InstructionSelector* selector, ArchOpcode opcode,
                            Node* node) {
  ArmOperandGenerator g(selector);
  selector->Emit(opcode, g.DefineAsDoubleRegister(node),
                 g.UseDoubleRegister(node->InputAt(0)),
                 g.UseDoubleRegister(node->InputAt(1)));
}


static Instruction* EmitBinop(InstructionSelector* selector,
                              InstructionCode opcode, size_t output_count,
                              InstructionOperand** outputs, Node* left,
                              Node* right, size_t label_count,
                              InstructionOperand** labels) {
  ArmOperandGenerator g(selector);
  InstructionOperand* inputs[5];
  size_t input_count = 0;

  inputs[input_count++] = g.UseRegister(left);
  if (g.CanBeImmediate(right, opcode)) {
    opcode |= AddressingModeField::encode(kMode_Operand2_I);
    inputs[input_count++] = g.UseImmediate(right);
  } else if (right->opcode() == IrOpcode::kWord32Sar) {
    Int32BinopMatcher mright(right);
    inputs[input_count++] = g.UseRegister(mright.left().node());
    if (mright.right().IsInRange(1, 32)) {
      opcode |= AddressingModeField::encode(kMode_Operand2_R_ASR_I);
      inputs[input_count++] = g.UseImmediate(mright.right().node());
    } else {
      opcode |= AddressingModeField::encode(kMode_Operand2_R_ASR_R);
      inputs[input_count++] = g.UseRegister(mright.right().node());
    }
  } else if (right->opcode() == IrOpcode::kWord32Shl) {
    Int32BinopMatcher mright(right);
    inputs[input_count++] = g.UseRegister(mright.left().node());
    if (mright.right().IsInRange(0, 31)) {
      opcode |= AddressingModeField::encode(kMode_Operand2_R_LSL_I);
      inputs[input_count++] = g.UseImmediate(mright.right().node());
    } else {
      opcode |= AddressingModeField::encode(kMode_Operand2_R_LSL_R);
      inputs[input_count++] = g.UseRegister(mright.right().node());
    }
  } else if (right->opcode() == IrOpcode::kWord32Shr) {
    Int32BinopMatcher mright(right);
    inputs[input_count++] = g.UseRegister(mright.left().node());
    if (mright.right().IsInRange(1, 32)) {
      opcode |= AddressingModeField::encode(kMode_Operand2_R_LSR_I);
      inputs[input_count++] = g.UseImmediate(mright.right().node());
    } else {
      opcode |= AddressingModeField::encode(kMode_Operand2_R_LSR_R);
      inputs[input_count++] = g.UseRegister(mright.right().node());
    }
  } else {
    opcode |= AddressingModeField::encode(kMode_Operand2_R);
    inputs[input_count++] = g.UseRegister(right);
  }

  // Append the optional labels.
  while (label_count-- != 0) {
    inputs[input_count++] = *labels++;
  }

  ASSERT_NE(0, input_count);
  ASSERT_GE(ARRAY_SIZE(inputs), input_count);
  ASSERT_NE(kMode_None, AddressingModeField::decode(opcode));

  return selector->Emit(opcode, output_count, outputs, input_count, inputs);
}


static Instruction* EmitBinop(InstructionSelector* selector,
                              InstructionCode opcode, Node* node, Node* left,
                              Node* right) {
  ArmOperandGenerator g(selector);
  InstructionOperand* outputs[] = {g.DefineAsRegister(node)};
  const size_t output_count = ARRAY_SIZE(outputs);
  return EmitBinop(selector, opcode, output_count, outputs, left, right, 0,
                   NULL);
}


// Shared routine for multiple binary operations.
static void VisitBinop(InstructionSelector* selector, Node* node,
                       InstructionCode opcode, InstructionCode reverse_opcode) {
  ArmOperandGenerator g(selector);
  Int32BinopMatcher m(node);

  Node* left = m.left().node();
  Node* right = m.right().node();
  if (g.CanBeImmediate(m.left().node(), reverse_opcode) ||
      m.left().IsWord32Sar() || m.left().IsWord32Shl() ||
      m.left().IsWord32Shr()) {
    opcode = reverse_opcode;
    std::swap(left, right);
  }

  EmitBinop(selector, opcode, node, left, right);
}


void InstructionSelector::VisitLoad(Node* node) {
  MachineRepresentation rep = OpParameter<MachineRepresentation>(node);
  ArmOperandGenerator g(this);
  Node* base = node->InputAt(0);
  Node* index = node->InputAt(1);

  InstructionOperand* result = rep == kMachineFloat64
                                   ? g.DefineAsDoubleRegister(node)
                                   : g.DefineAsRegister(node);

  ArchOpcode opcode;
  switch (rep) {
    case kMachineFloat64:
      opcode = kArmFloat64Load;
      break;
    case kMachineWord8:
      opcode = kArmLoadWord8;
      break;
    case kMachineWord16:
      opcode = kArmLoadWord16;
      break;
    case kMachineTagged:  // Fall through.
    case kMachineWord32:
      opcode = kArmLoadWord32;
      break;
    default:
      UNREACHABLE();
      return;
  }

  if (g.CanBeImmediate(index, opcode)) {
    Emit(opcode | AddressingModeField::encode(kMode_Offset_RI), result,
         g.UseRegister(base), g.UseImmediate(index));
  } else if (g.CanBeImmediate(base, opcode)) {
    Emit(opcode | AddressingModeField::encode(kMode_Offset_RI), result,
         g.UseRegister(index), g.UseImmediate(base));
  } else {
    Emit(opcode | AddressingModeField::encode(kMode_Offset_RR), result,
         g.UseRegister(base), g.UseRegister(index));
  }
}


void InstructionSelector::VisitStore(Node* node) {
  ArmOperandGenerator g(this);
  Node* base = node->InputAt(0);
  Node* index = node->InputAt(1);
  Node* value = node->InputAt(2);

  StoreRepresentation store_rep = OpParameter<StoreRepresentation>(node);
  MachineRepresentation rep = store_rep.rep;
  if (store_rep.write_barrier_kind == kFullWriteBarrier) {
    ASSERT(rep == kMachineTagged);
    // TODO(dcarney): refactor RecordWrite function to take temp registers
    //                and pass them here instead of using fixed regs
    // TODO(dcarney): handle immediate indices.
    InstructionOperand* temps[] = {g.TempRegister(r5), g.TempRegister(r6)};
    Emit(kArmStoreWriteBarrier, NULL, g.UseFixed(base, r4),
         g.UseFixed(index, r5), g.UseFixed(value, r6), ARRAY_SIZE(temps),
         temps);
    return;
  }
  ASSERT_EQ(kNoWriteBarrier, store_rep.write_barrier_kind);
  InstructionOperand* val = rep == kMachineFloat64 ? g.UseDoubleRegister(value)
                                                   : g.UseRegister(value);

  ArchOpcode opcode;
  switch (rep) {
    case kMachineFloat64:
      opcode = kArmFloat64Store;
      break;
    case kMachineWord8:
      opcode = kArmStoreWord8;
      break;
    case kMachineWord16:
      opcode = kArmStoreWord16;
      break;
    case kMachineTagged:  // Fall through.
    case kMachineWord32:
      opcode = kArmStoreWord32;
      break;
    default:
      UNREACHABLE();
      return;
  }

  if (g.CanBeImmediate(index, opcode)) {
    Emit(opcode | AddressingModeField::encode(kMode_Offset_RI), NULL,
         g.UseRegister(base), g.UseImmediate(index), val);
  } else if (g.CanBeImmediate(base, opcode)) {
    Emit(opcode | AddressingModeField::encode(kMode_Offset_RI), NULL,
         g.UseRegister(index), g.UseImmediate(base), val);
  } else {
    Emit(opcode | AddressingModeField::encode(kMode_Offset_RR), NULL,
         g.UseRegister(base), g.UseRegister(index), val);
  }
}


void InstructionSelector::VisitWord32And(Node* node) {
  ArmOperandGenerator g(this);
  Int32BinopMatcher m(node);
  if (m.left().IsWord32Xor() && CanCover(node, m.left().node())) {
    Int32BinopMatcher mleft(m.left().node());
    if (mleft.right().Is(-1)) {
      EmitBinop(this, kArmBic, node, m.right().node(), mleft.left().node());
      return;
    }
  }
  if (m.right().IsWord32Xor() && CanCover(node, m.right().node())) {
    Int32BinopMatcher mright(m.right().node());
    if (mright.right().Is(-1)) {
      EmitBinop(this, kArmBic, node, m.left().node(), mright.left().node());
      return;
    }
  }
  if (CpuFeatures::IsSupported(ARMv7) && m.right().HasValue()) {
    uint32_t value = m.right().Value();
    uint32_t width = CompilerIntrinsics::CountSetBits(value);
    uint32_t msb = CompilerIntrinsics::CountLeadingZeros(value);
    if (msb + width == 32) {
      ASSERT_EQ(0, CompilerIntrinsics::CountTrailingZeros(value));
      if (m.left().IsWord32Shr()) {
        Int32BinopMatcher mleft(m.left().node());
        if (mleft.right().IsInRange(0, 31)) {
          Emit(kArmUbfx, g.DefineAsRegister(node),
               g.UseRegister(mleft.left().node()),
               g.UseImmediate(mleft.right().node()), g.TempImmediate(width));
          return;
        }
      }
      Emit(kArmUbfx, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
           g.TempImmediate(0), g.TempImmediate(width));
      return;
    }
    // Try to interpret this AND as BFC.
    width = 32 - width;
    msb = CompilerIntrinsics::CountLeadingZeros(~value);
    uint32_t lsb = CompilerIntrinsics::CountTrailingZeros(~value);
    if (msb + width + lsb == 32) {
      Emit(kArmBfc, g.DefineSameAsFirst(node), g.UseRegister(m.left().node()),
           g.TempImmediate(lsb), g.TempImmediate(width));
      return;
    }
  }
  VisitBinop(this, node, kArmAnd, kArmAnd);
}


void InstructionSelector::VisitWord32Or(Node* node) {
  VisitBinop(this, node, kArmOrr, kArmOrr);
}


void InstructionSelector::VisitWord32Xor(Node* node) {
  ArmOperandGenerator g(this);
  Int32BinopMatcher m(node);
  if (m.right().Is(-1)) {
    Emit(kArmMvn | AddressingModeField::encode(kMode_Operand2_R),
         g.DefineSameAsFirst(node), g.UseRegister(m.left().node()));
  } else {
    VisitBinop(this, node, kArmEor, kArmEor);
  }
}


void InstructionSelector::VisitWord32Shl(Node* node) {
  ArmOperandGenerator g(this);
  Int32BinopMatcher m(node);
  if (m.right().IsInRange(0, 31)) {
    Emit(kArmMov | AddressingModeField::encode(kMode_Operand2_R_LSL_I),
         g.DefineAsRegister(node), g.UseRegister(m.left().node()),
         g.UseImmediate(m.right().node()));
  } else {
    Emit(kArmMov | AddressingModeField::encode(kMode_Operand2_R_LSL_R),
         g.DefineAsRegister(node), g.UseRegister(m.left().node()),
         g.UseRegister(m.right().node()));
  }
}


void InstructionSelector::VisitWord32Shr(Node* node) {
  ArmOperandGenerator g(this);
  Int32BinopMatcher m(node);
  if (CpuFeatures::IsSupported(ARMv7) && m.left().IsWord32And() &&
      m.right().IsInRange(0, 31)) {
    int32_t lsb = m.right().Value();
    Int32BinopMatcher mleft(m.left().node());
    if (mleft.right().HasValue()) {
      uint32_t value = (mleft.right().Value() >> lsb) << lsb;
      uint32_t width = CompilerIntrinsics::CountSetBits(value);
      uint32_t msb = CompilerIntrinsics::CountLeadingZeros(value);
      if (msb + width + lsb == 32) {
        ASSERT_EQ(lsb, CompilerIntrinsics::CountTrailingZeros(value));
        Emit(kArmUbfx, g.DefineAsRegister(node),
             g.UseRegister(mleft.left().node()), g.TempImmediate(lsb),
             g.TempImmediate(width));
        return;
      }
    }
  }
  if (m.right().IsInRange(1, 32)) {
    Emit(kArmMov | AddressingModeField::encode(kMode_Operand2_R_LSR_I),
         g.DefineAsRegister(node), g.UseRegister(m.left().node()),
         g.UseImmediate(m.right().node()));
    return;
  }
  Emit(kArmMov | AddressingModeField::encode(kMode_Operand2_R_LSR_R),
       g.DefineAsRegister(node), g.UseRegister(m.left().node()),
       g.UseRegister(m.right().node()));
}


void InstructionSelector::VisitWord32Sar(Node* node) {
  ArmOperandGenerator g(this);
  Int32BinopMatcher m(node);
  if (m.right().IsInRange(1, 32)) {
    Emit(kArmMov | AddressingModeField::encode(kMode_Operand2_R_ASR_I),
         g.DefineAsRegister(node), g.UseRegister(m.left().node()),
         g.UseImmediate(m.right().node()));
  } else {
    Emit(kArmMov | AddressingModeField::encode(kMode_Operand2_R_ASR_R),
         g.DefineAsRegister(node), g.UseRegister(m.left().node()),
         g.UseRegister(m.right().node()));
  }
}


void InstructionSelector::VisitInt32Add(Node* node) {
  ArmOperandGenerator g(this);
  Int32BinopMatcher m(node);
  if (m.left().IsInt32Mul() && CanCover(node, m.left().node())) {
    Int32BinopMatcher mleft(m.left().node());
    Emit(kArmMla, g.DefineAsRegister(node), g.UseRegister(mleft.left().node()),
         g.UseRegister(mleft.right().node()), g.UseRegister(m.right().node()));
    return;
  }
  if (m.right().IsInt32Mul() && CanCover(node, m.right().node())) {
    Int32BinopMatcher mright(m.right().node());
    Emit(kArmMla, g.DefineAsRegister(node), g.UseRegister(mright.left().node()),
         g.UseRegister(mright.right().node()), g.UseRegister(m.left().node()));
    return;
  }
  VisitBinop(this, node, kArmAdd, kArmAdd);
}


void InstructionSelector::VisitInt32Sub(Node* node) {
  ArmOperandGenerator g(this);
  Int32BinopMatcher m(node);
  if (CpuFeatures::IsSupported(MLS) && m.right().IsInt32Mul() &&
      CanCover(node, m.right().node())) {
    Int32BinopMatcher mright(m.right().node());
    Emit(kArmMls, g.DefineAsRegister(node), g.UseRegister(mright.left().node()),
         g.UseRegister(mright.right().node()), g.UseRegister(m.left().node()));
    return;
  }
  VisitBinop(this, node, kArmSub, kArmRsb);
}


void InstructionSelector::VisitInt32Mul(Node* node) {
  ArmOperandGenerator g(this);
  Int32BinopMatcher m(node);
  if (m.right().HasValue() && m.right().Value() > 0) {
    int32_t value = m.right().Value();
    if (IsPowerOf2(value - 1)) {
      Emit(kArmAdd | AddressingModeField::encode(kMode_Operand2_R_LSL_I),
           g.DefineAsRegister(node), g.UseRegister(m.left().node()),
           g.UseRegister(m.left().node()),
           g.TempImmediate(WhichPowerOf2(value - 1)));
      return;
    }
    if (value < kMaxInt && IsPowerOf2(value + 1)) {
      Emit(kArmRsb | AddressingModeField::encode(kMode_Operand2_R_LSL_I),
           g.DefineAsRegister(node), g.UseRegister(m.left().node()),
           g.UseRegister(m.left().node()),
           g.TempImmediate(WhichPowerOf2(value + 1)));
      return;
    }
  }
  Emit(kArmMul, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
       g.UseRegister(m.right().node()));
}


static void EmitDiv(InstructionSelector* selector, ArchOpcode div_opcode,
                    ArchOpcode f64i32_opcode, ArchOpcode i32f64_opcode,
                    InstructionOperand* result_operand,
                    InstructionOperand* left_operand,
                    InstructionOperand* right_operand) {
  ArmOperandGenerator g(selector);
  if (CpuFeatures::IsSupported(SUDIV)) {
    selector->Emit(div_opcode, result_operand, left_operand, right_operand);
    return;
  }
  InstructionOperand* left_double_operand = g.TempDoubleRegister();
  InstructionOperand* right_double_operand = g.TempDoubleRegister();
  InstructionOperand* result_double_operand = g.TempDoubleRegister();
  selector->Emit(f64i32_opcode, left_double_operand, left_operand);
  selector->Emit(f64i32_opcode, right_double_operand, right_operand);
  selector->Emit(kArmVdivF64, result_double_operand, left_double_operand,
                 right_double_operand);
  selector->Emit(i32f64_opcode, result_operand, result_double_operand);
}


static void VisitDiv(InstructionSelector* selector, Node* node,
                     ArchOpcode div_opcode, ArchOpcode f64i32_opcode,
                     ArchOpcode i32f64_opcode) {
  ArmOperandGenerator g(selector);
  Int32BinopMatcher m(node);
  EmitDiv(selector, div_opcode, f64i32_opcode, i32f64_opcode,
          g.DefineAsRegister(node), g.UseRegister(m.left().node()),
          g.UseRegister(m.right().node()));
}


void InstructionSelector::VisitInt32Div(Node* node) {
  VisitDiv(this, node, kArmSdiv, kArmVcvtF64S32, kArmVcvtS32F64);
}


void InstructionSelector::VisitInt32UDiv(Node* node) {
  VisitDiv(this, node, kArmUdiv, kArmVcvtF64U32, kArmVcvtU32F64);
}


static void VisitMod(InstructionSelector* selector, Node* node,
                     ArchOpcode div_opcode, ArchOpcode f64i32_opcode,
                     ArchOpcode i32f64_opcode) {
  ArmOperandGenerator g(selector);
  Int32BinopMatcher m(node);
  InstructionOperand* div_operand = g.TempRegister();
  InstructionOperand* result_operand = g.DefineAsRegister(node);
  InstructionOperand* left_operand = g.UseRegister(m.left().node());
  InstructionOperand* right_operand = g.UseRegister(m.right().node());
  EmitDiv(selector, div_opcode, f64i32_opcode, i32f64_opcode, div_operand,
          left_operand, right_operand);
  if (CpuFeatures::IsSupported(MLS)) {
    selector->Emit(kArmMls, result_operand, div_operand, right_operand,
                   left_operand);
    return;
  }
  InstructionOperand* mul_operand = g.TempRegister();
  selector->Emit(kArmMul, mul_operand, div_operand, right_operand);
  selector->Emit(kArmSub, result_operand, left_operand, mul_operand);
}


void InstructionSelector::VisitInt32Mod(Node* node) {
  VisitMod(this, node, kArmSdiv, kArmVcvtF64S32, kArmVcvtS32F64);
}


void InstructionSelector::VisitInt32UMod(Node* node) {
  VisitMod(this, node, kArmUdiv, kArmVcvtF64U32, kArmVcvtU32F64);
}


void InstructionSelector::VisitConvertInt32ToFloat64(Node* node) {
  ArmOperandGenerator g(this);
  Emit(kArmVcvtF64S32, g.DefineAsDoubleRegister(node),
       g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitConvertFloat64ToInt32(Node* node) {
  ArmOperandGenerator g(this);
  Emit(kArmVcvtS32F64, g.DefineAsRegister(node),
       g.UseDoubleRegister(node->InputAt(0)));
}


void InstructionSelector::VisitFloat64Add(Node* node) {
  ArmOperandGenerator g(this);
  Int32BinopMatcher m(node);
  if (m.left().IsFloat64Mul() && CanCover(node, m.left().node())) {
    Int32BinopMatcher mleft(m.left().node());
    Emit(kArmVmlaF64, g.DefineSameAsFirst(node),
         g.UseRegister(m.right().node()), g.UseRegister(mleft.left().node()),
         g.UseRegister(mleft.right().node()));
    return;
  }
  if (m.right().IsFloat64Mul() && CanCover(node, m.right().node())) {
    Int32BinopMatcher mright(m.right().node());
    Emit(kArmVmlaF64, g.DefineSameAsFirst(node), g.UseRegister(m.left().node()),
         g.UseRegister(mright.left().node()),
         g.UseRegister(mright.right().node()));
    return;
  }
  VisitRRRFloat64(this, kArmVaddF64, node);
}


void InstructionSelector::VisitFloat64Sub(Node* node) {
  ArmOperandGenerator g(this);
  Int32BinopMatcher m(node);
  if (m.right().IsFloat64Mul() && CanCover(node, m.right().node())) {
    Int32BinopMatcher mright(m.right().node());
    Emit(kArmVmlsF64, g.DefineSameAsFirst(node), g.UseRegister(m.left().node()),
         g.UseRegister(mright.left().node()),
         g.UseRegister(mright.right().node()));
    return;
  }
  VisitRRRFloat64(this, kArmVsubF64, node);
}


void InstructionSelector::VisitFloat64Mul(Node* node) {
  ArmOperandGenerator g(this);
  Float64BinopMatcher m(node);
  if (m.right().Is(-1.0)) {
    Emit(kArmVnegF64, g.DefineAsRegister(node),
         g.UseDoubleRegister(m.left().node()));
  } else {
    VisitRRRFloat64(this, kArmVmulF64, node);
  }
}


void InstructionSelector::VisitFloat64Div(Node* node) {
  VisitRRRFloat64(this, kArmVdivF64, node);
}


void InstructionSelector::VisitFloat64Mod(Node* node) {
  ArmOperandGenerator g(this);
  Emit(kArmVmodF64, g.DefineAsFixedDouble(node, d0),
       g.UseFixedDouble(node->InputAt(0), d0),
       g.UseFixedDouble(node->InputAt(1), d1))->MarkAsCall();
}


void InstructionSelector::VisitCall(Node* call, BasicBlock* continuation,
                                    BasicBlock* deoptimization) {
  ArmOperandGenerator g(this);
  CallDescriptor* descriptor = OpParameter<CallDescriptor*>(call);
  CallBuffer buffer(zone(), descriptor);  // TODO(turbofan): temp zone here?

  // Compute InstructionOperands for inputs and outputs.
  // TODO(turbofan): on ARM64 it's probably better to use the code object in a
  // register if there are multiple uses of it. Improve constant pool and the
  // heuristics in the register allocator for where to emit constants.
  InitializeCallBuffer(call, &buffer, true, false, continuation,
                       deoptimization);

  // TODO(dcarney): might be possible to use claim/poke instead
  // Push any stack arguments.
  for (int i = buffer.pushed_count - 1; i >= 0; --i) {
    Node* input = buffer.pushed_nodes[i];
    Emit(kArmPush, NULL, g.UseRegister(input));
  }

  // Select the appropriate opcode based on the call type.
  InstructionCode opcode;
  switch (descriptor->kind()) {
    case CallDescriptor::kCallCodeObject: {
      bool lazy_deopt = descriptor->CanLazilyDeoptimize();
      opcode = kArmCallCodeObject | MiscField::encode(lazy_deopt ? 1 : 0);
      break;
    }
    case CallDescriptor::kCallAddress:
      opcode = kArmCallAddress;
      break;
    case CallDescriptor::kCallJSFunction:
      opcode = kArmCallJSFunction;
      break;
    default:
      UNREACHABLE();
      return;
  }

  // Emit the call instruction.
  Instruction* call_instr =
      Emit(opcode, buffer.output_count, buffer.outputs,
           buffer.fixed_and_control_count(), buffer.fixed_and_control_args);

  call_instr->MarkAsCall();
  if (deoptimization != NULL) {
    ASSERT(continuation != NULL);
    call_instr->MarkAsControl();
  }

  // Caller clean up of stack for C-style calls.
  if (descriptor->kind() == CallDescriptor::kCallAddress &&
      buffer.pushed_count > 0) {
    ASSERT(deoptimization == NULL && continuation == NULL);
    Emit(kArmDrop | MiscField::encode(buffer.pushed_count), NULL);
  }
}


// Shared routine for multiple compare operations.
static void VisitWordCompare(InstructionSelector* selector, Node* node,
                             InstructionCode opcode, FlagsContinuation* cont,
                             bool commutative, bool requires_output) {
  ArmOperandGenerator g(selector);
  Int32BinopMatcher m(node);

  Node* left = m.left().node();
  Node* right = m.right().node();
  if (g.CanBeImmediate(m.left().node(), opcode) || m.left().IsWord32Sar() ||
      m.left().IsWord32Shl() || m.left().IsWord32Shr()) {
    if (!commutative) cont->Commute();
    std::swap(left, right);
  }

  opcode = cont->Encode(opcode);
  if (cont->IsBranch()) {
    InstructionOperand* outputs[1];
    size_t output_count = 0;
    if (requires_output) {
      outputs[output_count++] = g.DefineAsRegister(node);
    }
    InstructionOperand* labels[] = {g.Label(cont->true_block()),
                                    g.Label(cont->false_block())};
    const size_t label_count = ARRAY_SIZE(labels);
    EmitBinop(selector, opcode, output_count, outputs, left, right, label_count,
              labels)->MarkAsControl();
  } else {
    ASSERT(cont->IsSet());
    EmitBinop(selector, opcode, cont->result(), left, right);
  }
}


void InstructionSelector::VisitWord32Test(Node* node, FlagsContinuation* cont) {
  switch (node->opcode()) {
    case IrOpcode::kInt32Add:
      return VisitWordCompare(this, node, kArmCmn, cont, true, false);
    case IrOpcode::kInt32Sub:
      return VisitWordCompare(this, node, kArmCmp, cont, false, false);
    case IrOpcode::kWord32And:
      return VisitWordCompare(this, node, kArmTst, cont, true, false);
    case IrOpcode::kWord32Or:
      return VisitWordCompare(this, node, kArmOrr, cont, true, true);
    case IrOpcode::kWord32Xor:
      return VisitWordCompare(this, node, kArmTeq, cont, true, false);
    default:
      break;
  }

  ArmOperandGenerator g(this);
  InstructionCode opcode =
      cont->Encode(kArmTst) | AddressingModeField::encode(kMode_Operand2_R);
  if (cont->IsBranch()) {
    Emit(opcode, NULL, g.UseRegister(node), g.UseRegister(node),
         g.Label(cont->true_block()),
         g.Label(cont->false_block()))->MarkAsControl();
  } else {
    Emit(opcode, g.DefineAsRegister(cont->result()), g.UseRegister(node),
         g.UseRegister(node));
  }
}


void InstructionSelector::VisitWord32Compare(Node* node,
                                             FlagsContinuation* cont) {
  VisitWordCompare(this, node, kArmCmp, cont, false, false);
}


void InstructionSelector::VisitFloat64Compare(Node* node,
                                              FlagsContinuation* cont) {
  ArmOperandGenerator g(this);
  Float64BinopMatcher m(node);
  if (cont->IsBranch()) {
    Emit(cont->Encode(kArmVcmpF64), NULL, g.UseDoubleRegister(m.left().node()),
         g.UseDoubleRegister(m.right().node()), g.Label(cont->true_block()),
         g.Label(cont->false_block()))->MarkAsControl();
  } else {
    ASSERT(cont->IsSet());
    Emit(cont->Encode(kArmVcmpF64), g.DefineAsRegister(cont->result()),
         g.UseDoubleRegister(m.left().node()),
         g.UseDoubleRegister(m.right().node()));
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
