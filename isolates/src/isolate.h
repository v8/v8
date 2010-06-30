// Copyright 2010 the V8 project authors. All rights reserved.
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

#ifndef V8_ISOLATE_H_
#define V8_ISOLATE_H_

#define V8_USE_TLS_FOR_GLOBAL_ISOLATE

#include "allocation.h"
#include "apiutils.h"
#include "builtins.h"
#include "contexts.h"
#include "execution.h"
#include "frames-inl.h"
#include "frames.h"
#include "global-handles.h"
#include "handles.h"
#include "heap.h"
#include "regexp-stack.h"
#include "runtime.h"
#include "zone.h"
#include "../include/v8-debug.h"

namespace v8 {
namespace internal {

class AstSentinels;
class Bootstrapper;
class CompilationCache;
class ContextSlotCache;
class ContextSwitcher;
class CodeRange;
class Counters;
class CpuFeatures;
class CpuProfiler;
class Deserializer;
class EmptyStatement;
class FunctionInfoListener;
class HandleScopeImplementer;
class HeapProfiler;
class InlineRuntimeFunctionsTable;
class NoAllocationStringAllocator;
class PreallocatedMemoryThread;
class ProducerHeapProfile;
class SaveContext;
class StubCache;
class StringInputBuffer;
class StringTracker;
class ScannerCharacterClasses;
class ThreadVisitor;  // Defined in v8threads.h
class ThreadManager;
class VMState;

typedef void* ExternalReferenceRedirector(void* original, bool fp_return);


#ifdef ENABLE_DEBUGGER_SUPPORT
class Debug;
class Debugger;
class DebuggerAgent;
#endif

#define RETURN_IF_SCHEDULED_EXCEPTION()              \
  if (Isolate::Current()->has_scheduled_exception()) \
      return Isolate::Current()->PromoteScheduledException()

#define ISOLATE_ADDRESS_LIST(C)            \
  C(handler_address)                       \
  C(c_entry_fp_address)                    \
  C(context_address)                       \
  C(pending_exception_address)             \
  C(external_caught_exception_address)

#ifdef ENABLE_LOGGING_AND_PROFILING
#define ISOLATE_ADDRESS_LIST_PROF(C)       \
  C(js_entry_sp_address)
#else
#define ISOLATE_ADDRESS_LIST_PROF(C)
#endif


class ThreadLocalTop BASE_EMBEDDED {
 public:
  // Initialize the thread data.
  void Initialize();

  // Get the top C++ try catch handler or NULL if none are registered.
  //
  // This method is not guarenteed to return an address that can be
  // used for comparison with addresses into the JS stack.  If such an
  // address is needed, use try_catch_handler_address.
  v8::TryCatch* TryCatchHandler();

  // Get the address of the top C++ try catch handler or NULL if
  // none are registered.
  //
  // This method always returns an address that can be compared to
  // pointers into the JavaScript stack.  When running on actual
  // hardware, try_catch_handler_address and TryCatchHandler return
  // the same pointer.  When running on a simulator with a separate JS
  // stack, try_catch_handler_address returns a JS stack address that
  // corresponds to the place on the JS stack where the C++ handler
  // would have been if the stack were not separate.
  inline Address try_catch_handler_address() {
    return try_catch_handler_address_;
  }

  // Set the address of the top C++ try catch handler.
  inline void set_try_catch_handler_address(Address address) {
    try_catch_handler_address_ = address;
  }

  void Free() {
    ASSERT(!has_pending_message_);
    ASSERT(!external_caught_exception_);
    ASSERT(try_catch_handler_address_ == NULL);
  }

  // The context where the current execution method is created and for variable
  // lookups.
  Context* context_;
  int thread_id_;
  Object* pending_exception_;
  bool has_pending_message_;
  const char* pending_message_;
  Object* pending_message_obj_;
  Script* pending_message_script_;
  int pending_message_start_pos_;
  int pending_message_end_pos_;
  // Use a separate value for scheduled exceptions to preserve the
  // invariants that hold about pending_exception.  We may want to
  // unify them later.
  Object* scheduled_exception_;
  bool external_caught_exception_;
  SaveContext* save_context_;
  v8::TryCatch* catcher_;

  // Stack.
  Address c_entry_fp_;  // the frame pointer of the top c entry frame
  Address handler_;   // try-blocks are chained through the stack
#ifdef ENABLE_LOGGING_AND_PROFILING
  Address js_entry_sp_;  // the stack pointer of the bottom js entry frame
#endif
  bool stack_is_cooked_;
  inline bool stack_is_cooked() { return stack_is_cooked_; }
  inline void set_stack_is_cooked(bool value) { stack_is_cooked_ = value; }

  // Generated code scratch locations.
  int32_t formal_count_;

  // Call back function to report unsafe JS accesses.
  v8::FailedAccessCheckCallback failed_access_check_callback_;

 private:
  Address try_catch_handler_address_;
};

#if defined(V8_TARGET_ARCH_ARM)

#define ISOLATE_PLATFORM_INIT_LIST(V)                                          \
  /* VirtualFrame::SpilledScope state */                                       \
  V(bool, is_virtual_frame_in_spilled_scope, false)

#else

#define ISOLATE_PLATFORM_INIT_LIST(V)

#endif

#ifdef ENABLE_DEBUGGER_SUPPORT

#define ISOLATE_DEBUGGER_INIT_LIST(V)                                          \
  V(v8::Debug::EventCallback, debug_event_callback, NULL)                      \
  V(DebuggerAgent*, debugger_agent_instance, NULL)
#else

#define ISOLATE_DEBUGGER_INIT_LIST(V)

#endif

#ifdef DEBUG

#define ISOLATE_INIT_DEBUG_ARRAY_LIST(V)                                       \
  V(CommentStatistic, paged_space_comments_statistics,                         \
      CommentStatistic::kMaxComments + 1)
#else

#define ISOLATE_INIT_DEBUG_ARRAY_LIST(V)

#endif

#ifdef ENABLE_LOGGING_AND_PROFILING

#define ISOLATE_LOGGING_INIT_LIST(V)                                           \
  V(CpuProfiler*, cpu_profiler, NULL)                                          \
  V(HeapProfiler*, heap_profiler, NULL)

#else

#define ISOLATE_LOGGING_INIT_LIST(V)

#endif

#define ISOLATE_INIT_ARRAY_LIST(V)                                             \
  /* SerializerDeserializer state. */                                          \
  V(Object*, serialize_partial_snapshot_cache, kPartialSnapshotCacheCapacity)  \
  V(int, jsregexp_static_offsets_vector, kJSRegexpStaticOffsetsVectorSize)     \
  ISOLATE_INIT_DEBUG_ARRAY_LIST(V)

typedef List<HeapObject*, PreallocatedStorage> DebugObjectCache;

#define ISOLATE_INIT_LIST(V)                                                   \
  /* AssertNoZoneAllocation state. */                                          \
  V(bool, zone_allow_allocation, true)                                         \
  /* SerializerDeserializer state. */                                          \
  V(int, serialize_partial_snapshot_cache_length, 0)                           \
  /* Assembler state. */                                                       \
  /* A previously allocated buffer of kMinimalBufferSize bytes, or NULL. */    \
  V(byte*, assembler_spare_buffer, NULL)                                       \
  /*This static counter ensures that NativeAllocationCheckers can be nested.*/ \
  V(int, allocation_disallowed, 0)                                             \
  V(FatalErrorCallback, exception_behavior, NULL)                              \
  V(v8::Debug::MessageHandler, message_handler, NULL)                          \
  /* To distinguish the function templates, so that we can find them in the */ \
  /* function cache of the global context. */                                  \
  V(int, next_serial_number, 0)                                                \
  V(ExternalReferenceRedirector*, external_reference_redirector, NULL)         \
  V(bool, always_allow_natives_syntax, false)                                  \
  /* A stack of VM states. */                                                  \
  V(VMState*, vm_state, NULL)                                                  \
  /* Part of the state of liveedit. */                                         \
  V(FunctionInfoListener*, active_function_info_listener, NULL)                \
  /* State for Relocatable. */                                                 \
  V(Relocatable*, relocatable_top, NULL)                                       \
  /* State for CodeEntry in profile-generator. */                              \
  V(unsigned, code_entry_next_call_uid, NULL)                                  \
  V(DebugObjectCache*, string_stream_debug_object_cache, NULL)                 \
  V(Object*, string_stream_current_security_token, NULL)                       \
  /* TODO(isolates): Release this on destruction? */                           \
  V(int*, irregexp_interpreter_backtrack_stack_cache, NULL)                    \
  ISOLATE_PLATFORM_INIT_LIST(V)                                                \
  ISOLATE_LOGGING_INIT_LIST(V)                                                 \
  ISOLATE_DEBUGGER_INIT_LIST(V)

class Isolate {
 public:
  ~Isolate();

  enum AddressId {
#define C(name) k_##name,
    ISOLATE_ADDRESS_LIST(C)
    ISOLATE_ADDRESS_LIST_PROF(C)
#undef C
    k_isolate_address_count
  };

  // Returns the single global isolate.
  static Isolate* Current() {
#ifdef V8_USE_TLS_FOR_GLOBAL_ISOLATE
    Isolate* isolate = reinterpret_cast<Isolate*>(
        Thread::GetThreadLocal(global_isolate_key_));
    if (isolate == NULL) {
      isolate = InitThreadForGlobalIsolate();
      ASSERT(isolate != NULL);
    }
    return isolate;
#else
    ASSERT(global_isolate_ != NULL);
    return global_isolate_;
#endif
  }

#ifdef V8_USE_TLS_FOR_GLOBAL_ISOLATE
  static Isolate* InitThreadForGlobalIsolate();
#endif

  // Destroy the global isolate.
  static void TearDown();

  // Destroy the global isolate and create a new one in its place.
  static void TearDownAndRecreateGlobalIsolate();

  // Creates a new isolate (perhaps using a deserializer). Returns null
  // on failure.
  static Isolate* Create(Deserializer* des);

  // Debug.
  // Mutex for serializing access to break control structures.
  Mutex* break_access() { return break_access_; }

  Address get_address_from_id(AddressId id);

  // Access to top context (where the current function object was created).
  Context* context() { return thread_local_top_.context_; }
  void set_context(Context* context) {
    thread_local_top_.context_ = context;
  }
  Context** context_address() { return &thread_local_top_.context_; }

  SaveContext* save_context() {return thread_local_top_.save_context_; }
  void set_save_context(SaveContext* save) {
    thread_local_top_.save_context_ = save;
  }

  // Access to current thread id.
  int thread_id() { return thread_local_top_.thread_id_; }
  void set_thread_id(int id) { thread_local_top_.thread_id_ = id; }

  // Interface to pending exception.
  Object* pending_exception() {
    ASSERT(has_pending_exception());
    return thread_local_top_.pending_exception_;
  }
  bool external_caught_exception() {
    return thread_local_top_.external_caught_exception_;
  }
  void set_pending_exception(Object* exception) {
    thread_local_top_.pending_exception_ = exception;
  }
  void clear_pending_exception() {
    thread_local_top_.pending_exception_ = heap_.the_hole_value();
  }
  Object** pending_exception_address() {
    return &thread_local_top_.pending_exception_;
  }
  bool has_pending_exception() {
    return !thread_local_top_.pending_exception_->IsTheHole();
  }
  void clear_pending_message() {
    thread_local_top_.has_pending_message_ = false;
    thread_local_top_.pending_message_ = NULL;
    thread_local_top_.pending_message_obj_ = heap_.the_hole_value();
    thread_local_top_.pending_message_script_ = NULL;
  }
  v8::TryCatch* try_catch_handler() {
    return thread_local_top_.TryCatchHandler();
  }
  Address try_catch_handler_address() {
    return thread_local_top_.try_catch_handler_address();
  }
  bool* external_caught_exception_address() {
    return &thread_local_top_.external_caught_exception_;
  }

  Object** scheduled_exception_address() {
    return &thread_local_top_.scheduled_exception_;
  }
  Object* scheduled_exception() {
    ASSERT(has_scheduled_exception());
    return thread_local_top_.scheduled_exception_;
  }
  bool has_scheduled_exception() {
    return !thread_local_top_.scheduled_exception_->IsTheHole();
  }
  void clear_scheduled_exception() {
    thread_local_top_.scheduled_exception_ = heap_.the_hole_value();
  }

  void setup_external_caught() {
    thread_local_top_.external_caught_exception_ =
        has_pending_exception() &&
        (thread_local_top_.catcher_ != NULL) &&
        (try_catch_handler() == thread_local_top_.catcher_);
  }

  // JS execution stack (see frames.h).
  static Address c_entry_fp(ThreadLocalTop* thread) {
    return thread->c_entry_fp_;
  }
  static Address handler(ThreadLocalTop* thread) { return thread->handler_; }

  inline Address* c_entry_fp_address() {
    return &thread_local_top_.c_entry_fp_;
  }
  inline Address* handler_address() { return &thread_local_top_.handler_; }

#ifdef ENABLE_LOGGING_AND_PROFILING
  // Bottom JS entry (see StackTracer::Trace in log.cc).
  static Address js_entry_sp(ThreadLocalTop* thread) {
    return thread->js_entry_sp_;
  }
  inline Address* js_entry_sp_address() {
    return &thread_local_top_.js_entry_sp_;
  }
#endif

  // Generated code scratch locations.
  void* formal_count_address() { return &thread_local_top_.formal_count_; }

  // Returns the global object of the current context. It could be
  // a builtin object, or a js global object.
  Handle<GlobalObject> global() {
    return Handle<GlobalObject>(context()->global());
  }

  // Returns the global proxy object of the current context.
  Object* global_proxy() {
    return context()->global_proxy();
  }

  Handle<JSBuiltinsObject> js_builtins_object() {
    return Handle<JSBuiltinsObject>(thread_local_top_.context_->builtins());
  }

  static int ArchiveSpacePerThread() { return sizeof(ThreadLocalTop); }
  void FreeThreadResources() { thread_local_top_.Free(); }

  // This method is called by the api after operations that may throw
  // exceptions.  If an exception was thrown and not handled by an external
  // handler the exception is scheduled to be rethrown when we return to running
  // JavaScript code.  If an exception is scheduled true is returned.
  bool OptionalRescheduleException(bool is_bottom_call);


  // Tells whether the current context has experienced an out of memory
  // exception.
  bool is_out_of_memory();


  void MarkCompactPrologue(bool is_compacting);
  void MarkCompactEpilogue(bool is_compacting);
  void MarkCompactPrologue(bool is_compacting,
                           char* archived_thread_data);
  void MarkCompactEpilogue(bool is_compacting,
                           char* archived_thread_data);
  void PrintCurrentStackTrace(FILE* out);
  void PrintStackTrace(FILE* out, char* thread_data);
  void PrintStack(StringStream* accumulator);
  void PrintStack();
  Handle<String> StackTraceString();
  Local<StackTrace> CaptureCurrentStackTrace(
      int frame_limit,
      StackTrace::StackTraceOptions options);

  // Returns if the top context may access the given global object. If
  // the result is false, the pending exception is guaranteed to be
  // set.
  bool MayNamedAccess(JSObject* receiver,
                      Object* key,
                      v8::AccessType type);
  bool MayIndexedAccess(JSObject* receiver,
                        uint32_t index,
                        v8::AccessType type);

  void SetFailedAccessCheckCallback(v8::FailedAccessCheckCallback callback);
  void ReportFailedAccessCheck(JSObject* receiver, v8::AccessType type);

  // Exception throwing support. The caller should use the result
  // of Throw() as its return value.
  Failure* Throw(Object* exception, MessageLocation* location = NULL);
  // Re-throw an exception.  This involves no error reporting since
  // error reporting was handled when the exception was thrown
  // originally.
  Failure* ReThrow(Object* exception, MessageLocation* location = NULL);
  void ScheduleThrow(Object* exception);
  void ReportPendingMessages();
  Failure* ThrowIllegalOperation();

  // Promote a scheduled exception to pending. Asserts has_scheduled_exception.
  Object* PromoteScheduledException();
  void DoThrow(Object* exception,
               MessageLocation* location,
               const char* message);
  bool ShouldReturnException(bool* is_caught_externally,
                             bool catchable_by_javascript);
  void ReportUncaughtException(Handle<Object> exception,
                               MessageLocation* location,
                               Handle<String> stack_trace);

  // Attempts to compute the current source location, storing the
  // result in the target out parameter.
  void ComputeLocation(MessageLocation* target);

  // Override command line flag.
  void TraceException(bool flag);

  // Out of resource exception helpers.
  Failure* StackOverflow();
  Failure* TerminateExecution();

  // Administration
  void Iterate(ObjectVisitor* v);
  void Iterate(ObjectVisitor* v, ThreadLocalTop* t);
  char* Iterate(ObjectVisitor* v, char* t);
  void IterateThread(ThreadVisitor* v);
  void IterateThread(ThreadVisitor* v, char* t);


  // Returns the current global context.
  Handle<Context> global_context();

  // Returns the global context of the calling JavaScript code.  That
  // is, the global context of the top-most JavaScript frame.
  Handle<Context> GetCallingGlobalContext();

  void RegisterTryCatchHandler(v8::TryCatch* that);
  void UnregisterTryCatchHandler(v8::TryCatch* that);

  char* ArchiveThread(char* to);
  char* RestoreThread(char* from);

  static const char* kStackOverflowMessage;

  // Accessors.
#define GLOBAL_ACCESSOR(type, name, initialvalue)                              \
  type name() const { return name##_; }                                        \
  void set_##name(type value) { name##_ = value; }
  ISOLATE_INIT_LIST(GLOBAL_ACCESSOR)
#undef GLOBAL_ACCESSOR

#define GLOBAL_ARRAY_ACCESSOR(type, name, length)                              \
  type* name() { return &(name##_[0]); }
  ISOLATE_INIT_ARRAY_LIST(GLOBAL_ARRAY_ACCESSOR)
#undef GLOBAL_ARRAY_ACCESSOR

#define GLOBAL_CONTEXT_FIELD_ACCESSOR(index, type, name)      \
  Handle<type> name() {                                       \
    return Handle<type>(context()->global_context()->name()); \
  }
  GLOBAL_CONTEXT_FIELDS(GLOBAL_CONTEXT_FIELD_ACCESSOR)
#undef GLOBAL_CONTEXT_FIELD_ACCESSOR

  Bootstrapper* bootstrapper() { return bootstrapper_; }
  Counters* counters() { return counters_; }
  CpuFeatures* cpu_features() { return cpu_features_; }
  CodeRange* code_range() { return code_range_; }
  CompilationCache* compilation_cache() { return compilation_cache_; }
  Logger* logger() { return logger_; }
  StackGuard* stack_guard() { return &stack_guard_; }
  Heap* heap() { return &heap_; }
  StatsTable* stats_table() { return stats_table_; }
  StubCache* stub_cache() { return stub_cache_; }
  ThreadLocalTop* thread_local_top() { return &thread_local_top_; }

  TranscendentalCache* transcendental_cache() const {
    return transcendental_cache_;
  }

  MemoryAllocator* memory_allocator() {
    return memory_allocator_;
  }

  KeyedLookupCache* keyed_lookup_cache() {
    return keyed_lookup_cache_;
  }

  ContextSlotCache* context_slot_cache() {
    return context_slot_cache_;
  }

  DescriptorLookupCache* descriptor_lookup_cache() {
    return descriptor_lookup_cache_;
  }

  v8::ImplementationUtilities::HandleScopeData* handle_scope_data() {
    return &handle_scope_data_;
  }
  HandleScopeImplementer* handle_scope_implementer() {
    ASSERT(handle_scope_implementer_);
    return handle_scope_implementer_;
  }
  Zone* zone() { return &zone_; }

  ScannerCharacterClasses* scanner_character_classes() {
    return scanner_character_classes_;
  }

  StringInputBuffer* write_input_buffer() { return write_input_buffer_; }

  GlobalHandles* global_handles() { return global_handles_; }

  ThreadManager* thread_manager() { return thread_manager_; }

  ContextSwitcher* context_switcher() { return context_switcher_; }

  void set_context_switcher(ContextSwitcher* switcher) {
    context_switcher_ = switcher;
  }

  StringTracker* string_tracker() { return string_tracker_; }

  unibrow::Mapping<unibrow::Ecma262UnCanonicalize>* jsregexp_uncanonicalize() {
    return &jsregexp_uncanonicalize_;
  }

  unibrow::Mapping<unibrow::CanonicalizationRange>* jsregexp_canonrange() {
    return &jsregexp_canonrange_;
  }

  StringInputBuffer* objects_string_compare_buffer_a() {
    return &objects_string_compare_buffer_a_;
  }

  StringInputBuffer* objects_string_compare_buffer_b() {
    return &objects_string_compare_buffer_b_;
  }

  StaticResource<StringInputBuffer>* objects_string_input_buffer() {
    return &objects_string_input_buffer_;
  }

  AstSentinels* ast_sentinels() { return ast_sentinels_; }

  InlineRuntimeFunctionsTable* inline_runtime_functions_table() {
    return inline_runtime_functions_table_;
  }

  RuntimeState* runtime_state() { return &runtime_state_; }

  StringInputBuffer* liveedit_compare_substrings_buf1() {
    return &liveedit_compare_substrings_buf1_;
  }

  StringInputBuffer* liveedit_compare_substrings_buf2() {
    return &liveedit_compare_substrings_buf2_;
  }

  StaticResource<SafeStringInputBuffer>* compiler_safe_string_input_buffer() {
    return &compiler_safe_string_input_buffer_;
  }

  Builtins* builtins() { return &builtins_; }

  unibrow::Mapping<unibrow::Ecma262Canonicalize>*
      regexp_macro_assembler_canonicalize() {
    return &regexp_macro_assembler_canonicalize_;
  }

  unibrow::Mapping<unibrow::Ecma262Canonicalize>*
      interp_canonicalize_mapping() {
    return &interp_canonicalize_mapping_;
  }

  void* PreallocatedStorageNew(size_t size);
  void PreallocatedStorageDelete(void* p);
  void PreallocatedStorageInit(size_t size);

#ifdef ENABLE_DEBUGGER_SUPPORT
  Debugger* debugger() { return debugger_; }
  Debug* debug() { return debug_; }
#endif

#ifdef ENABLE_LOGGING_AND_PROFILING
  ProducerHeapProfile* producer_heap_profile() {
    return producer_heap_profile_;
  }
#endif

#ifdef DEBUG
  HistogramInfo* heap_histograms() { return heap_histograms_; }

  JSObject::SpillInformation* js_spill_information() {
    return &js_spill_information_;
  }

  int* code_kind_statistics() { return code_kind_statistics_; }
#endif

  bool IsDefaultIsolate() { return this == global_isolate_; }

  // SerializerDeserializer state.
  static const int kPartialSnapshotCacheCapacity = 1300;

  static const int kJSRegexpStaticOffsetsVectorSize = 50;

  static int number_of_isolates() { return number_of_isolates_; }

  // Initialize process-wide state. Generally called from a static initializer,
  // but may be called manually if necessary. Not thread-safe; should ideally
  // be called by the same thread responsible for static initialization.
  static void InitOnce();

 private:
  Isolate();

#ifdef V8_USE_TLS_FOR_GLOBAL_ISOLATE
  static Thread::LocalStorageKey global_isolate_key_;
#endif
  static Isolate* global_isolate_;

  // TODO(isolates): Access to this global counter should be serialized.
  static int number_of_isolates_;

  bool PreInit();

  bool Init(Deserializer* des);

  enum State {
    UNINITIALIZED,    // Some components may not have been allocated.
    PREINITIALIZED,   // Components have been allocated but not initialized.
    INITIALIZED       // All components are fully initialized.
  };

  State state_;

  void PreallocatedMemoryThreadStart();
  void PreallocatedMemoryThreadStop();
  void InitializeThreadLocal();

  void PrintStackTrace(FILE* out, ThreadLocalTop* thread);
  void MarkCompactPrologue(bool is_compacting,
                           ThreadLocalTop* archived_thread_data);
  void MarkCompactEpilogue(bool is_compacting,
                           ThreadLocalTop* archived_thread_data);

  void FillCache();

  int stack_trace_nesting_level_;
  StringStream* incomplete_message_;
  // The preallocated memory thread singleton.
  PreallocatedMemoryThread* preallocated_memory_thread_;
  Address isolate_addresses_[k_isolate_address_count + 1];  // NOLINT
  NoAllocationStringAllocator* preallocated_message_space_;

  Bootstrapper* bootstrapper_;
  CompilationCache* compilation_cache_;
  Counters* counters_;
  CpuFeatures* cpu_features_;
  CodeRange* code_range_;
  Mutex* break_access_;
  Heap heap_;
  Logger* logger_;
  StackGuard stack_guard_;
  StatsTable* stats_table_;
  StubCache* stub_cache_;
  ThreadLocalTop thread_local_top_;
  TranscendentalCache* transcendental_cache_;
  MemoryAllocator* memory_allocator_;
  KeyedLookupCache* keyed_lookup_cache_;
  ContextSlotCache* context_slot_cache_;
  DescriptorLookupCache* descriptor_lookup_cache_;
  v8::ImplementationUtilities::HandleScopeData handle_scope_data_;
  HandleScopeImplementer* handle_scope_implementer_;
  ScannerCharacterClasses* scanner_character_classes_;
  Zone zone_;
  PreallocatedStorage in_use_list_;
  PreallocatedStorage free_list_;
  bool preallocated_storage_preallocated_;
  StringInputBuffer* write_input_buffer_;
  GlobalHandles* global_handles_;
  ContextSwitcher* context_switcher_;
  ThreadManager* thread_manager_;
  AstSentinels* ast_sentinels_;
  InlineRuntimeFunctionsTable* inline_runtime_functions_table_;
  RuntimeState runtime_state_;
  StringInputBuffer liveedit_compare_substrings_buf1_;
  StringInputBuffer liveedit_compare_substrings_buf2_;
  StaticResource<SafeStringInputBuffer> compiler_safe_string_input_buffer_;
  Builtins builtins_;
  StringTracker* string_tracker_;
  unibrow::Mapping<unibrow::Ecma262UnCanonicalize> jsregexp_uncanonicalize_;
  unibrow::Mapping<unibrow::CanonicalizationRange> jsregexp_canonrange_;
  StringInputBuffer objects_string_compare_buffer_a_;
  StringInputBuffer objects_string_compare_buffer_b_;
  StaticResource<StringInputBuffer> objects_string_input_buffer_;
  unibrow::Mapping<unibrow::Ecma262Canonicalize>
      regexp_macro_assembler_canonicalize_;
  unibrow::Mapping<unibrow::Ecma262Canonicalize> interp_canonicalize_mapping_;

#ifdef DEBUG
  // A static array of histogram info for each type.
  HistogramInfo heap_histograms_[LAST_TYPE + 1];
  JSObject::SpillInformation js_spill_information_;
  int code_kind_statistics_[Code::NUMBER_OF_KINDS];
#endif

#ifdef ENABLE_DEBUGGER_SUPPORT
  Debugger* debugger_;
  Debug* debug_;
#endif

#ifdef ENABLE_LOGGING_AND_PROFILING
  ProducerHeapProfile* producer_heap_profile_;
#endif

#define GLOBAL_BACKING_STORE(type, name, initialvalue)                         \
  type name##_;
  ISOLATE_INIT_LIST(GLOBAL_BACKING_STORE)
#undef GLOBAL_BACKING_STORE

#define GLOBAL_ARRAY_BACKING_STORE(type, name, length)                         \
  type name##_[length];
  ISOLATE_INIT_ARRAY_LIST(GLOBAL_ARRAY_BACKING_STORE)
#undef GLOBAL_ARRAY_BACKING_STORE

  friend class IsolateInitializer;

  DISALLOW_COPY_AND_ASSIGN(Isolate);
};


// If the GCC version is 4.1.x or 4.2.x an additional field is added to the
// class as a work around for a bug in the generated code found with these
// versions of GCC. See V8 issue 122 for details.
class SaveContext BASE_EMBEDDED {
 public:
  SaveContext()
      : context_(Isolate::Current()->context()),
#if __GNUC_VERSION__ >= 40100 && __GNUC_VERSION__ < 40300
        dummy_(Isolate::Current()->context()),
#endif
        prev_(Isolate::Current()->save_context()) {
    Isolate::Current()->set_save_context(this);

    // If there is no JS frame under the current C frame, use the value 0.
    JavaScriptFrameIterator it;
    js_sp_ = it.done() ? 0 : it.frame()->sp();
  }

  ~SaveContext() {
    Isolate::Current()->set_context(*context_);
    Isolate::Current()->set_save_context(prev_);
  }

  Handle<Context> context() { return context_; }
  SaveContext* prev() { return prev_; }

  // Returns true if this save context is below a given JavaScript frame.
  bool below(JavaScriptFrame* frame) {
    return (js_sp_ == 0) || (frame->sp() < js_sp_);
  }

 private:
  Handle<Context> context_;
#if __GNUC_VERSION__ >= 40100 && __GNUC_VERSION__ < 40300
  Handle<Context> dummy_;
#endif
  SaveContext* prev_;
  Address js_sp_;  // The top JS frame's sp when saving context.
};


class AssertNoContextChange BASE_EMBEDDED {
#ifdef DEBUG
 public:
  AssertNoContextChange() :
      context_(Isolate::Current()->context()) {
  }

  ~AssertNoContextChange() {
    ASSERT(Isolate::Current()->context() == *context_);
  }

 private:
  HandleScope scope_;
  Handle<Context> context_;
#else
 public:
  AssertNoContextChange() { }
#endif
};


class ExecutionAccess BASE_EMBEDDED {
 public:
  ExecutionAccess();
  ~ExecutionAccess();
};


// Support for checking for stack-overflows in C++ code.
class StackLimitCheck BASE_EMBEDDED {
 public:
  bool HasOverflowed() const {
    StackGuard* stack_guard = Isolate::Current()->stack_guard();
    // Stack has overflowed in C++ code only if stack pointer exceeds the C++
    // stack guard and the limits are not set to interrupt values.
    // TODO(214): Stack overflows are ignored if a interrupt is pending. This
    // code should probably always use the initial C++ limit.
    return (reinterpret_cast<uintptr_t>(this) < stack_guard->climit()) &&
           stack_guard->IsStackOverflow();
  }
};


// Support for temporarily postponing interrupts. When the outermost
// postpone scope is left the interrupts will be re-enabled and any
// interrupts that occurred while in the scope will be taken into
// account.
class PostponeInterruptsScope BASE_EMBEDDED {
 public:
  PostponeInterruptsScope() {
    StackGuard* stack_guard = Isolate::Current()->stack_guard();
    stack_guard->thread_local_.postpone_interrupts_nesting_++;
    stack_guard->DisableInterrupts();
  }

  ~PostponeInterruptsScope() {
    StackGuard* stack_guard = Isolate::Current()->stack_guard();
    if (--stack_guard->thread_local_.postpone_interrupts_nesting_ == 0) {
      stack_guard->EnableInterrupts();
    }
  }
};


// Temporary macros for accessing fields off the global isolate. Define these
// when reformatting code would become burdensome.
#define HEAP (v8::internal::Isolate::Current()->heap())
#define ZONE (v8::internal::Isolate::Current()->zone())
#define LOGGER (v8::internal::Isolate::Current()->logger())


// Tells whether the global context is marked with out of memory.
inline bool Context::has_out_of_memory() {
  return global_context()->out_of_memory() == HEAP->true_value();
}


// Mark the global context with out of memory.
inline void Context::mark_out_of_memory() {
  global_context()->set_out_of_memory(HEAP->true_value());
}


// Temporary macro to be used to flag definitions that are indeed static
// and not per-isolate. (It would be great to be able to grep for [static]!)
#define RLYSTC static


// Temporary macro to be used to flag classes that should be static.
#define STATIC_CLASS class


// Temporary macro to be used to flag classes that are completely converted
// to be isolate-friendly. Their mix of static/nonstatic methods/fields is
// correct.
#define ISOLATED_CLASS class

} }  // namespace v8::internal

#include "allocation-inl.h"

#endif  // V8_ISOLATE_H_
