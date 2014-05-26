// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "v8.h"

#include "assembler.h"
#include "isolate.h"
#include "elements.h"
#include "bootstrapper.h"
#include "debug.h"
#include "deoptimizer.h"
#include "frames.h"
#include "heap-profiler.h"
#include "hydrogen.h"
#ifdef V8_USE_DEFAULT_PLATFORM
#include "libplatform/default-platform.h"
#endif
#include "lithium-allocator.h"
#include "objects.h"
#include "once.h"
#include "platform.h"
#include "sampler.h"
#include "runtime-profiler.h"
#include "serialize.h"
#include "store-buffer.h"

namespace v8 {
namespace internal {

V8_DECLARE_ONCE(init_once);

v8::ArrayBuffer::Allocator* V8::array_buffer_allocator_ = NULL;
v8::Platform* V8::platform_ = NULL;


bool V8::Initialize(Deserializer* des) {
  InitializeOncePerProcess();
  Isolate* isolate = Isolate::UncheckedCurrent();
  if (isolate == NULL) return true;
  if (isolate->IsDead()) return false;
  if (isolate->IsInitialized()) return true;

#ifdef V8_USE_DEFAULT_PLATFORM
  DefaultPlatform* platform = static_cast<DefaultPlatform*>(platform_);
  platform->SetThreadPoolSize(isolate->max_available_threads());
  // We currently only start the threads early, if we know that we'll use them.
  if (FLAG_job_based_sweeping) platform->EnsureInitialized();
#endif

  return isolate->Init(des);
}


void V8::TearDown() {
  Bootstrapper::TearDownExtensions();
  ElementsAccessor::TearDown();
  LOperand::TearDownCaches();
  ExternalReference::TearDownMathExpData();
  RegisteredExtension::UnregisterAll();
  Isolate::GlobalTearDown();

  Sampler::TearDown();

#ifdef V8_USE_DEFAULT_PLATFORM
  DefaultPlatform* platform = static_cast<DefaultPlatform*>(platform_);
  platform_ = NULL;
  delete platform;
#endif
}


void V8::SetReturnAddressLocationResolver(
      ReturnAddressLocationResolver resolver) {
  StackFrame::SetReturnAddressLocationResolver(resolver);
}


void V8::InitializeOncePerProcessImpl() {
  FlagList::EnforceFlagImplications();

  if (FLAG_predictable && FLAG_random_seed == 0) {
    // Avoid random seeds in predictable mode.
    FLAG_random_seed = 12347;
  }

  if (FLAG_stress_compaction) {
    FLAG_force_marking_deque_overflows = true;
    FLAG_gc_global = true;
    FLAG_max_semi_space_size = 1;
  }

#ifdef V8_USE_DEFAULT_PLATFORM
  platform_ = new DefaultPlatform;
#endif
  Sampler::SetUp();
  CpuFeatures::Probe(false);
  init_memcopy_functions();
  // The custom exp implementation needs 16KB of lookup data; initialize it
  // on demand.
  init_fast_sqrt_function();
#ifdef _WIN64
  init_modulo_function();
#endif
  ElementsAccessor::InitializeOncePerProcess();
  LOperand::SetUpCaches();
  SetUpJSCallerSavedCodeData();
  ExternalReference::SetUp();
  Bootstrapper::InitializeOncePerProcess();
}


void V8::InitializeOncePerProcess() {
  CallOnce(&init_once, &InitializeOncePerProcessImpl);
}


void V8::InitializePlatform(v8::Platform* platform) {
  ASSERT(!platform_);
  ASSERT(platform);
  platform_ = platform;
}


void V8::ShutdownPlatform() {
  ASSERT(platform_);
  platform_ = NULL;
}


v8::Platform* V8::GetCurrentPlatform() {
  ASSERT(platform_);
  return platform_;
}

} }  // namespace v8::internal
