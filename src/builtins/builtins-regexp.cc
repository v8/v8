// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"

#include "src/code-factory.h"
#include "src/regexp/jsregexp.h"
#include "src/regexp/regexp-utils.h"
#include "src/string-builder.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// ES6 section 21.2 RegExp Objects

namespace {

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
  return JSRegExp::Initialize(regexp, pattern_string, flags_string);
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
    Maybe<bool> maybe_pattern_is_regexp =
        RegExpUtils::IsRegExp(isolate, pattern);
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

// The fast-path of StoreLastIndex when regexp is guaranteed to be an unmodified
// JSRegExp instance.
void FastStoreLastIndex(CodeStubAssembler* a, compiler::Node* context,
                        compiler::Node* regexp, compiler::Node* value) {
  // Store the in-object field.
  static const int field_offset =
      JSRegExp::kSize + JSRegExp::kLastIndexFieldIndex * kPointerSize;
  a->StoreObjectField(regexp, field_offset, value);
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
    FastStoreLastIndex(a, context, regexp, value);
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
                                                compiler::Node* match_info,
                                                compiler::Node* string) {
  typedef CodeStubAssembler::Variable Variable;
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Label out(a);

  CodeStubAssembler::ParameterMode mode = CodeStubAssembler::INTPTR_PARAMETERS;
  Node* const num_indices = a->SmiUntag(a->LoadFixedArrayElement(
      match_info, a->IntPtrConstant(RegExpMatchInfo::kNumberOfCapturesIndex), 0,
      mode));
  Node* const num_results = a->SmiTag(a->WordShr(num_indices, 1));
  Node* const start = a->LoadFixedArrayElement(
      match_info, a->IntPtrConstant(RegExpMatchInfo::kFirstCaptureIndex), 0,
      mode);
  Node* const end = a->LoadFixedArrayElement(
      match_info, a->IntPtrConstant(RegExpMatchInfo::kFirstCaptureIndex + 1), 0,
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
  Node* const limit = a->IntPtrAdd(
      a->IntPtrConstant(RegExpMatchInfo::kFirstCaptureIndex), num_indices);

  Variable var_from_cursor(a, MachineType::PointerRepresentation());
  Variable var_to_cursor(a, MachineType::PointerRepresentation());

  var_from_cursor.Bind(
      a->IntPtrConstant(RegExpMatchInfo::kFirstCaptureIndex + 2));
  var_to_cursor.Bind(a->IntPtrConstant(1));

  Variable* vars[] = {&var_from_cursor, &var_to_cursor};
  Label loop(a, 2, vars);

  a->Goto(&loop);
  a->Bind(&loop);
  {
    Node* const from_cursor = var_from_cursor.value();
    Node* const to_cursor = var_to_cursor.value();
    Node* const start = a->LoadFixedArrayElement(match_info, from_cursor);

    Label next_iter(a);
    a->GotoIf(a->SmiEqual(start, a->SmiConstant(Smi::FromInt(-1))), &next_iter);

    Node* const from_cursor_plus1 =
        a->IntPtrAdd(from_cursor, a->IntPtrConstant(1));
    Node* const end = a->LoadFixedArrayElement(match_info, from_cursor_plus1);

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
      a->GotoUnless(a->TaggedIsSmi(lastindex), &if_isoob);
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

    // {match_indices} is either null or the RegExpMatchInfo array.
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
    a->GotoUnless(should_update_last_index, &construct_result);

    // Update the new last index from {match_indices}.
    Node* const new_lastindex = a->LoadFixedArrayElement(
        match_indices,
        a->IntPtrConstant(RegExpMatchInfo::kFirstCaptureIndex + 1));

    StoreLastIndex(a, context, has_initialmap, regexp, new_lastindex);
    a->Goto(&construct_result);

    a->Bind(&construct_result);
    {
      Node* result = ConstructNewResultFromMatchInfo(isolate, a, context,
                                                     match_indices, string);
      a->Return(result);
    }
  }
}

namespace {

compiler::Node* ThrowIfNotJSReceiver(CodeStubAssembler* a, Isolate* isolate,
                                     compiler::Node* context,
                                     compiler::Node* value,
                                     MessageTemplate::Template msg_template,
                                     char const* method_name) {
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;
  typedef CodeStubAssembler::Variable Variable;

  Label out(a), throw_exception(a, Label::kDeferred);
  Variable var_value_map(a, MachineRepresentation::kTagged);

  a->GotoIf(a->TaggedIsSmi(value), &throw_exception);

  // Load the instance type of the {value}.
  var_value_map.Bind(a->LoadMap(value));
  Node* const value_instance_type =
      a->LoadMapInstanceType(var_value_map.value());

  a->Branch(a->IsJSReceiverInstanceType(value_instance_type), &out,
            &throw_exception);

  // The {value} is not a compatible receiver for this method.
  a->Bind(&throw_exception);
  {
    Node* const message_id = a->SmiConstant(Smi::FromInt(msg_template));
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

// RegExp fast path implementations rely on unmodified JSRegExp instances.
// We use a fairly coarse granularity for this and simply check whether both
// the regexp itself is unmodified (i.e. its map has not changed) and its
// prototype is unmodified.
void BranchIfFastPath(CodeStubAssembler* a, compiler::Node* context,
                      compiler::Node* map,
                      CodeStubAssembler::Label* if_isunmodified,
                      CodeStubAssembler::Label* if_ismodified) {
  typedef compiler::Node Node;

  Node* const native_context = a->LoadNativeContext(context);
  Node* const regexp_fun =
      a->LoadContextElement(native_context, Context::REGEXP_FUNCTION_INDEX);
  Node* const initial_map =
      a->LoadObjectField(regexp_fun, JSFunction::kPrototypeOrInitialMapOffset);
  Node* const has_initialmap = a->WordEqual(map, initial_map);

  a->GotoUnless(has_initialmap, if_ismodified);

  Node* const initial_proto_initial_map = a->LoadContextElement(
      native_context, Context::REGEXP_PROTOTYPE_MAP_INDEX);
  Node* const proto_map = a->LoadMap(a->LoadMapPrototype(map));
  Node* const proto_has_initialmap =
      a->WordEqual(proto_map, initial_proto_initial_map);

  // TODO(ishell): Update this check once map changes for constant field
  // tracking are landing.

  a->Branch(proto_has_initialmap, if_isunmodified, if_ismodified);
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
                                         MessageTemplate::kRegExpNonObject,
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

  a->GotoIf(a->TaggedIsSmi(receiver), &if_isnotunmodifiedjsregexp);

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

// The properties $1..$9 are the first nine capturing substrings of the last
// successful match, or ''.  The function RegExpMakeCaptureGetter will be
// called with indices from 1 to 9.
#define DEFINE_CAPTURE_GETTER(i)                        \
  BUILTIN(RegExpCapture##i##Getter) {                   \
    HandleScope scope(isolate);                         \
    return *RegExpUtils::GenericCaptureGetter(          \
        isolate, isolate->regexp_last_match_info(), i); \
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
  Handle<Object> obj(isolate->regexp_last_match_info()->LastInput(), isolate);
  return obj->IsUndefined(isolate) ? isolate->heap()->empty_string()
                                   : String::cast(*obj);
}

BUILTIN(RegExpInputSetter) {
  HandleScope scope(isolate);
  Handle<Object> value = args.atOrUndefined(isolate, 1);
  Handle<String> str;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, str,
                                     Object::ToString(isolate, value));
  isolate->regexp_last_match_info()->SetLastInput(*str);
  return isolate->heap()->undefined_value();
}

// Getters for the static properties lastMatch, lastParen, leftContext, and
// rightContext of the RegExp constructor.  The properties are computed based
// on the captures array of the last successful match and the subject string
// of the last successful match.
BUILTIN(RegExpLastMatchGetter) {
  HandleScope scope(isolate);
  return *RegExpUtils::GenericCaptureGetter(
      isolate, isolate->regexp_last_match_info(), 0);
}

BUILTIN(RegExpLastParenGetter) {
  HandleScope scope(isolate);
  Handle<RegExpMatchInfo> match_info = isolate->regexp_last_match_info();
  const int length = match_info->NumberOfCaptureRegisters();
  if (length <= 2) return isolate->heap()->empty_string();  // No captures.

  DCHECK_EQ(0, length % 2);
  const int last_capture = (length / 2) - 1;

  // We match the SpiderMonkey behavior: return the substring defined by the
  // last pair (after the first pair) of elements of the capture array even if
  // it is empty.
  return *RegExpUtils::GenericCaptureGetter(isolate, match_info, last_capture);
}

BUILTIN(RegExpLeftContextGetter) {
  HandleScope scope(isolate);
  Handle<RegExpMatchInfo> match_info = isolate->regexp_last_match_info();
  const int start_index = match_info->Capture(0);
  Handle<String> last_subject(match_info->LastSubject());
  return *isolate->factory()->NewSubString(last_subject, 0, start_index);
}

BUILTIN(RegExpRightContextGetter) {
  HandleScope scope(isolate);
  Handle<RegExpMatchInfo> match_info = isolate->regexp_last_match_info();
  const int start_index = match_info->Capture(1);
  Handle<String> last_subject(match_info->LastSubject());
  const int len = last_subject->length();
  return *isolate->factory()->NewSubString(last_subject, start_index, len);
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
      RegExpUtils::RegExpExec(isolate, recv, string,
                              isolate->factory()->undefined_value()));

  return isolate->heap()->ToBoolean(!result->IsNull(isolate));
}

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
    RETURN_RESULT_OR_FAILURE(
        isolate,
        RegExpUtils::RegExpExec(isolate, recv, string,
                                isolate->factory()->undefined_value()));
  }

  Handle<Object> unicode_obj;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, unicode_obj,
      JSReceiver::GetProperty(recv, isolate->factory()->unicode_string()));
  const bool unicode = unicode_obj->BooleanValue();

  RETURN_FAILURE_ON_EXCEPTION(isolate,
                              RegExpUtils::SetLastIndex(isolate, recv, 0));

  static const int kInitialArraySize = 8;
  Handle<FixedArray> elems =
      isolate->factory()->NewFixedArrayWithHoles(kInitialArraySize);

  int n = 0;
  for (;; n++) {
    Handle<Object> result;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, result,
        RegExpUtils::RegExpExec(isolate, recv, string,
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
      RETURN_FAILURE_ON_EXCEPTION(isolate, RegExpUtils::SetAdvancedStringIndex(
                                               isolate, recv, string, unicode));
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
                                     RegExpUtils::GetLastIndex(isolate, recv));

  if (!previous_last_index_obj->IsSmi() ||
      Smi::cast(*previous_last_index_obj)->value() != 0) {
    RETURN_FAILURE_ON_EXCEPTION(isolate,
                                RegExpUtils::SetLastIndex(isolate, recv, 0));
  }

  Handle<Object> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result,
      RegExpUtils::RegExpExec(isolate, recv, string,
                              isolate->factory()->undefined_value()));

  Handle<Object> current_last_index_obj;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, current_last_index_obj,
                                     RegExpUtils::GetLastIndex(isolate, recv));

  Maybe<bool> is_last_index_unchanged =
      Object::Equals(current_last_index_obj, previous_last_index_obj);
  if (is_last_index_unchanged.IsNothing()) return isolate->pending_exception();
  if (!is_last_index_unchanged.FromJust()) {
    if (previous_last_index_obj->IsSmi()) {
      RETURN_FAILURE_ON_EXCEPTION(
          isolate,
          RegExpUtils::SetLastIndex(
              isolate, recv, Smi::cast(*previous_last_index_obj)->value()));
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

namespace {

MUST_USE_RESULT MaybeHandle<Object> ToUint32(Isolate* isolate,
                                             Handle<Object> object,
                                             uint32_t* out) {
  if (object->IsUndefined(isolate)) {
    *out = kMaxUInt32;
    return object;
  }

  Handle<Object> number;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, number, Object::ToNumber(object), Object);
  *out = NumberToUint32(*number);
  return object;
}

bool AtSurrogatePair(Isolate* isolate, Handle<String> string, int index) {
  if (index + 1 >= string->length()) return false;
  const uint16_t first = string->Get(index);
  if (first < 0xD800 || first > 0xDBFF) return false;
  const uint16_t second = string->Get(index + 1);
  return (second >= 0xDC00 && second <= 0xDFFF);
}

Handle<JSArray> NewJSArrayWithElements(Isolate* isolate,
                                       Handle<FixedArray> elems,
                                       int num_elems) {
  elems->Shrink(num_elems);
  return isolate->factory()->NewJSArrayWithElements(elems);
}

MaybeHandle<JSArray> RegExpSplit(Isolate* isolate, Handle<JSRegExp> regexp,
                                 Handle<String> string,
                                 Handle<Object> limit_obj) {
  Factory* factory = isolate->factory();

  uint32_t limit;
  RETURN_ON_EXCEPTION(isolate, ToUint32(isolate, limit_obj, &limit), JSArray);

  const int length = string->length();

  if (limit == 0) return factory->NewJSArray(0);

  Handle<RegExpMatchInfo> last_match_info = isolate->regexp_last_match_info();

  if (length == 0) {
    Handle<Object> match_indices;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, match_indices,
        RegExpImpl::Exec(regexp, string, 0, last_match_info), JSArray);

    if (!match_indices->IsNull(isolate)) return factory->NewJSArray(0);

    Handle<FixedArray> elems = factory->NewUninitializedFixedArray(1);
    elems->set(0, *string);
    return factory->NewJSArrayWithElements(elems);
  }

  int current_index = 0;
  int start_index = 0;
  int start_match = 0;

  static const int kInitialArraySize = 8;
  Handle<FixedArray> elems = factory->NewFixedArrayWithHoles(kInitialArraySize);
  int num_elems = 0;

  while (true) {
    if (start_index == length) {
      Handle<String> substr =
          factory->NewSubString(string, current_index, length);
      elems = FixedArray::SetAndGrow(elems, num_elems++, substr);
      break;
    }

    Handle<Object> match_indices_obj;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, match_indices_obj,
        RegExpImpl::Exec(regexp, string, start_index,
                         isolate->regexp_last_match_info()),
        JSArray);

    if (match_indices_obj->IsNull(isolate)) {
      Handle<String> substr =
          factory->NewSubString(string, current_index, length);
      elems = FixedArray::SetAndGrow(elems, num_elems++, substr);
      break;
    }

    auto match_indices = Handle<RegExpMatchInfo>::cast(match_indices_obj);

    start_match = match_indices->Capture(0);

    if (start_match == length) {
      Handle<String> substr =
          factory->NewSubString(string, current_index, length);
      elems = FixedArray::SetAndGrow(elems, num_elems++, substr);
      break;
    }

    const int end_index = match_indices->Capture(1);

    if (start_index == end_index && end_index == current_index) {
      const bool unicode = (regexp->GetFlags() & JSRegExp::kUnicode) != 0;
      if (unicode && AtSurrogatePair(isolate, string, start_index)) {
        start_index += 2;
      } else {
        start_index += 1;
      }
      continue;
    }

    {
      Handle<String> substr =
          factory->NewSubString(string, current_index, start_match);
      elems = FixedArray::SetAndGrow(elems, num_elems++, substr);
    }

    if (num_elems == limit) break;

    for (int i = 2; i < match_indices->NumberOfCaptureRegisters(); i += 2) {
      const int start = match_indices->Capture(i);
      const int end = match_indices->Capture(i + 1);

      if (end != -1) {
        Handle<String> substr = factory->NewSubString(string, start, end);
        elems = FixedArray::SetAndGrow(elems, num_elems++, substr);
      } else {
        elems = FixedArray::SetAndGrow(elems, num_elems++,
                                       factory->undefined_value());
      }

      if (num_elems == limit) {
        return NewJSArrayWithElements(isolate, elems, num_elems);
      }
    }

    start_index = current_index = end_index;
  }

  return NewJSArrayWithElements(isolate, elems, num_elems);
}

// ES##sec-speciesconstructor
// SpeciesConstructor ( O, defaultConstructor )
MaybeHandle<Object> SpeciesConstructor(Isolate* isolate,
                                       Handle<JSReceiver> recv,
                                       Handle<JSFunction> default_ctor) {
  Handle<Object> ctor_obj;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, ctor_obj,
      JSObject::GetProperty(recv, isolate->factory()->constructor_string()),
      Object);

  if (ctor_obj->IsUndefined(isolate)) return default_ctor;

  if (!ctor_obj->IsJSReceiver()) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kConstructorNotReceiver),
                    Object);
  }

  Handle<JSReceiver> ctor = Handle<JSReceiver>::cast(ctor_obj);

  Handle<Object> species;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, species,
      JSObject::GetProperty(ctor, isolate->factory()->species_symbol()),
      Object);

  if (species->IsNull(isolate) || species->IsUndefined(isolate)) {
    return default_ctor;
  }

  if (species->IsConstructor()) return species;

  THROW_NEW_ERROR(
      isolate, NewTypeError(MessageTemplate::kSpeciesNotConstructor), Object);
}

}  // namespace

// ES#sec-regexp.prototype-@@split
// RegExp.prototype [ @@split ] ( string, limit )
BUILTIN(RegExpPrototypeSplit) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSReceiver, recv, "RegExp.prototype.@@split");

  Factory* factory = isolate->factory();

  Handle<Object> string_obj = args.atOrUndefined(isolate, 1);
  Handle<Object> limit_obj = args.atOrUndefined(isolate, 2);

  Handle<String> string;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, string,
                                     Object::ToString(isolate, string_obj));

  Handle<JSFunction> regexp_fun = isolate->regexp_function();
  Handle<Object> ctor;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, ctor, SpeciesConstructor(isolate, recv, regexp_fun));

  if (recv->IsJSRegExp() && *ctor == *regexp_fun) {
    Handle<Object> exec;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, exec, JSObject::GetProperty(
                           recv, factory->NewStringFromAsciiChecked("exec")));
    if (RegExpUtils::IsBuiltinExec(exec)) {
      RETURN_RESULT_OR_FAILURE(
          isolate, RegExpSplit(isolate, Handle<JSRegExp>::cast(recv), string,
                               limit_obj));
    }
  }

  Handle<Object> flags_obj;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, flags_obj, JSObject::GetProperty(recv, factory->flags_string()));

  Handle<String> flags;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, flags,
                                     Object::ToString(isolate, flags_obj));

  Handle<String> u_str = factory->LookupSingleCharacterStringFromCode('u');
  const bool unicode = (String::IndexOf(isolate, flags, u_str, 0) >= 0);

  Handle<String> y_str = factory->LookupSingleCharacterStringFromCode('y');
  const bool sticky = (String::IndexOf(isolate, flags, y_str, 0) >= 0);

  Handle<String> new_flags = flags;
  if (!sticky) {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, new_flags,
                                       factory->NewConsString(flags, y_str));
  }

  Handle<JSReceiver> splitter;
  {
    const int argc = 2;

    ScopedVector<Handle<Object>> argv(argc);
    argv[0] = recv;
    argv[1] = new_flags;

    Handle<JSFunction> ctor_fun = Handle<JSFunction>::cast(ctor);
    Handle<Object> splitter_obj;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, splitter_obj, Execution::New(ctor_fun, argc, argv.start()));

    splitter = Handle<JSReceiver>::cast(splitter_obj);
  }

  uint32_t limit;
  RETURN_FAILURE_ON_EXCEPTION(isolate, ToUint32(isolate, limit_obj, &limit));

  const int length = string->length();

  if (limit == 0) return *factory->NewJSArray(0);

  if (length == 0) {
    Handle<Object> result;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, result, RegExpUtils::RegExpExec(isolate, splitter, string,
                                                 factory->undefined_value()));

    if (!result->IsNull(isolate)) return *factory->NewJSArray(0);

    Handle<FixedArray> elems = factory->NewUninitializedFixedArray(1);
    elems->set(0, *string);
    return *factory->NewJSArrayWithElements(elems);
  }

  // TODO(jgruber): Wrap this in a helper class.
  static const int kInitialArraySize = 8;
  Handle<FixedArray> elems = factory->NewFixedArrayWithHoles(kInitialArraySize);
  int num_elems = 0;

  int string_index = 0;
  int prev_string_index = 0;
  while (string_index < length) {
    RETURN_FAILURE_ON_EXCEPTION(
        isolate, RegExpUtils::SetLastIndex(isolate, splitter, string_index));

    Handle<Object> result;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, result, RegExpUtils::RegExpExec(isolate, splitter, string,
                                                 factory->undefined_value()));

    if (result->IsNull(isolate)) {
      string_index += RegExpUtils::AdvanceStringIndex(isolate, string,
                                                      string_index, unicode);
      continue;
    }

    // TODO(jgruber): Extract toLength of some property into function.
    Handle<Object> last_index_obj;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, last_index_obj, RegExpUtils::GetLastIndex(isolate, splitter));

    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, last_index_obj, Object::ToLength(isolate, last_index_obj));
    const int last_index = Handle<Smi>::cast(last_index_obj)->value();

    const int end = std::min(last_index, length);
    if (end == prev_string_index) {
      string_index += RegExpUtils::AdvanceStringIndex(isolate, string,
                                                      string_index, unicode);
      continue;
    }

    {
      Handle<String> substr =
          factory->NewSubString(string, prev_string_index, string_index);
      elems = FixedArray::SetAndGrow(elems, num_elems++, substr);
      if (num_elems == limit) {
        return *NewJSArrayWithElements(isolate, elems, num_elems);
      }
    }

    prev_string_index = end;

    Handle<Object> num_captures_obj;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, num_captures_obj,
        Object::GetProperty(result, isolate->factory()->length_string()));

    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, num_captures_obj, Object::ToLength(isolate, num_captures_obj));
    const int num_captures =
        std::max(Handle<Smi>::cast(num_captures_obj)->value(), 0);

    for (int i = 1; i < num_captures; i++) {
      Handle<Object> capture;
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, capture, Object::GetElement(isolate, result, i));
      elems = FixedArray::SetAndGrow(elems, num_elems++, capture);
      if (num_elems == limit) {
        return *NewJSArrayWithElements(isolate, elems, num_elems);
      }
    }

    string_index = prev_string_index;
  }

  {
    Handle<String> substr =
        factory->NewSubString(string, prev_string_index, length);
    elems = FixedArray::SetAndGrow(elems, num_elems++, substr);
  }

  return *NewJSArrayWithElements(isolate, elems, num_elems);
}

namespace {

compiler::Node* ReplaceFastPath(CodeStubAssembler* a, compiler::Node* context,
                                compiler::Node* regexp,
                                compiler::Node* subject_string,
                                compiler::Node* replace_string) {
  // The fast path is reached only if {receiver} is an unmodified
  // JSRegExp instance, {replace_value} is non-callable, and
  // ToString({replace_value}) does not contain '$', i.e. we're doing a simple
  // string replacement.

  typedef CodeStubAssembler::Variable Variable;
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Isolate* const isolate = a->isolate();

  Node* const null = a->NullConstant();
  Node* const int_zero = a->IntPtrConstant(0);
  Node* const smi_zero = a->SmiConstant(Smi::kZero);

  Label out(a);
  Variable var_result(a, MachineRepresentation::kTagged);

  // Load the last match info.
  Node* const native_context = a->LoadNativeContext(context);
  Node* const last_match_info = a->LoadContextElement(
      native_context, Context::REGEXP_LAST_MATCH_INFO_INDEX);

  // Is {regexp} global?
  Label if_isglobal(a), if_isnonglobal(a);
  Node* const flags = a->LoadObjectField(regexp, JSRegExp::kFlagsOffset);
  Node* const is_global =
      a->WordAnd(a->SmiUntag(flags), a->IntPtrConstant(JSRegExp::kGlobal));
  a->Branch(a->WordEqual(is_global, int_zero), &if_isnonglobal, &if_isglobal);

  a->Bind(&if_isglobal);
  {
    // Hand off global regexps to runtime.
    FastStoreLastIndex(a, context, regexp, smi_zero);
    Node* const result =
        a->CallRuntime(Runtime::kStringReplaceGlobalRegExpWithString, context,
                       subject_string, regexp, replace_string, last_match_info);
    var_result.Bind(result);
    a->Goto(&out);
  }

  a->Bind(&if_isnonglobal);
  {
    // Run exec, then manually construct the resulting string.
    Callable exec_callable = CodeFactory::RegExpExec(isolate);
    Node* const match_indices =
        a->CallStub(exec_callable, context, regexp, subject_string, smi_zero,
                    last_match_info);

    Label if_matched(a), if_didnotmatch(a);
    a->Branch(a->WordEqual(match_indices, null), &if_didnotmatch, &if_matched);

    a->Bind(&if_didnotmatch);
    {
      FastStoreLastIndex(a, context, regexp, smi_zero);
      var_result.Bind(subject_string);
      a->Goto(&out);
    }

    a->Bind(&if_matched);
    {
      CodeStubAssembler::ParameterMode mode =
          CodeStubAssembler::INTPTR_PARAMETERS;

      Node* const subject_start = smi_zero;
      Node* const match_start = a->LoadFixedArrayElement(
          match_indices, a->IntPtrConstant(RegExpMatchInfo::kFirstCaptureIndex),
          0, mode);
      Node* const match_end = a->LoadFixedArrayElement(
          match_indices,
          a->IntPtrConstant(RegExpMatchInfo::kFirstCaptureIndex + 1), 0, mode);
      Node* const subject_end = a->LoadStringLength(subject_string);

      Label if_replaceisempty(a), if_replaceisnotempty(a);
      Node* const replace_length = a->LoadStringLength(replace_string);
      a->Branch(a->SmiEqual(replace_length, smi_zero), &if_replaceisempty,
                &if_replaceisnotempty);

      a->Bind(&if_replaceisempty);
      {
        // TODO(jgruber): We could skip many of the checks that using SubString
        // here entails.

        Node* const first_part =
            a->SubString(context, subject_string, subject_start, match_start);
        Node* const second_part =
            a->SubString(context, subject_string, match_end, subject_end);

        Node* const result = a->StringConcat(context, first_part, second_part);
        var_result.Bind(result);
        a->Goto(&out);
      }

      a->Bind(&if_replaceisnotempty);
      {
        Node* const first_part =
            a->SubString(context, subject_string, subject_start, match_start);
        Node* const second_part = replace_string;
        Node* const third_part =
            a->SubString(context, subject_string, match_end, subject_end);

        Node* result = a->StringConcat(context, first_part, second_part);
        result = a->StringConcat(context, result, third_part);

        var_result.Bind(result);
        a->Goto(&out);
      }
    }
  }

  a->Bind(&out);
  return var_result.value();
}

}  // namespace

// ES#sec-regexp.prototype-@@replace
// RegExp.prototype [ @@replace ] ( string, replaceValue )
void Builtins::Generate_RegExpPrototypeReplace(CodeStubAssembler* a) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Isolate* const isolate = a->isolate();

  Node* const maybe_receiver = a->Parameter(0);
  Node* const maybe_string = a->Parameter(1);
  Node* const replace_value = a->Parameter(2);
  Node* const context = a->Parameter(5);

  Node* const int_zero = a->IntPtrConstant(0);

  // Ensure {receiver} is a JSReceiver.
  Node* const map =
      ThrowIfNotJSReceiver(a, isolate, context, maybe_receiver,
                           MessageTemplate::kIncompatibleMethodReceiver,
                           "RegExp.prototype.@@replace");
  Node* const receiver = maybe_receiver;

  // Convert {maybe_string} to a String.
  Callable tostring_callable = CodeFactory::ToString(isolate);
  Node* const string = a->CallStub(tostring_callable, context, maybe_string);

  // Fast-path checks: 1. Is the {receiver} an unmodified JSRegExp instance?
  Label checkreplacecallable(a), runtime(a, Label::kDeferred), fastpath(a);
  BranchIfFastPath(a, context, map, &checkreplacecallable, &runtime);

  a->Bind(&checkreplacecallable);
  Node* const regexp = receiver;

  // 2. Is {replace_value} callable?
  Label checkreplacestring(a);
  a->GotoIf(a->TaggedIsSmi(replace_value), &checkreplacestring);

  Node* const replace_value_map = a->LoadMap(replace_value);
  a->Branch(
      a->Word32Equal(a->Word32And(a->LoadMapBitField(replace_value_map),
                                  a->Int32Constant(1 << Map::kIsCallable)),
                     a->Int32Constant(0)),
      &checkreplacestring, &runtime);

  // 3. Does ToString({replace_value}) contain '$'?
  a->Bind(&checkreplacestring);
  {
    Node* const replace_string =
        a->CallStub(tostring_callable, context, replace_value);

    Node* const dollar_char = a->IntPtrConstant('$');
    Node* const smi_minusone = a->SmiConstant(Smi::FromInt(-1));
    a->GotoUnless(a->SmiEqual(a->StringIndexOfChar(context, replace_string,
                                                   dollar_char, int_zero),
                              smi_minusone),
                  &runtime);

    a->Return(ReplaceFastPath(a, context, regexp, string, replace_string));
  }

  a->Bind(&runtime);
  {
    Node* const result = a->CallRuntime(Runtime::kRegExpReplace, context,
                                        receiver, string, replace_value);
    a->Return(result);
  }
}

// Simple string matching functionality for internal use which does not modify
// the last match info.
void Builtins::Generate_RegExpInternalMatch(CodeStubAssembler* a) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Isolate* const isolate = a->isolate();

  Node* const regexp = a->Parameter(1);
  Node* const string = a->Parameter(2);
  Node* const context = a->Parameter(5);

  Node* const null = a->NullConstant();
  Node* const smi_zero = a->SmiConstant(Smi::FromInt(0));

  Node* const native_context = a->LoadNativeContext(context);
  Node* const internal_match_info = a->LoadContextElement(
      native_context, Context::REGEXP_INTERNAL_MATCH_INFO_INDEX);

  Callable exec_callable = CodeFactory::RegExpExec(isolate);
  Node* const match_indices = a->CallStub(
      exec_callable, context, regexp, string, smi_zero, internal_match_info);

  Label if_matched(a), if_didnotmatch(a);
  a->Branch(a->WordEqual(match_indices, null), &if_didnotmatch, &if_matched);

  a->Bind(&if_didnotmatch);
  a->Return(null);

  a->Bind(&if_matched);
  {
    Node* result = ConstructNewResultFromMatchInfo(isolate, a, context,
                                                   match_indices, string);
    a->Return(result);
  }
}

}  // namespace internal
}  // namespace v8
