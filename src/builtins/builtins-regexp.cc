// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"

#include "src/code-factory.h"
#include "src/regexp/jsregexp.h"
#include "src/string-builder.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// ES6 section 21.2 RegExp Objects

namespace {

// ES#sec-isregexp IsRegExp ( argument )
Maybe<bool> IsRegExp(Isolate* isolate, Handle<Object> object) {
  if (!object->IsJSReceiver()) return Just(false);

  Handle<JSReceiver> receiver = Handle<JSReceiver>::cast(object);

  if (isolate->regexp_function()->initial_map() == receiver->map()) {
    // Fast-path for unmodified JSRegExp instances.
    return Just(true);
  }

  Handle<Object> match;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, match,
      JSObject::GetProperty(receiver, isolate->factory()->match_symbol()),
      Nothing<bool>());

  if (!match->IsUndefined(isolate)) return Just(match->BooleanValue());
  return Just(object->IsJSRegExp());
}

Handle<String> PatternFlags(Isolate* isolate, Handle<JSRegExp> regexp) {
  static const int kMaxFlagsLength = 5 + 1;  // 5 flags and '\0';
  char flags_string[kMaxFlagsLength];
  int i = 0;

  const JSRegExp::Flags flags = regexp->GetFlags();

  if ((flags & JSRegExp::kGlobal) != 0) flags_string[i++] = 'g';
  if ((flags & JSRegExp::kIgnoreCase) != 0) flags_string[i++] = 'i';
  if ((flags & JSRegExp::kMultiline) != 0) flags_string[i++] = 'm';
  if ((flags & JSRegExp::kUnicode) != 0) flags_string[i++] = 'u';
  if ((flags & JSRegExp::kSticky) != 0) flags_string[i++] = 'y';

  DCHECK_LT(i, kMaxFlagsLength);
  memset(&flags_string[i], '\0', kMaxFlagsLength - i);

  return isolate->factory()->NewStringFromAsciiChecked(flags_string);
}

// ES#sec-regexpinitialize
// Runtime Semantics: RegExpInitialize ( obj, pattern, flags )
MaybeHandle<JSRegExp> RegExpInitialize(Isolate* isolate,
                                       Handle<JSRegExp> regexp,
                                       Handle<Object> pattern,
                                       Handle<Object> flags) {
  Handle<String> pattern_string;
  if (pattern->IsUndefined(isolate)) {
    pattern_string = isolate->factory()->empty_string();
  } else {
    ASSIGN_RETURN_ON_EXCEPTION(isolate, pattern_string,
                               Object::ToString(isolate, pattern), JSRegExp);
  }

  Handle<String> flags_string;
  if (flags->IsUndefined(isolate)) {
    flags_string = isolate->factory()->empty_string();
  } else {
    ASSIGN_RETURN_ON_EXCEPTION(isolate, flags_string,
                               Object::ToString(isolate, flags), JSRegExp);
  }

  // TODO(jgruber): We could avoid the flags back and forth conversions.
  RETURN_RESULT(isolate,
                JSRegExp::Initialize(regexp, pattern_string, flags_string),
                JSRegExp);
}

}  // namespace

// ES#sec-regexp-pattern-flags
// RegExp ( pattern, flags )
BUILTIN(RegExpConstructor) {
  HandleScope scope(isolate);

  Handle<HeapObject> new_target = args.new_target();
  Handle<Object> pattern = args.atOrUndefined(isolate, 1);
  Handle<Object> flags = args.atOrUndefined(isolate, 2);

  Handle<JSFunction> target = isolate->regexp_function();

  bool pattern_is_regexp;
  {
    Maybe<bool> maybe_pattern_is_regexp = IsRegExp(isolate, pattern);
    if (maybe_pattern_is_regexp.IsNothing()) {
      DCHECK(isolate->has_pending_exception());
      return isolate->heap()->exception();
    }
    pattern_is_regexp = maybe_pattern_is_regexp.FromJust();
  }

  if (new_target->IsUndefined(isolate)) {
    new_target = target;

    // ES6 section 21.2.3.1 step 3.b
    if (pattern_is_regexp && flags->IsUndefined(isolate)) {
      Handle<Object> pattern_constructor;
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, pattern_constructor,
          Object::GetProperty(pattern,
                              isolate->factory()->constructor_string()));

      if (pattern_constructor.is_identical_to(new_target)) {
        return *pattern;
      }
    }
  }

  if (pattern->IsJSRegExp()) {
    Handle<JSRegExp> regexp_pattern = Handle<JSRegExp>::cast(pattern);

    if (flags->IsUndefined(isolate)) {
      flags = PatternFlags(isolate, regexp_pattern);
    }
    pattern = handle(regexp_pattern->source(), isolate);
  } else if (pattern_is_regexp) {
    Handle<Object> pattern_source;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, pattern_source,
        Object::GetProperty(pattern, isolate->factory()->source_string()));

    if (flags->IsUndefined(isolate)) {
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, flags,
          Object::GetProperty(pattern, isolate->factory()->flags_string()));
    }
    pattern = pattern_source;
  }

  Handle<JSReceiver> new_target_receiver = Handle<JSReceiver>::cast(new_target);

  // TODO(jgruber): Fast-path for target == new_target == unmodified JSRegExp.

  Handle<JSObject> object;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, object, JSObject::New(target, new_target_receiver));
  Handle<JSRegExp> regexp = Handle<JSRegExp>::cast(object);

  RETURN_RESULT_OR_FAILURE(isolate,
                           RegExpInitialize(isolate, regexp, pattern, flags));
}

BUILTIN(RegExpPrototypeCompile) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSRegExp, regexp, "RegExp.prototype.compile");

  Handle<Object> pattern = args.atOrUndefined(isolate, 1);
  Handle<Object> flags = args.atOrUndefined(isolate, 2);

  if (pattern->IsJSRegExp()) {
    Handle<JSRegExp> pattern_regexp = Handle<JSRegExp>::cast(pattern);

    if (!flags->IsUndefined(isolate)) {
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate, NewTypeError(MessageTemplate::kRegExpFlags));
    }

    flags = PatternFlags(isolate, pattern_regexp);
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, pattern,
        Object::GetProperty(pattern, isolate->factory()->source_string()));
  }

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, regexp, RegExpInitialize(isolate, regexp, pattern, flags));

  // Return undefined for compatibility with JSC.
  // See http://crbug.com/585775 for web compat details.

  return isolate->heap()->undefined_value();
}

namespace {

compiler::Node* LoadLastIndex(CodeStubAssembler* a, compiler::Node* context,
                              compiler::Node* has_initialmap,
                              compiler::Node* regexp) {
  typedef CodeStubAssembler::Variable Variable;
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Variable var_value(a, MachineRepresentation::kTagged);

  Label out(a), if_unmodified(a), if_modified(a, Label::kDeferred);
  a->Branch(has_initialmap, &if_unmodified, &if_modified);

  a->Bind(&if_unmodified);
  {
    // Load the in-object field.
    static const int field_offset =
        JSRegExp::kSize + JSRegExp::kLastIndexFieldIndex * kPointerSize;
    var_value.Bind(a->LoadObjectField(regexp, field_offset));
    a->Goto(&out);
  }

  a->Bind(&if_modified);
  {
    // Load through the GetProperty stub.
    Node* const name =
        a->HeapConstant(a->isolate()->factory()->lastIndex_string());
    Callable getproperty_callable = CodeFactory::GetProperty(a->isolate());
    var_value.Bind(a->CallStub(getproperty_callable, context, regexp, name));
    a->Goto(&out);
  }

  a->Bind(&out);
  return var_value.value();
}

void StoreLastIndex(CodeStubAssembler* a, compiler::Node* context,
                    compiler::Node* has_initialmap, compiler::Node* regexp,
                    compiler::Node* value) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Label out(a), if_unmodified(a), if_modified(a, Label::kDeferred);
  a->Branch(has_initialmap, &if_unmodified, &if_modified);

  a->Bind(&if_unmodified);
  {
    // Store the in-object field.
    static const int field_offset =
        JSRegExp::kSize + JSRegExp::kLastIndexFieldIndex * kPointerSize;
    a->StoreObjectField(regexp, field_offset, value);
    a->Goto(&out);
  }

  a->Bind(&if_modified);
  {
    // Store through runtime.
    // TODO(ishell): Use SetPropertyStub here once available.
    Node* const name =
        a->HeapConstant(a->isolate()->factory()->lastIndex_string());
    Node* const language_mode = a->SmiConstant(Smi::FromInt(STRICT));
    a->CallRuntime(Runtime::kSetProperty, context, regexp, name, value,
                   language_mode);
    a->Goto(&out);
  }

  a->Bind(&out);
}

compiler::Node* ConstructNewResultFromMatchInfo(Isolate* isolate,
                                                CodeStubAssembler* a,
                                                compiler::Node* context,
                                                compiler::Node* match_elements,
                                                compiler::Node* string) {
  typedef CodeStubAssembler::Variable Variable;
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Label out(a);

  CodeStubAssembler::ParameterMode mode = CodeStubAssembler::INTPTR_PARAMETERS;
  Node* const num_indices = a->SmiUntag(a->LoadFixedArrayElement(
      match_elements, a->IntPtrConstant(RegExpImpl::kLastCaptureCount), 0,
      mode));
  Node* const num_results = a->SmiTag(a->WordShr(num_indices, 1));
  Node* const start = a->LoadFixedArrayElement(
      match_elements, a->IntPtrConstant(RegExpImpl::kFirstCapture), 0, mode);
  Node* const end = a->LoadFixedArrayElement(
      match_elements, a->IntPtrConstant(RegExpImpl::kFirstCapture + 1), 0,
      mode);

  // Calculate the substring of the first match before creating the result array
  // to avoid an unnecessary write barrier storing the first result.
  Node* const first = a->SubString(context, string, start, end);

  Node* const result =
      a->AllocateRegExpResult(context, num_results, start, string);
  Node* const result_elements = a->LoadElements(result);

  a->StoreFixedArrayElement(result_elements, a->IntPtrConstant(0), first,
                            SKIP_WRITE_BARRIER);

  a->GotoIf(a->SmiEqual(num_results, a->SmiConstant(Smi::FromInt(1))), &out);

  // Store all remaining captures.
  Node* const limit =
      a->IntPtrAdd(a->IntPtrConstant(RegExpImpl::kFirstCapture), num_indices);

  Variable var_from_cursor(a, MachineType::PointerRepresentation());
  Variable var_to_cursor(a, MachineType::PointerRepresentation());

  var_from_cursor.Bind(a->IntPtrConstant(RegExpImpl::kFirstCapture + 2));
  var_to_cursor.Bind(a->IntPtrConstant(1));

  Variable* vars[] = {&var_from_cursor, &var_to_cursor};
  Label loop(a, 2, vars);

  a->Goto(&loop);
  a->Bind(&loop);
  {
    Node* const from_cursor = var_from_cursor.value();
    Node* const to_cursor = var_to_cursor.value();
    Node* const start = a->LoadFixedArrayElement(match_elements, from_cursor);

    Label next_iter(a);
    a->GotoIf(a->SmiEqual(start, a->SmiConstant(Smi::FromInt(-1))), &next_iter);

    Node* const from_cursor_plus1 =
        a->IntPtrAdd(from_cursor, a->IntPtrConstant(1));
    Node* const end =
        a->LoadFixedArrayElement(match_elements, from_cursor_plus1);

    Node* const capture = a->SubString(context, string, start, end);
    a->StoreFixedArrayElement(result_elements, to_cursor, capture);
    a->Goto(&next_iter);

    a->Bind(&next_iter);
    var_from_cursor.Bind(a->IntPtrAdd(from_cursor, a->IntPtrConstant(2)));
    var_to_cursor.Bind(a->IntPtrAdd(to_cursor, a->IntPtrConstant(1)));
    a->Branch(a->UintPtrLessThan(var_from_cursor.value(), limit), &loop, &out);
  }

  a->Bind(&out);
  return result;
}

}  // namespace

// ES#sec-regexp.prototype.exec
// RegExp.prototype.exec ( string )
void Builtins::Generate_RegExpPrototypeExec(CodeStubAssembler* a) {
  typedef CodeStubAssembler::Variable Variable;
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Isolate* const isolate = a->isolate();

  Node* const receiver = a->Parameter(0);
  Node* const maybe_string = a->Parameter(1);
  Node* const context = a->Parameter(4);

  Node* const null = a->NullConstant();
  Node* const int_zero = a->IntPtrConstant(0);
  Node* const smi_zero = a->SmiConstant(Smi::kZero);

  // Ensure {receiver} is a JSRegExp.
  Node* const regexp_map = a->ThrowIfNotInstanceType(
      context, receiver, JS_REGEXP_TYPE, "RegExp.prototype.exec");
  Node* const regexp = receiver;

  // Check whether the regexp instance is unmodified.
  Node* const native_context = a->LoadNativeContext(context);
  Node* const regexp_fun =
      a->LoadContextElement(native_context, Context::REGEXP_FUNCTION_INDEX);
  Node* const initial_map =
      a->LoadObjectField(regexp_fun, JSFunction::kPrototypeOrInitialMapOffset);
  Node* const has_initialmap = a->WordEqual(regexp_map, initial_map);

  // Convert {maybe_string} to a string.
  Callable tostring_callable = CodeFactory::ToString(isolate);
  Node* const string = a->CallStub(tostring_callable, context, maybe_string);
  Node* const string_length = a->LoadStringLength(string);

  // Check whether the regexp is global or sticky, which determines whether we
  // update last index later on.
  Node* const flags = a->LoadObjectField(regexp, JSRegExp::kFlagsOffset);
  Node* const is_global_or_sticky =
      a->WordAnd(a->SmiUntag(flags),
                 a->IntPtrConstant(JSRegExp::kGlobal | JSRegExp::kSticky));
  Node* const should_update_last_index =
      a->WordNotEqual(is_global_or_sticky, int_zero);

  // Grab and possibly update last index.
  Label run_exec(a);
  Variable var_lastindex(a, MachineRepresentation::kTagged);
  {
    Label if_doupdate(a), if_dontupdate(a);
    a->Branch(should_update_last_index, &if_doupdate, &if_dontupdate);

    a->Bind(&if_doupdate);
    {
      Node* const regexp_lastindex =
          LoadLastIndex(a, context, has_initialmap, regexp);

      Callable tolength_callable = CodeFactory::ToLength(isolate);
      Node* const lastindex =
          a->CallStub(tolength_callable, context, regexp_lastindex);
      var_lastindex.Bind(lastindex);

      Label if_isoob(a, Label::kDeferred);
      a->GotoUnless(a->WordIsSmi(lastindex), &if_isoob);
      a->GotoUnless(a->SmiLessThanOrEqual(lastindex, string_length), &if_isoob);
      a->Goto(&run_exec);

      a->Bind(&if_isoob);
      {
        StoreLastIndex(a, context, has_initialmap, regexp, smi_zero);
        a->Return(null);
      }
    }

    a->Bind(&if_dontupdate);
    {
      var_lastindex.Bind(smi_zero);
      a->Goto(&run_exec);
    }
  }

  Node* match_indices;
  Label successful_match(a);
  a->Bind(&run_exec);
  {
    // Get last match info from the context.
    Node* const last_match_info = a->LoadContextElement(
        native_context, Context::REGEXP_LAST_MATCH_INFO_INDEX);

    // Call the exec stub.
    Callable exec_callable = CodeFactory::RegExpExec(isolate);
    match_indices = a->CallStub(exec_callable, context, regexp, string,
                                var_lastindex.value(), last_match_info);

    // {match_indices} is either null or the RegExpLastMatchInfo array.
    // Return early if exec failed, possibly updating last index.
    a->GotoUnless(a->WordEqual(match_indices, null), &successful_match);

    Label return_null(a);
    a->GotoUnless(should_update_last_index, &return_null);

    StoreLastIndex(a, context, has_initialmap, regexp, smi_zero);
    a->Goto(&return_null);

    a->Bind(&return_null);
    a->Return(null);
  }

  Label construct_result(a);
  a->Bind(&successful_match);
  {
    Node* const match_elements = a->LoadElements(match_indices);

    a->GotoUnless(should_update_last_index, &construct_result);

    // Update the new last index from {match_indices}.
    Node* const new_lastindex = a->LoadFixedArrayElement(
        match_elements, a->IntPtrConstant(RegExpImpl::kFirstCapture + 1));

    StoreLastIndex(a, context, has_initialmap, regexp, new_lastindex);
    a->Goto(&construct_result);

    a->Bind(&construct_result);
    {
      Node* result = ConstructNewResultFromMatchInfo(isolate, a, context,
                                                     match_elements, string);
      a->Return(result);
    }
  }
}

namespace {

compiler::Node* ThrowIfNotJSReceiver(CodeStubAssembler* a, Isolate* isolate,
                                     compiler::Node* context,
                                     compiler::Node* value,
                                     char const* method_name) {
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;
  typedef CodeStubAssembler::Variable Variable;

  Label out(a), throw_exception(a, Label::kDeferred);
  Variable var_value_map(a, MachineRepresentation::kTagged);

  a->GotoIf(a->WordIsSmi(value), &throw_exception);

  // Load the instance type of the {value}.
  var_value_map.Bind(a->LoadMap(value));
  Node* const value_instance_type =
      a->LoadMapInstanceType(var_value_map.value());

  a->Branch(a->IsJSReceiverInstanceType(value_instance_type), &out,
            &throw_exception);

  // The {value} is not a compatible receiver for this method.
  a->Bind(&throw_exception);
  {
    Node* const message_id =
        a->SmiConstant(Smi::FromInt(MessageTemplate::kRegExpNonObject));
    Node* const method_name_str = a->HeapConstant(
        isolate->factory()->NewStringFromAsciiChecked(method_name, TENURED));

    Callable callable = CodeFactory::ToString(isolate);
    Node* const value_str = a->CallStub(callable, context, value);

    a->CallRuntime(Runtime::kThrowTypeError, context, message_id,
                   method_name_str, value_str);
    var_value_map.Bind(a->UndefinedConstant());
    a->Goto(&out);  // Never reached.
  }

  a->Bind(&out);
  return var_value_map.value();
}

compiler::Node* IsInitialRegExpMap(CodeStubAssembler* a,
                                   compiler::Node* context,
                                   compiler::Node* map) {
  typedef compiler::Node Node;

  Node* const native_context = a->LoadNativeContext(context);
  Node* const regexp_fun =
      a->LoadContextElement(native_context, Context::REGEXP_FUNCTION_INDEX);
  Node* const initial_map =
      a->LoadObjectField(regexp_fun, JSFunction::kPrototypeOrInitialMapOffset);
  Node* const has_initialmap = a->WordEqual(map, initial_map);

  return has_initialmap;
}

}  // namespace

void Builtins::Generate_RegExpPrototypeFlagsGetter(CodeStubAssembler* a) {
  typedef CodeStubAssembler::Variable Variable;
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Node* const receiver = a->Parameter(0);
  Node* const context = a->Parameter(3);

  Isolate* isolate = a->isolate();
  Node* const int_zero = a->IntPtrConstant(0);
  Node* const int_one = a->IntPtrConstant(1);

  Node* const map = ThrowIfNotJSReceiver(a, isolate, context, receiver,
                                         "RegExp.prototype.flags");

  Variable var_length(a, MachineType::PointerRepresentation());
  Variable var_flags(a, MachineType::PointerRepresentation());

  // First, count the number of characters we will need and check which flags
  // are set.

  var_length.Bind(int_zero);

  Label if_isunmodifiedjsregexp(a),
      if_isnotunmodifiedjsregexp(a, Label::kDeferred);
  a->Branch(IsInitialRegExpMap(a, context, map), &if_isunmodifiedjsregexp,
            &if_isnotunmodifiedjsregexp);

  Label construct_string(a);
  a->Bind(&if_isunmodifiedjsregexp);
  {
    // Refer to JSRegExp's flag property on the fast-path.
    Node* const flags_smi =
        a->LoadObjectField(receiver, JSRegExp::kFlagsOffset);
    Node* const flags_intptr = a->SmiUntag(flags_smi);
    var_flags.Bind(flags_intptr);

    Label label_global(a), label_ignorecase(a), label_multiline(a),
        label_unicode(a), label_sticky(a);

#define CASE_FOR_FLAG(FLAG, LABEL, NEXT_LABEL)                        \
  do {                                                                \
    a->Bind(&LABEL);                                                  \
    Node* const mask = a->IntPtrConstant(FLAG);                       \
    a->GotoIf(a->WordEqual(a->WordAnd(flags_intptr, mask), int_zero), \
              &NEXT_LABEL);                                           \
    var_length.Bind(a->IntPtrAdd(var_length.value(), int_one));       \
    a->Goto(&NEXT_LABEL);                                             \
  } while (false)

    a->Goto(&label_global);
    CASE_FOR_FLAG(JSRegExp::kGlobal, label_global, label_ignorecase);
    CASE_FOR_FLAG(JSRegExp::kIgnoreCase, label_ignorecase, label_multiline);
    CASE_FOR_FLAG(JSRegExp::kMultiline, label_multiline, label_unicode);
    CASE_FOR_FLAG(JSRegExp::kUnicode, label_unicode, label_sticky);
    CASE_FOR_FLAG(JSRegExp::kSticky, label_sticky, construct_string);
#undef CASE_FOR_FLAG
  }

  a->Bind(&if_isnotunmodifiedjsregexp);
  {
    // Fall back to GetProperty stub on the slow-path.
    var_flags.Bind(int_zero);

    Callable getproperty_callable = CodeFactory::GetProperty(a->isolate());
    Label label_global(a), label_ignorecase(a), label_multiline(a),
        label_unicode(a), label_sticky(a);

#define CASE_FOR_FLAG(NAME, FLAG, LABEL, NEXT_LABEL)                          \
  do {                                                                        \
    a->Bind(&LABEL);                                                          \
    Node* const name =                                                        \
        a->HeapConstant(isolate->factory()->NewStringFromAsciiChecked(NAME)); \
    Node* const flag =                                                        \
        a->CallStub(getproperty_callable, context, receiver, name);           \
    Label if_isflagset(a);                                                    \
    a->BranchIfToBooleanIsTrue(flag, &if_isflagset, &NEXT_LABEL);             \
    a->Bind(&if_isflagset);                                                   \
    var_length.Bind(a->IntPtrAdd(var_length.value(), int_one));               \
    var_flags.Bind(a->WordOr(var_flags.value(), a->IntPtrConstant(FLAG)));    \
    a->Goto(&NEXT_LABEL);                                                     \
  } while (false)

    a->Goto(&label_global);
    CASE_FOR_FLAG("global", JSRegExp::kGlobal, label_global, label_ignorecase);
    CASE_FOR_FLAG("ignoreCase", JSRegExp::kIgnoreCase, label_ignorecase,
                  label_multiline);
    CASE_FOR_FLAG("multiline", JSRegExp::kMultiline, label_multiline,
                  label_unicode);
    CASE_FOR_FLAG("unicode", JSRegExp::kUnicode, label_unicode, label_sticky);
    CASE_FOR_FLAG("sticky", JSRegExp::kSticky, label_sticky, construct_string);
#undef CASE_FOR_FLAG
  }

  // Allocate a string of the required length and fill it with the corresponding
  // char for each set flag.

  a->Bind(&construct_string);
  {
    Node* const result =
        a->AllocateSeqOneByteString(context, var_length.value());
    Node* const flags_intptr = var_flags.value();

    Variable var_offset(a, MachineType::PointerRepresentation());
    var_offset.Bind(
        a->IntPtrConstant(SeqOneByteString::kHeaderSize - kHeapObjectTag));

    Label label_global(a), label_ignorecase(a), label_multiline(a),
        label_unicode(a), label_sticky(a), out(a);

#define CASE_FOR_FLAG(FLAG, CHAR, LABEL, NEXT_LABEL)                  \
  do {                                                                \
    a->Bind(&LABEL);                                                  \
    Node* const mask = a->IntPtrConstant(FLAG);                       \
    a->GotoIf(a->WordEqual(a->WordAnd(flags_intptr, mask), int_zero), \
              &NEXT_LABEL);                                           \
    Node* const value = a->IntPtrConstant(CHAR);                      \
    a->StoreNoWriteBarrier(MachineRepresentation::kWord8, result,     \
                           var_offset.value(), value);                \
    var_offset.Bind(a->IntPtrAdd(var_offset.value(), int_one));       \
    a->Goto(&NEXT_LABEL);                                             \
  } while (false)

    a->Goto(&label_global);
    CASE_FOR_FLAG(JSRegExp::kGlobal, 'g', label_global, label_ignorecase);
    CASE_FOR_FLAG(JSRegExp::kIgnoreCase, 'i', label_ignorecase,
                  label_multiline);
    CASE_FOR_FLAG(JSRegExp::kMultiline, 'm', label_multiline, label_unicode);
    CASE_FOR_FLAG(JSRegExp::kUnicode, 'u', label_unicode, label_sticky);
    CASE_FOR_FLAG(JSRegExp::kSticky, 'y', label_sticky, out);
#undef CASE_FOR_FLAG

    a->Bind(&out);
    a->Return(result);
  }
}

// ES6 21.2.5.10.
BUILTIN(RegExpPrototypeSourceGetter) {
  HandleScope scope(isolate);

  Handle<Object> recv = args.receiver();
  if (!recv->IsJSRegExp()) {
    Handle<JSFunction> regexp_fun = isolate->regexp_function();
    if (*recv == regexp_fun->prototype()) {
      isolate->CountUsage(v8::Isolate::kRegExpPrototypeSourceGetter);
      return *isolate->factory()->NewStringFromAsciiChecked("(?:)");
    }
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kRegExpNonRegExp,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "RegExp.prototype.source")));
  }

  Handle<JSRegExp> regexp = Handle<JSRegExp>::cast(recv);
  return regexp->source();
}

BUILTIN(RegExpPrototypeToString) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSReceiver, recv, "RegExp.prototype.toString");

  if (*recv == isolate->regexp_function()->prototype()) {
    isolate->CountUsage(v8::Isolate::kRegExpPrototypeToString);
  }

  IncrementalStringBuilder builder(isolate);

  builder.AppendCharacter('/');
  {
    Handle<Object> source;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, source,
        JSReceiver::GetProperty(recv, isolate->factory()->source_string()));
    Handle<String> source_str;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, source_str,
                                       Object::ToString(isolate, source));
    builder.AppendString(source_str);
  }

  builder.AppendCharacter('/');
  {
    Handle<Object> flags;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, flags,
        JSReceiver::GetProperty(recv, isolate->factory()->flags_string()));
    Handle<String> flags_str;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, flags_str,
                                       Object::ToString(isolate, flags));
    builder.AppendString(flags_str);
  }

  RETURN_RESULT_OR_FAILURE(isolate, builder.Finish());
}

// ES6 21.2.4.2.
BUILTIN(RegExpPrototypeSpeciesGetter) {
  HandleScope scope(isolate);
  return *args.receiver();
}

namespace {

void Generate_FlagGetter(CodeStubAssembler* a, JSRegExp::Flag flag,
                         v8::Isolate::UseCounterFeature counter,
                         const char* method_name) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Node* const receiver = a->Parameter(0);
  Node* const context = a->Parameter(3);

  Isolate* isolate = a->isolate();
  Node* const int_zero = a->IntPtrConstant(0);

  // Check whether we have an unmodified regexp instance.
  Label if_isunmodifiedjsregexp(a),
      if_isnotunmodifiedjsregexp(a, Label::kDeferred);

  a->GotoIf(a->WordIsSmi(receiver), &if_isnotunmodifiedjsregexp);

  Node* const receiver_map = a->LoadMap(receiver);
  Node* const instance_type = a->LoadMapInstanceType(receiver_map);

  a->Branch(a->Word32Equal(instance_type, a->Int32Constant(JS_REGEXP_TYPE)),
            &if_isunmodifiedjsregexp, &if_isnotunmodifiedjsregexp);

  a->Bind(&if_isunmodifiedjsregexp);
  {
    // Refer to JSRegExp's flag property on the fast-path.
    Node* const flags_smi =
        a->LoadObjectField(receiver, JSRegExp::kFlagsOffset);
    Node* const flags_intptr = a->SmiUntag(flags_smi);
    Node* const mask = a->IntPtrConstant(flag);
    Node* const is_global =
        a->WordNotEqual(a->WordAnd(flags_intptr, mask), int_zero);
    a->Return(a->Select(is_global, a->TrueConstant(), a->FalseConstant()));
  }

  a->Bind(&if_isnotunmodifiedjsregexp);
  {
    Node* const native_context = a->LoadNativeContext(context);
    Node* const regexp_fun =
        a->LoadContextElement(native_context, Context::REGEXP_FUNCTION_INDEX);
    Node* const initial_map = a->LoadObjectField(
        regexp_fun, JSFunction::kPrototypeOrInitialMapOffset);
    Node* const initial_prototype = a->LoadMapPrototype(initial_map);

    Label if_isprototype(a), if_isnotprototype(a);
    a->Branch(a->WordEqual(receiver, initial_prototype), &if_isprototype,
              &if_isnotprototype);

    a->Bind(&if_isprototype);
    {
      Node* const counter_smi = a->SmiConstant(Smi::FromInt(counter));
      a->CallRuntime(Runtime::kIncrementUseCounter, context, counter_smi);
      a->Return(a->UndefinedConstant());
    }

    a->Bind(&if_isnotprototype);
    {
      Node* const message_id =
          a->SmiConstant(Smi::FromInt(MessageTemplate::kRegExpNonRegExp));
      Node* const method_name_str = a->HeapConstant(
          isolate->factory()->NewStringFromAsciiChecked(method_name));
      a->CallRuntime(Runtime::kThrowTypeError, context, message_id,
                     method_name_str);
      a->Return(a->UndefinedConstant());  // Never reached.
    }
  }
}

}  // namespace

// ES6 21.2.5.4.
void Builtins::Generate_RegExpPrototypeGlobalGetter(CodeStubAssembler* a) {
  Generate_FlagGetter(a, JSRegExp::kGlobal,
                      v8::Isolate::kRegExpPrototypeOldFlagGetter,
                      "RegExp.prototype.global");
}

// ES6 21.2.5.5.
void Builtins::Generate_RegExpPrototypeIgnoreCaseGetter(CodeStubAssembler* a) {
  Generate_FlagGetter(a, JSRegExp::kIgnoreCase,
                      v8::Isolate::kRegExpPrototypeOldFlagGetter,
                      "RegExp.prototype.ignoreCase");
}

// ES6 21.2.5.7.
void Builtins::Generate_RegExpPrototypeMultilineGetter(CodeStubAssembler* a) {
  Generate_FlagGetter(a, JSRegExp::kMultiline,
                      v8::Isolate::kRegExpPrototypeOldFlagGetter,
                      "RegExp.prototype.multiline");
}

// ES6 21.2.5.12.
void Builtins::Generate_RegExpPrototypeStickyGetter(CodeStubAssembler* a) {
  Generate_FlagGetter(a, JSRegExp::kSticky,
                      v8::Isolate::kRegExpPrototypeStickyGetter,
                      "RegExp.prototype.sticky");
}

// ES6 21.2.5.15.
void Builtins::Generate_RegExpPrototypeUnicodeGetter(CodeStubAssembler* a) {
  Generate_FlagGetter(a, JSRegExp::kUnicode,
                      v8::Isolate::kRegExpPrototypeUnicodeGetter,
                      "RegExp.prototype.unicode");
}

namespace {

// Constants for accessing RegExpLastMatchInfo.
// TODO(jgruber): Currently, RegExpLastMatchInfo is still a JSObject maintained
// and accessed from JS. This is a crutch until all RegExp logic is ported, then
// we can take care of RegExpLastMatchInfo.

Handle<Object> GetLastMatchField(Isolate* isolate, int index) {
  Handle<JSObject> last_match_info = isolate->regexp_last_match_info();
  return JSReceiver::GetElement(isolate, last_match_info, index)
      .ToHandleChecked();
}

void SetLastMatchField(Isolate* isolate, int index, Handle<Object> value) {
  Handle<JSObject> last_match_info = isolate->regexp_last_match_info();
  JSReceiver::SetElement(isolate, last_match_info, index, value, SLOPPY)
      .ToHandleChecked();
}

int GetLastMatchNumberOfCaptures(Isolate* isolate) {
  Handle<Object> obj =
      GetLastMatchField(isolate, RegExpImpl::kLastCaptureCount);
  return Handle<Smi>::cast(obj)->value();
}

Handle<String> GetLastMatchSubject(Isolate* isolate) {
  return Handle<String>::cast(
      GetLastMatchField(isolate, RegExpImpl::kLastSubject));
}

Handle<Object> GetLastMatchInput(Isolate* isolate) {
  return GetLastMatchField(isolate, RegExpImpl::kLastInput);
}

int GetLastMatchCapture(Isolate* isolate, int i) {
  Handle<Object> obj =
      GetLastMatchField(isolate, RegExpImpl::kFirstCapture + i);
  return Handle<Smi>::cast(obj)->value();
}

Object* GenericCaptureGetter(Isolate* isolate, int capture) {
  HandleScope scope(isolate);
  const int index = capture * 2;
  if (index >= GetLastMatchNumberOfCaptures(isolate)) {
    return isolate->heap()->empty_string();
  }

  const int match_start = GetLastMatchCapture(isolate, index);
  const int match_end = GetLastMatchCapture(isolate, index + 1);
  if (match_start == -1 || match_end == -1) {
    return isolate->heap()->empty_string();
  }

  Handle<String> last_subject = GetLastMatchSubject(isolate);
  return *isolate->factory()->NewSubString(last_subject, match_start,
                                           match_end);
}

}  // namespace

// The properties $1..$9 are the first nine capturing substrings of the last
// successful match, or ''.  The function RegExpMakeCaptureGetter will be
// called with indices from 1 to 9.
#define DEFINE_CAPTURE_GETTER(i)             \
  BUILTIN(RegExpCapture##i##Getter) {        \
    HandleScope scope(isolate);              \
    return GenericCaptureGetter(isolate, i); \
  }
DEFINE_CAPTURE_GETTER(1)
DEFINE_CAPTURE_GETTER(2)
DEFINE_CAPTURE_GETTER(3)
DEFINE_CAPTURE_GETTER(4)
DEFINE_CAPTURE_GETTER(5)
DEFINE_CAPTURE_GETTER(6)
DEFINE_CAPTURE_GETTER(7)
DEFINE_CAPTURE_GETTER(8)
DEFINE_CAPTURE_GETTER(9)
#undef DEFINE_CAPTURE_GETTER

// The properties `input` and `$_` are aliases for each other.  When this
// value is set, the value it is set to is coerced to a string.
// Getter and setter for the input.

BUILTIN(RegExpInputGetter) {
  HandleScope scope(isolate);
  Handle<Object> obj = GetLastMatchInput(isolate);
  return obj->IsUndefined(isolate) ? isolate->heap()->empty_string()
                                   : String::cast(*obj);
}

BUILTIN(RegExpInputSetter) {
  HandleScope scope(isolate);
  Handle<Object> value = args.atOrUndefined(isolate, 1);
  Handle<String> str;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, str,
                                     Object::ToString(isolate, value));
  SetLastMatchField(isolate, RegExpImpl::kLastInput, str);
  return isolate->heap()->undefined_value();
}

// Getters for the static properties lastMatch, lastParen, leftContext, and
// rightContext of the RegExp constructor.  The properties are computed based
// on the captures array of the last successful match and the subject string
// of the last successful match.
BUILTIN(RegExpLastMatchGetter) {
  HandleScope scope(isolate);
  return GenericCaptureGetter(isolate, 0);
}

BUILTIN(RegExpLastParenGetter) {
  HandleScope scope(isolate);
  const int length = GetLastMatchNumberOfCaptures(isolate);
  if (length <= 2) return isolate->heap()->empty_string();  // No captures.

  DCHECK_EQ(0, length % 2);
  const int last_capture = (length / 2) - 1;

  // We match the SpiderMonkey behavior: return the substring defined by the
  // last pair (after the first pair) of elements of the capture array even if
  // it is empty.
  return GenericCaptureGetter(isolate, last_capture);
}

BUILTIN(RegExpLeftContextGetter) {
  HandleScope scope(isolate);
  const int start_index = GetLastMatchCapture(isolate, 0);
  Handle<String> last_subject = GetLastMatchSubject(isolate);
  return *isolate->factory()->NewSubString(last_subject, 0, start_index);
}

BUILTIN(RegExpRightContextGetter) {
  HandleScope scope(isolate);
  const int start_index = GetLastMatchCapture(isolate, 1);
  Handle<String> last_subject = GetLastMatchSubject(isolate);
  const int len = last_subject->length();
  return *isolate->factory()->NewSubString(last_subject, start_index, len);
}

namespace {

V8_INLINE bool HasInitialRegExpMap(Isolate* isolate, Handle<JSReceiver> recv) {
  return recv->map() == isolate->regexp_function()->initial_map();
}

}  // namespace

// ES#sec-regexpexec Runtime Semantics: RegExpExec ( R, S )
// Also takes an optional exec method in case our caller
// has already fetched exec.
MaybeHandle<Object> RegExpExec(Isolate* isolate, Handle<JSReceiver> regexp,
                               Handle<String> string, Handle<Object> exec) {
  if (exec->IsUndefined(isolate)) {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, exec,
        Object::GetProperty(
            regexp, isolate->factory()->NewStringFromAsciiChecked("exec")),
        Object);
  }

  if (exec->IsCallable()) {
    const int argc = 1;
    ScopedVector<Handle<Object>> argv(argc);
    argv[0] = string;

    Handle<Object> result;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, result,
        Execution::Call(isolate, exec, regexp, argc, argv.start()), Object);

    if (!result->IsJSReceiver() && !result->IsNull(isolate)) {
      THROW_NEW_ERROR(isolate,
                      NewTypeError(MessageTemplate::kInvalidRegExpExecResult),
                      Object);
    }
    return result;
  }

  if (!regexp->IsJSRegExp()) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     "RegExp.prototype.exec"),
                                 regexp),
                    Object);
  }

  {
    Handle<JSFunction> regexp_exec = isolate->regexp_exec_function();

    const int argc = 1;
    ScopedVector<Handle<Object>> argv(argc);
    argv[0] = string;

    return Execution::Call(isolate, exec, regexp_exec, argc, argv.start());
  }
}

// ES#sec-regexp.prototype.test
// RegExp.prototype.test ( S )
BUILTIN(RegExpPrototypeTest) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSReceiver, recv, "RegExp.prototype.test");

  Handle<Object> string_obj = args.atOrUndefined(isolate, 1);

  Handle<String> string;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, string,
                                     Object::ToString(isolate, string_obj));

  Handle<Object> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result,
      RegExpExec(isolate, recv, string, isolate->factory()->undefined_value()));

  return isolate->heap()->ToBoolean(!result->IsNull(isolate));
}

namespace {

// ES#sec-advancestringindex
// AdvanceStringIndex ( S, index, unicode )
int AdvanceStringIndex(Isolate* isolate, Handle<String> string, int index,
                       bool unicode) {
  int increment = 1;

  if (unicode && index < string->length()) {
    const uint16_t first = string->Get(index);
    if (first >= 0xD800 && first <= 0xDBFF && string->length() > index + 1) {
      const uint16_t second = string->Get(index + 1);
      if (second >= 0xDC00 && second <= 0xDFFF) {
        increment = 2;
      }
    }
  }

  return increment;
}

MaybeHandle<Object> SetLastIndex(Isolate* isolate, Handle<JSReceiver> recv,
                                 int value) {
  if (HasInitialRegExpMap(isolate, recv)) {
    JSRegExp::cast(*recv)->SetLastIndex(value);
    return recv;
  } else {
    return Object::SetProperty(recv, isolate->factory()->lastIndex_string(),
                               handle(Smi::FromInt(value), isolate), STRICT);
  }
}

MaybeHandle<Object> GetLastIndex(Isolate* isolate, Handle<JSReceiver> recv) {
  if (HasInitialRegExpMap(isolate, recv)) {
    return handle(JSRegExp::cast(*recv)->LastIndex(), isolate);
  } else {
    return Object::GetProperty(recv, isolate->factory()->lastIndex_string());
  }
}

MaybeHandle<Object> SetAdvancedStringIndex(Isolate* isolate,
                                           Handle<JSReceiver> regexp,
                                           Handle<String> string,
                                           bool unicode) {
  Handle<Object> last_index_obj;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, last_index_obj,
                             GetLastIndex(isolate, regexp), Object);

  ASSIGN_RETURN_ON_EXCEPTION(isolate, last_index_obj,
                             Object::ToLength(isolate, last_index_obj), Object);

  const int last_index = Handle<Smi>::cast(last_index_obj)->value();
  const int new_last_index =
      last_index + AdvanceStringIndex(isolate, string, last_index, unicode);

  return SetLastIndex(isolate, regexp, new_last_index);
}

}  // namespace

// ES#sec-regexp.prototype-@@match
// RegExp.prototype [ @@match ] ( string )
BUILTIN(RegExpPrototypeMatch) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSReceiver, recv, "RegExp.prototype.@@match");

  Handle<Object> string_obj = args.atOrUndefined(isolate, 1);

  Handle<String> string;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, string,
                                     Object::ToString(isolate, string_obj));

  Handle<Object> global_obj;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, global_obj,
      JSReceiver::GetProperty(recv, isolate->factory()->global_string()));
  const bool global = global_obj->BooleanValue();

  if (!global) {
    RETURN_RESULT_OR_FAILURE(isolate,
                             RegExpExec(isolate, recv, string,
                                        isolate->factory()->undefined_value()));
  }

  Handle<Object> unicode_obj;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, unicode_obj,
      JSReceiver::GetProperty(recv, isolate->factory()->unicode_string()));
  const bool unicode = unicode_obj->BooleanValue();

  RETURN_FAILURE_ON_EXCEPTION(isolate, SetLastIndex(isolate, recv, 0));

  static const int kInitialArraySize = 8;
  Handle<FixedArray> elems =
      isolate->factory()->NewFixedArrayWithHoles(kInitialArraySize);

  int n = 0;
  for (;; n++) {
    Handle<Object> result;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, result, RegExpExec(isolate, recv, string,
                                    isolate->factory()->undefined_value()));

    if (result->IsNull(isolate)) {
      if (n == 0) return isolate->heap()->null_value();
      break;
    }

    Handle<Object> match_obj;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, match_obj,
                                       Object::GetElement(isolate, result, 0));

    Handle<String> match;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, match,
                                       Object::ToString(isolate, match_obj));

    elems = FixedArray::SetAndGrow(elems, n, match);

    if (match->length() == 0) {
      RETURN_FAILURE_ON_EXCEPTION(
          isolate, SetAdvancedStringIndex(isolate, recv, string, unicode));
    }
  }

  elems->Shrink(n);
  return *isolate->factory()->NewJSArrayWithElements(elems);
}

// ES#sec-regexp.prototype-@@search
// RegExp.prototype [ @@search ] ( string )
BUILTIN(RegExpPrototypeSearch) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSReceiver, recv, "RegExp.prototype.@@search");

  Handle<Object> string_obj = args.atOrUndefined(isolate, 1);

  Handle<String> string;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, string,
                                     Object::ToString(isolate, string_obj));

  Handle<Object> previous_last_index_obj;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, previous_last_index_obj,
                                     GetLastIndex(isolate, recv));

  if (!previous_last_index_obj->IsSmi() ||
      Smi::cast(*previous_last_index_obj)->value() != 0) {
    RETURN_FAILURE_ON_EXCEPTION(isolate, SetLastIndex(isolate, recv, 0));
  }

  Handle<Object> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result,
      RegExpExec(isolate, recv, string, isolate->factory()->undefined_value()));

  Handle<Object> current_last_index_obj;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, current_last_index_obj,
                                     GetLastIndex(isolate, recv));

  Maybe<bool> is_last_index_unchanged =
      Object::Equals(current_last_index_obj, previous_last_index_obj);
  if (is_last_index_unchanged.IsNothing()) return isolate->pending_exception();
  if (!is_last_index_unchanged.FromJust()) {
    if (previous_last_index_obj->IsSmi()) {
      RETURN_FAILURE_ON_EXCEPTION(
          isolate, SetLastIndex(isolate, recv,
                                Smi::cast(*previous_last_index_obj)->value()));
    } else {
      RETURN_FAILURE_ON_EXCEPTION(
          isolate,
          Object::SetProperty(recv, isolate->factory()->lastIndex_string(),
                              previous_last_index_obj, STRICT));
    }
  }

  if (result->IsNull(isolate)) return Smi::FromInt(-1);

  RETURN_RESULT_OR_FAILURE(
      isolate, Object::GetProperty(result, isolate->factory()->index_string()));
}

}  // namespace internal
}  // namespace v8
