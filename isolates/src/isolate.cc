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
#include "compilation-cache.h"
#include "debug.h"
#include "heap-profiler.h"
#include "isolate.h"
#include "log.h"
#include "serialize.h"
#include "scanner.h"
#include "scopeinfo.h"
#include "simulator.h"
#include "stub-cache.h"
#include "oprofile-agent.h"

namespace v8 {
namespace internal {


// Create a dummy thread that will wait forever on a semaphore. The only
// purpose for this thread is to have some stack area to save essential data
// into for use by a stacks only core dump (aka minidump).
class PreallocatedMemoryThread: public Thread {
 public:
  char* data() {
    if (data_ready_semaphore_ != NULL) {
      // Initial access is guarded until the data has been published.
      data_ready_semaphore_->Wait();
      delete data_ready_semaphore_;
      data_ready_semaphore_ = NULL;
    }
    return data_;
  }

  unsigned length() {
    if (data_ready_semaphore_ != NULL) {
      // Initial access is guarded until the data has been published.
      data_ready_semaphore_->Wait();
      delete data_ready_semaphore_;
      data_ready_semaphore_ = NULL;
    }
    return length_;
  }

  // Stop the PreallocatedMemoryThread and release its resources.
  void StopThread() {
    keep_running_ = false;
    wait_for_ever_semaphore_->Signal();

    // Wait for the thread to terminate.
    Join();

    if (data_ready_semaphore_ != NULL) {
      delete data_ready_semaphore_;
      data_ready_semaphore_ = NULL;
    }

    delete wait_for_ever_semaphore_;
    wait_for_ever_semaphore_ = NULL;
  }

 protected:
  // When the thread starts running it will allocate a fixed number of bytes
  // on the stack and publish the location of this memory for others to use.
  void Run() {
    EmbeddedVector<char, 15 * 1024> local_buffer;

    // Initialize the buffer with a known good value.
    OS::StrNCpy(local_buffer, "Trace data was not generated.\n",
                local_buffer.length());

    // Publish the local buffer and signal its availability.
    data_ = local_buffer.start();
    length_ = local_buffer.length();
    data_ready_semaphore_->Signal();

    while (keep_running_) {
      // This thread will wait here until the end of time.
      wait_for_ever_semaphore_->Wait();
    }

    // Make sure we access the buffer after the wait to remove all possibility
    // of it being optimized away.
    OS::StrNCpy(local_buffer, "PreallocatedMemoryThread shutting down.\n",
                local_buffer.length());
  }


 private:
  PreallocatedMemoryThread()
      : keep_running_(true),
        wait_for_ever_semaphore_(OS::CreateSemaphore(0)),
        data_ready_semaphore_(OS::CreateSemaphore(0)),
        data_(NULL),
        length_(0) {
  }

  // Used to make sure that the thread keeps looping even for spurious wakeups.
  bool keep_running_;

  // This semaphore is used by the PreallocatedMemoryThread to wait for ever.
  Semaphore* wait_for_ever_semaphore_;
  // Semaphore to signal that the data has been initialized.
  Semaphore* data_ready_semaphore_;

  // Location and size of the preallocated memory block.
  char* data_;
  unsigned length_;

  friend class Isolate;

  DISALLOW_COPY_AND_ASSIGN(PreallocatedMemoryThread);
};


void Isolate::PreallocatedMemoryThreadStart() {
  if (preallocated_memory_thread_ != NULL) return;
  preallocated_memory_thread_ = new PreallocatedMemoryThread();
  preallocated_memory_thread_->Start();
}


void Isolate::PreallocatedMemoryThreadStop() {
  if (preallocated_memory_thread_ == NULL) return;
  preallocated_memory_thread_->StopThread();
  // Done with the thread entirely.
  delete preallocated_memory_thread_;
  preallocated_memory_thread_ = NULL;
}

#ifdef V8_USE_TLS_FOR_GLOBAL_ISOLATE
Thread::LocalStorageKey Isolate::global_isolate_key_;


Isolate* Isolate::InitThreadForGlobalIsolate() {
  ASSERT(global_isolate_ != NULL);
  Thread::SetThreadLocal(global_isolate_key_, global_isolate_);
  return global_isolate_;
}
#endif


Isolate* Isolate::global_isolate_ = NULL;
int Isolate::number_of_isolates_ = 0;


class IsolateInitializer {
 public:
  IsolateInitializer() {
    Isolate::InitOnce();
  }
};

static IsolateInitializer isolate_initializer;


void Isolate::InitOnce() {
  ASSERT(global_isolate_ == NULL);
#ifdef V8_USE_TLS_FOR_GLOBAL_ISOLATE
  global_isolate_key_ = Thread::CreateThreadLocalKey();
#endif
  global_isolate_ = new Isolate();
  CHECK(global_isolate_->PreInit());
}


Isolate* Isolate::Create(Deserializer* des) {
  // While we're still building out support for isolates, only support
  // one single global isolate.

  if (global_isolate_ != NULL) {
    // Allow for two-phase initialization.
    ASSERT(global_isolate_->state_ != INITIALIZED);
  } else {
    global_isolate_ = new Isolate();
  }

  if (global_isolate_->Init(des)) {
    ++number_of_isolates_;
    return global_isolate_;
  } else {
    delete global_isolate_;
    global_isolate_ = NULL;
#ifdef V8_USE_TLS_FOR_GLOBAL_ISOLATE
    Thread::SetThreadLocal(global_isolate_key_, NULL);
#endif
    return NULL;
  }
}


Isolate::Isolate()
    : state_(UNINITIALIZED),
      stack_trace_nesting_level_(0),
      incomplete_message_(NULL),
      preallocated_memory_thread_(NULL),
      preallocated_message_space_(NULL),
      bootstrapper_(NULL),
      compilation_cache_(new CompilationCache()),
      cpu_features_(NULL),
      break_access_(OS::CreateMutex()),
      stub_cache_(NULL),
      transcendental_cache_(new TranscendentalCache()),
      keyed_lookup_cache_(new KeyedLookupCache()),
      context_slot_cache_(new ContextSlotCache()),
      descriptor_lookup_cache_(new DescriptorLookupCache()),
      handle_scope_implementer_(NULL),
      scanner_character_classes_(new ScannerCharacterClasses()) {
  memset(isolate_addresses_, 0,
      sizeof(isolate_addresses_[0]) * (k_isolate_address_count + 1));

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


void Isolate::TearDownAndRecreateGlobalIsolate() {
  if (global_isolate_ != NULL) {
    delete global_isolate_;
    global_isolate_ = NULL;
  }

  global_isolate_ = new Isolate();
  global_isolate_->PreInit();
}


Isolate::~Isolate() {
  if (state_ == INITIALIZED) {
    OProfileAgent::TearDown();
    if (FLAG_preemption) {
      v8::Locker locker;
      v8::Locker::StopPreemption();
    }
    Builtins::TearDown();
    bootstrapper_->TearDown();

    // Remove the external reference to the preallocated stack memory.
    delete preallocated_message_space_;
    preallocated_message_space_ = NULL;
    PreallocatedMemoryThreadStop();

    HeapProfiler::TearDown();
    CpuProfiler::TearDown();
    heap_.TearDown();
    Logger::TearDown();
  }
  delete scanner_character_classes_;
  scanner_character_classes_ = NULL;

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
  delete compilation_cache_;
  compilation_cache_ = NULL;
  delete bootstrapper_;
  bootstrapper_ = NULL;

  if (state_ == INITIALIZED) --number_of_isolates_;
}


bool Isolate::PreInit() {
  if (state_ != UNINITIALIZED) return true;
  ASSERT(global_isolate_ == this);

#ifdef V8_USE_TLS_FOR_GLOBAL_ISOLATE
  InitThreadForGlobalIsolate();
#endif

  // Safe after setting Heap::isolate_, initializing StackGuard and
  // ensuring that Isolate::Current() == this.
  heap_.SetStackLimits();

#ifdef DEBUG
  DisallowAllocationFailure disallow_allocation_failure;
#endif

#define C(name) isolate_addresses_[Isolate::k_##name] =                        \
    reinterpret_cast<Address>(name());
  ISOLATE_ADDRESS_LIST(C)
  ISOLATE_ADDRESS_LIST_PROF(C)
#undef C

  bootstrapper_ = new Bootstrapper();
  cpu_features_ = new CpuFeatures();
  handle_scope_implementer_ = new HandleScopeImplementer();
  stub_cache_ = new StubCache();
  state_ = PREINITIALIZED;
  return true;
}


void Isolate::InitializeThreadLocal() {
  thread_local_top_.Initialize();
  clear_pending_exception();
  clear_pending_message();
  clear_scheduled_exception();
}


bool Isolate::Init(Deserializer* des) {
  ASSERT(global_isolate_ == this);
  ASSERT(state_ != INITIALIZED);

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

  InitializeThreadLocal();

  // Only preallocate on the first initialization.
  if (FLAG_preallocate_message_memory && preallocated_message_space_ == NULL) {
    // Start the thread which will set aside some memory.
    PreallocatedMemoryThreadStart();
    preallocated_message_space_ =
        new NoAllocationStringAllocator(
            preallocated_memory_thread_->data(),
            preallocated_memory_thread_->length());
    PreallocatedStorage::Init(preallocated_memory_thread_->length() / 4);
  }

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
