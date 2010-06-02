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
#include "log.h"
#include "isolate.h"
#include "serialize.h"
#include "simulator.h"
#include "stub-cache.h"
#include "oprofile-agent.h"

namespace v8 {
namespace internal {


Isolate* Isolate::global_isolate = NULL;


void Isolate::InitOnce() {
}


Isolate* Isolate::Create(Deserializer* des) {
  // While we're still building out support for isolates, only support
  // one single global isolate.
  ASSERT(global_isolate == NULL);
  global_isolate = new Isolate();
  if (global_isolate->Init(des)) {
    return global_isolate;
  } else {
    delete global_isolate;
    global_isolate = NULL;
    return NULL;
  }
}


Isolate::Isolate()
    : bootstrapper_(NULL),
      stub_cache_(NULL) {
}


Isolate::~Isolate() {
  delete stub_cache_;
  stub_cache_ = NULL;
  delete bootstrapper_;
  bootstrapper_ = NULL;
}


bool Isolate::Init(Deserializer* des) {
  ASSERT(global_isolate == this);

  bool create_heap_objects = des == NULL;

#ifdef DEBUG
  // The initialization process does not handle memory exhaustion.
  DisallowAllocationFailure disallow_allocation_failure;
#endif

  // Allocate per-isolate globals early.
  bootstrapper_ = new Bootstrapper();
  stub_cache_ = new StubCache();

  // Enable logging before setting up the heap
  Logger::Setup();

  CpuProfiler::Setup();

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
    StackGuard::InitThread(lock);
  }

  // Setup the object heap
  ASSERT(!Heap::HasBeenSetup());
  if (!Heap::Setup(create_heap_objects)) {
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
  Heap::SetStackLimits();

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

  return true;
}


} }  // namespace v8::internal
