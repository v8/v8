// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-create-lowering.h"

#include "src/code-factory.h"
#include "src/compilation-dependencies.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/linkage.h"
#include "src/compiler/node.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/state-values-utils.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

// A helper class to construct inline allocations on the simplified operator
// level. This keeps track of the effect chain for initial stores on a newly
// allocated object and also provides helpers for commonly allocated objects.
class AllocationBuilder final {
 public:
  AllocationBuilder(JSGraph* jsgraph, Node* effect, Node* control)
      : jsgraph_(jsgraph),
        allocation_(nullptr),
        effect_(effect),
        control_(control) {}

  // Primitive allocation of static size.
  void Allocate(int size, PretenureFlag pretenure = NOT_TENURED) {
    effect_ = graph()->NewNode(common()->BeginRegion(), effect_);
    allocation_ =
        graph()->NewNode(simplified()->Allocate(pretenure),
                         jsgraph()->Constant(size), effect_, control_);
    effect_ = allocation_;
  }

  // Primitive store into a field.
  void Store(const FieldAccess& access, Node* value) {
    effect_ = graph()->NewNode(simplified()->StoreField(access), allocation_,
                               value, effect_, control_);
  }

  // Primitive store into an element.
  void Store(ElementAccess const& access, Node* index, Node* value) {
    effect_ = graph()->NewNode(simplified()->StoreElement(access), allocation_,
                               index, value, effect_, control_);
  }

  // Compound allocation of a FixedArray.
  void AllocateArray(int length, Handle<Map> map,
                     PretenureFlag pretenure = NOT_TENURED) {
    DCHECK(map->instance_type() == FIXED_ARRAY_TYPE ||
           map->instance_type() == FIXED_DOUBLE_ARRAY_TYPE);
    int size = (map->instance_type() == FIXED_ARRAY_TYPE)
                   ? FixedArray::SizeFor(length)
                   : FixedDoubleArray::SizeFor(length);
    Allocate(size, pretenure);
    Store(AccessBuilder::ForMap(), map);
    Store(AccessBuilder::ForFixedArrayLength(), jsgraph()->Constant(length));
  }

  // Compound store of a constant into a field.
  void Store(const FieldAccess& access, Handle<Object> value) {
    Store(access, jsgraph()->Constant(value));
  }

  void FinishAndChange(Node* node) {
    NodeProperties::SetType(allocation_, NodeProperties::GetType(node));
    node->ReplaceInput(0, allocation_);
    node->ReplaceInput(1, effect_);
    node->TrimInputCount(2);
    NodeProperties::ChangeOp(node, common()->FinishRegion());
  }

  Node* Finish() {
    return graph()->NewNode(common()->FinishRegion(), allocation_, effect_);
  }

 protected:
  JSGraph* jsgraph() { return jsgraph_; }
  Graph* graph() { return jsgraph_->graph(); }
  CommonOperatorBuilder* common() { return jsgraph_->common(); }
  SimplifiedOperatorBuilder* simplified() { return jsgraph_->simplified(); }

 private:
  JSGraph* const jsgraph_;
  Node* allocation_;
  Node* effect_;
  Node* control_;
};

// Retrieves the frame state holding actual argument values.
Node* GetArgumentsFrameState(Node* frame_state) {
  Node* const outer_state = NodeProperties::GetFrameStateInput(frame_state, 0);
  FrameStateInfo outer_state_info = OpParameter<FrameStateInfo>(outer_state);
  return outer_state_info.type() == FrameStateType::kArgumentsAdaptor
             ? outer_state
             : frame_state;
}

// Maximum instance size for which allocations will be inlined.
const int kMaxInlineInstanceSize = 64 * kPointerSize;

// Checks whether allocation using the given constructor can be inlined.
bool IsAllocationInlineable(Handle<JSFunction> constructor) {
  // TODO(bmeurer): Further relax restrictions on inlining, i.e.
  // instance type and maybe instance size (inobject properties
  // are limited anyways by the runtime).
  return constructor->has_initial_map() &&
         constructor->initial_map()->instance_type() == JS_OBJECT_TYPE &&
         constructor->initial_map()->instance_size() < kMaxInlineInstanceSize;
}

// When initializing arrays, we'll unfold the loop if the number of
// elements is known to be of this type.
const int kElementLoopUnrollLimit = 16;

// Limits up to which context allocations are inlined.
const int kFunctionContextAllocationLimit = 16;
const int kBlockContextAllocationLimit = 16;

}  // namespace

Reduction JSCreateLowering::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kJSCreate:
      return ReduceJSCreate(node);
    case IrOpcode::kJSCreateArguments:
      return ReduceJSCreateArguments(node);
    case IrOpcode::kJSCreateArray:
      return ReduceJSCreateArray(node);
    case IrOpcode::kJSCreateIterResultObject:
      return ReduceJSCreateIterResultObject(node);
    case IrOpcode::kJSCreateFunctionContext:
      return ReduceJSCreateFunctionContext(node);
    case IrOpcode::kJSCreateWithContext:
      return ReduceJSCreateWithContext(node);
    case IrOpcode::kJSCreateCatchContext:
      return ReduceJSCreateCatchContext(node);
    case IrOpcode::kJSCreateBlockContext:
      return ReduceJSCreateBlockContext(node);
    default:
      break;
  }
  return NoChange();
}

Reduction JSCreateLowering::ReduceJSCreate(Node* node) {
  DCHECK_EQ(IrOpcode::kJSCreate, node->opcode());
  Node* const target = NodeProperties::GetValueInput(node, 0);
  Type* const target_type = NodeProperties::GetType(target);
  Node* const new_target = NodeProperties::GetValueInput(node, 1);
  Node* const effect = NodeProperties::GetEffectInput(node);
  // TODO(turbofan): Add support for NewTarget passed to JSCreate.
  if (target != new_target) return NoChange();
  // Extract constructor function.
  if (target_type->IsConstant() &&
      target_type->AsConstant()->Value()->IsJSFunction()) {
    Handle<JSFunction> constructor =
        Handle<JSFunction>::cast(target_type->AsConstant()->Value());
    DCHECK(constructor->IsConstructor());
    // Force completion of inobject slack tracking before
    // generating code to finalize the instance size.
    constructor->CompleteInobjectSlackTrackingIfActive();

    // TODO(bmeurer): We fall back to the runtime in case we cannot inline
    // the allocation here, which is sort of expensive. We should think about
    // a soft fallback to some NewObjectCodeStub.
    if (IsAllocationInlineable(constructor)) {
      // Compute instance size from initial map of {constructor}.
      Handle<Map> initial_map(constructor->initial_map(), isolate());
      int const instance_size = initial_map->instance_size();

      // Add a dependency on the {initial_map} to make sure that this code is
      // deoptimized whenever the {initial_map} of the {constructor} changes.
      dependencies()->AssumeInitialMapCantChange(initial_map);

      // Emit code to allocate the JSObject instance for the {constructor}.
      AllocationBuilder a(jsgraph(), effect, graph()->start());
      a.Allocate(instance_size);
      a.Store(AccessBuilder::ForMap(), initial_map);
      a.Store(AccessBuilder::ForJSObjectProperties(),
              jsgraph()->EmptyFixedArrayConstant());
      a.Store(AccessBuilder::ForJSObjectElements(),
              jsgraph()->EmptyFixedArrayConstant());
      for (int i = 0; i < initial_map->GetInObjectProperties(); ++i) {
        a.Store(AccessBuilder::ForJSObjectInObjectProperty(initial_map, i),
                jsgraph()->UndefinedConstant());
      }
      a.FinishAndChange(node);
      return Changed(node);
    }
  }
  return NoChange();
}

Reduction JSCreateLowering::ReduceJSCreateArguments(Node* node) {
  DCHECK_EQ(IrOpcode::kJSCreateArguments, node->opcode());
  CreateArgumentsType type = CreateArgumentsTypeOf(node->op());
  Node* const frame_state = NodeProperties::GetFrameStateInput(node, 0);
  Node* const outer_state = frame_state->InputAt(kFrameStateOuterStateInput);
  FrameStateInfo state_info = OpParameter<FrameStateInfo>(frame_state);

  // Use the ArgumentsAccessStub for materializing both mapped and unmapped
  // arguments object, but only for non-inlined (i.e. outermost) frames.
  if (outer_state->opcode() != IrOpcode::kFrameState) {
    switch (type) {
      case CreateArgumentsType::kMappedArguments: {
        // TODO(bmeurer): Cleanup this mess at some point.
        int parameter_count = state_info.parameter_count() - 1;
        int parameter_offset = parameter_count * kPointerSize;
        int offset = StandardFrameConstants::kCallerSPOffset + parameter_offset;
        Node* parameter_pointer =
            graph()->NewNode(machine()->IntAdd(),
                             graph()->NewNode(machine()->LoadFramePointer()),
                             jsgraph()->IntPtrConstant(offset));
        Handle<SharedFunctionInfo> shared;
        if (!state_info.shared_info().ToHandle(&shared)) return NoChange();
        Callable callable = CodeFactory::ArgumentsAccess(
            isolate(), shared->has_duplicate_parameters());
        CallDescriptor* desc = Linkage::GetStubCallDescriptor(
            isolate(), graph()->zone(), callable.descriptor(), 0,
            CallDescriptor::kNeedsFrameState);
        const Operator* new_op = common()->Call(desc);
        Node* stub_code = jsgraph()->HeapConstant(callable.code());
        node->InsertInput(graph()->zone(), 0, stub_code);
        node->InsertInput(graph()->zone(), 2,
                          jsgraph()->Constant(parameter_count));
        node->InsertInput(graph()->zone(), 3, parameter_pointer);
        NodeProperties::ChangeOp(node, new_op);
        return Changed(node);
      }
      case CreateArgumentsType::kUnmappedArguments: {
        Callable callable = CodeFactory::FastNewStrictArguments(isolate());
        CallDescriptor* desc = Linkage::GetStubCallDescriptor(
            isolate(), graph()->zone(), callable.descriptor(), 0,
            CallDescriptor::kNeedsFrameState);
        const Operator* new_op = common()->Call(desc);
        Node* stub_code = jsgraph()->HeapConstant(callable.code());
        node->InsertInput(graph()->zone(), 0, stub_code);
        NodeProperties::ChangeOp(node, new_op);
        return Changed(node);
      }
      case CreateArgumentsType::kRestParameter: {
        Callable callable = CodeFactory::FastNewRestParameter(isolate());
        CallDescriptor* desc = Linkage::GetStubCallDescriptor(
            isolate(), graph()->zone(), callable.descriptor(), 0,
            CallDescriptor::kNeedsFrameState);
        const Operator* new_op = common()->Call(desc);
        Node* stub_code = jsgraph()->HeapConstant(callable.code());
        node->InsertInput(graph()->zone(), 0, stub_code);
        NodeProperties::ChangeOp(node, new_op);
        return Changed(node);
      }
    }
    UNREACHABLE();
  } else if (outer_state->opcode() == IrOpcode::kFrameState) {
    // Use inline allocation for all mapped arguments objects within inlined
    // (i.e. non-outermost) frames, independent of the object size.
    if (type == CreateArgumentsType::kMappedArguments) {
      Handle<SharedFunctionInfo> shared;
      if (!state_info.shared_info().ToHandle(&shared)) return NoChange();
      Node* const callee = NodeProperties::GetValueInput(node, 0);
      Node* const control = NodeProperties::GetControlInput(node);
      Node* const context = NodeProperties::GetContextInput(node);
      Node* effect = NodeProperties::GetEffectInput(node);
      // TODO(mstarzinger): Duplicate parameters are not handled yet.
      if (shared->has_duplicate_parameters()) return NoChange();
      // Choose the correct frame state and frame state info depending on
      // whether there conceptually is an arguments adaptor frame in the call
      // chain.
      Node* const args_state = GetArgumentsFrameState(frame_state);
      FrameStateInfo args_state_info = OpParameter<FrameStateInfo>(args_state);
      // Prepare element backing store to be used by arguments object.
      bool has_aliased_arguments = false;
      Node* const elements = AllocateAliasedArguments(
          effect, control, args_state, context, shared, &has_aliased_arguments);
      effect = elements->op()->EffectOutputCount() > 0 ? elements : effect;
      // Load the arguments object map from the current native context.
      Node* const load_native_context = effect = graph()->NewNode(
          javascript()->LoadContext(0, Context::NATIVE_CONTEXT_INDEX, true),
          context, context, effect);
      Node* const load_arguments_map = effect = graph()->NewNode(
          simplified()->LoadField(AccessBuilder::ForContextSlot(
              has_aliased_arguments ? Context::FAST_ALIASED_ARGUMENTS_MAP_INDEX
                                    : Context::SLOPPY_ARGUMENTS_MAP_INDEX)),
          load_native_context, effect, control);
      // Actually allocate and initialize the arguments object.
      AllocationBuilder a(jsgraph(), effect, control);
      Node* properties = jsgraph()->EmptyFixedArrayConstant();
      int length = args_state_info.parameter_count() - 1;  // Minus receiver.
      STATIC_ASSERT(JSSloppyArgumentsObject::kSize == 5 * kPointerSize);
      a.Allocate(JSSloppyArgumentsObject::kSize);
      a.Store(AccessBuilder::ForMap(), load_arguments_map);
      a.Store(AccessBuilder::ForJSObjectProperties(), properties);
      a.Store(AccessBuilder::ForJSObjectElements(), elements);
      a.Store(AccessBuilder::ForArgumentsLength(), jsgraph()->Constant(length));
      a.Store(AccessBuilder::ForArgumentsCallee(), callee);
      RelaxControls(node);
      a.FinishAndChange(node);
      return Changed(node);
    } else if (type == CreateArgumentsType::kUnmappedArguments) {
      // Use inline allocation for all unmapped arguments objects within inlined
      // (i.e. non-outermost) frames, independent of the object size.
      Node* const control = NodeProperties::GetControlInput(node);
      Node* const context = NodeProperties::GetContextInput(node);
      Node* effect = NodeProperties::GetEffectInput(node);
      // Choose the correct frame state and frame state info depending on
      // whether there conceptually is an arguments adaptor frame in the call
      // chain.
      Node* const args_state = GetArgumentsFrameState(frame_state);
      FrameStateInfo args_state_info = OpParameter<FrameStateInfo>(args_state);
      // Prepare element backing store to be used by arguments object.
      Node* const elements = AllocateArguments(effect, control, args_state);
      effect = elements->op()->EffectOutputCount() > 0 ? elements : effect;
      // Load the arguments object map from the current native context.
      Node* const load_native_context = effect = graph()->NewNode(
          javascript()->LoadContext(0, Context::NATIVE_CONTEXT_INDEX, true),
          context, context, effect);
      Node* const load_arguments_map = effect = graph()->NewNode(
          simplified()->LoadField(AccessBuilder::ForContextSlot(
              Context::STRICT_ARGUMENTS_MAP_INDEX)),
          load_native_context, effect, control);
      // Actually allocate and initialize the arguments object.
      AllocationBuilder a(jsgraph(), effect, control);
      Node* properties = jsgraph()->EmptyFixedArrayConstant();
      int length = args_state_info.parameter_count() - 1;  // Minus receiver.
      STATIC_ASSERT(JSStrictArgumentsObject::kSize == 4 * kPointerSize);
      a.Allocate(JSStrictArgumentsObject::kSize);
      a.Store(AccessBuilder::ForMap(), load_arguments_map);
      a.Store(AccessBuilder::ForJSObjectProperties(), properties);
      a.Store(AccessBuilder::ForJSObjectElements(), elements);
      a.Store(AccessBuilder::ForArgumentsLength(), jsgraph()->Constant(length));
      RelaxControls(node);
      a.FinishAndChange(node);
      return Changed(node);
    } else if (type == CreateArgumentsType::kRestParameter) {
      Handle<SharedFunctionInfo> shared;
      if (!state_info.shared_info().ToHandle(&shared)) return NoChange();
      int start_index = shared->internal_formal_parameter_count();
      // Use inline allocation for all unmapped arguments objects within inlined
      // (i.e. non-outermost) frames, independent of the object size.
      Node* const control = NodeProperties::GetControlInput(node);
      Node* const context = NodeProperties::GetContextInput(node);
      Node* effect = NodeProperties::GetEffectInput(node);
      // Choose the correct frame state and frame state info depending on
      // whether there conceptually is an arguments adaptor frame in the call
      // chain.
      Node* const args_state = GetArgumentsFrameState(frame_state);
      FrameStateInfo args_state_info = OpParameter<FrameStateInfo>(args_state);
      // Prepare element backing store to be used by the rest array.
      Node* const elements =
          AllocateRestArguments(effect, control, args_state, start_index);
      effect = elements->op()->EffectOutputCount() > 0 ? elements : effect;
      // Load the JSArray object map from the current native context.
      Node* const load_native_context = effect = graph()->NewNode(
          javascript()->LoadContext(0, Context::NATIVE_CONTEXT_INDEX, true),
          context, context, effect);
      Node* const load_jsarray_map = effect = graph()->NewNode(
          simplified()->LoadField(AccessBuilder::ForContextSlot(
              Context::JS_ARRAY_FAST_ELEMENTS_MAP_INDEX)),
          load_native_context, effect, control);
      // Actually allocate and initialize the jsarray.
      AllocationBuilder a(jsgraph(), effect, control);
      Node* properties = jsgraph()->EmptyFixedArrayConstant();

      // -1 to minus receiver
      int argument_count = args_state_info.parameter_count() - 1;
      int length = std::max(0, argument_count - start_index);
      STATIC_ASSERT(JSArray::kSize == 4 * kPointerSize);
      a.Allocate(JSArray::kSize);
      a.Store(AccessBuilder::ForMap(), load_jsarray_map);
      a.Store(AccessBuilder::ForJSObjectProperties(), properties);
      a.Store(AccessBuilder::ForJSObjectElements(), elements);
      a.Store(AccessBuilder::ForJSArrayLength(FAST_ELEMENTS),
              jsgraph()->Constant(length));
      RelaxControls(node);
      a.FinishAndChange(node);
      return Changed(node);
    }
  }

  return NoChange();
}

Reduction JSCreateLowering::ReduceNewArray(Node* node, Node* length,
                                           int capacity,
                                           Handle<AllocationSite> site) {
  DCHECK_EQ(IrOpcode::kJSCreateArray, node->opcode());
  Node* context = NodeProperties::GetContextInput(node);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  // Extract transition and tenuring feedback from the {site} and add
  // appropriate code dependencies on the {site} if deoptimization is
  // enabled.
  PretenureFlag pretenure = site->GetPretenureMode();
  ElementsKind elements_kind = site->GetElementsKind();
  DCHECK(IsFastElementsKind(elements_kind));
  dependencies()->AssumeTenuringDecision(site);
  dependencies()->AssumeTransitionStable(site);

  // Retrieve the initial map for the array from the appropriate native context.
  Node* native_context = effect = graph()->NewNode(
      javascript()->LoadContext(0, Context::NATIVE_CONTEXT_INDEX, true),
      context, context, effect);
  Node* js_array_map = effect = graph()->NewNode(
      javascript()->LoadContext(0, Context::ArrayMapIndex(elements_kind), true),
      native_context, native_context, effect);

  // Setup elements and properties.
  Node* elements;
  if (capacity == 0) {
    elements = jsgraph()->EmptyFixedArrayConstant();
  } else {
    elements = effect =
        AllocateElements(effect, control, elements_kind, capacity, pretenure);
  }
  Node* properties = jsgraph()->EmptyFixedArrayConstant();

  // Perform the allocation of the actual JSArray object.
  AllocationBuilder a(jsgraph(), effect, control);
  a.Allocate(JSArray::kSize, pretenure);
  a.Store(AccessBuilder::ForMap(), js_array_map);
  a.Store(AccessBuilder::ForJSObjectProperties(), properties);
  a.Store(AccessBuilder::ForJSObjectElements(), elements);
  a.Store(AccessBuilder::ForJSArrayLength(elements_kind), length);
  RelaxControls(node);
  a.FinishAndChange(node);
  return Changed(node);
}

Reduction JSCreateLowering::ReduceJSCreateArray(Node* node) {
  DCHECK_EQ(IrOpcode::kJSCreateArray, node->opcode());
  CreateArrayParameters const& p = CreateArrayParametersOf(node->op());
  Node* target = NodeProperties::GetValueInput(node, 0);
  Node* new_target = NodeProperties::GetValueInput(node, 1);

  // TODO(bmeurer): Optimize the subclassing case.
  if (target != new_target) return NoChange();

  // Check if we have a feedback {site} on the {node}.
  Handle<AllocationSite> site = p.site();
  if (p.site().is_null()) return NoChange();

  // Attempt to inline calls to the Array constructor for the relevant cases
  // where either no arguments are provided, or exactly one unsigned number
  // argument is given.
  if (site->CanInlineCall()) {
    if (p.arity() == 0) {
      Node* length = jsgraph()->ZeroConstant();
      int capacity = JSArray::kPreallocatedArrayElements;
      return ReduceNewArray(node, length, capacity, site);
    } else if (p.arity() == 1) {
      Node* length = NodeProperties::GetValueInput(node, 2);
      Type* length_type = NodeProperties::GetType(length);
      if (length_type->Is(Type::SignedSmall()) &&
          length_type->Min() >= 0 &&
          length_type->Max() <= kElementLoopUnrollLimit) {
        int capacity = static_cast<int>(length_type->Max());
        return ReduceNewArray(node, length, capacity, site);
      }
    }
  }

  return NoChange();
}

Reduction JSCreateLowering::ReduceJSCreateIterResultObject(Node* node) {
  DCHECK_EQ(IrOpcode::kJSCreateIterResultObject, node->opcode());
  Node* value = NodeProperties::GetValueInput(node, 0);
  Node* done = NodeProperties::GetValueInput(node, 1);
  Node* context = NodeProperties::GetContextInput(node);
  Node* effect = NodeProperties::GetEffectInput(node);

  // Load the JSIteratorResult map for the {context}.
  Node* native_context = effect = graph()->NewNode(
      javascript()->LoadContext(0, Context::NATIVE_CONTEXT_INDEX, true),
      context, context, effect);
  Node* iterator_result_map = effect = graph()->NewNode(
      javascript()->LoadContext(0, Context::ITERATOR_RESULT_MAP_INDEX, true),
      native_context, native_context, effect);

  // Emit code to allocate the JSIteratorResult instance.
  AllocationBuilder a(jsgraph(), effect, graph()->start());
  a.Allocate(JSIteratorResult::kSize);
  a.Store(AccessBuilder::ForMap(), iterator_result_map);
  a.Store(AccessBuilder::ForJSObjectProperties(),
          jsgraph()->EmptyFixedArrayConstant());
  a.Store(AccessBuilder::ForJSObjectElements(),
          jsgraph()->EmptyFixedArrayConstant());
  a.Store(AccessBuilder::ForJSIteratorResultValue(), value);
  a.Store(AccessBuilder::ForJSIteratorResultDone(), done);
  STATIC_ASSERT(JSIteratorResult::kSize == 5 * kPointerSize);
  a.FinishAndChange(node);
  return Changed(node);
}

Reduction JSCreateLowering::ReduceJSCreateFunctionContext(Node* node) {
  DCHECK_EQ(IrOpcode::kJSCreateFunctionContext, node->opcode());
  int slot_count = OpParameter<int>(node->op());
  Node* const closure = NodeProperties::GetValueInput(node, 0);

  // Use inline allocation for function contexts up to a size limit.
  if (slot_count < kFunctionContextAllocationLimit) {
    // JSCreateFunctionContext[slot_count < limit]](fun)
    Node* effect = NodeProperties::GetEffectInput(node);
    Node* control = NodeProperties::GetControlInput(node);
    Node* context = NodeProperties::GetContextInput(node);
    Node* extension = jsgraph()->TheHoleConstant();
    Node* native_context = effect = graph()->NewNode(
        javascript()->LoadContext(0, Context::NATIVE_CONTEXT_INDEX, true),
        context, context, effect);
    AllocationBuilder a(jsgraph(), effect, control);
    STATIC_ASSERT(Context::MIN_CONTEXT_SLOTS == 4);  // Ensure fully covered.
    int context_length = slot_count + Context::MIN_CONTEXT_SLOTS;
    a.AllocateArray(context_length, factory()->function_context_map());
    a.Store(AccessBuilder::ForContextSlot(Context::CLOSURE_INDEX), closure);
    a.Store(AccessBuilder::ForContextSlot(Context::PREVIOUS_INDEX), context);
    a.Store(AccessBuilder::ForContextSlot(Context::EXTENSION_INDEX), extension);
    a.Store(AccessBuilder::ForContextSlot(Context::NATIVE_CONTEXT_INDEX),
            native_context);
    for (int i = Context::MIN_CONTEXT_SLOTS; i < context_length; ++i) {
      a.Store(AccessBuilder::ForContextSlot(i), jsgraph()->UndefinedConstant());
    }
    RelaxControls(node);
    a.FinishAndChange(node);
    return Changed(node);
  }

  return NoChange();
}

Reduction JSCreateLowering::ReduceJSCreateWithContext(Node* node) {
  DCHECK_EQ(IrOpcode::kJSCreateWithContext, node->opcode());
  Node* object = NodeProperties::GetValueInput(node, 0);
  Node* closure = NodeProperties::GetValueInput(node, 1);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);
  Node* context = NodeProperties::GetContextInput(node);
  Node* native_context = effect = graph()->NewNode(
      javascript()->LoadContext(0, Context::NATIVE_CONTEXT_INDEX, true),
      context, context, effect);
  AllocationBuilder a(jsgraph(), effect, control);
  STATIC_ASSERT(Context::MIN_CONTEXT_SLOTS == 4);  // Ensure fully covered.
  a.AllocateArray(Context::MIN_CONTEXT_SLOTS, factory()->with_context_map());
  a.Store(AccessBuilder::ForContextSlot(Context::CLOSURE_INDEX), closure);
  a.Store(AccessBuilder::ForContextSlot(Context::PREVIOUS_INDEX), context);
  a.Store(AccessBuilder::ForContextSlot(Context::EXTENSION_INDEX), object);
  a.Store(AccessBuilder::ForContextSlot(Context::NATIVE_CONTEXT_INDEX),
          native_context);
  RelaxControls(node);
  a.FinishAndChange(node);
  return Changed(node);
}

Reduction JSCreateLowering::ReduceJSCreateCatchContext(Node* node) {
  DCHECK_EQ(IrOpcode::kJSCreateCatchContext, node->opcode());
  Handle<String> name = OpParameter<Handle<String>>(node);
  Node* exception = NodeProperties::GetValueInput(node, 0);
  Node* closure = NodeProperties::GetValueInput(node, 1);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);
  Node* context = NodeProperties::GetContextInput(node);
  Node* native_context = effect = graph()->NewNode(
      javascript()->LoadContext(0, Context::NATIVE_CONTEXT_INDEX, true),
      context, context, effect);
  AllocationBuilder a(jsgraph(), effect, control);
  STATIC_ASSERT(Context::MIN_CONTEXT_SLOTS == 4);  // Ensure fully covered.
  a.AllocateArray(Context::MIN_CONTEXT_SLOTS + 1,
                  factory()->catch_context_map());
  a.Store(AccessBuilder::ForContextSlot(Context::CLOSURE_INDEX), closure);
  a.Store(AccessBuilder::ForContextSlot(Context::PREVIOUS_INDEX), context);
  a.Store(AccessBuilder::ForContextSlot(Context::EXTENSION_INDEX), name);
  a.Store(AccessBuilder::ForContextSlot(Context::NATIVE_CONTEXT_INDEX),
          native_context);
  a.Store(AccessBuilder::ForContextSlot(Context::THROWN_OBJECT_INDEX),
          exception);
  RelaxControls(node);
  a.FinishAndChange(node);
  return Changed(node);
}

Reduction JSCreateLowering::ReduceJSCreateBlockContext(Node* node) {
  DCHECK_EQ(IrOpcode::kJSCreateBlockContext, node->opcode());
  Handle<ScopeInfo> scope_info = OpParameter<Handle<ScopeInfo>>(node);
  int const context_length = scope_info->ContextLength();
  Node* const closure = NodeProperties::GetValueInput(node, 0);

  // Use inline allocation for block contexts up to a size limit.
  if (context_length < kBlockContextAllocationLimit) {
    // JSCreateBlockContext[scope[length < limit]](fun)
    Node* effect = NodeProperties::GetEffectInput(node);
    Node* control = NodeProperties::GetControlInput(node);
    Node* context = NodeProperties::GetContextInput(node);
    Node* extension = jsgraph()->Constant(scope_info);
    Node* native_context = effect = graph()->NewNode(
        javascript()->LoadContext(0, Context::NATIVE_CONTEXT_INDEX, true),
        context, context, effect);
    AllocationBuilder a(jsgraph(), effect, control);
    STATIC_ASSERT(Context::MIN_CONTEXT_SLOTS == 4);  // Ensure fully covered.
    a.AllocateArray(context_length, factory()->block_context_map());
    a.Store(AccessBuilder::ForContextSlot(Context::CLOSURE_INDEX), closure);
    a.Store(AccessBuilder::ForContextSlot(Context::PREVIOUS_INDEX), context);
    a.Store(AccessBuilder::ForContextSlot(Context::EXTENSION_INDEX), extension);
    a.Store(AccessBuilder::ForContextSlot(Context::NATIVE_CONTEXT_INDEX),
            native_context);
    for (int i = Context::MIN_CONTEXT_SLOTS; i < context_length; ++i) {
      a.Store(AccessBuilder::ForContextSlot(i), jsgraph()->UndefinedConstant());
    }
    RelaxControls(node);
    a.FinishAndChange(node);
    return Changed(node);
  }

  return NoChange();
}

// Helper that allocates a FixedArray holding argument values recorded in the
// given {frame_state}. Serves as backing store for JSCreateArguments nodes.
Node* JSCreateLowering::AllocateArguments(Node* effect, Node* control,
                                          Node* frame_state) {
  FrameStateInfo state_info = OpParameter<FrameStateInfo>(frame_state);
  int argument_count = state_info.parameter_count() - 1;  // Minus receiver.
  if (argument_count == 0) return jsgraph()->EmptyFixedArrayConstant();

  // Prepare an iterator over argument values recorded in the frame state.
  Node* const parameters = frame_state->InputAt(kFrameStateParametersInput);
  StateValuesAccess parameters_access(parameters);
  auto parameters_it = ++parameters_access.begin();

  // Actually allocate the backing store.
  AllocationBuilder a(jsgraph(), effect, control);
  a.AllocateArray(argument_count, factory()->fixed_array_map());
  for (int i = 0; i < argument_count; ++i, ++parameters_it) {
    a.Store(AccessBuilder::ForFixedArraySlot(i), (*parameters_it).node);
  }
  return a.Finish();
}

// Helper that allocates a FixedArray holding argument values recorded in the
// given {frame_state}. Serves as backing store for JSCreateArguments nodes.
Node* JSCreateLowering::AllocateRestArguments(Node* effect, Node* control,
                                              Node* frame_state,
                                              int start_index) {
  FrameStateInfo state_info = OpParameter<FrameStateInfo>(frame_state);
  int argument_count = state_info.parameter_count() - 1;  // Minus receiver.
  int num_elements = std::max(0, argument_count - start_index);
  if (num_elements == 0) return jsgraph()->EmptyFixedArrayConstant();

  // Prepare an iterator over argument values recorded in the frame state.
  Node* const parameters = frame_state->InputAt(kFrameStateParametersInput);
  StateValuesAccess parameters_access(parameters);
  auto parameters_it = ++parameters_access.begin();

  // Skip unused arguments.
  for (int i = 0; i < start_index; i++) {
    ++parameters_it;
  }

  // Actually allocate the backing store.
  AllocationBuilder a(jsgraph(), effect, control);
  a.AllocateArray(num_elements, factory()->fixed_array_map());
  for (int i = 0; i < num_elements; ++i, ++parameters_it) {
    a.Store(AccessBuilder::ForFixedArraySlot(i), (*parameters_it).node);
  }
  return a.Finish();
}

// Helper that allocates a FixedArray serving as a parameter map for values
// recorded in the given {frame_state}. Some elements map to slots within the
// given {context}. Serves as backing store for JSCreateArguments nodes.
Node* JSCreateLowering::AllocateAliasedArguments(
    Node* effect, Node* control, Node* frame_state, Node* context,
    Handle<SharedFunctionInfo> shared, bool* has_aliased_arguments) {
  FrameStateInfo state_info = OpParameter<FrameStateInfo>(frame_state);
  int argument_count = state_info.parameter_count() - 1;  // Minus receiver.
  if (argument_count == 0) return jsgraph()->EmptyFixedArrayConstant();

  // If there is no aliasing, the arguments object elements are not special in
  // any way, we can just return an unmapped backing store instead.
  int parameter_count = shared->internal_formal_parameter_count();
  if (parameter_count == 0) {
    return AllocateArguments(effect, control, frame_state);
  }

  // Calculate number of argument values being aliased/mapped.
  int mapped_count = Min(argument_count, parameter_count);
  *has_aliased_arguments = true;

  // Prepare an iterator over argument values recorded in the frame state.
  Node* const parameters = frame_state->InputAt(kFrameStateParametersInput);
  StateValuesAccess parameters_access(parameters);
  auto paratemers_it = ++parameters_access.begin();

  // The unmapped argument values recorded in the frame state are stored yet
  // another indirection away and then linked into the parameter map below,
  // whereas mapped argument values are replaced with a hole instead.
  AllocationBuilder aa(jsgraph(), effect, control);
  aa.AllocateArray(argument_count, factory()->fixed_array_map());
  for (int i = 0; i < mapped_count; ++i, ++paratemers_it) {
    aa.Store(AccessBuilder::ForFixedArraySlot(i), jsgraph()->TheHoleConstant());
  }
  for (int i = mapped_count; i < argument_count; ++i, ++paratemers_it) {
    aa.Store(AccessBuilder::ForFixedArraySlot(i), (*paratemers_it).node);
  }
  Node* arguments = aa.Finish();

  // Actually allocate the backing store.
  AllocationBuilder a(jsgraph(), arguments, control);
  a.AllocateArray(mapped_count + 2, factory()->sloppy_arguments_elements_map());
  a.Store(AccessBuilder::ForFixedArraySlot(0), context);
  a.Store(AccessBuilder::ForFixedArraySlot(1), arguments);
  for (int i = 0; i < mapped_count; ++i) {
    int idx = Context::MIN_CONTEXT_SLOTS + parameter_count - 1 - i;
    a.Store(AccessBuilder::ForFixedArraySlot(i + 2), jsgraph()->Constant(idx));
  }
  return a.Finish();
}

Node* JSCreateLowering::AllocateElements(Node* effect, Node* control,
                                         ElementsKind elements_kind,
                                         int capacity,
                                         PretenureFlag pretenure) {
  DCHECK_LE(1, capacity);
  DCHECK_LE(capacity, JSArray::kInitialMaxFastElementArray);

  Handle<Map> elements_map = IsFastDoubleElementsKind(elements_kind)
                                 ? factory()->fixed_double_array_map()
                                 : factory()->fixed_array_map();
  ElementAccess access = IsFastDoubleElementsKind(elements_kind)
                             ? AccessBuilder::ForFixedDoubleArrayElement()
                             : AccessBuilder::ForFixedArrayElement();
  Node* value =
      IsFastDoubleElementsKind(elements_kind)
          ? jsgraph()->Float64Constant(bit_cast<double>(kHoleNanInt64))
          : jsgraph()->TheHoleConstant();

  // Actually allocate the backing store.
  AllocationBuilder a(jsgraph(), effect, control);
  a.AllocateArray(capacity, elements_map, pretenure);
  for (int i = 0; i < capacity; ++i) {
    Node* index = jsgraph()->Constant(i);
    a.Store(access, index, value);
  }
  return a.Finish();
}

Factory* JSCreateLowering::factory() const { return isolate()->factory(); }

Graph* JSCreateLowering::graph() const { return jsgraph()->graph(); }

Isolate* JSCreateLowering::isolate() const { return jsgraph()->isolate(); }

JSOperatorBuilder* JSCreateLowering::javascript() const {
  return jsgraph()->javascript();
}

CommonOperatorBuilder* JSCreateLowering::common() const {
  return jsgraph()->common();
}

SimplifiedOperatorBuilder* JSCreateLowering::simplified() const {
  return jsgraph()->simplified();
}

MachineOperatorBuilder* JSCreateLowering::machine() const {
  return jsgraph()->machine();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
