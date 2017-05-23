// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-iterator-gen.h"

namespace v8 {
namespace internal {

using compiler::Node;

Node* IteratorBuiltinsAssembler::GetIterator(Node* context, Node* object) {
  Node* method = GetProperty(context, object, factory()->iterator_symbol());

  Callable callable = CodeFactory::Call(isolate());
  Node* iterator = CallJS(callable, context, method, object);

  Label done(this), if_notobject(this, Label::kDeferred);
  GotoIf(TaggedIsSmi(iterator), &if_notobject);
  Branch(IsJSReceiver(iterator), &done, &if_notobject);

  BIND(&if_notobject);
  {
    CallRuntime(Runtime::kThrowTypeError, context,
                SmiConstant(MessageTemplate::kNotAnIterator), iterator);
    Unreachable();
  }

  BIND(&done);
  return iterator;
}

Node* IteratorBuiltinsAssembler::IteratorStep(Node* context, Node* iterator,
                                              Label* if_done,
                                              Node* fast_iterator_result_map) {
  DCHECK_NOT_NULL(if_done);

  // IteratorNext
  Node* next_method = GetProperty(context, iterator, factory()->next_string());

  // 1. a. Let result be ? Invoke(iterator, "next", « »).
  Callable callable = CodeFactory::Call(isolate());
  Node* result = CallJS(callable, context, next_method, iterator);

  // 3. If Type(result) is not Object, throw a TypeError exception.
  Label if_notobject(this, Label::kDeferred), return_result(this);
  GotoIf(TaggedIsSmi(result), &if_notobject);
  GotoIfNot(IsJSReceiver(result), &if_notobject);

  Label if_generic(this);
  VARIABLE(var_done, MachineRepresentation::kTagged);

  if (fast_iterator_result_map != nullptr) {
    // 4. Return result.
    Node* map = LoadMap(result);
    GotoIfNot(WordEqual(map, fast_iterator_result_map), &if_generic);

    // IteratorComplete
    // 2. Return ToBoolean(? Get(iterResult, "done")).
    Node* done = LoadObjectField(result, JSIteratorResult::kDoneOffset);
    CSA_ASSERT(this, IsBoolean(done));
    var_done.Bind(done);
    Goto(&return_result);
  } else {
    Goto(&if_generic);
  }

  BIND(&if_generic);
  {
    // IteratorComplete
    // 2. Return ToBoolean(? Get(iterResult, "done")).
    Node* done = GetProperty(context, result, factory()->done_string());
    var_done.Bind(done);

    Label to_boolean(this, Label::kDeferred);
    GotoIf(TaggedIsSmi(done), &to_boolean);
    Branch(IsBoolean(done), &return_result, &to_boolean);

    BIND(&to_boolean);
    var_done.Bind(CallStub(CodeFactory::ToBoolean(isolate()), context, done));
    Goto(&return_result);
  }

  BIND(&if_notobject);
  {
    CallRuntime(Runtime::kThrowIteratorResultNotAnObject, context, result);
    Goto(if_done);
  }

  BIND(&return_result);
  GotoIf(IsTrue(var_done.value()), if_done);
  return result;
}

Node* IteratorBuiltinsAssembler::IteratorValue(Node* context, Node* result,
                                               Node* fast_iterator_result_map) {
  CSA_ASSERT(this, IsJSReceiver(result));

  Label exit(this), if_generic(this);
  VARIABLE(var_value, MachineRepresentation::kTagged);
  if (fast_iterator_result_map != nullptr) {
    Node* map = LoadMap(result);
    GotoIfNot(WordEqual(map, fast_iterator_result_map), &if_generic);
    var_value.Bind(LoadObjectField(result, JSIteratorResult::kValueOffset));
    Goto(&exit);
  } else {
    Goto(&if_generic);
  }

  BIND(&if_generic);
  {
    Node* value = GetProperty(context, result, factory()->value_string());
    var_value.Bind(value);
    Goto(&exit);
  }

  BIND(&exit);
  return var_value.value();
}

void IteratorBuiltinsAssembler::IteratorClose(Node* context, Node* iterator,
                                              Node* exception) {
  CSA_ASSERT(this, IsJSReceiver(iterator));
  VARIABLE(var_iter_exception, MachineRepresentation::kTagged,
           UndefinedConstant());

  Label rethrow_exception(this);
  Node* method = GetProperty(context, iterator, factory()->return_string());
  GotoIf(Word32Or(IsUndefined(method), IsNull(method)), &rethrow_exception);

  Label if_iter_exception(this), if_notobject(this);

  Node* inner_result =
      CallJS(CodeFactory::Call(isolate()), context, method, iterator);

  GotoIfException(inner_result, &if_iter_exception, &var_iter_exception);
  GotoIfNot(IsUndefined(exception), &rethrow_exception);

  GotoIf(TaggedIsSmi(inner_result), &if_notobject);
  Branch(IsJSReceiver(inner_result), &rethrow_exception, &if_notobject);

  BIND(&if_notobject);
  {
    CallRuntime(Runtime::kThrowIteratorResultNotAnObject, context,
                inner_result);
    Unreachable();
  }

  BIND(&if_iter_exception);
  {
    GotoIfNot(IsUndefined(exception), &rethrow_exception);
    CallRuntime(Runtime::kReThrow, context, var_iter_exception.value());
    Unreachable();
  }

  BIND(&rethrow_exception);
  {
    CallRuntime(Runtime::kReThrow, context, exception);
    Unreachable();
  }
}

}  // namespace internal
}  // namespace v8
