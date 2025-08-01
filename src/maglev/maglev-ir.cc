// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-ir.h"

#include <cmath>
#include <limits>
#include <optional>

#include "src/base/bounds.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/builtins/builtins-constructor.h"
#include "src/builtins/builtins-inl.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/interface-descriptors.h"
#include "src/codegen/macro-assembler.h"
#include "src/common/globals.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/fast-api-calls.h"
#include "src/compiler/heap-refs.h"
#include "src/deoptimizer/deoptimize-reason.h"
#include "src/execution/isolate-inl.h"
#include "src/heap/local-heap.h"
#include "src/heap/parked-scope.h"
#include "src/interpreter/bytecode-flags-and-tokens.h"
#include "src/objects/fixed-array.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-array.h"
#include "src/objects/js-generator.h"
#include "src/objects/property-cell.h"
#include "src/roots/static-roots.h"
#ifdef V8_ENABLE_MAGLEV
#include "src/maglev/maglev-assembler-inl.h"
#include "src/maglev/maglev-assembler.h"
#include "src/maglev/maglev-code-gen-state.h"
#endif
#include "src/maglev/maglev-compilation-unit.h"
#include "src/maglev/maglev-graph-labeller.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-ir-inl.h"
#include "src/roots/roots.h"

namespace v8 {
namespace internal {
namespace maglev {

#define __ masm->

const char* OpcodeToString(Opcode opcode) {
#define DEF_NAME(Name) #Name,
  static constexpr const char* const names[] = {NODE_BASE_LIST(DEF_NAME)};
#undef DEF_NAME
  return names[static_cast<int>(opcode)];
}

BasicBlock* Phi::predecessor_at(int i) {
  return merge_state_->predecessor_at(i);
}

namespace {

// Prevent people from accidentally using kScratchRegister here and having their
// code break in arm64.
[[maybe_unused]] struct Do_not_use_kScratchRegister_in_arch_independent_code {
} kScratchRegister;
[[maybe_unused]] struct
    Do_not_use_kScratchDoubleRegister_in_arch_independent_code {
} kScratchDoubleRegister;
static_assert(!std::is_same_v<decltype(kScratchRegister), Register>);
static_assert(
    !std::is_same_v<decltype(kScratchDoubleRegister), DoubleRegister>);

}  // namespace

#ifdef DEBUG
namespace {

template <size_t InputCount, typename Base, typename Derived>
int StaticInputCount(FixedInputNodeTMixin<InputCount, Base, Derived>*) {
  return InputCount;
}

int StaticInputCount(NodeBase*) { UNREACHABLE(); }

}  // namespace

void NodeBase::CheckCanOverwriteWith(Opcode new_opcode,
                                     OpProperties new_properties) {
  if (new_opcode == Opcode::kDead) return;

  DCHECK_IMPLIES(new_properties.can_eager_deopt(),
                 properties().can_eager_deopt());
  DCHECK_IMPLIES(new_properties.can_lazy_deopt(),
                 properties().can_lazy_deopt());
  DCHECK_IMPLIES(new_properties.needs_register_snapshot(),
                 properties().needs_register_snapshot());

  int old_input_count = input_count();
  size_t old_sizeof = -1;
  switch (opcode()) {
#define CASE(op)             \
  case Opcode::k##op:        \
    old_sizeof = sizeof(op); \
    break;
    NODE_BASE_LIST(CASE);
#undef CASE
  }

  switch (new_opcode) {
#define CASE(op)                                                          \
  case Opcode::k##op: {                                                   \
    DCHECK_EQ(old_input_count, StaticInputCount(static_cast<op*>(this))); \
    DCHECK_LE(sizeof(op), old_sizeof);                                    \
    break;                                                                \
  }
    NODE_BASE_LIST(CASE)
#undef CASE
  }
}

#endif  // DEBUG

std::ostream& operator<<(std::ostream& os, UseRepresentation repr) {
  switch (repr) {
    case UseRepresentation::kTagged:
      return os << "Tagged";
    case UseRepresentation::kInt32:
      return os << "Int32";
    case UseRepresentation::kTruncatedInt32:
      return os << "TruncatedInt32";
    case UseRepresentation::kUint32:
      return os << "Uint32";
    case UseRepresentation::kFloat64:
      return os << "Float64";
    case UseRepresentation::kHoleyFloat64:
      return os << "HoleyFloat64";
  }
}

bool Phi::is_loop_phi() const { return merge_state()->is_loop(); }

bool Phi::is_unmerged_loop_phi() const {
  DCHECK(is_loop_phi());
  return merge_state()->is_unmerged_loop();
}

void Phi::RecordUseReprHint(UseRepresentationSet repr_mask) {
  if (is_loop_phi() && is_unmerged_loop_phi()) {
    same_loop_uses_repr_hint_.Add(repr_mask);
  }

  if (!repr_mask.is_subset_of(uses_repr_hint_)) {
    uses_repr_hint_.Add(repr_mask);

    // Propagate in inputs, ignoring unbounded loop backedges.
    int bound_inputs = input_count();
    if (merge_state()->is_unmerged_loop()) --bound_inputs;

    for (int i = 0; i < bound_inputs; i++) {
      if (Phi* phi_input = input(i).node()->TryCast<Phi>()) {
        phi_input->RecordUseReprHint(repr_mask);
      }
    }
  }
}

void Phi::SetUseRequires31BitValue() {
  if (uses_require_31_bit_value()) return;
  set_uses_require_31_bit_value();
  auto inputs =
      is_loop_phi() ? merge_state_->predecessors_so_far() : input_count();
  for (uint32_t i = 0; i < inputs; ++i) {
    ValueNode* input_node = input(i).node();
    DCHECK(input_node);
    if (auto phi = input_node->TryCast<Phi>()) {
      phi->SetUseRequires31BitValue();
    }
  }
}

InitialValue::InitialValue(uint64_t bitfield, interpreter::Register source)
    : Base(bitfield), source_(source) {}

namespace {

// ---
// Print
// ---

bool IsStoreToNonEscapedObject(const NodeBase* node) {
  if (CanBeStoreToNonEscapedObject(node->opcode())) {
    DCHECK_GT(node->input_count(), 0);
    if (InlinedAllocation* alloc =
            node->input(0).node()->template TryCast<InlinedAllocation>()) {
      return alloc->HasBeenAnalysed() && alloc->HasBeenElided();
    }
  }
  return false;
}

void PrintInputs(std::ostream& os, const NodeBase* node,
                 bool has_regalloc_data) {
  if (!node->has_inputs()) return;

  os << " [";
  for (int i = 0; i < node->input_count(); i++) {
    if (i != 0) os << ", ";
    GetCurrentGraphLabeller()->PrintInput(os, node->input(i),
                                          has_regalloc_data);
  }
  os << "]";
}

void PrintResult(std::ostream& os, const NodeBase* node,
                 bool has_regalloc_data) {}

void PrintResult(std::ostream& os, const ValueNode* node,
                 bool has_regalloc_data) {
  if (has_regalloc_data) {
    auto node_info = node->regalloc_info();
    os << " → " << node_info->result().operand();
    if (node_info->result().operand().IsAllocated() &&
        node_info->is_spilled() &&
        node_info->spill_slot() != node_info->result().operand()) {
      os << " (spilled: " << node_info->spill_slot() << ")";
    }
    if (node_info->has_valid_live_range()) {
      os << ", live range: [" << node_info->live_range().start << "-"
         << node_info->live_range().end << "]";
    }
  } else {
    os << ", " << node->use_count() << " uses";
    if (const InlinedAllocation* alloc = node->TryCast<InlinedAllocation>()) {
      os << " (" << alloc->non_escaping_use_count() << " non escaping uses)";
      if (alloc->HasBeenAnalysed() && alloc->HasBeenElided()) {
        os << " 🪦";
      }
    } else if (!node->is_used()) {
      if (node->opcode() != Opcode::kAllocationBlock &&
          node->properties().is_required_when_unused()) {
        os << ", but required";
      } else {
        os << " 🪦";
      }
    }
    // Don't print to non float64 nodes, nor to nodes that we bypass the flag.
    if (node->value_representation() == ValueRepresentation::kFloat64 &&
        !node->Is<Float64Constant>() && !node->Is<ChangeInt32ToFloat64>()) {
      if (node->can_truncate_to_int32()) {
        os << ", can truncate to int32 " << node->GetRange();
      } else {
        os << ", cannot truncate to int32";
      }
    }
  }
}

void PrintTargets(std::ostream& os, const NodeBase* node) {}

void PrintTargets(std::ostream& os, const UnconditionalControlNode* node) {
  os << " b" << node->target()->id();
}

void PrintTargets(std::ostream& os, const BranchControlNode* node) {
  os << " b" << node->if_true()->id() << " b" << node->if_false()->id();
}

void PrintTargets(std::ostream& os, const Switch* node) {
  for (int i = 0; i < node->size(); i++) {
    const BasicBlockRef& target = node->Cast<Switch>()->targets()[i];
    os << " b" << target.block_ptr()->id();
  }
  if (node->Cast<Switch>()->has_fallthrough()) {
    BasicBlock* fallthrough_target = node->Cast<Switch>()->fallthrough();
    os << " b" << fallthrough_target->id();
  }
}

class MaybeUnparkForPrint {
 public:
  MaybeUnparkForPrint() {
    LocalHeap* local_heap = LocalHeap::Current();
    if (!local_heap) {
      local_heap = Isolate::Current()->main_thread_local_heap();
    }
    DCHECK_NOT_NULL(local_heap);
    if (local_heap->IsParked()) {
      scope_.emplace(local_heap);
    }
  }

 private:
  std::optional<UnparkedScope> scope_;
};

template <typename NodeT>
void PrintImpl(std::ostream& os, const NodeT* node, bool has_regalloc_data,
               bool skip_targets) {
  MaybeUnparkForPrint unpark;
  os << node->opcode();
  node->PrintParams(os);
  PrintInputs(os, node, has_regalloc_data);
  PrintResult(os, node, has_regalloc_data);
  if (IsStoreToNonEscapedObject(node)) {
    os << " 🪦";
  }
  if (!skip_targets) {
    PrintTargets(os, node);
  }
}

bool RootToBoolean(RootIndex index) {
  switch (index) {
    case RootIndex::kFalseValue:
    case RootIndex::kNullValue:
    case RootIndex::kUndefinedValue:
    case RootIndex::kNanValue:
    case RootIndex::kUndefinedNanValue:
    case RootIndex::kHoleNanValue:
    case RootIndex::kMinusZeroValue:
    case RootIndex::kempty_string:
#ifdef V8_ENABLE_WEBASSEMBLY
    case RootIndex::kWasmNull:
#endif
      return false;
    default:
      return true;
  }
}

#ifdef DEBUG
// For all RO roots, check that RootToBoolean returns the same value as
// BooleanValue on that root.
bool CheckToBooleanOnAllRoots(LocalIsolate* local_isolate) {
  ReadOnlyRoots roots(local_isolate);
  // Use the READ_ONLY_ROOT_LIST macro list rather than a for loop to get nicer
  // error messages if there is a failure.
#define DO_CHECK(type, name, CamelName)                                   \
  /* Ignore 'undefined' roots that are not the undefined value itself. */ \
  if (roots.name() != roots.undefined_value() ||                          \
      RootIndex::k##CamelName == RootIndex::kUndefinedValue) {            \
    DCHECK_EQ(Object::BooleanValue(roots.name(), local_isolate),          \
              RootToBoolean(RootIndex::k##CamelName));                    \
  }
  READ_ONLY_ROOT_LIST(DO_CHECK)
#undef DO_CHECK
  return true;
}
#endif

}  // namespace

void VirtualObjectList::Print(std::ostream& os, const char* prefix) const {
  os << prefix;
  for (const VirtualObject* vo : *this) {
    GetCurrentGraphLabeller()->PrintNodeLabel(os, vo, false);
    os << "; ";
  }
  os << std::endl;
}

void DeoptInfo::InitializeInputLocations(Zone* zone, size_t count) {
  DCHECK_NULL(input_locations_);
  input_locations_ = zone->AllocateArray<InputLocation>(count);
  // Initialise locations so that they correctly don't have a next use id.
  for (size_t i = 0; i < count; ++i) {
    new (&input_locations_[i]) InputLocation();
  }
#ifdef DEBUG
  input_location_count_ = count;
#endif  // DEBUG
}

bool RootConstant::ToBoolean(LocalIsolate* local_isolate) const {
#ifdef DEBUG
  // (Ab)use static locals to call CheckToBooleanOnAllRoots once, on first
  // call to this function.
  static bool check_once = CheckToBooleanOnAllRoots(local_isolate);
  DCHECK(check_once);
#endif
  // ToBoolean is only supported for RO roots.
  DCHECK(RootsTable::IsReadOnly(index_));
  return RootToBoolean(index_);
}

bool FromConstantToBool(LocalIsolate* local_isolate, ValueNode* node) {
  DCHECK(IsConstantNode(node->opcode()));
  switch (node->opcode()) {
#define CASE(Name)                                       \
  case Opcode::k##Name: {                                \
    return node->Cast<Name>()->ToBoolean(local_isolate); \
  }
    CONSTANT_VALUE_NODE_LIST(CASE)
#undef CASE
    default:
      UNREACHABLE();
  }
}

void Input::clear() {
  node_->remove_use();
  node_ = nullptr;
}

DeoptInfo::DeoptInfo(Zone* zone, DeoptFrame* top_frame,
                     compiler::FeedbackSource feedback_to_update)
    : top_frame_(top_frame), feedback_to_update_(feedback_to_update) {}

bool LazyDeoptInfo::IsResultRegister(interpreter::Register reg) const {
  if (top_frame().type() == DeoptFrame::FrameType::kConstructInvokeStubFrame) {
    return reg == interpreter::Register::virtual_accumulator();
  }
  if (V8_LIKELY(result_size() == 1)) {
    return reg == result_location_;
  }
  if (result_size() == 0) {
    return false;
  }
  DCHECK_EQ(result_size(), 2);
  return reg == result_location_ ||
         reg == interpreter::Register(result_location_.index() + 1);
}

bool LazyDeoptInfo::InReturnValues(interpreter::Register reg,
                                   interpreter::Register result_location,
                                   int result_size) {
  if (result_size == 0 || !result_location.is_valid()) {
    return false;
  }
  return base::IsInRange(reg.index(), result_location.index(),
                         result_location.index() + result_size - 1);
}

int InterpretedDeoptFrame::ComputeReturnOffset(
    interpreter::Register result_location, int result_size) const {
  // Return offsets are counted from the end of the translation frame,
  // which is the array [parameters..., locals..., accumulator]. Since
  // it's the end, we don't need to worry about earlier frames.
  if (result_location == interpreter::Register::virtual_accumulator()) {
    return 0;
  } else if (result_location.is_parameter()) {
    // This is slightly tricky to reason about because of zero indexing
    // and fence post errors. As an example, consider a frame with 2
    // locals and 2 parameters, where we want argument index 1 -- looking
    // at the array in reverse order we have:
    //   [acc, r1, r0, a1, a0]
    //                  ^
    // and this calculation gives, correctly:
    //   2 + 2 - 1 = 3
    return unit().register_count() + unit().parameter_count() -
           result_location.ToParameterIndex();
  } else {
    return unit().register_count() - result_location.index();
  }
}

const InterpretedDeoptFrame& LazyDeoptInfo::GetFrameForExceptionHandler(
    const ExceptionHandlerInfo* handler_info) {
  const DeoptFrame* target_frame = &top_frame();
  for (int i = 0;; i++) {
    while (target_frame->type() != DeoptFrame::FrameType::kInterpretedFrame) {
      target_frame = target_frame->parent();
    }
    if (i == handler_info->depth()) break;
    target_frame = target_frame->parent();
  }
  return target_frame->as_interpreted();
}

void NodeBase::Print(std::ostream& os, bool has_regalloc_data,
                     bool skip_targets) const {
  switch (opcode()) {
#define V(Name)         \
  case Opcode::k##Name: \
    return PrintImpl(os, this->Cast<Name>(), has_regalloc_data, skip_targets);
    NODE_BASE_LIST(V)
#undef V
  }
  UNREACHABLE();
}

void NodeBase::Print() const {
  Print(std::cout);
  std::cout << std::endl;
}

void ValueNode::SetHint(compiler::InstructionOperand hint) {
  DCHECK_EQ(state_, kRegallocInfo);
  auto node_info = regalloc_info();
  if (node_info->has_hint()) return;
  node_info->set_hint(hint);
  auto result = node_info->result().operand();
  if (node_info->result().operand().IsUnallocated()) {
    auto operand = compiler::UnallocatedOperand::cast(result);
    if (operand.HasSameAsInputPolicy()) {
      input(operand.input_index()).node()->SetHint(hint);
    }
  }
  if (this->Is<Phi>()) {
    Phi* phi = Cast<Phi>();
    for (int i = 0; i < input_count(); i++) {
      if (phi->is_loop_phi() && phi->is_backedge_offset(i)) {
        continue;
      }
      input(i).node()->SetHint(hint);
    }
  }
}

compiler::OptionalHeapObjectRef ValueNode::TryGetConstant(
    compiler::JSHeapBroker* broker) {
  if (Constant* c = TryCast<Constant>()) {
    return c->object();
  }
  if (RootConstant* c = TryCast<RootConstant>()) {
    return MakeRef(broker, broker->local_isolate()->root_handle(c->index()))
        .AsHeapObject();
  }
  return {};
}

NodeType ValueNode::GetStaticType(compiler::JSHeapBroker* broker) {
  switch (properties().value_representation()) {
    case ValueRepresentation::kInt32:
    case ValueRepresentation::kUint32:
    case ValueRepresentation::kFloat64:
    case ValueRepresentation::kIntPtr:
      return NodeType::kNumber;
    case ValueRepresentation::kHoleyFloat64:
      return NodeType::kNumberOrOddball;
    case ValueRepresentation::kTagged:
      break;
    case ValueRepresentation::kNone:
      UNREACHABLE();
  }
  switch (opcode()) {
    case Opcode::kPhi:
      return Cast<Phi>()->type();
    case Opcode::kReturnedValue:
      return input(0).node()->GetStaticType(broker);
    case Opcode::kCheckedSmiTagInt32:
    case Opcode::kCheckedSmiTagUint32:
    case Opcode::kCheckedSmiTagIntPtr:
    case Opcode::kCheckedSmiTagFloat64:
    case Opcode::kUnsafeSmiTagInt32:
    case Opcode::kUnsafeSmiTagUint32:
    case Opcode::kUnsafeSmiTagIntPtr:
    case Opcode::kSmiConstant:
      return NodeType::kSmi;
    case Opcode::kInt32ToNumber:
    case Opcode::kUint32ToNumber:
    case Opcode::kIntPtrToNumber:
    case Opcode::kFloat64ToTagged:
      return NodeType::kNumber;
    case Opcode::kHoleyFloat64ToTagged:
      return NodeType::kNumberOrOddball;
    case Opcode::kAllocationBlock:
    case Opcode::kInlinedAllocation: {
      auto obj = Cast<InlinedAllocation>()->object();
      if (obj->has_static_map()) {
        return StaticTypeForMap(obj->map(), broker);
      } else {
        switch (obj->type()) {
          case VirtualObject::kConsString:
            return NodeType::kString;
          case VirtualObject::kDefault:
          case VirtualObject::kHeapNumber:
          case VirtualObject::kFixedDoubleArray:
            UNREACHABLE();
        }
      }
    }
    case Opcode::kRootConstant: {
      RootConstant* constant = Cast<RootConstant>();
      switch (constant->index()) {
        case RootIndex::kTrueValue:
        case RootIndex::kFalseValue:
          return NodeType::kBoolean;
        case RootIndex::kUndefinedValue:
        case RootIndex::kNullValue:
          return NodeType::kOddball;
        default:
          break;
      }
      [[fallthrough]];
    }
    case Opcode::kConstant: {
      compiler::HeapObjectRef ref = TryGetConstant(broker).value();
      return StaticTypeForConstant(broker, ref);
    }
    case Opcode::kToNumberOrNumeric:
      if (Cast<ToNumberOrNumeric>()->mode() == Object::Conversion::kToNumber) {
        return NodeType::kNumber;
      }
      // TODO(verwaest): Check what we need here.
      return NodeType::kUnknown;
    case Opcode::kBuiltinStringPrototypeCharCodeOrCodePointAt:
    case Opcode::kBuiltinSeqOneByteStringCharCodeAt:
      return NodeType::kNumber;
    case Opcode::kToString:
    case Opcode::kNumberToString:
    case Opcode::kUnwrapStringWrapper:
    case Opcode::kStringAt:
    case Opcode::kStringConcat:
    case Opcode::kBuiltinStringFromCharCode:
    case Opcode::kNewConsString:
      return NodeType::kString;
    case Opcode::kCheckedInternalizedString:
      return NodeType::kInternalizedString;
    case Opcode::kSeqOneByteStringAt:
      // Seq one-byte StringAt will always return an internalized string from
      // the single character roots.
      return NodeType::kROSeqInternalizedOneByteString;
    case Opcode::kToObject:
    case Opcode::kCreateObjectLiteral:
    case Opcode::kCreateShallowObjectLiteral:
      return NodeType::kJSReceiver;
    case Opcode::kCreateArrayLiteral:
    case Opcode::kCreateShallowArrayLiteral:
      return NodeType::kJSArray;
    case Opcode::kToName:
      return NodeType::kName;
    case Opcode::kFastCreateClosure:
    case Opcode::kCreateClosure:
      return NodeType::kJSFunction;
    case Opcode::kInt32Compare:
    case Opcode::kFloat64Compare:
    case Opcode::kGenericEqual:
    case Opcode::kGenericStrictEqual:
    case Opcode::kGenericLessThan:
    case Opcode::kGenericLessThanOrEqual:
    case Opcode::kGenericGreaterThan:
    case Opcode::kGenericGreaterThanOrEqual:
    case Opcode::kLogicalNot:
    case Opcode::kStringEqual:
    case Opcode::kTaggedEqual:
    case Opcode::kTaggedNotEqual:
    case Opcode::kTestInstanceOf:
    case Opcode::kTestTypeOf:
    case Opcode::kTestUndetectable:
    case Opcode::kToBoolean:
    case Opcode::kToBooleanLogicalNot:
    case Opcode::kIntPtrToBoolean:
    case Opcode::kSetPrototypeHas:
      return NodeType::kBoolean;
    case Opcode::kCreateFunctionContext:
      return NodeType::kContext;
    case Opcode::kCallRuntime:
      switch (Cast<CallRuntime>()->function_id()) {
        case Runtime::kPushBlockContext:
        case Runtime::kPushCatchContext:
        case Runtime::kNewFunctionContext:
          return NodeType::kContext;
        default:
          return NodeType::kUnknown;
      }
      UNREACHABLE();
      // Not value nodes:
#define GENERATE_CASE(Name) case Opcode::k##Name:
      CONTROL_NODE_LIST(GENERATE_CASE)
      NON_VALUE_NODE_LIST(GENERATE_CASE)
#undef GENERATE_CASE
      UNREACHABLE();
    case Opcode::kCreateFastArrayElements:
    case Opcode::kTransitionElementsKind:
    // Unsorted value nodes. TODO(maglev): See which of these should return
    // something else than kUnknown.
    case Opcode::kIdentity:
    case Opcode::kArgumentsElements:
    case Opcode::kArgumentsLength:
    case Opcode::kRestLength:
    case Opcode::kCall:
    case Opcode::kCallBuiltin:
    case Opcode::kCallCPPBuiltin:
    case Opcode::kCallForwardVarargs:
    case Opcode::kCallWithArrayLike:
    case Opcode::kCallWithSpread:
    case Opcode::kCallKnownApiFunction:
    case Opcode::kCallKnownJSFunction:
    case Opcode::kCallSelf:
    case Opcode::kConstruct:
    case Opcode::kCheckConstructResult:
    case Opcode::kCheckDerivedConstructResult:
    case Opcode::kConstructWithSpread:
    case Opcode::kConvertReceiver:
    case Opcode::kConvertHoleToUndefined:
    case Opcode::kCreateRegExpLiteral:
    case Opcode::kDeleteProperty:
    case Opcode::kEnsureWritableFastElements:
    case Opcode::kExtendPropertiesBackingStore:
    case Opcode::kForInPrepare:
    case Opcode::kForInNext:
    case Opcode::kGeneratorRestoreRegister:
    case Opcode::kGetIterator:
    case Opcode::kGetSecondReturnedValue:
    case Opcode::kGetTemplateObject:
    case Opcode::kHasInPrototypeChain:
    case Opcode::kInitialValue:
    case Opcode::kLoadTaggedField:
    case Opcode::kLoadTaggedFieldForProperty:
    case Opcode::kLoadTaggedFieldForContextSlotNoCells:
    case Opcode::kLoadTaggedFieldForContextSlot:
    case Opcode::kLoadDoubleField:
    case Opcode::kLoadFloat64:
    case Opcode::kLoadInt32:
    case Opcode::kLoadTaggedFieldByFieldIndex:
    case Opcode::kLoadFixedArrayElement:
    case Opcode::kLoadFixedDoubleArrayElement:
    case Opcode::kLoadHoleyFixedDoubleArrayElement:
    case Opcode::kLoadHoleyFixedDoubleArrayElementCheckedNotHole:
#ifdef V8_ENABLE_EXPERIMENTAL_UNDEFINED_DOUBLE
    case Opcode::kLoadHoleyFixedDoubleArrayElementCheckedNotUndefinedOrHole:
#endif  // V8_ENABLE_EXPERIMENTAL_UNDEFINED_DOUBLE
    case Opcode::kLoadSignedIntDataViewElement:
    case Opcode::kLoadDoubleDataViewElement:
    case Opcode::kLoadTypedArrayLength:
    case Opcode::kLoadSignedIntTypedArrayElement:
    case Opcode::kLoadUnsignedIntTypedArrayElement:
    case Opcode::kLoadDoubleTypedArrayElement:
    case Opcode::kLoadSignedIntConstantTypedArrayElement:
    case Opcode::kLoadUnsignedIntConstantTypedArrayElement:
    case Opcode::kLoadDoubleConstantTypedArrayElement:
    case Opcode::kLoadEnumCacheLength:
    case Opcode::kLoadGlobal:
    case Opcode::kLoadNamedGeneric:
    case Opcode::kLoadNamedFromSuperGeneric:
    case Opcode::kMaybeGrowFastElements:
    case Opcode::kMigrateMapIfNeeded:
    case Opcode::kSetNamedGeneric:
    case Opcode::kDefineNamedOwnGeneric:
    case Opcode::kStoreInArrayLiteralGeneric:
    case Opcode::kStoreGlobal:
    case Opcode::kGetKeyedGeneric:
    case Opcode::kSetKeyedGeneric:
    case Opcode::kDefineKeyedOwnGeneric:
    case Opcode::kRegisterInput:
    case Opcode::kCheckedSmiSizedInt32:
    case Opcode::kCheckedSmiUntag:
    case Opcode::kUnsafeSmiUntag:
    case Opcode::kCheckedObjectToIndex:
    case Opcode::kCheckedTruncateNumberOrOddballToInt32:
    case Opcode::kCheckedInt32ToUint32:
    case Opcode::kCheckedIntPtrToUint32:
    case Opcode::kUnsafeInt32ToUint32:
    case Opcode::kCheckedUint32ToInt32:
    case Opcode::kCheckedIntPtrToInt32:
    case Opcode::kChangeInt32ToFloat64:
    case Opcode::kChangeUint32ToFloat64:
    case Opcode::kChangeIntPtrToFloat64:
    case Opcode::kCheckedTruncateFloat64ToInt32:
    case Opcode::kCheckedTruncateFloat64ToUint32:
    case Opcode::kTruncateNumberOrOddballToInt32:
    case Opcode::kCheckedNumberToInt32:
    case Opcode::kTruncateUint32ToInt32:
    case Opcode::kTruncateFloat64ToInt32:
    case Opcode::kUnsafeTruncateUint32ToInt32:
    case Opcode::kUnsafeTruncateFloat64ToInt32:
    case Opcode::kInt32ToUint8Clamped:
    case Opcode::kUint32ToUint8Clamped:
    case Opcode::kFloat64ToUint8Clamped:
    case Opcode::kCheckedNumberToUint8Clamped:
    case Opcode::kFloat64ToHeapNumberForField:
    case Opcode::kCheckedNumberOrOddballToFloat64:
    case Opcode::kUncheckedNumberOrOddballToFloat64:
    case Opcode::kCheckedNumberOrOddballToHoleyFloat64:
    case Opcode::kCheckedHoleyFloat64ToFloat64:
    case Opcode::kHoleyFloat64ToMaybeNanFloat64:
#ifdef V8_ENABLE_EXPERIMENTAL_UNDEFINED_DOUBLE
    case Opcode::kFloat64ToHoleyFloat64:
    case Opcode::kConvertHoleNanToUndefinedNan:
    case Opcode::kHoleyFloat64IsUndefinedOrHole:
#else
    case Opcode::kHoleyFloat64IsHole:
#endif  // V8_ENABLE_EXPERIMENTAL_UNDEFINED_DOUBLE
    case Opcode::kSetPendingMessage:
    case Opcode::kStringLength:
    case Opcode::kAllocateElementsArray:
    case Opcode::kUpdateJSArrayLength:
    case Opcode::kVirtualObject:
    case Opcode::kGetContinuationPreservedEmbedderData:
    case Opcode::kFloat64Constant:
    case Opcode::kInt32Constant:
    case Opcode::kUint32Constant:
    case Opcode::kIntPtrConstant:
    case Opcode::kTaggedIndexConstant:
    case Opcode::kTrustedConstant:
    case Opcode::kInt32Add:
    case Opcode::kInt32Subtract:
    case Opcode::kInt32Multiply:
    case Opcode::kInt32MultiplyOverflownBits:
    case Opcode::kInt32Divide:
    case Opcode::kInt32AbsWithOverflow:
    case Opcode::kInt32AddWithOverflow:
    case Opcode::kInt32SubtractWithOverflow:
    case Opcode::kInt32MultiplyWithOverflow:
    case Opcode::kInt32DivideWithOverflow:
    case Opcode::kInt32ModulusWithOverflow:
    case Opcode::kInt32BitwiseAnd:
    case Opcode::kInt32BitwiseOr:
    case Opcode::kInt32BitwiseXor:
    case Opcode::kInt32ShiftLeft:
    case Opcode::kInt32ShiftRight:
    case Opcode::kInt32ShiftRightLogical:
    case Opcode::kInt32BitwiseNot:
    case Opcode::kInt32NegateWithOverflow:
    case Opcode::kInt32IncrementWithOverflow:
    case Opcode::kInt32DecrementWithOverflow:
    case Opcode::kInt32ToBoolean:
    case Opcode::kFloat64Abs:
    case Opcode::kFloat64Add:
    case Opcode::kFloat64Subtract:
    case Opcode::kFloat64Multiply:
    case Opcode::kFloat64Divide:
    case Opcode::kFloat64Exponentiate:
    case Opcode::kFloat64Modulus:
    case Opcode::kFloat64Negate:
    case Opcode::kFloat64Round:
    case Opcode::kFloat64ToBoolean:
    case Opcode::kFloat64Ieee754Unary:
    case Opcode::kFloat64Ieee754Binary:
    case Opcode::kFloat64Sqrt:
    case Opcode::kInt32CountLeadingZeros:
    case Opcode::kTaggedCountLeadingZeros:
    case Opcode::kFloat64CountLeadingZeros:
    case Opcode::kCheckedSmiIncrement:
    case Opcode::kCheckedSmiDecrement:
    case Opcode::kGenericAdd:
    case Opcode::kGenericSubtract:
    case Opcode::kGenericMultiply:
    case Opcode::kGenericDivide:
    case Opcode::kGenericModulus:
    case Opcode::kGenericExponentiate:
    case Opcode::kGenericBitwiseAnd:
    case Opcode::kGenericBitwiseOr:
    case Opcode::kGenericBitwiseXor:
    case Opcode::kGenericShiftLeft:
    case Opcode::kGenericShiftRight:
    case Opcode::kGenericShiftRightLogical:
    case Opcode::kGenericBitwiseNot:
    case Opcode::kGenericNegate:
    case Opcode::kGenericIncrement:
    case Opcode::kGenericDecrement:
    case Opcode::kConsStringMap:
    case Opcode::kMapPrototypeGet:
    case Opcode::kMapPrototypeGetInt32Key:
      return NodeType::kUnknown;
  }
}

ExternalReference Float64Ieee754Unary::ieee_function_ref() const {
  switch (ieee_function_) {
#define CASE(MathName, ExtName, EnumName) \
  case Ieee754Function::k##EnumName:      \
    return ExternalReference::ieee754_##ExtName##_function();
    IEEE_754_UNARY_LIST(CASE)
#undef CASE
  }
}

ExternalReference Float64Ieee754Binary::ieee_function_ref() const {
  switch (ieee_function_) {
#define CASE(MathName, ExtName, EnumName) \
  case Ieee754Function::k##EnumName:      \
    return ExternalReference::ieee754_##ExtName##_function();
    IEEE_754_BINARY_LIST(CASE)
#undef CASE
  }
}

// ---
// Check input value representation
// ---

ValueRepresentation ToValueRepresentation(MachineType type) {
  switch (type.representation()) {
    case MachineRepresentation::kTagged:
    case MachineRepresentation::kTaggedSigned:
    case MachineRepresentation::kTaggedPointer:
      return ValueRepresentation::kTagged;
    case MachineRepresentation::kFloat64:
      return ValueRepresentation::kFloat64;
    case MachineRepresentation::kWord64:
      DCHECK_EQ(kSystemPointerSize, 8);
      return ValueRepresentation::kIntPtr;
    default:
      return ValueRepresentation::kInt32;
  }
}

void CheckValueInputIs(const NodeBase* node, int i,
                       ValueRepresentation expected) {
  ValueNode* input = node->input(i).node();
  DCHECK(!input->Is<Identity>());
  ValueRepresentation got = input->properties().value_representation();
  bool valid = ValueRepresentationIs(got, expected);
  if (!valid) {
    std::ostringstream str;
    str << "Type representation error: node ";
    if (GetCurrentGraphLabeller()) {
      str << "#" << GetCurrentGraphLabeller()->NodeId(node) << " : ";
    }
    str << node->opcode() << " (input @" << i << " = " << input->opcode()
        << ") type " << got << " is not " << expected;
    FATAL("%s", str.str().c_str());
  }
}

void CheckValueInputIs(const NodeBase* node, int i, Opcode expected) {
  ValueNode* input = node->input(i).node();
  Opcode got = input->opcode();
  if (got != expected) {
    std::ostringstream str;
    str << "Opcode error: node ";
    if (GetCurrentGraphLabeller()) {
      str << "#" << GetCurrentGraphLabeller()->NodeId(node) << " : ";
    }
    str << node->opcode() << " (input @" << i << " = " << input->opcode()
        << ") opcode " << got << " is not " << expected;
    FATAL("%s", str.str().c_str());
  }
}

void GeneratorStore::VerifyInputs() const {
  for (int i = 0; i < input_count(); i++) {
    CheckValueInputIs(this, i, ValueRepresentation::kTagged);
  }
}

void Phi::VerifyInputs() const {
  switch (value_representation()) {
#define CASE_REPR(repr)                                         \
  case ValueRepresentation::k##repr:                            \
    for (int i = 0; i < input_count(); i++) {                   \
      CheckValueInputIs(this, i, ValueRepresentation::k##repr); \
    }                                                           \
    break;

    CASE_REPR(Tagged)
    CASE_REPR(Int32)
    CASE_REPR(Uint32)
    CASE_REPR(Float64)
    CASE_REPR(HoleyFloat64)
#undef CASE_REPR
    case ValueRepresentation::kIntPtr:
    case ValueRepresentation::kNone:
      UNREACHABLE();
  }
}

void Call::VerifyInputs() const {
  for (int i = 0; i < input_count(); i++) {
    CheckValueInputIs(this, i, ValueRepresentation::kTagged);
  }
}

#ifdef V8_COMPRESS_POINTERS
void Call::MarkTaggedInputsAsDecompressing() {
  for (int i = 0; i < input_count(); i++) {
    input(i).node()->SetTaggedResultNeedsDecompress();
  }
}
#endif

void CallForwardVarargs::VerifyInputs() const {
  for (int i = 0; i < input_count(); i++) {
    CheckValueInputIs(this, i, ValueRepresentation::kTagged);
  }
}

#ifdef V8_COMPRESS_POINTERS
void CallForwardVarargs::MarkTaggedInputsAsDecompressing() {
  for (int i = 0; i < input_count(); i++) {
    input(i).node()->SetTaggedResultNeedsDecompress();
  }
}
#endif

void CallWithArrayLike::VerifyInputs() const {
  for (int i = 0; i < input_count(); i++) {
    CheckValueInputIs(this, i, ValueRepresentation::kTagged);
  }
}

#ifdef V8_COMPRESS_POINTERS
void CallWithArrayLike::MarkTaggedInputsAsDecompressing() {
  for (int i = 0; i < input_count(); i++) {
    input(i).node()->SetTaggedResultNeedsDecompress();
  }
}
#endif

void CallWithSpread::VerifyInputs() const {
  for (int i = 0; i < input_count(); i++) {
    CheckValueInputIs(this, i, ValueRepresentation::kTagged);
  }
}

#ifdef V8_COMPRESS_POINTERS
void CallWithSpread::MarkTaggedInputsAsDecompressing() {
  for (int i = 0; i < input_count(); i++) {
    input(i).node()->SetTaggedResultNeedsDecompress();
  }
}
#endif

void CallSelf::VerifyInputs() const {
  for (int i = 0; i < input_count(); i++) {
    CheckValueInputIs(this, i, ValueRepresentation::kTagged);
  }
}

#ifdef V8_COMPRESS_POINTERS
void CallSelf::MarkTaggedInputsAsDecompressing() {
  for (int i = 0; i < input_count(); i++) {
    input(i).node()->SetTaggedResultNeedsDecompress();
  }
}
#endif

void CallKnownJSFunction::VerifyInputs() const {
  for (int i = 0; i < input_count(); i++) {
    CheckValueInputIs(this, i, ValueRepresentation::kTagged);
  }
}

#ifdef V8_COMPRESS_POINTERS
void CallKnownJSFunction::MarkTaggedInputsAsDecompressing() {
  for (int i = 0; i < input_count(); i++) {
    input(i).node()->SetTaggedResultNeedsDecompress();
  }
}
#endif

void CallKnownApiFunction::VerifyInputs() const {
  for (int i = 0; i < input_count(); i++) {
    CheckValueInputIs(this, i, ValueRepresentation::kTagged);
  }
}

#ifdef V8_COMPRESS_POINTERS
void CallKnownApiFunction::MarkTaggedInputsAsDecompressing() {
  for (int i = 0; i < input_count(); i++) {
    input(i).node()->SetTaggedResultNeedsDecompress();
  }
}
#endif

void Construct::VerifyInputs() const {
  for (int i = 0; i < input_count(); i++) {
    CheckValueInputIs(this, i, ValueRepresentation::kTagged);
  }
}

#ifdef V8_COMPRESS_POINTERS
void Construct::MarkTaggedInputsAsDecompressing() {
  for (int i = 0; i < input_count(); i++) {
    input(i).node()->SetTaggedResultNeedsDecompress();
  }
}
#endif

void ConstructWithSpread::VerifyInputs() const {
  for (int i = 0; i < input_count(); i++) {
    CheckValueInputIs(this, i, ValueRepresentation::kTagged);
  }
}

#ifdef V8_COMPRESS_POINTERS
void ConstructWithSpread::MarkTaggedInputsAsDecompressing() {
  for (int i = 0; i < input_count(); i++) {
    input(i).node()->SetTaggedResultNeedsDecompress();
  }
}
#endif

void CallBuiltin::VerifyInputs() const {
  auto descriptor = Builtins::CallInterfaceDescriptorFor(builtin());
  int count = input_count();
  // Verify context.
  if (descriptor.HasContextParameter()) {
    CheckValueInputIs(this, count - 1, ValueRepresentation::kTagged);
    count--;
  }

// {all_input_count} includes the feedback slot and vector.
#ifdef DEBUG
  int all_input_count = count + (has_feedback() ? 2 : 0);
  if (descriptor.AllowVarArgs()) {
    DCHECK_GE(all_input_count, descriptor.GetParameterCount());
  } else {
    DCHECK_EQ(all_input_count, descriptor.GetParameterCount());
  }
#endif
  int i = 0;
  // Check the rest of inputs.
  for (; i < count; ++i) {
    MachineType type = i < descriptor.GetParameterCount()
                           ? descriptor.GetParameterType(i)
                           : MachineType::AnyTagged();
    CheckValueInputIs(this, i, ToValueRepresentation(type));
  }
}

#ifdef V8_COMPRESS_POINTERS
void CallBuiltin::MarkTaggedInputsAsDecompressing() {
  auto descriptor = Builtins::CallInterfaceDescriptorFor(builtin());
  int count = input_count();
  // Set context.
  if (descriptor.HasContextParameter()) {
    input(count - 1).node()->SetTaggedResultNeedsDecompress();
    count--;
  }
  int i = 0;
  // Set the rest of the tagged inputs.
  for (; i < count; ++i) {
    MachineType type = i < descriptor.GetParameterCount()
                           ? descriptor.GetParameterType(i)
                           : MachineType::AnyTagged();
    if (type.IsTagged() && !type.IsTaggedSigned()) {
      input(i).node()->SetTaggedResultNeedsDecompress();
    }
  }
}
#endif

void CallCPPBuiltin::VerifyInputs() const {
  for (int i = 0; i < input_count(); i++) {
    CheckValueInputIs(this, i, ValueRepresentation::kTagged);
  }
}

#ifdef V8_COMPRESS_POINTERS
void CallCPPBuiltin::MarkTaggedInputsAsDecompressing() {
  for (int i = 0; i < input_count(); i++) {
    input(i).node()->SetTaggedResultNeedsDecompress();
  }
}
#endif

void CallRuntime::VerifyInputs() const {
  for (int i = 0; i < input_count(); i++) {
    CheckValueInputIs(this, i, ValueRepresentation::kTagged);
  }
}

#ifdef V8_COMPRESS_POINTERS
void CallRuntime::MarkTaggedInputsAsDecompressing() {
  for (int i = 0; i < input_count(); i++) {
    input(i).node()->SetTaggedResultNeedsDecompress();
  }
}
#endif

void StoreTaggedFieldNoWriteBarrier::VerifyInputs() const {
  Base::VerifyInputs();
  if (auto host_alloc =
          input(kObjectIndex).node()->TryCast<InlinedAllocation>()) {
    if (input(kValueIndex).node()->Is<InlinedAllocation>()) {
      CHECK_EQ(host_alloc->allocation_block()->allocation_type(),
               AllocationType::kYoung);
    }
  }
}

void InlinedAllocation::VerifyInputs() const {
  Base::VerifyInputs();
  CheckValueInputIs(this, 0, Opcode::kAllocationBlock);
  CHECK_LE(object()->type(), VirtualObject::Type::kLast);
}

AllocationBlock* InlinedAllocation::allocation_block() {
  return allocation_block_input().node()->Cast<AllocationBlock>();
}

void AllocationBlock::TryPretenure() {
  if (allocation_type_ == AllocationType::kOld) {
    return;
  }
  if (elided_write_barriers_depend_on_type()) {
    return;
  }
  allocation_type_ = AllocationType::kOld;
  for (auto alloc : allocation_list_) {
    alloc->object()->ForEachInput(
        [&](ValueNode* value) { TryPretenure(value); });
  }
}

void AllocationBlock::TryPretenure(ValueNode* value) {
  DCHECK(v8_flags.maglev_pretenure_store_values);
  DCHECK_EQ(allocation_type_, AllocationType::kOld);

  if (elided_write_barriers_depend_on_type()) {
    return;
  }

  if (IsConstantNode(value->opcode())) return;
  if (auto next_alloc = value->TryCast<InlinedAllocation>()) {
    next_alloc->allocation_block()->TryPretenure();
  } else if (auto phi = value->TryCast<Phi>()) {
    if (!phi->is_loop_phi() || !phi->is_unmerged_loop_phi()) {
      for (int i = 0; i < phi->input_count(); ++i) {
        auto phi_input = phi->input(i).node();
        if (auto phi_alloc = phi_input->TryCast<InlinedAllocation>()) {
          phi_alloc->allocation_block()->TryPretenure();
        }
      }
    }
  }
}

// ---
// Reify constants
// ---

DirectHandle<Object> ValueNode::Reify(LocalIsolate* isolate) const {
  switch (opcode()) {
#define V(Name)         \
  case Opcode::k##Name: \
    return this->Cast<Name>()->DoReify(isolate);
    CONSTANT_VALUE_NODE_LIST(V)
#undef V
    default:
      UNREACHABLE();
  }
}

DirectHandle<Object> SmiConstant::DoReify(LocalIsolate* isolate) const {
  return direct_handle(value_, isolate);
}

DirectHandle<Object> TaggedIndexConstant::DoReify(LocalIsolate* isolate) const {
  UNREACHABLE();
}

DirectHandle<Object> Int32Constant::DoReify(LocalIsolate* isolate) const {
  return isolate->factory()->NewNumberFromInt<AllocationType::kOld>(value());
}

DirectHandle<Object> Uint32Constant::DoReify(LocalIsolate* isolate) const {
  return isolate->factory()->NewNumberFromUint<AllocationType::kOld>(value());
}

DirectHandle<Object> IntPtrConstant::DoReify(LocalIsolate* isolate) const {
  return isolate->factory()->NewNumberFromInt64<AllocationType::kOld>(value());
}

DirectHandle<Object> Float64Constant::DoReify(LocalIsolate* isolate) const {
  return isolate->factory()->NewNumber<AllocationType::kOld>(
      value_.get_scalar());
}

DirectHandle<Object> Constant::DoReify(LocalIsolate* isolate) const {
  return object_.object();
}

DirectHandle<Object> TrustedConstant::DoReify(LocalIsolate* isolate) const {
  return object_.object();
}

Handle<Object> RootConstant::DoReify(LocalIsolate* isolate) const {
  return isolate->root_handle(index());
}

#ifdef V8_ENABLE_MAGLEV

bool FromConstantToBool(MaglevAssembler* masm, ValueNode* node) {
  // TODO(leszeks): Getting the main thread local isolate is not what we
  // actually want here, but it's all we have, and it happens to work because
  // really all we're using it for is ReadOnlyRoots. We should change ToBoolean
  // to be able to pass ReadOnlyRoots in directly.
  return FromConstantToBool(masm->isolate()->AsLocalIsolate(), node);
}

// ---
// Load node to registers
// ---

namespace {
template <typename NodeT>
void LoadToRegisterHelper(NodeT* node, MaglevAssembler* masm, Register reg) {
  if constexpr (!IsDoubleRepresentation(
                    NodeT::kProperties.value_representation())) {
    return node->DoLoadToRegister(masm, reg);
  } else {
    UNREACHABLE();
  }
}
template <typename NodeT>
void LoadToRegisterHelper(NodeT* node, MaglevAssembler* masm,
                          DoubleRegister reg) {
  if constexpr (IsDoubleRepresentation(
                    NodeT::kProperties.value_representation())) {
    return node->DoLoadToRegister(masm, reg);
  } else {
    UNREACHABLE();
  }
}
}  // namespace

void ValueNode::LoadToRegister(MaglevAssembler* masm, Register reg) {
  switch (opcode()) {
#define V(Name)         \
  case Opcode::k##Name: \
    return LoadToRegisterHelper(this->Cast<Name>(), masm, reg);
    VALUE_NODE_LIST(V)
#undef V
    default:
      UNREACHABLE();
  }
}
void ValueNode::LoadToRegister(MaglevAssembler* masm, DoubleRegister reg) {
  switch (opcode()) {
#define V(Name)         \
  case Opcode::k##Name: \
    return LoadToRegisterHelper(this->Cast<Name>(), masm, reg);
    VALUE_NODE_LIST(V)
#undef V
    default:
      UNREACHABLE();
  }
}

void ValueNode::DoLoadToRegister(MaglevAssembler* masm, Register reg) {
  DCHECK(regalloc_info()->is_spilled());
  DCHECK(!use_double_register());
  __ Move(reg, masm->GetStackSlot(compiler::AllocatedOperand::cast(
                   regalloc_info()->spill_slot())));
}

void ValueNode::DoLoadToRegister(MaglevAssembler* masm, DoubleRegister reg) {
  DCHECK(regalloc_info()->is_spilled());
  DCHECK(use_double_register());
  __ LoadFloat64(reg, masm->GetStackSlot(compiler::AllocatedOperand::cast(
                          regalloc_info()->spill_slot())));
}

void SmiConstant::DoLoadToRegister(MaglevAssembler* masm, Register reg) {
  __ Move(reg, value());
}

void TaggedIndexConstant::DoLoadToRegister(MaglevAssembler* masm,
                                           Register reg) {
  __ Move(reg, value());
}

void Int32Constant::DoLoadToRegister(MaglevAssembler* masm, Register reg) {
  __ Move(reg, value());
}

void Uint32Constant::DoLoadToRegister(MaglevAssembler* masm, Register reg) {
  __ Move(reg, value());
}

void IntPtrConstant::DoLoadToRegister(MaglevAssembler* masm, Register reg) {
  __ Move(reg, value());
}

void Float64Constant::DoLoadToRegister(MaglevAssembler* masm,
                                       DoubleRegister reg) {
  __ Move(reg, value());
}

void Constant::DoLoadToRegister(MaglevAssembler* masm, Register reg) {
  __ Move(reg, object_.object());
}

void RootConstant::DoLoadToRegister(MaglevAssembler* masm, Register reg) {
  __ LoadRoot(reg, index());
}

void TrustedConstant::DoLoadToRegister(MaglevAssembler* masm, Register reg) {
  __ Move(reg, object_.object());
}

// ---
// Arch agnostic nodes
// ---

#define TURBOLEV_UNREACHABLE_NODE(Name)                               \
  void Name::SetValueLocationConstraints() { UNREACHABLE(); }         \
  void Name::GenerateCode(MaglevAssembler*, const ProcessingState&) { \
    UNREACHABLE();                                                    \
  }

TURBOLEV_VALUE_NODE_LIST(TURBOLEV_UNREACHABLE_NODE)
TURBOLEV_NON_VALUE_NODE_LIST(TURBOLEV_UNREACHABLE_NODE)
#undef TURBOLEV_UNREACHABLE_NODE

void SmiConstant::SetValueLocationConstraints() { DefineAsConstant(this); }
void SmiConstant::GenerateCode(MaglevAssembler* masm,
                               const ProcessingState& state) {}

void TaggedIndexConstant::SetValueLocationConstraints() {
  DefineAsConstant(this);
}
void TaggedIndexConstant::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {}

void Int32Constant::SetValueLocationConstraints() { DefineAsConstant(this); }
void Int32Constant::GenerateCode(MaglevAssembler* masm,
                                 const ProcessingState& state) {}

void Uint32Constant::SetValueLocationConstraints() { DefineAsConstant(this); }
void Uint32Constant::GenerateCode(MaglevAssembler* masm,
                                  const ProcessingState& state) {}

void IntPtrConstant::SetValueLocationConstraints() { DefineAsConstant(this); }
void IntPtrConstant::GenerateCode(MaglevAssembler* masm,
                                  const ProcessingState& state) {}

void Float64Constant::SetValueLocationConstraints() { DefineAsConstant(this); }
void Float64Constant::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {}

void Constant::SetValueLocationConstraints() { DefineAsConstant(this); }
void Constant::GenerateCode(MaglevAssembler* masm,
                            const ProcessingState& state) {}

void TrustedConstant::SetValueLocationConstraints() { DefineAsConstant(this); }
void TrustedConstant::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {
#ifndef V8_ENABLE_SANDBOX
  UNREACHABLE();
#endif
}

void RootConstant::SetValueLocationConstraints() { DefineAsConstant(this); }
void RootConstant::GenerateCode(MaglevAssembler* masm,
                                const ProcessingState& state) {}

void InitialValue::SetValueLocationConstraints() {
  result().SetUnallocated(compiler::UnallocatedOperand::FIXED_SLOT,
                          stack_slot(), kNoVreg);
}
void InitialValue::GenerateCode(MaglevAssembler* masm,
                                const ProcessingState& state) {
  // No-op, the value is already in the appropriate slot.
}

// static
uint32_t InitialValue::stack_slot(uint32_t register_index) {
  // TODO(leszeks): Make this nicer.
  return (StandardFrameConstants::kExpressionsOffset -
          UnoptimizedFrameConstants::kRegisterFileFromFp) /
             kSystemPointerSize +
         register_index;
}

uint32_t InitialValue::stack_slot() const {
  return stack_slot(source_.index());
}

int FunctionEntryStackCheck::MaxCallStackArgs() const { return 0; }
void FunctionEntryStackCheck::SetValueLocationConstraints() {
  set_temporaries_needed(2);
  // kReturnRegister0 should not be one of the available temporary registers.
  RequireSpecificTemporary(kReturnRegister0);
}
void FunctionEntryStackCheck::GenerateCode(MaglevAssembler* masm,
                                           const ProcessingState& state) {
  // Stack check. This folds the checks for both the interrupt stack limit
  // check and the real stack limit into one by just checking for the
  // interrupt limit. The interrupt limit is either equal to the real
  // stack limit or tighter. By ensuring we have space until that limit
  // after building the frame we can quickly precheck both at once.
  const int stack_check_offset = masm->code_gen_state()->stack_check_offset();
  // Only NewTarget can be live at this point.
  DCHECK_LE(register_snapshot().live_registers.Count(), 1);
  Builtin builtin =
      register_snapshot().live_tagged_registers.has(
          kJavaScriptCallNewTargetRegister)
          ? Builtin::kMaglevFunctionEntryStackCheck_WithNewTarget
          : Builtin::kMaglevFunctionEntryStackCheck_WithoutNewTarget;
  ZoneLabelRef done(masm);
  Condition cond = __ FunctionEntryStackCheck(stack_check_offset);
  if (masm->isolate()->is_short_builtin_calls_enabled()) {
    __ JumpIf(cond, *done, Label::kNear);
    __ Move(kReturnRegister0, Smi::FromInt(stack_check_offset));
    __ MacroAssembler::CallBuiltin(builtin);
    masm->DefineLazyDeoptPoint(lazy_deopt_info());
  } else {
    __ JumpToDeferredIf(
        NegateCondition(cond),
        [](MaglevAssembler* masm, ZoneLabelRef done,
           FunctionEntryStackCheck* node, Builtin builtin,
           int stack_check_offset) {
          __ Move(kReturnRegister0, Smi::FromInt(stack_check_offset));
          __ MacroAssembler::CallBuiltin(builtin);
          masm->DefineLazyDeoptPoint(node->lazy_deopt_info());
          __ Jump(*done);
        },
        done, this, builtin, stack_check_offset);
  }
  __ bind(*done);
}

void RegisterInput::SetValueLocationConstraints() {
  DefineAsFixed(this, input());
}
void RegisterInput::GenerateCode(MaglevAssembler* masm,
                                 const ProcessingState& state) {
  // Nothing to be done, the value is already in the register.
}

void GetSecondReturnedValue::SetValueLocationConstraints() {
  DefineAsFixed(this, kReturnRegister1);
}
void GetSecondReturnedValue::GenerateCode(MaglevAssembler* masm,
                                          const ProcessingState& state) {
  // No-op. This is just a hack that binds kReturnRegister1 to a value node.
  // kReturnRegister1 is guaranteed to be free in the register allocator, since
  // previous node in the basic block is a call.
#ifdef DEBUG
  // Check if the previous node is call.
  Node* previous = nullptr;
  for (Node* node : state.block()->nodes()) {
    if (node == this) {
      break;
    }
    previous = node;
  }
  DCHECK_NE(previous, nullptr);
  DCHECK(previous->properties().is_call());
#endif  // DEBUG
}

void Deopt::SetValueLocationConstraints() {}
void Deopt::GenerateCode(MaglevAssembler* masm, const ProcessingState& state) {
  __ EmitEagerDeopt(this, deoptimize_reason());
}

void Phi::SetValueLocationConstraints() {
  for (Input& input : *this) {
    UseAny(input);
  }

  // We have to pass a policy for the result, but it is ignored during register
  // allocation. See StraightForwardRegisterAllocator::AllocateRegisters which
  // has special handling for Phis.
  static const compiler::UnallocatedOperand::ExtendedPolicy kIgnoredPolicy =
      compiler::UnallocatedOperand::REGISTER_OR_SLOT_OR_CONSTANT;

  result().SetUnallocated(kIgnoredPolicy, kNoVreg);
}

void Phi::GenerateCode(MaglevAssembler* masm, const ProcessingState& state) {}

void ArgumentsElements::SetValueLocationConstraints() {
  using SloppyArgsD =
      CallInterfaceDescriptorFor<Builtin::kNewSloppyArgumentsElements>::type;
  using StrictArgsD =
      CallInterfaceDescriptorFor<Builtin::kNewStrictArgumentsElements>::type;
  using RestArgsD =
      CallInterfaceDescriptorFor<Builtin::kNewRestArgumentsElements>::type;
  static_assert(
      SloppyArgsD::GetRegisterParameter(SloppyArgsD::kArgumentCount) ==
      StrictArgsD::GetRegisterParameter(StrictArgsD::kArgumentCount));
  static_assert(
      SloppyArgsD::GetRegisterParameter(SloppyArgsD::kArgumentCount) ==
      StrictArgsD::GetRegisterParameter(RestArgsD::kArgumentCount));
  UseFixed(arguments_count_input(),
           SloppyArgsD::GetRegisterParameter(SloppyArgsD::kArgumentCount));
  DefineAsFixed(this, kReturnRegister0);
}

void ArgumentsElements::GenerateCode(MaglevAssembler* masm,
                                     const ProcessingState& state) {
  Register arguments_count = ToRegister(arguments_count_input());
  switch (type()) {
    case CreateArgumentsType::kMappedArguments:
      __ CallBuiltin<Builtin::kNewSloppyArgumentsElements>(
          __ GetFramePointer(), formal_parameter_count(), arguments_count);
      break;
    case CreateArgumentsType::kUnmappedArguments:
      __ CallBuiltin<Builtin::kNewStrictArgumentsElements>(
          __ GetFramePointer(), formal_parameter_count(), arguments_count);
      break;
    case CreateArgumentsType::kRestParameter:
      __ CallBuiltin<Builtin::kNewRestArgumentsElements>(
          __ GetFramePointer(), formal_parameter_count(), arguments_count);
      break;
  }
}

void AllocateElementsArray::SetValueLocationConstraints() {
  UseAndClobberRegister(length_input());
  DefineAsRegister(this);
  set_temporaries_needed(1);
}
void AllocateElementsArray::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  Register length = ToRegister(length_input());
  Register elements = ToRegister(result());
  Label allocate_elements, done;
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  // Be sure to save the length in the register snapshot.
  RegisterSnapshot snapshot = register_snapshot();
  snapshot.live_registers.set(length);

  // Return empty fixed array if length equal zero.
  __ CompareInt32AndJumpIf(length, 0, kNotEqual, &allocate_elements,
                           Label::Distance::kNear);
  __ LoadRoot(elements, RootIndex::kEmptyFixedArray);
  __ Jump(&done);

  // Allocate a fixed array object.
  __ bind(&allocate_elements);
  __ CompareInt32AndJumpIf(
      length, JSArray::kInitialMaxFastElementArray, kGreaterThanEqual,
      __ GetDeoptLabel(this,
                       DeoptimizeReason::kGreaterThanMaxFastElementArray));
  {
    Register size_in_bytes = scratch;
    __ Move(size_in_bytes, length);
    __ ShiftLeft(size_in_bytes, kTaggedSizeLog2);
    __ AddInt32(size_in_bytes, OFFSET_OF_DATA_START(FixedArray));
    __ Allocate(snapshot, elements, size_in_bytes, allocation_type_);
    __ SetMapAsRoot(elements, RootIndex::kFixedArrayMap);
  }
  {
    Register smi_length = scratch;
    __ UncheckedSmiTagInt32(smi_length, length);
    __ StoreTaggedFieldNoWriteBarrier(elements, offsetof(FixedArray, length_),
                                      smi_length);
  }

  // Initialize the array with holes.
  {
    Label loop;
    Register the_hole = scratch;
    __ LoadTaggedRoot(the_hole, RootIndex::kTheHoleValue);
    __ bind(&loop);
    __ DecrementInt32(length);
    // TODO(victorgomes): This can be done more efficiently  by have the root
    // (the_hole) as an immediate in the store.
    __ StoreFixedArrayElementNoWriteBarrier(elements, length, the_hole);
    __ CompareInt32AndJumpIf(length, 0, kGreaterThan, &loop,
                             Label::Distance::kNear);
  }
  __ bind(&done);
}

namespace {

constexpr Builtin BuiltinFor(Operation operation) {
  switch (operation) {
#define CASE(name)         \
  case Operation::k##name: \
    return Builtin::k##name##_WithFeedback;
    OPERATION_LIST(CASE)
#undef CASE
  }
}

}  // namespace

template <class Derived, Operation kOperation>
void UnaryWithFeedbackNode<Derived, kOperation>::SetValueLocationConstraints() {
  using D = UnaryOp_WithFeedbackDescriptor;
  UseFixed(operand_input(), D::GetRegisterParameter(D::kValue));
  DefineAsFixed(this, kReturnRegister0);
}

template <class Derived, Operation kOperation>
void UnaryWithFeedbackNode<Derived, kOperation>::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  __ CallBuiltin<BuiltinFor(kOperation)>(
      masm->native_context().object(),  // context
      operand_input(),                  // value
      feedback().index(),               // feedback slot
      feedback().vector                 // feedback vector
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

template <class Derived, Operation kOperation>
void BinaryWithFeedbackNode<Derived,
                            kOperation>::SetValueLocationConstraints() {
  using D = BinaryOp_WithFeedbackDescriptor;
  UseFixed(left_input(), D::GetRegisterParameter(D::kLeft));
  UseFixed(right_input(), D::GetRegisterParameter(D::kRight));
  DefineAsFixed(this, kReturnRegister0);
}

template <class Derived, Operation kOperation>
void BinaryWithFeedbackNode<Derived, kOperation>::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  __ CallBuiltin<BuiltinFor(kOperation)>(
      masm->native_context().object(),  // context
      left_input(),                     // left
      right_input(),                    // right
      feedback().index(),               // feedback slot
      feedback().vector                 // feedback vector
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

#define DEF_OPERATION(Name)                               \
  void Name::SetValueLocationConstraints() {              \
    Base::SetValueLocationConstraints();                  \
  }                                                       \
  void Name::GenerateCode(MaglevAssembler* masm,          \
                          const ProcessingState& state) { \
    Base::GenerateCode(masm, state);                      \
  }
GENERIC_OPERATIONS_NODE_LIST(DEF_OPERATION)
#undef DEF_OPERATION

void ConstantGapMove::SetValueLocationConstraints() { UNREACHABLE(); }

namespace {
template <typename T>
struct GetRegister;
template <>
struct GetRegister<Register> {
  static Register Get(compiler::AllocatedOperand target) {
    return target.GetRegister();
  }
};
template <>
struct GetRegister<DoubleRegister> {
  static DoubleRegister Get(compiler::AllocatedOperand target) {
    return target.GetDoubleRegister();
  }
};
}  // namespace

void ConstantGapMove::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {
  switch (node_->opcode()) {
#define CASE(Name)                                \
  case Opcode::k##Name:                           \
    return node_->Cast<Name>()->DoLoadToRegister( \
        masm, GetRegister<Name::OutputRegister>::Get(target()));
    CONSTANT_VALUE_NODE_LIST(CASE)
#undef CASE
    default:
      UNREACHABLE();
  }
}

void GapMove::SetValueLocationConstraints() { UNREACHABLE(); }
void GapMove::GenerateCode(MaglevAssembler* masm,
                           const ProcessingState& state) {
  DCHECK_EQ(source().representation(), target().representation());
  MachineRepresentation repr = source().representation();
  if (source().IsRegister()) {
    Register source_reg = ToRegister(source());
    if (target().IsAnyRegister()) {
      DCHECK(target().IsRegister());
      __ MoveRepr(repr, ToRegister(target()), source_reg);
    } else {
      __ MoveRepr(repr, masm->ToMemOperand(target()), source_reg);
    }
  } else if (source().IsDoubleRegister()) {
    DoubleRegister source_reg = ToDoubleRegister(source());
    if (target().IsAnyRegister()) {
      DCHECK(target().IsDoubleRegister());
      __ Move(ToDoubleRegister(target()), source_reg);
    } else {
      __ StoreFloat64(masm->ToMemOperand(target()), source_reg);
    }
  } else {
    DCHECK(source().IsAnyStackSlot());
    MemOperand source_op = masm->ToMemOperand(source());
    if (target().IsRegister()) {
      __ MoveRepr(MachineRepresentation::kTaggedPointer, ToRegister(target()),
                  source_op);
    } else if (target().IsDoubleRegister()) {
      __ LoadFloat64(ToDoubleRegister(target()), source_op);
    } else {
      DCHECK(target().IsAnyStackSlot());
      DCHECK_EQ(ElementSizeInBytes(repr), kSystemPointerSize);
      __ MoveRepr(repr, masm->ToMemOperand(target()), source_op);
    }
  }
}

void AssertInt32::SetValueLocationConstraints() {
  UseRegister(left_input());
  UseRegister(right_input());
}
void AssertInt32::GenerateCode(MaglevAssembler* masm,
                               const ProcessingState& state) {
  __ CompareInt32AndAssert(ToRegister(left_input()), ToRegister(right_input()),
                           ToCondition(condition_), reason_);
}

void CheckUint32IsSmi::SetValueLocationConstraints() { UseRegister(input()); }
void CheckUint32IsSmi::GenerateCode(MaglevAssembler* masm,
                                    const ProcessingState& state) {
  Register reg = ToRegister(input());
  // Perform an unsigned comparison against Smi::kMaxValue.
  __ CompareUInt32AndEmitEagerDeoptIf(reg, Smi::kMaxValue, kUnsignedGreaterThan,
                                      DeoptimizeReason::kNotASmi, this);
}

void CheckedSmiUntag::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}

void CheckedSmiUntag::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {
  Register value = ToRegister(input());
  // TODO(leszeks): Consider optimizing away this test and using the carry bit
  // of the `sarl` for cases where the deopt uses the value from a different
  // register.
  __ EmitEagerDeoptIfNotSmi(this, value, DeoptimizeReason::kNotASmi);
  __ SmiToInt32(value);
}

void UnsafeSmiUntag::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}

void UnsafeSmiUntag::GenerateCode(MaglevAssembler* masm,
                                  const ProcessingState& state) {
  Register value = ToRegister(input());
  __ AssertSmi(value);
  __ SmiToInt32(value);
}

void CheckInt32IsSmi::SetValueLocationConstraints() { UseRegister(input()); }
void CheckInt32IsSmi::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {
  // We shouldn't be emitting this node for 32-bit Smis.
  DCHECK(!SmiValuesAre32Bits());

  // TODO(leszeks): This basically does a SmiTag and throws the result away.
  // Don't throw the result away if we want to actually use it.
  Register reg = ToRegister(input());
  Label* fail = __ GetDeoptLabel(this, DeoptimizeReason::kNotASmi);
  __ CheckInt32IsSmi(reg, fail);
}

void CheckIntPtrIsSmi::SetValueLocationConstraints() { UseRegister(input()); }
void CheckIntPtrIsSmi::GenerateCode(MaglevAssembler* masm,
                                    const ProcessingState& state) {
  Register reg = ToRegister(input());
  Label* fail = __ GetDeoptLabel(this, DeoptimizeReason::kNotASmi);
  __ CheckIntPtrIsSmi(reg, fail);
}

void CheckedInt32ToUint32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}
void CheckedInt32ToUint32::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  __ CompareInt32AndJumpIf(
      ToRegister(input()), 0, kLessThan,
      __ GetDeoptLabel(this, DeoptimizeReason::kNotUint32));
}

void CheckedIntPtrToUint32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
  set_temporaries_needed(1);
}

void CheckedIntPtrToUint32::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  __ Move(scratch, std::numeric_limits<uint32_t>::max());
  __ CompareIntPtrAndJumpIf(
      ToRegister(input()), scratch, kUnsignedGreaterThan,
      __ GetDeoptLabel(this, DeoptimizeReason::kNotUint32));
}

void UnsafeInt32ToUint32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}
void UnsafeInt32ToUint32::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {}

void CheckHoleyFloat64IsSmi::SetValueLocationConstraints() {
  UseRegister(input());
  set_temporaries_needed(1);
}
void CheckHoleyFloat64IsSmi::GenerateCode(MaglevAssembler* masm,
                                          const ProcessingState& state) {
  DoubleRegister value = ToDoubleRegister(input());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  Label* fail = __ GetDeoptLabel(this, DeoptimizeReason::kNotASmi);
  __ TryTruncateDoubleToInt32(scratch, value, fail);
  if (!SmiValuesAre32Bits()) {
    __ CheckInt32IsSmi(scratch, fail, scratch);
  }
}

void CheckedSmiTagInt32::SetValueLocationConstraints() {
  UseAndClobberRegister(input());
  DefineSameAsFirst(this);
}
void CheckedSmiTagInt32::GenerateCode(MaglevAssembler* masm,
                                      const ProcessingState& state) {
  Register reg = ToRegister(input());
  Label* fail = __ GetDeoptLabel(this, DeoptimizeReason::kNotASmi);
  // None of the mutated input registers should be a register input into the
  // eager deopt info.
  DCHECK_REGLIST_EMPTY(RegList{reg} &
                       GetGeneralRegistersUsedAsInputs(eager_deopt_info()));
  __ SmiTagInt32AndJumpIfFail(reg, fail);
}

void CheckedSmiSizedInt32::SetValueLocationConstraints() {
  UseAndClobberRegister(input());
  DefineSameAsFirst(this);
}
void CheckedSmiSizedInt32::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  // We shouldn't be emitting this node for 32-bit Smis.
  DCHECK(!SmiValuesAre32Bits());

  Register reg = ToRegister(input());
  Label* fail = __ GetDeoptLabel(this, DeoptimizeReason::kNotASmi);
  __ CheckInt32IsSmi(reg, fail);
}

void CheckedSmiTagUint32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}
void CheckedSmiTagUint32::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {
  Register reg = ToRegister(input());
  Label* fail = __ GetDeoptLabel(this, DeoptimizeReason::kNotASmi);
  // None of the mutated input registers should be a register input into the
  // eager deopt info.
  DCHECK_REGLIST_EMPTY(RegList{reg} &
                       GetGeneralRegistersUsedAsInputs(eager_deopt_info()));
  __ SmiTagUint32AndJumpIfFail(reg, fail);
}

void CheckedSmiTagIntPtr::SetValueLocationConstraints() {
  UseAndClobberRegister(input());
  DefineSameAsFirst(this);
}
void CheckedSmiTagIntPtr::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {
  Register reg = ToRegister(input());
  Label* fail = __ GetDeoptLabel(this, DeoptimizeReason::kNotASmi);
  // None of the mutated input registers should be a register input into the
  // eager deopt info.
  DCHECK_REGLIST_EMPTY(RegList{reg} &
                       GetGeneralRegistersUsedAsInputs(eager_deopt_info()));
  __ SmiTagIntPtrAndJumpIfFail(reg, reg, fail);
}

void UnsafeSmiTagInt32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}
void UnsafeSmiTagInt32::GenerateCode(MaglevAssembler* masm,
                                     const ProcessingState& state) {
  __ UncheckedSmiTagInt32(ToRegister(input()));
}

void UnsafeSmiTagUint32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}
void UnsafeSmiTagUint32::GenerateCode(MaglevAssembler* masm,
                                      const ProcessingState& state) {
  __ UncheckedSmiTagUint32(ToRegister(input()));
}

void UnsafeSmiTagIntPtr::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}
void UnsafeSmiTagIntPtr::GenerateCode(MaglevAssembler* masm,
                                      const ProcessingState& state) {
  // If the IntPtr is guaranteed to be a SMI, we can treat it as Int32.
  // TODO(388844115): Rename IntPtr to make it clear it's non-negative.
  __ UncheckedSmiTagInt32(ToRegister(input()));
}

void CheckedSmiIncrement::SetValueLocationConstraints() {
  UseRegister(value_input());
  DefineSameAsFirst(this);
}

void CheckedSmiIncrement::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {
  Label* deopt_label = __ GetDeoptLabel(this, DeoptimizeReason::kOverflow);
  __ SmiAddConstant(ToRegister(value_input()), 1, deopt_label);
}

void CheckedSmiDecrement::SetValueLocationConstraints() {
  UseRegister(value_input());
  DefineSameAsFirst(this);
}

void CheckedSmiDecrement::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {
  Label* deopt_label = __ GetDeoptLabel(this, DeoptimizeReason::kOverflow);
  __ SmiSubConstant(ToRegister(value_input()), 1, deopt_label);
}

namespace {

void JumpToFailIfNotHeapNumberOrOddball(
    MaglevAssembler* masm, Register value,
    TaggedToFloat64ConversionType conversion_type, Label* fail) {
  if (!fail && !v8_flags.debug_code) return;

  static_assert(InstanceType::HEAP_NUMBER_TYPE + 1 ==
                InstanceType::ODDBALL_TYPE);
  switch (conversion_type) {
    case TaggedToFloat64ConversionType::kNumberOrBoolean: {
      // Check if HeapNumber or Boolean, jump to fail otherwise.
      MaglevAssembler::TemporaryRegisterScope temps(masm);
      Register map = temps.AcquireScratch();

#if V8_STATIC_ROOTS_BOOL
      static_assert(StaticReadOnlyRoot::kBooleanMap + Map::kSize ==
                    StaticReadOnlyRoot::kHeapNumberMap);
      __ LoadMapForCompare(map, value);
      if (fail) {
        __ JumpIfObjectNotInRange(map, StaticReadOnlyRoot::kBooleanMap,
                                  StaticReadOnlyRoot::kHeapNumberMap, fail);
      } else {
        __ AssertObjectInRange(map, StaticReadOnlyRoot::kBooleanMap,
                               StaticReadOnlyRoot::kHeapNumberMap,
                               AbortReason::kUnexpectedValue);
      }
#else
      Label done;
      __ LoadMap(map, value);
      __ CompareRoot(map, RootIndex::kHeapNumberMap);
      __ JumpIf(kEqual, &done);
      __ CompareRoot(map, RootIndex::kBooleanMap);
      if (fail) {
        __ JumpIf(kNotEqual, fail);
      } else {
        __ Assert(kEqual, AbortReason::kUnexpectedValue);
      }
      __ bind(&done);
#endif
      break;
    }

    case TaggedToFloat64ConversionType::kNumberOrUndefined: {
      // Check if HeapNumber or Undefined, jump to fail otherwise.
      MaglevAssembler::TemporaryRegisterScope temps(masm);
      Register map = temps.AcquireScratch();

      Label done;
      __ LoadMap(map, value);
      __ CompareRoot(map, RootIndex::kHeapNumberMap);
      __ JumpIf(kEqual, &done);
      __ CompareRoot(map, RootIndex::kUndefinedMap);
      if (fail) {
        __ JumpIf(kNotEqual, fail);
      } else {
        __ Assert(kEqual, AbortReason::kUnexpectedValue);
      }
      __ bind(&done);
      break;
    }

    case TaggedToFloat64ConversionType::kNumberOrOddball:
      // Check if HeapNumber or Oddball, jump to fail otherwise.
      if (fail) {
        __ JumpIfObjectTypeNotInRange(value, InstanceType::HEAP_NUMBER_TYPE,
                                      InstanceType::ODDBALL_TYPE, fail);
      } else {
        __ AssertObjectTypeInRange(value, InstanceType::HEAP_NUMBER_TYPE,
                                   InstanceType::ODDBALL_TYPE,
                                   AbortReason::kUnexpectedValue);
      }
      break;
    case TaggedToFloat64ConversionType::kOnlyNumber:
      // Check if HeapNumber, jump to fail otherwise.
      if (fail) {
        __ JumpIfNotObjectType(value, InstanceType::HEAP_NUMBER_TYPE, fail);
      } else {
        __ AssertObjectType(value, InstanceType::HEAP_NUMBER_TYPE,
                            AbortReason::kUnexpectedValue);
      }
      break;
  }
}

void TryUnboxNumberOrOddball(MaglevAssembler* masm, DoubleRegister dst,
                             Register clobbered_src,
                             TaggedToFloat64ConversionType conversion_type,
                             Label* fail) {
  Label is_not_smi, done;
  // Check if Smi.
  __ JumpIfNotSmi(clobbered_src, &is_not_smi, Label::kNear);
  // If Smi, convert to Float64.
  __ SmiToInt32(clobbered_src);
  __ Int32ToDouble(dst, clobbered_src);
  __ Jump(&done);
  __ bind(&is_not_smi);
  JumpToFailIfNotHeapNumberOrOddball(masm, clobbered_src, conversion_type,
                                     fail);
  __ LoadHeapNumberOrOddballValue(dst, clobbered_src);
  __ bind(&done);
}

}  // namespace
template <typename Derived, ValueRepresentation FloatType>
  requires(FloatType == ValueRepresentation::kFloat64 ||
           FloatType == ValueRepresentation::kHoleyFloat64)
void CheckedNumberOrOddballToFloat64OrHoleyFloat64<
    Derived, FloatType>::SetValueLocationConstraints() {
  UseAndClobberRegister(input());
  DefineAsRegister(this);
}
template <typename Derived, ValueRepresentation FloatType>
  requires(FloatType == ValueRepresentation::kFloat64 ||
           FloatType == ValueRepresentation::kHoleyFloat64)
void CheckedNumberOrOddballToFloat64OrHoleyFloat64<
    Derived, FloatType>::GenerateCode(MaglevAssembler* masm,
                                      const ProcessingState& state) {
  Register value = ToRegister(input());
  TryUnboxNumberOrOddball(masm, ToDoubleRegister(result()), value,
                          conversion_type(),
                          __ GetDeoptLabel(this, deoptimize_reason()));
}

void CheckedNumberOrOddballToHoleyFloat64::SetValueLocationConstraints() {
  UseAndClobberRegister(input());
  DefineAsRegister(this);
}
void CheckedNumberOrOddballToHoleyFloat64::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  Register src = ToRegister(input());
  DoubleRegister dst = ToDoubleRegister(result());
  Label* fail = __ GetDeoptLabel(this, deoptimize_reason());

  Label is_not_smi, done;
  // Check if Smi.
  __ JumpIfNotSmi(src, &is_not_smi, Label::kNear);
  // If Smi, convert to Float64.
  __ SmiToInt32(src);
  __ Int32ToDouble(dst, src);
  __ Jump(&done);
  __ bind(&is_not_smi);
  JumpToFailIfNotHeapNumberOrOddball(masm, src, conversion_type(), fail);
  __ LoadHeapNumberOrOddballValue(dst, src);
  __ JumpIfNotObjectType(src, InstanceType::HEAP_NUMBER_TYPE, &done,
                         Label::kNear);
  __ Float64SilenceNan(dst);
  __ bind(&done);
}

void UncheckedNumberOrOddballToFloat64::SetValueLocationConstraints() {
  UseAndClobberRegister(input());
  DefineAsRegister(this);
}
void UncheckedNumberOrOddballToFloat64::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  Register value = ToRegister(input());
  TryUnboxNumberOrOddball(masm, ToDoubleRegister(result()), value,
                          conversion_type(), nullptr);
}

void CheckedNumberToInt32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
  set_double_temporaries_needed(1);
}
void CheckedNumberToInt32::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  DoubleRegister double_value = temps.AcquireDouble();
  Register value = ToRegister(input());
  Label is_not_smi, done;
  Label* deopt_label = __ GetDeoptLabel(this, DeoptimizeReason::kNotInt32);
  // Check if Smi.
  __ JumpIfNotSmi(value, &is_not_smi, Label::kNear);
  __ SmiToInt32(ToRegister(result()), value);
  __ Jump(&done);
  __ bind(&is_not_smi);
  // Check if Number.
  JumpToFailIfNotHeapNumberOrOddball(
      masm, value, TaggedToFloat64ConversionType::kOnlyNumber, deopt_label);
  __ LoadHeapNumberValue(double_value, value);
  __ TryTruncateDoubleToInt32(ToRegister(result()), double_value, deopt_label);
  __ bind(&done);
}

namespace {

void EmitTruncateNumberOrOddballToInt32(
    MaglevAssembler* masm, Register value, Register result_reg,
    TaggedToFloat64ConversionType conversion_type, Label* not_a_number) {
  Label is_not_smi, done;
  // Check if Smi.
  __ JumpIfNotSmi(value, &is_not_smi, Label::kNear);
  // If Smi, convert to Int32.
  __ SmiToInt32(value);
  __ Jump(&done, Label::kNear);
  __ bind(&is_not_smi);
  JumpToFailIfNotHeapNumberOrOddball(masm, value, conversion_type,
                                     not_a_number);
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  DoubleRegister double_value = temps.AcquireScratchDouble();
  __ LoadHeapNumberOrOddballValue(double_value, value);
  __ TruncateDoubleToInt32(result_reg, double_value);
  __ bind(&done);
}

}  // namespace

void CheckedObjectToIndex::SetValueLocationConstraints() {
  UseRegister(object_input());
  DefineAsRegister(this);
  set_double_temporaries_needed(1);
}
void CheckedObjectToIndex::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  Register object = ToRegister(object_input());
  Register result_reg = ToRegister(result());
  ZoneLabelRef done(masm);
  __ JumpIfNotSmi(
      object,
      __ MakeDeferredCode(
          [](MaglevAssembler* masm, Register object, Register result_reg,
             ZoneLabelRef done, CheckedObjectToIndex* node) {
            MaglevAssembler::TemporaryRegisterScope temps(masm);
            Register map = temps.AcquireScratch();
            Label check_string;
            __ LoadMapForCompare(map, object);
            __ JumpIfNotRoot(
                map, RootIndex::kHeapNumberMap, &check_string,
                v8_flags.deopt_every_n_times > 0 ? Label::kFar : Label::kNear);
            {
              DoubleRegister number_value = temps.AcquireDouble();
              __ LoadHeapNumberValue(number_value, object);
              __ TryChangeFloat64ToIndex(
                  result_reg, number_value, *done,
                  __ GetDeoptLabel(node, DeoptimizeReason::kNotInt32));
            }
            __ bind(&check_string);
            // The IC will go generic if it encounters something other than a
            // Number or String key.
            __ JumpIfStringMap(
                map, __ GetDeoptLabel(node, DeoptimizeReason::kNotInt32),
                Label::kFar, false);
            // map is clobbered after this call.

            {
              // TODO(verwaest): Load the cached number from the string hash.
              RegisterSnapshot snapshot = node->register_snapshot();
              snapshot.live_registers.clear(result_reg);
              DCHECK(!snapshot.live_tagged_registers.has(result_reg));
              {
                SaveRegisterStateForCall save_register_state(masm, snapshot);
                AllowExternalCallThatCantCauseGC scope(masm);
                __ PrepareCallCFunction(1);
                __ Move(kCArgRegs[0], object);
                __ CallCFunction(
                    ExternalReference::string_to_array_index_function(), 1);
                // No need for safepoint since this is a fast C call.
                __ Move(result_reg, kReturnRegister0);
              }
              __ CompareInt32AndJumpIf(
                  result_reg, 0, kLessThan,
                  __ GetDeoptLabel(node, DeoptimizeReason::kNotInt32));
              __ Jump(*done);
            }
          },
          object, result_reg, done, this));

  // If we didn't enter the deferred block, we're a Smi.
  __ SmiToInt32(result_reg, object);
  __ bind(*done);
}

void CheckedTruncateNumberOrOddballToInt32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}
void CheckedTruncateNumberOrOddballToInt32::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  Register value = ToRegister(input());
  Register result_reg = ToRegister(result());
  DCHECK_EQ(value, result_reg);
  Label* deopt_label =
      __ GetDeoptLabel(this, DeoptimizeReason::kNotANumberOrOddball);
  EmitTruncateNumberOrOddballToInt32(masm, value, result_reg, conversion_type(),
                                     deopt_label);
}

void TruncateNumberOrOddballToInt32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}
void TruncateNumberOrOddballToInt32::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  Register value = ToRegister(input());
  Register result_reg = ToRegister(result());
  DCHECK_EQ(value, result_reg);
  EmitTruncateNumberOrOddballToInt32(masm, value, result_reg, conversion_type(),
                                     nullptr);
}

void ChangeInt32ToFloat64::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void ChangeInt32ToFloat64::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  __ Int32ToDouble(ToDoubleRegister(result()), ToRegister(input()));
}

void ChangeUint32ToFloat64::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void ChangeUint32ToFloat64::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  __ Uint32ToDouble(ToDoubleRegister(result()), ToRegister(input()));
}

void ChangeIntPtrToFloat64::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void ChangeIntPtrToFloat64::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  __ IntPtrToDouble(ToDoubleRegister(result()), ToRegister(input()));
}

void CheckMaps::SetValueLocationConstraints() {
  UseRegister(receiver_input());
  set_temporaries_needed(MapCompare::TemporaryCount(maps_.size()));
}

void CheckMaps::GenerateCode(MaglevAssembler* masm,
                             const ProcessingState& state) {
  Register object = ToRegister(receiver_input());

  // We emit an unconditional deopt if we intersect the map sets and the
  // intersection is empty.
  DCHECK(!maps().is_empty());

  bool maps_include_heap_number = compiler::AnyMapIsHeapNumber(maps());

  // Experimentally figured out map limit (with slack) which allows us to use
  // near jumps in the code below. If --deopt-every-n-times is on, we generate
  // a bit more code, so disable the near jump optimization.
  constexpr int kMapCountForNearJumps = kTaggedSize == 4 ? 10 : 5;
  Label::Distance jump_distance = (maps().size() <= kMapCountForNearJumps &&
                                   v8_flags.deopt_every_n_times <= 0)
                                      ? Label::Distance::kNear
                                      : Label::Distance::kFar;

  Label done;
  if (check_type() == CheckType::kOmitHeapObjectCheck) {
    __ AssertNotSmi(object);
  } else {
    if (maps_include_heap_number) {
      // Smis count as matching the HeapNumber map, so we're done.
      __ JumpIfSmi(object, &done, jump_distance);
    } else {
      __ EmitEagerDeoptIfSmi(this, object, DeoptimizeReason::kWrongMap);
    }
  }

  MapCompare map_compare(masm, object, maps_.size());
  size_t map_count = maps().size();
  for (size_t i = 0; i < map_count - 1; ++i) {
    Handle<Map> map = maps().at(i).object();
    map_compare.Generate(map, kEqual, &done, jump_distance);
  }
  Handle<Map> last_map = maps().at(map_count - 1).object();
  Label* fail = __ GetDeoptLabel(this, DeoptimizeReason::kWrongMap);
  map_compare.Generate(last_map, kNotEqual, fail);
  __ bind(&done);
}

int CheckMapsWithMigrationAndDeopt::MaxCallStackArgs() const {
  DCHECK_EQ(Runtime::FunctionForId(
                Runtime::kTryMigrateInstanceAndMarkMapAsMigrationTarget)
                ->nargs,
            1);
  return 1;
}

void CheckMapsWithMigrationAndDeopt::SetValueLocationConstraints() {
  UseRegister(receiver_input());
  set_temporaries_needed(MapCompare::TemporaryCount(maps_.size()));
}

void CheckMapsWithMigrationAndDeopt::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  Register object = ToRegister(receiver_input());

  // We emit an unconditional deopt if we intersect the map sets and the
  // intersection is empty.
  DCHECK(!maps().is_empty());

  bool maps_include_heap_number = compiler::AnyMapIsHeapNumber(maps());

  // Experimentally figured out map limit (with slack) which allows us to use
  // near jumps in the code below. If --deopt-every-n-times is on, we generate
  // a bit more code, so disable the near jump optimization.
  constexpr int kMapCountForNearJumps = kTaggedSize == 4 ? 10 : 5;
  Label::Distance jump_distance = (maps().size() <= kMapCountForNearJumps &&
                                   v8_flags.deopt_every_n_times <= 0)
                                      ? Label::Distance::kNear
                                      : Label::Distance::kFar;

  Label done;
  if (check_type() == CheckType::kOmitHeapObjectCheck) {
    __ AssertNotSmi(object);
  } else {
    if (maps_include_heap_number) {
      // Smis count as matching the HeapNumber map, so we're done.
      __ JumpIfSmi(object, &done, jump_distance);
    } else {
      __ EmitEagerDeoptIfSmi(this, object, DeoptimizeReason::kWrongMap);
    }
  }

  MapCompare map_compare(masm, object, maps_.size());
  size_t map_count = maps().size();
  for (size_t i = 0; i < map_count - 1; ++i) {
    Handle<Map> map = maps().at(i).object();
    map_compare.Generate(map, kEqual, &done, jump_distance);
  }

  Handle<Map> last_map = maps().at(map_count - 1).object();
  map_compare.Generate(
      last_map, kNotEqual,
      __ MakeDeferredCode(
          [](MaglevAssembler* masm, RegisterSnapshot register_snapshot,
             MapCompare map_compare, CheckMapsWithMigrationAndDeopt* node) {
            Label* deopt = __ GetDeoptLabel(node, DeoptimizeReason::kWrongMap);
            // If the map is not deprecated, we fail the map check.
            __ TestInt32AndJumpIfAllClear(
                FieldMemOperand(map_compare.GetMap(), Map::kBitField3Offset),
                Map::Bits3::IsDeprecatedBit::kMask, deopt);

            // Otherwise, try migrating the object.
            __ TryMigrateInstanceAndMarkMapAsMigrationTarget(
                map_compare.GetObject(), register_snapshot);
            // Deopt even if the migration was successful.
            __ JumpToDeopt(deopt);
          },
          register_snapshot(), map_compare, this));
  // If the jump to deferred code was not taken, the map was equal to the
  // last map.
  __ bind(&done);
}

int CheckMapsWithMigration::MaxCallStackArgs() const {
  DCHECK_EQ(Runtime::FunctionForId(Runtime::kTryMigrateInstance)->nargs, 1);
  return 1;
}

void CheckMapsWithMigration::SetValueLocationConstraints() {
  UseRegister(receiver_input());
  set_temporaries_needed(MapCompare::TemporaryCount(maps_.size()));
}

void CheckMapsWithMigration::GenerateCode(MaglevAssembler* masm,
                                          const ProcessingState& state) {
  // We emit an unconditional deopt if we intersect the map sets and the
  // intersection is empty.
  DCHECK(!maps().is_empty());

  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register object = ToRegister(receiver_input());

  bool maps_include_heap_number = compiler::AnyMapIsHeapNumber(maps());

  ZoneLabelRef map_checks(masm), done(masm);

  if (check_type() == CheckType::kOmitHeapObjectCheck) {
    __ AssertNotSmi(object);
  } else {
    if (maps_include_heap_number) {
      // Smis count as matching the HeapNumber map, so we're done.
      __ JumpIfSmi(object, *done);
    } else {
      __ EmitEagerDeoptIfSmi(this, object, DeoptimizeReason::kWrongMap);
    }
  }

  // If we jump from here from the deferred code (below), we need to reload
  // the map.
  __ bind(*map_checks);

  RegisterSnapshot save_registers = register_snapshot();
  // Make sure that the object register is not clobbered by the
  // Runtime::kMigrateInstance runtime call. It's ok to clobber the register
  // where the object map is, since the map is reloaded after the runtime call.
  save_registers.live_registers.set(object);
  save_registers.live_tagged_registers.set(object);

  size_t map_count = maps().size();
  bool has_migration_targets = false;
  MapCompare map_compare(masm, object, maps_.size());
  Handle<Map> map_handle;
  for (size_t i = 0; i < map_count; ++i) {
    map_handle = maps().at(i).object();
    const bool last_map = (i == map_count - 1);
    if (!last_map) {
      map_compare.Generate(map_handle, kEqual, *done);
    }
    if (map_handle->is_migration_target()) {
      has_migration_targets = true;
    }
  }

  if (!has_migration_targets) {
    // Emit deopt for the last map.
    map_compare.Generate(map_handle, kNotEqual,
                         __ GetDeoptLabel(this, DeoptimizeReason::kWrongMap));
  } else {
    map_compare.Generate(
        map_handle, kNotEqual,
        __ MakeDeferredCode(
            [](MaglevAssembler* masm, RegisterSnapshot register_snapshot,
               ZoneLabelRef map_checks, MapCompare map_compare,
               CheckMapsWithMigration* node) {
              Label* deopt =
                  __ GetDeoptLabel(node, DeoptimizeReason::kWrongMap);
              // If the map is not deprecated, we fail the map check.
              __ TestInt32AndJumpIfAllClear(
                  FieldMemOperand(map_compare.GetMap(), Map::kBitField3Offset),
                  Map::Bits3::IsDeprecatedBit::kMask, deopt);

              // Otherwise, try migrating the object.
              __ TryMigrateInstance(map_compare.GetObject(), register_snapshot,
                                    deopt);
              __ Jump(*map_checks);
              // We'll need to reload the map since it might have changed; it's
              // done right after the map_checks label.
            },
            save_registers, map_checks, map_compare, this));
    // If the jump to deferred code was not taken, the map was equal to the
    // last map.
  }  // End of the `has_migration_targets` case.
  __ bind(*done);
}

void CheckMapsWithAlreadyLoadedMap::SetValueLocationConstraints() {
  UseRegister(object_input());
  UseRegister(map_input());
}

void CheckMapsWithAlreadyLoadedMap::GenerateCode(MaglevAssembler* masm,
                                                 const ProcessingState& state) {
  Register map = ToRegister(map_input());

  // We emit an unconditional deopt if we intersect the map sets and the
  // intersection is empty.
  DCHECK(!maps().is_empty());

  // CheckMapsWithAlreadyLoadedMap can only be used in contexts where SMIs /
  // HeapNumbers don't make sense (e.g., if we're loading properties from them).
  DCHECK(!compiler::AnyMapIsHeapNumber(maps()));

  // Experimentally figured out map limit (with slack) which allows us to use
  // near jumps in the code below. If --deopt-every-n-times is on, we generate
  // a bit more code, so disable the near jump optimization.
  constexpr int kMapCountForNearJumps = kTaggedSize == 4 ? 10 : 5;
  Label::Distance jump_distance = (maps().size() <= kMapCountForNearJumps &&
                                   v8_flags.deopt_every_n_times <= 0)
                                      ? Label::Distance::kNear
                                      : Label::Distance::kFar;

  Label done;
  size_t map_count = maps().size();
  for (size_t i = 0; i < map_count - 1; ++i) {
    Handle<Map> map_at_i = maps().at(i).object();
    __ CompareTaggedAndJumpIf(map, map_at_i, kEqual, &done, jump_distance);
  }
  Handle<Map> last_map = maps().at(map_count - 1).object();
  Label* fail = __ GetDeoptLabel(this, DeoptimizeReason::kWrongMap);
  __ CompareTaggedAndJumpIf(map, last_map, kNotEqual, fail);
  __ bind(&done);
}

int MigrateMapIfNeeded::MaxCallStackArgs() const {
  DCHECK_EQ(Runtime::FunctionForId(Runtime::kTryMigrateInstance)->nargs, 1);
  return 1;
}

void MigrateMapIfNeeded::SetValueLocationConstraints() {
  UseRegister(map_input());
  UseRegister(object_input());
  DefineSameAsFirst(this);
}

void MigrateMapIfNeeded::GenerateCode(MaglevAssembler* masm,
                                      const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register object = ToRegister(object_input());
  Register map = ToRegister(map_input());
  DCHECK_EQ(map, ToRegister(result()));

  ZoneLabelRef done(masm);

  RegisterSnapshot save_registers = register_snapshot();
  // Make sure that the object register are not clobbered by TryMigrateInstance
  // (which does a runtime call). We need the object register for reloading the
  // map. It's okay to clobber the map register, since we will always reload (or
  // deopt) after the runtime call.
  save_registers.live_registers.set(object);
  save_registers.live_tagged_registers.set(object);

  // If the map is deprecated, jump to the deferred code which will migrate it.
  __ TestInt32AndJumpIfAnySet(
      FieldMemOperand(map, Map::kBitField3Offset),
      Map::Bits3::IsDeprecatedBit::kMask,
      __ MakeDeferredCode(
          [](MaglevAssembler* masm, RegisterSnapshot register_snapshot,
             ZoneLabelRef done, Register object, Register map,
             MigrateMapIfNeeded* node) {
            Label* deopt = __ GetDeoptLabel(node, DeoptimizeReason::kWrongMap);
            __ TryMigrateInstance(object, register_snapshot, deopt);
            // Reload the map since TryMigrateInstance might have changed it.
            __ LoadTaggedField(map, object, HeapObject::kMapOffset);
            __ Jump(*done);
          },
          save_registers, done, object, map, this));

  // No migration needed. Return the original map. We already have it in the
  // first input register which is the same as the return register.

  __ bind(*done);
}

int DeleteProperty::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kDeleteProperty>::type;
  return D::GetStackParameterCount();
}
void DeleteProperty::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kDeleteProperty>::type;
  UseFixed(context(), kContextRegister);
  UseFixed(object(), D::GetRegisterParameter(D::kObject));
  UseFixed(key(), D::GetRegisterParameter(D::kKey));
  DefineAsFixed(this, kReturnRegister0);
}
void DeleteProperty::GenerateCode(MaglevAssembler* masm,
                                  const ProcessingState& state) {
  __ CallBuiltin<Builtin::kDeleteProperty>(
      context(),                              // context
      object(),                               // object
      key(),                                  // key
      Smi::FromInt(static_cast<int>(mode()))  // language mode
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int ForInPrepare::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kForInPrepare>::type;
  return D::GetStackParameterCount();
}
void ForInPrepare::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kForInPrepare>::type;
  UseFixed(context(), kContextRegister);
  UseFixed(enumerator(), D::GetRegisterParameter(D::kEnumerator));
  DefineAsFixed(this, kReturnRegister0);
}
void ForInPrepare::GenerateCode(MaglevAssembler* masm,
                                const ProcessingState& state) {
  __ CallBuiltin<Builtin::kForInPrepare>(
      context(),                                    // context
      enumerator(),                                 // enumerator
      TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
      feedback().vector                             // feedback vector
  );
}

int ForInNext::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kForInNext>::type;
  return D::GetStackParameterCount();
}
void ForInNext::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kForInNext>::type;
  UseFixed(context(), kContextRegister);
  UseFixed(receiver(), D::GetRegisterParameter(D::kReceiver));
  UseFixed(cache_array(), D::GetRegisterParameter(D::kCacheArray));
  UseFixed(cache_type(), D::GetRegisterParameter(D::kCacheType));
  UseFixed(cache_index(), D::GetRegisterParameter(D::kCacheIndex));
  DefineAsFixed(this, kReturnRegister0);
}
void ForInNext::GenerateCode(MaglevAssembler* masm,
                             const ProcessingState& state) {
  __ CallBuiltin<Builtin::kForInNext>(context(),           // context
                                      feedback().index(),  // feedback slot
                                      receiver(),          // receiver
                                      cache_array(),       // cache array
                                      cache_type(),        // cache type
                                      cache_index(),       // cache index
                                      feedback().vector    // feedback vector
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int GetIterator::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kGetIteratorWithFeedback>::type;
  return D::GetStackParameterCount();
}
void GetIterator::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kGetIteratorWithFeedback>::type;
  UseFixed(context(), kContextRegister);
  UseFixed(receiver(), D::GetRegisterParameter(D::kReceiver));
  DefineAsFixed(this, kReturnRegister0);
}
void GetIterator::GenerateCode(MaglevAssembler* masm,
                               const ProcessingState& state) {
  __ CallBuiltin<Builtin::kGetIteratorWithFeedback>(
      context(),                             // context
      receiver(),                            // receiver
      TaggedIndex::FromIntptr(load_slot()),  // feedback load slot
      TaggedIndex::FromIntptr(call_slot()),  // feedback call slot
      feedback()                             // feedback vector
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

void Int32Compare::SetValueLocationConstraints() {
  UseRegister(left_input());
  if (right_input().node()->Is<Int32Constant>()) {
    UseAny(right_input());
  } else {
    UseRegister(right_input());
  }
  DefineAsRegister(this);
}

void Int32Compare::GenerateCode(MaglevAssembler* masm,
                                const ProcessingState& state) {
  Register result = ToRegister(this->result());
  Label is_true, end;
  if (Int32Constant* constant =
          right_input().node()->TryCast<Int32Constant>()) {
    int32_t right_value = constant->value();
    __ CompareInt32AndJumpIf(ToRegister(left_input()), right_value,
                             ConditionFor(operation()), &is_true,
                             Label::Distance::kNear);
  } else {
    __ CompareInt32AndJumpIf(
        ToRegister(left_input()), ToRegister(right_input()),
        ConditionFor(operation()), &is_true, Label::Distance::kNear);
  }
  // TODO(leszeks): Investigate loading existing materialisations of roots here,
  // if available.
  __ LoadRoot(result, RootIndex::kFalseValue);
  __ jmp(&end);
  {
    __ bind(&is_true);
    __ LoadRoot(result, RootIndex::kTrueValue);
  }
  __ bind(&end);
}

void Int32ToBoolean::SetValueLocationConstraints() {
  UseRegister(value());
  DefineAsRegister(this);
}

void Int32ToBoolean::GenerateCode(MaglevAssembler* masm,
                                  const ProcessingState& state) {
  Register result = ToRegister(this->result());
  Label is_true, end;
  __ CompareInt32AndJumpIf(ToRegister(value()), 0, kNotEqual, &is_true,
                           Label::Distance::kNear);
  // TODO(leszeks): Investigate loading existing materialisations of roots here,
  // if available.
  __ LoadRoot(result, flip() ? RootIndex::kTrueValue : RootIndex::kFalseValue);
  __ jmp(&end);
  {
    __ bind(&is_true);
    __ LoadRoot(result,
                flip() ? RootIndex::kFalseValue : RootIndex::kTrueValue);
  }
  __ bind(&end);
}

void IntPtrToBoolean::SetValueLocationConstraints() {
  UseRegister(value());
  DefineAsRegister(this);
}

void IntPtrToBoolean::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {
  Register result = ToRegister(this->result());
  Label is_true, end;
  __ CompareIntPtrAndJumpIf(ToRegister(value()), 0, kNotEqual, &is_true,
                            Label::Distance::kNear);
  // TODO(leszeks): Investigate loading existing materialisations of roots here,
  // if available.
  __ LoadRoot(result, flip() ? RootIndex::kTrueValue : RootIndex::kFalseValue);
  __ jmp(&end);
  {
    __ bind(&is_true);
    __ LoadRoot(result,
                flip() ? RootIndex::kFalseValue : RootIndex::kTrueValue);
  }
  __ bind(&end);
}

void Float64Compare::SetValueLocationConstraints() {
  UseRegister(left_input());
  UseRegister(right_input());
  DefineAsRegister(this);
}

void Float64Compare::GenerateCode(MaglevAssembler* masm,
                                  const ProcessingState& state) {
  DoubleRegister left = ToDoubleRegister(left_input());
  DoubleRegister right = ToDoubleRegister(right_input());
  Register result = ToRegister(this->result());
  Label is_false, end;
  __ CompareFloat64AndJumpIf(left, right,
                             NegateCondition(ConditionForFloat64(operation())),
                             &is_false, &is_false, Label::Distance::kNear);
  // TODO(leszeks): Investigate loading existing materialisations of roots here,
  // if available.
  __ LoadRoot(result, RootIndex::kTrueValue);
  __ Jump(&end);
  {
    __ bind(&is_false);
    __ LoadRoot(result, RootIndex::kFalseValue);
  }
  __ bind(&end);
}

void Float64ToBoolean::SetValueLocationConstraints() {
  UseRegister(value());
  set_double_temporaries_needed(1);
  DefineAsRegister(this);
}
void Float64ToBoolean::GenerateCode(MaglevAssembler* masm,
                                    const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  DoubleRegister double_scratch = temps.AcquireDouble();
  Register result = ToRegister(this->result());
  Label is_false, end;

  __ Move(double_scratch, 0.0);
  __ CompareFloat64AndJumpIf(ToDoubleRegister(value()), double_scratch, kEqual,
                             &is_false, &is_false, Label::Distance::kNear);

  __ LoadRoot(result, flip() ? RootIndex::kFalseValue : RootIndex::kTrueValue);
  __ Jump(&end);
  {
    __ bind(&is_false);
    __ LoadRoot(result,
                flip() ? RootIndex::kTrueValue : RootIndex::kFalseValue);
  }
  __ bind(&end);
}

void CheckedHoleyFloat64ToFloat64::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
  set_temporaries_needed(1);
}
void CheckedHoleyFloat64ToFloat64::GenerateCode(MaglevAssembler* masm,
                                                const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  __ JumpIfHoleNan(ToDoubleRegister(input()), temps.Acquire(),
                   __ GetDeoptLabel(this, DeoptimizeReason::kHole));
}

void LoadDoubleField::SetValueLocationConstraints() {
  UseRegister(object_input());
  DefineAsRegister(this);
  set_temporaries_needed(1);
}
void LoadDoubleField::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register tmp = temps.Acquire();
  Register object = ToRegister(object_input());
  __ AssertNotSmi(object);
  __ LoadTaggedField(tmp, object, offset());
  __ AssertNotSmi(tmp);
  __ LoadHeapNumberValue(ToDoubleRegister(result()), tmp);
}

void LoadFloat64::SetValueLocationConstraints() {
  UseRegister(object_input());
  DefineAsRegister(this);
}
void LoadFloat64::GenerateCode(MaglevAssembler* masm,
                               const ProcessingState& state) {
  Register object = ToRegister(object_input());
  __ AssertNotSmi(object);
  __ LoadFloat64(ToDoubleRegister(result()), FieldMemOperand(object, offset()));
}

void LoadInt32::SetValueLocationConstraints() {
  UseRegister(object_input());
  DefineAsRegister(this);
}
void LoadInt32::GenerateCode(MaglevAssembler* masm,
                             const ProcessingState& state) {
  Register object = ToRegister(object_input());
  __ AssertNotSmi(object);
  __ LoadInt32(ToRegister(result()), FieldMemOperand(object, offset()));
}

template <typename T>
void AbstractLoadTaggedField<T>::SetValueLocationConstraints() {
  UseRegister(object_input());
  DefineAsRegister(this);
}
template <typename T>
void AbstractLoadTaggedField<T>::GenerateCode(MaglevAssembler* masm,
                                              const ProcessingState& state) {
  Register object = ToRegister(object_input());
  __ AssertNotSmi(object);
  if (this->decompresses_tagged_result()) {
    __ LoadTaggedField(ToRegister(result()), object, offset());
  } else {
    __ LoadTaggedFieldWithoutDecompressing(ToRegister(result()), object,
                                           offset());
  }
}

void LoadTaggedFieldForContextSlot::SetValueLocationConstraints() {
  UseRegister(context());
  set_temporaries_needed(2);
  set_double_temporaries_needed(1);
  DefineAsRegister(this);
}

void LoadTaggedFieldForContextSlot::GenerateCode(MaglevAssembler* masm,
                                                 const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register script_context = ToRegister(context());
  Register value = ToRegister(result());
  Register scratch = temps.Acquire();
  ZoneLabelRef done(masm);

  // Load value from context.
  __ LoadTaggedField(value, script_context, offset());

  // Check if an ContextCell
  __ JumpIfSmi(value, *done);
  __ CompareMapWithRoot(value, RootIndex::kContextCellMap, scratch);
  __ JumpToDeferredIf(
      kEqual,
      [](MaglevAssembler* masm, Register value, Register scratch,
         LoadTaggedFieldForContextSlot* node, ZoneLabelRef done) {
        MaglevAssembler::TemporaryRegisterScope temps(masm);
        DoubleRegister double_value = temps.AcquireDouble();
        Label allocate, is_untagged;
        __ LoadContextCellState(scratch, value);

        static_assert(ContextCell::State::kConst == 0);
        static_assert(ContextCell::State::kSmi == 1);
        __ CompareInt32AndJumpIf(scratch, ContextCell::kSmi, kGreaterThan,
                                 &is_untagged);
        __ LoadContextCellTaggedValue(value, value);
        __ Jump(*done);

        __ bind(&is_untagged);
        {
          Label check_float64;
          __ CompareInt32AndJumpIf(scratch, ContextCell::kInt32, kNotEqual,
                                   &check_float64);
          __ LoadContextCellInt32Value(scratch, value);
          __ SmiTagInt32AndJumpIfSuccess(value, scratch, *done);
          __ Int32ToDouble(double_value, scratch);
          __ Jump(&allocate, Label::kNear);
          __ bind(&check_float64);
        }
        __ LoadContextCellFloat64Value(double_value, value);

        __ bind(&allocate);
        __ AllocateHeapNumber(node->register_snapshot(), value, double_value);
        __ Jump(*done);
      },
      value, scratch, this, done);

  __ bind(*done);
}

void LoadTaggedFieldByFieldIndex::SetValueLocationConstraints() {
  UseRegister(object_input());
  UseAndClobberRegister(index_input());
  DefineAsRegister(this);
  set_temporaries_needed(1);
  set_double_temporaries_needed(1);
}
void LoadTaggedFieldByFieldIndex::GenerateCode(MaglevAssembler* masm,
                                               const ProcessingState& state) {
  Register object = ToRegister(object_input());
  Register field_index = ToRegister(index_input());
  Register result_reg = ToRegister(result());
  __ AssertNotSmi(object);
  __ AssertSmi(field_index);

  ZoneLabelRef done(masm);

  // For in-object properties, the field_index is encoded as:
  //
  //      field_index = array_index | is_double_bit | smi_tag
  //                  = array_index << (1+kSmiTagBits)
  //                        + is_double_bit << kSmiTagBits
  //
  // The value we want is at the field offset:
  //
  //      (array_index << kTaggedSizeLog2) + JSObject::kHeaderSize
  //
  // We could get field_index from array_index by shifting away the double bit
  // and smi tag, followed by shifting back up again, but this means shifting
  // twice:
  //
  //      ((field_index >> kSmiTagBits >> 1) << kTaggedSizeLog2
  //          + JSObject::kHeaderSize
  //
  // Instead, we can do some rearranging to get the offset with either a single
  // small shift, or no shift at all:
  //
  //      (array_index << kTaggedSizeLog2) + JSObject::kHeaderSize
  //
  //    [Split shift to match array_index component of field_index]
  //    = (
  //        (array_index << 1+kSmiTagBits)) << (kTaggedSizeLog2-1-kSmiTagBits)
  //      ) + JSObject::kHeaderSize
  //
  //    [Substitute in field_index]
  //    = (
  //        (field_index - is_double_bit << kSmiTagBits)
  //           << (kTaggedSizeLog2-1-kSmiTagBits)
  //      ) + JSObject::kHeaderSize
  //
  //    [Fold together the constants]
  //    = (field_index << (kTaggedSizeLog2-1-kSmiTagBits)
  //          + (JSObject::kHeaderSize - (is_double_bit << (kTaggedSizeLog2-1)))
  //
  // Note that this results in:
  //
  //     * No shift when kSmiTagBits == kTaggedSizeLog2 - 1, which is the case
  //       when pointer compression is on.
  //     * A shift of 1 when kSmiTagBits == 1 and kTaggedSizeLog2 == 3, which
  //       is the case when pointer compression is off but Smis are 31 bit.
  //     * A shift of 2 when kSmiTagBits == 0 and kTaggedSizeLog2 == 3, which
  //       is the case when pointer compression is off, Smis are 32 bit, and
  //       the Smi was untagged to int32 already.
  //
  // These shifts are small enough to encode in the load operand.
  //
  // For out-of-object properties, the encoding is:
  //
  //     field_index = (-1 - array_index) | is_double_bit | smi_tag
  //                 = (-1 - array_index) << (1+kSmiTagBits)
  //                       + is_double_bit << kSmiTagBits
  //                 = -array_index << (1+kSmiTagBits)
  //                       - 1 << (1+kSmiTagBits) + is_double_bit << kSmiTagBits
  //                 = -array_index << (1+kSmiTagBits)
  //                       - 2 << kSmiTagBits + is_double_bit << kSmiTagBits
  //                 = -array_index << (1+kSmiTagBits)
  //                       (is_double_bit - 2) << kSmiTagBits
  //
  // The value we want is in the property array at offset:
  //
  //      (array_index << kTaggedSizeLog2) + OFFSET_OF_DATA_START(FixedArray)
  //
  //    [Split shift to match array_index component of field_index]
  //    = (array_index << (1+kSmiTagBits)) << (kTaggedSizeLog2-1-kSmiTagBits)
  //        + OFFSET_OF_DATA_START(FixedArray)
  //
  //    [Substitute in field_index]
  //    = (-field_index - (is_double_bit - 2) << kSmiTagBits)
  //        << (kTaggedSizeLog2-1-kSmiTagBits)
  //        + OFFSET_OF_DATA_START(FixedArray)
  //
  //    [Fold together the constants]
  //    = -field_index << (kTaggedSizeLog2-1-kSmiTagBits)
  //        + OFFSET_OF_DATA_START(FixedArray)
  //        - (is_double_bit - 2) << (kTaggedSizeLog2-1))
  //
  // This allows us to simply negate the field_index register and do a load with
  // otherwise constant offset and the same scale factor as for in-object
  // properties.

  static constexpr int kSmiTagBitsInValue = SmiValuesAre32Bits() ? 0 : 1;
  static_assert(kSmiTagBitsInValue == 32 - kSmiValueSize);
  if (SmiValuesAre32Bits()) {
    __ SmiUntag(field_index);
  }

  static constexpr int scale = 1 << (kTaggedSizeLog2 - 1 - kSmiTagBitsInValue);

  // Check if field is a mutable double field.
  static constexpr int32_t kIsDoubleBitMask = 1 << kSmiTagBitsInValue;
  __ TestInt32AndJumpIfAnySet(
      field_index, kIsDoubleBitMask,
      __ MakeDeferredCode(
          [](MaglevAssembler* masm, Register object, Register field_index,
             Register result_reg, RegisterSnapshot register_snapshot,
             ZoneLabelRef done) {
            // The field is a Double field, a.k.a. a mutable HeapNumber.
            static constexpr int kIsDoubleBit = 1;

            // Check if field is in-object or out-of-object. The is_double bit
            // value doesn't matter, since negative values will stay negative.
            Label if_outofobject, loaded_field;
            __ CompareInt32AndJumpIf(field_index, 0, kLessThan,
                                     &if_outofobject);

            // The field is located in the {object} itself.
            {
              // See giant comment above.
              if (SmiValuesAre31Bits() && kTaggedSize != kSystemPointerSize) {
                // We haven't untagged, so we need to sign extend.
                __ SignExtend32To64Bits(field_index, field_index);
              }
              __ LoadTaggedFieldByIndex(
                  result_reg, object, field_index, scale,
                  JSObject::kHeaderSize -
                      (kIsDoubleBit << (kTaggedSizeLog2 - 1)));
              __ Jump(&loaded_field);
            }

            __ bind(&if_outofobject);
            {
              MaglevAssembler::TemporaryRegisterScope temps(masm);
              Register property_array = temps.Acquire();
              // Load the property array.
              __ LoadTaggedField(
                  property_array,
                  FieldMemOperand(object, JSObject::kPropertiesOrHashOffset));

              // See giant comment above. No need to sign extend, negate will
              // handle it.
              __ NegateInt32(field_index);
              __ LoadTaggedFieldByIndex(
                  result_reg, property_array, field_index, scale,
                  OFFSET_OF_DATA_START(FixedArray) -
                      ((2 - kIsDoubleBit) << (kTaggedSizeLog2 - 1)));
              __ Jump(&loaded_field);
            }

            __ bind(&loaded_field);
            // We may have transitioned in-place away from double, so check that
            // this is a HeapNumber -- otherwise the load is fine and we don't
            // need to copy anything anyway.
            __ JumpIfSmi(result_reg, *done);
            MaglevAssembler::TemporaryRegisterScope temps(masm);
            Register map = temps.Acquire();
            // Hack: The temporary allocated for `map` might alias the result
            // register. If it does, use the field_index register as a temporary
            // instead (since it's clobbered anyway).
            // TODO(leszeks): Extend the result register's lifetime to overlap
            // the temporaries, so that this alias isn't possible.
            if (map == result_reg) {
              DCHECK_NE(map, field_index);
              map = field_index;
            }
            __ LoadMapForCompare(map, result_reg);
            __ JumpIfNotRoot(map, RootIndex::kHeapNumberMap, *done);
            DoubleRegister double_value = temps.AcquireDouble();
            __ LoadHeapNumberValue(double_value, result_reg);
            __ AllocateHeapNumber(register_snapshot, result_reg, double_value);
            __ Jump(*done);
          },
          object, field_index, result_reg, register_snapshot(), done));

  // The field is a proper Tagged field on {object}. The {field_index} is
  // shifted to the left by one in the code below.
  {
    static constexpr int kIsDoubleBit = 0;

    // Check if field is in-object or out-of-object. The is_double bit value
    // doesn't matter, since negative values will stay negative.
    Label if_outofobject;
    __ CompareInt32AndJumpIf(field_index, 0, kLessThan, &if_outofobject);

    // The field is located in the {object} itself.
    {
      // See giant comment above.
      if (SmiValuesAre31Bits() && kTaggedSize != kSystemPointerSize) {
        // We haven't untagged, so we need to sign extend.
        __ SignExtend32To64Bits(field_index, field_index);
      }
      __ LoadTaggedFieldByIndex(
          result_reg, object, field_index, scale,
          JSObject::kHeaderSize - (kIsDoubleBit << (kTaggedSizeLog2 - 1)));
      __ Jump(*done);
    }

    __ bind(&if_outofobject);
    {
      MaglevAssembler::TemporaryRegisterScope temps(masm);
      Register property_array = temps.Acquire();
      // Load the property array.
      __ LoadTaggedField(
          property_array,
          FieldMemOperand(object, JSObject::kPropertiesOrHashOffset));

      // See giant comment above. No need to sign extend, negate will handle it.
      __ NegateInt32(field_index);
      __ LoadTaggedFieldByIndex(
          result_reg, property_array, field_index, scale,
          OFFSET_OF_DATA_START(FixedArray) -
              ((2 - kIsDoubleBit) << (kTaggedSizeLog2 - 1)));
      // Fallthrough to `done`.
    }
  }

  __ bind(*done);
}

void LoadFixedArrayElement::SetValueLocationConstraints() {
  UseRegister(elements_input());
  UseRegister(index_input());
  DefineAsRegister(this);
}
void LoadFixedArrayElement::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  Register elements = ToRegister(elements_input());
  Register index = ToRegister(index_input());
  Register result_reg = ToRegister(result());
  if (this->decompresses_tagged_result()) {
    __ LoadFixedArrayElement(result_reg, elements, index);
  } else {
    __ LoadFixedArrayElementWithoutDecompressing(result_reg, elements, index);
  }
}

void LoadFixedDoubleArrayElement::SetValueLocationConstraints() {
  UseRegister(elements_input());
  UseRegister(index_input());
  DefineAsRegister(this);
}
void LoadFixedDoubleArrayElement::GenerateCode(MaglevAssembler* masm,
                                               const ProcessingState& state) {
  Register elements = ToRegister(elements_input());
  Register index = ToRegister(index_input());
  DoubleRegister result_reg = ToDoubleRegister(result());
  __ LoadFixedDoubleArrayElement(result_reg, elements, index);
}

void LoadHoleyFixedDoubleArrayElement::SetValueLocationConstraints() {
  UseRegister(elements_input());
  UseRegister(index_input());
  DefineAsRegister(this);
}
void LoadHoleyFixedDoubleArrayElement::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  Register elements = ToRegister(elements_input());
  Register index = ToRegister(index_input());
  DoubleRegister result_reg = ToDoubleRegister(result());
  __ LoadFixedDoubleArrayElement(result_reg, elements, index);
}

void LoadHoleyFixedDoubleArrayElementCheckedNotHole::
    SetValueLocationConstraints() {
  UseRegister(elements_input());
  UseRegister(index_input());
  DefineAsRegister(this);
  set_temporaries_needed(1);
}
void LoadHoleyFixedDoubleArrayElementCheckedNotHole::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register elements = ToRegister(elements_input());
  Register index = ToRegister(index_input());
  DoubleRegister result_reg = ToDoubleRegister(result());
  __ LoadFixedDoubleArrayElement(result_reg, elements, index);
  __ JumpIfHoleNan(result_reg, temps.Acquire(),
                   __ GetDeoptLabel(this, DeoptimizeReason::kHole));
}

#ifdef V8_ENABLE_EXPERIMENTAL_UNDEFINED_DOUBLE
void LoadHoleyFixedDoubleArrayElementCheckedNotUndefinedOrHole::
    SetValueLocationConstraints() {
  UseRegister(elements_input());
  UseRegister(index_input());
  DefineAsRegister(this);
  set_temporaries_needed(1);
}
void LoadHoleyFixedDoubleArrayElementCheckedNotUndefinedOrHole::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register elements = ToRegister(elements_input());
  Register index = ToRegister(index_input());
  DoubleRegister result_reg = ToDoubleRegister(result());
  __ LoadFixedDoubleArrayElement(result_reg, elements, index);
  // TODO(nicohartmann): Should have a combined JumpIfUndefinedOrHoleNan.
  Register scratch = temps.Acquire();
  Label* deopt_label = __ GetDeoptLabel(this, DeoptimizeReason::kHole);
  __ JumpIfUndefinedNan(result_reg, scratch, deopt_label);
  __ JumpIfHoleNan(result_reg, scratch, deopt_label);
}
#endif  // V8_ENABLE_EXPERIMENTAL_UNDEFINED_DOUBLE

void StoreFixedDoubleArrayElement::SetValueLocationConstraints() {
  UseRegister(elements_input());
  UseRegister(index_input());
  UseRegister(value_input());
}
void StoreFixedDoubleArrayElement::GenerateCode(MaglevAssembler* masm,
                                                const ProcessingState& state) {
  Register elements = ToRegister(elements_input());
  Register index = ToRegister(index_input());
  DoubleRegister value = ToDoubleRegister(value_input());
  if (v8_flags.debug_code) {
    __ AssertObjectType(elements, FIXED_DOUBLE_ARRAY_TYPE,
                        AbortReason::kUnexpectedValue);
    __ CompareInt32AndAssert(index, 0, kUnsignedGreaterThanEqual,
                             AbortReason::kUnexpectedNegativeValue);
  }
  __ StoreFixedDoubleArrayElement(elements, index, value);
}

int StoreMap::MaxCallStackArgs() const {
  return WriteBarrierDescriptor::GetStackParameterCount();
}
void StoreMap::SetValueLocationConstraints() {
  UseFixed(object_input(), WriteBarrierDescriptor::ObjectRegister());
  set_temporaries_needed(1);
}
void StoreMap::GenerateCode(MaglevAssembler* masm,
                            const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  // TODO(leszeks): Consider making this an arbitrary register and push/popping
  // in the deferred path.
  Register object = WriteBarrierDescriptor::ObjectRegister();
  DCHECK_EQ(object, ToRegister(object_input()));
  Register value = temps.Acquire();
  __ MoveTagged(value, map_.object());

  switch (kind()) {
    case Kind::kInlinedAllocation: {
      DCHECK(object_input().node()->Cast<InlinedAllocation>());
      auto inlined = object_input().node()->Cast<InlinedAllocation>();
      if (inlined->allocation_block()->allocation_type() ==
          AllocationType::kYoung) {
        __ StoreTaggedFieldNoWriteBarrier(object, HeapObject::kMapOffset,
                                          value);
        __ AssertElidedWriteBarrier(object, value, register_snapshot());
        break;
      }
      [[fallthrough]];
    }
    case Kind::kInitializing:
    case Kind::kTransitioning:
      __ StoreTaggedFieldWithWriteBarrier(object, HeapObject::kMapOffset, value,
                                          register_snapshot(),
                                          MaglevAssembler::kValueIsCompressed,
                                          MaglevAssembler::kValueCannotBeSmi);
      break;
  }
}

int StoreTaggedFieldWithWriteBarrier::MaxCallStackArgs() const {
  return WriteBarrierDescriptor::GetStackParameterCount();
}
void StoreTaggedFieldWithWriteBarrier::SetValueLocationConstraints() {
  UseFixed(object_input(), WriteBarrierDescriptor::ObjectRegister());
  UseRegister(value_input());
}
void StoreTaggedFieldWithWriteBarrier::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  // TODO(leszeks): Consider making this an arbitrary register and push/popping
  // in the deferred path.
  Register object = WriteBarrierDescriptor::ObjectRegister();
  DCHECK_EQ(object, ToRegister(object_input()));
  Register value = ToRegister(value_input());

  __ StoreTaggedFieldWithWriteBarrier(
      object, offset(), value, register_snapshot(),
      value_input().node()->decompresses_tagged_result()
          ? MaglevAssembler::kValueIsDecompressed
          : MaglevAssembler::kValueIsCompressed,
      MaglevAssembler::kValueCanBeSmi);
}

int StoreTrustedPointerFieldWithWriteBarrier::MaxCallStackArgs() const {
  return WriteBarrierDescriptor::GetStackParameterCount();
}
void StoreTrustedPointerFieldWithWriteBarrier::SetValueLocationConstraints() {
  UseFixed(object_input(), WriteBarrierDescriptor::ObjectRegister());
  UseRegister(value_input());
}
void StoreTrustedPointerFieldWithWriteBarrier::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
#ifdef V8_ENABLE_SANDBOX
  // TODO(leszeks): Consider making this an arbitrary register and push/popping
  // in the deferred path.
  Register object = WriteBarrierDescriptor::ObjectRegister();
  DCHECK_EQ(object, ToRegister(object_input()));
  Register value = ToRegister(value_input());
  __ StoreTrustedPointerFieldWithWriteBarrier(object, offset(), value,
                                              register_snapshot(), tag());
#else
  UNREACHABLE();
#endif
}

void LoadSignedIntDataViewElement::SetValueLocationConstraints() {
  UseRegister(object_input());
  UseRegister(index_input());
  if (is_little_endian_constant() ||
      type_ == ExternalArrayType::kExternalInt8Array) {
    UseAny(is_little_endian_input());
  } else {
    UseRegister(is_little_endian_input());
  }
  DefineAsRegister(this);
  set_temporaries_needed(1);
}
void LoadSignedIntDataViewElement::GenerateCode(MaglevAssembler* masm,
                                                const ProcessingState& state) {
  Register object = ToRegister(object_input());
  Register index = ToRegister(index_input());
  Register result_reg = ToRegister(result());

  if (v8_flags.debug_code) {
    __ AssertObjectTypeInRange(object,
                               FIRST_JS_DATA_VIEW_OR_RAB_GSAB_DATA_VIEW_TYPE,
                               LAST_JS_DATA_VIEW_OR_RAB_GSAB_DATA_VIEW_TYPE,
                               AbortReason::kUnexpectedValue);
  }

  int element_size = compiler::ExternalArrayElementSize(type_);

  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register data_pointer = temps.Acquire();

  // We need to make sure we don't clobber is_little_endian_input by writing to
  // the result register.
  Register reg_with_result = result_reg;
  if (type_ != ExternalArrayType::kExternalInt8Array &&
      !is_little_endian_constant() &&
      result_reg == ToRegister(is_little_endian_input())) {
    reg_with_result = data_pointer;
  }

  // Load data pointer.
  __ LoadExternalPointerField(
      data_pointer, FieldMemOperand(object, JSDataView::kDataPointerOffset));
  MemOperand element_address = __ DataViewElementOperand(data_pointer, index);
  __ LoadSignedField(reg_with_result, element_address, element_size);

  // We ignore little endian argument if type is a byte size.
  if (type_ != ExternalArrayType::kExternalInt8Array) {
    if (is_little_endian_constant()) {
      if (FromConstantToBool(masm, is_little_endian_input().node()) ==
          V8_TARGET_BIG_ENDIAN_BOOL) {
        DCHECK_EQ(reg_with_result, result_reg);
        __ ReverseByteOrder(result_reg, element_size);
      }
    } else {
      ZoneLabelRef keep_byte_order(masm), reverse_byte_order(masm);
      DCHECK_NE(reg_with_result, ToRegister(is_little_endian_input()));
      __ ToBoolean(
          ToRegister(is_little_endian_input()), CheckType::kCheckHeapObject,
          V8_TARGET_BIG_ENDIAN_BOOL ? reverse_byte_order : keep_byte_order,
          V8_TARGET_BIG_ENDIAN_BOOL ? keep_byte_order : reverse_byte_order,
          false);
      __ bind(*reverse_byte_order);
      __ ReverseByteOrder(reg_with_result, element_size);
      __ bind(*keep_byte_order);
      if (reg_with_result != result_reg) {
        __ Move(result_reg, reg_with_result);
      }
    }
  }
}

void StoreSignedIntDataViewElement::SetValueLocationConstraints() {
  UseRegister(object_input());
  UseRegister(index_input());
  if (compiler::ExternalArrayElementSize(type_) > 1) {
    UseAndClobberRegister(value_input());
  } else {
    UseRegister(value_input());
  }
  if (is_little_endian_constant() ||
      type_ == ExternalArrayType::kExternalInt8Array) {
    UseAny(is_little_endian_input());
  } else {
    UseRegister(is_little_endian_input());
  }
  set_temporaries_needed(1);
}
void StoreSignedIntDataViewElement::GenerateCode(MaglevAssembler* masm,
                                                 const ProcessingState& state) {
  Register object = ToRegister(object_input());
  Register index = ToRegister(index_input());
  Register value = ToRegister(value_input());

  if (v8_flags.debug_code) {
    __ AssertObjectTypeInRange(object,
                               FIRST_JS_DATA_VIEW_OR_RAB_GSAB_DATA_VIEW_TYPE,
                               LAST_JS_DATA_VIEW_OR_RAB_GSAB_DATA_VIEW_TYPE,
                               AbortReason::kUnexpectedValue);
  }

  int element_size = compiler::ExternalArrayElementSize(type_);

  // We ignore little endian argument if type is a byte size.
  if (element_size > 1) {
    if (is_little_endian_constant()) {
      if (FromConstantToBool(masm, is_little_endian_input().node()) ==
          V8_TARGET_BIG_ENDIAN_BOOL) {
        __ ReverseByteOrder(value, element_size);
      }
    } else {
      ZoneLabelRef keep_byte_order(masm), reverse_byte_order(masm);
      __ ToBoolean(
          ToRegister(is_little_endian_input()), CheckType::kCheckHeapObject,
          V8_TARGET_BIG_ENDIAN_BOOL ? reverse_byte_order : keep_byte_order,
          V8_TARGET_BIG_ENDIAN_BOOL ? keep_byte_order : reverse_byte_order,
          false);
      __ bind(*reverse_byte_order);
      __ ReverseByteOrder(value, element_size);
      __ bind(*keep_byte_order);
    }
  }

  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register data_pointer = temps.Acquire();
  __ LoadExternalPointerField(
      data_pointer, FieldMemOperand(object, JSDataView::kDataPointerOffset));
  MemOperand element_address = __ DataViewElementOperand(data_pointer, index);
  __ StoreField(element_address, value, element_size);
}

void LoadDoubleDataViewElement::SetValueLocationConstraints() {
  UseRegister(object_input());
  UseRegister(index_input());
  if (is_little_endian_constant()) {
    UseAny(is_little_endian_input());
  } else {
    UseRegister(is_little_endian_input());
  }
  set_temporaries_needed(1);
  DefineAsRegister(this);
}
void LoadDoubleDataViewElement::GenerateCode(MaglevAssembler* masm,
                                             const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register object = ToRegister(object_input());
  Register index = ToRegister(index_input());
  DoubleRegister result_reg = ToDoubleRegister(result());
  Register data_pointer = temps.Acquire();

  if (v8_flags.debug_code) {
    __ AssertObjectTypeInRange(object,
                               FIRST_JS_DATA_VIEW_OR_RAB_GSAB_DATA_VIEW_TYPE,
                               LAST_JS_DATA_VIEW_OR_RAB_GSAB_DATA_VIEW_TYPE,
                               AbortReason::kUnexpectedValue);
  }

  // Load data pointer.
  __ LoadExternalPointerField(
      data_pointer, FieldMemOperand(object, JSDataView::kDataPointerOffset));

  if (is_little_endian_constant()) {
    if (FromConstantToBool(masm, is_little_endian_input().node()) !=
        V8_TARGET_BIG_ENDIAN_BOOL) {
      __ LoadUnalignedFloat64(result_reg, data_pointer, index);
    } else {
      __ LoadUnalignedFloat64AndReverseByteOrder(result_reg, data_pointer,
                                                 index);
    }
  } else {
    Label done;
    ZoneLabelRef keep_byte_order(masm), reverse_byte_order(masm);
    // TODO(leszeks): We're likely to be calling this on an existing boolean --
    // maybe that's a case we should fast-path here and reuse that boolean
    // value?
    __ ToBoolean(
        ToRegister(is_little_endian_input()), CheckType::kCheckHeapObject,
        V8_TARGET_BIG_ENDIAN_BOOL ? reverse_byte_order : keep_byte_order,
        V8_TARGET_BIG_ENDIAN_BOOL ? keep_byte_order : reverse_byte_order, true);
    __ bind(*keep_byte_order);
    __ LoadUnalignedFloat64(result_reg, data_pointer, index);
    __ Jump(&done);
    // We should swap the bytes if big endian.
    __ bind(*reverse_byte_order);
    __ LoadUnalignedFloat64AndReverseByteOrder(result_reg, data_pointer, index);
    __ bind(&done);
  }
}

void StoreDoubleDataViewElement::SetValueLocationConstraints() {
  UseRegister(object_input());
  UseRegister(index_input());
  UseRegister(value_input());
  if (is_little_endian_constant()) {
    UseAny(is_little_endian_input());
  } else {
    UseRegister(is_little_endian_input());
  }
  set_temporaries_needed(1);
}
void StoreDoubleDataViewElement::GenerateCode(MaglevAssembler* masm,
                                              const ProcessingState& state) {
  Register object = ToRegister(object_input());
  Register index = ToRegister(index_input());
  DoubleRegister value = ToDoubleRegister(value_input());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register data_pointer = temps.Acquire();

  if (v8_flags.debug_code) {
    __ AssertObjectTypeInRange(object,
                               FIRST_JS_DATA_VIEW_OR_RAB_GSAB_DATA_VIEW_TYPE,
                               LAST_JS_DATA_VIEW_OR_RAB_GSAB_DATA_VIEW_TYPE,
                               AbortReason::kUnexpectedValue);
  }

  // Load data pointer.
  __ LoadExternalPointerField(
      data_pointer, FieldMemOperand(object, JSDataView::kDataPointerOffset));

  if (is_little_endian_constant()) {
    if (FromConstantToBool(masm, is_little_endian_input().node()) !=
        V8_TARGET_BIG_ENDIAN_BOOL) {
      __ StoreUnalignedFloat64(data_pointer, index, value);
    } else {
      __ ReverseByteOrderAndStoreUnalignedFloat64(data_pointer, index, value);
    }
  } else {
    Label done;
    ZoneLabelRef keep_byte_order(masm), reverse_byte_order(masm);
    // TODO(leszeks): We're likely to be calling this on an existing boolean --
    // maybe that's a case we should fast-path here and reuse that boolean
    // value?
    __ ToBoolean(
        ToRegister(is_little_endian_input()), CheckType::kCheckHeapObject,
        V8_TARGET_BIG_ENDIAN_BOOL ? reverse_byte_order : keep_byte_order,
        V8_TARGET_BIG_ENDIAN_BOOL ? keep_byte_order : reverse_byte_order, true);
    __ bind(*keep_byte_order);
    __ StoreUnalignedFloat64(data_pointer, index, value);
    __ Jump(&done);
    // We should swap the bytes if big endian.
    __ bind(*reverse_byte_order);
    __ ReverseByteOrderAndStoreUnalignedFloat64(data_pointer, index, value);
    __ bind(&done);
  }
}


void LoadEnumCacheLength::SetValueLocationConstraints() {
  UseRegister(map_input());
  DefineAsRegister(this);
}
void LoadEnumCacheLength::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {
  Register map = ToRegister(map_input());
  Register result_reg = ToRegister(result());
  __ AssertMap(map);
  __ LoadBitField<Map::Bits3::EnumLengthBits>(
      result_reg, FieldMemOperand(map, Map::kBitField3Offset));
}

int LoadGlobal::MaxCallStackArgs() const {
  if (typeof_mode() == TypeofMode::kNotInside) {
    using D = CallInterfaceDescriptorFor<Builtin::kLoadGlobalIC>::type;
    return D::GetStackParameterCount();
  } else {
    using D =
        CallInterfaceDescriptorFor<Builtin::kLoadGlobalICInsideTypeof>::type;
    return D::GetStackParameterCount();
  }
}
void LoadGlobal::SetValueLocationConstraints() {
  UseFixed(context(), kContextRegister);
  DefineAsFixed(this, kReturnRegister0);
}
void LoadGlobal::GenerateCode(MaglevAssembler* masm,
                              const ProcessingState& state) {
  if (typeof_mode() == TypeofMode::kNotInside) {
    __ CallBuiltin<Builtin::kLoadGlobalIC>(
        context(),                                    // context
        name().object(),                              // name
        TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
        feedback().vector                             // feedback vector
    );
  } else {
    DCHECK_EQ(typeof_mode(), TypeofMode::kInside);
    __ CallBuiltin<Builtin::kLoadGlobalICInsideTypeof>(
        context(),                                    // context
        name().object(),                              // name
        TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
        feedback().vector                             // feedback vector
    );
  }

  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int StoreGlobal::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kStoreGlobalIC>::type;
  return D::GetStackParameterCount();
}
void StoreGlobal::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kStoreGlobalIC>::type;
  UseFixed(context(), kContextRegister);
  UseFixed(value(), D::GetRegisterParameter(D::kValue));
  DefineAsFixed(this, kReturnRegister0);
}
void StoreGlobal::GenerateCode(MaglevAssembler* masm,
                               const ProcessingState& state) {
  __ CallBuiltin<Builtin::kStoreGlobalIC>(
      context(),                                    // context
      name().object(),                              // name
      value(),                                      // value
      TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
      feedback().vector                             // feedback vector
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

void CheckValue::SetValueLocationConstraints() { UseRegister(target_input()); }
void CheckValue::GenerateCode(MaglevAssembler* masm,
                              const ProcessingState& state) {
  Register target = ToRegister(target_input());
  Label* fail = __ GetDeoptLabel(this, deoptimize_reason());
  __ CompareTaggedAndJumpIf(target, value().object(), kNotEqual, fail);
}

void CheckValueEqualsInt32::SetValueLocationConstraints() {
  UseRegister(target_input());
}
void CheckValueEqualsInt32::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  Register target = ToRegister(target_input());
  Label* fail = __ GetDeoptLabel(this, deoptimize_reason());
  __ CompareInt32AndJumpIf(target, value(), kNotEqual, fail);
}

void CheckValueEqualsString::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kStringEqual>::type;
  UseFixed(target_input(), D::GetRegisterParameter(D::kLeft));
  RequireSpecificTemporary(D::GetRegisterParameter(D::kLength));
}
void CheckValueEqualsString::GenerateCode(MaglevAssembler* masm,
                                          const ProcessingState& state) {
  using D = CallInterfaceDescriptorFor<Builtin::kStringEqual>::type;

  ZoneLabelRef end(masm);
  DCHECK_EQ(D::GetRegisterParameter(D::kLeft), ToRegister(target_input()));
  Register target = D::GetRegisterParameter(D::kLeft);
  // Maybe the string is internalized already, do a fast reference check first.
  __ CompareTaggedAndJumpIf(target, value().object(), kEqual, *end,
                            Label::kNear);

  __ EmitEagerDeoptIfSmi(this, target, deoptimize_reason());
  __ JumpIfString(
      target,
      __ MakeDeferredCode(
          [](MaglevAssembler* masm, CheckValueEqualsString* node,
             ZoneLabelRef end, DeoptimizeReason deoptimize_reason) {
            Register target = D::GetRegisterParameter(D::kLeft);
            Register string_length = D::GetRegisterParameter(D::kLength);
            __ StringLength(string_length, target);
            Label* fail = __ GetDeoptLabel(node, deoptimize_reason);
            __ CompareInt32AndJumpIf(string_length, node->value().length(),
                                     kNotEqual, fail);
            RegisterSnapshot snapshot = node->register_snapshot();
            {
              SaveRegisterStateForCall save_register_state(masm, snapshot);
              __ CallBuiltin<Builtin::kStringEqual>(
                  node->target_input(),    // left
                  node->value().object(),  // right
                  string_length            // length
              );
              save_register_state.DefineSafepoint();
              // Compare before restoring registers, so that the deopt below has
              // the correct register set.
              __ CompareRoot(kReturnRegister0, RootIndex::kTrueValue);
            }
            __ EmitEagerDeoptIf(kNotEqual, deoptimize_reason, node);
            __ Jump(*end);
          },
          this, end, deoptimize_reason()));

  __ EmitEagerDeopt(this, deoptimize_reason());

  __ bind(*end);
}

void CheckDynamicValue::SetValueLocationConstraints() {
  UseRegister(first_input());
  UseRegister(second_input());
}
void CheckDynamicValue::GenerateCode(MaglevAssembler* masm,
                                     const ProcessingState& state) {
  Register first = ToRegister(first_input());
  Register second = ToRegister(second_input());
  Label* fail = __ GetDeoptLabel(this, deoptimize_reason());
  __ CompareTaggedAndJumpIf(first, second, kNotEqual, fail);
}

void CheckSmi::SetValueLocationConstraints() { UseRegister(receiver_input()); }
void CheckSmi::GenerateCode(MaglevAssembler* masm,
                            const ProcessingState& state) {
  Register object = ToRegister(receiver_input());
  __ EmitEagerDeoptIfNotSmi(this, object, DeoptimizeReason::kNotASmi);
}

void CheckHeapObject::SetValueLocationConstraints() {
  UseRegister(receiver_input());
}
void CheckHeapObject::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {
  Register object = ToRegister(receiver_input());
  __ EmitEagerDeoptIfSmi(this, object, DeoptimizeReason::kSmi);
}

void CheckSymbol::SetValueLocationConstraints() {
  UseRegister(receiver_input());
}
void CheckSymbol::GenerateCode(MaglevAssembler* masm,
                               const ProcessingState& state) {
  Register object = ToRegister(receiver_input());
  if (check_type() == CheckType::kOmitHeapObjectCheck) {
    __ AssertNotSmi(object);
  } else {
    __ EmitEagerDeoptIfSmi(this, object, DeoptimizeReason::kNotASymbol);
  }
  __ JumpIfNotObjectType(object, SYMBOL_TYPE,
                         __ GetDeoptLabel(this, DeoptimizeReason::kNotASymbol));
}

void CheckInstanceType::SetValueLocationConstraints() {
  UseRegister(receiver_input());
}
void CheckInstanceType::GenerateCode(MaglevAssembler* masm,
                                     const ProcessingState& state) {
  Register object = ToRegister(receiver_input());
  if (check_type() == CheckType::kOmitHeapObjectCheck) {
    __ AssertNotSmi(object);
  } else {
    __ EmitEagerDeoptIfSmi(this, object, DeoptimizeReason::kWrongInstanceType);
  }
  if (first_instance_type_ == last_instance_type_) {
    __ JumpIfNotObjectType(
        object, first_instance_type_,
        __ GetDeoptLabel(this, DeoptimizeReason::kWrongInstanceType));
  } else {
    __ JumpIfObjectTypeNotInRange(
        object, first_instance_type_, last_instance_type_,
        __ GetDeoptLabel(this, DeoptimizeReason::kWrongInstanceType));
  }
}

void CheckCacheIndicesNotCleared::SetValueLocationConstraints() {
  UseRegister(indices_input());
  UseRegister(length_input());
}
void CheckCacheIndicesNotCleared::GenerateCode(MaglevAssembler* masm,
                                               const ProcessingState& state) {
  Register indices = ToRegister(indices_input());
  Register length = ToRegister(length_input());
  __ AssertNotSmi(indices);

  if (v8_flags.debug_code) {
    __ AssertObjectType(indices, FIXED_ARRAY_TYPE,
                        AbortReason::kOperandIsNotAFixedArray);
  }
  Label done;
  // If the cache length is zero, we don't have any indices, so we know this is
  // ok even though the indices are the empty array.
  __ CompareInt32AndJumpIf(length, 0, kEqual, &done);
  // Otherwise, an empty array with non-zero required length is not valid.
  __ JumpIfRoot(indices, RootIndex::kEmptyFixedArray,
                __ GetDeoptLabel(this, DeoptimizeReason::kWrongEnumIndices));
  __ bind(&done);
}

void CheckTypedArrayBounds::SetValueLocationConstraints() {
  UseRegister(index_input());
  if (length_input().node()->Is<IntPtrConstant>()) {
    UseAny(length_input());
    set_temporaries_needed(1);
  } else {
    UseRegister(length_input());
  }
}
void CheckTypedArrayBounds::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register index = ToRegister(index_input());
  Register length = Register::no_reg();
  if (auto length_constant = length_input().node()->TryCast<IntPtrConstant>()) {
    length = temps.Acquire();
    __ Move(length, length_constant->value());
  } else {
    length = ToRegister(length_input());
  }
  // This also handles negative indices.
  __ SignExtend32To64Bits(index, index);
  __ CompareIntPtrAndJumpIf(
      index, length, kUnsignedGreaterThanEqual,
      __ GetDeoptLabel(this, DeoptimizeReason::kOutOfBounds));
}

void CheckInt32Condition::SetValueLocationConstraints() {
  UseRegister(left_input());
  UseRegister(right_input());
}
void CheckInt32Condition::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {
  Label* fail = __ GetDeoptLabel(this, deoptimize_reason());
  __ CompareInt32AndJumpIf(ToRegister(left_input()), ToRegister(right_input()),
                           NegateCondition(ToCondition(condition())), fail);
}

int StoreContextSlotWithWriteBarrier::MaxCallStackArgs() const {
  return WriteBarrierDescriptor::GetStackParameterCount();
}

void StoreContextSlotWithWriteBarrier::SetValueLocationConstraints() {
  UseFixed(context_input(), WriteBarrierDescriptor::ObjectRegister());
  UseRegister(new_value_input());
  set_temporaries_needed(2);
  set_double_temporaries_needed(1);
}

void StoreContextSlotWithWriteBarrier::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  using D = CallInterfaceDescriptorFor<Builtin::kDetachContextCell>::type;
  __ RecordComment("StoreContextSlotWithWriteBarrier");
  ZoneLabelRef do_normal_store(masm);
  ZoneLabelRef done(masm);
  // TODO(leszeks): Consider making this an arbitrary register and push/popping
  // in the deferred path.
  Register context = WriteBarrierDescriptor::ObjectRegister();
  Register new_value = ToRegister(new_value_input());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  Register old_value = temps.Acquire();
  __ LoadTaggedField(old_value, context, offset());

  __ JumpIfSmi(old_value, *do_normal_store);
  __ CompareMapWithRoot(old_value, RootIndex::kContextCellMap, scratch);
  __ JumpToDeferredIf(
      kEqual,
      [](MaglevAssembler* masm, Register context, Register old_value,
         Register new_value, Register scratch,
         StoreContextSlotWithWriteBarrier* node, ZoneLabelRef do_normal_store,
         ZoneLabelRef done) {
        __ JumpIfRoot(old_value, RootIndex::kUndefinedContextCell,
                      *do_normal_store);
        {
          // Since we compiled without context specialization, we detach the
          // context cells.
          RegisterSnapshot snapshot = node->register_snapshot();
          SaveRegisterStateForCall save_register_state(masm, snapshot);
          if (new_value == D::GetRegisterParameter(D::kTheContext)) {
            // Be sure not to clobber new_value.
            if (context == D::GetRegisterParameter(D::kNewValue)) {
              __ Move(scratch, new_value);
              new_value = scratch;
            } else {
              __ Move(D::GetRegisterParameter(D::kNewValue), new_value);
              new_value = D::GetRegisterParameter(D::kNewValue);
            }
          }
          __ CallBuiltin<Builtin::kDetachContextCell>(context, new_value,
                                                      node->index());
          save_register_state.DefineSafepointWithLazyDeopt(
              node->lazy_deopt_info());
        }
        __ Jump(*done);
      },
      context, old_value, new_value, scratch, this, do_normal_store, done);

  __ bind(*do_normal_store);
  __ StoreTaggedFieldWithWriteBarrier(
      context, offset(), new_value, register_snapshot(),
      new_value_input().node()->decompresses_tagged_result()
          ? MaglevAssembler::kValueIsDecompressed
          : MaglevAssembler::kValueIsCompressed,
      MaglevAssembler::kValueCanBeSmi);
  __ bind(*done);
}

void CheckString::SetValueLocationConstraints() {
  UseRegister(receiver_input());
}
void CheckString::GenerateCode(MaglevAssembler* masm,
                               const ProcessingState& state) {
  Register object = ToRegister(receiver_input());
  if (check_type() == CheckType::kOmitHeapObjectCheck) {
    __ AssertNotSmi(object);
  } else {
    __ EmitEagerDeoptIfSmi(this, object, DeoptimizeReason::kNotAString);
  }
  __ JumpIfNotString(object,
                     __ GetDeoptLabel(this, DeoptimizeReason::kNotAString));
}

void CheckSeqOneByteString::SetValueLocationConstraints() {
  UseRegister(receiver_input());
}
void CheckSeqOneByteString::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  Register object = ToRegister(receiver_input());
  if (check_type() == CheckType::kOmitHeapObjectCheck) {
    __ AssertNotSmi(object);
  } else {
    __ EmitEagerDeoptIfSmi(this, object,
                           DeoptimizeReason::kNotASeqOneByteString);
  }
  __ JumpIfNotSeqOneByteString(
      object, __ GetDeoptLabel(this, DeoptimizeReason::kNotASeqOneByteString));
}

void CheckStringOrStringWrapper::SetValueLocationConstraints() {
  UseRegister(receiver_input());
  set_temporaries_needed(1);
}

void CheckStringOrStringWrapper::GenerateCode(MaglevAssembler* masm,
                                              const ProcessingState& state) {
  Register object = ToRegister(receiver_input());

  if (check_type() == CheckType::kOmitHeapObjectCheck) {
    __ AssertNotSmi(object);
  } else {
    __ EmitEagerDeoptIfSmi(this, object,
                           DeoptimizeReason::kNotAStringOrStringWrapper);
  }

  auto deopt =
      __ GetDeoptLabel(this, DeoptimizeReason::kNotAStringOrStringWrapper);
  Label done;

  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();

  __ JumpIfString(object, &done);
  __ JumpIfNotObjectType(object, InstanceType::JS_PRIMITIVE_WRAPPER_TYPE,
                         deopt);
  __ LoadMap(scratch, object);
  __ LoadBitField<Map::Bits2::ElementsKindBits>(
      scratch, FieldMemOperand(scratch, Map::kBitField2Offset));
  static_assert(FAST_STRING_WRAPPER_ELEMENTS + 1 ==
                SLOW_STRING_WRAPPER_ELEMENTS);
  __ CompareInt32AndJumpIf(scratch, FAST_STRING_WRAPPER_ELEMENTS, kLessThan,
                           deopt);
  __ CompareInt32AndJumpIf(scratch, SLOW_STRING_WRAPPER_ELEMENTS, kGreaterThan,
                           deopt);
  __ Jump(&done);
  __ bind(&done);
}

void CheckStringOrOddball::SetValueLocationConstraints() {
  UseRegister(receiver_input());
}
void CheckStringOrOddball::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  Register object = ToRegister(receiver_input());
  if (check_type() == CheckType::kOmitHeapObjectCheck) {
    __ AssertNotSmi(object);
  } else {
    __ EmitEagerDeoptIfSmi(this, object,
                           DeoptimizeReason::kNotAStringOrOddball);
  }

  Label done;
  __ JumpIfObjectType(object, InstanceType::ODDBALL_TYPE, &done);
  __ JumpIfNotString(
      object, __ GetDeoptLabel(this, DeoptimizeReason::kNotAStringOrOddball));

  __ bind(&done);
}

void CheckDetectableCallable::SetValueLocationConstraints() {
  UseRegister(receiver_input());
  set_temporaries_needed(1);
}

void CheckDetectableCallable::GenerateCode(MaglevAssembler* masm,
                                           const ProcessingState& state) {
  Register object = ToRegister(receiver_input());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  auto deopt = __ GetDeoptLabel(this, DeoptimizeReason::kNotDetectableReceiver);
  __ JumpIfNotCallable(object, scratch, check_type(), deopt);
  __ JumpIfUndetectable(object, scratch, CheckType::kOmitHeapObjectCheck,
                        deopt);
}

void CheckJSReceiverOrNullOrUndefined::SetValueLocationConstraints() {
  UseRegister(object_input());
  set_temporaries_needed(1);
}
void CheckJSReceiverOrNullOrUndefined::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  Register object = ToRegister(object_input());

  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();

  if (check_type() == CheckType::kOmitHeapObjectCheck) {
    __ AssertNotSmi(object);
  } else {
    __ EmitEagerDeoptIfSmi(
        this, object, DeoptimizeReason::kNotAJavaScriptObjectOrNullOrUndefined);
  }

  Label done;
  // Undetectable (document.all) is a JSReceiverOrNullOrUndefined. We already
  // checked for Smis above, so no check needed here.
  __ JumpIfUndetectable(object, scratch, CheckType::kOmitHeapObjectCheck,
                        &done);
  __ JumpIfObjectTypeNotInRange(
      object, FIRST_JS_RECEIVER_TYPE, LAST_JS_RECEIVER_TYPE,
      __ GetDeoptLabel(
          this, DeoptimizeReason::kNotAJavaScriptObjectOrNullOrUndefined));
  __ Jump(&done);
  __ bind(&done);
}

void CheckNotHole::SetValueLocationConstraints() {
  UseRegister(object_input());
}
void CheckNotHole::GenerateCode(MaglevAssembler* masm,
                                const ProcessingState& state) {
  __ CompareRootAndEmitEagerDeoptIf(ToRegister(object_input()),
                                    RootIndex::kTheHoleValue, kEqual,
                                    DeoptimizeReason::kHole, this);
}

void CheckHoleyFloat64NotHole::SetValueLocationConstraints() {
  UseRegister(float64_input());
  set_temporaries_needed(1);
}
void CheckHoleyFloat64NotHole::GenerateCode(MaglevAssembler* masm,
                                            const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.AcquireScratch();
  __ JumpIfHoleNan(ToDoubleRegister(float64_input()), scratch,
                   __ GetDeoptLabel(this, DeoptimizeReason::kHole),
                   Label::kFar);
}

void ConvertHoleToUndefined::SetValueLocationConstraints() {
  UseRegister(object_input());
  DefineSameAsFirst(this);
}
void ConvertHoleToUndefined::GenerateCode(MaglevAssembler* masm,
                                          const ProcessingState& state) {
  Label done;
  DCHECK_EQ(ToRegister(object_input()), ToRegister(result()));
  __ JumpIfNotRoot(ToRegister(object_input()), RootIndex::kTheHoleValue, &done);
  __ LoadRoot(ToRegister(result()), RootIndex::kUndefinedValue);
  __ bind(&done);
}

int ConvertReceiver::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kToObject>::type;
  return D::GetStackParameterCount();
}
void ConvertReceiver::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kToObject>::type;
  static_assert(D::GetRegisterParameter(D::kInput) == kReturnRegister0);
  UseFixed(receiver_input(), D::GetRegisterParameter(D::kInput));
  DefineAsFixed(this, kReturnRegister0);
}
void ConvertReceiver::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {
  Label convert_to_object, done;
  Register receiver = ToRegister(receiver_input());
  __ JumpIfSmi(
      receiver, &convert_to_object,
      v8_flags.debug_code ? Label::Distance::kFar : Label::Distance::kNear);

  // If {receiver} is not primitive, no need to move it to {result}, since
  // they share the same register.
  DCHECK_EQ(receiver, ToRegister(result()));
  __ JumpIfJSAnyIsNotPrimitive(receiver, &done);

  compiler::JSHeapBroker* broker = masm->compilation_info()->broker();
  if (mode_ != ConvertReceiverMode::kNotNullOrUndefined) {
    Label convert_global_proxy;
    __ JumpIfRoot(receiver, RootIndex::kUndefinedValue, &convert_global_proxy,
                  Label::Distance::kNear);
    __ JumpIfNotRoot(
        receiver, RootIndex::kNullValue, &convert_to_object,
        v8_flags.debug_code ? Label::Distance::kFar : Label::Distance::kNear);
    __ bind(&convert_global_proxy);
    // Patch receiver to global proxy.
    __ Move(ToRegister(result()),
            native_context_.global_proxy_object(broker).object());
    __ Jump(&done);
  }

  __ bind(&convert_to_object);
  __ CallBuiltin<Builtin::kToObject>(native_context_.object(),
                                     receiver_input());
  __ bind(&done);
}

int CheckDerivedConstructResult::MaxCallStackArgs() const { return 0; }
void CheckDerivedConstructResult::SetValueLocationConstraints() {
  UseRegister(construct_result_input());
  DefineSameAsFirst(this);
}
void CheckDerivedConstructResult::GenerateCode(MaglevAssembler* masm,
                                               const ProcessingState& state) {
  Register construct_result = ToRegister(construct_result_input());

  DCHECK_EQ(construct_result, ToRegister(result()));

  // If the result is an object (in the ECMA sense), we should get rid
  // of the receiver and use the result; see ECMA-262 section 13.2.2-7
  // on page 74.
  Label done, do_throw;

  __ CompareRoot(construct_result, RootIndex::kUndefinedValue);
  __ Assert(kNotEqual, AbortReason::kUnexpectedValue);

  // If the result is a smi, it is *not* an object in the ECMA sense.
  __ JumpIfSmi(construct_result, &do_throw, Label::Distance::kNear);

  // Check if the type of the result is not an object in the ECMA sense.
  __ JumpIfJSAnyIsNotPrimitive(construct_result, &done, Label::Distance::kNear);

  // Throw away the result of the constructor invocation and use the
  // implicit receiver as the result.
  __ bind(&do_throw);
  __ Jump(__ MakeDeferredCode(
      [](MaglevAssembler* masm, CheckDerivedConstructResult* node) {
        __ Move(kContextRegister, masm->native_context().object());
        __ CallRuntime(Runtime::kThrowConstructorReturnedNonObject);
        masm->DefineExceptionHandlerAndLazyDeoptPoint(node);
        __ Abort(AbortReason::kUnexpectedReturnFromThrow);
      },
      this));

  __ bind(&done);
}

int CheckConstructResult::MaxCallStackArgs() const { return 0; }
void CheckConstructResult::SetValueLocationConstraints() {
  UseRegister(construct_result_input());
  UseRegister(implicit_receiver_input());
  DefineSameAsFirst(this);
}
void CheckConstructResult::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  Register construct_result = ToRegister(construct_result_input());
  Register result_reg = ToRegister(result());

  DCHECK_EQ(construct_result, result_reg);

  // If the result is an object (in the ECMA sense), we should get rid
  // of the receiver and use the result; see ECMA-262 section 13.2.2-7
  // on page 74.
  Label done, use_receiver;

  // If the result is undefined, we'll use the implicit receiver.
  __ JumpIfRoot(construct_result, RootIndex::kUndefinedValue, &use_receiver,
                Label::Distance::kNear);

  // If the result is a smi, it is *not* an object in the ECMA sense.
  __ JumpIfSmi(construct_result, &use_receiver, Label::Distance::kNear);

  // Check if the type of the result is not an object in the ECMA sense.
  __ JumpIfJSAnyIsNotPrimitive(construct_result, &done, Label::Distance::kNear);

  // Throw away the result of the constructor invocation and use the
  // implicit receiver as the result.
  __ bind(&use_receiver);
  Register implicit_receiver = ToRegister(implicit_receiver_input());
  __ Move(result_reg, implicit_receiver);

  __ bind(&done);
}

int CreateObjectLiteral::MaxCallStackArgs() const {
  DCHECK_EQ(Runtime::FunctionForId(Runtime::kCreateObjectLiteral)->nargs, 4);
  return 4;
}
void CreateObjectLiteral::SetValueLocationConstraints() {
  DefineAsFixed(this, kReturnRegister0);
}
void CreateObjectLiteral::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {
  __ CallBuiltin<Builtin::kCreateObjectFromSlowBoilerplate>(
      masm->native_context().object(),              // context
      feedback().vector,                            // feedback vector
      TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
      boilerplate_descriptor().object(),            // boilerplate descriptor
      Smi::FromInt(flags())                         // flags
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int CreateShallowArrayLiteral::MaxCallStackArgs() const {
  using D =
      CallInterfaceDescriptorFor<Builtin::kCreateShallowArrayLiteral>::type;
  return D::GetStackParameterCount();
}
void CreateShallowArrayLiteral::SetValueLocationConstraints() {
  DefineAsFixed(this, kReturnRegister0);
}
void CreateShallowArrayLiteral::GenerateCode(MaglevAssembler* masm,
                                             const ProcessingState& state) {
  __ CallBuiltin<Builtin::kCreateShallowArrayLiteral>(
      masm->native_context().object(),              // context
      feedback().vector,                            // feedback vector
      TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
      constant_elements().object(),                 // constant elements
      Smi::FromInt(flags())                         // flags
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int CreateArrayLiteral::MaxCallStackArgs() const {
  DCHECK_EQ(Runtime::FunctionForId(Runtime::kCreateArrayLiteral)->nargs, 4);
  return 4;
}
void CreateArrayLiteral::SetValueLocationConstraints() {
  DefineAsFixed(this, kReturnRegister0);
}
void CreateArrayLiteral::GenerateCode(MaglevAssembler* masm,
                                      const ProcessingState& state) {
  __ CallBuiltin<Builtin::kCreateArrayFromSlowBoilerplate>(
      masm->native_context().object(),              // context
      feedback().vector,                            // feedback vector
      TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
      constant_elements().object(),                 // boilerplate descriptor
      Smi::FromInt(flags())                         // flags
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int CreateShallowObjectLiteral::MaxCallStackArgs() const {
  using D =
      CallInterfaceDescriptorFor<Builtin::kCreateShallowObjectLiteral>::type;
  return D::GetStackParameterCount();
}
void CreateShallowObjectLiteral::SetValueLocationConstraints() {
  DefineAsFixed(this, kReturnRegister0);
}
void CreateShallowObjectLiteral::GenerateCode(MaglevAssembler* masm,
                                              const ProcessingState& state) {
  __ CallBuiltin<Builtin::kCreateShallowObjectLiteral>(
      masm->native_context().object(),              // context
      feedback().vector,                            // feedback vector
      TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
      boilerplate_descriptor().object(),            // desc
      Smi::FromInt(flags())                         // flags
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

void AllocationBlock::SetValueLocationConstraints() { DefineAsRegister(this); }

void AllocationBlock::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {
  __ Allocate(register_snapshot(), ToRegister(result()), size(),
              allocation_type());
}

int CreateClosure::MaxCallStackArgs() const {
  DCHECK_EQ(Runtime::FunctionForId(pretenured() ? Runtime::kNewClosure_Tenured
                                                : Runtime::kNewClosure)
                ->nargs,
            2);
  return 2;
}
void CreateClosure::SetValueLocationConstraints() {
  UseFixed(context(), kContextRegister);
  DefineAsFixed(this, kReturnRegister0);
}
void CreateClosure::GenerateCode(MaglevAssembler* masm,
                                 const ProcessingState& state) {
  Runtime::FunctionId function_id =
      pretenured() ? Runtime::kNewClosure_Tenured : Runtime::kNewClosure;
  __ Push(shared_function_info().object(), feedback_cell().object());
  __ CallRuntime(function_id);
}

int FastCreateClosure::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kFastNewClosure>::type;
  return D::GetStackParameterCount();
}
void FastCreateClosure::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kFastNewClosure>::type;
  static_assert(D::HasContextParameter());
  UseFixed(context(), D::ContextRegister());
  DefineAsFixed(this, kReturnRegister0);
}
void FastCreateClosure::GenerateCode(MaglevAssembler* masm,
                                     const ProcessingState& state) {
  __ CallBuiltin<Builtin::kFastNewClosure>(
      context(),                        // context
      shared_function_info().object(),  // shared function info
      feedback_cell().object()          // feedback cell
  );
  masm->DefineLazyDeoptPoint(lazy_deopt_info());
}

int CreateFunctionContext::MaxCallStackArgs() const {
  if (scope_type() == FUNCTION_SCOPE) {
    using D = CallInterfaceDescriptorFor<
        Builtin::kFastNewFunctionContextFunction>::type;
    return D::GetStackParameterCount();
  } else {
    using D =
        CallInterfaceDescriptorFor<Builtin::kFastNewFunctionContextEval>::type;
    return D::GetStackParameterCount();
  }
}
void CreateFunctionContext::SetValueLocationConstraints() {
  DCHECK_LE(slot_count(),
            static_cast<uint32_t>(
                ConstructorBuiltins::MaximumFunctionContextSlots()));
  if (scope_type() == FUNCTION_SCOPE) {
    using D = CallInterfaceDescriptorFor<
        Builtin::kFastNewFunctionContextFunction>::type;
    static_assert(D::HasContextParameter());
    UseFixed(context(), D::ContextRegister());
  } else {
    DCHECK_EQ(scope_type(), ScopeType::EVAL_SCOPE);
    using D =
        CallInterfaceDescriptorFor<Builtin::kFastNewFunctionContextEval>::type;
    static_assert(D::HasContextParameter());
    UseFixed(context(), D::ContextRegister());
  }
  DefineAsFixed(this, kReturnRegister0);
}
void CreateFunctionContext::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  if (scope_type() == FUNCTION_SCOPE) {
    __ CallBuiltin<Builtin::kFastNewFunctionContextFunction>(
        context(),              // context
        scope_info().object(),  // scope info
        slot_count()            // slots
    );
  } else {
    __ CallBuiltin<Builtin::kFastNewFunctionContextEval>(
        context(),              // context
        scope_info().object(),  // scope info
        slot_count()            // slots
    );
  }
  masm->DefineLazyDeoptPoint(lazy_deopt_info());
}

int CreateRegExpLiteral::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kCreateRegExpLiteral>::type;
  return D::GetStackParameterCount();
}
void CreateRegExpLiteral::SetValueLocationConstraints() {
  DefineAsFixed(this, kReturnRegister0);
}
void CreateRegExpLiteral::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {
  __ CallBuiltin<Builtin::kCreateRegExpLiteral>(
      masm->native_context().object(),              // context
      feedback().vector,                            // feedback vector
      TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
      pattern().object(),                           // pattern
      Smi::FromInt(flags())                         // flags
  );
  masm->DefineLazyDeoptPoint(lazy_deopt_info());
}

int GetTemplateObject::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kGetTemplateObject>::type;
  return D::GetStackParameterCount();
}
void GetTemplateObject::SetValueLocationConstraints() {
  using D = GetTemplateObjectDescriptor;
  UseFixed(description(), D::GetRegisterParameter(D::kDescription));
  DefineAsFixed(this, kReturnRegister0);
}
void GetTemplateObject::GenerateCode(MaglevAssembler* masm,
                                     const ProcessingState& state) {
  __ CallBuiltin<Builtin::kGetTemplateObject>(
      masm->native_context().object(),  // context
      shared_function_info_.object(),   // shared function info
      description(),                    // description
      feedback().index(),               // feedback slot
      feedback().vector                 // feedback vector
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int HasInPrototypeChain::MaxCallStackArgs() const {
  DCHECK_EQ(2, Runtime::FunctionForId(Runtime::kHasInPrototypeChain)->nargs);
  return 2;
}
void HasInPrototypeChain::SetValueLocationConstraints() {
  UseRegister(object());
  DefineAsRegister(this);
  set_temporaries_needed(2);
}
void HasInPrototypeChain::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register object_reg = ToRegister(object());
  Register result_reg = ToRegister(result());

  Label return_false, return_true;
  ZoneLabelRef done(masm);

  __ JumpIfSmi(object_reg, &return_false,
               v8_flags.debug_code ? Label::kFar : Label::kNear);

  // Loop through the prototype chain looking for the {prototype}.
  Register map = temps.Acquire();
  __ LoadMap(map, object_reg);
  Label loop;
  {
    __ bind(&loop);
    Register scratch = temps.Acquire();
    // Check if we can determine the prototype directly from the {object_map}.
    ZoneLabelRef if_objectisdirect(masm);
    Register instance_type = scratch;
    Condition jump_cond = __ CompareInstanceTypeRange(
        map, instance_type, FIRST_TYPE, LAST_SPECIAL_RECEIVER_TYPE);
    __ JumpToDeferredIf(
        jump_cond,
        [](MaglevAssembler* masm, RegisterSnapshot snapshot,
           Register object_reg, Register map, Register instance_type,
           Register result_reg, HasInPrototypeChain* node,
           ZoneLabelRef if_objectisdirect, ZoneLabelRef done) {
          Label return_runtime;
          // The {object_map} is a special receiver map or a primitive map,
          // check if we need to use the if_objectisspecial path in the runtime.
          __ JumpIfEqual(instance_type, JS_PROXY_TYPE, &return_runtime);

          int mask = Map::Bits1::HasNamedInterceptorBit::kMask |
                     Map::Bits1::IsAccessCheckNeededBit::kMask;
          __ TestUint8AndJumpIfAllClear(
              FieldMemOperand(map, Map::kBitFieldOffset), mask,
              *if_objectisdirect);

          __ bind(&return_runtime);
          {
            snapshot.live_registers.clear(result_reg);
            SaveRegisterStateForCall save_register_state(masm, snapshot);
            __ Push(object_reg, node->prototype().object());
            __ Move(kContextRegister, masm->native_context().object());
            __ CallRuntime(Runtime::kHasInPrototypeChain, 2);
            masm->DefineExceptionHandlerPoint(node);
            save_register_state.DefineSafepointWithLazyDeopt(
                node->lazy_deopt_info());
            __ Move(result_reg, kReturnRegister0);
          }
          __ Jump(*done);
        },
        register_snapshot(), object_reg, map, instance_type, result_reg, this,
        if_objectisdirect, done);
    instance_type = Register::no_reg();

    __ bind(*if_objectisdirect);
    // Check the current {object} prototype.
    Register object_prototype = scratch;
    __ LoadTaggedField(object_prototype, map, Map::kPrototypeOffset);
    __ JumpIfRoot(object_prototype, RootIndex::kNullValue, &return_false,
                  v8_flags.debug_code ? Label::kFar : Label::kNear);
    __ CompareTaggedAndJumpIf(object_prototype, prototype().object(), kEqual,
                              &return_true, Label::kNear);

    // Continue with the prototype.
    __ AssertNotSmi(object_prototype);
    __ LoadMap(map, object_prototype);
    __ Jump(&loop);
  }

  __ bind(&return_true);
  __ LoadRoot(result_reg, RootIndex::kTrueValue);
  __ Jump(*done, Label::kNear);

  __ bind(&return_false);
  __ LoadRoot(result_reg, RootIndex::kFalseValue);
  __ bind(*done);
}

void DebugBreak::SetValueLocationConstraints() {}
void DebugBreak::GenerateCode(MaglevAssembler* masm,
                              const ProcessingState& state) {
  __ DebugBreak();
}

int Abort::MaxCallStackArgs() const {
  DCHECK_EQ(Runtime::FunctionForId(Runtime::kAbort)->nargs, 1);
  return 1;
}
void Abort::SetValueLocationConstraints() {}
void Abort::GenerateCode(MaglevAssembler* masm, const ProcessingState& state) {
  __ Push(Smi::FromInt(static_cast<int>(reason())));
  __ CallRuntime(Runtime::kAbort, 1);
  __ Trap();
}

void LogicalNot::SetValueLocationConstraints() {
  UseAny(value());
  DefineAsRegister(this);
}
void LogicalNot::GenerateCode(MaglevAssembler* masm,
                              const ProcessingState& state) {
  if (v8_flags.debug_code) {
    // LogicalNot expects either TrueValue or FalseValue.
    Label next;
    __ JumpIf(__ IsRootConstant(value(), RootIndex::kFalseValue), &next);
    __ JumpIf(__ IsRootConstant(value(), RootIndex::kTrueValue), &next);
    __ Abort(AbortReason::kUnexpectedValue);
    __ bind(&next);
  }

  Label return_false, done;
  __ JumpIf(__ IsRootConstant(value(), RootIndex::kTrueValue), &return_false);
  __ LoadRoot(ToRegister(result()), RootIndex::kTrueValue);
  __ Jump(&done);

  __ bind(&return_false);
  __ LoadRoot(ToRegister(result()), RootIndex::kFalseValue);

  __ bind(&done);
}

int LoadNamedGeneric::MaxCallStackArgs() const {
  return LoadWithVectorDescriptor::GetStackParameterCount();
}
void LoadNamedGeneric::SetValueLocationConstraints() {
  using D = LoadWithVectorDescriptor;
  UseFixed(context(), kContextRegister);
  UseFixed(object_input(), D::GetRegisterParameter(D::kReceiver));
  DefineAsFixed(this, kReturnRegister0);
}
void LoadNamedGeneric::GenerateCode(MaglevAssembler* masm,
                                    const ProcessingState& state) {
  __ CallBuiltin<Builtin::kLoadIC>(
      context(),                                    // context
      object_input(),                               // receiver
      name().object(),                              // name
      TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
      feedback().vector                             // feedback vector
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int LoadNamedFromSuperGeneric::MaxCallStackArgs() const {
  return LoadWithReceiverAndVectorDescriptor::GetStackParameterCount();
}
void LoadNamedFromSuperGeneric::SetValueLocationConstraints() {
  using D = LoadWithReceiverAndVectorDescriptor;
  UseFixed(context(), kContextRegister);
  UseFixed(receiver(), D::GetRegisterParameter(D::kReceiver));
  UseFixed(lookup_start_object(),
           D::GetRegisterParameter(D::kLookupStartObject));
  DefineAsFixed(this, kReturnRegister0);
}
void LoadNamedFromSuperGeneric::GenerateCode(MaglevAssembler* masm,
                                             const ProcessingState& state) {
  __ CallBuiltin<Builtin::kLoadSuperIC>(
      context(),                                    // context
      receiver(),                                   // receiver
      lookup_start_object(),                        // lookup start object
      name().object(),                              // name
      TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
      feedback().vector                             // feedback vector
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int SetNamedGeneric::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kStoreIC>::type;
  return D::GetStackParameterCount();
}
void SetNamedGeneric::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kStoreIC>::type;
  UseFixed(context(), kContextRegister);
  UseFixed(object_input(), D::GetRegisterParameter(D::kReceiver));
  UseFixed(value_input(), D::GetRegisterParameter(D::kValue));
  DefineAsFixed(this, kReturnRegister0);
}
void SetNamedGeneric::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {
  __ CallBuiltin<Builtin::kStoreIC>(
      context(),                                    // context
      object_input(),                               // receiver
      name().object(),                              // name
      value_input(),                                // value
      TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
      feedback().vector                             // feedback vector
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int DefineNamedOwnGeneric::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kDefineNamedOwnIC>::type;
  return D::GetStackParameterCount();
}
void DefineNamedOwnGeneric::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kDefineNamedOwnIC>::type;
  UseFixed(context(), kContextRegister);
  UseFixed(object_input(), D::GetRegisterParameter(D::kReceiver));
  UseFixed(value_input(), D::GetRegisterParameter(D::kValue));
  DefineAsFixed(this, kReturnRegister0);
}
void DefineNamedOwnGeneric::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  __ CallBuiltin<Builtin::kDefineNamedOwnIC>(
      context(),                                    // context
      object_input(),                               // receiver
      name().object(),                              // name
      value_input(),                                // value
      TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
      feedback().vector                             // feedback vector
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

void UpdateJSArrayLength::SetValueLocationConstraints() {
  UseRegister(length_input());
  UseRegister(object_input());
  UseAndClobberRegister(index_input());
  DefineSameAsFirst(this);
}

void UpdateJSArrayLength::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {
  Register length = ToRegister(length_input());
  Register object = ToRegister(object_input());
  Register index = ToRegister(index_input());
  DCHECK_EQ(length, ToRegister(result()));

  Label done, tag_length;
  if (v8_flags.debug_code) {
    __ AssertObjectType(object, JS_ARRAY_TYPE, AbortReason::kUnexpectedValue);
    static_assert(Internals::IsValidSmi(FixedArray::kMaxLength),
                  "MaxLength not a Smi");
    __ CompareInt32AndAssert(index, FixedArray::kMaxLength, kUnsignedLessThan,
                             AbortReason::kUnexpectedValue);
  }
  __ CompareInt32AndJumpIf(index, length, kUnsignedLessThan, &tag_length,
                           Label::kNear);
  __ IncrementInt32(index);  // This cannot overflow.
  __ SmiTag(length, index);
  __ StoreTaggedSignedField(object, JSArray::kLengthOffset, length);
  __ Jump(&done, Label::kNear);
  __ bind(&tag_length);
  __ SmiTag(length);
  __ bind(&done);
}

void EnsureWritableFastElements::SetValueLocationConstraints() {
  UseRegister(elements_input());
  UseRegister(object_input());
  set_temporaries_needed(1);
  DefineSameAsFirst(this);
}
void EnsureWritableFastElements::GenerateCode(MaglevAssembler* masm,
                                              const ProcessingState& state) {
  Register object = ToRegister(object_input());
  Register elements = ToRegister(elements_input());
  DCHECK_EQ(elements, ToRegister(result()));
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  __ EnsureWritableFastElements(register_snapshot(), elements, object, scratch);
}

void MaybeGrowFastElements::SetValueLocationConstraints() {
  UseRegister(elements_input());
  UseRegister(object_input());
  UseRegister(index_input());
  UseRegister(elements_length_input());
  if (IsSmiOrObjectElementsKind(elements_kind())) {
    set_temporaries_needed(1);
  }
  DefineSameAsFirst(this);
}
void MaybeGrowFastElements::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  Register elements = ToRegister(elements_input());
  Register object = ToRegister(object_input());
  Register index = ToRegister(index_input());
  Register elements_length = ToRegister(elements_length_input());
  DCHECK_EQ(elements, ToRegister(result()));

  ZoneLabelRef done(masm);

  __ CompareInt32AndJumpIf(
      index, elements_length, kUnsignedGreaterThanEqual,
      __ MakeDeferredCode(
          [](MaglevAssembler* masm, ZoneLabelRef done, Register object,
             Register index, Register result_reg, MaybeGrowFastElements* node) {
            {
              RegisterSnapshot snapshot = node->register_snapshot();
              snapshot.live_registers.clear(result_reg);
              snapshot.live_tagged_registers.clear(result_reg);
              SaveRegisterStateForCall save_register_state(masm, snapshot);
              using D = GrowArrayElementsDescriptor;
              if (index == D::GetRegisterParameter(D::kObject)) {
                // That implies that the first parameter move will clobber the
                // index value. So we use the result register as temporary.
                // TODO(leszeks): Use parallel moves to resolve cases like this.
                __ SmiTag(result_reg, index);
                index = result_reg;
              } else {
                __ SmiTag(index);
              }
              if (IsDoubleElementsKind(node->elements_kind())) {
                __ CallBuiltin<Builtin::kGrowFastDoubleElements>(object, index);
              } else {
                __ CallBuiltin<Builtin::kGrowFastSmiOrObjectElements>(object,
                                                                      index);
              }
              save_register_state.DefineSafepoint();
              __ Move(result_reg, kReturnRegister0);
            }
            __ EmitEagerDeoptIfSmi(node, result_reg,
                                   DeoptimizeReason::kCouldNotGrowElements);
            __ Jump(*done);
          },
          done, object, index, elements, this));

  __ bind(*done);
}

void ExtendPropertiesBackingStore::SetValueLocationConstraints() {
  UseRegister(property_array_input());
  UseRegister(object_input());
  DefineAsRegister(this);
  set_temporaries_needed(2);
}

void ExtendPropertiesBackingStore::GenerateCode(MaglevAssembler* masm,
                                                const ProcessingState& state) {
  Register object = ToRegister(object_input());
  Register old_property_array = ToRegister(property_array_input());
  Register result_reg = ToRegister(result());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register new_property_array =
      result_reg == object || result_reg == old_property_array ? temps.Acquire()
                                                               : result_reg;
  Register scratch = temps.Acquire();
  DCHECK(!AreAliased(object, old_property_array, new_property_array, scratch));

  int new_length = old_length_ + JSObject::kFieldsAdded;

  // Allocate new PropertyArray.
  {
    RegisterSnapshot snapshot = register_snapshot();
    // old_property_array needs to be live, since we'll read data from it.
    // Object needs to be live, since we write the new property array into it.
    snapshot.live_registers.set(object);
    snapshot.live_registers.set(old_property_array);
    snapshot.live_tagged_registers.set(object);
    snapshot.live_tagged_registers.set(old_property_array);

    Register size_in_bytes = scratch;
    __ Move(size_in_bytes, PropertyArray::SizeFor(new_length));
    __ Allocate(snapshot, new_property_array, size_in_bytes,
                AllocationType::kYoung);
    __ SetMapAsRoot(new_property_array, RootIndex::kPropertyArrayMap);
  }

  // Copy existing properties over.
  {
    RegisterSnapshot snapshot = register_snapshot();
    snapshot.live_registers.set(object);
    snapshot.live_registers.set(old_property_array);
    snapshot.live_registers.set(new_property_array);
    snapshot.live_tagged_registers.set(object);
    snapshot.live_tagged_registers.set(old_property_array);
    snapshot.live_tagged_registers.set(new_property_array);

    for (int i = 0; i < old_length_; ++i) {
      __ LoadTaggedFieldWithoutDecompressing(
          scratch, old_property_array, PropertyArray::OffsetOfElementAt(i));

      __ StoreTaggedFieldWithWriteBarrier(
          new_property_array, PropertyArray::OffsetOfElementAt(i), scratch,
          snapshot, MaglevAssembler::kValueIsCompressed,
          MaglevAssembler::kValueCanBeSmi);
    }
  }

  // Initialize new properties to undefined.
  __ LoadRoot(scratch, RootIndex::kUndefinedValue);
  for (int i = 0; i < JSObject::kFieldsAdded; ++i) {
    __ StoreTaggedFieldNoWriteBarrier(
        new_property_array, PropertyArray::OffsetOfElementAt(old_length_ + i),
        scratch);
  }

  // Read the hash.
  if (old_length_ == 0) {
    // The object might still have a hash, stored in properties_or_hash. If
    // properties_or_hash is a SMI, then it's the hash. It can also be an empty
    // PropertyArray.
    __ LoadTaggedField(scratch, object, JSObject::kPropertiesOrHashOffset);

    Label done;
    __ JumpIfSmi(scratch, &done);

    __ Move(scratch, PropertyArray::kNoHashSentinel);

    __ bind(&done);
    __ SmiUntag(scratch);
    __ ShiftLeft(scratch, PropertyArray::HashField::kShift);
  } else {
    __ LoadTaggedField(scratch, old_property_array,
                       PropertyArray::kLengthAndHashOffset);
    __ SmiUntag(scratch);
    __ AndInt32(scratch, PropertyArray::HashField::kMask);
  }

  // Add the new length and write the length-and-hash field.
  static_assert(PropertyArray::LengthField::kShift == 0);
  __ OrInt32(scratch, new_length);

  __ UncheckedSmiTagInt32(scratch, scratch);
  __ StoreTaggedFieldNoWriteBarrier(
      new_property_array, PropertyArray::kLengthAndHashOffset, scratch);

  {
    RegisterSnapshot snapshot = register_snapshot();
    // new_property_array needs to be live since we'll return it.
    snapshot.live_registers.set(new_property_array);
    snapshot.live_tagged_registers.set(new_property_array);

    __ StoreTaggedFieldWithWriteBarrier(
        object, JSObject::kPropertiesOrHashOffset, new_property_array, snapshot,
        MaglevAssembler::kValueIsDecompressed,
        MaglevAssembler::kValueCannotBeSmi);
  }
  if (result_reg != new_property_array) {
    __ Move(result_reg, new_property_array);
  }
}

int SetKeyedGeneric::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kKeyedStoreIC>::type;
  return D::GetStackParameterCount();
}
void SetKeyedGeneric::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kKeyedStoreIC>::type;
  UseFixed(context(), kContextRegister);
  UseFixed(object_input(), D::GetRegisterParameter(D::kReceiver));
  UseFixed(key_input(), D::GetRegisterParameter(D::kName));
  UseFixed(value_input(), D::GetRegisterParameter(D::kValue));
  DefineAsFixed(this, kReturnRegister0);
}
void SetKeyedGeneric::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {
  __ CallBuiltin<Builtin::kKeyedStoreIC>(
      context(),                                    // context
      object_input(),                               // receiver
      key_input(),                                  // name
      value_input(),                                // value
      TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
      feedback().vector                             // feedback vector
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int DefineKeyedOwnGeneric::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kDefineKeyedOwnIC>::type;
  return D::GetStackParameterCount();
}
void DefineKeyedOwnGeneric::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kDefineKeyedOwnIC>::type;
  UseFixed(context(), kContextRegister);
  UseFixed(object_input(), D::GetRegisterParameter(D::kReceiver));
  UseFixed(key_input(), D::GetRegisterParameter(D::kName));
  UseFixed(value_input(), D::GetRegisterParameter(D::kValue));
  UseFixed(flags_input(), D::GetRegisterParameter(D::kFlags));
  DefineAsFixed(this, kReturnRegister0);
}
void DefineKeyedOwnGeneric::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  __ CallBuiltin<Builtin::kDefineKeyedOwnIC>(
      context(),                                    // context
      object_input(),                               // receiver
      key_input(),                                  // name
      value_input(),                                // value
      flags_input(),                                // flags
      TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
      feedback().vector                             // feedback vector
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int StoreInArrayLiteralGeneric::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kStoreInArrayLiteralIC>::type;
  return D::GetStackParameterCount();
}
void StoreInArrayLiteralGeneric::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kStoreInArrayLiteralIC>::type;
  UseFixed(context(), kContextRegister);
  UseFixed(object_input(), D::GetRegisterParameter(D::kReceiver));
  UseFixed(name_input(), D::GetRegisterParameter(D::kName));
  UseFixed(value_input(), D::GetRegisterParameter(D::kValue));
  DefineAsFixed(this, kReturnRegister0);
}
void StoreInArrayLiteralGeneric::GenerateCode(MaglevAssembler* masm,
                                              const ProcessingState& state) {
  __ CallBuiltin<Builtin::kStoreInArrayLiteralIC>(
      context(),                                    // context
      object_input(),                               // receiver
      name_input(),                                 // name
      value_input(),                                // value
      TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
      feedback().vector                             // feedback vector
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

void GeneratorRestoreRegister::SetValueLocationConstraints() {
  UseRegister(array_input());
  UseRegister(stale_input());
  DefineAsRegister(this);
  set_temporaries_needed(1);
}
void GeneratorRestoreRegister::GenerateCode(MaglevAssembler* masm,
                                            const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register temp = temps.Acquire();
  Register array = ToRegister(array_input());
  Register stale = ToRegister(stale_input());
  Register result_reg = ToRegister(result());

  // The input and the output can alias, if that happens we use a temporary
  // register and a move at the end.
  Register value = (array == result_reg ? temp : result_reg);

  // Loads the current value in the generator register file.
  __ LoadTaggedField(value, array, FixedArray::OffsetOfElementAt(index()));

  // And trashs it with StaleRegisterConstant.
  DCHECK(stale_input().node()->Is<RootConstant>());
  __ StoreTaggedFieldNoWriteBarrier(
      array, FixedArray::OffsetOfElementAt(index()), stale);

  if (value != result_reg) {
    __ Move(result_reg, value);
  }
}

int GeneratorStore::MaxCallStackArgs() const {
  return WriteBarrierDescriptor::GetStackParameterCount();
}
void GeneratorStore::SetValueLocationConstraints() {
  UseAny(context_input());
  UseRegister(generator_input());
  for (int i = 0; i < num_parameters_and_registers(); i++) {
    UseAny(parameters_and_registers(i));
  }
  RequireSpecificTemporary(WriteBarrierDescriptor::ObjectRegister());
  RequireSpecificTemporary(WriteBarrierDescriptor::SlotAddressRegister());
}
void GeneratorStore::GenerateCode(MaglevAssembler* masm,
                                  const ProcessingState& state) {
  Register generator = ToRegister(generator_input());
  Register array = WriteBarrierDescriptor::ObjectRegister();
  __ LoadTaggedField(array, generator,
                     JSGeneratorObject::kParametersAndRegistersOffset);

  RegisterSnapshot register_snapshot_during_store = register_snapshot();
  // Include the array and generator registers in the register snapshot while
  // storing parameters and registers, to avoid the write barrier clobbering
  // them.
  register_snapshot_during_store.live_registers.set(array);
  register_snapshot_during_store.live_tagged_registers.set(array);
  register_snapshot_during_store.live_registers.set(generator);
  register_snapshot_during_store.live_tagged_registers.set(generator);
  for (int i = 0; i < num_parameters_and_registers(); i++) {
    // Use WriteBarrierDescriptor::SlotAddressRegister() as the temporary for
    // the value -- it'll be clobbered by StoreTaggedFieldWithWriteBarrier since
    // it's not in the register snapshot, but that's ok, and a clobberable value
    // register lets the write barrier emit slightly better code.
    Input value_input = parameters_and_registers(i);
    Register value = __ FromAnyToRegister(
        value_input, WriteBarrierDescriptor::SlotAddressRegister());
    // Include the value register in the live set, in case it is used by future
    // inputs.
    register_snapshot_during_store.live_registers.set(value);
    register_snapshot_during_store.live_tagged_registers.set(value);
    __ StoreTaggedFieldWithWriteBarrier(
        array, FixedArray::OffsetOfElementAt(i), value,
        register_snapshot_during_store,
        value_input.node()->decompresses_tagged_result()
            ? MaglevAssembler::kValueIsDecompressed
            : MaglevAssembler::kValueIsCompressed,
        MaglevAssembler::kValueCanBeSmi);
  }

  __ StoreTaggedSignedField(generator, JSGeneratorObject::kContinuationOffset,
                            Smi::FromInt(suspend_id()));
  __ StoreTaggedSignedField(generator,
                            JSGeneratorObject::kInputOrDebugPosOffset,
                            Smi::FromInt(bytecode_offset()));

  // Use WriteBarrierDescriptor::SlotAddressRegister() as the scratch
  // register, see comment above. At this point we no longer need to preserve
  // the array or generator registers, so use the original register snapshot.
  Register context = __ FromAnyToRegister(
      context_input(), WriteBarrierDescriptor::SlotAddressRegister());
  __ StoreTaggedFieldWithWriteBarrier(
      generator, JSGeneratorObject::kContextOffset, context,
      register_snapshot(),
      context_input().node()->decompresses_tagged_result()
          ? MaglevAssembler::kValueIsDecompressed
          : MaglevAssembler::kValueIsCompressed,
      MaglevAssembler::kValueCannotBeSmi);
}

int GetKeyedGeneric::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kKeyedLoadIC>::type;
  return D::GetStackParameterCount();
}
void GetKeyedGeneric::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kKeyedLoadIC>::type;
  UseFixed(context(), kContextRegister);
  UseFixed(object_input(), D::GetRegisterParameter(D::kReceiver));
  UseFixed(key_input(), D::GetRegisterParameter(D::kName));
  DefineAsFixed(this, kReturnRegister0);
}
void GetKeyedGeneric::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {
  __ CallBuiltin<Builtin::kKeyedLoadIC>(
      context(),                                    // context
      object_input(),                               // receiver
      key_input(),                                  // name
      TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
      feedback().vector                             // feedback vector
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

void Int32ToNumber::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void Int32ToNumber::GenerateCode(MaglevAssembler* masm,
                                 const ProcessingState& state) {
  Register object = ToRegister(result());
  Register value = ToRegister(input());
  ZoneLabelRef done(masm);
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  // Object is not allowed to alias value, because SmiTagInt32AndJumpIfFail will
  // clobber `object` even if the tagging fails, and we don't want it to clobber
  // `value`.
  bool input_output_alias = (object == value);
  Register res = object;
  if (input_output_alias) {
    res = temps.AcquireScratch();
  }
  __ SmiTagInt32AndJumpIfFail(
      res, value,
      __ MakeDeferredCode(
          [](MaglevAssembler* masm, Register object, Register value,
             Register scratch, ZoneLabelRef done, Int32ToNumber* node) {
            MaglevAssembler::TemporaryRegisterScope temps(masm);
            // AllocateHeapNumber needs a scratch register, and the res scratch
            // register isn't needed anymore, so return it to the pool.
            if (scratch.is_valid()) {
              temps.IncludeScratch(scratch);
            }
            DoubleRegister double_value = temps.AcquireScratchDouble();
            __ Int32ToDouble(double_value, value);
            __ AllocateHeapNumber(node->register_snapshot(), object,
                                  double_value);
            __ Jump(*done);
          },
          object, value, input_output_alias ? res : Register::no_reg(), done,
          this));
  if (input_output_alias) {
    __ Move(object, res);
  }
  __ bind(*done);
}

void Uint32ToNumber::SetValueLocationConstraints() {
  UseRegister(input());
#ifdef V8_TARGET_ARCH_X64
  // We emit slightly more efficient code if result is the same as input.
  DefineSameAsFirst(this);
#else
  DefineAsRegister(this);
#endif
}
void Uint32ToNumber::GenerateCode(MaglevAssembler* masm,
                                  const ProcessingState& state) {
  ZoneLabelRef done(masm);
  Register value = ToRegister(input());
  Register object = ToRegister(result());
  // Unlike Int32ToNumber, object is allowed to alias value here (indeed, the
  // code is better if it does). The difference is that Uint32 smi tagging first
  // does a range check, and doesn't clobber `object` on failure.
  __ SmiTagUint32AndJumpIfFail(
      object, value,
      __ MakeDeferredCode(
          [](MaglevAssembler* masm, Register object, Register value,
             ZoneLabelRef done, Uint32ToNumber* node) {
            MaglevAssembler::TemporaryRegisterScope temps(masm);
            DoubleRegister double_value = temps.AcquireScratchDouble();
            __ Uint32ToDouble(double_value, value);
            __ AllocateHeapNumber(node->register_snapshot(), object,
                                  double_value);
            __ Jump(*done);
          },
          object, value, done, this));
  __ bind(*done);
}

void IntPtrToNumber::SetValueLocationConstraints() {
  UseRegister(input());
#ifdef V8_TARGET_ARCH_X64
  // We emit slightly more efficient code if result is the same as input.
  DefineSameAsFirst(this);
#else
  DefineAsRegister(this);
#endif
}

void IntPtrToNumber::GenerateCode(MaglevAssembler* masm,
                                  const ProcessingState& state) {
  ZoneLabelRef done(masm);
  Register value = ToRegister(input());
  Register object = ToRegister(result());
  // Unlike Int32ToNumber, object is allowed to alias value here (indeed, the
  // code is better if it does). The difference is that IntPtr smi tagging first
  // does a range check, and doesn't clobber `object` on failure.
  __ SmiTagIntPtrAndJumpIfFail(
      object, value,
      __ MakeDeferredCode(
          [](MaglevAssembler* masm, Register object, Register value,
             ZoneLabelRef done, IntPtrToNumber* node) {
            MaglevAssembler::TemporaryRegisterScope temps(masm);
            DoubleRegister double_value = temps.AcquireScratchDouble();
            __ IntPtrToDouble(double_value, value);
            __ AllocateHeapNumber(node->register_snapshot(), object,
                                  double_value);
            __ Jump(*done);
          },
          object, value, done, this));
  __ bind(*done);
}

void Float64ToTagged::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void Float64ToTagged::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {
  DoubleRegister value = ToDoubleRegister(input());
  Register object = ToRegister(result());
  Label box, done;
  if (canonicalize_smi()) {
    __ TryTruncateDoubleToInt32(object, value, &box);
    __ SmiTagInt32AndJumpIfFail(object, &box);
    __ Jump(&done, Label::kNear);
    __ bind(&box);
  }
  __ AllocateHeapNumber(register_snapshot(), object, value);
  if (canonicalize_smi()) {
    __ bind(&done);
  }
}

void Float64ToHeapNumberForField::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void Float64ToHeapNumberForField::GenerateCode(MaglevAssembler* masm,
                                               const ProcessingState& state) {
  DoubleRegister value = ToDoubleRegister(input());
  Register object = ToRegister(result());
  __ AllocateHeapNumber(register_snapshot(), object, value);
}

void HoleyFloat64ToTagged::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void HoleyFloat64ToTagged::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  ZoneLabelRef done(masm);
  DoubleRegister value = ToDoubleRegister(input());
  Register object = ToRegister(result());
  Label box;
  if (canonicalize_smi()) {
    __ TryTruncateDoubleToInt32(object, value, &box);
    __ SmiTagInt32AndJumpIfFail(object, &box);
    __ Jump(*done, Label::kNear);
    __ bind(&box);
  }
  // Using return as scratch register.
  __ JumpIfHoleNan(
      value, ToRegister(result()),
      __ MakeDeferredCode(
          [](MaglevAssembler* masm, Register object, ZoneLabelRef done) {
            // TODO(leszeks): Evaluate whether this is worth deferring.
            __ LoadRoot(object, RootIndex::kUndefinedValue);
            __ Jump(*done);
          },
          object, done));
#ifdef V8_ENABLE_EXPERIMENTAL_UNDEFINED_DOUBLE
  Label not_undefined;
  __ JumpIfNotUndefinedNan(value, ToRegister(result()), &not_undefined);
  __ LoadRoot(object, RootIndex::kUndefinedValue);
  __ Jump(*done, Label::kNear);
  __ bind(&not_undefined);
#endif  // V8_ENABLE_EXPERIMENTAL_UNDEFINED_DOUBLE
  __ AllocateHeapNumber(register_snapshot(), object, value);
  __ bind(*done);
}

void Float64Round::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
  if (kind_ == Kind::kNearest) {
    set_double_temporaries_needed(1);
  }
}

void Int32AbsWithOverflow::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}

void Float64Abs::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}

void Int32CountLeadingZeros::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void Int32CountLeadingZeros::GenerateCode(MaglevAssembler* masm,
                                          const ProcessingState&) {
  Register in = ToRegister(input());
  Register out = ToRegister(result());
  __ CountLeadingZerosInt32(out, in);
}

void TaggedCountLeadingZeros::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
  set_double_temporaries_needed(1);
}
void TaggedCountLeadingZeros::GenerateCode(MaglevAssembler* masm,
                                           const ProcessingState&) {
  Register in = ToRegister(input());
  Register out = ToRegister(result());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  DoubleRegister double_value = temps.AcquireDouble();
  Label is_not_smi, done;

  __ JumpIfNotSmi(in, &is_not_smi, Label::Distance::kNear);

  __ SmiToInt32(out, in);
  __ CountLeadingZerosInt32(out, out);
  __ Jump(&done, Label::Distance::kNear);

  __ bind(&is_not_smi);
  if (v8_flags.debug_code) {
    __ AssertObjectType(in, HEAP_NUMBER_TYPE, AbortReason::kUnexpectedValue);
  }
  // If heap number, get double value.
  __ LoadHeapNumberValue(double_value, in);
  __ TruncateDoubleToInt32(out, double_value);
  __ CountLeadingZerosInt32(out, out);

  __ bind(&done);
}

void Float64CountLeadingZeros::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void Float64CountLeadingZeros::GenerateCode(MaglevAssembler* masm,
                                            const ProcessingState& state) {
  DoubleRegister in = ToDoubleRegister(input());
  Register out = ToRegister(result());
  __ TruncateDoubleToInt32(out, in);
  __ CountLeadingZerosInt32(out, out);
}

void CheckedSmiTagFloat64::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void CheckedSmiTagFloat64::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  DoubleRegister value = ToDoubleRegister(input());
  Register object = ToRegister(result());

  __ TryTruncateDoubleToInt32(
      object, value, __ GetDeoptLabel(this, DeoptimizeReason::kNotASmi));
  __ SmiTagInt32AndJumpIfFail(
      object, __ GetDeoptLabel(this, DeoptimizeReason::kNotASmi));
}

void StoreFloat64::SetValueLocationConstraints() {
  UseRegister(object_input());
  UseRegister(value_input());
}
void StoreFloat64::GenerateCode(MaglevAssembler* masm,
                                const ProcessingState& state) {
  Register object = ToRegister(object_input());
  DoubleRegister value = ToDoubleRegister(value_input());

  __ AssertNotSmi(object);
  __ StoreFloat64(FieldMemOperand(object, offset()), value);
}

void StoreInt32::SetValueLocationConstraints() {
  UseRegister(object_input());
  UseRegister(value_input());
}
void StoreInt32::GenerateCode(MaglevAssembler* masm,
                              const ProcessingState& state) {
  Register object = ToRegister(object_input());
  Register value = ToRegister(value_input());

  __ AssertNotSmi(object);
  __ StoreInt32(FieldMemOperand(object, offset()), value);
}

void StoreTaggedFieldNoWriteBarrier::SetValueLocationConstraints() {
  UseRegister(object_input());
  UseRegister(value_input());
}
void StoreTaggedFieldNoWriteBarrier::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  Register object = ToRegister(object_input());
  Register value = ToRegister(value_input());

  __ AssertNotSmi(object);

  __ StoreTaggedFieldNoWriteBarrier(object, offset(), value);
  __ AssertElidedWriteBarrier(object, value, register_snapshot());
}

int StringAt::MaxCallStackArgs() const {
  DCHECK_EQ(Runtime::FunctionForId(Runtime::kStringCharCodeAt)->nargs, 2);
  return std::max(2, AllocateDescriptor::GetStackParameterCount());
}
void StringAt::SetValueLocationConstraints() {
  UseAndClobberRegister(string_input());
  UseAndClobberRegister(index_input());
  DefineAsRegister(this);
  set_temporaries_needed(1);
}
void StringAt::GenerateCode(MaglevAssembler* masm,
                            const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  Register result_string = ToRegister(result());
  Register string = ToRegister(string_input());
  Register index = ToRegister(index_input());
  Register char_code = string;

  ZoneLabelRef done(masm);
  Label cached_one_byte_string;

  RegisterSnapshot save_registers = register_snapshot();
  __ StringCharCodeOrCodePointAt(
      BuiltinStringPrototypeCharCodeOrCodePointAt::kCharCodeAt, save_registers,
      char_code, string, index, scratch, Register::no_reg(),
      &cached_one_byte_string);
  __ StringFromCharCode(save_registers, &cached_one_byte_string, result_string,
                        char_code, scratch,
                        MaglevAssembler::CharCodeMaskMode::kValueIsInRange);
}

void SeqOneByteStringAt::SetValueLocationConstraints() {
  UseRegister(string_input());
  UseRegister(index_input());
  DefineAsRegister(this);
  set_temporaries_needed(1);
}
void SeqOneByteStringAt::GenerateCode(MaglevAssembler* masm,
                                      const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  Register result_string = ToRegister(result());
  Register string = ToRegister(string_input());
  Register index = ToRegister(index_input());
  Register char_code = result_string;

  __ SeqOneByteStringCharCodeAt(char_code, string, index);
  __ LoadSingleCharacterString(result_string, char_code, scratch);
}

int BuiltinStringPrototypeCharCodeOrCodePointAt::MaxCallStackArgs() const {
  DCHECK_EQ(Runtime::FunctionForId(Runtime::kStringCharCodeAt)->nargs, 2);
  return 2;
}
void BuiltinStringPrototypeCharCodeOrCodePointAt::
    SetValueLocationConstraints() {
  UseAndClobberRegister(string_input());
  UseAndClobberRegister(index_input());
  DefineAsRegister(this);
  // TODO(victorgomes): Add a mode to the register allocator where we ensure
  // input cannot alias with output. We can then remove the second scratch.
  set_temporaries_needed(
      mode() == BuiltinStringPrototypeCharCodeOrCodePointAt::kCodePointAt ? 2
                                                                          : 1);
}
void BuiltinStringPrototypeCharCodeOrCodePointAt::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch1 = temps.Acquire();
  Register scratch2 = Register::no_reg();
  if (mode() == BuiltinStringPrototypeCharCodeOrCodePointAt::kCodePointAt) {
    scratch2 = temps.Acquire();
  }
  Register string = ToRegister(string_input());
  Register index = ToRegister(index_input());
  ZoneLabelRef done(masm);
  RegisterSnapshot save_registers = register_snapshot();
  __ StringCharCodeOrCodePointAt(mode(), save_registers, ToRegister(result()),
                                 string, index, scratch1, scratch2, *done);
  __ bind(*done);
}

void BuiltinSeqOneByteStringCharCodeAt::SetValueLocationConstraints() {
  UseRegister(string_input());
  UseRegister(index_input());
  DefineAsRegister(this);
}
void BuiltinSeqOneByteStringCharCodeAt::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  Register string = ToRegister(string_input());
  Register index = ToRegister(index_input());
  __ SeqOneByteStringCharCodeAt(ToRegister(result()), string, index);
}

void StringLength::SetValueLocationConstraints() {
  UseRegister(object_input());
  DefineAsRegister(this);
}
void StringLength::GenerateCode(MaglevAssembler* masm,
                                const ProcessingState& state) {
  __ StringLength(ToRegister(result()), ToRegister(object_input()));
}

void StringConcat::SetValueLocationConstraints() {
  using D = StringAdd_CheckNoneDescriptor;
  UseFixed(lhs(), D::GetRegisterParameter(D::kLeft));
  UseFixed(rhs(), D::GetRegisterParameter(D::kRight));
  DefineAsFixed(this, kReturnRegister0);
}
void StringConcat::GenerateCode(MaglevAssembler* masm,
                                const ProcessingState& state) {
  __ CallBuiltin<Builtin::kStringAdd_CheckNone>(
      masm->native_context().object(),  // context
      lhs(),                            // left
      rhs()                             // right
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
  DCHECK_EQ(kReturnRegister0, ToRegister(result()));
}

void ConsStringMap::SetValueLocationConstraints() {
  UseRegister(lhs());
  UseRegister(rhs());
  DefineSameAsFirst(this);
}
void ConsStringMap::GenerateCode(MaglevAssembler* masm,
                                 const ProcessingState& state) {
  Label two_byte, ok;
  Register res = ToRegister(result());
  Register left = ToRegister(lhs());
  Register right = ToRegister(rhs());

  // Fast case for when the lhs() (which is identical to the result) happens to
  // contain the result for one byte string map inputs. In this case we only
  // need to check the rhs() and if it is one byte too, already have the result
  // in the correct register.
  bool left_contains_one_byte_res_map =
      lhs().node()->Is<RootConstant>() &&
      lhs().node()->Cast<RootConstant>()->index() ==
          RootIndex::kConsOneByteStringMap;

#ifdef V8_STATIC_ROOTS
  static_assert(InstanceTypeChecker::kOneByteStringMapBit == 0 ||
                InstanceTypeChecker::kTwoByteStringMapBit == 0);
  auto TestForTwoByte = [&](Register reg, Register second) {
    if constexpr (InstanceTypeChecker::kOneByteStringMapBit == 0) {
      // Two-byte is represented as 1: Check if either of them have the two-byte
      // bit set
      if (second != no_reg) {
        __ OrInt32(reg, second);
      }
      __ TestInt32AndJumpIfAnySet(reg,
                                  InstanceTypeChecker::kStringMapEncodingMask,
                                  &two_byte, Label::kNear);
    } else {
      // One-byte is represented as 1: Check that both of them have the one-byte
      // bit set
      if (second != no_reg) {
        __ AndInt32(reg, second);
      }
      __ TestInt32AndJumpIfAllClear(reg,
                                    InstanceTypeChecker::kStringMapEncodingMask,
                                    &two_byte, Label::kNear);
    }
  };
  if (left_contains_one_byte_res_map) {
    TestForTwoByte(right, no_reg);
  } else {
    TestForTwoByte(left, right);
  }
#else
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.AcquireScratch();
  static_assert(kTwoByteStringTag == 0);
  if (left_contains_one_byte_res_map) {
    __ LoadByte(scratch, FieldMemOperand(right, Map::kInstanceTypeOffset));
    __ TestInt32AndJumpIfAllClear(scratch, kStringEncodingMask, &two_byte,
                                  Label::kNear);
  } else {
    __ LoadByte(left, FieldMemOperand(left, Map::kInstanceTypeOffset));
    if (left != right) {
      __ LoadByte(scratch, FieldMemOperand(right, Map::kInstanceTypeOffset));
      __ AndInt32(scratch, left);
    }
    __ TestInt32AndJumpIfAllClear(scratch, kStringEncodingMask, &two_byte,
                                  Label::kNear);
  }
#endif  // V8_STATIC_ROOTS
  DCHECK_EQ(left, res);
  if (!left_contains_one_byte_res_map) {
    __ LoadRoot(res, RootIndex::kConsOneByteStringMap);
  }
  __ Jump(&ok, Label::kNear);

  __ bind(&two_byte);
  __ LoadRoot(res, RootIndex::kConsTwoByteStringMap);
  __ bind(&ok);
}

void UnwrapStringWrapper::SetValueLocationConstraints() {
  UseRegister(value_input());
  DefineSameAsFirst(this);
}
void UnwrapStringWrapper::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {
  Register input = ToRegister(value_input());
  Label done;
  __ JumpIfString(input, &done, Label::kNear);
  __ LoadTaggedField(input, input, JSPrimitiveWrapper::kValueOffset);
  __ bind(&done);
}

void StringEqual::SetValueLocationConstraints() {
  using D = StringEqualDescriptor;
  UseFixed(lhs(), D::GetRegisterParameter(D::kLeft));
  UseFixed(rhs(), D::GetRegisterParameter(D::kRight));
  set_temporaries_needed(1);
  RequireSpecificTemporary(D::GetRegisterParameter(D::kLength));
  DefineAsFixed(this, kReturnRegister0);
}
void StringEqual::GenerateCode(MaglevAssembler* masm,
                               const ProcessingState& state) {
  using D = StringEqualDescriptor;
  Label done, if_equal, if_not_equal;
  Register left = ToRegister(lhs());
  Register right = ToRegister(rhs());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register left_length = temps.Acquire();
  Register right_length = D::GetRegisterParameter(D::kLength);

  __ CmpTagged(left, right);
  __ JumpIf(kEqual, &if_equal,
            // Debug checks in StringLength can make this jump too long for a
            // near jump.
            v8_flags.debug_code ? Label::kFar : Label::kNear);

  if (inputs_ == StringEqualInputs::kStringsOrOddballs) {
    // This code is only used for strict equality. If either left or right is
    // not a string, then they must be non-equal (they cannot be the same
    // oddball, since that was checked above).
    __ JumpIfNotString(left, &if_not_equal);
    __ JumpIfNotString(right, &if_not_equal);
  }
  __ StringLength(left_length, left);
  __ StringLength(right_length, right);
  __ CompareInt32AndJumpIf(left_length, right_length, kNotEqual, &if_not_equal,
                           Label::Distance::kNear);

  // The inputs are already in the right registers. The |left| and |right|
  // inputs were required to come in in the left/right inputs of the builtin,
  // and the |length| input of the builtin is where we loaded the length of the
  // right string (which matches the length of the left string when we get
  // here).
  DCHECK_EQ(right_length, D::GetRegisterParameter(D::kLength));
  __ CallBuiltin<Builtin::kStringEqual>(lhs(), rhs(),
                                        D::GetRegisterParameter(D::kLength));
  masm->DefineLazyDeoptPoint(this->lazy_deopt_info());
  __ Jump(&done, Label::Distance::kNear);

  __ bind(&if_equal);
  __ LoadRoot(ToRegister(result()), RootIndex::kTrueValue);
  __ Jump(&done, Label::Distance::kNear);

  __ bind(&if_not_equal);
  __ LoadRoot(ToRegister(result()), RootIndex::kFalseValue);

  __ bind(&done);
}

void TaggedEqual::SetValueLocationConstraints() {
  UseRegister(lhs());
  UseRegister(rhs());
  DefineAsRegister(this);
}
void TaggedEqual::GenerateCode(MaglevAssembler* masm,
                               const ProcessingState& state) {
  Label done, if_equal;
  __ CmpTagged(ToRegister(lhs()), ToRegister(rhs()));
  __ JumpIf(kEqual, &if_equal, Label::Distance::kNear);
  __ LoadRoot(ToRegister(result()), RootIndex::kFalseValue);
  __ Jump(&done);
  __ bind(&if_equal);
  __ LoadRoot(ToRegister(result()), RootIndex::kTrueValue);
  __ bind(&done);
}

void TaggedNotEqual::SetValueLocationConstraints() {
  UseRegister(lhs());
  UseRegister(rhs());
  DefineAsRegister(this);
}
void TaggedNotEqual::GenerateCode(MaglevAssembler* masm,
                                  const ProcessingState& state) {
  Label done, if_equal;
  __ CmpTagged(ToRegister(lhs()), ToRegister(rhs()));
  __ JumpIf(kEqual, &if_equal, Label::Distance::kNear);
  __ LoadRoot(ToRegister(result()), RootIndex::kTrueValue);
  __ Jump(&done);
  __ bind(&if_equal);
  __ LoadRoot(ToRegister(result()), RootIndex::kFalseValue);
  __ bind(&done);
}

int TestInstanceOf::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kInstanceOf_WithFeedback>::type;
  return D::GetStackParameterCount();
}
void TestInstanceOf::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kInstanceOf_WithFeedback>::type;
  UseFixed(context(), kContextRegister);
  UseFixed(object(), D::GetRegisterParameter(D::kLeft));
  UseFixed(callable(), D::GetRegisterParameter(D::kRight));
  DefineAsFixed(this, kReturnRegister0);
}
void TestInstanceOf::GenerateCode(MaglevAssembler* masm,
                                  const ProcessingState& state) {
  __ CallBuiltin<Builtin::kInstanceOf_WithFeedback>(
      context(),           // context
      object(),            // left
      callable(),          // right
      feedback().index(),  // feedback slot
      feedback().vector    // feedback vector
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

void TestTypeOf::SetValueLocationConstraints() {
  UseRegister(value());
  DefineAsRegister(this);
#ifdef V8_TARGET_ARCH_ARM
  set_temporaries_needed(1);
#endif
}
void TestTypeOf::GenerateCode(MaglevAssembler* masm,
                              const ProcessingState& state) {
#ifdef V8_TARGET_ARCH_ARM
  // Arm32 needs one extra scratch register for TestTypeOf, so take a maglev
  // temporary and allow it to be used as a macro assembler scratch register.
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  temps.IncludeScratch(temps.Acquire());
#endif
  Register object = ToRegister(value());
  Label is_true, is_false, done;
  __ TestTypeOf(object, literal_, &is_true, Label::Distance::kNear, true,
                &is_false, Label::Distance::kNear, false);
  // Fallthrough into true.
  __ bind(&is_true);
  __ LoadRoot(ToRegister(result()), RootIndex::kTrueValue);
  __ Jump(&done, Label::Distance::kNear);
  __ bind(&is_false);
  __ LoadRoot(ToRegister(result()), RootIndex::kFalseValue);
  __ bind(&done);
}

void ToBoolean::SetValueLocationConstraints() {
  UseRegister(value());
  DefineAsRegister(this);
}
void ToBoolean::GenerateCode(MaglevAssembler* masm,
                             const ProcessingState& state) {
  Register object = ToRegister(value());
  Register return_value = ToRegister(result());
  Label done;
  ZoneLabelRef object_is_true(masm), object_is_false(masm);
  // TODO(leszeks): We're likely to be calling this on an existing boolean --
  // maybe that's a case we should fast-path here and reuse that boolean value?
  __ ToBoolean(object, check_type(), object_is_true, object_is_false, true);
  __ bind(*object_is_true);
  __ LoadRoot(return_value, RootIndex::kTrueValue);
  __ Jump(&done);
  __ bind(*object_is_false);
  __ LoadRoot(return_value, RootIndex::kFalseValue);
  __ bind(&done);
}

void ToBooleanLogicalNot::SetValueLocationConstraints() {
  UseRegister(value());
  DefineAsRegister(this);
}
void ToBooleanLogicalNot::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {
  Register object = ToRegister(value());
  Register return_value = ToRegister(result());
  Label done;
  ZoneLabelRef object_is_true(masm), object_is_false(masm);
  __ ToBoolean(object, check_type(), object_is_true, object_is_false, true);
  __ bind(*object_is_true);
  __ LoadRoot(return_value, RootIndex::kFalseValue);
  __ Jump(&done);
  __ bind(*object_is_false);
  __ LoadRoot(return_value, RootIndex::kTrueValue);
  __ bind(&done);
}

int ToName::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kToName>::type;
  return D::GetStackParameterCount();
}
void ToName::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kToName>::type;
  UseFixed(context(), kContextRegister);
  UseFixed(value_input(), D::GetRegisterParameter(D::kInput));
  DefineAsFixed(this, kReturnRegister0);
}
void ToName::GenerateCode(MaglevAssembler* masm, const ProcessingState& state) {
  __ CallBuiltin<Builtin::kToName>(context(),     // context
                                   value_input()  // input
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int ToNumberOrNumeric::MaxCallStackArgs() const {
  return TypeConversionDescriptor::GetStackParameterCount();
}
void ToNumberOrNumeric::SetValueLocationConstraints() {
  UseRegister(value_input());
  set_temporaries_needed(1);
  DefineAsRegister(this);
}
void ToNumberOrNumeric::GenerateCode(MaglevAssembler* masm,
                                     const ProcessingState& state) {
  ZoneLabelRef done(masm);
  Label move_and_return;
  Register object = ToRegister(value_input());
  Register result_reg = ToRegister(result());

  __ JumpIfSmi(object, &move_and_return, Label::kNear);
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  __ CompareMapWithRoot(object, RootIndex::kHeapNumberMap, scratch);
  __ JumpToDeferredIf(
      kNotEqual,
      [](MaglevAssembler* masm, Object::Conversion mode, Register object,
         Register result_reg, ToNumberOrNumeric* node, ZoneLabelRef done) {
        {
          RegisterSnapshot snapshot = node->register_snapshot();
          snapshot.live_registers.clear(result_reg);
          SaveRegisterStateForCall save_register_state(masm, snapshot);
          switch (mode) {
            case Object::Conversion::kToNumber:
              __ CallBuiltin<Builtin::kToNumber>(
                  masm->native_context().object(), object);
              break;
            case Object::Conversion::kToNumeric:
              __ CallBuiltin<Builtin::kToNumeric>(
                  masm->native_context().object(), object);
              break;
          }
          masm->DefineExceptionHandlerPoint(node);
          save_register_state.DefineSafepointWithLazyDeopt(
              node->lazy_deopt_info());
          __ Move(result_reg, kReturnRegister0);
        }
        __ Jump(*done);
      },
      mode(), object, result_reg, this, done);
  __ bind(&move_and_return);
  __ Move(result_reg, object);

  __ bind(*done);
}

int ToObject::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kToObject>::type;
  return D::GetStackParameterCount();
}
void ToObject::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kToObject>::type;
  UseFixed(context(), kContextRegister);
  UseFixed(value_input(), D::GetRegisterParameter(D::kInput));
  DefineAsFixed(this, kReturnRegister0);
}
void ToObject::GenerateCode(MaglevAssembler* masm,
                            const ProcessingState& state) {
  Register value = ToRegister(value_input());
  Label call_builtin, done;
  // Avoid the builtin call if {value} is a JSReceiver.
  if (check_type() == CheckType::kOmitHeapObjectCheck) {
    __ AssertNotSmi(value);
  } else {
    __ JumpIfSmi(value, &call_builtin, Label::Distance::kNear);
  }
  __ JumpIfJSAnyIsNotPrimitive(value, &done, Label::Distance::kNear);
  __ bind(&call_builtin);
  __ CallBuiltin<Builtin::kToObject>(context(),     // context
                                     value_input()  // input
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
  __ bind(&done);
}

int ToString::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kToString>::type;
  return D::GetStackParameterCount();
}
void ToString::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kToString>::type;
  UseFixed(context(), kContextRegister);
  UseFixed(value_input(), D::GetRegisterParameter(D::kO));
  DefineAsFixed(this, kReturnRegister0);
}
void ToString::GenerateCode(MaglevAssembler* masm,
                            const ProcessingState& state) {
  Register value = ToRegister(value_input());
  Label call_builtin, done;
  // Avoid the builtin call if {value} is a string.
  __ JumpIfSmi(value, &call_builtin, Label::Distance::kNear);
  __ JumpIfString(value, &done, Label::Distance::kNear);
  __ bind(&call_builtin);
  if (mode() == kConvertSymbol) {
    __ CallBuiltin<Builtin::kToStringConvertSymbol>(context(),       // context
                                                    value_input());  // input
  } else {
    __ CallBuiltin<Builtin::kToString>(context(),       // context
                                       value_input());  // input
  }
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
  __ bind(&done);
}

void NumberToString::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kNumberToString>::type;
  UseFixed(value_input(), D::GetRegisterParameter(D::kInput));
  DefineAsFixed(this, kReturnRegister0);
}
void NumberToString::GenerateCode(MaglevAssembler* masm,
                                  const ProcessingState& state) {
  __ CallBuiltin<Builtin::kNumberToString>(value_input());
  masm->DefineLazyDeoptPoint(this->lazy_deopt_info());
}

int ThrowReferenceErrorIfHole::MaxCallStackArgs() const { return 1; }
void ThrowReferenceErrorIfHole::SetValueLocationConstraints() {
  UseAny(value());
}
void ThrowReferenceErrorIfHole::GenerateCode(MaglevAssembler* masm,
                                             const ProcessingState& state) {
  __ JumpToDeferredIf(
      __ IsRootConstant(value(), RootIndex::kTheHoleValue),
      [](MaglevAssembler* masm, ThrowReferenceErrorIfHole* node) {
        __ Push(node->name().object());
        __ Move(kContextRegister, masm->native_context().object());
        __ CallRuntime(Runtime::kThrowAccessedUninitializedVariable, 1);
        masm->DefineExceptionHandlerAndLazyDeoptPoint(node);
        __ Abort(AbortReason::kUnexpectedReturnFromThrow);
      },
      this);
}

int ThrowSuperNotCalledIfHole::MaxCallStackArgs() const { return 0; }
void ThrowSuperNotCalledIfHole::SetValueLocationConstraints() {
  UseAny(value());
}
void ThrowSuperNotCalledIfHole::GenerateCode(MaglevAssembler* masm,
                                             const ProcessingState& state) {
  __ JumpToDeferredIf(
      __ IsRootConstant(value(), RootIndex::kTheHoleValue),
      [](MaglevAssembler* masm, ThrowSuperNotCalledIfHole* node) {
        __ Move(kContextRegister, masm->native_context().object());
        __ CallRuntime(Runtime::kThrowSuperNotCalled, 0);
        masm->DefineExceptionHandlerAndLazyDeoptPoint(node);
        __ Abort(AbortReason::kUnexpectedReturnFromThrow);
      },
      this);
}

int ThrowSuperAlreadyCalledIfNotHole::MaxCallStackArgs() const { return 0; }
void ThrowSuperAlreadyCalledIfNotHole::SetValueLocationConstraints() {
  UseAny(value());
}
void ThrowSuperAlreadyCalledIfNotHole::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  __ JumpToDeferredIf(
      NegateCondition(__ IsRootConstant(value(), RootIndex::kTheHoleValue)),
      [](MaglevAssembler* masm, ThrowSuperAlreadyCalledIfNotHole* node) {
        __ Move(kContextRegister, masm->native_context().object());
        __ CallRuntime(Runtime::kThrowSuperAlreadyCalledError, 0);
        masm->DefineExceptionHandlerAndLazyDeoptPoint(node);
        __ Abort(AbortReason::kUnexpectedReturnFromThrow);
      },
      this);
}

int ThrowIfNotCallable::MaxCallStackArgs() const { return 1; }
void ThrowIfNotCallable::SetValueLocationConstraints() {
  UseRegister(value());
  set_temporaries_needed(1);
}
void ThrowIfNotCallable::GenerateCode(MaglevAssembler* masm,
                                      const ProcessingState& state) {
  Label* if_not_callable = __ MakeDeferredCode(
      [](MaglevAssembler* masm, ThrowIfNotCallable* node) {
        __ Push(node->value());
        __ Move(kContextRegister, masm->native_context().object());
        __ CallRuntime(Runtime::kThrowCalledNonCallable, 1);
        masm->DefineExceptionHandlerAndLazyDeoptPoint(node);
        __ Abort(AbortReason::kUnexpectedReturnFromThrow);
      },
      this);

  Register value_reg = ToRegister(value());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  __ JumpIfNotCallable(value_reg, scratch, CheckType::kCheckHeapObject,
                       if_not_callable);
}

int ThrowIfNotSuperConstructor::MaxCallStackArgs() const { return 2; }
void ThrowIfNotSuperConstructor::SetValueLocationConstraints() {
  UseRegister(constructor());
  UseRegister(function());
  set_temporaries_needed(1);
}
void ThrowIfNotSuperConstructor::GenerateCode(MaglevAssembler* masm,
                                              const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  __ LoadMap(scratch, ToRegister(constructor()));
  static_assert(Map::kBitFieldOffsetEnd + 1 - Map::kBitFieldOffset == 1);
  __ TestUint8AndJumpIfAllClear(
      FieldMemOperand(scratch, Map::kBitFieldOffset),
      Map::Bits1::IsConstructorBit::kMask,
      __ MakeDeferredCode(
          [](MaglevAssembler* masm, ThrowIfNotSuperConstructor* node) {
            __ Push(ToRegister(node->constructor()),
                    ToRegister(node->function()));
            __ Move(kContextRegister, masm->native_context().object());
            __ CallRuntime(Runtime::kThrowNotSuperConstructor, 2);
            masm->DefineExceptionHandlerAndLazyDeoptPoint(node);
            __ Abort(AbortReason::kUnexpectedReturnFromThrow);
          },
          this));
}

void TruncateUint32ToInt32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}
void TruncateUint32ToInt32::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  // No code emitted -- as far as the machine is concerned, int32 is uint32.
  DCHECK_EQ(ToRegister(input()), ToRegister(result()));
}

void TruncateFloat64ToInt32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void TruncateFloat64ToInt32::GenerateCode(MaglevAssembler* masm,
                                          const ProcessingState& state) {
  __ TruncateDoubleToInt32(ToRegister(result()), ToDoubleRegister(input()));
}

void CheckedTruncateFloat64ToInt32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void CheckedTruncateFloat64ToInt32::GenerateCode(MaglevAssembler* masm,
                                                 const ProcessingState& state) {
  __ TryTruncateDoubleToInt32(
      ToRegister(result()), ToDoubleRegister(input()),
      __ GetDeoptLabel(this, DeoptimizeReason::kNotInt32));
}

void CheckedTruncateFloat64ToUint32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void CheckedTruncateFloat64ToUint32::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  __ TryTruncateDoubleToUint32(
      ToRegister(result()), ToDoubleRegister(input()),
      __ GetDeoptLabel(this, DeoptimizeReason::kNotUint32));
}

void UnsafeTruncateFloat64ToInt32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void UnsafeTruncateFloat64ToInt32::GenerateCode(MaglevAssembler* masm,
                                                const ProcessingState& state) {
#ifdef DEBUG
  Label fail, start;
  __ Jump(&start);
  __ bind(&fail);
  __ Abort(AbortReason::kFloat64IsNotAInt32);

  __ bind(&start);
  __ TryTruncateDoubleToInt32(ToRegister(result()), ToDoubleRegister(input()),
                              &fail);
#else
  // TODO(dmercadier): TruncateDoubleToInt32 does additional work when the
  // double doesn't fit in a 32-bit integer. This is not necessary for
  // UnsafeTruncateFloat64ToInt32 (since we statically know that it the double
  // fits in a 32-bit int) and could be instead just a Cvttsd2si (x64) or Fcvtzs
  // (arm64).
  __ TruncateDoubleToInt32(ToRegister(result()), ToDoubleRegister(input()));
#endif
}

void CheckedUint32ToInt32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}
void CheckedUint32ToInt32::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  Register input_reg = ToRegister(input());
  Label* fail = __ GetDeoptLabel(this, DeoptimizeReason::kNotInt32);
  __ CompareInt32AndJumpIf(input_reg, 0, kLessThan, fail);
}

void UnsafeTruncateUint32ToInt32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}
void UnsafeTruncateUint32ToInt32::GenerateCode(MaglevAssembler* masm,
                                               const ProcessingState& state) {
#ifdef DEBUG
  Register input_reg = ToRegister(input());
  __ CompareInt32AndAssert(input_reg, 0, kGreaterThanEqual,
                           AbortReason::kUint32IsNotAInt32);
#endif
  // No code emitted -- as far as the machine is concerned, int32 is uint32.
  DCHECK_EQ(ToRegister(input()), ToRegister(result()));
}

void Int32ToUint8Clamped::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}
void Int32ToUint8Clamped::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {
  Register value = ToRegister(input());
  Register result_reg = ToRegister(result());
  DCHECK_EQ(value, result_reg);
  Label min, done;
  __ CompareInt32AndJumpIf(value, 0, kLessThanEqual, &min);
  __ CompareInt32AndJumpIf(value, 255, kLessThanEqual, &done);
  __ Move(result_reg, 255);
  __ Jump(&done, Label::Distance::kNear);
  __ bind(&min);
  __ Move(result_reg, 0);
  __ bind(&done);
}

void Uint32ToUint8Clamped::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}
void Uint32ToUint8Clamped::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  Register value = ToRegister(input());
  DCHECK_EQ(value, ToRegister(result()));
  Label done;
  __ CompareInt32AndJumpIf(value, 255, kUnsignedLessThanEqual, &done,
                           Label::Distance::kNear);
  __ Move(value, 255);
  __ bind(&done);
}

void Float64ToUint8Clamped::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void Float64ToUint8Clamped::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  DoubleRegister value = ToDoubleRegister(input());
  Register result_reg = ToRegister(result());
  Label min, max, done;
  __ ToUint8Clamped(result_reg, value, &min, &max, &done);
  __ bind(&min);
  __ Move(result_reg, 0);
  __ Jump(&done, Label::Distance::kNear);
  __ bind(&max);
  __ Move(result_reg, 255);
  __ bind(&done);
}

void CheckNumber::SetValueLocationConstraints() {
  UseRegister(receiver_input());
}
void CheckNumber::GenerateCode(MaglevAssembler* masm,
                               const ProcessingState& state) {
  Label done;
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.AcquireScratch();
  Register value = ToRegister(receiver_input());
  // If {value} is a Smi or a HeapNumber, we're done.
  __ JumpIfSmi(
      value, &done,
      v8_flags.debug_code ? Label::Distance::kFar : Label::Distance::kNear);
  if (mode() == Object::Conversion::kToNumeric) {
    __ LoadMapForCompare(scratch, value);
    __ CompareTaggedRoot(scratch, RootIndex::kHeapNumberMap);
    // Jump to done if it is a HeapNumber.
    __ JumpIf(
        kEqual, &done,
        v8_flags.debug_code ? Label::Distance::kFar : Label::Distance::kNear);
    // Check if it is a BigInt.
    __ CompareTaggedRootAndEmitEagerDeoptIf(
        scratch, RootIndex::kBigIntMap, kNotEqual,
        DeoptimizeReason::kNotANumber, this);
  } else {
    __ CompareMapWithRootAndEmitEagerDeoptIf(
        value, RootIndex::kHeapNumberMap, scratch, kNotEqual,
        DeoptimizeReason::kNotANumber, this);
  }
  __ bind(&done);
}

void CheckedInternalizedString::SetValueLocationConstraints() {
  UseRegister(object_input());
  DefineSameAsFirst(this);
}
void CheckedInternalizedString::GenerateCode(MaglevAssembler* masm,
                                             const ProcessingState& state) {
  Register object = ToRegister(object_input());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register instance_type = temps.AcquireScratch();
  if (check_type() == CheckType::kOmitHeapObjectCheck) {
    __ AssertNotSmi(object);
  } else {
    __ EmitEagerDeoptIfSmi(this, object, DeoptimizeReason::kWrongMap);
  }
  __ LoadInstanceType(instance_type, object);
  __ RecordComment("Test IsInternalizedString");
  // Go to the slow path if this is a non-string, or a non-internalised string.
  static_assert((kStringTag | kInternalizedTag) == 0);
  ZoneLabelRef done(masm);
  __ TestInt32AndJumpIfAnySet(
      instance_type, kIsNotStringMask | kIsNotInternalizedMask,
      __ MakeDeferredCode(
          [](MaglevAssembler* masm, ZoneLabelRef done,
             CheckedInternalizedString* node, Register object,
             Register instance_type) {
            __ RecordComment("Deferred Test IsThinString");
            // Deopt if this isn't a string.
            __ TestInt32AndJumpIfAnySet(
                instance_type, kIsNotStringMask,
                __ GetDeoptLabel(node, DeoptimizeReason::kWrongMap));
            // Deopt if this isn't a thin string.
            static_assert(base::bits::CountPopulation(kThinStringTagBit) == 1);
            __ TestInt32AndJumpIfAllClear(
                instance_type, kThinStringTagBit,
                __ GetDeoptLabel(node, DeoptimizeReason::kWrongMap));
            // Load internalized string from thin string.
            __ LoadTaggedField(object, object, offsetof(ThinString, actual_));
            if (v8_flags.debug_code) {
              __ RecordComment("DCHECK IsInternalizedString");
              Label checked;
              __ LoadInstanceType(instance_type, object);
              __ TestInt32AndJumpIfAllClear(
                  instance_type, kIsNotStringMask | kIsNotInternalizedMask,
                  &checked);
              __ Abort(AbortReason::kUnexpectedValue);
              __ bind(&checked);
            }
            __ Jump(*done);
          },
          done, this, object, instance_type));
  __ bind(*done);
}

void CheckedNumberToUint8Clamped::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
  set_temporaries_needed(1);
  set_double_temporaries_needed(1);
}
void CheckedNumberToUint8Clamped::GenerateCode(MaglevAssembler* masm,
                                               const ProcessingState& state) {
  Register value = ToRegister(input());
  Register result_reg = ToRegister(result());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  DoubleRegister double_value = temps.AcquireDouble();
  Label is_not_smi, min, max, done;
  // Check if Smi.
  __ JumpIfNotSmi(value, &is_not_smi);
  // If Smi, convert to Int32.
  __ SmiToInt32(value);
  // Clamp.
  __ CompareInt32AndJumpIf(value, 0, kLessThanEqual, &min);
  __ CompareInt32AndJumpIf(value, 255, kGreaterThanEqual, &max);
  __ Jump(&done);
  __ bind(&is_not_smi);
  // Check if HeapNumber, deopt otherwise.
  __ CompareMapWithRootAndEmitEagerDeoptIf(value, RootIndex::kHeapNumberMap,
                                           scratch, kNotEqual,
                                           DeoptimizeReason::kNotANumber, this);
  // If heap number, get double value.
  __ LoadHeapNumberValue(double_value, value);
  // Clamp.
  __ ToUint8Clamped(value, double_value, &min, &max, &done);
  __ bind(&min);
  __ Move(result_reg, 0);
  __ Jump(&done, Label::Distance::kNear);
  __ bind(&max);
  __ Move(result_reg, 255);
  __ bind(&done);
}

void StoreFixedArrayElementWithWriteBarrier::SetValueLocationConstraints() {
  UseRegister(elements_input());
  UseRegister(index_input());
  UseRegister(value_input());
  RequireSpecificTemporary(WriteBarrierDescriptor::ObjectRegister());
  RequireSpecificTemporary(WriteBarrierDescriptor::SlotAddressRegister());
}
void StoreFixedArrayElementWithWriteBarrier::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  Register elements = ToRegister(elements_input());
  Register index = ToRegister(index_input());
  Register value = ToRegister(value_input());
  __ StoreFixedArrayElementWithWriteBarrier(elements, index, value,
                                            register_snapshot());
}

void StoreFixedArrayElementNoWriteBarrier::SetValueLocationConstraints() {
  UseRegister(elements_input());
  UseRegister(index_input());
  UseRegister(value_input());
}
void StoreFixedArrayElementNoWriteBarrier::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  Register elements = ToRegister(elements_input());
  Register index = ToRegister(index_input());
  Register value = ToRegister(value_input());
  __ StoreFixedArrayElementNoWriteBarrier(elements, index, value);
  __ AssertElidedWriteBarrier(elements, value, register_snapshot());
}

// ---
// Arch agnostic call nodes
// ---

int Call::MaxCallStackArgs() const { return num_args(); }
void Call::SetValueLocationConstraints() {
  using D = CallTrampolineDescriptor;
  UseFixed(function(), D::GetRegisterParameter(D::kFunction));
  UseAny(arg(0));
  for (int i = 1; i < num_args(); i++) {
    UseAny(arg(i));
  }
  UseFixed(context(), kContextRegister);
  DefineAsFixed(this, kReturnRegister0);
}

void Call::GenerateCode(MaglevAssembler* masm, const ProcessingState& state) {
  __ PushReverse(args());

  uint32_t arg_count = num_args();
  if (target_type_ == TargetType::kAny) {
    switch (receiver_mode_) {
      case ConvertReceiverMode::kNullOrUndefined:
        __ CallBuiltin<Builtin::kCall_ReceiverIsNullOrUndefined>(
            context(), function(), arg_count);
        break;
      case ConvertReceiverMode::kNotNullOrUndefined:
        __ CallBuiltin<Builtin::kCall_ReceiverIsNotNullOrUndefined>(
            context(), function(), arg_count);
        break;
      case ConvertReceiverMode::kAny:
        __ CallBuiltin<Builtin::kCall_ReceiverIsAny>(context(), function(),
                                                     arg_count);
        break;
    }
  } else {
    DCHECK_EQ(TargetType::kJSFunction, target_type_);
    switch (receiver_mode_) {
      case ConvertReceiverMode::kNullOrUndefined:
        __ CallBuiltin<Builtin::kCallFunction_ReceiverIsNullOrUndefined>(
            context(), function(), arg_count);
        break;
      case ConvertReceiverMode::kNotNullOrUndefined:
        __ CallBuiltin<Builtin::kCallFunction_ReceiverIsNotNullOrUndefined>(
            context(), function(), arg_count);
        break;
      case ConvertReceiverMode::kAny:
        __ CallBuiltin<Builtin::kCallFunction_ReceiverIsAny>(
            context(), function(), arg_count);
        break;
    }
  }

  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int CallForwardVarargs::MaxCallStackArgs() const { return num_args(); }
void CallForwardVarargs::SetValueLocationConstraints() {
  using D = CallTrampolineDescriptor;
  UseFixed(function(), D::GetRegisterParameter(D::kFunction));
  UseAny(arg(0));
  for (int i = 1; i < num_args(); i++) {
    UseAny(arg(i));
  }
  UseFixed(context(), kContextRegister);
  DefineAsFixed(this, kReturnRegister0);
}

void CallForwardVarargs::GenerateCode(MaglevAssembler* masm,
                                      const ProcessingState& state) {
  __ PushReverse(args());
  switch (target_type_) {
    case Call::TargetType::kJSFunction:
      __ CallBuiltin<Builtin::kCallFunctionForwardVarargs>(
          context(), function(), num_args(), start_index_);
      break;
    case Call::TargetType::kAny:
      __ CallBuiltin<Builtin::kCallForwardVarargs>(context(), function(),
                                                   num_args(), start_index_);
      break;
  }
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int CallSelf::MaxCallStackArgs() const {
  int actual_parameter_count = num_args() + 1;
  return std::max(expected_parameter_count_, actual_parameter_count);
}
void CallSelf::SetValueLocationConstraints() {
  UseAny(receiver());
  for (int i = 0; i < num_args(); i++) {
    UseAny(arg(i));
  }
  UseFixed(closure(), kJavaScriptCallTargetRegister);
  UseFixed(new_target(), kJavaScriptCallNewTargetRegister);
  UseFixed(context(), kContextRegister);
  DefineAsFixed(this, kReturnRegister0);
  set_temporaries_needed(1);
}

void CallSelf::GenerateCode(MaglevAssembler* masm,
                            const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  int actual_parameter_count = num_args() + 1;
  if (actual_parameter_count < expected_parameter_count_) {
    int number_of_undefineds =
        expected_parameter_count_ - actual_parameter_count;
    __ LoadRoot(scratch, RootIndex::kUndefinedValue);
    __ PushReverse(receiver(), args(),
                   RepeatValue(scratch, number_of_undefineds));
  } else {
    __ PushReverse(receiver(), args());
  }
  DCHECK_EQ(kContextRegister, ToRegister(context()));
  DCHECK_EQ(kJavaScriptCallTargetRegister, ToRegister(closure()));
  __ Move(kJavaScriptCallArgCountRegister, actual_parameter_count);
  __ CallSelf();
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int CallKnownJSFunction::MaxCallStackArgs() const {
  int actual_parameter_count = num_args() + 1;
  return std::max(expected_parameter_count_, actual_parameter_count);
}
void CallKnownJSFunction::SetValueLocationConstraints() {
  UseAny(receiver());
  for (int i = 0; i < num_args(); i++) {
    UseAny(arg(i));
  }
  UseFixed(closure(), kJavaScriptCallTargetRegister);
  UseFixed(new_target(), kJavaScriptCallNewTargetRegister);
  UseFixed(context(), kContextRegister);
  DefineAsFixed(this, kReturnRegister0);
  set_temporaries_needed(1);
}

void CallKnownJSFunction::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  int actual_parameter_count = num_args() + 1;
  if (actual_parameter_count < expected_parameter_count_) {
    int number_of_undefineds =
        expected_parameter_count_ - actual_parameter_count;
    __ LoadRoot(scratch, RootIndex::kUndefinedValue);
    __ PushReverse(receiver(), args(),
                   RepeatValue(scratch, number_of_undefineds));
  } else {
    __ PushReverse(receiver(), args());
  }
  // From here on, we're going to do a call, so all registers are valid temps,
  // except for the ones we're going to write. This is needed in case one of the
  // helper methods below wants to use a temp and one of these is in the temp
  // list (in particular, this can happen on arm64 where cp is a temp register
  // by default).
  temps.SetAvailable(MaglevAssembler::GetAllocatableRegisters() -
                     RegList{kContextRegister, kJavaScriptCallCodeStartRegister,
                             kJavaScriptCallTargetRegister,
                             kJavaScriptCallNewTargetRegister,
                             kJavaScriptCallArgCountRegister});
  DCHECK_EQ(kContextRegister, ToRegister(context()));
  DCHECK_EQ(kJavaScriptCallTargetRegister, ToRegister(closure()));
  __ Move(kJavaScriptCallArgCountRegister, actual_parameter_count);
  if (shared_function_info().HasBuiltinId()) {
    Builtin builtin = shared_function_info().builtin_id();

    // This SBXCHECK is a defense-in-depth measure to ensure that we always
    // generate valid calls here (with matching signatures).
    SBXCHECK_EQ(expected_parameter_count_,
                Builtins::GetFormalParameterCount(builtin));

    __ CallBuiltin(builtin);
  } else {
#if V8_ENABLE_LEAPTIERING
    __ CallJSDispatchEntry(dispatch_handle_, expected_parameter_count_);
#else
    __ CallJSFunction(kJavaScriptCallTargetRegister, expected_parameter_count_);
#endif
  }
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int CallKnownApiFunction::MaxCallStackArgs() const {
  int actual_parameter_count = num_args() + 1;
  return actual_parameter_count;
}

void CallKnownApiFunction::SetValueLocationConstraints() {
  UseAny(receiver());
  for (int i = 0; i < num_args(); i++) {
    UseAny(arg(i));
  }
  UseFixed(context(), kContextRegister);

  DefineAsFixed(this, kReturnRegister0);

  if (inline_builtin()) {
    set_temporaries_needed(2);
  }
}

void CallKnownApiFunction::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  __ PushReverse(receiver(), args());

  // From here on, we're going to do a call, so all registers are valid temps,
  // except for the ones we're going to write. This is needed in case one of the
  // helper methods below wants to use a temp and one of these is in the temp
  // list (in particular, this can happen on arm64 where cp is a temp register
  // by default).
  temps.SetAvailable(
      kAllocatableGeneralRegisters -
      RegList{
          kContextRegister,
          CallApiCallbackOptimizedDescriptor::ActualArgumentsCountRegister(),
          CallApiCallbackOptimizedDescriptor::FunctionTemplateInfoRegister(),
          CallApiCallbackOptimizedDescriptor::ApiFunctionAddressRegister()});
  DCHECK_EQ(kContextRegister, ToRegister(context()));

  if (inline_builtin()) {
    GenerateCallApiCallbackOptimizedInline(masm, state);
    return;
  }

  __ Move(CallApiCallbackOptimizedDescriptor::ActualArgumentsCountRegister(),
          num_args());  // not including receiver

  __ Move(CallApiCallbackOptimizedDescriptor::FunctionTemplateInfoRegister(),
          i::Cast<HeapObject>(function_template_info_.object()));

  compiler::JSHeapBroker* broker = masm->compilation_info()->broker();
  ApiFunction function(function_template_info_.callback(broker));
  ExternalReference reference =
      ExternalReference::Create(&function, ExternalReference::DIRECT_API_CALL);
  __ Move(CallApiCallbackOptimizedDescriptor::ApiFunctionAddressRegister(),
          reference);

  switch (mode()) {
    case kNoProfiling:
      __ CallBuiltin(Builtin::kCallApiCallbackOptimizedNoProfiling);
      break;
    case kNoProfilingInlined:
      UNREACHABLE();
    case kGeneric:
      __ CallBuiltin(Builtin::kCallApiCallbackOptimized);
      break;
  }
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

void CallKnownApiFunction::GenerateCallApiCallbackOptimizedInline(
    MaglevAssembler* masm, const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();

  using FCA = FunctionCallbackArguments;
  using ER = ExternalReference;
  using FC = ApiCallbackExitFrameConstants;

  static_assert(FCA::kArgsLength == 6);
  static_assert(FCA::kNewTargetIndex == 5);
  static_assert(FCA::kTargetIndex == 4);
  static_assert(FCA::kReturnValueIndex == 3);
  static_assert(FCA::kContextIndex == 2);
  static_assert(FCA::kIsolateIndex == 1);
  static_assert(FCA::kUnusedIndex == 0);

  // Set up FunctionCallbackInfo's implicit_args on the stack as follows:
  //
  // Target state:
  //   sp[0 * kSystemPointerSize]: kUnused  <= FCA::implicit_args_
  //   sp[1 * kSystemPointerSize]: kIsolate
  //   sp[2 * kSystemPointerSize]: kContext
  //   sp[3 * kSystemPointerSize]: undefined (kReturnValue)
  //   sp[4 * kSystemPointerSize]: kTarget
  //   sp[5 * kSystemPointerSize]: undefined (kNewTarget)
  // Existing state:
  //   sp[6 * kSystemPointerSize]:          <= FCA:::values_

  __ StoreRootRelative(IsolateData::topmost_script_having_context_offset(),
                       kContextRegister);

  ASM_CODE_COMMENT_STRING(masm, "inlined CallApiCallbackOptimized builtin");
  __ LoadRoot(scratch, RootIndex::kUndefinedValue);
  // kNewTarget, kTarget, kReturnValue, kContext
  __ Push(scratch, i::Cast<HeapObject>(function_template_info_.object()),
          scratch, kContextRegister);
  __ Move(scratch2, ER::isolate_address());
  // kIsolate, kUnused
  __ Push(scratch2, scratch);

  Register api_function_address =
      CallApiCallbackOptimizedDescriptor::ApiFunctionAddressRegister();

  compiler::JSHeapBroker* broker = masm->compilation_info()->broker();
  ApiFunction function(function_template_info_.callback(broker));
  ExternalReference reference =
      ExternalReference::Create(&function, ExternalReference::DIRECT_API_CALL);
  __ Move(api_function_address, reference);

  Label done, call_api_callback_builtin_inline;
  __ Call(&call_api_callback_builtin_inline);
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
  __ jmp(&done);

  //
  // Generate a CallApiCallback builtin inline.
  //
  __ bind(&call_api_callback_builtin_inline);

  FrameScope frame_scope(masm, StackFrame::MANUAL);
  __ EmitEnterExitFrame(FC::getExtraSlotsCountFrom<ExitFrameConstants>(),
                        StackFrame::API_CALLBACK_EXIT, api_function_address,
                        scratch2);

  Register fp = __ GetFramePointer();
#ifdef V8_TARGET_ARCH_ARM64
  // This is a workaround for performance regression observed on Apple Silicon
  // (https://crbug.com/347741609): reading argc value after the call via
  //   MemOperand argc_operand = MemOperand(fp, FC::kFCIArgcOffset);
  // is noticeably slower than using sp-based access:
  MemOperand argc_operand = ExitFrameStackSlotOperand(FCA::kLengthOffset);
#else
  // We don't enable this workaround for other configurations because
  // a) it's not possible to convert fp-based encoding to sp-based one:
  //    V8 guarantees stack pointer to be only kSystemPointerSize-aligned,
  //    while C function might require stack pointer to be 16-byte aligned on
  //    certain platforms,
  // b) local experiments on x64 didn't show improvements.
  MemOperand argc_operand = MemOperand(fp, FC::kFCIArgcOffset);
#endif  // V8_TARGET_ARCH_ARM64
  {
    ASM_CODE_COMMENT_STRING(masm, "Initialize v8::FunctionCallbackInfo");
    // FunctionCallbackInfo::length_.
    __ Move(scratch, num_args());  // not including receiver
    __ Move(argc_operand, scratch);

    // FunctionCallbackInfo::implicit_args_.
    __ LoadAddress(scratch, MemOperand(fp, FC::kImplicitArgsArrayOffset));
    __ Move(MemOperand(fp, FC::kFCIImplicitArgsOffset), scratch);

    // FunctionCallbackInfo::values_ (points at JS arguments on the stack).
    __ LoadAddress(scratch, MemOperand(fp, FC::kFirstArgumentOffset));
    __ Move(MemOperand(fp, FC::kFCIValuesOffset), scratch);
  }

  Register function_callback_info_arg = kCArgRegs[0];

  __ RecordComment("v8::FunctionCallback's argument.");
  __ LoadAddress(function_callback_info_arg,
                 MemOperand(fp, FC::kFunctionCallbackInfoOffset));

  DCHECK(!AreAliased(api_function_address, function_callback_info_arg));

  MemOperand return_value_operand = MemOperand(fp, FC::kReturnValueOffset);
  const int kSlotsToDropOnReturn =
      FC::kFunctionCallbackInfoArgsLength + kJSArgcReceiverSlots + num_args();

  const bool with_profiling = false;
  ExternalReference no_thunk_ref;
  Register no_thunk_arg = no_reg;

  CallApiFunctionAndReturn(masm, with_profiling, api_function_address,
                           no_thunk_ref, no_thunk_arg, kSlotsToDropOnReturn,
                           nullptr, return_value_operand);
  __ RecordComment("end of inlined CallApiCallbackOptimized builtin");

  __ bind(&done);
}

int CallBuiltin::MaxCallStackArgs() const {
  auto descriptor = Builtins::CallInterfaceDescriptorFor(builtin());
  if (!descriptor.AllowVarArgs()) {
    return descriptor.GetStackParameterCount();
  } else {
    int all_input_count = InputCountWithoutContext() + (has_feedback() ? 2 : 0);
    DCHECK_GE(all_input_count, descriptor.GetRegisterParameterCount());
    return all_input_count - descriptor.GetRegisterParameterCount();
  }
}

void CallBuiltin::SetValueLocationConstraints() {
  auto descriptor = Builtins::CallInterfaceDescriptorFor(builtin());
  bool has_context = descriptor.HasContextParameter();
  int i = 0;
  for (; i < InputsInRegisterCount(); i++) {
    UseFixed(input(i), descriptor.GetRegisterParameter(i));
  }
  for (; i < InputCountWithoutContext(); i++) {
    UseAny(input(i));
  }
  if (has_context) {
    UseFixed(input(i), kContextRegister);
  }
  DefineAsFixed(this, kReturnRegister0);
}

template <typename... Args>
void CallBuiltin::PushArguments(MaglevAssembler* masm, Args... extra_args) {
  auto descriptor = Builtins::CallInterfaceDescriptorFor(builtin());
  if (descriptor.GetStackArgumentOrder() == StackArgumentOrder::kDefault) {
    // In Default order we cannot have extra args (feedback).
    DCHECK_EQ(sizeof...(extra_args), 0);
    __ Push(stack_args());
  } else {
    DCHECK_EQ(descriptor.GetStackArgumentOrder(), StackArgumentOrder::kJS);
    __ PushReverse(extra_args..., stack_args());
  }
}

void CallBuiltin::PassFeedbackSlotInRegister(MaglevAssembler* masm) {
  DCHECK(has_feedback());
  auto descriptor = Builtins::CallInterfaceDescriptorFor(builtin());
  int slot_index = InputCountWithoutContext();
  switch (slot_type()) {
    case kTaggedIndex:
      __ Move(descriptor.GetRegisterParameter(slot_index),
              TaggedIndex::FromIntptr(feedback().index()));
      break;
    case kSmi:
      __ Move(descriptor.GetRegisterParameter(slot_index),
              Smi::FromInt(feedback().index()));
      break;
  }
}

void CallBuiltin::PushFeedbackAndArguments(MaglevAssembler* masm) {
  DCHECK(has_feedback());

  auto descriptor = Builtins::CallInterfaceDescriptorFor(builtin());
  int slot_index = InputCountWithoutContext();
  int vector_index = slot_index + 1;

  // There are three possibilities:
  // 1. Feedback slot and vector are in register.
  // 2. Feedback slot is in register and vector is on stack.
  // 3. Feedback slot and vector are on stack.
  if (vector_index < descriptor.GetRegisterParameterCount()) {
    PassFeedbackSlotInRegister(masm);
    __ Move(descriptor.GetRegisterParameter(vector_index), feedback().vector);
    PushArguments(masm);
  } else if (vector_index == descriptor.GetRegisterParameterCount()) {
    PassFeedbackSlotInRegister(masm);
    DCHECK_EQ(descriptor.GetStackArgumentOrder(), StackArgumentOrder::kJS);
    // Ensure that the builtin only expects the feedback vector on the stack and
    // potentional additional var args are passed through to another builtin.
    // This is required to align the stack correctly (e.g. on arm64).
    DCHECK_EQ(descriptor.GetStackParameterCount(), 1);
    PushArguments(masm);
    __ Push(feedback().vector);
  } else {
    int slot = feedback().index();
    Handle<FeedbackVector> vector = feedback().vector;
    switch (slot_type()) {
      case kTaggedIndex:
        PushArguments(masm, TaggedIndex::FromIntptr(slot), vector);
        break;
      case kSmi:
        PushArguments(masm, Smi::FromInt(slot), vector);
        break;
    }
  }
}

void CallBuiltin::GenerateCode(MaglevAssembler* masm,
                               const ProcessingState& state) {
  if (has_feedback()) {
    PushFeedbackAndArguments(masm);
  } else {
    PushArguments(masm);
  }
  __ CallBuiltin(builtin());
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int CallCPPBuiltin::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<kCEntry_Builtin>::type;
  return D::GetStackParameterCount() + num_args();
}

void CallCPPBuiltin::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<kCEntry_Builtin>::type;
  UseAny(target());
  UseAny(new_target());
  UseFixed(context(), kContextRegister);
  for (int i = 0; i < num_args(); i++) {
    UseAny(arg(i));
  }
  DefineAsFixed(this, kReturnRegister0);
  set_temporaries_needed(1);
  RequireSpecificTemporary(D::GetRegisterParameter(D::kArity));
  RequireSpecificTemporary(D::GetRegisterParameter(D::kCFunction));
}

void CallCPPBuiltin::GenerateCode(MaglevAssembler* masm,
                                  const ProcessingState& state) {
  using D = CallInterfaceDescriptorFor<kCEntry_Builtin>::type;
  constexpr Register kArityReg = D::GetRegisterParameter(D::kArity);
  constexpr Register kCFunctionReg = D::GetRegisterParameter(D::kCFunction);

  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  __ LoadRoot(scratch, RootIndex::kTheHoleValue);

  // Push all arguments to the builtin (including the receiver).
  static_assert(BuiltinArguments::kReceiverIndex == 4);
  __ PushReverse(args());

  static_assert(BuiltinArguments::kNumExtraArgs == 4);
  static_assert(BuiltinArguments::kNewTargetIndex == 0);
  static_assert(BuiltinArguments::kTargetIndex == 1);
  static_assert(BuiltinArguments::kArgcIndex == 2);
  static_assert(BuiltinArguments::kPaddingIndex == 3);
  // Push stack arguments for CEntry.
  Tagged<Smi> tagged_argc = Smi::FromInt(BuiltinArguments::kNumExtraArgs +
                                         num_args());  // Includes receiver.
  __ Push(scratch /* padding */, tagged_argc, target(), new_target());

  // Move values to fixed registers after all arguments are pushed. Registers
  // for arguments and CEntry registers might overlap.
  __ Move(kArityReg, BuiltinArguments::kNumExtraArgs + num_args());
  ExternalReference builtin_address =
      ExternalReference::Create(Builtins::CppEntryOf(builtin()));
  __ Move(kCFunctionReg, builtin_address);

  DCHECK_EQ(Builtins::CallInterfaceDescriptorFor(builtin()).GetReturnCount(),
            1);
  __ CallBuiltin(Builtin::kCEntry_Return1_ArgvOnStack_BuiltinExit);
}

int CallRuntime::MaxCallStackArgs() const { return num_args(); }
void CallRuntime::SetValueLocationConstraints() {
  UseFixed(context(), kContextRegister);
  for (int i = 0; i < num_args(); i++) {
    UseAny(arg(i));
  }
  DefineAsFixed(this, kReturnRegister0);
}
void CallRuntime::GenerateCode(MaglevAssembler* masm,
                               const ProcessingState& state) {
  DCHECK_EQ(ToRegister(context()), kContextRegister);
  __ Push(args());
  __ CallRuntime(function_id(), num_args());
  // TODO(victorgomes): Not sure if this is needed for all runtime calls.
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int CallWithSpread::MaxCallStackArgs() const {
  int argc_no_spread = num_args() - 1;
  using D = CallInterfaceDescriptorFor<Builtin::kCallWithSpread>::type;
  return argc_no_spread + D::GetStackParameterCount();
}
void CallWithSpread::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kCallWithSpread>::type;
  UseFixed(function(), D::GetRegisterParameter(D::kTarget));
  UseFixed(spread(), D::GetRegisterParameter(D::kSpread));
  UseFixed(context(), kContextRegister);
  for (int i = 0; i < num_args() - 1; i++) {
    UseAny(arg(i));
  }
  DefineAsFixed(this, kReturnRegister0);
}
void CallWithSpread::GenerateCode(MaglevAssembler* masm,
                                  const ProcessingState& state) {
  __ CallBuiltin<Builtin::kCallWithSpread>(
      context(),             // context
      function(),            // target
      num_args_no_spread(),  // arguments count
      spread(),              // spread
      args_no_spread()       // pushed args
  );

  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int CallWithArrayLike::MaxCallStackArgs() const {
  using D = CallInterfaceDescriptorFor<Builtin::kCallWithArrayLike>::type;
  return D::GetStackParameterCount();
}
void CallWithArrayLike::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<Builtin::kCallWithArrayLike>::type;
  UseFixed(function(), D::GetRegisterParameter(D::kTarget));
  UseAny(receiver());
  UseFixed(arguments_list(), D::GetRegisterParameter(D::kArgumentsList));
  UseFixed(context(), kContextRegister);
  DefineAsFixed(this, kReturnRegister0);
}
void CallWithArrayLike::GenerateCode(MaglevAssembler* masm,
                                     const ProcessingState& state) {
  // CallWithArrayLike is a weird builtin that expects a receiver as top of the
  // stack, but doesn't explicitly list it as an extra argument. Push it
  // manually, and assert that there are no other stack arguments.
  static_assert(
      CallInterfaceDescriptorFor<
          Builtin::kCallWithArrayLike>::type::GetStackParameterCount() == 0);
  __ Push(receiver());
  __ CallBuiltin<Builtin::kCallWithArrayLike>(
      context(),        // context
      function(),       // target
      arguments_list()  // arguments list
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

// ---
// Arch agnostic construct nodes
// ---

int Construct::MaxCallStackArgs() const {
  using D = Construct_WithFeedbackDescriptor;
  return num_args() + D::GetStackParameterCount();
}
void Construct::SetValueLocationConstraints() {
  using D = Construct_WithFeedbackDescriptor;
  UseFixed(function(), D::GetRegisterParameter(D::kTarget));
  UseFixed(new_target(), D::GetRegisterParameter(D::kNewTarget));
  UseFixed(context(), kContextRegister);
  for (int i = 0; i < num_args(); i++) {
    UseAny(arg(i));
  }
  DefineAsFixed(this, kReturnRegister0);
}
void Construct::GenerateCode(MaglevAssembler* masm,
                             const ProcessingState& state) {
  __ CallBuiltin<Builtin::kConstruct_WithFeedback>(
      context(),           // context
      function(),          // target
      new_target(),        // new target
      num_args(),          // actual arguments count
      feedback().index(),  // feedback slot
      feedback().vector,   // feedback vector
      args()               // args
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

int ConstructWithSpread::MaxCallStackArgs() const {
  int argc_no_spread = num_args() - 1;
  using D = CallInterfaceDescriptorFor<
      Builtin::kConstructWithSpread_WithFeedback>::type;
  return argc_no_spread + D::GetStackParameterCount();
}
void ConstructWithSpread::SetValueLocationConstraints() {
  using D = CallInterfaceDescriptorFor<
      Builtin::kConstructWithSpread_WithFeedback>::type;
  UseFixed(function(), D::GetRegisterParameter(D::kTarget));
  UseFixed(new_target(), D::GetRegisterParameter(D::kNewTarget));
  UseFixed(context(), kContextRegister);
  for (int i = 0; i < num_args() - 1; i++) {
    UseAny(arg(i));
  }
  UseFixed(spread(), D::GetRegisterParameter(D::kSpread));
  DefineAsFixed(this, kReturnRegister0);
}
void ConstructWithSpread::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {
  __ CallBuiltin<Builtin::kConstructWithSpread_WithFeedback>(
      context(),                                    // context
      function(),                                   // target
      new_target(),                                 // new target
      num_args_no_spread(),                         // actual arguments count
      spread(),                                     // spread
      TaggedIndex::FromIntptr(feedback().index()),  // feedback slot
      feedback().vector,                            // feedback vector
      args_no_spread()                              // args
  );
  masm->DefineExceptionHandlerAndLazyDeoptPoint(this);
}

void SetPendingMessage::SetValueLocationConstraints() {
  UseRegister(value());
  DefineAsRegister(this);
}

void SetPendingMessage::GenerateCode(MaglevAssembler* masm,
                                     const ProcessingState& state) {
  Register new_message = ToRegister(value());
  Register return_value = ToRegister(result());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.AcquireScratch();
  MemOperand pending_message_operand = __ ExternalReferenceAsOperand(
      ExternalReference::address_of_pending_message(masm->isolate()), scratch);
  if (new_message != return_value) {
    __ Move(return_value, pending_message_operand);
    __ Move(pending_message_operand, new_message);
  } else {
    __ Move(scratch, pending_message_operand);
    __ Move(pending_message_operand, new_message);
    __ Move(return_value, scratch);
  }
}

void StoreDoubleField::SetValueLocationConstraints() {
  UseRegister(object_input());
  UseRegister(value_input());
}
void StoreDoubleField::GenerateCode(MaglevAssembler* masm,
                                    const ProcessingState& state) {
  Register object = ToRegister(object_input());
  DoubleRegister value = ToDoubleRegister(value_input());

  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register heap_number = temps.AcquireScratch();

  __ AssertNotSmi(object);
  __ LoadTaggedField(heap_number, object, offset());
  __ AssertNotSmi(heap_number);
  __ StoreHeapNumberValue(value, heap_number);
}

namespace {

template <typename NodeT>
void GenerateTransitionElementsKind(
    MaglevAssembler* masm, NodeT* node, Register object, Register map,
    base::Vector<const compiler::MapRef> transition_sources,
    const compiler::MapRef transition_target, ZoneLabelRef done,
    std::optional<Register> result_opt) {
  DCHECK(!compiler::AnyMapIsHeapNumber(transition_sources));
  DCHECK(!IsHeapNumberMap(*transition_target.object()));

  for (const compiler::MapRef transition_source : transition_sources) {
    bool is_simple = IsSimpleMapChangeTransition(
        transition_source.elements_kind(), transition_target.elements_kind());

    // TODO(leszeks): If there are a lot of transition source maps, move the
    // source into a register and share the deferred code between maps.
    __ CompareTaggedAndJumpIf(
        map, transition_source.object(), kEqual,
        __ MakeDeferredCode(
            [](MaglevAssembler* masm, Register object, Register map,
               RegisterSnapshot register_snapshot,
               compiler::MapRef transition_target, bool is_simple,
               ZoneLabelRef done, std::optional<Register> result_opt) {
              if (is_simple) {
                __ MoveTagged(map, transition_target.object());
                __ StoreTaggedFieldWithWriteBarrier(
                    object, HeapObject::kMapOffset, map, register_snapshot,
                    MaglevAssembler::kValueIsCompressed,
                    MaglevAssembler::kValueCannotBeSmi);
              } else {
                SaveRegisterStateForCall save_state(masm, register_snapshot);
                __ Push(object, transition_target.object());
                __ Move(kContextRegister, masm->native_context().object());
                __ CallRuntime(Runtime::kTransitionElementsKind);
                save_state.DefineSafepoint();
              }
              if (result_opt) {
                __ MoveTagged(*result_opt, transition_target.object());
              }
              __ Jump(*done);
            },
            object, map, node->register_snapshot(), transition_target,
            is_simple, done, result_opt));
  }
}

}  // namespace

int TransitionElementsKind::MaxCallStackArgs() const {
  return std::max(WriteBarrierDescriptor::GetStackParameterCount(), 2);
}

void TransitionElementsKind::SetValueLocationConstraints() {
  UseRegister(object_input());
  UseRegister(map_input());
  DefineAsRegister(this);
}

void TransitionElementsKind::GenerateCode(MaglevAssembler* masm,
                                          const ProcessingState& state) {
  Register object = ToRegister(object_input());
  Register map = ToRegister(map_input());
  Register result_register = ToRegister(result());

  ZoneLabelRef done(masm);

  __ AssertNotSmi(object);
  GenerateTransitionElementsKind(masm, this, object, map,
                                 base::VectorOf(transition_sources_),
                                 transition_target_, done, result_register);
  // No transition happened, return the original map.
  __ Move(result_register, map);
  __ Jump(*done);
  __ bind(*done);
}

int TransitionElementsKindOrCheckMap::MaxCallStackArgs() const {
  return std::max(WriteBarrierDescriptor::GetStackParameterCount(), 2);
}

void TransitionElementsKindOrCheckMap::SetValueLocationConstraints() {
  UseRegister(object_input());
  UseRegister(map_input());
}

void TransitionElementsKindOrCheckMap::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  Register object = ToRegister(object_input());
  Register map = ToRegister(map_input());

  ZoneLabelRef done(masm);

  __ CompareTaggedAndJumpIf(map, transition_target_.object(), kEqual, *done);

  GenerateTransitionElementsKind(masm, this, object, map,
                                 base::VectorOf(transition_sources_),
                                 transition_target_, done, {});
  // If we didn't jump to 'done' yet, the transition failed.
  __ EmitEagerDeopt(this, DeoptimizeReason::kWrongMap);
  __ bind(*done);
}

void CheckTypedArrayNotDetached::SetValueLocationConstraints() {
  UseRegister(object_input());
  set_temporaries_needed(1);
}

void CheckTypedArrayNotDetached::GenerateCode(MaglevAssembler* masm,
                                              const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register object = ToRegister(object_input());
  Register scratch = temps.Acquire();
  __ DeoptIfBufferDetached(object, scratch, this);
}

void GetContinuationPreservedEmbedderData::SetValueLocationConstraints() {
  DefineAsRegister(this);
}

void GetContinuationPreservedEmbedderData::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  Register result = ToRegister(this->result());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  MemOperand reference = __ ExternalReferenceAsOperand(
      IsolateFieldId::kContinuationPreservedEmbedderData);
  __ Move(result, reference);
}

void SetContinuationPreservedEmbedderData::SetValueLocationConstraints() {
  UseRegister(data_input());
}

void SetContinuationPreservedEmbedderData::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  Register data = ToRegister(data_input());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  MemOperand reference = __ ExternalReferenceAsOperand(
      IsolateFieldId::kContinuationPreservedEmbedderData);
  __ Move(reference, data);
}

namespace {

template <typename ResultReg>
void GenerateTypedArrayLoadFromDataPointer(MaglevAssembler* masm,
                                           Register data_pointer,
                                           Register index, ResultReg result_reg,
                                           ElementsKind kind) {
  int element_size = ElementsKindToByteSize(kind);
  MemOperand operand =
      __ TypedArrayElementOperand(data_pointer, index, element_size);
  if constexpr (std::is_same_v<ResultReg, Register>) {
    if (IsSignedIntTypedArrayElementsKind(kind)) {
      __ LoadSignedField(result_reg, operand, element_size);
    } else {
      DCHECK(IsUnsignedIntTypedArrayElementsKind(kind));
      __ LoadUnsignedField(result_reg, operand, element_size);
    }
  } else {
#ifdef DEBUG
    bool result_reg_is_double = std::is_same_v<ResultReg, DoubleRegister>;
    DCHECK(result_reg_is_double);
    DCHECK(IsFloatTypedArrayElementsKind(kind));
#endif
    switch (kind) {
      case FLOAT32_ELEMENTS:
        __ LoadFloat32(result_reg, operand);
        break;
      case FLOAT64_ELEMENTS:
        __ LoadFloat64(result_reg, operand);
        break;
      default:
        UNREACHABLE();
    }
  }
}

template <typename ResultReg>
void GenerateTypedArrayLoad(MaglevAssembler* masm, Register object,
                            Register index, ResultReg result_reg,
                            ElementsKind kind) {
  __ AssertNotSmi(object);
  if (v8_flags.debug_code) {
    MaglevAssembler::TemporaryRegisterScope temps(masm);
    __ AssertObjectType(object, JS_TYPED_ARRAY_TYPE,
                        AbortReason::kUnexpectedValue);
  }

  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();

  Register data_pointer = scratch;
  // TODO(victorgomes): Consider hoisting array data pointer.
  __ BuildTypedArrayDataPointer(data_pointer, object);

  GenerateTypedArrayLoadFromDataPointer(masm, data_pointer, index, result_reg,
                                        kind);
}

template <typename ResultReg>
void GenerateConstantTypedArrayLoad(MaglevAssembler* masm,
                                    compiler::JSTypedArrayRef typed_array,
                                    Register index, ResultReg result_reg,
                                    ElementsKind kind) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register data_pointer = temps.Acquire();
  __ Move(data_pointer, reinterpret_cast<intptr_t>(typed_array.data_ptr()));

  GenerateTypedArrayLoadFromDataPointer(masm, data_pointer, index, result_reg,
                                        kind);
}

template <typename ValueReg>
void GenerateTypedArrayStoreToDataPointer(MaglevAssembler* masm,
                                          Register data_pointer, Register index,
                                          ValueReg value, ElementsKind kind) {
  int element_size = ElementsKindToByteSize(kind);
  MemOperand operand =
      __ TypedArrayElementOperand(data_pointer, index, element_size);
  if constexpr (std::is_same_v<ValueReg, Register>) {
    __ StoreField(operand, value, element_size);
  } else {
#ifdef DEBUG
    bool value_is_double = std::is_same_v<ValueReg, DoubleRegister>;
    DCHECK(value_is_double);
    DCHECK(IsFloatTypedArrayElementsKind(kind));
#endif
    switch (kind) {
      case FLOAT32_ELEMENTS:
        __ StoreFloat32(operand, value);
        break;
      case FLOAT64_ELEMENTS:
        __ StoreFloat64(operand, value);
        break;
      default:
        UNREACHABLE();
    }
  }
}

template <typename ValueReg>
void GenerateTypedArrayStore(MaglevAssembler* masm, Register object,
                             Register index, ValueReg value,
                             ElementsKind kind) {
  __ AssertNotSmi(object);
  if (v8_flags.debug_code) {
    MaglevAssembler::TemporaryRegisterScope temps(masm);
    __ AssertObjectType(object, JS_TYPED_ARRAY_TYPE,
                        AbortReason::kUnexpectedValue);
  }

  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();

  Register data_pointer = scratch;
  __ BuildTypedArrayDataPointer(data_pointer, object);

  GenerateTypedArrayStoreToDataPointer(masm, data_pointer, index, value, kind);
}

template <typename ValueReg>
void GenerateConstantTypedArrayStore(MaglevAssembler* masm,
                                     compiler::JSTypedArrayRef typed_array,
                                     Register index, ValueReg value,
                                     ElementsKind elements_kind) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register data_pointer = temps.Acquire();

  __ Move(data_pointer, reinterpret_cast<intptr_t>(typed_array.data_ptr()));
  GenerateTypedArrayStoreToDataPointer(masm, data_pointer, index, value,
                                       elements_kind);
}

}  // namespace

#define DEF_LOAD_TYPED_ARRAY(Name, ResultReg, ToResultReg)                   \
  void Name::SetValueLocationConstraints() {                                 \
    UseRegister(object_input());                                             \
    UseRegister(index_input());                                              \
    DefineAsRegister(this);                                                  \
    set_temporaries_needed(1);                                               \
  }                                                                          \
  void Name::GenerateCode(MaglevAssembler* masm,                             \
                          const ProcessingState& state) {                    \
    Register index = ToRegister(index_input());                              \
    ResultReg result_reg = ToResultReg(result());                            \
                                                                             \
    Register object = ToRegister(object_input());                            \
    GenerateTypedArrayLoad(masm, object, index, result_reg, elements_kind_); \
  }

DEF_LOAD_TYPED_ARRAY(LoadSignedIntTypedArrayElement, Register, ToRegister)

DEF_LOAD_TYPED_ARRAY(LoadUnsignedIntTypedArrayElement, Register, ToRegister)

DEF_LOAD_TYPED_ARRAY(LoadDoubleTypedArrayElement, DoubleRegister,
                     ToDoubleRegister)
#undef DEF_LOAD_TYPED_ARRAY

#define DEF_LOAD_CONSTANT_TYPED_ARRAY(Name, ResultReg, ToResultReg)        \
  void Name::SetValueLocationConstraints() {                               \
    UseRegister(index_input());                                            \
    DefineAsRegister(this);                                                \
    set_temporaries_needed(1);                                             \
  }                                                                        \
  void Name::GenerateCode(MaglevAssembler* masm,                           \
                          const ProcessingState& state) {                  \
    Register index = ToRegister(index_input());                            \
    ResultReg result_reg = ToResultReg(result());                          \
    GenerateConstantTypedArrayLoad(masm, typed_array(), index, result_reg, \
                                   elements_kind_);                        \
  }

DEF_LOAD_CONSTANT_TYPED_ARRAY(LoadSignedIntConstantTypedArrayElement, Register,
                              ToRegister)

DEF_LOAD_CONSTANT_TYPED_ARRAY(LoadUnsignedIntConstantTypedArrayElement,
                              Register, ToRegister)

DEF_LOAD_CONSTANT_TYPED_ARRAY(LoadDoubleConstantTypedArrayElement,
                              DoubleRegister, ToDoubleRegister)
#undef DEF_LOAD_CONSTANT_TYPED_ARRAY

#define DEF_STORE_TYPED_ARRAY(Name, ValueReg, ToValueReg)                \
  void Name::SetValueLocationConstraints() {                             \
    UseRegister(object_input());                                         \
    UseRegister(index_input());                                          \
    UseRegister(value_input());                                          \
    set_temporaries_needed(1);                                           \
  }                                                                      \
  void Name::GenerateCode(MaglevAssembler* masm,                         \
                          const ProcessingState& state) {                \
    Register index = ToRegister(index_input());                          \
    ValueReg value = ToValueReg(value_input());                          \
    Register object = ToRegister(object_input());                        \
    GenerateTypedArrayStore(masm, object, index, value, elements_kind_); \
  }

DEF_STORE_TYPED_ARRAY(StoreIntTypedArrayElement, Register, ToRegister)

DEF_STORE_TYPED_ARRAY(StoreDoubleTypedArrayElement, DoubleRegister,
                      ToDoubleRegister)
#undef DEF_STORE_TYPED_ARRAY

#define DEF_STORE_CONSTANT_TYPED_ARRAY(Name, ValueReg, ToValueReg)     \
  void Name::SetValueLocationConstraints() {                           \
    UseRegister(index_input());                                        \
    UseRegister(value_input());                                        \
    set_temporaries_needed(1);                                         \
  }                                                                    \
  void Name::GenerateCode(MaglevAssembler* masm,                       \
                          const ProcessingState& state) {              \
    Register index = ToRegister(index_input());                        \
    ValueReg value = ToValueReg(value_input());                        \
    GenerateConstantTypedArrayStore(masm, typed_array(), index, value, \
                                    elements_kind_);                   \
  }

DEF_STORE_CONSTANT_TYPED_ARRAY(StoreIntConstantTypedArrayElement, Register,
                               ToRegister)

DEF_STORE_CONSTANT_TYPED_ARRAY(StoreDoubleConstantTypedArrayElement,
                               DoubleRegister, ToDoubleRegister)
#undef DEF_STORE_CONSTANT_TYPED_ARRAY

// ---
// Arch agnostic control nodes
// ---

void Jump::SetValueLocationConstraints() {}
void Jump::GenerateCode(MaglevAssembler* masm, const ProcessingState& state) {
  // Avoid emitting a jump to the next block.
  if (target() != state.next_block()) {
    __ Jump(target()->label());
  }
}

void CheckpointedJump::SetValueLocationConstraints() {}
void CheckpointedJump::GenerateCode(MaglevAssembler* masm,
                                    const ProcessingState& state) {
  // Avoid emitting a jump to the next block.
  if (target() != state.next_block()) {
    __ Jump(target()->label());
  }
}

namespace {

void AttemptOnStackReplacement(MaglevAssembler* masm,
                               ZoneLabelRef no_code_for_osr,
                               TryOnStackReplacement* node, Register scratch0,
                               Register scratch1, int32_t loop_depth,
                               FeedbackSlot feedback_slot,
                               BytecodeOffset osr_offset) {
  // Two cases may cause us to attempt OSR, in the following order:
  //
  // 1) Presence of cached OSR Turbofan code.
  // 2) The OSR urgency exceeds the current loop depth - in that case, call
  //    into runtime to trigger a Turbofan OSR compilation. A non-zero return
  //    value means we should deopt into Ignition which will handle all further
  //    necessary steps (rewriting the stack frame, jumping to OSR'd code).
  //
  // See also: InterpreterAssembler::OnStackReplacement.

  __ AssertFeedbackVector(scratch0, scratch1);

  // Case 1).
  Label deopt;
  Register maybe_target_code = scratch1;
  __ TryLoadOptimizedOsrCode(scratch1, CodeKind::TURBOFAN_JS, scratch0,
                             feedback_slot, &deopt, Label::kFar);

  // Case 2).
  {
    __ LoadByte(scratch1,
                FieldMemOperand(scratch0, FeedbackVector::kOsrStateOffset));
    __ DecodeField<FeedbackVector::OsrUrgencyBits>(scratch1);
    __ JumpIfByte(kUnsignedLessThanEqual, scratch1, loop_depth,
                  *no_code_for_osr);

    // If tiering is already in progress wait.
    __ LoadByte(scratch1,
                FieldMemOperand(scratch0, FeedbackVector::kFlagsOffset));
    __ DecodeField<FeedbackVector::OsrTieringInProgressBit>(scratch1);
    __ JumpIfByte(kNotEqual, scratch1, 0, *no_code_for_osr);

    // The osr_urgency exceeds the current loop_depth, signaling an OSR
    // request. Call into runtime to compile.
    {
      RegisterSnapshot snapshot = node->register_snapshot();
      DCHECK(!snapshot.live_registers.has(maybe_target_code));
      SaveRegisterStateForCall save_register_state(masm, snapshot);
      DCHECK(!node->unit()->is_inline());
      __ Push(Smi::FromInt(osr_offset.ToInt()));
      __ Move(kContextRegister, masm->native_context().object());
      __ CallRuntime(Runtime::kCompileOptimizedOSRFromMaglev, 1);
      save_register_state.DefineSafepoint();
      __ Move(maybe_target_code, kReturnRegister0);
    }

    // A `0` return value means there is no OSR code available yet. Continue
    // execution in Maglev, OSR code will be picked up once it exists and is
    // cached on the feedback vector.
    __ CompareInt32AndJumpIf(maybe_target_code, 0, kEqual, *no_code_for_osr);
  }

  __ bind(&deopt);
  if (V8_LIKELY(v8_flags.turbofan)) {
    // None of the mutated input registers should be a register input into the
    // eager deopt info.
    DCHECK_REGLIST_EMPTY(
        RegList{scratch0, scratch1} &
        GetGeneralRegistersUsedAsInputs(node->eager_deopt_info()));
    __ EmitEagerDeopt(node, DeoptimizeReason::kPrepareForOnStackReplacement);
  } else {
    // Continue execution in Maglev. With TF disabled we cannot OSR and thus it
    // doesn't make sense to start the process. We do still perform all
    // remaining bookkeeping above though, to keep Maglev code behavior roughly
    // the same in both configurations.
    __ Jump(*no_code_for_osr);
  }
}

}  // namespace

int TryOnStackReplacement::MaxCallStackArgs() const {
  // For the kCompileOptimizedOSRFromMaglev call.
  if (unit()->is_inline()) return 2;
  return 1;
}
void TryOnStackReplacement::SetValueLocationConstraints() {
  UseAny(closure());
  set_temporaries_needed(2);
}
void TryOnStackReplacement::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch0 = temps.Acquire();
  Register scratch1 = temps.Acquire();

  const Register osr_state = scratch1;
  __ Move(scratch0, unit_->feedback().object());
  __ AssertFeedbackVector(scratch0, scratch1);
  __ LoadByte(osr_state,
              FieldMemOperand(scratch0, FeedbackVector::kOsrStateOffset));

  ZoneLabelRef no_code_for_osr(masm);

  if (v8_flags.maglev_osr) {
    // In case we use maglev_osr, we need to explicitly know if there is
    // turbofan code waiting for us (i.e., ignore the MaybeHasMaglevOsrCodeBit).
    __ DecodeField<
        base::BitFieldUnion<FeedbackVector::OsrUrgencyBits,
                            FeedbackVector::MaybeHasTurbofanOsrCodeBit>>(
        osr_state);
  }

  // The quick initial OSR check. If it passes, we proceed on to more
  // expensive OSR logic.
  static_assert(FeedbackVector::MaybeHasTurbofanOsrCodeBit::encode(true) >
                FeedbackVector::kMaxOsrUrgency);
  __ CompareInt32AndJumpIf(
      osr_state, loop_depth_, kUnsignedGreaterThan,
      __ MakeDeferredCode(AttemptOnStackReplacement, no_code_for_osr, this,
                          scratch0, scratch1, loop_depth_, feedback_slot_,
                          osr_offset_));
  __ bind(*no_code_for_osr);
}

void JumpLoop::SetValueLocationConstraints() {}
void JumpLoop::GenerateCode(MaglevAssembler* masm,
                            const ProcessingState& state) {
  __ Jump(target()->label());
}

void BranchIfSmi::SetValueLocationConstraints() {
  UseRegister(condition_input());
}
void BranchIfSmi::GenerateCode(MaglevAssembler* masm,
                               const ProcessingState& state) {
  __ Branch(__ CheckSmi(ToRegister(condition_input())), if_true(), if_false(),
            state.next_block());
}

void BranchIfRootConstant::SetValueLocationConstraints() {
  UseRegister(condition_input());
}
void BranchIfRootConstant::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  __ CompareRoot(ToRegister(condition_input()), root_index());
  __ Branch(ConditionFor(Operation::kEqual), if_true(), if_false(),
            state.next_block());
}

void BranchIfToBooleanTrue::SetValueLocationConstraints() {
  // TODO(victorgomes): consider using any input instead.
  UseRegister(condition_input());
}
void BranchIfToBooleanTrue::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  // BasicBlocks are zone allocated and so safe to be casted to ZoneLabelRef.
  ZoneLabelRef true_label =
      ZoneLabelRef::UnsafeFromLabelPointer(if_true()->label());
  ZoneLabelRef false_label =
      ZoneLabelRef::UnsafeFromLabelPointer(if_false()->label());
  bool fallthrough_when_true = (if_true() == state.next_block());
  __ ToBoolean(ToRegister(condition_input()), check_type(), true_label,
               false_label, fallthrough_when_true);
}

void BranchIfInt32ToBooleanTrue::SetValueLocationConstraints() {
  // TODO(victorgomes): consider using any input instead.
  UseRegister(condition_input());
}
void BranchIfInt32ToBooleanTrue::GenerateCode(MaglevAssembler* masm,
                                              const ProcessingState& state) {
  __ CompareInt32AndBranch(ToRegister(condition_input()), 0, kNotEqual,
                           if_true(), if_false(), state.next_block());
}

void BranchIfIntPtrToBooleanTrue::SetValueLocationConstraints() {
  // TODO(victorgomes): consider using any input instead.
  UseRegister(condition_input());
}
void BranchIfIntPtrToBooleanTrue::GenerateCode(MaglevAssembler* masm,
                                               const ProcessingState& state) {
  __ CompareIntPtrAndBranch(ToRegister(condition_input()), 0, kNotEqual,
                            if_true(), if_false(), state.next_block());
}

void BranchIfFloat64ToBooleanTrue::SetValueLocationConstraints() {
  UseRegister(condition_input());
  set_double_temporaries_needed(1);
}
void BranchIfFloat64ToBooleanTrue::GenerateCode(MaglevAssembler* masm,
                                                const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  DoubleRegister double_scratch = temps.AcquireDouble();

  __ Move(double_scratch, 0.0);
  __ CompareFloat64AndBranch(ToDoubleRegister(condition_input()),
                             double_scratch, kEqual, if_false(), if_true(),
                             state.next_block(), if_false());
}

#ifdef V8_ENABLE_EXPERIMENTAL_UNDEFINED_DOUBLE
void BranchIfFloat64IsUndefinedOrHole::SetValueLocationConstraints() {
  UseRegister(condition_input());
  set_temporaries_needed(1);
}
void BranchIfFloat64IsUndefinedOrHole::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  DoubleRegister input = ToDoubleRegister(condition_input());
  // See MaglevAssembler::Branch.
  bool fallthrough_when_true = if_true() == state.next_block();
  bool fallthrough_when_false = if_false() == state.next_block();
  if (fallthrough_when_false && fallthrough_when_true) {
    // If both paths are a fallthrough, do nothing.
    DCHECK_EQ(if_true(), if_false());
    return;
  }
  // TODO(nicohartmann): Consider providing a combined JumpIfUndefinedOrHoleNan.
  // Jump over the false block if true, otherwise fall through into it.
  __ JumpIfUndefinedNan(input, scratch, if_true()->label(), Label::kFar);
  if (fallthrough_when_false) {
    __ JumpIfHoleNan(input, scratch, if_true()->label(), Label::kFar);
  } else {
    // Jump to the false block if true.
    __ JumpIfNotHoleNan(input, scratch, if_false()->label(), Label::kFar);
    // Jump to the true block if it's not the next block.
    if (!fallthrough_when_true) {
      __ Jump(if_true()->label(), Label::kFar);
    }
  }
}
#endif  // V8_ENABLE_EXPERIMENTAL_UNDEFINED_DOUBLE

void BranchIfFloat64IsHole::SetValueLocationConstraints() {
  UseRegister(condition_input());
  set_temporaries_needed(1);
}
void BranchIfFloat64IsHole::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  DoubleRegister input = ToDoubleRegister(condition_input());
  // See MaglevAssembler::Branch.
  bool fallthrough_when_true = if_true() == state.next_block();
  bool fallthrough_when_false = if_false() == state.next_block();
  if (fallthrough_when_false) {
    if (fallthrough_when_true) {
      // If both paths are a fallthrough, do nothing.
      DCHECK_EQ(if_true(), if_false());
      return;
    }
    // Jump over the false block if true, otherwise fall through into it.
    __ JumpIfHoleNan(input, scratch, if_true()->label(), Label::kFar);
  } else {
    // Jump to the false block if true.
    __ JumpIfNotHoleNan(input, scratch, if_false()->label(), Label::kFar);
    // Jump to the true block if it's not the next block.
    if (!fallthrough_when_true) {
      __ Jump(if_true()->label(), Label::kFar);
    }
  }
}

#ifdef V8_ENABLE_EXPERIMENTAL_UNDEFINED_DOUBLE

void HoleyFloat64IsUndefinedOrHole::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
  set_temporaries_needed(1);
}
void HoleyFloat64IsUndefinedOrHole::GenerateCode(MaglevAssembler* masm,
                                                 const ProcessingState& state) {
  ZoneLabelRef if_undefined_or_hole(masm);
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  DoubleRegister value = ToDoubleRegister(input());
  Label done;
  // TODO(nicohartmann): Consider using a combined JumpIfUndefinedOrHoleNan.
  __ JumpIfUndefinedNan(value, scratch, *if_undefined_or_hole, Label::kNear);
  __ JumpIfHoleNan(value, scratch, *if_undefined_or_hole, Label::kNear);
  __ LoadRoot(ToRegister(result()), RootIndex::kFalseValue);
  __ Jump(&done);
  __ bind(*if_undefined_or_hole);
  __ LoadRoot(ToRegister(result()), RootIndex::kTrueValue);
  __ bind(&done);
}

#else

void HoleyFloat64IsHole::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
  set_temporaries_needed(1);
}
void HoleyFloat64IsHole::GenerateCode(MaglevAssembler* masm,
                                      const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  DoubleRegister value = ToDoubleRegister(input());
  Label done, if_not_hole;
  __ JumpIfNotHoleNan(value, scratch, &if_not_hole, Label::kNear);
  __ LoadRoot(ToRegister(result()), RootIndex::kTrueValue);
  __ Jump(&done);
  __ bind(&if_not_hole);
  __ LoadRoot(ToRegister(result()), RootIndex::kFalseValue);
  __ bind(&done);
}

#endif  // V8_ENABLE_EXPERIMENTAL_UNDEFINED_DOUBLE

void BranchIfFloat64Compare::SetValueLocationConstraints() {
  UseRegister(left_input());
  UseRegister(right_input());
}
void BranchIfFloat64Compare::GenerateCode(MaglevAssembler* masm,
                                          const ProcessingState& state) {
  DoubleRegister left = ToDoubleRegister(left_input());
  DoubleRegister right = ToDoubleRegister(right_input());
  __ CompareFloat64AndBranch(left, right, ConditionForFloat64(operation_),
                             if_true(), if_false(), state.next_block(),
                             if_false());
}

void BranchIfReferenceEqual::SetValueLocationConstraints() {
  UseRegister(left_input());
  UseRegister(right_input());
}
void BranchIfReferenceEqual::GenerateCode(MaglevAssembler* masm,
                                          const ProcessingState& state) {
  Register left = ToRegister(left_input());
  Register right = ToRegister(right_input());
  __ CmpTagged(left, right);
  __ Branch(kEqual, if_true(), if_false(), state.next_block());
}

void BranchIfInt32Compare::SetValueLocationConstraints() {
  UseRegister(left_input());
  UseRegister(right_input());
}
void BranchIfInt32Compare::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  Register left = ToRegister(left_input());
  Register right = ToRegister(right_input());
  __ CompareInt32AndBranch(left, right, ConditionFor(operation_), if_true(),
                           if_false(), state.next_block());
}

void BranchIfUint32Compare::SetValueLocationConstraints() {
  UseRegister(left_input());
  UseRegister(right_input());
}
void BranchIfUint32Compare::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  Register left = ToRegister(left_input());
  Register right = ToRegister(right_input());
  __ CompareInt32AndBranch(left, right, UnsignedConditionFor(operation_),
                           if_true(), if_false(), state.next_block());
}

void BranchIfUndefinedOrNull::SetValueLocationConstraints() {
  UseRegister(condition_input());
}
void BranchIfUndefinedOrNull::GenerateCode(MaglevAssembler* masm,
                                           const ProcessingState& state) {
  Register value = ToRegister(condition_input());
  __ JumpIfRoot(value, RootIndex::kUndefinedValue, if_true()->label());
  __ JumpIfRoot(value, RootIndex::kNullValue, if_true()->label());
  auto* next_block = state.next_block();
  if (if_false() != next_block) {
    __ Jump(if_false()->label());
  }
}

void BranchIfUndetectable::SetValueLocationConstraints() {
  UseRegister(condition_input());
  set_temporaries_needed(1);
}
void BranchIfUndetectable::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  Register value = ToRegister(condition_input());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();

  auto* next_block = state.next_block();
  if (next_block == if_true() || next_block != if_false()) {
    __ JumpIfNotUndetectable(value, scratch, check_type(), if_false()->label());
    if (next_block != if_true()) {
      __ Jump(if_true()->label());
    }
  } else {
    __ JumpIfUndetectable(value, scratch, check_type(), if_true()->label());
  }
}

void TestUndetectable::SetValueLocationConstraints() {
  UseRegister(value());
  set_temporaries_needed(1);
  DefineAsRegister(this);
}
void TestUndetectable::GenerateCode(MaglevAssembler* masm,
                                    const ProcessingState& state) {
  Register object = ToRegister(value());
  Register return_value = ToRegister(result());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();

  Label return_false, done;
  __ JumpIfNotUndetectable(object, scratch, check_type(), &return_false,
                           Label::kNear);

  __ LoadRoot(return_value, RootIndex::kTrueValue);
  __ Jump(&done, Label::kNear);

  __ bind(&return_false);
  __ LoadRoot(return_value, RootIndex::kFalseValue);

  __ bind(&done);
}

void BranchIfTypeOf::SetValueLocationConstraints() {
  UseRegister(value_input());
  // One temporary for TestTypeOf.
  set_temporaries_needed(1);
}
void BranchIfTypeOf::GenerateCode(MaglevAssembler* masm,
                                  const ProcessingState& state) {
  Register value = ToRegister(value_input());
  __ TestTypeOf(value, literal_, if_true()->label(), Label::kFar,
                if_true() == state.next_block(), if_false()->label(),
                Label::kFar, if_false() == state.next_block());
}

void BranchIfJSReceiver::SetValueLocationConstraints() {
  UseRegister(condition_input());
}
void BranchIfJSReceiver::GenerateCode(MaglevAssembler* masm,
                                      const ProcessingState& state) {
  Register value = ToRegister(condition_input());
  __ JumpIfSmi(value, if_false()->label());
  __ JumpIfJSAnyIsNotPrimitive(value, if_true()->label());
  __ jmp(if_false()->label());
}

void Switch::SetValueLocationConstraints() {
  UseAndClobberRegister(value());
  set_temporaries_needed(1);
}
void Switch::GenerateCode(MaglevAssembler* masm, const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  std::unique_ptr<Label*[]> labels = std::make_unique<Label*[]>(size());
  for (int i = 0; i < size(); i++) {
    BasicBlock* block = (targets())[i].block_ptr();
    block->set_start_block_of_switch_case(true);
    labels[i] = block->label();
  }
  Register val = ToRegister(value());
  // Switch requires {val} (the switch's condition) to be 64-bit, but maglev
  // usually manipulates/creates 32-bit integers. We thus sign-extend {val} to
  // 64-bit to have the correct value for negative numbers.
  __ SignExtend32To64Bits(val, val);
  __ Switch(scratch, val, value_base(), labels.get(), size());
  if (has_fallthrough()) {
    // If we jump-thread the fallthrough, it's not necessarily the next block.
    if (fallthrough() != state.next_block()) {
      __ Jump(fallthrough()->label());
    }
  } else {
    __ Trap();
  }
}

void HandleNoHeapWritesInterrupt::GenerateCode(MaglevAssembler* masm,
                                               const ProcessingState& state) {
  ZoneLabelRef done(masm);
  Label* deferred = __ MakeDeferredCode(
      [](MaglevAssembler* masm, ZoneLabelRef done, Node* node) {
        ASM_CODE_COMMENT_STRING(masm, "HandleNoHeapWritesInterrupt");
        {
          SaveRegisterStateForCall save_register_state(
              masm, node->register_snapshot());
          __ Move(kContextRegister, masm->native_context().object());
          __ CallRuntime(Runtime::kHandleNoHeapWritesInterrupts, 0);
          save_register_state.DefineSafepointWithLazyDeopt(
              node->lazy_deopt_info());
        }
        __ Jump(*done);
      },
      done, this);

  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.AcquireScratch();
  MemOperand check = __ ExternalReferenceAsOperand(
      ExternalReference::address_of_no_heap_write_interrupt_request(
          masm->isolate()),
      scratch);
  __ CompareByteAndJumpIf(check, 0, kNotEqual, scratch, deferred, Label::kFar);
  __ bind(*done);
}

#endif  // V8_ENABLE_MAGLEV

// ---
// Print params
// ---

void SmiConstant::PrintParams(std::ostream& os) const {
  os << "(" << value() << ")";
}

void TaggedIndexConstant::PrintParams(std::ostream& os) const {
  os << "(" << value() << ")";
}

void Int32Constant::PrintParams(std::ostream& os) const {
  os << "(" << value() << ")";
}

void Uint32Constant::PrintParams(std::ostream& os) const {
  os << "(" << value() << ")";
}

void IntPtrConstant::PrintParams(std::ostream& os) const {
  os << "(" << value() << ")";
}

void Float64Constant::PrintParams(std::ostream& os) const {
  if (value().is_nan()) {
    os << "(NaN [0x" << std::hex << value().get_bits() << std::dec << "]";
    if (value().is_hole_nan()) {
      os << ", the hole";
    } else if (value().get_bits() ==
               base::bit_cast<uint64_t>(
                   std::numeric_limits<double>::quiet_NaN())) {
      os << ", quiet NaN";
    }
    os << ")";

  } else {
    os << "(" << value().get_scalar() << ")";
  }
}

void Constant::PrintParams(std::ostream& os) const {
  os << "(" << *object_.object() << ")";
}

void TrustedConstant::PrintParams(std::ostream& os) const {
  os << "(" << *object_.object() << ")";
}

void DeleteProperty::PrintParams(std::ostream& os) const {
  os << "(" << LanguageMode2String(mode()) << ")";
}

void InitialValue::PrintParams(std::ostream& os) const {
  os << "(" << source().ToString() << ")";
}

void LoadGlobal::PrintParams(std::ostream& os) const {
  os << "(" << *name().object() << ")";
}

void StoreGlobal::PrintParams(std::ostream& os) const {
  os << "(" << *name().object() << ")";
}

void RegisterInput::PrintParams(std::ostream& os) const {
  os << "(" << input() << ")";
}

void RootConstant::PrintParams(std::ostream& os) const {
  os << "(" << RootsTable::name(index()) << ")";
}

void CreateFunctionContext::PrintParams(std::ostream& os) const {
  os << "(" << *scope_info().object() << ", " << slot_count() << ")";
}

void FastCreateClosure::PrintParams(std::ostream& os) const {
  os << "(" << *shared_function_info().object() << ", "
     << feedback_cell().object() << ")";
}

void CreateClosure::PrintParams(std::ostream& os) const {
  os << "(" << *shared_function_info().object() << ", "
     << feedback_cell().object();
  if (pretenured()) {
    os << " [pretenured]";
  }
  os << ")";
}

void AllocationBlock::PrintParams(std::ostream& os) const {
  os << "(" << allocation_type() << ")";
}

void InlinedAllocation::PrintParams(std::ostream& os) const {
  os << "(" << object()->type();
  if (object()->has_static_map()) {
    os << " " << *object()->map().object();
  }
  os << ")";
}

void VirtualObject::PrintParams(std::ostream& os) const {
  os << "(" << *map().object() << ")";
}

void Abort::PrintParams(std::ostream& os) const {
  os << "(" << GetAbortReason(reason()) << ")";
}

void AssertInt32::PrintParams(std::ostream& os) const {
  os << "(" << condition_ << ")";
}

void BuiltinStringPrototypeCharCodeOrCodePointAt::PrintParams(
    std::ostream& os) const {
  switch (mode()) {
    case BuiltinStringPrototypeCharCodeOrCodePointAt::kCharCodeAt:
      os << "(CharCodeAt)";
      break;
    case BuiltinStringPrototypeCharCodeOrCodePointAt::kCodePointAt:
      os << "(CodePointAt)";
      break;
  }
}

void CheckMaps::PrintParams(std::ostream& os) const {
  os << "(";
  bool first = true;
  for (compiler::MapRef map : maps()) {
    if (first) {
      first = false;
    } else {
      os << ", ";
    }
    os << *map.object();
  }
  os << ")";
}

void CheckMapsWithAlreadyLoadedMap::PrintParams(std::ostream& os) const {
  os << "(";
  bool first = true;
  for (compiler::MapRef map : maps()) {
    if (first) {
      first = false;
    } else {
      os << ", ";
    }
    os << *map.object();
  }
  os << ")";
}

void CheckMapsWithMigrationAndDeopt::PrintParams(std::ostream& os) const {
  os << "(";
  bool first = true;
  for (compiler::MapRef map : maps()) {
    if (first) {
      first = false;
    } else {
      os << ", ";
    }
    os << *map.object();
  }
  os << ")";
}

void TransitionElementsKindOrCheckMap::PrintParams(std::ostream& os) const {
  os << "(" << Node::input(0).node() << ", [";
  os << *transition_target().object();
  for (compiler::MapRef source : transition_sources()) {
    os << ", " << *source.object();
  }
  os << "]-->" << *transition_target().object() << ")";
}

void CheckDynamicValue::PrintParams(std::ostream& os) const {
  os << "(" << DeoptimizeReasonToString(deoptimize_reason()) << ")";
}

void CheckValue::PrintParams(std::ostream& os) const {
  os << "(" << *value().object() << ", "
     << DeoptimizeReasonToString(deoptimize_reason()) << ")";
}

void CheckValueEqualsInt32::PrintParams(std::ostream& os) const {
  os << "(" << value() << ", " << DeoptimizeReasonToString(deoptimize_reason())
     << ")";
}

void CheckFloat64SameValue::PrintParams(std::ostream& os) const {
  os << "(" << value().get_scalar() << ", "
     << DeoptimizeReasonToString(deoptimize_reason()) << ")";
}

void CheckValueEqualsString::PrintParams(std::ostream& os) const {
  os << "(" << *value().object() << ", "
     << DeoptimizeReasonToString(deoptimize_reason()) << ")";
}

void CheckInstanceType::PrintParams(std::ostream& os) const {
  os << "(" << first_instance_type_;
  if (first_instance_type_ != last_instance_type_) {
    os << " - " << last_instance_type_;
  }
  os << ")";
}

void CheckMapsWithMigration::PrintParams(std::ostream& os) const {
  os << "(";
  bool first = true;
  for (compiler::MapRef map : maps()) {
    if (first) {
      first = false;
    } else {
      os << ", ";
    }
    os << *map.object();
  }
  os << ")";
}

void CheckInt32Condition::PrintParams(std::ostream& os) const {
  os << "(" << condition() << ", " << deoptimize_reason() << ")";
}

void StoreContextSlotWithWriteBarrier::PrintParams(std::ostream& os) const {
  os << "(" << index_ << ")";
}

template <typename Derived, ValueRepresentation FloatType>
  requires(FloatType == ValueRepresentation::kFloat64 ||
           FloatType == ValueRepresentation::kHoleyFloat64)
void CheckedNumberOrOddballToFloat64OrHoleyFloat64<
    Derived, FloatType>::PrintParams(std::ostream& os) const {
  os << "(" << conversion_type() << ")";
}

void UncheckedNumberOrOddballToFloat64::PrintParams(std::ostream& os) const {
  os << "(" << conversion_type() << ")";
}

void CheckedTruncateNumberOrOddballToInt32::PrintParams(
    std::ostream& os) const {
  os << "(" << conversion_type() << ")";
}

void TruncateNumberOrOddballToInt32::PrintParams(std::ostream& os) const {
  os << "(" << conversion_type() << ")";
}

template <typename T>
void AbstractLoadTaggedField<T>::PrintParams(std::ostream& os) const {
  os << "(0x" << std::hex << offset() << std::dec;
  // TODO(victorgomes): Print compression status only after the result is
  // allocated, since that's when we do decompression marking.
  if (decompresses_tagged_result()) {
    os << ", decompressed";
  } else {
    os << ", compressed";
  }
  os << ")";
}

void LoadTaggedFieldForContextSlot::PrintParams(std::ostream& os) const {
  os << "(0x" << std::hex << offset() << std::dec << ")";
}

void LoadDoubleField::PrintParams(std::ostream& os) const {
  os << "(0x" << std::hex << offset() << std::dec << ")";
}

void LoadFloat64::PrintParams(std::ostream& os) const {
  os << "(0x" << std::hex << offset() << std::dec << ")";
}

void LoadInt32::PrintParams(std::ostream& os) const {
  os << "(0x" << std::hex << offset() << std::dec << ")";
}

void LoadFixedArrayElement::PrintParams(std::ostream& os) const {
  // TODO(victorgomes): Print compression status only after the result is
  // allocated, since that's when we do decompression marking.
  if (decompresses_tagged_result()) {
    os << "(decompressed)";
  } else {
    os << "(compressed)";
  }
}

void StoreDoubleField::PrintParams(std::ostream& os) const {
  os << "(0x" << std::hex << offset() << std::dec << ")";
}

void StoreFloat64::PrintParams(std::ostream& os) const {
  os << "(0x" << std::hex << offset() << std::dec << ")";
}

void StoreInt32::PrintParams(std::ostream& os) const {
  os << "(0x" << std::hex << offset() << std::dec << ")";
}

void StoreTaggedFieldNoWriteBarrier::PrintParams(std::ostream& os) const {
  os << "(0x" << std::hex << offset() << std::dec << ")";
}

std::ostream& operator<<(std::ostream& os, StoreMap::Kind kind) {
  switch (kind) {
    case StoreMap::Kind::kInitializing:
      os << "Initializing";
      break;
    case StoreMap::Kind::kInlinedAllocation:
      os << "InlinedAllocation";
      break;
    case StoreMap::Kind::kTransitioning:
      os << "Transitioning";
      break;
  }
  return os;
}

void StoreMap::PrintParams(std::ostream& os) const {
  os << "(" << *map_.object() << ", " << kind() << ")";
}

void StoreTaggedFieldWithWriteBarrier::PrintParams(std::ostream& os) const {
  os << "(0x" << std::hex << offset() << std::dec << ")";
}

void StoreTrustedPointerFieldWithWriteBarrier::PrintParams(
    std::ostream& os) const {
  os << "(0x" << std::hex << offset() << std::dec << ")";
}

void LoadNamedGeneric::PrintParams(std::ostream& os) const {
  os << "(" << *name_.object() << ")";
}

void LoadNamedFromSuperGeneric::PrintParams(std::ostream& os) const {
  os << "(" << *name_.object() << ")";
}

void SetNamedGeneric::PrintParams(std::ostream& os) const {
  os << "(" << *name_.object() << ")";
}

void DefineNamedOwnGeneric::PrintParams(std::ostream& os) const {
  os << "(" << *name_.object() << ")";
}

void HasInPrototypeChain::PrintParams(std::ostream& os) const {
  os << "(" << *prototype_.object() << ")";
}

void GapMove::PrintParams(std::ostream& os) const {
  os << "(" << source() << " → " << target() << ")";
}

void ConstantGapMove::PrintParams(std::ostream& os) const {
  os << "(";
  GetCurrentGraphLabeller()->PrintNodeLabel(os, node_, false);
  os << " → " << target() << ")";
}

void Float64Compare::PrintParams(std::ostream& os) const {
  os << "(" << operation() << ")";
}

void Float64ToBoolean::PrintParams(std::ostream& os) const {
  if (flip()) {
    os << "(flipped)";
  }
}

void Int32Compare::PrintParams(std::ostream& os) const {
  os << "(" << operation() << ")";
}

void Int32ToBoolean::PrintParams(std::ostream& os) const {
  if (flip()) {
    os << "(flipped)";
  }
}

void IntPtrToBoolean::PrintParams(std::ostream& os) const {
  if (flip()) {
    os << "(flipped)";
  }
}

void Float64Ieee754Unary::PrintParams(std::ostream& os) const {
  switch (ieee_function_) {
#define CASE(MathName, ExtName, EnumName) \
  case Ieee754Function::k##EnumName:      \
    os << "(" << #EnumName << ")";        \
    break;
    IEEE_754_UNARY_LIST(CASE)
#undef CASE
  }
}

void Float64Ieee754Binary::PrintParams(std::ostream& os) const {
  switch (ieee_function_) {
#define CASE(MathName, ExtName, EnumName) \
  case Ieee754Function::k##EnumName:      \
    os << "(" << #EnumName << ")";        \
    break;
    IEEE_754_BINARY_LIST(CASE)
#undef CASE
  }
}

void Float64Sqrt::PrintParams(std::ostream& os) const { os << "(MathSqrt)"; }

void Float64Round::PrintParams(std::ostream& os) const {
  switch (kind_) {
    case Kind::kCeil:
      os << "(ceil)";
      return;
    case Kind::kFloor:
      os << "(floor)";
      return;
    case Kind::kNearest:
      os << "(nearest)";
      return;
  }
}

void Phi::PrintParams(std::ostream& os) const {
  os << "(" << (owner().is_valid() ? owner().ToString() : "VO") << ")";
}

void Call::PrintParams(std::ostream& os) const {
  os << "(" << receiver_mode_ << ", ";
  switch (target_type_) {
    case TargetType::kJSFunction:
      os << "JSFunction";
      break;
    case TargetType::kAny:
      os << "Any";
      break;
  }
  os << ")";
}

void CallSelf::PrintParams(std::ostream& os) const {}

void CallKnownJSFunction::PrintParams(std::ostream& os) const {
  os << "(" << shared_function_info_.object() << ", " << uses_repr_hint_ << ")";
}

void CallKnownApiFunction::PrintParams(std::ostream& os) const {
  os << "(";
  switch (mode()) {
    case kNoProfiling:
      os << "no profiling, ";
      break;
    case kNoProfilingInlined:
      os << "no profiling inlined, ";
      break;
    case kGeneric:
      break;
  }
  os << function_template_info_.object() << ")";
}

void CallBuiltin::PrintParams(std::ostream& os) const {
  os << "(" << Builtins::name(builtin()) << ")";
}

void CallCPPBuiltin::PrintParams(std::ostream& os) const {
  os << "(" << Builtins::name(builtin()) << ")";
}

void CallForwardVarargs::PrintParams(std::ostream& os) const {
  if (start_index_ == 0) return;
  os << "(" << start_index_ << ")";
}

void CallRuntime::PrintParams(std::ostream& os) const {
  os << "(" << Runtime::FunctionForId(function_id())->name << ")";
}

void TestTypeOf::PrintParams(std::ostream& os) const {
  os << "(" << interpreter::TestTypeOfFlags::ToString(literal_) << ")";
}

void ReduceInterruptBudgetForLoop::PrintParams(std::ostream& os) const {
  os << "(" << amount() << ")";
}

void ReduceInterruptBudgetForReturn::PrintParams(std::ostream& os) const {
  os << "(" << amount() << ")";
}

void Deopt::PrintParams(std::ostream& os) const {
  os << "(" << DeoptimizeReasonToString(deoptimize_reason()) << ")";
}

void BranchIfRootConstant::PrintParams(std::ostream& os) const {
  os << "(" << RootsTable::name(root_index_) << ")";
}

void BranchIfFloat64Compare::PrintParams(std::ostream& os) const {
  os << "(" << operation_ << ")";
}

void BranchIfInt32Compare::PrintParams(std::ostream& os) const {
  os << "(" << operation_ << ")";
}

void BranchIfUint32Compare::PrintParams(std::ostream& os) const {
  os << "(" << operation_ << ")";
}

void BranchIfTypeOf::PrintParams(std::ostream& os) const {
  os << "(" << interpreter::TestTypeOfFlags::ToString(literal_) << ")";
}

void ExtendPropertiesBackingStore::PrintParams(std::ostream& os) const {
  os << "(" << old_length_ << ")";
}

// Keeping track of the effects this instruction has on known node aspects.
void NodeBase::ClearElementsProperties(bool is_tracing_enabled,
                                       KnownNodeAspects& known_node_aspects) {
  DCHECK(IsElementsArrayWrite(opcode()));
  // Clear Elements cache.
  auto elements_properties = known_node_aspects.loaded_properties.find(
      KnownNodeAspects::LoadedPropertyMapKey::Elements());
  if (elements_properties != known_node_aspects.loaded_properties.end()) {
    elements_properties->second.clear();
    if (V8_UNLIKELY(v8_flags.trace_maglev_graph_building &&
                    is_tracing_enabled)) {
      std::cout << "  * Removing non-constant cached [Elements]";
    }
  }
}

void NodeBase::ClearUnstableNodeAspects(bool is_tracing_enabled,
                                        KnownNodeAspects& known_node_aspects) {
  DCHECK(properties().can_write());
  DCHECK(!IsSimpleFieldStore(opcode()));
  DCHECK(!IsElementsArrayWrite(opcode()));
  known_node_aspects.ClearUnstableNodeAspects(is_tracing_enabled);
}

void StoreMap::ClearUnstableNodeAspects(bool is_tracing_enabled,
                                        KnownNodeAspects& known_node_aspects) {
  switch (kind()) {
    case Kind::kInitializing:
    case Kind::kInlinedAllocation:
      return;
    case Kind::kTransitioning: {
      if (NodeInfo* node_info =
              known_node_aspects.TryGetInfoFor(object_input().node())) {
        if (node_info->possible_maps_are_known() &&
            node_info->possible_maps().size() == 1) {
          compiler::MapRef old_map = node_info->possible_maps().at(0);
          auto MaybeAliases = [&](compiler::MapRef map) -> bool {
            return map.equals(old_map);
          };
          known_node_aspects.ClearUnstableMapsIfAny(MaybeAliases);
          if (V8_UNLIKELY(v8_flags.trace_maglev_graph_building &&
                          is_tracing_enabled)) {
            std::cout << "  ! StoreMap: Clearing unstable map "
                      << Brief(*old_map.object()) << std::endl;
          }
          return;
        }
      }
      break;
    }
  }
  // TODO(olivf): Only invalidate nodes with the same type.
  known_node_aspects.ClearUnstableMaps();
  if (V8_UNLIKELY(v8_flags.trace_maglev_graph_building && is_tracing_enabled)) {
    std::cout << "  ! StoreMap: Clearing unstable maps" << std::endl;
  }
}

void CheckMapsWithMigration::ClearUnstableNodeAspects(
    bool is_tracing_enabled, KnownNodeAspects& known_node_aspects) {
  // This instruction only migrates representations of values, not the values
  // themselves, so cached values are still valid.
}

void MigrateMapIfNeeded::ClearUnstableNodeAspects(
    bool is_tracing_enabled, KnownNodeAspects& known_node_aspects) {
  // This instruction only migrates representations of values, not the values
  // themselves, so cached values are still valid.
}

template class AbstractLoadTaggedField<LoadTaggedField>;
template class AbstractLoadTaggedField<LoadTaggedFieldForContextSlotNoCells>;
template class AbstractLoadTaggedField<LoadTaggedFieldForProperty>;

template class CheckedNumberOrOddballToFloat64OrHoleyFloat64<
    CheckedNumberOrOddballToFloat64, ValueRepresentation::kFloat64>;
template class CheckedNumberOrOddballToFloat64OrHoleyFloat64<
    CheckedNumberOrOddballToHoleyFloat64, ValueRepresentation::kHoleyFloat64>;

std::optional<int32_t> NodeBase::TryGetInt32ConstantInput(int index) {
  Node* node = input(index).node();
  if (auto smi = node->TryCast<SmiConstant>()) {
    return smi->value().value();
  }
  if (auto i32 = node->TryCast<Int32Constant>()) {
    return i32->value();
  }
  return {};
}

RangeType ValueNode::GetRange() const {
  // TODO(victorgomes): Support other constant nodes.
  if (Is<Float64Constant>()) {
    double value = Cast<Float64Constant>()->value().get_scalar();
    if (!IsSafeInteger(value)) return {};
    return RangeType(value);
  }
  if (properties().is_conversion()) {
    return input(0).node()->GetRange();
  }
  switch (value_representation()) {
    case ValueRepresentation::kInt32:
      // TODO(victorgomes): we could be more precise for Int32 operations.
      return RangeType(INT32_MIN, INT32_MAX);
    case ValueRepresentation::kFloat64:
      switch (opcode()) {
        case Opcode::kFloat64Add:
          return Cast<Float64Add>()->range();
        case Opcode::kFloat64Subtract:
          return Cast<Float64Subtract>()->range();
        case Opcode::kFloat64Multiply:
          return Cast<Float64Multiply>()->range();
        case Opcode::kFloat64Divide:
          return Cast<Float64Divide>()->range();
        default:
          return {};
      }
    default:
      return {};
  }
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
