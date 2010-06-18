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

// #define V8_USE_TLS_FOR_GLOBAL_ISOLATE

#include "apiutils.h"
#include "heap.h"
#include "execution.h"
#include "zone.h"

namespace v8 {
namespace internal {

class Bootstrapper;
class CompilationCache;
class ContextSlotCache;
class CpuFeatures;
class Deserializer;
class HandleScopeImplementer;
class SaveContext;
class StubCache;
class ScannerCharacterClasses;

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

#define ISOLATE_INIT_ARRAY_LIST(V)                                             \
  /* SerializerDeserializer state. */                                          \
  V(Object*, serialize_partial_snapshot_cache, kPartialSnapshotCacheCapacity)

#define ISOLATE_INIT_LIST(V)                                                   \
  /* AssertNoZoneAllocation state. */                                          \
  V(bool, zone_allow_allocation, true)                                         \
  /* SerializerDeserializer state. */                                          \
  V(int, serialize_partial_snapshot_cache_length, 0)                           \
  /* Assembler state. */                                                       \
  /* A previously allocated buffer of kMinimalBufferSize bytes, or NULL. */    \
  V(byte*, assembler_spare_buffer, NULL)                                       \
  ISOLATE_PLATFORM_INIT_LIST(V)

class Isolate {
 public:
  ~Isolate();

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

  // Creates a new isolate (perhaps using a deserializer). Returns null
  // on failure.
  static Isolate* Create(Deserializer* des);

#define GLOBAL_ACCESSOR(type, name, initialvalue)                              \
  type name() const { return name##_; }                                        \
  void set_##name(type value) { name##_ = value; }
  ISOLATE_INIT_LIST(GLOBAL_ACCESSOR)
#undef GLOBAL_ACCESSOR

#define GLOBAL_ARRAY_ACCESSOR(type, name, length)                              \
  type* name() { return &(name##_[0]); }
  ISOLATE_INIT_ARRAY_LIST(GLOBAL_ARRAY_ACCESSOR)
#undef GLOBAL_ARRAY_ACCESSOR

  // Debug.
  // Mutex for serializing access to break control structures.
  Mutex* break_access() { return break_access_; }

  // Accessors.
  Bootstrapper* bootstrapper() { return bootstrapper_; }
  CpuFeatures* cpu_features() { return cpu_features_; }
  CompilationCache* compilation_cache() { return compilation_cache_; }
  StackGuard* stack_guard() { return &stack_guard_; }
  Heap* heap() { return &heap_; }
  StubCache* stub_cache() { return stub_cache_; }
  ThreadLocalTop* thread_local_top() { return &thread_local_top_; }

  TranscendentalCache* transcendental_cache() const {
    return transcendental_cache_;
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

  // SerializerDeserializer state.
  static const int kPartialSnapshotCacheCapacity = 1300;

  static int number_of_isolates() { return number_of_isolates_; }

 private:
  Isolate();

#ifdef V8_USE_TLS_FOR_GLOBAL_ISOLATE
  static Thread::LocalStorageKey global_isolate_key_;
#endif
  static Isolate* global_isolate_;

  // TODO(isolates): Access to this global counter should be serialized.
  static int number_of_isolates_;

  // Initialize process-wide state.
  static void InitOnce();

  bool PreInit();

  bool Init(Deserializer* des);

  enum State {
    UNINITIALIZED,    // Some components may not have been allocated.
    PREINITIALIZED,   // Components have been allocated but not initialized.
    INITIALIZED       // All components are fully initialized.
  };

  State state_;

  Bootstrapper* bootstrapper_;
  CompilationCache* compilation_cache_;
  CpuFeatures* cpu_features_;
  Mutex* break_access_;
  Heap heap_;
  StackGuard stack_guard_;
  StubCache* stub_cache_;
  ThreadLocalTop thread_local_top_;
  TranscendentalCache* transcendental_cache_;
  KeyedLookupCache* keyed_lookup_cache_;
  ContextSlotCache* context_slot_cache_;
  DescriptorLookupCache* descriptor_lookup_cache_;
  v8::ImplementationUtilities::HandleScopeData handle_scope_data_;
  HandleScopeImplementer* handle_scope_implementer_;
  ScannerCharacterClasses* scanner_character_classes_;
  Zone zone_;

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

#endif  // V8_ISOLATE_H_
