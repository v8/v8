// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"

#include "src/code-factory.h"
#include "src/regexp/jsregexp.h"

namespace v8 {
namespace internal {

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
        a->HeapConstant(a->isolate()->factory()->last_index_string());
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
        a->HeapConstant(a->isolate()->factory()->last_index_string());
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
  Node* const smi_zero = a->SmiConstant(Smi::FromInt(0));

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

}  // namespace internal
}  // namespace v8
