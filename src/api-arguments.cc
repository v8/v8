// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api-arguments.h"
#include "src/api-arguments-inl.h"

#include "src/debug/debug.h"
#include "src/objects-inl.h"
#include "src/tracing/trace-event.h"
#include "src/vm-state-inl.h"

namespace v8 {
namespace internal {

// static
bool CustomArgumentsBase ::PerformSideEffectCheck(Isolate* isolate,
                                                  Object* callback_info) {
  // TODO(7515): always pass a valid callback info object.
  if (callback_info == nullptr) return false;
  return isolate->debug()->PerformSideEffectCheckForCallback(callback_info);
}

}  // namespace internal
}  // namespace v8
