// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/bits.h"
#include "src/compiler/instruction-selector-impl.h"
#include "src/compiler/node-matchers.h"

namespace v8 {
namespace internal {
namespace compiler {

#define TRACE_UNIMPL() \
  PrintF("UNIMPLEMENTED instr_sel: %s at line %d\n", __FUNCTION__, __LINE__)

#define TRACE() PrintF("instr_sel: %s at line %d\n", __FUNCTION__, __LINE__)


// Adds Mips-specific methods for generating InstructionOperands.
class MipsOperandGenerator FINAL : public OperandGenerator {
 public:
  explicit MipsOperandGenerator(InstructionSelector* selector)
      : OperandGenerator(selector) {}

  InstructionOperand* UseOperand(Node* node, InstructionCode opcode) {
    if (CanBeImmediate(node, opcode)) {
      return UseImmediate(node);
    }
    return UseRegister(node);
  }

  bool CanBeImmediate(Node* node, InstructionCode opcode) {
    Int32Matcher m(node);
    if (!m.HasValue()) return false;
    int32_t value = m.Value();
    switch (ArchOpcodeField::decode(opcode)) {
      case kMipsShl:
      case kMipsSar:
      case kMipsShr:
        return is_uint5(value);
      case kMipsXor:
        return is_uint16(value);
      case kMipsLdc1:
      case kMipsSdc1:
        return is_int16(value + kIntSize);
      default:
        return is_int16(value);
    }
  }

 private:
  bool ImmediateFitsAddrMode1Instruction(int32_t imm) const {
    TRACE_UNIMPL();
    return false;
  }
};


static void VisitRRR(InstructionSelector* selector, ArchOpcode opcode,
                     Node* node) {
  MipsOperandGenerator g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(node->InputAt(0)),
                 g.UseRegister(node->InputAt(1)));
}


static void VisitRRO(InstructionSelector* selector, ArchOpcode opcode,
                     Node* node) {
  MipsOperandGenerator g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(node->InputAt(0)),
                 g.UseOperand(node->InputAt(1), opcode));
}


static void VisitBinop(InstructionSelector* selector, Node* node,
                       InstructionCode opcode, FlagsContinuation* cont) {
  MipsOperandGenerator g(selector);
  Int32BinopMatcher m(node);
  InstructionOperand* inputs[4];
  size_t input_count = 0;
  InstructionOperand* outputs[2];
  size_t output_count = 0;

  inputs[input_count++] = g.UseRegister(m.left().node());
  inputs[input_count++] = g.UseOperand(m.right().node(), opcode);

  if (cont->IsBranch()) {
    inputs[input_count++] = g.Label(cont->true_block());
    inputs[input_count++] = g.Label(cont->false_block());
  }

  outputs[output_count++] = g.DefineAsRegister(node);
  if (cont->IsSet()) {
    outputs[output_count++] = g.DefineAsRegister(cont->result());
  }

  DCHECK_NE(0, input_count);
  DCHECK_NE(0, output_count);
  DCHECK_GE(arraysize(inputs), input_count);
  DCHECK_GE(arraysize(outputs), output_count);

  Instruction* instr = selector->Emit(cont->Encode(opcode), output_count,
                                      outputs, input_count, inputs);
  if (cont->IsBranch()) instr->MarkAsControl();
}


static void VisitBinop(InstructionSelector* selector, Node* node,
                       InstructionCode opcode) {
  FlagsContinuation cont;
  VisitBinop(selector, node, opcode, &cont);
}


void InstructionSelector::VisitLoad(Node* node) {
  MachineType rep = RepresentationOf(OpParameter<LoadRepresentation>(node));
  MachineType typ = TypeOf(OpParameter<LoadRepresentation>(node));
  MipsOperandGenerator g(this);
  Node* base = node->InputAt(0);
  Node* index = node->InputAt(1);

  ArchOpcode opcode;
  switch (rep) {
    case kRepFloat32:
      opcode = kMipsLwc1;
      break;
    case kRepFloat64:
      opcode = kMipsLdc1;
      break;
    case kRepBit:  // Fall through.
    case kRepWord8:
      opcode = typ == kTypeUint32 ? kMipsLbu : kMipsLb;
      break;
    case kRepWord16:
      opcode = typ == kTypeUint32 ? kMipsLhu : kMipsLh;
      break;
    case kRepTagged:  // Fall through.
    case kRepWord32:
      opcode = kMipsLw;
      break;
    default:
      UNREACHABLE();
      return;
  }

  if (g.CanBeImmediate(index, opcode)) {
    Emit(opcode | AddressingModeField::encode(kMode_MRI),
         g.DefineAsRegister(node), g.UseRegister(base), g.UseImmediate(index));
  } else {
    InstructionOperand* addr_reg = g.TempRegister();
    Emit(kMipsAdd | AddressingModeField::encode(kMode_None), addr_reg,
         g.UseRegister(index), g.UseRegister(base));
    // Emit desired load opcode, using temp addr_reg.
    Emit(opcode | AddressingModeField::encode(kMode_MRI),
         g.DefineAsRegister(node), addr_reg, g.TempImmediate(0));
  }
}


void InstructionSelector::VisitStore(Node* node) {
  MipsOperandGenerator g(this);
  Node* base = node->InputAt(0);
  Node* index = node->InputAt(1);
  Node* value = node->InputAt(2);

  StoreRepresentation store_rep = OpParameter<StoreRepresentation>(node);
  MachineType rep = RepresentationOf(store_rep.machine_type());
  if (store_rep.write_barrier_kind() == kFullWriteBarrier) {
    DCHECK(rep == kRepTagged);
    // TODO(dcarney): refactor RecordWrite function to take temp registers
    //                and pass them here instead of using fixed regs
    // TODO(dcarney): handle immediate indices.
    InstructionOperand* temps[] = {g.TempRegister(t1), g.TempRegister(t2)};
    Emit(kMipsStoreWriteBarrier, NULL, g.UseFixed(base, t0),
         g.UseFixed(index, t1), g.UseFixed(value, t2), arraysize(temps), temps);
    return;
  }
  DCHECK_EQ(kNoWriteBarrier, store_rep.write_barrier_kind());

  ArchOpcode opcode;
  switch (rep) {
    case kRepFloat32:
      opcode = kMipsSwc1;
      break;
    case kRepFloat64:
      opcode = kMipsSdc1;
      break;
    case kRepBit:  // Fall through.
    case kRepWord8:
      opcode = kMipsSb;
      break;
    case kRepWord16:
      opcode = kMipsSh;
      break;
    case kRepTagged:  // Fall through.
    case kRepWord32:
      opcode = kMipsSw;
      break;
    default:
      UNREACHABLE();
      return;
  }

  if (g.CanBeImmediate(index, opcode)) {
    Emit(opcode | AddressingModeField::encode(kMode_MRI), NULL,
         g.UseRegister(base), g.UseImmediate(index), g.UseRegister(value));
  } else {
    InstructionOperand* addr_reg = g.TempRegister();
    Emit(kMipsAdd | AddressingModeField::encode(kMode_None), addr_reg,
         g.UseRegister(index), g.UseRegister(base));
    // Emit desired store opcode, using temp addr_reg.
    Emit(opcode | AddressingModeField::encode(kMode_MRI), NULL, addr_reg,
         g.TempImmediate(0), g.UseRegister(value));
  }
}


void InstructionSelector::VisitWord32And(Node* node) {
  VisitBinop(this, node, kMipsAnd);
}


void InstructionSelector::VisitWord32Or(Node* node) {
  VisitBinop(this, node, kMipsOr);
}


void InstructionSelector::VisitWord32Xor(Node* node) {
  VisitBinop(this, node, kMipsXor);
}


void InstructionSelector::VisitWord32Shl(Node* node) {
  VisitRRO(this, kMipsShl, node);
}


void InstructionSelector::VisitWord32Shr(Node* node) {
  VisitRRO(this, kMipsShr, node);
}


void InstructionSelector::VisitWord32Sar(Node* node) {
  VisitRRO(this, kMipsSar, node);
}


void InstructionSelector::VisitWord32Ror(Node* node) {
  VisitRRO(this, kMipsRor, node);
}


void InstructionSelector::VisitInt32Add(Node* node) {
  MipsOperandGenerator g(this);

  // TODO(plind): Consider multiply & add optimization from arm port.
  VisitBinop(this, node, kMipsAdd);
}


void InstructionSelector::VisitInt32Sub(Node* node) {
  VisitBinop(this, node, kMipsSub);
}


void InstructionSelector::VisitInt32Mul(Node* node) {
  MipsOperandGenerator g(this);
  Int32BinopMatcher m(node);
  if (m.right().HasValue() && m.right().Value() > 0) {
    int32_t value = m.right().Value();
    if (base::bits::IsPowerOfTwo32(value)) {
      Emit(kMipsShl | AddressingModeField::encode(kMode_None),
           g.DefineAsRegister(node), g.UseRegister(m.left().node()),
           g.TempImmediate(WhichPowerOf2(value)));
      return;
    }
    if (base::bits::IsPowerOfTwo32(value - 1)) {
      InstructionOperand* temp = g.TempRegister();
      Emit(kMipsShl | AddressingModeField::encode(kMode_None), temp,
           g.UseRegister(m.left().node()),
           g.TempImmediate(WhichPowerOf2(value - 1)));
      Emit(kMipsAdd | AddressingModeField::encode(kMode_None),
           g.DefineAsRegister(node), g.UseRegister(m.left().node()), temp);
      return;
    }
    if (base::bits::IsPowerOfTwo32(value + 1)) {
      InstructionOperand* temp = g.TempRegister();
      Emit(kMipsShl | AddressingModeField::encode(kMode_None), temp,
           g.UseRegister(m.left().node()),
           g.TempImmediate(WhichPowerOf2(value + 1)));
      Emit(kMipsSub | AddressingModeField::encode(kMode_None),
           g.DefineAsRegister(node), temp, g.UseRegister(m.left().node()));
      return;
    }
  }
  Emit(kMipsMul, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
       g.UseRegister(m.right().node()));
}


void InstructionSelector::VisitInt32Div(Node* node) {
  MipsOperandGenerator g(this);
  Int32BinopMatcher m(node);
  Emit(kMipsDiv, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
       g.UseRegister(m.right().node()));
}


void InstructionSelector::VisitUint32Div(Node* node) {
  MipsOperandGenerator g(this);
  Int32BinopMatcher m(node);
  Emit(kMipsDivU, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
       g.UseRegister(m.right().node()));
}


void InstructionSelector::VisitInt32Mod(Node* node) {
  MipsOperandGenerator g(this);
  Int32BinopMatcher m(node);
  Emit(kMipsMod, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
       g.UseRegister(m.right().node()));
}


void InstructionSelector::VisitUint32Mod(Node* node) {
  MipsOperandGenerator g(this);
  Int32BinopMatcher m(node);
  Emit(kMipsModU, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
       g.UseRegister(m.right().node()));
}


void InstructionSelector::VisitChangeFloat32ToFloat64(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsCvtDS, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitChangeInt32ToFloat64(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsCvtDW, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitChangeUint32ToFloat64(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsCvtDUw, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitChangeFloat64ToInt32(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsTruncWD, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitChangeFloat64ToUint32(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsTruncUwD, g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitTruncateFloat64ToFloat32(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsCvtSD, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitFloat64Add(Node* node) {
  VisitRRR(this, kMipsAddD, node);
}


void InstructionSelector::VisitFloat64Sub(Node* node) {
  VisitRRR(this, kMipsSubD, node);
}


void InstructionSelector::VisitFloat64Mul(Node* node) {
  VisitRRR(this, kMipsMulD, node);
}


void InstructionSelector::VisitFloat64Div(Node* node) {
  VisitRRR(this, kMipsDivD, node);
}


void InstructionSelector::VisitFloat64Mod(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsModD, g.DefineAsFixed(node, f0), g.UseFixed(node->InputAt(0), f12),
       g.UseFixed(node->InputAt(1), f14))->MarkAsCall();
}


void InstructionSelector::VisitFloat64Sqrt(Node* node) {
  MipsOperandGenerator g(this);
  Emit(kMipsSqrtD, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
}

void InstructionSelector::VisitCall(Node* call, BasicBlock* continuation,
                                    BasicBlock* deoptimization) {
  MipsOperandGenerator g(this);
  CallDescriptor* descriptor = OpParameter<CallDescriptor*>(call);

  FrameStateDescriptor* frame_state_descriptor = NULL;
  if (descriptor->NeedsFrameState()) {
    frame_state_descriptor =
        GetFrameStateDescriptor(call->InputAt(descriptor->InputCount()));
  }

  CallBuffer buffer(zone(), descriptor, frame_state_descriptor);

  // Compute InstructionOperands for inputs and outputs.
  InitializeCallBuffer(call, &buffer, true, false);

  // TODO(dcarney): might be possible to use claim/poke instead
  // Push any stack arguments.
  for (NodeVectorRIter input = buffer.pushed_nodes.rbegin();
       input != buffer.pushed_nodes.rend(); input++) {
    // TODO(plind): inefficient for MIPS, use MultiPush here.
    //    - Also need to align the stack. See arm64.
    //    - Maybe combine with arg slot stuff in DirectCEntry stub.
    Emit(kMipsPush, NULL, g.UseRegister(*input));
  }

  // Select the appropriate opcode based on the call type.
  InstructionCode opcode;
  switch (descriptor->kind()) {
    case CallDescriptor::kCallCodeObject: {
      opcode = kArchCallCodeObject;
      break;
    }
    case CallDescriptor::kCallJSFunction:
      opcode = kArchCallJSFunction;
      break;
    default:
      UNREACHABLE();
      return;
  }
  opcode |= MiscField::encode(descriptor->flags());

  // Emit the call instruction.
  Instruction* call_instr =
      Emit(opcode, buffer.outputs.size(), &buffer.outputs.front(),
           buffer.instruction_args.size(), &buffer.instruction_args.front());

  call_instr->MarkAsCall();
  if (deoptimization != NULL) {
    DCHECK(continuation != NULL);
    call_instr->MarkAsControl();
  }
}


void InstructionSelector::VisitInt32AddWithOverflow(Node* node,
                                                    FlagsContinuation* cont) {
  VisitBinop(this, node, kMipsAddOvf, cont);
}


void InstructionSelector::VisitInt32SubWithOverflow(Node* node,
                                                    FlagsContinuation* cont) {
  VisitBinop(this, node, kMipsSubOvf, cont);
}


// Shared routine for multiple compare operations.
static void VisitCompare(InstructionSelector* selector, InstructionCode opcode,
                         InstructionOperand* left, InstructionOperand* right,
                         FlagsContinuation* cont) {
  MipsOperandGenerator g(selector);
  opcode = cont->Encode(opcode);
  if (cont->IsBranch()) {
    selector->Emit(opcode, NULL, left, right, g.Label(cont->true_block()),
                   g.Label(cont->false_block()))->MarkAsControl();
  } else {
    DCHECK(cont->IsSet());
    // TODO(plind): Revisit and test this path.
    selector->Emit(opcode, g.DefineAsRegister(cont->result()), left, right);
  }
}


// Shared routine for multiple word compare operations.
static void VisitWordCompare(InstructionSelector* selector, Node* node,
                             InstructionCode opcode, FlagsContinuation* cont,
                             bool commutative) {
  MipsOperandGenerator g(selector);
  Node* left = node->InputAt(0);
  Node* right = node->InputAt(1);

  // Match immediates on left or right side of comparison.
  if (g.CanBeImmediate(right, opcode)) {
    VisitCompare(selector, opcode, g.UseRegister(left), g.UseImmediate(right),
                 cont);
  } else if (g.CanBeImmediate(left, opcode)) {
    if (!commutative) cont->Commute();
    VisitCompare(selector, opcode, g.UseRegister(right), g.UseImmediate(left),
                 cont);
  } else {
    VisitCompare(selector, opcode, g.UseRegister(left), g.UseRegister(right),
                 cont);
  }
}


void InstructionSelector::VisitWord32Test(Node* node, FlagsContinuation* cont) {
  switch (node->opcode()) {
    case IrOpcode::kWord32And:
      // TODO(plind): understand the significance of 'IR and' special case.
      return VisitWordCompare(this, node, kMipsTst, cont, true);
    default:
      break;
  }

  MipsOperandGenerator g(this);
  // kMipsTst is a pseudo-instruction to do logical 'and' and leave the result
  // in a dedicated tmp register.
  VisitCompare(this, kMipsTst, g.UseRegister(node), g.UseRegister(node), cont);
}


void InstructionSelector::VisitWord32Compare(Node* node,
                                             FlagsContinuation* cont) {
  VisitWordCompare(this, node, kMipsCmp, cont, false);
}


void InstructionSelector::VisitFloat64Compare(Node* node,
                                              FlagsContinuation* cont) {
  MipsOperandGenerator g(this);
  Node* left = node->InputAt(0);
  Node* right = node->InputAt(1);
  VisitCompare(this, kMipsCmpD, g.UseRegister(left), g.UseRegister(right),
               cont);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
