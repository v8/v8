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

#include "ast.h"
#include "bootstrapper.h"
#include "codegen.h"
#include "compilation-cache.h"
#include "debug.h"
#include "heap-profiler.h"
#include "isolate.h"
#include "log.h"
#include "regexp-stack.h"
#include "serialize.h"
#include "scanner.h"
#include "scopeinfo.h"
#include "simulator.h"
#include "stub-cache.h"
#include "spaces.h"
#include "oprofile-agent.h"
#include "version.h"

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
  explicit PreallocatedMemoryThread(Isolate* isolate)
      : Thread(isolate),
        keep_running_(true),
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
  preallocated_memory_thread_ = new PreallocatedMemoryThread(this);
  preallocated_memory_thread_->Start();
}


void Isolate::PreallocatedMemoryThreadStop() {
  if (preallocated_memory_thread_ == NULL) return;
  preallocated_memory_thread_->StopThread();
  // Done with the thread entirely.
  delete preallocated_memory_thread_;
  preallocated_memory_thread_ = NULL;
}


Isolate* Isolate::default_isolate_ = NULL;
Thread::LocalStorageKey Isolate::isolate_key_;
Thread::LocalStorageKey Isolate::thread_id_key_;
Thread::LocalStorageKey Isolate::per_isolate_thread_data_key_;
Mutex* Isolate::process_wide_mutex_ = NULL;
Isolate::ThreadDataTable* Isolate::thread_data_table_ = NULL;
Isolate::ThreadId Isolate::highest_thread_id_ = 0;

class IsolateInitializer {
 public:
  IsolateInitializer() {
    Isolate::isolate_key_ = Thread::CreateThreadLocalKey();
    Isolate::thread_id_key_ = Thread::CreateThreadLocalKey();
    Isolate::per_isolate_thread_data_key_ = Thread::CreateThreadLocalKey();
    Isolate::process_wide_mutex_ = OS::CreateMutex();
    Isolate::thread_data_table_ = new Isolate::ThreadDataTable();
    Isolate::EnterDefaultIsolate();
  }
};

bool Isolate::EnsureDefaultIsolateAllocated() {
  // TODO(isolates): Use the system threading API to do this once?
  static IsolateInitializer static_initializer;
  USE(static_initializer);
  return true;
}

static bool static_initialized = Isolate::EnsureDefaultIsolateAllocated();


Isolate::ThreadId Isolate::AllocateThreadId() {
  ThreadId new_id;
  {
    ScopedLock lock(process_wide_mutex_);
    new_id = ++highest_thread_id_;
  }
  return new_id;
}


Isolate::PerIsolateThreadData* Isolate::AllocatePerIsolateThreadData(
    ThreadId thread_id) {
  ASSERT(thread_id != 0);
  ASSERT(Thread::GetThreadLocalInt(thread_id_key_) == thread_id);
  PerIsolateThreadData* per_thread = new PerIsolateThreadData(this, thread_id);
  {
    ScopedLock lock(process_wide_mutex_);
    ASSERT(thread_data_table_->Lookup(this, thread_id) == NULL);
    thread_data_table_->Insert(per_thread);
    ASSERT(thread_data_table_->Lookup(this, thread_id) == per_thread);
  }
  return per_thread;
}


Isolate::PerIsolateThreadData*
    Isolate::FindOrAllocatePerThreadDataForThisThread() {
  ThreadId thread_id = Thread::GetThreadLocalInt(thread_id_key_);
  if (thread_id == 0) {
    thread_id = AllocateThreadId();
    Thread::SetThreadLocalInt(thread_id_key_, thread_id);
  }
  PerIsolateThreadData* per_thread = NULL;
  {
    ScopedLock lock(process_wide_mutex_);
    per_thread = thread_data_table_->Lookup(this, thread_id);
    if (per_thread == NULL) {
      per_thread = AllocatePerIsolateThreadData(thread_id);
    }
  }
  return per_thread;
}


bool Isolate::InitializeDefaultIsolate(Deserializer* des) {
  ASSERT(default_isolate_ != NULL);
  ASSERT(default_isolate_->state_ != INITIALIZED);
  CHECK(default_isolate_->Init(des));
  return true;
}


void Isolate::EnsureDefaultIsolate() {
  ScopedLock lock(process_wide_mutex_);
  if (default_isolate_ == NULL) {
    USE(static_initialized);
    Isolate* new_default_isolate = new Isolate();
    CHECK(new_default_isolate->PreInit());
    default_isolate_ = new_default_isolate;
  }
}


Isolate* Isolate::EnterDefaultIsolate() {
  // TODO(isolates): Check that we're not in multiple-isolate mode.
  EnsureDefaultIsolate();

  ASSERT(default_isolate_ != NULL);
  PerIsolateThreadData* per_thread = CurrentPerIsolateThreadData();
  if (per_thread != NULL) {
    ASSERT(per_thread->isolate_ == default_isolate_);
    ASSERT(Current() != NULL);
    return default_isolate_;
  }
  PerIsolateThreadData* new_per_thread =
      default_isolate_->FindOrAllocatePerThreadDataForThisThread();
  ASSERT(new_per_thread != NULL);
  ASSERT(new_per_thread->isolate_ == default_isolate_);
  Thread::SetThreadLocal(per_isolate_thread_data_key_, new_per_thread);
  Thread::SetThreadLocal(isolate_key_, default_isolate_);
  return default_isolate_;
}


Isolate::ThreadDataTable::ThreadDataTable()
    : list_(NULL) {
}


Isolate::PerIsolateThreadData*
    Isolate::ThreadDataTable::Lookup(Isolate* isolate, ThreadId thread_id) {
  for (PerIsolateThreadData* data = list_; data != NULL; data = data->next_) {
    if (data->Matches(isolate, thread_id)) return data;
  }
  return NULL;
}


void Isolate::ThreadDataTable::Insert(Isolate::PerIsolateThreadData* data) {
  if (list_ != NULL) list_->prev_ = data;
  data->next_ = list_;
  list_ = data;
}


void Isolate::ThreadDataTable::Remove(PerIsolateThreadData* data) {
  if (list_ == data) list_ = data->next_;
  if (data->next_ != NULL) data->next_->prev_ = data->prev_;
  if (data->prev_ != NULL) data->prev_->next_ = data->next_;
}


void Isolate::ThreadDataTable::Remove(Isolate* isolate, ThreadId thread_id) {
  PerIsolateThreadData* data = Lookup(isolate, thread_id);
  if (data != NULL) {
    Remove(data);
  }
}


Isolate::Isolate()
    : state_(UNINITIALIZED),
      stack_trace_nesting_level_(0),
      incomplete_message_(NULL),
      preallocated_memory_thread_(NULL),
      preallocated_message_space_(NULL),
      bootstrapper_(NULL),
      compilation_cache_(NULL),
      counters_(new Counters()),
      cpu_features_(NULL),
      code_range_(NULL),
      break_access_(OS::CreateMutex()),
      logger_(new Logger()),
      stats_table_(new StatsTable()),
      stub_cache_(NULL),
      capture_stack_trace_for_uncaught_exceptions_(false),
      stack_trace_for_uncaught_exceptions_frame_limit_(0),
      stack_trace_for_uncaught_exceptions_options_(StackTrace::kOverview),
      transcendental_cache_(NULL),
      memory_allocator_(NULL),
      keyed_lookup_cache_(NULL),
      context_slot_cache_(NULL),
      descriptor_lookup_cache_(NULL),
      handle_scope_implementer_(NULL),
      scanner_character_classes_(NULL),
      in_use_list_(0),
      free_list_(0),
      preallocated_storage_preallocated_(false),
      write_input_buffer_(NULL),
      global_handles_(NULL),
      context_switcher_(NULL),
      thread_manager_(NULL),
      ast_sentinels_(NULL),
      inline_runtime_functions_table_(NULL),
      string_tracker_(NULL),
      regexp_stack_(NULL),
      frame_element_constant_list_(0),
      result_constant_list_(0),
      vm_state_(0) {
  memset(isolate_addresses_, 0,
      sizeof(isolate_addresses_[0]) * (k_isolate_address_count + 1));

  heap_.isolate_ = this;
  zone_.isolate_ = this;
  stack_guard_.isolate_ = this;

#if defined(V8_TARGET_ARCH_ARM) && !defined(__arm__)
  simulator_initialized_ = false;
  simulator_i_cache_ = NULL;
  simulator_key_ = Thread::CreateThreadLocalKey();
  simulator_redirection_ = NULL;
#endif

#ifdef DEBUG
  // heap_histograms_ initializes itself.
  memset(&js_spill_information_, 0, sizeof(js_spill_information_));
  memset(code_kind_statistics_, 0,
         sizeof(code_kind_statistics_[0]) * Code::NUMBER_OF_KINDS);
#endif

#ifdef ENABLE_DEBUGGER_SUPPORT
  debug_ = NULL;
  debugger_ = NULL;
#endif

#ifdef ENABLE_LOGGING_AND_PROFILING
  producer_heap_profile_ = NULL;
#endif

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

void Isolate::TearDown() {
  if (default_isolate_ != NULL) {
    delete default_isolate_;
    default_isolate_ = NULL;
  }
}

// TODO(isolates): This is unlikely to be correct.
void Isolate::TearDownAndRecreateGlobalIsolate() {
  TearDown();

  default_isolate_ = new Isolate();
  default_isolate_->PreInit();
}


Isolate::~Isolate() {
  if (state_ == INITIALIZED) {
    OProfileAgent::TearDown();
    if (FLAG_preemption) {
      v8::Locker locker;
      v8::Locker::StopPreemption();
    }
    builtins_.TearDown();
    bootstrapper_->TearDown();

    // Remove the external reference to the preallocated stack memory.
    delete preallocated_message_space_;
    preallocated_message_space_ = NULL;
    PreallocatedMemoryThreadStop();

    HeapProfiler::TearDown();
    CpuProfiler::TearDown();
    heap_.TearDown();
    logger_->TearDown();
  }

#ifdef ENABLE_LOGGING_AND_PROFILING
  delete producer_heap_profile_;
  producer_heap_profile_ = NULL;
#endif

  delete scanner_character_classes_;
  scanner_character_classes_ = NULL;

  delete regexp_stack_;
  regexp_stack_ = NULL;

  delete inline_runtime_functions_table_;
  inline_runtime_functions_table_ = NULL;

  delete ast_sentinels_;
  ast_sentinels_ = NULL;

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
  delete stats_table_;
  stats_table_ = NULL;

  delete logger_;
  logger_ = NULL;

  delete counters_;
  counters_ = NULL;
  delete cpu_features_;
  cpu_features_ = NULL;

  delete compilation_cache_;
  compilation_cache_ = NULL;
  delete bootstrapper_;
  bootstrapper_ = NULL;
  delete write_input_buffer_;
  write_input_buffer_ = NULL;

  delete context_switcher_;
  context_switcher_ = NULL;
  delete thread_manager_;
  thread_manager_ = NULL;

  delete string_tracker_;
  string_tracker_ = NULL;

  delete memory_allocator_;
  memory_allocator_ = NULL;
  delete code_range_;
  code_range_ = NULL;
  delete global_handles_;
  global_handles_ = NULL;

#ifdef ENABLE_DEBUGGER_SUPPORT
  delete debugger_;
  debugger_ = NULL;
  delete debug_;
  debug_ = NULL;
#endif

#if defined(V8_TARGET_ARCH_ARM) && !defined(__arm__)
  Thread::DeleteThreadLocalKey(simulator_key_);
#endif
}


bool Isolate::PreInit() {
  if (state_ != UNINITIALIZED) return true;
  ASSERT(default_isolate_ == NULL);

  // TODO(isolates): Use the entry/exit stack properly here.
  PerIsolateThreadData* per_thread = CurrentPerIsolateThreadData();
  // This thread has no business being in an isolate if the global isolate
  // hasn't yet been initialized!
  ASSERT_EQ(NULL, per_thread);
  per_thread = FindOrAllocatePerThreadDataForThisThread();
  ASSERT(per_thread != NULL);
  ASSERT(per_thread->isolate_ == this);
  Thread::SetThreadLocal(per_isolate_thread_data_key_, per_thread);
  Thread::SetThreadLocal(isolate_key_, this);

#ifdef ENABLE_DEBUGGER_SUPPORT
  debug_ = new Debug(this);
  debugger_ = new Debugger();
  debugger_->isolate_ = this;
#endif

  memory_allocator_ = new MemoryAllocator();
  memory_allocator_->isolate_ = this;
  code_range_ = new CodeRange();
  code_range_->isolate_ = this;

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

  string_tracker_ = new StringTracker();
  string_tracker_->isolate_ = this;
  thread_manager_ = new ThreadManager();
  thread_manager_->isolate_ = this;
  compilation_cache_ = new CompilationCache();
  transcendental_cache_ = new TranscendentalCache();
  keyed_lookup_cache_ = new KeyedLookupCache();
  context_slot_cache_ = new ContextSlotCache();
  descriptor_lookup_cache_ = new DescriptorLookupCache();
  scanner_character_classes_ = new ScannerCharacterClasses();
  write_input_buffer_ = new StringInputBuffer();
  global_handles_ = new GlobalHandles();
  bootstrapper_ = new Bootstrapper();
  cpu_features_ = new CpuFeatures();
  handle_scope_implementer_ = new HandleScopeImplementer();
  stub_cache_ = new StubCache();
  ast_sentinels_ = new AstSentinels();
  inline_runtime_functions_table_ = new InlineRuntimeFunctionsTable();
  regexp_stack_ = new RegExpStack();
  regexp_stack_->isolate_ = this;

#ifdef ENABLE_DEBUGGER_SUPPORT
  debugger_ = new Debugger();
  debugger_->isolate_ = this;
#endif

#ifdef ENABLE_LOGGING_AND_PROFILING
  producer_heap_profile_ = new ProducerHeapProfile();
  producer_heap_profile_->isolate_ = this;
#endif

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
  ASSERT(default_isolate_ == this);
  ASSERT(state_ != INITIALIZED);

  bool create_heap_objects = des == NULL;

#ifdef DEBUG
  // The initialization process does not handle memory exhaustion.
  DisallowAllocationFailure disallow_allocation_failure;
#endif

  if (state_ == UNINITIALIZED && !PreInit()) return false;

  // Enable logging before setting up the heap
  logger_->Setup();

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
  builtins_.Setup(create_heap_objects);

  InitializeThreadLocal();

  // Only preallocate on the first initialization.
  if (FLAG_preallocate_message_memory && preallocated_message_space_ == NULL) {
    // Start the thread which will set aside some memory.
    PreallocatedMemoryThreadStart();
    preallocated_message_space_ =
        new NoAllocationStringAllocator(
            preallocated_memory_thread_->data(),
            preallocated_memory_thread_->length());
    PreallocatedStorageInit(preallocated_memory_thread_->length() / 4);
  }

  if (FLAG_preemption) {
    v8::Locker locker;
    v8::Locker::StartPreemption(100);
  }

#ifdef ENABLE_DEBUGGER_SUPPORT
  debug_->Setup(create_heap_objects);
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
