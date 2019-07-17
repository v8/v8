// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_SERIALIZER_FOR_BACKGROUND_COMPILATION_H_
#define V8_COMPILER_SERIALIZER_FOR_BACKGROUND_COMPILATION_H_

#include "src/base/optional.h"
#include "src/compiler/access-info.h"
#include "src/compiler/bytecode-analysis.h"
#include "src/handles/handles.h"
#include "src/handles/maybe-handles.h"
#include "src/utils/utils.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

namespace interpreter {
class BytecodeArrayIterator;
}  // namespace interpreter

class BytecodeArray;
class FeedbackVector;
class LookupIterator;
class NativeContext;
class ScriptContextTable;
class SharedFunctionInfo;
class SourcePositionTableIterator;
class Zone;

namespace compiler {

#define CLEAR_ENVIRONMENT_LIST(V) \
  V(CallRuntimeForPair)           \
  V(Debugger)                     \
  V(ResumeGenerator)              \
  V(SuspendGenerator)

#define KILL_ENVIRONMENT_LIST(V) \
  V(Abort)                       \
  V(ReThrow)                     \
  V(Throw)

#define CLEAR_ACCUMULATOR_LIST(V) \
  V(Add)                          \
  V(AddSmi)                       \
  V(BitwiseAnd)                   \
  V(BitwiseAndSmi)                \
  V(BitwiseNot)                   \
  V(BitwiseOr)                    \
  V(BitwiseOrSmi)                 \
  V(BitwiseXor)                   \
  V(BitwiseXorSmi)                \
  V(CallRuntime)                  \
  V(CloneObject)                  \
  V(CreateArrayFromIterable)      \
  V(CreateArrayLiteral)           \
  V(CreateEmptyArrayLiteral)      \
  V(CreateEmptyObjectLiteral)     \
  V(CreateMappedArguments)        \
  V(CreateObjectLiteral)          \
  V(CreateRegExpLiteral)          \
  V(CreateRestParameter)          \
  V(CreateUnmappedArguments)      \
  V(Dec)                          \
  V(DeletePropertySloppy)         \
  V(DeletePropertyStrict)         \
  V(Div)                          \
  V(DivSmi)                       \
  V(Exp)                          \
  V(ExpSmi)                       \
  V(ForInContinue)                \
  V(ForInEnumerate)               \
  V(ForInNext)                    \
  V(ForInStep)                    \
  V(Inc)                          \
  V(LdaLookupSlot)                \
  V(LdaLookupSlotInsideTypeof)    \
  V(LogicalNot)                   \
  V(Mod)                          \
  V(ModSmi)                       \
  V(Mul)                          \
  V(MulSmi)                       \
  V(Negate)                       \
  V(SetPendingMessage)            \
  V(ShiftLeft)                    \
  V(ShiftLeftSmi)                 \
  V(ShiftRight)                   \
  V(ShiftRightLogical)            \
  V(ShiftRightLogicalSmi)         \
  V(ShiftRightSmi)                \
  V(StaLookupSlot)                \
  V(Sub)                          \
  V(SubSmi)                       \
  V(TestEqual)                    \
  V(TestEqualStrict)              \
  V(TestGreaterThan)              \
  V(TestGreaterThanOrEqual)       \
  V(TestInstanceOf)               \
  V(TestLessThan)                 \
  V(TestLessThanOrEqual)          \
  V(TestNull)                     \
  V(TestReferenceEqual)           \
  V(TestTypeOf)                   \
  V(TestUndefined)                \
  V(TestUndetectable)             \
  V(ToBooleanLogicalNot)          \
  V(ToName)                       \
  V(ToNumber)                     \
  V(ToNumeric)                    \
  V(ToString)                     \
  V(TypeOf)

#define UNCONDITIONAL_JUMPS_LIST(V) \
  V(Jump)                           \
  V(JumpConstant)                   \
  V(JumpLoop)

#define CONDITIONAL_JUMPS_LIST(V) \
  V(JumpIfFalse)                  \
  V(JumpIfFalseConstant)          \
  V(JumpIfJSReceiver)             \
  V(JumpIfJSReceiverConstant)     \
  V(JumpIfNotNull)                \
  V(JumpIfNotNullConstant)        \
  V(JumpIfNotUndefined)           \
  V(JumpIfNotUndefinedConstant)   \
  V(JumpIfNull)                   \
  V(JumpIfNullConstant)           \
  V(JumpIfToBooleanFalse)         \
  V(JumpIfToBooleanFalseConstant) \
  V(JumpIfToBooleanTrue)          \
  V(JumpIfToBooleanTrueConstant)  \
  V(JumpIfTrue)                   \
  V(JumpIfTrueConstant)           \
  V(JumpIfUndefined)              \
  V(JumpIfUndefinedConstant)

#define IGNORED_BYTECODE_LIST(V)      \
  V(CallNoFeedback)                   \
  V(IncBlockCounter)                  \
  V(LdaNamedPropertyNoFeedback)       \
  V(StackCheck)                       \
  V(StaNamedPropertyNoFeedback)       \
  V(ThrowReferenceErrorIfHole)        \
  V(ThrowSuperAlreadyCalledIfNotHole) \
  V(ThrowSuperNotCalledIfHole)

#define UNREACHABLE_BYTECODE_LIST(V) \
  V(ExtraWide)                       \
  V(Illegal)                         \
  V(Wide)

#define SUPPORTED_BYTECODE_LIST(V)    \
  V(CallAnyReceiver)                  \
  V(CallJSRuntime)                    \
  V(CallProperty)                     \
  V(CallProperty0)                    \
  V(CallProperty1)                    \
  V(CallProperty2)                    \
  V(CallUndefinedReceiver)            \
  V(CallUndefinedReceiver0)           \
  V(CallUndefinedReceiver1)           \
  V(CallUndefinedReceiver2)           \
  V(CallWithSpread)                   \
  V(Construct)                        \
  V(ConstructWithSpread)              \
  V(CreateBlockContext)               \
  V(CreateCatchContext)               \
  V(CreateClosure)                    \
  V(CreateEvalContext)                \
  V(CreateFunctionContext)            \
  V(CreateWithContext)                \
  V(GetSuperConstructor)              \
  V(GetTemplateObject)                \
  V(InvokeIntrinsic)                  \
  V(LdaConstant)                      \
  V(LdaContextSlot)                   \
  V(LdaCurrentContextSlot)            \
  V(LdaImmutableContextSlot)          \
  V(LdaImmutableCurrentContextSlot)   \
  V(LdaModuleVariable)                \
  V(LdaFalse)                         \
  V(LdaGlobal)                        \
  V(LdaGlobalInsideTypeof)            \
  V(LdaKeyedProperty)                 \
  V(LdaLookupContextSlot)             \
  V(LdaLookupContextSlotInsideTypeof) \
  V(LdaLookupGlobalSlot)              \
  V(LdaLookupGlobalSlotInsideTypeof)  \
  V(LdaNamedProperty)                 \
  V(LdaNull)                          \
  V(Ldar)                             \
  V(LdaSmi)                           \
  V(LdaTheHole)                       \
  V(LdaTrue)                          \
  V(LdaUndefined)                     \
  V(LdaZero)                          \
  V(Mov)                              \
  V(PopContext)                       \
  V(PushContext)                      \
  V(Return)                           \
  V(StaContextSlot)                   \
  V(StaCurrentContextSlot)            \
  V(StaGlobal)                        \
  V(StaInArrayLiteral)                \
  V(StaKeyedProperty)                 \
  V(StaModuleVariable)                \
  V(StaNamedOwnProperty)              \
  V(StaNamedProperty)                 \
  V(Star)                             \
  V(SwitchOnGeneratorState)           \
  V(SwitchOnSmiNoFeedback)            \
  V(TestIn)                           \
  CLEAR_ACCUMULATOR_LIST(V)           \
  CLEAR_ENVIRONMENT_LIST(V)           \
  CONDITIONAL_JUMPS_LIST(V)           \
  IGNORED_BYTECODE_LIST(V)            \
  KILL_ENVIRONMENT_LIST(V)            \
  UNCONDITIONAL_JUMPS_LIST(V)         \
  UNREACHABLE_BYTECODE_LIST(V)

class JSHeapBroker;

template <typename T>
struct HandleComparator {
  bool operator()(const Handle<T>& lhs, const Handle<T>& rhs) const {
    return lhs.address() < rhs.address();
  }
};

struct VirtualContext {
  unsigned int distance;
  Handle<Context> context;

  VirtualContext(unsigned int distance_in, Handle<Context> context_in)
      : distance(distance_in), context(context_in) {
    CHECK_GT(distance, 0);
  }
  bool operator<(const VirtualContext& other) const {
    return HandleComparator<Context>()(context, other.context) &&
           distance < other.distance;
  }
};

class FunctionBlueprint;
using ConstantsSet = ZoneSet<Handle<Object>, HandleComparator<Object>>;
using VirtualContextsSet = ZoneSet<VirtualContext>;
using MapsSet = ZoneSet<Handle<Map>, HandleComparator<Map>>;
using BlueprintsSet = ZoneSet<FunctionBlueprint>;

class Hints {
 public:
  explicit Hints(Zone* zone);

  const ConstantsSet& constants() const;
  const MapsSet& maps() const;
  const BlueprintsSet& function_blueprints() const;
  const VirtualContextsSet& virtual_contexts() const;

  void AddConstant(Handle<Object> constant);
  void AddMap(Handle<Map> map);
  void AddFunctionBlueprint(FunctionBlueprint function_blueprint);
  void AddVirtualContext(VirtualContext virtual_context);

  void Add(const Hints& other);

  void Clear();
  bool IsEmpty() const;

#ifdef ENABLE_SLOW_DCHECKS
  bool Includes(Hints const& other) const;
  bool Equals(Hints const& other) const;
#endif

 private:
  VirtualContextsSet virtual_contexts_;
  ConstantsSet constants_;
  MapsSet maps_;
  BlueprintsSet function_blueprints_;
};
using HintsVector = ZoneVector<Hints>;

enum class SerializerForBackgroundCompilationFlag : uint8_t {
  kBailoutOnUninitialized = 1 << 0,
  kCollectSourcePositions = 1 << 1,
  kAnalyzeEnvironmentLiveness = 1 << 2,
};
using SerializerForBackgroundCompilationFlags =
    base::Flags<SerializerForBackgroundCompilationFlag>;

class FunctionBlueprint {
 public:
  FunctionBlueprint(Handle<JSFunction> function, Isolate* isolate, Zone* zone);

  FunctionBlueprint(Handle<SharedFunctionInfo> shared,
                    Handle<FeedbackVector> feedback_vector,
                    const Hints& context_hints);

  Handle<SharedFunctionInfo> shared() const { return shared_; }
  Handle<FeedbackVector> feedback_vector() const { return feedback_vector_; }
  const Hints& context_hints() const { return context_hints_; }

  bool operator<(const FunctionBlueprint& other) const {
    // A feedback vector is never used for more than one SFI, so it can
    // be used for strict ordering of blueprints.
    DCHECK_IMPLIES(feedback_vector_.equals(other.feedback_vector_),
                   shared_.equals(other.shared_));
    return HandleComparator<FeedbackVector>()(feedback_vector_,
                                              other.feedback_vector_);
  }

 private:
  Handle<SharedFunctionInfo> shared_;
  Handle<FeedbackVector> feedback_vector_;
  Hints context_hints_;
};

class CompilationSubject {
 public:
  explicit CompilationSubject(FunctionBlueprint blueprint)
      : blueprint_(blueprint) {}

  // The zone parameter is to correctly initialize the blueprint,
  // which contains zone-allocated context information.
  CompilationSubject(Handle<JSFunction> closure, Isolate* isolate, Zone* zone);

  const FunctionBlueprint& blueprint() const { return blueprint_; }
  MaybeHandle<JSFunction> closure() const { return closure_; }

 private:
  FunctionBlueprint blueprint_;
  MaybeHandle<JSFunction> closure_;
};

// The SerializerForBackgroundCompilation makes sure that the relevant function
// data such as bytecode, SharedFunctionInfo and FeedbackVector, used by later
// optimizations in the compiler, is copied to the heap broker.
class SerializerForBackgroundCompilation {
 public:
  SerializerForBackgroundCompilation(
      JSHeapBroker* broker, CompilationDependencies* dependencies, Zone* zone,
      Handle<JSFunction> closure, SerializerForBackgroundCompilationFlags flags,
      BailoutId osr_offset);
  Hints Run();  // NOTE: Returns empty for an already-serialized function.

  class Environment;

 private:
  SerializerForBackgroundCompilation(
      JSHeapBroker* broker, CompilationDependencies* dependencies, Zone* zone,
      CompilationSubject function, base::Optional<Hints> new_target,
      const HintsVector& arguments,
      SerializerForBackgroundCompilationFlags flags);

  bool BailoutOnUninitialized(FeedbackSlot slot);

  void TraverseBytecode();

#define DECLARE_VISIT_BYTECODE(name, ...) \
  void Visit##name(interpreter::BytecodeArrayIterator* iterator);
  SUPPORTED_BYTECODE_LIST(DECLARE_VISIT_BYTECODE)
#undef DECLARE_VISIT_BYTECODE

  void ProcessCallOrConstruct(Hints callee, base::Optional<Hints> new_target,
                              const HintsVector& arguments, FeedbackSlot slot,
                              bool with_spread = false);
  void ProcessCallVarArgs(interpreter::BytecodeArrayIterator* iterator,
                          ConvertReceiverMode receiver_mode,
                          bool with_spread = false);
  void ProcessApiCall(Handle<SharedFunctionInfo> target,
                      const HintsVector& arguments);
  void ProcessReceiverMapForApiCall(
      FunctionTemplateInfoRef& target,  // NOLINT(runtime/references)
      Handle<Map> receiver);
  void ProcessBuiltinCall(Handle<SharedFunctionInfo> target,
                          const HintsVector& arguments);

  void ProcessJump(interpreter::BytecodeArrayIterator* iterator);

  void ProcessKeyedPropertyAccess(Hints const& receiver, Hints const& key,
                                  FeedbackSlot slot, AccessMode mode);
  void ProcessNamedPropertyAccess(interpreter::BytecodeArrayIterator* iterator,
                                  AccessMode mode);
  void ProcessNamedPropertyAccess(Hints const& receiver, NameRef const& name,
                                  FeedbackSlot slot, AccessMode mode);
  void ProcessMapHintsForPromises(Hints const& receiver_hints);
  void ProcessHintsForPromiseResolve(Hints const& resolution_hints);
  void ProcessHintsForRegExpTest(Hints const& regexp_hints);
  PropertyAccessInfo ProcessMapForRegExpTest(MapRef map);
  void ProcessHintsForFunctionCall(Hints const& target_hints);

  GlobalAccessFeedback const* ProcessFeedbackForGlobalAccess(FeedbackSlot slot);
  NamedAccessFeedback const* ProcessFeedbackMapsForNamedAccess(
      const MapHandles& maps, AccessMode mode, NameRef const& name);
  ElementAccessFeedback const* ProcessFeedbackMapsForElementAccess(
      const MapHandles& maps, AccessMode mode,
      KeyedAccessMode const& keyed_mode);
  void ProcessFeedbackForPropertyAccess(FeedbackSlot slot, AccessMode mode,
                                        base::Optional<NameRef> static_name);
  void ProcessMapForNamedPropertyAccess(MapRef const& map, NameRef const& name);

  void ProcessCreateContext();
  enum ContextProcessingMode {
    kIgnoreSlot,
    kSerializeSlot,
    kSerializeSlotAndAddToAccumulator
  };

  void ProcessContextAccess(const Hints& context_hints, int slot, int depth,
                            ContextProcessingMode mode);
  void ProcessImmutableLoad(ContextRef& context,  // NOLINT(runtime/references)
                            int slot, ContextProcessingMode mode);
  void ProcessLdaLookupGlobalSlot(interpreter::BytecodeArrayIterator* iterator);
  void ProcessLdaLookupContextSlot(
      interpreter::BytecodeArrayIterator* iterator);

  // Performs extension lookups for [0, depth) like
  // BytecodeGraphBuilder::CheckContextExtensions().
  void ProcessCheckContextExtensions(int depth);

  Hints RunChildSerializer(CompilationSubject function,
                           base::Optional<Hints> new_target,
                           const HintsVector& arguments, bool with_spread);

  // When (forward-)branching bytecodes are encountered, e.g. a conditional
  // jump, we call ContributeToJumpTargetEnvironment to "remember" the current
  // environment, associated with the jump target offset. When serialization
  // eventually reaches that offset, we call IncorporateJumpTargetEnvironment to
  // merge that environment back into whatever is the current environment then.
  // Note: Since there may be multiple jumps to the same target,
  // ContributeToJumpTargetEnvironment may actually do a merge as well.
  void ContributeToJumpTargetEnvironment(int target_offset);
  void IncorporateJumpTargetEnvironment(int target_offset);

  Handle<BytecodeArray> bytecode_array() const;
  BytecodeAnalysis const& GetBytecodeAnalysis(bool serialize);

  JSHeapBroker* broker() const { return broker_; }
  CompilationDependencies* dependencies() const { return dependencies_; }
  Zone* zone() const { return zone_; }
  Environment* environment() const { return environment_; }
  SerializerForBackgroundCompilationFlags flags() const { return flags_; }
  BailoutId osr_offset() const { return osr_offset_; }

  JSHeapBroker* const broker_;
  CompilationDependencies* const dependencies_;
  Zone* const zone_;
  Environment* const environment_;
  ZoneUnorderedMap<int, Environment*> jump_target_environments_;
  SerializerForBackgroundCompilationFlags const flags_;
  BailoutId const osr_offset_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_SERIALIZER_FOR_BACKGROUND_COMPILATION_H_
