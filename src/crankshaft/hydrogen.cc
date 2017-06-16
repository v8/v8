// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/crankshaft/hydrogen.h"

#include <memory>
#include <sstream>

#include "src/allocation-site-scopes.h"
#include "src/ast/ast-numbering.h"
#include "src/ast/compile-time-value.h"
#include "src/ast/scopes.h"
#include "src/code-factory.h"
#include "src/crankshaft/hydrogen-bce.h"
#include "src/crankshaft/hydrogen-canonicalize.h"
#include "src/crankshaft/hydrogen-check-elimination.h"
#include "src/crankshaft/hydrogen-dce.h"
#include "src/crankshaft/hydrogen-dehoist.h"
#include "src/crankshaft/hydrogen-environment-liveness.h"
#include "src/crankshaft/hydrogen-escape-analysis.h"
#include "src/crankshaft/hydrogen-gvn.h"
#include "src/crankshaft/hydrogen-infer-representation.h"
#include "src/crankshaft/hydrogen-infer-types.h"
#include "src/crankshaft/hydrogen-load-elimination.h"
#include "src/crankshaft/hydrogen-mark-unreachable.h"
#include "src/crankshaft/hydrogen-range-analysis.h"
#include "src/crankshaft/hydrogen-redundant-phi.h"
#include "src/crankshaft/hydrogen-removable-simulates.h"
#include "src/crankshaft/hydrogen-representation-changes.h"
#include "src/crankshaft/hydrogen-sce.h"
#include "src/crankshaft/hydrogen-store-elimination.h"
#include "src/crankshaft/hydrogen-uint32-analysis.h"
#include "src/crankshaft/lithium-allocator.h"
#include "src/field-type.h"
#include "src/full-codegen/full-codegen.h"
#include "src/globals.h"
#include "src/ic/call-optimization.h"
#include "src/ic/ic.h"
// GetRootConstructor
#include "src/ic/ic-inl.h"
#include "src/isolate-inl.h"
#include "src/objects/map.h"
#include "src/runtime/runtime.h"

#if V8_TARGET_ARCH_IA32
#include "src/crankshaft/ia32/lithium-codegen-ia32.h"  // NOLINT
#elif V8_TARGET_ARCH_X64
#include "src/crankshaft/x64/lithium-codegen-x64.h"  // NOLINT
#elif V8_TARGET_ARCH_ARM64
#include "src/crankshaft/arm64/lithium-codegen-arm64.h"  // NOLINT
#elif V8_TARGET_ARCH_ARM
#include "src/crankshaft/arm/lithium-codegen-arm.h"  // NOLINT
#elif V8_TARGET_ARCH_PPC
#include "src/crankshaft/ppc/lithium-codegen-ppc.h"  // NOLINT
#elif V8_TARGET_ARCH_MIPS
#include "src/crankshaft/mips/lithium-codegen-mips.h"  // NOLINT
#elif V8_TARGET_ARCH_MIPS64
#include "src/crankshaft/mips64/lithium-codegen-mips64.h"  // NOLINT
#elif V8_TARGET_ARCH_S390
#include "src/crankshaft/s390/lithium-codegen-s390.h"  // NOLINT
#elif V8_TARGET_ARCH_X87
#include "src/crankshaft/x87/lithium-codegen-x87.h"  // NOLINT
#else
#error Unsupported target architecture.
#endif

namespace v8 {
namespace internal {

const auto GetRegConfig = RegisterConfiguration::Crankshaft;

HBasicBlock::HBasicBlock(HGraph* graph)
    : block_id_(graph->GetNextBlockID()),
      graph_(graph),
      phis_(4, graph->zone()),
      first_(NULL),
      last_(NULL),
      end_(NULL),
      loop_information_(NULL),
      predecessors_(2, graph->zone()),
      dominator_(NULL),
      dominated_blocks_(4, graph->zone()),
      last_environment_(NULL),
      argument_count_(-1),
      first_instruction_index_(-1),
      last_instruction_index_(-1),
      deleted_phis_(4, graph->zone()),
      parent_loop_header_(NULL),
      is_reachable_(true),
      dominates_loop_successors_(false),
      is_osr_entry_(false),
      is_ordered_(false) { }


Isolate* HBasicBlock::isolate() const {
  return graph_->isolate();
}


void HBasicBlock::MarkUnreachable() {
  is_reachable_ = false;
}


void HBasicBlock::AttachLoopInformation() {
  DCHECK(!IsLoopHeader());
  loop_information_ = new(zone()) HLoopInformation(this, zone());
}


void HBasicBlock::DetachLoopInformation() {
  DCHECK(IsLoopHeader());
  loop_information_ = NULL;
}


void HBasicBlock::AddPhi(HPhi* phi) {
  DCHECK(!IsStartBlock());
  phis_.Add(phi, zone());
  phi->SetBlock(this);
}


void HBasicBlock::RemovePhi(HPhi* phi) {
  DCHECK(phi->block() == this);
  DCHECK(phis_.Contains(phi));
  phi->Kill();
  phis_.RemoveElement(phi);
  phi->SetBlock(NULL);
}


void HBasicBlock::AddInstruction(HInstruction* instr, SourcePosition position) {
  DCHECK(!IsStartBlock() || !IsFinished());
  DCHECK(!instr->IsLinked());
  DCHECK(!IsFinished());

  if (position.IsKnown()) {
    instr->set_position(position);
  }
  if (first_ == NULL) {
    DCHECK(last_environment() != NULL);
    DCHECK(!last_environment()->ast_id().IsNone());
    HBlockEntry* entry = new(zone()) HBlockEntry();
    entry->InitializeAsFirst(this);
    if (position.IsKnown()) {
      entry->set_position(position);
    } else {
      DCHECK(!FLAG_hydrogen_track_positions ||
             !graph()->info()->IsOptimizing() || instr->IsAbnormalExit());
    }
    first_ = last_ = entry;
  }
  instr->InsertAfter(last_);
}


HPhi* HBasicBlock::AddNewPhi(int merged_index) {
  if (graph()->IsInsideNoSideEffectsScope()) {
    merged_index = HPhi::kInvalidMergedIndex;
  }
  HPhi* phi = new(zone()) HPhi(merged_index, zone());
  AddPhi(phi);
  return phi;
}


HSimulate* HBasicBlock::CreateSimulate(BailoutId ast_id,
                                       RemovableSimulate removable) {
  DCHECK(HasEnvironment());
  HEnvironment* environment = last_environment();
  DCHECK(ast_id.IsNone() || ast_id == BailoutId::StubEntry());

  int push_count = environment->push_count();
  int pop_count = environment->pop_count();

  HSimulate* instr =
      new(zone()) HSimulate(ast_id, pop_count, zone(), removable);
#ifdef DEBUG
  instr->set_closure(environment->closure());
#endif
  // Order of pushed values: newest (top of stack) first. This allows
  // HSimulate::MergeWith() to easily append additional pushed values
  // that are older (from further down the stack).
  for (int i = 0; i < push_count; ++i) {
    instr->AddPushedValue(environment->ExpressionStackAt(i));
  }
  for (GrowableBitVector::Iterator it(environment->assigned_variables(),
                                      zone());
       !it.Done();
       it.Advance()) {
    int index = it.Current();
    instr->AddAssignedValue(index, environment->Lookup(index));
  }
  environment->ClearHistory();
  return instr;
}


void HBasicBlock::Finish(HControlInstruction* end, SourcePosition position) {
  DCHECK(!IsFinished());
  AddInstruction(end, position);
  end_ = end;
  for (HSuccessorIterator it(end); !it.Done(); it.Advance()) {
    it.Current()->RegisterPredecessor(this);
  }
}


void HBasicBlock::Goto(HBasicBlock* block, SourcePosition position,
                       bool add_simulate) {
  if (add_simulate) AddNewSimulate(BailoutId::None(), position);
  HGoto* instr = new(zone()) HGoto(block);
  Finish(instr, position);
}


void HBasicBlock::SetInitialEnvironment(HEnvironment* env) {
  DCHECK(!HasEnvironment());
  DCHECK(first() == NULL);
  UpdateEnvironment(env);
}


void HBasicBlock::UpdateEnvironment(HEnvironment* env) {
  last_environment_ = env;
  graph()->update_maximum_environment_size(env->first_expression_index());
}


void HBasicBlock::SetJoinId(BailoutId ast_id) {
  int length = predecessors_.length();
  DCHECK(length > 0);
  for (int i = 0; i < length; i++) {
    HBasicBlock* predecessor = predecessors_[i];
    DCHECK(predecessor->end()->IsGoto());
    HSimulate* simulate = HSimulate::cast(predecessor->end()->previous());
    simulate->set_ast_id(ast_id);
    predecessor->last_environment()->set_ast_id(ast_id);
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


bool HBasicBlock::EqualToOrDominates(HBasicBlock* other) const {
  if (this == other) return true;
  return Dominates(other);
}


int HBasicBlock::LoopNestingDepth() const {
  const HBasicBlock* current = this;
  int result  = (current->IsLoopHeader()) ? 1 : 0;
  while (current->parent_loop_header() != NULL) {
    current = current->parent_loop_header();
    result++;
  }
  return result;
}


void HBasicBlock::MarkSuccEdgeUnreachable(int succ) {
  DCHECK(IsFinished());
  HBasicBlock* succ_block = end()->SuccessorAt(succ);

  DCHECK(succ_block->predecessors()->length() == 1);
  succ_block->MarkUnreachable();
}


void HBasicBlock::RegisterPredecessor(HBasicBlock* pred) {
  if (HasPredecessor()) {
    // Only loop header blocks can have a predecessor added after
    // instructions have been added to the block (they have phis for all
    // values in the environment, these phis may be eliminated later).
    DCHECK(IsLoopHeader() || first_ == NULL);
    HEnvironment* incoming_env = pred->last_environment();
    if (IsLoopHeader()) {
      DCHECK_EQ(phis()->length(), incoming_env->length());
      for (int i = 0; i < phis_.length(); ++i) {
        phis_[i]->AddInput(incoming_env->values()->at(i));
      }
    } else {
      last_environment()->AddIncomingEdge(this, pred->last_environment());
    }
  } else if (!HasEnvironment() && !IsFinished()) {
    DCHECK(!IsLoopHeader());
    SetInitialEnvironment(pred->last_environment()->Copy());
  }

  predecessors_.Add(pred, zone());
}


void HBasicBlock::AddDominatedBlock(HBasicBlock* block) {
  DCHECK(!dominated_blocks_.Contains(block));
  // Keep the list of dominated blocks sorted such that if there is two
  // succeeding block in this list, the predecessor is before the successor.
  int index = 0;
  while (index < dominated_blocks_.length() &&
         dominated_blocks_[index]->block_id() < block->block_id()) {
    ++index;
  }
  dominated_blocks_.InsertAt(index, block, zone());
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
      DCHECK(first != NULL && second != NULL);
    }

    if (dominator_ != first) {
      DCHECK(dominator_->dominated_blocks_.Contains(this));
      dominator_->dominated_blocks_.RemoveElement(this);
      dominator_ = first;
      first->AddDominatedBlock(this);
    }
  }
}


void HBasicBlock::AssignLoopSuccessorDominators() {
  // Mark blocks that dominate all subsequent reachable blocks inside their
  // loop. Exploit the fact that blocks are sorted in reverse post order. When
  // the loop is visited in increasing block id order, if the number of
  // non-loop-exiting successor edges at the dominator_candidate block doesn't
  // exceed the number of previously encountered predecessor edges, there is no
  // path from the loop header to any block with higher id that doesn't go
  // through the dominator_candidate block. In this case, the
  // dominator_candidate block is guaranteed to dominate all blocks reachable
  // from it with higher ids.
  HBasicBlock* last = loop_information()->GetLastBackEdge();
  int outstanding_successors = 1;  // one edge from the pre-header
  // Header always dominates everything.
  MarkAsLoopSuccessorDominator();
  for (int j = block_id(); j <= last->block_id(); ++j) {
    HBasicBlock* dominator_candidate = graph_->blocks()->at(j);
    for (HPredecessorIterator it(dominator_candidate); !it.Done();
         it.Advance()) {
      HBasicBlock* predecessor = it.Current();
      // Don't count back edges.
      if (predecessor->block_id() < dominator_candidate->block_id()) {
        outstanding_successors--;
      }
    }

    // If more successors than predecessors have been seen in the loop up to
    // now, it's not possible to guarantee that the current block dominates
    // all of the blocks with higher IDs. In this case, assume conservatively
    // that those paths through loop that don't go through the current block
    // contain all of the loop's dependencies. Also be careful to record
    // dominator information about the current loop that's being processed,
    // and not nested loops, which will be processed when
    // AssignLoopSuccessorDominators gets called on their header.
    DCHECK(outstanding_successors >= 0);
    HBasicBlock* parent_loop_header = dominator_candidate->parent_loop_header();
    if (outstanding_successors == 0 &&
        (parent_loop_header == this && !dominator_candidate->IsLoopHeader())) {
      dominator_candidate->MarkAsLoopSuccessorDominator();
    }
    HControlInstruction* end = dominator_candidate->end();
    for (HSuccessorIterator it(end); !it.Done(); it.Advance()) {
      HBasicBlock* successor = it.Current();
      // Only count successors that remain inside the loop and don't loop back
      // to a loop header.
      if (successor->block_id() > dominator_candidate->block_id() &&
          successor->block_id() <= last->block_id()) {
        // Backwards edges must land on loop headers.
        DCHECK(successor->block_id() > dominator_candidate->block_id() ||
               successor->IsLoopHeader());
        outstanding_successors++;
      }
    }
  }
}


int HBasicBlock::PredecessorIndexOf(HBasicBlock* predecessor) const {
  for (int i = 0; i < predecessors_.length(); ++i) {
    if (predecessors_[i] == predecessor) return i;
  }
  UNREACHABLE();
}


#ifdef DEBUG
void HBasicBlock::Verify() {
  // Check that every block is finished.
  DCHECK(IsFinished());
  DCHECK(block_id() >= 0);

  // Check that the incoming edges are in edge split form.
  if (predecessors_.length() > 1) {
    for (int i = 0; i < predecessors_.length(); ++i) {
      DCHECK(predecessors_[i]->end()->SecondSuccessor() == NULL);
    }
  }
}
#endif


void HLoopInformation::RegisterBackEdge(HBasicBlock* block) {
  this->back_edges_.Add(block, block->zone());
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
    blocks_.Add(block, block->zone());
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
        stack_(16, entry_block->zone()),
        reachable_(block_count, entry_block->zone()),
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
      stack_.Add(block, block->zone());
      visited_count_++;
    }
  }

  void Analyze() {
    while (!stack_.is_empty()) {
      HControlInstruction* end = stack_.RemoveLast()->end();
      for (HSuccessorIterator it(end); !it.Done(); it.Advance()) {
        PushBlock(it.Current());
      }
    }
  }

  int visited_count_;
  ZoneList<HBasicBlock*> stack_;
  BitVector reachable_;
  HBasicBlock* dont_visit_;
};


void HGraph::Verify(bool do_full_verify) const {
  base::LockGuard<base::Mutex> guard(isolate()->heap()->relocation_mutex());
  AllowHandleDereference allow_deref;
  AllowDeferredHandleDereference allow_deferred_deref;
  for (int i = 0; i < blocks_.length(); i++) {
    HBasicBlock* block = blocks_.at(i);

    block->Verify();

    // Check that every block contains at least one node and that only the last
    // node is a control instruction.
    HInstruction* current = block->first();
    DCHECK(current != NULL && current->IsBlockEntry());
    while (current != NULL) {
      DCHECK((current->next() == NULL) == current->IsControlInstruction());
      DCHECK(current->block() == block);
      current->Verify();
      current = current->next();
    }

    // Check that successors are correctly set.
    HBasicBlock* first = block->end()->FirstSuccessor();
    HBasicBlock* second = block->end()->SecondSuccessor();
    DCHECK(second == NULL || first != NULL);

    // Check that the predecessor array is correct.
    if (first != NULL) {
      DCHECK(first->predecessors()->Contains(block));
      if (second != NULL) {
        DCHECK(second->predecessors()->Contains(block));
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
      BailoutId id =
          block->predecessors()->first()->last_environment()->ast_id();
      for (int k = 0; k < block->predecessors()->length(); k++) {
        HBasicBlock* predecessor = block->predecessors()->at(k);
        DCHECK(predecessor->end()->IsGoto() ||
               predecessor->end()->IsDeoptimize());
        DCHECK(predecessor->last_environment()->ast_id() == id);
      }
    }
  }

  // Check special property of first block to have no predecessors.
  DCHECK(blocks_.at(0)->predecessors()->is_empty());

  if (do_full_verify) {
    // Check that the graph is fully connected.
    ReachabilityAnalyzer analyzer(entry_block_, blocks_.length(), NULL);
    DCHECK(analyzer.visited_count() == blocks_.length());

    // Check that entry block dominator is NULL.
    DCHECK(entry_block_->dominator() == NULL);

    // Check dominators.
    for (int i = 0; i < blocks_.length(); ++i) {
      HBasicBlock* block = blocks_.at(i);
      if (block->dominator() == NULL) {
        // Only start block may have no dominator assigned to.
        DCHECK(i == 0);
      } else {
        // Assert that block is unreachable if dominator must not be visited.
        ReachabilityAnalyzer dominator_analyzer(entry_block_,
                                                blocks_.length(),
                                                block->dominator());
        DCHECK(!dominator_analyzer.reachable()->Contains(block->block_id()));
      }
    }
  }
}

#endif


HConstant* HGraph::GetConstant(SetOncePointer<HConstant>* pointer,
                               int32_t value) {
  if (!pointer->is_set()) {
    // Can't pass GetInvalidContext() to HConstant::New, because that will
    // recursively call GetConstant
    HConstant* constant = HConstant::New(isolate(), zone(), NULL, value);
    constant->InsertAfter(entry_block()->first());
    pointer->set(constant);
    return constant;
  }
  return ReinsertConstantIfNecessary(pointer->get());
}


HConstant* HGraph::ReinsertConstantIfNecessary(HConstant* constant) {
  if (!constant->IsLinked()) {
    // The constant was removed from the graph. Reinsert.
    constant->ClearFlag(HValue::kIsDead);
    constant->InsertAfter(entry_block()->first());
  }
  return constant;
}


HConstant* HGraph::GetConstant0() {
  return GetConstant(&constant_0_, 0);
}


HConstant* HGraph::GetConstant1() {
  return GetConstant(&constant_1_, 1);
}


HConstant* HGraph::GetConstantMinus1() {
  return GetConstant(&constant_minus1_, -1);
}


HConstant* HGraph::GetConstantBool(bool value) {
  return value ? GetConstantTrue() : GetConstantFalse();
}

#define DEFINE_GET_CONSTANT(Name, name, constant, type, htype, boolean_value, \
                            undetectable)                                     \
  HConstant* HGraph::GetConstant##Name() {                                    \
    if (!constant_##name##_.is_set()) {                                       \
      HConstant* constant = new (zone()) HConstant(                           \
          Unique<Object>::CreateImmovable(isolate()->factory()->constant()),  \
          Unique<Map>::CreateImmovable(isolate()->factory()->type##_map()),   \
          false, Representation::Tagged(), htype, true, boolean_value,        \
          undetectable, ODDBALL_TYPE);                                        \
      constant->InsertAfter(entry_block()->first());                          \
      constant_##name##_.set(constant);                                       \
    }                                                                         \
    return ReinsertConstantIfNecessary(constant_##name##_.get());             \
  }

DEFINE_GET_CONSTANT(Undefined, undefined, undefined_value, undefined,
                    HType::Undefined(), false, true)
DEFINE_GET_CONSTANT(True, true, true_value, boolean, HType::Boolean(), true,
                    false)
DEFINE_GET_CONSTANT(False, false, false_value, boolean, HType::Boolean(), false,
                    false)
DEFINE_GET_CONSTANT(Hole, the_hole, the_hole_value, the_hole, HType::None(),
                    false, false)
DEFINE_GET_CONSTANT(Null, null, null_value, null, HType::Null(), false, true)
DEFINE_GET_CONSTANT(OptimizedOut, optimized_out, optimized_out, optimized_out,
                    HType::None(), false, false)

#undef DEFINE_GET_CONSTANT

#define DEFINE_IS_CONSTANT(Name, name)                                         \
bool HGraph::IsConstant##Name(HConstant* constant) {                           \
  return constant_##name##_.is_set() && constant == constant_##name##_.get();  \
}
DEFINE_IS_CONSTANT(Undefined, undefined)
DEFINE_IS_CONSTANT(0, 0)
DEFINE_IS_CONSTANT(1, 1)
DEFINE_IS_CONSTANT(Minus1, minus1)
DEFINE_IS_CONSTANT(True, true)
DEFINE_IS_CONSTANT(False, false)
DEFINE_IS_CONSTANT(Hole, the_hole)
DEFINE_IS_CONSTANT(Null, null)

#undef DEFINE_IS_CONSTANT


HConstant* HGraph::GetInvalidContext() {
  return GetConstant(&constant_invalid_context_, 0xFFFFC0C7);
}


bool HGraph::IsStandardConstant(HConstant* constant) {
  if (IsConstantUndefined(constant)) return true;
  if (IsConstant0(constant)) return true;
  if (IsConstant1(constant)) return true;
  if (IsConstantMinus1(constant)) return true;
  if (IsConstantTrue(constant)) return true;
  if (IsConstantFalse(constant)) return true;
  if (IsConstantHole(constant)) return true;
  if (IsConstantNull(constant)) return true;
  return false;
}


HGraphBuilder::IfBuilder::IfBuilder() : builder_(NULL), needs_compare_(true) {}


HGraphBuilder::IfBuilder::IfBuilder(HGraphBuilder* builder)
    : needs_compare_(true) {
  Initialize(builder);
}


HGraphBuilder::IfBuilder::IfBuilder(HGraphBuilder* builder,
                                    HIfContinuation* continuation)
    : needs_compare_(false), first_true_block_(NULL), first_false_block_(NULL) {
  InitializeDontCreateBlocks(builder);
  continuation->Continue(&first_true_block_, &first_false_block_);
}


void HGraphBuilder::IfBuilder::InitializeDontCreateBlocks(
    HGraphBuilder* builder) {
  builder_ = builder;
  finished_ = false;
  did_then_ = false;
  did_else_ = false;
  did_else_if_ = false;
  did_and_ = false;
  did_or_ = false;
  captured_ = false;
  pending_merge_block_ = false;
  split_edge_merge_block_ = NULL;
  merge_at_join_blocks_ = NULL;
  normal_merge_at_join_block_count_ = 0;
  deopt_merge_at_join_block_count_ = 0;
}


void HGraphBuilder::IfBuilder::Initialize(HGraphBuilder* builder) {
  InitializeDontCreateBlocks(builder);
  HEnvironment* env = builder->environment();
  first_true_block_ = builder->CreateBasicBlock(env->Copy());
  first_false_block_ = builder->CreateBasicBlock(env->Copy());
}


HControlInstruction* HGraphBuilder::IfBuilder::AddCompare(
    HControlInstruction* compare) {
  DCHECK(did_then_ == did_else_);
  if (did_else_) {
    // Handle if-then-elseif
    did_else_if_ = true;
    did_else_ = false;
    did_then_ = false;
    did_and_ = false;
    did_or_ = false;
    pending_merge_block_ = false;
    split_edge_merge_block_ = NULL;
    HEnvironment* env = builder()->environment();
    first_true_block_ = builder()->CreateBasicBlock(env->Copy());
    first_false_block_ = builder()->CreateBasicBlock(env->Copy());
  }
  if (split_edge_merge_block_ != NULL) {
    HEnvironment* env = first_false_block_->last_environment();
    HBasicBlock* split_edge = builder()->CreateBasicBlock(env->Copy());
    if (did_or_) {
      compare->SetSuccessorAt(0, split_edge);
      compare->SetSuccessorAt(1, first_false_block_);
    } else {
      compare->SetSuccessorAt(0, first_true_block_);
      compare->SetSuccessorAt(1, split_edge);
    }
    builder()->GotoNoSimulate(split_edge, split_edge_merge_block_);
  } else {
    compare->SetSuccessorAt(0, first_true_block_);
    compare->SetSuccessorAt(1, first_false_block_);
  }
  builder()->FinishCurrentBlock(compare);
  needs_compare_ = false;
  return compare;
}


void HGraphBuilder::IfBuilder::Or() {
  DCHECK(!needs_compare_);
  DCHECK(!did_and_);
  did_or_ = true;
  HEnvironment* env = first_false_block_->last_environment();
  if (split_edge_merge_block_ == NULL) {
    split_edge_merge_block_ = builder()->CreateBasicBlock(env->Copy());
    builder()->GotoNoSimulate(first_true_block_, split_edge_merge_block_);
    first_true_block_ = split_edge_merge_block_;
  }
  builder()->set_current_block(first_false_block_);
  first_false_block_ = builder()->CreateBasicBlock(env->Copy());
}


void HGraphBuilder::IfBuilder::And() {
  DCHECK(!needs_compare_);
  DCHECK(!did_or_);
  did_and_ = true;
  HEnvironment* env = first_false_block_->last_environment();
  if (split_edge_merge_block_ == NULL) {
    split_edge_merge_block_ = builder()->CreateBasicBlock(env->Copy());
    builder()->GotoNoSimulate(first_false_block_, split_edge_merge_block_);
    first_false_block_ = split_edge_merge_block_;
  }
  builder()->set_current_block(first_true_block_);
  first_true_block_ = builder()->CreateBasicBlock(env->Copy());
}


void HGraphBuilder::IfBuilder::CaptureContinuation(
    HIfContinuation* continuation) {
  DCHECK(!did_else_if_);
  DCHECK(!finished_);
  DCHECK(!captured_);

  HBasicBlock* true_block = NULL;
  HBasicBlock* false_block = NULL;
  Finish(&true_block, &false_block);
  DCHECK(true_block != NULL);
  DCHECK(false_block != NULL);
  continuation->Capture(true_block, false_block);
  captured_ = true;
  builder()->set_current_block(NULL);
  End();
}


void HGraphBuilder::IfBuilder::JoinContinuation(HIfContinuation* continuation) {
  DCHECK(!did_else_if_);
  DCHECK(!finished_);
  DCHECK(!captured_);
  HBasicBlock* true_block = NULL;
  HBasicBlock* false_block = NULL;
  Finish(&true_block, &false_block);
  merge_at_join_blocks_ = NULL;
  if (true_block != NULL && !true_block->IsFinished()) {
    DCHECK(continuation->IsTrueReachable());
    builder()->GotoNoSimulate(true_block, continuation->true_branch());
  }
  if (false_block != NULL && !false_block->IsFinished()) {
    DCHECK(continuation->IsFalseReachable());
    builder()->GotoNoSimulate(false_block, continuation->false_branch());
  }
  captured_ = true;
  End();
}


void HGraphBuilder::IfBuilder::Then() {
  DCHECK(!captured_);
  DCHECK(!finished_);
  did_then_ = true;
  if (needs_compare_) {
    // Handle if's without any expressions, they jump directly to the "else"
    // branch. However, we must pretend that the "then" branch is reachable,
    // so that the graph builder visits it and sees any live range extending
    // constructs within it.
    HConstant* constant_false = builder()->graph()->GetConstantFalse();
    ToBooleanHints boolean_type = ToBooleanHint::kBoolean;
    HBranch* branch = builder()->New<HBranch>(
        constant_false, boolean_type, first_true_block_, first_false_block_);
    builder()->FinishCurrentBlock(branch);
  }
  builder()->set_current_block(first_true_block_);
  pending_merge_block_ = true;
}


void HGraphBuilder::IfBuilder::Else() {
  DCHECK(did_then_);
  DCHECK(!captured_);
  DCHECK(!finished_);
  AddMergeAtJoinBlock(false);
  builder()->set_current_block(first_false_block_);
  pending_merge_block_ = true;
  did_else_ = true;
}

void HGraphBuilder::IfBuilder::Deopt(DeoptimizeReason reason) {
  DCHECK(did_then_);
  builder()->Add<HDeoptimize>(reason, Deoptimizer::EAGER);
  AddMergeAtJoinBlock(true);
}


void HGraphBuilder::IfBuilder::Return(HValue* value) {
  HValue* parameter_count = builder()->graph()->GetConstantMinus1();
  builder()->FinishExitCurrentBlock(
      builder()->New<HReturn>(value, parameter_count));
  AddMergeAtJoinBlock(false);
}


void HGraphBuilder::IfBuilder::AddMergeAtJoinBlock(bool deopt) {
  if (!pending_merge_block_) return;
  HBasicBlock* block = builder()->current_block();
  DCHECK(block == NULL || !block->IsFinished());
  MergeAtJoinBlock* record = new (builder()->zone())
      MergeAtJoinBlock(block, deopt, merge_at_join_blocks_);
  merge_at_join_blocks_ = record;
  if (block != NULL) {
    DCHECK(block->end() == NULL);
    if (deopt) {
      normal_merge_at_join_block_count_++;
    } else {
      deopt_merge_at_join_block_count_++;
    }
  }
  builder()->set_current_block(NULL);
  pending_merge_block_ = false;
}


void HGraphBuilder::IfBuilder::Finish() {
  DCHECK(!finished_);
  if (!did_then_) {
    Then();
  }
  AddMergeAtJoinBlock(false);
  if (!did_else_) {
    Else();
    AddMergeAtJoinBlock(false);
  }
  finished_ = true;
}


void HGraphBuilder::IfBuilder::Finish(HBasicBlock** then_continuation,
                                      HBasicBlock** else_continuation) {
  Finish();

  MergeAtJoinBlock* else_record = merge_at_join_blocks_;
  if (else_continuation != NULL) {
    *else_continuation = else_record->block_;
  }
  MergeAtJoinBlock* then_record = else_record->next_;
  if (then_continuation != NULL) {
    *then_continuation = then_record->block_;
  }
  DCHECK(then_record->next_ == NULL);
}


void HGraphBuilder::IfBuilder::EndUnreachable() {
  if (captured_) return;
  Finish();
  builder()->set_current_block(nullptr);
}


void HGraphBuilder::IfBuilder::End() {
  if (captured_) return;
  Finish();

  int total_merged_blocks = normal_merge_at_join_block_count_ +
    deopt_merge_at_join_block_count_;
  DCHECK(total_merged_blocks >= 1);
  HBasicBlock* merge_block =
      total_merged_blocks == 1 ? NULL : builder()->graph()->CreateBasicBlock();

  // Merge non-deopt blocks first to ensure environment has right size for
  // padding.
  MergeAtJoinBlock* current = merge_at_join_blocks_;
  while (current != NULL) {
    if (!current->deopt_ && current->block_ != NULL) {
      // If there is only one block that makes it through to the end of the
      // if, then just set it as the current block and continue rather then
      // creating an unnecessary merge block.
      if (total_merged_blocks == 1) {
        builder()->set_current_block(current->block_);
        return;
      }
      builder()->GotoNoSimulate(current->block_, merge_block);
    }
    current = current->next_;
  }

  // Merge deopt blocks, padding when necessary.
  current = merge_at_join_blocks_;
  while (current != NULL) {
    if (current->deopt_ && current->block_ != NULL) {
      current->block_->FinishExit(
          HAbnormalExit::New(builder()->isolate(), builder()->zone(), NULL),
          SourcePosition::Unknown());
    }
    current = current->next_;
  }
  builder()->set_current_block(merge_block);
}


HGraphBuilder::LoopBuilder::LoopBuilder(HGraphBuilder* builder) {
  Initialize(builder, NULL, kWhileTrue, NULL);
}


HGraphBuilder::LoopBuilder::LoopBuilder(HGraphBuilder* builder, HValue* context,
                                        LoopBuilder::Direction direction) {
  Initialize(builder, context, direction, builder->graph()->GetConstant1());
}


HGraphBuilder::LoopBuilder::LoopBuilder(HGraphBuilder* builder, HValue* context,
                                        LoopBuilder::Direction direction,
                                        HValue* increment_amount) {
  Initialize(builder, context, direction, increment_amount);
  increment_amount_ = increment_amount;
}


void HGraphBuilder::LoopBuilder::Initialize(HGraphBuilder* builder,
                                            HValue* context,
                                            Direction direction,
                                            HValue* increment_amount) {
  builder_ = builder;
  context_ = context;
  direction_ = direction;
  increment_amount_ = increment_amount;

  finished_ = false;
  header_block_ = builder->CreateLoopHeaderBlock();
  body_block_ = NULL;
  exit_block_ = NULL;
  exit_trampoline_block_ = NULL;
}


HValue* HGraphBuilder::LoopBuilder::BeginBody(
    HValue* initial,
    HValue* terminating,
    Token::Value token) {
  DCHECK(direction_ != kWhileTrue);
  HEnvironment* env = builder_->environment();
  phi_ = header_block_->AddNewPhi(env->values()->length());
  phi_->AddInput(initial);
  env->Push(initial);
  builder_->GotoNoSimulate(header_block_);

  HEnvironment* body_env = env->Copy();
  HEnvironment* exit_env = env->Copy();
  // Remove the phi from the expression stack
  body_env->Pop();
  exit_env->Pop();
  body_block_ = builder_->CreateBasicBlock(body_env);
  exit_block_ = builder_->CreateBasicBlock(exit_env);

  builder_->set_current_block(header_block_);
  env->Pop();
  builder_->FinishCurrentBlock(builder_->New<HCompareNumericAndBranch>(
          phi_, terminating, token, body_block_, exit_block_));

  builder_->set_current_block(body_block_);
  if (direction_ == kPreIncrement || direction_ == kPreDecrement) {
    Isolate* isolate = builder_->isolate();
    HValue* one = builder_->graph()->GetConstant1();
    if (direction_ == kPreIncrement) {
      increment_ = HAdd::New(isolate, zone(), context_, phi_, one);
    } else {
      increment_ = HSub::New(isolate, zone(), context_, phi_, one);
    }
    increment_->ClearFlag(HValue::kCanOverflow);
    builder_->AddInstruction(increment_);
    return increment_;
  } else {
    return phi_;
  }
}


void HGraphBuilder::LoopBuilder::BeginBody(int drop_count) {
  DCHECK(direction_ == kWhileTrue);
  HEnvironment* env = builder_->environment();
  builder_->GotoNoSimulate(header_block_);
  builder_->set_current_block(header_block_);
  env->Drop(drop_count);
}


void HGraphBuilder::LoopBuilder::Break() {
  if (exit_trampoline_block_ == NULL) {
    // Its the first time we saw a break.
    if (direction_ == kWhileTrue) {
      HEnvironment* env = builder_->environment()->Copy();
      exit_trampoline_block_ = builder_->CreateBasicBlock(env);
    } else {
      HEnvironment* env = exit_block_->last_environment()->Copy();
      exit_trampoline_block_ = builder_->CreateBasicBlock(env);
      builder_->GotoNoSimulate(exit_block_, exit_trampoline_block_);
    }
  }

  builder_->GotoNoSimulate(exit_trampoline_block_);
  builder_->set_current_block(NULL);
}


void HGraphBuilder::LoopBuilder::EndBody() {
  DCHECK(!finished_);

  if (direction_ == kPostIncrement || direction_ == kPostDecrement) {
    Isolate* isolate = builder_->isolate();
    if (direction_ == kPostIncrement) {
      increment_ =
          HAdd::New(isolate, zone(), context_, phi_, increment_amount_);
    } else {
      increment_ =
          HSub::New(isolate, zone(), context_, phi_, increment_amount_);
    }
    increment_->ClearFlag(HValue::kCanOverflow);
    builder_->AddInstruction(increment_);
  }

  if (direction_ != kWhileTrue) {
    // Push the new increment value on the expression stack to merge into
    // the phi.
    builder_->environment()->Push(increment_);
  }
  HBasicBlock* last_block = builder_->current_block();
  builder_->GotoNoSimulate(last_block, header_block_);
  header_block_->loop_information()->RegisterBackEdge(last_block);

  if (exit_trampoline_block_ != NULL) {
    builder_->set_current_block(exit_trampoline_block_);
  } else {
    builder_->set_current_block(exit_block_);
  }
  finished_ = true;
}


HGraph* HGraphBuilder::CreateGraph() {
  DCHECK(!FLAG_minimal);
  graph_ = new (zone()) HGraph(info_, descriptor_);
  if (FLAG_hydrogen_stats) isolate()->GetHStatistics()->Initialize(info_);
  CompilationPhase phase("H_Block building", info_);
  set_current_block(graph()->entry_block());
  if (!BuildGraph()) return NULL;
  graph()->FinalizeUniqueness();
  return graph_;
}


HInstruction* HGraphBuilder::AddInstruction(HInstruction* instr) {
  DCHECK(current_block() != NULL);
  DCHECK(!FLAG_hydrogen_track_positions || position_.IsKnown() ||
         !info_->IsOptimizing());
  current_block()->AddInstruction(instr, source_position());
  if (graph()->IsInsideNoSideEffectsScope()) {
    instr->SetFlag(HValue::kHasNoObservableSideEffects);
  }
  return instr;
}


void HGraphBuilder::FinishCurrentBlock(HControlInstruction* last) {
  DCHECK(!FLAG_hydrogen_track_positions || !info_->IsOptimizing() ||
         position_.IsKnown());
  current_block()->Finish(last, source_position());
  if (last->IsReturn() || last->IsAbnormalExit()) {
    set_current_block(NULL);
  }
}


void HGraphBuilder::FinishExitCurrentBlock(HControlInstruction* instruction) {
  DCHECK(!FLAG_hydrogen_track_positions || !info_->IsOptimizing() ||
         position_.IsKnown());
  current_block()->FinishExit(instruction, source_position());
  if (instruction->IsReturn() || instruction->IsAbnormalExit()) {
    set_current_block(NULL);
  }
}


void HGraphBuilder::AddIncrementCounter(StatsCounter* counter) {
  if (FLAG_native_code_counters && counter->Enabled()) {
    HValue* reference = Add<HConstant>(ExternalReference(counter));
    HValue* old_value =
        Add<HLoadNamedField>(reference, nullptr, HObjectAccess::ForCounter());
    HValue* new_value = AddUncasted<HAdd>(old_value, graph()->GetConstant1());
    new_value->ClearFlag(HValue::kCanOverflow);  // Ignore counter overflow
    Add<HStoreNamedField>(reference, HObjectAccess::ForCounter(),
                          new_value, STORE_TO_INITIALIZED_ENTRY);
  }
}


void HGraphBuilder::AddSimulate(BailoutId id,
                                RemovableSimulate removable) {
  DCHECK(current_block() != NULL);
  DCHECK(!graph()->IsInsideNoSideEffectsScope());
  current_block()->AddNewSimulate(id, source_position(), removable);
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


HValue* HGraphBuilder::BuildGetElementsKind(HValue* object) {
  HValue* map = Add<HLoadNamedField>(object, nullptr, HObjectAccess::ForMap());

  HValue* bit_field2 =
      Add<HLoadNamedField>(map, nullptr, HObjectAccess::ForMapBitField2());
  return BuildDecodeField<Map::ElementsKindBits>(bit_field2);
}


HValue* HGraphBuilder::BuildEnumLength(HValue* map) {
  NoObservableSideEffectsScope scope(this);
  HValue* bit_field3 =
      Add<HLoadNamedField>(map, nullptr, HObjectAccess::ForMapBitField3());
  return BuildDecodeField<Map::EnumLengthBits>(bit_field3);
}


HValue* HGraphBuilder::BuildCheckHeapObject(HValue* obj) {
  if (obj->type().IsHeapObject()) return obj;
  return Add<HCheckHeapObject>(obj);
}

void HGraphBuilder::FinishExitWithHardDeoptimization(DeoptimizeReason reason) {
  Add<HDeoptimize>(reason, Deoptimizer::EAGER);
  FinishExitCurrentBlock(New<HAbnormalExit>());
}


HValue* HGraphBuilder::BuildCheckString(HValue* string) {
  if (!string->type().IsString()) {
    DCHECK(!string->IsConstant() ||
           !HConstant::cast(string)->HasStringValue());
    BuildCheckHeapObject(string);
    return Add<HCheckInstanceType>(string, HCheckInstanceType::IS_STRING);
  }
  return string;
}

HValue* HGraphBuilder::BuildWrapReceiver(HValue* object, HValue* checked) {
  if (object->type().IsJSObject()) return object;
  HValue* function = checked->ActualValue();
  if (function->IsConstant() &&
      HConstant::cast(function)->handle(isolate())->IsJSFunction()) {
    Handle<JSFunction> f = Handle<JSFunction>::cast(
        HConstant::cast(function)->handle(isolate()));
    SharedFunctionInfo* shared = f->shared();
    if (is_strict(shared->language_mode()) || shared->native()) return object;
  }
  return Add<HWrapReceiver>(object, checked);
}


HValue* HGraphBuilder::BuildCheckAndGrowElementsCapacity(
    HValue* object, HValue* elements, ElementsKind kind, HValue* length,
    HValue* capacity, HValue* key) {
  HValue* max_gap = Add<HConstant>(static_cast<int32_t>(JSObject::kMaxGap));
  HValue* max_capacity = AddUncasted<HAdd>(capacity, max_gap);
  Add<HBoundsCheck>(key, max_capacity);

  HValue* new_capacity = BuildNewElementsCapacity(key);
  HValue* new_elements = BuildGrowElementsCapacity(object, elements, kind, kind,
                                                   length, new_capacity);
  return new_elements;
}


HValue* HGraphBuilder::BuildCheckForCapacityGrow(
    HValue* object,
    HValue* elements,
    ElementsKind kind,
    HValue* length,
    HValue* key,
    bool is_js_array,
    PropertyAccessType access_type) {
  IfBuilder length_checker(this);

  Token::Value token = IsHoleyElementsKind(kind) ? Token::GTE : Token::EQ;
  length_checker.If<HCompareNumericAndBranch>(key, length, token);

  length_checker.Then();

  HValue* current_capacity = AddLoadFixedArrayLength(elements);

  if (top_info()->IsStub()) {
    IfBuilder capacity_checker(this);
    capacity_checker.If<HCompareNumericAndBranch>(key, current_capacity,
                                                  Token::GTE);
    capacity_checker.Then();
    HValue* new_elements = BuildCheckAndGrowElementsCapacity(
        object, elements, kind, length, current_capacity, key);
    environment()->Push(new_elements);
    capacity_checker.Else();
    environment()->Push(elements);
    capacity_checker.End();
  } else {
    HValue* result = Add<HMaybeGrowElements>(
        object, elements, key, current_capacity, is_js_array, kind);
    environment()->Push(result);
  }

  if (is_js_array) {
    HValue* new_length = AddUncasted<HAdd>(key, graph_->GetConstant1());
    new_length->ClearFlag(HValue::kCanOverflow);

    Add<HStoreNamedField>(object, HObjectAccess::ForArrayLength(kind),
                          new_length);
  }

  if (access_type == STORE && kind == FAST_SMI_ELEMENTS) {
    HValue* checked_elements = environment()->Top();

    // Write zero to ensure that the new element is initialized with some smi.
    Add<HStoreKeyed>(checked_elements, key, graph()->GetConstant0(), nullptr,
                     kind);
  }

  length_checker.Else();
  Add<HBoundsCheck>(key, length);

  environment()->Push(elements);
  length_checker.End();

  return environment()->Pop();
}


HValue* HGraphBuilder::BuildCopyElementsOnWrite(HValue* object,
                                                HValue* elements,
                                                ElementsKind kind,
                                                HValue* length) {
  Factory* factory = isolate()->factory();

  IfBuilder cow_checker(this);

  cow_checker.If<HCompareMap>(elements, factory->fixed_cow_array_map());
  cow_checker.Then();

  HValue* capacity = AddLoadFixedArrayLength(elements);

  HValue* new_elements = BuildGrowElementsCapacity(object, elements, kind,
                                                   kind, length, capacity);

  environment()->Push(new_elements);

  cow_checker.Else();

  environment()->Push(elements);

  cow_checker.End();

  return environment()->Pop();
}

HValue* HGraphBuilder::BuildCreateIterResultObject(HValue* value,
                                                   HValue* done) {
  NoObservableSideEffectsScope scope(this);

  // Allocate the JSIteratorResult object.
  HValue* result =
      Add<HAllocate>(Add<HConstant>(JSIteratorResult::kSize), HType::JSObject(),
                     NOT_TENURED, JS_OBJECT_TYPE, graph()->GetConstant0());

  // Initialize the JSIteratorResult object.
  HValue* native_context = BuildGetNativeContext();
  HValue* map = Add<HLoadNamedField>(
      native_context, nullptr,
      HObjectAccess::ForContextSlot(Context::ITERATOR_RESULT_MAP_INDEX));
  Add<HStoreNamedField>(result, HObjectAccess::ForMap(), map);
  HValue* empty_fixed_array = Add<HLoadRoot>(Heap::kEmptyFixedArrayRootIndex);
  Add<HStoreNamedField>(result, HObjectAccess::ForPropertiesPointer(),
                        empty_fixed_array);
  Add<HStoreNamedField>(result, HObjectAccess::ForElementsPointer(),
                        empty_fixed_array);
  Add<HStoreNamedField>(result, HObjectAccess::ForObservableJSObjectOffset(
                                    JSIteratorResult::kValueOffset),
                        value);
  Add<HStoreNamedField>(result, HObjectAccess::ForObservableJSObjectOffset(
                                    JSIteratorResult::kDoneOffset),
                        done);
  STATIC_ASSERT(JSIteratorResult::kSize == 5 * kPointerSize);
  return result;
}


HValue* HGraphBuilder::BuildNumberToString(HValue* object, AstType* type) {
  NoObservableSideEffectsScope scope(this);

  // Convert constant numbers at compile time.
  if (object->IsConstant() && HConstant::cast(object)->HasNumberValue()) {
    Handle<Object> number = HConstant::cast(object)->handle(isolate());
    Handle<String> result = isolate()->factory()->NumberToString(number);
    return Add<HConstant>(result);
  }

  // Create a joinable continuation.
  HIfContinuation found(graph()->CreateBasicBlock(),
                        graph()->CreateBasicBlock());

  // Load the number string cache.
  HValue* number_string_cache =
      Add<HLoadRoot>(Heap::kNumberStringCacheRootIndex);

  // Make the hash mask from the length of the number string cache. It
  // contains two elements (number and string) for each cache entry.
  HValue* mask = AddLoadFixedArrayLength(number_string_cache);
  mask->set_type(HType::Smi());
  mask = AddUncasted<HSar>(mask, graph()->GetConstant1());
  mask = AddUncasted<HSub>(mask, graph()->GetConstant1());

  // Check whether object is a smi.
  IfBuilder if_objectissmi(this);
  if_objectissmi.If<HIsSmiAndBranch>(object);
  if_objectissmi.Then();
  {
    // Compute hash for smi similar to smi_get_hash().
    HValue* hash = AddUncasted<HBitwise>(Token::BIT_AND, object, mask);

    // Load the key.
    HValue* key_index = AddUncasted<HShl>(hash, graph()->GetConstant1());
    HValue* key = Add<HLoadKeyed>(number_string_cache, key_index, nullptr,
                                  nullptr, FAST_ELEMENTS, ALLOW_RETURN_HOLE);

    // Check if object == key.
    IfBuilder if_objectiskey(this);
    if_objectiskey.If<HCompareObjectEqAndBranch>(object, key);
    if_objectiskey.Then();
    {
      // Make the key_index available.
      Push(key_index);
    }
    if_objectiskey.JoinContinuation(&found);
  }
  if_objectissmi.Else();
  {
    if (type->Is(AstType::SignedSmall())) {
      if_objectissmi.Deopt(DeoptimizeReason::kExpectedSmi);
    } else {
      // Check if the object is a heap number.
      IfBuilder if_objectisnumber(this);
      HValue* objectisnumber = if_objectisnumber.If<HCompareMap>(
          object, isolate()->factory()->heap_number_map());
      if_objectisnumber.Then();
      {
        // Compute hash for heap number similar to double_get_hash().
        HValue* low = Add<HLoadNamedField>(
            object, objectisnumber,
            HObjectAccess::ForHeapNumberValueLowestBits());
        HValue* high = Add<HLoadNamedField>(
            object, objectisnumber,
            HObjectAccess::ForHeapNumberValueHighestBits());
        HValue* hash = AddUncasted<HBitwise>(Token::BIT_XOR, low, high);
        hash = AddUncasted<HBitwise>(Token::BIT_AND, hash, mask);

        // Load the key.
        HValue* key_index = AddUncasted<HShl>(hash, graph()->GetConstant1());
        HValue* key =
            Add<HLoadKeyed>(number_string_cache, key_index, nullptr, nullptr,
                            FAST_ELEMENTS, ALLOW_RETURN_HOLE);

        // Check if the key is a heap number and compare it with the object.
        IfBuilder if_keyisnotsmi(this);
        HValue* keyisnotsmi = if_keyisnotsmi.IfNot<HIsSmiAndBranch>(key);
        if_keyisnotsmi.Then();
        {
          IfBuilder if_keyisheapnumber(this);
          if_keyisheapnumber.If<HCompareMap>(
              key, isolate()->factory()->heap_number_map());
          if_keyisheapnumber.Then();
          {
            // Check if values of key and object match.
            IfBuilder if_keyeqobject(this);
            if_keyeqobject.If<HCompareNumericAndBranch>(
                Add<HLoadNamedField>(key, keyisnotsmi,
                                     HObjectAccess::ForHeapNumberValue()),
                Add<HLoadNamedField>(object, objectisnumber,
                                     HObjectAccess::ForHeapNumberValue()),
                Token::EQ);
            if_keyeqobject.Then();
            {
              // Make the key_index available.
              Push(key_index);
            }
            if_keyeqobject.JoinContinuation(&found);
          }
          if_keyisheapnumber.JoinContinuation(&found);
        }
        if_keyisnotsmi.JoinContinuation(&found);
      }
      if_objectisnumber.Else();
      {
        if (type->Is(AstType::Number())) {
          if_objectisnumber.Deopt(DeoptimizeReason::kExpectedHeapNumber);
        }
      }
      if_objectisnumber.JoinContinuation(&found);
    }
  }
  if_objectissmi.JoinContinuation(&found);

  // Check for cache hit.
  IfBuilder if_found(this, &found);
  if_found.Then();
  {
    // Count number to string operation in native code.
    AddIncrementCounter(isolate()->counters()->number_to_string_native());

    // Load the value in case of cache hit.
    HValue* key_index = Pop();
    HValue* value_index = AddUncasted<HAdd>(key_index, graph()->GetConstant1());
    Push(Add<HLoadKeyed>(number_string_cache, value_index, nullptr, nullptr,
                         FAST_ELEMENTS, ALLOW_RETURN_HOLE));
  }
  if_found.Else();
  {
    // Cache miss, fallback to runtime.
    Add<HPushArguments>(object);
    Push(Add<HCallRuntime>(
            Runtime::FunctionForId(Runtime::kNumberToStringSkipCache),
            1));
  }
  if_found.End();

  return Pop();
}

HValue* HGraphBuilder::BuildToNumber(HValue* input) {
  if (input->type().IsTaggedNumber() ||
      input->representation().IsSpecialization()) {
    return input;
  }
  Callable callable = Builtins::CallableFor(isolate(), Builtins::kToNumber);
  HValue* stub = Add<HConstant>(callable.code());
  HValue* values[] = {input};
  HCallWithDescriptor* instr = Add<HCallWithDescriptor>(
      stub, 0, callable.descriptor(), ArrayVector(values));
  instr->set_type(HType::TaggedNumber());
  return instr;
}


HValue* HGraphBuilder::BuildToObject(HValue* receiver) {
  NoObservableSideEffectsScope scope(this);

  // Create a joinable continuation.
  HIfContinuation wrap(graph()->CreateBasicBlock(),
                       graph()->CreateBasicBlock());

  // Determine the proper global constructor function required to wrap
  // {receiver} into a JSValue, unless {receiver} is already a {JSReceiver}, in
  // which case we just return it.  Deopts to Runtime::kToObject if {receiver}
  // is undefined or null.
  IfBuilder receiver_is_smi(this);
  receiver_is_smi.If<HIsSmiAndBranch>(receiver);
  receiver_is_smi.Then();
  {
    // Use global Number function.
    Push(Add<HConstant>(Context::NUMBER_FUNCTION_INDEX));
  }
  receiver_is_smi.Else();
  {
    // Determine {receiver} map and instance type.
    HValue* receiver_map =
        Add<HLoadNamedField>(receiver, nullptr, HObjectAccess::ForMap());
    HValue* receiver_instance_type = Add<HLoadNamedField>(
        receiver_map, nullptr, HObjectAccess::ForMapInstanceType());

    // First check whether {receiver} is already a spec object (fast case).
    IfBuilder receiver_is_not_spec_object(this);
    receiver_is_not_spec_object.If<HCompareNumericAndBranch>(
        receiver_instance_type, Add<HConstant>(FIRST_JS_RECEIVER_TYPE),
        Token::LT);
    receiver_is_not_spec_object.Then();
    {
      // Load the constructor function index from the {receiver} map.
      HValue* constructor_function_index = Add<HLoadNamedField>(
          receiver_map, nullptr,
          HObjectAccess::ForMapInObjectPropertiesOrConstructorFunctionIndex());

      // Check if {receiver} has a constructor (null and undefined have no
      // constructors, so we deoptimize to the runtime to throw an exception).
      IfBuilder constructor_function_index_is_invalid(this);
      constructor_function_index_is_invalid.If<HCompareNumericAndBranch>(
          constructor_function_index,
          Add<HConstant>(Map::kNoConstructorFunctionIndex), Token::EQ);
      constructor_function_index_is_invalid.ThenDeopt(
          DeoptimizeReason::kUndefinedOrNullInToObject);
      constructor_function_index_is_invalid.End();

      // Use the global constructor function.
      Push(constructor_function_index);
    }
    receiver_is_not_spec_object.JoinContinuation(&wrap);
  }
  receiver_is_smi.JoinContinuation(&wrap);

  // Wrap the receiver if necessary.
  IfBuilder if_wrap(this, &wrap);
  if_wrap.Then();
  {
    // Grab the constructor function index.
    HValue* constructor_index = Pop();

    // Load native context.
    HValue* native_context = BuildGetNativeContext();

    // Determine the initial map for the global constructor.
    HValue* constructor = Add<HLoadKeyed>(native_context, constructor_index,
                                          nullptr, nullptr, FAST_ELEMENTS);
    HValue* constructor_initial_map = Add<HLoadNamedField>(
        constructor, nullptr, HObjectAccess::ForPrototypeOrInitialMap());
    // Allocate and initialize a JSValue wrapper.
    HValue* value =
        BuildAllocate(Add<HConstant>(JSValue::kSize), HType::JSObject(),
                      JS_VALUE_TYPE, HAllocationMode());
    Add<HStoreNamedField>(value, HObjectAccess::ForMap(),
                          constructor_initial_map);
    HValue* empty_fixed_array = Add<HLoadRoot>(Heap::kEmptyFixedArrayRootIndex);
    Add<HStoreNamedField>(value, HObjectAccess::ForPropertiesPointer(),
                          empty_fixed_array);
    Add<HStoreNamedField>(value, HObjectAccess::ForElementsPointer(),
                          empty_fixed_array);
    Add<HStoreNamedField>(value, HObjectAccess::ForObservableJSObjectOffset(
                                     JSValue::kValueOffset),
                          receiver);
    Push(value);
  }
  if_wrap.Else();
  { Push(receiver); }
  if_wrap.End();
  return Pop();
}


HAllocate* HGraphBuilder::BuildAllocate(
    HValue* object_size,
    HType type,
    InstanceType instance_type,
    HAllocationMode allocation_mode) {
  // Compute the effective allocation size.
  HValue* size = object_size;
  if (allocation_mode.CreateAllocationMementos()) {
    size = AddUncasted<HAdd>(size, Add<HConstant>(AllocationMemento::kSize));
    size->ClearFlag(HValue::kCanOverflow);
  }

  // Perform the actual allocation.
  HAllocate* object = Add<HAllocate>(
      size, type, allocation_mode.GetPretenureMode(), instance_type,
      graph()->GetConstant0(), allocation_mode.feedback_site());

  // Setup the allocation memento.
  if (allocation_mode.CreateAllocationMementos()) {
    BuildCreateAllocationMemento(
        object, object_size, allocation_mode.current_site());
  }

  return object;
}


HValue* HGraphBuilder::BuildAddStringLengths(HValue* left_length,
                                             HValue* right_length) {
  // Compute the combined string length and check against max string length.
  HValue* length = AddUncasted<HAdd>(left_length, right_length);
  // Check that length <= kMaxLength <=> length < MaxLength + 1.
  HValue* max_length = Add<HConstant>(String::kMaxLength + 1);
  if (top_info()->IsStub() || !isolate()->IsStringLengthOverflowIntact()) {
    // This is a mitigation for crbug.com/627934; the real fix
    // will be to migrate the StringAddStub to TurboFan one day.
    IfBuilder if_invalid(this);
    if_invalid.If<HCompareNumericAndBranch>(length, max_length, Token::GT);
    if_invalid.Then();
    {
      Add<HCallRuntime>(
          Runtime::FunctionForId(Runtime::kThrowInvalidStringLength), 0);
    }
    if_invalid.End();
  } else {
    graph()->MarkDependsOnStringLengthOverflow();
    Add<HBoundsCheck>(length, max_length);
  }
  return length;
}


HValue* HGraphBuilder::BuildCreateConsString(
    HValue* length,
    HValue* left,
    HValue* right,
    HAllocationMode allocation_mode) {
  // Determine the string instance types.
  HInstruction* left_instance_type = AddLoadStringInstanceType(left);
  HInstruction* right_instance_type = AddLoadStringInstanceType(right);

  // Allocate the cons string object. HAllocate does not care whether we
  // pass CONS_STRING_TYPE or CONS_ONE_BYTE_STRING_TYPE here, so we just use
  // CONS_STRING_TYPE here. Below we decide whether the cons string is
  // one-byte or two-byte and set the appropriate map.
  DCHECK(HAllocate::CompatibleInstanceTypes(CONS_STRING_TYPE,
                                            CONS_ONE_BYTE_STRING_TYPE));
  HAllocate* result = BuildAllocate(Add<HConstant>(ConsString::kSize),
                                    HType::String(), CONS_STRING_TYPE,
                                    allocation_mode);

  // Compute intersection and difference of instance types.
  HValue* anded_instance_types = AddUncasted<HBitwise>(
      Token::BIT_AND, left_instance_type, right_instance_type);
  HValue* xored_instance_types = AddUncasted<HBitwise>(
      Token::BIT_XOR, left_instance_type, right_instance_type);

  // We create a one-byte cons string if
  // 1. both strings are one-byte, or
  // 2. at least one of the strings is two-byte, but happens to contain only
  //    one-byte characters.
  // To do this, we check
  // 1. if both strings are one-byte, or if the one-byte data hint is set in
  //    both strings, or
  // 2. if one of the strings has the one-byte data hint set and the other
  //    string is one-byte.
  IfBuilder if_onebyte(this);
  STATIC_ASSERT(kOneByteStringTag != 0);
  STATIC_ASSERT(kOneByteDataHintMask != 0);
  if_onebyte.If<HCompareNumericAndBranch>(
      AddUncasted<HBitwise>(
          Token::BIT_AND, anded_instance_types,
          Add<HConstant>(static_cast<int32_t>(
                  kStringEncodingMask | kOneByteDataHintMask))),
      graph()->GetConstant0(), Token::NE);
  if_onebyte.Or();
  STATIC_ASSERT(kOneByteStringTag != 0 &&
                kOneByteDataHintTag != 0 &&
                kOneByteDataHintTag != kOneByteStringTag);
  if_onebyte.If<HCompareNumericAndBranch>(
      AddUncasted<HBitwise>(
          Token::BIT_AND, xored_instance_types,
          Add<HConstant>(static_cast<int32_t>(
                  kOneByteStringTag | kOneByteDataHintTag))),
      Add<HConstant>(static_cast<int32_t>(
              kOneByteStringTag | kOneByteDataHintTag)), Token::EQ);
  if_onebyte.Then();
  {
    // We can safely skip the write barrier for storing the map here.
    Add<HStoreNamedField>(
        result, HObjectAccess::ForMap(),
        Add<HConstant>(isolate()->factory()->cons_one_byte_string_map()));
  }
  if_onebyte.Else();
  {
    // We can safely skip the write barrier for storing the map here.
    Add<HStoreNamedField>(
        result, HObjectAccess::ForMap(),
        Add<HConstant>(isolate()->factory()->cons_string_map()));
  }
  if_onebyte.End();

  // Initialize the cons string fields.
  Add<HStoreNamedField>(result, HObjectAccess::ForStringHashField(),
                        Add<HConstant>(String::kEmptyHashField));
  Add<HStoreNamedField>(result, HObjectAccess::ForStringLength(), length);
  Add<HStoreNamedField>(result, HObjectAccess::ForConsStringFirst(), left);
  Add<HStoreNamedField>(result, HObjectAccess::ForConsStringSecond(), right);

  // Count the native string addition.
  AddIncrementCounter(isolate()->counters()->string_add_native());

  return result;
}


void HGraphBuilder::BuildCopySeqStringChars(HValue* src,
                                            HValue* src_offset,
                                            String::Encoding src_encoding,
                                            HValue* dst,
                                            HValue* dst_offset,
                                            String::Encoding dst_encoding,
                                            HValue* length) {
  DCHECK(dst_encoding != String::ONE_BYTE_ENCODING ||
         src_encoding == String::ONE_BYTE_ENCODING);
  LoopBuilder loop(this, context(), LoopBuilder::kPostIncrement);
  HValue* index = loop.BeginBody(graph()->GetConstant0(), length, Token::LT);
  {
    HValue* src_index = AddUncasted<HAdd>(src_offset, index);
    HValue* value =
        AddUncasted<HSeqStringGetChar>(src_encoding, src, src_index);
    HValue* dst_index = AddUncasted<HAdd>(dst_offset, index);
    Add<HSeqStringSetChar>(dst_encoding, dst, dst_index, value);
  }
  loop.EndBody();
}


HValue* HGraphBuilder::BuildObjectSizeAlignment(
    HValue* unaligned_size, int header_size) {
  DCHECK((header_size & kObjectAlignmentMask) == 0);
  HValue* size = AddUncasted<HAdd>(
      unaligned_size, Add<HConstant>(static_cast<int32_t>(
          header_size + kObjectAlignmentMask)));
  size->ClearFlag(HValue::kCanOverflow);
  return AddUncasted<HBitwise>(
      Token::BIT_AND, size, Add<HConstant>(static_cast<int32_t>(
          ~kObjectAlignmentMask)));
}


HValue* HGraphBuilder::BuildUncheckedStringAdd(
    HValue* left,
    HValue* right,
    HAllocationMode allocation_mode) {
  // Determine the string lengths.
  HValue* left_length = AddLoadStringLength(left);
  HValue* right_length = AddLoadStringLength(right);

  // Compute the combined string length.
  HValue* length = BuildAddStringLengths(left_length, right_length);

  // Do some manual constant folding here.
  if (left_length->IsConstant()) {
    HConstant* c_left_length = HConstant::cast(left_length);
    DCHECK_NE(0, c_left_length->Integer32Value());
    if (c_left_length->Integer32Value() + 1 >= ConsString::kMinLength) {
      // The right string contains at least one character.
      return BuildCreateConsString(length, left, right, allocation_mode);
    }
  } else if (right_length->IsConstant()) {
    HConstant* c_right_length = HConstant::cast(right_length);
    DCHECK_NE(0, c_right_length->Integer32Value());
    if (c_right_length->Integer32Value() + 1 >= ConsString::kMinLength) {
      // The left string contains at least one character.
      return BuildCreateConsString(length, left, right, allocation_mode);
    }
  }

  // Check if we should create a cons string.
  IfBuilder if_createcons(this);
  if_createcons.If<HCompareNumericAndBranch>(
      length, Add<HConstant>(ConsString::kMinLength), Token::GTE);
  if_createcons.And();
  if_createcons.If<HCompareNumericAndBranch>(
      length, Add<HConstant>(ConsString::kMaxLength), Token::LTE);
  if_createcons.Then();
  {
    // Create a cons string.
    Push(BuildCreateConsString(length, left, right, allocation_mode));
  }
  if_createcons.Else();
  {
    // Determine the string instance types.
    HValue* left_instance_type = AddLoadStringInstanceType(left);
    HValue* right_instance_type = AddLoadStringInstanceType(right);

    // Compute union and difference of instance types.
    HValue* ored_instance_types = AddUncasted<HBitwise>(
        Token::BIT_OR, left_instance_type, right_instance_type);
    HValue* xored_instance_types = AddUncasted<HBitwise>(
        Token::BIT_XOR, left_instance_type, right_instance_type);

    // Check if both strings have the same encoding and both are
    // sequential.
    IfBuilder if_sameencodingandsequential(this);
    if_sameencodingandsequential.If<HCompareNumericAndBranch>(
        AddUncasted<HBitwise>(
            Token::BIT_AND, xored_instance_types,
            Add<HConstant>(static_cast<int32_t>(kStringEncodingMask))),
        graph()->GetConstant0(), Token::EQ);
    if_sameencodingandsequential.And();
    STATIC_ASSERT(kSeqStringTag == 0);
    if_sameencodingandsequential.If<HCompareNumericAndBranch>(
        AddUncasted<HBitwise>(
            Token::BIT_AND, ored_instance_types,
            Add<HConstant>(static_cast<int32_t>(kStringRepresentationMask))),
        graph()->GetConstant0(), Token::EQ);
    if_sameencodingandsequential.Then();
    {
      HConstant* string_map =
          Add<HConstant>(isolate()->factory()->string_map());
      HConstant* one_byte_string_map =
          Add<HConstant>(isolate()->factory()->one_byte_string_map());

      // Determine map and size depending on whether result is one-byte string.
      IfBuilder if_onebyte(this);
      STATIC_ASSERT(kOneByteStringTag != 0);
      if_onebyte.If<HCompareNumericAndBranch>(
          AddUncasted<HBitwise>(
              Token::BIT_AND, ored_instance_types,
              Add<HConstant>(static_cast<int32_t>(kStringEncodingMask))),
          graph()->GetConstant0(), Token::NE);
      if_onebyte.Then();
      {
        // Allocate sequential one-byte string object.
        Push(length);
        Push(one_byte_string_map);
      }
      if_onebyte.Else();
      {
        // Allocate sequential two-byte string object.
        HValue* size = AddUncasted<HShl>(length, graph()->GetConstant1());
        size->ClearFlag(HValue::kCanOverflow);
        size->SetFlag(HValue::kUint32);
        Push(size);
        Push(string_map);
      }
      if_onebyte.End();
      HValue* map = Pop();

      // Calculate the number of bytes needed for the characters in the
      // string while observing object alignment.
      STATIC_ASSERT((SeqString::kHeaderSize & kObjectAlignmentMask) == 0);
      HValue* size = BuildObjectSizeAlignment(Pop(), SeqString::kHeaderSize);

      IfBuilder if_size(this);
      if_size.If<HCompareNumericAndBranch>(
          size, Add<HConstant>(kMaxRegularHeapObjectSize), Token::LT);
      if_size.Then();
      {
        // Allocate the string object. HAllocate does not care whether we pass
        // STRING_TYPE or ONE_BYTE_STRING_TYPE here, so we just use STRING_TYPE.
        HAllocate* result =
            BuildAllocate(size, HType::String(), STRING_TYPE, allocation_mode);
        Add<HStoreNamedField>(result, HObjectAccess::ForMap(), map);

        // Initialize the string fields.
        Add<HStoreNamedField>(result, HObjectAccess::ForStringHashField(),
                              Add<HConstant>(String::kEmptyHashField));
        Add<HStoreNamedField>(result, HObjectAccess::ForStringLength(), length);

        // Copy characters to the result string.
        IfBuilder if_twobyte(this);
        if_twobyte.If<HCompareObjectEqAndBranch>(map, string_map);
        if_twobyte.Then();
        {
          // Copy characters from the left string.
          BuildCopySeqStringChars(
              left, graph()->GetConstant0(), String::TWO_BYTE_ENCODING, result,
              graph()->GetConstant0(), String::TWO_BYTE_ENCODING, left_length);

          // Copy characters from the right string.
          BuildCopySeqStringChars(
              right, graph()->GetConstant0(), String::TWO_BYTE_ENCODING, result,
              left_length, String::TWO_BYTE_ENCODING, right_length);
        }
        if_twobyte.Else();
        {
          // Copy characters from the left string.
          BuildCopySeqStringChars(
              left, graph()->GetConstant0(), String::ONE_BYTE_ENCODING, result,
              graph()->GetConstant0(), String::ONE_BYTE_ENCODING, left_length);

          // Copy characters from the right string.
          BuildCopySeqStringChars(
              right, graph()->GetConstant0(), String::ONE_BYTE_ENCODING, result,
              left_length, String::ONE_BYTE_ENCODING, right_length);
        }
        if_twobyte.End();

        // Count the native string addition.
        AddIncrementCounter(isolate()->counters()->string_add_native());

        // Return the sequential string.
        Push(result);
      }
      if_size.Else();
      {
        // Fallback to the runtime to add the two strings. The string has to be
        // allocated in LO space.
        Add<HPushArguments>(left, right);
        Push(Add<HCallRuntime>(Runtime::FunctionForId(Runtime::kStringAdd), 2));
      }
      if_size.End();
    }
    if_sameencodingandsequential.Else();
    {
      // Fallback to the runtime to add the two strings.
      Add<HPushArguments>(left, right);
      Push(Add<HCallRuntime>(Runtime::FunctionForId(Runtime::kStringAdd), 2));
    }
    if_sameencodingandsequential.End();
  }
  if_createcons.End();

  return Pop();
}


HValue* HGraphBuilder::BuildStringAdd(
    HValue* left,
    HValue* right,
    HAllocationMode allocation_mode) {
  NoObservableSideEffectsScope no_effects(this);

  // Determine string lengths.
  HValue* left_length = AddLoadStringLength(left);
  HValue* right_length = AddLoadStringLength(right);

  // Check if left string is empty.
  IfBuilder if_leftempty(this);
  if_leftempty.If<HCompareNumericAndBranch>(
      left_length, graph()->GetConstant0(), Token::EQ);
  if_leftempty.Then();
  {
    // Count the native string addition.
    AddIncrementCounter(isolate()->counters()->string_add_native());

    // Just return the right string.
    Push(right);
  }
  if_leftempty.Else();
  {
    // Check if right string is empty.
    IfBuilder if_rightempty(this);
    if_rightempty.If<HCompareNumericAndBranch>(
        right_length, graph()->GetConstant0(), Token::EQ);
    if_rightempty.Then();
    {
      // Count the native string addition.
      AddIncrementCounter(isolate()->counters()->string_add_native());

      // Just return the left string.
      Push(left);
    }
    if_rightempty.Else();
    {
      // Add the two non-empty strings.
      Push(BuildUncheckedStringAdd(left, right, allocation_mode));
    }
    if_rightempty.End();
  }
  if_leftempty.End();

  return Pop();
}


HInstruction* HGraphBuilder::BuildUncheckedMonomorphicElementAccess(
    HValue* checked_object,
    HValue* key,
    HValue* val,
    bool is_js_array,
    ElementsKind elements_kind,
    PropertyAccessType access_type,
    LoadKeyedHoleMode load_mode,
    KeyedAccessStoreMode store_mode) {
  DCHECK(top_info()->IsStub() || checked_object->IsCompareMap() ||
         checked_object->IsCheckMaps());
  DCHECK(!IsFixedTypedArrayElementsKind(elements_kind) || !is_js_array);
  // No GVNFlag is necessary for ElementsKind if there is an explicit dependency
  // on a HElementsTransition instruction. The flag can also be removed if the
  // map to check has FAST_HOLEY_ELEMENTS, since there can be no further
  // ElementsKind transitions. Finally, the dependency can be removed for stores
  // for FAST_ELEMENTS, since a transition to HOLEY elements won't change the
  // generated store code.
  if ((elements_kind == FAST_HOLEY_ELEMENTS) ||
      (elements_kind == FAST_ELEMENTS && access_type == STORE)) {
    checked_object->ClearDependsOnFlag(kElementsKind);
  }

  bool fast_smi_only_elements = IsFastSmiElementsKind(elements_kind);
  bool fast_elements = IsFastObjectElementsKind(elements_kind);
  HValue* elements = AddLoadElements(checked_object);
  if (access_type == STORE && (fast_elements || fast_smi_only_elements) &&
      store_mode != STORE_NO_TRANSITION_HANDLE_COW) {
    HCheckMaps* check_cow_map = Add<HCheckMaps>(
        elements, isolate()->factory()->fixed_array_map());
    check_cow_map->ClearDependsOnFlag(kElementsKind);
  }
  HInstruction* length = NULL;
  if (is_js_array) {
    length = Add<HLoadNamedField>(
        checked_object->ActualValue(), checked_object,
        HObjectAccess::ForArrayLength(elements_kind));
  } else {
    length = AddLoadFixedArrayLength(elements);
  }
  length->set_type(HType::Smi());
  HValue* checked_key = NULL;
  if (IsFixedTypedArrayElementsKind(elements_kind)) {
    checked_object = Add<HCheckArrayBufferNotNeutered>(checked_object);

    HValue* external_pointer = Add<HLoadNamedField>(
        elements, nullptr,
        HObjectAccess::ForFixedTypedArrayBaseExternalPointer());
    HValue* base_pointer = Add<HLoadNamedField>(
        elements, nullptr, HObjectAccess::ForFixedTypedArrayBaseBasePointer());
    HValue* backing_store = AddUncasted<HAdd>(external_pointer, base_pointer,
                                              AddOfExternalAndTagged);

    if (store_mode == STORE_NO_TRANSITION_IGNORE_OUT_OF_BOUNDS) {
      NoObservableSideEffectsScope no_effects(this);
      IfBuilder length_checker(this);
      length_checker.If<HCompareNumericAndBranch>(key, length, Token::LT);
      length_checker.Then();
      IfBuilder negative_checker(this);
      HValue* bounds_check = negative_checker.If<HCompareNumericAndBranch>(
          key, graph()->GetConstant0(), Token::GTE);
      negative_checker.Then();
      HInstruction* result = AddElementAccess(
          backing_store, key, val, bounds_check, checked_object->ActualValue(),
          elements_kind, access_type);
      negative_checker.ElseDeopt(DeoptimizeReason::kNegativeKeyEncountered);
      negative_checker.End();
      length_checker.End();
      return result;
    } else {
      DCHECK(store_mode == STANDARD_STORE);
      checked_key = Add<HBoundsCheck>(key, length);
      return AddElementAccess(backing_store, checked_key, val, checked_object,
                              checked_object->ActualValue(), elements_kind,
                              access_type);
    }
  }
  DCHECK(fast_smi_only_elements ||
         fast_elements ||
         IsFastDoubleElementsKind(elements_kind));

  // In case val is stored into a fast smi array, assure that the value is a smi
  // before manipulating the backing store. Otherwise the actual store may
  // deopt, leaving the backing store in an invalid state.
  if (access_type == STORE && IsFastSmiElementsKind(elements_kind) &&
      !val->type().IsSmi()) {
    val = AddUncasted<HForceRepresentation>(val, Representation::Smi());
  }

  if (IsGrowStoreMode(store_mode)) {
    NoObservableSideEffectsScope no_effects(this);
    Representation representation = HStoreKeyed::RequiredValueRepresentation(
        elements_kind, STORE_TO_INITIALIZED_ENTRY);
    val = AddUncasted<HForceRepresentation>(val, representation);
    elements = BuildCheckForCapacityGrow(checked_object, elements,
                                         elements_kind, length, key,
                                         is_js_array, access_type);
    checked_key = key;
  } else {
    checked_key = Add<HBoundsCheck>(key, length);

    if (access_type == STORE && (fast_elements || fast_smi_only_elements)) {
      if (store_mode == STORE_NO_TRANSITION_HANDLE_COW) {
        NoObservableSideEffectsScope no_effects(this);
        elements = BuildCopyElementsOnWrite(checked_object, elements,
                                            elements_kind, length);
      } else {
        HCheckMaps* check_cow_map = Add<HCheckMaps>(
            elements, isolate()->factory()->fixed_array_map());
        check_cow_map->ClearDependsOnFlag(kElementsKind);
      }
    }
  }
  return AddElementAccess(elements, checked_key, val, checked_object, nullptr,
                          elements_kind, access_type, load_mode);
}


HValue* HGraphBuilder::BuildCalculateElementsSize(ElementsKind kind,
                                                  HValue* capacity) {
  int elements_size = IsFastDoubleElementsKind(kind)
      ? kDoubleSize
      : kPointerSize;

  HConstant* elements_size_value = Add<HConstant>(elements_size);
  HInstruction* mul =
      HMul::NewImul(isolate(), zone(), context(), capacity->ActualValue(),
                    elements_size_value);
  AddInstruction(mul);
  mul->ClearFlag(HValue::kCanOverflow);

  STATIC_ASSERT(FixedDoubleArray::kHeaderSize == FixedArray::kHeaderSize);

  HConstant* header_size = Add<HConstant>(FixedArray::kHeaderSize);
  HValue* total_size = AddUncasted<HAdd>(mul, header_size);
  total_size->ClearFlag(HValue::kCanOverflow);
  return total_size;
}


HAllocate* HGraphBuilder::AllocateJSArrayObject(AllocationSiteMode mode) {
  int base_size = JSArray::kSize;
  if (mode == TRACK_ALLOCATION_SITE) {
    base_size += AllocationMemento::kSize;
  }
  HConstant* size_in_bytes = Add<HConstant>(base_size);
  return Add<HAllocate>(size_in_bytes, HType::JSArray(), NOT_TENURED,
                        JS_OBJECT_TYPE, graph()->GetConstant0());
}


HConstant* HGraphBuilder::EstablishElementsAllocationSize(
    ElementsKind kind,
    int capacity) {
  int base_size = IsFastDoubleElementsKind(kind)
      ? FixedDoubleArray::SizeFor(capacity)
      : FixedArray::SizeFor(capacity);

  return Add<HConstant>(base_size);
}


HAllocate* HGraphBuilder::BuildAllocateElements(ElementsKind kind,
                                                HValue* size_in_bytes) {
  InstanceType instance_type = IsFastDoubleElementsKind(kind)
      ? FIXED_DOUBLE_ARRAY_TYPE
      : FIXED_ARRAY_TYPE;

  return Add<HAllocate>(size_in_bytes, HType::HeapObject(), NOT_TENURED,
                        instance_type, graph()->GetConstant0());
}


void HGraphBuilder::BuildInitializeElementsHeader(HValue* elements,
                                                  ElementsKind kind,
                                                  HValue* capacity) {
  Factory* factory = isolate()->factory();
  Handle<Map> map = IsFastDoubleElementsKind(kind)
      ? factory->fixed_double_array_map()
      : factory->fixed_array_map();

  Add<HStoreNamedField>(elements, HObjectAccess::ForMap(), Add<HConstant>(map));
  Add<HStoreNamedField>(elements, HObjectAccess::ForFixedArrayLength(),
                        capacity);
}


HValue* HGraphBuilder::BuildAllocateAndInitializeArray(ElementsKind kind,
                                                       HValue* capacity) {
  // The HForceRepresentation is to prevent possible deopt on int-smi
  // conversion after allocation but before the new object fields are set.
  capacity = AddUncasted<HForceRepresentation>(capacity, Representation::Smi());
  HValue* size_in_bytes = BuildCalculateElementsSize(kind, capacity);
  HValue* new_array = BuildAllocateElements(kind, size_in_bytes);
  BuildInitializeElementsHeader(new_array, kind, capacity);
  return new_array;
}


void HGraphBuilder::BuildJSArrayHeader(HValue* array,
                                       HValue* array_map,
                                       HValue* elements,
                                       AllocationSiteMode mode,
                                       ElementsKind elements_kind,
                                       HValue* allocation_site_payload,
                                       HValue* length_field) {
  Add<HStoreNamedField>(array, HObjectAccess::ForMap(), array_map);

  HValue* empty_fixed_array = Add<HLoadRoot>(Heap::kEmptyFixedArrayRootIndex);

  Add<HStoreNamedField>(
      array, HObjectAccess::ForPropertiesPointer(), empty_fixed_array);

  Add<HStoreNamedField>(array, HObjectAccess::ForElementsPointer(),
                        elements != nullptr ? elements : empty_fixed_array);

  Add<HStoreNamedField>(
      array, HObjectAccess::ForArrayLength(elements_kind), length_field);

  if (mode == TRACK_ALLOCATION_SITE) {
    BuildCreateAllocationMemento(
        array, Add<HConstant>(JSArray::kSize), allocation_site_payload);
  }
}


HInstruction* HGraphBuilder::AddElementAccess(
    HValue* elements, HValue* checked_key, HValue* val, HValue* dependency,
    HValue* backing_store_owner, ElementsKind elements_kind,
    PropertyAccessType access_type, LoadKeyedHoleMode load_mode) {
  if (access_type == STORE) {
    DCHECK(val != NULL);
    if (elements_kind == UINT8_CLAMPED_ELEMENTS) {
      val = Add<HClampToUint8>(val);
    }
    return Add<HStoreKeyed>(elements, checked_key, val, backing_store_owner,
                            elements_kind, STORE_TO_INITIALIZED_ENTRY);
  }

  DCHECK(access_type == LOAD);
  DCHECK(val == NULL);
  HLoadKeyed* load =
      Add<HLoadKeyed>(elements, checked_key, dependency, backing_store_owner,
                      elements_kind, load_mode);
  if (elements_kind == UINT32_ELEMENTS) {
    graph()->RecordUint32Instruction(load);
  }
  return load;
}


HLoadNamedField* HGraphBuilder::AddLoadMap(HValue* object,
                                           HValue* dependency) {
  return Add<HLoadNamedField>(object, dependency, HObjectAccess::ForMap());
}


HLoadNamedField* HGraphBuilder::AddLoadElements(HValue* object,
                                                HValue* dependency) {
  return Add<HLoadNamedField>(
      object, dependency, HObjectAccess::ForElementsPointer());
}


HLoadNamedField* HGraphBuilder::AddLoadFixedArrayLength(
    HValue* array,
    HValue* dependency) {
  return Add<HLoadNamedField>(
      array, dependency, HObjectAccess::ForFixedArrayLength());
}


HLoadNamedField* HGraphBuilder::AddLoadArrayLength(HValue* array,
                                                   ElementsKind kind,
                                                   HValue* dependency) {
  return Add<HLoadNamedField>(
      array, dependency, HObjectAccess::ForArrayLength(kind));
}


HValue* HGraphBuilder::BuildNewElementsCapacity(HValue* old_capacity) {
  HValue* half_old_capacity = AddUncasted<HShr>(old_capacity,
                                                graph_->GetConstant1());

  HValue* new_capacity = AddUncasted<HAdd>(half_old_capacity, old_capacity);
  new_capacity->ClearFlag(HValue::kCanOverflow);

  HValue* min_growth = Add<HConstant>(16);

  new_capacity = AddUncasted<HAdd>(new_capacity, min_growth);
  new_capacity->ClearFlag(HValue::kCanOverflow);

  return new_capacity;
}


HValue* HGraphBuilder::BuildGrowElementsCapacity(HValue* object,
                                                 HValue* elements,
                                                 ElementsKind kind,
                                                 ElementsKind new_kind,
                                                 HValue* length,
                                                 HValue* new_capacity) {
  Add<HBoundsCheck>(
      new_capacity,
      Add<HConstant>((kMaxRegularHeapObjectSize - FixedArray::kHeaderSize) >>
                     ElementsKindToShiftSize(new_kind)));

  HValue* new_elements =
      BuildAllocateAndInitializeArray(new_kind, new_capacity);

  BuildCopyElements(elements, kind, new_elements,
                    new_kind, length, new_capacity);

  Add<HStoreNamedField>(object, HObjectAccess::ForElementsPointer(),
                        new_elements);

  return new_elements;
}


void HGraphBuilder::BuildFillElementsWithValue(HValue* elements,
                                               ElementsKind elements_kind,
                                               HValue* from,
                                               HValue* to,
                                               HValue* value) {
  if (to == NULL) {
    to = AddLoadFixedArrayLength(elements);
  }

  // Special loop unfolding case
  STATIC_ASSERT(JSArray::kPreallocatedArrayElements <=
                kElementLoopUnrollThreshold);
  int initial_capacity = -1;
  if (from->IsInteger32Constant() && to->IsInteger32Constant()) {
    int constant_from = from->GetInteger32Constant();
    int constant_to = to->GetInteger32Constant();

    if (constant_from == 0 && constant_to <= kElementLoopUnrollThreshold) {
      initial_capacity = constant_to;
    }
  }

  if (initial_capacity >= 0) {
    for (int i = 0; i < initial_capacity; i++) {
      HInstruction* key = Add<HConstant>(i);
      Add<HStoreKeyed>(elements, key, value, nullptr, elements_kind);
    }
  } else {
    // Carefully loop backwards so that the "from" remains live through the loop
    // rather than the to. This often corresponds to keeping length live rather
    // then capacity, which helps register allocation, since length is used more
    // other than capacity after filling with holes.
    LoopBuilder builder(this, context(), LoopBuilder::kPostDecrement);

    HValue* key = builder.BeginBody(to, from, Token::GT);

    HValue* adjusted_key = AddUncasted<HSub>(key, graph()->GetConstant1());
    adjusted_key->ClearFlag(HValue::kCanOverflow);

    Add<HStoreKeyed>(elements, adjusted_key, value, nullptr, elements_kind);

    builder.EndBody();
  }
}


void HGraphBuilder::BuildFillElementsWithHole(HValue* elements,
                                              ElementsKind elements_kind,
                                              HValue* from,
                                              HValue* to) {
  // Fast elements kinds need to be initialized in case statements below cause a
  // garbage collection.

  HValue* hole = IsFastSmiOrObjectElementsKind(elements_kind)
                     ? graph()->GetConstantHole()
                     : Add<HConstant>(HConstant::kHoleNaN);

  // Since we're about to store a hole value, the store instruction below must
  // assume an elements kind that supports heap object values.
  if (IsFastSmiOrObjectElementsKind(elements_kind)) {
    elements_kind = FAST_HOLEY_ELEMENTS;
  }

  BuildFillElementsWithValue(elements, elements_kind, from, to, hole);
}


void HGraphBuilder::BuildCopyProperties(HValue* from_properties,
                                        HValue* to_properties, HValue* length,
                                        HValue* capacity) {
  ElementsKind kind = FAST_ELEMENTS;

  BuildFillElementsWithValue(to_properties, kind, length, capacity,
                             graph()->GetConstantUndefined());

  LoopBuilder builder(this, context(), LoopBuilder::kPostDecrement);

  HValue* key = builder.BeginBody(length, graph()->GetConstant0(), Token::GT);

  key = AddUncasted<HSub>(key, graph()->GetConstant1());
  key->ClearFlag(HValue::kCanOverflow);

  HValue* element =
      Add<HLoadKeyed>(from_properties, key, nullptr, nullptr, kind);

  Add<HStoreKeyed>(to_properties, key, element, nullptr, kind);

  builder.EndBody();
}


void HGraphBuilder::BuildCopyElements(HValue* from_elements,
                                      ElementsKind from_elements_kind,
                                      HValue* to_elements,
                                      ElementsKind to_elements_kind,
                                      HValue* length,
                                      HValue* capacity) {
  int constant_capacity = -1;
  if (capacity != NULL &&
      capacity->IsConstant() &&
      HConstant::cast(capacity)->HasInteger32Value()) {
    int constant_candidate = HConstant::cast(capacity)->Integer32Value();
    if (constant_candidate <= kElementLoopUnrollThreshold) {
      constant_capacity = constant_candidate;
    }
  }

  bool pre_fill_with_holes =
    IsFastDoubleElementsKind(from_elements_kind) &&
    IsFastObjectElementsKind(to_elements_kind);
  if (pre_fill_with_holes) {
    // If the copy might trigger a GC, make sure that the FixedArray is
    // pre-initialized with holes to make sure that it's always in a
    // consistent state.
    BuildFillElementsWithHole(to_elements, to_elements_kind,
                              graph()->GetConstant0(), NULL);
  }

  if (constant_capacity != -1) {
    // Unroll the loop for small elements kinds.
    for (int i = 0; i < constant_capacity; i++) {
      HValue* key_constant = Add<HConstant>(i);
      HInstruction* value = Add<HLoadKeyed>(
          from_elements, key_constant, nullptr, nullptr, from_elements_kind);
      Add<HStoreKeyed>(to_elements, key_constant, value, nullptr,
                       to_elements_kind);
    }
  } else {
    if (!pre_fill_with_holes &&
        (capacity == NULL || !length->Equals(capacity))) {
      BuildFillElementsWithHole(to_elements, to_elements_kind,
                                length, NULL);
    }

    LoopBuilder builder(this, context(), LoopBuilder::kPostDecrement);

    HValue* key = builder.BeginBody(length, graph()->GetConstant0(),
                                    Token::GT);

    key = AddUncasted<HSub>(key, graph()->GetConstant1());
    key->ClearFlag(HValue::kCanOverflow);

    HValue* element = Add<HLoadKeyed>(from_elements, key, nullptr, nullptr,
                                      from_elements_kind, ALLOW_RETURN_HOLE);

    ElementsKind kind = (IsHoleyElementsKind(from_elements_kind) &&
                         IsFastSmiElementsKind(to_elements_kind))
      ? FAST_HOLEY_ELEMENTS : to_elements_kind;

    if (IsHoleyElementsKind(from_elements_kind) &&
        from_elements_kind != to_elements_kind) {
      IfBuilder if_hole(this);
      if_hole.If<HCompareHoleAndBranch>(element);
      if_hole.Then();
      HConstant* hole_constant = IsFastDoubleElementsKind(to_elements_kind)
                                     ? Add<HConstant>(HConstant::kHoleNaN)
                                     : graph()->GetConstantHole();
      Add<HStoreKeyed>(to_elements, key, hole_constant, nullptr, kind);
      if_hole.Else();
      HStoreKeyed* store =
          Add<HStoreKeyed>(to_elements, key, element, nullptr, kind);
      store->SetFlag(HValue::kTruncatingToNumber);
      if_hole.End();
    } else {
      HStoreKeyed* store =
          Add<HStoreKeyed>(to_elements, key, element, nullptr, kind);
      store->SetFlag(HValue::kTruncatingToNumber);
    }

    builder.EndBody();
  }

  Counters* counters = isolate()->counters();
  AddIncrementCounter(counters->inlined_copied_elements());
}

void HGraphBuilder::BuildCreateAllocationMemento(
    HValue* previous_object,
    HValue* previous_object_size,
    HValue* allocation_site) {
  DCHECK(allocation_site != NULL);
  HInnerAllocatedObject* allocation_memento = Add<HInnerAllocatedObject>(
      previous_object, previous_object_size, HType::HeapObject());
  AddStoreMapConstant(
      allocation_memento, isolate()->factory()->allocation_memento_map());
  Add<HStoreNamedField>(
      allocation_memento,
      HObjectAccess::ForAllocationMementoSite(),
      allocation_site);
  if (FLAG_allocation_site_pretenuring) {
    HValue* memento_create_count =
        Add<HLoadNamedField>(allocation_site, nullptr,
                             HObjectAccess::ForAllocationSiteOffset(
                                 AllocationSite::kPretenureCreateCountOffset));
    memento_create_count = AddUncasted<HAdd>(
        memento_create_count, graph()->GetConstant1());
    // This smi value is reset to zero after every gc, overflow isn't a problem
    // since the counter is bounded by the new space size.
    memento_create_count->ClearFlag(HValue::kCanOverflow);
    Add<HStoreNamedField>(
        allocation_site, HObjectAccess::ForAllocationSiteOffset(
            AllocationSite::kPretenureCreateCountOffset), memento_create_count);
  }
}


HInstruction* HGraphBuilder::BuildGetNativeContext() {
  return Add<HLoadNamedField>(
      context(), nullptr,
      HObjectAccess::ForContextSlot(Context::NATIVE_CONTEXT_INDEX));
}

HValue* HGraphBuilder::BuildArrayBufferViewFieldAccessor(HValue* object,
                                                         HValue* checked_object,
                                                         FieldIndex index) {
  NoObservableSideEffectsScope scope(this);
  HObjectAccess access = HObjectAccess::ForObservableJSObjectOffset(
      index.offset(), Representation::Tagged());
  HInstruction* buffer = Add<HLoadNamedField>(
      object, checked_object, HObjectAccess::ForJSArrayBufferViewBuffer());
  HInstruction* field = Add<HLoadNamedField>(object, checked_object, access);

  HInstruction* flags = Add<HLoadNamedField>(
      buffer, nullptr, HObjectAccess::ForJSArrayBufferBitField());
  HValue* was_neutered_mask =
      Add<HConstant>(1 << JSArrayBuffer::WasNeutered::kShift);
  HValue* was_neutered_test =
      AddUncasted<HBitwise>(Token::BIT_AND, flags, was_neutered_mask);

  IfBuilder if_was_neutered(this);
  if_was_neutered.If<HCompareNumericAndBranch>(
      was_neutered_test, graph()->GetConstant0(), Token::NE);
  if_was_neutered.Then();
  Push(graph()->GetConstant0());
  if_was_neutered.Else();
  Push(field);
  if_was_neutered.End();

  return Pop();
}


void HBasicBlock::FinishExit(HControlInstruction* instruction,
                             SourcePosition position) {
  Finish(instruction, position);
  ClearEnvironment();
}


std::ostream& operator<<(std::ostream& os, const HBasicBlock& b) {
  return os << "B" << b.block_id();
}

HGraph::HGraph(CompilationInfo* info, CallInterfaceDescriptor descriptor)
    : isolate_(info->isolate()),
      next_block_id_(0),
      entry_block_(NULL),
      blocks_(8, info->zone()),
      values_(16, info->zone()),
      phi_list_(NULL),
      uint32_instructions_(NULL),
      info_(info),
      descriptor_(descriptor),
      zone_(info->zone()),
      allow_code_motion_(false),
      use_optimistic_licm_(false),
      depends_on_empty_array_proto_elements_(false),
      depends_on_string_length_overflow_(false),
      type_change_checksum_(0),
      maximum_environment_size_(0),
      no_side_effects_scope_count_(0),
      disallow_adding_new_values_(false) {
  if (info->IsStub()) {
    // For stubs, explicitly add the context to the environment.
    start_environment_ =
        new (zone_) HEnvironment(zone_, descriptor.GetParameterCount() + 1);
  } else {
    start_environment_ =
        new(zone_) HEnvironment(NULL, info->scope(), info->closure(), zone_);
  }
  start_environment_->set_ast_id(BailoutId::FunctionContext());
  entry_block_ = CreateBasicBlock();
  entry_block_->SetInitialEnvironment(start_environment_);
}


HBasicBlock* HGraph::CreateBasicBlock() {
  HBasicBlock* result = new(zone()) HBasicBlock(this);
  blocks_.Add(result, zone());
  return result;
}


void HGraph::FinalizeUniqueness() {
  DisallowHeapAllocation no_gc;
  for (int i = 0; i < blocks()->length(); ++i) {
    for (HInstructionIterator it(blocks()->at(i)); !it.Done(); it.Advance()) {
      it.Current()->FinalizeUniqueness();
    }
  }
}


// Block ordering was implemented with two mutually recursive methods,
// HGraph::Postorder and HGraph::PostorderLoopBlocks.
// The recursion could lead to stack overflow so the algorithm has been
// implemented iteratively.
// At a high level the algorithm looks like this:
//
// Postorder(block, loop_header) : {
//   if (block has already been visited or is of another loop) return;
//   mark block as visited;
//   if (block is a loop header) {
//     VisitLoopMembers(block, loop_header);
//     VisitSuccessorsOfLoopHeader(block);
//   } else {
//     VisitSuccessors(block)
//   }
//   put block in result list;
// }
//
// VisitLoopMembers(block, outer_loop_header) {
//   foreach (block b in block loop members) {
//     VisitSuccessorsOfLoopMember(b, outer_loop_header);
//     if (b is loop header) VisitLoopMembers(b);
//   }
// }
//
// VisitSuccessorsOfLoopMember(block, outer_loop_header) {
//   foreach (block b in block successors) Postorder(b, outer_loop_header)
// }
//
// VisitSuccessorsOfLoopHeader(block) {
//   foreach (block b in block successors) Postorder(b, block)
// }
//
// VisitSuccessors(block, loop_header) {
//   foreach (block b in block successors) Postorder(b, loop_header)
// }
//
// The ordering is started calling Postorder(entry, NULL).
//
// Each instance of PostorderProcessor represents the "stack frame" of the
// recursion, and particularly keeps the state of the loop (iteration) of the
// "Visit..." function it represents.
// To recycle memory we keep all the frames in a double linked list but
// this means that we cannot use constructors to initialize the frames.
//
class PostorderProcessor : public ZoneObject {
 public:
  // Back link (towards the stack bottom).
  PostorderProcessor* parent() {return father_; }
  // Forward link (towards the stack top).
  PostorderProcessor* child() {return child_; }
  HBasicBlock* block() { return block_; }
  HLoopInformation* loop() { return loop_; }
  HBasicBlock* loop_header() { return loop_header_; }

  static PostorderProcessor* CreateEntryProcessor(Zone* zone,
                                                  HBasicBlock* block) {
    PostorderProcessor* result = new(zone) PostorderProcessor(NULL);
    return result->SetupSuccessors(zone, block, NULL);
  }

  PostorderProcessor* PerformStep(Zone* zone,
                                  ZoneList<HBasicBlock*>* order) {
    PostorderProcessor* next =
        PerformNonBacktrackingStep(zone, order);
    if (next != NULL) {
      return next;
    } else {
      return Backtrack(zone, order);
    }
  }

 private:
  explicit PostorderProcessor(PostorderProcessor* father)
      : father_(father), child_(NULL), successor_iterator(NULL) { }

  // Each enum value states the cycle whose state is kept by this instance.
  enum LoopKind {
    NONE,
    SUCCESSORS,
    SUCCESSORS_OF_LOOP_HEADER,
    LOOP_MEMBERS,
    SUCCESSORS_OF_LOOP_MEMBER
  };

  // Each "Setup..." method is like a constructor for a cycle state.
  PostorderProcessor* SetupSuccessors(Zone* zone,
                                      HBasicBlock* block,
                                      HBasicBlock* loop_header) {
    if (block == NULL || block->IsOrdered() ||
        block->parent_loop_header() != loop_header) {
      kind_ = NONE;
      block_ = NULL;
      loop_ = NULL;
      loop_header_ = NULL;
      return this;
    } else {
      block_ = block;
      loop_ = NULL;
      block->MarkAsOrdered();

      if (block->IsLoopHeader()) {
        kind_ = SUCCESSORS_OF_LOOP_HEADER;
        loop_header_ = block;
        InitializeSuccessors();
        PostorderProcessor* result = Push(zone);
        return result->SetupLoopMembers(zone, block, block->loop_information(),
                                        loop_header);
      } else {
        DCHECK(block->IsFinished());
        kind_ = SUCCESSORS;
        loop_header_ = loop_header;
        InitializeSuccessors();
        return this;
      }
    }
  }

  PostorderProcessor* SetupLoopMembers(Zone* zone,
                                       HBasicBlock* block,
                                       HLoopInformation* loop,
                                       HBasicBlock* loop_header) {
    kind_ = LOOP_MEMBERS;
    block_ = block;
    loop_ = loop;
    loop_header_ = loop_header;
    InitializeLoopMembers();
    return this;
  }

  PostorderProcessor* SetupSuccessorsOfLoopMember(
      HBasicBlock* block,
      HLoopInformation* loop,
      HBasicBlock* loop_header) {
    kind_ = SUCCESSORS_OF_LOOP_MEMBER;
    block_ = block;
    loop_ = loop;
    loop_header_ = loop_header;
    InitializeSuccessors();
    return this;
  }

  // This method "allocates" a new stack frame.
  PostorderProcessor* Push(Zone* zone) {
    if (child_ == NULL) {
      child_ = new(zone) PostorderProcessor(this);
    }
    return child_;
  }

  void ClosePostorder(ZoneList<HBasicBlock*>* order, Zone* zone) {
    DCHECK(block_->end()->FirstSuccessor() == NULL ||
           order->Contains(block_->end()->FirstSuccessor()) ||
           block_->end()->FirstSuccessor()->IsLoopHeader());
    DCHECK(block_->end()->SecondSuccessor() == NULL ||
           order->Contains(block_->end()->SecondSuccessor()) ||
           block_->end()->SecondSuccessor()->IsLoopHeader());
    order->Add(block_, zone);
  }

  // This method is the basic block to walk up the stack.
  PostorderProcessor* Pop(Zone* zone,
                          ZoneList<HBasicBlock*>* order) {
    switch (kind_) {
      case SUCCESSORS:
      case SUCCESSORS_OF_LOOP_HEADER:
        ClosePostorder(order, zone);
        return father_;
      case LOOP_MEMBERS:
        return father_;
      case SUCCESSORS_OF_LOOP_MEMBER:
        if (block()->IsLoopHeader() && block() != loop_->loop_header()) {
          // In this case we need to perform a LOOP_MEMBERS cycle so we
          // initialize it and return this instead of father.
          return SetupLoopMembers(zone, block(),
                                  block()->loop_information(), loop_header_);
        } else {
          return father_;
        }
      case NONE:
        return father_;
    }
    UNREACHABLE();
  }

  // Walks up the stack.
  PostorderProcessor* Backtrack(Zone* zone,
                                ZoneList<HBasicBlock*>* order) {
    PostorderProcessor* parent = Pop(zone, order);
    while (parent != NULL) {
      PostorderProcessor* next =
          parent->PerformNonBacktrackingStep(zone, order);
      if (next != NULL) {
        return next;
      } else {
        parent = parent->Pop(zone, order);
      }
    }
    return NULL;
  }

  PostorderProcessor* PerformNonBacktrackingStep(
      Zone* zone,
      ZoneList<HBasicBlock*>* order) {
    HBasicBlock* next_block;
    switch (kind_) {
      case SUCCESSORS:
        next_block = AdvanceSuccessors();
        if (next_block != NULL) {
          PostorderProcessor* result = Push(zone);
          return result->SetupSuccessors(zone, next_block, loop_header_);
        }
        break;
      case SUCCESSORS_OF_LOOP_HEADER:
        next_block = AdvanceSuccessors();
        if (next_block != NULL) {
          PostorderProcessor* result = Push(zone);
          return result->SetupSuccessors(zone, next_block, block());
        }
        break;
      case LOOP_MEMBERS:
        next_block = AdvanceLoopMembers();
        if (next_block != NULL) {
          PostorderProcessor* result = Push(zone);
          return result->SetupSuccessorsOfLoopMember(next_block,
                                                     loop_, loop_header_);
        }
        break;
      case SUCCESSORS_OF_LOOP_MEMBER:
        next_block = AdvanceSuccessors();
        if (next_block != NULL) {
          PostorderProcessor* result = Push(zone);
          return result->SetupSuccessors(zone, next_block, loop_header_);
        }
        break;
      case NONE:
        return NULL;
    }
    return NULL;
  }

  // The following two methods implement a "foreach b in successors" cycle.
  void InitializeSuccessors() {
    loop_index = 0;
    loop_length = 0;
    successor_iterator = HSuccessorIterator(block_->end());
  }

  HBasicBlock* AdvanceSuccessors() {
    if (!successor_iterator.Done()) {
      HBasicBlock* result = successor_iterator.Current();
      successor_iterator.Advance();
      return result;
    }
    return NULL;
  }

  // The following two methods implement a "foreach b in loop members" cycle.
  void InitializeLoopMembers() {
    loop_index = 0;
    loop_length = loop_->blocks()->length();
  }

  HBasicBlock* AdvanceLoopMembers() {
    if (loop_index < loop_length) {
      HBasicBlock* result = loop_->blocks()->at(loop_index);
      loop_index++;
      return result;
    } else {
      return NULL;
    }
  }

  LoopKind kind_;
  PostorderProcessor* father_;
  PostorderProcessor* child_;
  HLoopInformation* loop_;
  HBasicBlock* block_;
  HBasicBlock* loop_header_;
  int loop_index;
  int loop_length;
  HSuccessorIterator successor_iterator;
};


void HGraph::OrderBlocks() {
  CompilationPhase phase("H_Block ordering", info());

#ifdef DEBUG
  // Initially the blocks must not be ordered.
  for (int i = 0; i < blocks_.length(); ++i) {
    DCHECK(!blocks_[i]->IsOrdered());
  }
#endif

  PostorderProcessor* postorder =
      PostorderProcessor::CreateEntryProcessor(zone(), blocks_[0]);
  blocks_.Rewind(0);
  while (postorder) {
    postorder = postorder->PerformStep(zone(), &blocks_);
  }

#ifdef DEBUG
  // Now all blocks must be marked as ordered.
  for (int i = 0; i < blocks_.length(); ++i) {
    DCHECK(blocks_[i]->IsOrdered());
  }
#endif

  // Reverse block list and assign block IDs.
  for (int i = 0, j = blocks_.length(); --j >= i; ++i) {
    HBasicBlock* bi = blocks_[i];
    HBasicBlock* bj = blocks_[j];
    bi->set_block_id(j);
    bj->set_block_id(i);
    blocks_[i] = bj;
    blocks_[j] = bi;
  }
}


void HGraph::AssignDominators() {
  HPhase phase("H_Assign dominators", this);
  for (int i = 0; i < blocks_.length(); ++i) {
    HBasicBlock* block = blocks_[i];
    if (block->IsLoopHeader()) {
      // Only the first predecessor of a loop header is from outside the loop.
      // All others are back edges, and thus cannot dominate the loop header.
      block->AssignCommonDominator(block->predecessors()->first());
      block->AssignLoopSuccessorDominators();
    } else {
      for (int j = blocks_[i]->predecessors()->length() - 1; j >= 0; --j) {
        blocks_[i]->AssignCommonDominator(blocks_[i]->predecessors()->at(j));
      }
    }
  }
}


bool HGraph::CheckArgumentsPhiUses() {
  int block_count = blocks_.length();
  for (int i = 0; i < block_count; ++i) {
    for (int j = 0; j < blocks_[i]->phis()->length(); ++j) {
      HPhi* phi = blocks_[i]->phis()->at(j);
      // We don't support phi uses of arguments for now.
      if (phi->CheckFlag(HValue::kIsArguments)) return false;
    }
  }
  return true;
}


bool HGraph::CheckConstPhiUses() {
  int block_count = blocks_.length();
  for (int i = 0; i < block_count; ++i) {
    for (int j = 0; j < blocks_[i]->phis()->length(); ++j) {
      HPhi* phi = blocks_[i]->phis()->at(j);
      // Check for the hole value (from an uninitialized const).
      for (int k = 0; k < phi->OperandCount(); k++) {
        if (phi->OperandAt(k) == GetConstantHole()) return false;
      }
    }
  }
  return true;
}


void HGraph::CollectPhis() {
  int block_count = blocks_.length();
  phi_list_ = new(zone()) ZoneList<HPhi*>(block_count, zone());
  for (int i = 0; i < block_count; ++i) {
    for (int j = 0; j < blocks_[i]->phis()->length(); ++j) {
      HPhi* phi = blocks_[i]->phis()->at(j);
      phi_list_->Add(phi, zone());
    }
  }
}


bool HGraph::Optimize(BailoutReason* bailout_reason) {
  OrderBlocks();
  AssignDominators();

  // We need to create a HConstant "zero" now so that GVN will fold every
  // zero-valued constant in the graph together.
  // The constant is needed to make idef-based bounds check work: the pass
  // evaluates relations with "zero" and that zero cannot be created after GVN.
  GetConstant0();

#ifdef DEBUG
  // Do a full verify after building the graph and computing dominators.
  Verify(true);
#endif

  if (FLAG_analyze_environment_liveness && maximum_environment_size() != 0) {
    Run<HEnvironmentLivenessAnalysisPhase>();
  }

  if (!CheckConstPhiUses()) {
    *bailout_reason = kUnsupportedPhiUseOfConstVariable;
    return false;
  }
  Run<HRedundantPhiEliminationPhase>();
  if (!CheckArgumentsPhiUses()) {
    *bailout_reason = kUnsupportedPhiUseOfArguments;
    return false;
  }

  // Find and mark unreachable code to simplify optimizations, especially gvn,
  // where unreachable code could unnecessarily defeat LICM.
  Run<HMarkUnreachableBlocksPhase>();

  if (FLAG_dead_code_elimination) Run<HDeadCodeEliminationPhase>();
  if (FLAG_use_escape_analysis) Run<HEscapeAnalysisPhase>();

  if (FLAG_load_elimination) Run<HLoadEliminationPhase>();

  CollectPhis();

  Run<HInferRepresentationPhase>();

  // Remove HSimulate instructions that have turned out not to be needed
  // after all by folding them into the following HSimulate.
  // This must happen after inferring representations.
  Run<HMergeRemovableSimulatesPhase>();

  Run<HRepresentationChangesPhase>();

  Run<HInferTypesPhase>();

  // Must be performed before canonicalization to ensure that Canonicalize
  // will not remove semantically meaningful ToInt32 operations e.g. BIT_OR with
  // zero.
  Run<HUint32AnalysisPhase>();

  if (FLAG_use_canonicalizing) Run<HCanonicalizePhase>();

  if (FLAG_use_gvn) Run<HGlobalValueNumberingPhase>();

  if (FLAG_check_elimination) Run<HCheckEliminationPhase>();

  if (FLAG_store_elimination) Run<HStoreEliminationPhase>();

  Run<HRangeAnalysisPhase>();

  // Eliminate redundant stack checks on backwards branches.
  Run<HStackCheckEliminationPhase>();

  if (FLAG_array_bounds_checks_elimination) Run<HBoundsCheckEliminationPhase>();
  if (FLAG_array_index_dehoisting) Run<HDehoistIndexComputationsPhase>();
  if (FLAG_dead_code_elimination) Run<HDeadCodeEliminationPhase>();

  RestoreActualValues();

  // Find unreachable code a second time, GVN and other optimizations may have
  // made blocks unreachable that were previously reachable.
  Run<HMarkUnreachableBlocksPhase>();

  return true;
}


void HGraph::RestoreActualValues() {
  HPhase phase("H_Restore actual values", this);

  for (int block_index = 0; block_index < blocks()->length(); block_index++) {
    HBasicBlock* block = blocks()->at(block_index);

#ifdef DEBUG
    for (int i = 0; i < block->phis()->length(); i++) {
      HPhi* phi = block->phis()->at(i);
      DCHECK(phi->ActualValue() == phi);
    }
#endif

    for (HInstructionIterator it(block); !it.Done(); it.Advance()) {
      HInstruction* instruction = it.Current();
      if (instruction->ActualValue() == instruction) continue;
      if (instruction->CheckFlag(HValue::kIsDead)) {
        // The instruction was marked as deleted but left in the graph
        // as a control flow dependency point for subsequent
        // instructions.
        instruction->DeleteAndReplaceWith(instruction->ActualValue());
      } else {
        DCHECK(instruction->IsInformativeDefinition());
        if (instruction->IsPurelyInformativeDefinition()) {
          instruction->DeleteAndReplaceWith(instruction->RedefinedOperand());
        } else {
          instruction->ReplaceAllUsesWith(instruction->ActualValue());
        }
      }
    }
  }
}


HInstruction* HGraphBuilder::AddLoadStringInstanceType(HValue* string) {
  if (string->IsConstant()) {
    HConstant* c_string = HConstant::cast(string);
    if (c_string->HasStringValue()) {
      return Add<HConstant>(c_string->StringValue()->map()->instance_type());
    }
  }
  return Add<HLoadNamedField>(
      Add<HLoadNamedField>(string, nullptr, HObjectAccess::ForMap()), nullptr,
      HObjectAccess::ForMapInstanceType());
}


HInstruction* HGraphBuilder::AddLoadStringLength(HValue* string) {
  return AddInstruction(BuildLoadStringLength(string));
}


HInstruction* HGraphBuilder::BuildLoadStringLength(HValue* string) {
  if (string->IsConstant()) {
    HConstant* c_string = HConstant::cast(string);
    if (c_string->HasStringValue()) {
      return New<HConstant>(c_string->StringValue()->length());
    }
  }
  return New<HLoadNamedField>(string, nullptr,
                              HObjectAccess::ForStringLength());
}


HInstruction* HGraphBuilder::BuildConstantMapCheck(Handle<JSObject> constant,
                                                   bool ensure_no_elements) {
  HCheckMaps* check = Add<HCheckMaps>(
      Add<HConstant>(constant), handle(constant->map()));
  check->ClearDependsOnFlag(kElementsKind);
  if (ensure_no_elements) {
    // TODO(ishell): remove this once we support NO_ELEMENTS elements kind.
    HValue* elements = AddLoadElements(check, nullptr);
    HValue* empty_elements =
        Add<HConstant>(isolate()->factory()->empty_fixed_array());
    IfBuilder if_empty(this);
    if_empty.IfNot<HCompareObjectEqAndBranch>(elements, empty_elements);
    if_empty.ThenDeopt(DeoptimizeReason::kWrongMap);
    if_empty.End();
  }
  return check;
}

HInstruction* HGraphBuilder::BuildCheckPrototypeMaps(Handle<JSObject> prototype,
                                                     Handle<JSObject> holder,
                                                     bool ensure_no_elements) {
  PrototypeIterator iter(isolate(), prototype, kStartAtReceiver);
  while (holder.is_null() ||
         !PrototypeIterator::GetCurrent(iter).is_identical_to(holder)) {
    BuildConstantMapCheck(PrototypeIterator::GetCurrent<JSObject>(iter),
                          ensure_no_elements);
    iter.Advance();
    if (iter.IsAtEnd()) {
      return NULL;
    }
  }
  return BuildConstantMapCheck(holder);
}


// Checks if the given shift amounts have following forms:
// (N1) and (N2) with N1 + N2 = 32; (sa) and (32 - sa).
static bool ShiftAmountsAllowReplaceByRotate(HValue* sa,
                                             HValue* const32_minus_sa) {
  if (sa->IsConstant() && const32_minus_sa->IsConstant()) {
    const HConstant* c1 = HConstant::cast(sa);
    const HConstant* c2 = HConstant::cast(const32_minus_sa);
    return c1->HasInteger32Value() && c2->HasInteger32Value() &&
        (c1->Integer32Value() + c2->Integer32Value() == 32);
  }
  if (!const32_minus_sa->IsSub()) return false;
  HSub* sub = HSub::cast(const32_minus_sa);
  return sub->left()->EqualsInteger32Constant(32) && sub->right() == sa;
}


// Checks if the left and the right are shift instructions with the oposite
// directions that can be replaced by one rotate right instruction or not.
// Returns the operand and the shift amount for the rotate instruction in the
// former case.
bool HGraphBuilder::MatchRotateRight(HValue* left,
                                     HValue* right,
                                     HValue** operand,
                                     HValue** shift_amount) {
  HShl* shl;
  HShr* shr;
  if (left->IsShl() && right->IsShr()) {
    shl = HShl::cast(left);
    shr = HShr::cast(right);
  } else if (left->IsShr() && right->IsShl()) {
    shl = HShl::cast(right);
    shr = HShr::cast(left);
  } else {
    return false;
  }
  if (shl->left() != shr->left()) return false;

  if (!ShiftAmountsAllowReplaceByRotate(shl->right(), shr->right()) &&
      !ShiftAmountsAllowReplaceByRotate(shr->right(), shl->right())) {
    return false;
  }
  *operand = shr->left();
  *shift_amount = shr->right();
  return true;
}


bool CanBeZero(HValue* right) {
  if (right->IsConstant()) {
    HConstant* right_const = HConstant::cast(right);
    if (right_const->HasInteger32Value() &&
       (right_const->Integer32Value() & 0x1f) != 0) {
      return false;
    }
  }
  return true;
}

HValue* HGraphBuilder::EnforceNumberType(HValue* number, AstType* expected) {
  if (expected->Is(AstType::SignedSmall())) {
    return AddUncasted<HForceRepresentation>(number, Representation::Smi());
  }
  if (expected->Is(AstType::Signed32())) {
    return AddUncasted<HForceRepresentation>(number,
                                             Representation::Integer32());
  }
  return number;
}

HValue* HGraphBuilder::TruncateToNumber(HValue* value, AstType** expected) {
  if (value->IsConstant()) {
    HConstant* constant = HConstant::cast(value);
    Maybe<HConstant*> number =
        constant->CopyToTruncatedNumber(isolate(), zone());
    if (number.IsJust()) {
      *expected = AstType::Number();
      return AddInstruction(number.FromJust());
    }
  }

  // We put temporary values on the stack, which don't correspond to anything
  // in baseline code. Since nothing is observable we avoid recording those
  // pushes with a NoObservableSideEffectsScope.
  NoObservableSideEffectsScope no_effects(this);

  AstType* expected_type = *expected;

  // Separate the number type from the rest.
  AstType* expected_obj =
      AstType::Intersect(expected_type, AstType::NonNumber(), zone());
  AstType* expected_number =
      AstType::Intersect(expected_type, AstType::Number(), zone());

  // We expect to get a number.
  // (We need to check first, since AstType::None->Is(AstType::Any()) == true.
  if (expected_obj->Is(AstType::None())) {
    DCHECK(!expected_number->Is(AstType::None()));
    return value;
  }

  if (expected_obj->Is(AstType::Undefined())) {
    // This is already done by HChange.
    *expected = AstType::Union(expected_number, AstType::Number(), zone());
    return value;
  }

  return value;
}


static Representation RepresentationFor(AstType* type) {
  DisallowHeapAllocation no_allocation;
  if (type->Is(AstType::None())) return Representation::None();
  if (type->Is(AstType::SignedSmall())) return Representation::Smi();
  if (type->Is(AstType::Signed32())) return Representation::Integer32();
  if (type->Is(AstType::Number())) return Representation::Double();
  return Representation::Tagged();
}


HValue* HGraphBuilder::BuildBinaryOperation(
    Token::Value op, HValue* left, HValue* right, AstType* left_type,
    AstType* right_type, AstType* result_type, Maybe<int> fixed_right_arg,
    HAllocationMode allocation_mode, BailoutId opt_id) {
  bool maybe_string_add = false;
  if (op == Token::ADD) {
    // If we are adding constant string with something for which we don't have
    // a feedback yet, assume that it's also going to be a string and don't
    // generate deopt instructions.
    if (!left_type->IsInhabited() && right->IsConstant() &&
        HConstant::cast(right)->HasStringValue()) {
      left_type = AstType::String();
    }

    if (!right_type->IsInhabited() && left->IsConstant() &&
        HConstant::cast(left)->HasStringValue()) {
      right_type = AstType::String();
    }

    maybe_string_add = (left_type->Maybe(AstType::String()) ||
                        left_type->Maybe(AstType::Receiver()) ||
                        right_type->Maybe(AstType::String()) ||
                        right_type->Maybe(AstType::Receiver()));
  }

  Representation left_rep = RepresentationFor(left_type);
  Representation right_rep = RepresentationFor(right_type);

  if (!left_type->IsInhabited()) {
    Add<HDeoptimize>(
        DeoptimizeReason::kInsufficientTypeFeedbackForLHSOfBinaryOperation,
        Deoptimizer::SOFT);
    left_type = AstType::Any();
    left_rep = RepresentationFor(left_type);
    maybe_string_add = op == Token::ADD;
  }

  if (!right_type->IsInhabited()) {
    Add<HDeoptimize>(
        DeoptimizeReason::kInsufficientTypeFeedbackForRHSOfBinaryOperation,
        Deoptimizer::SOFT);
    right_type = AstType::Any();
    right_rep = RepresentationFor(right_type);
    maybe_string_add = op == Token::ADD;
  }

  if (!maybe_string_add) {
    left = TruncateToNumber(left, &left_type);
    right = TruncateToNumber(right, &right_type);
  }

  // Special case for string addition here.
  if (op == Token::ADD &&
      (left_type->Is(AstType::String()) || right_type->Is(AstType::String()))) {
    // Validate type feedback for left argument.
    if (left_type->Is(AstType::String())) {
      left = BuildCheckString(left);
    }

    // Validate type feedback for right argument.
    if (right_type->Is(AstType::String())) {
      right = BuildCheckString(right);
    }

    // Convert left argument as necessary.
    if (left_type->Is(AstType::Number())) {
      DCHECK(right_type->Is(AstType::String()));
      left = BuildNumberToString(left, left_type);
    } else if (!left_type->Is(AstType::String())) {
      DCHECK(right_type->Is(AstType::String()));
      return AddUncasted<HStringAdd>(
          left, right, allocation_mode.GetPretenureMode(),
          STRING_ADD_CONVERT_LEFT, allocation_mode.feedback_site());
    }

    // Convert right argument as necessary.
    if (right_type->Is(AstType::Number())) {
      DCHECK(left_type->Is(AstType::String()));
      right = BuildNumberToString(right, right_type);
    } else if (!right_type->Is(AstType::String())) {
      DCHECK(left_type->Is(AstType::String()));
      return AddUncasted<HStringAdd>(
          left, right, allocation_mode.GetPretenureMode(),
          STRING_ADD_CONVERT_RIGHT, allocation_mode.feedback_site());
    }

    // Fast paths for empty constant strings.
    Handle<String> left_string =
        left->IsConstant() && HConstant::cast(left)->HasStringValue()
            ? HConstant::cast(left)->StringValue()
            : Handle<String>();
    Handle<String> right_string =
        right->IsConstant() && HConstant::cast(right)->HasStringValue()
            ? HConstant::cast(right)->StringValue()
            : Handle<String>();
    if (!left_string.is_null() && left_string->length() == 0) return right;
    if (!right_string.is_null() && right_string->length() == 0) return left;
    if (!left_string.is_null() && !right_string.is_null()) {
      return AddUncasted<HStringAdd>(
          left, right, allocation_mode.GetPretenureMode(),
          STRING_ADD_CHECK_NONE, allocation_mode.feedback_site());
    }

    // Register the dependent code with the allocation site.
    if (!allocation_mode.feedback_site().is_null()) {
      DCHECK(!graph()->info()->IsStub());
      Handle<AllocationSite> site(allocation_mode.feedback_site());
      top_info()->dependencies()->AssumeTenuringDecision(site);
    }

    // Inline the string addition into the stub when creating allocation
    // mementos to gather allocation site feedback, or if we can statically
    // infer that we're going to create a cons string.
    if ((graph()->info()->IsStub() &&
         allocation_mode.CreateAllocationMementos()) ||
        (left->IsConstant() &&
         HConstant::cast(left)->HasStringValue() &&
         HConstant::cast(left)->StringValue()->length() + 1 >=
           ConsString::kMinLength) ||
        (right->IsConstant() &&
         HConstant::cast(right)->HasStringValue() &&
         HConstant::cast(right)->StringValue()->length() + 1 >=
           ConsString::kMinLength)) {
      return BuildStringAdd(left, right, allocation_mode);
    }

    // Fallback to using the string add stub.
    return AddUncasted<HStringAdd>(
        left, right, allocation_mode.GetPretenureMode(), STRING_ADD_CHECK_NONE,
        allocation_mode.feedback_site());
  }

  // Special case for +x here.
  if (op == Token::MUL) {
    if (left->EqualsInteger32Constant(1)) {
      return BuildToNumber(right);
    }
    if (right->EqualsInteger32Constant(1)) {
      return BuildToNumber(left);
    }
  }

  if (graph()->info()->IsStub()) {
    left = EnforceNumberType(left, left_type);
    right = EnforceNumberType(right, right_type);
  }

  Representation result_rep = RepresentationFor(result_type);

  bool is_non_primitive = (left_rep.IsTagged() && !left_rep.IsSmi()) ||
                          (right_rep.IsTagged() && !right_rep.IsSmi());

  HInstruction* instr = NULL;
  // Only the stub is allowed to call into the runtime, since otherwise we would
  // inline several instructions (including the two pushes) for every tagged
  // operation in optimized code, which is more expensive, than a stub call.
  if (graph()->info()->IsStub() && is_non_primitive) {
    HValue* values[] = {left, right};
#define GET_STUB(Name)                                                       \
  do {                                                                       \
    Callable callable = Builtins::CallableFor(isolate(), Builtins::k##Name); \
    HValue* stub = Add<HConstant>(callable.code());                          \
    instr = AddUncasted<HCallWithDescriptor>(stub, 0, callable.descriptor(), \
                                             ArrayVector(values));           \
  } while (false)

    switch (op) {
      default:
        UNREACHABLE();
      case Token::ADD:
        GET_STUB(Add);
        break;
      case Token::SUB:
        GET_STUB(Subtract);
        break;
      case Token::MUL:
        GET_STUB(Multiply);
        break;
      case Token::DIV:
        GET_STUB(Divide);
        break;
      case Token::MOD:
        GET_STUB(Modulus);
        break;
      case Token::BIT_OR:
        GET_STUB(BitwiseOr);
        break;
      case Token::BIT_AND:
        GET_STUB(BitwiseAnd);
        break;
      case Token::BIT_XOR:
        GET_STUB(BitwiseXor);
        break;
      case Token::SAR:
        GET_STUB(ShiftRight);
        break;
      case Token::SHR:
        GET_STUB(ShiftRightLogical);
        break;
      case Token::SHL:
        GET_STUB(ShiftLeft);
        break;
    }
#undef GET_STUB
  } else {
    switch (op) {
      case Token::ADD:
        instr = AddUncasted<HAdd>(left, right);
        break;
      case Token::SUB:
        instr = AddUncasted<HSub>(left, right);
        break;
      case Token::MUL:
        instr = AddUncasted<HMul>(left, right);
        break;
      case Token::MOD: {
        if (fixed_right_arg.IsJust() &&
            !right->EqualsInteger32Constant(fixed_right_arg.FromJust())) {
          HConstant* fixed_right =
              Add<HConstant>(static_cast<int>(fixed_right_arg.FromJust()));
          IfBuilder if_same(this);
          if_same.If<HCompareNumericAndBranch>(right, fixed_right, Token::EQ);
          if_same.Then();
          if_same.ElseDeopt(DeoptimizeReason::kUnexpectedRHSOfBinaryOperation);
          right = fixed_right;
        }
        instr = AddUncasted<HMod>(left, right);
        break;
      }
      case Token::DIV:
        instr = AddUncasted<HDiv>(left, right);
        break;
      case Token::BIT_XOR:
      case Token::BIT_AND:
        instr = AddUncasted<HBitwise>(op, left, right);
        break;
      case Token::BIT_OR: {
        HValue *operand, *shift_amount;
        if (left_type->Is(AstType::Signed32()) &&
            right_type->Is(AstType::Signed32()) &&
            MatchRotateRight(left, right, &operand, &shift_amount)) {
          instr = AddUncasted<HRor>(operand, shift_amount);
        } else {
          instr = AddUncasted<HBitwise>(op, left, right);
        }
        break;
      }
      case Token::SAR:
        instr = AddUncasted<HSar>(left, right);
        break;
      case Token::SHR:
        instr = AddUncasted<HShr>(left, right);
        if (instr->IsShr() && CanBeZero(right)) {
          graph()->RecordUint32Instruction(instr);
        }
        break;
      case Token::SHL:
        instr = AddUncasted<HShl>(left, right);
        break;
      default:
        UNREACHABLE();
    }
  }

  if (instr->IsBinaryOperation()) {
    HBinaryOperation* binop = HBinaryOperation::cast(instr);
    binop->set_observed_input_representation(1, left_rep);
    binop->set_observed_input_representation(2, right_rep);
    binop->initialize_output_representation(result_rep);
    if (graph()->info()->IsStub()) {
      // Stub should not call into stub.
      instr->SetFlag(HValue::kCannotBeTagged);
      // And should truncate on HForceRepresentation already.
      if (left->IsForceRepresentation()) {
        left->CopyFlag(HValue::kTruncatingToSmi, instr);
        left->CopyFlag(HValue::kTruncatingToInt32, instr);
      }
      if (right->IsForceRepresentation()) {
        right->CopyFlag(HValue::kTruncatingToSmi, instr);
        right->CopyFlag(HValue::kTruncatingToInt32, instr);
      }
    }
  }
  return instr;
}


HEnvironment::HEnvironment(HEnvironment* outer,
                           Scope* scope,
                           Handle<JSFunction> closure,
                           Zone* zone)
    : closure_(closure),
      values_(0, zone),
      frame_type_(JS_FUNCTION),
      parameter_count_(0),
      specials_count_(1),
      local_count_(0),
      outer_(outer),
      entry_(NULL),
      pop_count_(0),
      push_count_(0),
      ast_id_(BailoutId::None()),
      zone_(zone) {
  DeclarationScope* declaration_scope = scope->GetDeclarationScope();
  Initialize(declaration_scope->num_parameters() + 1,
             declaration_scope->num_stack_slots(), 0);
}


HEnvironment::HEnvironment(Zone* zone, int parameter_count)
    : values_(0, zone),
      frame_type_(STUB),
      parameter_count_(parameter_count),
      specials_count_(1),
      local_count_(0),
      outer_(NULL),
      entry_(NULL),
      pop_count_(0),
      push_count_(0),
      ast_id_(BailoutId::None()),
      zone_(zone) {
  Initialize(parameter_count, 0, 0);
}


HEnvironment::HEnvironment(const HEnvironment* other, Zone* zone)
    : values_(0, zone),
      frame_type_(JS_FUNCTION),
      parameter_count_(0),
      specials_count_(0),
      local_count_(0),
      outer_(NULL),
      entry_(NULL),
      pop_count_(0),
      push_count_(0),
      ast_id_(other->ast_id()),
      zone_(zone) {
  Initialize(other);
}


HEnvironment::HEnvironment(HEnvironment* outer,
                           Handle<JSFunction> closure,
                           FrameType frame_type,
                           int arguments,
                           Zone* zone)
    : closure_(closure),
      values_(arguments, zone),
      frame_type_(frame_type),
      parameter_count_(arguments),
      specials_count_(0),
      local_count_(0),
      outer_(outer),
      entry_(NULL),
      pop_count_(0),
      push_count_(0),
      ast_id_(BailoutId::None()),
      zone_(zone) {
}


void HEnvironment::Initialize(int parameter_count,
                              int local_count,
                              int stack_height) {
  parameter_count_ = parameter_count;
  local_count_ = local_count;

  // Avoid reallocating the temporaries' backing store on the first Push.
  int total = parameter_count + specials_count_ + local_count + stack_height;
  values_.Initialize(total + 4, zone());
  for (int i = 0; i < total; ++i) values_.Add(NULL, zone());
}


void HEnvironment::Initialize(const HEnvironment* other) {
  closure_ = other->closure();
  values_.AddAll(other->values_, zone());
  assigned_variables_.Union(other->assigned_variables_, zone());
  frame_type_ = other->frame_type_;
  parameter_count_ = other->parameter_count_;
  local_count_ = other->local_count_;
  if (other->outer_ != NULL) outer_ = other->outer_->Copy();  // Deep copy.
  entry_ = other->entry_;
  pop_count_ = other->pop_count_;
  push_count_ = other->push_count_;
  specials_count_ = other->specials_count_;
  ast_id_ = other->ast_id_;
}


void HEnvironment::AddIncomingEdge(HBasicBlock* block, HEnvironment* other) {
  DCHECK(!block->IsLoopHeader());
  DCHECK(values_.length() == other->values_.length());

  int length = values_.length();
  for (int i = 0; i < length; ++i) {
    HValue* value = values_[i];
    if (value != NULL && value->IsPhi() && value->block() == block) {
      // There is already a phi for the i'th value.
      HPhi* phi = HPhi::cast(value);
      // Assert index is correct and that we haven't missed an incoming edge.
      DCHECK(phi->merged_index() == i || !phi->HasMergedIndex());
      DCHECK(phi->OperandCount() == block->predecessors()->length());
      phi->AddInput(other->values_[i]);
    } else if (values_[i] != other->values_[i]) {
      // There is a fresh value on the incoming edge, a phi is needed.
      DCHECK(values_[i] != NULL && other->values_[i] != NULL);
      HPhi* phi = block->AddNewPhi(i);
      HValue* old_value = values_[i];
      for (int j = 0; j < block->predecessors()->length(); j++) {
        phi->AddInput(old_value);
      }
      phi->AddInput(other->values_[i]);
      this->values_[i] = phi;
    }
  }
}


void HEnvironment::Bind(int index, HValue* value) {
  DCHECK(value != NULL);
  assigned_variables_.Add(index, zone());
  values_[index] = value;
}


bool HEnvironment::HasExpressionAt(int index) const {
  return index >= parameter_count_ + specials_count_ + local_count_;
}


bool HEnvironment::ExpressionStackIsEmpty() const {
  DCHECK(length() >= first_expression_index());
  return length() == first_expression_index();
}


void HEnvironment::SetExpressionStackAt(int index_from_top, HValue* value) {
  int count = index_from_top + 1;
  int index = values_.length() - count;
  DCHECK(HasExpressionAt(index));
  // The push count must include at least the element in question or else
  // the new value will not be included in this environment's history.
  if (push_count_ < count) {
    // This is the same effect as popping then re-pushing 'count' elements.
    pop_count_ += (count - push_count_);
    push_count_ = count;
  }
  values_[index] = value;
}


HValue* HEnvironment::RemoveExpressionStackAt(int index_from_top) {
  int count = index_from_top + 1;
  int index = values_.length() - count;
  DCHECK(HasExpressionAt(index));
  // Simulate popping 'count' elements and then
  // pushing 'count - 1' elements back.
  pop_count_ += Max(count - push_count_, 0);
  push_count_ = Max(push_count_ - count, 0) + (count - 1);
  return values_.Remove(index);
}


void HEnvironment::Drop(int count) {
  for (int i = 0; i < count; ++i) {
    Pop();
  }
}


void HEnvironment::Print() const {
  OFStream os(stdout);
  os << *this << "\n";
}


HEnvironment* HEnvironment::Copy() const {
  return new(zone()) HEnvironment(this, zone());
}


HEnvironment* HEnvironment::CopyWithoutHistory() const {
  HEnvironment* result = Copy();
  result->ClearHistory();
  return result;
}


HEnvironment* HEnvironment::CopyAsLoopHeader(HBasicBlock* loop_header) const {
  HEnvironment* new_env = Copy();
  for (int i = 0; i < values_.length(); ++i) {
    HPhi* phi = loop_header->AddNewPhi(i);
    phi->AddInput(values_[i]);
    new_env->values_[i] = phi;
  }
  new_env->ClearHistory();
  return new_env;
}


HEnvironment* HEnvironment::CreateStubEnvironment(HEnvironment* outer,
                                                  Handle<JSFunction> target,
                                                  FrameType frame_type,
                                                  int arguments) const {
  HEnvironment* new_env =
      new(zone()) HEnvironment(outer, target, frame_type,
                               arguments + 1, zone());
  for (int i = 0; i <= arguments; ++i) {  // Include receiver.
    new_env->Push(ExpressionStackAt(arguments - i));
  }
  new_env->ClearHistory();
  return new_env;
}

void HEnvironment::MarkAsTailCaller() {
  DCHECK_EQ(JS_FUNCTION, frame_type());
  frame_type_ = TAIL_CALLER_FUNCTION;
}

void HEnvironment::ClearTailCallerMark() {
  DCHECK_EQ(TAIL_CALLER_FUNCTION, frame_type());
  frame_type_ = JS_FUNCTION;
}

HEnvironment* HEnvironment::CopyForInlining(
    Handle<JSFunction> target, int arguments, FunctionLiteral* function,
    HConstant* undefined, InliningKind inlining_kind,
    TailCallMode syntactic_tail_call_mode) const {
  DCHECK_EQ(JS_FUNCTION, frame_type());

  // Outer environment is a copy of this one without the arguments.
  int arity = function->scope()->num_parameters();

  HEnvironment* outer = Copy();
  outer->Drop(arguments + 1);  // Including receiver.
  outer->ClearHistory();

  if (syntactic_tail_call_mode == TailCallMode::kAllow) {
    DCHECK_EQ(NORMAL_RETURN, inlining_kind);
    outer->MarkAsTailCaller();
  }

  if (inlining_kind == CONSTRUCT_CALL_RETURN) {
    // Create artificial constructor stub environment.  The receiver should
    // actually be the constructor function, but we pass the newly allocated
    // object instead, DoComputeConstructStubFrame() relies on that.
    outer = CreateStubEnvironment(outer, target, JS_CONSTRUCT, arguments);
  } else if (inlining_kind == GETTER_CALL_RETURN) {
    // We need an additional StackFrame::INTERNAL frame for restoring the
    // correct context.
    outer = CreateStubEnvironment(outer, target, JS_GETTER, arguments);
  } else if (inlining_kind == SETTER_CALL_RETURN) {
    // We need an additional StackFrame::INTERNAL frame for temporarily saving
    // the argument of the setter, see StoreStubCompiler::CompileStoreViaSetter.
    outer = CreateStubEnvironment(outer, target, JS_SETTER, arguments);
  }

  if (arity != arguments) {
    // Create artificial arguments adaptation environment.
    outer = CreateStubEnvironment(outer, target, ARGUMENTS_ADAPTOR, arguments);
  }

  HEnvironment* inner =
      new(zone()) HEnvironment(outer, function->scope(), target, zone());
  // Get the argument values from the original environment.
  for (int i = 0; i <= arity; ++i) {  // Include receiver.
    HValue* push = (i <= arguments) ?
        ExpressionStackAt(arguments - i) : undefined;
    inner->SetValueAt(i, push);
  }
  inner->SetValueAt(arity + 1, context());
  for (int i = arity + 2; i < inner->length(); ++i) {
    inner->SetValueAt(i, undefined);
  }

  inner->set_ast_id(BailoutId::FunctionEntry());
  return inner;
}


std::ostream& operator<<(std::ostream& os, const HEnvironment& env) {
  for (int i = 0; i < env.length(); i++) {
    if (i == 0) os << "parameters\n";
    if (i == env.parameter_count()) os << "specials\n";
    if (i == env.parameter_count() + env.specials_count()) os << "locals\n";
    if (i == env.parameter_count() + env.specials_count() + env.local_count()) {
      os << "expressions\n";
    }
    HValue* val = env.values()->at(i);
    os << i << ": ";
    if (val != NULL) {
      os << val;
    } else {
      os << "NULL";
    }
    os << "\n";
  }
  return os << "\n";
}


void HTracer::TraceCompilation(CompilationInfo* info) {
  Tag tag(this, "compilation");
  std::string name;
  if (info->parse_info()) {
    Object* source_name = info->script()->name();
    if (source_name->IsString()) {
      String* str = String::cast(source_name);
      if (str->length() > 0) {
        name.append(str->ToCString().get());
        name.append(":");
      }
    }
  }
  std::unique_ptr<char[]> method_name = info->GetDebugName();
  name.append(method_name.get());
  if (info->IsOptimizing()) {
    PrintStringProperty("name", name.c_str());
    PrintIndent();
    trace_.Add("method \"%s:%d\"\n", method_name.get(),
               info->optimization_id());
  } else {
    PrintStringProperty("name", name.c_str());
    PrintStringProperty("method", "stub");
  }
  PrintLongProperty("date",
                    static_cast<int64_t>(base::OS::TimeCurrentMillis()));
}


void HTracer::TraceLithium(const char* name, LChunk* chunk) {
  DCHECK(!chunk->isolate()->concurrent_recompilation_enabled());
  AllowHandleDereference allow_deref;
  AllowDeferredHandleDereference allow_deferred_deref;
  Trace(name, chunk->graph(), chunk);
}


void HTracer::TraceHydrogen(const char* name, HGraph* graph) {
  DCHECK(!graph->isolate()->concurrent_recompilation_enabled());
  AllowHandleDereference allow_deref;
  AllowDeferredHandleDereference allow_deferred_deref;
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

    if (current->end()->SuccessorCount() == 0) {
      PrintEmptyProperty("successors");
    } else  {
      PrintIndent();
      trace_.Add("successors");
      for (HSuccessorIterator it(current->end()); !it.Done(); it.Advance()) {
        trace_.Add(" \"B%d\"", it.Current()->block_id());
      }
      trace_.Add("\n");
    }

    PrintEmptyProperty("xhandlers");

    {
      PrintIndent();
      trace_.Add("flags");
      if (current->IsLoopSuccessorDominator()) {
        trace_.Add(" \"dom-loop-succ\"");
      }
      if (current->IsUnreachable()) {
        trace_.Add(" \"dead\"");
      }
      if (current->is_osr_entry()) {
        trace_.Add(" \"osr\"");
      }
      trace_.Add("\n");
    }

    if (current->dominator() != NULL) {
      PrintBlockProperty("dominator", current->dominator()->block_id());
    }

    PrintIntProperty("loop_depth", current->LoopNestingDepth());

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
      PrintIntProperty("size", current->phis()->length());
      PrintStringProperty("method", "None");
      for (int j = 0; j < total; ++j) {
        HPhi* phi = current->phis()->at(j);
        PrintIndent();
        std::ostringstream os;
        os << phi->merged_index() << " " << NameOf(phi) << " " << *phi << "\n";
        trace_.Add(os.str().c_str());
      }
    }

    {
      Tag HIR_tag(this, "HIR");
      for (HInstructionIterator it(current); !it.Done(); it.Advance()) {
        HInstruction* instruction = it.Current();
        int uses = instruction->UseCount();
        PrintIndent();
        std::ostringstream os;
        os << "0 " << uses << " " << NameOf(instruction) << " " << *instruction;
        if (instruction->has_position()) {
          const SourcePosition pos = instruction->position();
          os << " pos:";
          if (pos.isInlined()) os << "inlining(" << pos.InliningId() << "),";
          os << pos.ScriptOffset();
        }
        os << " <|@\n";
        trace_.Add(os.str().c_str());
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
            PrintIndent();
            trace_.Add("%d ",
                       LifetimePosition::FromInstructionIndex(i).Value());
            linstr->PrintTo(&trace_);
            std::ostringstream os;
            os << " [hir:" << NameOf(linstr->hydrogen_value()) << "] <|@\n";
            trace_.Add(os.str().c_str());
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
    TraceLiveRange(fixed_d->at(i), "fixed", allocator->zone());
  }

  const Vector<LiveRange*>* fixed = allocator->fixed_live_ranges();
  for (int i = 0; i < fixed->length(); ++i) {
    TraceLiveRange(fixed->at(i), "fixed", allocator->zone());
  }

  const ZoneList<LiveRange*>* live_ranges = allocator->live_ranges();
  for (int i = 0; i < live_ranges->length(); ++i) {
    TraceLiveRange(live_ranges->at(i), "object", allocator->zone());
  }
}


void HTracer::TraceLiveRange(LiveRange* range, const char* type,
                             Zone* zone) {
  if (range != NULL && !range->IsEmpty()) {
    PrintIndent();
    trace_.Add("%d %s", range->id(), type);
    if (range->HasRegisterAssigned()) {
      LOperand* op = range->CreateAssignedOperand(zone);
      int assigned_reg = op->index();
      if (op->IsDoubleRegister()) {
        trace_.Add(" \"%s\"",
                   GetRegConfig()->GetDoubleRegisterName(assigned_reg));
      } else {
        DCHECK(op->IsRegister());
        trace_.Add(" \"%s\"",
                   GetRegConfig()->GetGeneralRegisterName(assigned_reg));
      }
    } else if (range->IsSpilled()) {
      LOperand* op = range->TopLevel()->GetSpillOperand();
      if (op->IsDoubleStackSlot()) {
        trace_.Add(" \"double_stack:%d\"", op->index());
      } else {
        DCHECK(op->IsStackSlot());
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
    if (op != NULL && op->IsUnallocated()) {
      hint_index = LUnallocated::cast(op)->virtual_register();
    }
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
  AppendChars(filename_.start(), trace_.ToCString().get(), trace_.length(),
              false);
  trace_.Reset();
}


void HStatistics::Initialize(CompilationInfo* info) {
  if (!info->has_shared_info()) return;
  source_size_ += info->shared_info()->SourceSize();
}


void HStatistics::Print() {
  PrintF(
      "\n"
      "----------------------------------------"
      "----------------------------------------\n"
      "--- Hydrogen timing results:\n"
      "----------------------------------------"
      "----------------------------------------\n");
  base::TimeDelta sum;
  for (int i = 0; i < times_.length(); ++i) {
    sum += times_[i];
  }

  for (int i = 0; i < names_.length(); ++i) {
    PrintF("%33s", names_[i]);
    double ms = times_[i].InMillisecondsF();
    double percent = times_[i].PercentOf(sum);
    PrintF(" %8.3f ms / %4.1f %% ", ms, percent);

    size_t size = sizes_[i];
    double size_percent = static_cast<double>(size) * 100 / total_size_;
    PrintF(" %9zu bytes / %4.1f %%\n", size, size_percent);
  }

  PrintF(
      "----------------------------------------"
      "----------------------------------------\n");
  base::TimeDelta total = create_graph_ + optimize_graph_ + generate_code_;
  PrintF("%33s %8.3f ms / %4.1f %% \n", "Create graph",
         create_graph_.InMillisecondsF(), create_graph_.PercentOf(total));
  PrintF("%33s %8.3f ms / %4.1f %% \n", "Optimize graph",
         optimize_graph_.InMillisecondsF(), optimize_graph_.PercentOf(total));
  PrintF("%33s %8.3f ms / %4.1f %% \n", "Generate and install code",
         generate_code_.InMillisecondsF(), generate_code_.PercentOf(total));
  PrintF(
      "----------------------------------------"
      "----------------------------------------\n");
  PrintF("%33s %8.3f ms           %9zu bytes\n", "Total",
         total.InMillisecondsF(), total_size_);
  PrintF("%33s     (%.1f times slower than full code gen)\n", "",
         total.TimesOf(full_code_gen_));

  double source_size_in_kb = static_cast<double>(source_size_) / 1024;
  double normalized_time =  source_size_in_kb > 0
      ? total.InMillisecondsF() / source_size_in_kb
      : 0;
  double normalized_size_in_kb =
      source_size_in_kb > 0
          ? static_cast<double>(total_size_) / 1024 / source_size_in_kb
          : 0;
  PrintF("%33s %8.3f ms           %7.3f kB allocated\n",
         "Average per kB source", normalized_time, normalized_size_in_kb);
}


void HStatistics::SaveTiming(const char* name, base::TimeDelta time,
                             size_t size) {
  total_size_ += size;
  for (int i = 0; i < names_.length(); ++i) {
    if (strcmp(names_[i], name) == 0) {
      times_[i] += time;
      sizes_[i] += size;
      return;
    }
  }
  names_.Add(name);
  times_.Add(time);
  sizes_.Add(size);
}


HPhase::~HPhase() {
  if (ShouldProduceTraceOutput()) {
    isolate()->GetHTracer()->TraceHydrogen(name(), graph_);
  }

#ifdef DEBUG
  graph_->Verify(false);  // No full verify.
#endif
}

}  // namespace internal
}  // namespace v8
