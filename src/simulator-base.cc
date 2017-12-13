// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/simulator-base.h"

#include "src/assembler.h"
#include "src/isolate.h"

#if defined(USE_SIMULATOR)

namespace v8 {
namespace internal {

// static
base::Mutex* SimulatorBase::redirection_mutex_ = nullptr;

// static
Redirection* SimulatorBase::redirection_ = nullptr;

// static
void SimulatorBase::InitializeOncePerProcess() {
  DCHECK_NULL(redirection_mutex_);
  redirection_mutex_ = new base::Mutex();
}

// static
void SimulatorBase::Initialize(Isolate* isolate) {
  if (isolate->simulator_initialized()) return;
  isolate->set_simulator_initialized(true);
  ExternalReference::set_redirector(isolate, &RedirectExternalReference);
}

}  // namespace internal
}  // namespace v8

#endif  // defined(USE_SIMULATOR)
