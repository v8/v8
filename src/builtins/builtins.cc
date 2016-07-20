// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins.h"
#include "src/builtins/builtins-utils.h"

#include "src/api-arguments.h"
#include "src/api-natives.h"
#include "src/code-factory.h"
#include "src/ic/handler-compiler.h"
#include "src/ic/ic.h"
#include "src/vm-state-inl.h"

namespace v8 {
namespace internal {

// Forward declarations for C++ builtins.
#define FORWARD_DECLARE(Name) \
  Object* Builtin_##Name(int argc, Object** args, Isolate* isolate);
BUILTIN_LIST_C(FORWARD_DECLARE)
#undef FORWARD_DECLARE

// -----------------------------------------------------------------------------
// ES6 section 19.2 Function Objects

// ES6 section 19.2.3.6 Function.prototype [ @@hasInstance ] ( V )
void Builtins::Generate_FunctionPrototypeHasInstance(
    CodeStubAssembler* assembler) {
  using compiler::Node;

  Node* f = assembler->Parameter(0);
  Node* v = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* result = assembler->OrdinaryHasInstance(context, f, v);
  assembler->Return(result);
}

// -----------------------------------------------------------------------------
// ES6 section 25.3 Generator Objects

namespace {

void Generate_GeneratorPrototypeResume(
    CodeStubAssembler* assembler, JSGeneratorObject::ResumeMode resume_mode,
    char const* const method_name) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* value = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);
  Node* closed =
      assembler->SmiConstant(Smi::FromInt(JSGeneratorObject::kGeneratorClosed));

  // Check if the {receiver} is actually a JSGeneratorObject.
  Label if_receiverisincompatible(assembler, Label::kDeferred);
  assembler->GotoIf(assembler->WordIsSmi(receiver), &if_receiverisincompatible);
  Node* receiver_instance_type = assembler->LoadInstanceType(receiver);
  assembler->GotoUnless(assembler->Word32Equal(
                            receiver_instance_type,
                            assembler->Int32Constant(JS_GENERATOR_OBJECT_TYPE)),
                        &if_receiverisincompatible);

  // Check if the {receiver} is running or already closed.
  Node* receiver_continuation = assembler->LoadObjectField(
      receiver, JSGeneratorObject::kContinuationOffset);
  Label if_receiverisclosed(assembler, Label::kDeferred),
      if_receiverisrunning(assembler, Label::kDeferred);
  assembler->GotoIf(assembler->SmiEqual(receiver_continuation, closed),
                    &if_receiverisclosed);
  DCHECK_LT(JSGeneratorObject::kGeneratorExecuting,
            JSGeneratorObject::kGeneratorClosed);
  assembler->GotoIf(assembler->SmiLessThan(receiver_continuation, closed),
                    &if_receiverisrunning);

  // Resume the {receiver} using our trampoline.
  Node* result = assembler->CallStub(
      CodeFactory::ResumeGenerator(assembler->isolate()), context, value,
      receiver, assembler->SmiConstant(Smi::FromInt(resume_mode)));
  assembler->Return(result);

  assembler->Bind(&if_receiverisincompatible);
  {
    // The {receiver} is not a valid JSGeneratorObject.
    Node* result = assembler->CallRuntime(
        Runtime::kThrowIncompatibleMethodReceiver, context,
        assembler->HeapConstant(assembler->factory()->NewStringFromAsciiChecked(
            method_name, TENURED)),
        receiver);
    assembler->Return(result);  // Never reached.
  }

  assembler->Bind(&if_receiverisclosed);
  {
    // The {receiver} is closed already.
    Node* result = nullptr;
    switch (resume_mode) {
      case JSGeneratorObject::kNext:
        result = assembler->CallRuntime(Runtime::kCreateIterResultObject,
                                        context, assembler->UndefinedConstant(),
                                        assembler->BooleanConstant(true));
        break;
      case JSGeneratorObject::kReturn:
        result =
            assembler->CallRuntime(Runtime::kCreateIterResultObject, context,
                                   value, assembler->BooleanConstant(true));
        break;
      case JSGeneratorObject::kThrow:
        result = assembler->CallRuntime(Runtime::kThrow, context, value);
        break;
    }
    assembler->Return(result);
  }

  assembler->Bind(&if_receiverisrunning);
  {
    Node* result =
        assembler->CallRuntime(Runtime::kThrowGeneratorRunning, context);
    assembler->Return(result);  // Never reached.
  }
}

}  // namespace

// ES6 section 25.3.1.2 Generator.prototype.next ( value )
void Builtins::Generate_GeneratorPrototypeNext(CodeStubAssembler* assembler) {
  Generate_GeneratorPrototypeResume(assembler, JSGeneratorObject::kNext,
                                    "[Generator].prototype.next");
}

// ES6 section 25.3.1.3 Generator.prototype.return ( value )
void Builtins::Generate_GeneratorPrototypeReturn(CodeStubAssembler* assembler) {
  Generate_GeneratorPrototypeResume(assembler, JSGeneratorObject::kReturn,
                                    "[Generator].prototype.return");
}

// ES6 section 25.3.1.4 Generator.prototype.throw ( exception )
void Builtins::Generate_GeneratorPrototypeThrow(CodeStubAssembler* assembler) {
  Generate_GeneratorPrototypeResume(assembler, JSGeneratorObject::kThrow,
                                    "[Generator].prototype.throw");
}

// -----------------------------------------------------------------------------

//

namespace {

// Returns the holder JSObject if the function can legally be called with this
// receiver.  Returns nullptr if the call is illegal.
// TODO(dcarney): CallOptimization duplicates this logic, merge.
JSObject* GetCompatibleReceiver(Isolate* isolate, FunctionTemplateInfo* info,
                                JSObject* receiver) {
  Object* recv_type = info->signature();
  // No signature, return holder.
  if (!recv_type->IsFunctionTemplateInfo()) return receiver;
  FunctionTemplateInfo* signature = FunctionTemplateInfo::cast(recv_type);

  // Check the receiver. Fast path for receivers with no hidden prototypes.
  if (signature->IsTemplateFor(receiver)) return receiver;
  if (!receiver->map()->has_hidden_prototype()) return nullptr;
  for (PrototypeIterator iter(isolate, receiver, kStartAtPrototype,
                              PrototypeIterator::END_AT_NON_HIDDEN);
       !iter.IsAtEnd(); iter.Advance()) {
    JSObject* current = iter.GetCurrent<JSObject>();
    if (signature->IsTemplateFor(current)) return current;
  }
  return nullptr;
}

template <bool is_construct>
MUST_USE_RESULT MaybeHandle<Object> HandleApiCallHelper(
    Isolate* isolate, Handle<HeapObject> function,
    Handle<HeapObject> new_target, Handle<FunctionTemplateInfo> fun_data,
    Handle<Object> receiver, BuiltinArguments args) {
  Handle<JSObject> js_receiver;
  JSObject* raw_holder;
  if (is_construct) {
    DCHECK(args.receiver()->IsTheHole(isolate));
    if (fun_data->instance_template()->IsUndefined(isolate)) {
      v8::Local<ObjectTemplate> templ =
          ObjectTemplate::New(reinterpret_cast<v8::Isolate*>(isolate),
                              ToApiHandle<v8::FunctionTemplate>(fun_data));
      fun_data->set_instance_template(*Utils::OpenHandle(*templ));
    }
    Handle<ObjectTemplateInfo> instance_template(
        ObjectTemplateInfo::cast(fun_data->instance_template()), isolate);
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, js_receiver,
        ApiNatives::InstantiateObject(instance_template,
                                      Handle<JSReceiver>::cast(new_target)),
        Object);
    args[0] = *js_receiver;
    DCHECK_EQ(*js_receiver, *args.receiver());

    raw_holder = *js_receiver;
  } else {
    DCHECK(receiver->IsJSReceiver());

    if (!receiver->IsJSObject()) {
      // This function cannot be called with the given receiver.  Abort!
      THROW_NEW_ERROR(
          isolate, NewTypeError(MessageTemplate::kIllegalInvocation), Object);
    }

    js_receiver = Handle<JSObject>::cast(receiver);

    if (!fun_data->accept_any_receiver() &&
        js_receiver->IsAccessCheckNeeded() &&
        !isolate->MayAccess(handle(isolate->context()), js_receiver)) {
      isolate->ReportFailedAccessCheck(js_receiver);
      RETURN_EXCEPTION_IF_SCHEDULED_EXCEPTION(isolate, Object);
    }

    raw_holder = GetCompatibleReceiver(isolate, *fun_data, *js_receiver);

    if (raw_holder == nullptr) {
      // This function cannot be called with the given receiver.  Abort!
      THROW_NEW_ERROR(
          isolate, NewTypeError(MessageTemplate::kIllegalInvocation), Object);
    }
  }

  Object* raw_call_data = fun_data->call_code();
  if (!raw_call_data->IsUndefined(isolate)) {
    DCHECK(raw_call_data->IsCallHandlerInfo());
    CallHandlerInfo* call_data = CallHandlerInfo::cast(raw_call_data);
    Object* callback_obj = call_data->callback();
    v8::FunctionCallback callback =
        v8::ToCData<v8::FunctionCallback>(callback_obj);
    Object* data_obj = call_data->data();

    LOG(isolate, ApiObjectAccess("call", JSObject::cast(*js_receiver)));

    FunctionCallbackArguments custom(isolate, data_obj, *function, raw_holder,
                                     *new_target, &args[0] - 1,
                                     args.length() - 1);

    Handle<Object> result = custom.Call(callback);

    RETURN_EXCEPTION_IF_SCHEDULED_EXCEPTION(isolate, Object);
    if (result.is_null()) {
      if (is_construct) return js_receiver;
      return isolate->factory()->undefined_value();
    }
    // Rebox the result.
    result->VerifyApiCallResultType();
    if (!is_construct || result->IsJSObject()) return handle(*result, isolate);
  }

  return js_receiver;
}

}  // namespace

BUILTIN(HandleApiCall) {
  HandleScope scope(isolate);
  Handle<JSFunction> function = args.target<JSFunction>();
  Handle<Object> receiver = args.receiver();
  Handle<HeapObject> new_target = args.new_target();
  Handle<FunctionTemplateInfo> fun_data(function->shared()->get_api_func_data(),
                                        isolate);
  if (new_target->IsJSReceiver()) {
    RETURN_RESULT_OR_FAILURE(
        isolate, HandleApiCallHelper<true>(isolate, function, new_target,
                                           fun_data, receiver, args));
  } else {
    RETURN_RESULT_OR_FAILURE(
        isolate, HandleApiCallHelper<false>(isolate, function, new_target,
                                            fun_data, receiver, args));
  }
}

Handle<Code> Builtins::CallFunction(ConvertReceiverMode mode,
                                    TailCallMode tail_call_mode) {
  switch (tail_call_mode) {
    case TailCallMode::kDisallow:
      switch (mode) {
        case ConvertReceiverMode::kNullOrUndefined:
          return CallFunction_ReceiverIsNullOrUndefined();
        case ConvertReceiverMode::kNotNullOrUndefined:
          return CallFunction_ReceiverIsNotNullOrUndefined();
        case ConvertReceiverMode::kAny:
          return CallFunction_ReceiverIsAny();
      }
      break;
    case TailCallMode::kAllow:
      switch (mode) {
        case ConvertReceiverMode::kNullOrUndefined:
          return TailCallFunction_ReceiverIsNullOrUndefined();
        case ConvertReceiverMode::kNotNullOrUndefined:
          return TailCallFunction_ReceiverIsNotNullOrUndefined();
        case ConvertReceiverMode::kAny:
          return TailCallFunction_ReceiverIsAny();
      }
      break;
  }
  UNREACHABLE();
  return Handle<Code>::null();
}

Handle<Code> Builtins::Call(ConvertReceiverMode mode,
                            TailCallMode tail_call_mode) {
  switch (tail_call_mode) {
    case TailCallMode::kDisallow:
      switch (mode) {
        case ConvertReceiverMode::kNullOrUndefined:
          return Call_ReceiverIsNullOrUndefined();
        case ConvertReceiverMode::kNotNullOrUndefined:
          return Call_ReceiverIsNotNullOrUndefined();
        case ConvertReceiverMode::kAny:
          return Call_ReceiverIsAny();
      }
      break;
    case TailCallMode::kAllow:
      switch (mode) {
        case ConvertReceiverMode::kNullOrUndefined:
          return TailCall_ReceiverIsNullOrUndefined();
        case ConvertReceiverMode::kNotNullOrUndefined:
          return TailCall_ReceiverIsNotNullOrUndefined();
        case ConvertReceiverMode::kAny:
          return TailCall_ReceiverIsAny();
      }
      break;
  }
  UNREACHABLE();
  return Handle<Code>::null();
}

Handle<Code> Builtins::CallBoundFunction(TailCallMode tail_call_mode) {
  switch (tail_call_mode) {
    case TailCallMode::kDisallow:
      return CallBoundFunction();
    case TailCallMode::kAllow:
      return TailCallBoundFunction();
  }
  UNREACHABLE();
  return Handle<Code>::null();
}

Handle<Code> Builtins::NonPrimitiveToPrimitive(ToPrimitiveHint hint) {
  switch (hint) {
    case ToPrimitiveHint::kDefault:
      return NonPrimitiveToPrimitive_Default();
    case ToPrimitiveHint::kNumber:
      return NonPrimitiveToPrimitive_Number();
    case ToPrimitiveHint::kString:
      return NonPrimitiveToPrimitive_String();
  }
  UNREACHABLE();
  return Handle<Code>::null();
}

Handle<Code> Builtins::OrdinaryToPrimitive(OrdinaryToPrimitiveHint hint) {
  switch (hint) {
    case OrdinaryToPrimitiveHint::kNumber:
      return OrdinaryToPrimitive_Number();
    case OrdinaryToPrimitiveHint::kString:
      return OrdinaryToPrimitive_String();
  }
  UNREACHABLE();
  return Handle<Code>::null();
}

Handle<Code> Builtins::InterpreterPushArgsAndCall(TailCallMode tail_call_mode,
                                                  CallableType function_type) {
  switch (tail_call_mode) {
    case TailCallMode::kDisallow:
      if (function_type == CallableType::kJSFunction) {
        return InterpreterPushArgsAndCallFunction();
      } else {
        return InterpreterPushArgsAndCall();
      }
    case TailCallMode::kAllow:
      if (function_type == CallableType::kJSFunction) {
        return InterpreterPushArgsAndTailCallFunction();
      } else {
        return InterpreterPushArgsAndTailCall();
      }
  }
  UNREACHABLE();
  return Handle<Code>::null();
}

namespace {

class RelocatableArguments : public BuiltinArguments, public Relocatable {
 public:
  RelocatableArguments(Isolate* isolate, int length, Object** arguments)
      : BuiltinArguments(length, arguments), Relocatable(isolate) {}

  virtual inline void IterateInstance(ObjectVisitor* v) {
    if (length() == 0) return;
    v->VisitPointers(lowest_address(), highest_address() + 1);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(RelocatableArguments);
};

}  // namespace

MaybeHandle<Object> Builtins::InvokeApiFunction(Isolate* isolate,
                                                Handle<HeapObject> function,
                                                Handle<Object> receiver,
                                                int argc,
                                                Handle<Object> args[]) {
  DCHECK(function->IsFunctionTemplateInfo() ||
         (function->IsJSFunction() &&
          JSFunction::cast(*function)->shared()->IsApiFunction()));

  // Do proper receiver conversion for non-strict mode api functions.
  if (!receiver->IsJSReceiver()) {
    if (function->IsFunctionTemplateInfo() ||
        is_sloppy(JSFunction::cast(*function)->shared()->language_mode())) {
      ASSIGN_RETURN_ON_EXCEPTION(isolate, receiver,
                                 Object::ConvertReceiver(isolate, receiver),
                                 Object);
    }
  }

  Handle<FunctionTemplateInfo> fun_data =
      function->IsFunctionTemplateInfo()
          ? Handle<FunctionTemplateInfo>::cast(function)
          : handle(JSFunction::cast(*function)->shared()->get_api_func_data(),
                   isolate);
  Handle<HeapObject> new_target = isolate->factory()->undefined_value();
  // Construct BuiltinArguments object:
  // new target, function, arguments reversed, receiver.
  const int kBufferSize = 32;
  Object* small_argv[kBufferSize];
  Object** argv;
  const int frame_argc = argc + BuiltinArguments::kNumExtraArgsWithReceiver;
  if (frame_argc <= kBufferSize) {
    argv = small_argv;
  } else {
    argv = new Object*[frame_argc];
  }
  int cursor = frame_argc - 1;
  argv[cursor--] = *receiver;
  for (int i = 0; i < argc; ++i) {
    argv[cursor--] = *args[i];
  }
  DCHECK(cursor == BuiltinArguments::kArgcOffset);
  argv[BuiltinArguments::kArgcOffset] = Smi::FromInt(frame_argc);
  argv[BuiltinArguments::kTargetOffset] = *function;
  argv[BuiltinArguments::kNewTargetOffset] = *new_target;
  MaybeHandle<Object> result;
  {
    RelocatableArguments arguments(isolate, frame_argc, &argv[frame_argc - 1]);
    result = HandleApiCallHelper<false>(isolate, function, new_target, fun_data,
                                        receiver, arguments);
  }
  if (argv != small_argv) delete[] argv;
  return result;
}

// Helper function to handle calls to non-function objects created through the
// API. The object can be called as either a constructor (using new) or just as
// a function (without new).
MUST_USE_RESULT static Object* HandleApiCallAsFunctionOrConstructor(
    Isolate* isolate, bool is_construct_call, BuiltinArguments args) {
  Handle<Object> receiver = args.receiver();

  // Get the object called.
  JSObject* obj = JSObject::cast(*receiver);

  // Set the new target.
  HeapObject* new_target;
  if (is_construct_call) {
    // TODO(adamk): This should be passed through in args instead of
    // being patched in here. We need to set a non-undefined value
    // for v8::FunctionCallbackInfo::IsConstructCall() to get the
    // right answer.
    new_target = obj;
  } else {
    new_target = isolate->heap()->undefined_value();
  }

  // Get the invocation callback from the function descriptor that was
  // used to create the called object.
  DCHECK(obj->map()->is_callable());
  JSFunction* constructor = JSFunction::cast(obj->map()->GetConstructor());
  // TODO(ishell): turn this back to a DCHECK.
  CHECK(constructor->shared()->IsApiFunction());
  Object* handler =
      constructor->shared()->get_api_func_data()->instance_call_handler();
  DCHECK(!handler->IsUndefined(isolate));
  // TODO(ishell): remove this debugging code.
  CHECK(handler->IsCallHandlerInfo());
  CallHandlerInfo* call_data = CallHandlerInfo::cast(handler);
  Object* callback_obj = call_data->callback();
  v8::FunctionCallback callback =
      v8::ToCData<v8::FunctionCallback>(callback_obj);

  // Get the data for the call and perform the callback.
  Object* result;
  {
    HandleScope scope(isolate);
    LOG(isolate, ApiObjectAccess("call non-function", obj));

    FunctionCallbackArguments custom(isolate, call_data->data(), constructor,
                                     obj, new_target, &args[0] - 1,
                                     args.length() - 1);
    Handle<Object> result_handle = custom.Call(callback);
    if (result_handle.is_null()) {
      result = isolate->heap()->undefined_value();
    } else {
      result = *result_handle;
    }
  }
  // Check for exceptions and return result.
  RETURN_FAILURE_IF_SCHEDULED_EXCEPTION(isolate);
  return result;
}

// Handle calls to non-function objects created through the API. This delegate
// function is used when the call is a normal function call.
BUILTIN(HandleApiCallAsFunction) {
  return HandleApiCallAsFunctionOrConstructor(isolate, false, args);
}

// Handle calls to non-function objects created through the API. This delegate
// function is used when the call is a construct call.
BUILTIN(HandleApiCallAsConstructor) {
  return HandleApiCallAsFunctionOrConstructor(isolate, true, args);
}

void Builtins::Generate_LoadIC_Miss(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* name = assembler->Parameter(1);
  Node* slot = assembler->Parameter(2);
  Node* vector = assembler->Parameter(3);
  Node* context = assembler->Parameter(4);

  assembler->TailCallRuntime(Runtime::kLoadIC_Miss, context, receiver, name,
                             slot, vector);
}

void Builtins::Generate_LoadGlobalIC_Miss(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* slot = assembler->Parameter(0);
  Node* vector = assembler->Parameter(1);
  Node* context = assembler->Parameter(2);

  assembler->TailCallRuntime(Runtime::kLoadGlobalIC_Miss, context, slot,
                             vector);
}

void Builtins::Generate_LoadIC_Normal(MacroAssembler* masm) {
  LoadIC::GenerateNormal(masm);
}

void Builtins::Generate_LoadIC_Getter_ForDeopt(MacroAssembler* masm) {
  NamedLoadHandlerCompiler::GenerateLoadViaGetterForDeopt(masm);
}

void Builtins::Generate_LoadIC_Slow(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* name = assembler->Parameter(1);
  // Node* slot = assembler->Parameter(2);
  // Node* vector = assembler->Parameter(3);
  Node* context = assembler->Parameter(4);

  assembler->TailCallRuntime(Runtime::kGetProperty, context, receiver, name);
}

namespace {
void Generate_LoadGlobalIC_Slow(CodeStubAssembler* assembler, TypeofMode mode) {
  typedef compiler::Node Node;

  Node* slot = assembler->Parameter(0);
  Node* vector = assembler->Parameter(1);
  Node* context = assembler->Parameter(2);
  Node* typeof_mode = assembler->SmiConstant(Smi::FromInt(mode));

  assembler->TailCallRuntime(Runtime::kGetGlobal, context, slot, vector,
                             typeof_mode);
}
}  // anonymous namespace

void Builtins::Generate_LoadGlobalIC_SlowInsideTypeof(
    CodeStubAssembler* assembler) {
  Generate_LoadGlobalIC_Slow(assembler, INSIDE_TYPEOF);
}

void Builtins::Generate_LoadGlobalIC_SlowNotInsideTypeof(
    CodeStubAssembler* assembler) {
  Generate_LoadGlobalIC_Slow(assembler, NOT_INSIDE_TYPEOF);
}

void Builtins::Generate_KeyedLoadIC_Slow(MacroAssembler* masm) {
  KeyedLoadIC::GenerateRuntimeGetProperty(masm);
}

void Builtins::Generate_KeyedLoadIC_Miss(MacroAssembler* masm) {
  KeyedLoadIC::GenerateMiss(masm);
}

void Builtins::Generate_KeyedLoadIC_Megamorphic(MacroAssembler* masm) {
  KeyedLoadIC::GenerateMegamorphic(masm);
}

void Builtins::Generate_StoreIC_Miss(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* name = assembler->Parameter(1);
  Node* value = assembler->Parameter(2);
  Node* slot = assembler->Parameter(3);
  Node* vector = assembler->Parameter(4);
  Node* context = assembler->Parameter(5);

  assembler->TailCallRuntime(Runtime::kStoreIC_Miss, context, receiver, name,
                             value, slot, vector);
}

void Builtins::Generate_StoreIC_Normal(MacroAssembler* masm) {
  StoreIC::GenerateNormal(masm);
}

namespace {
void Generate_StoreIC_Slow(CodeStubAssembler* assembler,
                           LanguageMode language_mode) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* name = assembler->Parameter(1);
  Node* value = assembler->Parameter(2);
  // Node* slot = assembler->Parameter(3);
  // Node* vector = assembler->Parameter(4);
  Node* context = assembler->Parameter(5);
  Node* lang_mode = assembler->SmiConstant(Smi::FromInt(language_mode));

  // The slow case calls into the runtime to complete the store without causing
  // an IC miss that would otherwise cause a transition to the generic stub.
  assembler->TailCallRuntime(Runtime::kSetProperty, context, receiver, name,
                             value, lang_mode);
}
}  // anonymous namespace

void Builtins::Generate_StoreIC_SlowSloppy(CodeStubAssembler* assembler) {
  Generate_StoreIC_Slow(assembler, SLOPPY);
}

void Builtins::Generate_StoreIC_SlowStrict(CodeStubAssembler* assembler) {
  Generate_StoreIC_Slow(assembler, STRICT);
}

namespace {
// 7.1.1.1 OrdinaryToPrimitive ( O, hint )
void Generate_OrdinaryToPrimitive(CodeStubAssembler* assembler,
                                  OrdinaryToPrimitiveHint hint) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Node* input = assembler->Parameter(0);
  Node* context = assembler->Parameter(1);

  Variable var_result(assembler, MachineRepresentation::kTagged);
  Label return_result(assembler, &var_result);

  Handle<String> method_names[2];
  switch (hint) {
    case OrdinaryToPrimitiveHint::kNumber:
      method_names[0] = assembler->factory()->valueOf_string();
      method_names[1] = assembler->factory()->toString_string();
      break;
    case OrdinaryToPrimitiveHint::kString:
      method_names[0] = assembler->factory()->toString_string();
      method_names[1] = assembler->factory()->valueOf_string();
      break;
  }
  for (Handle<String> name : method_names) {
    // Lookup the {name} on the {input}.
    Callable callable = CodeFactory::GetProperty(assembler->isolate());
    Node* name_string = assembler->HeapConstant(name);
    Node* method = assembler->CallStub(callable, context, input, name_string);

    // Check if the {method} is callable.
    Label if_methodiscallable(assembler),
        if_methodisnotcallable(assembler, Label::kDeferred);
    assembler->GotoIf(assembler->WordIsSmi(method), &if_methodisnotcallable);
    Node* method_map = assembler->LoadMap(method);
    Node* method_bit_field = assembler->LoadMapBitField(method_map);
    assembler->Branch(
        assembler->Word32Equal(
            assembler->Word32And(method_bit_field, assembler->Int32Constant(
                                                       1 << Map::kIsCallable)),
            assembler->Int32Constant(0)),
        &if_methodisnotcallable, &if_methodiscallable);

    assembler->Bind(&if_methodiscallable);
    {
      // Call the {method} on the {input}.
      Callable callable = CodeFactory::Call(assembler->isolate());
      Node* result = assembler->CallJS(callable, context, method, input);
      var_result.Bind(result);

      // Return the {result} if it is a primitive.
      assembler->GotoIf(assembler->WordIsSmi(result), &return_result);
      Node* result_instance_type = assembler->LoadInstanceType(result);
      STATIC_ASSERT(FIRST_PRIMITIVE_TYPE == FIRST_TYPE);
      assembler->GotoIf(assembler->Int32LessThanOrEqual(
                            result_instance_type,
                            assembler->Int32Constant(LAST_PRIMITIVE_TYPE)),
                        &return_result);
    }

    // Just continue with the next {name} if the {method} is not callable.
    assembler->Goto(&if_methodisnotcallable);
    assembler->Bind(&if_methodisnotcallable);
  }

  assembler->TailCallRuntime(Runtime::kThrowCannotConvertToPrimitive, context);

  assembler->Bind(&return_result);
  assembler->Return(var_result.value());
}
}  // anonymous namespace

void Builtins::Generate_OrdinaryToPrimitive_Number(
    CodeStubAssembler* assembler) {
  Generate_OrdinaryToPrimitive(assembler, OrdinaryToPrimitiveHint::kNumber);
}

void Builtins::Generate_OrdinaryToPrimitive_String(
    CodeStubAssembler* assembler) {
  Generate_OrdinaryToPrimitive(assembler, OrdinaryToPrimitiveHint::kString);
}

namespace {
// ES6 section 7.1.1 ToPrimitive ( input [ , PreferredType ] )
void Generate_NonPrimitiveToPrimitive(CodeStubAssembler* assembler,
                                      ToPrimitiveHint hint) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Node* input = assembler->Parameter(0);
  Node* context = assembler->Parameter(1);

  // Lookup the @@toPrimitive property on the {input}.
  Callable callable = CodeFactory::GetProperty(assembler->isolate());
  Node* to_primitive_symbol =
      assembler->HeapConstant(assembler->factory()->to_primitive_symbol());
  Node* exotic_to_prim =
      assembler->CallStub(callable, context, input, to_primitive_symbol);

  // Check if {exotic_to_prim} is neither null nor undefined.
  Label ordinary_to_primitive(assembler);
  assembler->GotoIf(
      assembler->WordEqual(exotic_to_prim, assembler->NullConstant()),
      &ordinary_to_primitive);
  assembler->GotoIf(
      assembler->WordEqual(exotic_to_prim, assembler->UndefinedConstant()),
      &ordinary_to_primitive);
  {
    // Invoke the {exotic_to_prim} method on the {input} with a string
    // representation of the {hint}.
    Callable callable = CodeFactory::Call(assembler->isolate());
    Node* hint_string = assembler->HeapConstant(
        assembler->factory()->ToPrimitiveHintString(hint));
    Node* result = assembler->CallJS(callable, context, exotic_to_prim, input,
                                     hint_string);

    // Verify that the {result} is actually a primitive.
    Label if_resultisprimitive(assembler),
        if_resultisnotprimitive(assembler, Label::kDeferred);
    assembler->GotoIf(assembler->WordIsSmi(result), &if_resultisprimitive);
    Node* result_instance_type = assembler->LoadInstanceType(result);
    STATIC_ASSERT(FIRST_PRIMITIVE_TYPE == FIRST_TYPE);
    assembler->Branch(assembler->Int32LessThanOrEqual(
                          result_instance_type,
                          assembler->Int32Constant(LAST_PRIMITIVE_TYPE)),
                      &if_resultisprimitive, &if_resultisnotprimitive);

    assembler->Bind(&if_resultisprimitive);
    {
      // Just return the {result}.
      assembler->Return(result);
    }

    assembler->Bind(&if_resultisnotprimitive);
    {
      // Somehow the @@toPrimitive method on {input} didn't yield a primitive.
      assembler->TailCallRuntime(Runtime::kThrowCannotConvertToPrimitive,
                                 context);
    }
  }

  // Convert using the OrdinaryToPrimitive algorithm instead.
  assembler->Bind(&ordinary_to_primitive);
  {
    Callable callable = CodeFactory::OrdinaryToPrimitive(
        assembler->isolate(), (hint == ToPrimitiveHint::kString)
                                  ? OrdinaryToPrimitiveHint::kString
                                  : OrdinaryToPrimitiveHint::kNumber);
    assembler->TailCallStub(callable, context, input);
  }
}
}  // anonymous namespace

void Builtins::Generate_NonPrimitiveToPrimitive_Default(
    CodeStubAssembler* assembler) {
  Generate_NonPrimitiveToPrimitive(assembler, ToPrimitiveHint::kDefault);
}

void Builtins::Generate_NonPrimitiveToPrimitive_Number(
    CodeStubAssembler* assembler) {
  Generate_NonPrimitiveToPrimitive(assembler, ToPrimitiveHint::kNumber);
}

void Builtins::Generate_NonPrimitiveToPrimitive_String(
    CodeStubAssembler* assembler) {
  Generate_NonPrimitiveToPrimitive(assembler, ToPrimitiveHint::kString);
}

// ES6 section 7.1.3 ToNumber ( argument )
void Builtins::Generate_NonNumberToNumber(CodeStubAssembler* assembler) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Node* input = assembler->Parameter(0);
  Node* context = assembler->Parameter(1);

  // We might need to loop once here due to ToPrimitive conversions.
  Variable var_input(assembler, MachineRepresentation::kTagged);
  Label loop(assembler, &var_input);
  var_input.Bind(input);
  assembler->Goto(&loop);
  assembler->Bind(&loop);
  {
    // Load the current {input} value (known to be a HeapObject).
    Node* input = var_input.value();

    // Dispatch on the {input} instance type.
    Node* input_instance_type = assembler->LoadInstanceType(input);
    Label if_inputisstring(assembler), if_inputisoddball(assembler),
        if_inputisreceiver(assembler, Label::kDeferred),
        if_inputisother(assembler, Label::kDeferred);
    assembler->GotoIf(assembler->Int32LessThan(
                          input_instance_type,
                          assembler->Int32Constant(FIRST_NONSTRING_TYPE)),
                      &if_inputisstring);
    assembler->GotoIf(
        assembler->Word32Equal(input_instance_type,
                               assembler->Int32Constant(ODDBALL_TYPE)),
        &if_inputisoddball);
    STATIC_ASSERT(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
    assembler->Branch(assembler->Int32GreaterThanOrEqual(
                          input_instance_type,
                          assembler->Int32Constant(FIRST_JS_RECEIVER_TYPE)),
                      &if_inputisreceiver, &if_inputisother);

    assembler->Bind(&if_inputisstring);
    {
      // The {input} is a String, use the fast stub to convert it to a Number.
      // TODO(bmeurer): Consider inlining the StringToNumber logic here.
      Callable callable = CodeFactory::StringToNumber(assembler->isolate());
      assembler->TailCallStub(callable, context, input);
    }

    assembler->Bind(&if_inputisoddball);
    {
      // The {input} is an Oddball, we just need to the Number value of it.
      Node* result =
          assembler->LoadObjectField(input, Oddball::kToNumberOffset);
      assembler->Return(result);
    }

    assembler->Bind(&if_inputisreceiver);
    {
      // The {input} is a JSReceiver, we need to convert it to a Primitive first
      // using the ToPrimitive type conversion, preferably yielding a Number.
      Callable callable = CodeFactory::NonPrimitiveToPrimitive(
          assembler->isolate(), ToPrimitiveHint::kNumber);
      Node* result = assembler->CallStub(callable, context, input);

      // Check if the {result} is already a Number.
      Label if_resultisnumber(assembler), if_resultisnotnumber(assembler);
      assembler->GotoIf(assembler->WordIsSmi(result), &if_resultisnumber);
      Node* result_map = assembler->LoadMap(result);
      assembler->Branch(
          assembler->WordEqual(result_map, assembler->HeapNumberMapConstant()),
          &if_resultisnumber, &if_resultisnotnumber);

      assembler->Bind(&if_resultisnumber);
      {
        // The ToPrimitive conversion already gave us a Number, so we're done.
        assembler->Return(result);
      }

      assembler->Bind(&if_resultisnotnumber);
      {
        // We now have a Primitive {result}, but it's not yet a Number.
        var_input.Bind(result);
        assembler->Goto(&loop);
      }
    }

    assembler->Bind(&if_inputisother);
    {
      // The {input} is something else (i.e. Symbol or Simd128Value), let the
      // runtime figure out the correct exception.
      // Note: We cannot tail call to the runtime here, as js-to-wasm
      // trampolines also use this code currently, and they declare all
      // outgoing parameters as untagged, while we would push a tagged
      // object here.
      Node* result = assembler->CallRuntime(Runtime::kToNumber, context, input);
      assembler->Return(result);
    }
  }
}

// ES6 section 7.1.2 ToBoolean ( argument )
void Builtins::Generate_ToBoolean(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;

  Node* value = assembler->Parameter(0);

  Label return_true(assembler), return_false(assembler);
  assembler->BranchIfToBooleanIsTrue(value, &return_true, &return_false);

  assembler->Bind(&return_true);
  assembler->Return(assembler->BooleanConstant(true));

  assembler->Bind(&return_false);
  assembler->Return(assembler->BooleanConstant(false));
}

void Builtins::Generate_KeyedStoreIC_Slow(MacroAssembler* masm) {
  ElementHandlerCompiler::GenerateStoreSlow(masm);
}

void Builtins::Generate_StoreIC_Setter_ForDeopt(MacroAssembler* masm) {
  NamedStoreHandlerCompiler::GenerateStoreViaSetterForDeopt(masm);
}

void Builtins::Generate_KeyedStoreIC_Megamorphic(MacroAssembler* masm) {
  KeyedStoreIC::GenerateMegamorphic(masm, SLOPPY);
}

void Builtins::Generate_KeyedStoreIC_Megamorphic_Strict(MacroAssembler* masm) {
  KeyedStoreIC::GenerateMegamorphic(masm, STRICT);
}

void Builtins::Generate_KeyedStoreIC_Miss(MacroAssembler* masm) {
  KeyedStoreIC::GenerateMiss(masm);
}

void Builtins::Generate_Return_DebugBreak(MacroAssembler* masm) {
  DebugCodegen::GenerateDebugBreakStub(masm,
                                       DebugCodegen::SAVE_RESULT_REGISTER);
}

void Builtins::Generate_Slot_DebugBreak(MacroAssembler* masm) {
  DebugCodegen::GenerateDebugBreakStub(masm,
                                       DebugCodegen::IGNORE_RESULT_REGISTER);
}

void Builtins::Generate_FrameDropper_LiveEdit(MacroAssembler* masm) {
  DebugCodegen::GenerateFrameDropperLiveEdit(masm);
}

Builtins::Builtins() : initialized_(false) {
  memset(builtins_, 0, sizeof(builtins_[0]) * builtin_count);
}

Builtins::~Builtins() {}

namespace {
void PostBuildProfileAndTracing(Isolate* isolate, Code* code,
                                const char* name) {
  PROFILE(isolate, CodeCreateEvent(CodeEventListener::BUILTIN_TAG,
                                   AbstractCode::cast(code), name));
#ifdef ENABLE_DISASSEMBLER
  if (FLAG_print_builtin_code) {
    CodeTracer::Scope trace_scope(isolate->GetCodeTracer());
    OFStream os(trace_scope.file());
    os << "Builtin: " << name << "\n";
    code->Disassemble(name, os);
    os << "\n";
  }
#endif
}

typedef void (*MacroAssemblerGenerator)(MacroAssembler*);
typedef void (*CodeAssemblerGenerator)(CodeStubAssembler*);

Code* BuildWithMacroAssembler(Isolate* isolate,
                              MacroAssemblerGenerator generator,
                              Code::Flags flags, const char* s_name) {
  HandleScope scope(isolate);
  const size_t buffer_size = 32 * KB;
  byte buffer[buffer_size];  // NOLINT(runtime/arrays)
  MacroAssembler masm(isolate, buffer, buffer_size, CodeObjectRequired::kYes);
  DCHECK(!masm.has_frame());
  generator(&masm);
  CodeDesc desc;
  masm.GetCode(&desc);
  Handle<Code> code =
      isolate->factory()->NewCode(desc, flags, masm.CodeObject());
  PostBuildProfileAndTracing(isolate, *code, s_name);
  return *code;
}

Code* BuildAdaptor(Isolate* isolate, Address builtin_address,
                   Builtins::ExitFrameType exit_frame_type, Code::Flags flags,
                   const char* name) {
  HandleScope scope(isolate);
  const size_t buffer_size = 32 * KB;
  byte buffer[buffer_size];  // NOLINT(runtime/arrays)
  MacroAssembler masm(isolate, buffer, buffer_size, CodeObjectRequired::kYes);
  DCHECK(!masm.has_frame());
  Builtins::Generate_Adaptor(&masm, builtin_address, exit_frame_type);
  CodeDesc desc;
  masm.GetCode(&desc);
  Handle<Code> code =
      isolate->factory()->NewCode(desc, flags, masm.CodeObject());
  PostBuildProfileAndTracing(isolate, *code, name);
  return *code;
}

// Builder for builtins implemented in TurboFan with JS linkage.
Code* BuildWithCodeStubAssemblerJS(Isolate* isolate,
                                   CodeAssemblerGenerator generator, int argc,
                                   Code::Flags flags, const char* name) {
  HandleScope scope(isolate);
  Zone zone(isolate->allocator());
  CodeStubAssembler assembler(isolate, &zone, argc, flags, name);
  generator(&assembler);
  Handle<Code> code = assembler.GenerateCode();
  PostBuildProfileAndTracing(isolate, *code, name);
  return *code;
}

// Builder for builtins implemented in TurboFan with CallStub linkage.
Code* BuildWithCodeStubAssemblerCS(Isolate* isolate,
                                   CodeAssemblerGenerator generator,
                                   CallDescriptors::Key interface_descriptor,
                                   Code::Flags flags, const char* name) {
  HandleScope scope(isolate);
  Zone zone(isolate->allocator());
  // The interface descriptor with given key must be initialized at this point
  // and this construction just queries the details from the descriptors table.
  CallInterfaceDescriptor descriptor(isolate, interface_descriptor);
  // Ensure descriptor is already initialized.
  DCHECK_NOT_NULL(descriptor.GetFunctionType());
  CodeStubAssembler assembler(isolate, &zone, descriptor, flags, name);
  generator(&assembler);
  Handle<Code> code = assembler.GenerateCode();
  PostBuildProfileAndTracing(isolate, *code, name);
  return *code;
}
}  // anonymous namespace

void Builtins::SetUp(Isolate* isolate, bool create_heap_objects) {
  DCHECK(!initialized_);

  // Create a scope for the handles in the builtins.
  HandleScope scope(isolate);

  if (create_heap_objects) {
    int index = 0;
    const Code::Flags kBuiltinFlags = Code::ComputeFlags(Code::BUILTIN);
    Code* code;
#define BUILD_CPP(Name)                                                     \
  code = BuildAdaptor(isolate, FUNCTION_ADDR(Builtin_##Name), BUILTIN_EXIT, \
                      kBuiltinFlags, #Name);                                \
  builtins_[index++] = code;
#define BUILD_API(Name)                                             \
  code = BuildAdaptor(isolate, FUNCTION_ADDR(Builtin_##Name), EXIT, \
                      kBuiltinFlags, #Name);                        \
  builtins_[index++] = code;
#define BUILD_TFJ(Name, Argc)                                          \
  code = BuildWithCodeStubAssemblerJS(isolate, &Generate_##Name, Argc, \
                                      kBuiltinFlags, #Name);           \
  builtins_[index++] = code;
#define BUILD_TFS(Name, Kind, Extra, InterfaceDescriptor)              \
  { InterfaceDescriptor##Descriptor descriptor(isolate); }             \
  code = BuildWithCodeStubAssemblerCS(                                 \
      isolate, &Generate_##Name, CallDescriptors::InterfaceDescriptor, \
      Code::ComputeFlags(Code::Kind, Extra), #Name);                   \
  builtins_[index++] = code;
#define BUILD_ASM(Name)                                                        \
  code =                                                                       \
      BuildWithMacroAssembler(isolate, Generate_##Name, kBuiltinFlags, #Name); \
  builtins_[index++] = code;
#define BUILD_ASH(Name, Kind, Extra)                                           \
  code = BuildWithMacroAssembler(                                              \
      isolate, Generate_##Name, Code::ComputeFlags(Code::Kind, Extra), #Name); \
  builtins_[index++] = code;

    BUILTIN_LIST(BUILD_CPP, BUILD_API, BUILD_TFJ, BUILD_TFS, BUILD_ASM,
                 BUILD_ASH, BUILD_ASM);

#undef BUILD_CPP
#undef BUILD_API
#undef BUILD_TFJ
#undef BUILD_TFS
#undef BUILD_ASM
#undef BUILD_ASH
    CHECK_EQ(builtin_count, index);
    for (int i = 0; i < builtin_count; i++) {
      Code::cast(builtins_[i])->set_builtin_index(i);
    }
  }

  // Mark as initialized.
  initialized_ = true;
}

void Builtins::TearDown() { initialized_ = false; }

void Builtins::IterateBuiltins(ObjectVisitor* v) {
  v->VisitPointers(&builtins_[0], &builtins_[0] + builtin_count);
}

const char* Builtins::Lookup(byte* pc) {
  // may be called during initialization (disassembler!)
  if (initialized_) {
    for (int i = 0; i < builtin_count; i++) {
      Code* entry = Code::cast(builtins_[i]);
      if (entry->contains(pc)) return name(i);
    }
  }
  return NULL;
}

const char* Builtins::name(int index) {
  switch (index) {
#define CASE(Name, ...) \
  case k##Name:         \
    return #Name;
    BUILTIN_LIST_ALL(CASE)
#undef CASE
    default:
      UNREACHABLE();
      break;
  }
  return "";
}

void Builtins::Generate_InterruptCheck(MacroAssembler* masm) {
  masm->TailCallRuntime(Runtime::kInterrupt);
}

void Builtins::Generate_StackCheck(MacroAssembler* masm) {
  masm->TailCallRuntime(Runtime::kStackGuard);
}

void Builtins::Generate_CallFunction_ReceiverIsNullOrUndefined(
    MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kNullOrUndefined,
                        TailCallMode::kDisallow);
}

void Builtins::Generate_CallFunction_ReceiverIsNotNullOrUndefined(
    MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kNotNullOrUndefined,
                        TailCallMode::kDisallow);
}

void Builtins::Generate_CallFunction_ReceiverIsAny(MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kAny,
                        TailCallMode::kDisallow);
}

void Builtins::Generate_TailCallFunction_ReceiverIsNullOrUndefined(
    MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kNullOrUndefined,
                        TailCallMode::kAllow);
}

void Builtins::Generate_TailCallFunction_ReceiverIsNotNullOrUndefined(
    MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kNotNullOrUndefined,
                        TailCallMode::kAllow);
}

void Builtins::Generate_TailCallFunction_ReceiverIsAny(MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kAny, TailCallMode::kAllow);
}

void Builtins::Generate_CallBoundFunction(MacroAssembler* masm) {
  Generate_CallBoundFunctionImpl(masm, TailCallMode::kDisallow);
}

void Builtins::Generate_TailCallBoundFunction(MacroAssembler* masm) {
  Generate_CallBoundFunctionImpl(masm, TailCallMode::kAllow);
}

void Builtins::Generate_Call_ReceiverIsNullOrUndefined(MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kNullOrUndefined,
                TailCallMode::kDisallow);
}

void Builtins::Generate_Call_ReceiverIsNotNullOrUndefined(
    MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kNotNullOrUndefined,
                TailCallMode::kDisallow);
}

void Builtins::Generate_Call_ReceiverIsAny(MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kAny, TailCallMode::kDisallow);
}

void Builtins::Generate_TailCall_ReceiverIsNullOrUndefined(
    MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kNullOrUndefined,
                TailCallMode::kAllow);
}

void Builtins::Generate_TailCall_ReceiverIsNotNullOrUndefined(
    MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kNotNullOrUndefined,
                TailCallMode::kAllow);
}

void Builtins::Generate_TailCall_ReceiverIsAny(MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kAny, TailCallMode::kAllow);
}

void Builtins::Generate_InterpreterPushArgsAndCall(MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndCallImpl(masm, TailCallMode::kDisallow,
                                                 CallableType::kAny);
}
void Builtins::Generate_InterpreterPushArgsAndTailCall(MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndCallImpl(masm, TailCallMode::kAllow,
                                                 CallableType::kAny);
}

void Builtins::Generate_InterpreterPushArgsAndCallFunction(
    MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndCallImpl(masm, TailCallMode::kDisallow,
                                                 CallableType::kJSFunction);
}

void Builtins::Generate_InterpreterPushArgsAndTailCallFunction(
    MacroAssembler* masm) {
  return Generate_InterpreterPushArgsAndCallImpl(masm, TailCallMode::kAllow,
                                                 CallableType::kJSFunction);
}

#define DEFINE_BUILTIN_ACCESSOR(Name, ...)                                    \
  Handle<Code> Builtins::Name() {                                             \
    Code** code_address = reinterpret_cast<Code**>(builtin_address(k##Name)); \
    return Handle<Code>(code_address);                                        \
  }
BUILTIN_LIST_ALL(DEFINE_BUILTIN_ACCESSOR)
#undef DEFINE_BUILTIN_ACCESSOR

}  // namespace internal
}  // namespace v8
