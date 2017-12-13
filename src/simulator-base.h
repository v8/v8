// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SIMULATOR_BASE_H_
#define V8_SIMULATOR_BASE_H_

#include "src/assembler.h"
#include "src/globals.h"

namespace v8 {
namespace internal {

class Redirection;

class SimulatorBase {
 public:
  // Call on process start and exit.
  static void InitializeOncePerProcess();
  // TODO(mstarzinger): Move implementation to "simulator-base.cc" file.
  static void GlobalTearDown();

  // Call on isolate initialization and teardown.
  static void Initialize(Isolate* isolate);
  // TODO(mstarzinger): Move implementation to "simulator-base.cc" file.
  static void TearDown(base::CustomMatcherHashMap* i_cache);

  static base::Mutex* redirection_mutex() { return redirection_mutex_; }
  static Redirection* redirection() { return redirection_; }
  static void set_redirection(Redirection* r) { redirection_ = r; }

 private:
  // Runtime call support. Uses the isolate in a thread-safe way.
  static void* RedirectExternalReference(
      Isolate* isolate, void* external_function,
      v8::internal::ExternalReference::Type type);

  static base::Mutex* redirection_mutex_;
  static Redirection* redirection_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SIMULATOR_BASE_H_
