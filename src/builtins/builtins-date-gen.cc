// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/code-stub-assembler.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// ES6 section 20.3 Date Objects

class DateBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit DateBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

 protected:
  void Generate_DatePrototype_GetField(int field_index);
};

void DateBuiltinsAssembler::Generate_DatePrototype_GetField(int field_index) {
  Node* receiver = Parameter(0);
  Node* context = Parameter(3);

  Label receiver_not_date(this, Label::kDeferred);

  GotoIf(TaggedIsSmi(receiver), &receiver_not_date);
  Node* receiver_instance_type = LoadInstanceType(receiver);
  GotoIf(Word32NotEqual(receiver_instance_type, Int32Constant(JS_DATE_TYPE)),
         &receiver_not_date);

  // Load the specified date field, falling back to the runtime as necessary.
  if (field_index == JSDate::kDateValue) {
    Return(LoadObjectField(receiver, JSDate::kValueOffset));
  } else {
    if (field_index < JSDate::kFirstUncachedField) {
      Label stamp_mismatch(this, Label::kDeferred);
      Node* date_cache_stamp = Load(
          MachineType::AnyTagged(),
          ExternalConstant(ExternalReference::date_cache_stamp(isolate())));

      Node* cache_stamp = LoadObjectField(receiver, JSDate::kCacheStampOffset);
      GotoIf(WordNotEqual(date_cache_stamp, cache_stamp), &stamp_mismatch);
      Return(LoadObjectField(
          receiver, JSDate::kValueOffset + field_index * kPointerSize));

      Bind(&stamp_mismatch);
    }

    Node* field_index_smi = SmiConstant(Smi::FromInt(field_index));
    Node* function =
        ExternalConstant(ExternalReference::get_date_field_function(isolate()));
    Node* result = CallCFunction2(
        MachineType::AnyTagged(), MachineType::AnyTagged(),
        MachineType::AnyTagged(), function, receiver, field_index_smi);
    Return(result);
  }

  // Raise a TypeError if the receiver is not a date.
  Bind(&receiver_not_date);
  {
    CallRuntime(Runtime::kThrowNotDateError, context);
    Unreachable();
  }
}

TF_BUILTIN(DatePrototypeGetDate, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kDay);
}

TF_BUILTIN(DatePrototypeGetDay, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kWeekday);
}

TF_BUILTIN(DatePrototypeGetFullYear, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kYear);
}

TF_BUILTIN(DatePrototypeGetHours, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kHour);
}

TF_BUILTIN(DatePrototypeGetMilliseconds, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kMillisecond);
}

TF_BUILTIN(DatePrototypeGetMinutes, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kMinute);
}

TF_BUILTIN(DatePrototypeGetMonth, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kMonth);
}

TF_BUILTIN(DatePrototypeGetSeconds, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kSecond);
}

TF_BUILTIN(DatePrototypeGetTime, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kDateValue);
}

TF_BUILTIN(DatePrototypeGetTimezoneOffset, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kTimezoneOffset);
}

TF_BUILTIN(DatePrototypeGetUTCDate, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kDayUTC);
}

TF_BUILTIN(DatePrototypeGetUTCDay, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kWeekdayUTC);
}

TF_BUILTIN(DatePrototypeGetUTCFullYear, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kYearUTC);
}

TF_BUILTIN(DatePrototypeGetUTCHours, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kHourUTC);
}

TF_BUILTIN(DatePrototypeGetUTCMilliseconds, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kMillisecondUTC);
}

TF_BUILTIN(DatePrototypeGetUTCMinutes, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kMinuteUTC);
}

TF_BUILTIN(DatePrototypeGetUTCMonth, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kMonthUTC);
}

TF_BUILTIN(DatePrototypeGetUTCSeconds, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kSecondUTC);
}

TF_BUILTIN(DatePrototypeValueOf, DateBuiltinsAssembler) {
  Generate_DatePrototype_GetField(JSDate::kDateValue);
}

TF_BUILTIN(DatePrototypeToPrimitive, CodeStubAssembler) {
  Node* receiver = Parameter(0);
  Node* hint = Parameter(1);
  Node* context = Parameter(4);

  // Check if the {receiver} is actually a JSReceiver.
  Label receiver_is_invalid(this, Label::kDeferred);
  GotoIf(TaggedIsSmi(receiver), &receiver_is_invalid);
  GotoIfNot(IsJSReceiver(receiver), &receiver_is_invalid);

  // Dispatch to the appropriate OrdinaryToPrimitive builtin.
  Label hint_is_number(this), hint_is_string(this),
      hint_is_invalid(this, Label::kDeferred);

  // Fast cases for internalized strings.
  Node* number_string = LoadRoot(Heap::knumber_stringRootIndex);
  GotoIf(WordEqual(hint, number_string), &hint_is_number);
  Node* default_string = LoadRoot(Heap::kdefault_stringRootIndex);
  GotoIf(WordEqual(hint, default_string), &hint_is_string);
  Node* string_string = LoadRoot(Heap::kstring_stringRootIndex);
  GotoIf(WordEqual(hint, string_string), &hint_is_string);

  // Slow-case with actual string comparisons.
  Callable string_equal = CodeFactory::StringEqual(isolate());
  GotoIf(TaggedIsSmi(hint), &hint_is_invalid);
  GotoIfNot(IsString(hint), &hint_is_invalid);
  GotoIf(WordEqual(CallStub(string_equal, context, hint, number_string),
                   TrueConstant()),
         &hint_is_number);
  GotoIf(WordEqual(CallStub(string_equal, context, hint, default_string),
                   TrueConstant()),
         &hint_is_string);
  GotoIf(WordEqual(CallStub(string_equal, context, hint, string_string),
                   TrueConstant()),
         &hint_is_string);
  Goto(&hint_is_invalid);

  // Use the OrdinaryToPrimitive builtin to convert to a Number.
  Bind(&hint_is_number);
  {
    Callable callable = CodeFactory::OrdinaryToPrimitive(
        isolate(), OrdinaryToPrimitiveHint::kNumber);
    Node* result = CallStub(callable, context, receiver);
    Return(result);
  }

  // Use the OrdinaryToPrimitive builtin to convert to a String.
  Bind(&hint_is_string);
  {
    Callable callable = CodeFactory::OrdinaryToPrimitive(
        isolate(), OrdinaryToPrimitiveHint::kString);
    Node* result = CallStub(callable, context, receiver);
    Return(result);
  }

  // Raise a TypeError if the {hint} is invalid.
  Bind(&hint_is_invalid);
  {
    CallRuntime(Runtime::kThrowInvalidHint, context, hint);
    Unreachable();
  }

  // Raise a TypeError if the {receiver} is not a JSReceiver instance.
  Bind(&receiver_is_invalid);
  {
    CallRuntime(Runtime::kThrowIncompatibleMethodReceiver, context,
                HeapConstant(factory()->NewStringFromAsciiChecked(
                    "Date.prototype [ @@toPrimitive ]", TENURED)),
                receiver);
    Unreachable();
  }
}

}  // namespace internal
}  // namespace v8
