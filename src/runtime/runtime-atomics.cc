// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime-utils.h"

#include "src/arguments.h"
#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/conversions-inl.h"
#include "src/factory.h"

// Implement Atomic accesses to SharedArrayBuffers as defined in the
// SharedArrayBuffer draft spec, found here
// https://github.com/tc39/ecmascript_sharedmem

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_ThrowNotIntegerSharedTypedArrayError) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(Object, value, 0);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate,
      NewTypeError(MessageTemplate::kNotIntegerSharedTypedArray, value));
}

RUNTIME_FUNCTION(Runtime_ThrowNotInt32SharedTypedArrayError) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(Object, value, 0);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kNotInt32SharedTypedArray, value));
}

RUNTIME_FUNCTION(Runtime_ThrowInvalidAtomicAccessIndexError) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewRangeError(MessageTemplate::kInvalidAtomicAccessIndex));
}

}  // namespace internal
}  // namespace v8
