// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/member.h"

#include "src/base/macros.h"

namespace cppgc {
namespace internal {

EnabledCheckingPolicy::EnabledCheckingPolicy() {
  USE(impl_);
  // TODO(chromium:1056170): Save creating heap state.
}

void EnabledCheckingPolicy::CheckPointer(const void* ptr) {
  // TODO(chromium:1056170): Provide implementation.
}

}  // namespace internal
}  // namespace cppgc
