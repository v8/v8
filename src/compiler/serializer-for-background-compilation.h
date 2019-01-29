// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_SERIALIZER_FOR_BACKGROUND_COMPILATION_H_
#define V8_COMPILER_SERIALIZER_FOR_BACKGROUND_COMPILATION_H_

#include "src/base/optional.h"
#include "src/handles.h"
#include "src/maybe-handles.h"
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
  V(Abort)                        \
  V(CallRuntime)                  \
  V(CallRuntimeForPair)           \
  V(CreateBlockContext)           \
  V(CreateFunctionContext)        \
  V(CreateEvalContext)            \
  V(Debugger)                     \
  V(Jump)                         \
  V(JumpConstant)                 \
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
  V(JumpIfToBooleanTrueConstant)  \
  V(JumpIfToBooleanFalseConstant) \
  V(JumpIfToBooleanTrue)          \
  V(JumpIfToBooleanFalse)         \
  V(JumpIfTrue)                   \
  V(JumpIfTrueConstant)           \
  V(JumpIfUndefined)              \
  V(JumpIfUndefinedConstant)      \
  V(JumpLoop)                     \
  V(PushContext)                  \
  V(PopContext)                   \
  V(ResumeGenerator)              \
  V(ReThrow)                      \
  V(StaContextSlot)               \
  V(StaCurrentContextSlot)        \
  V(SuspendGenerator)             \
  V(SwitchOnGeneratorState)       \
  V(Throw)                        \
  V(ThrowReferenceErrorIfHole)    \
  V(ThrowSuperNotCalledIfHole)    \
  V(ThrowSuperAlreadyCalledIfNotHole)

#define CLEAR_ACCUMULATOR_LIST(V)   \
  V(CreateEmptyObjectLiteral)       \
  V(CreateMappedArguments)          \
  V(CreateRestParameter)            \
  V(CreateUnmappedArguments)        \
  V(LdaContextSlot)                 \
  V(LdaCurrentContextSlot)          \
  V(LdaGlobal)                      \
  V(LdaGlobalInsideTypeof)          \
  V(LdaImmutableContextSlot)        \
  V(LdaImmutableCurrentContextSlot) \
  V(LdaKeyedProperty)               \
  V(LdaNamedProperty)               \
  V(LdaNamedPropertyNoFeedback)

#define SUPPORTED_BYTECODE_LIST(V) \
  V(CallAnyReceiver)               \
  V(CallNoFeedback)                \
  V(CallProperty)                  \
  V(CallProperty0)                 \
  V(CallProperty1)                 \
  V(CallProperty2)                 \
  V(CallUndefinedReceiver)         \
  V(CallUndefinedReceiver0)        \
  V(CallUndefinedReceiver1)        \
  V(CallUndefinedReceiver2)        \
  V(CallWithSpread)                \
  V(Construct)                     \
  V(ConstructWithSpread)           \
  V(CreateClosure)                 \
  V(ExtraWide)                     \
  V(Illegal)                       \
  V(LdaConstant)                   \
  V(LdaNull)                       \
  V(Ldar)                          \
  V(LdaSmi)                        \
  V(LdaUndefined)                  \
  V(LdaZero)                       \
  V(Mov)                           \
  V(Return)                        \
  V(StackCheck)                    \
  V(Star)                          \
  V(Wide)                          \
  CLEAR_ENVIRONMENT_LIST(V)        \
  CLEAR_ACCUMULATOR_LIST(V)

class JSHeapBroker;

struct FunctionBlueprint {
  Handle<SharedFunctionInfo> shared;
  Handle<FeedbackVector> feedback_vector;
};

class CompilationSubject {
 public:
  explicit CompilationSubject(FunctionBlueprint blueprint)
      : blueprint_(blueprint) {}
  CompilationSubject(Handle<JSFunction> closure, Isolate* isolate);

  FunctionBlueprint blueprint() const { return blueprint_; }
  MaybeHandle<JSFunction> closure() const { return closure_; }

 private:
  FunctionBlueprint blueprint_;
  MaybeHandle<JSFunction> closure_;
};

class Hints {
 public:
  explicit Hints(Zone* zone);

  const ZoneVector<Handle<Object>>& constants() const;
  const ZoneVector<Handle<Map>>& maps() const;
  const ZoneVector<FunctionBlueprint>& function_blueprints() const;

  void AddConstant(Handle<Object> constant);
  void AddMap(Handle<Map> map);
  void AddFunctionBlueprint(FunctionBlueprint function_blueprint);

  void Add(const Hints& other);

  void Clear();
  bool IsEmpty() const;

 private:
  ZoneVector<Handle<Object>> constants_;
  ZoneVector<Handle<Map>> maps_;
  ZoneVector<FunctionBlueprint> function_blueprints_;
};

typedef ZoneVector<Hints> HintsVector;

// The SerializerForBackgroundCompilation makes sure that the relevant function
// data such as bytecode, SharedFunctionInfo and FeedbackVector, used by later
// optimizations in the compiler, is copied to the heap broker.
class SerializerForBackgroundCompilation {
 public:
  SerializerForBackgroundCompilation(JSHeapBroker* broker, Zone* zone,
                                     Handle<JSFunction> closure);
  Hints Run();  // NOTE: Returns empty for an already-serialized function.

 private:
  SerializerForBackgroundCompilation(JSHeapBroker* broker, Zone* zone,
                                     CompilationSubject function,
                                     base::Optional<Hints> new_target,
                                     const HintsVector& arguments);

  void TraverseBytecode();

#define DECLARE_VISIT_BYTECODE(name, ...) \
  void Visit##name(interpreter::BytecodeArrayIterator* iterator);
  SUPPORTED_BYTECODE_LIST(DECLARE_VISIT_BYTECODE)
#undef DECLARE_VISIT_BYTECODE

  class Environment;

  void ProcessCallOrConstruct(Hints callee, base::Optional<Hints> new_target,
                              const HintsVector& arguments, FeedbackSlot slot,
                              bool with_spread = false);
  void ProcessCallVarArgs(interpreter::BytecodeArrayIterator* iterator,
                          ConvertReceiverMode receiver_mode,
                          bool with_spread = false);

  Hints RunChildSerializer(CompilationSubject function,
                           base::Optional<Hints> new_target,
                           const HintsVector& arguments, bool with_spread);

  JSHeapBroker* broker() const { return broker_; }
  Zone* zone() const { return zone_; }
  Environment* environment() const { return environment_; }

  JSHeapBroker* const broker_;
  Zone* const zone_;
  Environment* const environment_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_SERIALIZER_FOR_BACKGROUND_COMPILATION_H_
