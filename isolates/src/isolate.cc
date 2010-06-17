// Copyright 2006-2010 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stdlib.h>

#include "v8.h"

#include "bootstrapper.h"
#include "debug.h"
#include "heap-profiler.h"
#include "isolate.h"
#include "log.h"
#include "serialize.h"
#include "scopeinfo.h"
#include "simulator.h"
#include "stub-cache.h"
#include "oprofile-agent.h"

namespace v8 {
namespace internal {


Isolate* Isolate::global_isolate = NULL;
int Isolate::number_of_isolates_ = 0;


class IsolateInitializer {
 public:
  IsolateInitializer() {
    Isolate::InitOnce();
  }
};


static IsolateInitializer isolate_initializer;


void Isolate::InitOnce() {
  ASSERT(global_isolate == NULL);
  global_isolate = new Isolate();
  CHECK(global_isolate->PreInit());
}


Isolate* Isolate::Create(Deserializer* des) {
  // While we're still building out support for isolates, only support
  // one single global isolate.

  if (global_isolate != NULL) {
    // Allow for two-phase initialization.
    ASSERT(global_isolate->state_ != INITIALIZED);
  } else {
    global_isolate = new Isolate();
  }

  if (global_isolate->Init(des)) {
    ++number_of_isolates_;
    return global_isolate;
  } else {
    delete global_isolate;
    global_isolate = NULL;
    return NULL;
  }
}


Isolate::Isolate()
    : state_(UNINITIALIZED),
      bootstrapper_(NULL),
      cpu_features_(NULL),
      break_access_(OS::CreateMutex()),
      stub_cache_(NULL),
      transcendental_cache_(new TranscendentalCache()),
      keyed_lookup_cache_(new KeyedLookupCache()),
      context_slot_cache_(new ContextSlotCache()),
      descriptor_lookup_cache_(new DescriptorLookupCache()),
      handle_scope_implementer_(NULL) {
  heap_.isolate_ = this;
  stack_guard_.isolate_ = this;

  handle_scope_data_.Initialize();

#define ISOLATE_INIT_EXECUTE(type, name, initial_value)                        \
  name##_ = (initial_value);
  ISOLATE_INIT_LIST(ISOLATE_INIT_EXECUTE)
#undef ISOLATE_INIT_EXECUTE

#define ISOLATE_INIT_ARRAY_EXECUTE(type, name, length)                         \
  memset(name##_, 0, sizeof(type) * length);
  ISOLATE_INIT_ARRAY_LIST(ISOLATE_INIT_ARRAY_EXECUTE)
#undef ISOLATE_INIT_ARRAY_EXECUTE
}


Isolate::~Isolate() {
  delete descriptor_lookup_cache_;
  descriptor_lookup_cache_ = NULL;
  delete context_slot_cache_;
  context_slot_cache_ = NULL;
  delete keyed_lookup_cache_;
  keyed_lookup_cache_ = NULL;

  delete transcendental_cache_;
  transcendental_cache_ = NULL;
  delete stub_cache_;
  stub_cache_ = NULL;
  delete cpu_features_;
  cpu_features_ = NULL;
  delete bootstrapper_;
  bootstrapper_ = NULL;

  if (state_ == INITIALIZED) --number_of_isolates_;
}


bool Isolate::PreInit() {
  if (state_ != UNINITIALIZED) return true;
  ASSERT(global_isolate == this);

  // Safe after setting Heap::isolate_, initializing StackGuard and
  // ensuring that Isolate::Current() == this.
  heap_.SetStackLimits();

#ifdef DEBUG
  DisallowAllocationFailure disallow_allocation_failure;
#endif
  bootstrapper_ = new Bootstrapper();
  cpu_features_ = new CpuFeatures();
  handle_scope_implementer_ = new HandleScopeImplementer();
  stub_cache_ = new StubCache();
  state_ = PREINITIALIZED;
  return true;
}


bool Isolate::Init(Deserializer* des) {
  ASSERT(global_isolate == this);

  bool create_heap_objects = des == NULL;

#ifdef DEBUG
  // The initialization process does not handle memory exhaustion.
  DisallowAllocationFailure disallow_allocation_failure;
#endif

  if (state_ == UNINITIALIZED && !PreInit()) return false;

  // Enable logging before setting up the heap
  Logger::Setup();

  CpuProfiler::Setup();
  HeapProfiler::Setup();

  // Setup the platform OS support.
  OS::Setup();

  // Initialize other runtime facilities
#if !V8_HOST_ARCH_ARM && V8_TARGET_ARCH_ARM
  ::assembler::arm::Simulator::Initialize();
#endif

  { // NOLINT
    // Ensure that the thread has a valid stack guard.  The v8::Locker object
    // will ensure this too, but we don't have to use lockers if we are only
    // using one thread.
    ExecutionAccess lock;
    stack_guard_.InitThread(lock);
  }

  // Setup the object heap
  ASSERT(!heap_.HasBeenSetup());
  if (!heap_.Setup(create_heap_objects)) {
    V8::SetFatalError();
    return false;
  }

  bootstrapper_->Initialize(create_heap_objects);
  Builtins::Setup(create_heap_objects);
  Top::Initialize();

  if (FLAG_preemption) {
    v8::Locker locker;
    v8::Locker::StartPreemption(100);
  }

#ifdef ENABLE_DEBUGGER_SUPPORT
  Debug::Setup(create_heap_objects);
#endif
  stub_cache_->Initialize(create_heap_objects);

  // If we are deserializing, read the state into the now-empty heap.
  if (des != NULL) {
    des->Deserialize();
    stub_cache_->Clear();
  }

  // Deserializing may put strange things in the root array's copy of the
  // stack guard.
  heap_.SetStackLimits();

  // Setup the CPU support. Must be done after heap setup and after
  // any deserialization because we have to have the initial heap
  // objects in place for creating the code object used for probing.
  CPU::Setup();

  OProfileAgent::Initialize();

  // If we are deserializing, log non-function code objects and compiled
  // functions found in the snapshot.
  if (des != NULL && FLAG_log_code) {
    HandleScope scope;
    LOG(LogCodeObjects());
    LOG(LogCompiledFunctions());
  }

  state_ = INITIALIZED;
  return true;
}


} }  // namespace v8::internal
