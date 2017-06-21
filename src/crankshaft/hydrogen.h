// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CRANKSHAFT_HYDROGEN_H_
#define V8_CRANKSHAFT_HYDROGEN_H_

#include "src/accessors.h"
#include "src/allocation.h"
#include "src/ast/ast-type-bounds.h"
#include "src/ast/scopes.h"
#include "src/bailout-reason.h"
#include "src/compilation-info.h"
#include "src/compiler.h"
#include "src/counters.h"
#include "src/crankshaft/compilation-phase.h"
#include "src/crankshaft/hydrogen-instructions.h"
#include "src/globals.h"
#include "src/parsing/parse-info.h"
#include "src/string-stream.h"
#include "src/transitions.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

// Forward declarations.
class BitVector;
class HEnvironment;
class HGraph;
class HLoopInformation;
class HTracer;
class LAllocator;
class LChunk;
class LiveRange;

class HBasicBlock final : public ZoneObject {
 public:
  explicit HBasicBlock(HGraph* graph);
  ~HBasicBlock() { }

  // Simple accessors.
  int block_id() const { return block_id_; }
  void set_block_id(int id) { block_id_ = id; }
  HGraph* graph() const { return graph_; }
  Isolate* isolate() const;
  const ZoneList<HPhi*>* phis() const { return &phis_; }
  HInstruction* first() const { return first_; }
  HInstruction* last() const { return last_; }
  void set_last(HInstruction* instr) { last_ = instr; }
  HControlInstruction* end() const { return end_; }
  HLoopInformation* loop_information() const { return loop_information_; }
  HLoopInformation* current_loop() const {
    return IsLoopHeader() ? loop_information()
                          : (parent_loop_header() != NULL
                            ? parent_loop_header()->loop_information() : NULL);
  }
  const ZoneList<HBasicBlock*>* predecessors() const { return &predecessors_; }
  bool HasPredecessor() const { return predecessors_.length() > 0; }
  const ZoneList<HBasicBlock*>* dominated_blocks() const {
    return &dominated_blocks_;
  }
  const ZoneList<int>* deleted_phis() const {
    return &deleted_phis_;
  }
  void RecordDeletedPhi(int merge_index) {
    deleted_phis_.Add(merge_index, zone());
  }
  HBasicBlock* dominator() const { return dominator_; }
  HEnvironment* last_environment() const { return last_environment_; }
  int argument_count() const { return argument_count_; }
  void set_argument_count(int count) { argument_count_ = count; }
  int first_instruction_index() const { return first_instruction_index_; }
  void set_first_instruction_index(int index) {
    first_instruction_index_ = index;
  }
  int last_instruction_index() const { return last_instruction_index_; }
  void set_last_instruction_index(int index) {
    last_instruction_index_ = index;
  }
  bool is_osr_entry() { return is_osr_entry_; }
  void set_osr_entry() { is_osr_entry_ = true; }

  void AttachLoopInformation();
  void DetachLoopInformation();
  bool IsLoopHeader() const { return loop_information() != NULL; }
  bool IsStartBlock() const { return block_id() == 0; }

  bool IsFinished() const { return end_ != NULL; }
  void AddPhi(HPhi* phi);
  void RemovePhi(HPhi* phi);
  void AddInstruction(HInstruction* instr, SourcePosition position);
  bool Dominates(HBasicBlock* other) const;
  bool EqualToOrDominates(HBasicBlock* other) const;
  int LoopNestingDepth() const;

  void SetInitialEnvironment(HEnvironment* env);
  void ClearEnvironment() {
    DCHECK(IsFinished());
    DCHECK(end()->SuccessorCount() == 0);
    last_environment_ = NULL;
  }
  bool HasEnvironment() const { return last_environment_ != NULL; }
  void UpdateEnvironment(HEnvironment* env);
  HBasicBlock* parent_loop_header() const { return parent_loop_header_; }

  void set_parent_loop_header(HBasicBlock* block) {
    DCHECK(parent_loop_header_ == NULL);
    parent_loop_header_ = block;
  }

  bool HasParentLoopHeader() const { return parent_loop_header_ != NULL; }

  void SetJoinId(BailoutId ast_id);

  int PredecessorIndexOf(HBasicBlock* predecessor) const;
  HPhi* AddNewPhi(int merged_index);
  HSimulate* AddNewSimulate(BailoutId ast_id, SourcePosition position,
                            RemovableSimulate removable = FIXED_SIMULATE) {
    HSimulate* instr = CreateSimulate(ast_id, removable);
    AddInstruction(instr, position);
    return instr;
  }
  void AssignCommonDominator(HBasicBlock* other);
  void AssignLoopSuccessorDominators();

  bool IsDeoptimizing() const {
    return end() != NULL && end()->IsDeoptimize();
  }

  void MarkUnreachable();
  bool IsUnreachable() const { return !is_reachable_; }
  bool IsReachable() const { return is_reachable_; }

  bool IsLoopSuccessorDominator() const {
    return dominates_loop_successors_;
  }
  void MarkAsLoopSuccessorDominator() {
    dominates_loop_successors_ = true;
  }

  bool IsOrdered() const { return is_ordered_; }
  void MarkAsOrdered() { is_ordered_ = true; }

  void MarkSuccEdgeUnreachable(int succ);

  inline Zone* zone() const;

#ifdef DEBUG
  void Verify();
#endif

 protected:
  friend class HGraphBuilder;

  HSimulate* CreateSimulate(BailoutId ast_id, RemovableSimulate removable);
  void Finish(HControlInstruction* last, SourcePosition position);
  void FinishExit(HControlInstruction* instruction, SourcePosition position);
  void Goto(HBasicBlock* block, SourcePosition position,
            bool add_simulate = true);
  void GotoNoSimulate(HBasicBlock* block, SourcePosition position) {
    Goto(block, position, false);
  }

 private:
  void RegisterPredecessor(HBasicBlock* pred);
  void AddDominatedBlock(HBasicBlock* block);

  int block_id_;
  HGraph* graph_;
  ZoneList<HPhi*> phis_;
  HInstruction* first_;
  HInstruction* last_;
  HControlInstruction* end_;
  HLoopInformation* loop_information_;
  ZoneList<HBasicBlock*> predecessors_;
  HBasicBlock* dominator_;
  ZoneList<HBasicBlock*> dominated_blocks_;
  HEnvironment* last_environment_;
  // Outgoing parameter count at block exit, set during lithium translation.
  int argument_count_;
  // Instruction indices into the lithium code stream.
  int first_instruction_index_;
  int last_instruction_index_;
  ZoneList<int> deleted_phis_;
  HBasicBlock* parent_loop_header_;
  bool is_reachable_ : 1;
  bool dominates_loop_successors_ : 1;
  bool is_osr_entry_ : 1;
  bool is_ordered_ : 1;
};


std::ostream& operator<<(std::ostream& os, const HBasicBlock& b);


class HPredecessorIterator final BASE_EMBEDDED {
 public:
  explicit HPredecessorIterator(HBasicBlock* block)
      : predecessor_list_(block->predecessors()), current_(0) { }

  bool Done() { return current_ >= predecessor_list_->length(); }
  HBasicBlock* Current() { return predecessor_list_->at(current_); }
  void Advance() { current_++; }

 private:
  const ZoneList<HBasicBlock*>* predecessor_list_;
  int current_;
};


class HInstructionIterator final BASE_EMBEDDED {
 public:
  explicit HInstructionIterator(HBasicBlock* block)
      : instr_(block->first()) {
    next_ = Done() ? NULL : instr_->next();
  }

  inline bool Done() const { return instr_ == NULL; }
  inline HInstruction* Current() { return instr_; }
  inline void Advance() {
    instr_ = next_;
    next_ = Done() ? NULL : instr_->next();
  }

 private:
  HInstruction* instr_;
  HInstruction* next_;
};


class HLoopInformation final : public ZoneObject {
 public:
  HLoopInformation(HBasicBlock* loop_header, Zone* zone)
      : back_edges_(4, zone),
        loop_header_(loop_header),
        blocks_(8, zone),
        stack_check_(NULL) {
    blocks_.Add(loop_header, zone);
  }
  ~HLoopInformation() {}

  const ZoneList<HBasicBlock*>* back_edges() const { return &back_edges_; }
  const ZoneList<HBasicBlock*>* blocks() const { return &blocks_; }
  HBasicBlock* loop_header() const { return loop_header_; }
  HBasicBlock* GetLastBackEdge() const;
  void RegisterBackEdge(HBasicBlock* block);

  HStackCheck* stack_check() const { return stack_check_; }
  void set_stack_check(HStackCheck* stack_check) {
    stack_check_ = stack_check;
  }

  bool IsNestedInThisLoop(HLoopInformation* other) {
    while (other != NULL) {
      if (other == this) {
        return true;
      }
      other = other->parent_loop();
    }
    return false;
  }
  HLoopInformation* parent_loop() {
    HBasicBlock* parent_header = loop_header()->parent_loop_header();
    return parent_header != NULL ? parent_header->loop_information() : NULL;
  }

 private:
  void AddBlock(HBasicBlock* block);

  ZoneList<HBasicBlock*> back_edges_;
  HBasicBlock* loop_header_;
  ZoneList<HBasicBlock*> blocks_;
  HStackCheck* stack_check_;
};

class HGraph final : public ZoneObject {
 public:
  explicit HGraph(CompilationInfo* info, CallInterfaceDescriptor descriptor);

  Isolate* isolate() const { return isolate_; }
  Zone* zone() const { return zone_; }
  CompilationInfo* info() const { return info_; }
  CallInterfaceDescriptor descriptor() const { return descriptor_; }

  const ZoneList<HBasicBlock*>* blocks() const { return &blocks_; }
  const ZoneList<HPhi*>* phi_list() const { return phi_list_; }
  HBasicBlock* entry_block() const { return entry_block_; }
  HEnvironment* start_environment() const { return start_environment_; }

  void FinalizeUniqueness();
  void OrderBlocks();
  void AssignDominators();
  void RestoreActualValues();

  // Returns false if there are phi-uses of the arguments-object
  // which are not supported by the optimizing compiler.
  bool CheckArgumentsPhiUses();

  // Returns false if there are phi-uses of an uninitialized const
  // which are not supported by the optimizing compiler.
  bool CheckConstPhiUses();

  void CollectPhis();

  HConstant* GetConstantUndefined();
  HConstant* GetConstant0();
  HConstant* GetConstant1();
  HConstant* GetConstantMinus1();
  HConstant* GetConstantTrue();
  HConstant* GetConstantFalse();
  HConstant* GetConstantBool(bool value);
  HConstant* GetConstantHole();
  HConstant* GetConstantNull();
  HConstant* GetConstantOptimizedOut();
  HConstant* GetInvalidContext();

  bool IsConstantUndefined(HConstant* constant);
  bool IsConstant0(HConstant* constant);
  bool IsConstant1(HConstant* constant);
  bool IsConstantMinus1(HConstant* constant);
  bool IsConstantTrue(HConstant* constant);
  bool IsConstantFalse(HConstant* constant);
  bool IsConstantHole(HConstant* constant);
  bool IsConstantNull(HConstant* constant);
  bool IsStandardConstant(HConstant* constant);

  HBasicBlock* CreateBasicBlock();

  int GetMaximumValueID() const { return values_.length(); }
  int GetNextBlockID() { return next_block_id_++; }
  int GetNextValueID(HValue* value) {
    DCHECK(!disallow_adding_new_values_);
    values_.Add(value, zone());
    return values_.length() - 1;
  }
  HValue* LookupValue(int id) const {
    if (id >= 0 && id < values_.length()) return values_[id];
    return NULL;
  }
  void DisallowAddingNewValues() {
    disallow_adding_new_values_ = true;
  }

  bool Optimize(BailoutReason* bailout_reason);

#ifdef DEBUG
  void Verify(bool do_full_verify) const;
#endif

  int update_type_change_checksum(int delta) {
    type_change_checksum_ += delta;
    return type_change_checksum_;
  }

  void update_maximum_environment_size(int environment_size) {
    if (environment_size > maximum_environment_size_) {
      maximum_environment_size_ = environment_size;
    }
  }
  int maximum_environment_size() { return maximum_environment_size_; }

  bool allow_code_motion() const { return allow_code_motion_; }
  void set_allow_code_motion(bool value) { allow_code_motion_ = value; }

  bool use_optimistic_licm() const { return use_optimistic_licm_; }
  void set_use_optimistic_licm(bool value) { use_optimistic_licm_ = value; }

  void MarkDependsOnEmptyArrayProtoElements() {
    // Add map dependency if not already added.
    if (depends_on_empty_array_proto_elements_) return;
    info()->dependencies()->AssumePropertyCell(
        isolate()->factory()->array_protector());
    depends_on_empty_array_proto_elements_ = true;
  }

  bool depends_on_empty_array_proto_elements() {
    return depends_on_empty_array_proto_elements_;
  }

  void MarkDependsOnStringLengthOverflow() {
    if (depends_on_string_length_overflow_) return;
    info()->dependencies()->AssumePropertyCell(
        isolate()->factory()->string_length_protector());
    depends_on_string_length_overflow_ = true;
  }

  bool has_uint32_instructions() {
    DCHECK(uint32_instructions_ == NULL || !uint32_instructions_->is_empty());
    return uint32_instructions_ != NULL;
  }

  ZoneList<HInstruction*>* uint32_instructions() {
    DCHECK(uint32_instructions_ == NULL || !uint32_instructions_->is_empty());
    return uint32_instructions_;
  }

  void RecordUint32Instruction(HInstruction* instr) {
    DCHECK(uint32_instructions_ == NULL || !uint32_instructions_->is_empty());
    if (uint32_instructions_ == NULL) {
      uint32_instructions_ = new(zone()) ZoneList<HInstruction*>(4, zone());
    }
    uint32_instructions_->Add(instr, zone());
  }

  void IncrementInNoSideEffectsScope() { no_side_effects_scope_count_++; }
  void DecrementInNoSideEffectsScope() { no_side_effects_scope_count_--; }
  bool IsInsideNoSideEffectsScope() { return no_side_effects_scope_count_ > 0; }

 private:
  HConstant* ReinsertConstantIfNecessary(HConstant* constant);
  HConstant* GetConstant(SetOncePointer<HConstant>* pointer,
                         int32_t integer_value);

  template<class Phase>
  void Run() {
    Phase phase(this);
    phase.Run();
  }

  Isolate* isolate_;
  int next_block_id_;
  HBasicBlock* entry_block_;
  HEnvironment* start_environment_;
  ZoneList<HBasicBlock*> blocks_;
  ZoneList<HValue*> values_;
  ZoneList<HPhi*>* phi_list_;
  ZoneList<HInstruction*>* uint32_instructions_;
  SetOncePointer<HConstant> constant_undefined_;
  SetOncePointer<HConstant> constant_0_;
  SetOncePointer<HConstant> constant_1_;
  SetOncePointer<HConstant> constant_minus1_;
  SetOncePointer<HConstant> constant_true_;
  SetOncePointer<HConstant> constant_false_;
  SetOncePointer<HConstant> constant_the_hole_;
  SetOncePointer<HConstant> constant_null_;
  SetOncePointer<HConstant> constant_optimized_out_;
  SetOncePointer<HConstant> constant_invalid_context_;

  CompilationInfo* info_;
  CallInterfaceDescriptor descriptor_;
  Zone* zone_;

  bool allow_code_motion_;
  bool use_optimistic_licm_;
  bool depends_on_empty_array_proto_elements_;
  bool depends_on_string_length_overflow_;
  int type_change_checksum_;
  int maximum_environment_size_;
  int no_side_effects_scope_count_;
  bool disallow_adding_new_values_;

  DISALLOW_COPY_AND_ASSIGN(HGraph);
};


Zone* HBasicBlock::zone() const { return graph_->zone(); }


// Type of stack frame an environment might refer to.
enum FrameType {
  JS_FUNCTION,
  JS_CONSTRUCT,
  JS_GETTER,
  JS_SETTER,
  ARGUMENTS_ADAPTOR,
  TAIL_CALLER_FUNCTION,
  STUB
};

class HEnvironment final : public ZoneObject {
 public:
  HEnvironment(HEnvironment* outer,
               Scope* scope,
               Handle<JSFunction> closure,
               Zone* zone);

  HEnvironment(Zone* zone, int parameter_count);

  HEnvironment* arguments_environment() {
    return outer()->frame_type() == ARGUMENTS_ADAPTOR ? outer() : this;
  }

  // Simple accessors.
  Handle<JSFunction> closure() const { return closure_; }
  const ZoneList<HValue*>* values() const { return &values_; }
  const GrowableBitVector* assigned_variables() const {
    return &assigned_variables_;
  }
  FrameType frame_type() const { return frame_type_; }
  int parameter_count() const { return parameter_count_; }
  int specials_count() const { return specials_count_; }
  int local_count() const { return local_count_; }
  HEnvironment* outer() const { return outer_; }
  int pop_count() const { return pop_count_; }
  int push_count() const { return push_count_; }

  BailoutId ast_id() const { return ast_id_; }
  void set_ast_id(BailoutId id) { ast_id_ = id; }

  HEnterInlined* entry() const { return entry_; }
  void set_entry(HEnterInlined* entry) { entry_ = entry; }

  int length() const { return values_.length(); }

  int first_expression_index() const {
    return parameter_count() + specials_count() + local_count();
  }

  int first_local_index() const {
    return parameter_count() + specials_count();
  }

  void Bind(Variable* variable, HValue* value) {
    Bind(IndexFor(variable), value);
  }

  void Bind(int index, HValue* value);

  void BindContext(HValue* value) {
    Bind(parameter_count(), value);
  }

  HValue* Lookup(Variable* variable) const {
    return Lookup(IndexFor(variable));
  }

  HValue* Lookup(int index) const {
    HValue* result = values_[index];
    DCHECK(result != NULL);
    return result;
  }

  HValue* context() const {
    // Return first special.
    return Lookup(parameter_count());
  }

  void Push(HValue* value) {
    DCHECK(value != NULL);
    ++push_count_;
    values_.Add(value, zone());
  }

  HValue* Pop() {
    DCHECK(!ExpressionStackIsEmpty());
    if (push_count_ > 0) {
      --push_count_;
    } else {
      ++pop_count_;
    }
    return values_.RemoveLast();
  }

  void Drop(int count);

  HValue* Top() const { return ExpressionStackAt(0); }

  bool ExpressionStackIsEmpty() const;

  HValue* ExpressionStackAt(int index_from_top) const {
    int index = length() - index_from_top - 1;
    DCHECK(HasExpressionAt(index));
    return values_[index];
  }

  void SetExpressionStackAt(int index_from_top, HValue* value);
  HValue* RemoveExpressionStackAt(int index_from_top);

  void Print() const;

  HEnvironment* Copy() const;
  HEnvironment* CopyWithoutHistory() const;
  HEnvironment* CopyAsLoopHeader(HBasicBlock* block) const;

  // Create an "inlined version" of this environment, where the original
  // environment is the outer environment but the top expression stack
  // elements are moved to an inner environment as parameters.
  HEnvironment* CopyForInlining(Handle<JSFunction> target, int arguments,
                                FunctionLiteral* function, HConstant* undefined,
                                InliningKind inlining_kind,
                                TailCallMode syntactic_tail_call_mode) const;

  HEnvironment* DiscardInlined(bool drop_extra) {
    HEnvironment* outer = outer_;
    while (outer->frame_type() != JS_FUNCTION &&
           outer->frame_type() != TAIL_CALLER_FUNCTION) {
      outer = outer->outer_;
    }
    if (drop_extra) outer->Drop(1);
    if (outer->frame_type() == TAIL_CALLER_FUNCTION) {
      outer->ClearTailCallerMark();
    }
    return outer;
  }

  void AddIncomingEdge(HBasicBlock* block, HEnvironment* other);

  void ClearHistory() {
    pop_count_ = 0;
    push_count_ = 0;
    assigned_variables_.Clear();
  }

  void SetValueAt(int index, HValue* value) {
    DCHECK(index < length());
    values_[index] = value;
  }

  // Map a variable to an environment index.  Parameter indices are shifted
  // by 1 (receiver is parameter index -1 but environment index 0).
  // Stack-allocated local indices are shifted by the number of parameters.
  int IndexFor(Variable* variable) const {
    DCHECK(variable->IsStackAllocated());
    int shift = variable->IsParameter()
        ? 1
        : parameter_count_ + specials_count_;
    return variable->index() + shift;
  }

  bool is_local_index(int i) const {
    return i >= first_local_index() && i < first_expression_index();
  }

  bool is_parameter_index(int i) const {
    return i >= 0 && i < parameter_count();
  }

  bool is_special_index(int i) const {
    return i >= parameter_count() && i < parameter_count() + specials_count();
  }

  Zone* zone() const { return zone_; }

 private:
  HEnvironment(const HEnvironment* other, Zone* zone);

  HEnvironment(HEnvironment* outer,
               Handle<JSFunction> closure,
               FrameType frame_type,
               int arguments,
               Zone* zone);

  // Create an artificial stub environment (e.g. for argument adaptor or
  // constructor stub).
  HEnvironment* CreateStubEnvironment(HEnvironment* outer,
                                      Handle<JSFunction> target,
                                      FrameType frame_type,
                                      int arguments) const;

  // Marks current environment as tail caller by setting frame type to
  // TAIL_CALLER_FUNCTION.
  void MarkAsTailCaller();
  void ClearTailCallerMark();

  // True if index is included in the expression stack part of the environment.
  bool HasExpressionAt(int index) const;

  void Initialize(int parameter_count, int local_count, int stack_height);
  void Initialize(const HEnvironment* other);

  Handle<JSFunction> closure_;
  // Value array [parameters] [specials] [locals] [temporaries].
  ZoneList<HValue*> values_;
  GrowableBitVector assigned_variables_;
  FrameType frame_type_;
  int parameter_count_;
  int specials_count_;
  int local_count_;
  HEnvironment* outer_;
  HEnterInlined* entry_;
  int pop_count_;
  int push_count_;
  BailoutId ast_id_;
  Zone* zone_;
};


std::ostream& operator<<(std::ostream& os, const HEnvironment& env);


enum ArgumentsAllowedFlag {
  ARGUMENTS_NOT_ALLOWED,
  ARGUMENTS_ALLOWED,
  ARGUMENTS_FAKED
};


class HIfContinuation;


class HIfContinuation final {
 public:
  HIfContinuation()
    : continuation_captured_(false),
      true_branch_(NULL),
      false_branch_(NULL) {}
  HIfContinuation(HBasicBlock* true_branch,
                  HBasicBlock* false_branch)
      : continuation_captured_(true), true_branch_(true_branch),
        false_branch_(false_branch) {}
  ~HIfContinuation() { DCHECK(!continuation_captured_); }

  void Capture(HBasicBlock* true_branch,
               HBasicBlock* false_branch) {
    DCHECK(!continuation_captured_);
    true_branch_ = true_branch;
    false_branch_ = false_branch;
    continuation_captured_ = true;
  }

  void Continue(HBasicBlock** true_branch,
                HBasicBlock** false_branch) {
    DCHECK(continuation_captured_);
    *true_branch = true_branch_;
    *false_branch = false_branch_;
    continuation_captured_ = false;
  }

  bool IsTrueReachable() { return true_branch_ != NULL; }
  bool IsFalseReachable() { return false_branch_ != NULL; }
  bool TrueAndFalseReachable() {
    return IsTrueReachable() || IsFalseReachable();
  }

  HBasicBlock* true_branch() const { return true_branch_; }
  HBasicBlock* false_branch() const { return false_branch_; }

 private:
  bool continuation_captured_;
  HBasicBlock* true_branch_;
  HBasicBlock* false_branch_;
};


class HAllocationMode final BASE_EMBEDDED {
 public:
  explicit HAllocationMode(Handle<AllocationSite> feedback_site)
      : current_site_(NULL), feedback_site_(feedback_site),
        pretenure_flag_(NOT_TENURED) {}
  explicit HAllocationMode(HValue* current_site)
      : current_site_(current_site), pretenure_flag_(NOT_TENURED) {}
  explicit HAllocationMode(PretenureFlag pretenure_flag)
      : current_site_(NULL), pretenure_flag_(pretenure_flag) {}
  HAllocationMode()
      : current_site_(NULL), pretenure_flag_(NOT_TENURED) {}

  HValue* current_site() const { return current_site_; }
  Handle<AllocationSite> feedback_site() const { return feedback_site_; }

  bool CreateAllocationMementos() const WARN_UNUSED_RESULT {
    return current_site() != NULL;
  }

  PretenureFlag GetPretenureMode() const WARN_UNUSED_RESULT {
    if (!feedback_site().is_null()) return feedback_site()->GetPretenureMode();
    return pretenure_flag_;
  }

 private:
  HValue* current_site_;
  Handle<AllocationSite> feedback_site_;
  PretenureFlag pretenure_flag_;
};


class HGraphBuilder {
 public:
  explicit HGraphBuilder(CompilationInfo* info,
                         CallInterfaceDescriptor descriptor,
                         bool track_positions)
      : info_(info),
        descriptor_(descriptor),
        graph_(NULL),
        current_block_(NULL),
        scope_(info->scope()),
        position_(SourcePosition::Unknown()),
        track_positions_(track_positions) {}
  virtual ~HGraphBuilder() {}

  Scope* scope() const { return scope_; }
  void set_scope(Scope* scope) { scope_ = scope; }

  HBasicBlock* current_block() const { return current_block_; }
  void set_current_block(HBasicBlock* block) { current_block_ = block; }
  HEnvironment* environment() const {
    return current_block()->last_environment();
  }
  Zone* zone() const { return info_->zone(); }
  HGraph* graph() const { return graph_; }
  Isolate* isolate() const { return graph_->isolate(); }
  CompilationInfo* top_info() { return info_; }

  HGraph* CreateGraph();

  // Bailout environment manipulation.
  void Push(HValue* value) { environment()->Push(value); }
  HValue* Pop() { return environment()->Pop(); }

  virtual HValue* context() = 0;

  // Adding instructions.
  HInstruction* AddInstruction(HInstruction* instr);
  void FinishCurrentBlock(HControlInstruction* last);
  void FinishExitCurrentBlock(HControlInstruction* instruction);

  void Goto(HBasicBlock* from,
            HBasicBlock* target,
            bool add_simulate = true) {
    from->Goto(target, source_position(), add_simulate);
  }
  void Goto(HBasicBlock* target,
            bool add_simulate = true) {
    Goto(current_block(), target, add_simulate);
  }
  void GotoNoSimulate(HBasicBlock* from, HBasicBlock* target) {
    Goto(from, target, false);
  }
  void GotoNoSimulate(HBasicBlock* target) { Goto(target, false); }

  template <class I>
  HInstruction* NewUncasted() {
    return I::New(isolate(), zone(), context());
  }

  template <class I>
  I* New() {
    return I::New(isolate(), zone(), context());
  }

  template<class I>
  HInstruction* AddUncasted() { return AddInstruction(NewUncasted<I>());}

  template<class I>
  I* Add() { return AddInstructionTyped(New<I>());}

  template<class I, class P1>
  HInstruction* NewUncasted(P1 p1) {
    return I::New(isolate(), zone(), context(), p1);
  }

  template <class I, class P1>
  I* New(P1 p1) {
    return I::New(isolate(), zone(), context(), p1);
  }

  template<class I, class P1>
  HInstruction* AddUncasted(P1 p1) {
    HInstruction* result = AddInstruction(NewUncasted<I>(p1));
    // Specializations must have their parameters properly casted
    // to avoid landing here.
    DCHECK(!result->IsReturn() && !result->IsSimulate() &&
           !result->IsDeoptimize());
    return result;
  }

  template<class I, class P1>
  I* Add(P1 p1) {
    I* result = AddInstructionTyped(New<I>(p1));
    // Specializations must have their parameters properly casted
    // to avoid landing here.
    DCHECK(!result->IsReturn() && !result->IsSimulate() &&
           !result->IsDeoptimize());
    return result;
  }

  template<class I, class P1, class P2>
  HInstruction* NewUncasted(P1 p1, P2 p2) {
    return I::New(isolate(), zone(), context(), p1, p2);
  }

  template<class I, class P1, class P2>
  I* New(P1 p1, P2 p2) {
    return I::New(isolate(), zone(), context(), p1, p2);
  }

  template<class I, class P1, class P2>
  HInstruction* AddUncasted(P1 p1, P2 p2) {
    HInstruction* result = AddInstruction(NewUncasted<I>(p1, p2));
    // Specializations must have their parameters properly casted
    // to avoid landing here.
    DCHECK(!result->IsSimulate());
    return result;
  }

  template<class I, class P1, class P2>
  I* Add(P1 p1, P2 p2) {
    I* result = AddInstructionTyped(New<I>(p1, p2));
    // Specializations must have their parameters properly casted
    // to avoid landing here.
    DCHECK(!result->IsSimulate());
    return result;
  }

  template<class I, class P1, class P2, class P3>
  HInstruction* NewUncasted(P1 p1, P2 p2, P3 p3) {
    return I::New(isolate(), zone(), context(), p1, p2, p3);
  }

  template<class I, class P1, class P2, class P3>
  I* New(P1 p1, P2 p2, P3 p3) {
    return I::New(isolate(), zone(), context(), p1, p2, p3);
  }

  template<class I, class P1, class P2, class P3>
  HInstruction* AddUncasted(P1 p1, P2 p2, P3 p3) {
    return AddInstruction(NewUncasted<I>(p1, p2, p3));
  }

  template<class I, class P1, class P2, class P3>
  I* Add(P1 p1, P2 p2, P3 p3) {
    return AddInstructionTyped(New<I>(p1, p2, p3));
  }

  template<class I, class P1, class P2, class P3, class P4>
  HInstruction* NewUncasted(P1 p1, P2 p2, P3 p3, P4 p4) {
    return I::New(isolate(), zone(), context(), p1, p2, p3, p4);
  }

  template<class I, class P1, class P2, class P3, class P4>
  I* New(P1 p1, P2 p2, P3 p3, P4 p4) {
    return I::New(isolate(), zone(), context(), p1, p2, p3, p4);
  }

  template<class I, class P1, class P2, class P3, class P4>
  HInstruction* AddUncasted(P1 p1, P2 p2, P3 p3, P4 p4) {
    return AddInstruction(NewUncasted<I>(p1, p2, p3, p4));
  }

  template<class I, class P1, class P2, class P3, class P4>
  I* Add(P1 p1, P2 p2, P3 p3, P4 p4) {
    return AddInstructionTyped(New<I>(p1, p2, p3, p4));
  }

  template<class I, class P1, class P2, class P3, class P4, class P5>
  HInstruction* NewUncasted(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) {
    return I::New(isolate(), zone(), context(), p1, p2, p3, p4, p5);
  }

  template<class I, class P1, class P2, class P3, class P4, class P5>
  I* New(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) {
    return I::New(isolate(), zone(), context(), p1, p2, p3, p4, p5);
  }

  template<class I, class P1, class P2, class P3, class P4, class P5>
  HInstruction* AddUncasted(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) {
    return AddInstruction(NewUncasted<I>(p1, p2, p3, p4, p5));
  }

  template<class I, class P1, class P2, class P3, class P4, class P5>
  I* Add(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) {
    return AddInstructionTyped(New<I>(p1, p2, p3, p4, p5));
  }

  template<class I, class P1, class P2, class P3, class P4, class P5, class P6>
  HInstruction* NewUncasted(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6) {
    return I::New(isolate(), zone(), context(), p1, p2, p3, p4, p5, p6);
  }

  template<class I, class P1, class P2, class P3, class P4, class P5, class P6>
  I* New(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6) {
    return I::New(isolate(), zone(), context(), p1, p2, p3, p4, p5, p6);
  }

  template<class I, class P1, class P2, class P3, class P4, class P5, class P6>
  HInstruction* AddUncasted(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6) {
    return AddInstruction(NewUncasted<I>(p1, p2, p3, p4, p5, p6));
  }

  template<class I, class P1, class P2, class P3, class P4, class P5, class P6>
  I* Add(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6) {
    return AddInstructionTyped(New<I>(p1, p2, p3, p4, p5, p6));
  }

  template<class I, class P1, class P2, class P3, class P4,
      class P5, class P6, class P7>
  HInstruction* NewUncasted(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7) {
    return I::New(isolate(), zone(), context(), p1, p2, p3, p4, p5, p6, p7);
  }

  template<class I, class P1, class P2, class P3, class P4,
      class P5, class P6, class P7>
      I* New(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7) {
    return I::New(isolate(), zone(), context(), p1, p2, p3, p4, p5, p6, p7);
  }

  template<class I, class P1, class P2, class P3,
           class P4, class P5, class P6, class P7>
  HInstruction* AddUncasted(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7) {
    return AddInstruction(NewUncasted<I>(p1, p2, p3, p4, p5, p6, p7));
  }

  template<class I, class P1, class P2, class P3,
           class P4, class P5, class P6, class P7>
  I* Add(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7) {
    return AddInstructionTyped(New<I>(p1, p2, p3, p4, p5, p6, p7));
  }

  template<class I, class P1, class P2, class P3, class P4,
      class P5, class P6, class P7, class P8>
  HInstruction* NewUncasted(P1 p1, P2 p2, P3 p3, P4 p4,
                            P5 p5, P6 p6, P7 p7, P8 p8) {
    return I::New(isolate(), zone(), context(), p1, p2, p3, p4, p5, p6, p7, p8);
  }

  template<class I, class P1, class P2, class P3, class P4,
      class P5, class P6, class P7, class P8>
      I* New(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7, P8 p8) {
    return I::New(isolate(), zone(), context(), p1, p2, p3, p4, p5, p6, p7, p8);
  }

  template<class I, class P1, class P2, class P3, class P4,
           class P5, class P6, class P7, class P8>
  HInstruction* AddUncasted(P1 p1, P2 p2, P3 p3, P4 p4,
                            P5 p5, P6 p6, P7 p7, P8 p8) {
    return AddInstruction(NewUncasted<I>(p1, p2, p3, p4, p5, p6, p7, p8));
  }

  template<class I, class P1, class P2, class P3, class P4,
           class P5, class P6, class P7, class P8>
  I* Add(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7, P8 p8) {
    return AddInstructionTyped(New<I>(p1, p2, p3, p4, p5, p6, p7, p8));
  }

  template <class I, class P1, class P2, class P3, class P4, class P5, class P6,
            class P7, class P8, class P9>
  I* New(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7, P8 p8, P9 p9) {
    return I::New(isolate(), zone(), context(), p1, p2, p3, p4, p5, p6, p7, p8,
                  p9);
  }

  template <class I, class P1, class P2, class P3, class P4, class P5, class P6,
            class P7, class P8, class P9>
  HInstruction* AddUncasted(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7,
                            P8 p8, P9 p9) {
    return AddInstruction(NewUncasted<I>(p1, p2, p3, p4, p5, p6, p7, p8, p9));
  }

  template <class I, class P1, class P2, class P3, class P4, class P5, class P6,
            class P7, class P8, class P9>
  I* Add(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7, P8 p8, P9 p9) {
    return AddInstructionTyped(New<I>(p1, p2, p3, p4, p5, p6, p7, p8, p9));
  }

  void AddSimulate(BailoutId id, RemovableSimulate removable = FIXED_SIMULATE);

  // When initializing arrays, we'll unfold the loop if the number of elements
  // is known at compile time and is <= kElementLoopUnrollThreshold.
  static const int kElementLoopUnrollThreshold = 8;

 protected:
  virtual bool BuildGraph() = 0;

  HBasicBlock* CreateBasicBlock(HEnvironment* env);
  HBasicBlock* CreateLoopHeaderBlock();

  template <class BitFieldClass>
  HValue* BuildDecodeField(HValue* encoded_field) {
    HValue* mask_value = Add<HConstant>(static_cast<int>(BitFieldClass::kMask));
    HValue* masked_field =
        AddUncasted<HBitwise>(Token::BIT_AND, encoded_field, mask_value);
    return AddUncasted<HShr>(masked_field,
        Add<HConstant>(static_cast<int>(BitFieldClass::kShift)));
  }

  HValue* BuildGetElementsKind(HValue* object);

  HValue* BuildEnumLength(HValue* map);

  HValue* BuildCheckHeapObject(HValue* object);
  HValue* BuildCheckString(HValue* string);
  HValue* BuildWrapReceiver(HValue* object, HValue* function);

  // Building common constructs
  HValue* BuildCheckForCapacityGrow(HValue* object,
                                    HValue* elements,
                                    ElementsKind kind,
                                    HValue* length,
                                    HValue* key,
                                    bool is_js_array,
                                    PropertyAccessType access_type);

  HValue* BuildCheckAndGrowElementsCapacity(HValue* object, HValue* elements,
                                            ElementsKind kind, HValue* length,
                                            HValue* capacity, HValue* key);

  HValue* BuildCopyElementsOnWrite(HValue* object,
                                   HValue* elements,
                                   ElementsKind kind,
                                   HValue* length);

  HValue* BuildNumberToString(HValue* object, AstType* type);
  HValue* BuildToNumber(HValue* input);
  HValue* BuildToObject(HValue* receiver);

  // Allocates a new object according with the given allocation properties.
  HAllocate* BuildAllocate(HValue* object_size,
                           HType type,
                           InstanceType instance_type,
                           HAllocationMode allocation_mode);
  // Computes the sum of two string lengths, taking care of overflow handling.
  HValue* BuildAddStringLengths(HValue* left_length, HValue* right_length);
  // Creates a cons string using the two input strings.
  HValue* BuildCreateConsString(HValue* length,
                                HValue* left,
                                HValue* right,
                                HAllocationMode allocation_mode);
  // Copies characters from one sequential string to another.
  void BuildCopySeqStringChars(HValue* src,
                               HValue* src_offset,
                               String::Encoding src_encoding,
                               HValue* dst,
                               HValue* dst_offset,
                               String::Encoding dst_encoding,
                               HValue* length);

  // Align an object size to object alignment boundary
  HValue* BuildObjectSizeAlignment(HValue* unaligned_size, int header_size);

  // Both operands are non-empty strings.
  HValue* BuildUncheckedStringAdd(HValue* left,
                                  HValue* right,
                                  HAllocationMode allocation_mode);
  // Add two strings using allocation mode, validating type feedback.
  HValue* BuildStringAdd(HValue* left,
                         HValue* right,
                         HAllocationMode allocation_mode);

  HInstruction* BuildUncheckedMonomorphicElementAccess(
      HValue* checked_object,
      HValue* key,
      HValue* val,
      bool is_js_array,
      ElementsKind elements_kind,
      PropertyAccessType access_type,
      LoadKeyedHoleMode load_mode,
      KeyedAccessStoreMode store_mode);

  HInstruction* AddElementAccess(
      HValue* elements, HValue* checked_key, HValue* val, HValue* dependency,
      HValue* backing_store_owner, ElementsKind elements_kind,
      PropertyAccessType access_type,
      LoadKeyedHoleMode load_mode = NEVER_RETURN_HOLE);

  HInstruction* AddLoadStringInstanceType(HValue* string);
  HInstruction* AddLoadStringLength(HValue* string);
  HInstruction* BuildLoadStringLength(HValue* string);
  HStoreNamedField* AddStoreMapConstant(HValue* object, Handle<Map> map) {
    return Add<HStoreNamedField>(object, HObjectAccess::ForMap(),
                                 Add<HConstant>(map));
  }
  HLoadNamedField* AddLoadMap(HValue* object,
                              HValue* dependency = NULL);
  HLoadNamedField* AddLoadElements(HValue* object,
                                   HValue* dependency = NULL);

  bool MatchRotateRight(HValue* left,
                        HValue* right,
                        HValue** operand,
                        HValue** shift_amount);

  HValue* BuildBinaryOperation(Token::Value op, HValue* left, HValue* right,
                               AstType* left_type, AstType* right_type,
                               AstType* result_type, Maybe<int> fixed_right_arg,
                               HAllocationMode allocation_mode,
                               BailoutId opt_id = BailoutId::None());

  HLoadNamedField* AddLoadFixedArrayLength(HValue *object,
                                           HValue *dependency = NULL);

  HLoadNamedField* AddLoadArrayLength(HValue *object,
                                      ElementsKind kind,
                                      HValue *dependency = NULL);

  HValue* EnforceNumberType(HValue* number, AstType* expected);
  HValue* TruncateToNumber(HValue* value, AstType** expected);

  void FinishExitWithHardDeoptimization(DeoptimizeReason reason);

  void AddIncrementCounter(StatsCounter* counter);

  class IfBuilder final {
   public:
    // If using this constructor, Initialize() must be called explicitly!
    IfBuilder();

    explicit IfBuilder(HGraphBuilder* builder);
    IfBuilder(HGraphBuilder* builder,
              HIfContinuation* continuation);

    ~IfBuilder() {
      if (!finished_) End();
    }

    void Initialize(HGraphBuilder* builder);

    template<class Condition>
    Condition* If(HValue *p) {
      Condition* compare = builder()->New<Condition>(p);
      AddCompare(compare);
      return compare;
    }

    template<class Condition, class P2>
    Condition* If(HValue* p1, P2 p2) {
      Condition* compare = builder()->New<Condition>(p1, p2);
      AddCompare(compare);
      return compare;
    }

    template<class Condition, class P2, class P3>
    Condition* If(HValue* p1, P2 p2, P3 p3) {
      Condition* compare = builder()->New<Condition>(p1, p2, p3);
      AddCompare(compare);
      return compare;
    }

    template<class Condition>
    Condition* IfNot(HValue* p) {
      Condition* compare = If<Condition>(p);
      compare->Not();
      return compare;
    }

    template<class Condition, class P2>
    Condition* IfNot(HValue* p1, P2 p2) {
      Condition* compare = If<Condition>(p1, p2);
      compare->Not();
      return compare;
    }

    template<class Condition, class P2, class P3>
    Condition* IfNot(HValue* p1, P2 p2, P3 p3) {
      Condition* compare = If<Condition>(p1, p2, p3);
      compare->Not();
      return compare;
    }

    template<class Condition>
    Condition* OrIf(HValue *p) {
      Or();
      return If<Condition>(p);
    }

    template<class Condition, class P2>
    Condition* OrIf(HValue* p1, P2 p2) {
      Or();
      return If<Condition>(p1, p2);
    }

    template<class Condition, class P2, class P3>
    Condition* OrIf(HValue* p1, P2 p2, P3 p3) {
      Or();
      return If<Condition>(p1, p2, p3);
    }

    template<class Condition>
    Condition* AndIf(HValue *p) {
      And();
      return If<Condition>(p);
    }

    template<class Condition, class P2>
    Condition* AndIf(HValue* p1, P2 p2) {
      And();
      return If<Condition>(p1, p2);
    }

    template<class Condition, class P2, class P3>
    Condition* AndIf(HValue* p1, P2 p2, P3 p3) {
      And();
      return If<Condition>(p1, p2, p3);
    }

    void Or();
    void And();

    // Captures the current state of this IfBuilder in the specified
    // continuation and ends this IfBuilder.
    void CaptureContinuation(HIfContinuation* continuation);

    // Joins the specified continuation from this IfBuilder and ends this
    // IfBuilder. This appends a Goto instruction from the true branch of
    // this IfBuilder to the true branch of the continuation unless the
    // true branch of this IfBuilder is already finished. And vice versa
    // for the false branch.
    //
    // The basic idea is as follows: You have several nested IfBuilder's
    // that you want to join based on two possible outcomes (i.e. success
    // and failure, or whatever). You can do this easily using this method
    // now, for example:
    //
    //   HIfContinuation cont(graph()->CreateBasicBlock(),
    //                        graph()->CreateBasicBlock());
    //   ...
    //     IfBuilder if_whatever(this);
    //     if_whatever.If<Condition>(arg);
    //     if_whatever.Then();
    //     ...
    //     if_whatever.Else();
    //     ...
    //     if_whatever.JoinContinuation(&cont);
    //   ...
    //     IfBuilder if_something(this);
    //     if_something.If<Condition>(arg1, arg2);
    //     if_something.Then();
    //     ...
    //     if_something.Else();
    //     ...
    //     if_something.JoinContinuation(&cont);
    //   ...
    //   IfBuilder if_finally(this, &cont);
    //   if_finally.Then();
    //   // continues after then code of if_whatever or if_something.
    //   ...
    //   if_finally.Else();
    //   // continues after else code of if_whatever or if_something.
    //   ...
    //   if_finally.End();
    void JoinContinuation(HIfContinuation* continuation);

    void Then();
    void Else();
    void End();
    void EndUnreachable();

    void Deopt(DeoptimizeReason reason);
    void ThenDeopt(DeoptimizeReason reason) {
      Then();
      Deopt(reason);
    }
    void ElseDeopt(DeoptimizeReason reason) {
      Else();
      Deopt(reason);
    }

    void Return(HValue* value);

   private:
    void InitializeDontCreateBlocks(HGraphBuilder* builder);

    HControlInstruction* AddCompare(HControlInstruction* compare);

    HGraphBuilder* builder() const {
      DCHECK(builder_ != NULL);  // Have you called "Initialize"?
      return builder_;
    }

    void AddMergeAtJoinBlock(bool deopt);

    void Finish();
    void Finish(HBasicBlock** then_continuation,
                HBasicBlock** else_continuation);

    class MergeAtJoinBlock : public ZoneObject {
     public:
      MergeAtJoinBlock(HBasicBlock* block,
                       bool deopt,
                       MergeAtJoinBlock* next)
        : block_(block),
          deopt_(deopt),
          next_(next) {}
      HBasicBlock* block_;
      bool deopt_;
      MergeAtJoinBlock* next_;
    };

    HGraphBuilder* builder_;
    bool finished_ : 1;
    bool did_then_ : 1;
    bool did_else_ : 1;
    bool did_else_if_ : 1;
    bool did_and_ : 1;
    bool did_or_ : 1;
    bool captured_ : 1;
    bool needs_compare_ : 1;
    bool pending_merge_block_ : 1;
    HBasicBlock* first_true_block_;
    HBasicBlock* first_false_block_;
    HBasicBlock* split_edge_merge_block_;
    MergeAtJoinBlock* merge_at_join_blocks_;
    int normal_merge_at_join_block_count_;
    int deopt_merge_at_join_block_count_;
  };

  class LoopBuilder final {
   public:
    enum Direction {
      kPreIncrement,
      kPostIncrement,
      kPreDecrement,
      kPostDecrement,
      kWhileTrue
    };

    explicit LoopBuilder(HGraphBuilder* builder);  // while (true) {...}
    LoopBuilder(HGraphBuilder* builder,
                HValue* context,
                Direction direction);
    LoopBuilder(HGraphBuilder* builder,
                HValue* context,
                Direction direction,
                HValue* increment_amount);

    ~LoopBuilder() {
      DCHECK(finished_);
    }

    HValue* BeginBody(
        HValue* initial,
        HValue* terminating,
        Token::Value token);

    void BeginBody(int drop_count);

    void Break();

    void EndBody();

   private:
    void Initialize(HGraphBuilder* builder, HValue* context,
                    Direction direction, HValue* increment_amount);
    Zone* zone() { return builder_->zone(); }

    HGraphBuilder* builder_;
    HValue* context_;
    HValue* increment_amount_;
    HInstruction* increment_;
    HPhi* phi_;
    HBasicBlock* header_block_;
    HBasicBlock* body_block_;
    HBasicBlock* exit_block_;
    HBasicBlock* exit_trampoline_block_;
    Direction direction_;
    bool finished_;
  };

  HValue* BuildNewElementsCapacity(HValue* old_capacity);

  HValue* BuildCalculateElementsSize(ElementsKind kind,
                                     HValue* capacity);
  HAllocate* AllocateJSArrayObject(AllocationSiteMode mode);
  HConstant* EstablishElementsAllocationSize(ElementsKind kind, int capacity);

  HAllocate* BuildAllocateElements(ElementsKind kind, HValue* size_in_bytes);

  void BuildInitializeElementsHeader(HValue* elements,
                                     ElementsKind kind,
                                     HValue* capacity);

  // Build allocation and header initialization code for respective successor
  // of FixedArrayBase.
  HValue* BuildAllocateAndInitializeArray(ElementsKind kind, HValue* capacity);

  // |array| must have been allocated with enough room for
  // 1) the JSArray and 2) an AllocationMemento if mode requires it.
  // If the |elements| value provided is NULL then the array elements storage
  // is initialized with empty array.
  void BuildJSArrayHeader(HValue* array,
                          HValue* array_map,
                          HValue* elements,
                          AllocationSiteMode mode,
                          ElementsKind elements_kind,
                          HValue* allocation_site_payload,
                          HValue* length_field);

  HValue* BuildGrowElementsCapacity(HValue* object,
                                    HValue* elements,
                                    ElementsKind kind,
                                    ElementsKind new_kind,
                                    HValue* length,
                                    HValue* new_capacity);

  void BuildFillElementsWithValue(HValue* elements,
                                  ElementsKind elements_kind,
                                  HValue* from,
                                  HValue* to,
                                  HValue* value);

  void BuildFillElementsWithHole(HValue* elements,
                                 ElementsKind elements_kind,
                                 HValue* from,
                                 HValue* to);

  void BuildCopyProperties(HValue* from_properties, HValue* to_properties,
                           HValue* length, HValue* capacity);

  void BuildCopyElements(HValue* from_elements,
                         ElementsKind from_elements_kind,
                         HValue* to_elements,
                         ElementsKind to_elements_kind,
                         HValue* length,
                         HValue* capacity);

  void BuildCreateAllocationMemento(HValue* previous_object,
                                    HValue* previous_object_size,
                                    HValue* payload);

  HInstruction* BuildConstantMapCheck(Handle<JSObject> constant,
                                      bool ensure_no_elements = false);
  HInstruction* BuildCheckPrototypeMaps(Handle<JSObject> prototype,
                                        Handle<JSObject> holder,
                                        bool ensure_no_elements = false);

  HInstruction* BuildGetNativeContext();

  HValue* BuildArrayBufferViewFieldAccessor(HValue* object,
                                            HValue* checked_object,
                                            FieldIndex index);


 protected:
  void SetSourcePosition(int position) {
    if (position != kNoSourcePosition) {
      position_.SetScriptOffset(position);
    }
    // Otherwise position remains unknown.
  }

  void EnterInlinedSource(int inlining_id) {
    if (is_tracking_positions()) {
      position_.SetInliningId(inlining_id);
    }
  }

  // Convert the given absolute offset from the start of the script to
  // the SourcePosition assuming that this position corresponds to the
  // same function as position_.
  SourcePosition ScriptPositionToSourcePosition(int position) {
    if (position == kNoSourcePosition) {
      return SourcePosition::Unknown();
    }
    return SourcePosition(position, position_.InliningId());
  }

  SourcePosition source_position() { return position_; }
  void set_source_position(SourcePosition position) { position_ = position; }

  bool is_tracking_positions() { return track_positions_; }

  HValue* BuildAllocateEmptyArrayBuffer(HValue* byte_length);
  template <typename ViewClass>
  void BuildArrayBufferViewInitialization(HValue* obj,
                                          HValue* buffer,
                                          HValue* byte_offset,
                                          HValue* byte_length);

 private:
  HGraphBuilder();

  template <class I>
  I* AddInstructionTyped(I* instr) {
    return I::cast(AddInstruction(instr));
  }

  CompilationInfo* info_;
  CallInterfaceDescriptor descriptor_;
  HGraph* graph_;
  HBasicBlock* current_block_;
  Scope* scope_;
  SourcePosition position_;
  bool track_positions_;
};

template <>
inline HDeoptimize* HGraphBuilder::Add<HDeoptimize>(
    DeoptimizeReason reason, Deoptimizer::BailoutType type) {
  if (type == Deoptimizer::SOFT) {
    isolate()->counters()->soft_deopts_requested()->Increment();
    if (FLAG_always_opt) return NULL;
  }
  if (current_block()->IsDeoptimizing()) return NULL;
  HBasicBlock* after_deopt_block = CreateBasicBlock(
      current_block()->last_environment());
  HDeoptimize* instr = New<HDeoptimize>(reason, type, after_deopt_block);
  if (type == Deoptimizer::SOFT) {
    isolate()->counters()->soft_deopts_inserted()->Increment();
  }
  FinishCurrentBlock(instr);
  set_current_block(after_deopt_block);
  return instr;
}

template <>
inline HInstruction* HGraphBuilder::AddUncasted<HDeoptimize>(
    DeoptimizeReason reason, Deoptimizer::BailoutType type) {
  return Add<HDeoptimize>(reason, type);
}


template<>
inline HSimulate* HGraphBuilder::Add<HSimulate>(
    BailoutId id,
    RemovableSimulate removable) {
  HSimulate* instr = current_block()->CreateSimulate(id, removable);
  AddInstruction(instr);
  return instr;
}


template<>
inline HSimulate* HGraphBuilder::Add<HSimulate>(
    BailoutId id) {
  return Add<HSimulate>(id, FIXED_SIMULATE);
}


template<>
inline HInstruction* HGraphBuilder::AddUncasted<HSimulate>(BailoutId id) {
  return Add<HSimulate>(id, FIXED_SIMULATE);
}


template<>
inline HReturn* HGraphBuilder::Add<HReturn>(HValue* value) {
  int num_parameters = graph()->info()->num_parameters();
  HValue* params = AddUncasted<HConstant>(num_parameters);
  HReturn* return_instruction = New<HReturn>(value, params);
  FinishExitCurrentBlock(return_instruction);
  return return_instruction;
}


template<>
inline HReturn* HGraphBuilder::Add<HReturn>(HConstant* value) {
  return Add<HReturn>(static_cast<HValue*>(value));
}

template<>
inline HInstruction* HGraphBuilder::AddUncasted<HReturn>(HValue* value) {
  return Add<HReturn>(value);
}


template<>
inline HInstruction* HGraphBuilder::AddUncasted<HReturn>(HConstant* value) {
  return Add<HReturn>(value);
}


template<>
inline HCallRuntime* HGraphBuilder::Add<HCallRuntime>(
    const Runtime::Function* c_function,
    int argument_count) {
  HCallRuntime* instr = New<HCallRuntime>(c_function, argument_count);
  if (graph()->info()->IsStub()) {
    // When compiling code stubs, we don't want to save all double registers
    // upon entry to the stub, but instead have the call runtime instruction
    // save the double registers only on-demand (in the fallback case).
    instr->set_save_doubles(kSaveFPRegs);
  }
  AddInstruction(instr);
  return instr;
}


template<>
inline HInstruction* HGraphBuilder::AddUncasted<HCallRuntime>(
    Handle<String> name,
    const Runtime::Function* c_function,
    int argument_count) {
  return Add<HCallRuntime>(c_function, argument_count);
}


template <>
inline HParameter* HGraphBuilder::New<HParameter>(unsigned index) {
  return HParameter::New(isolate(), zone(), nullptr, index);
}


template <>
inline HParameter* HGraphBuilder::New<HParameter>(
    unsigned index, HParameter::ParameterKind kind) {
  return HParameter::New(isolate(), zone(), nullptr, index, kind);
}


template <>
inline HParameter* HGraphBuilder::New<HParameter>(
    unsigned index, HParameter::ParameterKind kind, Representation r) {
  return HParameter::New(isolate(), zone(), nullptr, index, kind, r);
}


template <>
inline HPrologue* HGraphBuilder::New<HPrologue>() {
  return HPrologue::New(zone());
}


template <>
inline HContext* HGraphBuilder::New<HContext>() {
  return HContext::New(zone());
}


class HStatistics final : public Malloced {
 public:
  HStatistics()
      : times_(5),
        names_(5),
        sizes_(5),
        total_size_(0),
        source_size_(0) { }

  void Initialize(CompilationInfo* info);
  void Print();
  void SaveTiming(const char* name, base::TimeDelta time, size_t size);

  void IncrementFullCodeGen(base::TimeDelta full_code_gen) {
    full_code_gen_ += full_code_gen;
  }

  void IncrementCreateGraph(base::TimeDelta delta) { create_graph_ += delta; }

  void IncrementOptimizeGraph(base::TimeDelta delta) {
    optimize_graph_ += delta;
  }

  void IncrementGenerateCode(base::TimeDelta delta) { generate_code_ += delta; }

  void IncrementSubtotals(base::TimeDelta create_graph,
                          base::TimeDelta optimize_graph,
                          base::TimeDelta generate_code) {
    IncrementCreateGraph(create_graph);
    IncrementOptimizeGraph(optimize_graph);
    IncrementGenerateCode(generate_code);
  }

 private:
  List<base::TimeDelta> times_;
  List<const char*> names_;
  List<size_t> sizes_;
  base::TimeDelta create_graph_;
  base::TimeDelta optimize_graph_;
  base::TimeDelta generate_code_;
  size_t total_size_;
  base::TimeDelta full_code_gen_;
  double source_size_;
};


class HPhase : public CompilationPhase {
 public:
  HPhase(const char* name, HGraph* graph)
      : CompilationPhase(name, graph->info()),
        graph_(graph) { }
  ~HPhase();

 protected:
  HGraph* graph() const { return graph_; }

 private:
  HGraph* graph_;

  DISALLOW_COPY_AND_ASSIGN(HPhase);
};


class HTracer final : public Malloced {
 public:
  explicit HTracer(int isolate_id)
      : trace_(&string_allocator_), indent_(0) {
    if (FLAG_trace_hydrogen_file == NULL) {
      SNPrintF(filename_,
               "hydrogen-%d-%d.cfg",
               base::OS::GetCurrentProcessId(),
               isolate_id);
    } else {
      StrNCpy(filename_, FLAG_trace_hydrogen_file, filename_.length());
    }
    WriteChars(filename_.start(), "", 0, false);
  }

  void TraceCompilation(CompilationInfo* info);
  void TraceHydrogen(const char* name, HGraph* graph);
  void TraceLithium(const char* name, LChunk* chunk);
  void TraceLiveRanges(const char* name, LAllocator* allocator);

 private:
  class Tag final BASE_EMBEDDED {
   public:
    Tag(HTracer* tracer, const char* name) {
      name_ = name;
      tracer_ = tracer;
      tracer->PrintIndent();
      tracer->trace_.Add("begin_%s\n", name);
      tracer->indent_++;
    }

    ~Tag() {
      tracer_->indent_--;
      tracer_->PrintIndent();
      tracer_->trace_.Add("end_%s\n", name_);
      DCHECK(tracer_->indent_ >= 0);
      tracer_->FlushToFile();
    }

   private:
    HTracer* tracer_;
    const char* name_;
  };

  void TraceLiveRange(LiveRange* range, const char* type, Zone* zone);
  void Trace(const char* name, HGraph* graph, LChunk* chunk);
  void FlushToFile();

  void PrintEmptyProperty(const char* name) {
    PrintIndent();
    trace_.Add("%s\n", name);
  }

  void PrintStringProperty(const char* name, const char* value) {
    PrintIndent();
    trace_.Add("%s \"%s\"\n", name, value);
  }

  void PrintLongProperty(const char* name, int64_t value) {
    PrintIndent();
    trace_.Add("%s %d000\n", name, static_cast<int>(value / 1000));
  }

  void PrintBlockProperty(const char* name, int block_id) {
    PrintIndent();
    trace_.Add("%s \"B%d\"\n", name, block_id);
  }

  void PrintIntProperty(const char* name, int value) {
    PrintIndent();
    trace_.Add("%s %d\n", name, value);
  }

  void PrintIndent() {
    for (int i = 0; i < indent_; i++) {
      trace_.Add("  ");
    }
  }

  EmbeddedVector<char, 64> filename_;
  HeapStringAllocator string_allocator_;
  StringStream trace_;
  int indent_;
};


class NoObservableSideEffectsScope final {
 public:
  explicit NoObservableSideEffectsScope(HGraphBuilder* builder) :
      builder_(builder) {
    builder_->graph()->IncrementInNoSideEffectsScope();
  }
  ~NoObservableSideEffectsScope() {
    builder_->graph()->DecrementInNoSideEffectsScope();
  }

 private:
  HGraphBuilder* builder_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CRANKSHAFT_HYDROGEN_H_
