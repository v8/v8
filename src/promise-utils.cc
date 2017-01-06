// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/promise-utils.h"

#include "src/factory.h"
#include "src/isolate.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

JSPromise* PromiseUtils::GetPromise(Handle<Context> context) {
  return JSPromise::cast(context->get(kPromiseSlot));
}

Object* PromiseUtils::GetDebugEvent(Handle<Context> context) {
  return context->get(kDebugEventSlot);
}

bool PromiseUtils::HasAlreadyVisited(Handle<Context> context) {
  return Smi::cast(context->get(kAlreadyVisitedSlot))->value() != 0;
}

void PromiseUtils::SetAlreadyVisited(Handle<Context> context) {
  context->set(kAlreadyVisitedSlot, Smi::FromInt(1));
}

}  // namespace internal
}  // namespace v8
