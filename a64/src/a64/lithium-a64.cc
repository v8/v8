// Copyright 2013 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "lithium-allocator-inl.h"
#include "a64/lithium-a64.h"
#include "a64/lithium-codegen-a64.h"

namespace v8 {
namespace internal {


#define DEFINE_COMPILE(type)                            \
  void L##type::CompileToNative(LCodeGen* generator) {  \
    generator->Do##type(this);                          \
  }
LITHIUM_CONCRETE_INSTRUCTION_LIST(DEFINE_COMPILE)
#undef DEFINE_COMPILE


void LLabel::PrintDataTo(StringStream* stream) {
  LGap::PrintDataTo(stream);
  LLabel* rep = replacement();
  if (rep != NULL) {
    stream->Add(" Dead block replaced with B%d", rep->block_id());
  }
}


void LAccessArgumentsAt::PrintDataTo(StringStream* stream) {
  arguments()->PrintTo(stream);
  stream->Add(" length ");
  length()->PrintTo(stream);
  stream->Add(" index ");
  index()->PrintTo(stream);
}


void LBranch::PrintDataTo(StringStream* stream) {
  stream->Add("B%d | B%d on ", true_block_id(), false_block_id());
  value()->PrintTo(stream);
}


void LCallConstantFunction::PrintDataTo(StringStream* stream) {
  stream->Add("#%d / ", arity());
}


void LCallNamed::PrintDataTo(StringStream* stream) {
  SmartArrayPointer<char> name_string = name()->ToCString();
  stream->Add("%s #%d / ", *name_string, arity());
}


void LCallKeyed::PrintDataTo(StringStream* stream) {
  stream->Add("[x2] #%d / ", arity());
}


void LCallKnownGlobal::PrintDataTo(StringStream* stream) {
  stream->Add("#%d / ", arity());
}


void LCallGlobal::PrintDataTo(StringStream* stream) {
  SmartArrayPointer<char> name_string = name()->ToCString();
  stream->Add("%s #%d / ", *name_string, arity());
}


void LCallNew::PrintDataTo(StringStream* stream) {
  stream->Add("= ");
  constructor()->PrintTo(stream);
  stream->Add(" #%d / ", arity());
}


void LCallNewArray::PrintDataTo(StringStream* stream) {
  stream->Add("= ");
  constructor()->PrintTo(stream);
  stream->Add(" #%d / ", arity());
  ASSERT(hydrogen()->property_cell()->value()->IsSmi());
  ElementsKind kind = static_cast<ElementsKind>(
      Smi::cast(hydrogen()->property_cell()->value())->value());
  stream->Add(" (%s) ", ElementsKindToString(kind));
}


void LClassOfTestAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if class_of_test(");
  value()->PrintTo(stream);
  stream->Add(", \"%o\") then B%d else B%d",
              *hydrogen()->class_name(),
              true_block_id(),
              false_block_id());
}


void LCmpIDAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if ");
  left()->PrintTo(stream);
  stream->Add(" %s ", Token::String(op()));
  right()->PrintTo(stream);
  stream->Add(" then B%d else B%d", true_block_id(), false_block_id());
}


void LHasCachedArrayIndexAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if has_cached_array_index(");
  value()->PrintTo(stream);
  stream->Add(") then B%d else B%d", true_block_id(), false_block_id());
}


bool LGoto::HasInterestingComment(LCodeGen* gen) const {
  return !gen->IsNextEmittedBlock(block_id());
}


void LGoto::PrintDataTo(StringStream* stream) {
  stream->Add("B%d", block_id());
}


void LInnerAllocatedObject::PrintDataTo(StringStream* stream) {
  stream->Add(" = ");
  base_object()->PrintTo(stream);
  stream->Add(" + %d", offset());
}


void LInvokeFunction::PrintDataTo(StringStream* stream) {
  stream->Add("= ");
  function()->PrintTo(stream);
  stream->Add(" #%d / ", arity());
}


void LInstruction::PrintTo(StringStream* stream) {
  stream->Add("%s ", this->Mnemonic());

  PrintOutputOperandTo(stream);

  PrintDataTo(stream);

  if (HasEnvironment()) {
    stream->Add(" ");
    environment()->PrintTo(stream);
  }

  if (HasPointerMap()) {
    stream->Add(" ");
    pointer_map()->PrintTo(stream);
  }
}


void LInstruction::PrintDataTo(StringStream* stream) {
  stream->Add("= ");
  for (int i = 0; i < InputCount(); i++) {
    if (i > 0) stream->Add(" ");
    if (InputAt(i) == NULL) {
      stream->Add("NULL");
    } else {
      InputAt(i)->PrintTo(stream);
    }
  }
}


void LInstruction::PrintOutputOperandTo(StringStream* stream) {
  if (HasResult()) result()->PrintTo(stream);
}


void LHasInstanceTypeAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if has_instance_type(");
  value()->PrintTo(stream);
  stream->Add(") then B%d else B%d", true_block_id(), false_block_id());
}


void LIsObjectAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if is_object(");
  value()->PrintTo(stream);
  stream->Add(") then B%d else B%d", true_block_id(), false_block_id());
}


void LIsStringAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if is_string(");
  value()->PrintTo(stream);
  stream->Add(") then B%d else B%d", true_block_id(), false_block_id());
}


void LIsSmiAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if is_smi(");
  value()->PrintTo(stream);
  stream->Add(") then B%d else B%d", true_block_id(), false_block_id());
}


void LTypeofIsAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if typeof ");
  value()->PrintTo(stream);
  stream->Add(" == \"%s\" then B%d else B%d",
              *hydrogen()->type_literal()->ToCString(),
              true_block_id(), false_block_id());
}


void LIsUndetectableAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if is_undetectable(");
  value()->PrintTo(stream);
  stream->Add(") then B%d else B%d", true_block_id(), false_block_id());
}


bool LGap::IsRedundant() const {
  for (int i = 0; i < 4; i++) {
    if ((parallel_moves_[i] != NULL) && !parallel_moves_[i]->IsRedundant()) {
      return false;
    }
  }

  return true;
}


void LGap::PrintDataTo(StringStream* stream) {
  for (int i = 0; i < 4; i++) {
    stream->Add("(");
    if (parallel_moves_[i] != NULL) {
      parallel_moves_[i]->PrintDataTo(stream);
    }
    stream->Add(") ");
  }
}


void LLoadContextSlot::PrintDataTo(StringStream* stream) {
  context()->PrintTo(stream);
  stream->Add("[%d]", slot_index());
}


void LStoreContextSlot::PrintDataTo(StringStream* stream) {
  context()->PrintTo(stream);
  stream->Add("[%d] <- ", slot_index());
  value()->PrintTo(stream);
}


void LStoreKeyedGeneric::PrintDataTo(StringStream* stream) {
  object()->PrintTo(stream);
  stream->Add("[");
  key()->PrintTo(stream);
  stream->Add("] <- ");
  value()->PrintTo(stream);
}


void LStoreNamedField::PrintDataTo(StringStream* stream) {
  object()->PrintTo(stream);
  hydrogen()->access().PrintTo(stream);
  stream->Add(" <- ");
  value()->PrintTo(stream);
}


void LStoreNamedGeneric::PrintDataTo(StringStream* stream) {
  object()->PrintTo(stream);
  stream->Add(".");
  stream->Add(*String::cast(*name())->ToCString());
  stream->Add(" <- ");
  value()->PrintTo(stream);
}


void LStringCompareAndBranch::PrintDataTo(StringStream* stream) {
  stream->Add("if string_compare(");
  left()->PrintTo(stream);
  right()->PrintTo(stream);
  stream->Add(") then B%d else B%d", true_block_id(), false_block_id());
}


void LTransitionElementsKind::PrintDataTo(StringStream* stream) {
  object()->PrintTo(stream);
  stream->Add("%p -> %p", *original_map(), *transitioned_map());
}


template<int T>
void LUnaryMathOperation<T>::PrintDataTo(StringStream* stream) {
  value()->PrintTo(stream);
}


const char* LArithmeticD::Mnemonic() const {
  switch (op()) {
    case Token::ADD: return "add-d";
    case Token::SUB: return "sub-d";
    case Token::MUL: return "mul-d";
    case Token::DIV: return "div-d";
    case Token::MOD: return "mod-d";
    default:
      UNREACHABLE();
      return NULL;
  }
}


const char* LArithmeticT::Mnemonic() const {
  switch (op()) {
    case Token::ADD: return "add-t";
    case Token::SUB: return "sub-t";
    case Token::MUL: return "mul-t";
    case Token::MOD: return "mod-t";
    case Token::DIV: return "div-t";
    case Token::BIT_AND: return "bit-and-t";
    case Token::BIT_OR: return "bit-or-t";
    case Token::BIT_XOR: return "bit-xor-t";
    case Token::ROR: return "ror-t";
    case Token::SHL: return "shl-t";
    case Token::SAR: return "sar-t";
    case Token::SHR: return "shr-t";
    default:
      UNREACHABLE();
      return NULL;
  }
}


void LChunkBuilder::Abort(const char* reason) {
  info()->set_bailout_reason(reason);
  status_ = ABORTED;
}


LUnallocated* LChunkBuilder::ToUnallocated(Register reg) {
  return new(zone()) LUnallocated(LUnallocated::FIXED_REGISTER,
                                  Register::ToAllocationIndex(reg));
}


LUnallocated* LChunkBuilder::ToUnallocated(DoubleRegister reg) {
  return new(zone()) LUnallocated(LUnallocated::FIXED_DOUBLE_REGISTER,
                                  DoubleRegister::ToAllocationIndex(reg));
}


LOperand* LChunkBuilder::Use(HValue* value, LUnallocated* operand) {
  if (value->EmitAtUses()) {
    HInstruction* instr = HInstruction::cast(value);
    VisitInstruction(instr);
  }
  operand->set_virtual_register(value->id());
  return operand;
}


LOperand* LChunkBuilder::UseFixed(HValue* value, Register fixed_register) {
  return Use(value, ToUnallocated(fixed_register));
}


LOperand* LChunkBuilder::UseFixedDouble(HValue* value,
                                        DoubleRegister fixed_register) {
  return Use(value, ToUnallocated(fixed_register));
}


LOperand* LChunkBuilder::UseRegister(HValue* value) {
  return Use(value, new(zone()) LUnallocated(LUnallocated::MUST_HAVE_REGISTER));
}


LOperand* LChunkBuilder::UseRegisterAndClobber(HValue* value) {
  return Use(value, new(zone()) LUnallocated(LUnallocated::WRITABLE_REGISTER));
}


LOperand* LChunkBuilder::UseRegisterAtStart(HValue* value) {
  return Use(value,
             new(zone()) LUnallocated(LUnallocated::MUST_HAVE_REGISTER,
                                      LUnallocated::USED_AT_START));
}


LOperand* LChunkBuilder::UseRegisterOrConstant(HValue* value) {
  return value->IsConstant() ? UseConstant(value) : UseRegister(value);
}


LOperand* LChunkBuilder::UseRegisterOrConstantAtStart(HValue* value) {
  return value->IsConstant() ? UseConstant(value) : UseRegisterAtStart(value);
}


LConstantOperand* LChunkBuilder::UseConstant(HValue* value) {
  return chunk_->DefineConstantOperand(HConstant::cast(value));
}


LOperand* LChunkBuilder::UseAny(HValue* value) {
  return value->IsConstant()
      ? UseConstant(value)
      : Use(value, new(zone()) LUnallocated(LUnallocated::ANY));
}


template<int I, int T>
LInstruction* LChunkBuilder::Define(LTemplateInstruction<1, I, T>* instr,
                                    LUnallocated* result) {
  result->set_virtual_register(current_instruction_->id());
  instr->set_result(result);
  return instr;
}


template<int I, int T>
LInstruction* LChunkBuilder::DefineAsRegister(
    LTemplateInstruction<1, I, T>* instr) {
  return Define(instr,
                new(zone()) LUnallocated(LUnallocated::MUST_HAVE_REGISTER));
}


template<int I, int T>
LInstruction* LChunkBuilder::DefineAsSpilled(
    LTemplateInstruction<1, I, T>* instr, int index) {
  return Define(instr,
                new(zone()) LUnallocated(LUnallocated::FIXED_SLOT, index));
}


template<int I, int T>
LInstruction* LChunkBuilder::DefineSameAsFirst(
    LTemplateInstruction<1, I, T>* instr) {
  return Define(instr,
                new(zone()) LUnallocated(LUnallocated::SAME_AS_FIRST_INPUT));
}


template<int I, int T>
LInstruction* LChunkBuilder::DefineFixed(
    LTemplateInstruction<1, I, T>* instr, Register reg) {
  return Define(instr, ToUnallocated(reg));
}


template<int I, int T>
LInstruction* LChunkBuilder::DefineFixedDouble(
    LTemplateInstruction<1, I, T>* instr, DoubleRegister reg) {
  return Define(instr, ToUnallocated(reg));
}


LInstruction* LChunkBuilder::MarkAsCall(LInstruction* instr,
                                        HInstruction* hinstr,
                                        CanDeoptimize can_deoptimize) {
  info()->MarkAsNonDeferredCalling();
  // TODO(all): Verify call in debug mode.
  instr->MarkAsCall();
  instr = AssignPointerMap(instr);

  if (hinstr->HasObservableSideEffects()) {
    ASSERT(hinstr->next()->IsSimulate());
    HSimulate* sim = HSimulate::cast(hinstr->next());
    ASSERT(instruction_pending_deoptimization_environment_ == NULL);
    ASSERT(pending_deoptimization_ast_id_.IsNone());
    instruction_pending_deoptimization_environment_ = instr;
    pending_deoptimization_ast_id_ = sim->ast_id();
  }

  // If instruction does not have side-effects lazy deoptimization
  // after the call will try to deoptimize to the point before the call.
  // Thus we still need to attach environment to this call even if
  // call sequence can not deoptimize eagerly.
  bool needs_environment =
      (can_deoptimize == CAN_DEOPTIMIZE_EAGERLY) ||
      !hinstr->HasObservableSideEffects();
  if (needs_environment && !instr->HasEnvironment()) {
    instr = AssignEnvironment(instr);
  }

  return instr;
}


LInstruction* LChunkBuilder::AssignPointerMap(LInstruction* instr) {
  ASSERT(!instr->HasPointerMap());
  instr->set_pointer_map(new(zone()) LPointerMap(position_, zone()));
  return instr;
}


LUnallocated* LChunkBuilder::TempRegister() {
  LUnallocated* operand =
      new(zone()) LUnallocated(LUnallocated::MUST_HAVE_REGISTER);
  int vreg = allocator_->GetVirtualRegister();
  if (!allocator_->AllocationOk()) {
    Abort("Out of virtual registers while trying to allocate temp register.");
    vreg = 0;
  }
  operand->set_virtual_register(vreg);
  return operand;
}


int LPlatformChunk::GetNextSpillIndex() {
  return spill_slot_count_++;
}


LOperand* LPlatformChunk::GetNextSpillSlot(bool is_double) {
  int index = GetNextSpillIndex();
  if (is_double) {
    return LDoubleStackSlot::Create(index, zone());
  } else {
    return LStackSlot::Create(index, zone());
  }
}


LOperand* LChunkBuilder::FixedTemp(DoubleRegister reg) {
  LUnallocated* operand = ToUnallocated(reg);
  ASSERT(operand->HasFixedPolicy());
  return operand;
}


LPlatformChunk* LChunkBuilder::Build() {
  ASSERT(is_unused());
  chunk_ = new(zone_) LPlatformChunk(info_, graph_);
  LPhase phase("L_Building chunk", chunk_);
  status_ = BUILDING;
  const ZoneList<HBasicBlock*>* blocks = graph_->blocks();
  for (int i = 0; i < blocks->length(); i++) {
    DoBasicBlock(blocks->at(i));
    if (is_aborted()) return NULL;
  }
  status_ = DONE;
  return chunk_;
}


void LChunkBuilder::DoBasicBlock(HBasicBlock* block) {
  ASSERT(is_building());
  current_block_ = block;

  if (block->IsStartBlock()) {
    block->UpdateEnvironment(graph_->start_environment());
    argument_count_ = 0;
  } else if (block->predecessors()->length() == 1) {
    // We have a single predecessor => copy environment and outgoing
    // argument count from the predecessor.
    ASSERT(block->phis()->length() == 0);
    HBasicBlock* pred = block->predecessors()->at(0);
    HEnvironment* last_environment = pred->last_environment();
    ASSERT(last_environment != NULL);

    // Only copy the environment, if it is later used again.
    if (pred->end()->SecondSuccessor() == NULL) {
      ASSERT(pred->end()->FirstSuccessor() == block);
    } else {
      if ((pred->end()->FirstSuccessor()->block_id() > block->block_id()) ||
          (pred->end()->SecondSuccessor()->block_id() > block->block_id())) {
        last_environment = last_environment->Copy();
      }
    }
    block->UpdateEnvironment(last_environment);
    ASSERT(pred->argument_count() >= 0);
    argument_count_ = pred->argument_count();
  } else {
    // We are at a state join => process phis.
    HBasicBlock* pred = block->predecessors()->at(0);
    // No need to copy the environment, it cannot be used later.
    HEnvironment* last_environment = pred->last_environment();
    for (int i = 0; i < block->phis()->length(); ++i) {
      HPhi* phi = block->phis()->at(i);
      if (phi->merged_index() < last_environment->length()) {
        last_environment->SetValueAt(phi->merged_index(), phi);
      }
    }
    for (int i = 0; i < block->deleted_phis()->length(); ++i) {
      if (block->deleted_phis()->at(i) < last_environment->length()) {
        last_environment->SetValueAt(block->deleted_phis()->at(i),
                                     graph_->GetConstantUndefined());
      }
    }
    block->UpdateEnvironment(last_environment);
    // Pick up the outgoing argument count of one of the predecessors.
    argument_count_ = pred->argument_count();
  }

  // Translate hydrogen instructions to lithium ones for the current block.
  HInstruction* current = block->first();
  int start = chunk_->instructions()->length();
  while ((current != NULL) && !is_aborted()) {
    // Code for constants in registers is generated lazily.
    if (!current->EmitAtUses()) {
      VisitInstruction(current);
    }
    current = current->next();
  }
  int end = chunk_->instructions()->length() - 1;
  if (end >= start) {
    block->set_first_instruction_index(start);
    block->set_last_instruction_index(end);
  }
  block->set_argument_count(argument_count_);
  current_block_ = NULL;
}


void LChunkBuilder::VisitInstruction(HInstruction* current) {
  HInstruction* old_current = current_instruction_;
  current_instruction_ = current;
  if (current->has_position()) position_ = current->position();
  LInstruction* instr = current->CompileToLithium(this);

  if (instr != NULL) {
#if DEBUG
    // Make sure that the lithium instruction has either no fixed register
    // constraints in temps or the result OR no uses that are only used at
    // start. If this invariant doesn't hold, the register allocator can decide
    // to insert a split of a range immediately before the instruction due to an
    // already allocated register needing to be used for the instruction's fixed
    // register constraint. In this case, the register allocator won't see an
    // interference between the split child and the use-at-start (it would if
    // the it was just a plain use), so it is free to move the split child into
    // the same register that is used for the use-at-start.
    // See https://code.google.com/p/chromium/issues/detail?id=201590
    if (!(instr->ClobbersRegisters() && instr->ClobbersDoubleRegisters())) {
      int fixed = 0;
      int used_at_start = 0;
      for (UseIterator it(instr); !it.Done(); it.Advance()) {
        LUnallocated* operand = LUnallocated::cast(it.Current());
        if (operand->IsUsedAtStart()) ++used_at_start;
      }
      if (instr->Output() != NULL) {
        if (LUnallocated::cast(instr->Output())->HasFixedPolicy()) ++fixed;
      }
      for (TempIterator it(instr); !it.Done(); it.Advance()) {
        LUnallocated* operand = LUnallocated::cast(it.Current());
        if (operand->HasFixedPolicy()) ++fixed;
      }
      ASSERT(fixed == 0 || used_at_start == 0);
    }
#endif

    // TODO(all): Add support for FLAG_stress_pointer_maps and
    // FLAG_stress_environments.
    instr->set_hydrogen_value(current);
    chunk_->AddInstruction(instr, current_block_);
  }
  current_instruction_ = old_current;
}


LInstruction* LChunkBuilder::AssignEnvironment(LInstruction* instr) {
  HEnvironment* hydrogen_env = current_block_->last_environment();
  int argument_index_accumulator = 0;
  instr->set_environment(CreateEnvironment(hydrogen_env,
                                           &argument_index_accumulator));
  return instr;
}


LEnvironment* LChunkBuilder::CreateEnvironment(
    HEnvironment* hydrogen_env,
    int* argument_index_accumulator) {
  if (hydrogen_env == NULL) return NULL;

  LEnvironment* outer =
      CreateEnvironment(hydrogen_env->outer(), argument_index_accumulator);
  BailoutId ast_id = hydrogen_env->ast_id();
  ASSERT(!ast_id.IsNone() || (hydrogen_env->frame_type() != JS_FUNCTION));
  int value_count = hydrogen_env->length() - hydrogen_env->specials_count();

  LEnvironment* result = new(zone()) LEnvironment(
      hydrogen_env->closure(),
      hydrogen_env->frame_type(),
      ast_id,
      hydrogen_env->parameter_count(),
      argument_count(),
      value_count,
      outer,
      hydrogen_env->entry(),
      zone());

  bool needs_arguments_object_materialization = false;
  int argument_index = *argument_index_accumulator;
  for (int i = 0; i < hydrogen_env->length(); ++i) {
    if (hydrogen_env->is_special_index(i)) continue;

    HValue* value = hydrogen_env->values()->at(i);
    LOperand* op = NULL;
    if (value->IsArgumentsObject()) {
      needs_arguments_object_materialization = true;
      op = NULL;
    } else if (value->IsPushArgument()) {
      op = new(zone()) LArgument(argument_index++);
    } else {
      op = UseAny(value);
    }
    result->AddValue(op,
                     value->representation(),
                     value->CheckFlag(HInstruction::kUint32));
  }

  if (needs_arguments_object_materialization) {
    HArgumentsObject* arguments = hydrogen_env->entry() == NULL
        ? graph()->GetArgumentsObject()
        : hydrogen_env->entry()->arguments_object();
    ASSERT(arguments->IsLinked());
    for (int i = 1; i < arguments->arguments_count(); ++i) {
      HValue* value = arguments->arguments_values()->at(i);
      ASSERT(!value->IsArgumentsObject() && !value->IsPushArgument());
      LOperand* op = UseAny(value);
      result->AddValue(op,
                       value->representation(),
                       value->CheckFlag(HInstruction::kUint32));
    }
  }

  if (hydrogen_env->frame_type() == JS_FUNCTION) {
    *argument_index_accumulator = argument_index;
  }

  return result;
}


LInstruction* LChunkBuilder::DoArithmeticD(Token::Value op,
                                           HArithmeticBinaryOperation* instr) {
  ASSERT(instr->representation().IsDouble());
  ASSERT(instr->left()->representation().IsDouble());
  ASSERT(instr->right()->representation().IsDouble());

  if (op == Token::MOD) {
    LOperand* left = UseFixedDouble(instr->left(), d0);
    LOperand* right = UseFixedDouble(instr->right(), d1);
    LArithmeticD* result = new(zone()) LArithmeticD(Token::MOD, left, right);
    return MarkAsCall(DefineFixedDouble(result, d0), instr);
  } else {
    LOperand* left = UseRegisterAtStart(instr->left());
    LOperand* right = UseRegisterAtStart(instr->right());
    LArithmeticD* result = new(zone()) LArithmeticD(op, left, right);
    return DefineAsRegister(result);
  }
}


LInstruction* LChunkBuilder::DoArithmeticT(Token::Value op,
                                           HBinaryOperation* instr) {
  ASSERT((op == Token::ADD) || (op == Token::SUB) || (op == Token::MUL) ||
         (op == Token::DIV) || (op == Token::MOD) || (op == Token::SHR) ||
         (op == Token::SHL) || (op == Token::SAR) || (op == Token::ROR) ||
         (op == Token::BIT_OR) || (op == Token::BIT_AND) ||
         (op == Token::BIT_XOR));
  HValue* left = instr->left();
  HValue* right = instr->right();

  ASSERT(instr->representation().IsSmiOrTagged());
  ASSERT(left->representation().IsSmiOrTagged());
  ASSERT(right->representation().IsSmiOrTagged());

  LOperand* left_operand = UseFixed(left, x1);
  LOperand* right_operand = UseFixed(right, x0);
  LArithmeticT* result =
      new(zone()) LArithmeticT(op, left_operand, right_operand);
  return MarkAsCall(DefineFixed(result, x0), instr);
}


#define UNIMPLEMENTED_INSTRUCTION()                                     \
  do {                                                                  \
    static char msg[128];                                               \
    snprintf(msg, sizeof(msg),                                          \
             "unsupported hydrogen instruction (%s)", __func__);        \
    info_->set_bailout_reason(msg);                                     \
    status_ = ABORTED;                                                  \
    return NULL;                                                        \
  } while (0)


LInstruction* LChunkBuilder::DoBoundsCheckBaseIndexInformation(
    HBoundsCheckBaseIndexInformation* instr) {
  UNREACHABLE();
  return NULL;
}


LInstruction* LChunkBuilder::DoAbnormalExit(HAbnormalExit* instr) {
  // The control instruction marking the end of a block that completed abruptly
  // (e.g., threw an exception). There is nothing specific to do.
  return NULL;
}


LInstruction* LChunkBuilder::DoAccessArgumentsAt(HAccessArgumentsAt* instr) {
  info()->MarkAsRequiresFrame();
  LOperand* args = NULL;
  LOperand* length = NULL;
  LOperand* index = NULL;
  LOperand* temp = NULL;

  if (instr->length()->IsConstant() && instr->index()->IsConstant()) {
    args = UseRegisterAtStart(instr->arguments());
    length = UseConstant(instr->length());
    index = UseConstant(instr->index());
  } else {
    args = UseRegister(instr->arguments());
    length = UseRegisterAtStart(instr->length());
    index = UseRegisterOrConstantAtStart(instr->index());
    temp = TempRegister();
  }

  return DefineAsRegister(
      new(zone()) LAccessArgumentsAt(args, length, index, temp));
}


LInstruction* LChunkBuilder::DoAdd(HAdd* instr) {
  if (instr->representation().IsInteger32()) {
    // TODO(all): LAddI instruction should also handle the addition of
    // two SMI values.
    ASSERT(instr->left()->representation().IsInteger32());
    ASSERT(instr->right()->representation().IsInteger32());
    LOperand* left = UseRegisterAtStart(instr->BetterLeftOperand());
    LOperand* right =
        UseRegisterOrConstantAtStart(instr->BetterRightOperand());
    LInstruction* result = DefineAsRegister(new(zone()) LAddI(left, right));
    if (instr->CheckFlag(HValue::kCanOverflow)) {
      result = AssignEnvironment(result);
    }
    return result;
  } else if (instr->representation().IsDouble()) {
    return DoArithmeticD(Token::ADD, instr);
  } else {
    ASSERT(instr->representation().IsSmiOrTagged());
    return DoArithmeticT(Token::ADD, instr);
  }
}


LInstruction* LChunkBuilder::DoAllocate(HAllocate* instr) {
  info()->MarkAsDeferredCalling();
  LOperand* size = UseRegisterOrConstant(instr->size());
  LOperand* temp1 = TempRegister();
  LOperand* temp2 = TempRegister();
  LAllocate* result = new(zone()) LAllocate(size, temp1, temp2);
  return AssignPointerMap(DefineAsRegister(result));
}


LInstruction* LChunkBuilder::DoAllocateObject(HAllocateObject* instr) {
  info()->MarkAsDeferredCalling();
  LAllocateObject* result =
      new(zone()) LAllocateObject(TempRegister(), TempRegister());
  return AssignPointerMap(DefineAsRegister(result));
}


LInstruction* LChunkBuilder::DoApplyArguments(HApplyArguments* instr) {
  LOperand* function = UseFixed(instr->function(), x1);
  LOperand* receiver = UseFixed(instr->receiver(), x0);
  LOperand* length = UseFixed(instr->length(), x2);
  LOperand* elements = UseFixed(instr->elements(), x3);
  LApplyArguments* result = new(zone()) LApplyArguments(function,
                                                        receiver,
                                                        length,
                                                        elements);
  return MarkAsCall(DefineFixed(result, x0), instr, CAN_DEOPTIMIZE_EAGERLY);
}


LInstruction* LChunkBuilder::DoArgumentsElements(HArgumentsElements* instr) {
  info()->MarkAsRequiresFrame();
  LOperand* temp = instr->from_inlined() ? NULL : TempRegister();
  return DefineAsRegister(new(zone()) LArgumentsElements(temp));
}


LInstruction* LChunkBuilder::DoArgumentsLength(HArgumentsLength* instr) {
  info()->MarkAsRequiresFrame();
  LOperand* value = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new(zone()) LArgumentsLength(value));
}


LInstruction* LChunkBuilder::DoArgumentsObject(HArgumentsObject* instr) {
  // There are no real uses of the arguments object.
  // arguments.length and element access are supported directly on
  // stack arguments, and any real arguments object use causes a bailout.
  // So this value is never used.
  return NULL;
}


LInstruction* LChunkBuilder::DoBitwise(HBitwise* instr) {
  // TODO(all): The latest bleeding_edge handles smi representations too.
  if (instr->representation().IsInteger32()) {
    ASSERT(instr->left()->representation().IsInteger32());
    ASSERT(instr->right()->representation().IsInteger32());

    LOperand* left = UseRegisterAtStart(instr->BetterLeftOperand());
    LOperand* right =
        UseRegisterOrConstantAtStart(instr->BetterRightOperand());
    return DefineAsRegister(new(zone()) LBitI(left, right));
  } else {
    return DoArithmeticT(instr->op(), instr);
  }
}


LInstruction* LChunkBuilder::DoBitNot(HBitNot* instr) {
  ASSERT(instr->value()->representation().IsInteger32());
  ASSERT(instr->representation().IsInteger32());
  if (instr->HasNoUses()) return NULL;
  LOperand* value = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new(zone()) LBitNotI(value));
}


LInstruction* LChunkBuilder::DoBlockEntry(HBlockEntry* instr) {
  // V8 expects a label to be generated for each basic block.
  // This is used in some places like LAllocator::IsBlockBoundary
  // in lithium-allocator.cc
  return new(zone()) LLabel(instr->block());
}


LInstruction* LChunkBuilder::DoBoundsCheck(HBoundsCheck* instr) {
  LOperand* value = UseRegisterOrConstantAtStart(instr->index());
  LOperand* length = UseRegister(instr->length());
  return AssignEnvironment(new(zone()) LBoundsCheck(value, length));
}


LInstruction* LChunkBuilder::DoBranch(HBranch* instr) {
  HValue* value = instr->value();

  if (value->EmitAtUses()) {
    // TODO(all): Handle this case with CheckElideControlInstruction once
    // we've done the rebase.
    HBasicBlock* successor = HConstant::cast(value)->BooleanValue()
        ? instr->FirstSuccessor()
        : instr->SecondSuccessor();
    return new(zone()) LGoto(successor->block_id());
  }

  Representation r = value->representation();
  HType type = value->type();

  if (r.IsInteger32() || r.IsSmi() || r.IsDouble()) {
    // These representations have simple checks that cannot deoptimize.
    return new(zone()) LBranch(UseRegister(value), NULL, NULL);
  } else {
    ASSERT(r.IsTagged());
    if (type.IsBoolean() || type.IsSmi() || type.IsJSArray() ||
        type.IsHeapNumber()) {
      // These types have simple checks that cannot deoptimize.
      return new(zone()) LBranch(UseRegister(value), NULL, NULL);
    }

    if (type.IsString()) {
      // This type cannot deoptimize, but needs a scratch register.
      return new(zone()) LBranch(UseRegister(value), TempRegister(), NULL);
    }

    ToBooleanStub::Types expected = instr->expected_input_types();
    bool needs_temps = expected.NeedsMap() || expected.IsEmpty();
    LOperand* temp1 = needs_temps ? TempRegister() : NULL;
    LOperand* temp2 = needs_temps ? TempRegister() : NULL;

    if (expected.IsGeneric() || expected.IsEmpty()) {
      // The generic case cannot deoptimize because it already supports every
      // possible input type.
      ASSERT(needs_temps);
      return new(zone()) LBranch(UseRegister(value), temp1, temp2);
    } else {
      return AssignEnvironment(
          new(zone()) LBranch(UseRegister(value), temp1, temp2));
    }
  }
}


LInstruction* LChunkBuilder::DoCallConstantFunction(
    HCallConstantFunction* instr) {
  argument_count_ -= instr->argument_count();
  return MarkAsCall(DefineFixed(new(zone()) LCallConstantFunction, x0), instr);
}


LInstruction* LChunkBuilder::DoCallFunction(HCallFunction* instr) {
  LOperand* function = UseFixed(instr->function(), x1);
  argument_count_ -= instr->argument_count();
  LInstruction* result = DefineFixed(new(zone()) LCallFunction(function), x0);
  // TODO(all): Uncomment the following line during the rebase.
  // if (instr->IsTailCall()) return result;
  return MarkAsCall(result, instr);
}


LInstruction* LChunkBuilder::DoCallGlobal(HCallGlobal* instr) {
  argument_count_ -= instr->argument_count();
  return MarkAsCall(DefineFixed(new(zone()) LCallGlobal, x0), instr);
}


LInstruction* LChunkBuilder::DoCallKeyed(HCallKeyed* instr) {
  ASSERT(instr->key()->representation().IsTagged());
  argument_count_ -= instr->argument_count();
  LOperand* key = UseFixed(instr->key(), x2);
  return MarkAsCall(DefineFixed(new(zone()) LCallKeyed(key), x0), instr);
}


LInstruction* LChunkBuilder::DoCallKnownGlobal(HCallKnownGlobal* instr) {
  argument_count_ -= instr->argument_count();
  return MarkAsCall(DefineFixed(new(zone()) LCallKnownGlobal, x0), instr);
}


LInstruction* LChunkBuilder::DoCallNamed(HCallNamed* instr) {
  argument_count_ -= instr->argument_count();
  return MarkAsCall(DefineFixed(new(zone()) LCallNamed, x0), instr);
}


LInstruction* LChunkBuilder::DoCallNew(HCallNew* instr) {
  // The call to CallConstructStub will expect the constructor to be in x1.
  LOperand* constructor = UseFixed(instr->constructor(), x1);
  argument_count_ -= instr->argument_count();
  LCallNew* result = new(zone()) LCallNew(constructor);
  return MarkAsCall(DefineFixed(result, x0), instr);
}


LInstruction* LChunkBuilder::DoCallNewArray(HCallNewArray* instr) {
  // The call to ArrayConstructCode will expect the constructor to be in x1.
  LOperand* constructor = UseFixed(instr->constructor(), x1);
  argument_count_ -= instr->argument_count();
  LCallNewArray* result = new(zone()) LCallNewArray(constructor);
  return MarkAsCall(DefineFixed(result, x0), instr);
}


LInstruction* LChunkBuilder::DoCallRuntime(HCallRuntime* instr) {
  argument_count_ -= instr->argument_count();
  return MarkAsCall(DefineFixed(new(zone()) LCallRuntime, x0), instr);
}


LInstruction* LChunkBuilder::DoCallStub(HCallStub* instr) {
  argument_count_ -= instr->argument_count();
  return MarkAsCall(DefineFixed(new(zone()) LCallStub, x0), instr);
}


LInstruction* LChunkBuilder::DoChange(HChange* instr) {
  Representation from = instr->from();
  Representation to = instr->to();

  if (from.IsSmi()) {
    if (to.IsTagged()) {
      LOperand* value = UseRegister(instr->value());
      return DefineSameAsFirst(new(zone()) LDummyUse(value));
    }
    from = Representation::Tagged();
  }

  if (from.IsTagged()) {
    if (to.IsDouble()) {
      info()->MarkAsDeferredCalling();
      LOperand* value = UseRegister(instr->value());
      LOperand* temp = TempRegister();
      LNumberUntagD* res = new(zone()) LNumberUntagD(value, temp);
      return AssignEnvironment(DefineAsRegister(res));
    } else if (to.IsSmi()) {
      LOperand* value = UseRegister(instr->value());
      if (instr->value()->type().IsSmi()) {
        return DefineSameAsFirst(new(zone()) LDummyUse(value));
      }
      return AssignEnvironment(DefineSameAsFirst(new(zone()) LCheckSmi(value)));
    } else {
      ASSERT(to.IsInteger32());
      LInstruction* res = NULL;

      if (instr->value()->type().IsSmi()) {
        LOperand* value = UseRegisterAtStart(instr->value());
        res = DefineAsRegister(new(zone()) LSmiUntag(value, false));
      } else {
        LOperand* value = UseRegister(instr->value());
        LOperand* temp1 = TempRegister();
        LOperand* temp2 =
            instr->CanTruncateToInt32() ? TempRegister() : FixedTemp(d24);
        res = DefineAsRegister(new(zone()) LTaggedToI(value, temp1, temp2));
        res = AssignEnvironment(res);
      }

      return res;
    }
  } else if (from.IsDouble()) {
    if (to.IsTagged()) {
      info()->MarkAsDeferredCalling();
      LOperand* value = UseRegister(instr->value());
      LOperand* temp1 = TempRegister();
      LOperand* temp2 = TempRegister();

      LNumberTagD* result = new(zone()) LNumberTagD(value, temp1, temp2);
      return AssignPointerMap(DefineAsRegister(result));
    } else if (to.IsSmi()) {
      // TODO(all): Split LDoubleToSmi into two instructions.
      LOperand* value = UseRegister(instr->value());
      LOperand* temp1 = instr->CanTruncateToInt32() ? TempRegister() : NULL;
      LOperand* temp2 = instr->CanTruncateToInt32() ? TempRegister() : NULL;
      LDoubleToSmi* result = new(zone()) LDoubleToSmi(value, temp1, temp2);
      return AssignEnvironment(DefineAsRegister(result));
    } else {
      // TODO(all): Split LDoubleToI into two instructions, truncating and not.
      ASSERT(to.IsInteger32());
      LOperand* value = UseRegister(instr->value());
      LOperand* temp1 = instr->CanTruncateToInt32() ? TempRegister() : NULL;
      LOperand* temp2 = instr->CanTruncateToInt32() ? TempRegister() : NULL;
      LDoubleToI* result = new(zone()) LDoubleToI(value, temp1, temp2);
      return AssignEnvironment(DefineAsRegister(result));
    }
  } else if (from.IsInteger32()) {
    info()->MarkAsDeferredCalling();
    if (to.IsTagged()) {
      HValue* val = instr->value();

      if (val->CheckFlag(HInstruction::kUint32)) {
        LOperand* value = UseRegister(val);
        LNumberTagU* result = new(zone()) LNumberTagU(value,
                                                      TempRegister(),
                                                      TempRegister());
        return AssignEnvironment(AssignPointerMap(DefineAsRegister(result)));
      } else {
        STATIC_ASSERT((kMinInt == Smi::kMinValue) &&
                      (kMaxInt == Smi::kMaxValue));
        LOperand* value = UseRegisterAtStart(val);
        return DefineAsRegister(new(zone()) LSmiTag(value));
      }
    } else if (to.IsSmi()) {
      LOperand* value = UseRegisterAtStart(instr->value());
      // This cannot deoptimize because an A64 smi can represent any int32.
      return DefineAsRegister(new(zone()) LInteger32ToSmi(value));
    } else {
      ASSERT(to.IsDouble());
      if (instr->value()->CheckFlag(HInstruction::kUint32)) {
        return DefineAsRegister(
            new(zone()) LUint32ToDouble(UseRegisterAtStart(instr->value())));
      } else {
        return DefineAsRegister(
            new(zone()) LInteger32ToDouble(UseRegisterAtStart(instr->value())));
      }
    }
  }

  UNREACHABLE();
  return NULL;
}


LInstruction* LChunkBuilder::DoCheckFunction(HCheckFunction* instr) {
  // We only need a temp register if the target is in new space, but we can't
  // dereference the handle to test that here.
  LOperand* value = UseRegister(instr->value());
  LOperand* temp = TempRegister();
  return AssignEnvironment(new(zone()) LCheckFunction(value, temp));
}


LInstruction* LChunkBuilder::DoCheckInstanceType(HCheckInstanceType* instr) {
  LOperand* value = UseRegisterAtStart(instr->value());
  LOperand* temp = TempRegister();
  LInstruction* result = new(zone()) LCheckInstanceType(value, temp);
  return AssignEnvironment(result);
}


LInstruction* LChunkBuilder::DoCheckMaps(HCheckMaps* instr) {
  LOperand* value = UseRegister(instr->value());
  LOperand* temp = TempRegister();
  LInstruction* result = new(zone()) LCheckMaps(value, temp);
  return AssignEnvironment(result);
}


LInstruction* LChunkBuilder::DoCheckHeapObject(HCheckHeapObject* instr) {
  LOperand* value = UseRegisterAtStart(instr->value());
  return AssignEnvironment(new(zone()) LCheckNonSmi(value));
}


LInstruction* LChunkBuilder::DoCheckPrototypeMaps(HCheckPrototypeMaps* instr) {
  // TODO(jbramley): The scratch registers are not needed if
  // instr->CanOmitPrototypeChecks(). Can we safely test that here?
  LOperand* temp1 = TempRegister();
  LOperand* temp2 = TempRegister();
  return AssignEnvironment(new(zone()) LCheckPrototypeMaps(temp1, temp2));
}


LInstruction* LChunkBuilder::DoClampToUint8(HClampToUint8* instr) {
  HValue* value = instr->value();
  Representation input_rep = value->representation();
  LOperand* reg = UseRegister(value);
  if (input_rep.IsDouble()) {
    return DefineAsRegister(new(zone()) LClampDToUint8(reg));
  } else if (input_rep.IsInteger32()) {
    return DefineAsRegister(new(zone()) LClampIToUint8(reg));
  } else {
    ASSERT(input_rep.IsSmiOrTagged());
    return AssignEnvironment(
        DefineAsRegister(new(zone()) LClampTToUint8(reg,
                                                    TempRegister(),
                                                    FixedTemp(d24))));
  }
}


LInstruction* LChunkBuilder::DoClassOfTestAndBranch(
    HClassOfTestAndBranch* instr) {
  ASSERT(instr->value()->representation().IsTagged());
  LOperand* value = UseRegisterAtStart(instr->value());
  return new(zone()) LClassOfTestAndBranch(value,
                                           TempRegister(),
                                           TempRegister());
}


LInstruction* LChunkBuilder::DoCompareIDAndBranch(HCompareIDAndBranch* instr) {
  Representation r = instr->representation();

  // TODO(all): This instruction has been replaced by HCompareNumericAndBranch
  // on bleeding_edge. We should update when we'll do the rebase.
  if (r.IsSmiOrInteger32()) {
    ASSERT(instr->left()->representation().Equals(r));
    ASSERT(instr->right()->representation().Equals(r));
    LOperand* left = UseRegisterOrConstantAtStart(instr->left());
    LOperand* right = UseRegisterOrConstantAtStart(instr->right());
    return new(zone()) LCmpIDAndBranch(left, right);
  } else {
    ASSERT(r.IsDouble());
    ASSERT(instr->left()->representation().IsDouble());
    ASSERT(instr->right()->representation().IsDouble());
    // TODO(all): In fact the only case that we can handle more efficiently is
    // when one of the operand is the constant 0. Currently the MacroAssembler
    // will be able to cope with any constant by loading it into an internal
    // scratch register. This means that if the constant is used more that once,
    // it will be loaded multiple times. Unfortunatly crankshaft already
    // duplicates constant loads, but we should modify the code below once this
    // issue has been addressed in crankshaft.
    LOperand* left = UseRegisterOrConstantAtStart(instr->left());
    LOperand* right = UseRegisterOrConstantAtStart(instr->right());
    return new(zone()) LCmpIDAndBranch(left, right);
  }
}


LInstruction* LChunkBuilder::DoRandom(HRandom* instr) {
  ASSERT(instr->representation().IsDouble());
  ASSERT(instr->global_object()->representation().IsTagged());
  LOperand* global_object = UseFixed(instr->global_object(), x0);
  LRandom* result = new(zone()) LRandom(global_object);
  return MarkAsCall(DefineFixedDouble(result, d7), instr);
}


LInstruction* LChunkBuilder::DoCompareGeneric(HCompareGeneric* instr) {
  ASSERT(instr->left()->representation().IsTagged());
  ASSERT(instr->right()->representation().IsTagged());
  LOperand* left = UseFixed(instr->left(), x1);
  LOperand* right = UseFixed(instr->right(), x0);
  LCmpT* result = new(zone()) LCmpT(left, right);
  return MarkAsCall(DefineFixed(result, x0), instr);
}


LInstruction* LChunkBuilder::DoCompareObjectEqAndBranch(
    HCompareObjectEqAndBranch* instr) {
  LOperand* left = UseRegisterAtStart(instr->left());
  LOperand* right = UseRegisterAtStart(instr->right());
  return new(zone()) LCmpObjectEqAndBranch(left, right);
}


LInstruction* LChunkBuilder::DoCompareMap(HCompareMap* instr) {
  ASSERT(instr->value()->representation().IsTagged());
  LOperand* value = UseRegisterAtStart(instr->value());
  LOperand* temp = TempRegister();
  return new(zone()) LCmpMapAndBranch(value, temp);
}


LInstruction* LChunkBuilder::DoCompareConstantEqAndBranch(
    HCompareConstantEqAndBranch* instr) {
  UNIMPLEMENTED_INSTRUCTION();
}


LInstruction* LChunkBuilder::DoConstant(HConstant* instr) {
  Representation r = instr->representation();
  if (r.IsSmi()) {
    return DefineAsRegister(new(zone()) LConstantS);
  } else if (r.IsInteger32()) {
    return DefineAsRegister(new(zone()) LConstantI);
  } else if (r.IsDouble()) {
    return DefineAsRegister(new(zone()) LConstantD);
  } else if (r.IsTagged()) {
    return DefineAsRegister(new(zone()) LConstantT);
  } else {
    UNREACHABLE();
    return NULL;
  }
}


LInstruction* LChunkBuilder::DoContext(HContext* instr) {
  // If there is a non-return use, the context must be allocated in a register.
  for (HUseIterator it(instr->uses()); !it.Done(); it.Advance()) {
    if (!it.value()->IsReturn()) {
      return DefineAsRegister(new(zone()) LContext);
    }
  }

  return NULL;
}


LInstruction* LChunkBuilder::DoDateField(HDateField* instr) {
  LOperand* object = UseFixed(instr->value(), x0);
  LDateField* result = new(zone()) LDateField(object, instr->index());
  return MarkAsCall(DefineFixed(result, x0), instr, CAN_DEOPTIMIZE_EAGERLY);
}


LInstruction* LChunkBuilder::DoDebugBreak(HDebugBreak* instr) {
  return new(zone()) LDebugBreak();
}


LInstruction* LChunkBuilder::DoDeclareGlobals(HDeclareGlobals* instr) {
  return MarkAsCall(new(zone()) LDeclareGlobals, instr);
}


LInstruction* LChunkBuilder::DoDeleteProperty(HDeleteProperty* instr) {
  UNIMPLEMENTED_INSTRUCTION();
}


LInstruction* LChunkBuilder::DoDeoptimize(HDeoptimize* instr) {
  return AssignEnvironment(new(zone()) LDeoptimize);
}


LInstruction* LChunkBuilder::DoDiv(HDiv* instr) {
  if (instr->representation().IsInteger32()) {
    // TODO(all): Update this case to support smi inputs.
    ASSERT(instr->left()->representation().Equals(instr->representation()));
    ASSERT(instr->right()->representation().Equals(instr->representation()));
    if (instr->HasPowerOf2Divisor()) {
      ASSERT(!instr->CheckFlag(HValue::kCanBeDivByZero));
      LOperand* value = UseRegisterAtStart(instr->left());
      LDivI* div = new(zone()) LDivI(value, UseConstant(instr->right()), NULL);
      return AssignEnvironment(DefineAsRegister(div));
    }
    LOperand* dividend = UseRegister(instr->left());
    LOperand* divisor = UseRegister(instr->right());
    LOperand* temp = instr->CheckFlag(HInstruction::kAllUsesTruncatingToInt32)
        ? NULL : TempRegister();
    LDivI* div = new(zone()) LDivI(dividend, divisor, temp);
    return AssignEnvironment(DefineAsRegister(div));
  } else if (instr->representation().IsDouble()) {
    return DoArithmeticD(Token::DIV, instr);
  } else {
    return DoArithmeticT(Token::DIV, instr);
  }
}


LInstruction* LChunkBuilder::DoDummyUse(HDummyUse* instr) {
  return DefineAsRegister(new(zone()) LDummyUse(UseAny(instr->value())));
}


LInstruction* LChunkBuilder::DoElementsKind(HElementsKind* instr) {
  LOperand* object = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new(zone()) LElementsKind(object));
}


LInstruction* LChunkBuilder::DoEnterInlined(HEnterInlined* instr) {
  HEnvironment* outer = current_block_->last_environment();
  HConstant* undefined = graph()->GetConstantUndefined();
  HEnvironment* inner = outer->CopyForInlining(instr->closure(),
                                               instr->arguments_count(),
                                               instr->function(),
                                               undefined,
                                               instr->inlining_kind(),
                                               instr->undefined_receiver());
  // Only replay binding of arguments object if it wasn't removed from graph.
  if ((instr->arguments_var() != NULL) &&
      instr->arguments_object()->IsLinked()) {
    inner->Bind(instr->arguments_var(), instr->arguments_object());
  }
  inner->set_entry(instr);
  current_block_->UpdateEnvironment(inner);
  chunk_->AddInlinedClosure(instr->closure());
  return NULL;
}


LInstruction* LChunkBuilder::DoEnvironmentMarker(HEnvironmentMarker* instr) {
  UNREACHABLE();
  return NULL;
}


LInstruction* LChunkBuilder::DoFixedArrayBaseLength(
    HFixedArrayBaseLength* instr) {
  LOperand* array = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new(zone()) LFixedArrayBaseLength(array));
}


LInstruction* LChunkBuilder::DoForceRepresentation(
    HForceRepresentation* instr) {
  UNIMPLEMENTED_INSTRUCTION();
}


LInstruction* LChunkBuilder::DoFunctionLiteral(HFunctionLiteral* instr) {
  return MarkAsCall(DefineFixed(new(zone()) LFunctionLiteral, x0), instr);
}


LInstruction* LChunkBuilder::DoGetCachedArrayIndex(
    HGetCachedArrayIndex* instr) {
  ASSERT(instr->value()->representation().IsTagged());
  LOperand* value = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new(zone()) LGetCachedArrayIndex(value));
}


LInstruction* LChunkBuilder::DoGlobalObject(HGlobalObject* instr) {
  LOperand* context = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new(zone()) LGlobalObject(context));
}


LInstruction* LChunkBuilder::DoGlobalReceiver(HGlobalReceiver* instr) {
  LOperand* global_object = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new(zone()) LGlobalReceiver(global_object));
}


LInstruction* LChunkBuilder::DoGoto(HGoto* instr) {
  return new(zone()) LGoto(instr->FirstSuccessor()->block_id());
}


LInstruction* LChunkBuilder::DoHasCachedArrayIndexAndBranch(
    HHasCachedArrayIndexAndBranch* instr) {
  ASSERT(instr->value()->representation().IsTagged());
  return new(zone()) LHasCachedArrayIndexAndBranch(
      UseRegisterAtStart(instr->value()), TempRegister());
}


LInstruction* LChunkBuilder::DoHasInstanceTypeAndBranch(
    HHasInstanceTypeAndBranch* instr) {
  ASSERT(instr->value()->representation().IsTagged());
  LOperand* value = UseRegisterAtStart(instr->value());
  return new(zone()) LHasInstanceTypeAndBranch(value, TempRegister());
}


LInstruction* LChunkBuilder::DoIn(HIn* instr) {
  LOperand* key = UseRegisterAtStart(instr->key());
  LOperand* object = UseRegisterAtStart(instr->object());
  LIn* result = new(zone()) LIn(key, object);
  return MarkAsCall(DefineFixed(result, x0), instr);
}


LInstruction* LChunkBuilder::DoInductionVariableAnnotation(
    HInductionVariableAnnotation* instr) {
  return NULL;
}


LInstruction* LChunkBuilder::DoInnerAllocatedObject(
    HInnerAllocatedObject* inner_object) {
  LOperand* base_object = UseRegisterAtStart(inner_object->base_object());
  return DefineAsRegister(new(zone()) LInnerAllocatedObject(base_object));
}


LInstruction* LChunkBuilder::DoInstanceOf(HInstanceOf* instr) {
  LInstanceOf* result = new(zone()) LInstanceOf(
      UseFixed(instr->left(), InstanceofStub::left()),
      UseFixed(instr->right(), InstanceofStub::right()));
  return MarkAsCall(DefineFixed(result, x0), instr);
}


LInstruction* LChunkBuilder::DoInstanceOfKnownGlobal(
    HInstanceOfKnownGlobal* instr) {
  LInstanceOfKnownGlobal* result = new(zone()) LInstanceOfKnownGlobal(
      UseFixed(instr->left(), InstanceofStub::left()));
  return MarkAsCall(DefineFixed(result, x0), instr);
}


LInstruction* LChunkBuilder::DoInstanceSize(HInstanceSize* instr) {
  LOperand* object = UseRegisterAtStart(instr->object());
  return DefineAsRegister(new(zone()) LInstanceSize(object));
}


LInstruction* LChunkBuilder::DoInvokeFunction(HInvokeFunction* instr) {
  // The function is required (by MacroAssembler::InvokeFunction) to be in x1.
  LOperand* function = UseFixed(instr->function(), x1);
  argument_count_ -= instr->argument_count();
  LInvokeFunction* result = new(zone()) LInvokeFunction(function);
  return MarkAsCall(DefineFixed(result, x0), instr, CANNOT_DEOPTIMIZE_EAGERLY);
}


LInstruction* LChunkBuilder::DoIsConstructCallAndBranch(
    HIsConstructCallAndBranch* instr) {
  return new(zone()) LIsConstructCallAndBranch(TempRegister(), TempRegister());
}


LInstruction* LChunkBuilder::DoIsObjectAndBranch(HIsObjectAndBranch* instr) {
  ASSERT(instr->value()->representation().IsTagged());
  LOperand* value = UseRegisterAtStart(instr->value());
  LOperand* temp1 = TempRegister();
  LOperand* temp2 = TempRegister();
  return new(zone()) LIsObjectAndBranch(value, temp1, temp2);
}


LInstruction* LChunkBuilder::DoIsStringAndBranch(HIsStringAndBranch* instr) {
  ASSERT(instr->value()->representation().IsTagged());
  LOperand* value = UseRegisterAtStart(instr->value());
  LOperand* temp = TempRegister();
  return new(zone()) LIsStringAndBranch(value, temp);
}


LInstruction* LChunkBuilder::DoIsSmiAndBranch(HIsSmiAndBranch* instr) {
  ASSERT(instr->value()->representation().IsTagged());
  return new(zone()) LIsSmiAndBranch(UseRegisterAtStart(instr->value()));
}


LInstruction* LChunkBuilder::DoIsUndetectableAndBranch(
    HIsUndetectableAndBranch* instr) {
  ASSERT(instr->value()->representation().IsTagged());
  LOperand* value = UseRegisterAtStart(instr->value());
  return new(zone()) LIsUndetectableAndBranch(value, TempRegister());
}


LInstruction* LChunkBuilder::DoLeaveInlined(HLeaveInlined* instr) {
  LInstruction* pop = NULL;
  HEnvironment* env = current_block_->last_environment();

  if (env->entry()->arguments_pushed()) {
    int argument_count = env->arguments_environment()->parameter_count();
    pop = new(zone()) LDrop(argument_count);
    argument_count_ -= argument_count;
  }

  HEnvironment* outer =
      current_block_->last_environment()->DiscardInlined(false);
  current_block_->UpdateEnvironment(outer);

  return pop;
}


LInstruction* LChunkBuilder::DoLoadContextSlot(HLoadContextSlot* instr) {
  LOperand* context = UseRegisterAtStart(instr->value());
  LInstruction* result =
      DefineAsRegister(new(zone()) LLoadContextSlot(context));
  return instr->RequiresHoleCheck() ? AssignEnvironment(result) : result;
}


LInstruction* LChunkBuilder::DoLoadExternalArrayPointer(
    HLoadExternalArrayPointer* instr) {
  LOperand* input = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new(zone()) LLoadExternalArrayPointer(input));
}


LInstruction* LChunkBuilder::DoLoadFunctionPrototype(
    HLoadFunctionPrototype* instr) {
  LOperand* function = UseRegister(instr->function());
  LOperand* temp = TempRegister();
  return AssignEnvironment(DefineAsRegister(
      new(zone()) LLoadFunctionPrototype(function, temp)));
}


LInstruction* LChunkBuilder::DoLoadGlobalCell(HLoadGlobalCell* instr) {
  LLoadGlobalCell* result = new(zone()) LLoadGlobalCell();
  return instr->RequiresHoleCheck()
      ? AssignEnvironment(DefineAsRegister(result))
      : DefineAsRegister(result);
}


LInstruction* LChunkBuilder::DoLoadGlobalGeneric(HLoadGlobalGeneric* instr) {
  LOperand* global_object = UseFixed(instr->global_object(), x0);
  LLoadGlobalGeneric* result = new(zone()) LLoadGlobalGeneric(global_object);
  return MarkAsCall(DefineFixed(result, x0), instr);
}


LInstruction* LChunkBuilder::DoLoadKeyed(HLoadKeyed* instr) {
  ASSERT(instr->key()->representation().IsInteger32() ||
         instr->key()->representation().IsSmi());
  ElementsKind elements_kind = instr->elements_kind();
  LOperand* elements = UseRegister(instr->elements());
  LOperand* key = UseRegisterOrConstantAtStart(instr->key());

  if (!instr->is_external()) {
    if (instr->representation().IsDouble()) {
      LOperand* temp = (!instr->key()->IsConstant() ||
                        instr->RequiresHoleCheck())
             ? TempRegister()
             : NULL;

      LLoadKeyedFixedDouble* result =
          new(zone()) LLoadKeyedFixedDouble(elements, key, temp);
      return instr->RequiresHoleCheck()
          ? AssignEnvironment(DefineAsRegister(result))
          : DefineAsRegister(result);
    } else {
      ASSERT(instr->representation().IsSmiOrTagged());
      LOperand* temp = instr->key()->IsConstant() ? NULL : TempRegister();
      LLoadKeyedFixed* result =
          new(zone()) LLoadKeyedFixed(elements, key, temp);
      return instr->RequiresHoleCheck()
          ? AssignEnvironment(DefineAsRegister(result))
          : DefineAsRegister(result);
    }
  } else {
    ASSERT((instr->representation().IsInteger32() &&
            (elements_kind != EXTERNAL_FLOAT_ELEMENTS) &&
            (elements_kind != EXTERNAL_DOUBLE_ELEMENTS)) ||
           (instr->representation().IsDouble() &&
            ((elements_kind == EXTERNAL_FLOAT_ELEMENTS) ||
             (elements_kind == EXTERNAL_DOUBLE_ELEMENTS))));

    LOperand* temp = instr->key()->IsConstant() ? NULL : TempRegister();
    LLoadKeyedExternal* result =
        new(zone()) LLoadKeyedExternal(elements, key, temp);
    // An unsigned int array load might overflow and cause a deopt. Make sure it
    // has an environment.
    return (elements_kind == EXTERNAL_UNSIGNED_INT_ELEMENTS)
        ? AssignEnvironment(DefineAsRegister(result))
        : DefineAsRegister(result);
  }
}


LInstruction* LChunkBuilder::DoLoadKeyedGeneric(HLoadKeyedGeneric* instr) {
  LOperand* object = UseFixed(instr->object(), x1);
  LOperand* key = UseFixed(instr->key(), x0);

  LInstruction* result =
      DefineFixed(new(zone()) LLoadKeyedGeneric(object, key), x0);
  return MarkAsCall(result, instr);
}


LInstruction* LChunkBuilder::DoLoadNamedField(HLoadNamedField* instr) {
  LOperand* object = UseRegisterAtStart(instr->object());
  return DefineAsRegister(new(zone()) LLoadNamedField(object));
}


LInstruction* LChunkBuilder::DoLoadNamedFieldPolymorphic(
    HLoadNamedFieldPolymorphic* instr) {
  ASSERT(instr->representation().IsTagged());
  if (instr->need_generic()) {
    LOperand* obj = UseFixed(instr->object(), x0);
    LLoadNamedFieldPolymorphic* result =
        new(zone()) LLoadNamedFieldPolymorphic(obj);
    return MarkAsCall(DefineFixed(result, x0), instr);
  } else {
    LOperand* obj = UseRegister(instr->object());
    LLoadNamedFieldPolymorphic* result =
        new(zone()) LLoadNamedFieldPolymorphic(obj);
    return AssignEnvironment(DefineAsRegister(result));
  }
}


LInstruction* LChunkBuilder::DoLoadNamedGeneric(HLoadNamedGeneric* instr) {
  LOperand* object = UseFixed(instr->object(), x0);
  LInstruction* result = DefineFixed(new(zone()) LLoadNamedGeneric(object), x0);
  return MarkAsCall(result, instr);
}


LInstruction* LChunkBuilder::DoMapEnumLength(HMapEnumLength* instr) {
  LOperand* map = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new(zone()) LMapEnumLength(map));
}


HValue* LChunkBuilder::SimplifiedDivisorForMathFloorOfDiv(HValue* divisor) {
  // A value with an integer representation does not need to be transformed.
  if (divisor->representation().IsInteger32()) {
    return divisor;
  // A change from an integer32 can be replaced by the integer32 value.
  } else if (divisor->IsChange() &&
             HChange::cast(divisor)->from().IsInteger32()) {
    return HChange::cast(divisor)->value();
  }

  if (divisor->IsConstant() && HConstant::cast(divisor)->HasInteger32Value()) {
    HConstant* constant_val = HConstant::cast(divisor);
    return constant_val->CopyToRepresentation(Representation::Integer32(),
                                              divisor->block()->zone());
  }

  return NULL;
}


LInstruction* LChunkBuilder::DoMathFloorOfDiv(HMathFloorOfDiv* instr) {
  HValue* right = instr->right();
  LOperand* dividend = UseRegister(instr->left());
  LOperand* divisor = UseRegister(right);
  LOperand* remainder = TempRegister();
  return AssignEnvironment(DefineAsRegister(
          new(zone()) LMathFloorOfDiv(dividend, divisor, remainder)));
}


LInstruction* LChunkBuilder::DoMathMinMax(HMathMinMax* instr) {
  LOperand* left = NULL;
  LOperand* right = NULL;
  if (instr->representation().IsInteger32()) {
    ASSERT(instr->left()->representation().IsInteger32());
    ASSERT(instr->right()->representation().IsInteger32());
    left = UseRegisterAtStart(instr->BetterLeftOperand());
    right = UseRegisterOrConstantAtStart(instr->BetterRightOperand());
  } else {
    ASSERT(instr->representation().IsDouble());
    ASSERT(instr->left()->representation().IsDouble());
    ASSERT(instr->right()->representation().IsDouble());
    left = UseRegisterAtStart(instr->left());
    right = UseRegisterAtStart(instr->right());
  }
  return DefineAsRegister(new(zone()) LMathMinMax(left, right));
}


LInstruction* LChunkBuilder::DoMod(HMod* hmod) {
  HValue* hleft = hmod->left();
  HValue* hright = hmod->right();

  if (hmod->representation().IsInteger32()) {
    ASSERT(hleft->representation().IsInteger32());
    ASSERT(hleft->representation().IsInteger32());
    LOperand* left_op;
    LOperand* right_op;

    if (hmod->HasPowerOf2Divisor()) {
      left_op = UseRegisterAtStart(hleft);
      right_op = UseConstant(hright);
    } else {
      right_op = UseRegister(hright);
      left_op = UseRegister(hleft);
    }

    LModI* lmod = new(zone()) LModI(left_op, right_op);

    if (hmod->right()->CanBeZero() ||
        (hmod->CheckFlag(HValue::kBailoutOnMinusZero) &&
         hmod->left()->CanBeNegative() && hmod->CanBeZero())) {
      AssignEnvironment(lmod);
    }
    return DefineAsRegister(lmod);

  } else if (hmod->representation().IsSmiOrTagged()) {
    return DoArithmeticT(Token::MOD, hmod);
  } else {
    return DoArithmeticD(Token::MOD, hmod);
  }
}


LInstruction* LChunkBuilder::DoMul(HMul* instr) {
  if (instr->representation().IsInteger32()) {
    ASSERT(instr->left()->representation().IsInteger32());
    ASSERT(instr->right()->representation().IsInteger32());

    bool can_overflow = instr->CheckFlag(HValue::kCanOverflow);
    bool bailout_on_minus_zero = instr->CheckFlag(HValue::kBailoutOnMinusZero);
    bool needs_environment = can_overflow || bailout_on_minus_zero;

    HValue* least_const = instr->BetterLeftOperand();
    HValue* most_const = instr->BetterRightOperand();

    LOperand* left = UseRegisterAtStart(least_const);

    // LMulConstI can handle a subset of constants:
    //  With support for overflow detection:
    //    -1, 0, 1, 2
    //  Without support for overflow detection:
    //    2^n, -(2^n)
    //    2^n + 1, -(2^n - 1)
    if (most_const->IsConstant()) {
      int32_t constant = HConstant::cast(most_const)->Integer32Value();
      int32_t constant_abs = (constant >= 0) ? constant : -constant;

      if (((constant >= -1) && (constant <= 2)) ||
          (!can_overflow && (IsPowerOf2(constant_abs) ||
                             IsPowerOf2(constant_abs + 1) ||
                             IsPowerOf2(constant_abs - 1)))) {
        LConstantOperand* right = UseConstant(most_const);
        LMulConstI* mul = new(zone()) LMulConstI(left, right);
        if (needs_environment) AssignEnvironment(mul);
        return DefineAsRegister(mul);
      }
    }

    // LMulI can handle all cases, but it requires that a register is allocated
    // for the second operand.
    LOperand* right = UseRegisterAtStart(most_const);
    LMulI* mul = new(zone()) LMulI(left, right);
    if (needs_environment) AssignEnvironment(mul);
    return DefineAsRegister(mul);
  } else if (instr->representation().IsDouble()) {
    return DoArithmeticD(Token::MUL, instr);
  } else {
    return DoArithmeticT(Token::MUL, instr);
  }
}


LInstruction* LChunkBuilder::DoNumericConstraint(HNumericConstraint* instr) {
  return NULL;
}


LInstruction* LChunkBuilder::DoOsrEntry(HOsrEntry* instr) {
  ASSERT(argument_count_ == 0);
  allocator_->MarkAsOsrEntry();
  current_block_->last_environment()->set_ast_id(instr->ast_id());
  return AssignEnvironment(new(zone()) LOsrEntry);
}


LInstruction* LChunkBuilder::DoOuterContext(HOuterContext* instr) {
  LOperand* context = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new(zone()) LOuterContext(context));
}


LInstruction* LChunkBuilder::DoParameter(HParameter* instr) {
  LParameter* result = new(zone()) LParameter;
  if (instr->kind() == HParameter::STACK_PARAMETER) {
    int spill_index = chunk_->GetParameterStackSlot(instr->index());
    return DefineAsSpilled(result, spill_index);
  } else {
    ASSERT(info()->IsStub());
    CodeStubInterfaceDescriptor* descriptor =
        info()->code_stub()->GetInterfaceDescriptor(info()->isolate());
    int index = static_cast<int>(instr->index());
    Register reg = DESCRIPTOR_GET_PARAMETER_REGISTER(descriptor, index);
    return DefineFixed(result, reg);
  }
}


LInstruction* LChunkBuilder::DoPower(HPower* instr) {
  ASSERT(instr->representation().IsDouble());
  // We call a C function for double power. It can't trigger a GC.
  // We need to use fixed result register for the call.
  Representation exponent_type = instr->right()->representation();
  ASSERT(instr->left()->representation().IsDouble());
  LOperand* left = UseFixedDouble(instr->left(), d0);
  LOperand* right = exponent_type.IsDouble() ?
      UseFixedDouble(instr->right(), d1) :
      UseFixed(instr->right(), x11);
  LPower* result = new(zone()) LPower(left, right);
  return MarkAsCall(DefineFixedDouble(result, d0),
                    instr,
                    CAN_DEOPTIMIZE_EAGERLY);
}


LInstruction* LChunkBuilder::DoPushArgument(HPushArgument* instr) {
  ++argument_count_;
  LOperand* argument = UseRegister(instr->argument());
  return new(zone()) LPushArgument(argument);
}


LInstruction* LChunkBuilder::DoRegExpLiteral(HRegExpLiteral* instr) {
  return MarkAsCall(DefineFixed(new(zone()) LRegExpLiteral, x0), instr);
}


LInstruction* LChunkBuilder::DoReturn(HReturn* instr) {
  LOperand* parameter_count = UseRegisterOrConstant(instr->parameter_count());
  return new(zone()) LReturn(UseFixed(instr->value(), x0), parameter_count);
}


LInstruction* LChunkBuilder::DoSeqStringSetChar(HSeqStringSetChar* instr) {
  LOperand* string = UseRegister(instr->string());
  LOperand* index = UseRegister(instr->index());
  LOperand* value = UseRegister(instr->value());
  LOperand* temp = TempRegister();
  LSeqStringSetChar* result =
      new(zone()) LSeqStringSetChar(instr->encoding(),
                                    string, index, value, temp);
  return DefineAsRegister(result);
}


LInstruction* LChunkBuilder::DoShift(Token::Value op,
                                     HBitwiseBinaryOperation* instr) {
  if (instr->representation().IsSmiOrTagged()) {
    return DoArithmeticT(op, instr);
  }

  ASSERT(instr->representation().IsInteger32());
  ASSERT(instr->left()->representation().IsInteger32());
  ASSERT(instr->right()->representation().IsInteger32());
  LOperand* left = UseRegisterAtStart(instr->left());

  HValue* right_value = instr->right();
  LOperand* right = NULL;
  int constant_value = 0;
  if (right_value->IsConstant()) {
    right = UseConstant(right_value);
    HConstant* constant = HConstant::cast(right_value);
    constant_value = constant->Integer32Value() & 0x1f;
  } else {
    right = UseRegisterAtStart(right_value);
  }

  // Shift operations can only deoptimize if we do a logical shift by 0 and the
  // result cannot be truncated to int32.
  bool does_deopt = false;
  if ((op == Token::SHR) && (constant_value == 0)) {
    if (FLAG_opt_safe_uint32_operations) {
      does_deopt = !instr->CheckFlag(HInstruction::kUint32);
    } else {
      for (HUseIterator it(instr->uses()); !it.Done(); it.Advance()) {
        if (!it.value()->CheckFlag(HValue::kTruncatingToInt32)) {
          does_deopt = true;
          break;
        }
      }
    }
  }

  LInstruction* result =
      DefineAsRegister(new(zone()) LShiftI(op, left, right, does_deopt));
  return does_deopt ? AssignEnvironment(result) : result;
}


LInstruction* LChunkBuilder::DoRor(HRor* instr) {
  return DoShift(Token::ROR, instr);
}


LInstruction* LChunkBuilder::DoSar(HSar* instr) {
  return DoShift(Token::SAR, instr);
}


LInstruction* LChunkBuilder::DoShl(HShl* instr) {
  return DoShift(Token::SHL, instr);
}


LInstruction* LChunkBuilder::DoShr(HShr* instr) {
  return DoShift(Token::SHR, instr);
}


LInstruction* LChunkBuilder::DoSimulate(HSimulate* instr) {
  HEnvironment* env = current_block_->last_environment();
  ASSERT(env != NULL);

  env->set_ast_id(instr->ast_id());

  env->Drop(instr->pop_count());
  for (int i = instr->values()->length() - 1; i >= 0; --i) {
    HValue* value = instr->values()->at(i);
    if (instr->HasAssignedIndexAt(i)) {
      env->Bind(instr->GetAssignedIndexAt(i), value);
    } else {
      env->Push(value);
    }
  }

  // If there is an instruction pending deoptimization environment create a
  // lazy bailout instruction to capture the environment.
  if (pending_deoptimization_ast_id_ == instr->ast_id()) {
    LInstruction* result = new(zone()) LLazyBailout;
    result = AssignEnvironment(result);
    // Store the lazy deopt environment with the instruction if needed. Right
    // now it is only used for LInstanceOfKnownGlobal.
    instruction_pending_deoptimization_environment_->
        SetDeferredLazyDeoptimizationEnvironment(result->environment());
    instruction_pending_deoptimization_environment_ = NULL;
    pending_deoptimization_ast_id_ = BailoutId::None();
    return result;
  }

  return NULL;
}


LInstruction* LChunkBuilder::DoSoftDeoptimize(HSoftDeoptimize* instr) {
  return AssignEnvironment(new(zone()) LDeoptimize);
}


LInstruction* LChunkBuilder::DoStackCheck(HStackCheck* instr) {
  if (instr->is_function_entry()) {
    return MarkAsCall(new(zone()) LStackCheck, instr);
  } else {
    ASSERT(instr->is_backwards_branch());
    return AssignEnvironment(AssignPointerMap(new(zone()) LStackCheck));
  }
}


LInstruction* LChunkBuilder::DoStoreContextSlot(HStoreContextSlot* instr) {
  LOperand* temp = TempRegister();
  LOperand* context;
  LOperand* value;
  if (instr->NeedsWriteBarrier()) {
    // TODO(all): Replace these constraints when RecordWriteStub has been
    // rewritten. See GOOGJSE-586.
    context = UseRegisterAndClobber(instr->context());
    value = UseRegisterAndClobber(instr->value());
  } else {
    context = UseRegister(instr->context());
    value = UseRegister(instr->value());
  }
  LInstruction* result = new(zone()) LStoreContextSlot(context, value, temp);
  return instr->RequiresHoleCheck() ? AssignEnvironment(result) : result;
}


LInstruction* LChunkBuilder::DoStoreGlobalCell(HStoreGlobalCell* instr) {
  LOperand* value = UseRegister(instr->value());
  if (instr->RequiresHoleCheck()) {
    return AssignEnvironment(new(zone()) LStoreGlobalCell(value,
                                                          TempRegister(),
                                                          TempRegister()));
  } else {
    return new(zone()) LStoreGlobalCell(value, TempRegister(), NULL);
  }
}


LInstruction* LChunkBuilder::DoStoreGlobalGeneric(HStoreGlobalGeneric* instr) {
  LOperand* global_object = UseFixed(instr->global_object(), x1);
  LOperand* value = UseFixed(instr->value(), x0);
  LStoreGlobalGeneric* result =
      new(zone()) LStoreGlobalGeneric(global_object, value);
  return MarkAsCall(result, instr);
}


LInstruction* LChunkBuilder::DoStoreKeyed(HStoreKeyed* instr) {
  LOperand* temp = NULL;
  LOperand* elements = NULL;
  LOperand* val = NULL;
  LOperand* key = NULL;

  if (!instr->is_external() && instr->value()->representation().IsTagged() &&
      instr->NeedsWriteBarrier()) {
    // RecordWrite() will clobber all registers.
    elements = UseRegisterAndClobber(instr->elements());
    val = UseRegisterAndClobber(instr->value());
    key = UseRegisterAndClobber(instr->key());
  } else {
    elements = UseRegister(instr->elements());
    val = UseRegister(instr->value());
    key = UseRegisterOrConstantAtStart(instr->key());
  }

  if (instr->is_external()) {
    ASSERT(instr->elements()->representation().IsExternal());
    ASSERT((instr->value()->representation().IsInteger32() &&
            (instr->elements_kind() != EXTERNAL_FLOAT_ELEMENTS) &&
            (instr->elements_kind() != EXTERNAL_DOUBLE_ELEMENTS)) ||
           (instr->value()->representation().IsDouble() &&
            ((instr->elements_kind() == EXTERNAL_FLOAT_ELEMENTS) ||
             (instr->elements_kind() == EXTERNAL_DOUBLE_ELEMENTS))));
    temp = instr->key()->IsConstant() ? NULL : TempRegister();
    return new(zone()) LStoreKeyedExternal(elements, key, val, temp);
  } else if (instr->value()->representation().IsDouble()) {
    ASSERT(instr->elements()->representation().IsTagged());

    // The constraint used here is UseRegister, even though the StoreKeyed
    // instruction may canonicalize the value in the register if it is a NaN.
    temp = TempRegister();
    return new(zone()) LStoreKeyedFixedDouble(elements, key, val, temp);
  } else {
    ASSERT(instr->elements()->representation().IsTagged());
    ASSERT(instr->value()->representation().IsSmiOrTagged());

    temp = TempRegister();
    return new(zone()) LStoreKeyedFixed(elements, key, val, temp);
  }
}


LInstruction* LChunkBuilder::DoStoreKeyedGeneric(HStoreKeyedGeneric* instr) {
  LOperand* object = UseFixed(instr->object(), x2);
  LOperand* key = UseFixed(instr->key(), x1);
  LOperand* value = UseFixed(instr->value(), x0);

  ASSERT(instr->object()->representation().IsTagged());
  ASSERT(instr->key()->representation().IsTagged());
  ASSERT(instr->value()->representation().IsTagged());

  return MarkAsCall(new(zone()) LStoreKeyedGeneric(object, key, value), instr);
}


LInstruction* LChunkBuilder::DoStoreNamedField(HStoreNamedField* instr) {
  // TODO(jbramley): Optimize register usage in this instruction. For now, it
  // allocates everything that it might need because it keeps changing in the
  // merge and keeping it valid is time-consuming.

  // TODO(jbramley): It might be beneficial to allow value to be a constant in
  // some cases. x64 makes use of this with FLAG_track_fields, for example.

  LOperand* object = UseRegister(instr->object());
  LOperand* value = UseRegisterAndClobber(instr->value());
  LOperand* temp0 = TempRegister();
  LOperand* temp1 = TempRegister();

  LStoreNamedField* result =
      new(zone()) LStoreNamedField(object, value, temp0, temp1);
  if (FLAG_track_heap_object_fields &&
      instr->field_representation().IsHeapObject() &&
      !instr->value()->type().IsHeapObject()) {
    return AssignEnvironment(result);
  }
  return result;
}


LInstruction* LChunkBuilder::DoStoreNamedGeneric(HStoreNamedGeneric* instr) {
  LOperand* object = UseFixed(instr->object(), x1);
  LOperand* value = UseFixed(instr->value(), x0);
  LInstruction* result = new(zone()) LStoreNamedGeneric(object, value);
  return MarkAsCall(result, instr);
}


LInstruction* LChunkBuilder::DoStringAdd(HStringAdd* instr) {
  LOperand* left = UseRegisterAtStart(instr->left());
  LOperand* right = UseRegisterAtStart(instr->right());

  LStringAdd* result = new(zone()) LStringAdd(left, right);
  return MarkAsCall(DefineFixed(result, x0), instr);
}


LInstruction* LChunkBuilder::DoStringCharCodeAt(HStringCharCodeAt* instr) {
  LOperand* string = UseRegisterAndClobber(instr->string());
  LOperand* index = UseRegisterAndClobber(instr->index());
  LStringCharCodeAt* result = new(zone()) LStringCharCodeAt(string, index);
  return AssignEnvironment(AssignPointerMap(DefineAsRegister(result)));
}


LInstruction* LChunkBuilder::DoStringCharFromCode(HStringCharFromCode* instr) {
  // TODO(all) use at start and remove assert in codegen
  LOperand* char_code = UseRegister(instr->value());
  LStringCharFromCode* result = new(zone()) LStringCharFromCode(char_code);
  return AssignPointerMap(DefineAsRegister(result));
}


LInstruction* LChunkBuilder::DoStringCompareAndBranch(
    HStringCompareAndBranch* instr) {
  ASSERT(instr->left()->representation().IsTagged());
  ASSERT(instr->right()->representation().IsTagged());
  LOperand* left = UseFixed(instr->left(), x1);
  LOperand* right = UseFixed(instr->right(), x0);
  LStringCompareAndBranch* result =
      new(zone()) LStringCompareAndBranch(left, right);
  return MarkAsCall(result, instr);
}


LInstruction* LChunkBuilder::DoStringLength(HStringLength* instr) {
  // TODO(all): This instruction doesn't exist anymore on bleeding_edge.
  LOperand* string = UseRegisterAtStart(instr->value());
  return DefineAsRegister(new(zone()) LStringLength(string));
}


LInstruction* LChunkBuilder::DoSub(HSub* instr) {
  if (instr->representation().IsInteger32()) {
    ASSERT(instr->left()->representation().IsInteger32());
    ASSERT(instr->right()->representation().IsInteger32());

    LOperand *left;
    if (instr->left()->IsConstant() &&
       (HConstant::cast(instr->left())->Integer32Value() == 0)) {
      left = UseConstant(instr->left());
    } else {
      left = UseRegisterAtStart(instr->left());
    }
    LOperand* right = UseRegisterOrConstantAtStart(instr->right());
    LSubI* sub = new(zone()) LSubI(left, right);
    LInstruction* result = DefineAsRegister(sub);
    if (instr->CheckFlag(HValue::kCanOverflow)) {
      result = AssignEnvironment(result);
    }
    return result;
  } else if (instr->representation().IsDouble()) {
    return DoArithmeticD(Token::SUB, instr);
  } else {
    return DoArithmeticT(Token::SUB, instr);
  }
}


LInstruction* LChunkBuilder::DoThisFunction(HThisFunction* instr) {
  if (instr->HasNoUses()) {
    return NULL;
  } else {
    return DefineAsRegister(new(zone()) LThisFunction);
  }
}


LInstruction* LChunkBuilder::DoThrow(HThrow* instr) {
  // TODO(all): This should not need to be a fixed register, as it's pushed to
  // the stack in DoThrow. However, we hit an assertion if it's assigned a non-
  // fixed register.
  LOperand* value = UseFixed(instr->value(), x0);
  return MarkAsCall(new(zone()) LThrow(value), instr);
}


LInstruction* LChunkBuilder::DoToFastProperties(HToFastProperties* instr) {
  LOperand* object = UseFixed(instr->value(), x0);
  LToFastProperties* result = new(zone()) LToFastProperties(object);
  return MarkAsCall(DefineFixed(result, x0), instr);
}


LInstruction* LChunkBuilder::DoTransitionElementsKind(
    HTransitionElementsKind* instr) {
  if (IsSimpleMapChangeTransition(instr->from_kind(), instr->to_kind())) {
    LOperand* object = UseRegister(instr->object());
    LTransitionElementsKind* result =
        new(zone()) LTransitionElementsKind(object, TempRegister(),
                                            TempRegister());
    return result;
  } else if (FLAG_compiled_transitions) {
    LOperand* object = UseRegister(instr->object());
    LTransitionElementsKind* result =
        new(zone()) LTransitionElementsKind(object, NULL, NULL);
    return AssignPointerMap(result);
  } else {
    LOperand* object = UseFixed(instr->object(), x0);
    LTransitionElementsKind* result =
        new(zone()) LTransitionElementsKind(object, NULL, NULL);
    return MarkAsCall(result, instr);
  }
}


LInstruction* LChunkBuilder::DoTrapAllocationMemento(
    HTrapAllocationMemento* instr) {
  LOperand* object = UseRegister(instr->object());
  LOperand* temp1 = TempRegister();
  LOperand* temp2 = TempRegister();
  LTrapAllocationMemento* result =
      new(zone()) LTrapAllocationMemento(object, temp1, temp2);
  return AssignEnvironment(result);
}


LInstruction* LChunkBuilder::DoTypeof(HTypeof* instr) {
  // TODO(jbramley): In ARM, this uses UseFixed to force the input to x0.
  // However, LCodeGen::DoTypeof just pushes it to the stack (for CallRuntime)
  // anyway, so the input doesn't have to be in x0. We might be able to improve
  // the ARM back-end a little by relaxing this restriction.
  LTypeof* result = new(zone()) LTypeof(UseRegisterAtStart(instr->value()));
  return MarkAsCall(DefineFixed(result, x0), instr);
}


LInstruction* LChunkBuilder::DoTypeofIsAndBranch(HTypeofIsAndBranch* instr) {
  // We only need temp registers in some cases, but we can't dereference the
  // instr->type_literal() handle to test that here.
  LOperand* temp1 = TempRegister();
  LOperand* temp2 = TempRegister();

  return new(zone()) LTypeofIsAndBranch(
      UseRegister(instr->value()), temp1, temp2);
}


LInstruction* LChunkBuilder::DoUnaryMathOperation(HUnaryMathOperation* instr) {
  switch (instr->op()) {
    case kMathAbs: {
      Representation r = instr->representation();
      if (r.IsTagged()) {
        // The tagged case might need to allocate a HeapNumber for the result,
        // so it is handled by a separate LInstruction.
        LOperand* input = UseRegister(instr->value());
        LOperand* temp1 = TempRegister();
        LOperand* temp2 = TempRegister();
        LOperand* temp3 = TempRegister();
        LMathAbsTagged* result =
            new(zone()) LMathAbsTagged(input, temp1, temp2, temp3);
        return AssignEnvironment(AssignPointerMap(DefineAsRegister(result)));
      } else {
        LOperand* input = UseRegisterAtStart(instr->value());
        LMathAbs* result = new(zone()) LMathAbs(input);
        if (r.IsDouble()) {
          // The Double case can never fail so it doesn't need an environment.
          return DefineAsRegister(result);
        } else {
          ASSERT(r.IsInteger32());
          // The Integer32 case needs an environment because it can deoptimize
          // on INT_MIN.
          return AssignEnvironment(DefineAsRegister(result));
        }
      }
    }
    case kMathCos: {
      ASSERT(instr->representation().IsDouble());
      ASSERT(instr->value()->representation().IsDouble());
      LOperand* input = UseFixedDouble(instr->value(), d0);
      LMathCos* result = new(zone()) LMathCos(input);
      return MarkAsCall(DefineFixedDouble(result, d0), instr);
    }
    case kMathExp: {
      ASSERT(instr->representation().IsDouble());
      ASSERT(instr->value()->representation().IsDouble());
      LOperand* input = UseRegister(instr->value());
      // TODO(all): Implement TempFPRegister.
      LOperand* double_temp1 = FixedTemp(d24);   // This was chosen arbitrarily.
      LOperand* temp1 = TempRegister();
      LOperand* temp2 = TempRegister();
      LOperand* temp3 = TempRegister();
      LMathExp* result = new(zone()) LMathExp(input, double_temp1,
                                              temp1, temp2, temp3);
      return DefineAsRegister(result);
    }
    case kMathFloor: {
      ASSERT(instr->representation().IsInteger32());
      ASSERT(instr->value()->representation().IsDouble());
      // TODO(jbramley): A64 can easily handle a double argument with frintm,
      // but we're never asked for it here. At the moment, we fall back to the
      // runtime if the result doesn't fit, like the other architectures.
      LOperand* input = UseRegisterAtStart(instr->value());
      LMathFloor* result = new(zone()) LMathFloor(input);
      return AssignEnvironment(AssignPointerMap(DefineAsRegister(result)));
    }
    case kMathLog: {
      ASSERT(instr->representation().IsDouble());
      ASSERT(instr->value()->representation().IsDouble());
      LOperand* input = UseFixedDouble(instr->value(), d0);
      LMathLog* result = new(zone()) LMathLog(input);
      return MarkAsCall(DefineFixedDouble(result, d0), instr);
    }
    case kMathPowHalf: {
      ASSERT(instr->representation().IsDouble());
      ASSERT(instr->value()->representation().IsDouble());
      LOperand* input = UseRegister(instr->value());
      return DefineAsRegister(new(zone()) LMathPowHalf(input));
    }
    case kMathRound: {
      ASSERT(instr->representation().IsInteger32());
      ASSERT(instr->value()->representation().IsDouble());
      // TODO(jbramley): As with kMathFloor, we can probably handle double
      // results fairly easily, but we are never asked for them.
      LOperand* input = UseRegister(instr->value());
      LOperand* temp = FixedTemp(d24);  // Choosen arbitrarily.
      LMathRound* result = new(zone()) LMathRound(input, temp);
      return AssignEnvironment(DefineAsRegister(result));
    }
    case kMathSin: {
      ASSERT(instr->representation().IsDouble());
      ASSERT(instr->value()->representation().IsDouble());
      LOperand* input = UseFixedDouble(instr->value(), d0);
      LMathSin* result = new(zone()) LMathSin(input);
      return MarkAsCall(DefineFixedDouble(result, d0), instr);
    }
    case kMathSqrt: {
      ASSERT(instr->representation().IsDouble());
      ASSERT(instr->value()->representation().IsDouble());
      LOperand* input = UseRegisterAtStart(instr->value());
      return DefineAsRegister(new(zone()) LMathSqrt(input));
    }
    case kMathTan: {
      ASSERT(instr->representation().IsDouble());
      ASSERT(instr->value()->representation().IsDouble());
      LOperand* input = UseFixedDouble(instr->value(), d0);
      LMathTan* result = new(zone()) LMathTan(input);
      return MarkAsCall(DefineFixedDouble(result, d0), instr);
    }
    default:
      UNREACHABLE();
      return NULL;
  }
}


LInstruction* LChunkBuilder::DoUnknownOSRValue(HUnknownOSRValue* instr) {
  int spill_index = chunk_->GetNextSpillIndex();
  if (spill_index > LUnallocated::kMaxFixedSlotIndex) {
    Abort("Too many spill slots needed for OSR");
    spill_index = 0;
  }
  return DefineAsSpilled(new(zone()) LUnknownOSRValue, spill_index);
}


LInstruction* LChunkBuilder::DoUseConst(HUseConst* instr) {
  return NULL;
}


LInstruction* LChunkBuilder::DoValueOf(HValueOf* instr) {
  LOperand* object = UseRegister(instr->value());
  LValueOf* result = new(zone()) LValueOf(object, TempRegister());
  return DefineSameAsFirst(result);
}


LInstruction* LChunkBuilder::DoForInPrepareMap(HForInPrepareMap* instr) {
  // Assign object to a fixed register different from those already used in
  // LForInPrepareMap.
  LOperand* object = UseFixed(instr->enumerable(), x0);
  LForInPrepareMap* result = new(zone()) LForInPrepareMap(object);
  return MarkAsCall(DefineFixed(result, x0), instr, CAN_DEOPTIMIZE_EAGERLY);
}


LInstruction* LChunkBuilder::DoForInCacheArray(HForInCacheArray* instr) {
  LOperand* map = UseRegister(instr->map());
  return AssignEnvironment(DefineAsRegister(new(zone()) LForInCacheArray(map)));
}


LInstruction* LChunkBuilder::DoCheckMapValue(HCheckMapValue* instr) {
  LOperand* value = UseRegisterAtStart(instr->value());
  LOperand* map = UseRegister(instr->map());
  LOperand* temp = TempRegister();
  return AssignEnvironment(new(zone()) LCheckMapValue(value, map, temp));
}


LInstruction* LChunkBuilder::DoLoadFieldByIndex(HLoadFieldByIndex* instr) {
  LOperand* object = UseRegisterAtStart(instr->object());
  LOperand* index = UseRegister(instr->index());
  return DefineAsRegister(new(zone()) LLoadFieldByIndex(object, index));
}


LInstruction* LChunkBuilder::DoWrapReceiver(HWrapReceiver* instr) {
  LOperand* receiver = UseRegister(instr->receiver());
  LOperand* function = UseRegisterAtStart(instr->function());
  LOperand* temp = TempRegister();
  LWrapReceiver* result = new(zone()) LWrapReceiver(receiver, function, temp);
  return AssignEnvironment(DefineAsRegister(result));
}


} }  // namespace v8::internal
