// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/serializer-for-background-compilation.h"

#include "src/compiler/js-heap-broker.h"
#include "src/handles-inl.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/objects/code.h"
#include "src/objects/shared-function-info-inl.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

class SerializerForBackgroundCompilation::Environment : public ZoneObject {
 public:
  explicit Environment(Zone* zone, Isolate* isolate, int register_count,
                       int parameter_count);

  Environment(SerializerForBackgroundCompilation* serializer, Isolate* isolate,
              int register_count, int parameter_count, Hints receiver,
              const HintsVector& arguments);

  int parameter_count() const { return parameter_count_; }
  int register_count() const { return register_count_; }

  // Remove all hints except those for the return value.
  void Clear();

  // Getters for the hints gathered until now (constants or maps).
  const Hints& LookupAccumulator();
  const Hints& LookupReturnValue();
  const Hints& LookupRegister(interpreter::Register the_register);

  // Setters for hints.
  void AddAccumulatorHints(const Hints& hints);
  void ReplaceAccumulatorHint(Handle<Object> hint);
  void SetAccumulatorHints(const Hints& hints);
  void ClearAccumulatorHints();
  void AddReturnValueHints(const Hints& hints);
  void SetRegisterHints(interpreter::Register the_register, const Hints& hints);

 private:
  explicit Environment(Zone* zone)
      : environment_hints_(zone),
        return_value_hints_(zone),
        register_count_(0),
        parameter_count_(0),
        empty_hints_(Hints(zone)) {}
  Zone* zone() const { return zone_; }

  int RegisterToLocalIndex(interpreter::Register the_register) const;

  Zone* zone_;

  // environment_hints_ contains best-effort guess for state of the registers,
  // the accumulator and the parameters. The structure is inspired by
  // BytecodeGraphBuilder::Environment and looks like:
  // hints: receiver | parameters | registers | accumulator
  // indices:    0         register_base() accumulator_base()

  HintsVector environment_hints_;
  Hints return_value_hints_;
  const int register_count_;
  const int parameter_count_;
  int register_base() const { return parameter_count_ + 1; }
  int accumulator_base() const { return register_base() + register_count_; }
  static const int kReceiverIndex = 0;
  static const int kParameterBase = 1;
  const Hints empty_hints_;
};

SerializerForBackgroundCompilation::Environment::Environment(
    Zone* zone, Isolate* isolate, int register_count, int parameter_count)
    : zone_(zone),
      environment_hints_(register_count + parameter_count + 2, Hints(zone),
                         zone),
      return_value_hints_(zone),
      register_count_(register_count),
      parameter_count_(parameter_count),
      empty_hints_(Hints(zone)) {}

SerializerForBackgroundCompilation::Environment::Environment(
    SerializerForBackgroundCompilation* serializer, Isolate* isolate,
    int register_count, int parameter_count, Hints receiver,
    const HintsVector& arguments)
    : Environment(serializer->zone(), isolate, register_count,
                  parameter_count) {
  environment_hints_[kReceiverIndex] = receiver;

  size_t param_count = static_cast<size_t>(parameter_count);

  // Copy the hints for the actually passed arguments, at most up to
  // the parameter_count.
  for (size_t i = 0; i < std::min(arguments.size(), param_count); ++i) {
    environment_hints_[kParameterBase + i] = arguments[i];
  }

  Hints undefined_hint(serializer->zone());
  undefined_hint.push_back(
      serializer->broker()->isolate()->factory()->undefined_value());
  // Pad the rest with "undefined".
  for (size_t i = arguments.size(); i < param_count; ++i) {
    environment_hints_[kParameterBase + i] = undefined_hint;
  }
}

int SerializerForBackgroundCompilation::Environment::RegisterToLocalIndex(
    interpreter::Register the_register) const {
  if (the_register.is_parameter()) {
    return kParameterBase + the_register.ToParameterIndex(parameter_count());
  } else {
    return register_base() + the_register.index();
  }
}

void SerializerForBackgroundCompilation::Environment::Clear() {
  environment_hints_ =
      HintsVector(environment_hints_.size(), empty_hints_, zone());
}

const Hints&
SerializerForBackgroundCompilation::Environment::LookupAccumulator() {
  return environment_hints_[accumulator_base()];
}

const Hints&
SerializerForBackgroundCompilation::Environment::LookupReturnValue() {
  return return_value_hints_;
}

const Hints& SerializerForBackgroundCompilation::Environment::LookupRegister(
    interpreter::Register the_register) {
  // TODO(mslekova): We also want to gather hints for the context and
  // we already have data about the closure, so eventually more useful
  // info should be returned here instead of empty hints.
  if (the_register.is_current_context() || the_register.is_function_closure()) {
    return empty_hints_;
  }
  int local_index = RegisterToLocalIndex(the_register);
  DCHECK(static_cast<size_t>(local_index) < environment_hints_.size());
  return environment_hints_[local_index];
}

void SerializerForBackgroundCompilation::Environment::AddAccumulatorHints(
    const Hints& hints) {
  for (auto v : hints) {
    environment_hints_[accumulator_base()].push_back(v);
  }
}

void SerializerForBackgroundCompilation::Environment::ReplaceAccumulatorHint(
    Handle<Object> hint) {
  ClearAccumulatorHints();
  environment_hints_[accumulator_base()].push_back(hint);
}

void SerializerForBackgroundCompilation::Environment::ClearAccumulatorHints() {
  SetAccumulatorHints(empty_hints_);
}

void SerializerForBackgroundCompilation::Environment::SetAccumulatorHints(
    const Hints& hints) {
  environment_hints_[accumulator_base()] = hints;
}

void SerializerForBackgroundCompilation::Environment::AddReturnValueHints(
    const Hints& hints) {
  for (auto v : hints) {
    return_value_hints_.push_back(v);
  }
}

void SerializerForBackgroundCompilation::Environment::SetRegisterHints(
    interpreter::Register the_register, const Hints& hints) {
  int local_index = RegisterToLocalIndex(the_register);
  DCHECK(static_cast<size_t>(local_index) < environment_hints_.size());
  environment_hints_[local_index] = hints;
}

SerializerForBackgroundCompilation::SerializerForBackgroundCompilation(
    JSHeapBroker* broker, Zone* zone, Handle<JSFunction> function)
    : broker_(broker),
      zone_(zone),
      sfi_(function->shared(), broker->isolate()),
      feedback_(function->feedback_vector(), broker->isolate()),
      environment_(new (zone) Environment(
          zone, broker_->isolate(), sfi_->GetBytecodeArray()->register_count(),
          sfi_->GetBytecodeArray()->parameter_count())) {
  JSFunctionRef(broker, function).Serialize();
}

SerializerForBackgroundCompilation::SerializerForBackgroundCompilation(
    JSHeapBroker* broker, Zone* zone, Handle<SharedFunctionInfo> sfi,
    Handle<FeedbackVector> feedback, const Hints& receiver,
    const HintsVector& arguments)
    : broker_(broker),
      zone_(zone),
      sfi_(sfi),
      feedback_(feedback),
      environment_(new (zone) Environment(
          this, broker->isolate(), sfi->GetBytecodeArray()->register_count(),
          sfi->GetBytecodeArray()->parameter_count(), receiver, arguments)) {}

Hints SerializerForBackgroundCompilation::Run() {
  SharedFunctionInfoRef sfi(broker(), sfi_);
  FeedbackVectorRef feedback(broker(), feedback_);
  if (sfi.IsSerializedForCompilation(feedback)) {
    return Hints(zone());
  }
  sfi.SetSerializedForCompilation(feedback);
  feedback.SerializeSlots();
  TraverseBytecode();
  return environment()->LookupReturnValue();
}

void SerializerForBackgroundCompilation::TraverseBytecode() {
  BytecodeArrayRef bytecode_array(
      broker(), handle(sfi_->GetBytecodeArray(), broker()->isolate()));
  interpreter::BytecodeArrayIterator iterator(bytecode_array.object());

  for (; !iterator.done(); iterator.Advance()) {
    switch (iterator.current_bytecode()) {
#define DEFINE_BYTECODE_CASE(name)     \
  case interpreter::Bytecode::k##name: \
    Visit##name(&iterator);            \
    break;
      SUPPORTED_BYTECODE_LIST(DEFINE_BYTECODE_CASE)
#undef DEFINE_BYTECODE_CASE
      default: {
        environment()->Clear();
        break;
      }
    }
  }
}

void SerializerForBackgroundCompilation::VisitIllegal(
    interpreter::BytecodeArrayIterator* iterator) {
  UNREACHABLE();
}

void SerializerForBackgroundCompilation::VisitWide(
    interpreter::BytecodeArrayIterator* iterator) {
  UNREACHABLE();
}

void SerializerForBackgroundCompilation::VisitExtraWide(
    interpreter::BytecodeArrayIterator* iterator) {
  UNREACHABLE();
}

void SerializerForBackgroundCompilation::VisitLdaUndefined(
    interpreter::BytecodeArrayIterator* iterator) {
  environment()->ReplaceAccumulatorHint(
      broker()->isolate()->factory()->undefined_value());
}

void SerializerForBackgroundCompilation::VisitLdaNull(
    interpreter::BytecodeArrayIterator* iterator) {
  environment()->ReplaceAccumulatorHint(
      broker()->isolate()->factory()->null_value());
}

void SerializerForBackgroundCompilation::VisitLdaZero(
    interpreter::BytecodeArrayIterator* iterator) {
  Handle<Object> zero(Smi::FromInt(0), broker()->isolate());
  environment()->ReplaceAccumulatorHint(zero);
}

void SerializerForBackgroundCompilation::VisitLdaSmi(
    interpreter::BytecodeArrayIterator* iterator) {
  Handle<Object> smi(Smi::FromInt(iterator->GetImmediateOperand(0)),
                     broker()->isolate());
  environment()->ReplaceAccumulatorHint(smi);
}

void SerializerForBackgroundCompilation::VisitLdaConstant(
    interpreter::BytecodeArrayIterator* iterator) {
  Handle<Object> constant(iterator->GetConstantForIndexOperand(0),
                          broker()->isolate());
  environment()->ReplaceAccumulatorHint(constant);
}

void SerializerForBackgroundCompilation::VisitLdar(
    interpreter::BytecodeArrayIterator* iterator) {
  const Hints& hints =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));
  environment()->SetAccumulatorHints(hints);
}

void SerializerForBackgroundCompilation::VisitStar(
    interpreter::BytecodeArrayIterator* iterator) {
  const Hints& hints = environment()->LookupAccumulator();
  environment()->SetRegisterHints(iterator->GetRegisterOperand(0), hints);
}

void SerializerForBackgroundCompilation::VisitMov(
    interpreter::BytecodeArrayIterator* iterator) {
  const Hints& hints =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));
  environment()->SetRegisterHints(iterator->GetRegisterOperand(1), hints);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver(
    interpreter::BytecodeArrayIterator* iterator) {
  ProcessCallVarArgs(iterator, ConvertReceiverMode::kNullOrUndefined);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver0(
    interpreter::BytecodeArrayIterator* iterator) {
  Hints receiver(zone());
  receiver.push_back(broker()->isolate()->factory()->undefined_value());

  const Hints& callee =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));

  HintsVector parameters(zone());

  ProcessCallOrConstruct(callee, receiver, parameters);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver1(
    interpreter::BytecodeArrayIterator* iterator) {
  Hints receiver(zone());
  receiver.push_back(broker()->isolate()->factory()->undefined_value());

  const Hints& callee =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));

  const Hints& arg0 =
      environment()->LookupRegister(iterator->GetRegisterOperand(1));

  HintsVector parameters(zone());
  parameters.push_back(arg0);

  ProcessCallOrConstruct(callee, receiver, parameters);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver2(
    interpreter::BytecodeArrayIterator* iterator) {
  Hints receiver(zone());
  receiver.push_back(broker()->isolate()->factory()->undefined_value());

  const Hints& callee =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));

  const Hints& arg0 =
      environment()->LookupRegister(iterator->GetRegisterOperand(1));
  const Hints& arg1 =
      environment()->LookupRegister(iterator->GetRegisterOperand(2));

  HintsVector parameters(zone());
  parameters.push_back(arg0);
  parameters.push_back(arg1);

  ProcessCallOrConstruct(callee, receiver, parameters);
}

void SerializerForBackgroundCompilation::VisitCallAnyReceiver(
    interpreter::BytecodeArrayIterator* iterator) {
  ProcessCallVarArgs(iterator, ConvertReceiverMode::kAny);
}

void SerializerForBackgroundCompilation::VisitCallNoFeedback(
    interpreter::BytecodeArrayIterator* iterator) {
  ProcessCallVarArgs(iterator, ConvertReceiverMode::kNullOrUndefined);
}

void SerializerForBackgroundCompilation::VisitCallProperty(
    interpreter::BytecodeArrayIterator* iterator) {
  ProcessCallVarArgs(iterator, ConvertReceiverMode::kNullOrUndefined);
}

void SerializerForBackgroundCompilation::VisitCallProperty0(
    interpreter::BytecodeArrayIterator* iterator) {
  const Hints& callee =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));
  const Hints& receiver =
      environment()->LookupRegister(iterator->GetRegisterOperand(1));

  HintsVector parameters(zone());

  ProcessCallOrConstruct(callee, receiver, parameters);
}

void SerializerForBackgroundCompilation::VisitCallProperty1(
    interpreter::BytecodeArrayIterator* iterator) {
  const Hints& callee =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));
  const Hints& receiver =
      environment()->LookupRegister(iterator->GetRegisterOperand(1));
  const Hints& arg0 =
      environment()->LookupRegister(iterator->GetRegisterOperand(2));

  HintsVector parameters(zone());
  parameters.push_back(arg0);

  ProcessCallOrConstruct(callee, receiver, parameters);
}

void SerializerForBackgroundCompilation::VisitCallProperty2(
    interpreter::BytecodeArrayIterator* iterator) {
  const Hints& callee =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));
  const Hints& receiver =
      environment()->LookupRegister(iterator->GetRegisterOperand(1));
  const Hints& arg0 =
      environment()->LookupRegister(iterator->GetRegisterOperand(2));
  const Hints& arg1 =
      environment()->LookupRegister(iterator->GetRegisterOperand(3));

  HintsVector parameters(zone());
  parameters.push_back(arg0);
  parameters.push_back(arg1);

  ProcessCallOrConstruct(callee, receiver, parameters);
}

void SerializerForBackgroundCompilation::ProcessCallOrConstruct(
    const Hints& callee, const Hints& receiver, const HintsVector& arguments) {
  environment()->ClearAccumulatorHints();

  for (auto hint : callee) {
    if (!hint->IsJSFunction()) continue;

    Handle<JSFunction> function = Handle<JSFunction>::cast(hint);
    if (!function->shared()->IsInlineable()) continue;

    JSFunctionRef(broker(), function).Serialize();

    Handle<SharedFunctionInfo> sfi(function->shared(), broker()->isolate());
    Handle<FeedbackVector> feedback(function->feedback_vector(),
                                    broker()->isolate());
    SerializerForBackgroundCompilation child_serializer(
        broker(), zone(), sfi, feedback, receiver, arguments);
    environment()->AddAccumulatorHints(child_serializer.Run());
  }
}

void SerializerForBackgroundCompilation::ProcessCallVarArgs(
    interpreter::BytecodeArrayIterator* iterator,
    ConvertReceiverMode receiver_mode) {
  const Hints& callee =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));

  interpreter::Register first_reg = iterator->GetRegisterOperand(1);
  size_t reg_count = iterator->GetRegisterCountOperand(2);

  Hints receiver(zone());
  interpreter::Register first_arg;

  int arg_count;
  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    // The receiver is implicit (and undefined), the arguments are in
    // consecutive registers.
    arg_count = static_cast<int>(reg_count);
    receiver.push_back(broker()->isolate()->factory()->undefined_value());
    first_arg = first_reg;
  } else {
    // The receiver is the first register, followed by the arguments in the
    // consecutive registers.
    arg_count = static_cast<int>(reg_count) - 1;
    receiver = environment()->LookupRegister(first_reg);
    first_arg = interpreter::Register(first_reg.index() + 1);
  }

  HintsVector arguments(zone());

  // The function arguments are in consecutive registers.
  int arg_base = first_arg.index();
  for (int i = 0; i < arg_count; ++i) {
    arguments.push_back(
        environment()->LookupRegister(interpreter::Register(arg_base + i)));
  }

  ProcessCallOrConstruct(callee, receiver, arguments);
}

void SerializerForBackgroundCompilation::VisitReturn(
    interpreter::BytecodeArrayIterator* iterator) {
  environment()->AddReturnValueHints(environment()->LookupAccumulator());
  environment()->Clear();
}

void SerializerForBackgroundCompilation::VisitConstruct(
    interpreter::BytecodeArrayIterator* iterator) {
  const Hints& callee =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));

  interpreter::Register first_reg = iterator->GetRegisterOperand(1);
  size_t reg_count = iterator->GetRegisterCountOperand(2);

  Hints receiver = environment()->LookupRegister(first_reg);
  interpreter::Register first_arg =
      interpreter::Register(first_reg.index() + 1);

  int arg_count = static_cast<int>(reg_count) - 1;

  HintsVector arguments(zone());
  // Push the target of the construct.
  arguments.push_back(callee);

  // The function arguments are in consecutive registers.
  int arg_base = first_arg.index();
  for (int i = 0; i < arg_count; ++i) {
    arguments.push_back(
        environment()->LookupRegister(interpreter::Register(arg_base + i)));
  }
  // Push the new_target of the construct.
  arguments.push_back(environment()->LookupAccumulator());

  ProcessCallOrConstruct(callee, receiver, arguments);
}

#define DEFINE_SKIPPED_JUMP(name, ...)                  \
  void SerializerForBackgroundCompilation::Visit##name( \
      interpreter::BytecodeArrayIterator* iterator) {   \
    environment()->Clear();                             \
  }
CLEAR_ENVIRONMENT_LIST(DEFINE_SKIPPED_JUMP)
#undef DEFINE_SKIPPED_JUMP

#define DEFINE_CLEAR_ACCUMULATOR(name, ...)             \
  void SerializerForBackgroundCompilation::Visit##name( \
      interpreter::BytecodeArrayIterator* iterator) {   \
    environment()->ClearAccumulatorHints();             \
  }
CLEAR_ACCUMULATOR_LIST(DEFINE_CLEAR_ACCUMULATOR)
#undef DEFINE_CLEAR_ACCUMULATOR

}  // namespace compiler
}  // namespace internal
}  // namespace v8
