// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_SERIALIZER_FOR_BACKGROUND_COMPILATION_H_
#define V8_COMPILER_SERIALIZER_FOR_BACKGROUND_COMPILATION_H_

#include "src/handles.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

namespace interpreter {
class BytecodeArrayIterator;
class Register;
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
  V(ReThrow)                      \
  V(StaContextSlot)               \
  V(StaCurrentContextSlot)        \
  V(Throw)

#define CLEAR_ACCUMULATOR_LIST(V)   \
  V(CallWithSpread)                 \
  V(ConstructWithSpread)            \
  V(CreateClosure)                  \
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
  V(Construct)                     \
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
  V(Star)                          \
  V(Wide)                          \
  CLEAR_ENVIRONMENT_LIST(V)        \
  CLEAR_ACCUMULATOR_LIST(V)

class JSHeapBroker;
typedef ZoneVector<Handle<Object>> Hints;
typedef ZoneVector<Hints> HintsVector;

// The SerializerForBackgroundCompilation makes sure that the relevant function
// data such as bytecode, SharedFunctionInfo and FeedbackVector, used by later
// optimizations in the compiler, is copied to the heap broker.
class SerializerForBackgroundCompilation {
 public:
  class Environment;

  SerializerForBackgroundCompilation(JSHeapBroker* broker, Zone* zone,
                                     Handle<JSFunction> closure);
  SerializerForBackgroundCompilation(JSHeapBroker* broker, Zone* zone,
                                     Handle<JSFunction> closure,
                                     const Hints& receiver,
                                     const HintsVector& arguments);

  Hints Run();

  Zone* zone() const { return zone_; }

 private:
  void TraverseBytecode();

#define DECLARE_VISIT_BYTECODE(name, ...) \
  void Visit##name(interpreter::BytecodeArrayIterator* iterator);
  SUPPORTED_BYTECODE_LIST(DECLARE_VISIT_BYTECODE)
#undef DECLARE_VISIT_BYTECODE

  JSHeapBroker* broker() const { return broker_; }
  Environment* environment() const { return environment_; }

  void ProcessCallOrConstruct(const Hints& callee, const Hints& receiver,
                              const HintsVector& arguments);
  void ProcessCallVarArgs(interpreter::BytecodeArrayIterator* iterator,
                          ConvertReceiverMode receiver_mode);

  JSHeapBroker* broker_;
  Zone* zone_;
  Environment* environment_;

  Handle<JSFunction> closure_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_SERIALIZER_FOR_BACKGROUND_COMPILATION_H_
