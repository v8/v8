// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/arguments.h"
#include "src/execution/isolate-inl.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_RequestEnableETW) {
  isolate->RequestEnableETW();
  return ReadOnlyRoots(isolate).undefined_value();
}

}  // namespace internal
}  // namespace v8
