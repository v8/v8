// Copyright 2011 the V8 project authors. All rights reserved.
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
#include "deoptimizer.h"
#include "heap-profiler.h"
#include "hydrogen.h"
#include "isolate.h"
#include "lithium-allocator.h"
#include "log.h"
#include "regexp-stack.h"
#include "runtime-profiler.h"
#include "scanner.h"
#include "scopeinfo.h"
#include "serialize.h"
#include "simulator.h"
#include "spaces.h"
#include "stub-cache.h"
#include "version.h"


namespace v8 {
namespace internal {

Atomic32 ThreadId::highest_thread_id_ = 0;

int ThreadId::AllocateThreadId() {
  int new_id = NoBarrier_AtomicIncrement(&highest_thread_id_, 1);
  return new_id;
}

int ThreadId::GetCurrentThreadId() {
  int thread_id = Thread::GetThreadLocalInt(Isolate::thread_id_key_);
  if (thread_id == 0) {
    thread_id = AllocateThreadId();
    Thread::SetThreadLocalInt(Isolate::thread_id_key_, thread_id);
  }
  return thread_id;
}


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
      : Thread(isolate, "v8:PreallocMem"),
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


void Isolate::PreallocatedStorageInit(size_t size) {
  ASSERT(free_list_.next_ == &free_list_);
  ASSERT(free_list_.previous_ == &free_list_);
  PreallocatedStorage* free_chunk =
      reinterpret_cast<PreallocatedStorage*>(new char[size]);
  free_list_.next_ = free_list_.previous_ = free_chunk;
  free_chunk->next_ = free_chunk->previous_ = &free_list_;
  free_chunk->size_ = size - sizeof(PreallocatedStorage);
  preallocated_storage_preallocated_ = true;
}


void* Isolate::PreallocatedStorageNew(size_t size) {
  if (!preallocated_storage_preallocated_) {
    return FreeStoreAllocationPolicy::New(size);
  }
  ASSERT(free_list_.next_ != &free_list_);
  ASSERT(free_list_.previous_ != &free_list_);

  size = (size + kPointerSize - 1) & ~(kPointerSize - 1);
  // Search for exact fit.
  for (PreallocatedStorage* storage = free_list_.next_;
       storage != &free_list_;
       storage = storage->next_) {
    if (storage->size_ == size) {
      storage->Unlink();
      storage->LinkTo(&in_use_list_);
      return reinterpret_cast<void*>(storage + 1);
    }
  }
  // Search for first fit.
  for (PreallocatedStorage* storage = free_list_.next_;
       storage != &free_list_;
       storage = storage->next_) {
    if (storage->size_ >= size + sizeof(PreallocatedStorage)) {
      storage->Unlink();
      storage->LinkTo(&in_use_list_);
      PreallocatedStorage* left_over =
          reinterpret_cast<PreallocatedStorage*>(
              reinterpret_cast<char*>(storage + 1) + size);
      left_over->size_ = storage->size_ - size - sizeof(PreallocatedStorage);
      ASSERT(size + left_over->size_ + sizeof(PreallocatedStorage) ==
             storage->size_);
      storage->size_ = size;
      left_over->LinkTo(&free_list_);
      return reinterpret_cast<void*>(storage + 1);
    }
  }
  // Allocation failure.
  ASSERT(false);
  return NULL;
}


// We don't attempt to coalesce.
void Isolate::PreallocatedStorageDelete(void* p) {
  if (p == NULL) {
    return;
  }
  if (!preallocated_storage_preallocated_) {
    FreeStoreAllocationPolicy::Delete(p);
    return;
  }
  PreallocatedStorage* storage = reinterpret_cast<PreallocatedStorage*>(p) - 1;
  ASSERT(storage->next_->previous_ == storage);
  ASSERT(storage->previous_->next_ == storage);
  storage->Unlink();
  storage->LinkTo(&free_list_);
}


Isolate* Isolate::default_isolate_ = NULL;
Thread::LocalStorageKey Isolate::isolate_key_;
Thread::LocalStorageKey Isolate::thread_id_key_;
Thread::LocalStorageKey Isolate::per_isolate_thread_data_key_;
Mutex* Isolate::process_wide_mutex_ = OS::CreateMutex();
Isolate::ThreadDataTable* Isolate::thread_data_table_ = NULL;


class IsolateInitializer {
 public:
  IsolateInitializer() {
    Isolate::EnsureDefaultIsolate();
  }
};

static IsolateInitializer* EnsureDefaultIsolateAllocated() {
  // TODO(isolates): Use the system threading API to do this once?
  static IsolateInitializer static_initializer;
  return &static_initializer;
}

// This variable only needed to trigger static intialization.
static IsolateInitializer* static_initializer = EnsureDefaultIsolateAllocated();





Isolate::PerIsolateThreadData* Isolate::AllocatePerIsolateThreadData(
    ThreadId thread_id) {
  ASSERT(!thread_id.Equals(ThreadId::Invalid()));
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
  ThreadId thread_id = ThreadId::Current();
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


void Isolate::EnsureDefaultIsolate() {
  ScopedLock lock(process_wide_mutex_);
  if (default_isolate_ == NULL) {
    isolate_key_ = Thread::CreateThreadLocalKey();
    thread_id_key_ = Thread::CreateThreadLocalKey();
    per_isolate_thread_data_key_ = Thread::CreateThreadLocalKey();
    thread_data_table_ = new Isolate::ThreadDataTable();
    default_isolate_ = new Isolate();
  }
  // Can't use SetIsolateThreadLocals(default_isolate_, NULL) here
  // becase a non-null thread data may be already set.
  Thread::SetThreadLocal(isolate_key_, default_isolate_);
}


Debugger* Isolate::GetDefaultIsolateDebugger() {
  EnsureDefaultIsolate();
  return default_isolate_->debugger();
}


StackGuard* Isolate::GetDefaultIsolateStackGuard() {
  EnsureDefaultIsolate();
  return default_isolate_->stack_guard();
}


void Isolate::EnterDefaultIsolate() {
  EnsureDefaultIsolate();
  ASSERT(default_isolate_ != NULL);

  PerIsolateThreadData* data = CurrentPerIsolateThreadData();
  // If not yet in default isolate - enter it.
  if (data == NULL || data->isolate() != default_isolate_) {
    default_isolate_->Enter();
  }
}


Isolate* Isolate::GetDefaultIsolateForLocking() {
  EnsureDefaultIsolate();
  return default_isolate_;
}


Isolate::ThreadDataTable::ThreadDataTable()
    : list_(NULL) {
}


Isolate::PerIsolateThreadData*
    Isolate::ThreadDataTable::Lookup(Isolate* isolate,
                                     ThreadId thread_id) {
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


void Isolate::ThreadDataTable::Remove(Isolate* isolate,
                                      ThreadId thread_id) {
  PerIsolateThreadData* data = Lookup(isolate, thread_id);
  if (data != NULL) {
    Remove(data);
  }
}


#ifdef DEBUG
#define TRACE_ISOLATE(tag)                                              \
  do {                                                                  \
    if (FLAG_trace_isolates) {                                          \
      PrintF("Isolate %p " #tag "\n", reinterpret_cast<void*>(this));   \
    }                                                                   \
  } while (false)
#else
#define TRACE_ISOLATE(tag)
#endif


Isolate::Isolate()
    : state_(UNINITIALIZED),
      entry_stack_(NULL),
      stack_trace_nesting_level_(0),
      incomplete_message_(NULL),
      preallocated_memory_thread_(NULL),
      preallocated_message_space_(NULL),
      bootstrapper_(NULL),
      runtime_profiler_(NULL),
      compilation_cache_(NULL),
      counters_(NULL),
      code_range_(NULL),
      // Must be initialized early to allow v8::SetResourceConstraints calls.
      break_access_(OS::CreateMutex()),
      debugger_initialized_(false),
      // Must be initialized early to allow v8::Debug calls.
      debugger_access_(OS::CreateMutex()),
      logger_(NULL),
      stats_table_(NULL),
      stub_cache_(NULL),
      deoptimizer_data_(NULL),
      capture_stack_trace_for_uncaught_exceptions_(false),
      stack_trace_for_uncaught_exceptions_frame_limit_(0),
      stack_trace_for_uncaught_exceptions_options_(StackTrace::kOverview),
      transcendental_cache_(NULL),
      memory_allocator_(NULL),
      keyed_lookup_cache_(NULL),
      context_slot_cache_(NULL),
      descriptor_lookup_cache_(NULL),
      handle_scope_implementer_(NULL),
      unicode_cache_(NULL),
      in_use_list_(0),
      free_list_(0),
      preallocated_storage_preallocated_(false),
      pc_to_code_cache_(NULL),
      write_input_buffer_(NULL),
      global_handles_(NULL),
      context_switcher_(NULL),
      thread_manager_(NULL),
      ast_sentinels_(NULL),
      string_tracker_(NULL),
      regexp_stack_(NULL),
      frame_element_constant_list_(0),
      result_constant_list_(0) {
  TRACE_ISOLATE(constructor);

  memset(isolate_addresses_, 0,
      sizeof(isolate_addresses_[0]) * (k_isolate_address_count + 1));

  heap_.isolate_ = this;
  zone_.isolate_ = this;
  stack_guard_.isolate_ = this;

#if defined(V8_TARGET_ARCH_ARM) && !defined(__arm__) || \
    defined(V8_TARGET_ARCH_MIPS) && !defined(__mips__)
  simulator_initialized_ = false;
  simulator_i_cache_ = NULL;
  simulator_redirection_ = NULL;
#endif

  thread_manager_ = new ThreadManager();
  thread_manager_->isolate_ = this;

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
  TRACE_ISOLATE(tear_down);

  // Temporarily set this isolate as current so that various parts of
  // the isolate can access it in their destructors without having a
  // direct pointer. We don't use Enter/Exit here to avoid
  // initializing the thread data.
  PerIsolateThreadData* saved_data = CurrentPerIsolateThreadData();
  Isolate* saved_isolate = UncheckedCurrent();
  SetIsolateThreadLocals(this, NULL);

  Deinit();

  if (!IsDefaultIsolate()) {
    delete this;
  }

  // Restore the previous current isolate.
  SetIsolateThreadLocals(saved_isolate, saved_data);
}


void Isolate::Deinit() {
  if (state_ == INITIALIZED) {
    TRACE_ISOLATE(deinit);

    if (FLAG_hydrogen_stats) HStatistics::Instance()->Print();

    // We must stop the logger before we tear down other components.
    logger_->EnsureTickerStopped();

    delete deoptimizer_data_;
    deoptimizer_data_ = NULL;
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
    if (runtime_profiler_ != NULL) {
      runtime_profiler_->TearDown();
      delete runtime_profiler_;
      runtime_profiler_ = NULL;
    }
    heap_.TearDown();
    logger_->TearDown();

    // The default isolate is re-initializable due to legacy API.
    state_ = UNINITIALIZED;
  }
}


void Isolate::SetIsolateThreadLocals(Isolate* isolate,
                                     PerIsolateThreadData* data) {
  Thread::SetThreadLocal(isolate_key_, isolate);
  Thread::SetThreadLocal(per_isolate_thread_data_key_, data);
}


Isolate::~Isolate() {
  TRACE_ISOLATE(destructor);

#ifdef ENABLE_LOGGING_AND_PROFILING
  delete producer_heap_profile_;
  producer_heap_profile_ = NULL;
#endif

  delete unicode_cache_;
  unicode_cache_ = NULL;

  delete regexp_stack_;
  regexp_stack_ = NULL;

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

  delete handle_scope_implementer_;
  handle_scope_implementer_ = NULL;
  delete break_access_;
  break_access_ = NULL;

  delete compilation_cache_;
  compilation_cache_ = NULL;
  delete bootstrapper_;
  bootstrapper_ = NULL;
  delete pc_to_code_cache_;
  pc_to_code_cache_ = NULL;
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
}


void Isolate::InitializeThreadLocal() {
  thread_local_top_.Initialize();
  clear_pending_exception();
  clear_pending_message();
  clear_scheduled_exception();
}


void Isolate::PropagatePendingExceptionToExternalTryCatch() {
  ASSERT(has_pending_exception());

  bool external_caught = IsExternallyCaught();
  thread_local_top_.external_caught_exception_ = external_caught;

  if (!external_caught) return;

  if (thread_local_top_.pending_exception_ == Failure::OutOfMemoryException()) {
    // Do not propagate OOM exception: we should kill VM asap.
  } else if (thread_local_top_.pending_exception_ ==
             heap()->termination_exception()) {
    try_catch_handler()->can_continue_ = false;
    try_catch_handler()->exception_ = heap()->null_value();
  } else {
    // At this point all non-object (failure) exceptions have
    // been dealt with so this shouldn't fail.
    ASSERT(!pending_exception()->IsFailure());
    try_catch_handler()->can_continue_ = true;
    try_catch_handler()->exception_ = pending_exception();
    if (!thread_local_top_.pending_message_obj_->IsTheHole()) {
      try_catch_handler()->message_ = thread_local_top_.pending_message_obj_;
    }
  }
}


void Isolate::InitializeLoggingAndCounters() {
  if (logger_ == NULL) {
    logger_ = new Logger;
  }
  if (counters_ == NULL) {
    counters_ = new Counters;
  }
}


void Isolate::InitializeDebugger() {
#ifdef ENABLE_DEBUGGER_SUPPORT
  ScopedLock lock(debugger_access_);
  if (NoBarrier_Load(&debugger_initialized_)) return;
  InitializeLoggingAndCounters();
  debug_ = new Debug(this);
  debugger_ = new Debugger(this);
  Release_Store(&debugger_initialized_, true);
#endif
}


bool Isolate::Init(Deserializer* des) {
  ASSERT(state_ != INITIALIZED);
  ASSERT(Isolate::Current() == this);
  TRACE_ISOLATE(init);

#ifdef DEBUG
  // The initialization process does not handle memory exhaustion.
  DisallowAllocationFailure disallow_allocation_failure;
#endif

  InitializeLoggingAndCounters();

  InitializeDebugger();

  memory_allocator_ = new MemoryAllocator(this);
  code_range_ = new CodeRange(this);

  // Safe after setting Heap::isolate_, initializing StackGuard and
  // ensuring that Isolate::Current() == this.
  heap_.SetStackLimits();

#define C(name) isolate_addresses_[Isolate::k_##name] =                        \
    reinterpret_cast<Address>(name());
  ISOLATE_ADDRESS_LIST(C)
  ISOLATE_ADDRESS_LIST_PROF(C)
#undef C

  string_tracker_ = new StringTracker();
  string_tracker_->isolate_ = this;
  compilation_cache_ = new CompilationCache(this);
  transcendental_cache_ = new TranscendentalCache();
  keyed_lookup_cache_ = new KeyedLookupCache();
  context_slot_cache_ = new ContextSlotCache();
  descriptor_lookup_cache_ = new DescriptorLookupCache();
  unicode_cache_ = new UnicodeCache();
  pc_to_code_cache_ = new PcToCodeCache(this);
  write_input_buffer_ = new StringInputBuffer();
  global_handles_ = new GlobalHandles(this);
  bootstrapper_ = new Bootstrapper();
  handle_scope_implementer_ = new HandleScopeImplementer();
  stub_cache_ = new StubCache(this);
  ast_sentinels_ = new AstSentinels();
  regexp_stack_ = new RegExpStack();
  regexp_stack_->isolate_ = this;

#ifdef ENABLE_LOGGING_AND_PROFILING
  producer_heap_profile_ = new ProducerHeapProfile();
  producer_heap_profile_->isolate_ = this;
#endif

  // Enable logging before setting up the heap
  logger_->Setup();

  CpuProfiler::Setup();
  HeapProfiler::Setup();

  // Initialize other runtime facilities
#if defined(USE_SIMULATOR)
#if defined(V8_TARGET_ARCH_ARM) || defined(V8_TARGET_ARCH_MIPS)
  Simulator::Initialize();
#endif
#endif

  { // NOLINT
    // Ensure that the thread has a valid stack guard.  The v8::Locker object
    // will ensure this too, but we don't have to use lockers if we are only
    // using one thread.
    ExecutionAccess lock(this);
    stack_guard_.InitThread(lock);
  }

  // Setup the object heap.
  const bool create_heap_objects = (des == NULL);
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

  deoptimizer_data_ = new DeoptimizerData;
  runtime_profiler_ = new RuntimeProfiler(this);
  runtime_profiler_->Setup();

  // If we are deserializing, log non-function code objects and compiled
  // functions found in the snapshot.
  if (des != NULL && FLAG_log_code) {
    HandleScope scope;
    LOG(this, LogCodeObjects());
    LOG(this, LogCompiledFunctions());
  }

  state_ = INITIALIZED;
  return true;
}


// Initialized lazily to allow early
// v8::V8::SetAddHistogramSampleFunction calls.
StatsTable* Isolate::stats_table() {
  if (stats_table_ == NULL) {
    stats_table_ = new StatsTable;
  }
  return stats_table_;
}


void Isolate::Enter() {
  Isolate* current_isolate = NULL;
  PerIsolateThreadData* current_data = CurrentPerIsolateThreadData();
  if (current_data != NULL) {
    current_isolate = current_data->isolate_;
    ASSERT(current_isolate != NULL);
    if (current_isolate == this) {
      ASSERT(Current() == this);
      ASSERT(entry_stack_ != NULL);
      ASSERT(entry_stack_->previous_thread_data == NULL ||
             entry_stack_->previous_thread_data->thread_id().Equals(
                 ThreadId::Current()));
      // Same thread re-enters the isolate, no need to re-init anything.
      entry_stack_->entry_count++;
      return;
    }
  }

  // Threads can have default isolate set into TLS as Current but not yet have
  // PerIsolateThreadData for it, as it requires more advanced phase of the
  // initialization. For example, a thread might be the one that system used for
  // static initializers - in this case the default isolate is set in TLS but
  // the thread did not yet Enter the isolate. If PerisolateThreadData is not
  // there, use the isolate set in TLS.
  if (current_isolate == NULL) {
    current_isolate = Isolate::UncheckedCurrent();
  }

  PerIsolateThreadData* data = FindOrAllocatePerThreadDataForThisThread();
  ASSERT(data != NULL);
  ASSERT(data->isolate_ == this);

  EntryStackItem* item = new EntryStackItem(current_data,
                                            current_isolate,
                                            entry_stack_);
  entry_stack_ = item;

  SetIsolateThreadLocals(this, data);

  // In case it's the first time some thread enters the isolate.
  set_thread_id(data->thread_id());
}


void Isolate::Exit() {
  ASSERT(entry_stack_ != NULL);
  ASSERT(entry_stack_->previous_thread_data == NULL ||
         entry_stack_->previous_thread_data->thread_id().Equals(
             ThreadId::Current()));

  if (--entry_stack_->entry_count > 0) return;

  ASSERT(CurrentPerIsolateThreadData() != NULL);
  ASSERT(CurrentPerIsolateThreadData()->isolate_ == this);

  // Pop the stack.
  EntryStackItem* item = entry_stack_;
  entry_stack_ = item->previous_item;

  PerIsolateThreadData* previous_thread_data = item->previous_thread_data;
  Isolate* previous_isolate = item->previous_isolate;

  delete item;

  // Reinit the current thread for the isolate it was running before this one.
  SetIsolateThreadLocals(previous_isolate, previous_thread_data);
}


void Isolate::ResetEagerOptimizingData() {
  compilation_cache_->ResetEagerOptimizingData();
}


#ifdef DEBUG
#define ISOLATE_FIELD_OFFSET(type, name, ignored)                       \
const intptr_t Isolate::name##_debug_offset_ = OFFSET_OF(Isolate, name##_);
ISOLATE_INIT_LIST(ISOLATE_FIELD_OFFSET)
ISOLATE_INIT_ARRAY_LIST(ISOLATE_FIELD_OFFSET)
#undef ISOLATE_FIELD_OFFSET
#endif

} }  // namespace v8::internal
