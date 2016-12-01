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

typedef compiler::Node Node;
typedef CodeStubAssembler::Label CLabel;
typedef CodeStubAssembler::Variable CVariable;
typedef CodeStubAssembler::ParameterMode ParameterMode;
typedef compiler::CodeAssemblerState CodeAssemblerState;

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
MUST_USE_RESULT MaybeHandle<JSRegExp> RegExpInitialize(Isolate* isolate,
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

Node* FastLoadLastIndex(CodeStubAssembler* a, Node* regexp) {
  // Load the in-object field.
  static const int field_offset =
      JSRegExp::kSize + JSRegExp::kLastIndexFieldIndex * kPointerSize;
  return a->LoadObjectField(regexp, field_offset);
}

Node* SlowLoadLastIndex(CodeStubAssembler* a, Node* context, Node* regexp) {
  // Load through the GetProperty stub.
  Node* const name =
      a->HeapConstant(a->isolate()->factory()->lastIndex_string());
  Callable getproperty_callable = CodeFactory::GetProperty(a->isolate());
  return a->CallStub(getproperty_callable, context, regexp, name);
}

Node* LoadLastIndex(CodeStubAssembler* a, Node* context, Node* regexp,
                    bool is_fastpath) {
  return is_fastpath ? FastLoadLastIndex(a, regexp)
                     : SlowLoadLastIndex(a, context, regexp);
}

// The fast-path of StoreLastIndex when regexp is guaranteed to be an unmodified
// JSRegExp instance.
void FastStoreLastIndex(CodeStubAssembler* a, Node* regexp, Node* value) {
  // Store the in-object field.
  static const int field_offset =
      JSRegExp::kSize + JSRegExp::kLastIndexFieldIndex * kPointerSize;
  a->StoreObjectField(regexp, field_offset, value);
}

void SlowStoreLastIndex(CodeStubAssembler* a, Node* context, Node* regexp,
                        Node* value) {
  // Store through runtime.
  // TODO(ishell): Use SetPropertyStub here once available.
  Node* const name =
      a->HeapConstant(a->isolate()->factory()->lastIndex_string());
  Node* const language_mode = a->SmiConstant(Smi::FromInt(STRICT));
  a->CallRuntime(Runtime::kSetProperty, context, regexp, name, value,
                 language_mode);
}

void StoreLastIndex(CodeStubAssembler* a, Node* context, Node* regexp,
                    Node* value, bool is_fastpath) {
  if (is_fastpath) {
    FastStoreLastIndex(a, regexp, value);
  } else {
    SlowStoreLastIndex(a, context, regexp, value);
  }
}

Node* ConstructNewResultFromMatchInfo(Isolate* isolate, CodeStubAssembler* a,
                                      Node* context, Node* match_info,
                                      Node* string) {
  CLabel out(a);

  Node* const num_indices = a->SmiUntag(a->LoadFixedArrayElement(
      match_info, RegExpMatchInfo::kNumberOfCapturesIndex));
  Node* const num_results = a->SmiTag(a->WordShr(num_indices, 1));
  Node* const start =
      a->LoadFixedArrayElement(match_info, RegExpMatchInfo::kFirstCaptureIndex);
  Node* const end = a->LoadFixedArrayElement(
      match_info, RegExpMatchInfo::kFirstCaptureIndex + 1);

  // Calculate the substring of the first match before creating the result array
  // to avoid an unnecessary write barrier storing the first result.
  Node* const first = a->SubString(context, string, start, end);

  Node* const result =
      a->AllocateRegExpResult(context, num_results, start, string);
  Node* const result_elements = a->LoadElements(result);

  a->StoreFixedArrayElement(result_elements, 0, first, SKIP_WRITE_BARRIER);

  a->GotoIf(a->SmiEqual(num_results, a->SmiConstant(Smi::FromInt(1))), &out);

  // Store all remaining captures.
  Node* const limit = a->IntPtrAdd(
      a->IntPtrConstant(RegExpMatchInfo::kFirstCaptureIndex), num_indices);

  CVariable var_from_cursor(a, MachineType::PointerRepresentation());
  CVariable var_to_cursor(a, MachineType::PointerRepresentation());

  var_from_cursor.Bind(
      a->IntPtrConstant(RegExpMatchInfo::kFirstCaptureIndex + 2));
  var_to_cursor.Bind(a->IntPtrConstant(1));

  CVariable* vars[] = {&var_from_cursor, &var_to_cursor};
  CLabel loop(a, 2, vars);

  a->Goto(&loop);
  a->Bind(&loop);
  {
    Node* const from_cursor = var_from_cursor.value();
    Node* const to_cursor = var_to_cursor.value();
    Node* const start = a->LoadFixedArrayElement(match_info, from_cursor);

    CLabel next_iter(a);
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

// ES#sec-regexp.prototype.exec
// RegExp.prototype.exec ( string )
// Implements the core of RegExp.prototype.exec but without actually
// constructing the JSRegExpResult. Returns either null (if the RegExp did not
// match) or a fixed array containing match indices as returned by
// RegExpExecStub.
Node* RegExpPrototypeExecBodyWithoutResult(
    CodeStubAssembler* a, Node* const context, Node* const regexp,
    Node* const string, CLabel* if_didnotmatch, const bool is_fastpath) {
  Isolate* const isolate = a->isolate();

  Node* const null = a->NullConstant();
  Node* const int_zero = a->IntPtrConstant(0);
  Node* const smi_zero = a->SmiConstant(Smi::kZero);

  if (!is_fastpath) {
    a->ThrowIfNotInstanceType(context, regexp, JS_REGEXP_TYPE,
                              "RegExp.prototype.exec");
  }

  CSA_ASSERT(a, a->IsStringInstanceType(a->LoadInstanceType(string)));
  CSA_ASSERT(a, a->HasInstanceType(regexp, JS_REGEXP_TYPE));

  CVariable var_result(a, MachineRepresentation::kTagged);
  CLabel out(a);

  Node* const native_context = a->LoadNativeContext(context);
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
  CLabel run_exec(a);
  CVariable var_lastindex(a, MachineRepresentation::kTagged);
  {
    CLabel if_doupdate(a), if_dontupdate(a);
    a->Branch(should_update_last_index, &if_doupdate, &if_dontupdate);

    a->Bind(&if_doupdate);
    {
      Node* const regexp_lastindex =
          LoadLastIndex(a, context, regexp, is_fastpath);
      var_lastindex.Bind(regexp_lastindex);

      // Omit ToLength if lastindex is a non-negative smi.
      {
        CLabel call_tolength(a, CLabel::kDeferred), next(a);
        a->Branch(a->WordIsPositiveSmi(regexp_lastindex), &next,
                  &call_tolength);

        a->Bind(&call_tolength);
        {
          Callable tolength_callable = CodeFactory::ToLength(isolate);
          var_lastindex.Bind(
              a->CallStub(tolength_callable, context, regexp_lastindex));
          a->Goto(&next);
        }

        a->Bind(&next);
      }

      Node* const lastindex = var_lastindex.value();

      CLabel if_isoob(a, CLabel::kDeferred);
      a->GotoUnless(a->TaggedIsSmi(lastindex), &if_isoob);
      a->GotoUnless(a->SmiLessThanOrEqual(lastindex, string_length), &if_isoob);
      a->Goto(&run_exec);

      a->Bind(&if_isoob);
      {
        StoreLastIndex(a, context, regexp, smi_zero, is_fastpath);
        var_result.Bind(null);
        a->Goto(if_didnotmatch);
      }
    }

    a->Bind(&if_dontupdate);
    {
      var_lastindex.Bind(smi_zero);
      a->Goto(&run_exec);
    }
  }

  Node* match_indices;
  CLabel successful_match(a);
  a->Bind(&run_exec);
  {
    // Get last match info from the context.
    Node* const last_match_info = a->LoadContextElement(
        native_context, Context::REGEXP_LAST_MATCH_INFO_INDEX);

    // Call the exec stub.
    Callable exec_callable = CodeFactory::RegExpExec(isolate);
    match_indices = a->CallStub(exec_callable, context, regexp, string,
                                var_lastindex.value(), last_match_info);
    var_result.Bind(match_indices);

    // {match_indices} is either null or the RegExpMatchInfo array.
    // Return early if exec failed, possibly updating last index.
    a->GotoUnless(a->WordEqual(match_indices, null), &successful_match);

    a->GotoUnless(should_update_last_index, if_didnotmatch);

    StoreLastIndex(a, context, regexp, smi_zero, is_fastpath);
    a->Goto(if_didnotmatch);
  }

  a->Bind(&successful_match);
  {
    a->GotoUnless(should_update_last_index, &out);

    // Update the new last index from {match_indices}.
    Node* const new_lastindex = a->LoadFixedArrayElement(
        match_indices, RegExpMatchInfo::kFirstCaptureIndex + 1);

    StoreLastIndex(a, context, regexp, new_lastindex, is_fastpath);
    a->Goto(&out);
  }

  a->Bind(&out);
  return var_result.value();
}

// ES#sec-regexp.prototype.exec
// RegExp.prototype.exec ( string )
Node* RegExpPrototypeExecBody(CodeStubAssembler* a, Node* const context,
                              Node* const regexp, Node* const string,
                              const bool is_fastpath) {
  Isolate* const isolate = a->isolate();
  Node* const null = a->NullConstant();

  CVariable var_result(a, MachineRepresentation::kTagged);

  CLabel if_didnotmatch(a), out(a);
  Node* const indices_or_null = RegExpPrototypeExecBodyWithoutResult(
      a, context, regexp, string, &if_didnotmatch, is_fastpath);

  // Successful match.
  {
    Node* const match_indices = indices_or_null;
    Node* const result = ConstructNewResultFromMatchInfo(isolate, a, context,
                                                         match_indices, string);
    var_result.Bind(result);
    a->Goto(&out);
  }

  a->Bind(&if_didnotmatch);
  {
    var_result.Bind(null);
    a->Goto(&out);
  }

  a->Bind(&out);
  return var_result.value();
}

Node* ThrowIfNotJSReceiver(CodeStubAssembler* a, Isolate* isolate,
                           Node* context, Node* value,
                           MessageTemplate::Template msg_template,
                           char const* method_name) {
  CLabel out(a), throw_exception(a, CLabel::kDeferred);
  CVariable var_value_map(a, MachineRepresentation::kTagged);

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

Node* IsInitialRegExpMap(CodeStubAssembler* a, Node* context, Node* map) {
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
void BranchIfFastPath(CodeStubAssembler* a, Node* context, Node* map,
                      CLabel* if_isunmodified, CLabel* if_ismodified) {
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

void BranchIfFastRegExpResult(CodeStubAssembler* a, Node* context, Node* map,
                              CLabel* if_isunmodified, CLabel* if_ismodified) {
  Node* const native_context = a->LoadNativeContext(context);
  Node* const initial_regexp_result_map =
      a->LoadContextElement(native_context, Context::REGEXP_RESULT_MAP_INDEX);

  a->Branch(a->WordEqual(map, initial_regexp_result_map), if_isunmodified,
            if_ismodified);
}

}  // namespace

// ES#sec-regexp.prototype.exec
// RegExp.prototype.exec ( string )
void Builtins::Generate_RegExpPrototypeExec(CodeAssemblerState* state) {
  CodeStubAssembler a(state);

  Node* const maybe_receiver = a.Parameter(0);
  Node* const maybe_string = a.Parameter(1);
  Node* const context = a.Parameter(4);

  // Ensure {maybe_receiver} is a JSRegExp.
  Node* const regexp_map = a.ThrowIfNotInstanceType(
      context, maybe_receiver, JS_REGEXP_TYPE, "RegExp.prototype.exec");
  Node* const receiver = maybe_receiver;

  // Convert {maybe_string} to a String.
  Node* const string = a.ToString(context, maybe_string);

  CLabel if_isfastpath(&a), if_isslowpath(&a);
  a.Branch(IsInitialRegExpMap(&a, context, regexp_map), &if_isfastpath,
           &if_isslowpath);

  a.Bind(&if_isfastpath);
  {
    Node* const result =
        RegExpPrototypeExecBody(&a, context, receiver, string, true);
    a.Return(result);
  }

  a.Bind(&if_isslowpath);
  {
    Node* const result =
        RegExpPrototypeExecBody(&a, context, receiver, string, false);
    a.Return(result);
  }
}

void Builtins::Generate_RegExpPrototypeFlagsGetter(CodeAssemblerState* state) {
  CodeStubAssembler a(state);

  Node* const receiver = a.Parameter(0);
  Node* const context = a.Parameter(3);

  Isolate* isolate = a.isolate();
  Node* const int_zero = a.IntPtrConstant(0);
  Node* const int_one = a.IntPtrConstant(1);

  Node* const map = ThrowIfNotJSReceiver(&a, isolate, context, receiver,
                                         MessageTemplate::kRegExpNonObject,
                                         "RegExp.prototype.flags");

  CVariable var_length(&a, MachineType::PointerRepresentation());
  CVariable var_flags(&a, MachineType::PointerRepresentation());

  // First, count the number of characters we will need and check which flags
  // are set.

  var_length.Bind(int_zero);

  CLabel if_isunmodifiedjsregexp(&a),
      if_isnotunmodifiedjsregexp(&a, CLabel::kDeferred);
  a.Branch(IsInitialRegExpMap(&a, context, map), &if_isunmodifiedjsregexp,
           &if_isnotunmodifiedjsregexp);

  CLabel construct_string(&a);
  a.Bind(&if_isunmodifiedjsregexp);
  {
    // Refer to JSRegExp's flag property on the fast-path.
    Node* const flags_smi = a.LoadObjectField(receiver, JSRegExp::kFlagsOffset);
    Node* const flags_intptr = a.SmiUntag(flags_smi);
    var_flags.Bind(flags_intptr);

    CLabel label_global(&a), label_ignorecase(&a), label_multiline(&a),
        label_unicode(&a), label_sticky(&a);

#define CASE_FOR_FLAG(FLAG, LABEL, NEXT_LABEL)                     \
  do {                                                             \
    a.Bind(&LABEL);                                                \
    Node* const mask = a.IntPtrConstant(FLAG);                     \
    a.GotoIf(a.WordEqual(a.WordAnd(flags_intptr, mask), int_zero), \
             &NEXT_LABEL);                                         \
    var_length.Bind(a.IntPtrAdd(var_length.value(), int_one));     \
    a.Goto(&NEXT_LABEL);                                           \
  } while (false)

    a.Goto(&label_global);
    CASE_FOR_FLAG(JSRegExp::kGlobal, label_global, label_ignorecase);
    CASE_FOR_FLAG(JSRegExp::kIgnoreCase, label_ignorecase, label_multiline);
    CASE_FOR_FLAG(JSRegExp::kMultiline, label_multiline, label_unicode);
    CASE_FOR_FLAG(JSRegExp::kUnicode, label_unicode, label_sticky);
    CASE_FOR_FLAG(JSRegExp::kSticky, label_sticky, construct_string);
#undef CASE_FOR_FLAG
  }

  a.Bind(&if_isnotunmodifiedjsregexp);
  {
    // Fall back to GetProperty stub on the slow-path.
    var_flags.Bind(int_zero);

    Callable getproperty_callable = CodeFactory::GetProperty(a.isolate());
    CLabel label_global(&a), label_ignorecase(&a), label_multiline(&a),
        label_unicode(&a), label_sticky(&a);

#define CASE_FOR_FLAG(NAME, FLAG, LABEL, NEXT_LABEL)                         \
  do {                                                                       \
    a.Bind(&LABEL);                                                          \
    Node* const name =                                                       \
        a.HeapConstant(isolate->factory()->NewStringFromAsciiChecked(NAME)); \
    Node* const flag =                                                       \
        a.CallStub(getproperty_callable, context, receiver, name);           \
    CLabel if_isflagset(&a);                                                 \
    a.BranchIfToBooleanIsTrue(flag, &if_isflagset, &NEXT_LABEL);             \
    a.Bind(&if_isflagset);                                                   \
    var_length.Bind(a.IntPtrAdd(var_length.value(), int_one));               \
    var_flags.Bind(a.WordOr(var_flags.value(), a.IntPtrConstant(FLAG)));     \
    a.Goto(&NEXT_LABEL);                                                     \
  } while (false)

    a.Goto(&label_global);
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

  a.Bind(&construct_string);
  {
    Node* const result =
        a.AllocateSeqOneByteString(context, var_length.value());
    Node* const flags_intptr = var_flags.value();

    CVariable var_offset(&a, MachineType::PointerRepresentation());
    var_offset.Bind(
        a.IntPtrConstant(SeqOneByteString::kHeaderSize - kHeapObjectTag));

    CLabel label_global(&a), label_ignorecase(&a), label_multiline(&a),
        label_unicode(&a), label_sticky(&a), out(&a);

#define CASE_FOR_FLAG(FLAG, CHAR, LABEL, NEXT_LABEL)               \
  do {                                                             \
    a.Bind(&LABEL);                                                \
    Node* const mask = a.IntPtrConstant(FLAG);                     \
    a.GotoIf(a.WordEqual(a.WordAnd(flags_intptr, mask), int_zero), \
             &NEXT_LABEL);                                         \
    Node* const value = a.IntPtrConstant(CHAR);                    \
    a.StoreNoWriteBarrier(MachineRepresentation::kWord8, result,   \
                          var_offset.value(), value);              \
    var_offset.Bind(a.IntPtrAdd(var_offset.value(), int_one));     \
    a.Goto(&NEXT_LABEL);                                           \
  } while (false)

    a.Goto(&label_global);
    CASE_FOR_FLAG(JSRegExp::kGlobal, 'g', label_global, label_ignorecase);
    CASE_FOR_FLAG(JSRegExp::kIgnoreCase, 'i', label_ignorecase,
                  label_multiline);
    CASE_FOR_FLAG(JSRegExp::kMultiline, 'm', label_multiline, label_unicode);
    CASE_FOR_FLAG(JSRegExp::kUnicode, 'u', label_unicode, label_sticky);
    CASE_FOR_FLAG(JSRegExp::kSticky, 'y', label_sticky, out);
#undef CASE_FOR_FLAG

    a.Bind(&out);
    a.Return(result);
  }
}

// ES6 21.2.5.10.
void Builtins::Generate_RegExpPrototypeSourceGetter(CodeAssemblerState* state) {
  CodeStubAssembler a(state);
  Node* const receiver = a.Parameter(0);
  Node* const context = a.Parameter(3);

  // Check whether we have an unmodified regexp instance.
  CLabel if_isjsregexp(&a), if_isnotjsregexp(&a, CLabel::kDeferred);

  a.GotoIf(a.TaggedIsSmi(receiver), &if_isnotjsregexp);
  a.Branch(a.HasInstanceType(receiver, JS_REGEXP_TYPE), &if_isjsregexp,
           &if_isnotjsregexp);

  a.Bind(&if_isjsregexp);
  {
    Node* const source = a.LoadObjectField(receiver, JSRegExp::kSourceOffset);
    a.Return(source);
  }

  a.Bind(&if_isnotjsregexp);
  {
    Isolate* isolate = a.isolate();
    Node* const native_context = a.LoadNativeContext(context);
    Node* const regexp_fun =
        a.LoadContextElement(native_context, Context::REGEXP_FUNCTION_INDEX);
    Node* const initial_map =
        a.LoadObjectField(regexp_fun, JSFunction::kPrototypeOrInitialMapOffset);
    Node* const initial_prototype = a.LoadMapPrototype(initial_map);

    CLabel if_isprototype(&a), if_isnotprototype(&a);
    a.Branch(a.WordEqual(receiver, initial_prototype), &if_isprototype,
             &if_isnotprototype);

    a.Bind(&if_isprototype);
    {
      const int counter = v8::Isolate::kRegExpPrototypeSourceGetter;
      Node* const counter_smi = a.SmiConstant(counter);
      a.CallRuntime(Runtime::kIncrementUseCounter, context, counter_smi);

      Node* const result =
          a.HeapConstant(isolate->factory()->NewStringFromAsciiChecked("(?:)"));
      a.Return(result);
    }

    a.Bind(&if_isnotprototype);
    {
      Node* const message_id =
          a.SmiConstant(Smi::FromInt(MessageTemplate::kRegExpNonRegExp));
      Node* const method_name_str =
          a.HeapConstant(isolate->factory()->NewStringFromAsciiChecked(
              "RegExp.prototype.source"));
      a.TailCallRuntime(Runtime::kThrowTypeError, context, message_id,
                        method_name_str);
    }
  }
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
void Builtins::Generate_RegExpPrototypeSpeciesGetter(
    CodeAssemblerState* state) {
  CodeStubAssembler a(state);
  Node* const receiver = a.Parameter(0);
  a.Return(receiver);
}

namespace {

// Fast-path implementation for flag checks on an unmodified JSRegExp instance.
Node* FastFlagGetter(CodeStubAssembler* a, Node* const regexp,
                     JSRegExp::Flag flag) {
  Node* const smi_zero = a->SmiConstant(Smi::kZero);
  Node* const flags = a->LoadObjectField(regexp, JSRegExp::kFlagsOffset);
  Node* const mask = a->SmiConstant(Smi::FromInt(flag));
  Node* const is_flag_set = a->WordNotEqual(a->WordAnd(flags, mask), smi_zero);

  return is_flag_set;
}

// Load through the GetProperty stub.
compiler::Node* SlowFlagGetter(CodeStubAssembler* a,
                               compiler::Node* const context,
                               compiler::Node* const regexp,
                               JSRegExp::Flag flag) {
  Factory* factory = a->isolate()->factory();

  CLabel out(a);
  CVariable var_result(a, MachineType::PointerRepresentation());

  Node* name;

  switch (flag) {
    case JSRegExp::kGlobal:
      name = a->HeapConstant(factory->global_string());
      break;
    case JSRegExp::kIgnoreCase:
      name = a->HeapConstant(factory->ignoreCase_string());
      break;
    case JSRegExp::kMultiline:
      name = a->HeapConstant(factory->multiline_string());
      break;
    case JSRegExp::kSticky:
      name = a->HeapConstant(factory->sticky_string());
      break;
    case JSRegExp::kUnicode:
      name = a->HeapConstant(factory->unicode_string());
      break;
    default:
      UNREACHABLE();
  }

  Callable getproperty_callable = CodeFactory::GetProperty(a->isolate());
  Node* const value = a->CallStub(getproperty_callable, context, regexp, name);

  CLabel if_true(a), if_false(a);
  a->BranchIfToBooleanIsTrue(value, &if_true, &if_false);

  a->Bind(&if_true);
  {
    var_result.Bind(a->IntPtrConstant(1));
    a->Goto(&out);
  }

  a->Bind(&if_false);
  {
    var_result.Bind(a->IntPtrConstant(0));
    a->Goto(&out);
  }

  a->Bind(&out);
  return var_result.value();
}

compiler::Node* FlagGetter(CodeStubAssembler* a, compiler::Node* const context,
                           compiler::Node* const regexp, JSRegExp::Flag flag,
                           bool is_fastpath) {
  return is_fastpath ? FastFlagGetter(a, regexp, flag)
                     : SlowFlagGetter(a, context, regexp, flag);
}

void Generate_FlagGetter(CodeStubAssembler* a, JSRegExp::Flag flag,
                         v8::Isolate::UseCounterFeature counter,
                         const char* method_name) {
  Node* const receiver = a->Parameter(0);
  Node* const context = a->Parameter(3);

  Isolate* isolate = a->isolate();

  // Check whether we have an unmodified regexp instance.
  CLabel if_isunmodifiedjsregexp(a),
      if_isnotunmodifiedjsregexp(a, CLabel::kDeferred);

  a->GotoIf(a->TaggedIsSmi(receiver), &if_isnotunmodifiedjsregexp);

  Node* const receiver_map = a->LoadMap(receiver);
  Node* const instance_type = a->LoadMapInstanceType(receiver_map);

  a->Branch(a->Word32Equal(instance_type, a->Int32Constant(JS_REGEXP_TYPE)),
            &if_isunmodifiedjsregexp, &if_isnotunmodifiedjsregexp);

  a->Bind(&if_isunmodifiedjsregexp);
  {
    // Refer to JSRegExp's flag property on the fast-path.
    Node* const is_flag_set = FastFlagGetter(a, receiver, flag);
    a->Return(a->Select(is_flag_set, a->TrueConstant(), a->FalseConstant()));
  }

  a->Bind(&if_isnotunmodifiedjsregexp);
  {
    Node* const native_context = a->LoadNativeContext(context);
    Node* const regexp_fun =
        a->LoadContextElement(native_context, Context::REGEXP_FUNCTION_INDEX);
    Node* const initial_map = a->LoadObjectField(
        regexp_fun, JSFunction::kPrototypeOrInitialMapOffset);
    Node* const initial_prototype = a->LoadMapPrototype(initial_map);

    CLabel if_isprototype(a), if_isnotprototype(a);
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
void Builtins::Generate_RegExpPrototypeGlobalGetter(CodeAssemblerState* state) {
  CodeStubAssembler a(state);
  Generate_FlagGetter(&a, JSRegExp::kGlobal,
                      v8::Isolate::kRegExpPrototypeOldFlagGetter,
                      "RegExp.prototype.global");
}

// ES6 21.2.5.5.
void Builtins::Generate_RegExpPrototypeIgnoreCaseGetter(
    CodeAssemblerState* state) {
  CodeStubAssembler a(state);
  Generate_FlagGetter(&a, JSRegExp::kIgnoreCase,
                      v8::Isolate::kRegExpPrototypeOldFlagGetter,
                      "RegExp.prototype.ignoreCase");
}

// ES6 21.2.5.7.
void Builtins::Generate_RegExpPrototypeMultilineGetter(
    CodeAssemblerState* state) {
  CodeStubAssembler a(state);
  Generate_FlagGetter(&a, JSRegExp::kMultiline,
                      v8::Isolate::kRegExpPrototypeOldFlagGetter,
                      "RegExp.prototype.multiline");
}

// ES6 21.2.5.12.
void Builtins::Generate_RegExpPrototypeStickyGetter(CodeAssemblerState* state) {
  CodeStubAssembler a(state);
  Generate_FlagGetter(&a, JSRegExp::kSticky,
                      v8::Isolate::kRegExpPrototypeStickyGetter,
                      "RegExp.prototype.sticky");
}

// ES6 21.2.5.15.
void Builtins::Generate_RegExpPrototypeUnicodeGetter(
    CodeAssemblerState* state) {
  CodeStubAssembler a(state);
  Generate_FlagGetter(&a, JSRegExp::kUnicode,
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

namespace {

// ES#sec-regexpexec Runtime Semantics: RegExpExec ( R, S )
Node* RegExpExec(CodeStubAssembler* a, Node* context, Node* recv,
                 Node* string) {
  Isolate* isolate = a->isolate();

  Node* const null = a->NullConstant();

  CVariable var_result(a, MachineRepresentation::kTagged);
  CLabel out(a), if_isfastpath(a), if_isslowpath(a);

  Node* const map = a->LoadMap(recv);
  BranchIfFastPath(a, context, map, &if_isfastpath, &if_isslowpath);

  a->Bind(&if_isfastpath);
  {
    Node* const result =
        RegExpPrototypeExecBody(a, context, recv, string, true);
    var_result.Bind(result);
    a->Goto(&out);
  }

  a->Bind(&if_isslowpath);
  {
    // Take the slow path of fetching the exec property, calling it, and
    // verifying its return value.

    // Get the exec property.
    Node* const name = a->HeapConstant(isolate->factory()->exec_string());
    Callable getproperty_callable = CodeFactory::GetProperty(a->isolate());
    Node* const exec = a->CallStub(getproperty_callable, context, recv, name);

    // Is {exec} callable?
    CLabel if_iscallable(a), if_isnotcallable(a);

    a->GotoIf(a->TaggedIsSmi(exec), &if_isnotcallable);

    Node* const exec_map = a->LoadMap(exec);
    a->Branch(a->IsCallableMap(exec_map), &if_iscallable, &if_isnotcallable);

    a->Bind(&if_iscallable);
    {
      Callable call_callable = CodeFactory::Call(isolate);
      Node* const result =
          a->CallJS(call_callable, context, exec, recv, string);

      var_result.Bind(result);
      a->GotoIf(a->WordEqual(result, null), &out);

      ThrowIfNotJSReceiver(a, isolate, context, result,
                           MessageTemplate::kInvalidRegExpExecResult, "unused");

      a->Goto(&out);
    }

    a->Bind(&if_isnotcallable);
    {
      a->ThrowIfNotInstanceType(context, recv, JS_REGEXP_TYPE,
                                "RegExp.prototype.exec");

      Node* const result =
          RegExpPrototypeExecBody(a, context, recv, string, false);
      var_result.Bind(result);
      a->Goto(&out);
    }
  }

  a->Bind(&out);
  return var_result.value();
}

}  // namespace

// ES#sec-regexp.prototype.test
// RegExp.prototype.test ( S )
void Builtins::Generate_RegExpPrototypeTest(CodeAssemblerState* state) {
  CodeStubAssembler a(state);

  Isolate* const isolate = a.isolate();

  Node* const maybe_receiver = a.Parameter(0);
  Node* const maybe_string = a.Parameter(1);
  Node* const context = a.Parameter(4);

  // Ensure {maybe_receiver} is a JSReceiver.
  Node* const map = ThrowIfNotJSReceiver(
      &a, isolate, context, maybe_receiver,
      MessageTemplate::kIncompatibleMethodReceiver, "RegExp.prototype.test");
  Node* const receiver = maybe_receiver;

  // Convert {maybe_string} to a String.
  Node* const string = a.ToString(context, maybe_string);

  CLabel fast_path(&a), slow_path(&a);
  BranchIfFastPath(&a, context, map, &fast_path, &slow_path);

  a.Bind(&fast_path);
  {
    CLabel if_didnotmatch(&a);
    RegExpPrototypeExecBodyWithoutResult(&a, context, receiver, string,
                                         &if_didnotmatch, true);
    a.Return(a.TrueConstant());

    a.Bind(&if_didnotmatch);
    a.Return(a.FalseConstant());
  }

  a.Bind(&slow_path);
  {
    // Call exec.
    Node* const match_indices = RegExpExec(&a, context, receiver, string);

    // Return true iff exec matched successfully.
    Node* const result = a.Select(a.WordEqual(match_indices, a.NullConstant()),
                                  a.FalseConstant(), a.TrueConstant());
    a.Return(result);
  }
}

namespace {

Node* AdvanceStringIndex(CodeStubAssembler* a, Node* const string,
                         Node* const index, Node* const is_unicode) {
  CVariable var_result(a, MachineRepresentation::kTagged);

  // Default to last_index + 1.
  Node* const index_plus_one = a->SmiAdd(index, a->SmiConstant(1));
  var_result.Bind(index_plus_one);

  CLabel if_isunicode(a), out(a);
  a->Branch(is_unicode, &if_isunicode, &out);

  a->Bind(&if_isunicode);
  {
    Node* const string_length = a->LoadStringLength(string);
    a->GotoUnless(a->SmiLessThan(index_plus_one, string_length), &out);

    Node* const lead = a->StringCharCodeAt(string, index);
    a->GotoUnless(a->Word32Equal(a->Word32And(lead, a->Int32Constant(0xFC00)),
                                 a->Int32Constant(0xD800)),
                  &out);

    Node* const trail = a->StringCharCodeAt(string, index_plus_one);
    a->GotoUnless(a->Word32Equal(a->Word32And(trail, a->Int32Constant(0xFC00)),
                                 a->Int32Constant(0xDC00)),
                  &out);

    // At a surrogate pair, return index + 2.
    Node* const index_plus_two = a->SmiAdd(index, a->SmiConstant(2));
    var_result.Bind(index_plus_two);

    a->Goto(&out);
  }

  a->Bind(&out);
  return var_result.value();
}

// Utility class implementing a growable fixed array through CSA.
class GrowableFixedArray {
 public:
  explicit GrowableFixedArray(CodeStubAssembler* a)
      : assembler_(a),
        var_array_(a, MachineRepresentation::kTagged),
        var_length_(a, MachineType::PointerRepresentation()),
        var_capacity_(a, MachineType::PointerRepresentation()) {
    Initialize();
  }

  Node* length() const { return var_length_.value(); }

  CVariable* var_array() { return &var_array_; }
  CVariable* var_length() { return &var_length_; }
  CVariable* var_capacity() { return &var_capacity_; }

  void Push(Node* const value) {
    CodeStubAssembler* a = assembler_;

    const WriteBarrierMode barrier_mode = UPDATE_WRITE_BARRIER;
    const ParameterMode mode = CodeStubAssembler::INTPTR_PARAMETERS;

    Node* const length = var_length_.value();
    Node* const capacity = var_capacity_.value();

    CLabel grow(a), store(a);
    a->Branch(a->IntPtrEqual(capacity, length), &grow, &store);

    a->Bind(&grow);
    {
      Node* const new_capacity = NewCapacity(a, capacity);
      Node* const new_array = GrowFixedArray(capacity, new_capacity, mode);

      var_capacity_.Bind(new_capacity);
      var_array_.Bind(new_array);
      a->Goto(&store);
    }

    a->Bind(&store);
    {
      Node* const array = var_array_.value();
      a->StoreFixedArrayElement(array, length, value, barrier_mode, 0, mode);

      Node* const new_length = a->IntPtrAdd(length, a->IntPtrConstant(1));
      var_length_.Bind(new_length);
    }
  }

  Node* ToJSArray(Node* const context) {
    CodeStubAssembler* a = assembler_;

    const ElementsKind kind = FAST_ELEMENTS;

    Node* const native_context = a->LoadNativeContext(context);
    Node* const array_map = a->LoadJSArrayElementsMap(kind, native_context);

    Node* const result_length = a->SmiTag(length());
    Node* const result = a->AllocateUninitializedJSArrayWithoutElements(
        kind, array_map, result_length, nullptr);

    // Note: We do not currently shrink the fixed array.

    a->StoreObjectField(result, JSObject::kElementsOffset, var_array_.value());

    return result;
  }

 private:
  void Initialize() {
    CodeStubAssembler* a = assembler_;

    const ElementsKind kind = FAST_ELEMENTS;
    const ParameterMode mode = CodeStubAssembler::INTPTR_PARAMETERS;

    static const int kInitialArraySize = 8;
    Node* const capacity = a->IntPtrConstant(kInitialArraySize);
    Node* const array = a->AllocateFixedArray(kind, capacity, mode);

    a->FillFixedArrayWithValue(kind, array, a->IntPtrConstant(0), capacity,
                               Heap::kTheHoleValueRootIndex, mode);

    var_array_.Bind(array);
    var_capacity_.Bind(capacity);
    var_length_.Bind(a->IntPtrConstant(0));
  }

  Node* NewCapacity(CodeStubAssembler* a, Node* const current_capacity) {
    CSA_ASSERT(a, a->IntPtrGreaterThan(current_capacity, a->IntPtrConstant(0)));

    // Growth rate is analog to JSObject::NewElementsCapacity:
    // new_capacity = (current_capacity + (current_capacity >> 1)) + 16.

    Node* const new_capacity = a->IntPtrAdd(
        a->IntPtrAdd(current_capacity, a->WordShr(current_capacity, 1)),
        a->IntPtrConstant(16));

    return new_capacity;
  }

  Node* GrowFixedArray(Node* const current_capacity, Node* const new_capacity,
                       ParameterMode mode) {
    DCHECK(mode == CodeStubAssembler::INTPTR_PARAMETERS);

    CodeStubAssembler* a = assembler_;

    CSA_ASSERT(a, a->IntPtrGreaterThan(current_capacity, a->IntPtrConstant(0)));
    CSA_ASSERT(a, a->IntPtrGreaterThan(new_capacity, current_capacity));

    const ElementsKind kind = FAST_ELEMENTS;
    const WriteBarrierMode barrier_mode = UPDATE_WRITE_BARRIER;

    Node* const from_array = var_array_.value();
    Node* const to_array = a->AllocateFixedArray(kind, new_capacity, mode);
    a->CopyFixedArrayElements(kind, from_array, kind, to_array,
                              current_capacity, new_capacity, barrier_mode,
                              mode);

    return to_array;
  }

 private:
  CodeStubAssembler* const assembler_;
  CVariable var_array_;
  CVariable var_length_;
  CVariable var_capacity_;
};

void RegExpPrototypeMatchBody(CodeStubAssembler* a, Node* const receiver,
                              Node* const string, Node* const context,
                              const bool is_fastpath) {
  Isolate* const isolate = a->isolate();

  Node* const null = a->NullConstant();
  Node* const int_zero = a->IntPtrConstant(0);
  Node* const smi_zero = a->SmiConstant(Smi::kZero);

  Node* const regexp = receiver;
  Node* const is_global =
      FlagGetter(a, context, regexp, JSRegExp::kGlobal, is_fastpath);

  CLabel if_isglobal(a), if_isnotglobal(a);
  a->Branch(is_global, &if_isglobal, &if_isnotglobal);

  a->Bind(&if_isnotglobal);
  {
    Node* const result =
        is_fastpath ? RegExpPrototypeExecBody(a, context, regexp, string, true)
                    : RegExpExec(a, context, regexp, string);
    a->Return(result);
  }

  a->Bind(&if_isglobal);
  {
    Node* const is_unicode =
        FlagGetter(a, context, regexp, JSRegExp::kUnicode, is_fastpath);

    StoreLastIndex(a, context, regexp, smi_zero, is_fastpath);

    // Allocate an array to store the resulting match strings.

    GrowableFixedArray array(a);

    // Loop preparations. Within the loop, collect results from RegExpExec
    // and store match strings in the array.

    CVariable* vars[] = {array.var_array(), array.var_length(),
                         array.var_capacity()};
    CLabel loop(a, 3, vars), out(a);
    a->Goto(&loop);

    a->Bind(&loop);
    {
      CVariable var_match(a, MachineRepresentation::kTagged);

      CLabel if_didmatch(a), if_didnotmatch(a);
      if (is_fastpath) {
        // On the fast path, grab the matching string from the raw match index
        // array.
        Node* const match_indices = RegExpPrototypeExecBodyWithoutResult(
            a, context, regexp, string, &if_didnotmatch, true);

        Node* const match_from = a->LoadFixedArrayElement(
            match_indices, RegExpMatchInfo::kFirstCaptureIndex);
        Node* const match_to = a->LoadFixedArrayElement(
            match_indices, RegExpMatchInfo::kFirstCaptureIndex + 1);

        Node* match = a->SubString(context, string, match_from, match_to);
        var_match.Bind(match);

        a->Goto(&if_didmatch);
      } else {
        DCHECK(!is_fastpath);
        Node* const result = RegExpExec(a, context, regexp, string);

        CLabel load_match(a);
        a->Branch(a->WordEqual(result, null), &if_didnotmatch, &load_match);

        a->Bind(&load_match);
        {
          CLabel fast_result(a), slow_result(a);
          BranchIfFastRegExpResult(a, context, a->LoadMap(result), &fast_result,
                                   &slow_result);

          a->Bind(&fast_result);
          {
            Node* const result_fixed_array = a->LoadElements(result);
            Node* const match = a->LoadFixedArrayElement(result_fixed_array, 0);

            // The match is guaranteed to be a string on the fast path.
            CSA_ASSERT(a, a->IsStringInstanceType(a->LoadInstanceType(match)));

            var_match.Bind(match);
            a->Goto(&if_didmatch);
          }

          a->Bind(&slow_result);
          {
            // TODO(ishell): Use GetElement stub once it's available.
            Node* const name = smi_zero;
            Callable getproperty_callable = CodeFactory::GetProperty(isolate);
            Node* const match =
                a->CallStub(getproperty_callable, context, result, name);

            var_match.Bind(a->ToString(context, match));
            a->Goto(&if_didmatch);
          }
        }
      }

      a->Bind(&if_didnotmatch);
      {
        // Return null if there were no matches, otherwise just exit the loop.
        a->GotoUnless(a->IntPtrEqual(array.length(), int_zero), &out);
        a->Return(null);
      }

      a->Bind(&if_didmatch);
      {
        Node* match = var_match.value();

        // Store the match, growing the fixed array if needed.

        array.Push(match);

        // Advance last index if the match is the empty string.

        Node* const match_length = a->LoadStringLength(match);
        a->GotoUnless(a->SmiEqual(match_length, smi_zero), &loop);

        Node* last_index = LoadLastIndex(a, context, regexp, is_fastpath);

        Callable tolength_callable = CodeFactory::ToLength(isolate);
        last_index = a->CallStub(tolength_callable, context, last_index);

        Node* const new_last_index =
            AdvanceStringIndex(a, string, last_index, is_unicode);

        StoreLastIndex(a, context, regexp, new_last_index, is_fastpath);

        a->Goto(&loop);
      }
    }

    a->Bind(&out);
    {
      // Wrap the match in a JSArray.

      Node* const result = array.ToJSArray(context);
      a->Return(result);
    }
  }
}

}  // namespace

// ES#sec-regexp.prototype-@@match
// RegExp.prototype [ @@match ] ( string )
void Builtins::Generate_RegExpPrototypeMatch(CodeAssemblerState* state) {
  CodeStubAssembler a(state);

  Node* const maybe_receiver = a.Parameter(0);
  Node* const maybe_string = a.Parameter(1);
  Node* const context = a.Parameter(4);

  // Ensure {maybe_receiver} is a JSReceiver.
  Node* const map = ThrowIfNotJSReceiver(
      &a, a.isolate(), context, maybe_receiver,
      MessageTemplate::kIncompatibleMethodReceiver, "RegExp.prototype.@@match");
  Node* const receiver = maybe_receiver;

  // Convert {maybe_string} to a String.
  Node* const string = a.ToString(context, maybe_string);

  CLabel fast_path(&a), slow_path(&a);
  BranchIfFastPath(&a, context, map, &fast_path, &slow_path);

  a.Bind(&fast_path);
  RegExpPrototypeMatchBody(&a, receiver, string, context, true);

  a.Bind(&slow_path);
  RegExpPrototypeMatchBody(&a, receiver, string, context, false);
}

namespace {

void RegExpPrototypeSearchBodyFast(CodeStubAssembler* a, Node* const receiver,
                                   Node* const string, Node* const context) {
  // Grab the initial value of last index.
  Node* const previous_last_index = FastLoadLastIndex(a, receiver);

  // Ensure last index is 0.
  FastStoreLastIndex(a, receiver, a->SmiConstant(Smi::kZero));

  // Call exec.
  CLabel if_didnotmatch(a);
  Node* const match_indices = RegExpPrototypeExecBodyWithoutResult(
      a, context, receiver, string, &if_didnotmatch, true);

  // Successful match.
  {
    // Reset last index.
    FastStoreLastIndex(a, receiver, previous_last_index);

    // Return the index of the match.
    Node* const index = a->LoadFixedArrayElement(
        match_indices, RegExpMatchInfo::kFirstCaptureIndex);
    a->Return(index);
  }

  a->Bind(&if_didnotmatch);
  {
    // Reset last index and return -1.
    FastStoreLastIndex(a, receiver, previous_last_index);
    a->Return(a->SmiConstant(-1));
  }
}

void RegExpPrototypeSearchBodySlow(CodeStubAssembler* a, Node* const receiver,
                                   Node* const string, Node* const context) {
  Isolate* const isolate = a->isolate();

  Node* const smi_zero = a->SmiConstant(Smi::kZero);

  // Grab the initial value of last index.
  Node* const previous_last_index = SlowLoadLastIndex(a, context, receiver);

  // Ensure last index is 0.
  {
    CLabel next(a);
    a->GotoIf(a->SameValue(previous_last_index, smi_zero, context), &next);

    SlowStoreLastIndex(a, context, receiver, smi_zero);
    a->Goto(&next);
    a->Bind(&next);
  }

  // Call exec.
  Node* const exec_result = RegExpExec(a, context, receiver, string);

  // Reset last index if necessary.
  {
    CLabel next(a);
    Node* const current_last_index = SlowLoadLastIndex(a, context, receiver);

    a->GotoIf(a->SameValue(current_last_index, previous_last_index, context),
              &next);

    SlowStoreLastIndex(a, context, receiver, previous_last_index);
    a->Goto(&next);

    a->Bind(&next);
  }

  // Return -1 if no match was found.
  {
    CLabel next(a);
    a->GotoUnless(a->WordEqual(exec_result, a->NullConstant()), &next);
    a->Return(a->SmiConstant(-1));
    a->Bind(&next);
  }

  // Return the index of the match.
  {
    CLabel fast_result(a), slow_result(a, CLabel::kDeferred);
    BranchIfFastRegExpResult(a, context, a->LoadMap(exec_result), &fast_result,
                             &slow_result);

    a->Bind(&fast_result);
    {
      Node* const index =
          a->LoadObjectField(exec_result, JSRegExpResult::kIndexOffset);
      a->Return(index);
    }

    a->Bind(&slow_result);
    {
      Node* const name = a->HeapConstant(isolate->factory()->index_string());
      Callable getproperty_callable = CodeFactory::GetProperty(a->isolate());
      Node* const index =
          a->CallStub(getproperty_callable, context, exec_result, name);
      a->Return(index);
    }
  }
}

}  // namespace

// ES#sec-regexp.prototype-@@search
// RegExp.prototype [ @@search ] ( string )
void Builtins::Generate_RegExpPrototypeSearch(CodeAssemblerState* state) {
  CodeStubAssembler a(state);

  Isolate* const isolate = a.isolate();

  Node* const maybe_receiver = a.Parameter(0);
  Node* const maybe_string = a.Parameter(1);
  Node* const context = a.Parameter(4);

  // Ensure {maybe_receiver} is a JSReceiver.
  Node* const map =
      ThrowIfNotJSReceiver(&a, isolate, context, maybe_receiver,
                           MessageTemplate::kIncompatibleMethodReceiver,
                           "RegExp.prototype.@@search");
  Node* const receiver = maybe_receiver;

  // Convert {maybe_string} to a String.
  Node* const string = a.ToString(context, maybe_string);

  CLabel fast_path(&a), slow_path(&a);
  BranchIfFastPath(&a, context, map, &fast_path, &slow_path);

  a.Bind(&fast_path);
  RegExpPrototypeSearchBodyFast(&a, receiver, string, context);

  a.Bind(&slow_path);
  RegExpPrototypeSearchBodySlow(&a, receiver, string, context);
}

namespace {

// Generates the fast path for @@split. {regexp} is an unmodified JSRegExp,
// {string} is a String, and {limit} is a Smi.
void Generate_RegExpPrototypeSplitBody(CodeStubAssembler* a, Node* const regexp,
                                       Node* const string, Node* const limit,
                                       Node* const context) {
  Isolate* isolate = a->isolate();

  Node* const null = a->NullConstant();
  Node* const smi_zero = a->SmiConstant(0);
  Node* const int_zero = a->IntPtrConstant(0);
  Node* const int_limit = a->SmiUntag(limit);

  const ElementsKind kind = FAST_ELEMENTS;
  const ParameterMode mode = CodeStubAssembler::INTPTR_PARAMETERS;

  Node* const allocation_site = nullptr;
  Node* const native_context = a->LoadNativeContext(context);
  Node* const array_map = a->LoadJSArrayElementsMap(kind, native_context);

  CLabel return_empty_array(a, CLabel::kDeferred);

  // If limit is zero, return an empty array.
  {
    CLabel next(a), if_limitiszero(a, CLabel::kDeferred);
    a->Branch(a->SmiEqual(limit, smi_zero), &return_empty_array, &next);
    a->Bind(&next);
  }

  Node* const string_length = a->LoadStringLength(string);

  // If passed the empty {string}, return either an empty array or a singleton
  // array depending on whether the {regexp} matches.
  {
    CLabel next(a), if_stringisempty(a, CLabel::kDeferred);
    a->Branch(a->SmiEqual(string_length, smi_zero), &if_stringisempty, &next);

    a->Bind(&if_stringisempty);
    {
      Node* const last_match_info = a->LoadContextElement(
          native_context, Context::REGEXP_LAST_MATCH_INFO_INDEX);

      Callable exec_callable = CodeFactory::RegExpExec(isolate);
      Node* const match_indices = a->CallStub(
          exec_callable, context, regexp, string, smi_zero, last_match_info);

      CLabel return_singleton_array(a);
      a->Branch(a->WordEqual(match_indices, null), &return_singleton_array,
                &return_empty_array);

      a->Bind(&return_singleton_array);
      {
        Node* const length = a->SmiConstant(1);
        Node* const capacity = a->IntPtrConstant(1);
        Node* const result = a->AllocateJSArray(kind, array_map, capacity,
                                                length, allocation_site, mode);

        Node* const fixed_array = a->LoadElements(result);
        a->StoreFixedArrayElement(fixed_array, 0, string);

        a->Return(result);
      }
    }

    a->Bind(&next);
  }

  // Loop preparations.

  GrowableFixedArray array(a);

  CVariable var_last_matched_until(a, MachineRepresentation::kTagged);
  CVariable var_next_search_from(a, MachineRepresentation::kTagged);

  var_last_matched_until.Bind(smi_zero);
  var_next_search_from.Bind(smi_zero);

  CVariable* vars[] = {array.var_array(), array.var_length(),
                       array.var_capacity(), &var_last_matched_until,
                       &var_next_search_from};
  const int vars_count = sizeof(vars) / sizeof(vars[0]);
  CLabel loop(a, vars_count, vars), push_suffix_and_out(a), out(a);
  a->Goto(&loop);

  a->Bind(&loop);
  {
    Node* const next_search_from = var_next_search_from.value();
    Node* const last_matched_until = var_last_matched_until.value();

    // We're done if we've reached the end of the string.
    {
      CLabel next(a);
      a->Branch(a->SmiEqual(next_search_from, string_length),
                &push_suffix_and_out, &next);
      a->Bind(&next);
    }

    // Search for the given {regexp}.

    Node* const last_match_info = a->LoadContextElement(
        native_context, Context::REGEXP_LAST_MATCH_INFO_INDEX);

    Callable exec_callable = CodeFactory::RegExpExec(isolate);
    Node* const match_indices =
        a->CallStub(exec_callable, context, regexp, string, next_search_from,
                    last_match_info);

    // We're done if no match was found.
    {
      CLabel next(a);
      a->Branch(a->WordEqual(match_indices, null), &push_suffix_and_out, &next);
      a->Bind(&next);
    }

    Node* const match_from = a->LoadFixedArrayElement(
        match_indices, RegExpMatchInfo::kFirstCaptureIndex);

    // We're done if the match starts beyond the string.
    {
      CLabel next(a);
      a->Branch(a->WordEqual(match_from, string_length), &push_suffix_and_out,
                &next);
      a->Bind(&next);
    }

    Node* const match_to = a->LoadFixedArrayElement(
        match_indices, RegExpMatchInfo::kFirstCaptureIndex + 1);

    // Advance index and continue if the match is empty.
    {
      CLabel next(a);

      a->GotoUnless(a->SmiEqual(match_to, next_search_from), &next);
      a->GotoUnless(a->SmiEqual(match_to, last_matched_until), &next);

      Node* const is_unicode = FastFlagGetter(a, regexp, JSRegExp::kUnicode);
      Node* const new_next_search_from =
          AdvanceStringIndex(a, string, next_search_from, is_unicode);
      var_next_search_from.Bind(new_next_search_from);
      a->Goto(&loop);

      a->Bind(&next);
    }

    // A valid match was found, add the new substring to the array.
    {
      Node* const from = last_matched_until;
      Node* const to = match_from;

      Node* const substr = a->SubString(context, string, from, to);
      array.Push(substr);

      a->GotoIf(a->WordEqual(array.length(), int_limit), &out);
    }

    // Add all captures to the array.
    {
      Node* const num_registers = a->LoadFixedArrayElement(
          match_indices, RegExpMatchInfo::kNumberOfCapturesIndex);
      Node* const int_num_registers = a->SmiUntag(num_registers);

      CVariable var_reg(a, MachineType::PointerRepresentation());
      var_reg.Bind(a->IntPtrConstant(2));

      CVariable* vars[] = {array.var_array(), array.var_length(),
                           array.var_capacity(), &var_reg};
      const int vars_count = sizeof(vars) / sizeof(vars[0]);
      CLabel nested_loop(a, vars_count, vars), nested_loop_out(a);
      a->Branch(a->IntPtrLessThan(var_reg.value(), int_num_registers),
                &nested_loop, &nested_loop_out);

      a->Bind(&nested_loop);
      {
        Node* const reg = var_reg.value();
        Node* const from = a->LoadFixedArrayElement(
            match_indices, reg,
            RegExpMatchInfo::kFirstCaptureIndex * kPointerSize, mode);
        Node* const to = a->LoadFixedArrayElement(
            match_indices, reg,
            (RegExpMatchInfo::kFirstCaptureIndex + 1) * kPointerSize, mode);

        CLabel select_capture(a), select_undefined(a), store_value(a);
        CVariable var_value(a, MachineRepresentation::kTagged);
        a->Branch(a->SmiEqual(to, a->SmiConstant(-1)), &select_undefined,
                  &select_capture);

        a->Bind(&select_capture);
        {
          Node* const substr = a->SubString(context, string, from, to);
          var_value.Bind(substr);
          a->Goto(&store_value);
        }

        a->Bind(&select_undefined);
        {
          Node* const undefined = a->UndefinedConstant();
          var_value.Bind(undefined);
          a->Goto(&store_value);
        }

        a->Bind(&store_value);
        {
          array.Push(var_value.value());
          a->GotoIf(a->WordEqual(array.length(), int_limit), &out);

          Node* const new_reg = a->IntPtrAdd(reg, a->IntPtrConstant(2));
          var_reg.Bind(new_reg);

          a->Branch(a->IntPtrLessThan(new_reg, int_num_registers), &nested_loop,
                    &nested_loop_out);
        }
      }

      a->Bind(&nested_loop_out);
    }

    var_last_matched_until.Bind(match_to);
    var_next_search_from.Bind(match_to);
    a->Goto(&loop);
  }

  a->Bind(&push_suffix_and_out);
  {
    Node* const from = var_last_matched_until.value();
    Node* const to = string_length;

    Node* const substr = a->SubString(context, string, from, to);
    array.Push(substr);

    a->Goto(&out);
  }

  a->Bind(&out);
  {
    Node* const result = array.ToJSArray(context);
    a->Return(result);
  }

  a->Bind(&return_empty_array);
  {
    Node* const length = smi_zero;
    Node* const capacity = int_zero;
    Node* const result = a->AllocateJSArray(kind, array_map, capacity, length,
                                            allocation_site, mode);
    a->Return(result);
  }
}

}  // namespace

// ES#sec-regexp.prototype-@@split
// RegExp.prototype [ @@split ] ( string, limit )
void Builtins::Generate_RegExpPrototypeSplit(CodeAssemblerState* state) {
  CodeStubAssembler a(state);

  Isolate* const isolate = a.isolate();

  Node* const undefined = a.UndefinedConstant();

  Node* const maybe_receiver = a.Parameter(0);
  Node* const maybe_string = a.Parameter(1);
  Node* const maybe_limit = a.Parameter(2);
  Node* const context = a.Parameter(5);

  // Ensure {maybe_receiver} is a JSReceiver.
  Node* const map = ThrowIfNotJSReceiver(
      &a, isolate, context, maybe_receiver,
      MessageTemplate::kIncompatibleMethodReceiver, "RegExp.prototype.@@split");
  Node* const receiver = maybe_receiver;

  // Convert {maybe_string} to a String.
  Node* const string = a.ToString(context, maybe_string);

  CLabel fast_path(&a), slow_path(&a);
  BranchIfFastPath(&a, context, map, &fast_path, &slow_path);

  a.Bind(&fast_path);
  {
    // Convert {maybe_limit} to a uint32, capping at the maximal smi value.
    CVariable var_limit(&a, MachineRepresentation::kTagged);
    CLabel if_limitissmimax(&a), limit_done(&a);

    a.GotoIf(a.WordEqual(maybe_limit, undefined), &if_limitissmimax);

    {
      Node* const limit = a.ToUint32(context, maybe_limit);
      a.GotoUnless(a.TaggedIsSmi(limit), &if_limitissmimax);

      var_limit.Bind(limit);
      a.Goto(&limit_done);
    }

    a.Bind(&if_limitissmimax);
    {
      // TODO(jgruber): In this case, we can probably generation of limit checks
      // in Generate_RegExpPrototypeSplitBody.
      Node* const smi_max = a.SmiConstant(Smi::kMaxValue);
      var_limit.Bind(smi_max);
      a.Goto(&limit_done);
    }

    a.Bind(&limit_done);
    {
      Node* const limit = var_limit.value();
      Generate_RegExpPrototypeSplitBody(&a, receiver, string, limit, context);
    }
  }

  a.Bind(&slow_path);
  {
    Node* const result = a.CallRuntime(Runtime::kRegExpSplit, context, receiver,
                                       string, maybe_limit);
    a.Return(result);
  }
}

namespace {

Node* ReplaceGlobalCallableFastPath(CodeStubAssembler* a, Node* context,
                                    Node* regexp, Node* subject_string,
                                    Node* replace_callable) {
  // The fast path is reached only if {receiver} is a global unmodified
  // JSRegExp instance and {replace_callable} is callable.

  Isolate* const isolate = a->isolate();

  Node* const null = a->NullConstant();
  Node* const undefined = a->UndefinedConstant();
  Node* const int_zero = a->IntPtrConstant(0);
  Node* const int_one = a->IntPtrConstant(1);
  Node* const smi_zero = a->SmiConstant(Smi::kZero);

  Node* const native_context = a->LoadNativeContext(context);

  CLabel out(a);
  CVariable var_result(a, MachineRepresentation::kTagged);

  // Set last index to 0.
  FastStoreLastIndex(a, regexp, smi_zero);

  // Allocate {result_array}.
  Node* result_array;
  {
    ElementsKind kind = FAST_ELEMENTS;
    Node* const array_map = a->LoadJSArrayElementsMap(kind, native_context);
    Node* const capacity = a->IntPtrConstant(16);
    Node* const length = smi_zero;
    Node* const allocation_site = nullptr;
    ParameterMode capacity_mode = CodeStubAssembler::INTPTR_PARAMETERS;

    result_array = a->AllocateJSArray(kind, array_map, capacity, length,
                                      allocation_site, capacity_mode);
  }

  // Call into runtime for RegExpExecMultiple.
  Node* last_match_info = a->LoadContextElement(
      native_context, Context::REGEXP_LAST_MATCH_INFO_INDEX);
  Node* const res =
      a->CallRuntime(Runtime::kRegExpExecMultiple, context, regexp,
                     subject_string, last_match_info, result_array);

  // Reset last index to 0.
  FastStoreLastIndex(a, regexp, smi_zero);

  // If no matches, return the subject string.
  var_result.Bind(subject_string);
  a->GotoIf(a->WordEqual(res, null), &out);

  // Reload last match info since it might have changed.
  last_match_info = a->LoadContextElement(
      native_context, Context::REGEXP_LAST_MATCH_INFO_INDEX);

  Node* const res_length = a->LoadJSArrayLength(res);
  Node* const res_elems = a->LoadElements(res);
  CSA_ASSERT(a, a->HasInstanceType(res_elems, FIXED_ARRAY_TYPE));

  Node* const num_capture_registers = a->LoadFixedArrayElement(
      last_match_info, RegExpMatchInfo::kNumberOfCapturesIndex);

  CLabel if_hasexplicitcaptures(a), if_noexplicitcaptures(a), create_result(a);
  a->Branch(a->SmiEqual(num_capture_registers, a->SmiConstant(Smi::FromInt(2))),
            &if_noexplicitcaptures, &if_hasexplicitcaptures);

  a->Bind(&if_noexplicitcaptures);
  {
    // If the number of captures is two then there are no explicit captures in
    // the regexp, just the implicit capture that captures the whole match. In
    // this case we can simplify quite a bit and end up with something faster.
    // The builder will consist of some integers that indicate slices of the
    // input string and some replacements that were returned from the replace
    // function.

    CVariable var_match_start(a, MachineRepresentation::kTagged);
    var_match_start.Bind(smi_zero);

    Node* const end = a->SmiUntag(res_length);
    CVariable var_i(a, MachineType::PointerRepresentation());
    var_i.Bind(int_zero);

    CVariable* vars[] = {&var_i, &var_match_start};
    CLabel loop(a, 2, vars);
    a->Goto(&loop);
    a->Bind(&loop);
    {
      Node* const i = var_i.value();
      a->GotoUnless(a->IntPtrLessThan(i, end), &create_result);

      ParameterMode mode = CodeStubAssembler::INTPTR_PARAMETERS;
      Node* const elem = a->LoadFixedArrayElement(res_elems, i, 0, mode);

      CLabel if_issmi(a), if_isstring(a), loop_epilogue(a);
      a->Branch(a->TaggedIsSmi(elem), &if_issmi, &if_isstring);

      a->Bind(&if_issmi);
      {
        // Integers represent slices of the original string.
        CLabel if_isnegativeorzero(a), if_ispositive(a);
        a->BranchIfSmiLessThanOrEqual(elem, smi_zero, &if_isnegativeorzero,
                                      &if_ispositive);

        a->Bind(&if_ispositive);
        {
          Node* const int_elem = a->SmiUntag(elem);
          Node* const new_match_start =
              a->IntPtrAdd(a->WordShr(int_elem, a->IntPtrConstant(11)),
                           a->WordAnd(int_elem, a->IntPtrConstant(0x7ff)));
          var_match_start.Bind(a->SmiTag(new_match_start));
          a->Goto(&loop_epilogue);
        }

        a->Bind(&if_isnegativeorzero);
        {
          Node* const next_i = a->IntPtrAdd(i, int_one);
          var_i.Bind(next_i);

          Node* const next_elem =
              a->LoadFixedArrayElement(res_elems, next_i, 0, mode);

          Node* const new_match_start = a->SmiSub(next_elem, elem);
          var_match_start.Bind(new_match_start);
          a->Goto(&loop_epilogue);
        }
      }

      a->Bind(&if_isstring);
      {
        CSA_ASSERT(a, a->IsStringInstanceType(a->LoadInstanceType(elem)));

        Callable call_callable = CodeFactory::Call(isolate);
        Node* const replacement_obj =
            a->CallJS(call_callable, context, replace_callable, undefined, elem,
                      var_match_start.value(), subject_string);

        Node* const replacement_str = a->ToString(context, replacement_obj);
        a->StoreFixedArrayElement(res_elems, i, replacement_str);

        Node* const elem_length = a->LoadStringLength(elem);
        Node* const new_match_start =
            a->SmiAdd(var_match_start.value(), elem_length);
        var_match_start.Bind(new_match_start);

        a->Goto(&loop_epilogue);
      }

      a->Bind(&loop_epilogue);
      {
        var_i.Bind(a->IntPtrAdd(var_i.value(), int_one));
        a->Goto(&loop);
      }
    }
  }

  a->Bind(&if_hasexplicitcaptures);
  {
    ParameterMode mode = CodeStubAssembler::INTPTR_PARAMETERS;

    Node* const from = int_zero;
    Node* const to = a->SmiUntag(res_length);
    const int increment = 1;

    a->BuildFastLoop(
        MachineType::PointerRepresentation(), from, to,
        [res_elems, isolate, native_context, context, undefined,
         replace_callable, mode](CodeStubAssembler* a, Node* index) {
          Node* const elem =
              a->LoadFixedArrayElement(res_elems, index, 0, mode);

          CLabel do_continue(a);
          a->GotoIf(a->TaggedIsSmi(elem), &do_continue);

          // elem must be an Array.
          // Use the apply argument as backing for global RegExp properties.

          CSA_ASSERT(a, a->HasInstanceType(elem, JS_ARRAY_TYPE));

          // TODO(jgruber): Remove indirection through Call->ReflectApply.
          Callable call_callable = CodeFactory::Call(isolate);
          Node* const reflect_apply = a->LoadContextElement(
              native_context, Context::REFLECT_APPLY_INDEX);

          Node* const replacement_obj =
              a->CallJS(call_callable, context, reflect_apply, undefined,
                        replace_callable, undefined, elem);

          // Overwrite the i'th element in the results with the string we got
          // back from the callback function.

          Node* const replacement_str = a->ToString(context, replacement_obj);
          a->StoreFixedArrayElement(res_elems, index, replacement_str,
                                    UPDATE_WRITE_BARRIER, 0, mode);

          a->Goto(&do_continue);
          a->Bind(&do_continue);
        },
        increment, CodeStubAssembler::IndexAdvanceMode::kPost);

    a->Goto(&create_result);
  }

  a->Bind(&create_result);
  {
    Node* const result = a->CallRuntime(Runtime::kStringBuilderConcat, context,
                                        res, res_length, subject_string);
    var_result.Bind(result);
    a->Goto(&out);
  }

  a->Bind(&out);
  return var_result.value();
}

Node* ReplaceSimpleStringFastPath(CodeStubAssembler* a, Node* context,
                                  Node* regexp, Node* subject_string,
                                  Node* replace_string) {
  // The fast path is reached only if {receiver} is an unmodified
  // JSRegExp instance, {replace_value} is non-callable, and
  // ToString({replace_value}) does not contain '$', i.e. we're doing a simple
  // string replacement.

  Isolate* const isolate = a->isolate();

  Node* const null = a->NullConstant();
  Node* const int_zero = a->IntPtrConstant(0);
  Node* const smi_zero = a->SmiConstant(Smi::kZero);

  CLabel out(a);
  CVariable var_result(a, MachineRepresentation::kTagged);

  // Load the last match info.
  Node* const native_context = a->LoadNativeContext(context);
  Node* const last_match_info = a->LoadContextElement(
      native_context, Context::REGEXP_LAST_MATCH_INFO_INDEX);

  // Is {regexp} global?
  CLabel if_isglobal(a), if_isnonglobal(a);
  Node* const flags = a->LoadObjectField(regexp, JSRegExp::kFlagsOffset);
  Node* const is_global =
      a->WordAnd(a->SmiUntag(flags), a->IntPtrConstant(JSRegExp::kGlobal));
  a->Branch(a->WordEqual(is_global, int_zero), &if_isnonglobal, &if_isglobal);

  a->Bind(&if_isglobal);
  {
    // Hand off global regexps to runtime.
    FastStoreLastIndex(a, regexp, smi_zero);
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

    CLabel if_matched(a), if_didnotmatch(a);
    a->Branch(a->WordEqual(match_indices, null), &if_didnotmatch, &if_matched);

    a->Bind(&if_didnotmatch);
    {
      FastStoreLastIndex(a, regexp, smi_zero);
      var_result.Bind(subject_string);
      a->Goto(&out);
    }

    a->Bind(&if_matched);
    {
      Node* const subject_start = smi_zero;
      Node* const match_start = a->LoadFixedArrayElement(
          match_indices, RegExpMatchInfo::kFirstCaptureIndex);
      Node* const match_end = a->LoadFixedArrayElement(
          match_indices, RegExpMatchInfo::kFirstCaptureIndex + 1);
      Node* const subject_end = a->LoadStringLength(subject_string);

      CLabel if_replaceisempty(a), if_replaceisnotempty(a);
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

        Node* const result = a->StringAdd(context, first_part, second_part);
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

        Node* result = a->StringAdd(context, first_part, second_part);
        result = a->StringAdd(context, result, third_part);

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
void Builtins::Generate_RegExpPrototypeReplace(CodeAssemblerState* state) {
  CodeStubAssembler a(state);

  Isolate* const isolate = a.isolate();

  Node* const maybe_receiver = a.Parameter(0);
  Node* const maybe_string = a.Parameter(1);
  Node* const replace_value = a.Parameter(2);
  Node* const context = a.Parameter(5);

  Node* const int_zero = a.IntPtrConstant(0);

  // Ensure {maybe_receiver} is a JSReceiver.
  Node* const map =
      ThrowIfNotJSReceiver(&a, isolate, context, maybe_receiver,
                           MessageTemplate::kIncompatibleMethodReceiver,
                           "RegExp.prototype.@@replace");
  Node* const receiver = maybe_receiver;

  // Convert {maybe_string} to a String.
  Callable tostring_callable = CodeFactory::ToString(isolate);
  Node* const string = a.CallStub(tostring_callable, context, maybe_string);

  // Fast-path checks: 1. Is the {receiver} an unmodified JSRegExp instance?
  CLabel checkreplacecallable(&a), runtime(&a, CLabel::kDeferred), fastpath(&a);
  BranchIfFastPath(&a, context, map, &checkreplacecallable, &runtime);

  a.Bind(&checkreplacecallable);
  Node* const regexp = receiver;

  // 2. Is {replace_value} callable?
  CLabel checkreplacestring(&a), if_iscallable(&a);
  a.GotoIf(a.TaggedIsSmi(replace_value), &checkreplacestring);

  Node* const replace_value_map = a.LoadMap(replace_value);
  a.Branch(a.IsCallableMap(replace_value_map), &if_iscallable,
           &checkreplacestring);

  // 3. Does ToString({replace_value}) contain '$'?
  a.Bind(&checkreplacestring);
  {
    Node* const replace_string =
        a.CallStub(tostring_callable, context, replace_value);

    Node* const dollar_char = a.IntPtrConstant('$');
    Node* const smi_minusone = a.SmiConstant(Smi::FromInt(-1));
    a.GotoUnless(a.SmiEqual(a.StringIndexOfChar(context, replace_string,
                                                dollar_char, int_zero),
                            smi_minusone),
                 &runtime);

    a.Return(ReplaceSimpleStringFastPath(&a, context, regexp, string,
                                         replace_string));
  }

  // {regexp} is unmodified and {replace_value} is callable.
  a.Bind(&if_iscallable);
  {
    Node* const replace_callable = replace_value;

    // Check if the {regexp} is global.
    CLabel if_isglobal(&a), if_isnotglobal(&a);
    Node* const is_global = FastFlagGetter(&a, regexp, JSRegExp::kGlobal);
    a.Branch(is_global, &if_isglobal, &if_isnotglobal);

    a.Bind(&if_isglobal);
    {
      Node* const result = ReplaceGlobalCallableFastPath(
          &a, context, regexp, string, replace_callable);
      a.Return(result);
    }

    a.Bind(&if_isnotglobal);
    {
      Node* const result =
          a.CallRuntime(Runtime::kStringReplaceNonGlobalRegExpWithFunction,
                        context, string, regexp, replace_callable);
      a.Return(result);
    }
  }

  a.Bind(&runtime);
  {
    Node* const result = a.CallRuntime(Runtime::kRegExpReplace, context,
                                       receiver, string, replace_value);
    a.Return(result);
  }
}

// Simple string matching functionality for internal use which does not modify
// the last match info.
void Builtins::Generate_RegExpInternalMatch(CodeAssemblerState* state) {
  CodeStubAssembler a(state);

  Isolate* const isolate = a.isolate();

  Node* const regexp = a.Parameter(1);
  Node* const string = a.Parameter(2);
  Node* const context = a.Parameter(5);

  Node* const null = a.NullConstant();
  Node* const smi_zero = a.SmiConstant(Smi::FromInt(0));

  Node* const native_context = a.LoadNativeContext(context);
  Node* const internal_match_info = a.LoadContextElement(
      native_context, Context::REGEXP_INTERNAL_MATCH_INFO_INDEX);

  Callable exec_callable = CodeFactory::RegExpExec(isolate);
  Node* const match_indices = a.CallStub(exec_callable, context, regexp, string,
                                         smi_zero, internal_match_info);

  CLabel if_matched(&a), if_didnotmatch(&a);
  a.Branch(a.WordEqual(match_indices, null), &if_didnotmatch, &if_matched);

  a.Bind(&if_didnotmatch);
  a.Return(null);

  a.Bind(&if_matched);
  {
    Node* result = ConstructNewResultFromMatchInfo(isolate, &a, context,
                                                   match_indices, string);
    a.Return(result);
  }
}

}  // namespace internal
}  // namespace v8
