// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/serializer-for-background-compilation.h"

#include "src/compiler/js-heap-broker.h"
#include "src/handles-inl.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/objects/code.h"
#include "src/objects/shared-function-info-inl.h"
#include "src/vector-slot-pair.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

using BytecodeArrayIterator = interpreter::BytecodeArrayIterator;

CompilationSubject::CompilationSubject(Handle<JSFunction> closure,
                                       Isolate* isolate)
    : blueprint_{handle(closure->shared(), isolate),
                 handle(closure->feedback_vector(), isolate)},
      closure_(closure) {}

Hints::Hints(Zone* zone)
    : constants_(zone), maps_(zone), function_blueprints_(zone) {}

const ZoneVector<Handle<Object>>& Hints::constants() const {
  return constants_;
}

const ZoneVector<Handle<Map>>& Hints::maps() const { return maps_; }

const ZoneVector<FunctionBlueprint>& Hints::function_blueprints() const {
  return function_blueprints_;
}

void Hints::AddConstant(Handle<Object> constant) {
  constants_.push_back(constant);
}

void Hints::AddMap(Handle<Map> map) { maps_.push_back(map); }

void Hints::AddFunctionBlueprint(FunctionBlueprint function_blueprint) {
  function_blueprints_.push_back(function_blueprint);
}

void Hints::Add(const Hints& other) {
  for (auto x : other.constants()) AddConstant(x);
  for (auto x : other.maps()) AddMap(x);
  for (auto x : other.function_blueprints()) AddFunctionBlueprint(x);
}

bool Hints::IsEmpty() const {
  return constants().empty() && maps().empty() && function_blueprints().empty();
}

void Hints::Clear() {
  constants_.clear();
  maps_.clear();
  function_blueprints_.clear();
  DCHECK(IsEmpty());
}

class SerializerForBackgroundCompilation::Environment : public ZoneObject {
 public:
  Environment(Zone* zone, Isolate* isolate, CompilationSubject function);
  Environment(Zone* zone, Isolate* isolate, CompilationSubject function,
              base::Optional<Hints> new_target, const HintsVector& arguments);

  FunctionBlueprint function() const { return function_; }

  Hints& accumulator_hints() { return environment_hints_[accumulator_index()]; }
  Hints& register_hints(interpreter::Register reg) {
    int local_index = RegisterToLocalIndex(reg);
    DCHECK_LT(local_index, environment_hints_.size());
    return environment_hints_[local_index];
  }
  Hints& return_value_hints() { return return_value_hints_; }

  // Clears all hints except those for the return value and the closure.
  void ClearEphemeralHints() {
    DCHECK_EQ(environment_hints_.size(), function_closure_index() + 1);
    for (int i = 0; i < function_closure_index(); ++i) {
      environment_hints_[i].Clear();
    }
  }

  // Appends the hints for the given register range to {dst} (in order).
  void ExportRegisterHints(interpreter::Register first, size_t count,
                           HintsVector& dst);

 private:
  int RegisterToLocalIndex(interpreter::Register reg) const;

  Zone* zone() const { return zone_; }
  int parameter_count() const { return parameter_count_; }
  int register_count() const { return register_count_; }

  Zone* const zone_;
  // Instead of storing the blueprint here, we could extract it from the
  // (closure) hints but that would be cumbersome.
  FunctionBlueprint const function_;
  int const parameter_count_;
  int const register_count_;

  // environment_hints_ contains hints for the contents of the registers,
  // the accumulator and the parameters. The layout is as follows:
  // [ parameters | registers | accumulator | context | closure ]
  // The first parameter is the receiver.
  HintsVector environment_hints_;
  int accumulator_index() const { return parameter_count() + register_count(); }
  int current_context_index() const { return accumulator_index() + 1; }
  int function_closure_index() const { return current_context_index() + 1; }
  int environment_hints_size() const { return function_closure_index() + 1; }

  Hints return_value_hints_;
};

SerializerForBackgroundCompilation::Environment::Environment(
    Zone* zone, Isolate* isolate, CompilationSubject function)
    : zone_(zone),
      function_(function.blueprint()),
      parameter_count_(function_.shared->GetBytecodeArray()->parameter_count()),
      register_count_(function_.shared->GetBytecodeArray()->register_count()),
      environment_hints_(environment_hints_size(), Hints(zone), zone),
      return_value_hints_(zone) {
  Handle<JSFunction> closure;
  if (function.closure().ToHandle(&closure)) {
    environment_hints_[function_closure_index()].AddConstant(closure);
  } else {
    environment_hints_[function_closure_index()].AddFunctionBlueprint(
        function.blueprint());
  }
}

SerializerForBackgroundCompilation::Environment::Environment(
    Zone* zone, Isolate* isolate, CompilationSubject function,
    base::Optional<Hints> new_target, const HintsVector& arguments)
    : Environment(zone, isolate, function) {
  // Copy the hints for the actually passed arguments, at most up to
  // the parameter_count.
  size_t param_count = static_cast<size_t>(parameter_count());
  for (size_t i = 0; i < std::min(arguments.size(), param_count); ++i) {
    environment_hints_[i] = arguments[i];
  }

  // Pad the rest with "undefined".
  Hints undefined_hint(zone);
  undefined_hint.AddConstant(isolate->factory()->undefined_value());
  for (size_t i = arguments.size(); i < param_count; ++i) {
    environment_hints_[i] = undefined_hint;
  }

  interpreter::Register new_target_reg =
      function_.shared->GetBytecodeArray()
          ->incoming_new_target_or_generator_register();
  if (new_target_reg.is_valid()) {
    DCHECK(register_hints(new_target_reg).IsEmpty());
    if (new_target.has_value()) {
      register_hints(new_target_reg).Add(*new_target);
    }
  }
}

int SerializerForBackgroundCompilation::Environment::RegisterToLocalIndex(
    interpreter::Register reg) const {
  // TODO(mslekova): We also want to gather hints for the context.
  if (reg.is_current_context()) return current_context_index();
  if (reg.is_function_closure()) return function_closure_index();
  if (reg.is_parameter()) {
    return reg.ToParameterIndex(parameter_count());
  } else {
    return parameter_count() + reg.index();
  }
}

SerializerForBackgroundCompilation::SerializerForBackgroundCompilation(
    JSHeapBroker* broker, Zone* zone, Handle<JSFunction> closure)
    : broker_(broker),
      zone_(zone),
      environment_(new (zone) Environment(zone, broker_->isolate(),
                                          {closure, broker_->isolate()})) {
  JSFunctionRef(broker, closure).Serialize();
}

SerializerForBackgroundCompilation::SerializerForBackgroundCompilation(
    JSHeapBroker* broker, Zone* zone, CompilationSubject function,
    base::Optional<Hints> new_target, const HintsVector& arguments)
    : broker_(broker),
      zone_(zone),
      environment_(new (zone) Environment(zone, broker_->isolate(), function,
                                          new_target, arguments)) {
  Handle<JSFunction> closure;
  if (function.closure().ToHandle(&closure)) {
    JSFunctionRef(broker, closure).Serialize();
  }
}

Hints SerializerForBackgroundCompilation::Run() {
  SharedFunctionInfoRef shared(broker(), environment()->function().shared);
  FeedbackVectorRef feedback_vector(broker(),
                                    environment()->function().feedback_vector);
  if (shared.IsSerializedForCompilation(feedback_vector)) {
    return Hints(zone());
  }
  shared.SetSerializedForCompilation(feedback_vector);
  feedback_vector.SerializeSlots();
  TraverseBytecode();
  return environment()->return_value_hints();
}

void SerializerForBackgroundCompilation::TraverseBytecode() {
  BytecodeArrayRef bytecode_array(
      broker(), handle(environment()->function().shared->GetBytecodeArray(),
                       broker()->isolate()));
  BytecodeArrayIterator iterator(bytecode_array.object());

  for (; !iterator.done(); iterator.Advance()) {
    switch (iterator.current_bytecode()) {
#define DEFINE_BYTECODE_CASE(name)     \
  case interpreter::Bytecode::k##name: \
    Visit##name(&iterator);            \
    break;
      SUPPORTED_BYTECODE_LIST(DEFINE_BYTECODE_CASE)
#undef DEFINE_BYTECODE_CASE
      default: {
        environment()->ClearEphemeralHints();
        break;
      }
    }
  }
}

void SerializerForBackgroundCompilation::VisitIllegal(
    BytecodeArrayIterator* iterator) {
  UNREACHABLE();
}

void SerializerForBackgroundCompilation::VisitWide(
    BytecodeArrayIterator* iterator) {
  UNREACHABLE();
}

void SerializerForBackgroundCompilation::VisitExtraWide(
    BytecodeArrayIterator* iterator) {
  UNREACHABLE();
}

void SerializerForBackgroundCompilation::VisitStackCheck(
    BytecodeArrayIterator* iterator) {}

void SerializerForBackgroundCompilation::VisitLdaUndefined(
    BytecodeArrayIterator* iterator) {
  environment()->accumulator_hints().Clear();
  environment()->accumulator_hints().AddConstant(
      broker()->isolate()->factory()->undefined_value());
}

void SerializerForBackgroundCompilation::VisitLdaNull(
    BytecodeArrayIterator* iterator) {
  environment()->accumulator_hints().Clear();
  environment()->accumulator_hints().AddConstant(
      broker()->isolate()->factory()->null_value());
}

void SerializerForBackgroundCompilation::VisitLdaZero(
    BytecodeArrayIterator* iterator) {
  environment()->accumulator_hints().Clear();
  environment()->accumulator_hints().AddConstant(
      handle(Smi::FromInt(0), broker()->isolate()));
}

void SerializerForBackgroundCompilation::VisitLdaSmi(
    BytecodeArrayIterator* iterator) {
  environment()->accumulator_hints().Clear();
  environment()->accumulator_hints().AddConstant(handle(
      Smi::FromInt(iterator->GetImmediateOperand(0)), broker()->isolate()));
}

void SerializerForBackgroundCompilation::VisitLdaConstant(
    BytecodeArrayIterator* iterator) {
  environment()->accumulator_hints().Clear();
  environment()->accumulator_hints().AddConstant(
      handle(iterator->GetConstantForIndexOperand(0), broker()->isolate()));
}

void SerializerForBackgroundCompilation::VisitLdar(
    BytecodeArrayIterator* iterator) {
  environment()->accumulator_hints().Clear();
  environment()->accumulator_hints().Add(
      environment()->register_hints(iterator->GetRegisterOperand(0)));
}

void SerializerForBackgroundCompilation::VisitStar(
    BytecodeArrayIterator* iterator) {
  interpreter::Register reg = iterator->GetRegisterOperand(0);
  environment()->register_hints(reg).Clear();
  environment()->register_hints(reg).Add(environment()->accumulator_hints());
}

void SerializerForBackgroundCompilation::VisitMov(
    BytecodeArrayIterator* iterator) {
  interpreter::Register src = iterator->GetRegisterOperand(0);
  interpreter::Register dst = iterator->GetRegisterOperand(1);
  environment()->register_hints(dst).Clear();
  environment()->register_hints(dst).Add(environment()->register_hints(src));
}

void SerializerForBackgroundCompilation::VisitCreateClosure(
    BytecodeArrayIterator* iterator) {
  Handle<SharedFunctionInfo> shared(
      SharedFunctionInfo::cast(iterator->GetConstantForIndexOperand(0)),
      broker()->isolate());

  FeedbackNexus nexus(environment()->function().feedback_vector,
                      iterator->GetSlotOperand(1));
  Handle<Object> cell_value(nexus.GetFeedbackCell()->value(),
                            broker()->isolate());

  environment()->accumulator_hints().Clear();
  if (cell_value->IsFeedbackVector()) {
    environment()->accumulator_hints().AddFunctionBlueprint(
        {shared, Handle<FeedbackVector>::cast(cell_value)});
  }
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver(
    BytecodeArrayIterator* iterator) {
  ProcessCallVarArgs(iterator, ConvertReceiverMode::kNullOrUndefined);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver0(
    BytecodeArrayIterator* iterator) {
  const Hints& callee =
      environment()->register_hints(iterator->GetRegisterOperand(0));
  FeedbackSlot slot = FeedbackVector::ToSlot(iterator->GetIndexOperand(1));

  Hints receiver(zone());
  receiver.AddConstant(broker()->isolate()->factory()->undefined_value());

  HintsVector parameters({receiver}, zone());
  ProcessCallOrConstruct(callee, base::nullopt, parameters, slot);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver1(
    BytecodeArrayIterator* iterator) {
  const Hints& callee =
      environment()->register_hints(iterator->GetRegisterOperand(0));
  const Hints& arg0 =
      environment()->register_hints(iterator->GetRegisterOperand(1));
  FeedbackSlot slot = FeedbackVector::ToSlot(iterator->GetIndexOperand(2));

  Hints receiver(zone());
  receiver.AddConstant(broker()->isolate()->factory()->undefined_value());

  HintsVector parameters({receiver, arg0}, zone());
  ProcessCallOrConstruct(callee, base::nullopt, parameters, slot);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver2(
    BytecodeArrayIterator* iterator) {
  const Hints& callee =
      environment()->register_hints(iterator->GetRegisterOperand(0));
  const Hints& arg0 =
      environment()->register_hints(iterator->GetRegisterOperand(1));
  const Hints& arg1 =
      environment()->register_hints(iterator->GetRegisterOperand(2));
  FeedbackSlot slot = FeedbackVector::ToSlot(iterator->GetIndexOperand(3));

  Hints receiver(zone());
  receiver.AddConstant(broker()->isolate()->factory()->undefined_value());

  HintsVector parameters({receiver, arg0, arg1}, zone());
  ProcessCallOrConstruct(callee, base::nullopt, parameters, slot);
}

void SerializerForBackgroundCompilation::VisitCallAnyReceiver(
    BytecodeArrayIterator* iterator) {
  ProcessCallVarArgs(iterator, ConvertReceiverMode::kAny);
}

void SerializerForBackgroundCompilation::VisitCallNoFeedback(
    BytecodeArrayIterator* iterator) {
  ProcessCallVarArgs(iterator, ConvertReceiverMode::kNullOrUndefined);
}

void SerializerForBackgroundCompilation::VisitCallProperty(
    BytecodeArrayIterator* iterator) {
  ProcessCallVarArgs(iterator, ConvertReceiverMode::kNullOrUndefined);
}

void SerializerForBackgroundCompilation::VisitCallProperty0(
    BytecodeArrayIterator* iterator) {
  const Hints& callee =
      environment()->register_hints(iterator->GetRegisterOperand(0));
  const Hints& receiver =
      environment()->register_hints(iterator->GetRegisterOperand(1));
  FeedbackSlot slot = FeedbackVector::ToSlot(iterator->GetIndexOperand(2));

  HintsVector parameters({receiver}, zone());
  ProcessCallOrConstruct(callee, base::nullopt, parameters, slot);
}

void SerializerForBackgroundCompilation::VisitCallProperty1(
    BytecodeArrayIterator* iterator) {
  const Hints& callee =
      environment()->register_hints(iterator->GetRegisterOperand(0));
  const Hints& receiver =
      environment()->register_hints(iterator->GetRegisterOperand(1));
  const Hints& arg0 =
      environment()->register_hints(iterator->GetRegisterOperand(2));
  FeedbackSlot slot = FeedbackVector::ToSlot(iterator->GetIndexOperand(3));

  HintsVector parameters({receiver, arg0}, zone());
  ProcessCallOrConstruct(callee, base::nullopt, parameters, slot);
}

void SerializerForBackgroundCompilation::VisitCallProperty2(
    BytecodeArrayIterator* iterator) {
  const Hints& callee =
      environment()->register_hints(iterator->GetRegisterOperand(0));
  const Hints& receiver =
      environment()->register_hints(iterator->GetRegisterOperand(1));
  const Hints& arg0 =
      environment()->register_hints(iterator->GetRegisterOperand(2));
  const Hints& arg1 =
      environment()->register_hints(iterator->GetRegisterOperand(3));
  FeedbackSlot slot = FeedbackVector::ToSlot(iterator->GetIndexOperand(4));

  HintsVector parameters({receiver, arg0, arg1}, zone());
  ProcessCallOrConstruct(callee, base::nullopt, parameters, slot);
}

void SerializerForBackgroundCompilation::VisitCallWithSpread(
    BytecodeArrayIterator* iterator) {
  ProcessCallVarArgs(iterator, ConvertReceiverMode::kAny, true);
}

Hints SerializerForBackgroundCompilation::RunChildSerializer(
    CompilationSubject function, base::Optional<Hints> new_target,
    const HintsVector& arguments, bool with_spread) {
  if (with_spread) {
    DCHECK_LT(0, arguments.size());
    // Pad the missing arguments in case we were called with spread operator.
    // Drop the last actually passed argument, which contains the spread.
    // We don't know what the spread element produces. Therefore we pretend
    // that the function is called with the maximal number of parameters and
    // that we have no information about the parameters that were not
    // explicitly provided.
    HintsVector padded = arguments;
    padded.pop_back();  // Remove the spread element.
    // Fill the rest with empty hints.
    padded.resize(
        function.blueprint().shared->GetBytecodeArray()->parameter_count(),
        Hints(zone()));
    return RunChildSerializer(function, new_target, padded, false);
  }

  SerializerForBackgroundCompilation child_serializer(
      broker(), zone(), function, new_target, arguments);
  return child_serializer.Run();
}

namespace {
base::Optional<HeapObjectRef> GetHeapObjectFeedback(
    JSHeapBroker* broker, Handle<FeedbackVector> feedback_vector,
    FeedbackSlot slot) {
  if (slot.IsInvalid()) return base::nullopt;
  FeedbackNexus nexus(feedback_vector, slot);
  VectorSlotPair feedback(feedback_vector, slot, nexus.ic_state());
  DCHECK(feedback.IsValid());
  if (nexus.IsUninitialized()) return base::nullopt;
  HeapObject object;
  if (!nexus.GetFeedback()->GetHeapObject(&object)) return base::nullopt;
  return HeapObjectRef(broker, handle(object, broker->isolate()));
}
}  // namespace

void SerializerForBackgroundCompilation::ProcessCallOrConstruct(
    Hints callee, base::Optional<Hints> new_target,
    const HintsVector& arguments, FeedbackSlot slot, bool with_spread) {
  // Incorporate feedback into hints.
  base::Optional<HeapObjectRef> feedback = GetHeapObjectFeedback(
      broker(), environment()->function().feedback_vector, slot);
  if (feedback.has_value() && feedback->map().is_callable()) {
    if (new_target.has_value()) {
      // Construct; feedback is new_target.
      new_target->AddConstant(feedback->object());
    } else {
      // Call; feedback is callee.
      callee.AddConstant(feedback->object());
    }
  }

  environment()->accumulator_hints().Clear();

  for (auto hint : callee.constants()) {
    if (!hint->IsJSFunction()) continue;

    Handle<JSFunction> function = Handle<JSFunction>::cast(hint);
    if (!function->shared()->IsInlineable()) continue;

    environment()->accumulator_hints().Add(RunChildSerializer(
        {function, broker()->isolate()}, new_target, arguments, with_spread));
  }

  for (auto hint : callee.function_blueprints()) {
    if (!hint.shared->IsInlineable()) continue;
    environment()->accumulator_hints().Add(RunChildSerializer(
        CompilationSubject(hint), new_target, arguments, with_spread));
  }
}

void SerializerForBackgroundCompilation::ProcessCallVarArgs(
    BytecodeArrayIterator* iterator, ConvertReceiverMode receiver_mode,
    bool with_spread) {
  const Hints& callee =
      environment()->register_hints(iterator->GetRegisterOperand(0));
  interpreter::Register first_reg = iterator->GetRegisterOperand(1);
  int reg_count = static_cast<int>(iterator->GetRegisterCountOperand(2));
  FeedbackSlot slot;
  if (iterator->current_bytecode() != interpreter::Bytecode::kCallNoFeedback) {
    slot = FeedbackVector::ToSlot(iterator->GetIndexOperand(3));
  }

  HintsVector arguments(zone());
  // The receiver is either given in the first register or it is implicitly
  // the {undefined} value.
  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    Hints receiver(zone());
    receiver.AddConstant(broker()->isolate()->factory()->undefined_value());
    arguments.push_back(receiver);
  }
  environment()->ExportRegisterHints(first_reg, reg_count, arguments);

  ProcessCallOrConstruct(callee, base::nullopt, arguments, slot);
}

void SerializerForBackgroundCompilation::VisitReturn(
    BytecodeArrayIterator* iterator) {
  environment()->return_value_hints().Add(environment()->accumulator_hints());
  environment()->ClearEphemeralHints();
}

void SerializerForBackgroundCompilation::Environment::ExportRegisterHints(
    interpreter::Register first, size_t count, HintsVector& dst) {
  dst.resize(dst.size() + count, Hints(zone()));
  int reg_base = first.index();
  for (int i = 0; i < static_cast<int>(count); ++i) {
    dst.push_back(register_hints(interpreter::Register(reg_base + i)));
  }
}

void SerializerForBackgroundCompilation::VisitConstruct(
    BytecodeArrayIterator* iterator) {
  const Hints& callee =
      environment()->register_hints(iterator->GetRegisterOperand(0));
  interpreter::Register first_reg = iterator->GetRegisterOperand(1);
  size_t reg_count = iterator->GetRegisterCountOperand(2);
  FeedbackSlot slot = FeedbackVector::ToSlot(iterator->GetIndexOperand(3));
  const Hints& new_target = environment()->accumulator_hints();

  HintsVector arguments(zone());
  environment()->ExportRegisterHints(first_reg, reg_count, arguments);

  ProcessCallOrConstruct(callee, new_target, arguments, slot);
}

void SerializerForBackgroundCompilation::VisitConstructWithSpread(
    BytecodeArrayIterator* iterator) {
  const Hints& callee =
      environment()->register_hints(iterator->GetRegisterOperand(0));
  interpreter::Register first_reg = iterator->GetRegisterOperand(1);
  size_t reg_count = iterator->GetRegisterCountOperand(2);
  FeedbackSlot slot = FeedbackVector::ToSlot(iterator->GetIndexOperand(3));
  const Hints& new_target = environment()->accumulator_hints();

  HintsVector arguments(zone());
  environment()->ExportRegisterHints(first_reg, reg_count, arguments);

  ProcessCallOrConstruct(callee, new_target, arguments, slot, true);
}

#define DEFINE_SKIPPED_JUMP(name, ...)                  \
  void SerializerForBackgroundCompilation::Visit##name( \
      BytecodeArrayIterator* iterator) {                \
    environment()->ClearEphemeralHints();               \
  }
CLEAR_ENVIRONMENT_LIST(DEFINE_SKIPPED_JUMP)
#undef DEFINE_SKIPPED_JUMP

#define DEFINE_CLEAR_ACCUMULATOR(name, ...)             \
  void SerializerForBackgroundCompilation::Visit##name( \
      BytecodeArrayIterator* iterator) {                \
    environment()->accumulator_hints().Clear();         \
  }
CLEAR_ACCUMULATOR_LIST(DEFINE_CLEAR_ACCUMULATOR)
#undef DEFINE_CLEAR_ACCUMULATOR

}  // namespace compiler
}  // namespace internal
}  // namespace v8
