// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_DISPOSABLE_STACK_INL_H_
#define V8_OBJECTS_JS_DISPOSABLE_STACK_INL_H_

#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/heap/factory.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/js-disposable-stack.h"
#include "src/objects/objects-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-disposable-stack-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(JSDisposableStack)

CAST_ACCESSOR(JSDisposableStack)

BIT_FIELD_ACCESSORS(JSDisposableStack, status, state,
                    JSDisposableStack::StateBit)
BIT_FIELD_ACCESSORS(JSDisposableStack, status, length,
                    JSDisposableStack::LengthBits)

inline void JSDisposableStack::Add(Isolate* isolate,
                                   Handle<JSDisposableStack> disposable_stack,
                                   Handle<Object> value,
                                   Handle<Object> method) {
  DCHECK(!IsUndefined(disposable_stack->stack()));
  int length = disposable_stack->length();
  Handle<FixedArray> array(disposable_stack->stack(), isolate);
  array = FixedArray::SetAndGrow(isolate, array, length++, value);
  array = FixedArray::SetAndGrow(isolate, array, length++, method);

  disposable_stack->set_length(length);
  disposable_stack->set_stack(*array);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_DISPOSABLE_STACK_INL_H_
