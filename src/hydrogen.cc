// Copyright 2011 the V8 project authors. All rights reserved.
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
#include "hydrogen.h"

#include "codegen.h"
#include "data-flow.h"
#include "full-codegen.h"
#include "hashmap.h"
#include "lithium-allocator.h"
#include "parser.h"
#include "scopes.h"
#include "stub-cache.h"

#if V8_TARGET_ARCH_IA32
#include "ia32/lithium-codegen-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "x64/lithium-codegen-x64.h"
#elif V8_TARGET_ARCH_ARM
#include "arm/lithium-codegen-arm.h"
#elif V8_TARGET_ARCH_MIPS
#include "mips/lithium-codegen-mips.h"
#else
#error Unsupported target architecture.
#endif

namespace v8 {
namespace internal {

HBasicBlock::HBasicBlock(HGraph* graph)
    : block_id_(graph->GetNextBlockID()),
      graph_(graph),
      phis_(4),
      first_(NULL),
      last_(NULL),
      end_(NULL),
      loop_information_(NULL),
      predecessors_(2),
      dominator_(NULL),
      dominated_blocks_(4),
      last_environment_(NULL),
      argument_count_(-1),
      first_instruction_index_(-1),
      last_instruction_index_(-1),
      deleted_phis_(4),
      parent_loop_header_(NULL),
      is_inline_return_target_(false) {
}


void HBasicBlock::AttachLoopInformation() {
  ASSERT(!IsLoopHeader());
  loop_information_ = new(zone()) HLoopInformation(this);
}


void HBasicBlock::DetachLoopInformation() {
  ASSERT(IsLoopHeader());
  loop_information_ = NULL;
}


void HBasicBlock::AddPhi(HPhi* phi) {
  ASSERT(!IsStartBlock());
  phis_.Add(phi);
  phi->SetBlock(this);
}


void HBasicBlock::RemovePhi(HPhi* phi) {
  ASSERT(phi->block() == this);
  ASSERT(phis_.Contains(phi));
  ASSERT(phi->HasNoUses() || !phi->is_live());
  phi->ClearOperands();
  phis_.RemoveElement(phi);
  phi->SetBlock(NULL);
}


void HBasicBlock::AddInstruction(HInstruction* instr) {
  ASSERT(!IsStartBlock() || !IsFinished());
  ASSERT(!instr->IsLinked());
  ASSERT(!IsFinished());
  if (first_ == NULL) {
    HBlockEntry* entry = new(zone()) HBlockEntry();
    entry->InitializeAsFirst(this);
    first_ = last_ = entry;
  }
  instr->InsertAfter(last_);
  last_ = instr;
}


HDeoptimize* HBasicBlock::CreateDeoptimize() {
  ASSERT(HasEnvironment());
  HEnvironment* environment = last_environment();

  HDeoptimize* instr = new(zone()) HDeoptimize(environment->length());

  for (int i = 0; i < environment->length(); i++) {
    HValue* val = environment->values()->at(i);
    instr->AddEnvironmentValue(val);
  }

  return instr;
}


HSimulate* HBasicBlock::CreateSimulate(int id) {
  ASSERT(HasEnvironment());
  HEnvironment* environment = last_environment();
  ASSERT(id == AstNode::kNoNumber ||
         environment->closure()->shared()->VerifyBailoutId(id));

  int push_count = environment->push_count();
  int pop_count = environment->pop_count();

  HSimulate* instr = new(zone()) HSimulate(id, pop_count);
  for (int i = push_count - 1; i >= 0; --i) {
    instr->AddPushedValue(environment->ExpressionStackAt(i));
  }
  for (int i = 0; i < environment->assigned_variables()->length(); ++i) {
    int index = environment->assigned_variables()->at(i);
    instr->AddAssignedValue(index, environment->Lookup(index));
  }
  environment->ClearHistory();
  return instr;
}


void HBasicBlock::Finish(HControlInstruction* end) {
  ASSERT(!IsFinished());
  AddInstruction(end);
  end_ = end;
  if (end->FirstSuccessor() != NULL) {
    end->FirstSuccessor()->RegisterPredecessor(this);
    if (end->SecondSuccessor() != NULL) {
      end->SecondSuccessor()->RegisterPredecessor(this);
    }
  }
}


void HBasicBlock::Goto(HBasicBlock* block, bool include_stack_check) {
  if (block->IsInlineReturnTarget()) {
    AddInstruction(new(zone()) HLeaveInlined);
    last_environment_ = last_environment()->outer();
  }
  AddSimulate(AstNode::kNoNumber);
  HGoto* instr = new(zone()) HGoto(block);
  instr->set_include_stack_check(include_stack_check);
  Finish(instr);
}


void HBasicBlock::AddLeaveInlined(HValue* return_value, HBasicBlock* target) {
  ASSERT(target->IsInlineReturnTarget());
  ASSERT(return_value != NULL);
  AddInstruction(new(zone()) HLeaveInlined);
  last_environment_ = last_environment()->outer();
  last_environment()->Push(return_value);
  AddSimulate(AstNode::kNoNumber);
  HGoto* instr = new(zone()) HGoto(target);
  Finish(instr);
}


void HBasicBlock::SetInitialEnvironment(HEnvironment* env) {
  ASSERT(!HasEnvironment());
  ASSERT(first() == NULL);
  UpdateEnvironment(env);
}


void HBasicBlock::SetJoinId(int id) {
  int length = predecessors_.length();
  ASSERT(length > 0);
  for (int i = 0; i < length; i++) {
    HBasicBlock* predecessor = predecessors_[i];
    ASSERT(predecessor->end()->IsGoto());
    HSimulate* simulate = HSimulate::cast(predecessor->end()->previous());
    // We only need to verify the ID once.
    ASSERT(i != 0 ||
           predecessor->last_environment()->closure()->shared()
               ->VerifyBailoutId(id));
    simulate->set_ast_id(id);
  }
}


bool HBasicBlock::Dominates(HBasicBlock* other) const {
  HBasicBlock* current = other->dominator();
  while (current != NULL) {
    if (current == this) return true;
    current = current->dominator();
  }
  return false;
}


void HBasicBlock::PostProcessLoopHeader(IterationStatement* stmt) {
  ASSERT(IsLoopHeader());

  SetJoinId(stmt->EntryId());
  if (predecessors()->length() == 1) {
    // This is a degenerated loop.
    DetachLoopInformation();
    return;
  }

  // Only the first entry into the loop is from outside the loop. All other
  // entries must be back edges.
  for (int i = 1; i < predecessors()->length(); ++i) {
    loop_information()->RegisterBackEdge(predecessors()->at(i));
  }
}


void HBasicBlock::RegisterPredecessor(HBasicBlock* pred) {
  if (!predecessors_.is_empty()) {
    // Only loop header blocks can have a predecessor added after
    // instructions have been added to the block (they have phis for all
    // values in the environment, these phis may be eliminated later).
    ASSERT(IsLoopHeader() || first_ == NULL);
    HEnvironment* incoming_env = pred->last_environment();
    if (IsLoopHeader()) {
      ASSERT(phis()->length() == incoming_env->length());
      for (int i = 0; i < phis_.length(); ++i) {
        phis_[i]->AddInput(incoming_env->values()->at(i));
      }
    } else {
      last_environment()->AddIncomingEdge(this, pred->last_environment());
    }
  } else if (!HasEnvironment() && !IsFinished()) {
    ASSERT(!IsLoopHeader());
    SetInitialEnvironment(pred->last_environment()->Copy());
  }

  predecessors_.Add(pred);
}


void HBasicBlock::AddDominatedBlock(HBasicBlock* block) {
  ASSERT(!dominated_blocks_.Contains(block));
  // Keep the list of dominated blocks sorted such that if there is two
  // succeeding block in this list, the predecessor is before the successor.
  int index = 0;
  while (index < dominated_blocks_.length() &&
         dominated_blocks_[index]->block_id() < block->block_id()) {
    ++index;
  }
  dominated_blocks_.InsertAt(index, block);
}


void HBasicBlock::AssignCommonDominator(HBasicBlock* other) {
  if (dominator_ == NULL) {
    dominator_ = other;
    other->AddDominatedBlock(this);
  } else if (other->dominator() != NULL) {
    HBasicBlock* first = dominator_;
    HBasicBlock* second = other;

    while (first != second) {
      if (first->block_id() > second->block_id()) {
        first = first->dominator();
      } else {
        second = second->dominator();
      }
      ASSERT(first != NULL && second != NULL);
    }

    if (dominator_ != first) {
      ASSERT(dominator_->dominated_blocks_.Contains(this));
      dominator_->dominated_blocks_.RemoveElement(this);
      dominator_ = first;
      first->AddDominatedBlock(this);
    }
  }
}


int HBasicBlock::PredecessorIndexOf(HBasicBlock* predecessor) const {
  for (int i = 0; i < predecessors_.length(); ++i) {
    if (predecessors_[i] == predecessor) return i;
  }
  UNREACHABLE();
  return -1;
}


#ifdef DEBUG
void HBasicBlock::Verify() {
  // Check that every block is finished.
  ASSERT(IsFinished());
  ASSERT(block_id() >= 0);

  // Check that the incoming edges are in edge split form.
  if (predecessors_.length() > 1) {
    for (int i = 0; i < predecessors_.length(); ++i) {
      ASSERT(predecessors_[i]->end()->SecondSuccessor() == NULL);
    }
  }
}
#endif


void HLoopInformation::RegisterBackEdge(HBasicBlock* block) {
  this->back_edges_.Add(block);
  AddBlock(block);
}


HBasicBlock* HLoopInformation::GetLastBackEdge() const {
  int max_id = -1;
  HBasicBlock* result = NULL;
  for (int i = 0; i < back_edges_.length(); ++i) {
    HBasicBlock* cur = back_edges_[i];
    if (cur->block_id() > max_id) {
      max_id = cur->block_id();
      result = cur;
    }
  }
  return result;
}


void HLoopInformation::AddBlock(HBasicBlock* block) {
  if (block == loop_header()) return;
  if (block->parent_loop_header() == loop_header()) return;
  if (block->parent_loop_header() != NULL) {
    AddBlock(block->parent_loop_header());
  } else {
    block->set_parent_loop_header(loop_header());
    blocks_.Add(block);
    for (int i = 0; i < block->predecessors()->length(); ++i) {
      AddBlock(block->predecessors()->at(i));
    }
  }
}


#ifdef DEBUG

// Checks reachability of the blocks in this graph and stores a bit in
// the BitVector "reachable()" for every block that can be reached
// from the start block of the graph. If "dont_visit" is non-null, the given
// block is treated as if it would not be part of the graph. "visited_count()"
// returns the number of reachable blocks.
class ReachabilityAnalyzer BASE_EMBEDDED {
 public:
  ReachabilityAnalyzer(HBasicBlock* entry_block,
                       int block_count,
                       HBasicBlock* dont_visit)
      : visited_count_(0),
        stack_(16),
        reachable_(block_count),
        dont_visit_(dont_visit) {
    PushBlock(entry_block);
    Analyze();
  }

  int visited_count() const { return visited_count_; }
  const BitVector* reachable() const { return &reachable_; }

 private:
  void PushBlock(HBasicBlock* block) {
    if (block != NULL && block != dont_visit_ &&
        !reachable_.Contains(block->block_id())) {
      reachable_.Add(block->block_id());
      stack_.Add(block);
      visited_count_++;
    }
  }

  void Analyze() {
    while (!stack_.is_empty()) {
      HControlInstruction* end = stack_.RemoveLast()->end();
      PushBlock(end->FirstSuccessor());
      PushBlock(end->SecondSuccessor());
    }
  }

  int visited_count_;
  ZoneList<HBasicBlock*> stack_;
  BitVector reachable_;
  HBasicBlock* dont_visit_;
};


void HGraph::Verify() const {
  for (int i = 0; i < blocks_.length(); i++) {
    HBasicBlock* block = blocks_.at(i);

    block->Verify();

    // Check that every block contains at least one node and that only the last
    // node is a control instruction.
    HInstruction* current = block->first();
    ASSERT(current != NULL && current->IsBlockEntry());
    while (current != NULL) {
      ASSERT((current->next() == NULL) == current->IsControlInstruction());
      ASSERT(current->block() == block);
      current->Verify();
      current = current->next();
    }

    // Check that successors are correctly set.
    HBasicBlock* first = block->end()->FirstSuccessor();
    HBasicBlock* second = block->end()->SecondSuccessor();
    ASSERT(second == NULL || first != NULL);

    // Check that the predecessor array is correct.
    if (first != NULL) {
      ASSERT(first->predecessors()->Contains(block));
      if (second != NULL) {
        ASSERT(second->predecessors()->Contains(block));
      }
    }

    // Check that phis have correct arguments.
    for (int j = 0; j < block->phis()->length(); j++) {
      HPhi* phi = block->phis()->at(j);
      phi->Verify();
    }

    // Check that all join blocks have predecessors that end with an
    // unconditional goto and agree on their environment node id.
    if (block->predecessors()->length() >= 2) {
      int id = block->predecessors()->first()->last_environment()->ast_id();
      for (int k = 0; k < block->predecessors()->length(); k++) {
        HBasicBlock* predecessor = block->predecessors()->at(k);
        ASSERT(predecessor->end()->IsGoto());
        ASSERT(predecessor->last_environment()->ast_id() == id);
      }
    }
  }

  // Check special property of first block to have no predecessors.
  ASSERT(blocks_.at(0)->predecessors()->is_empty());

  // Check that the graph is fully connected.
  ReachabilityAnalyzer analyzer(entry_block_, blocks_.length(), NULL);
  ASSERT(analyzer.visited_count() == blocks_.length());

  // Check that entry block dominator is NULL.
  ASSERT(entry_block_->dominator() == NULL);

  // Check dominators.
  for (int i = 0; i < blocks_.length(); ++i) {
    HBasicBlock* block = blocks_.at(i);
    if (block->dominator() == NULL) {
      // Only start block may have no dominator assigned to.
      ASSERT(i == 0);
    } else {
      // Assert that block is unreachable if dominator must not be visited.
      ReachabilityAnalyzer dominator_analyzer(entry_block_,
                                              blocks_.length(),
                                              block->dominator());
      ASSERT(!dominator_analyzer.reachable()->Contains(block->block_id()));
    }
  }
}

#endif


HConstant* HGraph::GetConstant(SetOncePointer<HConstant>* pointer,
                               Object* value) {
  if (!pointer->is_set()) {
    HConstant* constant = new(zone()) HConstant(Handle<Object>(value),
                                                Representation::Tagged());
    constant->InsertAfter(GetConstantUndefined());
    pointer->set(constant);
  }
  return pointer->get();
}


HConstant* HGraph::GetConstant1() {
  return GetConstant(&constant_1_, Smi::FromInt(1));
}


HConstant* HGraph::GetConstantMinus1() {
  return GetConstant(&constant_minus1_, Smi::FromInt(-1));
}


HConstant* HGraph::GetConstantTrue() {
  return GetConstant(&constant_true_, isolate()->heap()->true_value());
}


HConstant* HGraph::GetConstantFalse() {
  return GetConstant(&constant_false_, isolate()->heap()->false_value());
}


HBasicBlock* HGraphBuilder::CreateJoin(HBasicBlock* first,
                                       HBasicBlock* second,
                                       int join_id) {
  if (first == NULL) {
    return second;
  } else if (second == NULL) {
    return first;
  } else {
    HBasicBlock* join_block = graph_->CreateBasicBlock();
    first->Goto(join_block);
    second->Goto(join_block);
    join_block->SetJoinId(join_id);
    return join_block;
  }
}


HBasicBlock* HGraphBuilder::JoinContinue(IterationStatement* statement,
                                         HBasicBlock* exit_block,
                                         HBasicBlock* continue_block) {
  if (continue_block != NULL) {
    if (exit_block != NULL) exit_block->Goto(continue_block);
    continue_block->SetJoinId(statement->ContinueId());
    return continue_block;
  }
  return exit_block;
}


HBasicBlock* HGraphBuilder::CreateLoop(IterationStatement* statement,
                                       HBasicBlock* loop_entry,
                                       HBasicBlock* body_exit,
                                       HBasicBlock* loop_successor,
                                       HBasicBlock* break_block) {
  if (body_exit != NULL) body_exit->Goto(loop_entry, true);
  loop_entry->PostProcessLoopHeader(statement);
  if (break_block != NULL) {
    if (loop_successor != NULL) loop_successor->Goto(break_block);
    break_block->SetJoinId(statement->ExitId());
    return break_block;
  }
  return loop_successor;
}


void HBasicBlock::FinishExit(HControlInstruction* instruction) {
  Finish(instruction);
  ClearEnvironment();
}


HGraph::HGraph(CompilationInfo* info)
    : isolate_(info->isolate()),
      next_block_id_(0),
      entry_block_(NULL),
      blocks_(8),
      values_(16),
      phi_list_(NULL) {
  start_environment_ =
      new(zone()) HEnvironment(NULL, info->scope(), info->closure());
  start_environment_->set_ast_id(AstNode::kFunctionEntryId);
  entry_block_ = CreateBasicBlock();
  entry_block_->SetInitialEnvironment(start_environment_);
}


Handle<Code> HGraph::Compile(CompilationInfo* info) {
  int values = GetMaximumValueID();
  if (values > LAllocator::max_initial_value_ids()) {
    if (FLAG_trace_bailout) PrintF("Function is too big\n");
    return Handle<Code>::null();
  }

  LAllocator allocator(values, this);
  LChunkBuilder builder(info, this, &allocator);
  LChunk* chunk = builder.Build();
  if (chunk == NULL) return Handle<Code>::null();

  if (!FLAG_alloc_lithium) return Handle<Code>::null();

  allocator.Allocate(chunk);

  if (!FLAG_use_lithium) return Handle<Code>::null();

  MacroAssembler assembler(info->isolate(), NULL, 0);
  LCodeGen generator(chunk, &assembler, info);

  if (FLAG_eliminate_empty_blocks) {
    chunk->MarkEmptyBlocks();
  }

  if (generator.GenerateCode()) {
    if (FLAG_trace_codegen) {
      PrintF("Crankshaft Compiler - ");
    }
    CodeGenerator::MakeCodePrologue(info);
    Code::Flags flags =
        Code::ComputeFlags(Code::OPTIMIZED_FUNCTION, NOT_IN_LOOP);
    Handle<Code> code =
        CodeGenerator::MakeCodeEpilogue(&assembler, flags, info);
    generator.FinishCode(code);
    CodeGenerator::PrintCode(code, info);
    return code;
  }
  return Handle<Code>::null();
}


HBasicBlock* HGraph::CreateBasicBlock() {
  HBasicBlock* result = new(zone()) HBasicBlock(this);
  blocks_.Add(result);
  return result;
}


void HGraph::Canonicalize() {
  if (!FLAG_use_canonicalizing) return;
  HPhase phase("Canonicalize", this);
  for (int i = 0; i < blocks()->length(); ++i) {
    HInstruction* instr = blocks()->at(i)->first();
    while (instr != NULL) {
      HValue* value = instr->Canonicalize();
      if (value != instr) instr->ReplaceAndDelete(value);
      instr = instr->next();
    }
  }
}


void HGraph::OrderBlocks() {
  HPhase phase("Block ordering");
  BitVector visited(blocks_.length());

  ZoneList<HBasicBlock*> reverse_result(8);
  HBasicBlock* start = blocks_[0];
  Postorder(start, &visited, &reverse_result, NULL);

  blocks_.Rewind(0);
  int index = 0;
  for (int i = reverse_result.length() - 1; i >= 0; --i) {
    HBasicBlock* b = reverse_result[i];
    blocks_.Add(b);
    b->set_block_id(index++);
  }
}


void HGraph::PostorderLoopBlocks(HLoopInformation* loop,
                                 BitVector* visited,
                                 ZoneList<HBasicBlock*>* order,
                                 HBasicBlock* loop_header) {
  for (int i = 0; i < loop->blocks()->length(); ++i) {
    HBasicBlock* b = loop->blocks()->at(i);
    Postorder(b->end()->SecondSuccessor(), visited, order, loop_header);
    Postorder(b->end()->FirstSuccessor(), visited, order, loop_header);
    if (b->IsLoopHeader() && b != loop->loop_header()) {
      PostorderLoopBlocks(b->loop_information(), visited, order, loop_header);
    }
  }
}


void HGraph::Postorder(HBasicBlock* block,
                       BitVector* visited,
                       ZoneList<HBasicBlock*>* order,
                       HBasicBlock* loop_header) {
  if (block == NULL || visited->Contains(block->block_id())) return;
  if (block->parent_loop_header() != loop_header) return;
  visited->Add(block->block_id());
  if (block->IsLoopHeader()) {
    PostorderLoopBlocks(block->loop_information(), visited, order, loop_header);
    Postorder(block->end()->SecondSuccessor(), visited, order, block);
    Postorder(block->end()->FirstSuccessor(), visited, order, block);
  } else {
    Postorder(block->end()->SecondSuccessor(), visited, order, loop_header);
    Postorder(block->end()->FirstSuccessor(), visited, order, loop_header);
  }
  ASSERT(block->end()->FirstSuccessor() == NULL ||
         order->Contains(block->end()->FirstSuccessor()) ||
         block->end()->FirstSuccessor()->IsLoopHeader());
  ASSERT(block->end()->SecondSuccessor() == NULL ||
         order->Contains(block->end()->SecondSuccessor()) ||
         block->end()->SecondSuccessor()->IsLoopHeader());
  order->Add(block);
}


void HGraph::AssignDominators() {
  HPhase phase("Assign dominators", this);
  for (int i = 0; i < blocks_.length(); ++i) {
    if (blocks_[i]->IsLoopHeader()) {
      blocks_[i]->AssignCommonDominator(blocks_[i]->predecessors()->first());
    } else {
      for (int j = 0; j < blocks_[i]->predecessors()->length(); ++j) {
        blocks_[i]->AssignCommonDominator(blocks_[i]->predecessors()->at(j));
      }
    }
  }
}


void HGraph::EliminateRedundantPhis() {
  HPhase phase("Redundant phi elimination", this);

  // Worklist of phis that can potentially be eliminated. Initialized
  // with all phi nodes. When elimination of a phi node modifies
  // another phi node the modified phi node is added to the worklist.
  ZoneList<HPhi*> worklist(blocks_.length());
  for (int i = 0; i < blocks_.length(); ++i) {
    worklist.AddAll(*blocks_[i]->phis());
  }

  while (!worklist.is_empty()) {
    HPhi* phi = worklist.RemoveLast();
    HBasicBlock* block = phi->block();

    // Skip phi node if it was already replaced.
    if (block == NULL) continue;

    // Get replacement value if phi is redundant.
    HValue* value = phi->GetRedundantReplacement();

    if (value != NULL) {
      // Iterate through uses finding the ones that should be
      // replaced.
      SmallPointerList<HValue>* uses = phi->uses();
      while (!uses->is_empty()) {
        HValue* use = uses->RemoveLast();
        if (use != NULL) {
          phi->ReplaceAtUse(use, value);
          if (use->IsPhi()) worklist.Add(HPhi::cast(use));
        }
      }
      block->RemovePhi(phi);
    }
  }
}


void HGraph::EliminateUnreachablePhis() {
  HPhase phase("Unreachable phi elimination", this);

  // Initialize worklist.
  ZoneList<HPhi*> phi_list(blocks_.length());
  ZoneList<HPhi*> worklist(blocks_.length());
  for (int i = 0; i < blocks_.length(); ++i) {
    for (int j = 0; j < blocks_[i]->phis()->length(); j++) {
      HPhi* phi = blocks_[i]->phis()->at(j);
      phi_list.Add(phi);
      // We can't eliminate phis in the receiver position in the environment
      // because in case of throwing an error we need this value to
      // construct a stack trace.
      if (phi->HasRealUses() || phi->IsReceiver())  {
        phi->set_is_live(true);
        worklist.Add(phi);
      }
    }
  }

  // Iteratively mark live phis.
  while (!worklist.is_empty()) {
    HPhi* phi = worklist.RemoveLast();
    for (int i = 0; i < phi->OperandCount(); i++) {
      HValue* operand = phi->OperandAt(i);
      if (operand->IsPhi() && !HPhi::cast(operand)->is_live()) {
        HPhi::cast(operand)->set_is_live(true);
        worklist.Add(HPhi::cast(operand));
      }
    }
  }

  // Remove unreachable phis.
  for (int i = 0; i < phi_list.length(); i++) {
    HPhi* phi = phi_list[i];
    if (!phi->is_live()) {
      HBasicBlock* block = phi->block();
      block->RemovePhi(phi);
      block->RecordDeletedPhi(phi->merged_index());
    }
  }
}


bool HGraph::CollectPhis() {
  int block_count = blocks_.length();
  phi_list_ = new ZoneList<HPhi*>(block_count);
  for (int i = 0; i < block_count; ++i) {
    for (int j = 0; j < blocks_[i]->phis()->length(); ++j) {
      HPhi* phi = blocks_[i]->phis()->at(j);
      phi_list_->Add(phi);
      // We don't support phi uses of arguments for now.
      if (phi->CheckFlag(HValue::kIsArguments)) return false;
    }
  }
  return true;
}


void HGraph::InferTypes(ZoneList<HValue*>* worklist) {
  BitVector in_worklist(GetMaximumValueID());
  for (int i = 0; i < worklist->length(); ++i) {
    ASSERT(!in_worklist.Contains(worklist->at(i)->id()));
    in_worklist.Add(worklist->at(i)->id());
  }

  while (!worklist->is_empty()) {
    HValue* current = worklist->RemoveLast();
    in_worklist.Remove(current->id());
    if (current->UpdateInferredType()) {
      for (int j = 0; j < current->uses()->length(); j++) {
        HValue* use = current->uses()->at(j);
        if (!in_worklist.Contains(use->id())) {
          in_worklist.Add(use->id());
          worklist->Add(use);
        }
      }
    }
  }
}


class HRangeAnalysis BASE_EMBEDDED {
 public:
  explicit HRangeAnalysis(HGraph* graph) : graph_(graph), changed_ranges_(16) {}

  void Analyze();

 private:
  void TraceRange(const char* msg, ...);
  void Analyze(HBasicBlock* block);
  void InferControlFlowRange(HTest* test, HBasicBlock* dest);
  void InferControlFlowRange(Token::Value op, HValue* value, HValue* other);
  void InferPhiRange(HPhi* phi);
  void InferRange(HValue* value);
  void RollBackTo(int index);
  void AddRange(HValue* value, Range* range);

  HGraph* graph_;
  ZoneList<HValue*> changed_ranges_;
};


void HRangeAnalysis::TraceRange(const char* msg, ...) {
  if (FLAG_trace_range) {
    va_list arguments;
    va_start(arguments, msg);
    OS::VPrint(msg, arguments);
    va_end(arguments);
  }
}


void HRangeAnalysis::Analyze() {
  HPhase phase("Range analysis", graph_);
  Analyze(graph_->blocks()->at(0));
}


void HRangeAnalysis::Analyze(HBasicBlock* block) {
  TraceRange("Analyzing block B%d\n", block->block_id());

  int last_changed_range = changed_ranges_.length() - 1;

  // Infer range based on control flow.
  if (block->predecessors()->length() == 1) {
    HBasicBlock* pred = block->predecessors()->first();
    if (pred->end()->IsTest()) {
      InferControlFlowRange(HTest::cast(pred->end()), block);
    }
  }

  // Process phi instructions.
  for (int i = 0; i < block->phis()->length(); ++i) {
    HPhi* phi = block->phis()->at(i);
    InferPhiRange(phi);
  }

  // Go through all instructions of the current block.
  HInstruction* instr = block->first();
  while (instr != block->end()) {
    InferRange(instr);
    instr = instr->next();
  }

  // Continue analysis in all dominated blocks.
  for (int i = 0; i < block->dominated_blocks()->length(); ++i) {
    Analyze(block->dominated_blocks()->at(i));
  }

  RollBackTo(last_changed_range);
}


void HRangeAnalysis::InferControlFlowRange(HTest* test, HBasicBlock* dest) {
  ASSERT((test->FirstSuccessor() == dest) == (test->SecondSuccessor() != dest));
  if (test->value()->IsCompare()) {
    HCompare* compare = HCompare::cast(test->value());
    if (compare->GetInputRepresentation().IsInteger32()) {
      Token::Value op = compare->token();
      if (test->SecondSuccessor() == dest) {
        op = Token::NegateCompareOp(op);
      }
      Token::Value inverted_op = Token::InvertCompareOp(op);
      InferControlFlowRange(op, compare->left(), compare->right());
      InferControlFlowRange(inverted_op, compare->right(), compare->left());
    }
  }
}


// We know that value [op] other. Use this information to update the range on
// value.
void HRangeAnalysis::InferControlFlowRange(Token::Value op,
                                           HValue* value,
                                           HValue* other) {
  Range temp_range;
  Range* range = other->range() != NULL ? other->range() : &temp_range;
  Range* new_range = NULL;

  TraceRange("Control flow range infer %d %s %d\n",
             value->id(),
             Token::Name(op),
             other->id());

  if (op == Token::EQ || op == Token::EQ_STRICT) {
    // The same range has to apply for value.
    new_range = range->Copy();
  } else if (op == Token::LT || op == Token::LTE) {
    new_range = range->CopyClearLower();
    if (op == Token::LT) {
      new_range->AddConstant(-1);
    }
  } else if (op == Token::GT || op == Token::GTE) {
    new_range = range->CopyClearUpper();
    if (op == Token::GT) {
      new_range->AddConstant(1);
    }
  }

  if (new_range != NULL && !new_range->IsMostGeneric()) {
    AddRange(value, new_range);
  }
}


void HRangeAnalysis::InferPhiRange(HPhi* phi) {
  // TODO(twuerthinger): Infer loop phi ranges.
  InferRange(phi);
}


void HRangeAnalysis::InferRange(HValue* value) {
  ASSERT(!value->HasRange());
  if (!value->representation().IsNone()) {
    value->ComputeInitialRange();
    Range* range = value->range();
    TraceRange("Initial inferred range of %d (%s) set to [%d,%d]\n",
               value->id(),
               value->Mnemonic(),
               range->lower(),
               range->upper());
  }
}


void HRangeAnalysis::RollBackTo(int index) {
  for (int i = index + 1; i < changed_ranges_.length(); ++i) {
    changed_ranges_[i]->RemoveLastAddedRange();
  }
  changed_ranges_.Rewind(index + 1);
}


void HRangeAnalysis::AddRange(HValue* value, Range* range) {
  Range* original_range = value->range();
  value->AddNewRange(range);
  changed_ranges_.Add(value);
  Range* new_range = value->range();
  TraceRange("Updated range of %d set to [%d,%d]\n",
             value->id(),
             new_range->lower(),
             new_range->upper());
  if (original_range != NULL) {
    TraceRange("Original range was [%d,%d]\n",
               original_range->lower(),
               original_range->upper());
  }
  TraceRange("New information was [%d,%d]\n",
             range->lower(),
             range->upper());
}


void TraceGVN(const char* msg, ...) {
  if (FLAG_trace_gvn) {
    va_list arguments;
    va_start(arguments, msg);
    OS::VPrint(msg, arguments);
    va_end(arguments);
  }
}


HValueMap::HValueMap(const HValueMap* other)
    : array_size_(other->array_size_),
      lists_size_(other->lists_size_),
      count_(other->count_),
      present_flags_(other->present_flags_),
      array_(ZONE->NewArray<HValueMapListElement>(other->array_size_)),
      lists_(ZONE->NewArray<HValueMapListElement>(other->lists_size_)),
      free_list_head_(other->free_list_head_) {
  memcpy(array_, other->array_, array_size_ * sizeof(HValueMapListElement));
  memcpy(lists_, other->lists_, lists_size_ * sizeof(HValueMapListElement));
}


void HValueMap::Kill(int flags) {
  int depends_flags = HValue::ConvertChangesToDependsFlags(flags);
  if ((present_flags_ & depends_flags) == 0) return;
  present_flags_ = 0;
  for (int i = 0; i < array_size_; ++i) {
    HValue* value = array_[i].value;
    if (value != NULL) {
      // Clear list of collisions first, so we know if it becomes empty.
      int kept = kNil;  // List of kept elements.
      int next;
      for (int current = array_[i].next; current != kNil; current = next) {
        next = lists_[current].next;
        if ((lists_[current].value->flags() & depends_flags) != 0) {
          // Drop it.
          count_--;
          lists_[current].next = free_list_head_;
          free_list_head_ = current;
        } else {
          // Keep it.
          lists_[current].next = kept;
          kept = current;
          present_flags_ |= lists_[current].value->flags();
        }
      }
      array_[i].next = kept;

      // Now possibly drop directly indexed element.
      if ((array_[i].value->flags() & depends_flags) != 0) {  // Drop it.
        count_--;
        int head = array_[i].next;
        if (head == kNil) {
          array_[i].value = NULL;
        } else {
          array_[i].value = lists_[head].value;
          array_[i].next = lists_[head].next;
          lists_[head].next = free_list_head_;
          free_list_head_ = head;
        }
      } else {
        present_flags_ |= array_[i].value->flags();  // Keep it.
      }
    }
  }
}


HValue* HValueMap::Lookup(HValue* value) const {
  uint32_t hash = static_cast<uint32_t>(value->Hashcode());
  uint32_t pos = Bound(hash);
  if (array_[pos].value != NULL) {
    if (array_[pos].value->Equals(value)) return array_[pos].value;
    int next = array_[pos].next;
    while (next != kNil) {
      if (lists_[next].value->Equals(value)) return lists_[next].value;
      next = lists_[next].next;
    }
  }
  return NULL;
}


void HValueMap::Resize(int new_size) {
  ASSERT(new_size > count_);
  // Hashing the values into the new array has no more collisions than in the
  // old hash map, so we can use the existing lists_ array, if we are careful.

  // Make sure we have at least one free element.
  if (free_list_head_ == kNil) {
    ResizeLists(lists_size_ << 1);
  }

  HValueMapListElement* new_array =
      ZONE->NewArray<HValueMapListElement>(new_size);
  memset(new_array, 0, sizeof(HValueMapListElement) * new_size);

  HValueMapListElement* old_array = array_;
  int old_size = array_size_;

  int old_count = count_;
  count_ = 0;
  // Do not modify present_flags_.  It is currently correct.
  array_size_ = new_size;
  array_ = new_array;

  if (old_array != NULL) {
    // Iterate over all the elements in lists, rehashing them.
    for (int i = 0; i < old_size; ++i) {
      if (old_array[i].value != NULL) {
        int current = old_array[i].next;
        while (current != kNil) {
          Insert(lists_[current].value);
          int next = lists_[current].next;
          lists_[current].next = free_list_head_;
          free_list_head_ = current;
          current = next;
        }
        // Rehash the directly stored value.
        Insert(old_array[i].value);
      }
    }
  }
  USE(old_count);
  ASSERT(count_ == old_count);
}


void HValueMap::ResizeLists(int new_size) {
  ASSERT(new_size > lists_size_);

  HValueMapListElement* new_lists =
      ZONE->NewArray<HValueMapListElement>(new_size);
  memset(new_lists, 0, sizeof(HValueMapListElement) * new_size);

  HValueMapListElement* old_lists = lists_;
  int old_size = lists_size_;

  lists_size_ = new_size;
  lists_ = new_lists;

  if (old_lists != NULL) {
    memcpy(lists_, old_lists, old_size * sizeof(HValueMapListElement));
  }
  for (int i = old_size; i < lists_size_; ++i) {
    lists_[i].next = free_list_head_;
    free_list_head_ = i;
  }
}


void HValueMap::Insert(HValue* value) {
  ASSERT(value != NULL);
  // Resizing when half of the hashtable is filled up.
  if (count_ >= array_size_ >> 1) Resize(array_size_ << 1);
  ASSERT(count_ < array_size_);
  count_++;
  uint32_t pos = Bound(static_cast<uint32_t>(value->Hashcode()));
  if (array_[pos].value == NULL) {
    array_[pos].value = value;
    array_[pos].next = kNil;
  } else {
    if (free_list_head_ == kNil) {
      ResizeLists(lists_size_ << 1);
    }
    int new_element_pos = free_list_head_;
    ASSERT(new_element_pos != kNil);
    free_list_head_ = lists_[free_list_head_].next;
    lists_[new_element_pos].value = value;
    lists_[new_element_pos].next = array_[pos].next;
    ASSERT(array_[pos].next == kNil || lists_[array_[pos].next].value != NULL);
    array_[pos].next = new_element_pos;
  }
}


class HStackCheckEliminator BASE_EMBEDDED {
 public:
  explicit HStackCheckEliminator(HGraph* graph) : graph_(graph) { }

  void Process();

 private:
  void RemoveStackCheck(HBasicBlock* block);

  HGraph* graph_;
};


void HStackCheckEliminator::Process() {
  // For each loop block walk the dominator tree from the backwards branch to
  // the loop header. If a call instruction is encountered the backwards branch
  // is dominated by a call and the stack check in the backwards branch can be
  // removed.
  for (int i = 0; i < graph_->blocks()->length(); i++) {
    HBasicBlock* block = graph_->blocks()->at(i);
    if (block->IsLoopHeader()) {
      HBasicBlock* back_edge = block->loop_information()->GetLastBackEdge();
      HBasicBlock* dominator = back_edge;
      bool back_edge_dominated_by_call = false;
      while (dominator != block && !back_edge_dominated_by_call) {
        HInstruction* instr = dominator->first();
        while (instr != NULL && !back_edge_dominated_by_call) {
          if (instr->IsCall()) {
            RemoveStackCheck(back_edge);
            back_edge_dominated_by_call = true;
          }
          instr = instr->next();
        }
        dominator = dominator->dominator();
      }
    }
  }
}


void HStackCheckEliminator::RemoveStackCheck(HBasicBlock* block) {
  HInstruction* instr = block->first();
  while (instr != NULL) {
    if (instr->IsGoto()) {
      HGoto::cast(instr)->set_include_stack_check(false);
      return;
    }
    instr = instr->next();
  }
}


class HGlobalValueNumberer BASE_EMBEDDED {
 public:
  explicit HGlobalValueNumberer(HGraph* graph, CompilationInfo* info)
      : graph_(graph),
        info_(info),
        block_side_effects_(graph_->blocks()->length()),
        loop_side_effects_(graph_->blocks()->length()) {
    ASSERT(info->isolate()->heap()->allow_allocation(false));
    block_side_effects_.AddBlock(0, graph_->blocks()->length());
    loop_side_effects_.AddBlock(0, graph_->blocks()->length());
  }
  ~HGlobalValueNumberer() {
    ASSERT(!info_->isolate()->heap()->allow_allocation(true));
  }

  void Analyze();

 private:
  void AnalyzeBlock(HBasicBlock* block, HValueMap* map);
  void ComputeBlockSideEffects();
  void LoopInvariantCodeMotion();
  void ProcessLoopBlock(HBasicBlock* block,
                        HBasicBlock* before_loop,
                        int loop_kills);
  bool AllowCodeMotion();
  bool ShouldMove(HInstruction* instr, HBasicBlock* loop_header);

  HGraph* graph() { return graph_; }
  CompilationInfo* info() { return info_; }
  Zone* zone() { return graph_->zone(); }

  HGraph* graph_;
  CompilationInfo* info_;

  // A map of block IDs to their side effects.
  ZoneList<int> block_side_effects_;

  // A map of loop header block IDs to their loop's side effects.
  ZoneList<int> loop_side_effects_;
};


void HGlobalValueNumberer::Analyze() {
  ComputeBlockSideEffects();
  if (FLAG_loop_invariant_code_motion) {
    LoopInvariantCodeMotion();
  }
  HValueMap* map = new(zone()) HValueMap();
  AnalyzeBlock(graph_->blocks()->at(0), map);
}


void HGlobalValueNumberer::ComputeBlockSideEffects() {
  for (int i = graph_->blocks()->length() - 1; i >= 0; --i) {
    // Compute side effects for the block.
    HBasicBlock* block = graph_->blocks()->at(i);
    HInstruction* instr = block->first();
    int id = block->block_id();
    int side_effects = 0;
    while (instr != NULL) {
      side_effects |= (instr->flags() & HValue::ChangesFlagsMask());
      instr = instr->next();
    }
    block_side_effects_[id] |= side_effects;

    // Loop headers are part of their loop.
    if (block->IsLoopHeader()) {
      loop_side_effects_[id] |= side_effects;
    }

    // Propagate loop side effects upwards.
    if (block->HasParentLoopHeader()) {
      int header_id = block->parent_loop_header()->block_id();
      loop_side_effects_[header_id] |=
          block->IsLoopHeader() ? loop_side_effects_[id] : side_effects;
    }
  }
}


void HGlobalValueNumberer::LoopInvariantCodeMotion() {
  for (int i = graph_->blocks()->length() - 1; i >= 0; --i) {
    HBasicBlock* block = graph_->blocks()->at(i);
    if (block->IsLoopHeader()) {
      int side_effects = loop_side_effects_[block->block_id()];
      TraceGVN("Try loop invariant motion for block B%d effects=0x%x\n",
               block->block_id(),
               side_effects);

      HBasicBlock* last = block->loop_information()->GetLastBackEdge();
      for (int j = block->block_id(); j <= last->block_id(); ++j) {
        ProcessLoopBlock(graph_->blocks()->at(j), block, side_effects);
      }
    }
  }
}


void HGlobalValueNumberer::ProcessLoopBlock(HBasicBlock* block,
                                            HBasicBlock* loop_header,
                                            int loop_kills) {
  HBasicBlock* pre_header = loop_header->predecessors()->at(0);
  int depends_flags = HValue::ConvertChangesToDependsFlags(loop_kills);
  TraceGVN("Loop invariant motion for B%d depends_flags=0x%x\n",
           block->block_id(),
           depends_flags);
  HInstruction* instr = block->first();
  while (instr != NULL) {
    HInstruction* next = instr->next();
    if (instr->CheckFlag(HValue::kUseGVN) &&
        (instr->flags() & depends_flags) == 0) {
      TraceGVN("Checking instruction %d (%s)\n",
               instr->id(),
               instr->Mnemonic());
      bool inputs_loop_invariant = true;
      for (int i = 0; i < instr->OperandCount(); ++i) {
        if (instr->OperandAt(i)->IsDefinedAfter(pre_header)) {
          inputs_loop_invariant = false;
        }
      }

      if (inputs_loop_invariant && ShouldMove(instr, loop_header)) {
        TraceGVN("Found loop invariant instruction %d\n", instr->id());
        // Move the instruction out of the loop.
        instr->Unlink();
        instr->InsertBefore(pre_header->end());
      }
    }
    instr = next;
  }
}


bool HGlobalValueNumberer::AllowCodeMotion() {
  return info()->shared_info()->opt_count() + 1 < Compiler::kDefaultMaxOptCount;
}


bool HGlobalValueNumberer::ShouldMove(HInstruction* instr,
                                      HBasicBlock* loop_header) {
  // If we've disabled code motion, don't move any instructions.
  if (!AllowCodeMotion()) return false;

  // If --aggressive-loop-invariant-motion, move everything except change
  // instructions.
  if (FLAG_aggressive_loop_invariant_motion && !instr->IsChange()) {
    return true;
  }

  // Otherwise only move instructions that postdominate the loop header
  // (i.e. are always executed inside the loop). This is to avoid
  // unnecessary deoptimizations assuming the loop is executed at least
  // once.  TODO(fschneider): Better type feedback should give us
  // information about code that was never executed.
  HBasicBlock* block = instr->block();
  bool result = true;
  if (block != loop_header) {
    for (int i = 1; i < loop_header->predecessors()->length(); ++i) {
      bool found = false;
      HBasicBlock* pred = loop_header->predecessors()->at(i);
      while (pred != loop_header) {
        if (pred == block) found = true;
        pred = pred->dominator();
      }
      if (!found) {
        result = false;
        break;
      }
    }
  }
  return result;
}


void HGlobalValueNumberer::AnalyzeBlock(HBasicBlock* block, HValueMap* map) {
  TraceGVN("Analyzing block B%d\n", block->block_id());

  // If this is a loop header kill everything killed by the loop.
  if (block->IsLoopHeader()) {
    map->Kill(loop_side_effects_[block->block_id()]);
  }

  // Go through all instructions of the current block.
  HInstruction* instr = block->first();
  while (instr != NULL) {
    HInstruction* next = instr->next();
    int flags = (instr->flags() & HValue::ChangesFlagsMask());
    if (flags != 0) {
      ASSERT(!instr->CheckFlag(HValue::kUseGVN));
      // Clear all instructions in the map that are affected by side effects.
      map->Kill(flags);
      TraceGVN("Instruction %d kills\n", instr->id());
    } else if (instr->CheckFlag(HValue::kUseGVN)) {
      HValue* other = map->Lookup(instr);
      if (other != NULL) {
        ASSERT(instr->Equals(other) && other->Equals(instr));
        TraceGVN("Replacing value %d (%s) with value %d (%s)\n",
                 instr->id(),
                 instr->Mnemonic(),
                 other->id(),
                 other->Mnemonic());
        instr->ReplaceAndDelete(other);
      } else {
        map->Add(instr);
      }
    }
    instr = next;
  }

  // Recursively continue analysis for all immediately dominated blocks.
  int length = block->dominated_blocks()->length();
  for (int i = 0; i < length; ++i) {
    HBasicBlock* dominated = block->dominated_blocks()->at(i);
    // No need to copy the map for the last child in the dominator tree.
    HValueMap* successor_map = (i == length - 1) ? map : map->Copy(zone());

    // If the dominated block is not a successor to this block we have to
    // kill everything killed on any path between this block and the
    // dominated block.  Note we rely on the block ordering.
    bool is_successor = false;
    int predecessor_count = dominated->predecessors()->length();
    for (int j = 0; !is_successor && j < predecessor_count; ++j) {
      is_successor = (dominated->predecessors()->at(j) == block);
    }

    if (!is_successor) {
      int side_effects = 0;
      for (int j = block->block_id() + 1; j < dominated->block_id(); ++j) {
        side_effects |= block_side_effects_[j];
      }
      successor_map->Kill(side_effects);
    }

    AnalyzeBlock(dominated, successor_map);
  }
}


class HInferRepresentation BASE_EMBEDDED {
 public:
  explicit HInferRepresentation(HGraph* graph)
      : graph_(graph), worklist_(8), in_worklist_(graph->GetMaximumValueID()) {}

  void Analyze();

 private:
  Representation TryChange(HValue* current);
  void AddToWorklist(HValue* current);
  void InferBasedOnInputs(HValue* current);
  void AddDependantsToWorklist(HValue* current);
  void InferBasedOnUses(HValue* current);

  Zone* zone() { return graph_->zone(); }

  HGraph* graph_;
  ZoneList<HValue*> worklist_;
  BitVector in_worklist_;
};


void HInferRepresentation::AddToWorklist(HValue* current) {
  if (current->representation().IsSpecialization()) return;
  if (!current->CheckFlag(HValue::kFlexibleRepresentation)) return;
  if (in_worklist_.Contains(current->id())) return;
  worklist_.Add(current);
  in_worklist_.Add(current->id());
}


// This method tries to specialize the representation type of the value
// given as a parameter. The value is asked to infer its representation type
// based on its inputs. If the inferred type is more specialized, then this
// becomes the new representation type of the node.
void HInferRepresentation::InferBasedOnInputs(HValue* current) {
  Representation r = current->representation();
  if (r.IsSpecialization()) return;
  ASSERT(current->CheckFlag(HValue::kFlexibleRepresentation));
  Representation inferred = current->InferredRepresentation();
  if (inferred.IsSpecialization()) {
    current->ChangeRepresentation(inferred);
    AddDependantsToWorklist(current);
  }
}


void HInferRepresentation::AddDependantsToWorklist(HValue* current) {
  for (int i = 0; i < current->uses()->length(); ++i) {
    AddToWorklist(current->uses()->at(i));
  }
  for (int i = 0; i < current->OperandCount(); ++i) {
    AddToWorklist(current->OperandAt(i));
  }
}


// This method calculates whether specializing the representation of the value
// given as the parameter has a benefit in terms of less necessary type
// conversions. If there is a benefit, then the representation of the value is
// specialized.
void HInferRepresentation::InferBasedOnUses(HValue* current) {
  Representation r = current->representation();
  if (r.IsSpecialization() || current->HasNoUses()) return;
  ASSERT(current->CheckFlag(HValue::kFlexibleRepresentation));
  Representation new_rep = TryChange(current);
  if (!new_rep.IsNone()) {
    if (!current->representation().Equals(new_rep)) {
      current->ChangeRepresentation(new_rep);
      AddDependantsToWorklist(current);
    }
  }
}


Representation HInferRepresentation::TryChange(HValue* current) {
  // Array of use counts for each representation.
  int use_count[Representation::kNumRepresentations];
  for (int i = 0; i < Representation::kNumRepresentations; i++) {
    use_count[i] = 0;
  }

  for (int i = 0; i < current->uses()->length(); ++i) {
    HValue* use = current->uses()->at(i);
    int index = use->LookupOperandIndex(0, current);
    Representation req_rep = use->RequiredInputRepresentation(index);
    if (req_rep.IsNone()) continue;
    if (use->IsPhi()) {
      HPhi* phi = HPhi::cast(use);
      phi->AddIndirectUsesTo(&use_count[0]);
    }
    use_count[req_rep.kind()]++;
  }
  int tagged_count = use_count[Representation::kTagged];
  int double_count = use_count[Representation::kDouble];
  int int32_count = use_count[Representation::kInteger32];
  int non_tagged_count = double_count + int32_count;

  // If a non-loop phi has tagged uses, don't convert it to untagged.
  if (current->IsPhi() && !current->block()->IsLoopHeader()) {
    if (tagged_count > 0) return Representation::None();
  }

  if (non_tagged_count >= tagged_count) {
    // More untagged than tagged.
    if (double_count > 0) {
      // There is at least one usage that is a double => guess that the
      // correct representation is double.
      return Representation::Double();
    } else if (int32_count > 0) {
      return Representation::Integer32();
    }
  }
  return Representation::None();
}


void HInferRepresentation::Analyze() {
  HPhase phase("Infer representations", graph_);

  // (1) Initialize bit vectors and count real uses. Each phi
  // gets a bit-vector of length <number of phis>.
  const ZoneList<HPhi*>* phi_list = graph_->phi_list();
  int num_phis = phi_list->length();
  ScopedVector<BitVector*> connected_phis(num_phis);
  for (int i = 0; i < num_phis; i++) {
    phi_list->at(i)->InitRealUses(i);
    connected_phis[i] = new(zone()) BitVector(num_phis);
    connected_phis[i]->Add(i);
  }

  // (2) Do a fixed point iteration to find the set of connected phis.
  // A phi is connected to another phi if its value is used either
  // directly or indirectly through a transitive closure of the def-use
  // relation.
  bool change = true;
  while (change) {
    change = false;
    for (int i = 0; i < num_phis; i++) {
      HPhi* phi = phi_list->at(i);
      for (int j = 0; j < phi->uses()->length(); j++) {
        HValue* use = phi->uses()->at(j);
        if (use->IsPhi()) {
          int phi_use = HPhi::cast(use)->phi_id();
          if (connected_phis[i]->UnionIsChanged(*connected_phis[phi_use])) {
            change = true;
          }
        }
      }
    }
  }

  // (3) Sum up the non-phi use counts of all connected phis.
  // Don't include the non-phi uses of the phi itself.
  for (int i = 0; i < num_phis; i++) {
    HPhi* phi = phi_list->at(i);
    for (BitVector::Iterator it(connected_phis.at(i));
         !it.Done();
         it.Advance()) {
      int index = it.Current();
      if (index != i) {
        HPhi* it_use = phi_list->at(it.Current());
        phi->AddNonPhiUsesFrom(it_use);
      }
    }
  }

  for (int i = 0; i < graph_->blocks()->length(); ++i) {
    HBasicBlock* block = graph_->blocks()->at(i);
    const ZoneList<HPhi*>* phis = block->phis();
    for (int j = 0; j < phis->length(); ++j) {
      AddToWorklist(phis->at(j));
    }

    HInstruction* current = block->first();
    while (current != NULL) {
      AddToWorklist(current);
      current = current->next();
    }
  }

  while (!worklist_.is_empty()) {
    HValue* current = worklist_.RemoveLast();
    in_worklist_.Remove(current->id());
    InferBasedOnInputs(current);
    InferBasedOnUses(current);
  }
}


void HGraph::InitializeInferredTypes() {
  HPhase phase("Inferring types", this);
  InitializeInferredTypes(0, this->blocks_.length() - 1);
}


void HGraph::InitializeInferredTypes(int from_inclusive, int to_inclusive) {
  for (int i = from_inclusive; i <= to_inclusive; ++i) {
    HBasicBlock* block = blocks_[i];

    const ZoneList<HPhi*>* phis = block->phis();
    for (int j = 0; j < phis->length(); j++) {
      phis->at(j)->UpdateInferredType();
    }

    HInstruction* current = block->first();
    while (current != NULL) {
      current->UpdateInferredType();
      current = current->next();
    }

    if (block->IsLoopHeader()) {
      HBasicBlock* last_back_edge =
          block->loop_information()->GetLastBackEdge();
      InitializeInferredTypes(i + 1, last_back_edge->block_id());
      // Skip all blocks already processed by the recursive call.
      i = last_back_edge->block_id();
      // Update phis of the loop header now after the whole loop body is
      // guaranteed to be processed.
      ZoneList<HValue*> worklist(block->phis()->length());
      for (int j = 0; j < block->phis()->length(); ++j) {
        worklist.Add(block->phis()->at(j));
      }
      InferTypes(&worklist);
    }
  }
}


void HGraph::PropagateMinusZeroChecks(HValue* value, BitVector* visited) {
  HValue* current = value;
  while (current != NULL) {
    if (visited->Contains(current->id())) return;

    // For phis, we must propagate the check to all of its inputs.
    if (current->IsPhi()) {
      visited->Add(current->id());
      HPhi* phi = HPhi::cast(current);
      for (int i = 0; i < phi->OperandCount(); ++i) {
        PropagateMinusZeroChecks(phi->OperandAt(i), visited);
      }
      break;
    }

    // For multiplication and division, we must propagate to the left and
    // the right side.
    if (current->IsMul()) {
      HMul* mul = HMul::cast(current);
      mul->EnsureAndPropagateNotMinusZero(visited);
      PropagateMinusZeroChecks(mul->left(), visited);
      PropagateMinusZeroChecks(mul->right(), visited);
    } else if (current->IsDiv()) {
      HDiv* div = HDiv::cast(current);
      div->EnsureAndPropagateNotMinusZero(visited);
      PropagateMinusZeroChecks(div->left(), visited);
      PropagateMinusZeroChecks(div->right(), visited);
    }

    current = current->EnsureAndPropagateNotMinusZero(visited);
  }
}


void HGraph::InsertRepresentationChangeForUse(HValue* value,
                                              HValue* use,
                                              Representation to) {
  // Insert the representation change right before its use. For phi-uses we
  // insert at the end of the corresponding predecessor.
  HInstruction* next = NULL;
  if (use->IsPhi()) {
    int index = 0;
    while (use->OperandAt(index) != value) ++index;
    next = use->block()->predecessors()->at(index)->end();
  } else {
    next = HInstruction::cast(use);
  }

  // For constants we try to make the representation change at compile
  // time. When a representation change is not possible without loss of
  // information we treat constants like normal instructions and insert the
  // change instructions for them.
  HInstruction* new_value = NULL;
  bool is_truncating = use->CheckFlag(HValue::kTruncatingToInt32);
  bool deoptimize_on_undefined = use->CheckFlag(HValue::kDeoptimizeOnUndefined);
  if (value->IsConstant()) {
    HConstant* constant = HConstant::cast(value);
    // Try to create a new copy of the constant with the new representation.
    new_value = is_truncating
        ? constant->CopyToTruncatedInt32()
        : constant->CopyToRepresentation(to);
  }

  if (new_value == NULL) {
    new_value = new(zone()) HChange(value, value->representation(), to,
                                    is_truncating, deoptimize_on_undefined);
  }

  new_value->InsertBefore(next);
  value->ReplaceFirstAtUse(use, new_value, to);
}


int CompareConversionUses(HValue* a,
                          HValue* b,
                          Representation a_rep,
                          Representation b_rep) {
  if (a_rep.kind() > b_rep.kind()) {
    // Make sure specializations are separated in the result array.
    return 1;
  }
  // Put truncating conversions before non-truncating conversions.
  bool a_truncate = a->CheckFlag(HValue::kTruncatingToInt32);
  bool b_truncate = b->CheckFlag(HValue::kTruncatingToInt32);
  if (a_truncate != b_truncate) {
    return a_truncate ? -1 : 1;
  }
  // Sort by increasing block ID.
  return a->block()->block_id() - b->block()->block_id();
}


void HGraph::InsertRepresentationChangesForValue(
    HValue* current,
    ZoneList<HValue*>* to_convert,
    ZoneList<Representation>* to_convert_reps) {
  Representation r = current->representation();
  if (r.IsNone()) return;
  if (current->uses()->length() == 0) return;

  // Collect the representation changes in a sorted list.  This allows
  // us to avoid duplicate changes without searching the list.
  ASSERT(to_convert->is_empty());
  ASSERT(to_convert_reps->is_empty());
  for (int i = 0; i < current->uses()->length(); ++i) {
    HValue* use = current->uses()->at(i);
    // The occurrences index means the index within the operand array of "use"
    // at which "current" is used. While iterating through the use array we
    // also have to iterate over the different occurrence indices.
    int occurrence_index = 0;
    if (use->UsesMultipleTimes(current)) {
      occurrence_index = current->uses()->CountOccurrences(use, 0, i - 1);
      if (FLAG_trace_representation) {
        PrintF("Instruction %d is used multiple times at %d; occurrence=%d\n",
               current->id(),
               use->id(),
               occurrence_index);
      }
    }
    int operand_index = use->LookupOperandIndex(occurrence_index, current);
    Representation req = use->RequiredInputRepresentation(operand_index);
    if (req.IsNone() || req.Equals(r)) continue;
    int index = 0;
    while (index < to_convert->length() &&
           CompareConversionUses(to_convert->at(index),
                                 use,
                                 to_convert_reps->at(index),
                                 req) < 0) {
      ++index;
    }
    if (FLAG_trace_representation) {
      PrintF("Inserting a representation change to %s of %d for use at %d\n",
             req.Mnemonic(),
             current->id(),
             use->id());
    }
    to_convert->InsertAt(index, use);
    to_convert_reps->InsertAt(index, req);
  }

  for (int i = 0; i < to_convert->length(); ++i) {
    HValue* use = to_convert->at(i);
    Representation r_to = to_convert_reps->at(i);
    InsertRepresentationChangeForUse(current, use, r_to);
  }

  if (current->uses()->is_empty()) {
    ASSERT(current->IsConstant());
    current->Delete();
  }
  to_convert->Rewind(0);
  to_convert_reps->Rewind(0);
}


void HGraph::InsertRepresentationChanges() {
  HPhase phase("Insert representation changes", this);


  // Compute truncation flag for phis: Initially assume that all
  // int32-phis allow truncation and iteratively remove the ones that
  // are used in an operation that does not allow a truncating
  // conversion.
  // TODO(fschneider): Replace this with a worklist-based iteration.
  for (int i = 0; i < phi_list()->length(); i++) {
    HPhi* phi = phi_list()->at(i);
    if (phi->representation().IsInteger32()) {
      phi->SetFlag(HValue::kTruncatingToInt32);
    }
  }
  bool change = true;
  while (change) {
    change = false;
    for (int i = 0; i < phi_list()->length(); i++) {
      HPhi* phi = phi_list()->at(i);
      if (!phi->CheckFlag(HValue::kTruncatingToInt32)) continue;
      for (int j = 0; j < phi->uses()->length(); j++) {
        HValue* use = phi->uses()->at(j);
        if (!use->CheckFlag(HValue::kTruncatingToInt32)) {
          phi->ClearFlag(HValue::kTruncatingToInt32);
          change = true;
          break;
        }
      }
    }
  }

  ZoneList<HValue*> value_list(4);
  ZoneList<Representation> rep_list(4);
  for (int i = 0; i < blocks_.length(); ++i) {
    // Process phi instructions first.
    for (int j = 0; j < blocks_[i]->phis()->length(); j++) {
      HPhi* phi = blocks_[i]->phis()->at(j);
      InsertRepresentationChangesForValue(phi, &value_list, &rep_list);
    }

    // Process normal instructions.
    HInstruction* current = blocks_[i]->first();
    while (current != NULL) {
      InsertRepresentationChangesForValue(current, &value_list, &rep_list);
      current = current->next();
    }
  }
}


void HGraph::RecursivelyMarkPhiDeoptimizeOnUndefined(HPhi* phi) {
  if (phi->CheckFlag(HValue::kDeoptimizeOnUndefined)) return;
  phi->SetFlag(HValue::kDeoptimizeOnUndefined);
  for (int i = 0; i < phi->OperandCount(); ++i) {
    HValue* input = phi->OperandAt(i);
    if (input->IsPhi()) {
      RecursivelyMarkPhiDeoptimizeOnUndefined(HPhi::cast(input));
    }
  }
}


void HGraph::MarkDeoptimizeOnUndefined() {
  HPhase phase("MarkDeoptimizeOnUndefined", this);
  // Compute DeoptimizeOnUndefined flag for phis.
  // Any phi that can reach a use with DeoptimizeOnUndefined set must
  // have DeoptimizeOnUndefined set.  Currently only HCompare, with
  // double input representation, has this flag set.
  // The flag is used by HChange tagged->double, which must deoptimize
  // if one of its uses has this flag set.
  for (int i = 0; i < phi_list()->length(); i++) {
    HPhi* phi = phi_list()->at(i);
    if (phi->representation().IsDouble()) {
      for (int j = 0; j < phi->uses()->length(); j++) {
        HValue* use = phi->uses()->at(j);
        if (use->CheckFlag(HValue::kDeoptimizeOnUndefined)) {
          RecursivelyMarkPhiDeoptimizeOnUndefined(phi);
          break;
        }
      }
    }
  }
}


void HGraph::ComputeMinusZeroChecks() {
  BitVector visited(GetMaximumValueID());
  for (int i = 0; i < blocks_.length(); ++i) {
    for (HInstruction* current = blocks_[i]->first();
         current != NULL;
         current = current->next()) {
      if (current->IsChange()) {
        HChange* change = HChange::cast(current);
        // Propagate flags for negative zero checks upwards from conversions
        // int32-to-tagged and int32-to-double.
        Representation from = change->value()->representation();
        ASSERT(from.Equals(change->from()));
        if (from.IsInteger32()) {
          ASSERT(change->to().IsTagged() || change->to().IsDouble());
          ASSERT(visited.IsEmpty());
          PropagateMinusZeroChecks(change->value(), &visited);
          visited.Clear();
        }
      }
    }
  }
}


// Implementation of utility class to encapsulate the translation state for
// a (possibly inlined) function.
FunctionState::FunctionState(HGraphBuilder* owner,
                             CompilationInfo* info,
                             TypeFeedbackOracle* oracle)
    : owner_(owner),
      compilation_info_(info),
      oracle_(oracle),
      call_context_(NULL),
      function_return_(NULL),
      test_context_(NULL),
      outer_(owner->function_state()) {
  if (outer_ != NULL) {
    // State for an inline function.
    if (owner->ast_context()->IsTest()) {
      HBasicBlock* if_true = owner->graph()->CreateBasicBlock();
      HBasicBlock* if_false = owner->graph()->CreateBasicBlock();
      if_true->MarkAsInlineReturnTarget();
      if_false->MarkAsInlineReturnTarget();
      // The AstContext constructor pushed on the context stack.  This newed
      // instance is the reason that AstContext can't be BASE_EMBEDDED.
      test_context_ = new TestContext(owner, if_true, if_false);
    } else {
      function_return_ = owner->graph()->CreateBasicBlock();
      function_return()->MarkAsInlineReturnTarget();
    }
    // Set this after possibly allocating a new TestContext above.
    call_context_ = owner->ast_context();
  }

  // Push on the state stack.
  owner->set_function_state(this);
}


FunctionState::~FunctionState() {
  delete test_context_;
  owner_->set_function_state(outer_);
}


// Implementation of utility classes to represent an expression's context in
// the AST.
AstContext::AstContext(HGraphBuilder* owner, Expression::Context kind)
    : owner_(owner),
      kind_(kind),
      outer_(owner->ast_context()),
      for_typeof_(false) {
  owner->set_ast_context(this);  // Push.
#ifdef DEBUG
  original_length_ = owner->environment()->length();
#endif
}


AstContext::~AstContext() {
  owner_->set_ast_context(outer_);  // Pop.
}


EffectContext::~EffectContext() {
  ASSERT(owner()->HasStackOverflow() ||
         owner()->current_block() == NULL ||
         owner()->environment()->length() == original_length_);
}


ValueContext::~ValueContext() {
  ASSERT(owner()->HasStackOverflow() ||
         owner()->current_block() == NULL ||
         owner()->environment()->length() == original_length_ + 1);
}


void EffectContext::ReturnValue(HValue* value) {
  // The value is simply ignored.
}


void ValueContext::ReturnValue(HValue* value) {
  // The value is tracked in the bailout environment, and communicated
  // through the environment as the result of the expression.
  owner()->Push(value);
}


void TestContext::ReturnValue(HValue* value) {
  BuildBranch(value);
}


void EffectContext::ReturnInstruction(HInstruction* instr, int ast_id) {
  owner()->AddInstruction(instr);
  if (instr->HasSideEffects()) owner()->AddSimulate(ast_id);
}


void ValueContext::ReturnInstruction(HInstruction* instr, int ast_id) {
  owner()->AddInstruction(instr);
  owner()->Push(instr);
  if (instr->HasSideEffects()) owner()->AddSimulate(ast_id);
}


void TestContext::ReturnInstruction(HInstruction* instr, int ast_id) {
  HGraphBuilder* builder = owner();
  builder->AddInstruction(instr);
  // We expect a simulate after every expression with side effects, though
  // this one isn't actually needed (and wouldn't work if it were targeted).
  if (instr->HasSideEffects()) {
    builder->Push(instr);
    builder->AddSimulate(ast_id);
    builder->Pop();
  }
  BuildBranch(instr);
}


void TestContext::BuildBranch(HValue* value) {
  // We expect the graph to be in edge-split form: there is no edge that
  // connects a branch node to a join node.  We conservatively ensure that
  // property by always adding an empty block on the outgoing edges of this
  // branch.
  HGraphBuilder* builder = owner();
  HBasicBlock* empty_true = builder->graph()->CreateBasicBlock();
  HBasicBlock* empty_false = builder->graph()->CreateBasicBlock();
  HTest* test = new(zone()) HTest(value, empty_true, empty_false);
  builder->current_block()->Finish(test);

  empty_true->Goto(if_true(), false);
  empty_false->Goto(if_false(), false);
  builder->set_current_block(NULL);
}


// HGraphBuilder infrastructure for bailing out and checking bailouts.
#define BAILOUT(reason)                         \
  do {                                          \
    Bailout(reason);                            \
    return;                                     \
  } while (false)


#define CHECK_BAILOUT                           \
  do {                                          \
    if (HasStackOverflow()) return;             \
  } while (false)


#define VISIT_FOR_EFFECT(expr)                  \
  do {                                          \
    VisitForEffect(expr);                       \
    if (HasStackOverflow()) return;             \
  } while (false)


#define VISIT_FOR_VALUE(expr)                   \
  do {                                          \
    VisitForValue(expr);                        \
    if (HasStackOverflow()) return;             \
  } while (false)


#define VISIT_FOR_CONTROL(expr, true_block, false_block)        \
  do {                                                          \
    VisitForControl(expr, true_block, false_block);             \
    if (HasStackOverflow()) return;                             \
  } while (false)


void HGraphBuilder::Bailout(const char* reason) {
  if (FLAG_trace_bailout) {
    SmartPointer<char> name(info()->shared_info()->DebugName()->ToCString());
    PrintF("Bailout in HGraphBuilder: @\"%s\": %s\n", *name, reason);
  }
  SetStackOverflow();
}


void HGraphBuilder::VisitForEffect(Expression* expr) {
  EffectContext for_effect(this);
  Visit(expr);
}


void HGraphBuilder::VisitForValue(Expression* expr) {
  ValueContext for_value(this);
  Visit(expr);
}


void HGraphBuilder::VisitForTypeOf(Expression* expr) {
  ValueContext for_value(this);
  for_value.set_for_typeof(true);
  Visit(expr);
}



void HGraphBuilder::VisitForControl(Expression* expr,
                                    HBasicBlock* true_block,
                                    HBasicBlock* false_block) {
  TestContext for_test(this, true_block, false_block);
  Visit(expr);
}


void HGraphBuilder::VisitArgument(Expression* expr) {
  VISIT_FOR_VALUE(expr);
  Push(AddInstruction(new(zone()) HPushArgument(Pop())));
}


void HGraphBuilder::VisitArgumentList(ZoneList<Expression*>* arguments) {
  for (int i = 0; i < arguments->length(); i++) {
    VisitArgument(arguments->at(i));
    if (HasStackOverflow() || current_block() == NULL) return;
  }
}


void HGraphBuilder::VisitExpressions(ZoneList<Expression*>* exprs) {
  for (int i = 0; i < exprs->length(); ++i) {
    VISIT_FOR_VALUE(exprs->at(i));
  }
}


HGraph* HGraphBuilder::CreateGraph() {
  graph_ = new(zone()) HGraph(info());
  if (FLAG_hydrogen_stats) HStatistics::Instance()->Initialize(info());

  {
    HPhase phase("Block building");
    current_block_ = graph()->entry_block();

    Scope* scope = info()->scope();
    if (scope->HasIllegalRedeclaration()) {
      Bailout("function with illegal redeclaration");
      return NULL;
    }
    SetupScope(scope);
    VisitDeclarations(scope->declarations());
    AddInstruction(new(zone()) HStackCheck());

    // Add an edge to the body entry.  This is warty: the graph's start
    // environment will be used by the Lithium translation as the initial
    // environment on graph entry, but it has now been mutated by the
    // Hydrogen translation of the instructions in the start block.  This
    // environment uses values which have not been defined yet.  These
    // Hydrogen instructions will then be replayed by the Lithium
    // translation, so they cannot have an environment effect.  The edge to
    // the body's entry block (along with some special logic for the start
    // block in HInstruction::InsertAfter) seals the start block from
    // getting unwanted instructions inserted.
    //
    // TODO(kmillikin): Fix this.  Stop mutating the initial environment.
    // Make the Hydrogen instructions in the initial block into Hydrogen
    // values (but not instructions), present in the initial environment and
    // not replayed by the Lithium translation.
    HEnvironment* initial_env = environment()->CopyWithoutHistory();
    HBasicBlock* body_entry = CreateBasicBlock(initial_env);
    current_block()->Goto(body_entry);
    body_entry->SetJoinId(AstNode::kFunctionEntryId);
    set_current_block(body_entry);
    VisitStatements(info()->function()->body());
    if (HasStackOverflow()) return NULL;

    if (current_block() != NULL) {
      HReturn* instr = new(zone()) HReturn(graph()->GetConstantUndefined());
      current_block()->FinishExit(instr);
      set_current_block(NULL);
    }
  }

  graph()->OrderBlocks();
  graph()->AssignDominators();
  graph()->EliminateRedundantPhis();
  if (FLAG_eliminate_dead_phis) graph()->EliminateUnreachablePhis();
  if (!graph()->CollectPhis()) {
    Bailout("Phi-use of arguments object");
    return NULL;
  }

  HInferRepresentation rep(graph());
  rep.Analyze();

  if (FLAG_use_range) {
    HRangeAnalysis rangeAnalysis(graph());
    rangeAnalysis.Analyze();
  }

  graph()->InitializeInferredTypes();
  graph()->Canonicalize();
  graph()->MarkDeoptimizeOnUndefined();
  graph()->InsertRepresentationChanges();
  graph()->ComputeMinusZeroChecks();

  // Eliminate redundant stack checks on backwards branches.
  HStackCheckEliminator sce(graph());
  sce.Process();

  // Perform common subexpression elimination and loop-invariant code motion.
  if (FLAG_use_gvn) {
    HPhase phase("Global value numbering", graph());
    HGlobalValueNumberer gvn(graph(), info());
    gvn.Analyze();
  }

  // Replace the results of check instructions with the original value, if the
  // result is used. This is safe now, since we don't do code motion after this
  // point. It enables better register allocation since the value produced by
  // check instructions is really a copy of the original value.
  graph()->ReplaceCheckedValues();

  return graph();
}


void HGraph::ReplaceCheckedValues() {
  HPhase phase("Replace checked values", this);
  for (int i = 0; i < blocks()->length(); ++i) {
    HInstruction* instr = blocks()->at(i)->first();
    while (instr != NULL) {
      if (instr->IsBoundsCheck()) {
        // Replace all uses of the checked value with the original input.
        ASSERT(instr->uses()->length() > 0);
        instr->ReplaceValue(HBoundsCheck::cast(instr)->index());
      }
      instr = instr->next();
    }
  }
}


HInstruction* HGraphBuilder::AddInstruction(HInstruction* instr) {
  ASSERT(current_block() != NULL);
  current_block()->AddInstruction(instr);
  return instr;
}


void HGraphBuilder::AddSimulate(int id) {
  ASSERT(current_block() != NULL);
  current_block()->AddSimulate(id);
}


void HGraphBuilder::AddPhi(HPhi* instr) {
  ASSERT(current_block() != NULL);
  current_block()->AddPhi(instr);
}


void HGraphBuilder::PushAndAdd(HInstruction* instr) {
  Push(instr);
  AddInstruction(instr);
}


template <int V>
HInstruction* HGraphBuilder::PreProcessCall(HCall<V>* call) {
  int count = call->argument_count();
  ZoneList<HValue*> arguments(count);
  for (int i = 0; i < count; ++i) {
    arguments.Add(Pop());
  }

  while (!arguments.is_empty()) {
    AddInstruction(new(zone()) HPushArgument(arguments.RemoveLast()));
  }
  return call;
}


void HGraphBuilder::SetupScope(Scope* scope) {
  // We don't yet handle the function name for named function expressions.
  if (scope->function() != NULL) BAILOUT("named function expression");

  HConstant* undefined_constant = new(zone()) HConstant(
      isolate()->factory()->undefined_value(), Representation::Tagged());
  AddInstruction(undefined_constant);
  graph_->set_undefined_constant(undefined_constant);

  // Set the initial values of parameters including "this".  "This" has
  // parameter index 0.
  int count = scope->num_parameters() + 1;
  for (int i = 0; i < count; ++i) {
    HInstruction* parameter = AddInstruction(new(zone()) HParameter(i));
    environment()->Bind(i, parameter);
  }

  // Set the initial values of stack-allocated locals.
  for (int i = count; i < environment()->length(); ++i) {
    environment()->Bind(i, undefined_constant);
  }

  // Handle the arguments and arguments shadow variables specially (they do
  // not have declarations).
  if (scope->arguments() != NULL) {
    if (!scope->arguments()->IsStackAllocated() ||
        (scope->arguments_shadow() != NULL &&
        !scope->arguments_shadow()->IsStackAllocated())) {
      BAILOUT("context-allocated arguments");
    }
    HArgumentsObject* object = new(zone()) HArgumentsObject;
    AddInstruction(object);
    graph()->SetArgumentsObject(object);
    environment()->Bind(scope->arguments(), object);
    if (scope->arguments_shadow() != NULL) {
      environment()->Bind(scope->arguments_shadow(), object);
    }
  }
}


void HGraphBuilder::VisitStatements(ZoneList<Statement*>* statements) {
  for (int i = 0; i < statements->length(); i++) {
    Visit(statements->at(i));
    if (HasStackOverflow() || current_block() == NULL) break;
  }
}


HBasicBlock* HGraphBuilder::CreateBasicBlock(HEnvironment* env) {
  HBasicBlock* b = graph()->CreateBasicBlock();
  b->SetInitialEnvironment(env);
  return b;
}


HBasicBlock* HGraphBuilder::CreateLoopHeaderBlock() {
  HBasicBlock* header = graph()->CreateBasicBlock();
  HEnvironment* entry_env = environment()->CopyAsLoopHeader(header);
  header->SetInitialEnvironment(entry_env);
  header->AttachLoopInformation();
  return header;
}


void HGraphBuilder::VisitBlock(Block* stmt) {
  BreakAndContinueInfo break_info(stmt);
  { BreakAndContinueScope push(&break_info, this);
    VisitStatements(stmt->statements());
    CHECK_BAILOUT;
  }
  HBasicBlock* break_block = break_info.break_block();
  if (break_block != NULL) {
    if (current_block() != NULL) current_block()->Goto(break_block);
    break_block->SetJoinId(stmt->ExitId());
    set_current_block(break_block);
  }
}


void HGraphBuilder::VisitExpressionStatement(ExpressionStatement* stmt) {
  VisitForEffect(stmt->expression());
}


void HGraphBuilder::VisitEmptyStatement(EmptyStatement* stmt) {
}


void HGraphBuilder::VisitIfStatement(IfStatement* stmt) {
  if (stmt->condition()->ToBooleanIsTrue()) {
    AddSimulate(stmt->ThenId());
    Visit(stmt->then_statement());
  } else if (stmt->condition()->ToBooleanIsFalse()) {
    AddSimulate(stmt->ElseId());
    Visit(stmt->else_statement());
  } else {
    HBasicBlock* cond_true = graph()->CreateBasicBlock();
    HBasicBlock* cond_false = graph()->CreateBasicBlock();
    VISIT_FOR_CONTROL(stmt->condition(), cond_true, cond_false);
    cond_true->SetJoinId(stmt->ThenId());
    cond_false->SetJoinId(stmt->ElseId());

    set_current_block(cond_true);
    Visit(stmt->then_statement());
    CHECK_BAILOUT;
    HBasicBlock* other = current_block();

    set_current_block(cond_false);
    Visit(stmt->else_statement());
    CHECK_BAILOUT;

    HBasicBlock* join = CreateJoin(other, current_block(), stmt->id());
    set_current_block(join);
  }
}


HBasicBlock* HGraphBuilder::BreakAndContinueScope::Get(
    BreakableStatement* stmt,
    BreakType type) {
  BreakAndContinueScope* current = this;
  while (current != NULL && current->info()->target() != stmt) {
    current = current->next();
  }
  ASSERT(current != NULL);  // Always found (unless stack is malformed).
  HBasicBlock* block = NULL;
  switch (type) {
    case BREAK:
      block = current->info()->break_block();
      if (block == NULL) {
        block = current->owner()->graph()->CreateBasicBlock();
        current->info()->set_break_block(block);
      }
      break;

    case CONTINUE:
      block = current->info()->continue_block();
      if (block == NULL) {
        block = current->owner()->graph()->CreateBasicBlock();
        current->info()->set_continue_block(block);
      }
      break;
  }

  return block;
}


void HGraphBuilder::VisitContinueStatement(ContinueStatement* stmt) {
  HBasicBlock* continue_block = break_scope()->Get(stmt->target(), CONTINUE);
  current_block()->Goto(continue_block);
  set_current_block(NULL);
}


void HGraphBuilder::VisitBreakStatement(BreakStatement* stmt) {
  HBasicBlock* break_block = break_scope()->Get(stmt->target(), BREAK);
  current_block()->Goto(break_block);
  set_current_block(NULL);
}


void HGraphBuilder::VisitReturnStatement(ReturnStatement* stmt) {
  AstContext* context = call_context();
  if (context == NULL) {
    // Not an inlined return, so an actual one.
    VISIT_FOR_VALUE(stmt->expression());
    HValue* result = environment()->Pop();
    current_block()->FinishExit(new(zone()) HReturn(result));
    set_current_block(NULL);
  } else {
    // Return from an inlined function, visit the subexpression in the
    // expression context of the call.
    if (context->IsTest()) {
      TestContext* test = TestContext::cast(context);
      VisitForControl(stmt->expression(),
                      test->if_true(),
                      test->if_false());
    } else if (context->IsEffect()) {
      VISIT_FOR_EFFECT(stmt->expression());
      current_block()->Goto(function_return(), false);
    } else {
      ASSERT(context->IsValue());
      VISIT_FOR_VALUE(stmt->expression());
      HValue* return_value = environment()->Pop();
      current_block()->AddLeaveInlined(return_value, function_return());
    }
    set_current_block(NULL);
  }
}


void HGraphBuilder::VisitWithEnterStatement(WithEnterStatement* stmt) {
  BAILOUT("WithEnterStatement");
}


void HGraphBuilder::VisitWithExitStatement(WithExitStatement* stmt) {
  BAILOUT("WithExitStatement");
}


void HGraphBuilder::VisitSwitchStatement(SwitchStatement* stmt) {
  // We only optimize switch statements with smi-literal smi comparisons,
  // with a bounded number of clauses.
  const int kCaseClauseLimit = 128;
  ZoneList<CaseClause*>* clauses = stmt->cases();
  int clause_count = clauses->length();
  if (clause_count > kCaseClauseLimit) {
    BAILOUT("SwitchStatement: too many clauses");
  }

  VISIT_FOR_VALUE(stmt->tag());
  AddSimulate(stmt->EntryId());
  HValue* tag_value = Pop();
  HBasicBlock* first_test_block = current_block();

  // 1. Build all the tests, with dangling true branches.  Unconditionally
  // deoptimize if we encounter a non-smi comparison.
  for (int i = 0; i < clause_count; ++i) {
    CaseClause* clause = clauses->at(i);
    if (clause->is_default()) continue;
    if (!clause->label()->IsSmiLiteral()) {
      BAILOUT("SwitchStatement: non-literal switch label");
    }

    // Unconditionally deoptimize on the first non-smi compare.
    clause->RecordTypeFeedback(oracle());
    if (!clause->IsSmiCompare()) {
      current_block()->FinishExitWithDeoptimization();
      set_current_block(NULL);
      break;
    }

    // Otherwise generate a compare and branch.
    VISIT_FOR_VALUE(clause->label());
    HValue* label_value = Pop();
    HCompare* compare =
        new(zone()) HCompare(tag_value, label_value, Token::EQ_STRICT);
    compare->SetInputRepresentation(Representation::Integer32());
    ASSERT(!compare->HasSideEffects());
    AddInstruction(compare);
    HBasicBlock* body_block = graph()->CreateBasicBlock();
    HBasicBlock* next_test_block = graph()->CreateBasicBlock();
    HTest* branch = new(zone()) HTest(compare, body_block, next_test_block);
    current_block()->Finish(branch);
    set_current_block(next_test_block);
  }

  // Save the current block to use for the default or to join with the
  // exit.  This block is NULL if we deoptimized.
  HBasicBlock* last_block = current_block();

  // 2. Loop over the clauses and the linked list of tests in lockstep,
  // translating the clause bodies.
  HBasicBlock* curr_test_block = first_test_block;
  HBasicBlock* fall_through_block = NULL;
  BreakAndContinueInfo break_info(stmt);
  { BreakAndContinueScope push(&break_info, this);
    for (int i = 0; i < clause_count; ++i) {
      CaseClause* clause = clauses->at(i);

      // Identify the block where normal (non-fall-through) control flow
      // goes to.
      HBasicBlock* normal_block = NULL;
      if (clause->is_default()) {
        if (last_block != NULL) {
          normal_block = last_block;
          last_block = NULL;  // Cleared to indicate we've handled it.
        }
      } else if (!curr_test_block->end()->IsDeoptimize()) {
        normal_block = curr_test_block->end()->FirstSuccessor();
        curr_test_block = curr_test_block->end()->SecondSuccessor();
      }

      // Identify a block to emit the body into.
      if (normal_block == NULL) {
        if (fall_through_block == NULL) {
          // (a) Unreachable.
          if (clause->is_default()) {
            continue;  // Might still be reachable clause bodies.
          } else {
            break;
          }
        } else {
          // (b) Reachable only as fall through.
          set_current_block(fall_through_block);
        }
      } else if (fall_through_block == NULL) {
        // (c) Reachable only normally.
        set_current_block(normal_block);
      } else {
        // (d) Reachable both ways.
        HBasicBlock* join = CreateJoin(fall_through_block,
                                       normal_block,
                                       clause->EntryId());
        set_current_block(join);
      }

      VisitStatements(clause->statements());
      CHECK_BAILOUT;
      fall_through_block = current_block();
    }
  }

  // Create an up-to-3-way join.  Use the break block if it exists since
  // it's already a join block.
  HBasicBlock* break_block = break_info.break_block();
  if (break_block == NULL) {
    set_current_block(CreateJoin(fall_through_block,
                                 last_block,
                                 stmt->ExitId()));
  } else {
    if (fall_through_block != NULL) fall_through_block->Goto(break_block);
    if (last_block != NULL) last_block->Goto(break_block);
    break_block->SetJoinId(stmt->ExitId());
    set_current_block(break_block);
  }
}


bool HGraphBuilder::HasOsrEntryAt(IterationStatement* statement) {
  return statement->OsrEntryId() == info()->osr_ast_id();
}


void HGraphBuilder::PreProcessOsrEntry(IterationStatement* statement) {
  if (!HasOsrEntryAt(statement)) return;

  HBasicBlock* non_osr_entry = graph()->CreateBasicBlock();
  HBasicBlock* osr_entry = graph()->CreateBasicBlock();
  HValue* true_value = graph()->GetConstantTrue();
  HTest* test = new(zone()) HTest(true_value, non_osr_entry, osr_entry);
  current_block()->Finish(test);

  HBasicBlock* loop_predecessor = graph()->CreateBasicBlock();
  non_osr_entry->Goto(loop_predecessor);

  set_current_block(osr_entry);
  int osr_entry_id = statement->OsrEntryId();
  // We want the correct environment at the OsrEntry instruction.  Build
  // it explicitly.  The expression stack should be empty.
  int count = environment()->length();
  ASSERT(count ==
         (environment()->parameter_count() + environment()->local_count()));
  for (int i = 0; i < count; ++i) {
    HUnknownOSRValue* unknown = new(zone()) HUnknownOSRValue;
    AddInstruction(unknown);
    environment()->Bind(i, unknown);
  }

  AddSimulate(osr_entry_id);
  AddInstruction(new(zone()) HOsrEntry(osr_entry_id));
  current_block()->Goto(loop_predecessor);
  loop_predecessor->SetJoinId(statement->EntryId());
  set_current_block(loop_predecessor);
}


void HGraphBuilder::VisitDoWhileStatement(DoWhileStatement* stmt) {
  ASSERT(current_block() != NULL);
  PreProcessOsrEntry(stmt);
  HBasicBlock* loop_entry = CreateLoopHeaderBlock();
  current_block()->Goto(loop_entry, false);
  set_current_block(loop_entry);

  BreakAndContinueInfo break_info(stmt);
  { BreakAndContinueScope push(&break_info, this);
    Visit(stmt->body());
    CHECK_BAILOUT;
  }
  HBasicBlock* body_exit =
      JoinContinue(stmt, current_block(), break_info.continue_block());
  HBasicBlock* loop_successor = NULL;
  if (body_exit != NULL && !stmt->cond()->ToBooleanIsTrue()) {
    set_current_block(body_exit);
    // The block for a true condition, the actual predecessor block of the
    // back edge.
    body_exit = graph()->CreateBasicBlock();
    loop_successor = graph()->CreateBasicBlock();
    VISIT_FOR_CONTROL(stmt->cond(), body_exit, loop_successor);
    body_exit->SetJoinId(stmt->BackEdgeId());
    loop_successor->SetJoinId(stmt->ExitId());
  }
  HBasicBlock* loop_exit = CreateLoop(stmt,
                                      loop_entry,
                                      body_exit,
                                      loop_successor,
                                      break_info.break_block());
  set_current_block(loop_exit);
}


void HGraphBuilder::VisitWhileStatement(WhileStatement* stmt) {
  ASSERT(current_block() != NULL);
  PreProcessOsrEntry(stmt);
  HBasicBlock* loop_entry = CreateLoopHeaderBlock();
  current_block()->Goto(loop_entry, false);
  set_current_block(loop_entry);

  // If the condition is constant true, do not generate a branch.
  HBasicBlock* loop_successor = NULL;
  if (!stmt->cond()->ToBooleanIsTrue()) {
    HBasicBlock* body_entry = graph()->CreateBasicBlock();
    loop_successor = graph()->CreateBasicBlock();
    VISIT_FOR_CONTROL(stmt->cond(), body_entry, loop_successor);
    body_entry->SetJoinId(stmt->BodyId());
    loop_successor->SetJoinId(stmt->ExitId());
    set_current_block(body_entry);
  }

  BreakAndContinueInfo break_info(stmt);
  { BreakAndContinueScope push(&break_info, this);
    Visit(stmt->body());
    CHECK_BAILOUT;
  }
  HBasicBlock* body_exit =
      JoinContinue(stmt, current_block(), break_info.continue_block());
  HBasicBlock* loop_exit = CreateLoop(stmt,
                                      loop_entry,
                                      body_exit,
                                      loop_successor,
                                      break_info.break_block());
  set_current_block(loop_exit);
}


void HGraphBuilder::VisitForStatement(ForStatement* stmt) {
  if (stmt->init() != NULL) {
    Visit(stmt->init());
    CHECK_BAILOUT;
  }
  ASSERT(current_block() != NULL);
  PreProcessOsrEntry(stmt);
  HBasicBlock* loop_entry = CreateLoopHeaderBlock();
  current_block()->Goto(loop_entry, false);
  set_current_block(loop_entry);

  HBasicBlock* loop_successor = NULL;
  if (stmt->cond() != NULL) {
    HBasicBlock* body_entry = graph()->CreateBasicBlock();
    loop_successor = graph()->CreateBasicBlock();
    VISIT_FOR_CONTROL(stmt->cond(), body_entry, loop_successor);
    body_entry->SetJoinId(stmt->BodyId());
    loop_successor->SetJoinId(stmt->ExitId());
    set_current_block(body_entry);
  }

  BreakAndContinueInfo break_info(stmt);
  { BreakAndContinueScope push(&break_info, this);
    Visit(stmt->body());
    CHECK_BAILOUT;
  }
  HBasicBlock* body_exit =
      JoinContinue(stmt, current_block(), break_info.continue_block());

  if (stmt->next() != NULL && body_exit != NULL) {
    set_current_block(body_exit);
    Visit(stmt->next());
    CHECK_BAILOUT;
    body_exit = current_block();
  }

  HBasicBlock* loop_exit = CreateLoop(stmt,
                                      loop_entry,
                                      body_exit,
                                      loop_successor,
                                      break_info.break_block());
  set_current_block(loop_exit);
}


void HGraphBuilder::VisitForInStatement(ForInStatement* stmt) {
  BAILOUT("ForInStatement");
}


void HGraphBuilder::VisitTryCatchStatement(TryCatchStatement* stmt) {
  BAILOUT("TryCatchStatement");
}


void HGraphBuilder::VisitTryFinallyStatement(TryFinallyStatement* stmt) {
  BAILOUT("TryFinallyStatement");
}


void HGraphBuilder::VisitDebuggerStatement(DebuggerStatement* stmt) {
  BAILOUT("DebuggerStatement");
}


static Handle<SharedFunctionInfo> SearchSharedFunctionInfo(
    Code* unoptimized_code, FunctionLiteral* expr) {
  int start_position = expr->start_position();
  RelocIterator it(unoptimized_code);
  for (;!it.done(); it.next()) {
    RelocInfo* rinfo = it.rinfo();
    if (rinfo->rmode() != RelocInfo::EMBEDDED_OBJECT) continue;
    Object* obj = rinfo->target_object();
    if (obj->IsSharedFunctionInfo()) {
      SharedFunctionInfo* shared = SharedFunctionInfo::cast(obj);
      if (shared->start_position() == start_position) {
        return Handle<SharedFunctionInfo>(shared);
      }
    }
  }

  return Handle<SharedFunctionInfo>();
}


void HGraphBuilder::VisitFunctionLiteral(FunctionLiteral* expr) {
  Handle<SharedFunctionInfo> shared_info =
      SearchSharedFunctionInfo(info()->shared_info()->code(),
                               expr);
  if (shared_info.is_null()) {
    shared_info = Compiler::BuildFunctionInfo(expr, info()->script());
  }
  CHECK_BAILOUT;
  HFunctionLiteral* instr =
      new(zone()) HFunctionLiteral(shared_info, expr->pretenure());
  ast_context()->ReturnInstruction(instr, expr->id());
}


void HGraphBuilder::VisitSharedFunctionInfoLiteral(
    SharedFunctionInfoLiteral* expr) {
  BAILOUT("SharedFunctionInfoLiteral");
}


void HGraphBuilder::VisitConditional(Conditional* expr) {
  HBasicBlock* cond_true = graph()->CreateBasicBlock();
  HBasicBlock* cond_false = graph()->CreateBasicBlock();
  VISIT_FOR_CONTROL(expr->condition(), cond_true, cond_false);
  cond_true->SetJoinId(expr->ThenId());
  cond_false->SetJoinId(expr->ElseId());

  // Visit the true and false subexpressions in the same AST context as the
  // whole expression.
  set_current_block(cond_true);
  Visit(expr->then_expression());
  CHECK_BAILOUT;
  HBasicBlock* other = current_block();

  set_current_block(cond_false);
  Visit(expr->else_expression());
  CHECK_BAILOUT;

  if (!ast_context()->IsTest()) {
    HBasicBlock* join = CreateJoin(other, current_block(), expr->id());
    set_current_block(join);
    if (!ast_context()->IsEffect()) ast_context()->ReturnValue(Pop());
  }
}


HGraphBuilder::GlobalPropertyAccess HGraphBuilder::LookupGlobalProperty(
    Variable* var, LookupResult* lookup, bool is_store) {
  if (var->is_this() || !info()->has_global_object()) {
    return kUseGeneric;
  }
  Handle<GlobalObject> global(info()->global_object());
  global->Lookup(*var->name(), lookup);
  if (!lookup->IsProperty() ||
      lookup->type() != NORMAL ||
      (is_store && lookup->IsReadOnly()) ||
      lookup->holder() != *global) {
    return kUseGeneric;
  }

  return kUseCell;
}


HValue* HGraphBuilder::BuildContextChainWalk(Variable* var) {
  ASSERT(var->IsContextSlot());
  HInstruction* context = new(zone()) HContext;
  AddInstruction(context);
  int length = info()->scope()->ContextChainLength(var->scope());
  while (length-- > 0) {
    context = new(zone()) HOuterContext(context);
    AddInstruction(context);
  }
  return context;
}


void HGraphBuilder::VisitVariableProxy(VariableProxy* expr) {
  Variable* variable = expr->AsVariable();
  if (variable == NULL) {
    BAILOUT("reference to rewritten variable");
  } else if (variable->IsStackAllocated()) {
    if (environment()->Lookup(variable)->CheckFlag(HValue::kIsArguments)) {
      BAILOUT("unsupported context for arguments object");
    }
    ast_context()->ReturnValue(environment()->Lookup(variable));
  } else if (variable->IsContextSlot()) {
    if (variable->mode() == Variable::CONST) {
      BAILOUT("reference to const context slot");
    }
    HValue* context = BuildContextChainWalk(variable);
    int index = variable->AsSlot()->index();
    HLoadContextSlot* instr = new(zone()) HLoadContextSlot(context, index);
    ast_context()->ReturnInstruction(instr, expr->id());
  } else if (variable->is_global()) {
    LookupResult lookup;
    GlobalPropertyAccess type = LookupGlobalProperty(variable, &lookup, false);

    if (type == kUseCell &&
        info()->global_object()->IsAccessCheckNeeded()) {
      type = kUseGeneric;
    }

    if (type == kUseCell) {
      Handle<GlobalObject> global(info()->global_object());
      Handle<JSGlobalPropertyCell> cell(global->GetPropertyCell(&lookup));
      bool check_hole = !lookup.IsDontDelete() || lookup.IsReadOnly();
      HLoadGlobalCell* instr = new(zone()) HLoadGlobalCell(cell, check_hole);
      ast_context()->ReturnInstruction(instr, expr->id());
    } else {
      HContext* context = new(zone()) HContext;
      AddInstruction(context);
      HGlobalObject* global_object = new(zone()) HGlobalObject(context);
      AddInstruction(global_object);
      HLoadGlobalGeneric* instr =
          new(zone()) HLoadGlobalGeneric(context,
                                         global_object,
                                         variable->name(),
                                         ast_context()->is_for_typeof());
      instr->set_position(expr->position());
      ASSERT(instr->HasSideEffects());
      ast_context()->ReturnInstruction(instr, expr->id());
    }
  } else {
    BAILOUT("reference to a variable which requires dynamic lookup");
  }
}


void HGraphBuilder::VisitLiteral(Literal* expr) {
  HConstant* instr =
      new(zone()) HConstant(expr->handle(), Representation::Tagged());
  ast_context()->ReturnInstruction(instr, expr->id());
}


void HGraphBuilder::VisitRegExpLiteral(RegExpLiteral* expr) {
  HRegExpLiteral* instr = new(zone()) HRegExpLiteral(expr->pattern(),
                                                     expr->flags(),
                                                     expr->literal_index());
  ast_context()->ReturnInstruction(instr, expr->id());
}


void HGraphBuilder::VisitObjectLiteral(ObjectLiteral* expr) {
  HContext* context = new(zone()) HContext;
  AddInstruction(context);
  HObjectLiteral* literal =
      new(zone()) HObjectLiteral(context,
                                 expr->constant_properties(),
                                 expr->fast_elements(),
                                 expr->literal_index(),
                                 expr->depth(),
                                 expr->has_function());
  // The object is expected in the bailout environment during computation
  // of the property values and is the value of the entire expression.
  PushAndAdd(literal);

  expr->CalculateEmitStore();

  for (int i = 0; i < expr->properties()->length(); i++) {
    ObjectLiteral::Property* property = expr->properties()->at(i);
    if (property->IsCompileTimeValue()) continue;

    Literal* key = property->key();
    Expression* value = property->value();

    switch (property->kind()) {
      case ObjectLiteral::Property::MATERIALIZED_LITERAL:
        ASSERT(!CompileTimeValue::IsCompileTimeValue(value));
        // Fall through.
      case ObjectLiteral::Property::COMPUTED:
        if (key->handle()->IsSymbol()) {
          if (property->emit_store()) {
            VISIT_FOR_VALUE(value);
            HValue* value = Pop();
            Handle<String> name = Handle<String>::cast(key->handle());
            HStoreNamedGeneric* store =
                new(zone()) HStoreNamedGeneric(
                                context,
                                literal,
                                name,
                                value,
                                function_strict_mode());
            AddInstruction(store);
            AddSimulate(key->id());
          } else {
            VISIT_FOR_EFFECT(value);
          }
          break;
        }
        // Fall through.
      case ObjectLiteral::Property::PROTOTYPE:
      case ObjectLiteral::Property::SETTER:
      case ObjectLiteral::Property::GETTER:
        BAILOUT("Object literal with complex property");
      default: UNREACHABLE();
    }
  }

  if (expr->has_function()) {
    // Return the result of the transformation to fast properties
    // instead of the original since this operation changes the map
    // of the object. This makes sure that the original object won't
    // be used by other optimized code before it is transformed
    // (e.g. because of code motion).
    HToFastProperties* result = new(zone()) HToFastProperties(Pop());
    AddInstruction(result);
    ast_context()->ReturnValue(result);
  } else {
    ast_context()->ReturnValue(Pop());
  }
}


void HGraphBuilder::VisitArrayLiteral(ArrayLiteral* expr) {
  ZoneList<Expression*>* subexprs = expr->values();
  int length = subexprs->length();

  HArrayLiteral* literal = new(zone()) HArrayLiteral(expr->constant_elements(),
                                                     length,
                                                     expr->literal_index(),
                                                     expr->depth());
  // The array is expected in the bailout environment during computation
  // of the property values and is the value of the entire expression.
  PushAndAdd(literal);

  HLoadElements* elements = NULL;

  for (int i = 0; i < length; i++) {
    Expression* subexpr = subexprs->at(i);
    // If the subexpression is a literal or a simple materialized literal it
    // is already set in the cloned array.
    if (CompileTimeValue::IsCompileTimeValue(subexpr)) continue;

    VISIT_FOR_VALUE(subexpr);
    HValue* value = Pop();
    if (!Smi::IsValid(i)) BAILOUT("Non-smi key in array literal");

    // Load the elements array before the first store.
    if (elements == NULL)  {
     elements = new(zone()) HLoadElements(literal);
     AddInstruction(elements);
    }

    HValue* key = AddInstruction(
        new(zone()) HConstant(Handle<Object>(Smi::FromInt(i)),
                              Representation::Integer32()));
    AddInstruction(new(zone()) HStoreKeyedFastElement(elements, key, value));
    AddSimulate(expr->GetIdForElement(i));
  }
  ast_context()->ReturnValue(Pop());
}


void HGraphBuilder::VisitCatchExtensionObject(CatchExtensionObject* expr) {
  BAILOUT("CatchExtensionObject");
}


// Sets the lookup result and returns true if the store can be inlined.
static bool ComputeStoredField(Handle<Map> type,
                               Handle<String> name,
                               LookupResult* lookup) {
  type->LookupInDescriptors(NULL, *name, lookup);
  if (!lookup->IsPropertyOrTransition()) return false;
  if (lookup->type() == FIELD) return true;
  return (lookup->type() == MAP_TRANSITION) &&
      (type->unused_property_fields() > 0);
}


static int ComputeStoredFieldIndex(Handle<Map> type,
                                   Handle<String> name,
                                   LookupResult* lookup) {
  ASSERT(lookup->type() == FIELD || lookup->type() == MAP_TRANSITION);
  if (lookup->type() == FIELD) {
    return lookup->GetLocalFieldIndexFromMap(*type);
  } else {
    Map* transition = lookup->GetTransitionMapFromMap(*type);
    return transition->PropertyIndexFor(*name) - type->inobject_properties();
  }
}


HInstruction* HGraphBuilder::BuildStoreNamedField(HValue* object,
                                                  Handle<String> name,
                                                  HValue* value,
                                                  Handle<Map> type,
                                                  LookupResult* lookup,
                                                  bool smi_and_map_check) {
  if (smi_and_map_check) {
    AddInstruction(new(zone()) HCheckNonSmi(object));
    AddInstruction(new(zone()) HCheckMap(object, type));
  }

  int index = ComputeStoredFieldIndex(type, name, lookup);
  bool is_in_object = index < 0;
  int offset = index * kPointerSize;
  if (index < 0) {
    // Negative property indices are in-object properties, indexed
    // from the end of the fixed part of the object.
    offset += type->instance_size();
  } else {
    offset += FixedArray::kHeaderSize;
  }
  HStoreNamedField* instr =
      new(zone()) HStoreNamedField(object, name, value, is_in_object, offset);
  if (lookup->type() == MAP_TRANSITION) {
    Handle<Map> transition(lookup->GetTransitionMapFromMap(*type));
    instr->set_transition(transition);
    // TODO(fschneider): Record the new map type of the object in the IR to
    // enable elimination of redundant checks after the transition store.
    instr->SetFlag(HValue::kChangesMaps);
  }
  return instr;
}


HInstruction* HGraphBuilder::BuildStoreNamedGeneric(HValue* object,
                                                    Handle<String> name,
                                                    HValue* value) {
  HContext* context = new(zone()) HContext;
  AddInstruction(context);
  return new(zone()) HStoreNamedGeneric(
                         context,
                         object,
                         name,
                         value,
                         function_strict_mode());
}


HInstruction* HGraphBuilder::BuildStoreNamed(HValue* object,
                                             HValue* value,
                                             Expression* expr) {
  Property* prop = (expr->AsProperty() != NULL)
      ? expr->AsProperty()
      : expr->AsAssignment()->target()->AsProperty();
  Literal* key = prop->key()->AsLiteral();
  Handle<String> name = Handle<String>::cast(key->handle());
  ASSERT(!name.is_null());

  LookupResult lookup;
  ZoneMapList* types = expr->GetReceiverTypes();
  bool is_monomorphic = expr->IsMonomorphic() &&
      ComputeStoredField(types->first(), name, &lookup);

  return is_monomorphic
      ? BuildStoreNamedField(object, name, value, types->first(), &lookup,
                             true)  // Needs smi and map check.
      : BuildStoreNamedGeneric(object, name, value);
}


void HGraphBuilder::HandlePolymorphicStoreNamedField(Assignment* expr,
                                                     HValue* object,
                                                     HValue* value,
                                                     ZoneMapList* types,
                                                     Handle<String> name) {
  // TODO(ager): We should recognize when the prototype chains for different
  // maps are identical. In that case we can avoid repeatedly generating the
  // same prototype map checks.
  int count = 0;
  HBasicBlock* join = NULL;
  for (int i = 0; i < types->length() && count < kMaxStorePolymorphism; ++i) {
    Handle<Map> map = types->at(i);
    LookupResult lookup;
    if (ComputeStoredField(map, name, &lookup)) {
      if (count == 0) {
        AddInstruction(new(zone()) HCheckNonSmi(object));  // Only needed once.
        join = graph()->CreateBasicBlock();
      }
      ++count;
      HBasicBlock* if_true = graph()->CreateBasicBlock();
      HBasicBlock* if_false = graph()->CreateBasicBlock();
      HCompareMap* compare =
          new(zone()) HCompareMap(object, map, if_true, if_false);
      current_block()->Finish(compare);

      set_current_block(if_true);
      HInstruction* instr =
          BuildStoreNamedField(object, name, value, map, &lookup, false);
      instr->set_position(expr->position());
      // Goto will add the HSimulate for the store.
      AddInstruction(instr);
      if (!ast_context()->IsEffect()) Push(value);
      current_block()->Goto(join);

      set_current_block(if_false);
    }
  }

  // Finish up.  Unconditionally deoptimize if we've handled all the maps we
  // know about and do not want to handle ones we've never seen.  Otherwise
  // use a generic IC.
  if (count == types->length() && FLAG_deoptimize_uncommon_cases) {
    current_block()->FinishExitWithDeoptimization();
  } else {
    HInstruction* instr = BuildStoreNamedGeneric(object, name, value);
    instr->set_position(expr->position());
    AddInstruction(instr);

    if (join != NULL) {
      if (!ast_context()->IsEffect()) Push(value);
      current_block()->Goto(join);
    } else {
      // The HSimulate for the store should not see the stored value in
      // effect contexts (it is not materialized at expr->id() in the
      // unoptimized code).
      if (instr->HasSideEffects()) {
        if (ast_context()->IsEffect()) {
          AddSimulate(expr->id());
        } else {
          Push(value);
          AddSimulate(expr->id());
          Drop(1);
        }
      }
      ast_context()->ReturnValue(value);
      return;
    }
  }

  ASSERT(join != NULL);
  join->SetJoinId(expr->id());
  set_current_block(join);
  if (!ast_context()->IsEffect()) ast_context()->ReturnValue(Pop());
}


void HGraphBuilder::HandlePropertyAssignment(Assignment* expr) {
  Property* prop = expr->target()->AsProperty();
  ASSERT(prop != NULL);
  expr->RecordTypeFeedback(oracle());
  VISIT_FOR_VALUE(prop->obj());

  HValue* value = NULL;
  HInstruction* instr = NULL;

  if (prop->key()->IsPropertyName()) {
    // Named store.
    VISIT_FOR_VALUE(expr->value());
    value = Pop();
    HValue* object = Pop();

    Literal* key = prop->key()->AsLiteral();
    Handle<String> name = Handle<String>::cast(key->handle());
    ASSERT(!name.is_null());

    ZoneMapList* types = expr->GetReceiverTypes();
    LookupResult lookup;

    if (expr->IsMonomorphic()) {
      instr = BuildStoreNamed(object, value, expr);

    } else if (types != NULL && types->length() > 1) {
      HandlePolymorphicStoreNamedField(expr, object, value, types, name);
      return;

    } else {
      instr = BuildStoreNamedGeneric(object, name, value);
    }

  } else {
    // Keyed store.
    VISIT_FOR_VALUE(prop->key());
    VISIT_FOR_VALUE(expr->value());
    value = Pop();
    HValue* key = Pop();
    HValue* object = Pop();
    instr = BuildStoreKeyed(object, key, value, expr);
  }
  Push(value);
  instr->set_position(expr->position());
  AddInstruction(instr);
  if (instr->HasSideEffects()) AddSimulate(expr->AssignmentId());
  ast_context()->ReturnValue(Pop());
}


// Because not every expression has a position and there is not common
// superclass of Assignment and CountOperation, we cannot just pass the
// owning expression instead of position and ast_id separately.
void HGraphBuilder::HandleGlobalVariableAssignment(Variable* var,
                                                   HValue* value,
                                                   int position,
                                                   int ast_id) {
  LookupResult lookup;
  GlobalPropertyAccess type = LookupGlobalProperty(var, &lookup, true);
  if (type == kUseCell) {
    bool check_hole = !lookup.IsDontDelete() || lookup.IsReadOnly();
    Handle<GlobalObject> global(info()->global_object());
    Handle<JSGlobalPropertyCell> cell(global->GetPropertyCell(&lookup));
    HInstruction* instr = new(zone()) HStoreGlobalCell(value, cell, check_hole);
    instr->set_position(position);
    AddInstruction(instr);
    if (instr->HasSideEffects()) AddSimulate(ast_id);
  } else {
    HContext* context = new(zone()) HContext;
    AddInstruction(context);
    HGlobalObject* global_object = new(zone()) HGlobalObject(context);
    AddInstruction(global_object);
    HStoreGlobalGeneric* instr =
        new(zone()) HStoreGlobalGeneric(context,
                                        global_object,
                                        var->name(),
                                        value,
                                        function_strict_mode());
    instr->set_position(position);
    AddInstruction(instr);
    ASSERT(instr->HasSideEffects());
    if (instr->HasSideEffects()) AddSimulate(ast_id);
  }
}


void HGraphBuilder::HandleCompoundAssignment(Assignment* expr) {
  Expression* target = expr->target();
  VariableProxy* proxy = target->AsVariableProxy();
  Variable* var = proxy->AsVariable();
  Property* prop = target->AsProperty();
  ASSERT(var == NULL || prop == NULL);

  // We have a second position recorded in the FullCodeGenerator to have
  // type feedback for the binary operation.
  BinaryOperation* operation = expr->binary_operation();

  if (var != NULL) {
    VISIT_FOR_VALUE(operation);

    if (var->is_global()) {
      HandleGlobalVariableAssignment(var,
                                     Top(),
                                     expr->position(),
                                     expr->AssignmentId());
    } else if (var->IsStackAllocated()) {
      Bind(var, Top());
    } else if (var->IsContextSlot()) {
      HValue* context = BuildContextChainWalk(var);
      int index = var->AsSlot()->index();
      HStoreContextSlot* instr =
          new(zone()) HStoreContextSlot(context, index, Top());
      AddInstruction(instr);
      if (instr->HasSideEffects()) AddSimulate(expr->AssignmentId());
    } else {
      BAILOUT("compound assignment to lookup slot");
    }
    ast_context()->ReturnValue(Pop());

  } else if (prop != NULL) {
    prop->RecordTypeFeedback(oracle());

    if (prop->key()->IsPropertyName()) {
      // Named property.
      VISIT_FOR_VALUE(prop->obj());
      HValue* obj = Top();

      HInstruction* load = NULL;
      if (prop->IsMonomorphic()) {
        Handle<String> name = prop->key()->AsLiteral()->AsPropertyName();
        Handle<Map> map = prop->GetReceiverTypes()->first();
        load = BuildLoadNamed(obj, prop, map, name);
      } else {
        load = BuildLoadNamedGeneric(obj, prop);
      }
      PushAndAdd(load);
      if (load->HasSideEffects()) AddSimulate(expr->CompoundLoadId());

      VISIT_FOR_VALUE(expr->value());
      HValue* right = Pop();
      HValue* left = Pop();

      HInstruction* instr = BuildBinaryOperation(operation, left, right);
      PushAndAdd(instr);
      if (instr->HasSideEffects()) AddSimulate(operation->id());

      HInstruction* store = BuildStoreNamed(obj, instr, prop);
      AddInstruction(store);
      // Drop the simulated receiver and value.  Return the value.
      Drop(2);
      Push(instr);
      if (store->HasSideEffects()) AddSimulate(expr->AssignmentId());
      ast_context()->ReturnValue(Pop());

    } else {
      // Keyed property.
      VISIT_FOR_VALUE(prop->obj());
      VISIT_FOR_VALUE(prop->key());
      HValue* obj = environment()->ExpressionStackAt(1);
      HValue* key = environment()->ExpressionStackAt(0);

      HInstruction* load = BuildLoadKeyed(obj, key, prop);
      PushAndAdd(load);
      if (load->HasSideEffects()) AddSimulate(expr->CompoundLoadId());

      VISIT_FOR_VALUE(expr->value());
      HValue* right = Pop();
      HValue* left = Pop();

      HInstruction* instr = BuildBinaryOperation(operation, left, right);
      PushAndAdd(instr);
      if (instr->HasSideEffects()) AddSimulate(operation->id());

      expr->RecordTypeFeedback(oracle());
      HInstruction* store = BuildStoreKeyed(obj, key, instr, expr);
      AddInstruction(store);
      // Drop the simulated receiver, key, and value.  Return the value.
      Drop(3);
      Push(instr);
      if (store->HasSideEffects()) AddSimulate(expr->AssignmentId());
      ast_context()->ReturnValue(Pop());
    }

  } else {
    BAILOUT("invalid lhs in compound assignment");
  }
}


void HGraphBuilder::VisitAssignment(Assignment* expr) {
  VariableProxy* proxy = expr->target()->AsVariableProxy();
  Variable* var = proxy->AsVariable();
  Property* prop = expr->target()->AsProperty();
  ASSERT(var == NULL || prop == NULL);

  if (expr->is_compound()) {
    HandleCompoundAssignment(expr);
    return;
  }

  if (var != NULL) {
    if (proxy->IsArguments()) BAILOUT("assignment to arguments");

    // Handle the assignment.
    if (var->IsStackAllocated()) {
      HValue* value = NULL;
      // Handle stack-allocated variables on the right-hand side directly.
      // We do not allow the arguments object to occur in a context where it
      // may escape, but assignments to stack-allocated locals are
      // permitted.  Handling such assignments here bypasses the check for
      // the arguments object in VisitVariableProxy.
      Variable* rhs_var = expr->value()->AsVariableProxy()->AsVariable();
      if (rhs_var != NULL && rhs_var->IsStackAllocated()) {
        value = environment()->Lookup(rhs_var);
      } else {
        VISIT_FOR_VALUE(expr->value());
        value = Pop();
      }
      Bind(var, value);
      ast_context()->ReturnValue(value);

    } else if (var->IsContextSlot() && var->mode() != Variable::CONST) {
      VISIT_FOR_VALUE(expr->value());
      HValue* context = BuildContextChainWalk(var);
      int index = var->AsSlot()->index();
      HStoreContextSlot* instr =
          new(zone()) HStoreContextSlot(context, index, Top());
      AddInstruction(instr);
      if (instr->HasSideEffects()) AddSimulate(expr->AssignmentId());
      ast_context()->ReturnValue(Pop());

    } else if (var->is_global()) {
      VISIT_FOR_VALUE(expr->value());
      HandleGlobalVariableAssignment(var,
                                     Top(),
                                     expr->position(),
                                     expr->AssignmentId());
      ast_context()->ReturnValue(Pop());

    } else {
      BAILOUT("assignment to LOOKUP or const CONTEXT variable");
    }

  } else if (prop != NULL) {
    HandlePropertyAssignment(expr);
  } else {
    BAILOUT("invalid left-hand side in assignment");
  }
}


void HGraphBuilder::VisitThrow(Throw* expr) {
  // We don't optimize functions with invalid left-hand sides in
  // assignments, count operations, or for-in.  Consequently throw can
  // currently only occur in an effect context.
  ASSERT(ast_context()->IsEffect());
  VISIT_FOR_VALUE(expr->exception());

  HValue* value = environment()->Pop();
  HThrow* instr = new(zone()) HThrow(value);
  instr->set_position(expr->position());
  AddInstruction(instr);
  AddSimulate(expr->id());
  current_block()->FinishExit(new(zone()) HAbnormalExit);
  set_current_block(NULL);
}


HLoadNamedField* HGraphBuilder::BuildLoadNamedField(HValue* object,
                                                    Property* expr,
                                                    Handle<Map> type,
                                                    LookupResult* lookup,
                                                    bool smi_and_map_check) {
  if (smi_and_map_check) {
    AddInstruction(new(zone()) HCheckNonSmi(object));
    AddInstruction(new(zone()) HCheckMap(object, type));
  }

  int index = lookup->GetLocalFieldIndexFromMap(*type);
  if (index < 0) {
    // Negative property indices are in-object properties, indexed
    // from the end of the fixed part of the object.
    int offset = (index * kPointerSize) + type->instance_size();
    return new(zone()) HLoadNamedField(object, true, offset);
  } else {
    // Non-negative property indices are in the properties array.
    int offset = (index * kPointerSize) + FixedArray::kHeaderSize;
    return new(zone()) HLoadNamedField(object, false, offset);
  }
}


HInstruction* HGraphBuilder::BuildLoadNamedGeneric(HValue* obj,
                                                   Property* expr) {
  ASSERT(expr->key()->IsPropertyName());
  Handle<Object> name = expr->key()->AsLiteral()->handle();
  HContext* context = new(zone()) HContext;
  AddInstruction(context);
  return new(zone()) HLoadNamedGeneric(context, obj, name);
}


HInstruction* HGraphBuilder::BuildLoadNamed(HValue* obj,
                                            Property* expr,
                                            Handle<Map> map,
                                            Handle<String> name) {
  LookupResult lookup;
  map->LookupInDescriptors(NULL, *name, &lookup);
  if (lookup.IsProperty() && lookup.type() == FIELD) {
    return BuildLoadNamedField(obj,
                               expr,
                               map,
                               &lookup,
                               true);
  } else if (lookup.IsProperty() && lookup.type() == CONSTANT_FUNCTION) {
    AddInstruction(new(zone()) HCheckNonSmi(obj));
    AddInstruction(new(zone()) HCheckMap(obj, map));
    Handle<JSFunction> function(lookup.GetConstantFunctionFromMap(*map));
    return new(zone()) HConstant(function, Representation::Tagged());
  } else {
    return BuildLoadNamedGeneric(obj, expr);
  }
}


HInstruction* HGraphBuilder::BuildLoadKeyedGeneric(HValue* object,
                                                   HValue* key) {
  HContext* context = new(zone()) HContext;
  AddInstruction(context);
  return new(zone()) HLoadKeyedGeneric(context, object, key);
}


HInstruction* HGraphBuilder::BuildLoadKeyedFastElement(HValue* object,
                                                       HValue* key,
                                                       Property* expr) {
  ASSERT(!expr->key()->IsPropertyName() && expr->IsMonomorphic());
  AddInstruction(new(zone()) HCheckNonSmi(object));
  Handle<Map> map = expr->GetMonomorphicReceiverType();
  ASSERT(map->has_fast_elements());
  AddInstruction(new(zone()) HCheckMap(object, map));
  bool is_array = (map->instance_type() == JS_ARRAY_TYPE);
  HLoadElements* elements = new(zone()) HLoadElements(object);
  HInstruction* length = NULL;
  HInstruction* checked_key = NULL;
  if (is_array) {
    length = AddInstruction(new(zone()) HJSArrayLength(object));
    checked_key = AddInstruction(new(zone()) HBoundsCheck(key, length));
    AddInstruction(elements);
  } else {
    AddInstruction(elements);
    length = AddInstruction(new(zone()) HFixedArrayLength(elements));
    checked_key = AddInstruction(new(zone()) HBoundsCheck(key, length));
  }
  return new(zone()) HLoadKeyedFastElement(elements, checked_key);
}


HInstruction* HGraphBuilder::BuildLoadKeyedSpecializedArrayElement(
    HValue* object,
    HValue* key,
    Property* expr) {
  ASSERT(!expr->key()->IsPropertyName() && expr->IsMonomorphic());
  AddInstruction(new(zone()) HCheckNonSmi(object));
  Handle<Map> map = expr->GetMonomorphicReceiverType();
  ASSERT(!map->has_fast_elements());
  ASSERT(map->has_external_array_elements());
  AddInstruction(new(zone()) HCheckMap(object, map));
  HLoadElements* elements = new(zone()) HLoadElements(object);
  AddInstruction(elements);
  HInstruction* length = new(zone()) HExternalArrayLength(elements);
  AddInstruction(length);
  HInstruction* checked_key =
      AddInstruction(new(zone()) HBoundsCheck(key, length));
  HLoadExternalArrayPointer* external_elements =
      new(zone()) HLoadExternalArrayPointer(elements);
  AddInstruction(external_elements);
  HLoadKeyedSpecializedArrayElement* pixel_array_value =
      new(zone()) HLoadKeyedSpecializedArrayElement(
          external_elements, checked_key, expr->external_array_type());
  return pixel_array_value;
}


HInstruction* HGraphBuilder::BuildLoadKeyed(HValue* obj,
                                            HValue* key,
                                            Property* prop) {
  if (prop->IsMonomorphic()) {
    Handle<Map> receiver_type(prop->GetMonomorphicReceiverType());
    // An object has either fast elements or pixel array elements, but never
    // both. Pixel array maps that are assigned to pixel array elements are
    // always created with the fast elements flag cleared.
    if (receiver_type->has_external_array_elements()) {
      return BuildLoadKeyedSpecializedArrayElement(obj, key, prop);
    } else if (receiver_type->has_fast_elements()) {
      return BuildLoadKeyedFastElement(obj, key, prop);
    }
  }
  return BuildLoadKeyedGeneric(obj, key);
}


HInstruction* HGraphBuilder::BuildStoreKeyedGeneric(HValue* object,
                                                    HValue* key,
                                                    HValue* value) {
  HContext* context = new(zone()) HContext;
  AddInstruction(context);
  return new(zone()) HStoreKeyedGeneric(
                         context,
                         object,
                         key,
                         value,
                         function_strict_mode());
}


HInstruction* HGraphBuilder::BuildStoreKeyedFastElement(HValue* object,
                                                        HValue* key,
                                                        HValue* val,
                                                        Expression* expr) {
  ASSERT(expr->IsMonomorphic());
  AddInstruction(new(zone()) HCheckNonSmi(object));
  Handle<Map> map = expr->GetMonomorphicReceiverType();
  ASSERT(map->has_fast_elements());
  AddInstruction(new(zone()) HCheckMap(object, map));
  HInstruction* elements = AddInstruction(new(zone()) HLoadElements(object));
  AddInstruction(new(zone()) HCheckMap(
      elements, isolate()->factory()->fixed_array_map()));
  bool is_array = (map->instance_type() == JS_ARRAY_TYPE);
  HInstruction* length = NULL;
  if (is_array) {
    length = AddInstruction(new(zone()) HJSArrayLength(object));
  } else {
    length = AddInstruction(new(zone()) HFixedArrayLength(elements));
  }
  HInstruction* checked_key =
      AddInstruction(new(zone()) HBoundsCheck(key, length));
  return new(zone()) HStoreKeyedFastElement(elements, checked_key, val);
}


HInstruction* HGraphBuilder::BuildStoreKeyedSpecializedArrayElement(
    HValue* object,
    HValue* key,
    HValue* val,
    Expression* expr) {
  ASSERT(expr->IsMonomorphic());
  AddInstruction(new(zone()) HCheckNonSmi(object));
  Handle<Map> map = expr->GetMonomorphicReceiverType();
  ASSERT(!map->has_fast_elements());
  ASSERT(map->has_external_array_elements());
  AddInstruction(new(zone()) HCheckMap(object, map));
  HLoadElements* elements = new(zone()) HLoadElements(object);
  AddInstruction(elements);
  HInstruction* length = AddInstruction(
      new(zone()) HExternalArrayLength(elements));
  HInstruction* checked_key =
      AddInstruction(new(zone()) HBoundsCheck(key, length));
  HLoadExternalArrayPointer* external_elements =
      new(zone()) HLoadExternalArrayPointer(elements);
  AddInstruction(external_elements);
  return new(zone()) HStoreKeyedSpecializedArrayElement(
      external_elements,
      checked_key,
      val,
      expr->external_array_type());
}


HInstruction* HGraphBuilder::BuildStoreKeyed(HValue* object,
                                             HValue* key,
                                             HValue* value,
                                             Expression* expr) {
  if (expr->IsMonomorphic()) {
    Handle<Map> receiver_type(expr->GetMonomorphicReceiverType());
    // An object has either fast elements or external array elements, but
    // never both. Pixel array maps that are assigned to pixel array elements
    // are always created with the fast elements flag cleared.
    if (receiver_type->has_external_array_elements()) {
      return BuildStoreKeyedSpecializedArrayElement(object,
                                                    key,
                                                    value,
                                                    expr);
    } else if (receiver_type->has_fast_elements()) {
      return BuildStoreKeyedFastElement(object, key, value, expr);
    }
  }
  return BuildStoreKeyedGeneric(object, key, value);
}


bool HGraphBuilder::TryArgumentsAccess(Property* expr) {
  VariableProxy* proxy = expr->obj()->AsVariableProxy();
  if (proxy == NULL) return false;
  if (!proxy->var()->IsStackAllocated()) return false;
  if (!environment()->Lookup(proxy->var())->CheckFlag(HValue::kIsArguments)) {
    return false;
  }

  // Our implementation of arguments (based on this stack frame or an
  // adapter below it) does not work for inlined functions.
  if (function_state()->outer() != NULL) {
    Bailout("arguments access in inlined function");
    return true;
  }

  HInstruction* result = NULL;
  if (expr->key()->IsPropertyName()) {
    Handle<String> name = expr->key()->AsLiteral()->AsPropertyName();
    if (!name->IsEqualTo(CStrVector("length"))) return false;
    HInstruction* elements = AddInstruction(new(zone()) HArgumentsElements);
    result = new(zone()) HArgumentsLength(elements);
  } else {
    Push(graph()->GetArgumentsObject());
    VisitForValue(expr->key());
    if (HasStackOverflow()) return false;
    HValue* key = Pop();
    Drop(1);  // Arguments object.
    HInstruction* elements = AddInstruction(new(zone()) HArgumentsElements);
    HInstruction* length = AddInstruction(
        new(zone()) HArgumentsLength(elements));
    HInstruction* checked_key =
        AddInstruction(new(zone()) HBoundsCheck(key, length));
    result = new(zone()) HAccessArgumentsAt(elements, length, checked_key);
  }
  ast_context()->ReturnInstruction(result, expr->id());
  return true;
}


void HGraphBuilder::VisitProperty(Property* expr) {
  expr->RecordTypeFeedback(oracle());

  if (TryArgumentsAccess(expr)) return;
  CHECK_BAILOUT;

  VISIT_FOR_VALUE(expr->obj());

  HInstruction* instr = NULL;
  if (expr->IsArrayLength()) {
    HValue* array = Pop();
    AddInstruction(new(zone()) HCheckNonSmi(array));
    AddInstruction(new(zone()) HCheckInstanceType(array,
                                                  JS_ARRAY_TYPE,
                                                  JS_ARRAY_TYPE));
    instr = new(zone()) HJSArrayLength(array);

  } else if (expr->IsStringLength()) {
    HValue* string = Pop();
    AddInstruction(new(zone()) HCheckNonSmi(string));
    AddInstruction(new(zone()) HCheckInstanceType(string,
                                                  FIRST_STRING_TYPE,
                                                  LAST_STRING_TYPE));
    instr = new(zone()) HStringLength(string);
  } else if (expr->IsStringAccess()) {
    VISIT_FOR_VALUE(expr->key());
    HValue* index = Pop();
    HValue* string = Pop();
    HStringCharCodeAt* char_code = BuildStringCharCodeAt(string, index);
    AddInstruction(char_code);
    instr = new(zone()) HStringCharFromCode(char_code);

  } else if (expr->IsFunctionPrototype()) {
    HValue* function = Pop();
    AddInstruction(new(zone()) HCheckNonSmi(function));
    instr = new(zone()) HLoadFunctionPrototype(function);

  } else if (expr->key()->IsPropertyName()) {
    Handle<String> name = expr->key()->AsLiteral()->AsPropertyName();
    ZoneMapList* types = expr->GetReceiverTypes();

    HValue* obj = Pop();
    if (expr->IsMonomorphic()) {
      instr = BuildLoadNamed(obj, expr, types->first(), name);
    } else if (types != NULL && types->length() > 1) {
      AddInstruction(new(zone()) HCheckNonSmi(obj));
      instr = new(zone()) HLoadNamedFieldPolymorphic(obj, types, name);
    } else {
      instr = BuildLoadNamedGeneric(obj, expr);
    }

  } else {
    VISIT_FOR_VALUE(expr->key());

    HValue* key = Pop();
    HValue* obj = Pop();
    instr = BuildLoadKeyed(obj, key, expr);
  }
  instr->set_position(expr->position());
  ast_context()->ReturnInstruction(instr, expr->id());
}


void HGraphBuilder::AddCheckConstantFunction(Call* expr,
                                             HValue* receiver,
                                             Handle<Map> receiver_map,
                                             bool smi_and_map_check) {
  // Constant functions have the nice property that the map will change if they
  // are overwritten.  Therefore it is enough to check the map of the holder and
  // its prototypes.
  if (smi_and_map_check) {
    AddInstruction(new(zone()) HCheckNonSmi(receiver));
    AddInstruction(new(zone()) HCheckMap(receiver, receiver_map));
  }
  if (!expr->holder().is_null()) {
    AddInstruction(new(zone()) HCheckPrototypeMaps(
        Handle<JSObject>(JSObject::cast(receiver_map->prototype())),
        expr->holder()));
  }
}


void HGraphBuilder::HandlePolymorphicCallNamed(Call* expr,
                                               HValue* receiver,
                                               ZoneMapList* types,
                                               Handle<String> name) {
  // TODO(ager): We should recognize when the prototype chains for different
  // maps are identical. In that case we can avoid repeatedly generating the
  // same prototype map checks.
  int argument_count = expr->arguments()->length() + 1;  // Includes receiver.
  int count = 0;
  HBasicBlock* join = NULL;
  for (int i = 0; i < types->length() && count < kMaxCallPolymorphism; ++i) {
    Handle<Map> map = types->at(i);
    if (expr->ComputeTarget(map, name)) {
      if (count == 0) {
        // Only needed once.
        AddInstruction(new(zone()) HCheckNonSmi(receiver));
        join = graph()->CreateBasicBlock();
      }
      ++count;
      HBasicBlock* if_true = graph()->CreateBasicBlock();
      HBasicBlock* if_false = graph()->CreateBasicBlock();
      HCompareMap* compare =
          new(zone()) HCompareMap(receiver, map, if_true, if_false);
      current_block()->Finish(compare);

      set_current_block(if_true);
      AddCheckConstantFunction(expr, receiver, map, false);
      if (FLAG_trace_inlining && FLAG_polymorphic_inlining) {
        PrintF("Trying to inline the polymorphic call to %s\n",
               *name->ToCString());
      }
      if (!FLAG_polymorphic_inlining || !TryInline(expr)) {
        // Check for bailout, as trying to inline might fail due to bailout
        // during hydrogen processing.
        CHECK_BAILOUT;
        HCallConstantFunction* call =
            new(zone()) HCallConstantFunction(expr->target(), argument_count);
        call->set_position(expr->position());
        PreProcessCall(call);
        AddInstruction(call);
        if (!ast_context()->IsEffect()) Push(call);
      }

      if (current_block() != NULL) current_block()->Goto(join);
      set_current_block(if_false);
    }
  }

  // Finish up.  Unconditionally deoptimize if we've handled all the maps we
  // know about and do not want to handle ones we've never seen.  Otherwise
  // use a generic IC.
  if (count == types->length() && FLAG_deoptimize_uncommon_cases) {
    current_block()->FinishExitWithDeoptimization();
  } else {
    HContext* context = new(zone()) HContext;
    AddInstruction(context);
    HCallNamed* call = new(zone()) HCallNamed(context, name, argument_count);
    call->set_position(expr->position());
    PreProcessCall(call);

    if (join != NULL) {
      AddInstruction(call);
      if (!ast_context()->IsEffect()) Push(call);
      current_block()->Goto(join);
    } else {
      ast_context()->ReturnInstruction(call, expr->id());
      return;
    }
  }

  // We assume that control flow is always live after an expression.  So
  // even without predecessors to the join block, we set it as the exit
  // block and continue by adding instructions there.
  ASSERT(join != NULL);
  set_current_block(join);
  if (join->HasPredecessor()) {
    join->SetJoinId(expr->id());
    if (!ast_context()->IsEffect()) ast_context()->ReturnValue(Pop());
  }
}


void HGraphBuilder::TraceInline(Handle<JSFunction> target, const char* reason) {
  if (FLAG_trace_inlining) {
    if (reason == NULL) {
      // We are currently in the context of inlined function thus we have
      // to go to an outer FunctionState to get caller.
      SmartPointer<char> callee = target->shared()->DebugName()->ToCString();
      SmartPointer<char> caller =
          function_state()->outer()->compilation_info()->function()->
              debug_name()->ToCString();
      PrintF("Inlined %s called from %s.\n", *callee, *caller);
    } else {
      SmartPointer<char> callee = target->shared()->DebugName()->ToCString();
      SmartPointer<char> caller =
          info()->function()->debug_name()->ToCString();
      PrintF("Did not inline %s called from %s (%s).\n",
             *callee, *caller, reason);
    }
  }
}


bool HGraphBuilder::TryInline(Call* expr) {
  if (!FLAG_use_inlining) return false;

  // Precondition: call is monomorphic and we have found a target with the
  // appropriate arity.
  Handle<JSFunction> target = expr->target();

  // Do a quick check on source code length to avoid parsing large
  // inlining candidates.
  if (FLAG_limit_inlining && target->shared()->SourceSize() > kMaxSourceSize) {
    TraceInline(target, "target text too big");
    return false;
  }

  // Target must be inlineable.
  if (!target->IsInlineable()) {
    TraceInline(target, "target not inlineable");
    return false;
  }

  // No context change required.
  CompilationInfo* outer_info = info();
  if (target->context() != outer_info->closure()->context() ||
      outer_info->scope()->contains_with() ||
      outer_info->scope()->num_heap_slots() > 0) {
    TraceInline(target, "target requires context change");
    return false;
  }

  // Don't inline deeper than kMaxInliningLevels calls.
  HEnvironment* env = environment();
  int current_level = 1;
  while (env->outer() != NULL) {
    if (current_level == Compiler::kMaxInliningLevels) {
      TraceInline(target, "inline depth limit reached");
      return false;
    }
    current_level++;
    env = env->outer();
  }

  // Don't inline recursive functions.
  if (target->shared() == outer_info->closure()->shared()) {
    TraceInline(target, "target is recursive");
    return false;
  }

  // We don't want to add more than a certain number of nodes from inlining.
  if (FLAG_limit_inlining && inlined_count_ > kMaxInlinedNodes) {
    TraceInline(target, "cumulative AST node limit reached");
    return false;
  }

  int count_before = AstNode::Count();

  // Parse and allocate variables.
  CompilationInfo target_info(target);
  if (!ParserApi::Parse(&target_info) ||
      !Scope::Analyze(&target_info)) {
    if (target_info.isolate()->has_pending_exception()) {
      // Parse or scope error, never optimize this function.
      SetStackOverflow();
      target->shared()->set_optimization_disabled(true);
    }
    TraceInline(target, "parse failure");
    return false;
  }

  if (target_info.scope()->num_heap_slots() > 0) {
    TraceInline(target, "target has context-allocated variables");
    return false;
  }
  FunctionLiteral* function = target_info.function();

  // Count the number of AST nodes added by inlining this call.
  int nodes_added = AstNode::Count() - count_before;
  if (FLAG_limit_inlining && nodes_added > kMaxInlinedSize) {
    TraceInline(target, "target AST is too large");
    return false;
  }

  // Check if we can handle all declarations in the inlined functions.
  VisitDeclarations(target_info.scope()->declarations());
  if (HasStackOverflow()) {
    TraceInline(target, "target has non-trivial declaration");
    ClearStackOverflow();
    return false;
  }

  // Don't inline functions that uses the arguments object or that
  // have a mismatching number of parameters.
  Handle<SharedFunctionInfo> target_shared(target->shared());
  int arity = expr->arguments()->length();
  if (function->scope()->arguments() != NULL ||
      arity != target_shared->formal_parameter_count()) {
    TraceInline(target, "target requires special argument handling");
    return false;
  }

  // All statements in the body must be inlineable.
  for (int i = 0, count = function->body()->length(); i < count; ++i) {
    if (!function->body()->at(i)->IsInlineable()) {
      TraceInline(target, "target contains unsupported syntax");
      return false;
    }
  }

  // Generate the deoptimization data for the unoptimized version of
  // the target function if we don't already have it.
  if (!target_shared->has_deoptimization_support()) {
    // Note that we compile here using the same AST that we will use for
    // generating the optimized inline code.
    target_info.EnableDeoptimizationSupport();
    if (!FullCodeGenerator::MakeCode(&target_info)) {
      TraceInline(target, "could not generate deoptimization info");
      return false;
    }
    target_shared->EnableDeoptimizationSupport(*target_info.code());
    Compiler::RecordFunctionCompilation(Logger::FUNCTION_TAG,
                                        &target_info,
                                        target_shared);
  }

  // ----------------------------------------------------------------
  // Save the pending call context and type feedback oracle. Set up new ones
  // for the inlined function.
  ASSERT(target_shared->has_deoptimization_support());
  TypeFeedbackOracle target_oracle(
      Handle<Code>(target_shared->code()),
      Handle<Context>(target->context()->global_context()));
  FunctionState target_state(this, &target_info, &target_oracle);

  HConstant* undefined = graph()->GetConstantUndefined();
  HEnvironment* inner_env =
      environment()->CopyForInlining(target, function, true, undefined);
  HBasicBlock* body_entry = CreateBasicBlock(inner_env);
  current_block()->Goto(body_entry);

  body_entry->SetJoinId(expr->ReturnId());
  set_current_block(body_entry);
  AddInstruction(new(zone()) HEnterInlined(target, function));
  VisitStatements(function->body());
  if (HasStackOverflow()) {
    // Bail out if the inline function did, as we cannot residualize a call
    // instead.
    TraceInline(target, "inline graph construction failed");
    return false;
  }

  // Update inlined nodes count.
  inlined_count_ += nodes_added;

  TraceInline(target, NULL);

  if (current_block() != NULL) {
    // Add a return of undefined if control can fall off the body.  In a
    // test context, undefined is false.
    if (inlined_test_context() == NULL) {
      ASSERT(function_return() != NULL);
      ASSERT(call_context()->IsEffect() || call_context()->IsValue());
      if (call_context()->IsEffect()) {
        current_block()->Goto(function_return(), false);
      } else {
        current_block()->AddLeaveInlined(undefined, function_return());
      }
    } else {
      // The graph builder assumes control can reach both branches of a
      // test, so we materialize the undefined value and test it rather than
      // simply jumping to the false target.
      //
      // TODO(3168478): refactor to avoid this.
      HBasicBlock* empty_true = graph()->CreateBasicBlock();
      HBasicBlock* empty_false = graph()->CreateBasicBlock();
      HTest* test = new(zone()) HTest(undefined, empty_true, empty_false);
      current_block()->Finish(test);

      empty_true->Goto(inlined_test_context()->if_true(), false);
      empty_false->Goto(inlined_test_context()->if_false(), false);
    }
  }

  // Fix up the function exits.
  if (inlined_test_context() != NULL) {
    HBasicBlock* if_true = inlined_test_context()->if_true();
    HBasicBlock* if_false = inlined_test_context()->if_false();
    if_true->SetJoinId(expr->id());
    if_false->SetJoinId(expr->id());
    ASSERT(ast_context() == inlined_test_context());
    // Pop the return test context from the expression context stack.
    ClearInlinedTestContext();

    // Forward to the real test context.
    HBasicBlock* true_target = TestContext::cast(ast_context())->if_true();
    HBasicBlock* false_target = TestContext::cast(ast_context())->if_false();
    if_true->Goto(true_target, false);
    if_false->Goto(false_target, false);

    // TODO(kmillikin): Come up with a better way to handle this. It is too
    // subtle. NULL here indicates that the enclosing context has no control
    // flow to handle.
    set_current_block(NULL);

  } else {
    function_return()->SetJoinId(expr->id());
    set_current_block(function_return());
  }

  return true;
}


bool HGraphBuilder::TryInlineBuiltinFunction(Call* expr,
                                             HValue* receiver,
                                             Handle<Map> receiver_map,
                                             CheckType check_type) {
  ASSERT(check_type != RECEIVER_MAP_CHECK || !receiver_map.is_null());
  // Try to inline calls like Math.* as operations in the calling function.
  if (!expr->target()->shared()->HasBuiltinFunctionId()) return false;
  BuiltinFunctionId id = expr->target()->shared()->builtin_function_id();
  int argument_count = expr->arguments()->length() + 1;  // Plus receiver.
  switch (id) {
    case kStringCharCodeAt:
    case kStringCharAt:
      if (argument_count == 2 && check_type == STRING_CHECK) {
        HValue* index = Pop();
        HValue* string = Pop();
        ASSERT(!expr->holder().is_null());
        AddInstruction(new(zone()) HCheckPrototypeMaps(
            oracle()->GetPrototypeForPrimitiveCheck(STRING_CHECK),
            expr->holder()));
        HStringCharCodeAt* char_code = BuildStringCharCodeAt(string, index);
        if (id == kStringCharCodeAt) {
          ast_context()->ReturnInstruction(char_code, expr->id());
          return true;
        }
        AddInstruction(char_code);
        HStringCharFromCode* result =
            new(zone()) HStringCharFromCode(char_code);
        ast_context()->ReturnInstruction(result, expr->id());
        return true;
      }
      break;
    case kMathRound:
    case kMathFloor:
    case kMathAbs:
    case kMathSqrt:
    case kMathLog:
    case kMathSin:
    case kMathCos:
      if (argument_count == 2 && check_type == RECEIVER_MAP_CHECK) {
        AddCheckConstantFunction(expr, receiver, receiver_map, true);
        HValue* argument = Pop();
        Drop(1);  // Receiver.
        HUnaryMathOperation* op = new(zone()) HUnaryMathOperation(argument, id);
        op->set_position(expr->position());
        ast_context()->ReturnInstruction(op, expr->id());
        return true;
      }
      break;
    case kMathPow:
      if (argument_count == 3 && check_type == RECEIVER_MAP_CHECK) {
        AddCheckConstantFunction(expr, receiver, receiver_map, true);
        HValue* right = Pop();
        HValue* left = Pop();
        Pop();  // Pop receiver.
        HInstruction* result = NULL;
        // Use sqrt() if exponent is 0.5 or -0.5.
        if (right->IsConstant() && HConstant::cast(right)->HasDoubleValue()) {
          double exponent = HConstant::cast(right)->DoubleValue();
          if (exponent == 0.5) {
            result = new(zone()) HUnaryMathOperation(left, kMathPowHalf);
          } else if (exponent == -0.5) {
            HConstant* double_one =
                new(zone()) HConstant(Handle<Object>(Smi::FromInt(1)),
                                      Representation::Double());
            AddInstruction(double_one);
            HUnaryMathOperation* square_root =
                new(zone()) HUnaryMathOperation(left, kMathPowHalf);
            AddInstruction(square_root);
            // MathPowHalf doesn't have side effects so there's no need for
            // an environment simulation here.
            ASSERT(!square_root->HasSideEffects());
            result = new(zone()) HDiv(double_one, square_root);
          } else if (exponent == 2.0) {
            result = new(zone()) HMul(left, left);
          }
        } else if (right->IsConstant() &&
                   HConstant::cast(right)->HasInteger32Value() &&
                   HConstant::cast(right)->Integer32Value() == 2) {
          result = new(zone()) HMul(left, left);
        }

        if (result == NULL) {
          result = new(zone()) HPower(left, right);
        }
        ast_context()->ReturnInstruction(result, expr->id());
        return true;
      }
      break;
    default:
      // Not yet supported for inlining.
      break;
  }
  return false;
}


bool HGraphBuilder::TryCallApply(Call* expr) {
  Expression* callee = expr->expression();
  Property* prop = callee->AsProperty();
  ASSERT(prop != NULL);

  if (!expr->IsMonomorphic() || expr->check_type() != RECEIVER_MAP_CHECK) {
    return false;
  }
  Handle<Map> function_map = expr->GetReceiverTypes()->first();
  if (function_map->instance_type() != JS_FUNCTION_TYPE ||
      !expr->target()->shared()->HasBuiltinFunctionId() ||
      expr->target()->shared()->builtin_function_id() != kFunctionApply) {
    return false;
  }

  if (info()->scope()->arguments() == NULL) return false;

  ZoneList<Expression*>* args = expr->arguments();
  if (args->length() != 2) return false;

  VariableProxy* arg_two = args->at(1)->AsVariableProxy();
  if (arg_two == NULL || !arg_two->var()->IsStackAllocated()) return false;
  HValue* arg_two_value = environment()->Lookup(arg_two->var());
  if (!arg_two_value->CheckFlag(HValue::kIsArguments)) return false;

  // Our implementation of arguments (based on this stack frame or an
  // adapter below it) does not work for inlined functions.
  if (function_state()->outer() != NULL) {
    Bailout("Function.prototype.apply optimization in inlined function");
    return true;
  }

  // Found pattern f.apply(receiver, arguments).
  VisitForValue(prop->obj());
  if (HasStackOverflow()) return false;
  HValue* function = Pop();
  VisitForValue(args->at(0));
  if (HasStackOverflow()) return false;
  HValue* receiver = Pop();
  HInstruction* elements = AddInstruction(new(zone()) HArgumentsElements);
  HInstruction* length = AddInstruction(new(zone()) HArgumentsLength(elements));
  AddCheckConstantFunction(expr, function, function_map, true);
  HInstruction* result =
      new(zone()) HApplyArguments(function, receiver, length, elements);
  result->set_position(expr->position());
  ast_context()->ReturnInstruction(result, expr->id());
  return true;
}


void HGraphBuilder::VisitCall(Call* expr) {
  Expression* callee = expr->expression();
  int argument_count = expr->arguments()->length() + 1;  // Plus receiver.
  HInstruction* call = NULL;

  Property* prop = callee->AsProperty();
  if (prop != NULL) {
    if (!prop->key()->IsPropertyName()) {
      // Keyed function call.
      VISIT_FOR_VALUE(prop->obj());

      VISIT_FOR_VALUE(prop->key());
      // Push receiver and key like the non-optimized code generator expects it.
      HValue* key = Pop();
      HValue* receiver = Pop();
      Push(key);
      Push(receiver);

      VisitExpressions(expr->arguments());
      CHECK_BAILOUT;

      HContext* context = new(zone()) HContext;
      AddInstruction(context);
      call = PreProcessCall(
          new(zone()) HCallKeyed(context, key, argument_count));
      call->set_position(expr->position());
      Drop(1);  // Key.
      ast_context()->ReturnInstruction(call, expr->id());
      return;
    }

    // Named function call.
    expr->RecordTypeFeedback(oracle());

    if (TryCallApply(expr)) return;
    CHECK_BAILOUT;

    VISIT_FOR_VALUE(prop->obj());
    VisitExpressions(expr->arguments());
    CHECK_BAILOUT;

    Handle<String> name = prop->key()->AsLiteral()->AsPropertyName();

    expr->RecordTypeFeedback(oracle());
    ZoneMapList* types = expr->GetReceiverTypes();

    HValue* receiver =
        environment()->ExpressionStackAt(expr->arguments()->length());
    if (expr->IsMonomorphic()) {
      Handle<Map> receiver_map =
          (types == NULL) ? Handle<Map>::null() : types->first();
      if (TryInlineBuiltinFunction(expr,
                                   receiver,
                                   receiver_map,
                                   expr->check_type())) {
        return;
      }

      if (CallStubCompiler::HasCustomCallGenerator(*expr->target()) ||
          expr->check_type() != RECEIVER_MAP_CHECK) {
        // When the target has a custom call IC generator, use the IC,
        // because it is likely to generate better code.  Also use the IC
        // when a primitive receiver check is required.
        HContext* context = new(zone()) HContext;
        AddInstruction(context);
        call = PreProcessCall(
            new(zone()) HCallNamed(context, name, argument_count));
      } else {
        AddCheckConstantFunction(expr, receiver, receiver_map, true);

        if (TryInline(expr)) {
          return;
        } else {
          // Check for bailout, as the TryInline call in the if condition above
          // might return false due to bailout during hydrogen processing.
          CHECK_BAILOUT;
          call = PreProcessCall(
              new(zone()) HCallConstantFunction(expr->target(),
                                                argument_count));
        }
      }
    } else if (types != NULL && types->length() > 1) {
      ASSERT(expr->check_type() == RECEIVER_MAP_CHECK);
      HandlePolymorphicCallNamed(expr, receiver, types, name);
      return;

    } else {
      HContext* context = new(zone()) HContext;
      AddInstruction(context);
      call = PreProcessCall(
          new(zone()) HCallNamed(context, name, argument_count));
    }

  } else {
    Variable* var = expr->expression()->AsVariableProxy()->AsVariable();
    bool global_call = (var != NULL) && var->is_global() && !var->is_this();

    if (!global_call) {
      ++argument_count;
      VISIT_FOR_VALUE(expr->expression());
    }

    if (global_call) {
      bool known_global_function = false;
      // If there is a global property cell for the name at compile time and
      // access check is not enabled we assume that the function will not change
      // and generate optimized code for calling the function.
      LookupResult lookup;
      GlobalPropertyAccess type = LookupGlobalProperty(var, &lookup, false);
      if (type == kUseCell &&
          !info()->global_object()->IsAccessCheckNeeded()) {
        Handle<GlobalObject> global(info()->global_object());
        known_global_function = expr->ComputeGlobalTarget(global, &lookup);
      }
      if (known_global_function) {
        // Push the global object instead of the global receiver because
        // code generated by the full code generator expects it.
        HContext* context = new(zone()) HContext;
        HGlobalObject* global_object = new(zone()) HGlobalObject(context);
        AddInstruction(context);
        PushAndAdd(global_object);
        VisitExpressions(expr->arguments());
        CHECK_BAILOUT;

        VISIT_FOR_VALUE(expr->expression());
        HValue* function = Pop();
        AddInstruction(new(zone()) HCheckFunction(function, expr->target()));

        // Replace the global object with the global receiver.
        HGlobalReceiver* global_receiver =
            new(zone()) HGlobalReceiver(global_object);
        // Index of the receiver from the top of the expression stack.
        const int receiver_index = argument_count - 1;
        AddInstruction(global_receiver);
        ASSERT(environment()->ExpressionStackAt(receiver_index)->
               IsGlobalObject());
        environment()->SetExpressionStackAt(receiver_index, global_receiver);

        if (TryInline(expr)) {
          return;
        }
        // Check for bailout, as trying to inline might fail due to bailout
        // during hydrogen processing.
        CHECK_BAILOUT;

        call = PreProcessCall(new(zone()) HCallKnownGlobal(expr->target(),
                                                   argument_count));
      } else {
        HContext* context = new(zone()) HContext;
        AddInstruction(context);
        PushAndAdd(new(zone()) HGlobalObject(context));
        VisitExpressions(expr->arguments());
        CHECK_BAILOUT;

        call = PreProcessCall(new(zone()) HCallGlobal(context,
                                              var->name(),
                                              argument_count));
      }

    } else {
      HContext* context = new(zone()) HContext;
      HGlobalObject* global_object = new(zone()) HGlobalObject(context);
      AddInstruction(context);
      AddInstruction(global_object);
      PushAndAdd(new(zone()) HGlobalReceiver(global_object));
      VisitExpressions(expr->arguments());
      CHECK_BAILOUT;

      call = PreProcessCall(new(zone()) HCallFunction(context, argument_count));
    }
  }

  call->set_position(expr->position());
  ast_context()->ReturnInstruction(call, expr->id());
}


void HGraphBuilder::VisitCallNew(CallNew* expr) {
  // The constructor function is also used as the receiver argument to the
  // JS construct call builtin.
  VISIT_FOR_VALUE(expr->expression());
  VisitExpressions(expr->arguments());
  CHECK_BAILOUT;

  HContext* context = new(zone()) HContext;
  AddInstruction(context);

  // The constructor is both an operand to the instruction and an argument
  // to the construct call.
  int arg_count = expr->arguments()->length() + 1;  // Plus constructor.
  HValue* constructor = environment()->ExpressionStackAt(arg_count - 1);
  HCallNew* call = new(zone()) HCallNew(context, constructor, arg_count);
  call->set_position(expr->position());
  PreProcessCall(call);
  ast_context()->ReturnInstruction(call, expr->id());
}


// Support for generating inlined runtime functions.

// Lookup table for generators for runtime calls that are  generated inline.
// Elements of the table are member pointers to functions of HGraphBuilder.
#define INLINE_FUNCTION_GENERATOR_ADDRESS(Name, argc, ressize)  \
    &HGraphBuilder::Generate##Name,

const HGraphBuilder::InlineFunctionGenerator
    HGraphBuilder::kInlineFunctionGenerators[] = {
        INLINE_FUNCTION_LIST(INLINE_FUNCTION_GENERATOR_ADDRESS)
        INLINE_RUNTIME_FUNCTION_LIST(INLINE_FUNCTION_GENERATOR_ADDRESS)
};
#undef INLINE_FUNCTION_GENERATOR_ADDRESS


void HGraphBuilder::VisitCallRuntime(CallRuntime* expr) {
  if (expr->is_jsruntime()) {
    BAILOUT("call to a JavaScript runtime function");
  }

  const Runtime::Function* function = expr->function();
  ASSERT(function != NULL);
  if (function->intrinsic_type == Runtime::INLINE) {
    ASSERT(expr->name()->length() > 0);
    ASSERT(expr->name()->Get(0) == '_');
    // Call to an inline function.
    int lookup_index = static_cast<int>(function->function_id) -
        static_cast<int>(Runtime::kFirstInlineFunction);
    ASSERT(lookup_index >= 0);
    ASSERT(static_cast<size_t>(lookup_index) <
           ARRAY_SIZE(kInlineFunctionGenerators));
    InlineFunctionGenerator generator = kInlineFunctionGenerators[lookup_index];

    // Call the inline code generator using the pointer-to-member.
    (this->*generator)(expr);
  } else {
    ASSERT(function->intrinsic_type == Runtime::RUNTIME);
    VisitArgumentList(expr->arguments());
    CHECK_BAILOUT;

    Handle<String> name = expr->name();
    int argument_count = expr->arguments()->length();
    HCallRuntime* call =
        new(zone()) HCallRuntime(name, function, argument_count);
    call->set_position(RelocInfo::kNoPosition);
    Drop(argument_count);
    ast_context()->ReturnInstruction(call, expr->id());
  }
}


void HGraphBuilder::VisitUnaryOperation(UnaryOperation* expr) {
  Token::Value op = expr->op();
  if (op == Token::VOID) {
    VISIT_FOR_EFFECT(expr->expression());
    ast_context()->ReturnValue(graph()->GetConstantUndefined());
  } else if (op == Token::DELETE) {
    Property* prop = expr->expression()->AsProperty();
    Variable* var = expr->expression()->AsVariableProxy()->AsVariable();
    if (prop == NULL && var == NULL) {
      // Result of deleting non-property, non-variable reference is true.
      // Evaluate the subexpression for side effects.
      VISIT_FOR_EFFECT(expr->expression());
      ast_context()->ReturnValue(graph()->GetConstantTrue());
    } else if (var != NULL &&
               !var->is_global() &&
               var->AsSlot() != NULL &&
               var->AsSlot()->type() != Slot::LOOKUP) {
      // Result of deleting non-global, non-dynamic variables is false.
      // The subexpression does not have side effects.
      ast_context()->ReturnValue(graph()->GetConstantFalse());
    } else if (prop != NULL) {
      if (prop->is_synthetic()) {
        // Result of deleting parameters is false, even when they rewrite
        // to accesses on the arguments object.
        ast_context()->ReturnValue(graph()->GetConstantFalse());
      } else {
        VISIT_FOR_VALUE(prop->obj());
        VISIT_FOR_VALUE(prop->key());
        HValue* key = Pop();
        HValue* obj = Pop();
        HDeleteProperty* instr = new(zone()) HDeleteProperty(obj, key);
        ast_context()->ReturnInstruction(instr, expr->id());
      }
    } else if (var->is_global()) {
      BAILOUT("delete with global variable");
    } else {
      BAILOUT("delete with non-global variable");
    }
  } else if (op == Token::NOT) {
    if (ast_context()->IsTest()) {
      TestContext* context = TestContext::cast(ast_context());
      VisitForControl(expr->expression(),
                      context->if_false(),
                      context->if_true());
    } else if (ast_context()->IsValue()) {
      HBasicBlock* materialize_false = graph()->CreateBasicBlock();
      HBasicBlock* materialize_true = graph()->CreateBasicBlock();
      VISIT_FOR_CONTROL(expr->expression(),
                        materialize_false,
                        materialize_true);
      materialize_false->SetJoinId(expr->expression()->id());
      materialize_true->SetJoinId(expr->expression()->id());

      set_current_block(materialize_false);
      Push(graph()->GetConstantFalse());
      set_current_block(materialize_true);
      Push(graph()->GetConstantTrue());

      HBasicBlock* join =
          CreateJoin(materialize_false, materialize_true, expr->id());
      set_current_block(join);
      ast_context()->ReturnValue(Pop());
    } else {
      ASSERT(ast_context()->IsEffect());
      VisitForEffect(expr->expression());
    }

  } else if (op == Token::TYPEOF) {
    VisitForTypeOf(expr->expression());
    if (HasStackOverflow()) return;
    HValue* value = Pop();
    ast_context()->ReturnInstruction(new(zone()) HTypeof(value), expr->id());

  } else {
    VISIT_FOR_VALUE(expr->expression());
    HValue* value = Pop();
    HInstruction* instr = NULL;
    switch (op) {
      case Token::BIT_NOT:
        instr = new(zone()) HBitNot(value);
        break;
      case Token::SUB:
        instr = new(zone()) HMul(value, graph_->GetConstantMinus1());
        break;
      case Token::ADD:
        instr = new(zone()) HMul(value, graph_->GetConstant1());
        break;
      default:
        BAILOUT("Value: unsupported unary operation");
        break;
    }
    ast_context()->ReturnInstruction(instr, expr->id());
  }
}


HInstruction* HGraphBuilder::BuildIncrement(HValue* value, bool increment) {
  HConstant* delta = increment
      ? graph_->GetConstant1()
      : graph_->GetConstantMinus1();
  HInstruction* instr = new(zone()) HAdd(value, delta);
  AssumeRepresentation(instr,  Representation::Integer32());
  return instr;
}


void HGraphBuilder::VisitCountOperation(CountOperation* expr) {
  Expression* target = expr->expression();
  VariableProxy* proxy = target->AsVariableProxy();
  Variable* var = proxy->AsVariable();
  Property* prop = target->AsProperty();
  ASSERT(var == NULL || prop == NULL);
  bool inc = expr->op() == Token::INC;

  if (var != NULL) {
    VISIT_FOR_VALUE(target);

    // Match the full code generator stack by simulating an extra stack
    // element for postfix operations in a non-effect context.
    bool has_extra = expr->is_postfix() && !ast_context()->IsEffect();
    HValue* before = has_extra ? Top() : Pop();
    HInstruction* after = BuildIncrement(before, inc);
    AddInstruction(after);
    Push(after);

    if (var->is_global()) {
      HandleGlobalVariableAssignment(var,
                                     after,
                                     expr->position(),
                                     expr->AssignmentId());
    } else if (var->IsStackAllocated()) {
      Bind(var, after);
    } else if (var->IsContextSlot()) {
      HValue* context = BuildContextChainWalk(var);
      int index = var->AsSlot()->index();
      HStoreContextSlot* instr =
          new(zone()) HStoreContextSlot(context, index, after);
      AddInstruction(instr);
      if (instr->HasSideEffects()) AddSimulate(expr->AssignmentId());
    } else {
      BAILOUT("lookup variable in count operation");
    }
    Drop(has_extra ? 2 : 1);
    ast_context()->ReturnValue(expr->is_postfix() ? before : after);

  } else if (prop != NULL) {
    prop->RecordTypeFeedback(oracle());

    if (prop->key()->IsPropertyName()) {
      // Named property.

      // Match the full code generator stack by simulating an extra stack
      // element for postfix operations in a non-effect context.
      bool has_extra = expr->is_postfix() && !ast_context()->IsEffect();
      if (has_extra) Push(graph_->GetConstantUndefined());

      VISIT_FOR_VALUE(prop->obj());
      HValue* obj = Top();

      HInstruction* load = NULL;
      if (prop->IsMonomorphic()) {
        Handle<String> name = prop->key()->AsLiteral()->AsPropertyName();
        Handle<Map> map = prop->GetReceiverTypes()->first();
        load = BuildLoadNamed(obj, prop, map, name);
      } else {
        load = BuildLoadNamedGeneric(obj, prop);
      }
      PushAndAdd(load);
      if (load->HasSideEffects()) AddSimulate(expr->CountId());

      HValue* before = Pop();
      // There is no deoptimization to after the increment, so we don't need
      // to simulate the expression stack after this instruction.
      HInstruction* after = BuildIncrement(before, inc);
      AddInstruction(after);

      HInstruction* store = BuildStoreNamed(obj, after, prop);
      AddInstruction(store);

      // Overwrite the receiver in the bailout environment with the result
      // of the operation, and the placeholder with the original value if
      // necessary.
      environment()->SetExpressionStackAt(0, after);
      if (has_extra) environment()->SetExpressionStackAt(1, before);
      if (store->HasSideEffects()) AddSimulate(expr->AssignmentId());
      Drop(has_extra ? 2 : 1);

      ast_context()->ReturnValue(expr->is_postfix() ? before : after);

    } else {
      // Keyed property.

      // Match the full code generator stack by simulate an extra stack element
      // for postfix operations in a non-effect context.
      bool has_extra = expr->is_postfix() && !ast_context()->IsEffect();
      if (has_extra) Push(graph_->GetConstantUndefined());

      VISIT_FOR_VALUE(prop->obj());
      VISIT_FOR_VALUE(prop->key());
      HValue* obj = environment()->ExpressionStackAt(1);
      HValue* key = environment()->ExpressionStackAt(0);

      HInstruction* load = BuildLoadKeyed(obj, key, prop);
      PushAndAdd(load);
      if (load->HasSideEffects()) AddSimulate(expr->CountId());

      HValue* before = Pop();
      // There is no deoptimization to after the increment, so we don't need
      // to simulate the expression stack after this instruction.
      HInstruction* after = BuildIncrement(before, inc);
      AddInstruction(after);

      expr->RecordTypeFeedback(oracle());
      HInstruction* store = BuildStoreKeyed(obj, key, after, expr);
      AddInstruction(store);

      // Drop the key from the bailout environment.  Overwrite the receiver
      // with the result of the operation, and the placeholder with the
      // original value if necessary.
      Drop(1);
      environment()->SetExpressionStackAt(0, after);
      if (has_extra) environment()->SetExpressionStackAt(1, before);
      if (store->HasSideEffects()) AddSimulate(expr->AssignmentId());
      Drop(has_extra ? 2 : 1);

      ast_context()->ReturnValue(expr->is_postfix() ? before : after);
    }

  } else {
    BAILOUT("invalid lhs in count operation");
  }
}


HStringCharCodeAt* HGraphBuilder::BuildStringCharCodeAt(HValue* string,
                                                        HValue* index) {
  AddInstruction(new(zone()) HCheckNonSmi(string));
  AddInstruction(new(zone()) HCheckInstanceType(
      string, FIRST_STRING_TYPE, LAST_STRING_TYPE));
  HStringLength* length = new(zone()) HStringLength(string);
  AddInstruction(length);
  HInstruction* checked_index =
      AddInstruction(new(zone()) HBoundsCheck(index, length));
  return new(zone()) HStringCharCodeAt(string, checked_index);
}


HInstruction* HGraphBuilder::BuildBinaryOperation(BinaryOperation* expr,
                                                  HValue* left,
                                                  HValue* right) {
  HInstruction* instr = NULL;
  switch (expr->op()) {
    case Token::ADD:
      instr = new(zone()) HAdd(left, right);
      break;
    case Token::SUB:
      instr = new(zone()) HSub(left, right);
      break;
    case Token::MUL:
      instr = new(zone()) HMul(left, right);
      break;
    case Token::MOD:
      instr = new(zone()) HMod(left, right);
      break;
    case Token::DIV:
      instr = new(zone()) HDiv(left, right);
      break;
    case Token::BIT_XOR:
      instr = new(zone()) HBitXor(left, right);
      break;
    case Token::BIT_AND:
      instr = new(zone()) HBitAnd(left, right);
      break;
    case Token::BIT_OR:
      instr = new(zone()) HBitOr(left, right);
      break;
    case Token::SAR:
      instr = new(zone()) HSar(left, right);
      break;
    case Token::SHR:
      instr = new(zone()) HShr(left, right);
      break;
    case Token::SHL:
      instr = new(zone()) HShl(left, right);
      break;
    default:
      UNREACHABLE();
  }
  TypeInfo info = oracle()->BinaryType(expr);
  // If we hit an uninitialized binary op stub we will get type info
  // for a smi operation. If one of the operands is a constant string
  // do not generate code assuming it is a smi operation.
  if (info.IsSmi() &&
      ((left->IsConstant() && HConstant::cast(left)->HasStringValue()) ||
       (right->IsConstant() && HConstant::cast(right)->HasStringValue()))) {
    return instr;
  }
  if (FLAG_trace_representation) {
    PrintF("Info: %s/%s\n", info.ToString(), ToRepresentation(info).Mnemonic());
  }
  Representation rep = ToRepresentation(info);
  // We only generate either int32 or generic tagged bitwise operations.
  if (instr->IsBitwiseBinaryOperation() && rep.IsDouble()) {
    rep = Representation::Integer32();
  }
  AssumeRepresentation(instr, rep);
  return instr;
}


// Check for the form (%_ClassOf(foo) === 'BarClass').
static bool IsClassOfTest(CompareOperation* expr) {
  if (expr->op() != Token::EQ_STRICT) return false;
  CallRuntime* call = expr->left()->AsCallRuntime();
  if (call == NULL) return false;
  Literal* literal = expr->right()->AsLiteral();
  if (literal == NULL) return false;
  if (!literal->handle()->IsString()) return false;
  if (!call->name()->IsEqualTo(CStrVector("_ClassOf"))) return false;
  ASSERT(call->arguments()->length() == 1);
  return true;
}


void HGraphBuilder::VisitBinaryOperation(BinaryOperation* expr) {
  if (expr->op() == Token::COMMA) {
    VISIT_FOR_EFFECT(expr->left());
    // Visit the right subexpression in the same AST context as the entire
    // expression.
    Visit(expr->right());

  } else if (expr->op() == Token::AND || expr->op() == Token::OR) {
    bool is_logical_and = (expr->op() == Token::AND);
    if (ast_context()->IsTest()) {
      TestContext* context = TestContext::cast(ast_context());
      // Translate left subexpression.
      HBasicBlock* eval_right = graph()->CreateBasicBlock();
      if (is_logical_and) {
        VISIT_FOR_CONTROL(expr->left(), eval_right, context->if_false());
      } else {
        VISIT_FOR_CONTROL(expr->left(), context->if_true(), eval_right);
      }
      eval_right->SetJoinId(expr->RightId());

      // Translate right subexpression by visiting it in the same AST
      // context as the entire expression.
      set_current_block(eval_right);
      Visit(expr->right());

    } else if (ast_context()->IsValue()) {
      VISIT_FOR_VALUE(expr->left());
      ASSERT(current_block() != NULL);

      // We need an extra block to maintain edge-split form.
      HBasicBlock* empty_block = graph()->CreateBasicBlock();
      HBasicBlock* eval_right = graph()->CreateBasicBlock();
      HTest* test = is_logical_and
          ? new(zone()) HTest(Top(), eval_right, empty_block)
          : new(zone()) HTest(Top(), empty_block, eval_right);
      current_block()->Finish(test);

      set_current_block(eval_right);
      Drop(1);  // Value of the left subexpression.
      VISIT_FOR_VALUE(expr->right());

      HBasicBlock* join_block =
          CreateJoin(empty_block, current_block(), expr->id());
      set_current_block(join_block);
      ast_context()->ReturnValue(Pop());

    } else {
      ASSERT(ast_context()->IsEffect());
      // In an effect context, we don't need the value of the left
      // subexpression, only its control flow and side effects.  We need an
      // extra block to maintain edge-split form.
      HBasicBlock* empty_block = graph()->CreateBasicBlock();
      HBasicBlock* right_block = graph()->CreateBasicBlock();
      HBasicBlock* join_block = graph()->CreateBasicBlock();
      if (is_logical_and) {
        VISIT_FOR_CONTROL(expr->left(), right_block, empty_block);
      } else {
        VISIT_FOR_CONTROL(expr->left(), empty_block, right_block);
      }
      // TODO(kmillikin): Find a way to fix this.  It's ugly that there are
      // actually two empty blocks (one here and one inserted by
      // TestContext::BuildBranch, and that they both have an HSimulate
      // though the second one is not a merge node, and that we really have
      // no good AST ID to put on that first HSimulate.
      empty_block->SetJoinId(expr->id());
      right_block->SetJoinId(expr->RightId());
      set_current_block(right_block);
      VISIT_FOR_EFFECT(expr->right());

      empty_block->Goto(join_block);
      current_block()->Goto(join_block);
      join_block->SetJoinId(expr->id());
      set_current_block(join_block);
      // We did not materialize any value in the predecessor environments,
      // so there is no need to handle it here.
    }

  } else {
    VISIT_FOR_VALUE(expr->left());
    VISIT_FOR_VALUE(expr->right());

    HValue* right = Pop();
    HValue* left = Pop();
    HInstruction* instr = BuildBinaryOperation(expr, left, right);
    instr->set_position(expr->position());
    ast_context()->ReturnInstruction(instr, expr->id());
  }
}


void HGraphBuilder::AssumeRepresentation(HValue* value, Representation r) {
  if (value->CheckFlag(HValue::kFlexibleRepresentation)) {
    if (FLAG_trace_representation) {
      PrintF("Assume representation for %s to be %s (%d)\n",
             value->Mnemonic(),
             r.Mnemonic(),
             graph_->GetMaximumValueID());
    }
    value->ChangeRepresentation(r);
    // The representation of the value is dictated by type feedback and
    // will not be changed later.
    value->ClearFlag(HValue::kFlexibleRepresentation);
  } else if (FLAG_trace_representation) {
    PrintF("No representation assumed\n");
  }
}


Representation HGraphBuilder::ToRepresentation(TypeInfo info) {
  if (info.IsSmi()) return Representation::Integer32();
  if (info.IsInteger32()) return Representation::Integer32();
  if (info.IsDouble()) return Representation::Double();
  if (info.IsNumber()) return Representation::Double();
  return Representation::Tagged();
}


void HGraphBuilder::VisitCompareOperation(CompareOperation* expr) {
  if (IsClassOfTest(expr)) {
    CallRuntime* call = expr->left()->AsCallRuntime();
    VISIT_FOR_VALUE(call->arguments()->at(0));
    HValue* value = Pop();
    Literal* literal = expr->right()->AsLiteral();
    Handle<String> rhs = Handle<String>::cast(literal->handle());
    HInstruction* instr = new(zone()) HClassOfTest(value, rhs);
    instr->set_position(expr->position());
    ast_context()->ReturnInstruction(instr, expr->id());
    return;
  }

  // Check for the pattern: typeof <expression> == <string literal>.
  UnaryOperation* left_unary = expr->left()->AsUnaryOperation();
  Literal* right_literal = expr->right()->AsLiteral();
  if ((expr->op() == Token::EQ || expr->op() == Token::EQ_STRICT) &&
      left_unary != NULL && left_unary->op() == Token::TYPEOF &&
      right_literal != NULL && right_literal->handle()->IsString()) {
    VisitForTypeOf(left_unary->expression());
    if (HasStackOverflow()) return;
    HValue* left = Pop();
    HInstruction* instr = new(zone()) HTypeofIs(left,
        Handle<String>::cast(right_literal->handle()));
    instr->set_position(expr->position());
    ast_context()->ReturnInstruction(instr, expr->id());
    return;
  }

  VISIT_FOR_VALUE(expr->left());
  VISIT_FOR_VALUE(expr->right());

  HValue* right = Pop();
  HValue* left = Pop();
  Token::Value op = expr->op();

  TypeInfo type_info = oracle()->CompareType(expr);
  HInstruction* instr = NULL;
  if (op == Token::INSTANCEOF) {
    // Check to see if the rhs of the instanceof is a global function not
    // residing in new space. If it is we assume that the function will stay the
    // same.
    Handle<JSFunction> target = Handle<JSFunction>::null();
    Variable* var = expr->right()->AsVariableProxy()->AsVariable();
    bool global_function = (var != NULL) && var->is_global() && !var->is_this();
    if (global_function &&
        info()->has_global_object() &&
        !info()->global_object()->IsAccessCheckNeeded()) {
      Handle<String> name = var->name();
      Handle<GlobalObject> global(info()->global_object());
      LookupResult lookup;
      global->Lookup(*name, &lookup);
      if (lookup.IsProperty() &&
          lookup.type() == NORMAL &&
          lookup.GetValue()->IsJSFunction()) {
        Handle<JSFunction> candidate(JSFunction::cast(lookup.GetValue()));
        // If the function is in new space we assume it's more likely to
        // change and thus prefer the general IC code.
        if (!isolate()->heap()->InNewSpace(*candidate)) {
          target = candidate;
        }
      }
    }

    // If the target is not null we have found a known global function that is
    // assumed to stay the same for this instanceof.
    if (target.is_null()) {
      HContext* context = new(zone()) HContext;
      AddInstruction(context);
      instr = new(zone()) HInstanceOf(context, left, right);
    } else {
      AddInstruction(new(zone()) HCheckFunction(right, target));
      instr = new(zone()) HInstanceOfKnownGlobal(left, target);
    }
  } else if (op == Token::IN) {
    BAILOUT("Unsupported comparison: in");
  } else if (type_info.IsNonPrimitive()) {
    switch (op) {
      case Token::EQ:
      case Token::EQ_STRICT: {
        AddInstruction(new(zone()) HCheckNonSmi(left));
        AddInstruction(HCheckInstanceType::NewIsJSObjectOrJSFunction(left));
        AddInstruction(new(zone()) HCheckNonSmi(right));
        AddInstruction(HCheckInstanceType::NewIsJSObjectOrJSFunction(right));
        instr = new(zone()) HCompareJSObjectEq(left, right);
        break;
      }
      default:
        BAILOUT("Unsupported non-primitive compare");
        break;
    }
  } else {
    HCompare* compare = new(zone()) HCompare(left, right, op);
    Representation r = ToRepresentation(type_info);
    compare->SetInputRepresentation(r);
    instr = compare;
  }
  instr->set_position(expr->position());
  ast_context()->ReturnInstruction(instr, expr->id());
}


void HGraphBuilder::VisitCompareToNull(CompareToNull* expr) {
  VISIT_FOR_VALUE(expr->expression());

  HValue* value = Pop();
  HIsNull* compare = new(zone()) HIsNull(value, expr->is_strict());
  ast_context()->ReturnInstruction(compare, expr->id());
}


void HGraphBuilder::VisitThisFunction(ThisFunction* expr) {
  BAILOUT("ThisFunction");
}


void HGraphBuilder::VisitDeclaration(Declaration* decl) {
  // We allow only declarations that do not require code generation.
  // The following all require code generation: global variables and
  // functions, variables with slot type LOOKUP, declarations with
  // mode CONST, and functions.
  Variable* var = decl->proxy()->var();
  Slot* slot = var->AsSlot();
  if (var->is_global() ||
      (slot != NULL && slot->type() == Slot::LOOKUP) ||
      decl->mode() == Variable::CONST ||
      decl->fun() != NULL) {
    BAILOUT("unsupported declaration");
  }
}


// Generators for inline runtime functions.
// Support for types.
void HGraphBuilder::GenerateIsSmi(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  VISIT_FOR_VALUE(call->arguments()->at(0));
  HValue* value = Pop();
  HIsSmi* result = new(zone()) HIsSmi(value);
  ast_context()->ReturnInstruction(result, call->id());
}


void HGraphBuilder::GenerateIsSpecObject(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  VISIT_FOR_VALUE(call->arguments()->at(0));
  HValue* value = Pop();
  HHasInstanceType* result =
      new(zone()) HHasInstanceType(value, FIRST_JS_OBJECT_TYPE, LAST_TYPE);
  ast_context()->ReturnInstruction(result, call->id());
}


void HGraphBuilder::GenerateIsFunction(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  VISIT_FOR_VALUE(call->arguments()->at(0));
  HValue* value = Pop();
  HHasInstanceType* result =
      new(zone()) HHasInstanceType(value, JS_FUNCTION_TYPE);
  ast_context()->ReturnInstruction(result, call->id());
}


void HGraphBuilder::GenerateHasCachedArrayIndex(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  VISIT_FOR_VALUE(call->arguments()->at(0));
  HValue* value = Pop();
  HHasCachedArrayIndex* result = new(zone()) HHasCachedArrayIndex(value);
  ast_context()->ReturnInstruction(result, call->id());
}


void HGraphBuilder::GenerateIsArray(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  VISIT_FOR_VALUE(call->arguments()->at(0));
  HValue* value = Pop();
  HHasInstanceType* result = new(zone()) HHasInstanceType(value, JS_ARRAY_TYPE);
  ast_context()->ReturnInstruction(result, call->id());
}


void HGraphBuilder::GenerateIsRegExp(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  VISIT_FOR_VALUE(call->arguments()->at(0));
  HValue* value = Pop();
  HHasInstanceType* result =
      new(zone()) HHasInstanceType(value, JS_REGEXP_TYPE);
  ast_context()->ReturnInstruction(result, call->id());
}


void HGraphBuilder::GenerateIsObject(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  VISIT_FOR_VALUE(call->arguments()->at(0));
  HValue* value = Pop();
  HIsObject* test = new(zone()) HIsObject(value);
  ast_context()->ReturnInstruction(test, call->id());
}


void HGraphBuilder::GenerateIsNonNegativeSmi(CallRuntime* call) {
  BAILOUT("inlined runtime function: IsNonNegativeSmi");
}


void HGraphBuilder::GenerateIsUndetectableObject(CallRuntime* call) {
  BAILOUT("inlined runtime function: IsUndetectableObject");
}


void HGraphBuilder::GenerateIsStringWrapperSafeForDefaultValueOf(
    CallRuntime* call) {
  BAILOUT("inlined runtime function: IsStringWrapperSafeForDefaultValueOf");
}


// Support for construct call checks.
void HGraphBuilder::GenerateIsConstructCall(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 0);
  if (function_state()->outer() != NULL) {
    // We are generating graph for inlined function. Currently
    // constructor inlining is not supported and we can just return
    // false from %_IsConstructCall().
    ast_context()->ReturnValue(graph()->GetConstantFalse());
  } else {
    ast_context()->ReturnInstruction(new(zone()) HIsConstructCall, call->id());
  }
}


// Support for arguments.length and arguments[?].
void HGraphBuilder::GenerateArgumentsLength(CallRuntime* call) {
  // Our implementation of arguments (based on this stack frame or an
  // adapter below it) does not work for inlined functions.  This runtime
  // function is blacklisted by AstNode::IsInlineable.
  ASSERT(function_state()->outer() == NULL);
  ASSERT(call->arguments()->length() == 0);
  HInstruction* elements = AddInstruction(new(zone()) HArgumentsElements);
  HArgumentsLength* result = new(zone()) HArgumentsLength(elements);
  ast_context()->ReturnInstruction(result, call->id());
}


void HGraphBuilder::GenerateArguments(CallRuntime* call) {
  // Our implementation of arguments (based on this stack frame or an
  // adapter below it) does not work for inlined functions.  This runtime
  // function is blacklisted by AstNode::IsInlineable.
  ASSERT(function_state()->outer() == NULL);
  ASSERT(call->arguments()->length() == 1);
  VISIT_FOR_VALUE(call->arguments()->at(0));
  HValue* index = Pop();
  HInstruction* elements = AddInstruction(new(zone()) HArgumentsElements);
  HInstruction* length = AddInstruction(new(zone()) HArgumentsLength(elements));
  HAccessArgumentsAt* result =
      new(zone()) HAccessArgumentsAt(elements, length, index);
  ast_context()->ReturnInstruction(result, call->id());
}


// Support for accessing the class and value fields of an object.
void HGraphBuilder::GenerateClassOf(CallRuntime* call) {
  // The special form detected by IsClassOfTest is detected before we get here
  // and does not cause a bailout.
  BAILOUT("inlined runtime function: ClassOf");
}


void HGraphBuilder::GenerateValueOf(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  VISIT_FOR_VALUE(call->arguments()->at(0));
  HValue* value = Pop();
  HValueOf* result = new(zone()) HValueOf(value);
  ast_context()->ReturnInstruction(result, call->id());
}


void HGraphBuilder::GenerateSetValueOf(CallRuntime* call) {
  BAILOUT("inlined runtime function: SetValueOf");
}


// Fast support for charCodeAt(n).
void HGraphBuilder::GenerateStringCharCodeAt(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 2);
  VISIT_FOR_VALUE(call->arguments()->at(0));
  VISIT_FOR_VALUE(call->arguments()->at(1));
  HValue* index = Pop();
  HValue* string = Pop();
  HStringCharCodeAt* result = BuildStringCharCodeAt(string, index);
  ast_context()->ReturnInstruction(result, call->id());
}


// Fast support for string.charAt(n) and string[n].
void HGraphBuilder::GenerateStringCharFromCode(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  VISIT_FOR_VALUE(call->arguments()->at(0));
  HValue* char_code = Pop();
  HStringCharFromCode* result = new(zone()) HStringCharFromCode(char_code);
  ast_context()->ReturnInstruction(result, call->id());
}


// Fast support for string.charAt(n) and string[n].
void HGraphBuilder::GenerateStringCharAt(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 2);
  VISIT_FOR_VALUE(call->arguments()->at(0));
  VISIT_FOR_VALUE(call->arguments()->at(1));
  HValue* index = Pop();
  HValue* string = Pop();
  HStringCharCodeAt* char_code = BuildStringCharCodeAt(string, index);
  AddInstruction(char_code);
  HStringCharFromCode* result = new(zone()) HStringCharFromCode(char_code);
  ast_context()->ReturnInstruction(result, call->id());
}


// Fast support for object equality testing.
void HGraphBuilder::GenerateObjectEquals(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 2);
  VISIT_FOR_VALUE(call->arguments()->at(0));
  VISIT_FOR_VALUE(call->arguments()->at(1));
  HValue* right = Pop();
  HValue* left = Pop();
  HCompareJSObjectEq* result = new(zone()) HCompareJSObjectEq(left, right);
  ast_context()->ReturnInstruction(result, call->id());
}


void HGraphBuilder::GenerateLog(CallRuntime* call) {
  // %_Log is ignored in optimized code.
  ast_context()->ReturnValue(graph()->GetConstantUndefined());
}


// Fast support for Math.random().
void HGraphBuilder::GenerateRandomHeapNumber(CallRuntime* call) {
  BAILOUT("inlined runtime function: RandomHeapNumber");
}


// Fast support for StringAdd.
void HGraphBuilder::GenerateStringAdd(CallRuntime* call) {
  ASSERT_EQ(2, call->arguments()->length());
  VisitArgumentList(call->arguments());
  CHECK_BAILOUT;
  HContext* context = new(zone()) HContext;
  AddInstruction(context);
  HCallStub* result = new(zone()) HCallStub(context, CodeStub::StringAdd, 2);
  Drop(2);
  ast_context()->ReturnInstruction(result, call->id());
}


// Fast support for SubString.
void HGraphBuilder::GenerateSubString(CallRuntime* call) {
  ASSERT_EQ(3, call->arguments()->length());
  VisitArgumentList(call->arguments());
  CHECK_BAILOUT;
  HContext* context = new(zone()) HContext;
  AddInstruction(context);
  HCallStub* result = new(zone()) HCallStub(context, CodeStub::SubString, 3);
  Drop(3);
  ast_context()->ReturnInstruction(result, call->id());
}


// Fast support for StringCompare.
void HGraphBuilder::GenerateStringCompare(CallRuntime* call) {
  ASSERT_EQ(2, call->arguments()->length());
  VisitArgumentList(call->arguments());
  CHECK_BAILOUT;
  HContext* context = new(zone()) HContext;
  AddInstruction(context);
  HCallStub* result =
      new(zone()) HCallStub(context, CodeStub::StringCompare, 2);
  Drop(2);
  ast_context()->ReturnInstruction(result, call->id());
}


// Support for direct calls from JavaScript to native RegExp code.
void HGraphBuilder::GenerateRegExpExec(CallRuntime* call) {
  ASSERT_EQ(4, call->arguments()->length());
  VisitArgumentList(call->arguments());
  CHECK_BAILOUT;
  HContext* context = new(zone()) HContext;
  AddInstruction(context);
  HCallStub* result = new(zone()) HCallStub(context, CodeStub::RegExpExec, 4);
  Drop(4);
  ast_context()->ReturnInstruction(result, call->id());
}


// Construct a RegExp exec result with two in-object properties.
void HGraphBuilder::GenerateRegExpConstructResult(CallRuntime* call) {
  ASSERT_EQ(3, call->arguments()->length());
  VisitArgumentList(call->arguments());
  CHECK_BAILOUT;
  HContext* context = new(zone()) HContext;
  AddInstruction(context);
  HCallStub* result =
      new(zone()) HCallStub(context, CodeStub::RegExpConstructResult, 3);
  Drop(3);
  ast_context()->ReturnInstruction(result, call->id());
}


// Support for fast native caches.
void HGraphBuilder::GenerateGetFromCache(CallRuntime* call) {
  BAILOUT("inlined runtime function: GetFromCache");
}


// Fast support for number to string.
void HGraphBuilder::GenerateNumberToString(CallRuntime* call) {
  ASSERT_EQ(1, call->arguments()->length());
  VisitArgumentList(call->arguments());
  CHECK_BAILOUT;
  HContext* context = new(zone()) HContext;
  AddInstruction(context);
  HCallStub* result =
      new(zone()) HCallStub(context, CodeStub::NumberToString, 1);
  Drop(1);
  ast_context()->ReturnInstruction(result, call->id());
}


// Fast swapping of elements. Takes three expressions, the object and two
// indices. This should only be used if the indices are known to be
// non-negative and within bounds of the elements array at the call site.
void HGraphBuilder::GenerateSwapElements(CallRuntime* call) {
  BAILOUT("inlined runtime function: SwapElements");
}


// Fast call for custom callbacks.
void HGraphBuilder::GenerateCallFunction(CallRuntime* call) {
  BAILOUT("inlined runtime function: CallFunction");
}


// Fast call to math functions.
void HGraphBuilder::GenerateMathPow(CallRuntime* call) {
  ASSERT_EQ(2, call->arguments()->length());
  VISIT_FOR_VALUE(call->arguments()->at(0));
  VISIT_FOR_VALUE(call->arguments()->at(1));
  HValue* right = Pop();
  HValue* left = Pop();
  HPower* result = new(zone()) HPower(left, right);
  ast_context()->ReturnInstruction(result, call->id());
}


void HGraphBuilder::GenerateMathSin(CallRuntime* call) {
  ASSERT_EQ(1, call->arguments()->length());
  VisitArgumentList(call->arguments());
  CHECK_BAILOUT;
  HContext* context = new(zone()) HContext;
  AddInstruction(context);
  HCallStub* result =
      new(zone()) HCallStub(context, CodeStub::TranscendentalCache, 1);
  result->set_transcendental_type(TranscendentalCache::SIN);
  Drop(1);
  ast_context()->ReturnInstruction(result, call->id());
}


void HGraphBuilder::GenerateMathCos(CallRuntime* call) {
  ASSERT_EQ(1, call->arguments()->length());
  VisitArgumentList(call->arguments());
  CHECK_BAILOUT;
  HContext* context = new(zone()) HContext;
  AddInstruction(context);
  HCallStub* result =
      new(zone()) HCallStub(context, CodeStub::TranscendentalCache, 1);
  result->set_transcendental_type(TranscendentalCache::COS);
  Drop(1);
  ast_context()->ReturnInstruction(result, call->id());
}


void HGraphBuilder::GenerateMathLog(CallRuntime* call) {
  ASSERT_EQ(1, call->arguments()->length());
  VisitArgumentList(call->arguments());
  CHECK_BAILOUT;
  HContext* context = new(zone()) HContext;
  AddInstruction(context);
  HCallStub* result =
      new(zone()) HCallStub(context, CodeStub::TranscendentalCache, 1);
  result->set_transcendental_type(TranscendentalCache::LOG);
  Drop(1);
  ast_context()->ReturnInstruction(result, call->id());
}


void HGraphBuilder::GenerateMathSqrt(CallRuntime* call) {
  BAILOUT("inlined runtime function: MathSqrt");
}


// Check whether two RegExps are equivalent
void HGraphBuilder::GenerateIsRegExpEquivalent(CallRuntime* call) {
  BAILOUT("inlined runtime function: IsRegExpEquivalent");
}


void HGraphBuilder::GenerateGetCachedArrayIndex(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  VISIT_FOR_VALUE(call->arguments()->at(0));
  HValue* value = Pop();
  HGetCachedArrayIndex* result = new(zone()) HGetCachedArrayIndex(value);
  ast_context()->ReturnInstruction(result, call->id());
}


void HGraphBuilder::GenerateFastAsciiArrayJoin(CallRuntime* call) {
  BAILOUT("inlined runtime function: FastAsciiArrayJoin");
}


#undef BAILOUT
#undef CHECK_BAILOUT
#undef VISIT_FOR_EFFECT
#undef VISIT_FOR_VALUE
#undef ADD_TO_SUBGRAPH


HEnvironment::HEnvironment(HEnvironment* outer,
                           Scope* scope,
                           Handle<JSFunction> closure)
    : closure_(closure),
      values_(0),
      assigned_variables_(4),
      parameter_count_(0),
      local_count_(0),
      outer_(outer),
      pop_count_(0),
      push_count_(0),
      ast_id_(AstNode::kNoNumber) {
  Initialize(scope->num_parameters() + 1, scope->num_stack_slots(), 0);
}


HEnvironment::HEnvironment(const HEnvironment* other)
    : values_(0),
      assigned_variables_(0),
      parameter_count_(0),
      local_count_(0),
      outer_(NULL),
      pop_count_(0),
      push_count_(0),
      ast_id_(other->ast_id()) {
  Initialize(other);
}


void HEnvironment::Initialize(int parameter_count,
                              int local_count,
                              int stack_height) {
  parameter_count_ = parameter_count;
  local_count_ = local_count;

  // Avoid reallocating the temporaries' backing store on the first Push.
  int total = parameter_count + local_count + stack_height;
  values_.Initialize(total + 4);
  for (int i = 0; i < total; ++i) values_.Add(NULL);
}


void HEnvironment::Initialize(const HEnvironment* other) {
  closure_ = other->closure();
  values_.AddAll(other->values_);
  assigned_variables_.AddAll(other->assigned_variables_);
  parameter_count_ = other->parameter_count_;
  local_count_ = other->local_count_;
  if (other->outer_ != NULL) outer_ = other->outer_->Copy();  // Deep copy.
  pop_count_ = other->pop_count_;
  push_count_ = other->push_count_;
  ast_id_ = other->ast_id_;
}


void HEnvironment::AddIncomingEdge(HBasicBlock* block, HEnvironment* other) {
  ASSERT(!block->IsLoopHeader());
  ASSERT(values_.length() == other->values_.length());

  int length = values_.length();
  for (int i = 0; i < length; ++i) {
    HValue* value = values_[i];
    if (value != NULL && value->IsPhi() && value->block() == block) {
      // There is already a phi for the i'th value.
      HPhi* phi = HPhi::cast(value);
      // Assert index is correct and that we haven't missed an incoming edge.
      ASSERT(phi->merged_index() == i);
      ASSERT(phi->OperandCount() == block->predecessors()->length());
      phi->AddInput(other->values_[i]);
    } else if (values_[i] != other->values_[i]) {
      // There is a fresh value on the incoming edge, a phi is needed.
      ASSERT(values_[i] != NULL && other->values_[i] != NULL);
      HPhi* phi = new(block->zone()) HPhi(i);
      HValue* old_value = values_[i];
      for (int j = 0; j < block->predecessors()->length(); j++) {
        phi->AddInput(old_value);
      }
      phi->AddInput(other->values_[i]);
      this->values_[i] = phi;
      block->AddPhi(phi);
    }
  }
}


void HEnvironment::Bind(int index, HValue* value) {
  ASSERT(value != NULL);
  if (!assigned_variables_.Contains(index)) {
    assigned_variables_.Add(index);
  }
  values_[index] = value;
}


bool HEnvironment::HasExpressionAt(int index) const {
  return index >= parameter_count_ + local_count_;
}


bool HEnvironment::ExpressionStackIsEmpty() const {
  int first_expression = parameter_count() + local_count();
  ASSERT(length() >= first_expression);
  return length() == first_expression;
}


void HEnvironment::SetExpressionStackAt(int index_from_top, HValue* value) {
  int count = index_from_top + 1;
  int index = values_.length() - count;
  ASSERT(HasExpressionAt(index));
  // The push count must include at least the element in question or else
  // the new value will not be included in this environment's history.
  if (push_count_ < count) {
    // This is the same effect as popping then re-pushing 'count' elements.
    pop_count_ += (count - push_count_);
    push_count_ = count;
  }
  values_[index] = value;
}


void HEnvironment::Drop(int count) {
  for (int i = 0; i < count; ++i) {
    Pop();
  }
}


HEnvironment* HEnvironment::Copy() const {
  return new(closure()->GetIsolate()->zone()) HEnvironment(this);
}


HEnvironment* HEnvironment::CopyWithoutHistory() const {
  HEnvironment* result = Copy();
  result->ClearHistory();
  return result;
}


HEnvironment* HEnvironment::CopyAsLoopHeader(HBasicBlock* loop_header) const {
  HEnvironment* new_env = Copy();
  for (int i = 0; i < values_.length(); ++i) {
    HPhi* phi = new(loop_header->zone()) HPhi(i);
    phi->AddInput(values_[i]);
    new_env->values_[i] = phi;
    loop_header->AddPhi(phi);
  }
  new_env->ClearHistory();
  return new_env;
}


HEnvironment* HEnvironment::CopyForInlining(Handle<JSFunction> target,
                                            FunctionLiteral* function,
                                            bool is_speculative,
                                            HConstant* undefined) const {
  // Outer environment is a copy of this one without the arguments.
  int arity = function->scope()->num_parameters();
  HEnvironment* outer = Copy();
  outer->Drop(arity + 1);  // Including receiver.
  outer->ClearHistory();
  Zone* zone = closure()->GetIsolate()->zone();
  HEnvironment* inner =
      new(zone) HEnvironment(outer, function->scope(), target);
  // Get the argument values from the original environment.
  if (is_speculative) {
    for (int i = 0; i <= arity; ++i) {  // Include receiver.
      HValue* push = ExpressionStackAt(arity - i);
      inner->SetValueAt(i, push);
    }
  } else {
    for (int i = 0; i <= arity; ++i) {  // Include receiver.
      inner->SetValueAt(i, ExpressionStackAt(arity - i));
    }
  }

  // Initialize the stack-allocated locals to undefined.
  int local_base = arity + 1;
  int local_count = function->scope()->num_stack_slots();
  for (int i = 0; i < local_count; ++i) {
    inner->SetValueAt(local_base + i, undefined);
  }

  inner->set_ast_id(AstNode::kFunctionEntryId);
  return inner;
}


void HEnvironment::PrintTo(StringStream* stream) {
  for (int i = 0; i < length(); i++) {
    if (i == 0) stream->Add("parameters\n");
    if (i == parameter_count()) stream->Add("locals\n");
    if (i == parameter_count() + local_count()) stream->Add("expressions");
    HValue* val = values_.at(i);
    stream->Add("%d: ", i);
    if (val != NULL) {
      val->PrintNameTo(stream);
    } else {
      stream->Add("NULL");
    }
    stream->Add("\n");
  }
}


void HEnvironment::PrintToStd() {
  HeapStringAllocator string_allocator;
  StringStream trace(&string_allocator);
  PrintTo(&trace);
  PrintF("%s", *trace.ToCString());
}


void HTracer::TraceCompilation(FunctionLiteral* function) {
  Tag tag(this, "compilation");
  Handle<String> name = function->debug_name();
  PrintStringProperty("name", *name->ToCString());
  PrintStringProperty("method", *name->ToCString());
  PrintLongProperty("date", static_cast<int64_t>(OS::TimeCurrentMillis()));
}


void HTracer::TraceLithium(const char* name, LChunk* chunk) {
  Trace(name, chunk->graph(), chunk);
}


void HTracer::TraceHydrogen(const char* name, HGraph* graph) {
  Trace(name, graph, NULL);
}


void HTracer::Trace(const char* name, HGraph* graph, LChunk* chunk) {
  Tag tag(this, "cfg");
  PrintStringProperty("name", name);
  const ZoneList<HBasicBlock*>* blocks = graph->blocks();
  for (int i = 0; i < blocks->length(); i++) {
    HBasicBlock* current = blocks->at(i);
    Tag block_tag(this, "block");
    PrintBlockProperty("name", current->block_id());
    PrintIntProperty("from_bci", -1);
    PrintIntProperty("to_bci", -1);

    if (!current->predecessors()->is_empty()) {
      PrintIndent();
      trace_.Add("predecessors");
      for (int j = 0; j < current->predecessors()->length(); ++j) {
        trace_.Add(" \"B%d\"", current->predecessors()->at(j)->block_id());
      }
      trace_.Add("\n");
    } else {
      PrintEmptyProperty("predecessors");
    }

    if (current->end() == NULL || current->end()->FirstSuccessor() == NULL) {
      PrintEmptyProperty("successors");
    } else if (current->end()->SecondSuccessor() == NULL) {
      PrintBlockProperty("successors",
                             current->end()->FirstSuccessor()->block_id());
    } else {
      PrintBlockProperty("successors",
                             current->end()->FirstSuccessor()->block_id(),
                             current->end()->SecondSuccessor()->block_id());
    }

    PrintEmptyProperty("xhandlers");
    PrintEmptyProperty("flags");

    if (current->dominator() != NULL) {
      PrintBlockProperty("dominator", current->dominator()->block_id());
    }

    if (chunk != NULL) {
      int first_index = current->first_instruction_index();
      int last_index = current->last_instruction_index();
      PrintIntProperty(
          "first_lir_id",
          LifetimePosition::FromInstructionIndex(first_index).Value());
      PrintIntProperty(
          "last_lir_id",
          LifetimePosition::FromInstructionIndex(last_index).Value());
    }

    {
      Tag states_tag(this, "states");
      Tag locals_tag(this, "locals");
      int total = current->phis()->length();
      trace_.Add("size %d\n", total);
      trace_.Add("method \"None\"");
      for (int j = 0; j < total; ++j) {
        HPhi* phi = current->phis()->at(j);
        trace_.Add("%d ", phi->merged_index());
        phi->PrintNameTo(&trace_);
        trace_.Add(" ");
        phi->PrintTo(&trace_);
        trace_.Add("\n");
      }
    }

    {
      Tag HIR_tag(this, "HIR");
      HInstruction* instruction = current->first();
      while (instruction != NULL) {
        int bci = 0;
        int uses = instruction->uses()->length();
        trace_.Add("%d %d ", bci, uses);
        instruction->PrintNameTo(&trace_);
        trace_.Add(" ");
        instruction->PrintTo(&trace_);
        trace_.Add(" <|@\n");
        instruction = instruction->next();
      }
    }


    if (chunk != NULL) {
      Tag LIR_tag(this, "LIR");
      int first_index = current->first_instruction_index();
      int last_index = current->last_instruction_index();
      if (first_index != -1 && last_index != -1) {
        const ZoneList<LInstruction*>* instructions = chunk->instructions();
        for (int i = first_index; i <= last_index; ++i) {
          LInstruction* linstr = instructions->at(i);
          if (linstr != NULL) {
            trace_.Add("%d ",
                       LifetimePosition::FromInstructionIndex(i).Value());
            linstr->PrintTo(&trace_);
            trace_.Add(" <|@\n");
          }
        }
      }
    }
  }
}


void HTracer::TraceLiveRanges(const char* name, LAllocator* allocator) {
  Tag tag(this, "intervals");
  PrintStringProperty("name", name);

  const Vector<LiveRange*>* fixed_d = allocator->fixed_double_live_ranges();
  for (int i = 0; i < fixed_d->length(); ++i) {
    TraceLiveRange(fixed_d->at(i), "fixed");
  }

  const Vector<LiveRange*>* fixed = allocator->fixed_live_ranges();
  for (int i = 0; i < fixed->length(); ++i) {
    TraceLiveRange(fixed->at(i), "fixed");
  }

  const ZoneList<LiveRange*>* live_ranges = allocator->live_ranges();
  for (int i = 0; i < live_ranges->length(); ++i) {
    TraceLiveRange(live_ranges->at(i), "object");
  }
}


void HTracer::TraceLiveRange(LiveRange* range, const char* type) {
  if (range != NULL && !range->IsEmpty()) {
    trace_.Add("%d %s", range->id(), type);
    if (range->HasRegisterAssigned()) {
      LOperand* op = range->CreateAssignedOperand();
      int assigned_reg = op->index();
      if (op->IsDoubleRegister()) {
        trace_.Add(" \"%s\"",
                   DoubleRegister::AllocationIndexToString(assigned_reg));
      } else {
        ASSERT(op->IsRegister());
        trace_.Add(" \"%s\"", Register::AllocationIndexToString(assigned_reg));
      }
    } else if (range->IsSpilled()) {
      LOperand* op = range->TopLevel()->GetSpillOperand();
      if (op->IsDoubleStackSlot()) {
        trace_.Add(" \"double_stack:%d\"", op->index());
      } else {
        ASSERT(op->IsStackSlot());
        trace_.Add(" \"stack:%d\"", op->index());
      }
    }
    int parent_index = -1;
    if (range->IsChild()) {
      parent_index = range->parent()->id();
    } else {
      parent_index = range->id();
    }
    LOperand* op = range->FirstHint();
    int hint_index = -1;
    if (op != NULL && op->IsUnallocated()) hint_index = op->VirtualRegister();
    trace_.Add(" %d %d", parent_index, hint_index);
    UseInterval* cur_interval = range->first_interval();
    while (cur_interval != NULL && range->Covers(cur_interval->start())) {
      trace_.Add(" [%d, %d[",
                 cur_interval->start().Value(),
                 cur_interval->end().Value());
      cur_interval = cur_interval->next();
    }

    UsePosition* current_pos = range->first_pos();
    while (current_pos != NULL) {
      if (current_pos->RegisterIsBeneficial() || FLAG_trace_all_uses) {
        trace_.Add(" %d M", current_pos->pos().Value());
      }
      current_pos = current_pos->next();
    }

    trace_.Add(" \"\"\n");
  }
}


void HTracer::FlushToFile() {
  AppendChars(filename_, *trace_.ToCString(), trace_.length(), false);
  trace_.Reset();
}


void HStatistics::Initialize(CompilationInfo* info) {
  source_size_ += info->shared_info()->SourceSize();
}


void HStatistics::Print() {
  PrintF("Timing results:\n");
  int64_t sum = 0;
  for (int i = 0; i < timing_.length(); ++i) {
    sum += timing_[i];
  }

  for (int i = 0; i < names_.length(); ++i) {
    PrintF("%30s", names_[i]);
    double ms = static_cast<double>(timing_[i]) / 1000;
    double percent = static_cast<double>(timing_[i]) * 100 / sum;
    PrintF(" - %7.3f ms / %4.1f %% ", ms, percent);

    unsigned size = sizes_[i];
    double size_percent = static_cast<double>(size) * 100 / total_size_;
    PrintF(" %8u bytes / %4.1f %%\n", size, size_percent);
  }
  double source_size_in_kb = static_cast<double>(source_size_) / 1024;
  double normalized_time =  source_size_in_kb > 0
      ? (static_cast<double>(sum) / 1000) / source_size_in_kb
      : 0;
  double normalized_bytes = source_size_in_kb > 0
      ? total_size_ / source_size_in_kb
      : 0;
  PrintF("%30s - %7.3f ms           %7.3f bytes\n", "Sum",
         normalized_time, normalized_bytes);
  PrintF("---------------------------------------------------------------\n");
  PrintF("%30s - %7.3f ms (%.1f times slower than full code gen)\n",
         "Total",
         static_cast<double>(total_) / 1000,
         static_cast<double>(total_) / full_code_gen_);
}


void HStatistics::SaveTiming(const char* name, int64_t ticks, unsigned size) {
  if (name == HPhase::kFullCodeGen) {
    full_code_gen_ += ticks;
  } else if (name == HPhase::kTotal) {
    total_ += ticks;
  } else {
    total_size_ += size;
    for (int i = 0; i < names_.length(); ++i) {
      if (names_[i] == name) {
        timing_[i] += ticks;
        sizes_[i] += size;
        return;
      }
    }
    names_.Add(name);
    timing_.Add(ticks);
    sizes_.Add(size);
  }
}


const char* const HPhase::kFullCodeGen = "Full code generator";
const char* const HPhase::kTotal = "Total";


void HPhase::Begin(const char* name,
                   HGraph* graph,
                   LChunk* chunk,
                   LAllocator* allocator) {
  name_ = name;
  graph_ = graph;
  chunk_ = chunk;
  allocator_ = allocator;
  if (allocator != NULL && chunk_ == NULL) {
    chunk_ = allocator->chunk();
  }
  if (FLAG_hydrogen_stats) start_ = OS::Ticks();
  start_allocation_size_ = Zone::allocation_size_;
}


void HPhase::End() const {
  if (FLAG_hydrogen_stats) {
    int64_t end = OS::Ticks();
    unsigned size = Zone::allocation_size_ - start_allocation_size_;
    HStatistics::Instance()->SaveTiming(name_, end - start_, size);
  }

  if (FLAG_trace_hydrogen) {
    if (graph_ != NULL) HTracer::Instance()->TraceHydrogen(name_, graph_);
    if (chunk_ != NULL) HTracer::Instance()->TraceLithium(name_, chunk_);
    if (allocator_ != NULL) {
      HTracer::Instance()->TraceLiveRanges(name_, allocator_);
    }
  }

#ifdef DEBUG
  if (graph_ != NULL) graph_->Verify();
  if (allocator_ != NULL) allocator_->Verify();
#endif
}

} }  // namespace v8::internal
