// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api.h"

#include <algorithm>  // For min
#include <cmath>      // For isnan.
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>  // For move
#include <vector>

#include "include/v8-array-buffer.h"
#include "include/v8-callbacks.h"
#include "include/v8-cppgc.h"
#include "include/v8-date.h"
#include "include/v8-embedder-state-scope.h"
#include "include/v8-extension.h"
#include "include/v8-external-memory-accounter.h"
#include "include/v8-fast-api-calls.h"
#include "include/v8-function.h"
#include "include/v8-json.h"
#include "include/v8-locker.h"
#include "include/v8-primitive-object.h"
#include "include/v8-profiler.h"
#include "include/v8-source-location.h"
#include "include/v8-template.h"
#include "include/v8-unwinder-state.h"
#include "include/v8-util.h"
#include "include/v8-wasm.h"
#include "src/api/api-arguments.h"
#include "src/api/api-inl.h"
#include "src/api/api-natives.h"
#include "src/base/hashing.h"
#include "src/base/logging.h"
#include "src/base/numerics/safe_conversions.h"
#include "src/base/platform/memory.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"
#include "src/base/template-utils.h"
#include "src/base/utils/random-number-generator.h"
#include "src/base/vector.h"
#include "src/builtins/accessors.h"
#include "src/builtins/builtins-utils.h"
#include "src/codegen/compilation-cache.h"
#include "src/codegen/compiler.h"
#include "src/codegen/cpu-features.h"
#include "src/codegen/script-details.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/compiler-dispatcher/lazy-compile-dispatcher.h"
#include "src/date/date.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/embedder-state.h"
#include "src/execution/execution.h"
#include "src/execution/frames-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/messages.h"
#include "src/execution/microtask-queue.h"
#include "src/execution/simulator.h"
#include "src/execution/v8threads.h"
#include "src/execution/vm-state-inl.h"
#include "src/handles/global-handles.h"
#include "src/handles/persistent-handles.h"
#include "src/handles/shared-object-conveyor-handles.h"
#include "src/handles/traced-handles-inl.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap-layout-inl.h"
#include "src/heap/heap-write-barrier.h"
#include "src/heap/safepoint.h"
#include "src/heap/visit-object.h"
#include "src/init/bootstrapper.h"
#include "src/init/icu_util.h"
#include "src/init/startup-data-util.h"
#include "src/init/v8.h"
#include "src/json/json-parser.h"
#include "src/json/json-stringifier.h"
#include "src/logging/counters-scopes.h"
#include "src/logging/metrics.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/logging/tracing-flags.h"
#include "src/numbers/conversions-inl.h"
#include "src/objects/api-callbacks.h"
#include "src/objects/backing-store.h"
#include "src/objects/contexts.h"
#include "src/objects/cpp-heap-object-wrapper-inl.h"
#include "src/objects/embedder-data-array-inl.h"
#include "src/objects/embedder-data-slot-inl.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-collection-inl.h"
#include "src/objects/js-objects.h"
#include "src/objects/js-promise-inl.h"
#include "src/objects/js-regexp-inl.h"
#include "src/objects/js-weak-refs-inl.h"
#include "src/objects/module-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/oddball.h"
#include "src/objects/ordered-hash-table-inl.h"
#include "src/objects/primitive-heap-object.h"
#include "src/objects/property-descriptor.h"
#include "src/objects/property-details.h"
#include "src/objects/property.h"
#include "src/objects/prototype.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/slots.h"
#include "src/objects/smi.h"
#include "src/objects/string.h"
#include "src/objects/synthetic-module-inl.h"
#include "src/objects/templates.h"
#include "src/objects/value-serializer.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parser.h"
#include "src/parsing/pending-compilation-error-handler.h"
#include "src/parsing/scanner-character-streams.h"
#include "src/profiler/cpu-profiler.h"
#include "src/profiler/heap-profiler.h"
#include "src/profiler/heap-snapshot-generator-inl.h"
#include "src/profiler/profile-generator-inl.h"
#include "src/profiler/tick-sample.h"
#include "src/regexp/regexp-utils.h"
#include "src/roots/static-roots.h"
#include "src/runtime/runtime.h"
#include "src/sandbox/external-pointer.h"
#include "src/sandbox/isolate.h"
#include "src/sandbox/sandbox.h"
#include "src/snapshot/code-serializer.h"
#include "src/snapshot/embedded/embedded-data.h"
#include "src/snapshot/snapshot.h"
#include "src/strings/char-predicates-inl.h"
#include "src/strings/string-hasher.h"
#include "src/strings/unicode-inl.h"
#include "src/tracing/trace-event.h"
#include "src/utils/detachable-vector.h"
#include "src/utils/identity-map.h"
#include "src/utils/version.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/debug/debug-wasm-objects.h"
#include "src/trap-handler/trap-handler.h"
#include "src/wasm/streaming-decoder.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-js.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-result.h"
#include "src/wasm/wasm-serialization.h"
#endif  // V8_ENABLE_WEBASSEMBLY

#ifdef V8_INTL_SUPPORT
#include "src/objects/intl-objects.h"
#endif  // V8_INTL_SUPPORT

#if V8_OS_LINUX || V8_OS_DARWIN || V8_OS_FREEBSD
#include <signal.h>
#include <unistd.h>

#if V8_ENABLE_WEBASSEMBLY
#include "include/v8-wasm-trap-handler-posix.h"
#include "src/trap-handler/handler-inside-posix.h"
#endif  // V8_ENABLE_WEBASSEMBLY

#endif  // V8_OS_LINUX || V8_OS_DARWIN || V8_OS_FREEBSD

#if V8_OS_WIN
#include "include/v8-wasm-trap-handler-win.h"
#include "src/trap-handler/handler-inside-win.h"
#if defined(V8_OS_WIN64)
#include "src/base/platform/wrappers.h"
#include "src/diagnostics/unwinding-info-win64.h"
#endif  // V8_OS_WIN64
#endif  // V8_OS_WIN

#if defined(V8_ENABLE_ETW_STACK_WALKING)
#include "src/diagnostics/etw-jit-win.h"
#endif  // V8_ENABLE_ETW_STACK_WALKING

namespace v8 {

static OOMErrorCallback g_oom_error_callback = nullptr;

using RCCId = i::RuntimeCallCounterId;

static ScriptOrigin GetScriptOriginForScript(
    i::Isolate* i_isolate, i::DirectHandle<i::Script> script) {
  i::DirectHandle<i::Object> scriptName(script->GetNameOrSourceURL(),
                                        i_isolate);
  i::DirectHandle<i::Object> source_map_url(script->source_mapping_url(),
                                            i_isolate);
  i::DirectHandle<i::Object> host_defined_options(
      script->host_defined_options(), i_isolate);
  ScriptOriginOptions options(script->origin_options());
  bool is_wasm = false;
#if V8_ENABLE_WEBASSEMBLY
  is_wasm = script->type() == i::Script::Type::kWasm;
#endif  // V8_ENABLE_WEBASSEMBLY
  v8::ScriptOrigin origin(
      Utils::ToLocal(scriptName), script->line_offset(),
      script->column_offset(), options.IsSharedCrossOrigin(), script->id(),
      Utils::ToLocal(source_map_url), options.IsOpaque(), is_wasm,
      options.IsModule(), Utils::ToLocal(host_defined_options));
  return origin;
}

// --- E x c e p t i o n   B e h a v i o r ---

// When V8 cannot allocate memory FatalProcessOutOfMemory is called. The default
// OOM error handler is called and execution is stopped.
void i::V8::FatalProcessOutOfMemory(i::Isolate* i_isolate, const char* location,
                                    const OOMDetails& details) {
  i::HeapStats heap_stats;

  if (i_isolate == nullptr) {
    i_isolate = Isolate::TryGetCurrent();
  }

  if (i_isolate == nullptr) {
    // If the Isolate is not available for the current thread we cannot retrieve
    // memory information from the Isolate. Write easy-to-recognize values on
    // the stack.
    memset(&heap_stats, 0xBADC0DE, sizeof(heap_stats));
    // Give the embedder a chance to handle the condition. If it doesn't,
    // just crash.
    if (g_oom_error_callback) g_oom_error_callback(location, details);
    base::FatalOOM(base::OOMType::kProcess, location);
    UNREACHABLE();
  }

  memset(heap_stats.last_few_messages, 0, Heap::kTraceRingBufferSize + 1);

  if (i_isolate->heap()->HasBeenSetUp()) {
    i_isolate->heap()->RecordStats(&heap_stats);
    if (!v8_flags.correctness_fuzzer_suppressions) {
      char* first_newline = strchr(heap_stats.last_few_messages, '\n');
      if (first_newline == nullptr || first_newline[1] == '\0')
        first_newline = heap_stats.last_few_messages;
      base::OS::PrintError("\n<--- Last few GCs --->\n%s\n", first_newline);
    }
  }
  Utils::ReportOOMFailure(i_isolate, location, details);
  if (g_oom_error_callback) g_oom_error_callback(location, details);
  // If the fatal error handler returns, we stop execution.
  FATAL("API fatal error handler returned after process out of memory");
}

void i::V8::FatalProcessOutOfMemory(i::Isolate* i_isolate, const char* location,
                                    const char* detail) {
  OOMDetails details;
  details.detail = detail;
  FatalProcessOutOfMemory(i_isolate, location, details);
}

void Utils::ReportApiFailure(const char* location, const char* message) {
  i::Isolate* i_isolate = i::Isolate::TryGetCurrent();
  FatalErrorCallback callback = nullptr;
  if (i_isolate != nullptr) {
    callback = i_isolate->exception_behavior();
  }
  if (callback == nullptr) {
    base::OS::PrintError("\n#\n# Fatal error in %s\n# %s\n#\n\n", location,
                         message);
    base::OS::Abort();
  } else {
    callback(location, message);
  }
  i_isolate->SignalFatalError();
}

void Utils::ReportOOMFailure(i::Isolate* i_isolate, const char* location,
                             const OOMDetails& details) {
  if (auto oom_callback = i_isolate->oom_behavior()) {
    oom_callback(location, details);
  } else {
    // TODO(wfh): Remove this fallback once Blink is setting OOM handler. See
    // crbug.com/614440.
    FatalErrorCallback fatal_callback = i_isolate->exception_behavior();
    if (fatal_callback == nullptr) {
      base::OOMType type = details.is_heap_oom ? base::OOMType::kJavaScript
                                               : base::OOMType::kProcess;
      base::FatalOOM(type, location);
      UNREACHABLE();
    } else {
      fatal_callback(location,
                     details.is_heap_oom
                         ? "Allocation failed - JavaScript heap out of memory"
                         : "Allocation failed - process out of memory");
    }
  }
  i_isolate->SignalFatalError();
}

void V8::SetSnapshotDataBlob(StartupData* snapshot_blob) {
  i::V8::SetSnapshotBlob(snapshot_blob);
}

namespace {

#ifdef V8_ENABLE_SANDBOX
// ArrayBufferAllocator to use when the sandbox is enabled in which case all
// ArrayBuffer backing stores need to be allocated inside the sandbox.
class ArrayBufferAllocator : public v8::ArrayBuffer::Allocator {
 public:
  explicit ArrayBufferAllocator(i::IsolateGroup* group)
      : sandbox_(group->sandbox()),
        allocator_(group->GetSandboxedArrayBufferAllocator()) {}

  void* Allocate(size_t length) override {
    return allocator_->Allocate(length);
  }

  void* AllocateUninitialized(size_t length) override {
    return Allocate(length);
  }

  void Free(void* data, size_t length) override {
    return allocator_->Free(data);
  }

  PageAllocator* GetPageAllocator() override {
    return sandbox_->page_allocator();
  }

 private:
  i::Sandbox* sandbox_ = nullptr;
  i::SandboxedArrayBufferAllocatorBase* allocator_ = nullptr;
};

#else

class ArrayBufferAllocator : public v8::ArrayBuffer::Allocator {
 public:
  void* Allocate(size_t length) override { return base::Calloc(length, 1); }

  void* AllocateUninitialized(size_t length) override {
    return base::Malloc(length);
  }

  void Free(void* data, size_t) override { base::Free(data); }

  PageAllocator* GetPageAllocator() override {
    return i::GetPlatformPageAllocator();
  }
};
#endif  // V8_ENABLE_SANDBOX

}  // namespace

SnapshotCreator::SnapshotCreator(Isolate* v8_isolate,
                                 const intptr_t* external_references,
                                 const StartupData* existing_snapshot,
                                 bool owns_isolate)
    : impl_(new i::SnapshotCreatorImpl(
          reinterpret_cast<i::Isolate*>(v8_isolate), external_references,
          existing_snapshot, owns_isolate)) {}

SnapshotCreator::SnapshotCreator(const intptr_t* external_references,
                                 const StartupData* existing_snapshot)
    : SnapshotCreator(nullptr, external_references, existing_snapshot) {}

SnapshotCreator::SnapshotCreator(const v8::Isolate::CreateParams& params)
    : impl_(new i::SnapshotCreatorImpl(params)) {}

SnapshotCreator::SnapshotCreator(v8::Isolate* isolate,
                                 const v8::Isolate::CreateParams& params)
    : impl_(new i::SnapshotCreatorImpl(reinterpret_cast<i::Isolate*>(isolate),
                                       params)) {}

SnapshotCreator::~SnapshotCreator() {
  DCHECK_NOT_NULL(impl_);
  delete impl_;
}

Isolate* SnapshotCreator::GetIsolate() {
  return reinterpret_cast<v8::Isolate*>(impl_->isolate());
}

void SnapshotCreator::SetDefaultContext(
    Local<Context> context,
    SerializeInternalFieldsCallback internal_fields_serializer,
    SerializeContextDataCallback context_data_serializer,
    SerializeAPIWrapperCallback api_wrapper_serializer) {
  impl_->SetDefaultContext(
      Utils::OpenDirectHandle(*context),
      i::SerializeEmbedderFieldsCallback(internal_fields_serializer,
                                         context_data_serializer,
                                         api_wrapper_serializer));
}

size_t SnapshotCreator::AddContext(
    Local<Context> context,
    SerializeInternalFieldsCallback internal_fields_serializer,
    SerializeContextDataCallback context_data_serializer,
    SerializeAPIWrapperCallback api_wrapper_serializer) {
  return impl_->AddContext(
      Utils::OpenDirectHandle(*context),
      i::SerializeEmbedderFieldsCallback(internal_fields_serializer,
                                         context_data_serializer,
                                         api_wrapper_serializer));
}

size_t SnapshotCreator::AddData(i::Address object) {
  return impl_->AddData(object);
}

size_t SnapshotCreator::AddData(Local<Context> context, i::Address object) {
  return impl_->AddData(Utils::OpenDirectHandle(*context), object);
}

StartupData SnapshotCreator::CreateBlob(
    SnapshotCreator::FunctionCodeHandling function_code_handling) {
  return impl_->CreateBlob(function_code_handling);
}

bool StartupData::CanBeRehashed() const {
  DCHECK(i::Snapshot::VerifyChecksum(this));
  return i::Snapshot::ExtractRehashability(this);
}

bool StartupData::IsValid() const { return i::Snapshot::VersionIsValid(this); }

void V8::SetDcheckErrorHandler(DcheckErrorCallback that) {
  v8::base::SetDcheckFunction(that);
}

void V8::SetFatalErrorHandler(V8FatalErrorCallback that) {
  v8::base::SetFatalFunction(that);
}

void V8::SetFlagsFromString(const char* str) {
  SetFlagsFromString(str, strlen(str));
}

void V8::SetFlagsFromString(const char* str, size_t length) {
  i::FlagList::SetFlagsFromString(str, length);
}

void V8::SetFlagsFromCommandLine(int* argc, char** argv, bool remove_flags) {
  using HelpOptions = i::FlagList::HelpOptions;
  i::FlagList::SetFlagsFromCommandLine(argc, argv, remove_flags,
                                       HelpOptions(HelpOptions::kDontExit));
}

RegisteredExtension* RegisteredExtension::first_extension_ = nullptr;

RegisteredExtension::RegisteredExtension(std::unique_ptr<Extension> extension)
    : extension_(std::move(extension)) {}

// static
void RegisteredExtension::Register(std::unique_ptr<Extension> extension) {
  RegisteredExtension* new_extension =
      new RegisteredExtension(std::move(extension));
  new_extension->next_ = first_extension_;
  first_extension_ = new_extension;
}

// static
void RegisteredExtension::UnregisterAll() {
  RegisteredExtension* re = first_extension_;
  while (re != nullptr) {
    RegisteredExtension* next = re->next();
    delete re;
    re = next;
  }
  first_extension_ = nullptr;
}

namespace {
class ExtensionResource : public String::ExternalOneByteStringResource {
 public:
  ExtensionResource() : data_(nullptr), length_(0) {}
  ExtensionResource(const char* data, size_t length)
      : data_(data), length_(length) {}
  const char* data() const override { return data_; }
  size_t length() const override { return length_; }
  void Dispose() override {}

 private:
  const char* data_;
  size_t length_;
};
}  // anonymous namespace

void RegisterExtension(std::unique_ptr<Extension> extension) {
  RegisteredExtension::Register(std::move(extension));
}

Extension::Extension(const char* name, const char* source, int dep_count,
                     const char** deps, int source_length)
    : name_(name),
      source_length_(source_length >= 0
                         ? source_length
                         : (source ? static_cast<int>(strlen(source)) : 0)),
      dep_count_(dep_count),
      deps_(deps),
      auto_enable_(false) {
  source_ = new ExtensionResource(source, source_length_);
  CHECK(source != nullptr || source_length_ == 0);
}

void ResourceConstraints::ConfigureDefaultsFromHeapSize(
    size_t initial_heap_size_in_bytes, size_t maximum_heap_size_in_bytes) {
  CHECK_LE(initial_heap_size_in_bytes, maximum_heap_size_in_bytes);
  if (maximum_heap_size_in_bytes == 0) {
    return;
  }
  size_t young_generation, old_generation;
  i::Heap::GenerationSizesFromHeapSize(0, maximum_heap_size_in_bytes,
                                       &young_generation, &old_generation);
  set_max_young_generation_size_in_bytes(
      std::max(young_generation, i::Heap::MinYoungGenerationSize()));
  set_max_old_generation_size_in_bytes(
      std::max(old_generation, i::Heap::MinOldGenerationSize()));
  if (initial_heap_size_in_bytes > 0) {
    i::Heap::GenerationSizesFromHeapSize(0, initial_heap_size_in_bytes,
                                         &young_generation, &old_generation);
    // We do not set lower bounds for the initial sizes.
    set_initial_young_generation_size_in_bytes(young_generation);
    set_initial_old_generation_size_in_bytes(old_generation);
  }
  if (i::kPlatformRequiresCodeRange) {
    set_code_range_size_in_bytes(
        std::min(i::kMaximalCodeRangeSize, maximum_heap_size_in_bytes));
  }
}

void ResourceConstraints::ConfigureDefaults(uint64_t physical_memory,
                                            uint64_t virtual_memory_limit) {
  physical_memory_size_ = physical_memory;

  size_t heap_size = i::Heap::HeapSizeFromPhysicalMemory(physical_memory);
  size_t young_generation, old_generation;
  i::Heap::GenerationSizesFromHeapSize(physical_memory, heap_size,
                                       &young_generation, &old_generation);
  set_max_young_generation_size_in_bytes(young_generation);
  set_max_old_generation_size_in_bytes(old_generation);

  if (virtual_memory_limit > 0 && i::kPlatformRequiresCodeRange) {
    set_code_range_size_in_bytes(
        std::min(i::kMaximalCodeRangeSize,
                 static_cast<size_t>(virtual_memory_limit / 8)));
  }
}

#ifdef ENABLE_SLOW_DCHECKS
namespace api_internal {
void StackAllocated<true>::VerifyOnStack() const {
  if (internal::StackAllocatedCheck::Get()) {
    SLOW_DCHECK(::heap::base::Stack::IsOnStack(this));
  }
}
}  // namespace api_internal
#endif

namespace internal {

// static
v8::Isolate* Internals::GetCurrentIsolate() {
  return v8::Isolate::GetCurrent();
}

void VerifyHandleIsNonEmpty(bool is_empty) {
  Utils::ApiCheck(!is_empty, "v8::ReturnValue",
                  "SetNonEmpty() called with empty handle.");
}

i::Address* GlobalizeTracedReference(
    i::Isolate* i_isolate, i::Address value, internal::Address* slot,
    TracedReferenceStoreMode store_mode,
    TracedReferenceHandling reference_handling) {
  return i_isolate->traced_handles()
      ->Create(value, slot, store_mode, reference_handling)
      .location();
}

void MoveTracedReference(internal::Address** from, internal::Address** to) {
  TracedHandles::Move(from, to);
}

void CopyTracedReference(const internal::Address* const* from,
                         internal::Address** to) {
  TracedHandles::Copy(from, to);
}

void DisposeTracedReference(internal::Address* location) {
  TracedHandles::Destroy(location);
}

#if V8_STATIC_ROOTS_BOOL

// Check static root constants exposed in v8-internal.h.

namespace {
constexpr InstanceTypeChecker::TaggedAddressRange kStringMapRange =
    *InstanceTypeChecker::UniqueMapRangeOfInstanceTypeRange(FIRST_STRING_TYPE,
                                                            LAST_STRING_TYPE);
}  // namespace

#define EXPORTED_STATIC_ROOTS_PTR_MAPPING(V)                \
  V(UndefinedValue, i::StaticReadOnlyRoot::kUndefinedValue) \
  V(NullValue, i::StaticReadOnlyRoot::kNullValue)           \
  V(TrueValue, i::StaticReadOnlyRoot::kTrueValue)           \
  V(FalseValue, i::StaticReadOnlyRoot::kFalseValue)         \
  V(EmptyString, i::StaticReadOnlyRoot::kempty_string)      \
  V(TheHoleValue, i::StaticReadOnlyRoot::kTheHoleValue)     \
  V(StringMapLowerBound, kStringMapRange.first)             \
  V(StringMapUpperBound, kStringMapRange.second)

static_assert(std::is_same_v<Internals::Tagged_t, Tagged_t>);
// Ensure they have the correct value.
#define CHECK_STATIC_ROOT(name, value) \
  static_assert(Internals::StaticReadOnlyRoot::k##name == value);
EXPORTED_STATIC_ROOTS_PTR_MAPPING(CHECK_STATIC_ROOT)
#undef CHECK_STATIC_ROOT
#define PLUS_ONE(...) +1
static constexpr int kNumberOfCheckedStaticRoots =
    0 EXPORTED_STATIC_ROOTS_PTR_MAPPING(PLUS_ONE);
#undef EXPORTED_STATIC_ROOTS_PTR_MAPPING
static_assert(Internals::StaticReadOnlyRoot::kNumberOfExportedStaticRoots ==
              kNumberOfCheckedStaticRoots);

#endif  // V8_STATIC_ROOTS_BOOL

}  // namespace internal

namespace api_internal {

i::Address* GlobalizeReference(i::Isolate* i_isolate, i::Address value) {
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Persistent_New);
  i::IndirectHandle<i::Object> result =
      i_isolate->global_handles()->Create(value);
#ifdef VERIFY_HEAP
  if (i::v8_flags.verify_heap) {
    i::Object::ObjectVerify(i::Tagged<i::Object>(value), i_isolate);
  }
#endif  // VERIFY_HEAP
  return result.location();
}

i::Address* CopyGlobalReference(i::Address* from) {
  i::IndirectHandle<i::Object> result = i::GlobalHandles::CopyGlobal(from);
  return result.location();
}

void MoveGlobalReference(internal::Address** from, internal::Address** to) {
  i::GlobalHandles::MoveGlobal(from, to);
}

void MakeWeak(i::Address* location, void* parameter,
              WeakCallbackInfo<void>::Callback weak_callback,
              WeakCallbackType type) {
  i::GlobalHandles::MakeWeak(location, parameter, weak_callback, type);
}

void MakeWeak(i::Address** location_addr) {
  i::GlobalHandles::MakeWeak(location_addr);
}

void* ClearWeak(i::Address* location) {
  return i::GlobalHandles::ClearWeakness(location);
}

void AnnotateStrongRetainer(i::Address* location, const char* label) {
  i::GlobalHandles::AnnotateStrongRetainer(location, label);
}

void DisposeGlobal(i::Address* location) {
  i::GlobalHandles::Destroy(location);
}

i::Address* Eternalize(Isolate* v8_isolate, Value* value) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::Tagged<i::Object> object = *Utils::OpenDirectHandle(value);
  int index = -1;
  i_isolate->eternal_handles()->Create(i_isolate, object, &index);
  return i_isolate->eternal_handles()->Get(index).location();
}

void FromJustIsNothing() {
  Utils::ApiCheck(false, "v8::FromJust", "Maybe value is Nothing");
}

void ToLocalEmpty() {
  Utils::ApiCheck(false, "v8::ToLocalChecked", "Empty MaybeLocal");
}

void InternalFieldOutOfBounds(int index) {
  Utils::ApiCheck(0 <= index && index < kInternalFieldsInWeakCallback,
                  "WeakCallbackInfo::GetInternalField",
                  "Internal field out of bounds");
}

}  // namespace api_internal

// --- H a n d l e s ---

#ifdef V8_ENABLE_CHECKS
void HandleScope::DoInitializeAsserts(Isolate* v8_isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  // We do not want to check the correct usage of the Locker class all over the
  // place, so we do it only here: Without a HandleScope, an embedder can do
  // almost nothing, so it is enough to check in this central place.
  // We make an exception if the serializer is enabled, which means that the
  // Isolate is exclusively used to create a snapshot.
  Utils::ApiCheck(!i_isolate->was_locker_ever_used() ||
                      i_isolate->thread_manager()->IsLockedByCurrentThread() ||
                      i_isolate->serializer_enabled(),
                  "HandleScope::HandleScope",
                  "Entering the V8 API without proper locking in place");
}

void HandleScope::AssertScopeLevelsMatch() {
  auto i_isolate = reinterpret_cast<internal::Isolate*>(isolate_);
  CHECK_EQ(scope_level_, i_isolate->handle_scope_data()->level);
}

void HandleScope::DoCloseScopeAsserts(int before, internal::Address* limit,
                                      internal::HandleScopeData* current) {
#ifdef ENABLE_LOCAL_HANDLE_ZAPPING
  internal::HandleScope::ZapRange(current->next, limit);
#endif

  MSAN_ALLOCATED_UNINITIALIZED_MEMORY(
      current->next,
      static_cast<size_t>(reinterpret_cast<internal::Address>(limit) -
                          reinterpret_cast<internal::Address>(current->next)));
  if (internal::v8_flags.check_handle_count) {
    int after = NumberOfHandles(isolate_);
    DCHECK_LT(after - before, internal::HandleScope::kCheckHandleThreshold);
    DCHECK_LT(before, internal::HandleScope::kCheckHandleThreshold);
    USE(after);
  }
}
#endif  // V8_ENABLE_CHECKS

void* HandleScope::operator new(size_t) { base::OS::Abort(); }
void* HandleScope::operator new[](size_t) { base::OS::Abort(); }
void HandleScope::operator delete(void*, size_t) { base::OS::Abort(); }
void HandleScope::operator delete[](void*, size_t) { base::OS::Abort(); }

int HandleScope::NumberOfHandles(Isolate* v8_isolate) {
  return i::HandleScope::NumberOfHandles(
      reinterpret_cast<i::Isolate*>(v8_isolate));
}

i::Address* HandleScope::Extend(Isolate* isolate) {
  return i::HandleScope::Extend(reinterpret_cast<i::Isolate*>(isolate));
}

void HandleScope::DeleteExtensions(Isolate* v8_isolate) {
  using I = internal::Internals;
  internal::HandleScopeData* current = I::GetHandleScopeData(v8_isolate);
  reinterpret_cast<internal::Isolate*>(v8_isolate)
      ->handle_scope_implementer()
      ->DeleteExtensions(current->limit);
}

#ifdef V8_ENABLE_DIRECT_HANDLE

i::Address* HandleScope::CreateHandleForCurrentIsolate(i::Address value) {
  i::Isolate* i_isolate = i::Isolate::Current();
  return i::HandleScope::CreateHandle(i_isolate, value);
}

#endif  // V8_ENABLE_DIRECT_HANDLE

EscapableHandleScopeBase::EscapableHandleScopeBase(Isolate* v8_isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  escape_slot_ = CreateHandle(
      v8_isolate, i::ReadOnlyRoots(i_isolate).the_hole_value().ptr());
  Initialize(v8_isolate);
}

i::Address* EscapableHandleScopeBase::EscapeSlot(i::Address* escape_value) {
  DCHECK_NOT_NULL(escape_value);
  DCHECK(i::IsTheHole(i::Tagged<i::Object>(*escape_slot_),
                      reinterpret_cast<i::Isolate*>(GetIsolate())));
  *escape_slot_ = *escape_value;
  return escape_slot_;
}

SealHandleScope::SealHandleScope(Isolate* v8_isolate)
    : i_isolate_(reinterpret_cast<i::Isolate*>(v8_isolate)) {
  i::HandleScopeData* current = i_isolate_->handle_scope_data();
  prev_limit_ = current->limit;
  current->limit = current->next;
  prev_sealed_level_ = current->sealed_level;
  current->sealed_level = current->level;
}

SealHandleScope::~SealHandleScope() {
  i::HandleScopeData* current = i_isolate_->handle_scope_data();
  DCHECK_EQ(current->next, current->limit);
  current->limit = prev_limit_;
  DCHECK_EQ(current->level, current->sealed_level);
  current->sealed_level = prev_sealed_level_;
}

bool Data::IsModule() const {
  return i::IsModule(*Utils::OpenDirectHandle(this));
}

bool Data::IsModuleRequest() const {
  return i::IsModuleRequest(*Utils::OpenDirectHandle(this));
}

bool Data::IsFixedArray() const {
  return i::IsFixedArray(*Utils::OpenDirectHandle(this));
}

bool Data::IsValue() const {
  i::DisallowGarbageCollection no_gc;
  i::Tagged<i::Object> self = *Utils::OpenDirectHandle(this);
  if (i::IsSmi(self)) return true;
  i::Tagged<i::HeapObject> heap_object = i::Cast<i::HeapObject>(self);
  DCHECK(!IsTheHole(heap_object));
  if (i::IsSymbol(heap_object)) {
    return !i::Cast<i::Symbol>(heap_object)->is_private();
  }
  return IsPrimitiveHeapObject(heap_object) || IsJSReceiver(heap_object);
}

bool Data::IsPrivate() const {
  return i::IsPrivateSymbol(*Utils::OpenDirectHandle(this));
}

bool Data::IsObjectTemplate() const {
  return i::IsObjectTemplateInfo(*Utils::OpenDirectHandle(this));
}

bool Data::IsFunctionTemplate() const {
  return i::IsFunctionTemplateInfo(*Utils::OpenDirectHandle(this));
}

bool Data::IsContext() const {
  return i::IsContext(*Utils::OpenDirectHandle(this));
}

bool Data::IsCppHeapExternal() const {
  return IsCppHeapExternalObject(*Utils::OpenDirectHandle(this));
}

void Context::Enter() {
  i::DisallowGarbageCollection no_gc;
  i::Tagged<i::NativeContext> env = *Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScopeImplementer* impl = i_isolate->handle_scope_implementer();
  impl->EnterContext(env);
  impl->SaveContext(i_isolate->context());
  i_isolate->set_context(env);
}

void Context::Exit() {
  auto env = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScopeImplementer* impl = i_isolate->handle_scope_implementer();
  if (!Utils::ApiCheck(impl->LastEnteredContextWas(*env), "v8::Context::Exit()",
                       "Cannot exit non-entered context")) {
    return;
  }
  impl->LeaveContext();
  i_isolate->set_context(impl->RestoreContext());
}

Context::BackupIncumbentScope::BackupIncumbentScope(
    Local<Context> backup_incumbent_context)
    : backup_incumbent_context_(backup_incumbent_context) {
  DCHECK(!backup_incumbent_context_.IsEmpty());

  i::Isolate* i_isolate = i::Isolate::Current();

  js_stack_comparable_address_ =
      i::SimulatorStack::RegisterJSStackComparableAddress(i_isolate);

  prev_ = i_isolate->top_backup_incumbent_scope();
  i_isolate->set_top_backup_incumbent_scope(this);
  // Enforce slow incumbent computation in order to make it find this
  // BackupIncumbentScope.
  i_isolate->clear_topmost_script_having_context();
}

Context::BackupIncumbentScope::~BackupIncumbentScope() {
  i::Isolate* i_isolate = i::Isolate::Current();

  i::SimulatorStack::UnregisterJSStackComparableAddress(i_isolate);

  i_isolate->set_top_backup_incumbent_scope(prev_);
}

static_assert(i::Internals::kEmbedderDataSlotSize == i::kEmbedderDataSlotSize);
static_assert(i::Internals::kEmbedderDataSlotExternalPointerOffset ==
              i::EmbedderDataSlot::kExternalPointerOffset);

static i::DirectHandle<i::EmbedderDataArray> EmbedderDataFor(
    Context* context, int index, bool can_grow, const char* location) {
  auto env = Utils::OpenDirectHandle(context);
  i::Isolate* i_isolate = i::Isolate::Current();
  i::DisallowJavascriptExecutionDebugOnly no_execution(i_isolate);
  i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);
  bool ok = Utils::ApiCheck(i::IsNativeContext(*env), location,
                            "Not a native context") &&
            Utils::ApiCheck(index >= 0, location, "Negative index");
  if (!ok) return {};
  // TODO(ishell): remove cast once embedder_data slot has a proper type.
  i::DirectHandle<i::EmbedderDataArray> data(
      i::Cast<i::EmbedderDataArray>(env->embedder_data()), i_isolate);
  if (index < data->length()) return data;
  if (!Utils::ApiCheck(can_grow && index < i::EmbedderDataArray::kMaxLength,
                       location, "Index too large")) {
    return {};
  }
  data = i::EmbedderDataArray::EnsureCapacity(i_isolate, data, index);
  env->set_embedder_data(*data);
  return data;
}

uint32_t Context::GetNumberOfEmbedderDataFields() {
  auto context = Utils::OpenDirectHandle(this);
  i::DisallowJavascriptExecutionDebugOnly no_execution;
  i::DisallowExceptionsDebugOnly no_exceptions;
  Utils::ApiCheck(i::IsNativeContext(*context),
                  "Context::GetNumberOfEmbedderDataFields",
                  "Not a native context");
  // TODO(ishell): remove cast once embedder_data slot has a proper type.
  return static_cast<uint32_t>(
      i::Cast<i::EmbedderDataArray>(context->embedder_data())->length());
}

v8::Local<v8::Value> Context::SlowGetEmbedderData(int index) {
  const char* location = "v8::Context::GetEmbedderData()";
  i::DirectHandle<i::EmbedderDataArray> data =
      EmbedderDataFor(this, index, false, location);
  if (data.is_null()) return {};
  i::Isolate* i_isolate = i::Isolate::Current();
  return Utils::ToLocal(i::direct_handle(
      i::EmbedderDataSlot(*data, index).load_tagged(), i_isolate));
}

void Context::SetEmbedderData(int index, v8::Local<Value> value) {
  const char* location = "v8::Context::SetEmbedderData()";
  i::DirectHandle<i::EmbedderDataArray> data =
      EmbedderDataFor(this, index, true, location);
  if (data.is_null()) return;
  auto val = Utils::OpenDirectHandle(*value);
  i::EmbedderDataSlot::store_tagged(*data, index, *val);
  DCHECK_EQ(*Utils::OpenDirectHandle(*value),
            *Utils::OpenDirectHandle(*GetEmbedderData(index)));
}

void* Context::SlowGetAlignedPointerFromEmbedderData(int index) {
  const char* location = "v8::Context::GetAlignedPointerFromEmbedderData()";
  i::Isolate* i_isolate = i::Isolate::Current();
  i::HandleScope handle_scope(i_isolate);
  i::DirectHandle<i::EmbedderDataArray> data =
      EmbedderDataFor(this, index, false, location);
  if (data.is_null()) return nullptr;
  void* result;
  Utils::ApiCheck(
      i::EmbedderDataSlot(*data, index).ToAlignedPointer(i_isolate, &result),
      location, "Pointer is not aligned");
  return result;
}

void Context::SetAlignedPointerInEmbedderData(int index, void* value) {
  const char* location = "v8::Context::SetAlignedPointerInEmbedderData()";
  i::Isolate* i_isolate = i::Isolate::Current();
  i::DirectHandle<i::EmbedderDataArray> data =
      EmbedderDataFor(this, index, true, location);
  bool ok = i::EmbedderDataSlot(*data, index)
                .store_aligned_pointer(i_isolate, *data, value);
  Utils::ApiCheck(ok, location, "Pointer is not aligned");
  DCHECK_EQ(value, GetAlignedPointerFromEmbedderData(index));
}

// --- T e m p l a t e ---

void Template::Set(v8::Local<Name> name, v8::Local<Data> value,
                   v8::PropertyAttribute attribute) {
  auto templ = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScope scope(i_isolate);
  auto value_obj = Utils::OpenDirectHandle(*value);

  Utils::ApiCheck(!IsJSReceiver(*value_obj) || IsTemplateInfo(*value_obj),
                  "v8::Template::Set",
                  "Invalid value, must be a primitive or a Template");

  // The template cache only performs shallow clones, if we set an
  // ObjectTemplate as a property value then we can not cache the receiver
  // template.
  if (i::IsObjectTemplateInfo(*value_obj)) {
    templ->set_is_cacheable(false);
  }

  i::ApiNatives::AddDataProperty(i_isolate, templ,
                                 Utils::OpenDirectHandle(*name), value_obj,
                                 static_cast<i::PropertyAttributes>(attribute));
}

void Template::SetPrivate(v8::Local<Private> name, v8::Local<Data> value,
                          v8::PropertyAttribute attribute) {
  Set(Local<Name>::Cast(name), value, attribute);
}

void Template::SetAccessorProperty(v8::Local<v8::Name> name,
                                   v8::Local<FunctionTemplate> getter,
                                   v8::Local<FunctionTemplate> setter,
                                   v8::PropertyAttribute attribute) {
  auto templ = Utils::OpenDirectHandle(this);
  auto i_isolate = i::Isolate::Current();
  i::DirectHandle<i::FunctionTemplateInfo> i_getter;
  if (!getter.IsEmpty()) {
    i_getter = Utils::OpenDirectHandle(*getter);
    Utils::ApiCheck(i_getter->has_callback(i_isolate),
                    "v8::Template::SetAccessorProperty",
                    "Getter must have a call handler");
  }
  i::DirectHandle<i::FunctionTemplateInfo> i_setter;
  if (!setter.IsEmpty()) {
    i_setter = Utils::OpenDirectHandle(*setter);
    Utils::ApiCheck(i_setter->has_callback(i_isolate),
                    "v8::Template::SetAccessorProperty",
                    "Setter must have a call handler");
  }
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  DCHECK(!name.IsEmpty());
  DCHECK(!getter.IsEmpty() || !setter.IsEmpty());
  i::HandleScope scope(i_isolate);
  i::ApiNatives::AddAccessorProperty(
      i_isolate, templ, Utils::OpenDirectHandle(*name), i_getter, i_setter,
      static_cast<i::PropertyAttributes>(attribute));
}

// --- F u n c t i o n   T e m p l a t e ---

Local<ObjectTemplate> FunctionTemplate::PrototypeTemplate() {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::HeapObject> heap_obj(self->GetPrototypeTemplate(),
                                          i_isolate);
  if (i::IsUndefined(*heap_obj, i_isolate)) {
    // Do not cache prototype objects.
    constexpr bool do_not_cache = true;
    i::DirectHandle<i::ObjectTemplateInfo> proto_template =
        i_isolate->factory()->NewObjectTemplateInfo({}, do_not_cache);
    i::FunctionTemplateInfo::SetPrototypeTemplate(i_isolate, self,
                                                  proto_template);
    return Utils::ToLocal(proto_template);
  }
  return ToApiHandle<ObjectTemplate>(heap_obj);
}

void FunctionTemplate::SetPrototypeProviderTemplate(
    Local<FunctionTemplate> prototype_provider) {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::FunctionTemplateInfo> result =
      Utils::OpenDirectHandle(*prototype_provider);
  Utils::ApiCheck(i::IsUndefined(self->GetPrototypeTemplate(), i_isolate),
                  "v8::FunctionTemplate::SetPrototypeProviderTemplate",
                  "Protoype must be undefined");
  Utils::ApiCheck(i::IsUndefined(self->GetParentTemplate(), i_isolate),
                  "v8::FunctionTemplate::SetPrototypeProviderTemplate",
                  "Prototype provider must be empty");
  i::FunctionTemplateInfo::SetPrototypeProviderTemplate(i_isolate, self,
                                                        result);
}

namespace {
static void EnsureNotPublished(i::DirectHandle<i::FunctionTemplateInfo> info,
                               const char* func) {
  DCHECK_IMPLIES(info->instantiated(), info->published());
  Utils::ApiCheck(!info->published(), func,
                  "FunctionTemplate already instantiated");
}

i::DirectHandle<i::FunctionTemplateInfo> FunctionTemplateNew(
    i::Isolate* i_isolate, FunctionCallback callback, v8::Local<Value> data,
    v8::Local<Signature> signature, int length, ConstructorBehavior behavior,
    bool do_not_cache,
    v8::Local<Private> cached_property_name = v8::Local<Private>(),
    SideEffectType side_effect_type = SideEffectType::kHasSideEffect,
    const MemorySpan<const CFunction>& c_function_overloads = {}) {
  i::DirectHandle<i::FunctionTemplateInfo> obj =
      i_isolate->factory()->NewFunctionTemplateInfo(length, do_not_cache);
  {
    // Disallow GC until all fields of obj have acceptable types.
    i::DisallowGarbageCollection no_gc;
    i::Tagged<i::FunctionTemplateInfo> raw = *obj;
    if (!signature.IsEmpty()) {
      raw->set_signature(*Utils::OpenDirectHandle(*signature));
    }
    if (!cached_property_name.IsEmpty()) {
      raw->set_cached_property_name(
          *Utils::OpenDirectHandle(*cached_property_name));
    }
    if (behavior == ConstructorBehavior::kThrow) {
      raw->set_remove_prototype(true);
    }
  }
  if (callback != nullptr) {
    Utils::ToLocal(obj)->SetCallHandler(callback, data, side_effect_type,
                                        c_function_overloads);
  }
  return obj;
}
}  // namespace

void FunctionTemplate::Inherit(v8::Local<FunctionTemplate> value) {
  auto info = Utils::OpenDirectHandle(this);
  EnsureNotPublished(info, "v8::FunctionTemplate::Inherit");
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  Utils::ApiCheck(
      i::IsUndefined(info->GetPrototypeProviderTemplate(), i_isolate),
      "v8::FunctionTemplate::Inherit", "Protoype provider must be empty");
  i::FunctionTemplateInfo::SetParentTemplate(i_isolate, info,
                                             Utils::OpenDirectHandle(*value));
}

Local<FunctionTemplate> FunctionTemplate::New(
    Isolate* v8_isolate, FunctionCallback callback, v8::Local<Value> data,
    v8::Local<Signature> signature, int length, ConstructorBehavior behavior,
    SideEffectType side_effect_type, const CFunction* c_function,
    uint16_t instance_type, uint16_t allowed_receiver_instance_type_range_start,
    uint16_t allowed_receiver_instance_type_range_end) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  // Changes to the environment cannot be captured in the snapshot. Expect no
  // function templates when the isolate is created for serialization.
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_FunctionTemplate_New);

  if (!Utils::ApiCheck(
          !c_function || behavior == ConstructorBehavior::kThrow,
          "FunctionTemplate::New",
          "Fast API calls are not supported for constructor functions")) {
    return {};
  }

  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::FunctionTemplateInfo> templ = FunctionTemplateNew(
      i_isolate, callback, data, signature, length, behavior, false,
      Local<Private>(), side_effect_type,
      c_function ? MemorySpan<const CFunction>{c_function, 1}
                 : MemorySpan<const CFunction>{});

  if (instance_type) {
    if (!Utils::ApiCheck(
            base::IsInRange(static_cast<int>(instance_type),
                            i::Internals::kFirstEmbedderJSApiObjectType,
                            i::Internals::kLastEmbedderJSApiObjectType),
            "FunctionTemplate::New",
            "instance_type is outside the range of valid JSApiObject types")) {
      return {};
    }
    templ->SetInstanceType(instance_type);
  }

  if (allowed_receiver_instance_type_range_start ||
      allowed_receiver_instance_type_range_end) {
    if (!Utils::ApiCheck(i::Internals::kFirstEmbedderJSApiObjectType <=
                                 allowed_receiver_instance_type_range_start &&
                             allowed_receiver_instance_type_range_start <=
                                 allowed_receiver_instance_type_range_end &&
                             allowed_receiver_instance_type_range_end <=
                                 i::Internals::kLastEmbedderJSApiObjectType,
                         "FunctionTemplate::New",
                         "allowed receiver instance type range is outside the "
                         "range of valid JSApiObject types")) {
      return {};
    }
    templ->SetAllowedReceiverInstanceTypeRange(
        allowed_receiver_instance_type_range_start,
        allowed_receiver_instance_type_range_end);
  }
  return Utils::ToLocal(templ);
}

Local<FunctionTemplate> FunctionTemplate::NewWithCFunctionOverloads(
    Isolate* v8_isolate, FunctionCallback callback, v8::Local<Value> data,
    v8::Local<Signature> signature, int length, ConstructorBehavior behavior,
    SideEffectType side_effect_type,
    const MemorySpan<const CFunction>& c_function_overloads) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_FunctionTemplate_New);

  // Check that all overloads of the fast API callback have different numbers of
  // parameters. Since the number of overloads is supposed to be small, just
  // comparing them with each other should be fine.
  for (size_t i = 0; i < c_function_overloads.size(); ++i) {
    for (size_t j = i + 1; j < c_function_overloads.size(); ++j) {
      CHECK_NE(c_function_overloads.data()[i].ArgumentCount(),
               c_function_overloads.data()[j].ArgumentCount());
    }
  }

  if (!Utils::ApiCheck(
          c_function_overloads.empty() ||
              behavior == ConstructorBehavior::kThrow,
          "FunctionTemplate::NewWithCFunctionOverloads",
          "Fast API calls are not supported for constructor functions")) {
    return {};
  }

  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::FunctionTemplateInfo> templ = FunctionTemplateNew(
      i_isolate, callback, data, signature, length, behavior, false,
      Local<Private>(), side_effect_type, c_function_overloads);
  return Utils::ToLocal(templ);
}

Local<FunctionTemplate> FunctionTemplate::NewWithCache(
    Isolate* v8_isolate, FunctionCallback callback,
    Local<Private> cache_property, Local<Value> data,
    Local<Signature> signature, int length, SideEffectType side_effect_type) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_FunctionTemplate_NewWithCache);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::FunctionTemplateInfo> templ = FunctionTemplateNew(
      i_isolate, callback, data, signature, length, ConstructorBehavior::kAllow,
      false, cache_property, side_effect_type);
  return Utils::ToLocal(templ);
}

Local<Signature> Signature::New(Isolate* v8_isolate,
                                Local<FunctionTemplate> receiver) {
  return Local<Signature>::Cast(receiver);
}

#define SET_FIELD_WRAPPED(i_isolate, obj, setter, cdata, tag) \
  do {                                                        \
    i::DirectHandle<i::UnionOf<i::Smi, i::Foreign>> foreign = \
        FromCData<tag>(i_isolate, cdata);                     \
    (obj)->setter(*foreign);                                  \
  } while (false)

void FunctionTemplate::SetCallHandler(
    FunctionCallback callback, v8::Local<Value> data,
    SideEffectType side_effect_type,
    const MemorySpan<const CFunction>& c_function_overloads) {
  auto info = Utils::OpenDirectHandle(this);
  EnsureNotPublished(info, "v8::FunctionTemplate::SetCallHandler");
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScope scope(i_isolate);
  info->set_has_side_effects(side_effect_type !=
                             SideEffectType::kHasNoSideEffect);
  info->set_callback(i_isolate, reinterpret_cast<i::Address>(callback));
  if (data.IsEmpty()) {
    data = v8::Undefined(reinterpret_cast<v8::Isolate*>(i_isolate));
  }
  // "Release" callback and callback data fields.
  info->set_callback_data(*Utils::OpenDirectHandle(*data), kReleaseStore);

  if (!c_function_overloads.empty()) {
    // Stores the data for a sequence of CFunction overloads into a single
    // FixedArray, as [address_0, signature_0, ... address_n-1, signature_n-1].
    i::DirectHandle<i::FixedArray> function_overloads =
        i_isolate->factory()->NewFixedArray(static_cast<int>(
            c_function_overloads.size() *
            i::FunctionTemplateInfo::kFunctionOverloadEntrySize));
    int function_count = static_cast<int>(c_function_overloads.size());
    for (int i = 0; i < function_count; i++) {
      const CFunction& c_function = c_function_overloads.data()[i];
      i::DirectHandle<i::Object> address = FromCData<internal::kCFunctionTag>(
          i_isolate, c_function.GetAddress());
      function_overloads->set(
          i::FunctionTemplateInfo::kFunctionOverloadEntrySize * i, *address);
      i::DirectHandle<i::Object> signature =
          FromCData<internal::kCFunctionInfoTag>(i_isolate,
                                                 c_function.GetTypeInfo());
      function_overloads->set(
          i::FunctionTemplateInfo::kFunctionOverloadEntrySize * i + 1,
          *signature);
    }
    i::FunctionTemplateInfo::SetCFunctionOverloads(i_isolate, info,
                                                   function_overloads);
  }
}

namespace {

template <typename Getter, typename Setter>
i::DirectHandle<i::AccessorInfo> MakeAccessorInfo(i::Isolate* i_isolate,
                                                  v8::Local<Name> name,
                                                  Getter getter, Setter setter,
                                                  v8::Local<Value> data,
                                                  bool replace_on_access) {
  i::DirectHandle<i::AccessorInfo> obj =
      i_isolate->factory()->NewAccessorInfo();
  obj->set_getter(i_isolate, reinterpret_cast<i::Address>(getter));
  DCHECK_IMPLIES(replace_on_access, setter == nullptr);
  if (setter == nullptr) {
    setter = reinterpret_cast<Setter>(&i::Accessors::ReconfigureToDataProperty);
  }
  obj->set_setter(i_isolate, reinterpret_cast<i::Address>(setter));

  auto accessor_name = Utils::OpenDirectHandle(*name);
  if (!IsUniqueName(*accessor_name)) {
    accessor_name = i_isolate->factory()->InternalizeString(
        i::Cast<i::String>(accessor_name));
  }
  i::DisallowGarbageCollection no_gc;
  i::Tagged<i::AccessorInfo> raw_obj = *obj;
  if (data.IsEmpty()) {
    raw_obj->set_data(i::ReadOnlyRoots(i_isolate).undefined_value());
  } else {
    raw_obj->set_data(*Utils::OpenDirectHandle(*data));
  }
  raw_obj->set_name(*accessor_name);
  raw_obj->set_replace_on_access(replace_on_access);
  raw_obj->set_initial_property_attributes(i::NONE);
  return obj;
}

}  // namespace

Local<ObjectTemplate> FunctionTemplate::InstanceTemplate() {
  auto constructor = Utils::OpenDirectHandle(this, true);
  if (!Utils::ApiCheck(!constructor.is_null(),
                       "v8::FunctionTemplate::InstanceTemplate()",
                       "Reading from empty handle")) {
    return {};
  }
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  auto maybe_templ = constructor->GetInstanceTemplate();
  if (!i::IsUndefined(maybe_templ, i_isolate)) {
    return Utils::ToLocal(i::direct_handle(
        i::Cast<i::ObjectTemplateInfo>(maybe_templ), i_isolate));
  }
  constexpr bool do_not_cache = false;
  i::DirectHandle<i::ObjectTemplateInfo> templ =
      i_isolate->factory()->NewObjectTemplateInfo(constructor, do_not_cache);
  i::FunctionTemplateInfo::SetInstanceTemplate(i_isolate, constructor, templ);
  return Utils::ToLocal(templ);
}

void FunctionTemplate::SetLength(int length) {
  auto info = Utils::OpenDirectHandle(this);
  EnsureNotPublished(info, "v8::FunctionTemplate::SetLength");
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  info->set_length(length);
}

void FunctionTemplate::SetClassName(Local<String> name) {
  auto info = Utils::OpenDirectHandle(this);
  EnsureNotPublished(info, "v8::FunctionTemplate::SetClassName");
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  info->set_class_name(*Utils::OpenDirectHandle(*name));
}

void FunctionTemplate::SetInterfaceName(Local<String> name) {
  auto info = Utils::OpenDirectHandle(this);
  EnsureNotPublished(info, "v8::FunctionTemplate::SetInterfaceName");
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  info->set_interface_name(*Utils::OpenDirectHandle(*name));
}

void FunctionTemplate::SetExceptionContext(ExceptionContext context) {
  auto info = Utils::OpenDirectHandle(this);
  EnsureNotPublished(info, "v8::FunctionTemplate::SetExceptionContext");
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  info->set_exception_context(static_cast<uint32_t>(context));
}

void FunctionTemplate::SetAcceptAnyReceiver(bool value) {
  auto info = Utils::OpenDirectHandle(this);
  EnsureNotPublished(info, "v8::FunctionTemplate::SetAcceptAnyReceiver");
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  info->set_accept_any_receiver(value);
}

void FunctionTemplate::ReadOnlyPrototype() {
  auto info = Utils::OpenDirectHandle(this);
  EnsureNotPublished(info, "v8::FunctionTemplate::ReadOnlyPrototype");
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  info->set_read_only_prototype(true);
}

void FunctionTemplate::RemovePrototype() {
  auto info = Utils::OpenDirectHandle(this);
  EnsureNotPublished(info, "v8::FunctionTemplate::RemovePrototype");
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  info->set_remove_prototype(true);
}

// --- O b j e c t T e m p l a t e ---

Local<ObjectTemplate> ObjectTemplate::New(
    Isolate* v8_isolate, v8::Local<FunctionTemplate> constructor) {
  auto i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_ObjectTemplate_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  constexpr bool do_not_cache = false;
  i::DirectHandle<i::ObjectTemplateInfo> obj =
      i_isolate->factory()->NewObjectTemplateInfo(
          Utils::OpenDirectHandle(*constructor, true), do_not_cache);
  return Utils::ToLocal(obj);
}

namespace {
// Ensure that the object template has a constructor.  If no
// constructor is available we create one.
i::DirectHandle<i::FunctionTemplateInfo> EnsureConstructor(
    i::Isolate* i_isolate, ObjectTemplate* object_template) {
  i::Tagged<i::Object> obj =
      Utils::OpenDirectHandle(object_template)->constructor();
  if (!IsUndefined(obj, i_isolate)) {
    i::Tagged<i::FunctionTemplateInfo> info =
        i::Cast<i::FunctionTemplateInfo>(obj);
    return i::DirectHandle<i::FunctionTemplateInfo>(info, i_isolate);
  }
  Local<FunctionTemplate> templ =
      FunctionTemplate::New(reinterpret_cast<Isolate*>(i_isolate));
  auto constructor = Utils::OpenDirectHandle(*templ);
  i::FunctionTemplateInfo::SetInstanceTemplate(
      i_isolate, constructor, Utils::OpenDirectHandle(object_template));
  Utils::OpenDirectHandle(object_template)->set_constructor(*constructor);
  return constructor;
}

template <typename Getter, typename Setter, typename Data, typename Template>
void TemplateSetAccessor(Template* template_obj, v8::Local<Name> name,
                         Getter getter, Setter setter, Data data,
                         PropertyAttribute attribute, bool replace_on_access,
                         SideEffectType getter_side_effect_type,
                         SideEffectType setter_side_effect_type) {
  auto info = Utils::OpenDirectHandle(template_obj);
  auto i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScope scope(i_isolate);
  i::DirectHandle<i::AccessorInfo> accessor_info = MakeAccessorInfo(
      i_isolate, name, getter, setter, data, replace_on_access);
  {
    i::DisallowGarbageCollection no_gc;
    i::Tagged<i::AccessorInfo> raw = *accessor_info;
    raw->set_initial_property_attributes(
        static_cast<i::PropertyAttributes>(attribute));
    raw->set_getter_side_effect_type(getter_side_effect_type);
    raw->set_setter_side_effect_type(setter_side_effect_type);
  }
  i::ApiNatives::AddNativeDataProperty(i_isolate, info, accessor_info);
}
}  // namespace

void Template::SetNativeDataProperty(v8::Local<Name> name,
                                     AccessorNameGetterCallback getter,
                                     AccessorNameSetterCallback setter,
                                     v8::Local<Value> data,
                                     PropertyAttribute attribute,
                                     SideEffectType getter_side_effect_type,
                                     SideEffectType setter_side_effect_type) {
  TemplateSetAccessor(this, name, getter, setter, data, attribute, false,
                      getter_side_effect_type, setter_side_effect_type);
}

void Template::SetLazyDataProperty(v8::Local<Name> name,
                                   AccessorNameGetterCallback getter,
                                   v8::Local<Value> data,
                                   PropertyAttribute attribute,
                                   SideEffectType getter_side_effect_type,
                                   SideEffectType setter_side_effect_type) {
  TemplateSetAccessor(
      this, name, getter, static_cast<AccessorNameSetterCallback>(nullptr),
      data, attribute, true, getter_side_effect_type, setter_side_effect_type);
}

void Template::SetIntrinsicDataProperty(Local<Name> name, Intrinsic intrinsic,
                                        PropertyAttribute attribute) {
  auto templ = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScope scope(i_isolate);
  i::ApiNatives::AddDataProperty(i_isolate, templ,
                                 Utils::OpenDirectHandle(*name), intrinsic,
                                 static_cast<i::PropertyAttributes>(attribute));
}

namespace {
enum class PropertyType { kNamed, kIndexed };
template <PropertyType property_type, typename Getter, typename Setter,
          typename Query, typename Descriptor, typename Deleter,
          typename Enumerator, typename Definer>
i::DirectHandle<i::InterceptorInfo> CreateInterceptorInfo(
    i::Isolate* i_isolate, Getter getter, Setter setter, Query query,
    Descriptor descriptor, Deleter deleter, Enumerator enumerator,
    Definer definer, Local<Value> data,
    base::Flags<PropertyHandlerFlags> flags) {
  // TODO(saelo): instead of an in-sandbox struct with a lot of external
  // pointers (with different tags), consider creating an object in trusted
  // space instead. That way, only a single reference going out of the sandbox
  // would be required.
  auto obj = i_isolate->factory()->NewInterceptorInfo();
  obj->set_is_named(property_type == PropertyType::kNamed);

#define SET_CALLBACK_FIELD(Name, name)                                        \
  if (name != nullptr) {                                                      \
    if constexpr (property_type == PropertyType::kNamed) {                    \
      obj->set_named_##name(i_isolate, reinterpret_cast<i::Address>(name));   \
    } else {                                                                  \
      obj->set_indexed_##name(i_isolate, reinterpret_cast<i::Address>(name)); \
    }                                                                         \
  }
  INTERCEPTOR_INFO_CALLBACK_LIST(SET_CALLBACK_FIELD)
#undef SET_CALLBACK_FIELD

  obj->set_can_intercept_symbols(
      !(flags & PropertyHandlerFlags::kOnlyInterceptStrings));
  obj->set_non_masking(flags & PropertyHandlerFlags::kNonMasking);
  obj->set_has_no_side_effect(flags & PropertyHandlerFlags::kHasNoSideEffect);

  if (data.IsEmpty()) {
    data = v8::Undefined(reinterpret_cast<v8::Isolate*>(i_isolate));
  }
  obj->set_data(*Utils::OpenDirectHandle(*data));
  return obj;
}

template <typename Getter, typename Setter, typename Query, typename Descriptor,
          typename Deleter, typename Enumerator, typename Definer>
i::DirectHandle<i::InterceptorInfo> CreateNamedInterceptorInfo(
    i::Isolate* i_isolate, Getter getter, Setter setter, Query query,
    Descriptor descriptor, Deleter remover, Enumerator enumerator,
    Definer definer, Local<Value> data,
    base::Flags<PropertyHandlerFlags> flags) {
  auto interceptor = CreateInterceptorInfo<PropertyType::kNamed>(
      i_isolate, getter, setter, query, descriptor, remover, enumerator,
      definer, data, flags);
  return interceptor;
}

template <typename Getter, typename Setter, typename Query, typename Descriptor,
          typename Deleter, typename Enumerator, typename Definer>
i::DirectHandle<i::InterceptorInfo> CreateIndexedInterceptorInfo(
    i::Isolate* i_isolate, Getter getter, Setter setter, Query query,
    Descriptor descriptor, Deleter remover, Enumerator enumerator,
    Definer definer, Local<Value> data,
    base::Flags<PropertyHandlerFlags> flags) {
  auto interceptor = CreateInterceptorInfo<PropertyType::kIndexed>(
      i_isolate, getter, setter, query, descriptor, remover, enumerator,
      definer, data, flags);
  return interceptor;
}

template <typename Getter, typename Setter, typename Query, typename Descriptor,
          typename Deleter, typename Enumerator, typename Definer>
void ObjectTemplateSetNamedPropertyHandler(
    ObjectTemplate* templ, Getter getter, Setter setter, Query query,
    Descriptor descriptor, Deleter remover, Enumerator enumerator,
    Definer definer, Local<Value> data, PropertyHandlerFlags flags) {
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScope scope(i_isolate);
  auto cons = EnsureConstructor(i_isolate, templ);
  EnsureNotPublished(cons, "ObjectTemplateSetNamedPropertyHandler");
  auto obj =
      CreateNamedInterceptorInfo(i_isolate, getter, setter, query, descriptor,
                                 remover, enumerator, definer, data, flags);
  i::FunctionTemplateInfo::SetNamedPropertyHandler(i_isolate, cons, obj);
}
}  // namespace

void ObjectTemplate::SetHandler(
    const NamedPropertyHandlerConfiguration& config) {
  ObjectTemplateSetNamedPropertyHandler(
      this, config.getter, config.setter, config.query, config.descriptor,
      config.deleter, config.enumerator, config.definer, config.data,
      config.flags);
}

void ObjectTemplate::MarkAsUndetectable() {
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScope scope(i_isolate);
  auto cons = EnsureConstructor(i_isolate, this);
  EnsureNotPublished(cons, "v8::ObjectTemplate::MarkAsUndetectable");
  cons->set_undetectable(true);
}

void ObjectTemplate::SetAccessCheckCallback(AccessCheckCallback callback,
                                            Local<Value> data) {
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScope scope(i_isolate);
  auto cons = EnsureConstructor(i_isolate, this);
  EnsureNotPublished(cons, "v8::ObjectTemplate::SetAccessCheckCallback");

  i::DirectHandle<i::Struct> struct_info = i_isolate->factory()->NewStruct(
      i::ACCESS_CHECK_INFO_TYPE, i::AllocationType::kOld);
  auto info = i::Cast<i::AccessCheckInfo>(struct_info);

  SET_FIELD_WRAPPED(i_isolate, info, set_callback, callback,
                    internal::kApiAccessCheckCallbackTag);
  info->set_named_interceptor(i::Smi::zero());
  info->set_indexed_interceptor(i::Smi::zero());

  if (data.IsEmpty()) {
    data = v8::Undefined(reinterpret_cast<v8::Isolate*>(i_isolate));
  }
  info->set_data(*Utils::OpenDirectHandle(*data));

  i::FunctionTemplateInfo::SetAccessCheckInfo(i_isolate, cons, info);
  cons->set_needs_access_check(true);
}

void ObjectTemplate::SetAccessCheckCallbackAndHandler(
    AccessCheckCallback callback,
    const NamedPropertyHandlerConfiguration& named_handler,
    const IndexedPropertyHandlerConfiguration& indexed_handler,
    Local<Value> data) {
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScope scope(i_isolate);
  auto cons = EnsureConstructor(i_isolate, this);
  EnsureNotPublished(cons,
                     "v8::ObjectTemplate::SetAccessCheckCallbackWithHandler");

  i::DirectHandle<i::Struct> struct_info = i_isolate->factory()->NewStruct(
      i::ACCESS_CHECK_INFO_TYPE, i::AllocationType::kOld);
  auto info = i::Cast<i::AccessCheckInfo>(struct_info);

  SET_FIELD_WRAPPED(i_isolate, info, set_callback, callback,
                    internal::kApiAccessCheckCallbackTag);
  auto named_interceptor = CreateNamedInterceptorInfo(
      i_isolate, named_handler.getter, named_handler.setter,
      named_handler.query, named_handler.descriptor, named_handler.deleter,
      named_handler.enumerator, named_handler.definer, named_handler.data,
      named_handler.flags);
  info->set_named_interceptor(*named_interceptor);
  auto indexed_interceptor = CreateIndexedInterceptorInfo(
      i_isolate, indexed_handler.getter, indexed_handler.setter,
      indexed_handler.query, indexed_handler.descriptor,
      indexed_handler.deleter, indexed_handler.enumerator,
      indexed_handler.definer, indexed_handler.data, indexed_handler.flags);
  info->set_indexed_interceptor(*indexed_interceptor);

  if (data.IsEmpty()) {
    data = v8::Undefined(reinterpret_cast<v8::Isolate*>(i_isolate));
  }
  info->set_data(*Utils::OpenDirectHandle(*data));

  i::FunctionTemplateInfo::SetAccessCheckInfo(i_isolate, cons, info);
  cons->set_needs_access_check(true);
}

void ObjectTemplate::SetHandler(
    const IndexedPropertyHandlerConfiguration& config) {
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScope scope(i_isolate);
  auto cons = EnsureConstructor(i_isolate, this);
  EnsureNotPublished(cons, "v8::ObjectTemplate::SetHandler");
  auto obj = CreateIndexedInterceptorInfo(
      i_isolate, config.getter, config.setter, config.query, config.descriptor,
      config.deleter, config.enumerator, config.definer, config.data,
      config.flags);
  i::FunctionTemplateInfo::SetIndexedPropertyHandler(i_isolate, cons, obj);
}

void ObjectTemplate::SetCallAsFunctionHandler(FunctionCallback callback,
                                              Local<Value> data) {
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScope scope(i_isolate);
  auto cons = EnsureConstructor(i_isolate, this);
  EnsureNotPublished(cons, "v8::ObjectTemplate::SetCallAsFunctionHandler");
  DCHECK_NOT_NULL(callback);

  // This template is just a container for callback and data values and thus
  // it's not supposed to be instantiated. Don't cache it.
  constexpr bool do_not_cache = true;
  constexpr int length = 0;
  i::DirectHandle<i::FunctionTemplateInfo> templ =
      i_isolate->factory()->NewFunctionTemplateInfo(length, do_not_cache);
  templ->set_is_object_template_call_handler(true);
  Utils::ToLocal(templ)->SetCallHandler(callback, data);
  i::FunctionTemplateInfo::SetInstanceCallHandler(i_isolate, cons, templ);
}

int ObjectTemplate::InternalFieldCount() const {
  return Utils::OpenDirectHandle(this)->embedder_field_count();
}

void ObjectTemplate::SetInternalFieldCount(int value) {
  i::Isolate* i_isolate = i::Isolate::Current();
  if (!Utils::ApiCheck(i::Smi::IsValid(value),
                       "v8::ObjectTemplate::SetInternalFieldCount()",
                       "Invalid embedder field count")) {
    return;
  }
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  if (value > 0) {
    // The embedder field count is set by the constructor function's
    // construct code, so we ensure that there is a constructor
    // function to do the setting.
    EnsureConstructor(i_isolate, this);
  }
  Utils::OpenDirectHandle(this)->set_embedder_field_count(value);
}

bool ObjectTemplate::IsImmutableProto() const {
  return Utils::OpenDirectHandle(this)->immutable_proto();
}

void ObjectTemplate::SetImmutableProto() {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  self->set_immutable_proto(true);
}

bool ObjectTemplate::IsCodeLike() const {
  return Utils::OpenDirectHandle(this)->code_like();
}

void ObjectTemplate::SetCodeLike() {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  self->set_code_like(true);
}

Local<DictionaryTemplate> DictionaryTemplate::New(
    Isolate* isolate, MemorySpan<const std::string_view> names) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_DictionaryTemplate_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  return Utils::ToLocal(i::DictionaryTemplateInfo::Create(i_isolate, names));
}

Local<Object> DictionaryTemplate::NewInstance(
    Local<Context> context, MemorySpan<MaybeLocal<Value>> property_values) {
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(
      i_isolate, RCCId::kAPI_DictionaryTemplate_NewInstance);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  auto self = Utils::OpenDirectHandle(this);
  return ToApiHandle<Object>(i::DictionaryTemplateInfo::NewInstance(
      Utils::OpenDirectHandle(*context), self, property_values));
}

// --- S c r i p t s ---

// Internally, UnboundScript and UnboundModuleScript are SharedFunctionInfos,
// and Script is a JSFunction.

ScriptCompiler::CachedData::CachedData(const uint8_t* data_, int length_,
                                       BufferPolicy buffer_policy_)
    : data(data_),
      length(length_),
      rejected(false),
      buffer_policy(buffer_policy_) {}

ScriptCompiler::CachedData::~CachedData() {
  if (buffer_policy == BufferOwned) {
    delete[] data;
  }
}

ScriptCompiler::CachedData::CompatibilityCheckResult
ScriptCompiler::CachedData::CompatibilityCheck(Isolate* isolate) {
  i::AlignedCachedData aligned(data, length);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  i::SerializedCodeSanityCheckResult result;
  i::SerializedCodeData scd =
      i::SerializedCodeData::FromCachedDataWithoutSource(
          i_isolate->AsLocalIsolate(), &aligned, &result);
  return static_cast<ScriptCompiler::CachedData::CompatibilityCheckResult>(
      result);
}

ScriptCompiler::StreamedSource::StreamedSource(
    std::unique_ptr<ExternalSourceStream> stream, Encoding encoding)
    : impl_(new i::ScriptStreamingData(std::move(stream), encoding)) {}

ScriptCompiler::StreamedSource::~StreamedSource() = default;

Local<Script> UnboundScript::BindToCurrentContext() {
  auto function_info = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::JSFunction> function =
      i::Factory::JSFunctionBuilder{i_isolate, function_info,
                                    i_isolate->native_context()}
          .Build();
  return ToApiHandle<Script>(function);
}

int UnboundScript::GetId() const {
  auto function_info = Utils::OpenDirectHandle(this);
  ApiRuntimeCallStatsScope rcs_scope(i::Isolate::Current(),
                                     RCCId::kAPI_UnboundScript_GetId);
  return i::Cast<i::Script>(function_info->script())->id();
}

int UnboundScript::GetLineNumber(int code_pos) {
  auto obj = Utils::OpenDirectHandle(this);
  if (!i::IsScript(obj->script())) return -1;
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_UnboundScript_GetLineNumber);
  i::DirectHandle<i::Script> script(i::Cast<i::Script>(obj->script()),
                                    i_isolate);
  return i::Script::GetLineNumber(script, code_pos);
}

int UnboundScript::GetColumnNumber(int code_pos) {
  auto obj = Utils::OpenDirectHandle(this);
  if (!i::IsScript(obj->script())) return -1;
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_UnboundScript_GetColumnNumber);
  i::DirectHandle<i::Script> script(i::Cast<i::Script>(obj->script()),
                                    i_isolate);
  return i::Script::GetColumnNumber(script, code_pos);
}

Local<Value> UnboundScript::GetScriptName() {
  auto obj = Utils::OpenDirectHandle(this);
  if (!i::IsScript(obj->script())) return {};
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_UnboundScript_GetName);
  i::Tagged<i::Object> name = i::Cast<i::Script>(obj->script())->name();
  return Utils::ToLocal(i::direct_handle(name, i_isolate));
}

Local<Value> UnboundScript::GetSourceURL() {
  auto obj = Utils::OpenDirectHandle(this);
  if (!i::IsScript(obj->script())) return {};
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_UnboundScript_GetSourceURL);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::Tagged<i::Object> url = i::Cast<i::Script>(obj->script())->source_url();
  return Utils::ToLocal(i::direct_handle(url, i_isolate));
}

Local<Value> UnboundScript::GetSourceMappingURL() {
  auto obj = Utils::OpenDirectHandle(this);
  if (!i::IsScript(obj->script())) return {};
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(
      i_isolate, RCCId::kAPI_UnboundScript_GetSourceMappingURL);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::Tagged<i::Object> url =
      i::Cast<i::Script>(obj->script())->source_mapping_url();
  return Utils::ToLocal(i::direct_handle(url, i_isolate));
}

Local<Value> UnboundModuleScript::GetSourceURL() {
  auto obj = Utils::OpenDirectHandle(this);
  if (!i::IsScript(obj->script())) return {};
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(
      i_isolate, RCCId::kAPI_UnboundModuleScript_GetSourceURL);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::Tagged<i::Object> url = i::Cast<i::Script>(obj->script())->source_url();
  return Utils::ToLocal(i::direct_handle(url, i_isolate));
}

Local<Value> UnboundModuleScript::GetSourceMappingURL() {
  auto obj = Utils::OpenDirectHandle(this);
  if (!i::IsScript(obj->script())) return {};
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(
      i_isolate, RCCId::kAPI_UnboundModuleScript_GetSourceMappingURL);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::Tagged<i::Object> url =
      i::Cast<i::Script>(obj->script())->source_mapping_url();
  return Utils::ToLocal(i::direct_handle(url, i_isolate));
}

MaybeLocal<Value> Script::Run(Local<Context> context) {
  return Run(context, Local<Data>());
}

MaybeLocal<Value> Script::Run(Local<Context> context,
                              Local<Data> host_defined_options) {
  auto i_isolate = i::Isolate::Current();
  TRACE_EVENT_CALL_STATS_SCOPED(i_isolate, "v8", "V8.Execute");
  EnterV8Scope<InternalEscapableScope> api_scope{i_isolate, context,
                                                 RCCId::kAPI_Script_Run};
  i::TimerEventScope<i::TimerEventExecute> timer_scope(i_isolate);
  i::NestedTimedHistogramScope execute_timer(i_isolate->counters()->execute(),
                                             i_isolate);
  i::AggregatingHistogramTimerScope histogram_timer(
      i_isolate->counters()->compile_lazy());

#if defined(V8_ENABLE_ETW_STACK_WALKING)
  // In case ETW has been activated, tasks to log existing code are
  // created. But in case the task runner does not run those before
  // starting to execute code (as it happens in d8, that will run
  // first the code from prompt), then that code will not have
  // JIT instrumentation on time.
  //
  // To avoid this, on running scripts check first if JIT code log is
  // pending and generate immediately.
  if (i::v8_flags.enable_etw_stack_walking ||
      i::v8_flags.enable_etw_by_custom_filter_only) {
    i::ETWJITInterface::MaybeSetHandlerNow(i_isolate);
  }
#endif  // V8_ENABLE_ETW_STACK_WALKING
  auto fun = i::Cast<i::JSFunction>(Utils::OpenDirectHandle(this));
  i::DirectHandle<i::Object> receiver = i_isolate->global_proxy();
  // TODO(cbruni, chromium:1244145): Remove once migrated to the context.
  i::DirectHandle<i::Object> options(
      i::Cast<i::Script>(fun->shared()->script())->host_defined_options(),
      i_isolate);
  return api_scope.EscapeMaybe(
      i::Execution::CallScript(i_isolate, fun, receiver, options));
}

Local<Value> ScriptOrModule::GetResourceName() {
  auto obj = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  return ToApiHandle<Value>(i::direct_handle(obj->resource_name(), i_isolate));
}

Local<Data> ScriptOrModule::HostDefinedOptions() {
  auto obj = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  return ToApiHandle<Data>(
      i::direct_handle(obj->host_defined_options(), i_isolate));
}

Local<UnboundScript> Script::GetUnboundScript() {
  i::DisallowGarbageCollection no_gc;
  auto obj = Utils::OpenDirectHandle(this);
  i::DirectHandle<i::SharedFunctionInfo> sfi(obj->shared(),
                                             i::Isolate::Current());
  DCHECK(!i::HeapLayout::InReadOnlySpace(*sfi));
  return ToApiHandle<UnboundScript>(sfi);
}

Local<Value> Script::GetResourceName() {
  i::DisallowGarbageCollection no_gc;
  auto func = Utils::OpenDirectHandle(this);
  i::Tagged<i::SharedFunctionInfo> sfi = func->shared();
  CHECK(IsScript(sfi->script()));
  i::Isolate* i_isolate = i::Isolate::Current();
  return ToApiHandle<Value>(
      i::direct_handle(i::Cast<i::Script>(sfi->script())->name(), i_isolate));
}

std::vector<int> Script::GetProducedCompileHints() const {
  i::DisallowGarbageCollection no_gc;
  auto func = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  i::Tagged<i::SharedFunctionInfo> sfi = func->shared();
  CHECK(IsScript(sfi->script()));
  i::Tagged<i::Script> script = i::Cast<i::Script>(sfi->script());
  i::Tagged<i::Object> maybe_array_list =
      script->compiled_lazy_function_positions();
  std::vector<int> result;
  if (!IsUndefined(maybe_array_list, i_isolate)) {
    i::Tagged<i::ArrayList> array_list =
        i::Cast<i::ArrayList>(maybe_array_list);
    result.reserve(array_list->length());
    for (int i = 0; i < array_list->length(); ++i) {
      i::Tagged<i::Object> item = array_list->get(i);
      CHECK(IsSmi(item));
      result.push_back(i::Smi::ToInt(item));
    }
  }
  return result;
}

Local<CompileHintsCollector> Script::GetCompileHintsCollector() const {
  i::DisallowGarbageCollection no_gc;
  auto func = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  i::Tagged<i::SharedFunctionInfo> sfi = func->shared();
  CHECK(IsScript(sfi->script()));
  i::DirectHandle<i::Script> script(i::Cast<i::Script>(sfi->script()),
                                    i_isolate);
  return ToApiHandle<CompileHintsCollector>(script);
}

std::vector<int> CompileHintsCollector::GetCompileHints(
    Isolate* v8_isolate) const {
  i::DisallowGarbageCollection no_gc;
  auto script = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::Tagged<i::Object> maybe_array_list =
      script->compiled_lazy_function_positions();
  std::vector<int> result;
  if (!IsUndefined(maybe_array_list, i_isolate)) {
    i::Tagged<i::ArrayList> array_list =
        i::Cast<i::ArrayList>(maybe_array_list);
    result.reserve(array_list->length());
    for (int i = 0; i < array_list->length(); ++i) {
      i::Tagged<i::Object> item = array_list->get(i);
      CHECK(IsSmi(item));
      result.push_back(i::Smi::ToInt(item));
    }
  }
  return result;
}

// static
Local<PrimitiveArray> PrimitiveArray::New(Isolate* v8_isolate, int length) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  Utils::ApiCheck(length >= 0, "v8::PrimitiveArray::New",
                  "length must be equal or greater than zero");
  i::DirectHandle<i::FixedArray> array =
      i_isolate->factory()->NewFixedArray(length);
  return ToApiHandle<PrimitiveArray>(array);
}

int PrimitiveArray::Length() const {
  return Utils::OpenDirectHandle(this)->length();
}

void PrimitiveArray::Set(Isolate* v8_isolate, int index,
                         Local<Primitive> item) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  auto array = Utils::OpenDirectHandle(this);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  Utils::ApiCheck(index >= 0 && index < array->length(),
                  "v8::PrimitiveArray::Set",
                  "index must be greater than or equal to 0 and less than the "
                  "array length");
  array->set(index, *Utils::OpenDirectHandle(*item));
}

Local<Primitive> PrimitiveArray::Get(Isolate* v8_isolate, int index) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  auto array = Utils::OpenDirectHandle(this);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  Utils::ApiCheck(index >= 0 && index < array->length(),
                  "v8::PrimitiveArray::Get",
                  "index must be greater than or equal to 0 and less than the "
                  "array length");
  return ToApiHandle<Primitive>(i::direct_handle(array->get(index), i_isolate));
}

void v8::PrimitiveArray::CheckCast(v8::Data* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(
      i::IsFixedArray(*obj), "v8::PrimitiveArray::Cast",
      "Value is not a PrimitiveArray; this is a temporary issue, v8::Data and "
      "v8::PrimitiveArray will not be compatible in the future");
}

int FixedArray::Length() const {
  return Utils::OpenDirectHandle(this)->length();
}

Local<Data> FixedArray::Get(Local<Context> context, int i) const {
  auto self = Utils::OpenDirectHandle(this);
  auto i_isolate = i::Isolate::Current();
  CHECK_LT(i, self->length());
  return ToApiHandle<Data>(i::direct_handle(self->get(i), i_isolate));
}

Local<String> ModuleRequest::GetSpecifier() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  return ToApiHandle<String>(i::direct_handle(self->specifier(), i_isolate));
}

ModuleImportPhase ModuleRequest::GetPhase() const {
  auto self = Utils::OpenDirectHandle(this);
  return self->phase();
}

int ModuleRequest::GetSourceOffset() const {
  return Utils::OpenDirectHandle(this)->position();
}

Local<FixedArray> ModuleRequest::GetImportAttributes() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  return ToApiHandle<FixedArray>(
      i::direct_handle(self->import_attributes(), i_isolate));
}

Module::Status Module::GetStatus() const {
  auto self = Utils::OpenDirectHandle(this);
  switch (self->status()) {
    case i::Module::kUnlinked:
    case i::Module::kPreLinking:
      return kUninstantiated;
    case i::Module::kLinking:
      return kInstantiating;
    case i::Module::kLinked:
      return kInstantiated;
    case i::Module::kEvaluating:
      return kEvaluating;
    case i::Module::kEvaluatingAsync:
      // TODO(syg): Expose kEvaluatingAsync in API as well.
    case i::Module::kEvaluated:
      return kEvaluated;
    case i::Module::kErrored:
      return kErrored;
  }
  UNREACHABLE();
}

Local<Value> Module::GetException() const {
  Utils::ApiCheck(GetStatus() == kErrored, "v8::Module::GetException",
                  "Module status must be kErrored");
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  return ToApiHandle<Value>(i::direct_handle(self->GetException(), i_isolate));
}

Local<FixedArray> Module::GetModuleRequests() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  i::DisallowJavascriptExecutionDebugOnly no_execution(i_isolate);
  i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);
  if (i::IsSyntheticModule(*self)) {
    // Synthetic modules are leaf nodes in the module graph. They have no
    // ModuleRequests.
    return ToApiHandle<FixedArray>(i_isolate->factory()->empty_fixed_array());
  } else {
    return ToApiHandle<FixedArray>(i::direct_handle(
        i::Cast<i::SourceTextModule>(self)->info()->module_requests(),
        i_isolate));
  }
}

Location Module::SourceOffsetToLocation(int offset) const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScope scope(i_isolate);
  Utils::ApiCheck(
      i::IsSourceTextModule(*self), "v8::Module::SourceOffsetToLocation",
      "v8::Module::SourceOffsetToLocation must be used on an SourceTextModule");
  i::DirectHandle<i::Script> script(
      i::Cast<i::SourceTextModule>(self)->GetScript(), i_isolate);
  i::Script::PositionInfo info;
  i::Script::GetPositionInfo(script, offset, &info);
  return v8::Location(info.line, info.column);
}

Local<Value> Module::GetModuleNamespace() {
  Utils::ApiCheck(
      GetStatus() >= kInstantiated, "v8::Module::GetModuleNamespace",
      "v8::Module::GetModuleNamespace must be used on an instantiated module");
  auto self = Utils::OpenHandle(this);
  auto i_isolate = i::Isolate::Current();
  i::DisallowJavascriptExecutionDebugOnly no_execution(i_isolate);
  i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);
  i::DirectHandle<i::JSModuleNamespace> module_namespace =
      i::Module::GetModuleNamespace(i_isolate, self);
  return ToApiHandle<Value>(module_namespace);
}

Local<UnboundModuleScript> Module::GetUnboundModuleScript() {
  auto self = Utils::OpenDirectHandle(this);
  Utils::ApiCheck(
      i::IsSourceTextModule(*self), "v8::Module::GetUnboundModuleScript",
      "v8::Module::GetUnboundModuleScript must be used on an SourceTextModule");
  auto i_isolate = i::Isolate::Current();
  i::DisallowJavascriptExecutionDebugOnly no_execution(i_isolate);
  i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);
  return ToApiHandle<UnboundModuleScript>(i::direct_handle(
      i::Cast<i::SourceTextModule>(self)->GetSharedFunctionInfo(), i_isolate));
}

int Module::ScriptId() const {
  i::Tagged<i::Module> self = *Utils::OpenDirectHandle(this);
  Utils::ApiCheck(i::IsSourceTextModule(self), "v8::Module::ScriptId",
                  "v8::Module::ScriptId must be used on an SourceTextModule");
  i::DisallowJavascriptExecutionDebugOnly no_execution;
  i::DisallowExceptionsDebugOnly no_exceptions;
  return i::Cast<i::SourceTextModule>(self)->GetScript()->id();
}

bool Module::HasTopLevelAwait() const {
  i::Tagged<i::Module> self = *Utils::OpenDirectHandle(this);
  if (!i::IsSourceTextModule(self)) return false;
  return i::Cast<i::SourceTextModule>(self)->has_toplevel_await();
}

bool Module::IsGraphAsync() const {
  Utils::ApiCheck(
      GetStatus() >= kInstantiated, "v8::Module::IsGraphAsync",
      "v8::Module::IsGraphAsync must be used on an instantiated module");
  i::Tagged<i::Module> self = *Utils::OpenDirectHandle(this);
  auto i_isolate = i::Isolate::Current();
  i::DisallowJavascriptExecutionDebugOnly no_execution(i_isolate);
  i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);
  return self->IsGraphAsync(i_isolate);
}

bool Module::IsSourceTextModule() const {
  auto self = Utils::OpenDirectHandle(this);
  i::DisallowJavascriptExecutionDebugOnly no_execution;
  i::DisallowExceptionsDebugOnly no_exceptions;
  return i::IsSourceTextModule(*self);
}

bool Module::IsSyntheticModule() const {
  auto self = Utils::OpenDirectHandle(this);
  i::DisallowJavascriptExecutionDebugOnly no_execution;
  i::DisallowExceptionsDebugOnly no_exceptions;
  return i::IsSyntheticModule(*self);
}

int Module::GetIdentityHash() const {
  auto self = Utils::OpenDirectHandle(this);
  i::DisallowJavascriptExecutionDebugOnly no_execution;
  i::DisallowExceptionsDebugOnly no_exceptions;
  return self->hash();
}

Maybe<bool> Module::InstantiateModule(Local<Context> context,
                                      ResolveModuleCallback module_callback,
                                      ResolveSourceCallback source_callback) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context,
                           RCCId::kAPI_Module_InstantiateModule};
  if (!i::Module::Instantiate(i_isolate, Utils::OpenHandle(this), context,
                              module_callback, source_callback)) {
    return {};
  }
  return Just(true);
}

MaybeLocal<Value> Module::Evaluate(Local<Context> context) {
  auto i_isolate = i::Isolate::Current();
  TRACE_EVENT_CALL_STATS_SCOPED(i_isolate, "v8", "V8.Execute");
  EnterV8Scope<InternalEscapableScope> api_scope{i_isolate, context,
                                                 RCCId::kAPI_Module_Evaluate};
  i::TimerEventScope<i::TimerEventExecute> timer_scope(i_isolate);
  i::NestedTimedHistogramScope execute_timer(i_isolate->counters()->execute(),
                                             i_isolate);
  i::AggregatingHistogramTimerScope timer(
      i_isolate->counters()->compile_lazy());

  auto self = Utils::OpenHandle(this);
  Utils::ApiCheck(self->status() >= i::Module::kLinked, "Module::Evaluate",
                  "Expected instantiated module");

  return api_scope.EscapeMaybe(i::Module::Evaluate(i_isolate, self));
}

Local<Module> Module::CreateSyntheticModule(
    Isolate* v8_isolate, Local<String> module_name,
    const MemorySpan<const Local<String>>& export_names,
    v8::Module::SyntheticModuleEvaluationSteps evaluation_steps) {
  auto i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  auto i_module_name = Utils::OpenDirectHandle(*module_name);
  i::DirectHandle<i::FixedArray> i_export_names =
      i_isolate->factory()->NewFixedArray(
          static_cast<int>(export_names.size()));
  for (int i = 0; i < i_export_names->length(); ++i) {
    i::DirectHandle<i::String> str = i_isolate->factory()->InternalizeString(
        Utils::OpenDirectHandle(*export_names[i]));
    i_export_names->set(i, *str);
  }
  return v8::Utils::ToLocal(
      i::DirectHandle<i::Module>(i_isolate->factory()->NewSyntheticModule(
          i_module_name, i_export_names, evaluation_steps)));
}

Maybe<bool> Module::SetSyntheticModuleExport(Isolate* v8_isolate,
                                             Local<String> export_name,
                                             Local<v8::Value> export_value) {
  auto i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  auto i_export_name = Utils::OpenDirectHandle(*export_name);
  auto i_export_value = Utils::OpenDirectHandle(*export_value);
  auto self = Utils::OpenDirectHandle(this);
  Utils::ApiCheck(i::IsSyntheticModule(*self),
                  "v8::Module::SyntheticModuleSetExport",
                  "v8::Module::SyntheticModuleSetExport must only be called on "
                  "a SyntheticModule");
  EnterV8NoScriptScope<> api_scope{i_isolate, v8_isolate->GetCurrentContext(),
                                   RCCId::kAPI_Module_SetSyntheticModuleExport};
  if (i::SyntheticModule::SetExport(i_isolate,
                                    i::Cast<i::SyntheticModule>(self),
                                    i_export_name, i_export_value)
          .IsNothing()) {
    return {};
  }
  return Just(true);
}

std::pair<LocalVector<Module>, LocalVector<Message>>
Module::GetStalledTopLevelAwaitMessages(Isolate* isolate) {
  auto i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  auto self = Utils::OpenDirectHandle(this);
  Utils::ApiCheck(i::IsSourceTextModule(*self),
                  "v8::Module::GetStalledTopLevelAwaitMessages",
                  "v8::Module::GetStalledTopLevelAwaitMessages must only be "
                  "called on a SourceTextModule");
  std::pair<i::DirectHandleVector<i::SourceTextModule>,
            i::DirectHandleVector<i::JSMessageObject>>
      stalled_awaits =
          i::Cast<i::SourceTextModule>(self)->GetStalledTopLevelAwaitMessages(
              i_isolate);

  LocalVector<Module> modules(isolate);
  if (size_t stalled_awaits_count = stalled_awaits.first.size();
      stalled_awaits_count > 0) {
    modules.reserve(stalled_awaits_count);
    for (auto module : stalled_awaits.first)
      modules.push_back(ToApiHandle<Module>(module));
  }
  LocalVector<Message> messages(isolate);
  if (size_t stalled_awaits_count = stalled_awaits.second.size();
      stalled_awaits_count > 0) {
    messages.reserve(stalled_awaits_count);
    for (auto message : stalled_awaits.second)
      messages.push_back(ToApiHandle<Message>(message));
  }

  return {modules, messages};
}

namespace {

i::ScriptDetails GetScriptDetails(
    i::Isolate* i_isolate, Local<Value> resource_name, int resource_line_offset,
    int resource_column_offset, Local<Value> source_map_url,
    Local<Data> host_defined_options, ScriptOriginOptions origin_options) {
  i::ScriptDetails script_details(Utils::OpenHandle(*(resource_name), true),
                                  origin_options);
  script_details.line_offset = resource_line_offset;
  script_details.column_offset = resource_column_offset;
  script_details.host_defined_options =
      host_defined_options.IsEmpty()
          ? i_isolate->factory()->empty_fixed_array()
          : Utils::OpenHandle(*(host_defined_options));
  if (!source_map_url.IsEmpty()) {
    script_details.source_map_url = Utils::OpenHandle(*(source_map_url));
  }
  return script_details;
}

}  // namespace

MaybeLocal<UnboundScript> ScriptCompiler::CompileUnboundInternal(
    Isolate* v8_isolate, Source* source, CompileOptions options,
    NoCacheReason no_cache_reason) {
  auto i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  TRACE_EVENT_CALL_STATS_SCOPED(i_isolate, "v8", "V8.ScriptCompiler");
  EnterV8NoScriptScope<InternalEscapableScope> api_scope{
      i_isolate, v8_isolate->GetCurrentContext(),
      RCCId::kAPI_ScriptCompiler_CompileUnbound};

  auto str = Utils::OpenHandle(*(source->source_string));

  i::DirectHandle<i::SharedFunctionInfo> result;
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"), "V8.CompileScript");
  i::ScriptDetails script_details = GetScriptDetails(
      i_isolate, source->resource_name, source->resource_line_offset,
      source->resource_column_offset, source->source_map_url,
      source->host_defined_options, source->resource_options);

  i::MaybeDirectHandle<i::SharedFunctionInfo> maybe_function_info;
  if (options & kConsumeCodeCache) {
    if (source->consume_cache_task) {
      // Take ownership of the internal deserialization task and clear it off
      // the consume task on the source.
      DCHECK_NOT_NULL(source->consume_cache_task->impl_);
      std::unique_ptr<i::BackgroundDeserializeTask> deserialize_task =
          std::move(source->consume_cache_task->impl_);
      maybe_function_info =
          i::Compiler::GetSharedFunctionInfoForScriptWithDeserializeTask(
              i_isolate, str, script_details, deserialize_task.get(), options,
              no_cache_reason, i::NOT_NATIVES_CODE,
              &source->compilation_details);
      source->cached_data->rejected = deserialize_task->rejected();
    } else {
      DCHECK(source->cached_data);
      // AlignedCachedData takes care of pointer-aligning the data.
      auto cached_data = std::make_unique<i::AlignedCachedData>(
          source->cached_data->data, source->cached_data->length);
      maybe_function_info =
          i::Compiler::GetSharedFunctionInfoForScriptWithCachedData(
              i_isolate, str, script_details, cached_data.get(), options,
              no_cache_reason, i::NOT_NATIVES_CODE,
              &source->compilation_details);
      source->cached_data->rejected = cached_data->rejected();
    }
  } else if (options & kConsumeCompileHints) {
    maybe_function_info =
        i::Compiler::GetSharedFunctionInfoForScriptWithCompileHints(
            i_isolate, str, script_details, source->compile_hint_callback,
            source->compile_hint_callback_data, options, no_cache_reason,
            i::NOT_NATIVES_CODE, &source->compilation_details);
  } else {
    // Compile without any cache.
    maybe_function_info = i::Compiler::GetSharedFunctionInfoForScript(
        i_isolate, str, script_details, options, no_cache_reason,
        i::NOT_NATIVES_CODE, &source->compilation_details);
  }

  if (!maybe_function_info.ToHandle(&result)) return {};
  DCHECK(!i::HeapLayout::InReadOnlySpace(*result));
  return api_scope.Escape(ToApiHandle<UnboundScript>(result));
}

MaybeLocal<UnboundScript> ScriptCompiler::CompileUnboundScript(
    Isolate* v8_isolate, Source* source, CompileOptions options,
    NoCacheReason no_cache_reason) {
  Utils::ApiCheck(
      !source->GetResourceOptions().IsModule(),
      "v8::ScriptCompiler::CompileUnboundScript",
      "v8::ScriptCompiler::CompileModule must be used to compile modules");
  return CompileUnboundInternal(v8_isolate, source, options, no_cache_reason);
}

MaybeLocal<Script> ScriptCompiler::Compile(Local<Context> context,
                                           Source* source,
                                           CompileOptions options,
                                           NoCacheReason no_cache_reason) {
  Utils::ApiCheck(
      !source->GetResourceOptions().IsModule(), "v8::ScriptCompiler::Compile",
      "v8::ScriptCompiler::CompileModule must be used to compile modules");
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  MaybeLocal<UnboundScript> maybe =
      CompileUnboundInternal(isolate, source, options, no_cache_reason);
  Local<UnboundScript> result;
  if (!maybe.ToLocal(&result)) return {};
  v8::Context::Scope scope(context);
  return result->BindToCurrentContext();
}

MaybeLocal<Module> ScriptCompiler::CompileModule(
    Isolate* v8_isolate, Source* source, CompileOptions options,
    NoCacheReason no_cache_reason) {
  Utils::ApiCheck(v8::ScriptCompiler::CompileOptionsIsValid(options),
                  "v8::ScriptCompiler::CompileModule",
                  "Invalid CompileOptions");
  Utils::ApiCheck(source->GetResourceOptions().IsModule(),
                  "v8::ScriptCompiler::CompileModule",
                  "Invalid ScriptOrigin: is_module must be true");
  MaybeLocal<UnboundScript> maybe =
      CompileUnboundInternal(v8_isolate, source, options, no_cache_reason);
  Local<UnboundScript> unbound;
  if (!maybe.ToLocal(&unbound)) return {};
  auto shared = Utils::OpenDirectHandle(*unbound);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  return ToApiHandle<Module>(i_isolate->factory()->NewSourceTextModule(shared));
}

// static
V8_WARN_UNUSED_RESULT MaybeLocal<Function> ScriptCompiler::CompileFunction(
    Local<Context> v8_context, Source* source, size_t arguments_count,
    Local<String> arguments[], size_t context_extension_count,
    Local<Object> context_extensions[], CompileOptions options,
    NoCacheReason no_cache_reason) {
  PrepareForExecutionScope api_scope{
      v8_context, RCCId::kAPI_ScriptCompiler_CompileFunction};
  i::Isolate* i_isolate = api_scope.i_isolate();
  TRACE_EVENT_CALL_STATS_SCOPED(i_isolate, "v8", "V8.ScriptCompiler");

  DCHECK(options == CompileOptions::kConsumeCodeCache ||
         options == CompileOptions::kEagerCompile ||
         options == CompileOptions::kNoCompileOptions);

  i::DirectHandle<i::Context> context = Utils::OpenDirectHandle(*v8_context);

  DCHECK(IsNativeContext(*context));

  i::Handle<i::FixedArray> arguments_list =
      i_isolate->factory()->NewFixedArray(static_cast<int>(arguments_count));
  for (int i = 0; i < static_cast<int>(arguments_count); i++) {
    auto argument = Utils::OpenDirectHandle(*arguments[i]);
    if (!i::String::IsIdentifier(i_isolate, argument)) return {};
    arguments_list->set(i, *argument);
  }

  for (size_t i = 0; i < context_extension_count; ++i) {
    i::DirectHandle<i::JSReceiver> extension =
        Utils::OpenDirectHandle(*context_extensions[i]);
    if (!IsJSObject(*extension)) return {};
    context = i_isolate->factory()->NewWithContext(
        context,
        i::ScopeInfo::CreateForWithScope(
            i_isolate,
            IsNativeContext(*context)
                ? i::Handle<i::ScopeInfo>::null()
                : i::Handle<i::ScopeInfo>(context->scope_info(), i_isolate)),
        extension);
  }

  i::ScriptDetails script_details = GetScriptDetails(
      i_isolate, source->resource_name, source->resource_line_offset,
      source->resource_column_offset, source->source_map_url,
      source->host_defined_options, source->resource_options);
  script_details.wrapped_arguments = arguments_list;

  std::unique_ptr<i::AlignedCachedData> cached_data;
  if (options & kConsumeCodeCache) {
    DCHECK(source->cached_data);
    // ScriptData takes care of pointer-aligning the data.
    cached_data.reset(new i::AlignedCachedData(source->cached_data->data,
                                               source->cached_data->length));
  }

  i::DirectHandle<i::JSFunction> result;
  bool has_exception =
      !i::Compiler::GetWrappedFunction(
           i_isolate, Utils::OpenHandle(*source->source_string), context,
           script_details, cached_data.get(), options, no_cache_reason)
           .ToHandle(&result);
  if (options & kConsumeCodeCache) {
    source->cached_data->rejected = cached_data->rejected();
  }
  if (has_exception) return {};
  return api_scope.Escape(Utils::CallableToLocal(result));
}

void ScriptCompiler::ScriptStreamingTask::Run() { data_->task->Run(); }

ScriptCompiler::ScriptStreamingTask* ScriptCompiler::StartStreaming(
    Isolate* v8_isolate, StreamedSource* source, v8::ScriptType type,
    CompileOptions options, CompileHintCallback compile_hint_callback,
    void* compile_hint_callback_data) {
  Utils::ApiCheck(v8::ScriptCompiler::CompileOptionsIsValid(options),
                  "v8::ScriptCompiler::StartStreaming",
                  "Invalid CompileOptions");
  if (!i::v8_flags.script_streaming) return nullptr;
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::ScriptStreamingData* data = source->impl();
  std::unique_ptr<i::BackgroundCompileTask> task =
      std::make_unique<i::BackgroundCompileTask>(
          data, i_isolate, type, options, &source->compilation_details(),
          compile_hint_callback, compile_hint_callback_data);
  data->task = std::move(task);
  return new ScriptCompiler::ScriptStreamingTask(data);
}

ScriptCompiler::ConsumeCodeCacheTask::ConsumeCodeCacheTask(
    std::unique_ptr<i::BackgroundDeserializeTask> impl)
    : impl_(std::move(impl)) {}

ScriptCompiler::ConsumeCodeCacheTask::~ConsumeCodeCacheTask() = default;

void ScriptCompiler::ConsumeCodeCacheTask::Run() { impl_->Run(); }

void ScriptCompiler::ConsumeCodeCacheTask::SourceTextAvailable(
    Isolate* v8_isolate, Local<String> source_text,
    const ScriptOrigin& origin) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::DisallowJavascriptExecutionDebugOnly no_execution(i_isolate);
  i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);
  auto str = Utils::OpenHandle(*source_text);
  i::ScriptDetails script_details =
      GetScriptDetails(i_isolate, origin.ResourceName(), origin.LineOffset(),
                       origin.ColumnOffset(), origin.SourceMapUrl(),
                       origin.GetHostDefinedOptions(), origin.Options());
  impl_->SourceTextAvailable(i_isolate, str, script_details);
}

bool ScriptCompiler::ConsumeCodeCacheTask::ShouldMergeWithExistingScript()
    const {
  if (!i::v8_flags
           .merge_background_deserialized_script_with_compilation_cache) {
    return false;
  }
  return impl_->ShouldMergeWithExistingScript();
}

void ScriptCompiler::ConsumeCodeCacheTask::MergeWithExistingScript() {
  DCHECK(
      i::v8_flags.merge_background_deserialized_script_with_compilation_cache);
  impl_->MergeWithExistingScript();
}

ScriptCompiler::ConsumeCodeCacheTask* ScriptCompiler::StartConsumingCodeCache(
    Isolate* v8_isolate, std::unique_ptr<CachedData> cached_data) {
  if (!i::v8_flags.concurrent_cache_deserialization) return nullptr;
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::DisallowJavascriptExecutionDebugOnly no_execution(i_isolate);
  i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);
  return new ScriptCompiler::ConsumeCodeCacheTask(
      std::make_unique<i::BackgroundDeserializeTask>(i_isolate,
                                                     std::move(cached_data)));
}

ScriptCompiler::ConsumeCodeCacheTask*
ScriptCompiler::StartConsumingCodeCacheOnBackground(
    Isolate* v8_isolate, std::unique_ptr<CachedData> cached_data) {
  if (!i::v8_flags.concurrent_cache_deserialization) return nullptr;
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  return new ScriptCompiler::ConsumeCodeCacheTask(
      std::make_unique<i::BackgroundDeserializeTask>(i_isolate,
                                                     std::move(cached_data)));
}

namespace {
i::MaybeDirectHandle<i::SharedFunctionInfo> CompileStreamedSource(
    i::Isolate* i_isolate, ScriptCompiler::StreamedSource* v8_source,
    Local<String> full_source_string, const ScriptOrigin& origin) {
  auto str = Utils::OpenHandle(*full_source_string);
  i::ScriptDetails script_details =
      GetScriptDetails(i_isolate, origin.ResourceName(), origin.LineOffset(),
                       origin.ColumnOffset(), origin.SourceMapUrl(),
                       origin.GetHostDefinedOptions(), origin.Options());
  i::ScriptStreamingData* data = v8_source->impl();
  i::IsCompiledScope is_compiled_scope;
  return i::Compiler::GetSharedFunctionInfoForStreamedScript(
      i_isolate, str, script_details, data, &is_compiled_scope,
      &v8_source->compilation_details());
}

}  // namespace

MaybeLocal<Script> ScriptCompiler::Compile(Local<Context> context,
                                           StreamedSource* v8_source,
                                           Local<String> full_source_string,
                                           const ScriptOrigin& origin) {
  PrepareForExecutionScope api_scope{context,
                                     RCCId::kAPI_ScriptCompiler_Compile};
  i::Isolate* i_isolate = api_scope.i_isolate();
  TRACE_EVENT_CALL_STATS_SCOPED(i_isolate, "v8", "V8.ScriptCompiler");
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.CompileStreamedScript");
  i::DirectHandle<i::SharedFunctionInfo> sfi;
  if (!CompileStreamedSource(i_isolate, v8_source, full_source_string, origin)
           .ToHandle(&sfi)) {
    i_isolate->ReportPendingMessages();
    return {};
  }
  Local<UnboundScript> generic = ToApiHandle<UnboundScript>(sfi);
  if (generic.IsEmpty()) return {};
  return api_scope.Escape(generic->BindToCurrentContext());
}

MaybeLocal<Module> ScriptCompiler::CompileModule(
    Local<Context> context, StreamedSource* v8_source,
    Local<String> full_source_string, const ScriptOrigin& origin) {
  PrepareForExecutionScope api_scope{context,
                                     RCCId::kAPI_ScriptCompiler_Compile};
  i::Isolate* i_isolate = api_scope.i_isolate();
  TRACE_EVENT_CALL_STATS_SCOPED(i_isolate, "v8", "V8.ScriptCompiler");
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.CompileStreamedModule");
  i::DirectHandle<i::SharedFunctionInfo> sfi;
  if (!CompileStreamedSource(i_isolate, v8_source, full_source_string, origin)
           .ToHandle(&sfi)) {
    i_isolate->ReportPendingMessages();
    return {};
  }
  return api_scope.Escape(
      ToApiHandle<Module>(i_isolate->factory()->NewSourceTextModule(sfi)));
}

uint32_t ScriptCompiler::CachedDataVersionTag() {
  return static_cast<uint32_t>(base::hash_combine(internal::Version::Hash(),
                                                  internal::FlagList::Hash()));
}

ScriptCompiler::CachedData* ScriptCompiler::CreateCodeCache(
    Local<UnboundScript> unbound_script) {
  auto shared = Utils::OpenHandle(*unbound_script);
  i::Isolate* i_isolate = i::Isolate::Current();
  Utils::ApiCheck(!i_isolate->serializer_enabled(),
                  "ScriptCompiler::CreateCodeCache",
                  "Cannot create code cache while creating a snapshot");
  i::DisallowJavascriptExecutionDebugOnly no_execution(i_isolate);
  i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);
  DCHECK(shared->is_toplevel());
  return i::CodeSerializer::Serialize(i_isolate, shared);
}

// static
ScriptCompiler::CachedData* ScriptCompiler::CreateCodeCache(
    Local<UnboundModuleScript> unbound_module_script) {
  i::Handle<i::SharedFunctionInfo> shared =
      Utils::OpenHandle(*unbound_module_script);
  i::Isolate* i_isolate = i::Isolate::Current();
  Utils::ApiCheck(!i_isolate->serializer_enabled(),
                  "ScriptCompiler::CreateCodeCache",
                  "Cannot create code cache while creating a snapshot");
  i::DisallowJavascriptExecutionDebugOnly no_execution(i_isolate);
  i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);
  DCHECK(shared->is_toplevel());
  return i::CodeSerializer::Serialize(i_isolate, shared);
}

ScriptCompiler::CachedData* ScriptCompiler::CreateCodeCacheForFunction(
    Local<Function> function) {
  auto js_function = i::Cast<i::JSFunction>(Utils::OpenDirectHandle(*function));
  i::Isolate* i_isolate = i::Isolate::Current();
  Utils::ApiCheck(!i_isolate->serializer_enabled(),
                  "ScriptCompiler::CreateCodeCacheForFunction",
                  "Cannot create code cache while creating a snapshot");
  i::Handle<i::SharedFunctionInfo> shared(js_function->shared(), i_isolate);
  i::DisallowJavascriptExecutionDebugOnly no_execution(i_isolate);
  i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);
  Utils::ApiCheck(shared->is_wrapped(),
                  "v8::ScriptCompiler::CreateCodeCacheForFunction",
                  "Expected SharedFunctionInfo with wrapped source code");
  return i::CodeSerializer::Serialize(i_isolate, shared);
}

MaybeLocal<Script> Script::Compile(Local<Context> context, Local<String> source,
                                   ScriptOrigin* origin) {
  if (origin) {
    ScriptCompiler::Source script_source(source, *origin);
    return ScriptCompiler::Compile(context, &script_source);
  }
  ScriptCompiler::Source script_source(source);
  return ScriptCompiler::Compile(context, &script_source);
}

// --- E x c e p t i o n s ---

v8::TryCatch::TryCatch(v8::Isolate* v8_isolate)
    : i_isolate_(reinterpret_cast<i::Isolate*>(v8_isolate)),
      next_(i_isolate_->try_catch_handler()),
      is_verbose_(false),
      can_continue_(true),
      capture_message_(true),
      rethrow_(false) {
  ResetInternal();
  // Special handling for simulators which have a separate JS stack.
  js_stack_comparable_address_ = static_cast<internal::Address>(
      i::SimulatorStack::RegisterJSStackComparableAddress(i_isolate_));
  i_isolate_->RegisterTryCatchHandler(this);
}

namespace {

i::Tagged<i::Object> ToObject(void* object) {
  return i::Tagged<i::Object>(reinterpret_cast<i::Address>(object));
}

}  // namespace

v8::TryCatch::~TryCatch() {
  if (HasCaught()) {
    if (rethrow_ || (V8_UNLIKELY(HasTerminated()) &&
                     !i_isolate_->thread_local_top()->CallDepthIsZero())) {
      if (capture_message_) {
        // If an exception was caught and rethrow_ is indicated, the saved
        // message, script, and location need to be restored to Isolate TLS
        // for reuse.  capture_message_ needs to be disabled so that Throw()
        // does not create a new message.
        i_isolate_->thread_local_top()->rethrowing_message_ = true;
        i_isolate_->set_pending_message(ToObject(message_obj_));
      }
      i_isolate_->UnregisterTryCatchHandler(this);
      i_isolate_->clear_internal_exception();
      i_isolate_->Throw(ToObject(exception_));
      return;
    }
    Reset();
  }
  i_isolate_->UnregisterTryCatchHandler(this);
  DCHECK_IMPLIES(rethrow_,
                 !i_isolate_->thread_local_top()->rethrowing_message_);
}

void* v8::TryCatch::operator new(size_t) { base::OS::Abort(); }
void* v8::TryCatch::operator new[](size_t) { base::OS::Abort(); }
void v8::TryCatch::operator delete(void*, size_t) { base::OS::Abort(); }
void v8::TryCatch::operator delete[](void*, size_t) { base::OS::Abort(); }

bool v8::TryCatch::HasCaught() const {
  return !IsTheHole(ToObject(exception_), i_isolate_);
}

bool v8::TryCatch::CanContinue() const { return can_continue_; }

bool v8::TryCatch::HasTerminated() const {
  return ToObject(exception_) ==
         i::ReadOnlyRoots(i_isolate_).termination_exception();
}

v8::Local<v8::Value> v8::TryCatch::ReThrow() {
  if (!HasCaught()) return {};
  rethrow_ = true;
  return v8::Undefined(reinterpret_cast<v8::Isolate*>(i_isolate_));
}

v8::Local<Value> v8::TryCatch::Exception() const {
  if (!HasCaught()) return {};
  if (HasTerminated()) {
    return v8::Utils::ToLocal(i_isolate_->factory()->null_value());
  }
  return v8::Utils::ToLocal(i::direct_handle(ToObject(exception_), i_isolate_));
}

MaybeLocal<Value> v8::TryCatch::StackTrace(Local<Context> context,
                                           Local<Value> exception) {
  auto i_exception = Utils::OpenDirectHandle(*exception);
  if (!IsJSObject(*i_exception)) return {};
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_TryCatch_StackTrace};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto obj = i::Cast<i::JSObject>(i_exception);
  i::DirectHandle<i::String> name = i_isolate->factory()->stack_string();
  Maybe<bool> maybe = i::JSReceiver::HasProperty(i_isolate, obj, name);
  if (maybe != Just(true)) return {};
  return api_scope.EscapeMaybe(
      i::JSReceiver::GetProperty(i_isolate, obj, name));
}

MaybeLocal<Value> v8::TryCatch::StackTrace(Local<Context> context) const {
  if (!HasCaught()) return {};
  return StackTrace(context, Exception());
}

v8::Local<v8::Message> v8::TryCatch::Message() const {
  i::Tagged<i::Object> message = ToObject(message_obj_);
  DCHECK(IsJSMessageObject(message) || IsTheHole(message, i_isolate_));
  if (HasCaught() && !IsTheHole(message, i_isolate_)) {
    return v8::Utils::MessageToLocal(i::direct_handle(message, i_isolate_));
  } else {
    return {};
  }
}

void v8::TryCatch::Reset() {
  if (rethrow_) return;
  if (V8_UNLIKELY(i_isolate_->is_execution_terminating()) &&
      !i_isolate_->thread_local_top()->CallDepthIsZero()) {
    return;
  }
  i_isolate_->clear_internal_exception();
  i_isolate_->clear_pending_message();
  ResetInternal();
}

void v8::TryCatch::ResetInternal() {
  i::Tagged<i::Object> the_hole = i::ReadOnlyRoots(i_isolate_).the_hole_value();
  exception_ = reinterpret_cast<void*>(the_hole.ptr());
  message_obj_ = reinterpret_cast<void*>(the_hole.ptr());
}

void v8::TryCatch::SetVerbose(bool value) { is_verbose_ = value; }

bool v8::TryCatch::IsVerbose() const { return is_verbose_; }

void v8::TryCatch::SetCaptureMessage(bool value) { capture_message_ = value; }

// --- M e s s a g e ---

Local<String> Message::Get() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  InternalEscapableScope scope(i_isolate);
  Local<String> result =
      Utils::ToLocal(i::MessageHandler::GetMessage(i_isolate, self));
  return scope.Escape(result);
}

v8::Isolate* Message::GetIsolate() const {
  i::Isolate* i_isolate = i::Isolate::Current();
  return reinterpret_cast<Isolate*>(i_isolate);
}

ScriptOrigin Message::GetScriptOrigin() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::Script> script(self->script(), i_isolate);
  return GetScriptOriginForScript(i_isolate, script);
}

void ScriptOrigin::VerifyHostDefinedOptions() const {
  // TODO(cbruni, chromium:1244145): Remove checks once we allow arbitrary
  // host-defined options.
  if (host_defined_options_.IsEmpty()) return;
  Utils::ApiCheck(host_defined_options_->IsFixedArray(), "ScriptOrigin()",
                  "Host-defined options has to be a PrimitiveArray");
  auto options =
      Utils::OpenDirectHandle(*host_defined_options_.As<FixedArray>());
  for (int i = 0; i < options->length(); i++) {
    Utils::ApiCheck(i::IsPrimitive(options->get(i)), "ScriptOrigin()",
                    "PrimitiveArray can only contain primtive values");
  }
}

v8::Local<Value> Message::GetScriptResourceName() const {
  i::DisallowJavascriptExecutionDebugOnly no_execution;
  i::DisallowExceptionsDebugOnly no_exceptions;
  return GetScriptOrigin().ResourceName();
}

v8::Local<v8::StackTrace> Message::GetStackTrace() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  InternalEscapableScope scope(i_isolate);
  i::DirectHandle<i::Object> stack_trace(self->stack_trace(), i_isolate);
  if (!IsStackTraceInfo(*stack_trace)) return {};
  return scope.Escape(
      Utils::StackTraceToLocal(i::Cast<i::StackTraceInfo>(stack_trace)));
}

Maybe<int> Message::GetLineNumber(Local<Context> context) const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  HandleScope handle_scope(reinterpret_cast<Isolate*>(i_isolate));
  i::JSMessageObject::EnsureSourcePositionsAvailable(i_isolate, self);
  return Just(self->GetLineNumber());
}

int Message::GetStartPosition() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  HandleScope handle_scope(reinterpret_cast<Isolate*>(i_isolate));
  i::JSMessageObject::EnsureSourcePositionsAvailable(i_isolate, self);
  return self->GetStartPosition();
}

int Message::GetEndPosition() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  HandleScope handle_scope(reinterpret_cast<Isolate*>(i_isolate));
  i::JSMessageObject::EnsureSourcePositionsAvailable(i_isolate, self);
  return self->GetEndPosition();
}

int Message::ErrorLevel() const {
  auto self = Utils::OpenDirectHandle(this);
  i::DisallowJavascriptExecutionDebugOnly no_execution;
  i::DisallowExceptionsDebugOnly no_exceptions;
  return self->error_level();
}

int Message::GetStartColumn() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  HandleScope handle_scope(reinterpret_cast<Isolate*>(i_isolate));
  i::JSMessageObject::EnsureSourcePositionsAvailable(i_isolate, self);
  return self->GetColumnNumber();
}

int Message::GetWasmFunctionIndex() const {
#if V8_ENABLE_WEBASSEMBLY
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  HandleScope handle_scope(reinterpret_cast<Isolate*>(i_isolate));
  i::JSMessageObject::EnsureSourcePositionsAvailable(i_isolate, self);
  int start_position = self->GetColumnNumber();
  if (start_position == -1) return Message::kNoWasmFunctionIndexInfo;

  i::DirectHandle<i::Script> script(self->script(), i_isolate);

  if (script->type() != i::Script::Type::kWasm) {
    return Message::kNoWasmFunctionIndexInfo;
  }

  auto debug_script = ToApiHandle<debug::Script>(script);
  return Local<debug::WasmScript>::Cast(debug_script)
      ->GetContainingFunction(start_position);
#else
  return Message::kNoWasmFunctionIndexInfo;
#endif  // V8_ENABLE_WEBASSEMBLY
}

Maybe<int> Message::GetStartColumn(Local<Context> context) const {
  return Just(GetStartColumn());
}

int Message::GetEndColumn() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  HandleScope handle_scope(reinterpret_cast<Isolate*>(i_isolate));
  i::JSMessageObject::EnsureSourcePositionsAvailable(i_isolate, self);
  const int column_number = self->GetColumnNumber();
  if (column_number == -1) return -1;
  const int start = self->GetStartPosition();
  const int end = self->GetEndPosition();
  return column_number + (end - start);
}

Maybe<int> Message::GetEndColumn(Local<Context> context) const {
  return Just(GetEndColumn());
}

bool Message::IsSharedCrossOrigin() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  return self->script()->origin_options().IsSharedCrossOrigin();
}

bool Message::IsOpaque() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  return self->script()->origin_options().IsOpaque();
}

MaybeLocal<String> Message::GetSource(Local<Context> context) const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  InternalEscapableScope handle_scope(i_isolate);
  return handle_scope.Escape(
      Utils::ToLocal(i::direct_handle(self->GetSource(), i_isolate)));
}

MaybeLocal<String> Message::GetSourceLine(Local<Context> context) const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  InternalEscapableScope handle_scope(i_isolate);
  i::JSMessageObject::EnsureSourcePositionsAvailable(i_isolate, self);
  return handle_scope.Escape(Utils::ToLocal(self->GetSourceLine()));
}

void Message::PrintCurrentStackTrace(
    Isolate* v8_isolate, std::ostream& out,
    PrintCurrentStackTraceFilterCallback should_include_frame_callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i_isolate->PrintCurrentStackTrace(out, should_include_frame_callback);
}

// --- S t a c k T r a c e ---

int StackTrace::GetID() const {
  auto self = Utils::OpenDirectHandle(this);
  return self->id();
}

Local<StackFrame> StackTrace::GetFrame(Isolate* v8_isolate,
                                       uint32_t index) const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  return Utils::StackFrameToLocal(
      i::direct_handle(self->get(index), i_isolate));
}

int StackTrace::GetFrameCount() const {
  auto self = Utils::OpenDirectHandle(this);
  return self->length();
}

Local<StackTrace> StackTrace::CurrentStackTrace(Isolate* v8_isolate,
                                                int frame_limit,
                                                StackTraceOptions options) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::StackTraceInfo> stack_trace =
      i_isolate->CaptureDetailedStackTrace(frame_limit, options);
  return Utils::StackTraceToLocal(stack_trace);
}

Local<String> StackTrace::CurrentScriptNameOrSourceURL(Isolate* v8_isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::String> name_or_source_url =
      i_isolate->CurrentScriptNameOrSourceURL();
  return Utils::ToLocal(name_or_source_url);
}

int StackTrace::CurrentScriptId(Isolate* v8_isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  return i_isolate->CurrentScriptId();
}

// --- S t a c k F r a m e ---

Location StackFrame::GetLocation() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  i::DirectHandle<i::Script> script(self->script(), i_isolate);
  i::Script::PositionInfo info;
  CHECK(i::Script::GetPositionInfo(
      script, i::StackFrameInfo::GetSourcePosition(self), &info));
  if (script->HasSourceURLComment()) {
    info.line -= script->line_offset();
    if (info.line == 0) {
      info.column -= script->column_offset();
    }
  }
  return {info.line, info.column};
}

int StackFrame::GetSourcePosition() const {
  auto self = Utils::OpenHandle(this);

  return i::StackFrameInfo::GetSourcePosition(self);
}

int StackFrame::GetScriptId() const {
  return Utils::OpenDirectHandle(this)->script()->id();
}

Local<String> StackFrame::GetScriptName() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  i::DirectHandle<i::Object> name(self->script()->name(), i_isolate);
  if (!IsString(*name)) return {};
  return Utils::ToLocal(i::Cast<i::String>(name));
}

Local<String> StackFrame::GetScriptNameOrSourceURL() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  i::DirectHandle<i::Object> name_or_source_url(
      self->script()->GetNameOrSourceURL(), i_isolate);
  if (!IsString(*name_or_source_url)) return {};
  return Utils::ToLocal(i::Cast<i::String>(name_or_source_url));
}

Local<String> StackFrame::GetScriptSource() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  if (!self->script()->HasValidSource()) return {};
  i::DirectHandle<i::PrimitiveHeapObject> source(self->script()->source(),
                                                 i_isolate);
  if (!IsString(*source)) return {};
  return Utils::ToLocal(i::Cast<i::String>(source));
}

Local<String> StackFrame::GetScriptSourceMappingURL() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  i::DirectHandle<i::Object> source_mapping_url(
      self->script()->source_mapping_url(), i_isolate);
  if (!IsString(*source_mapping_url)) return {};
  return Utils::ToLocal(i::Cast<i::String>(source_mapping_url));
}

Local<String> StackFrame::GetFunctionName() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  i::DirectHandle<i::String> name(self->function_name(), i_isolate);
  if (name->length() == 0) return {};
  return Utils::ToLocal(name);
}

bool StackFrame::IsEval() const {
  auto self = Utils::OpenDirectHandle(this);
  return self->script()->compilation_type() ==
         i::Script::CompilationType::kEval;
}

bool StackFrame::IsConstructor() const {
  return Utils::OpenDirectHandle(this)->is_constructor();
}

bool StackFrame::IsWasm() const { return !IsUserJavaScript(); }

bool StackFrame::IsUserJavaScript() const {
  return Utils::OpenDirectHandle(this)->script()->IsUserJavaScript();
}

// --- J S O N ---

MaybeLocal<Value> JSON::Parse(Local<Context> context, Local<String> json_string,
                              std::optional<ScriptOrigin> origin) {
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_JSON_Parse};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto string = Utils::OpenHandle(*json_string);
  i::Handle<i::String> source = i::String::Flatten(i_isolate, string);
  i::Handle<i::Object> undefined = i_isolate->factory()->undefined_value();
  std::optional<i::ScriptDetails> script_details;
  if (origin.has_value()) {
    const ScriptOrigin& script_origin = origin.value();
    script_details.emplace(GetScriptDetails(
        i_isolate, script_origin.ResourceName(), script_origin.LineOffset(),
        script_origin.ColumnOffset(), script_origin.SourceMapUrl(),
        script_origin.GetHostDefinedOptions(), script_origin.Options()));
  }
  auto maybe_result = source->IsOneByteRepresentation()
                          ? i::JsonParser<uint8_t>::Parse(
                                i_isolate, source, undefined, script_details)
                          : i::JsonParser<uint16_t>::Parse(
                                i_isolate, source, undefined, script_details);
  return api_scope.EscapeMaybe(maybe_result);
}

MaybeLocal<String> JSON::Stringify(Local<Context> context,
                                   Local<Value> json_object,
                                   Local<String> gap) {
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_JSON_Stringify};
  i::Isolate* i_isolate = api_scope.i_isolate();
  i::Handle<i::JSAny> object;
  if (!Utils::ApiCheck(
          i::TryCast<i::JSAny>(Utils::OpenHandle(*json_object), &object),
          "JSON::Stringify",
          "Invalid object, must be a JSON-serializable object.")) {
    return {};
  }
  i::Handle<i::Undefined> replacer = i_isolate->factory()->undefined_value();
  i::Handle<i::String> gap_string = gap.IsEmpty()
                                        ? i_isolate->factory()->empty_string()
                                        : Utils::OpenHandle(*gap);
  i::DirectHandle<i::Object> maybe;
  if (!i::JsonStringify(i_isolate, object, replacer, gap_string)
           .ToHandle(&maybe)) {
    return {};
  }
  return api_scope.EscapeMaybe(i::Object::ToString(i_isolate, maybe));
}

// --- V a l u e   S e r i a l i z a t i o n ---

SharedValueConveyor::SharedValueConveyor(SharedValueConveyor&& other) noexcept
    : private_(std::move(other.private_)) {}

SharedValueConveyor::~SharedValueConveyor() = default;

SharedValueConveyor& SharedValueConveyor::operator=(
    SharedValueConveyor&& other) noexcept {
  private_ = std::move(other.private_);
  return *this;
}

SharedValueConveyor::SharedValueConveyor(Isolate* v8_isolate)
    : private_(std::make_unique<i::SharedObjectConveyorHandles>(
          reinterpret_cast<i::Isolate*>(v8_isolate))) {}

Maybe<bool> ValueSerializer::Delegate::WriteHostObject(Isolate* v8_isolate,
                                                       Local<Object> object) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  THROW_NEW_ERROR(i_isolate, NewError(i_isolate->error_function(),
                                      i::MessageTemplate::kDataCloneError,
                                      Utils::OpenDirectHandle(*object)));
}

bool ValueSerializer::Delegate::HasCustomHostObject(Isolate* v8_isolate) {
  return false;
}

Maybe<bool> ValueSerializer::Delegate::IsHostObject(Isolate* v8_isolate,
                                                    Local<Object> object) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  auto js_object = i::Cast<i::JSObject>(Utils::OpenDirectHandle(*object));
  return Just<bool>(
      i::JSObject::GetEmbedderFieldCount(js_object->map(i_isolate)));
}

Maybe<uint32_t> ValueSerializer::Delegate::GetSharedArrayBufferId(
    Isolate* v8_isolate, Local<SharedArrayBuffer> shared_array_buffer) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i_isolate->Throw(*i_isolate->factory()->NewError(
      i_isolate->error_function(), i::MessageTemplate::kDataCloneError,
      Utils::OpenDirectHandle(*shared_array_buffer)));
  return {};
}

Maybe<uint32_t> ValueSerializer::Delegate::GetWasmModuleTransferId(
    Isolate* v8_isolate, Local<WasmModuleObject> module) {
  return {};
}

bool ValueSerializer::Delegate::AdoptSharedValueConveyor(
    Isolate* v8_isolate, SharedValueConveyor&& conveyor) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i_isolate->Throw(*i_isolate->factory()->NewError(
      i_isolate->error_function(), i::MessageTemplate::kDataCloneError,
      i_isolate->factory()->NewStringFromAsciiChecked("shared value")));
  return false;
}

void* ValueSerializer::Delegate::ReallocateBufferMemory(void* old_buffer,
                                                        size_t size,
                                                        size_t* actual_size) {
  *actual_size = size;
  return base::Realloc(old_buffer, size);
}

void ValueSerializer::Delegate::FreeBufferMemory(void* buffer) {
  return base::Free(buffer);
}

struct ValueSerializer::PrivateData {
  explicit PrivateData(i::Isolate* i, ValueSerializer::Delegate* delegate)
      : isolate(i), serializer(i, delegate) {}
  i::Isolate* isolate;
  i::ValueSerializer serializer;
};

ValueSerializer::ValueSerializer(Isolate* v8_isolate)
    : ValueSerializer(v8_isolate, nullptr) {}

ValueSerializer::ValueSerializer(Isolate* v8_isolate, Delegate* delegate)
    : private_(new PrivateData(reinterpret_cast<i::Isolate*>(v8_isolate),
                               delegate)) {}

ValueSerializer::~ValueSerializer() { delete private_; }

void ValueSerializer::WriteHeader() { private_->serializer.WriteHeader(); }

void ValueSerializer::SetTreatArrayBufferViewsAsHostObjects(bool mode) {
  private_->serializer.SetTreatArrayBufferViewsAsHostObjects(mode);
}

Maybe<bool> ValueSerializer::WriteValue(Local<Context> context,
                                        Local<Value> value) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context,
                           RCCId::kAPI_ValueSerializer_WriteValue};
  auto object = Utils::OpenHandle(*value);
  return private_->serializer.WriteObject(object);
}

std::pair<uint8_t*, size_t> ValueSerializer::Release() {
  return private_->serializer.Release();
}

void ValueSerializer::TransferArrayBuffer(uint32_t transfer_id,
                                          Local<ArrayBuffer> array_buffer) {
  private_->serializer.TransferArrayBuffer(
      transfer_id, Utils::OpenDirectHandle(*array_buffer));
}

void ValueSerializer::WriteUint32(uint32_t value) {
  private_->serializer.WriteUint32(value);
}

void ValueSerializer::WriteUint64(uint64_t value) {
  private_->serializer.WriteUint64(value);
}

void ValueSerializer::WriteDouble(double value) {
  private_->serializer.WriteDouble(value);
}

void ValueSerializer::WriteRawBytes(const void* source, size_t length) {
  private_->serializer.WriteRawBytes(source, length);
}

MaybeLocal<Object> ValueDeserializer::Delegate::ReadHostObject(
    Isolate* v8_isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i_isolate->Throw(*i_isolate->factory()->NewError(
      i_isolate->error_function(),
      i::MessageTemplate::kDataCloneDeserializationError));
  return {};
}

MaybeLocal<WasmModuleObject> ValueDeserializer::Delegate::GetWasmModuleFromId(
    Isolate* v8_isolate, uint32_t id) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i_isolate->Throw(*i_isolate->factory()->NewError(
      i_isolate->error_function(),
      i::MessageTemplate::kDataCloneDeserializationError));
  return {};
}

MaybeLocal<SharedArrayBuffer>
ValueDeserializer::Delegate::GetSharedArrayBufferFromId(Isolate* v8_isolate,
                                                        uint32_t id) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i_isolate->Throw(*i_isolate->factory()->NewError(
      i_isolate->error_function(),
      i::MessageTemplate::kDataCloneDeserializationError));
  return {};
}

const SharedValueConveyor* ValueDeserializer::Delegate::GetSharedValueConveyor(
    Isolate* v8_isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i_isolate->Throw(*i_isolate->factory()->NewError(
      i_isolate->error_function(),
      i::MessageTemplate::kDataCloneDeserializationError));
  return nullptr;
}

struct ValueDeserializer::PrivateData {
  PrivateData(i::Isolate* i_isolate, base::Vector<const uint8_t> data,
              Delegate* delegate)
      : isolate(i_isolate), deserializer(i_isolate, data, delegate) {}
  i::Isolate* isolate;
  i::ValueDeserializer deserializer;
  bool supports_legacy_wire_format = false;
};

ValueDeserializer::ValueDeserializer(Isolate* v8_isolate, const uint8_t* data,
                                     size_t size)
    : ValueDeserializer(v8_isolate, data, size, nullptr) {}

ValueDeserializer::ValueDeserializer(Isolate* v8_isolate, const uint8_t* data,
                                     size_t size, Delegate* delegate) {
  private_ = new PrivateData(reinterpret_cast<i::Isolate*>(v8_isolate),
                             base::Vector<const uint8_t>(data, size), delegate);
}

ValueDeserializer::~ValueDeserializer() { delete private_; }

Maybe<bool> ValueDeserializer::ReadHeader(Local<Context> context) {
  auto i_isolate = i::Isolate::Current();
  EnterV8NoScriptScope<> api_scope{i_isolate, context,
                                   RCCId::kAPI_ValueDeserializer_ReadHeader};

  bool read_header = false;
  if (!private_->deserializer.ReadHeader().To(&read_header)) return {};
  DCHECK(read_header);

  static const uint32_t kMinimumNonLegacyVersion = 13;
  if (GetWireFormatVersion() < kMinimumNonLegacyVersion &&
      !private_->supports_legacy_wire_format) {
    i_isolate->Throw(*i_isolate->factory()->NewError(
        i::MessageTemplate::kDataCloneDeserializationVersionError));
    return {};
  }

  return Just(true);
}

void ValueDeserializer::SetSupportsLegacyWireFormat(
    bool supports_legacy_wire_format) {
  private_->supports_legacy_wire_format = supports_legacy_wire_format;
}

uint32_t ValueDeserializer::GetWireFormatVersion() const {
  return private_->deserializer.GetWireFormatVersion();
}

MaybeLocal<Value> ValueDeserializer::ReadValue(Local<Context> context) {
  PrepareForExecutionScope api_scope{context,
                                     RCCId::kAPI_ValueDeserializer_ReadValue};
  i::MaybeDirectHandle<i::Object> result =
      GetWireFormatVersion() > 0
          ? private_->deserializer.ReadObjectWrapper()
          : private_->deserializer.ReadObjectUsingEntireBufferForLegacyFormat();
  return api_scope.EscapeMaybe(result);
}

void ValueDeserializer::TransferArrayBuffer(uint32_t transfer_id,
                                            Local<ArrayBuffer> array_buffer) {
  private_->deserializer.TransferArrayBuffer(
      transfer_id, Utils::OpenDirectHandle(*array_buffer));
}

void ValueDeserializer::TransferSharedArrayBuffer(
    uint32_t transfer_id, Local<SharedArrayBuffer> shared_array_buffer) {
  private_->deserializer.TransferArrayBuffer(
      transfer_id, Utils::OpenDirectHandle(*shared_array_buffer));
}

bool ValueDeserializer::ReadUint32(uint32_t* value) {
  return private_->deserializer.ReadUint32(value);
}

bool ValueDeserializer::ReadUint64(uint64_t* value) {
  return private_->deserializer.ReadUint64(value);
}

bool ValueDeserializer::ReadDouble(double* value) {
  return private_->deserializer.ReadDouble(value);
}

bool ValueDeserializer::ReadRawBytes(size_t length, const void** data) {
  return private_->deserializer.ReadRawBytes(length, data);
}

// --- D a t a ---

bool Value::FullIsUndefined() const {
  bool result = i::IsUndefined(*Utils::OpenDirectHandle(this));
  DCHECK_EQ(result, QuickIsUndefined());
  return result;
}

bool Value::FullIsNull() const {
  bool result = i::IsNull(*Utils::OpenDirectHandle(this));
  DCHECK_EQ(result, QuickIsNull());
  return result;
}

bool Value::FullIsTrue() const {
  auto object = *Utils::OpenDirectHandle(this);
  if (i::IsSmi(object)) return false;
  return i::IsTrue(object);
}

bool Value::FullIsFalse() const {
  i::Tagged<i::Object> object = *Utils::OpenDirectHandle(this);
  if (i::IsSmi(object)) return false;
  return i::IsFalse(object);
}

bool Value::IsPrimitive() const {
  auto object = Utils::OpenDirectHandle(this);
  return i::IsPrimitive(*object) && !IsPrivateSymbol(*object);
}

bool Value::IsFunction() const {
  return IsCallable(*Utils::OpenDirectHandle(this));
}

bool Value::IsName() const { return i::IsName(*Utils::OpenDirectHandle(this)); }

bool Value::FullIsString() const {
  bool result = i::IsString(*Utils::OpenDirectHandle(this));
  DCHECK_EQ(result, QuickIsString());
  return result;
}

bool Value::IsSymbol() const {
  return IsPublicSymbol(*Utils::OpenDirectHandle(this));
}

bool Value::IsArray() const {
  return IsJSArray(*Utils::OpenDirectHandle(this));
}

bool Value::IsArrayBuffer() const {
  auto obj = *Utils::OpenDirectHandle(this);
  if (!IsJSArrayBuffer(obj)) return false;
  return !i::Cast<i::JSArrayBuffer>(obj)->is_shared();
}

bool Value::IsArrayBufferView() const {
  return IsJSArrayBufferView(*Utils::OpenDirectHandle(this));
}

bool Value::IsTypedArray() const {
  return IsJSTypedArray(*Utils::OpenDirectHandle(this));
}

#define VALUE_IS_TYPED_ARRAY(Type, typeName, TYPE, ctype)                      \
  bool Value::Is##Type##Array() const {                                        \
    auto obj = *Utils::OpenDirectHandle(this);                                 \
    return i::IsJSTypedArray(obj) &&                                           \
           i::Cast<i::JSTypedArray>(obj)->type() == i::kExternal##Type##Array; \
  }

TYPED_ARRAYS_BASE(VALUE_IS_TYPED_ARRAY)
#undef VALUE_IS_TYPED_ARRAY

bool Value::IsFloat16Array() const {
  auto obj = *Utils::OpenDirectHandle(this);
  return i::IsJSTypedArray(obj) &&
         i::Cast<i::JSTypedArray>(obj)->type() == i::kExternalFloat16Array &&
         Utils::ApiCheck(i::v8_flags.js_float16array, "Value::IsFloat16Array",
                         "Float16Array is not supported");
}

bool Value::IsDataView() const {
  auto obj = *Utils::OpenDirectHandle(this);
  return IsJSDataView(obj) || IsJSRabGsabDataView(obj);
}

bool Value::IsSharedArrayBuffer() const {
  auto obj = *Utils::OpenDirectHandle(this);
  if (!IsJSArrayBuffer(obj)) return false;
  return i::Cast<i::JSArrayBuffer>(obj)->is_shared();
}

bool Value::IsObject() const {
  return i::IsJSReceiver(*Utils::OpenDirectHandle(this));
}

bool Value::IsNumber() const {
  return i::IsNumber(*Utils::OpenDirectHandle(this));
}

bool Value::IsBigInt() const {
  return i::IsBigInt(*Utils::OpenDirectHandle(this));
}

bool Value::IsProxy() const {
  return i::IsJSProxy(*Utils::OpenDirectHandle(this));
}

#define VALUE_IS_SPECIFIC_TYPE(Type, Check)              \
  bool Value::Is##Type() const {                         \
    return i::Is##Check(*Utils::OpenDirectHandle(this)); \
  }

VALUE_IS_SPECIFIC_TYPE(ArgumentsObject, JSArgumentsObject)
VALUE_IS_SPECIFIC_TYPE(BigIntObject, BigIntWrapper)
VALUE_IS_SPECIFIC_TYPE(BooleanObject, BooleanWrapper)
VALUE_IS_SPECIFIC_TYPE(NumberObject, NumberWrapper)
VALUE_IS_SPECIFIC_TYPE(StringObject, StringWrapper)
VALUE_IS_SPECIFIC_TYPE(SymbolObject, SymbolWrapper)
VALUE_IS_SPECIFIC_TYPE(Date, JSDate)
VALUE_IS_SPECIFIC_TYPE(Map, JSMap)
VALUE_IS_SPECIFIC_TYPE(Set, JSSet)
#if V8_ENABLE_WEBASSEMBLY
VALUE_IS_SPECIFIC_TYPE(WasmMemoryMapDescriptor, WasmMemoryMapDescriptor)
VALUE_IS_SPECIFIC_TYPE(WasmMemoryObject, WasmMemoryObject)
VALUE_IS_SPECIFIC_TYPE(WasmModuleObject, WasmModuleObject)
VALUE_IS_SPECIFIC_TYPE(WasmNull, WasmNull)
#else
bool Value::IsWasmMemoryMapDescriptor() const { return false; }
bool Value::IsWasmMemoryObject() const { return false; }
bool Value::IsWasmModuleObject() const { return false; }
bool Value::IsWasmNull() const { return false; }
#endif  // V8_ENABLE_WEBASSEMBLY
VALUE_IS_SPECIFIC_TYPE(WeakMap, JSWeakMap)
VALUE_IS_SPECIFIC_TYPE(WeakSet, JSWeakSet)
VALUE_IS_SPECIFIC_TYPE(WeakRef, JSWeakRef)

#undef VALUE_IS_SPECIFIC_TYPE

bool Value::IsBoolean() const {
  return i::IsBoolean(*Utils::OpenDirectHandle(this));
}

bool Value::IsExternal() const {
  i::Tagged<i::Object> obj = *Utils::OpenDirectHandle(this);
  return IsJSExternalObject(obj);
}

bool Value::IsInt32() const {
  i::Tagged<i::Object> obj = *Utils::OpenDirectHandle(this);
  if (i::IsSmi(obj)) return true;
  if (i::IsNumber(obj)) {
    return i::IsInt32Double(i::Object::NumberValue(i::Cast<i::Number>(obj)));
  }
  return false;
}

bool Value::IsUint32() const {
  auto obj = *Utils::OpenDirectHandle(this);
  if (i::IsSmi(obj)) return i::Smi::ToInt(obj) >= 0;
  if (i::IsNumber(obj)) {
    double value = i::Object::NumberValue(i::Cast<i::Number>(obj));
    return !i::IsMinusZero(value) && value >= 0 && value <= i::kMaxUInt32 &&
           value == i::FastUI2D(i::FastD2UI(value));
  }
  return false;
}

bool Value::IsNativeError() const {
  return IsJSError(*Utils::OpenDirectHandle(this));
}

bool Value::IsRegExp() const {
  return IsJSRegExp(*Utils::OpenDirectHandle(this));
}

bool Value::IsAsyncFunction() const {
  auto obj = *Utils::OpenDirectHandle(this);
  if (!IsJSFunction(obj)) return false;
  auto func = i::Cast<i::JSFunction>(obj);
  return i::IsAsyncFunction(func->shared()->kind());
}

bool Value::IsGeneratorFunction() const {
  auto obj = *Utils::OpenDirectHandle(this);
  if (!IsJSFunction(obj)) return false;
  auto func = i::Cast<i::JSFunction>(obj);
  i::DisallowJavascriptExecutionDebugOnly no_execution;
  i::DisallowExceptionsDebugOnly no_exceptions;
  return i::IsGeneratorFunction(func->shared()->kind());
}

bool Value::IsGeneratorObject() const {
  return IsJSGeneratorObject(*Utils::OpenDirectHandle(this));
}

bool Value::IsMapIterator() const {
  return IsJSMapIterator(*Utils::OpenDirectHandle(this));
}

bool Value::IsSetIterator() const {
  return IsJSSetIterator(*Utils::OpenDirectHandle(this));
}

bool Value::IsPromise() const {
  return IsJSPromise(*Utils::OpenDirectHandle(this));
}

bool Value::IsModuleNamespaceObject() const {
  return IsJSModuleNamespace(*Utils::OpenDirectHandle(this));
}

MaybeLocal<String> Value::ToString(Local<Context> context) const {
  auto obj = Utils::OpenDirectHandle(this);
  if (i::IsString(*obj)) return ToApiHandle<String>(obj);
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Object_ToString};
  i::Isolate* i_isolate = api_scope.i_isolate();
  return api_scope.EscapeMaybe(i::Object::ToString(i_isolate, obj));
}

MaybeLocal<String> Value::ToDetailString(Local<Context> context) const {
  i::DirectHandle<i::Object> obj = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate;
  if (!context.IsEmpty()) {
    i_isolate = i::Isolate::Current();
  } else if (IsSmi(*obj) || !i::GetIsolateFromHeapObject(
                                i::Cast<i::HeapObject>(*obj), &i_isolate)) {
    i_isolate = i::Isolate::Current();
  }
  if (i::IsString(*obj)) return ToApiHandle<String>(obj);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  return Utils::ToLocal(i::Object::NoSideEffectsToString(i_isolate, obj));
}

MaybeLocal<Object> Value::ToObject(Local<Context> context) const {
  auto obj = Utils::OpenDirectHandle(this);
  if (i::IsJSReceiver(*obj)) return ToApiHandle<Object>(obj);
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Object_ToObject};
  i::Isolate* i_isolate = api_scope.i_isolate();
  return api_scope.EscapeMaybe(i::Object::ToObject(i_isolate, obj));
}

MaybeLocal<BigInt> Value::ToBigInt(Local<Context> context) const {
  auto obj = Utils::OpenDirectHandle(this);
  if (i::IsBigInt(*obj)) return ToApiHandle<BigInt>(obj);
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Object_ToBigInt};
  i::Isolate* i_isolate = api_scope.i_isolate();
  return api_scope.EscapeMaybe(i::BigInt::FromObject(i_isolate, obj));
}

bool Value::BooleanValue(Isolate* v8_isolate) const {
  return i::Object::BooleanValue(*Utils::OpenDirectHandle(this),
                                 reinterpret_cast<i::Isolate*>(v8_isolate));
}

MaybeLocal<Primitive> Value::ToPrimitive(Local<Context> context) const {
  auto obj = Utils::OpenDirectHandle(this);
  if (i::IsPrimitive(*obj)) return ToApiHandle<Primitive>(obj);
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Object_ToPrimitive};
  i::Isolate* i_isolate = api_scope.i_isolate();
  return api_scope.EscapeMaybe(
      Utils::ToMaybeLocal(i::Object::ToPrimitive(i_isolate, obj))
          .As<Primitive>());
}

MaybeLocal<Numeric> Value::ToNumeric(Local<Context> context) const {
  auto obj = Utils::OpenDirectHandle(this);
  if (i::IsNumeric(*obj)) return ToApiHandle<Numeric>(obj);
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Object_ToNumeric};
  i::Isolate* i_isolate = api_scope.i_isolate();
  return api_scope.EscapeMaybe(
      Utils::ToMaybeLocal(i::Object::ToNumeric(i_isolate, obj)).As<Numeric>());
}

Local<Boolean> Value::ToBoolean(Isolate* v8_isolate) const {
  auto i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::DisallowJavascriptExecutionDebugOnly no_execution(i_isolate);
  i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);
  return ToApiHandle<Boolean>(
      i_isolate->factory()->ToBoolean(BooleanValue(v8_isolate)));
}

MaybeLocal<Number> Value::ToNumber(Local<Context> context) const {
  auto obj = Utils::OpenDirectHandle(this);
  if (i::IsNumber(*obj)) return ToApiHandle<Number>(obj);
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Object_ToNumber};
  i::Isolate* i_isolate = api_scope.i_isolate();
  return api_scope.EscapeMaybe(
      Utils::ToMaybeLocal(i::Object::ToNumber(i_isolate, obj)).As<Number>());
}

MaybeLocal<Integer> Value::ToInteger(Local<Context> context) const {
  auto obj = Utils::OpenDirectHandle(this);
  if (i::IsSmi(*obj)) return ToApiHandle<Integer>(obj);
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Object_ToInteger};
  i::Isolate* i_isolate = api_scope.i_isolate();
  return api_scope.EscapeMaybe(
      Utils::ToMaybeLocal(i::Object::ToInteger(i_isolate, obj)).As<Integer>());
}

MaybeLocal<Int32> Value::ToInt32(Local<Context> context) const {
  auto obj = Utils::OpenDirectHandle(this);
  if (i::IsSmi(*obj)) return ToApiHandle<Int32>(obj);
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Object_ToInt32};
  i::Isolate* i_isolate = api_scope.i_isolate();
  return api_scope.EscapeMaybe(
      Utils::ToMaybeLocal(i::Object::ToInt32(i_isolate, obj)).As<Int32>());
}

MaybeLocal<Uint32> Value::ToUint32(Local<Context> context) const {
  auto obj = Utils::OpenDirectHandle(this);
  if (i::IsSmi(*obj)) return ToApiHandle<Uint32>(obj);
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Object_ToUint32};
  i::Isolate* i_isolate = api_scope.i_isolate();
  return api_scope.EscapeMaybe(
      Utils::ToMaybeLocal(i::Object::ToUint32(i_isolate, obj)).As<Uint32>());
}

// Note: This method is deprecated.
i::Isolate* i::IsolateFromNeverReadOnlySpaceObject(i::Address obj) {
  return i::Isolate::Current();
}

namespace api_internal {
i::Address ConvertToJSGlobalProxyIfNecessary(i::Address holder_ptr) {
  i::Tagged<i::HeapObject> holder =
      i::Cast<i::HeapObject>(i::Tagged<i::Object>(holder_ptr));

  if (i::IsJSGlobalObject(holder)) {
    return i::Cast<i::JSGlobalObject>(holder)->global_proxy().ptr();
  }
  return holder_ptr;
}
}  // namespace api_internal

bool i::ShouldThrowOnError(i::Isolate* i_isolate) {
  return i::GetShouldThrow(i_isolate, Nothing<i::ShouldThrow>()) ==
         i::ShouldThrow::kThrowOnError;
}

void i::Internals::CheckInitializedImpl(v8::Isolate* external_isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(external_isolate);
  Utils::ApiCheck(i_isolate != nullptr && !i_isolate->IsDead(),
                  "i::Internals::CheckInitialized",
                  "Isolate is not initialized or V8 has died");
}

void v8::Value::CheckCast(Data* that) {
  Utils::ApiCheck(that->IsValue(), "v8::Value::Cast", "Data is not a Value");
}

void External::CheckCast(v8::Value* that) {
  Utils::ApiCheck(that->IsExternal(), "v8::External::Cast",
                  "Value is not an External");
}

void v8::Object::CheckCast(Value* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsJSReceiver(*obj), "v8::Object::Cast",
                  "Value is not an Object");
}

void v8::Function::CheckCast(Value* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsCallable(*obj), "v8::Function::Cast",
                  "Value is not a Function");
}

void v8::Boolean::CheckCast(v8::Data* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsBoolean(*obj), "v8::Boolean::Cast",
                  "Value is not a Boolean");
}

void v8::Name::CheckCast(v8::Data* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsName(*obj), "v8::Name::Cast", "Value is not a Name");
}

void v8::String::CheckCast(v8::Data* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsString(*obj), "v8::String::Cast",
                  "Value is not a String");
}

void v8::Symbol::CheckCast(v8::Data* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsSymbol(*obj), "v8::Symbol::Cast",
                  "Value is not a Symbol");
}

void v8::Private::CheckCast(v8::Data* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(IsSymbol(*obj) && i::Cast<i::Symbol>(obj)->is_private(),
                  "v8::Private::Cast", "Value is not a Private");
}

void v8::FixedArray::CheckCast(v8::Data* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsFixedArray(*obj), "v8::FixedArray::Cast",
                  "Value is not a FixedArray");
}

void v8::ModuleRequest::CheckCast(v8::Data* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsModuleRequest(*obj), "v8::ModuleRequest::Cast",
                  "Value is not a ModuleRequest");
}

void v8::Module::CheckCast(v8::Data* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsModule(*obj), "v8::Module::Cast",
                  "Value is not a Module");
}

void v8::Numeric::CheckCast(v8::Data* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsNumeric(*obj), "v8::Numeric::Cast()",
                  "Value is not a Numeric");
}

void v8::Number::CheckCast(v8::Data* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsNumber(*obj), "v8::Number::Cast()",
                  "Value is not a Number");
}

void v8::Integer::CheckCast(v8::Data* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsNumber(*obj), "v8::Integer::Cast",
                  "Value is not an Integer");
}

void v8::Int32::CheckCast(v8::Data* that) {
  Utils::ApiCheck(Value::Cast(that)->IsInt32(), "v8::Int32::Cast",
                  "Value is not a 32-bit signed integer");
}

void v8::Uint32::CheckCast(v8::Data* that) {
  Utils::ApiCheck(Value::Cast(that)->IsUint32(), "v8::Uint32::Cast",
                  "Value is not a 32-bit unsigned integer");
}

void v8::BigInt::CheckCast(v8::Data* that) {
  Utils::ApiCheck(Value::Cast(that)->IsBigInt(), "v8::BigInt::Cast",
                  "Value is not a BigInt");
}

void v8::Context::CheckCast(v8::Data* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsContext(*obj), "v8::Context::Cast",
                  "Value is not a Context");
}

void v8::Array::CheckCast(Value* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsJSArray(*obj), "v8::Array::Cast",
                  "Value is not an Array");
}

void v8::Map::CheckCast(Value* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsJSMap(*obj), "v8::Map::Cast", "Value is not a Map");
}

void v8::Set::CheckCast(Value* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsJSSet(*obj), "v8_Set_Cast", "Value is not a Set");
}

void v8::Promise::CheckCast(Value* that) {
  Utils::ApiCheck(that->IsPromise(), "v8::Promise::Cast",
                  "Value is not a Promise");
}

void v8::Promise::Resolver::CheckCast(Value* that) {
  Utils::ApiCheck(that->IsPromise(), "v8::Promise::Resolver::Cast",
                  "Value is not a Promise::Resolver");
}

void v8::Proxy::CheckCast(Value* that) {
  Utils::ApiCheck(that->IsProxy(), "v8::Proxy::Cast", "Value is not a Proxy");
}

void v8::WasmMemoryObject::CheckCast(Value* that) {
  Utils::ApiCheck(that->IsWasmMemoryObject(), "v8::WasmMemoryObject::Cast",
                  "Value is not a WasmMemoryObject");
}

void v8::WasmMemoryMapDescriptor::CheckCast(Value* that) {
  Utils::ApiCheck(that->IsWasmMemoryMapDescriptor(),
                  "v8::WasmMemoryMapDescriptor::Cast",
                  "Value is not a WasmMemoryMapDescriptor");
}

void v8::WasmModuleObject::CheckCast(Value* that) {
  Utils::ApiCheck(that->IsWasmModuleObject(), "v8::WasmModuleObject::Cast",
                  "Value is not a WasmModuleObject");
}

void v8::CppHeapExternal::CheckCast(v8::Data* that) {
  Utils::ApiCheck(that->IsCppHeapExternal(), "v8::CppHeapExternal::Cast",
                  "Value is not a CppHeapExternal");
}

v8::BackingStore::~BackingStore() {
  auto i_this = reinterpret_cast<const i::BackingStore*>(this);
  i_this->~BackingStore();  // manually call internal destructor
}

void* v8::BackingStore::Data() const {
  return reinterpret_cast<const i::BackingStore*>(this)->buffer_start();
}

size_t v8::BackingStore::ByteLength() const {
  return reinterpret_cast<const i::BackingStore*>(this)->byte_length();
}

size_t v8::BackingStore::MaxByteLength() const {
  return reinterpret_cast<const i::BackingStore*>(this)->max_byte_length();
}

bool v8::BackingStore::IsShared() const {
  return reinterpret_cast<const i::BackingStore*>(this)->is_shared();
}

bool v8::BackingStore::IsResizableByUserJavaScript() const {
  return reinterpret_cast<const i::BackingStore*>(this)->is_resizable_by_js();
}

// static
void v8::BackingStore::EmptyDeleter(void* data, size_t length,
                                    void* deleter_data) {
  DCHECK_NULL(deleter_data);
}

std::shared_ptr<v8::BackingStore> v8::ArrayBuffer::GetBackingStore() {
  auto self = Utils::OpenDirectHandle(this);
  std::shared_ptr<i::BackingStore> backing_store = self->GetBackingStore();
  if (!backing_store) {
    backing_store =
        i::BackingStore::EmptyBackingStore(i::SharedFlag::kNotShared);
  }
  std::shared_ptr<i::BackingStoreBase> bs_base = backing_store;
  return std::static_pointer_cast<v8::BackingStore>(bs_base);
}

void* v8::ArrayBuffer::Data() const {
  return Utils::OpenDirectHandle(this)->backing_store();
}

bool v8::ArrayBuffer::IsResizableByUserJavaScript() const {
  return Utils::OpenDirectHandle(this)->is_resizable_by_js();
}

std::shared_ptr<v8::BackingStore> v8::SharedArrayBuffer::GetBackingStore() {
  auto self = Utils::OpenDirectHandle(this);
  std::shared_ptr<i::BackingStore> backing_store = self->GetBackingStore();
  if (!backing_store) {
    backing_store = i::BackingStore::EmptyBackingStore(i::SharedFlag::kShared);
  }
  std::shared_ptr<i::BackingStoreBase> bs_base = backing_store;
  return std::static_pointer_cast<v8::BackingStore>(bs_base);
}

void* v8::SharedArrayBuffer::Data() const {
  return Utils::OpenDirectHandle(this)->backing_store();
}

void v8::ArrayBuffer::CheckCast(Value* that) {
  auto obj = *Utils::OpenDirectHandle(that);
  Utils::ApiCheck(
      IsJSArrayBuffer(obj) && !i::Cast<i::JSArrayBuffer>(obj)->is_shared(),
      "v8::ArrayBuffer::Cast()", "Value is not an ArrayBuffer");
}

void v8::ArrayBufferView::CheckCast(Value* that) {
  auto obj = *Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsJSArrayBufferView(obj), "v8::ArrayBufferView::Cast()",
                  "Value is not an ArrayBufferView");
}

void v8::TypedArray::CheckCast(Value* that) {
  auto obj = *Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsJSTypedArray(obj), "v8::TypedArray::Cast()",
                  "Value is not a TypedArray");
}

#define CHECK_TYPED_ARRAY_CAST(Type, typeName, TYPE, ctype)                \
  void v8::Type##Array::CheckCast(Value* that) {                           \
    auto obj = *Utils::OpenDirectHandle(that);                             \
    Utils::ApiCheck(                                                       \
        i::IsJSTypedArray(obj) && i::Cast<i::JSTypedArray>(obj)->type() == \
                                      i::kExternal##Type##Array,           \
        "v8::" #Type "Array::Cast()", "Value is not a " #Type "Array");    \
  }

TYPED_ARRAYS_BASE(CHECK_TYPED_ARRAY_CAST)
#undef CHECK_TYPED_ARRAY_CAST

void v8::Float16Array::CheckCast(Value* that) {
  Utils::ApiCheck(i::v8_flags.js_float16array, "v8::Float16Array::Cast",
                  "Float16Array is not supported");
  auto obj = *Utils::OpenDirectHandle(that);
  Utils::ApiCheck(
      i::IsJSTypedArray(obj) &&
          i::Cast<i::JSTypedArray>(obj)->type() == i::kExternalFloat16Array,
      "v8::Float16Array::Cast()", "Value is not a Float16Array");
}

void v8::DataView::CheckCast(Value* that) {
  auto obj = *Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsJSDataView(obj) || IsJSRabGsabDataView(obj),
                  "v8::DataView::Cast()", "Value is not a DataView");
}

void v8::SharedArrayBuffer::CheckCast(Value* that) {
  auto obj = *Utils::OpenDirectHandle(that);
  Utils::ApiCheck(
      IsJSArrayBuffer(obj) && i::Cast<i::JSArrayBuffer>(obj)->is_shared(),
      "v8::SharedArrayBuffer::Cast()", "Value is not a SharedArrayBuffer");
}

void v8::Date::CheckCast(v8::Value* that) {
  auto obj = *Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsJSDate(obj), "v8::Date::Cast()", "Value is not a Date");
}

void v8::StringObject::CheckCast(v8::Value* that) {
  auto obj = *Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsStringWrapper(obj), "v8::StringObject::Cast()",
                  "Value is not a StringObject");
}

void v8::SymbolObject::CheckCast(v8::Value* that) {
  auto obj = *Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsSymbolWrapper(obj), "v8::SymbolObject::Cast()",
                  "Value is not a SymbolObject");
}

void v8::NumberObject::CheckCast(v8::Value* that) {
  auto obj = *Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsNumberWrapper(obj), "v8::NumberObject::Cast()",
                  "Value is not a NumberObject");
}

void v8::BigIntObject::CheckCast(v8::Value* that) {
  auto obj = *Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsBigIntWrapper(obj), "v8::BigIntObject::Cast()",
                  "Value is not a BigIntObject");
}

void v8::BooleanObject::CheckCast(v8::Value* that) {
  auto obj = *Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsBooleanWrapper(obj), "v8::BooleanObject::Cast()",
                  "Value is not a BooleanObject");
}

void v8::RegExp::CheckCast(v8::Value* that) {
  auto obj = *Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsJSRegExp(obj), "v8::RegExp::Cast()",
                  "Value is not a RegExp");
}

Maybe<double> Value::NumberValue(Local<Context> context) const {
  auto obj = Utils::OpenDirectHandle(this);
  if (i::IsNumber(*obj)) {
    return Just(i::Object::NumberValue(i::Cast<i::Number>(*obj)));
  }
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context, RCCId::kAPI_Value_NumberValue};
  i::DirectHandle<i::Number> num;
  if (!i::Object::ToNumber(i_isolate, obj).ToHandle(&num)) return {};
  return Just(i::Object::NumberValue(*num));
}

Maybe<int64_t> Value::IntegerValue(Local<Context> context) const {
  auto obj = Utils::OpenDirectHandle(this);
  if (i::IsNumber(*obj)) {
    return Just(NumberToInt64(*obj));
  }
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context, RCCId::kAPI_Value_IntegerValue};
  i::DirectHandle<i::Object> num;
  if (!i::Object::ToInteger(i_isolate, obj).ToHandle(&num)) return {};
  return Just(NumberToInt64(*num));
}

Maybe<int32_t> Value::Int32Value(Local<Context> context) const {
  auto obj = Utils::OpenDirectHandle(this);
  if (i::IsNumber(*obj)) return Just(NumberToInt32(*obj));
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context, RCCId::kAPI_Value_Int32Value};
  i::DirectHandle<i::Object> num;
  if (!i::Object::ToInt32(i_isolate, obj).ToHandle(&num)) return {};
  return Just(IsSmi(*num) ? i::Smi::ToInt(*num)
                          : static_cast<int32_t>(
                                i::Cast<i::HeapNumber>(*num)->value()));
}

Maybe<uint32_t> Value::Uint32Value(Local<Context> context) const {
  auto obj = Utils::OpenDirectHandle(this);
  if (i::IsNumber(*obj)) return Just(NumberToUint32(*obj));
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context, RCCId::kAPI_Value_Uint32Value};
  i::DirectHandle<i::Object> num;
  if (!i::Object::ToUint32(i_isolate, obj).ToHandle(&num)) return {};
  return Just(IsSmi(*num) ? static_cast<uint32_t>(i::Smi::ToInt(*num))
                          : static_cast<uint32_t>(
                                i::Cast<i::HeapNumber>(*num)->value()));
}

MaybeLocal<Uint32> Value::ToArrayIndex(Local<Context> context) const {
  auto self = Utils::OpenDirectHandle(this);
  if (i::IsSmi(*self)) {
    if (i::Smi::ToInt(*self) >= 0) return Utils::Uint32ToLocal(self);
    return {};
  }
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Object_ToArrayIndex};
  i::Isolate* i_isolate = api_scope.i_isolate();
  i::DirectHandle<i::Object> string_obj;
  if (!i::Object::ToString(i_isolate, self).ToHandle(&string_obj)) return {};
  auto str = i::Cast<i::String>(string_obj);
  uint32_t index;
  if (!str->AsArrayIndex(&index)) return {};
  i::DirectHandle<i::Number> value =
      index <= static_cast<uint32_t>(i::Smi::kMaxValue)
          ? direct_handle<i::Number>(i::Smi::FromInt(index), i_isolate)
          : i_isolate->factory()->NewNumber(index);
  return api_scope.Escape(Utils::Uint32ToLocal(value));
}

Maybe<bool> Value::Equals(Local<Context> context, Local<Value> that) const {
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context, RCCId::kAPI_Value_Equals};
  auto self = Utils::OpenDirectHandle(this);
  auto other = Utils::OpenDirectHandle(*that);
  return i::Object::Equals(i_isolate, self, other);
}

bool Value::StrictEquals(Local<Value> that) const {
  auto self = Utils::OpenDirectHandle(this);
  auto other = Utils::OpenDirectHandle(*that);
  return i::Object::StrictEquals(*self, *other);
}

bool Value::SameValue(Local<Value> that) const {
  auto self = Utils::OpenDirectHandle(this);
  auto other = Utils::OpenDirectHandle(*that);
  return i::Object::SameValue(*self, *other);
}

Local<String> Value::TypeOf(v8::Isolate* external_isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(external_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Value_TypeOf);
  return Utils::ToLocal(
      i::Object::TypeOf(i_isolate, Utils::OpenDirectHandle(this)));
}

Maybe<bool> Value::InstanceOf(v8::Local<v8::Context> context,
                              v8::Local<v8::Object> object) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context, RCCId::kAPI_Value_InstanceOf};
  i::DirectHandle<i::JSAny> left;
  if (!Utils::ApiCheck(
          i::TryCast<i::JSAny>(Utils::OpenDirectHandle(this), &left),
          "Value::InstanceOf",
          "Invalid type, must be a JS primitive or object.")) {
    return {};
  }
  auto right = Utils::OpenDirectHandle(*object);
  i::DirectHandle<i::Object> result;
  if (!i::Object::InstanceOf(i_isolate, left, right).ToHandle(&result)) {
    return {};
  }
  return Just(i::IsTrue(*result, i_isolate));
}

uint32_t Value::GetHash() {
  i::DisallowGarbageCollection no_gc;

  auto self = Utils::OpenDirectHandle(this);
  i::Tagged<i::Object> hash = i::Object::GetHash(*self);
  if (IsSmi(hash)) return i::Cast<i::Smi>(hash).value();

  i::DirectHandle<i::JSReceiver> obj = i::Cast<i::JSReceiver>(self);
  auto i_isolate = i::Isolate::Current();
  i::DisallowJavascriptExecutionDebugOnly no_execution(i_isolate);
  i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);
  return obj->GetOrCreateIdentityHash(i_isolate).value();
}

Maybe<bool> v8::Object::Set(v8::Local<v8::Context> context,
                            v8::Local<Value> key, v8::Local<Value> value) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context, RCCId::kAPI_Object_Set};
  auto self = Utils::OpenDirectHandle(this);
  auto key_obj = Utils::OpenDirectHandle(*key);
  auto value_obj = Utils::OpenDirectHandle(*value);
  if (i::Runtime::SetObjectProperty(i_isolate, self, key_obj, value_obj,
                                    i::StoreOrigin::kMaybeKeyed,
                                    Just(i::ShouldThrow::kDontThrow))
          .is_null()) {
    return {};
  }
  return Just(true);
}

Maybe<bool> v8::Object::Set(v8::Local<v8::Context> context,
                            v8::Local<Value> key, v8::Local<Value> value,
                            MaybeLocal<Object> receiver) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context, RCCId::kAPI_Object_Set};
  auto self = Utils::OpenDirectHandle(this);
  auto key_obj = Utils::OpenDirectHandle(*key);
  auto value_obj = Utils::OpenDirectHandle(*value);
  i::MaybeDirectHandle<i::JSReceiver> receiver_obj;
  if (!receiver.IsEmpty()) {
    receiver_obj = Utils::OpenDirectHandle(*receiver.ToLocalChecked());
  }
  if (i::Runtime::SetObjectProperty(i_isolate, self, key_obj, value_obj,
                                    receiver_obj, i::StoreOrigin::kMaybeKeyed,
                                    Just(i::ShouldThrow::kDontThrow))
          .is_null()) {
    return {};
  }
  return Just(true);
}

Maybe<bool> v8::Object::Set(v8::Local<v8::Context> context, uint32_t index,
                            v8::Local<Value> value) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context, RCCId::kAPI_Object_Set};
  auto self = Utils::OpenDirectHandle(this);
  auto value_obj = Utils::OpenHandle(*value);
  if (i::Object::SetElement(i_isolate, self, index, value_obj,
                            i::ShouldThrow::kDontThrow)
          .is_null()) {
    return {};
  }
  return Just(true);
}

Maybe<bool> v8::Object::CreateDataProperty(v8::Local<v8::Context> context,
                                           v8::Local<Name> key,
                                           v8::Local<Value> value) {
  auto i_isolate = i::Isolate::Current();
  auto self = Utils::OpenDirectHandle(this);
  auto key_obj = Utils::OpenDirectHandle(*key);
  auto value_obj = Utils::OpenDirectHandle(*value);

  i::PropertyKey lookup_key(i_isolate, key_obj);
  if (i::IsJSObject(*self)) {
    EnterV8NoScriptScope<> api_scope{i_isolate, context,
                                     RCCId::kAPI_Object_CreateDataProperty};
    return i::JSObject::CreateDataProperty(
        i_isolate, i::Cast<i::JSObject>(self), lookup_key, value_obj,
        Just(i::kDontThrow));
  }
  // JSProxy or WasmObject or other non-JSObject.
  EnterV8Scope<> api_scope{i_isolate, context,
                           RCCId::kAPI_Object_CreateDataProperty};
  return i::JSReceiver::CreateDataProperty(i_isolate, self, lookup_key,
                                           value_obj, Just(i::kDontThrow));
}

Maybe<bool> v8::Object::CreateDataProperty(v8::Local<v8::Context> context,
                                           uint32_t index,
                                           v8::Local<Value> value) {
  auto i_isolate = i::Isolate::Current();
  auto self = Utils::OpenDirectHandle(this);
  auto value_obj = Utils::OpenDirectHandle(*value);

  i::PropertyKey lookup_key(i_isolate, index);
  if (i::IsJSObject(*self)) {
    EnterV8NoScriptScope<> api_scope{i_isolate, context,
                                     RCCId::kAPI_Object_CreateDataProperty};
    return i::JSObject::CreateDataProperty(
        i_isolate, i::Cast<i::JSObject>(self), lookup_key, value_obj,
        Just(i::kDontThrow));
  }
  // JSProxy or WasmObject or other non-JSObject.
  EnterV8Scope<> api_scope{i_isolate, context,
                           RCCId::kAPI_Object_CreateDataProperty};
  return i::JSReceiver::CreateDataProperty(i_isolate, self, lookup_key,
                                           value_obj, Just(i::kDontThrow));
}

struct v8::PropertyDescriptor::PrivateData {
  PrivateData() : desc() {}
  i::PropertyDescriptor desc;
};

v8::PropertyDescriptor::PropertyDescriptor() : private_(new PrivateData()) {}

// DataDescriptor
v8::PropertyDescriptor::PropertyDescriptor(v8::Local<v8::Value> value)
    : private_(new PrivateData()) {
  private_->desc.set_value(
      Cast<i::JSAny>(Utils::OpenDirectHandle(*value, true)));
}

// DataDescriptor with writable field
v8::PropertyDescriptor::PropertyDescriptor(v8::Local<v8::Value> value,
                                           bool writable)
    : private_(new PrivateData()) {
  private_->desc.set_value(
      Cast<i::JSAny>(Utils::OpenDirectHandle(*value, true)));
  private_->desc.set_writable(writable);
}

// AccessorDescriptor
v8::PropertyDescriptor::PropertyDescriptor(v8::Local<v8::Value> get,
                                           v8::Local<v8::Value> set)
    : private_(new PrivateData()) {
  DCHECK(get.IsEmpty() || get->IsUndefined() || get->IsFunction());
  DCHECK(set.IsEmpty() || set->IsUndefined() || set->IsFunction());
  private_->desc.set_get(Cast<i::JSAny>(Utils::OpenDirectHandle(*get, true)));
  private_->desc.set_set(Cast<i::JSAny>(Utils::OpenDirectHandle(*set, true)));
}

v8::PropertyDescriptor::~PropertyDescriptor() { delete private_; }

v8::Local<Value> v8::PropertyDescriptor::value() const {
  DCHECK(private_->desc.has_value());
  return Utils::ToLocal(private_->desc.value());
}

v8::Local<Value> v8::PropertyDescriptor::get() const {
  DCHECK(private_->desc.has_get());
  return Utils::ToLocal(private_->desc.get());
}

v8::Local<Value> v8::PropertyDescriptor::set() const {
  DCHECK(private_->desc.has_set());
  return Utils::ToLocal(private_->desc.set());
}

bool v8::PropertyDescriptor::has_value() const {
  return private_->desc.has_value();
}
bool v8::PropertyDescriptor::has_get() const {
  return private_->desc.has_get();
}
bool v8::PropertyDescriptor::has_set() const {
  return private_->desc.has_set();
}

bool v8::PropertyDescriptor::writable() const {
  DCHECK(private_->desc.has_writable());
  return private_->desc.writable();
}

bool v8::PropertyDescriptor::has_writable() const {
  return private_->desc.has_writable();
}

void v8::PropertyDescriptor::set_enumerable(bool enumerable) {
  private_->desc.set_enumerable(enumerable);
}

bool v8::PropertyDescriptor::enumerable() const {
  DCHECK(private_->desc.has_enumerable());
  return private_->desc.enumerable();
}

bool v8::PropertyDescriptor::has_enumerable() const {
  return private_->desc.has_enumerable();
}

void v8::PropertyDescriptor::set_configurable(bool configurable) {
  private_->desc.set_configurable(configurable);
}

bool v8::PropertyDescriptor::configurable() const {
  DCHECK(private_->desc.has_configurable());
  return private_->desc.configurable();
}

bool v8::PropertyDescriptor::has_configurable() const {
  return private_->desc.has_configurable();
}

Maybe<bool> v8::Object::DefineOwnProperty(v8::Local<v8::Context> context,
                                          v8::Local<Name> key,
                                          v8::Local<Value> value,
                                          v8::PropertyAttribute attributes) {
  auto i_isolate = i::Isolate::Current();
  auto self = Utils::OpenDirectHandle(this);
  auto key_obj = Utils::OpenDirectHandle(*key);
  auto value_obj = Utils::OpenDirectHandle(*value);

  i::PropertyDescriptor desc;
  desc.set_writable(!(attributes & v8::ReadOnly));
  desc.set_enumerable(!(attributes & v8::DontEnum));
  desc.set_configurable(!(attributes & v8::DontDelete));
  desc.set_value(i::Cast<i::JSAny>(value_obj));

  if (i::IsJSObject(*self)) {
    // If it's not a JSProxy, i::JSReceiver::DefineOwnProperty should never run
    // a script.
    EnterV8NoScriptScope<> api_scope{i_isolate, context,
                                     RCCId::kAPI_Object_DefineOwnProperty};
    return i::JSReceiver::DefineOwnProperty(i_isolate, self, key_obj, &desc,
                                            Just(i::kDontThrow));
  }
  // JSProxy or WasmObject or other non-JSObject.
  EnterV8Scope<> api_scope{i_isolate, context,
                           RCCId::kAPI_Object_DefineOwnProperty};
  // Even though we said kDontThrow, there might be accessors that do throw.
  return i::JSReceiver::DefineOwnProperty(i_isolate, self, key_obj, &desc,
                                          Just(i::kDontThrow));
}

Maybe<bool> v8::Object::DefineProperty(v8::Local<v8::Context> context,
                                       v8::Local<Name> key,
                                       PropertyDescriptor& descriptor) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context,
                           RCCId::kAPI_Object_DefineOwnProperty};
  auto self = Utils::OpenDirectHandle(this);
  auto key_obj = Utils::OpenDirectHandle(*key);

  return i::JSReceiver::DefineOwnProperty(i_isolate, self, key_obj,
                                          &descriptor.get_private()->desc,
                                          Just(i::kDontThrow));
}

Maybe<bool> v8::Object::SetPrivate(Local<Context> context, Local<Private> key,
                                   Local<Value> value) {
  auto i_isolate = i::Isolate::Current();
  EnterV8NoScriptScope<> api_scope{i_isolate, context,
                                   RCCId::kAPI_Object_SetPrivate};
  auto self = Utils::OpenDirectHandle(this);
  auto key_obj = Utils::OpenDirectHandle(reinterpret_cast<Name*>(*key));
  auto value_obj = Utils::OpenDirectHandle(*value);
  if (i::IsJSObject(*self)) {
    auto js_object = i::Cast<i::JSObject>(self);
    i::LookupIterator it(i_isolate, js_object, key_obj, js_object);
    if (i::JSObject::DefineOwnPropertyIgnoreAttributes(&it, value_obj,
                                                       i::DONT_ENUM)
            .is_null()) {
      return {};
    }
    return Just(true);
  }
  if (i::IsJSProxy(*self)) {
    i::PropertyDescriptor desc;
    desc.set_writable(true);
    desc.set_enumerable(false);
    desc.set_configurable(true);
    desc.set_value(i::Cast<i::JSAny>(value_obj));
    return i::JSProxy::SetPrivateSymbol(i_isolate, i::Cast<i::JSProxy>(self),
                                        i::Cast<i::Symbol>(key_obj), &desc,
                                        Just(i::kDontThrow));
  }
  // Wasm object, or other kind of special object not supported here.
  return Just(false);
}

MaybeLocal<Value> v8::Object::Get(Local<v8::Context> context,
                                  Local<Value> key) {
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Object_Get};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto self = Utils::OpenDirectHandle(this);
  auto key_obj = Utils::OpenDirectHandle(*key);
  return api_scope.EscapeMaybe(
      i::Runtime::GetObjectProperty(i_isolate, self, key_obj));
}

MaybeLocal<Value> v8::Object::Get(Local<v8::Context> context, Local<Value> key,
                                  MaybeLocal<Object> receiver) {
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Object_Get};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto self = Utils::OpenDirectHandle(this);
  auto key_obj = Utils::OpenDirectHandle(*key);
  i::DirectHandle<i::JSReceiver> receiver_obj;
  if (!receiver.IsEmpty()) {
    receiver_obj = Utils::OpenDirectHandle(*receiver.ToLocalChecked());
  }
  return api_scope.EscapeMaybe(
      i::Runtime::GetObjectProperty(i_isolate, self, key_obj, receiver_obj));
}

MaybeLocal<Value> v8::Object::Get(Local<Context> context, uint32_t index) {
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Object_Get};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto self = Utils::OpenDirectHandle(this);
  return api_scope.EscapeMaybe(
      i::JSReceiver::GetElement(i_isolate, self, index));
}

MaybeLocal<Value> v8::Object::GetPrivate(Local<Context> context,
                                         Local<Private> key) {
  return Get(context, key.UnsafeAs<Value>());
}

Maybe<PropertyAttribute> v8::Object::GetPropertyAttributes(
    Local<Context> context, Local<Value> key) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context,
                           RCCId::kAPI_Object_GetPropertyAttributes};
  auto self = Utils::OpenDirectHandle(this);
  auto key_obj = Utils::OpenDirectHandle(*key);
  if (!i::IsName(*key_obj)) {
    if (!i::Object::ToString(i_isolate, key_obj).ToHandle(&key_obj)) return {};
  }
  auto key_name = i::Cast<i::Name>(key_obj);
  auto result = i::JSReceiver::GetPropertyAttributes(i_isolate, self, key_name);
  if (result.IsNothing()) return {};
  if (result.FromJust() == i::ABSENT) {
    return Just(static_cast<PropertyAttribute>(i::NONE));
  }
  return Just(static_cast<PropertyAttribute>(result.FromJust()));
}

MaybeLocal<Value> v8::Object::GetOwnPropertyDescriptor(Local<Context> context,
                                                       Local<Name> key) {
  PrepareForExecutionScope api_scope{
      context, RCCId::kAPI_Object_GetOwnPropertyDescriptor};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto obj = Utils::OpenDirectHandle(this);
  auto key_name = Utils::OpenDirectHandle(*key);

  i::PropertyDescriptor desc;
  Maybe<bool> found =
      i::JSReceiver::GetOwnPropertyDescriptor(i_isolate, obj, key_name, &desc);
  if (found.IsNothing()) return {};
  if (!found.FromJust()) {
    return v8::Undefined(reinterpret_cast<v8::Isolate*>(i_isolate));
  }
  return api_scope.Escape(Utils::ToLocal(desc.ToObject(i_isolate)));
}

Local<Value> v8::Object::GetPrototype() {
  auto self = Utils::OpenDirectHandle(this);
  auto i_isolate = i::Isolate::Current();
  i::PrototypeIterator iter(i_isolate, self);
  return Utils::ToLocal(i::PrototypeIterator::GetCurrent(iter));
}

Local<Value> v8::Object::GetPrototypeV2() {
  auto self = Utils::OpenDirectHandle(this);
  auto i_isolate = i::Isolate::Current();
  i::PrototypeIterator iter(i_isolate, self);
  if (i::IsJSGlobalProxy(*self)) {
    // Skip hidden prototype (i.e. JSGlobalObject).
    iter.Advance();
  }
  DCHECK(!i::IsJSGlobalObject(*i::PrototypeIterator::GetCurrent(iter)));
  return Utils::ToLocal(i::PrototypeIterator::GetCurrent(iter));
}

namespace {

Maybe<bool> SetPrototypeImpl(v8::Object* this_, Local<Context> context,
                             Local<Value> value, bool from_javascript) {
  auto i_isolate = i::Isolate::Current();
  auto self = Utils::OpenDirectHandle(this_);
  auto value_obj = Utils::OpenDirectHandle(*value);
  // TODO(333672197): turn this to DCHECK once it's no longer possible
  // to get JSGlobalObject via API.
  CHECK_IMPLIES(from_javascript, !i::IsJSGlobalObject(*value_obj));
  if (i::IsJSObject(*self)) {
    EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
    // TODO(333672197): turn this to DCHECK once it's no longer possible
    // to get JSGlobalObject via API.
    CHECK_IMPLIES(from_javascript, !i::IsJSGlobalObject(*self));
    auto result =
        i::JSObject::SetPrototype(i_isolate, i::Cast<i::JSObject>(self),
                                  value_obj, from_javascript, i::kDontThrow);
    // Convert Just(false) to Nothing.
    if (!result.FromJust()) return {};
    return result;
  }
  if (i::IsJSProxy(*self)) {
    EnterV8Scope<> api_scope{i_isolate, context,
                             RCCId::kAPI_Object_SetPrototype};
    // We do not allow exceptions thrown while setting the prototype
    // to propagate outside.
    TryCatch try_catch(reinterpret_cast<v8::Isolate*>(i_isolate));
    auto result =
        i::JSProxy::SetPrototype(i_isolate, i::Cast<i::JSProxy>(self),
                                 value_obj, from_javascript, i::kThrowOnError);
    if (result.IsNothing()) return {};
    // Convert Just(false) to Just(true).
    return Just(true);
  }
  // Wasm object or other kind of special object not supported here.
  return {};
}

}  // namespace

Maybe<bool> v8::Object::SetPrototype(Local<Context> context,
                                     Local<Value> value) {
  static constexpr bool from_javascript = false;
  return SetPrototypeImpl(this, context, value, from_javascript);
}

Maybe<bool> v8::Object::SetPrototypeV2(Local<Context> context,
                                       Local<Value> value) {
  static constexpr bool from_javascript = true;
  return SetPrototypeImpl(this, context, value, from_javascript);
}

Local<Object> v8::Object::FindInstanceInPrototypeChain(
    v8::Local<FunctionTemplate> tmpl) {
  auto self = Utils::OpenDirectHandle(this);
  auto i_isolate = i::Isolate::Current();
  i::PrototypeIterator iter(i_isolate, *self, i::kStartAtReceiver);
  i::Tagged<i::FunctionTemplateInfo> tmpl_info =
      *Utils::OpenDirectHandle(*tmpl);
  if (!IsJSObject(iter.GetCurrent())) return {};
  while (!tmpl_info->IsTemplateFor(iter.GetCurrent<i::JSObject>())) {
    iter.Advance();
    if (iter.IsAtEnd()) return {};
    if (!IsJSObject(iter.GetCurrent())) return {};
  }
  // IsTemplateFor() ensures that iter.GetCurrent() can't be a Proxy here.
  return Utils::ToLocal(
      i::direct_handle(iter.GetCurrent<i::JSObject>(), i_isolate));
}

MaybeLocal<Array> v8::Object::GetPropertyNames(Local<Context> context) {
  return GetPropertyNames(
      context, v8::KeyCollectionMode::kIncludePrototypes,
      static_cast<v8::PropertyFilter>(ONLY_ENUMERABLE | SKIP_SYMBOLS),
      v8::IndexFilter::kIncludeIndices);
}

MaybeLocal<Array> v8::Object::GetPropertyNames(
    Local<Context> context, KeyCollectionMode mode,
    PropertyFilter property_filter, IndexFilter index_filter,
    KeyConversionMode key_conversion) {
  PrepareForExecutionScope api_scope{context,
                                     RCCId::kAPI_Object_GetPropertyNames};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto self = Utils::OpenDirectHandle(this);
  i::DirectHandle<i::FixedArray> value;
  i::KeyAccumulator accumulator(
      i_isolate, static_cast<i::KeyCollectionMode>(mode),
      static_cast<i::PropertyFilter>(property_filter));
  accumulator.set_skip_indices(index_filter == IndexFilter::kSkipIndices);
  if (accumulator.CollectKeys(self, self).IsNothing()) return {};
  value =
      accumulator.GetKeys(static_cast<i::GetKeysConversion>(key_conversion));
  DCHECK(self->map()->EnumLength() == i::kInvalidEnumCacheSentinel ||
         self->map()->EnumLength() == 0 ||
         self->map()->instance_descriptors(i_isolate)->enum_cache()->keys() !=
             *value);
  auto result = i_isolate->factory()->NewJSArrayWithElements(value);
  return api_scope.Escape(Utils::ToLocal(result));
}

MaybeLocal<Array> v8::Object::GetOwnPropertyNames(Local<Context> context) {
  return GetOwnPropertyNames(
      context, static_cast<v8::PropertyFilter>(ONLY_ENUMERABLE | SKIP_SYMBOLS));
}

MaybeLocal<Array> v8::Object::GetOwnPropertyNames(
    Local<Context> context, PropertyFilter filter,
    KeyConversionMode key_conversion) {
  return GetPropertyNames(context, KeyCollectionMode::kOwnOnly, filter,
                          v8::IndexFilter::kIncludeIndices, key_conversion);
}

MaybeLocal<String> v8::Object::ObjectProtoToString(Local<Context> context) {
  PrepareForExecutionScope api_scope{context,
                                     RCCId::kAPI_Object_ObjectProtoToString};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto self = Utils::OpenDirectHandle(this);
  return api_scope.EscapeMaybe(i::Cast<i::String>(i::Execution::CallBuiltin(
      i_isolate, i_isolate->object_to_string(), self, {})));
}

Local<String> v8::Object::GetConstructorName() {
  // TODO(v8:12547): Consider adding GetConstructorName(Local<Context>).
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate;
  if (i::HeapLayout::InWritableSharedSpace(*self)) {
    i_isolate = i::Isolate::Current();
  } else {
    i_isolate = i::Isolate::Current();
  }
  i::DirectHandle<i::String> name =
      i::JSReceiver::GetConstructorName(i_isolate, self);
  return Utils::ToLocal(name);
}

Maybe<bool> v8::Object::SetIntegrityLevel(Local<Context> context,
                                          IntegrityLevel level) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context,
                           RCCId::kAPI_Object_SetIntegrityLevel};
  auto self = Utils::OpenDirectHandle(this);
  i::JSReceiver::IntegrityLevel i_level =
      level == IntegrityLevel::kFrozen ? i::FROZEN : i::SEALED;
  return i::JSReceiver::SetIntegrityLevel(i_isolate, self, i_level,
                                          i::kThrowOnError);
}

Maybe<bool> v8::Object::Delete(Local<Context> context, Local<Value> key) {
  auto i_isolate = i::Isolate::Current();
  auto self = Utils::OpenDirectHandle(this);
  auto key_obj = Utils::OpenDirectHandle(*key);
  if (i::IsJSProxy(*self)) {
    EnterV8Scope<> api_scope{i_isolate, context, RCCId::kAPI_Object_Delete};
    return i::Runtime::DeleteObjectProperty(i_isolate, self, key_obj,
                                            i::LanguageMode::kSloppy);
  } else {
    // If it's not a JSProxy, i::Runtime::DeleteObjectProperty should never run
    // a script.
    DCHECK(i::IsJSObject(*self) || i::IsWasmObject(*self));
    EnterV8NoScriptScope<> api_scope{i_isolate, context,
                                     RCCId::kAPI_Object_Delete};
    return i::Runtime::DeleteObjectProperty(i_isolate, self, key_obj,
                                            i::LanguageMode::kSloppy);
  }
}

Maybe<bool> v8::Object::DeletePrivate(Local<Context> context,
                                      Local<Private> key) {
  auto i_isolate = i::Isolate::Current();
  // In case of private symbols, i::Runtime::DeleteObjectProperty does not run
  // any author script.
  EnterV8NoScriptScope<> api_scope{i_isolate, context,
                                   RCCId::kAPI_Object_Delete};
  auto self = Utils::OpenDirectHandle(this);
  auto key_obj = Utils::OpenDirectHandle(*key);
  return i::Runtime::DeleteObjectProperty(i_isolate, self, key_obj,
                                          i::LanguageMode::kSloppy);
}

Maybe<bool> v8::Object::Has(Local<Context> context, Local<Value> key) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context, RCCId::kAPI_Object_Has};
  auto self = Utils::OpenDirectHandle(this);
  auto key_obj = Utils::OpenDirectHandle(*key);
  // Check if the given key is an array index.
  uint32_t index = 0;
  if (i::Object::ToArrayIndex(*key_obj, &index)) {
    return i::JSReceiver::HasElement(i_isolate, self, index);
  }
  // Convert the key to a name - possibly by calling back into JavaScript.
  i::DirectHandle<i::Name> name;
  if (i::Object::ToName(i_isolate, key_obj).ToHandle(&name)) {
    return i::JSReceiver::HasProperty(i_isolate, self, name);
  }
  return {};
}

Maybe<bool> v8::Object::HasPrivate(Local<Context> context, Local<Private> key) {
  return HasOwnProperty(context, key.UnsafeAs<Name>());
}

Maybe<bool> v8::Object::Delete(Local<Context> context, uint32_t index) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context, RCCId::kAPI_Object_Delete};
  auto self = Utils::OpenDirectHandle(this);
  return i::JSReceiver::DeleteElement(i_isolate, self, index);
}

Maybe<bool> v8::Object::Has(Local<Context> context, uint32_t index) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context, RCCId::kAPI_Object_Has};
  auto self = Utils::OpenDirectHandle(this);
  return i::JSReceiver::HasElement(i_isolate, self, index);
}

template <typename Getter, typename Setter, typename Data>
static Maybe<bool> ObjectSetAccessor(Local<Context> context, Object* self,
                                     Local<Name> name, Getter getter,
                                     Setter setter, Data data,
                                     PropertyAttribute attributes,
                                     bool replace_on_access,
                                     SideEffectType getter_side_effect_type,
                                     SideEffectType setter_side_effect_type) {
  auto i_isolate = i::Isolate::Current();
  EnterV8NoScriptScope<> api_scope{i_isolate, context,
                                   RCCId::kAPI_Object_SetAccessor};
  if (!IsJSObject(*Utils::OpenDirectHandle(self))) return Just(false);
  auto obj = i::Cast<i::JSObject>(Utils::OpenHandle(self));
  i::DirectHandle<i::AccessorInfo> info = MakeAccessorInfo(
      i_isolate, name, getter, setter, data, replace_on_access);
  info->set_getter_side_effect_type(getter_side_effect_type);
  info->set_setter_side_effect_type(setter_side_effect_type);
  if (info.is_null()) return {};
  bool fast = obj->HasFastProperties();
  i::DirectHandle<i::Object> result;

  i::DirectHandle<i::Name> accessor_name(info->name(), i_isolate);
  i::PropertyAttributes attrs = static_cast<i::PropertyAttributes>(attributes);
  if (!i::JSObject::SetAccessor(obj, accessor_name, info, attrs)
           .ToHandle(&result)) {
    return {};
  }
  if (i::IsUndefined(*result, i_isolate)) return Just(false);
  if (fast) {
    i::JSObject::MigrateSlowToFast(obj, 0, "APISetAccessor");
  }
  return Just(true);
}

void Object::SetAccessorProperty(Local<Name> name, Local<Function> getter,
                                 Local<Function> setter,
                                 PropertyAttribute attributes) {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScope scope(i_isolate);
  if (!IsJSObject(*self)) return;
  i::DirectHandle<i::JSReceiver> getter_i =
      v8::Utils::OpenDirectHandle(*getter);
  i::DirectHandle<i::JSAny> setter_i =
      v8::Utils::OpenDirectHandle(*setter, true);
  if (setter_i.is_null()) setter_i = i_isolate->factory()->null_value();

  i::PropertyDescriptor desc;
  desc.set_enumerable(!(attributes & v8::DontEnum));
  desc.set_configurable(!(attributes & v8::DontDelete));
  desc.set_get(getter_i);
  desc.set_set(setter_i);

  auto name_i = v8::Utils::OpenDirectHandle(*name);
  // DefineOwnProperty might still throw if the receiver is a JSProxy and it
  // might fail if the receiver is non-extensible or already has this property
  // as non-configurable.
  Maybe<bool> success = i::JSReceiver::DefineOwnProperty(
      i_isolate, self, name_i, &desc, Just(i::kDontThrow));
  USE(success);
}

Maybe<bool> Object::SetNativeDataProperty(
    v8::Local<v8::Context> context, v8::Local<Name> name,
    AccessorNameGetterCallback getter, AccessorNameSetterCallback setter,
    v8::Local<Value> data, PropertyAttribute attributes,
    SideEffectType getter_side_effect_type,
    SideEffectType setter_side_effect_type) {
  return ObjectSetAccessor(context, this, name, getter, setter, data,
                           attributes, false, getter_side_effect_type,
                           setter_side_effect_type);
}

Maybe<bool> Object::SetLazyDataProperty(
    v8::Local<v8::Context> context, v8::Local<Name> name,
    AccessorNameGetterCallback getter, v8::Local<Value> data,
    PropertyAttribute attributes, SideEffectType getter_side_effect_type,
    SideEffectType setter_side_effect_type) {
  return ObjectSetAccessor(context, this, name, getter,
                           static_cast<AccessorNameSetterCallback>(nullptr),
                           data, attributes, true, getter_side_effect_type,
                           setter_side_effect_type);
}

Maybe<bool> v8::Object::HasOwnProperty(Local<Context> context,
                                       Local<Name> key) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context,
                           RCCId::kAPI_Object_HasOwnProperty};
  auto self = Utils::OpenDirectHandle(this);
  auto key_val = Utils::OpenDirectHandle(*key);
  return i::JSReceiver::HasOwnProperty(i_isolate, self, key_val);
}

Maybe<bool> v8::Object::HasOwnProperty(Local<Context> context, uint32_t index) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context,
                           RCCId::kAPI_Object_HasOwnProperty};
  auto self = Utils::OpenDirectHandle(this);
  return i::JSReceiver::HasOwnProperty(i_isolate, self, index);
}

Maybe<bool> v8::Object::HasRealNamedProperty(Local<Context> context,
                                             Local<Name> key) {
  auto i_isolate = i::Isolate::Current();
  EnterV8NoScriptScope<> api_scope{i_isolate, context,
                                   RCCId::kAPI_Object_HasRealNamedProperty};
  auto self = Utils::OpenDirectHandle(this);
  if (!IsJSObject(*self)) return Just(false);
  auto key_val = Utils::OpenDirectHandle(*key);
  return i::JSObject::HasRealNamedProperty(i_isolate,
                                           i::Cast<i::JSObject>(self), key_val);
}

Maybe<bool> v8::Object::HasRealIndexedProperty(Local<Context> context,
                                               uint32_t index) {
  auto i_isolate = i::Isolate::Current();
  EnterV8NoScriptScope<> api_scope{i_isolate, context,
                                   RCCId::kAPI_Object_HasRealIndexedProperty};
  auto self = Utils::OpenDirectHandle(this);
  if (!IsJSObject(*self)) return Just(false);
  return i::JSObject::HasRealElementProperty(i_isolate,
                                             i::Cast<i::JSObject>(self), index);
}

Maybe<bool> v8::Object::HasRealNamedCallbackProperty(Local<Context> context,
                                                     Local<Name> key) {
  auto i_isolate = i::Isolate::Current();
  EnterV8NoScriptScope<> api_scope{
      i_isolate, context, RCCId::kAPI_Object_HasRealNamedCallbackProperty};
  auto self = Utils::OpenDirectHandle(this);
  if (!IsJSObject(*self)) return Just(false);
  auto key_val = Utils::OpenDirectHandle(*key);
  return i::JSObject::HasRealNamedCallbackProperty(
      i_isolate, i::Cast<i::JSObject>(self), key_val);
}

bool v8::Object::HasNamedLookupInterceptor() const {
  auto self = *Utils::OpenDirectHandle(this);
  if (!IsJSObject(*self)) return false;
  return i::Cast<i::JSObject>(self)->HasNamedInterceptor();
}

bool v8::Object::HasIndexedLookupInterceptor() const {
  auto self = *Utils::OpenDirectHandle(this);
  if (!IsJSObject(*self)) return false;
  return i::Cast<i::JSObject>(self)->HasIndexedInterceptor();
}

MaybeLocal<Value> v8::Object::GetRealNamedPropertyInPrototypeChain(
    Local<Context> context, Local<Name> key) {
  PrepareForExecutionScope api_scope{
      context, RCCId::kAPI_Object_GetRealNamedPropertyInPrototypeChain};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto self = Utils::OpenDirectHandle(this);
  if (!IsJSObject(*self)) return {};
  auto key_obj = Utils::OpenDirectHandle(*key);
  i::PrototypeIterator iter(i_isolate, self);
  if (iter.IsAtEnd()) return {};
  i::DirectHandle<i::JSReceiver> proto =
      i::PrototypeIterator::GetCurrent<i::JSReceiver>(iter);
  i::PropertyKey lookup_key(i_isolate, key_obj);
  i::LookupIterator it(i_isolate, self, lookup_key, proto,
                       i::LookupIterator::PROTOTYPE_CHAIN_SKIP_INTERCEPTOR);
  MaybeLocal<Value> result = Utils::ToMaybeLocal(i::Object::GetProperty(&it));
  if (!it.IsFound()) return {};
  return api_scope.EscapeMaybe(result);
}

Maybe<PropertyAttribute>
v8::Object::GetRealNamedPropertyAttributesInPrototypeChain(
    Local<Context> context, Local<Name> key) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{
      i_isolate, context,
      RCCId::kAPI_Object_GetRealNamedPropertyAttributesInPrototypeChain};
  auto self = Utils::OpenDirectHandle(this);
  if (!IsJSObject(*self)) return {};
  auto key_obj = Utils::OpenDirectHandle(*key);
  i::PrototypeIterator iter(i_isolate, self);
  if (iter.IsAtEnd()) return {};
  i::DirectHandle<i::JSReceiver> proto =
      i::PrototypeIterator::GetCurrent<i::JSReceiver>(iter);
  i::PropertyKey lookup_key(i_isolate, key_obj);
  i::LookupIterator it(i_isolate, self, lookup_key, proto,
                       i::LookupIterator::PROTOTYPE_CHAIN_SKIP_INTERCEPTOR);
  Maybe<i::PropertyAttributes> result =
      i::JSReceiver::GetPropertyAttributes(&it);
  if (result.IsNothing()) return {};
  if (!it.IsFound()) return {};
  if (result.FromJust() == i::ABSENT) return Just(None);
  return Just(static_cast<PropertyAttribute>(result.FromJust()));
}

MaybeLocal<Value> v8::Object::GetRealNamedProperty(Local<Context> context,
                                                   Local<Name> key) {
  PrepareForExecutionScope api_scope{context,
                                     RCCId::kAPI_Object_GetRealNamedProperty};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto self = Utils::OpenDirectHandle(this);
  auto key_obj = Utils::OpenDirectHandle(*key);
  i::PropertyKey lookup_key(i_isolate, key_obj);
  i::LookupIterator it(i_isolate, self, lookup_key, self,
                       i::LookupIterator::PROTOTYPE_CHAIN_SKIP_INTERCEPTOR);
  MaybeLocal<Value> result = Utils::ToMaybeLocal(i::Object::GetProperty(&it));
  if (!it.IsFound()) return {};
  return api_scope.EscapeMaybe(result);
}

Maybe<PropertyAttribute> v8::Object::GetRealNamedPropertyAttributes(
    Local<Context> context, Local<Name> key) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context,
                           RCCId::kAPI_Object_GetRealNamedPropertyAttributes};
  auto self = Utils::OpenDirectHandle(this);
  auto key_obj = Utils::OpenDirectHandle(*key);
  i::PropertyKey lookup_key(i_isolate, key_obj);
  i::LookupIterator it(i_isolate, self, lookup_key, self,
                       i::LookupIterator::PROTOTYPE_CHAIN_SKIP_INTERCEPTOR);
  auto result = i::JSReceiver::GetPropertyAttributes(&it);
  if (result.IsNothing()) return {};
  if (!it.IsFound()) return {};
  if (result.FromJust() == i::ABSENT) {
    return Just(static_cast<PropertyAttribute>(i::NONE));
  }
  return Just<PropertyAttribute>(
      static_cast<PropertyAttribute>(result.FromJust()));
}

Local<v8::Object> v8::Object::Clone(Isolate* isolate) {
  auto self = i::Cast<i::JSObject>(Utils::OpenDirectHandle(this));
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::JSObject> result =
      i_isolate->factory()->CopyJSObject(self);
  return Utils::ToLocal(result);
}

Local<v8::Object> v8::Object::Clone() {
  return Clone(reinterpret_cast<v8::Isolate*>(i::Isolate::Current()));
}

namespace {
V8_INLINE MaybeLocal<v8::Context> GetCreationContextImpl(
    i::DirectHandle<i::JSReceiver> object, i::Isolate* i_isolate) {
  i::DirectHandle<i::NativeContext> context;
  if (object->GetCreationContext(i_isolate).ToHandle(&context)) {
    return Utils::ToLocal(context);
  }
  return {};
}
}  // namespace

MaybeLocal<v8::Context> v8::Object::GetCreationContext(v8::Isolate* isolate) {
  auto self = Utils::OpenDirectHandle(this);
  auto i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  return GetCreationContextImpl(self, i_isolate);
}

MaybeLocal<v8::Context> v8::Object::GetCreationContext() {
  auto self = Utils::OpenDirectHandle(this);
  return GetCreationContextImpl(self, i::Isolate::Current());
}

MaybeLocal<v8::Context> v8::Object::GetCreationContext(
    const PersistentBase<Object>& object) {
  return object.template value<Object>()->GetCreationContext(
      Isolate::GetCurrent());
}

namespace {
V8_INLINE Local<v8::Context> GetCreationContextCheckedImpl(
    i::DirectHandle<i::JSReceiver> object, i::Isolate* i_isolate) {
  i::DirectHandle<i::NativeContext> context;
  Utils::ApiCheck(object->GetCreationContext(i_isolate).ToHandle(&context),
                  "v8::Object::GetCreationContextChecked",
                  "No creation context available");
  return Utils::ToLocal(context);
}
}  // namespace

Local<v8::Context> v8::Object::GetCreationContextChecked(v8::Isolate* isolate) {
  auto self = Utils::OpenDirectHandle(this);
  auto i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  return GetCreationContextCheckedImpl(self, i_isolate);
}

Local<v8::Context> v8::Object::GetCreationContextChecked() {
  auto self = Utils::OpenDirectHandle(this);
  return GetCreationContextCheckedImpl(self, i::Isolate::Current());
}

namespace {
V8_INLINE void* GetAlignedPointerFromEmbedderDataInCreationContextImpl(
    i::DirectHandle<i::JSReceiver> object,
    i::IsolateForSandbox i_isolate_for_sandbox, int index) {
  const char* location =
      "v8::Object::GetAlignedPointerFromEmbedderDataInCreationContext()";
  auto maybe_context = object->GetCreationContext();
  i::Tagged<i::NativeContext> native_context;
  if (maybe_context.has_value()) {
    native_context = maybe_context.value();
  } else {
    // This happens when we're accessing a global property but passed its (now
    // detached) global proxy as holder. If the global proxy is detached because
    // of navigation, it won't have a context-specific metamap anymore (it gets
    // cleared on Detach to avoid leaks). Since we're doing a global proxy
    // access though, the Isolate's current native context must be the native
    // context we care about.
    i::Isolate* isolate = i::Isolate::Current();
    i::Tagged<i::Context> context = isolate->context();
    CHECK_EQ(context->global_proxy(), *object);
    native_context = context->native_context();
  }

  // The code below mostly mimics Context::GetAlignedPointerFromEmbedderData()
  // but it doesn't try to expand the EmbedderDataArray instance.
  i::DisallowGarbageCollection no_gc;

  // This macro requires a real Isolate while |i_isolate_for_sandbox| might be
  // nullptr if the V8 sandbox is not enabled.
  i::DisallowJavascriptExecutionDebugOnly no_execution;
  i::DisallowExceptionsDebugOnly no_exceptions;

  // TODO(ishell): remove cast once embedder_data slot has a proper type.
  i::Tagged<i::EmbedderDataArray> data =
      i::Cast<i::EmbedderDataArray>(native_context->embedder_data());
  if (V8_LIKELY(static_cast<unsigned>(index) <
                static_cast<unsigned>(data->length()))) {
    void* result;
    Utils::ApiCheck(i::EmbedderDataSlot(data, index)
                        .ToAlignedPointer(i_isolate_for_sandbox, &result),
                    location, "Pointer is not aligned");
    return result;
  }
  // Bad index, report an API error.
  Utils::ApiCheck(index >= 0, location, "Negative index");
  Utils::ApiCheck(index < i::EmbedderDataArray::kMaxLength, location,
                  "Index too large");
  return nullptr;
}
}  // namespace

void* v8::Object::GetAlignedPointerFromEmbedderDataInCreationContext(
    v8::Isolate* isolate, int index) {
  auto self = Utils::OpenDirectHandle(this);
  auto i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  return GetAlignedPointerFromEmbedderDataInCreationContextImpl(self, i_isolate,
                                                                index);
}

void* v8::Object::GetAlignedPointerFromEmbedderDataInCreationContext(
    int index) {
  auto self = Utils::OpenDirectHandle(this);
  i::IsolateForSandbox isolate = i::GetCurrentIsolateForSandbox();
  return GetAlignedPointerFromEmbedderDataInCreationContextImpl(self, isolate,
                                                                index);
}

int v8::Object::GetIdentityHash() {
  i::DisallowGarbageCollection no_gc;
  auto self = Utils::OpenDirectHandle(this);
  auto i_isolate = i::Isolate::Current();
  i::DisallowJavascriptExecutionDebugOnly no_execution(i_isolate);
  i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);
  return self->GetOrCreateIdentityHash(i_isolate).value();
}

bool v8::Object::IsCallable() const {
  return i::IsCallable(*Utils::OpenDirectHandle(this));
}

bool v8::Object::IsConstructor() const {
  return i::IsConstructor(*Utils::OpenDirectHandle(this));
}

bool v8::Object::IsApiWrapper() const {
  auto self = Utils::OpenDirectHandle(this);
  // This checks whether an object of a given instance type can serve as API
  // object. It does not check whether the JS object is wrapped via embedder
  // fields or Wrap()/Unwrap() API.
  return IsJSApiWrapperObject(*self);
}

bool v8::Object::IsUndetectable() const {
  auto self = Utils::OpenDirectHandle(this);
  return i::IsUndetectable(*self);
}

namespace {
base::Vector<i::DirectHandle<i::Object>> PrepareArguments(int argc,
                                                          Local<Value> argv[]) {
  static_assert(sizeof(v8::Local<v8::Value>) ==
                sizeof(i::DirectHandle<i::Object>));
  return {reinterpret_cast<i::DirectHandle<i::Object>*>(argv),
          static_cast<size_t>(argc)};
}
}  // namespace

MaybeLocal<Value> Object::CallAsFunction(Local<Context> context,
                                         Local<Value> recv, int argc,
                                         Local<Value> argv[]) {
  auto i_isolate = i::Isolate::Current();
  TRACE_EVENT_CALL_STATS_SCOPED(i_isolate, "v8", "V8.Execute");
  EnterV8Scope<InternalEscapableScope> api_scope{
      i_isolate, context, RCCId::kAPI_Object_CallAsFunction};
  i::TimerEventScope<i::TimerEventExecute> timer_scope(i_isolate);
  i::NestedTimedHistogramScope execute_timer(i_isolate->counters()->execute(),
                                             i_isolate);
  auto self = Utils::OpenDirectHandle(this);
  auto recv_obj = Utils::OpenDirectHandle(*recv);
  auto args = PrepareArguments(argc, argv);
  return api_scope.EscapeMaybe(
      Utils::ToMaybeLocal(i::Execution::Call(i_isolate, self, recv_obj, args)));
}

MaybeLocal<Value> Object::CallAsConstructor(Local<Context> context, int argc,
                                            Local<Value> argv[]) {
  auto i_isolate = i::Isolate::Current();
  TRACE_EVENT_CALL_STATS_SCOPED(i_isolate, "v8", "V8.Execute");
  EnterV8Scope<InternalEscapableScope> api_scope{
      i_isolate, context, RCCId::kAPI_Object_CallAsConstructor};
  i::TimerEventScope<i::TimerEventExecute> timer_scope(i_isolate);
  i::NestedTimedHistogramScope execute_timer(i_isolate->counters()->execute(),
                                             i_isolate);
  auto self = Utils::OpenDirectHandle(this);
  auto args = PrepareArguments(argc, argv);
  return api_scope.EscapeMaybe(
      Utils::ToMaybeLocal(i::Execution::New(i_isolate, self, self, args)));
}

MaybeLocal<Function> Function::New(Local<Context> context,
                                   FunctionCallback callback, Local<Value> data,
                                   int length, ConstructorBehavior behavior,
                                   SideEffectType side_effect_type) {
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Function_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  auto templ =
      FunctionTemplateNew(i_isolate, callback, data, Local<Signature>(), length,
                          behavior, true, Local<Private>(), side_effect_type);
  return Utils::ToLocal(templ)->GetFunction(context);
}

MaybeLocal<Object> Function::NewInstance(Local<Context> context, int argc,
                                         v8::Local<v8::Value> argv[]) const {
  return NewInstanceWithSideEffectType(context, argc, argv,
                                       SideEffectType::kHasSideEffect);
}

MaybeLocal<Object> Function::NewInstanceWithSideEffectType(
    Local<Context> context, int argc, v8::Local<v8::Value> argv[],
    SideEffectType side_effect_type) const {
  auto i_isolate = i::Isolate::Current();
  TRACE_EVENT_CALL_STATS_SCOPED(i_isolate, "v8", "V8.Execute");
  EnterV8Scope<InternalEscapableScope> api_scope{
      i_isolate, context, RCCId::kAPI_Function_NewInstance};
  i::TimerEventScope<i::TimerEventExecute> timer_scope(i_isolate);
  i::NestedTimedHistogramScope execute_timer(i_isolate->counters()->execute(),
                                             i_isolate);
  auto self = Utils::OpenDirectHandle(this);
  bool should_set_has_no_side_effect =
      side_effect_type == SideEffectType::kHasNoSideEffect &&
      i_isolate->should_check_side_effects();
  if (should_set_has_no_side_effect) {
    CHECK(IsJSFunction(*self) &&
          i::Cast<i::JSFunction>(*self)->shared()->IsApiFunction());
    i::Tagged<i::FunctionTemplateInfo> func_data =
        i::Cast<i::JSFunction>(*self)->shared()->api_func_data();
    if (func_data->has_callback(i_isolate)) {
      if (func_data->has_side_effects()) {
        i_isolate->debug()->IgnoreSideEffectsOnNextCallTo(
            handle(func_data, i_isolate));
      }
    }
  }
  auto args = PrepareArguments(argc, argv);
  return api_scope.EscapeMaybe(
      Utils::ToMaybeLocal(i::Execution::New(i_isolate, self, self, args)));
}

MaybeLocal<v8::Value> Function::Call(v8::Isolate* isolate,
                                     Local<Context> context,
                                     v8::Local<v8::Value> recv, int argc,
                                     v8::Local<v8::Value> argv[]) {
  auto i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  TRACE_EVENT_CALL_STATS_SCOPED(i_isolate, "v8", "V8.Execute");
  EnterV8Scope<InternalEscapableScope> api_scope{i_isolate, context,
                                                 RCCId::kAPI_Function_Call};
  i::TimerEventScope<i::TimerEventExecute> timer_scope(i_isolate);
  i::NestedTimedHistogramScope execute_timer(i_isolate->counters()->execute(),
                                             i_isolate);
  auto self = Utils::OpenDirectHandle(this);
  Utils::ApiCheck(!self.is_null(), "v8::Function::Call",
                  "Function to be called is a null pointer");
  auto recv_obj = Utils::OpenDirectHandle(*recv);
  auto args = PrepareArguments(argc, argv);
  return api_scope.EscapeMaybe(
      Utils::ToMaybeLocal(i::Execution::Call(i_isolate, self, recv_obj, args)));
}

MaybeLocal<v8::Value> Function::Call(Local<Context> context,
                                     v8::Local<v8::Value> recv, int argc,
                                     v8::Local<v8::Value> argv[]) {
  return Call(v8::Isolate::GetCurrent(), context, recv, argc, argv);
}

void Function::SetName(v8::Local<v8::String> name) {
  auto self = Utils::OpenDirectHandle(this);
  if (!IsJSFunction(*self)) return;
  auto func = i::Cast<i::JSFunction>(self);
  i::DisallowJavascriptExecutionDebugOnly no_execution;
  i::DisallowExceptionsDebugOnly no_exceptions;
  func->shared()->SetName(*Utils::OpenDirectHandle(*name));
}

Local<Value> Function::GetName() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  if (i::IsJSBoundFunction(*self)) {
    auto func = i::Cast<i::JSBoundFunction>(self);
    i::DirectHandle<i::Object> name;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        i_isolate, name, i::JSBoundFunction::GetName(i_isolate, func),
        Local<Value>());
    return Utils::ToLocal(name);
  }
  if (i::IsJSFunction(*self)) {
    auto func = i::Cast<i::JSFunction>(self);
    return Utils::ToLocal(i::direct_handle(func->shared()->Name(), i_isolate));
  }
  return ToApiHandle<Primitive>(i_isolate->factory()->undefined_value());
}

Local<Value> Function::GetInferredName() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* isolate = i::Isolate::Current();
  if (!IsJSFunction(*self)) {
    return ToApiHandle<Primitive>(isolate->factory()->undefined_value());
  }
  auto func = i::Cast<i::JSFunction>(self);
  return Utils::ToLocal(
      i::direct_handle(func->shared()->inferred_name(), isolate));
}

Local<Value> Function::GetDebugName() const {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  if (!IsJSFunction(*self)) {
    return ToApiHandle<Primitive>(i_isolate->factory()->undefined_value());
  }
  auto func = i::Cast<i::JSFunction>(self);
  i::DirectHandle<i::String> name =
      i::JSFunction::GetDebugName(i_isolate, func);
  return Utils::ToLocal(i::direct_handle(*name, i_isolate));
}

ScriptOrigin Function::GetScriptOrigin() const {
  auto self = Utils::OpenDirectHandle(this);
  if (!IsJSFunction(*self)) return v8::ScriptOrigin(Local<Value>());
  auto func = i::Cast<i::JSFunction>(self);
  auto shared = func->shared();
  if (i::IsScript(shared->script())) {
    i::DirectHandle<i::Script> script(i::Cast<i::Script>(shared->script()),
                                      i::Isolate::Current());
    return GetScriptOriginForScript(i::Isolate::Current(), script);
  }
  return v8::ScriptOrigin(Local<Value>());
}

const int Function::kLineOffsetNotFound = -1;

int Function::GetScriptLineNumber() const {
  auto self = *Utils::OpenDirectHandle(this);
  if (!IsJSFunction(self)) {
    return kLineOffsetNotFound;
  }
  auto func = i::Cast<i::JSFunction>(self);
  auto shared = func->shared();
  if (i::IsScript(shared->script())) {
    i::DirectHandle<i::Script> script(i::Cast<i::Script>(shared->script()),
                                      i::Isolate::Current());
    return i::Script::GetLineNumber(script, shared->StartPosition());
  }
  return kLineOffsetNotFound;
}

int Function::GetScriptColumnNumber() const {
  auto self = *Utils::OpenDirectHandle(this);
  if (!IsJSFunction(self)) {
    return kLineOffsetNotFound;
  }
  auto func = i::Cast<i::JSFunction>(self);
  auto shared = func->shared();
  if (i::IsScript(shared->script())) {
    i::DirectHandle<i::Script> script(i::Cast<i::Script>(shared->script()),
                                      i::Isolate::Current());
    return i::Script::GetColumnNumber(script, shared->StartPosition());
  }
  return kLineOffsetNotFound;
}

Location Function::GetScriptLocation() const {
  auto self = *Utils::OpenDirectHandle(this);
  if (!IsJSFunction(self)) {
    return {-1, -1};
  }
  auto func = i::Cast<i::JSFunction>(self);
  auto shared = func->shared();
  if (i::IsScript(shared->script())) {
    i::DirectHandle<i::Script> script(i::Cast<i::Script>(shared->script()),
                                      i::Isolate::Current());
    return {i::Script::GetLineNumber(script, shared->StartPosition()),
            i::Script::GetColumnNumber(script, shared->StartPosition())};
  }
  return {-1, -1};
}

int Function::GetScriptStartPosition() const {
  auto self = *Utils::OpenDirectHandle(this);
  if (!IsJSFunction(self)) {
    return kLineOffsetNotFound;
  }
  auto func = i::Cast<i::JSFunction>(self);
  auto shared = func->shared();
  if (i::IsScript(shared->script())) {
    return shared->StartPosition();
  }
  return kLineOffsetNotFound;
}

int Function::ScriptId() const {
  auto self = *Utils::OpenDirectHandle(this);
  if (!IsJSFunction(self)) return v8::UnboundScript::kNoScriptId;
  auto func = i::Cast<i::JSFunction>(self);
  auto script = func->shared()->script();
  if (!IsScript(script)) return v8::UnboundScript::kNoScriptId;
  return i::Cast<i::Script>(script)->id();
}

Local<v8::Value> Function::GetBoundFunction() const {
  auto self = Utils::OpenDirectHandle(this);
  if (i::IsJSBoundFunction(*self)) {
    auto bound_function = i::Cast<i::JSBoundFunction>(self);
    auto bound_target_function = i::direct_handle(
        bound_function->bound_target_function(), i::Isolate::Current());
    return Utils::CallableToLocal(bound_target_function);
  }
  return v8::Undefined(reinterpret_cast<v8::Isolate*>(i::Isolate::Current()));
}

bool Function::Experimental_IsNopFunction() const {
  auto self = Utils::OpenDirectHandle(this);
  if (!IsJSFunction(*self)) return false;
  auto sfi = i::Cast<i::JSFunction>(*self)->shared();
  i::Isolate* i_isolate = i::Isolate::Current();
  i::IsCompiledScope is_compiled_scope(sfi->is_compiled_scope(i_isolate));
  if (!is_compiled_scope.is_compiled() &&
      !i::Compiler::Compile(i_isolate, i::handle(sfi, i_isolate),
                            i::Compiler::CLEAR_EXCEPTION, &is_compiled_scope)) {
    return false;
  }
  DCHECK(is_compiled_scope.is_compiled());
  // Since |sfi| can be GC'ed, we get it again.
  sfi = i::Cast<i::JSFunction>(*self)->shared();
  if (!sfi->HasBytecodeArray()) return false;
  i::Handle<i::BytecodeArray> bytecode_array(sfi->GetBytecodeArray(i_isolate),
                                             i_isolate);
  i::interpreter::BytecodeArrayIterator it(bytecode_array, 0);
  if (it.current_bytecode() != i::interpreter::Bytecode::kLdaUndefined) {
    return false;
  }
  it.Advance();
  DCHECK(!it.done());
  if (it.current_bytecode() != i::interpreter::Bytecode::kReturn) return false;
  it.Advance();
  DCHECK(it.done());
  return true;
}

MaybeLocal<String> v8::Function::FunctionProtoToString(Local<Context> context) {
  PrepareForExecutionScope api_scope{
      context, RCCId::kAPI_Function_FunctionProtoToString};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto self = Utils::OpenDirectHandle(this);
  return api_scope.EscapeMaybe(i::Cast<i::String>(i::Execution::CallBuiltin(
      i_isolate, i_isolate->function_to_string(), self, {})));
}

int Name::GetIdentityHash() {
  return static_cast<int>(Utils::OpenDirectHandle(this)->EnsureHash());
}

int String::Length() const {
  return static_cast<int>(Utils::OpenDirectHandle(this)->length());
}

bool String::IsOneByte() const {
  return Utils::OpenDirectHandle(this)->IsOneByteRepresentation();
}

// Helpers for ContainsOnlyOneByteHelper
template <size_t size>
struct OneByteMask;
template <>
struct OneByteMask<4> {
  static const uint32_t value = 0xFF00FF00;
};
template <>
struct OneByteMask<8> {
  static const uint64_t value = 0xFF00'FF00'FF00'FF00;
};
static const uintptr_t kOneByteMask = OneByteMask<sizeof(uintptr_t)>::value;
static const uintptr_t kAlignmentMask = sizeof(uintptr_t) - 1;
static inline bool Unaligned(const uint16_t* chars) {
  return reinterpret_cast<const uintptr_t>(chars) & kAlignmentMask;
}

static inline const uint16_t* Align(const uint16_t* chars) {
  return reinterpret_cast<uint16_t*>(reinterpret_cast<uintptr_t>(chars) &
                                     ~kAlignmentMask);
}

class ContainsOnlyOneByteHelper {
 public:
  ContainsOnlyOneByteHelper() : is_one_byte_(true) {}
  ContainsOnlyOneByteHelper(const ContainsOnlyOneByteHelper&) = delete;
  ContainsOnlyOneByteHelper& operator=(const ContainsOnlyOneByteHelper&) =
      delete;
  bool Check(i::Tagged<i::String> string) {
    i::Tagged<i::ConsString> cons_string =
        i::String::VisitFlat(this, string, 0);
    if (cons_string.is_null()) return is_one_byte_;
    return CheckCons(cons_string);
  }
  void VisitOneByteString(const uint8_t* chars, int length) {
    // Nothing to do.
  }
  void VisitTwoByteString(const uint16_t* chars, int length) {
    // Accumulated bits.
    uintptr_t acc = 0;
    // Align to uintptr_t.
    const uint16_t* end = chars + length;
    while (Unaligned(chars) && chars != end) {
      acc |= *chars++;
    }
    // Read word aligned in blocks,
    // checking the return value at the end of each block.
    const uint16_t* aligned_end = Align(end);
    const int increment = sizeof(uintptr_t) / sizeof(uint16_t);
    const int inner_loops = 16;
    while (chars + inner_loops * increment < aligned_end) {
      for (int i = 0; i < inner_loops; i++) {
        acc |= *reinterpret_cast<const uintptr_t*>(chars);
        chars += increment;
      }
      // Check for early return.
      if ((acc & kOneByteMask) != 0) {
        is_one_byte_ = false;
        return;
      }
    }
    // Read the rest.
    while (chars != end) {
      acc |= *chars++;
    }
    // Check result.
    if ((acc & kOneByteMask) != 0) is_one_byte_ = false;
  }

 private:
  bool CheckCons(i::Tagged<i::ConsString> cons_string) {
    while (true) {
      // Check left side if flat.
      i::Tagged<i::String> left = cons_string->first();
      i::Tagged<i::ConsString> left_as_cons =
          i::String::VisitFlat(this, left, 0);
      if (!is_one_byte_) return false;
      // Check right side if flat.
      i::Tagged<i::String> right = cons_string->second();
      i::Tagged<i::ConsString> right_as_cons =
          i::String::VisitFlat(this, right, 0);
      if (!is_one_byte_) return false;
      // Standard recurse/iterate trick.
      if (!left_as_cons.is_null() && !right_as_cons.is_null()) {
        if (left->length() < right->length()) {
          CheckCons(left_as_cons);
          cons_string = right_as_cons;
        } else {
          CheckCons(right_as_cons);
          cons_string = left_as_cons;
        }
        // Check fast return.
        if (!is_one_byte_) return false;
        continue;
      }
      // Descend left in place.
      if (!left_as_cons.is_null()) {
        cons_string = left_as_cons;
        continue;
      }
      // Descend right in place.
      if (!right_as_cons.is_null()) {
        cons_string = right_as_cons;
        continue;
      }
      // Terminate.
      break;
    }
    return is_one_byte_;
  }
  bool is_one_byte_;
};

bool String::ContainsOnlyOneByte() const {
  auto str = Utils::OpenDirectHandle(this);
  if (str->IsOneByteRepresentation()) return true;
  ContainsOnlyOneByteHelper helper;
  return helper.Check(*str);
}

int String::Utf8Length(Isolate* v8_isolate) const {
  auto str = Utils::OpenDirectHandle(this);
  str = i::String::Flatten(reinterpret_cast<i::Isolate*>(v8_isolate), str);
  int length = str->length();
  if (length == 0) return 0;
  i::DisallowGarbageCollection no_gc;
  i::String::FlatContent flat = str->GetFlatContent(no_gc);
  DCHECK(flat.IsFlat());
  int utf8_length = 0;
  if (flat.IsOneByte()) {
    for (uint8_t c : flat.ToOneByteVector()) {
      utf8_length += c >> 7;
    }
    utf8_length += length;
  } else {
    int last_character = unibrow::Utf16::kNoPreviousCharacter;
    for (uint16_t c : flat.ToUC16Vector()) {
      utf8_length += unibrow::Utf8::Length(c, last_character);
      last_character = c;
    }
  }
  return utf8_length;
}

size_t String::Utf8LengthV2(Isolate* v8_isolate) const {
  auto str = Utils::OpenDirectHandle(this);
  return i::String::Utf8Length(reinterpret_cast<i::Isolate*>(v8_isolate), str);
}

namespace {
// Writes the flat content of a string to a buffer. This is done in two phases.
// The first phase calculates a pessimistic estimate (writable_length) on how
// many code units can be safely written without exceeding the buffer capacity
// and without leaving at a lone surrogate. The estimated number of code units
// is then written out in one go, and the reported byte usage is used to
// correct the estimate. This is repeated until the estimate becomes <= 0 or
// all code units have been written out. The second phase writes out code
// units until the buffer capacity is reached, would be exceeded by the next
// unit, or all code units have been written out.
template <typename Char>
static int WriteUtf8Impl(base::Vector<const Char> string, char* write_start,
                         int write_capacity, int options,
                         int* utf16_chars_read_out) {
  bool write_null = !(options & v8::String::NO_NULL_TERMINATION);
  bool replace_invalid_utf8 = (options & v8::String::REPLACE_INVALID_UTF8);
  char* current_write = write_start;
  const Char* read_start = string.begin();
  int read_index = 0;
  int read_length = string.length();
  int prev_char = unibrow::Utf16::kNoPreviousCharacter;
  // Do a fast loop where there is no exit capacity check.
  // Need enough space to write everything but one character.
  static_assert(unibrow::Utf16::kMaxExtraUtf8BytesForOneUtf16CodeUnit == 3);
  static const int kMaxSizePerChar = sizeof(Char) == 1 ? 2 : 3;
  while (read_index < read_length) {
    int up_to = read_length;
    if (write_capacity != -1) {
      int remaining_capacity =
          write_capacity - static_cast<int>(current_write - write_start);
      int writable_length =
          (remaining_capacity - kMaxSizePerChar) / kMaxSizePerChar;
      // Need to drop into slow loop.
      if (writable_length <= 0) break;
      up_to = std::min(up_to, read_index + writable_length);
    }
    // Write the characters to the stream.
    if (sizeof(Char) == 1) {
      // Simply memcpy if we only have ASCII characters.
      uint8_t char_mask = 0;
      for (int i = read_index; i < up_to; i++) char_mask |= read_start[i];
      if ((char_mask & 0x80) == 0) {
        int copy_length = up_to - read_index;
        memcpy(current_write, read_start + read_index, copy_length);
        current_write += copy_length;
        read_index = up_to;
      } else {
        for (; read_index < up_to; read_index++) {
          current_write += unibrow::Utf8::EncodeOneByte(
              current_write, static_cast<uint8_t>(read_start[read_index]));
          DCHECK(write_capacity == -1 ||
                 (current_write - write_start) <= write_capacity);
        }
      }
    } else {
      for (; read_index < up_to; read_index++) {
        uint16_t character = read_start[read_index];
        current_write += unibrow::Utf8::Encode(current_write, character,
                                               prev_char, replace_invalid_utf8);
        prev_char = character;
        DCHECK(write_capacity == -1 ||
               (current_write - write_start) <= write_capacity);
      }
    }
  }
  if (read_index < read_length) {
    DCHECK_NE(-1, write_capacity);
    // Aborted due to limited capacity. Check capacity on each iteration.
    int remaining_capacity =
        write_capacity - static_cast<int>(current_write - write_start);
    DCHECK_GE(remaining_capacity, 0);
    for (; read_index < read_length && remaining_capacity > 0; read_index++) {
      uint32_t character = read_start[read_index];
      int written = 0;
      // We can't use a local buffer here because Encode needs to modify
      // previous characters in the stream.  We know, however, that
      // exactly one character will be advanced.
      if (unibrow::Utf16::IsSurrogatePair(prev_char, character)) {
        written = unibrow::Utf8::Encode(current_write, character, prev_char,
                                        replace_invalid_utf8);
        DCHECK_EQ(written, 1);
      } else {
        // Use a scratch buffer to check the required characters.
        char temp_buffer[unibrow::Utf8::kMaxEncodedSize];
        // Encoding a surrogate pair to Utf8 always takes 4 bytes.
        static const int kSurrogatePairEncodedSize =
            static_cast<int>(unibrow::Utf8::kMaxEncodedSize);
        // For REPLACE_INVALID_UTF8, catch the case where we cut off in the
        // middle of a surrogate pair. Abort before encoding the pair instead.
        if (replace_invalid_utf8 &&
            remaining_capacity < kSurrogatePairEncodedSize &&
            unibrow::Utf16::IsLeadSurrogate(character) &&
            read_index + 1 < read_length &&
            unibrow::Utf16::IsTrailSurrogate(read_start[read_index + 1])) {
          write_null = false;
          break;
        }
        // Can't encode using prev_char as gcc has array bounds issues.
        written = unibrow::Utf8::Encode(temp_buffer, character,
                                        unibrow::Utf16::kNoPreviousCharacter,
                                        replace_invalid_utf8);
        if (written > remaining_capacity) {
          // Won't fit. Abort and do not null-terminate the result.
          write_null = false;
          break;
        }
        // Copy over the character from temp_buffer.
        for (int i = 0; i < written; i++) current_write[i] = temp_buffer[i];
      }

      current_write += written;
      remaining_capacity -= written;
      prev_char = character;
    }
  }

  // Write out number of utf16 characters written to the stream.
  if (utf16_chars_read_out != nullptr) *utf16_chars_read_out = read_index;

  // Only null-terminate if there's space.
  if (write_null && (write_capacity == -1 ||
                     (current_write - write_start) < write_capacity)) {
    *current_write++ = '\0';
  }
  return static_cast<int>(current_write - write_start);
}
}  // anonymous namespace

int String::WriteUtf8(Isolate* v8_isolate, char* buffer, int capacity,
                      int* nchars_ref, int options) const {
  auto str = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_String_WriteUtf8);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  str = i::String::Flatten(i_isolate, str);
  i::DisallowGarbageCollection no_gc;
  i::String::FlatContent content = str->GetFlatContent(no_gc);
  if (content.IsOneByte()) {
    return WriteUtf8Impl<uint8_t>(content.ToOneByteVector(), buffer, capacity,
                                  options, nchars_ref);
  } else {
    return WriteUtf8Impl<uint16_t>(content.ToUC16Vector(), buffer, capacity,
                                   options, nchars_ref);
  }
}

template <typename CharType>
static inline int WriteHelper(i::Isolate* i_isolate, const String* string,
                              CharType* buffer, int start, int length,
                              int options) {
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_String_Write);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  DCHECK(start >= 0 && length >= -1);
  auto str = Utils::OpenDirectHandle(string);
  int end = start + length;
  if ((length == -1) || (static_cast<uint32_t>(length) > str->length() - start))
    end = str->length();
  if (end < 0) return 0;
  int write_length = end - start;
  if (start < end) i::String::WriteToFlat(*str, buffer, start, write_length);
  if (!(options & String::NO_NULL_TERMINATION) &&
      (length == -1 || write_length < length)) {
    buffer[write_length] = '\0';
  }
  return write_length;
}

int String::WriteOneByte(Isolate* v8_isolate, uint8_t* buffer, int start,
                         int length, int options) const {
  return WriteHelper(reinterpret_cast<i::Isolate*>(v8_isolate), this, buffer,
                     start, length, options);
}

int String::Write(Isolate* v8_isolate, uint16_t* buffer, int start, int length,
                  int options) const {
  return WriteHelper(reinterpret_cast<i::Isolate*>(v8_isolate), this, buffer,
                     start, length, options);
}

template <typename CharType>
static inline void WriteHelperV2(i::Isolate* i_isolate, const String* string,
                                 CharType* buffer, uint32_t offset,
                                 uint32_t length, int flags) {
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_String_Write);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);

  DCHECK_LE(length, string->Length());
  DCHECK_LE(offset, string->Length() - length);

  auto str = Utils::OpenDirectHandle(string);
  i::String::WriteToFlat(*str, buffer, offset, length);
  if (flags & String::WriteFlags::kNullTerminate) {
    buffer[length] = '\0';
  }
}

void String::WriteV2(Isolate* v8_isolate, uint32_t offset, uint32_t length,
                     uint16_t* buffer, int flags) const {
  WriteHelperV2(reinterpret_cast<i::Isolate*>(v8_isolate), this, buffer, offset,
                length, flags);
}

void String::WriteOneByteV2(Isolate* v8_isolate, uint32_t offset,
                            uint32_t length, uint8_t* buffer, int flags) const {
  DCHECK(IsOneByte());
  WriteHelperV2(reinterpret_cast<i::Isolate*>(v8_isolate), this, buffer, offset,
                length, flags);
}

size_t String::WriteUtf8V2(Isolate* v8_isolate, char* buffer, size_t capacity,
                           int flags,
                           size_t* processed_characters_return) const {
  auto str = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_String_WriteUtf8);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::String::Utf8EncodingFlags i_flags;
  if (flags & String::WriteFlags::kNullTerminate) {
    i_flags |= i::String::Utf8EncodingFlag::kNullTerminate;
  }
  if (flags & String::WriteFlags::kReplaceInvalidUtf8) {
    i_flags |= i::String::Utf8EncodingFlag::kReplaceInvalid;
  }
  return i::String::WriteUtf8(i_isolate, str, buffer, capacity, i_flags,
                              processed_characters_return);
}

namespace {

bool HasExternalStringResource(i::Tagged<i::String> string) {
  return i::StringShape(string).IsExternal() ||
         string->HasExternalForwardingIndex(kAcquireLoad);
}

v8::String::ExternalStringResourceBase* GetExternalResourceFromForwardingTable(
    i::Tagged<i::String> string, uint32_t raw_hash, bool* is_one_byte) {
  DCHECK(i::String::IsExternalForwardingIndex(raw_hash));
  const int index = i::String::ForwardingIndexValueBits::decode(raw_hash);
  // Note that with a shared heap the main and worker isolates all share the
  // same forwarding table.
  auto resource =
      i::Isolate::Current()->string_forwarding_table()->GetExternalResource(
          index, is_one_byte);
  DCHECK_NOT_NULL(resource);
  return resource;
}

}  // namespace

bool v8::String::IsExternal() const {
  return HasExternalStringResource(*Utils::OpenDirectHandle(this));
}

bool v8::String::IsExternalTwoByte() const {
  auto str = Utils::OpenDirectHandle(this);
  if (i::StringShape(*str).IsExternalTwoByte()) return true;
  uint32_t raw_hash_field = str->raw_hash_field(kAcquireLoad);
  if (i::String::IsExternalForwardingIndex(raw_hash_field)) {
    bool is_one_byte;
    GetExternalResourceFromForwardingTable(*str, raw_hash_field, &is_one_byte);
    return !is_one_byte;
  }
  return false;
}

bool v8::String::IsExternalOneByte() const {
  auto str = Utils::OpenDirectHandle(this);
  if (i::StringShape(*str).IsExternalOneByte()) return true;
  uint32_t raw_hash_field = str->raw_hash_field(kAcquireLoad);
  if (i::String::IsExternalForwardingIndex(raw_hash_field)) {
    bool is_one_byte;
    GetExternalResourceFromForwardingTable(*str, raw_hash_field, &is_one_byte);
    return is_one_byte;
  }
  return false;
}

Local<v8::String> v8::String::InternalizeString(Isolate* v8_isolate) {
  auto* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  auto str = Utils::OpenDirectHandle(this);
  return Utils::ToLocal(isolate->factory()->InternalizeString(str));
}

void v8::String::VerifyExternalStringResource(
    v8::String::ExternalStringResource* value) const {
  i::DisallowGarbageCollection no_gc;
  i::Tagged<i::String> str = *Utils::OpenDirectHandle(this);
  const v8::String::ExternalStringResource* expected = nullptr;

  if (i::IsThinString(str)) {
    str = i::Cast<i::ThinString>(str)->actual();
  }

  if (i::StringShape(str).IsExternalTwoByte()) {
    const void* resource = i::Cast<i::ExternalTwoByteString>(str)->resource();
    expected = reinterpret_cast<const ExternalStringResource*>(resource);
  } else {
    uint32_t raw_hash_field = str->raw_hash_field(kAcquireLoad);
    if (i::String::IsExternalForwardingIndex(raw_hash_field)) {
      bool is_one_byte;
      auto resource = GetExternalResourceFromForwardingTable(
          str, raw_hash_field, &is_one_byte);
      if (!is_one_byte) {
        expected = reinterpret_cast<const ExternalStringResource*>(resource);
      }
    }
  }
  CHECK_EQ(expected, value);
}

void v8::String::VerifyExternalStringResourceBase(
    v8::String::ExternalStringResourceBase* value, Encoding encoding) const {
  i::DisallowGarbageCollection no_gc;
  i::Tagged<i::String> str = *Utils::OpenDirectHandle(this);
  const v8::String::ExternalStringResourceBase* expected;
  Encoding expectedEncoding;

  if (i::IsThinString(str)) {
    str = i::Cast<i::ThinString>(str)->actual();
  }

  if (i::StringShape(str).IsExternalOneByte()) {
    const void* resource = i::Cast<i::ExternalOneByteString>(str)->resource();
    expected = reinterpret_cast<const ExternalStringResourceBase*>(resource);
    expectedEncoding = ONE_BYTE_ENCODING;
  } else if (i::StringShape(str).IsExternalTwoByte()) {
    const void* resource = i::Cast<i::ExternalTwoByteString>(str)->resource();
    expected = reinterpret_cast<const ExternalStringResourceBase*>(resource);
    expectedEncoding = TWO_BYTE_ENCODING;
  } else {
    uint32_t raw_hash_field = str->raw_hash_field(kAcquireLoad);
    if (i::String::IsExternalForwardingIndex(raw_hash_field)) {
      bool is_one_byte;
      expected = GetExternalResourceFromForwardingTable(str, raw_hash_field,
                                                        &is_one_byte);
      expectedEncoding = is_one_byte ? ONE_BYTE_ENCODING : TWO_BYTE_ENCODING;
    } else {
      expected = nullptr;
      expectedEncoding = str->IsOneByteRepresentation() ? ONE_BYTE_ENCODING
                                                        : TWO_BYTE_ENCODING;
    }
  }
  CHECK_EQ(expected, value);
  CHECK_EQ(expectedEncoding, encoding);
}

String::ExternalStringResource* String::GetExternalStringResourceSlow() const {
  i::DisallowGarbageCollection no_gc;
  i::Tagged<i::String> str = *Utils::OpenDirectHandle(this);

  if (i::IsThinString(str)) {
    str = i::Cast<i::ThinString>(str)->actual();
  }

  if (i::StringShape(str).IsExternalTwoByte()) {
    Isolate* isolate = i::Internals::GetCurrentIsolateForSandbox();
    i::Address value =
        i::Internals::ReadExternalPointerField<i::kExternalStringResourceTag>(
            isolate, str.ptr(), i::Internals::kStringResourceOffset);
    return reinterpret_cast<String::ExternalStringResource*>(value);
  } else {
    uint32_t raw_hash_field = str->raw_hash_field(kAcquireLoad);
    if (i::String::IsExternalForwardingIndex(raw_hash_field)) {
      bool is_one_byte;
      auto resource = GetExternalResourceFromForwardingTable(
          str, raw_hash_field, &is_one_byte);
      if (!is_one_byte) {
        return reinterpret_cast<ExternalStringResource*>(resource);
      }
    }
  }
  return nullptr;
}

void String::ExternalStringResource::UpdateDataCache() {
  DCHECK(IsCacheable());
  cached_data_ = data();
}

void String::ExternalStringResource::CheckCachedDataInvariants() const {
  DCHECK(IsCacheable() && cached_data_ != nullptr);
}

void String::ExternalOneByteStringResource::UpdateDataCache() {
  DCHECK(IsCacheable());
  cached_data_ = data();
}

void String::ExternalOneByteStringResource::CheckCachedDataInvariants() const {
  DCHECK(IsCacheable() && cached_data_ != nullptr);
}

String::ExternalStringResourceBase* String::GetExternalStringResourceBaseSlow(
    String::Encoding* encoding_out) const {
  i::DisallowGarbageCollection no_gc;
  ExternalStringResourceBase* resource = nullptr;
  i::Tagged<i::String> str = *Utils::OpenDirectHandle(this);

  if (i::IsThinString(str)) {
    str = i::Cast<i::ThinString>(str)->actual();
  }

  internal::Address string = str.ptr();
  int type = i::Internals::GetInstanceType(string) &
             i::Internals::kStringRepresentationAndEncodingMask;
  *encoding_out =
      static_cast<Encoding>(type & i::Internals::kStringEncodingMask);
  if (i::StringShape(str).IsExternalOneByte() ||
      i::StringShape(str).IsExternalTwoByte()) {
    Isolate* isolate = i::Internals::GetCurrentIsolateForSandbox();
    i::Address value =
        i::Internals::ReadExternalPointerField<i::kExternalStringResourceTag>(
            isolate, string, i::Internals::kStringResourceOffset);
    resource = reinterpret_cast<ExternalStringResourceBase*>(value);
  } else {
    uint32_t raw_hash_field = str->raw_hash_field();
    if (i::String::IsExternalForwardingIndex(raw_hash_field)) {
      bool is_one_byte;
      resource = GetExternalResourceFromForwardingTable(str, raw_hash_field,
                                                        &is_one_byte);
      *encoding_out = is_one_byte ? Encoding::ONE_BYTE_ENCODING
                                  : Encoding::TWO_BYTE_ENCODING;
    }
  }
  return resource;
}

const v8::String::ExternalOneByteStringResource*
v8::String::GetExternalOneByteStringResource() const {
  i::DisallowGarbageCollection no_gc;
  i::Tagged<i::String> str = *Utils::OpenDirectHandle(this);
  if (i::StringShape(str).IsExternalOneByte()) {
    return i::Cast<i::ExternalOneByteString>(str)->resource();
  } else if (i::IsThinString(str)) {
    str = i::Cast<i::ThinString>(str)->actual();
    if (i::StringShape(str).IsExternalOneByte()) {
      return i::Cast<i::ExternalOneByteString>(str)->resource();
    }
  }
  uint32_t raw_hash_field = str->raw_hash_field(kAcquireLoad);
  if (i::String::IsExternalForwardingIndex(raw_hash_field)) {
    bool is_one_byte;
    auto resource = GetExternalResourceFromForwardingTable(str, raw_hash_field,
                                                           &is_one_byte);
    if (is_one_byte) {
      return reinterpret_cast<ExternalOneByteStringResource*>(resource);
    }
  }
  return nullptr;
}

Local<Value> Symbol::Description(Isolate* v8_isolate) const {
  auto sym = Utils::OpenDirectHandle(this);
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  return Utils::ToLocal(i::direct_handle(sym->description(), isolate));
}

Local<Value> Private::Name() const {
  const Symbol* sym = reinterpret_cast<const Symbol*>(this);
  // v8::Private symbols are created by API and are therefore writable, so we
  // can always recover an Isolate.
  i::Isolate* i_isolate = i::Isolate::Current();
  return sym->Description(reinterpret_cast<Isolate*>(i_isolate));
}

double Number::Value() const {
  return i::Object::NumberValue(*Utils::OpenDirectHandle(this));
}

bool Boolean::Value() const {
  return i::IsTrue(*Utils::OpenDirectHandle(this));
}

int64_t Integer::Value() const {
  auto obj = *Utils::OpenDirectHandle(this);
  if (i::IsSmi(obj)) {
    return i::Smi::ToInt(obj);
  } else {
    return static_cast<int64_t>(i::Object::NumberValue(obj));
  }
}

int32_t Int32::Value() const {
  auto obj = *Utils::OpenDirectHandle(this);
  if (i::IsSmi(obj)) {
    return i::Smi::ToInt(obj);
  } else {
    return static_cast<int32_t>(i::Object::NumberValue(obj));
  }
}

uint32_t Uint32::Value() const {
  auto obj = *Utils::OpenDirectHandle(this);
  if (i::IsSmi(obj)) {
    return i::Smi::ToInt(obj);
  } else {
    return static_cast<uint32_t>(i::Object::NumberValue(obj));
  }
}

int v8::Object::InternalFieldCount() const {
  auto self = *Utils::OpenDirectHandle(this);
  if (!IsJSObject(self)) return 0;
  return i::Cast<i::JSObject>(self)->GetEmbedderFieldCount();
}

static V8_INLINE bool InternalFieldOK(i::DirectHandle<i::JSReceiver> obj,
                                      int index, const char* location) {
  return Utils::ApiCheck(
      IsJSObject(*obj) &&
          (index < i::Cast<i::JSObject>(*obj)->GetEmbedderFieldCount()),
      location, "Internal field out of bounds");
}

Local<Data> v8::Object::SlowGetInternalField(int index) {
  auto obj = Utils::OpenDirectHandle(this);
  const char* location = "v8::Object::GetInternalField()";
  if (!InternalFieldOK(obj, index, location)) return {};
  i::Isolate* isolate = i::Isolate::Current();
  return ToApiHandle<Data>(i::direct_handle(
      i::Cast<i::JSObject>(*obj)->GetEmbedderField(index), isolate));
}

void v8::Object::SetInternalField(int index, v8::Local<Data> value) {
  auto obj = Utils::OpenDirectHandle(this);
  const char* location = "v8::Object::SetInternalField()";
  if (!InternalFieldOK(obj, index, location)) return;
  auto val = Utils::OpenDirectHandle(*value);
  i::Cast<i::JSObject>(obj)->SetEmbedderField(index, *val);
}

void* v8::Object::SlowGetAlignedPointerFromInternalField(v8::Isolate* isolate,
                                                         int index) {
  auto obj = Utils::OpenDirectHandle(this);
  const char* location = "v8::Object::GetAlignedPointerFromInternalField()";
  if (!InternalFieldOK(obj, index, location)) return nullptr;
  void* result;
  Utils::ApiCheck(
      i::EmbedderDataSlot(i::Cast<i::JSObject>(*obj), index)
          .ToAlignedPointer(reinterpret_cast<i::Isolate*>(isolate), &result),
      location, "Unaligned pointer");
  return result;
}

void* v8::Object::SlowGetAlignedPointerFromInternalField(int index) {
  auto obj = Utils::OpenDirectHandle(this);
  const char* location = "v8::Object::GetAlignedPointerFromInternalField()";
  if (!InternalFieldOK(obj, index, location)) return nullptr;
  void* result;
  Utils::ApiCheck(i::EmbedderDataSlot(i::Cast<i::JSObject>(*obj), index)
                      .ToAlignedPointer(i::Isolate::Current(), &result),
                  location, "Unaligned pointer");
  return result;
}

void v8::Object::SetAlignedPointerInInternalField(int index, void* value) {
  auto obj = Utils::OpenDirectHandle(this);
  const char* location = "v8::Object::SetAlignedPointerInInternalField()";
  if (!InternalFieldOK(obj, index, location)) return;

  i::DisallowGarbageCollection no_gc;
  Utils::ApiCheck(
      i::EmbedderDataSlot(i::Cast<i::JSObject>(*obj), index)
          .store_aligned_pointer(i::Isolate::Current(), *obj, value),
      location, "Unaligned pointer");
  DCHECK_EQ(value, GetAlignedPointerFromInternalField(index));
}

void v8::Object::SetAlignedPointerInInternalFields(int argc, int indices[],
                                                   void* values[]) {
  auto obj = Utils::OpenDirectHandle(this);
  if (!IsJSObject(*obj)) return;
  i::DisallowGarbageCollection no_gc;
  const char* location = "v8::Object::SetAlignedPointerInInternalFields()";
  auto js_obj = i::Cast<i::JSObject>(*obj);
  int nof_embedder_fields = js_obj->GetEmbedderFieldCount();
  for (int i = 0; i < argc; i++) {
    int index = indices[i];
    if (!Utils::ApiCheck(index < nof_embedder_fields, location,
                         "Internal field out of bounds")) {
      return;
    }
    void* value = values[i];
    Utils::ApiCheck(
        i::EmbedderDataSlot(js_obj, index)
            .store_aligned_pointer(i::Isolate::Current(), *obj, value),
        location, "Unaligned pointer");
    DCHECK_EQ(value, GetAlignedPointerFromInternalField(index));
  }
}

// static
void* v8::Object::Unwrap(v8::Isolate* isolate, i::Address wrapper_obj,
                         CppHeapPointerTagRange tag_range) {
  DCHECK_LE(tag_range.lower_bound, tag_range.upper_bound);
  return i::CppHeapObjectWrapper(
             i::Cast<i::JSObject>(i::Tagged<i::Object>(wrapper_obj)))
      .GetCppHeapWrappable(reinterpret_cast<i::Isolate*>(isolate), tag_range);
}

// static
void v8::Object::Wrap(v8::Isolate* isolate, i::Address wrapper_obj,
                      CppHeapPointerTag tag, void* wrappable) {
  return i::CppHeapObjectWrapper(
             i::Cast<i::JSObject>(i::Tagged<i::Object>(wrapper_obj)))
      .SetCppHeapWrappable(reinterpret_cast<i::Isolate*>(isolate), wrappable,
                           tag);
}

namespace {

// Checks that given prototype is a valid hidden prototype of JSGlobalProxy.
// It must be either JSGlobalObject or remote object.
void CheckPrototypeIsGlobalObjectOrRemoteObject(
    i::Tagged<i::JSPrototype> prototype) {
  CHECK(i::IsJSSpecialObject(prototype));
  auto prototype_obj = i::Cast<i::JSSpecialObject>(prototype);
  CHECK(i::IsJSGlobalObject(prototype_obj) ||
        i::IsNull(prototype_obj->map()->map()->native_context_or_null()));
}

}  // namespace

// static
void v8::Object::WrapGlobal(v8::Isolate* isolate,
                            const v8::Local<v8::Object>& wrapper,
                            Wrappable* wrappable, CppHeapPointerTag tag) {
  auto global_proxy = i::Cast<i::JSObject>(
      i::Tagged<i::Object>(internal::ValueHelper::ValueAsAddress(*wrapper)));
  CHECK(i::IsJSGlobalProxy(global_proxy));

  auto prototype = global_proxy->map()->prototype();
  CheckPrototypeIsGlobalObjectOrRemoteObject(prototype);

  i::CppHeapObjectWrapper(global_proxy)
      .SetCppHeapWrappable(reinterpret_cast<i::Isolate*>(isolate), wrappable,
                           tag);
  i::CppHeapObjectWrapper(i::Cast<i::JSObject>(prototype))
      .SetCppHeapWrappable(reinterpret_cast<i::Isolate*>(isolate), wrappable,
                           tag);
}

// static
bool v8::Object::CheckGlobalWrappable(v8::Isolate* isolate,
                                      const v8::Local<v8::Object>& wrapper,
                                      CppHeapPointerTagRange tag_range) {
  auto global_proxy = i::Cast<i::JSObject>(
      i::Tagged<i::Object>(internal::ValueHelper::ValueAsAddress(*wrapper)));
  Utils::ApiCheck(i::IsJSGlobalProxy(global_proxy),
                  "v8::Object::CheckGlobalWrappable",
                  "Bad object provided, expecting JSGlobalProxy");

  auto prototype = global_proxy->map()->prototype();
  CheckPrototypeIsGlobalObjectOrRemoteObject(prototype);

  void* global_proxy_wrappable =
      i::CppHeapObjectWrapper(global_proxy)
          .GetCppHeapWrappable(reinterpret_cast<i::Isolate*>(isolate),
                               tag_range);

  void* hidden_prototype_wrappable =
      i::CppHeapObjectWrapper(i::Cast<i::JSObject>(prototype))
          .GetCppHeapWrappable(reinterpret_cast<i::Isolate*>(isolate),
                               tag_range);

  Utils::ApiCheck(
      global_proxy_wrappable == hidden_prototype_wrappable,
      "v8::Object::CheckGlobalWrappable",
      "Global object's wrappable is not equal hidden prototype's wrappable");
  return true;
}

// --- E n v i r o n m e n t ---

void v8::V8::InitializePlatform(Platform* platform) {
  i::V8::InitializePlatform(platform);
}

void v8::V8::DisposePlatform() { i::V8::DisposePlatform(); }

bool v8::V8::Initialize(const int build_config) {
  const bool kEmbedderPointerCompression =
      (build_config & kPointerCompression) != 0;
  if (kEmbedderPointerCompression != COMPRESS_POINTERS_BOOL) {
    FATAL(
        "Embedder-vs-V8 build configuration mismatch. On embedder side "
        "pointer compression is %s while on V8 side it's %s.",
        kEmbedderPointerCompression ? "ENABLED" : "DISABLED",
        COMPRESS_POINTERS_BOOL ? "ENABLED" : "DISABLED");
  }

  const int kEmbedderSmiValueSize = (build_config & k31BitSmis) ? 31 : 32;
  if (kEmbedderSmiValueSize != internal::kSmiValueSize) {
    FATAL(
        "Embedder-vs-V8 build configuration mismatch. On embedder side "
        "Smi value size is %d while on V8 side it's %d.",
        kEmbedderSmiValueSize, internal::kSmiValueSize);
  }

  const bool kEmbedderSandbox = (build_config & kSandbox) != 0;
  if (kEmbedderSandbox != V8_ENABLE_SANDBOX_BOOL) {
    FATAL(
        "Embedder-vs-V8 build configuration mismatch. On embedder side "
        "sandbox is %s while on V8 side it's %s.",
        kEmbedderSandbox ? "ENABLED" : "DISABLED",
        V8_ENABLE_SANDBOX_BOOL ? "ENABLED" : "DISABLED");
  }

  const bool kEmbedderTargetOsIsAndroid =
      (build_config & kTargetOsIsAndroid) != 0;
#ifdef V8_TARGET_OS_ANDROID
  const bool kV8TargetOsIsAndroid = true;
#else
  const bool kV8TargetOsIsAndroid = false;
#endif
  if (kEmbedderTargetOsIsAndroid != kV8TargetOsIsAndroid) {
    FATAL(
        "Embedder-vs-V8 build configuration mismatch. On embedder side "
        "target OS is %s while on V8 side it's %s.",
        kEmbedderTargetOsIsAndroid ? "Android" : "not Android",
        kV8TargetOsIsAndroid ? "Android" : "not Android");
  }

  const bool kEmbedderEnableChecks = (build_config & kEnableChecks) != 0;
#ifdef V8_ENABLE_CHECKS
  const bool kV8EnableChecks = true;
#else
  const bool kV8EnableChecks = false;
#endif
  if (kEmbedderEnableChecks != kV8EnableChecks) {
    FATAL(
        "Embedder-vs-V8 build configuration mismatch. On embedder side "
        "V8_ENABLE_CHECKS is %s while on V8 side it's %s.",
        kEmbedderEnableChecks ? "ENABLED" : "DISABLED",
        kV8EnableChecks ? "ENABLED" : "DISABLED");
  }

  i::V8::Initialize();
  if (!cppgc::IsInitialized()) {
    // cppgc::InitializeProcess() has to becalled after V8::Initialize().
    // V8::Initialize() triggers OS::Platform initialization code that is needed
    // by the default page allocator on Fuchsia to allocate memory correctly.
    cppgc::InitializeProcess(i::V8::GetCurrentPlatform()->GetPageAllocator());
  }
  return true;
}

#if V8_OS_LINUX || V8_OS_DARWIN
bool TryHandleWebAssemblyTrapPosix(int sig_code, siginfo_t* info,
                                   void* context) {
#if V8_ENABLE_WEBASSEMBLY && V8_TRAP_HANDLER_SUPPORTED
  return i::trap_handler::TryHandleSignal(sig_code, info, context);
#else
  return false;
#endif
}
#endif

#if V8_OS_WIN
bool TryHandleWebAssemblyTrapWindows(EXCEPTION_POINTERS* exception) {
#if V8_ENABLE_WEBASSEMBLY && V8_TRAP_HANDLER_SUPPORTED
  return i::trap_handler::TryHandleWasmTrap(exception);
#else
  return false;
#endif
}
#endif

bool V8::EnableWebAssemblyTrapHandler(bool use_v8_signal_handler) {
#if V8_ENABLE_WEBASSEMBLY
  return i::trap_handler::EnableTrapHandler(use_v8_signal_handler);
#else
  return false;
#endif
}

#if defined(V8_OS_WIN)
void V8::SetUnhandledExceptionCallback(
    UnhandledExceptionCallback unhandled_exception_callback) {
#if defined(V8_OS_WIN64)
  i::win64_unwindinfo::SetUnhandledExceptionCallback(
      unhandled_exception_callback);
#else
  // Not implemented, port needed.
#endif  // V8_OS_WIN64
}
#endif  // V8_OS_WIN

void v8::V8::SetFatalMemoryErrorCallback(
    v8::OOMErrorCallback oom_error_callback) {
  g_oom_error_callback = oom_error_callback;
}

void v8::V8::SetEntropySource(EntropySource entropy_source) {
  base::RandomNumberGenerator::SetEntropySource(entropy_source);
}

void v8::V8::SetReturnAddressLocationResolver(
    ReturnAddressLocationResolver return_address_resolver) {
  i::StackFrame::SetReturnAddressLocationResolver(return_address_resolver);
}

bool v8::V8::Dispose() {
  i::V8::Dispose();
  return true;
}

SharedMemoryStatistics::SharedMemoryStatistics()
    : read_only_space_size_(0),
      read_only_space_used_size_(0),
      read_only_space_physical_size_(0) {}

HeapStatistics::HeapStatistics()
    : total_heap_size_(0),
      total_heap_size_executable_(0),
      total_physical_size_(0),
      total_available_size_(0),
      used_heap_size_(0),
      heap_size_limit_(0),
      malloced_memory_(0),
      external_memory_(0),
      peak_malloced_memory_(0),
      does_zap_garbage_(false),
      number_of_native_contexts_(0),
      number_of_detached_contexts_(0) {}

HeapSpaceStatistics::HeapSpaceStatistics()
    : space_name_(nullptr),
      space_size_(0),
      space_used_size_(0),
      space_available_size_(0),
      physical_space_size_(0) {}

HeapObjectStatistics::HeapObjectStatistics()
    : object_type_(nullptr),
      object_sub_type_(nullptr),
      object_count_(0),
      object_size_(0) {}

HeapCodeStatistics::HeapCodeStatistics()
    : code_and_metadata_size_(0),
      bytecode_and_metadata_size_(0),
      external_script_source_size_(0),
      cpu_profiler_metadata_size_(0) {}

bool v8::V8::InitializeICU(const char* icu_data_file) {
  return i::InitializeICU(icu_data_file);
}

bool v8::V8::InitializeICUDefaultLocation(const char* exec_path,
                                          const char* icu_data_file) {
  return i::InitializeICUDefaultLocation(exec_path, icu_data_file);
}

void v8::V8::InitializeExternalStartupData(const char* directory_path) {
  i::InitializeExternalStartupData(directory_path);
}

// static
void v8::V8::InitializeExternalStartupDataFromFile(const char* snapshot_blob) {
  i::InitializeExternalStartupDataFromFile(snapshot_blob);
}

const char* v8::V8::GetVersion() { return i::Version::GetVersion(); }

#ifdef V8_ENABLE_SANDBOX
VirtualAddressSpace* v8::V8::GetSandboxAddressSpace() {
  Utils::ApiCheck(i::Sandbox::current()->is_initialized(),
                  "v8::V8::GetSandboxAddressSpace",
                  "The sandbox must be initialized first");
  return i::Sandbox::current()->address_space();
}

size_t v8::V8::GetSandboxSizeInBytes() {
  Utils::ApiCheck(i::Sandbox::current()->is_initialized(),
                  "v8::V8::GetSandboxSizeInBytes",
                  "The sandbox must be initialized first.");
  return i::Sandbox::current()->size();
}

size_t v8::V8::GetSandboxReservationSizeInBytes() {
  Utils::ApiCheck(i::Sandbox::current()->is_initialized(),
                  "v8::V8::GetSandboxReservationSizeInBytes",
                  "The sandbox must be initialized first");
  return i::Sandbox::current()->reservation_size();
}

bool v8::V8::IsSandboxConfiguredSecurely() {
  Utils::ApiCheck(i::Sandbox::current()->is_initialized(),
                  "v8::V8::IsSandoxConfiguredSecurely",
                  "The sandbox must be initialized first");
  // The sandbox is (only) configured insecurely if it is a partially reserved
  // sandbox, since in that case unrelated memory mappings may end up inside
  // the sandbox address space where they could be corrupted by an attacker.
  return !i::Sandbox::current()->is_partially_reserved();
}
#endif  // V8_ENABLE_SANDBOX

void V8::GetSharedMemoryStatistics(SharedMemoryStatistics* statistics) {
  i::ReadOnlyHeap::PopulateReadOnlySpaceStatistics(statistics);
}

template <typename ObjectType>
struct InvokeBootstrapper;

template <>
struct InvokeBootstrapper<i::NativeContext> {
  i::DirectHandle<i::NativeContext> Invoke(
      i::Isolate* i_isolate,
      i::MaybeDirectHandle<i::JSGlobalProxy> maybe_global_proxy,
      v8::Local<v8::ObjectTemplate> global_proxy_template,
      v8::ExtensionConfiguration* extensions, size_t context_snapshot_index,
      i::DeserializeEmbedderFieldsCallback embedder_fields_deserializer,
      v8::MicrotaskQueue* microtask_queue) {
    return i_isolate->bootstrapper()->CreateEnvironment(
        maybe_global_proxy, global_proxy_template, extensions,
        context_snapshot_index, embedder_fields_deserializer, microtask_queue);
  }
};

template <>
struct InvokeBootstrapper<i::JSGlobalProxy> {
  i::DirectHandle<i::JSGlobalProxy> Invoke(
      i::Isolate* i_isolate,
      i::MaybeDirectHandle<i::JSGlobalProxy> maybe_global_proxy,
      v8::Local<v8::ObjectTemplate> global_proxy_template,
      v8::ExtensionConfiguration* extensions, size_t context_snapshot_index,
      i::DeserializeEmbedderFieldsCallback embedder_fields_deserializer,
      v8::MicrotaskQueue* microtask_queue) {
    USE(extensions);
    USE(context_snapshot_index);
    return i_isolate->bootstrapper()->NewRemoteContext(maybe_global_proxy,
                                                       global_proxy_template);
  }
};

template <typename ObjectType>
static i::DirectHandle<ObjectType> CreateEnvironment(
    i::Isolate* i_isolate, v8::ExtensionConfiguration* extensions,
    v8::MaybeLocal<ObjectTemplate> maybe_global_template,
    v8::MaybeLocal<Value> maybe_global_proxy, size_t context_snapshot_index,
    i::DeserializeEmbedderFieldsCallback embedder_fields_deserializer,
    v8::MicrotaskQueue* microtask_queue) {
  i::DirectHandle<ObjectType> result;

  {
    // Do some part of the EnterV8Scope, but without a context yet.
    DCHECK_IMPLIES(i::v8_flags.strict_termination_checks,
                   !i_isolate->is_execution_terminating());
    i::VMState<v8::OTHER> vm_state(i_isolate);
    i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);

    v8::Local<ObjectTemplate> proxy_template;
    i::DirectHandle<i::FunctionTemplateInfo> proxy_constructor;
    i::DirectHandle<i::FunctionTemplateInfo> global_constructor;
    i::DirectHandle<i::UnionOf<i::Undefined, i::InterceptorInfo>>
        named_interceptor(i_isolate->factory()->undefined_value());
    i::DirectHandle<i::UnionOf<i::Undefined, i::InterceptorInfo>>
        indexed_interceptor(i_isolate->factory()->undefined_value());

    if (!maybe_global_template.IsEmpty()) {
      v8::Local<v8::ObjectTemplate> global_template =
          maybe_global_template.ToLocalChecked();
      // Make sure that the global_template has a constructor.
      global_constructor = EnsureConstructor(i_isolate, *global_template);

      // Create a fresh template for the global proxy object.
      proxy_template =
          ObjectTemplate::New(reinterpret_cast<v8::Isolate*>(i_isolate));
      proxy_constructor = EnsureConstructor(i_isolate, *proxy_template);

      // Set the global template to be the prototype template of
      // global proxy template.
      i::FunctionTemplateInfo::SetPrototypeTemplate(
          i_isolate, proxy_constructor,
          Utils::OpenDirectHandle(*global_template));

      proxy_template->SetInternalFieldCount(
          global_template->InternalFieldCount());

      // Migrate security handlers from global_template to
      // proxy_template.  Temporarily removing access check
      // information from the global template.
      if (!IsUndefined(global_constructor->GetAccessCheckInfo(), i_isolate)) {
        i::FunctionTemplateInfo::SetAccessCheckInfo(
            i_isolate, proxy_constructor,
            direct_handle(global_constructor->GetAccessCheckInfo(), i_isolate));
        proxy_constructor->set_needs_access_check(
            global_constructor->needs_access_check());
        global_constructor->set_needs_access_check(false);
        i::FunctionTemplateInfo::SetAccessCheckInfo(
            i_isolate, global_constructor,
            i_isolate->factory()->undefined_value());
      }

      // Same for other interceptors. If the global constructor has
      // interceptors, we need to replace them temporarily with noop
      // interceptors, so the map is correctly marked as having interceptors,
      // but we don't invoke any.
      if (!IsUndefined(global_constructor->GetNamedPropertyHandler(),
                       i_isolate)) {
        named_interceptor = direct_handle(
            global_constructor->GetNamedPropertyHandler(), i_isolate);
        i::FunctionTemplateInfo::SetNamedPropertyHandler(
            i_isolate, global_constructor,
            i_isolate->factory()->noop_interceptor_info());
      }
      if (!IsUndefined(global_constructor->GetIndexedPropertyHandler(),
                       i_isolate)) {
        indexed_interceptor = direct_handle(
            global_constructor->GetIndexedPropertyHandler(), i_isolate);
        i::FunctionTemplateInfo::SetIndexedPropertyHandler(
            i_isolate, global_constructor,
            i_isolate->factory()->noop_interceptor_info());
      }
    }

    i::MaybeDirectHandle<i::JSGlobalProxy> maybe_proxy;
    if (!maybe_global_proxy.IsEmpty()) {
      maybe_proxy = i::Cast<i::JSGlobalProxy>(
          Utils::OpenDirectHandle(*maybe_global_proxy.ToLocalChecked()));
    }
    // Create the environment.
    InvokeBootstrapper<ObjectType> invoke;
    result = invoke.Invoke(i_isolate, maybe_proxy, proxy_template, extensions,
                           context_snapshot_index, embedder_fields_deserializer,
                           microtask_queue);

    // Restore the access check info and interceptors on the global template.
    if (!maybe_global_template.IsEmpty()) {
      DCHECK(!global_constructor.is_null());
      DCHECK(!proxy_constructor.is_null());
      i::FunctionTemplateInfo::SetAccessCheckInfo(
          i_isolate, global_constructor,
          i::direct_handle(proxy_constructor->GetAccessCheckInfo(), i_isolate));
      global_constructor->set_needs_access_check(
          proxy_constructor->needs_access_check());
      i::FunctionTemplateInfo::SetNamedPropertyHandler(
          i_isolate, global_constructor, named_interceptor);
      i::FunctionTemplateInfo::SetIndexedPropertyHandler(
          i_isolate, global_constructor, indexed_interceptor);
    }
  }
  // Leave V8.

  return result;
}

Local<Context> NewContext(
    v8::Isolate* external_isolate, v8::ExtensionConfiguration* extensions,
    v8::MaybeLocal<ObjectTemplate> global_template,
    v8::MaybeLocal<Value> global_object, size_t context_snapshot_index,
    i::DeserializeEmbedderFieldsCallback embedder_fields_deserializer,
    v8::MicrotaskQueue* microtask_queue) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(external_isolate);
  // TODO(jkummerow): This is for crbug.com/713699. Remove it if it doesn't
  // fail.
  // Sanity-check that the isolate is initialized and usable.
  CHECK(IsCode(i_isolate->builtins()->code(i::Builtin::kIllegal)));

  TRACE_EVENT_CALL_STATS_SCOPED(i_isolate, "v8", "V8.NewContext");
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Context_New);
  i::HandleScope scope(i_isolate);
  ExtensionConfiguration no_extensions;
  if (extensions == nullptr) extensions = &no_extensions;
  i::DirectHandle<i::NativeContext> env = CreateEnvironment<i::NativeContext>(
      i_isolate, extensions, global_template, global_object,
      context_snapshot_index, embedder_fields_deserializer, microtask_queue);
  if (env.is_null()) return {};
  return Utils::ToLocal(scope.CloseAndEscape(env));
}

Local<Context> v8::Context::New(
    v8::Isolate* external_isolate, v8::ExtensionConfiguration* extensions,
    v8::MaybeLocal<ObjectTemplate> global_template,
    v8::MaybeLocal<Value> global_object,
    v8::DeserializeInternalFieldsCallback internal_fields_deserializer,
    v8::MicrotaskQueue* microtask_queue,
    v8::DeserializeContextDataCallback context_callback_deserializer,
    v8::DeserializeAPIWrapperCallback api_wrapper_deserializer) {
  return NewContext(
      external_isolate, extensions, global_template, global_object, 0,
      i::DeserializeEmbedderFieldsCallback(internal_fields_deserializer,
                                           context_callback_deserializer,
                                           api_wrapper_deserializer),
      microtask_queue);
}

MaybeLocal<Context> v8::Context::FromSnapshot(
    v8::Isolate* external_isolate, size_t context_snapshot_index,
    v8::DeserializeInternalFieldsCallback internal_fields_deserializer,
    v8::ExtensionConfiguration* extensions, MaybeLocal<Value> global_object,
    v8::MicrotaskQueue* microtask_queue,
    v8::DeserializeContextDataCallback context_callback_deserializer,
    v8::DeserializeAPIWrapperCallback api_wrapper_deserializer) {
  size_t index_including_default_context = context_snapshot_index + 1;
  if (!i::Snapshot::HasContextSnapshot(
          reinterpret_cast<i::Isolate*>(external_isolate),
          index_including_default_context)) {
    return {};
  }
  return NewContext(
      external_isolate, extensions, MaybeLocal<ObjectTemplate>(), global_object,
      index_including_default_context,
      i::DeserializeEmbedderFieldsCallback(internal_fields_deserializer,
                                           context_callback_deserializer,
                                           api_wrapper_deserializer),
      microtask_queue);
}

MaybeLocal<Object> v8::Context::NewRemoteContext(
    v8::Isolate* external_isolate, v8::Local<ObjectTemplate> global_template,
    v8::MaybeLocal<v8::Value> global_object) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(external_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_Context_NewRemoteContext);
  i::HandleScope scope(i_isolate);
  i::DirectHandle<i::FunctionTemplateInfo> global_constructor =
      EnsureConstructor(i_isolate, *global_template);
  Utils::ApiCheck(global_constructor->needs_access_check(),
                  "v8::Context::NewRemoteContext",
                  "Global template needs to have access checks enabled");
  i::DirectHandle<i::AccessCheckInfo> access_check_info(
      i::Cast<i::AccessCheckInfo>(global_constructor->GetAccessCheckInfo()),
      i_isolate);
  Utils::ApiCheck(
      access_check_info->named_interceptor() != i::Tagged<i::Object>(),
      "v8::Context::NewRemoteContext",
      "Global template needs to have access check handlers");
  i::DirectHandle<i::JSObject> global_proxy =
      CreateEnvironment<i::JSGlobalProxy>(
          i_isolate, nullptr, global_template, global_object, 0,
          i::DeserializeEmbedderFieldsCallback(), nullptr);
  if (global_proxy.is_null()) {
    if (i_isolate->has_exception()) i_isolate->clear_exception();
    return {};
  }
  return Utils::ToLocal(scope.CloseAndEscape(global_proxy));
}

void v8::Context::SetSecurityToken(Local<Value> token) {
  auto env = Utils::OpenDirectHandle(this);
  auto token_handle = Utils::OpenDirectHandle(*token);
  env->set_security_token(*token_handle);
}

void v8::Context::UseDefaultSecurityToken() {
  auto env = Utils::OpenDirectHandle(this);
  env->set_security_token(env->global_object());
}

Local<Value> v8::Context::GetSecurityToken() {
  auto env = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  i::Tagged<i::Object> security_token = env->security_token();
  return Utils::ToLocal(i::direct_handle(security_token, i_isolate));
}

namespace {

bool MayContainObjectsToFreeze(i::InstanceType obj_type) {
  if (i::InstanceTypeChecker::IsString(obj_type)) return false;
  // SharedFunctionInfo is cross-context so it shouldn't be frozen.
  if (i::InstanceTypeChecker::IsSharedFunctionInfo(obj_type)) return false;
  // All TemplateInfo objects are cross-context so they shouldn't be frozen.
  if (i::InstanceTypeChecker::IsTemplateInfo(obj_type)) return false;
  return true;
}

bool RequiresEmbedderSupportToFreeze(i::InstanceType obj_type) {
  DCHECK(i::InstanceTypeChecker::IsJSReceiver(obj_type));

  return (i::InstanceTypeChecker::IsJSApiObject(obj_type) ||
          i::InstanceTypeChecker::IsJSExternalObject(obj_type) ||
          i::InstanceTypeChecker::IsJSAPIObjectWithEmbedderSlots(obj_type) ||
          i::InstanceTypeChecker::IsCppHeapExternalObject(obj_type));
}

bool IsJSReceiverSafeToFreeze(i::InstanceType obj_type) {
  DCHECK(i::InstanceTypeChecker::IsJSReceiver(obj_type));

  switch (obj_type) {
    case i::JS_OBJECT_TYPE:
    case i::JS_GLOBAL_OBJECT_TYPE:
    case i::JS_GLOBAL_PROXY_TYPE:
    case i::JS_PRIMITIVE_WRAPPER_TYPE:
    case i::JS_FUNCTION_TYPE:
    /* Function types */
    case i::BIGINT64_TYPED_ARRAY_CONSTRUCTOR_TYPE:
    case i::BIGUINT64_TYPED_ARRAY_CONSTRUCTOR_TYPE:
    case i::FLOAT16_TYPED_ARRAY_CONSTRUCTOR_TYPE:
    case i::FLOAT32_TYPED_ARRAY_CONSTRUCTOR_TYPE:
    case i::FLOAT64_TYPED_ARRAY_CONSTRUCTOR_TYPE:
    case i::INT16_TYPED_ARRAY_CONSTRUCTOR_TYPE:
    case i::INT32_TYPED_ARRAY_CONSTRUCTOR_TYPE:
    case i::INT8_TYPED_ARRAY_CONSTRUCTOR_TYPE:
    case i::UINT16_TYPED_ARRAY_CONSTRUCTOR_TYPE:
    case i::UINT32_TYPED_ARRAY_CONSTRUCTOR_TYPE:
    case i::UINT8_CLAMPED_TYPED_ARRAY_CONSTRUCTOR_TYPE:
    case i::UINT8_TYPED_ARRAY_CONSTRUCTOR_TYPE:
    case i::JS_ARRAY_CONSTRUCTOR_TYPE:
    case i::JS_PROMISE_CONSTRUCTOR_TYPE:
    case i::JS_REG_EXP_CONSTRUCTOR_TYPE:
    case i::JS_CLASS_CONSTRUCTOR_TYPE:
    /* Prototype Types */
    case i::JS_ARRAY_ITERATOR_PROTOTYPE_TYPE:
    case i::JS_ITERATOR_PROTOTYPE_TYPE:
    case i::JS_MAP_ITERATOR_PROTOTYPE_TYPE:
    case i::JS_OBJECT_PROTOTYPE_TYPE:
    case i::JS_PROMISE_PROTOTYPE_TYPE:
    case i::JS_REG_EXP_PROTOTYPE_TYPE:
    case i::JS_SET_ITERATOR_PROTOTYPE_TYPE:
    case i::JS_SET_PROTOTYPE_TYPE:
    case i::JS_STRING_ITERATOR_PROTOTYPE_TYPE:
    case i::JS_TYPED_ARRAY_PROTOTYPE_TYPE:
    /* */
    case i::JS_ARRAY_TYPE:
      return true;
#if V8_ENABLE_WEBASSEMBLY
    case i::WASM_ARRAY_TYPE:
    case i::WASM_STRUCT_TYPE:
    case i::WASM_TAG_OBJECT_TYPE:
#endif  // V8_ENABLE_WEBASSEMBLY
    case i::JS_PROXY_TYPE:
      return true;
    // These types are known not to freeze.
    case i::JS_MAP_KEY_ITERATOR_TYPE:
    case i::JS_MAP_KEY_VALUE_ITERATOR_TYPE:
    case i::JS_MAP_VALUE_ITERATOR_TYPE:
    case i::JS_SET_KEY_VALUE_ITERATOR_TYPE:
    case i::JS_SET_VALUE_ITERATOR_TYPE:
    case i::JS_GENERATOR_OBJECT_TYPE:
    case i::JS_ASYNC_FUNCTION_OBJECT_TYPE:
    case i::JS_ASYNC_GENERATOR_OBJECT_TYPE:
    case i::JS_ARRAY_ITERATOR_TYPE: {
      return false;
    }
    default:
      // TODO(behamilton): Handle any types that fall through here.
      return false;
  }
}

class ObjectVisitorDeepFreezer : i::ObjectVisitor {
 public:
  explicit ObjectVisitorDeepFreezer(i::Isolate* isolate,
                                    Context::DeepFreezeDelegate* delegate)
      : isolate_(isolate),
        delegate_(delegate),
        objects_to_freeze_(isolate),
        lazy_accessor_pairs_to_freeze_(isolate) {}

  bool DeepFreeze(i::DirectHandle<i::Context> context) {
    bool success = VisitObject(i::Cast<i::HeapObject>(*context));
    if (success) {
      success = InstantiateAndVisitLazyAccessorPairs();
    }
    DCHECK_EQ(success, !error_.has_value());
    if (!success) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate_, NewTypeError(error_->msg_id, error_->name), false);
    }
    for (const auto& obj : objects_to_freeze_) {
      RETURN_ON_EXCEPTION_VALUE(isolate_,
                                i::JSReceiver::SetIntegrityLevel(
                                    isolate_, obj, i::FROZEN, i::kThrowOnError),
                                false);
    }
    return true;
  }

  void VisitPointers(i::Tagged<i::HeapObject> host, i::ObjectSlot start,
                     i::ObjectSlot end) final {
    VisitPointersImpl(start, end);
  }
  void VisitPointers(i::Tagged<i::HeapObject> host, i::MaybeObjectSlot start,
                     i::MaybeObjectSlot end) final {
    VisitPointersImpl(start, end);
  }
  void VisitMapPointer(i::Tagged<i::HeapObject> host) final {
    VisitPointer(host, host->map_slot());
  }
  void VisitInstructionStreamPointer(i::Tagged<i::Code> host,
                                     i::InstructionStreamSlot slot) final {}
  void VisitCustomWeakPointers(i::Tagged<i::HeapObject> host,
                               i::ObjectSlot start, i::ObjectSlot end) final {}

 private:
  struct ErrorInfo {
    i::MessageTemplate msg_id;
    i::DirectHandle<i::String> name;
  };

  template <typename TSlot>
  void VisitPointersImpl(TSlot start, TSlot end) {
    for (TSlot current = start; current < end; ++current) {
      typename TSlot::TObject object = current.load(isolate_);
      i::Tagged<i::HeapObject> heap_object;
      if (object.GetHeapObjectIfStrong(&heap_object)) {
        if (!VisitObject(heap_object)) {
          return;
        }
      }
    }
  }

  bool FreezeEmbedderObjectAndVisitChildren(i::DirectHandle<i::JSObject> obj) {
    DCHECK(delegate_);
    LocalVector<Object> children(reinterpret_cast<Isolate*>(isolate_));
    if (!delegate_->FreezeEmbedderObjectAndGetChildren(Utils::ToLocal(obj),
                                                       children)) {
      return false;
    }
    for (auto child : children) {
      if (!VisitObject(
              *Utils::OpenDirectHandle<Object, i::JSReceiver>(child))) {
        return false;
      }
    }
    return true;
  }

  bool VisitObject(i::Tagged<i::HeapObject> obj) {
    DCHECK(!obj.is_null());
    if (error_.has_value()) {
      return false;
    }

    i::DisallowGarbageCollection no_gc;
    i::InstanceType obj_type = obj->map()->instance_type();

    // Skip common types that can't contain items to freeze.
    if (!MayContainObjectsToFreeze(obj_type)) {
      return true;
    }

    if (!done_list_.insert(obj).second) {
      // If we couldn't insert (because it is already in the set) then we're
      // done.
      return true;
    }

    if (i::InstanceTypeChecker::IsAccessorPair(obj_type)) {
      // For AccessorPairs we need to ensure that the functions they point to
      // have been instantiated into actual JavaScript objects that can be
      // frozen. If they haven't then we need to save them to instantiate
      // (and recurse) before freezing.
      i::Tagged<i::AccessorPair> accessor_pair = i::Cast<i::AccessorPair>(obj);
      if (i::IsFunctionTemplateInfo(accessor_pair->getter()) ||
          IsFunctionTemplateInfo(accessor_pair->setter())) {
        i::DirectHandle<i::AccessorPair> lazy_accessor_pair(accessor_pair,
                                                            isolate_);
        lazy_accessor_pairs_to_freeze_.push_back(lazy_accessor_pair);
      }
    } else if (i::InstanceTypeChecker::IsContext(obj_type)) {
      // For contexts we need to ensure that all accessible locals are const.
      // If not they could be replaced to bypass freezing.
      i::Tagged<i::ScopeInfo> scope_info =
          i::Cast<i::Context>(obj)->scope_info();
      for (auto it : i::ScopeInfo::IterateLocalNames(scope_info, no_gc)) {
        if (!IsImmutableLexicalVariableMode(
                scope_info->ContextLocalMode(it->index()))) {
          DCHECK(!error_.has_value());
          error_ = ErrorInfo{i::MessageTemplate::kCannotDeepFreezeValue,
                             i::direct_handle(it->name(), isolate_)};
          return false;
        }
      }
    } else if (i::InstanceTypeChecker::IsJSReceiver(obj_type)) {
      i::DirectHandle<i::JSReceiver> receiver(i::Cast<i::JSReceiver>(obj),
                                              isolate_);
      if (RequiresEmbedderSupportToFreeze(obj_type)) {
        auto js_obj = i::Cast<i::JSObject>(receiver);

        // External objects don't have slots but still need to be processed by
        // the embedder.
        if (i::InstanceTypeChecker::IsJSExternalObject(obj_type) ||
            js_obj->GetEmbedderFieldCount() > 0) {
          if (!delegate_) {
            DCHECK(!error_.has_value());
            error_ =
                ErrorInfo{i::MessageTemplate::kCannotDeepFreezeObject,
                          i::direct_handle(receiver->class_name(), isolate_)};
            return false;
          }

          // Handle embedder specific types and any v8 children it wants to
          // freeze.
          if (!FreezeEmbedderObjectAndVisitChildren(js_obj)) {
            return false;
          }
        } else {
          DCHECK_EQ(js_obj->GetEmbedderFieldCount(), 0);
        }
      } else {
        DCHECK_IMPLIES(
            i::InstanceTypeChecker::IsJSObject(obj_type),
            i::Cast<i::JSObject>(*receiver)->GetEmbedderFieldCount() == 0);
        if (!IsJSReceiverSafeToFreeze(obj_type)) {
          DCHECK(!error_.has_value());
          error_ =
              ErrorInfo{i::MessageTemplate::kCannotDeepFreezeObject,
                        i::direct_handle(receiver->class_name(), isolate_)};
          return false;
        }
      }

      // Save this to freeze after we are done. Freezing triggers garbage
      // collection which doesn't work well with this visitor pattern, so we
      // delay it until after.
      objects_to_freeze_.push_back(receiver);

    } else {
      DCHECK(!i::InstanceTypeChecker::IsAccessorPair(obj_type));
      DCHECK(!i::InstanceTypeChecker::IsContext(obj_type));
      DCHECK(!i::InstanceTypeChecker::IsJSReceiver(obj_type));
    }

    DCHECK(!error_.has_value());
    i::VisitObject(isolate_, obj, this);
    // Iterate sets error_ on failure. We should propagate errors.
    return !error_.has_value();
  }

  bool InstantiateAndVisitLazyAccessorPairs() {
    i::DirectHandle<i::NativeContext> native_context =
        isolate_->native_context();

    i::DirectHandleVector<i::AccessorPair> lazy_accessor_pairs_to_freeze(
        isolate_);
    std::swap(lazy_accessor_pairs_to_freeze, lazy_accessor_pairs_to_freeze_);

    for (const auto& accessor_pair : lazy_accessor_pairs_to_freeze) {
      i::AccessorPair::GetComponent(isolate_, native_context, accessor_pair,
                                    i::ACCESSOR_GETTER);
      i::AccessorPair::GetComponent(isolate_, native_context, accessor_pair,
                                    i::ACCESSOR_SETTER);
      VisitObject(*accessor_pair);
    }
    // Ensure no new lazy accessor pairs were discovered.
    CHECK_EQ(lazy_accessor_pairs_to_freeze_.size(), 0);
    return true;
  }

  i::Isolate* isolate_;
  Context::DeepFreezeDelegate* delegate_;
  std::unordered_set<i::Tagged<i::Object>, i::Object::Hasher> done_list_;
  i::DirectHandleVector<i::JSReceiver> objects_to_freeze_;
  i::DirectHandleVector<i::AccessorPair> lazy_accessor_pairs_to_freeze_;
  std::optional<ErrorInfo> error_;
};

}  // namespace

Maybe<void> Context::DeepFreeze(DeepFreezeDelegate* delegate) {
  auto env = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();

  // TODO(behamilton): Incorporate compatibility improvements similar to NodeJS:
  // https://github.com/nodejs/node/blob/main/lib/internal/freeze_intrinsics.js
  // These need to be done before freezing.

  Local<Context> context = Utils::ToLocal(env);
  EnterV8NoScriptScope<> api_scope{i_isolate, context,
                                   RCCId::kAPI_Context_DeepFreeze};
  ObjectVisitorDeepFreezer vfreezer(i_isolate, delegate);
  if (!vfreezer.DeepFreeze(env)) return {};
  return JustVoid();
}

v8::Isolate* Context::GetIsolate() { return Isolate::GetCurrent(); }

v8::MicrotaskQueue* Context::GetMicrotaskQueue() {
  auto env = Utils::OpenDirectHandle(this);
  Utils::ApiCheck(i::IsNativeContext(*env), "v8::Context::GetMicrotaskQueue",
                  "Must be called on a native context");
  return env->microtask_queue();
}

void Context::SetMicrotaskQueue(v8::MicrotaskQueue* queue) {
  auto context = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  Utils::ApiCheck(i::IsNativeContext(*context),
                  "v8::Context::SetMicrotaskQueue",
                  "Must be called on a native context");
  i::HandleScopeImplementer* impl = i_isolate->handle_scope_implementer();
  Utils::ApiCheck(!context->microtask_queue()->IsRunningMicrotasks(),
                  "v8::Context::SetMicrotaskQueue",
                  "Must not be running microtasks");
  Utils::ApiCheck(context->microtask_queue()->GetMicrotasksScopeDepth() == 0,
                  "v8::Context::SetMicrotaskQueue",
                  "Must not have microtask scope pushed");
  Utils::ApiCheck(impl->EnteredContextCount() == 0,
                  "v8::Context::SetMicrotaskQueue()",
                  "Cannot set Microtask Queue with an entered context");
  context->set_microtask_queue(i_isolate,
                               static_cast<const i::MicrotaskQueue*>(queue));
}

v8::Local<v8::Object> Context::Global() {
  auto context = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  i::DirectHandle<i::JSObject> global(context->global_proxy(), i_isolate);
  return Utils::ToLocal(global);
}

void Context::DetachGlobal() {
  auto context = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i_isolate->DetachGlobal(context);
}

Local<v8::Object> Context::GetExtrasBindingObject() {
  auto context = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  return Utils::ToLocal(
      i::direct_handle(context->extras_binding_object(), i_isolate));
}

void Context::AllowCodeGenerationFromStrings(bool allow) {
  auto context = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  context->set_allow_code_gen_from_strings(
      i::ReadOnlyRoots(i_isolate).boolean_value(allow));
}

bool Context::IsCodeGenerationFromStringsAllowed() const {
  auto context = Utils::OpenDirectHandle(this);
  return !IsFalse(context->allow_code_gen_from_strings(),
                  i::Isolate::Current());
}

void Context::SetErrorMessageForCodeGenerationFromStrings(Local<String> error) {
  auto context = Utils::OpenDirectHandle(this);
  auto error_handle = Utils::OpenDirectHandle(*error);
  context->set_error_message_for_code_gen_from_strings(*error_handle);
}

void Context::SetErrorMessageForWasmCodeGeneration(Local<String> error) {
  auto context = Utils::OpenDirectHandle(this);
  auto error_handle = Utils::OpenDirectHandle(*error);
  context->set_error_message_for_wasm_code_gen(*error_handle);
}

void Context::SetAbortScriptExecution(
    Context::AbortScriptExecutionCallback callback) {
  auto context = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  if (callback == nullptr) {
    context->set_script_execution_callback(
        i::ReadOnlyRoots(i_isolate).undefined_value());
  } else {
    SET_FIELD_WRAPPED(i_isolate, context, set_script_execution_callback,
                      callback, internal::kApiAbortScriptExecutionCallbackTag);
  }
}

void v8::Context::SetPromiseHooks(Local<Function> init_hook,
                                  Local<Function> before_hook,
                                  Local<Function> after_hook,
                                  Local<Function> resolve_hook) {
#ifdef V8_ENABLE_JAVASCRIPT_PROMISE_HOOKS
  auto context = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();

  auto undefined = i_isolate->factory()->undefined_value();
  i::DirectHandle<i::Object> init = undefined;
  i::DirectHandle<i::Object> before = undefined;
  i::DirectHandle<i::Object> after = undefined;
  i::DirectHandle<i::Object> resolve = undefined;

  bool has_hook = false;

  if (!init_hook.IsEmpty()) {
    init = Utils::OpenDirectHandle(*init_hook);
    has_hook = true;
  }
  if (!before_hook.IsEmpty()) {
    before = Utils::OpenDirectHandle(*before_hook);
    has_hook = true;
  }
  if (!after_hook.IsEmpty()) {
    after = Utils::OpenDirectHandle(*after_hook);
    has_hook = true;
  }
  if (!resolve_hook.IsEmpty()) {
    resolve = Utils::OpenDirectHandle(*resolve_hook);
    has_hook = true;
  }

  i_isolate->SetHasContextPromiseHooks(has_hook);

  context->native_context()->set_promise_hook_init_function(*init);
  context->native_context()->set_promise_hook_before_function(*before);
  context->native_context()->set_promise_hook_after_function(*after);
  context->native_context()->set_promise_hook_resolve_function(*resolve);
#else   // V8_ENABLE_JAVASCRIPT_PROMISE_HOOKS
  Utils::ApiCheck(false, "v8::Context::SetPromiseHook",
                  "V8 was compiled without JavaScript Promise hooks");
#endif  // V8_ENABLE_JAVASCRIPT_PROMISE_HOOKS
}

bool Context::HasTemplateLiteralObject(Local<Value> object) {
  i::DisallowGarbageCollection no_gc;
  i::Tagged<i::Object> i_object = *Utils::OpenDirectHandle(*object);
  if (!IsJSArray(i_object)) return false;
  return Utils::OpenDirectHandle(this)
      ->native_context()
      ->HasTemplateLiteralObject(i::Cast<i::JSArray>(i_object));
}

MaybeLocal<Context> metrics::Recorder::GetContext(
    Isolate* v8_isolate, metrics::Recorder::ContextId id) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  return i_isolate->GetContextFromRecorderContextId(id);
}

metrics::Recorder::ContextId metrics::Recorder::GetContextId(
    Local<Context> context) {
  auto i_context = Utils::OpenDirectHandle(*context);
  i::Isolate* i_isolate = i::Isolate::Current();
  return i_isolate->GetOrRegisterRecorderContextId(
      direct_handle(i_context->native_context(), i_isolate));
}

metrics::LongTaskStats metrics::LongTaskStats::Get(v8::Isolate* v8_isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  return *i_isolate->GetCurrentLongTaskStats();
}

namespace {
i::ValueHelper::InternalRepresentationType GetSerializedDataFromFixedArray(
    i::Isolate* i_isolate, i::Tagged<i::FixedArray> list, size_t index) {
  if (index < static_cast<size_t>(list->length())) {
    int int_index = static_cast<int>(index);
    i::Tagged<i::Object> object = list->get(int_index);
    if (!IsTheHole(object, i_isolate)) {
      list->set_the_hole(i_isolate, int_index);
      // Shrink the list so that the last element is not the hole (unless it's
      // the first element, because we don't want to end up with a non-canonical
      // empty FixedArray).
      int last = list->length() - 1;
      while (last >= 0 && list->is_the_hole(i_isolate, last)) last--;
      if (last != -1) list->RightTrim(i_isolate, last + 1);
      return i::Handle<i::Object>(object, i_isolate).repr();
    }
  }
  return i::ValueHelper::kEmpty;
}
}  // anonymous namespace

i::ValueHelper::InternalRepresentationType Context::GetDataFromSnapshotOnce(
    size_t index) {
  auto context = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  auto list = i::Cast<i::FixedArray>(context->serialized_objects());
  return GetSerializedDataFromFixedArray(i_isolate, list, index);
}

MaybeLocal<v8::Object> ObjectTemplate::NewInstance(Local<Context> context) {
  PrepareForExecutionScope api_scope{context,
                                     RCCId::kAPI_ObjectTemplate_NewInstance};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto self = Utils::OpenDirectHandle(this);
  return api_scope.EscapeMaybe(
      i::ApiNatives::InstantiateObject(i_isolate, self));
}

void v8::ObjectTemplate::CheckCast(Data* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsObjectTemplateInfo(*obj), "v8::ObjectTemplate::Cast",
                  "Value is not an ObjectTemplate");
}

void v8::DictionaryTemplate::CheckCast(Data* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsDictionaryTemplateInfo(*obj),
                  "v8::DictionaryTemplate::Cast",
                  "Value is not an DictionaryTemplate");
}

void v8::FunctionTemplate::CheckCast(Data* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsFunctionTemplateInfo(*obj), "v8::FunctionTemplate::Cast",
                  "Value is not a FunctionTemplate");
}

void v8::Signature::CheckCast(Data* that) {
  auto obj = Utils::OpenDirectHandle(that);
  Utils::ApiCheck(i::IsFunctionTemplateInfo(*obj), "v8::Signature::Cast",
                  "Value is not a Signature");
}

MaybeLocal<v8::Function> FunctionTemplate::GetFunction(Local<Context> context) {
  PrepareForExecutionScope api_scope{context,
                                     RCCId::kAPI_FunctionTemplate_GetFunction};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto self = Utils::OpenDirectHandle(this);
  return api_scope.EscapeMaybe(i::ApiNatives::InstantiateFunction(
      i_isolate, i_isolate->native_context(), self));
}

MaybeLocal<v8::Object> FunctionTemplate::NewRemoteInstance() {
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(
      i_isolate, RCCId::kAPI_FunctionTemplate_NewRemoteInstance);
  i::HandleScope scope(i_isolate);
  i::DirectHandle<i::FunctionTemplateInfo> constructor =
      EnsureConstructor(i_isolate, *InstanceTemplate());
  Utils::ApiCheck(constructor->needs_access_check(),
                  "v8::FunctionTemplate::NewRemoteInstance",
                  "InstanceTemplate needs to have access checks enabled");
  i::DirectHandle<i::AccessCheckInfo> access_check_info(
      i::Cast<i::AccessCheckInfo>(constructor->GetAccessCheckInfo()),
      i_isolate);
  Utils::ApiCheck(
      access_check_info->named_interceptor() != i::Tagged<i::Object>(),
      "v8::FunctionTemplate::NewRemoteInstance",
      "InstanceTemplate needs to have access check handlers");
  i::DirectHandle<i::JSObject> object;
  if (!i::ApiNatives::InstantiateRemoteObject(
           Utils::OpenDirectHandle(*InstanceTemplate()))
           .ToHandle(&object)) {
    return {};
  }
  return Utils::ToLocal(scope.CloseAndEscape(object));
}

bool FunctionTemplate::HasInstance(v8::Local<v8::Value> value) {
  auto self = Utils::OpenDirectHandle(this);
  auto obj = Utils::OpenDirectHandle(*value);
  if (i::IsJSObject(*obj) && self->IsTemplateFor(i::Cast<i::JSObject>(*obj))) {
    return true;
  }
  if (i::IsJSGlobalProxy(*obj)) {
    // If it's a global proxy, then test with the global object. Note that the
    // inner global object may not necessarily be a JSGlobalObject.
    auto jsobj = i::Cast<i::JSObject>(*obj);
    i::PrototypeIterator iter(i::Isolate::Current(), jsobj->map());
    // The global proxy should always have a prototype, as it is a bug to call
    // this on a detached JSGlobalProxy.
    DCHECK(!iter.IsAtEnd());
    return self->IsTemplateFor(iter.GetCurrent<i::JSObject>());
  }
  return false;
}

bool FunctionTemplate::IsLeafTemplateForApiObject(
    v8::Local<v8::Value> value) const {
  i::DisallowGarbageCollection no_gc;
  auto self = Utils::OpenDirectHandle(this);
  i::Tagged<i::Object> object = *Utils::OpenDirectHandle(*value);
  return self->IsLeafTemplateForApiObject(object);
}

void ObjectTemplate::SealAndPrepareForPromotionToReadOnly() {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  i::ObjectTemplateInfo::SealAndPrepareForPromotionToReadOnly(i_isolate, self);
}

void FunctionTemplate::SealAndPrepareForPromotionToReadOnly() {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  i::FunctionTemplateInfo::SealAndPrepareForPromotionToReadOnly(i_isolate,
                                                                self);
}

Local<External> v8::External::New(Isolate* v8_isolate, void* value) {
  static_assert(sizeof(value) == sizeof(i::Address));
  // Nullptr is not allowed here because serialization/deserialization of
  // nullptr external api references is not possible as nullptr is used as an
  // external_references table terminator, see v8::SnapshotCreator()
  // constructors.
  DCHECK_NOT_NULL(value);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_External_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::JSObject> external =
      i_isolate->factory()->NewExternal(value);
  return Utils::ExternalToLocal(external);
}

void* External::Value() const {
  i::IsolateForSandbox isolate = i::GetCurrentIsolateForSandbox();
  return i::Cast<i::JSExternalObject>(*Utils::OpenDirectHandle(this))
      ->value(isolate);
}

Local<CppHeapExternal> v8::CppHeapExternal::NewImpl(Isolate* v8_isolate,
                                                    void* value,
                                                    CppHeapPointerTag tag) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_CppHeapExternal_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::CppHeapExternalObject> external =
      i_isolate->factory()->NewCppHeapExternal();
  i::CppHeapObjectWrapper(*external).SetCppHeapWrappable(i_isolate, value, tag);
  return Utils::CppHeapExternalToLocal(external);
}

void* CppHeapExternal::ValueImpl(v8::Isolate* isolate,
                                 CppHeapPointerTagRange tag_range) const {
  DCHECK_LE(tag_range.lower_bound, tag_range.upper_bound);
  auto self = Utils::OpenDirectHandle(this);
  return i::CppHeapObjectWrapper(*self).GetCppHeapWrappable(
      reinterpret_cast<i::Isolate*>(isolate), tag_range);
}

// anonymous namespace for string creation helper functions
namespace {

inline int StringLength(const char* string) {
  size_t len = strlen(string);
  CHECK_GE(i::kMaxInt, len);
  return static_cast<int>(len);
}

inline int StringLength(const uint8_t* string) {
  return StringLength(reinterpret_cast<const char*>(string));
}

inline int StringLength(const uint16_t* string) {
  size_t length = 0;
  while (string[length] != '\0') length++;
  CHECK_GE(i::kMaxInt, length);
  return static_cast<int>(length);
}

V8_WARN_UNUSED_RESULT
inline i::MaybeHandle<i::String> NewString(i::Factory* factory,
                                           NewStringType type,
                                           base::Vector<const char> string) {
  if (type == NewStringType::kInternalized) {
    return factory->InternalizeUtf8String(string);
  }
  return factory->NewStringFromUtf8(string);
}

V8_WARN_UNUSED_RESULT
inline i::MaybeHandle<i::String> NewString(i::Factory* factory,
                                           NewStringType type,
                                           base::Vector<const uint8_t> string) {
  if (type == NewStringType::kInternalized) {
    return factory->InternalizeString(string);
  }
  return factory->NewStringFromOneByte(string);
}

V8_WARN_UNUSED_RESULT
inline i::MaybeHandle<i::String> NewString(
    i::Factory* factory, NewStringType type,
    base::Vector<const uint16_t> string) {
  if (type == NewStringType::kInternalized) {
    return factory->InternalizeString(string);
  }
  return factory->NewStringFromTwoByte(string);
}

static_assert(v8::String::kMaxLength == i::String::kMaxLength);

}  // anonymous namespace

// TODO(dcarney): throw a context free exception.
#define NEW_STRING(v8_isolate, class_name, function_name, Char, data, type,   \
                   length)                                                    \
  MaybeLocal<String> result;                                                  \
  if (length == 0) {                                                          \
    result = String::Empty(v8_isolate);                                       \
  } else if (length > 0 &&                                                    \
             static_cast<uint32_t>(length) > i::String::kMaxLength) {         \
    result = MaybeLocal<String>();                                            \
  } else {                                                                    \
    i::Isolate* i_isolate = reinterpret_cast<internal::Isolate*>(v8_isolate); \
    EnterV8NoScriptNoExceptionScope api_scope(i_isolate);                     \
    ApiRuntimeCallStatsScope rcs_scope(                                       \
        i_isolate, RCCId::kAPI_##class_name##_##function_name);               \
    if (length < 0) length = StringLength(data);                              \
    i::DirectHandle<i::String> handle_result =                                \
        NewString(i_isolate->factory(), type,                                 \
                  base::Vector<const Char>(data, length))                     \
            .ToHandleChecked();                                               \
    result = Utils::ToLocal(handle_result);                                   \
  }

Local<String> String::NewFromUtf8Literal(Isolate* v8_isolate,
                                         const char* literal,
                                         NewStringType type, int length) {
  DCHECK_LE(length, i::String::kMaxLength);
  i::Isolate* i_isolate = reinterpret_cast<internal::Isolate*>(v8_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_String_NewFromUtf8Literal);
  i::DirectHandle<i::String> handle_result =
      NewString(i_isolate->factory(), type,
                base::Vector<const char>(literal, length))
          .ToHandleChecked();
  return Utils::ToLocal(handle_result);
}

MaybeLocal<String> String::NewFromUtf8(Isolate* v8_isolate, const char* data,
                                       NewStringType type, int length) {
  NEW_STRING(v8_isolate, String, NewFromUtf8, char, data, type, length);
  return result;
}

MaybeLocal<String> String::NewFromOneByte(Isolate* v8_isolate,
                                          const uint8_t* data,
                                          NewStringType type, int length) {
  NEW_STRING(v8_isolate, String, NewFromOneByte, uint8_t, data, type, length);
  return result;
}

MaybeLocal<String> String::NewFromTwoByte(Isolate* v8_isolate,
                                          const uint16_t* data,
                                          NewStringType type, int length) {
  NEW_STRING(v8_isolate, String, NewFromTwoByte, uint16_t, data, type, length);
  return result;
}

Local<String> v8::String::Concat(Isolate* v8_isolate, Local<String> left,
                                 Local<String> right) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  auto left_string = Utils::OpenHandle(*left);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_String_Concat);
  auto right_string = Utils::OpenHandle(*right);
  // If we are steering towards a range error, do not wait for the error to be
  // thrown, and return the null handle instead.
  if (left_string->length() + right_string->length() > i::String::kMaxLength) {
    return {};
  }
  i::DirectHandle<i::String> result =
      i_isolate->factory()
          ->NewConsString(left_string, right_string)
          .ToHandleChecked();
  return Utils::ToLocal(result);
}

MaybeLocal<String> v8::String::NewExternalTwoByte(
    Isolate* v8_isolate, v8::String::ExternalStringResource* resource) {
  CHECK(resource && resource->data());
  // TODO(dcarney): throw a context free exception.
  if (resource->length() > static_cast<size_t>(i::String::kMaxLength)) {
    return {};
  }
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_String_NewExternalTwoByte);
  if (resource->length() > 0) {
    i::DirectHandle<i::String> string =
        i_isolate->factory()
            ->NewExternalStringFromTwoByte(resource)
            .ToHandleChecked();
    return Utils::ToLocal(string);
  } else {
    // The resource isn't going to be used, free it immediately.
    resource->Unaccount(v8_isolate);
    resource->Dispose();
    return Utils::ToLocal(i_isolate->factory()->empty_string());
  }
}

MaybeLocal<String> v8::String::NewExternalOneByte(
    Isolate* v8_isolate, v8::String::ExternalOneByteStringResource* resource) {
  CHECK_NOT_NULL(resource);
  // TODO(dcarney): throw a context free exception.
  if (resource->length() > static_cast<size_t>(i::String::kMaxLength)) {
    return {};
  }
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_String_NewExternalOneByte);
  if (resource->length() == 0) {
    // The resource isn't going to be used, free it immediately.
    resource->Unaccount(v8_isolate);
    resource->Dispose();
    return Utils::ToLocal(i_isolate->factory()->empty_string());
  }
  CHECK_NOT_NULL(resource->data());
  i::DirectHandle<i::String> string =
      i_isolate->factory()
          ->NewExternalStringFromOneByte(resource)
          .ToHandleChecked();
  return Utils::ToLocal(string);
}

bool v8::String::MakeExternal(v8::String::ExternalStringResource* resource) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  return MakeExternal(isolate, resource);
}

bool v8::String::MakeExternal(Isolate* isolate,
                              v8::String::ExternalStringResource* resource) {
  i::DisallowGarbageCollection no_gc;

  i::Tagged<i::String> obj = *Utils::OpenDirectHandle(this);

  if (i::IsThinString(obj)) {
    obj = i::Cast<i::ThinString>(obj)->actual();
  }

  if (!obj->SupportsExternalization(Encoding::TWO_BYTE_ENCODING)) {
    return false;
  }

  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);

  CHECK(resource && resource->data());

  bool result = obj->MakeExternal(i_isolate, resource);
  DCHECK_IMPLIES(result, HasExternalStringResource(obj));
  return result;
}

bool v8::String::MakeExternal(
    v8::String::ExternalOneByteStringResource* resource) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  return MakeExternal(isolate, resource);
}

bool v8::String::MakeExternal(
    Isolate* isolate, v8::String::ExternalOneByteStringResource* resource) {
  i::DisallowGarbageCollection no_gc;

  i::Tagged<i::String> obj = *Utils::OpenDirectHandle(this);

  if (i::IsThinString(obj)) {
    obj = i::Cast<i::ThinString>(obj)->actual();
  }

  if (!obj->SupportsExternalization(Encoding::ONE_BYTE_ENCODING)) {
    return false;
  }

  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);

  CHECK(resource && resource->data());

  bool result = obj->MakeExternal(i_isolate, resource);
  DCHECK_IMPLIES(result, HasExternalStringResource(obj));
  return result;
}

bool v8::String::CanMakeExternal(Encoding encoding) const {
  i::Tagged<i::String> obj = *Utils::OpenDirectHandle(this);

  return obj->SupportsExternalization(encoding);
}

bool v8::String::StringEquals(Local<String> that) const {
  auto self = Utils::OpenDirectHandle(this);
  auto other = Utils::OpenDirectHandle(*that);
  return self->Equals(*other);
}

Isolate* v8::Object::GetIsolate() {
  i::Isolate* i_isolate = i::Isolate::Current();
  return reinterpret_cast<Isolate*>(i_isolate);
}

Local<v8::Object> v8::Object::New(Isolate* v8_isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Object_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::JSObject> obj =
      i_isolate->factory()->NewJSObject(i_isolate->object_function());
  return Utils::ToLocal(obj);
}

namespace {

// TODO(v8:7569): This is a workaround for the Handle vs MaybeHandle difference
// in the return types of the different Add functions:
// OrderedNameDictionary::Add returns MaybeHandle, NameDictionary::Add returns
// Handle.
template <typename T>
i::DirectHandle<T> ToHandle(i::Handle<T> h) {
  return h;
}
template <typename T>
i::DirectHandle<T> ToHandle(i::MaybeHandle<T> h) {
  return h.ToHandleChecked();
}

template <typename T>
i::DirectHandle<T> ToHandle(i::DirectHandle<T> h) {
  return h;
}
template <typename T>
i::DirectHandle<T> ToHandle(i::MaybeDirectHandle<T> h) {
  return h.ToHandleChecked();
}

template <typename Dictionary>
void AddPropertiesAndElementsToObject(
    i::Isolate* i_isolate, i::DirectHandle<Dictionary>& properties,
    i::DirectHandle<i::FixedArrayBase>& elements, Local<Name>* names,
    Local<Value>* values, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    auto name = Utils::OpenDirectHandle(*names[i]);
    auto value = Utils::OpenDirectHandle(*values[i]);

    // See if the {name} is a valid array index, in which case we need to
    // add the {name}/{value} pair to the {elements}, otherwise they end
    // up in the {properties} backing store.
    uint32_t index;
    if (name->AsArrayIndex(&index)) {
      // If this is the first element, allocate a proper
      // dictionary elements backing store for {elements}.
      if (!IsNumberDictionary(*elements)) {
        elements =
            i::NumberDictionary::New(i_isolate, static_cast<int>(length));
      }
      elements = i::NumberDictionary::Set(
          i_isolate, i::Cast<i::NumberDictionary>(elements), index, value);
    } else {
      // Internalize the {name} first.
      name = i_isolate->factory()->InternalizeName(name);
      i::InternalIndex const entry = properties->FindEntry(i_isolate, name);
      if (entry.is_not_found()) {
        // Add the {name}/{value} pair as a new entry.
        properties = ToHandle(Dictionary::Add(
            i_isolate, properties, name, value, i::PropertyDetails::Empty()));
      } else {
        // Overwrite the {entry} with the {value}.
        properties->ValueAtPut(entry, *value);
      }
    }
  }
}

}  // namespace

Local<v8::Object> v8::Object::New(Isolate* v8_isolate,
                                  Local<Value> prototype_or_null,
                                  Local<Name>* names, Local<Value>* values,
                                  size_t length) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::DirectHandle<i::JSPrototype> proto;
  if (!Utils::ApiCheck(
          i::TryCast(Utils::OpenDirectHandle(*prototype_or_null), &proto),
          "v8::Object::New", "prototype must be null or object")) {
    return {};
  }
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Object_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);

  i::DirectHandle<i::FixedArrayBase> elements =
      i_isolate->factory()->empty_fixed_array();

  // We assume that this API is mostly used to create objects with named
  // properties, and so we default to creating a properties backing store
  // large enough to hold all of them, while we start with no elements
  // (see http://bit.ly/v8-fast-object-create-cpp for the motivation).
  if (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
    i::DirectHandle<i::SwissNameDictionary> properties =
        i_isolate->factory()->NewSwissNameDictionary(static_cast<int>(length));
    AddPropertiesAndElementsToObject(i_isolate, properties, elements, names,
                                     values, length);
    i::DirectHandle<i::JSObject> obj =
        i_isolate->factory()->NewSlowJSObjectWithPropertiesAndElements(
            proto, properties, elements);
    return Utils::ToLocal(obj);
  } else {
    i::DirectHandle<i::NameDictionary> properties =
        i::NameDictionary::New(i_isolate, static_cast<int>(length));
    AddPropertiesAndElementsToObject(i_isolate, properties, elements, names,
                                     values, length);
    i::DirectHandle<i::JSObject> obj =
        i_isolate->factory()->NewSlowJSObjectWithPropertiesAndElements(
            proto, properties, elements);
    return Utils::ToLocal(obj);
  }
}

Local<v8::Value> v8::NumberObject::New(Isolate* v8_isolate, double value) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_NumberObject_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::Object> number = i_isolate->factory()->NewNumber(value);
  i::DirectHandle<i::Object> obj =
      i::Object::ToObject(i_isolate, number).ToHandleChecked();
  return Utils::ToLocal(obj);
}

double v8::NumberObject::ValueOf() const {
  auto obj = Utils::OpenDirectHandle(this);
  auto js_primitive_wrapper = i::Cast<i::JSPrimitiveWrapper>(obj);
  ApiRuntimeCallStatsScope rcs_scope(i::Isolate::Current(),
                                     RCCId::kAPI_NumberObject_NumberValue);
  return i::Object::NumberValue(
      i::Cast<i::Number>(js_primitive_wrapper->value()));
}

Local<v8::Value> v8::BigIntObject::New(Isolate* v8_isolate, int64_t value) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_BigIntObject_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::Object> bigint = i::BigInt::FromInt64(i_isolate, value);
  i::DirectHandle<i::Object> obj =
      i::Object::ToObject(i_isolate, bigint).ToHandleChecked();
  return Utils::ToLocal(obj);
}

Local<v8::BigInt> v8::BigIntObject::ValueOf() const {
  auto obj = Utils::OpenDirectHandle(this);
  auto js_primitive_wrapper = i::Cast<i::JSPrimitiveWrapper>(obj);
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_BigIntObject_BigIntValue);
  return Utils::ToLocal(i::direct_handle(
      i::Cast<i::BigInt>(js_primitive_wrapper->value()), i_isolate));
}

Local<v8::Value> v8::BooleanObject::New(Isolate* v8_isolate, bool value) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_BooleanObject_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::Object> boolean = i_isolate->factory()->ToBoolean(value);
  i::DirectHandle<i::Object> obj =
      i::Object::ToObject(i_isolate, boolean).ToHandleChecked();
  return Utils::ToLocal(obj);
}

bool v8::BooleanObject::ValueOf() const {
  i::Tagged<i::Object> obj = *Utils::OpenDirectHandle(this);
  i::Tagged<i::JSPrimitiveWrapper> js_primitive_wrapper =
      i::Cast<i::JSPrimitiveWrapper>(obj);
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_BooleanObject_BooleanValue);
  return i::IsTrue(js_primitive_wrapper->value(), i_isolate);
}

Local<v8::Value> v8::StringObject::New(Isolate* v8_isolate,
                                       Local<String> value) {
  auto string = Utils::OpenDirectHandle(*value);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_StringObject_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::Object> obj =
      i::Object::ToObject(i_isolate, string).ToHandleChecked();
  return Utils::ToLocal(obj);
}

Local<v8::String> v8::StringObject::ValueOf() const {
  auto obj = Utils::OpenDirectHandle(this);
  auto js_primitive_wrapper = i::Cast<i::JSPrimitiveWrapper>(obj);
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_StringObject_StringValue);
  return Utils::ToLocal(i::direct_handle(
      i::Cast<i::String>(js_primitive_wrapper->value()), i_isolate));
}

Local<v8::Value> v8::SymbolObject::New(Isolate* v8_isolate,
                                       Local<Symbol> value) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_SymbolObject_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::Object> obj =
      i::Object::ToObject(i_isolate, Utils::OpenDirectHandle(*value))
          .ToHandleChecked();
  return Utils::ToLocal(obj);
}

Local<v8::Symbol> v8::SymbolObject::ValueOf() const {
  auto obj = Utils::OpenDirectHandle(this);
  auto js_primitive_wrapper = i::Cast<i::JSPrimitiveWrapper>(obj);
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_SymbolObject_SymbolValue);
  return Utils::ToLocal(i::direct_handle(
      i::Cast<i::Symbol>(js_primitive_wrapper->value()), i_isolate));
}

MaybeLocal<v8::Value> v8::Date::New(Local<Context> context, double time) {
  if (std::isnan(time)) {
    // Introduce only canonical NaN value into the VM, to avoid signaling NaNs.
    time = std::numeric_limits<double>::quiet_NaN();
  }
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Date_New};
  i::Isolate* i_isolate = api_scope.i_isolate();
  return api_scope.EscapeMaybe(
      Utils::ToMaybeLocal(i::JSDate::New(i_isolate, i_isolate->date_function(),
                                         i_isolate->date_function(), time)));
}

MaybeLocal<Value> v8::Date::Parse(Local<Context> context, Local<String> value) {
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Date_Parse};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto string = Utils::OpenDirectHandle(*value);
  double time = ParseDateTimeString(i_isolate, string);

  return api_scope.EscapeMaybe(
      Utils::ToMaybeLocal(i::JSDate::New(i_isolate, i_isolate->date_function(),
                                         i_isolate->date_function(), time)));
}

double v8::Date::ValueOf() const {
  auto obj = Utils::OpenDirectHandle(this);
  auto jsdate = i::Cast<i::JSDate>(obj);
  return jsdate->value();
}

v8::Local<v8::String> v8::Date::ToISOString() const {
  auto obj = Utils::OpenDirectHandle(this);
  auto jsdate = i::Cast<i::JSDate>(obj);
  i::Isolate* i_isolate = i::Isolate::Current();
  i::DateBuffer buffer =
      i::ToDateString(jsdate->value(), i_isolate->date_cache(),
                      i::ToDateStringMode::kISODateAndTime);
  i::DirectHandle<i::String> str =
      i_isolate->factory()
          ->NewStringFromUtf8(base::VectorOf(buffer))
          .ToHandleChecked();
  return Utils::ToLocal(str);
}

v8::Local<v8::String> v8::Date::ToUTCString() const {
  auto obj = Utils::OpenDirectHandle(this);
  auto jsdate = i::Cast<i::JSDate>(obj);
  i::Isolate* i_isolate = i::Isolate::Current();
  i::DateBuffer buffer =
      i::ToDateString(jsdate->value(), i_isolate->date_cache(),
                      i::ToDateStringMode::kUTCDateAndTime);
  i::DirectHandle<i::String> str =
      i_isolate->factory()
          ->NewStringFromUtf8(base::VectorOf(buffer))
          .ToHandleChecked();
  return Utils::ToLocal(str);
}

// Assert that the static TimeZoneDetection cast in
// DateTimeConfigurationChangeNotification is valid.
#define TIME_ZONE_DETECTION_ASSERT_EQ(value)                     \
  static_assert(                                                 \
      static_cast<int>(v8::Isolate::TimeZoneDetection::value) == \
      static_cast<int>(base::TimezoneCache::TimeZoneDetection::value));
TIME_ZONE_DETECTION_ASSERT_EQ(kSkip)
TIME_ZONE_DETECTION_ASSERT_EQ(kRedetect)
#undef TIME_ZONE_DETECTION_ASSERT_EQ

MaybeLocal<v8::RegExp> v8::RegExp::New(Local<Context> context,
                                       Local<String> pattern, Flags flags) {
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_RegExp_New};
  i::Isolate* i_isolate = api_scope.i_isolate();
  return api_scope.EscapeMaybe(
      i::JSRegExp::New(i_isolate, Utils::OpenDirectHandle(*pattern),
                       static_cast<i::JSRegExp::Flags>(flags)));
}

MaybeLocal<v8::RegExp> v8::RegExp::NewWithBacktrackLimit(
    Local<Context> context, Local<String> pattern, Flags flags,
    uint32_t backtrack_limit) {
  Utils::ApiCheck(i::Smi::IsValid(backtrack_limit),
                  "v8::RegExp::NewWithBacktrackLimit",
                  "backtrack_limit is too large or too small");
  Utils::ApiCheck(backtrack_limit != i::JSRegExp::kNoBacktrackLimit,
                  "v8::RegExp::NewWithBacktrackLimit",
                  "Must set backtrack_limit");
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_RegExp_New};
  i::Isolate* i_isolate = api_scope.i_isolate();
  return api_scope.EscapeMaybe(i::JSRegExp::New(
      i_isolate, Utils::OpenDirectHandle(*pattern),
      static_cast<i::JSRegExp::Flags>(flags), backtrack_limit));
}

Local<v8::String> v8::RegExp::GetSource() const {
  auto obj = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  return Utils::ToLocal(i::direct_handle(obj->EscapedPattern(), i_isolate));
}

// Assert that the static flags cast in GetFlags is valid.
#define REGEXP_FLAG_ASSERT_EQ(flag)                   \
  static_assert(static_cast<int>(v8::RegExp::flag) == \
                static_cast<int>(i::JSRegExp::flag))
REGEXP_FLAG_ASSERT_EQ(kNone);
REGEXP_FLAG_ASSERT_EQ(kGlobal);
REGEXP_FLAG_ASSERT_EQ(kIgnoreCase);
REGEXP_FLAG_ASSERT_EQ(kMultiline);
REGEXP_FLAG_ASSERT_EQ(kSticky);
REGEXP_FLAG_ASSERT_EQ(kUnicode);
REGEXP_FLAG_ASSERT_EQ(kHasIndices);
REGEXP_FLAG_ASSERT_EQ(kLinear);
REGEXP_FLAG_ASSERT_EQ(kUnicodeSets);
#undef REGEXP_FLAG_ASSERT_EQ

v8::RegExp::Flags v8::RegExp::GetFlags() const {
  auto obj = Utils::OpenDirectHandle(this);
  return RegExp::Flags(static_cast<int>(obj->flags()));
}

MaybeLocal<v8::Object> v8::RegExp::Exec(Local<Context> context,
                                        Local<v8::String> subject) {
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_RegExp_Exec};

  i::Isolate* i_isolate = api_scope.i_isolate();
  auto regexp = Utils::OpenHandle(this);
  auto subject_string = Utils::OpenDirectHandle(*subject);

  // TODO(jgruber): RegExpUtils::RegExpExec was not written with efficiency in
  // mind. It fetches the 'exec' property and then calls it through JSEntry.
  // Unfortunately, this is currently the only full implementation of
  // RegExp.prototype.exec available in C++.
  i::DirectHandle<i::JSAny> result;
  if (!i::RegExpUtils::RegExpExec(i_isolate, regexp, subject_string,
                                  i_isolate->factory()->undefined_value())
           .ToHandle(&result)) {
    return {};
  }
  // RegExp::Exec is documented to return Null on failure, but the return type
  // is `MaybeLocal<Object>` which cannot legally hold `Null`, thus the unsafe
  // `Utils::Convert<Object>`.
  // TODO(jgruber): Fix this.
  return api_scope.Escape(Utils::Convert<i::JSAny, Object>(result));
}

Local<v8::Array> v8::Array::New(Isolate* v8_isolate, int length) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Array_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  int real_length = length > 0 ? length : 0;
  i::DirectHandle<i::JSArray> obj =
      i_isolate->factory()->NewJSArray(real_length);
  i::DirectHandle<i::Number> length_obj =
      i_isolate->factory()->NewNumberFromInt(real_length);
  obj->set_length(*length_obj);
  return Utils::ToLocal(obj);
}

Local<v8::Array> v8::Array::New(Isolate* v8_isolate, Local<Value>* elements,
                                size_t length) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::Factory* factory = i_isolate->factory();
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Array_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  int len = static_cast<int>(length);

  i::DirectHandle<i::FixedArray> result = factory->NewFixedArray(len);
  for (int i = 0; i < len; i++) {
    auto element = Utils::OpenDirectHandle(*elements[i]);
    result->set(i, *element);
  }

  return Utils::ToLocal(
      factory->NewJSArrayWithElements(result, i::PACKED_ELEMENTS, len));
}

// static
MaybeLocal<v8::Array> v8::Array::New(
    Local<Context> context, size_t length,
    std::function<MaybeLocal<v8::Value>()> next_value_callback) {
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Array_New};
  i::Isolate* i_isolate = api_scope.i_isolate();
  i::Factory* factory = i_isolate->factory();
  const int len = static_cast<int>(length);
  i::DirectHandle<i::FixedArray> backing = factory->NewFixedArray(len);
  v8::Local<v8::Value> value;
  for (int i = 0; i < len; i++) {
    MaybeLocal<v8::Value> maybe_value = next_value_callback();
    // The embedder may signal to abort creation on exception via an empty
    // local.
    if (!maybe_value.ToLocal(&value)) {
      CHECK(i_isolate->has_exception());
      return {};
    }
    backing->set(i, *Utils::OpenDirectHandle(*value));
  }
  return api_scope.Escape(Utils::ToLocal(
      factory->NewJSArrayWithElements(backing, i::PACKED_ELEMENTS, len)));
}

namespace internal {

uint32_t GetLength(Tagged<JSArray> array) {
  Tagged<Number> length = array->length();
  if (IsSmi(length)) return Smi::ToInt(length);
  return static_cast<uint32_t>(Object::NumberValue(length));
}

}  // namespace internal

uint32_t v8::Array::Length() const {
  return i::GetLength(*Utils::OpenDirectHandle(this));
}

namespace internal {

bool CanUseFastIteration(Isolate* isolate, DirectHandle<JSArray> array) {
  if (IsCustomElementsReceiverMap(array->map())) return false;
  if (array->GetElementsAccessor()->HasAccessors(*array)) return false;
  if (!JSObject::PrototypeHasNoElements(isolate, *array)) return false;
  return true;
}

enum class FastIterateResult {
  kException = static_cast<int>(v8::Array::CallbackResult::kException),
  kBreak = static_cast<int>(v8::Array::CallbackResult::kBreak),
  kSlowPath,
  kFinished,
};

FastIterateResult FastIterateArray(DirectHandle<JSArray> array,
                                   Isolate* isolate,
                                   v8::Array::IterationCallback callback,
                                   void* callback_data) {
  // Instead of relying on callers to check condition, this function returns
  // {kSlowPath} for situations it can't handle.
  // Most code paths below don't allocate, and rely on {callback} not allocating
  // either, but this isn't enforced with {DisallowHeapAllocation} to allow
  // embedders to allocate error objects before terminating the iteration.
  // Since {callback} must not allocate anyway, we can get away with fake
  // handles, reducing per-element overhead.
  if (!CanUseFastIteration(isolate, array)) return FastIterateResult::kSlowPath;
  using Result = v8::Array::CallbackResult;
  DisallowJavascriptExecution no_js(isolate);
  uint32_t length = GetLength(*array);
  if (length == 0) return FastIterateResult::kFinished;
  switch (array->GetElementsKind()) {
    case PACKED_SMI_ELEMENTS:
    case PACKED_ELEMENTS:
    case PACKED_FROZEN_ELEMENTS:
    case PACKED_SEALED_ELEMENTS:
    case PACKED_NONEXTENSIBLE_ELEMENTS: {
      Tagged<FixedArray> elements = Cast<FixedArray>(array->elements());
      for (uint32_t i = 0; i < length; i++) {
        Tagged<Object> element = elements->get(static_cast<int>(i));
        // TODO(13270): When we switch to CSS, we can pass {element} to
        // the callback directly, without {fake_handle}.
        IndirectHandle<Object> fake_handle(
            reinterpret_cast<Address*>(&element));
        Result result = callback(i, Utils::ToLocal(fake_handle), callback_data);
        if (result != Result::kContinue) {
          return static_cast<FastIterateResult>(result);
        }
        DCHECK(CanUseFastIteration(isolate, array));
      }
      return FastIterateResult::kFinished;
    }
    case HOLEY_SMI_ELEMENTS:
    case HOLEY_FROZEN_ELEMENTS:
    case HOLEY_SEALED_ELEMENTS:
    case HOLEY_NONEXTENSIBLE_ELEMENTS:
    case HOLEY_ELEMENTS: {
      Tagged<FixedArray> elements = Cast<FixedArray>(array->elements());
      for (uint32_t i = 0; i < length; i++) {
        Tagged<Object> element = elements->get(static_cast<int>(i));
        // TODO(13270): When we switch to CSS, we can pass {element} to
        // the callback directly, without {fake_handle}.
        auto fake_handle =
            IsTheHole(element)
                ? isolate->factory()->undefined_value()
                : IndirectHandle<Object>(reinterpret_cast<Address*>(&element));
        Result result = callback(i, Utils::ToLocal(fake_handle), callback_data);
        if (result != Result::kContinue) {
          return static_cast<FastIterateResult>(result);
        }
        DCHECK(CanUseFastIteration(isolate, array));
      }
      return FastIterateResult::kFinished;
    }
    case HOLEY_DOUBLE_ELEMENTS:
    case PACKED_DOUBLE_ELEMENTS: {
      DCHECK_NE(length, 0);  // Cast to FixedDoubleArray would be invalid.
      DirectHandle<FixedDoubleArray> elements(
          Cast<FixedDoubleArray>(array->elements()), isolate);
      FOR_WITH_HANDLE_SCOPE(isolate, uint32_t i = 0, i, i < length, i++) {
        DirectHandle<Object> value;
        if (elements->is_the_hole(i)) {
          value = Handle<Object>(isolate->factory()->undefined_value());
#ifdef V8_ENABLE_EXPERIMENTAL_UNDEFINED_DOUBLE
        } else if (elements->is_undefined(i)) {
          value = Handle<Object>(isolate->factory()->undefined_value());
#endif  // V8_ENABLE_EXPERIMENTAL_UNDEFINED_DOUBLE
        } else {
          value = isolate->factory()->NewNumber(elements->get_scalar(i));
        }
        Result result = callback(i, Utils::ToLocal(value), callback_data);
        if (result != Result::kContinue) {
          return static_cast<FastIterateResult>(result);
        }
        DCHECK(CanUseFastIteration(isolate, array));
      }
      return FastIterateResult::kFinished;
    }
    case DICTIONARY_ELEMENTS: {
      DisallowGarbageCollection no_gc;
      Tagged<NumberDictionary> dict = array->element_dictionary();
      struct Entry {
        uint32_t index;
        InternalIndex entry;
      };
      std::vector<Entry> sorted;
      sorted.reserve(dict->NumberOfElements());
      ReadOnlyRoots roots(isolate);
      for (InternalIndex i : dict->IterateEntries()) {
        Tagged<Object> key = dict->KeyAt(isolate, i);
        if (!dict->IsKey(roots, key)) continue;
        uint32_t index =
            static_cast<uint32_t>(Object::NumberValue(Cast<Number>(key)));
        sorted.push_back({index, i});
      }
      std::sort(
          sorted.begin(), sorted.end(),
          [](const Entry& a, const Entry& b) { return a.index < b.index; });
      for (const Entry& entry : sorted) {
        Tagged<Object> value = dict->ValueAt(entry.entry);
        // TODO(13270): When we switch to CSS, we can pass {element} to
        // the callback directly, without {fake_handle}.
        IndirectHandle<Object> fake_handle(reinterpret_cast<Address*>(&value));
        Result result =
            callback(entry.index, Utils::ToLocal(fake_handle), callback_data);
        if (result != Result::kContinue) {
          return static_cast<FastIterateResult>(result);
        }
        SLOW_DCHECK(CanUseFastIteration(isolate, array));
      }
      return FastIterateResult::kFinished;
    }
    case NO_ELEMENTS:
      return FastIterateResult::kFinished;
    case FAST_SLOPPY_ARGUMENTS_ELEMENTS:
    case SLOW_SLOPPY_ARGUMENTS_ELEMENTS:
      // Probably not worth implementing. Take the slow path.
      return FastIterateResult::kSlowPath;
    case WASM_ARRAY_ELEMENTS:
    case FAST_STRING_WRAPPER_ELEMENTS:
    case SLOW_STRING_WRAPPER_ELEMENTS:
    case SHARED_ARRAY_ELEMENTS:
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) case TYPE##_ELEMENTS:
      TYPED_ARRAYS(TYPED_ARRAY_CASE)
      RAB_GSAB_TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE
      // These are never used by v8::Array instances.
      UNREACHABLE();
  }
  // The input value of the switch is untrusted, so even if it's exhaustive, it
  // can skip all cases and end up here, triggering UB since there's no return.
  SBXCHECK(false);
}

}  // namespace internal

Maybe<void> v8::Array::Iterate(Local<Context> context,
                               v8::Array::IterationCallback callback,
                               void* callback_data) {
  auto array = Utils::OpenDirectHandle(this);
  i::Isolate* isolate = i::Isolate::Current();
  i::FastIterateResult fast_result =
      i::FastIterateArray(array, isolate, callback, callback_data);
  if (fast_result == i::FastIterateResult::kException) return {};
  // Early breaks and completed iteration both return successfully.
  if (fast_result != i::FastIterateResult::kSlowPath) return JustVoid();

  // Slow path: retrieving elements could have side effects.
  EnterV8Scope<> api_scope{isolate, context, RCCId::kAPI_Array_Iterate};
  for (uint32_t i = 0; i < i::GetLength(*array); ++i) {
    i::DirectHandle<i::Object> element;
    if (!i::JSReceiver::GetElement(isolate, array, i).ToHandle(&element)) {
      return {};
    }
    using Result = v8::Array::CallbackResult;
    Result result = callback(i, Utils::ToLocal(element), callback_data);
    if (result == Result::kException) return {};
    if (result == Result::kBreak) return JustVoid();
  }
  return JustVoid();
}

v8::TypecheckWitness::TypecheckWitness(Isolate* isolate)
#ifdef V8_ENABLE_DIRECT_HANDLE
    // An empty local suffices.
    : cached_map_()
#else
    // We need to reserve a handle that we can patch later.
    // We initialize it with something that cannot compare equal to any map.
    : cached_map_(v8::Number::New(isolate, 1))
#endif
{
}

void v8::TypecheckWitness::Update(Local<Value> baseline) {
  i::Tagged<i::Object> obj = *Utils::OpenDirectHandle(*baseline);
#ifdef V8_ENABLE_DIRECT_HANDLE
  if (IsSmi(obj)) {
    cached_map_ = Local<Data>();
  } else {
    i::Tagged<i::HeapObject> map = i::Cast<i::HeapObject>(obj)->map();
    cached_map_ = Local<Data>::FromAddress(map->ptr());
  }
#else
  i::Tagged<i::Object> map = i::Smi::zero();
  if (!IsSmi(obj)) map = i::Cast<i::HeapObject>(obj)->map();
  // Design overview: in the {TypecheckWitness} constructor, we create
  // a single handle for the witness value. Whenever {Update} is called, we
  // make this handle point at the fresh baseline/witness; the intention is
  // to allow having short-lived HandleScopes (e.g. in {FastIterateArray}
  // above) while a {TypecheckWitness} is alive: it therefore cannot hold
  // on to one of the short-lived handles.
  // Calling {OpenIndirectHandle} on the {cached_map_} only serves to
  // "reinterpret_cast" it to an {i::IndirectHandle} on which we can call
  // {PatchValue}.
  auto cache = Utils::OpenIndirectHandle(*cached_map_);
  cache.PatchValue(map);
#endif
}

Local<v8::Map> v8::Map::New(Isolate* v8_isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Map_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::JSMap> obj = i_isolate->factory()->NewJSMap();
  return Utils::ToLocal(obj);
}

size_t v8::Map::Size() const {
  auto obj = Utils::OpenDirectHandle(this);
  return i::Cast<i::OrderedHashMap>(obj->table())->NumberOfElements();
}

void Map::Clear() {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Map_Clear);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::JSMap::Clear(i_isolate, self);
}

MaybeLocal<Value> Map::Get(Local<Context> context, Local<Value> key) {
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Map_Get};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto self = Utils::OpenDirectHandle(this);
  i::DirectHandle<i::Object> args[] = {Utils::OpenDirectHandle(*key)};
  return api_scope.EscapeMaybe(i::Execution::CallBuiltin(
      i_isolate, i_isolate->map_get(), self, base::VectorOf(args)));
}

MaybeLocal<Map> Map::Set(Local<Context> context, Local<Value> key,
                         Local<Value> value) {
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Map_Set};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto self = Utils::OpenDirectHandle(this);
  i::DirectHandle<i::Object> args[] = {Utils::OpenDirectHandle(*key),
                                       Utils::OpenDirectHandle(*value)};
  return api_scope.EscapeMaybe(i::Cast<i::JSMap>(i::Execution::CallBuiltin(
      i_isolate, i_isolate->map_set(), self, base::VectorOf(args))));
}

Maybe<bool> Map::Has(Local<Context> context, Local<Value> key) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context, RCCId::kAPI_Map_Has};
  auto self = Utils::OpenDirectHandle(this);
  i::DirectHandle<i::Object> result;
  i::DirectHandle<i::Object> args[] = {Utils::OpenDirectHandle(*key)};
  if (!i::Execution::CallBuiltin(i_isolate, i_isolate->map_has(), self,
                                 base::VectorOf(args))
           .ToHandle(&result)) {
    return {};
  }
  return Just(i::IsTrue(*result, i_isolate));
}

Maybe<bool> Map::Delete(Local<Context> context, Local<Value> key) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context, RCCId::kAPI_Map_Delete};
  auto self = Utils::OpenDirectHandle(this);
  i::DirectHandle<i::Object> result;
  i::DirectHandle<i::Object> args[] = {Utils::OpenDirectHandle(*key)};
  if (!i::Execution::CallBuiltin(i_isolate, i_isolate->map_delete(), self,
                                 base::VectorOf(args))
           .ToHandle(&result)) {
    return {};
  }
  return Just(i::IsTrue(*result, i_isolate));
}

namespace {

enum class MapAsArrayKind {
  kEntries = i::JS_MAP_KEY_VALUE_ITERATOR_TYPE,
  kKeys = i::JS_MAP_KEY_ITERATOR_TYPE,
  kValues = i::JS_MAP_VALUE_ITERATOR_TYPE
};

enum class SetAsArrayKind {
  kEntries = i::JS_SET_KEY_VALUE_ITERATOR_TYPE,
  kValues = i::JS_SET_VALUE_ITERATOR_TYPE
};

i::DirectHandle<i::JSArray> MapAsArray(i::Isolate* i_isolate,
                                       i::Tagged<i::Object> table_obj,
                                       int offset, MapAsArrayKind kind) {
  i::Factory* factory = i_isolate->factory();
  i::DirectHandle<i::OrderedHashMap> table(
      i::Cast<i::OrderedHashMap>(table_obj), i_isolate);
  const bool collect_keys =
      kind == MapAsArrayKind::kEntries || kind == MapAsArrayKind::kKeys;
  const bool collect_values =
      kind == MapAsArrayKind::kEntries || kind == MapAsArrayKind::kValues;
  int capacity = table->UsedCapacity();
  int max_length =
      (capacity - offset) * ((collect_keys && collect_values) ? 2 : 1);
  i::DirectHandle<i::FixedArray> result = factory->NewFixedArray(max_length);
  int result_index = 0;
  {
    i::DisallowGarbageCollection no_gc;
    i::Tagged<i::Hole> hash_table_hole =
        i::ReadOnlyRoots(i_isolate).hash_table_hole_value();
    for (int i = offset; i < capacity; ++i) {
      i::InternalIndex entry(i);
      i::Tagged<i::Object> key = table->KeyAt(entry);
      if (key == hash_table_hole) continue;
      if (collect_keys) result->set(result_index++, key);
      if (collect_values) result->set(result_index++, table->ValueAt(entry));
    }
  }
  DCHECK_GE(max_length, result_index);
  if (result_index == 0) return factory->NewJSArray(0);
  result->RightTrim(i_isolate, result_index);
  return factory->NewJSArrayWithElements(result, i::PACKED_ELEMENTS,
                                         result_index);
}

}  // namespace

Local<Array> Map::AsArray() const {
  auto obj = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Map_AsArray);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  return Utils::ToLocal(
      MapAsArray(i_isolate, obj->table(), 0, MapAsArrayKind::kEntries));
}

Local<v8::Set> v8::Set::New(Isolate* v8_isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Set_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::JSSet> obj = i_isolate->factory()->NewJSSet();
  return Utils::ToLocal(obj);
}

size_t v8::Set::Size() const {
  auto obj = Utils::OpenDirectHandle(this);
  return i::Cast<i::OrderedHashSet>(obj->table())->NumberOfElements();
}

void Set::Clear() {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Set_Clear);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::JSSet::Clear(i_isolate, self);
}

MaybeLocal<Set> Set::Add(Local<Context> context, Local<Value> key) {
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Set_Add};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto self = Utils::OpenDirectHandle(this);
  i::DirectHandle<i::Object> args[] = {Utils::OpenDirectHandle(*key)};
  return api_scope.EscapeMaybe(i::Cast<i::JSSet>(i::Execution::CallBuiltin(
      i_isolate, i_isolate->set_add(), self, base::VectorOf(args))));
}

Maybe<bool> Set::Has(Local<Context> context, Local<Value> key) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context, RCCId::kAPI_Set_Has};
  auto self = Utils::OpenDirectHandle(this);
  i::DirectHandle<i::Object> result;
  i::DirectHandle<i::Object> args[] = {Utils::OpenDirectHandle(*key)};
  if (!i::Execution::CallBuiltin(i_isolate, i_isolate->set_has(), self,
                                 base::VectorOf(args))
           .ToHandle(&result)) {
    return {};
  }
  return Just(i::IsTrue(*result, i_isolate));
}

Maybe<bool> Set::Delete(Local<Context> context, Local<Value> key) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context, RCCId::kAPI_Set_Delete};
  auto self = Utils::OpenDirectHandle(this);
  i::DirectHandle<i::Object> result;
  i::DirectHandle<i::Object> args[] = {Utils::OpenDirectHandle(*key)};
  if (!i::Execution::CallBuiltin(i_isolate, i_isolate->set_delete(), self,
                                 base::VectorOf(args))
           .ToHandle(&result)) {
    return {};
  }
  return Just(i::IsTrue(*result, i_isolate));
}

namespace {
i::DirectHandle<i::JSArray> SetAsArray(i::Isolate* i_isolate,
                                       i::Tagged<i::Object> table_obj,
                                       int offset, SetAsArrayKind kind) {
  i::Factory* factory = i_isolate->factory();
  i::DirectHandle<i::OrderedHashSet> table(
      i::Cast<i::OrderedHashSet>(table_obj), i_isolate);
  // Elements skipped by |offset| may already be deleted.
  int capacity = table->UsedCapacity();
  const bool collect_key_values = kind == SetAsArrayKind::kEntries;
  int max_length = (capacity - offset) * (collect_key_values ? 2 : 1);
  if (max_length == 0) return factory->NewJSArray(0);
  i::DirectHandle<i::FixedArray> result = factory->NewFixedArray(max_length);
  int result_index = 0;
  {
    i::DisallowGarbageCollection no_gc;
    i::Tagged<i::Hole> hash_table_hole =
        i::ReadOnlyRoots(i_isolate).hash_table_hole_value();
    for (int i = offset; i < capacity; ++i) {
      i::InternalIndex entry(i);
      i::Tagged<i::Object> key = table->KeyAt(entry);
      if (key == hash_table_hole) continue;
      result->set(result_index++, key);
      if (collect_key_values) result->set(result_index++, key);
    }
  }
  DCHECK_GE(max_length, result_index);
  if (result_index == 0) return factory->NewJSArray(0);
  result->RightTrim(i_isolate, result_index);
  return factory->NewJSArrayWithElements(result, i::PACKED_ELEMENTS,
                                         result_index);
}
}  // namespace

Local<Array> Set::AsArray() const {
  auto obj = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Set_AsArray);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  return Utils::ToLocal(
      SetAsArray(i_isolate, obj->table(), 0, SetAsArrayKind::kValues));
}

MaybeLocal<Promise::Resolver> Promise::Resolver::New(Local<Context> context) {
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Promise_Resolver_New};
  i::Isolate* i_isolate = api_scope.i_isolate();
  Local<Promise::Resolver> result =
      Utils::ToLocal(i::Cast<i::JSObject>(i_isolate->factory()->NewJSPromise()))
          .As<Promise::Resolver>();
  // Also check if promise hooks set an exception.
  // TODO(clemensb): Should `Factory::NewJSPromise()` return a MaybeHandle
  // instead?
  if (i_isolate->has_exception()) return {};
  return api_scope.Escape(result);
}

Local<Promise> Promise::Resolver::GetPromise() {
  auto promise = Utils::OpenDirectHandle(this);
  return Local<Promise>::Cast(Utils::ToLocal(promise));
}

Maybe<bool> Promise::Resolver::Resolve(Local<Context> context,
                                       Local<Value> value) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context,
                           RCCId::kAPI_Promise_Resolver_Resolve};
  auto self = Utils::OpenDirectHandle(this);
  auto promise = i::Cast<i::JSPromise>(self);

  if (promise->status() != Promise::kPending) {
    return Just(true);
  }

  // TODO(434637746): Drop this CHECK when the bug is fixed.
  CHECK(!value.IsEmpty());

  if (i::JSPromise::Resolve(promise, Utils::OpenDirectHandle(*value))
          .is_null()) {
    return {};
  }
  return Just(true);
}

Maybe<bool> Promise::Resolver::Reject(Local<Context> context,
                                      Local<Value> value) {
  auto i_isolate = i::Isolate::Current();
  EnterV8Scope<> api_scope{i_isolate, context,
                           RCCId::kAPI_Promise_Resolver_Reject};
  auto self = Utils::OpenDirectHandle(this);
  auto promise = i::Cast<i::JSPromise>(self);

  if (promise->status() != Promise::kPending) {
    return Just(true);
  }

  if (i::JSPromise::Reject(promise, Utils::OpenDirectHandle(*value))
          .is_null()) {
    return {};
  }
  return Just(true);
}

MaybeLocal<Promise> Promise::Catch(Local<Context> context,
                                   Local<Function> handler) {
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Promise_Catch};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto self = Utils::OpenDirectHandle(this);
  i::DirectHandle<i::Object> args[] = {i_isolate->factory()->undefined_value(),
                                       Utils::OpenDirectHandle(*handler)};
  // Do not call the built-in Promise.prototype.catch!
  // v8::Promise should not call out to a monkeypatched Promise.prototype.then
  // as the implementation of Promise.prototype.catch does.
  return api_scope.EscapeMaybe(
      MaybeLocal<Promise>::Cast(Utils::ToMaybeLocal(i::Execution::CallBuiltin(
          i_isolate, i_isolate->promise_then(), self, base::VectorOf(args)))));
}

MaybeLocal<Promise> Promise::Then(Local<Context> context,
                                  Local<Function> handler) {
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Promise_Then};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto self = Utils::OpenDirectHandle(this);
  i::DirectHandle<i::Object> args[] = {Utils::OpenDirectHandle(*handler)};
  return api_scope.EscapeMaybe(
      MaybeLocal<Promise>::Cast(Utils::ToMaybeLocal(i::Execution::CallBuiltin(
          i_isolate, i_isolate->promise_then(), self, base::VectorOf(args)))));
}

MaybeLocal<Promise> Promise::Then(Local<Context> context,
                                  Local<Function> on_fulfilled,
                                  Local<Function> on_rejected) {
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Promise_Then};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto self = Utils::OpenDirectHandle(this);
  i::DirectHandle<i::Object> args[] = {Utils::OpenDirectHandle(*on_fulfilled),
                                       Utils::OpenDirectHandle(*on_rejected)};
  return api_scope.EscapeMaybe(
      MaybeLocal<Promise>::Cast(Utils::ToMaybeLocal(i::Execution::CallBuiltin(
          i_isolate, i_isolate->promise_then(), self, base::VectorOf(args)))));
}

bool Promise::HasHandler() const {
  i::Tagged<i::JSReceiver> promise = *Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_Promise_HasRejectHandler);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  if (!IsJSPromise(promise)) return false;
  return i::Cast<i::JSPromise>(promise)->has_handler();
}

Local<Value> Promise::Result() {
  auto promise = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Promise_Result);
  auto js_promise = i::Cast<i::JSPromise>(promise);
  Utils::ApiCheck(js_promise->status() != kPending, "v8_Promise_Result",
                  "Promise is still pending");
  return Utils::ToLocal(i::direct_handle(js_promise->result(), i_isolate));
}

Promise::PromiseState Promise::State() {
  auto promise = Utils::OpenDirectHandle(this);
  ApiRuntimeCallStatsScope rcs_scope(i::Isolate::Current(),
                                     RCCId::kAPI_Promise_Status);
  auto js_promise = i::Cast<i::JSPromise>(promise);
  return static_cast<PromiseState>(js_promise->status());
}

void Promise::MarkAsHandled() {
  Utils::OpenDirectHandle(this)->set_has_handler(true);
}

void Promise::MarkAsSilent() {
  Utils::OpenDirectHandle(this)->set_is_silent(true);
}

Local<Value> Proxy::GetTarget() {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  return Utils::ToLocal(i::direct_handle(self->target(), i_isolate));
}

Local<Value> Proxy::GetHandler() {
  auto self = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  return Utils::ToLocal(i::direct_handle(self->handler(), i_isolate));
}

bool Proxy::IsRevoked() const {
  return Utils::OpenDirectHandle(this)->IsRevoked();
}

void Proxy::Revoke() {
  auto self = Utils::OpenDirectHandle(this);
  i::JSProxy::Revoke(self);
}

MaybeLocal<Proxy> Proxy::New(Local<Context> context, Local<Object> local_target,
                             Local<Object> local_handler) {
  PrepareForExecutionScope api_scope{context, RCCId::kAPI_Proxy_New};
  i::Isolate* i_isolate = api_scope.i_isolate();
  auto target = Utils::OpenDirectHandle(*local_target);
  auto handler = Utils::OpenDirectHandle(*local_handler);
  // The API doesn't distinguish revocable proxies, so we must assume that all
  // objects created through it are revocable.
  bool api_proxies_assumed_revocable = true;
  return api_scope.EscapeMaybe(i::JSProxy::New(i_isolate, target, handler,
                                               api_proxies_assumed_revocable));
}

CompiledWasmModule::CompiledWasmModule(
    std::shared_ptr<internal::wasm::NativeModule> native_module,
    const char* source_url, size_t url_length)
    : native_module_(std::move(native_module)),
      source_url_(source_url, url_length) {
  CHECK_NOT_NULL(native_module_);
}

OwnedBuffer CompiledWasmModule::Serialize() {
#if V8_ENABLE_WEBASSEMBLY
  TRACE_EVENT0("v8.wasm", "wasm.SerializeModule");
  i::wasm::WasmSerializer wasm_serializer(native_module_.get());
  size_t buffer_size = wasm_serializer.GetSerializedNativeModuleSize();
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[buffer_size]);
  if (!wasm_serializer.SerializeNativeModule({buffer.get(), buffer_size})) {
    return {};
  }
  return {std::move(buffer), buffer_size};
#else
  UNREACHABLE();
#endif  // V8_ENABLE_WEBASSEMBLY
}

MemorySpan<const uint8_t> CompiledWasmModule::GetWireBytesRef() {
#if V8_ENABLE_WEBASSEMBLY
  base::Vector<const uint8_t> bytes_vec = native_module_->wire_bytes();
  return {bytes_vec.begin(), bytes_vec.size()};
#else
  UNREACHABLE();
#endif  // V8_ENABLE_WEBASSEMBLY
}

Local<ArrayBuffer> v8::WasmMemoryObject::Buffer() {
#if V8_ENABLE_WEBASSEMBLY
  auto obj = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  return Utils::ToLocal(i::direct_handle(obj->array_buffer(), i_isolate));
#else
  UNREACHABLE();
#endif  // V8_ENABLE_WEBASSEMBLY
}

CompiledWasmModule WasmModuleObject::GetCompiledModule() {
#if V8_ENABLE_WEBASSEMBLY
  auto obj = i::Cast<i::WasmModuleObject>(Utils::OpenDirectHandle(this));
  auto url = i::direct_handle(i::Cast<i::String>(obj->script()->name()),
                              i::Isolate::Current());
  size_t length;
  std::unique_ptr<char[]> cstring = url->ToCString(&length);
  return CompiledWasmModule(std::move(obj->shared_native_module()),
                            cstring.get(), length);
#else
  UNREACHABLE();
#endif  // V8_ENABLE_WEBASSEMBLY
}

MaybeLocal<WasmModuleObject> WasmModuleObject::FromCompiledModule(
    Isolate* v8_isolate, const CompiledWasmModule& compiled_module) {
#if V8_ENABLE_WEBASSEMBLY
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::DirectHandle<i::WasmModuleObject> module_object =
      i::wasm::GetWasmEngine()->ImportNativeModule(
          i_isolate, compiled_module.native_module_,
          base::VectorOf(compiled_module.source_url()));
  return Utils::ToLocal(module_object);
#else
  UNREACHABLE();
#endif  // V8_ENABLE_WEBASSEMBLY
}

MaybeLocal<WasmModuleObject> WasmModuleObject::Compile(
    Isolate* v8_isolate, MemorySpan<const uint8_t> wire_bytes) {
#if V8_ENABLE_WEBASSEMBLY
  base::OwnedVector<const uint8_t> bytes = base::OwnedCopyOf(wire_bytes);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  // We don't check for `IsWasmCodegenAllowed` here, because this function is
  // used for ESM integration, which in terms of security is equivalent to
  // <script> tags rather than to {eval}.
  i::MaybeDirectHandle<i::WasmModuleObject> maybe_compiled;
  {
    i::wasm::ErrorThrower thrower(i_isolate, "WasmModuleObject::Compile()");
    auto enabled_features =
        i::wasm::WasmEnabledFeatures::FromIsolate(i_isolate);
    // TODO(14179): Provide an API method that supports compile options.
    maybe_compiled = i::wasm::GetWasmEngine()->SyncCompile(
        i_isolate, enabled_features, i::wasm::CompileTimeImports{}, &thrower,
        std::move(bytes));
  }
  CHECK_EQ(maybe_compiled.is_null(), i_isolate->has_exception());
  if (maybe_compiled.is_null()) return {};
  return Utils::ToLocal(maybe_compiled.ToHandleChecked());
#else
  Utils::ApiCheck(false, "WasmModuleObject::Compile",
                  "WebAssembly support is not enabled");
  UNREACHABLE();
#endif  // V8_ENABLE_WEBASSEMBLY
}

// static
Local<WasmMemoryMapDescriptor> WasmMemoryMapDescriptor::New(
    v8::Isolate* v8_isolate, WasmMemoryMapDescriptor::WasmFileDescriptor fd) {
#if V8_ENABLE_WEBASSEMBLY
  CHECK(i::v8_flags.experimental_wasm_memory_control);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::DirectHandle<i::WasmMemoryMapDescriptor> result =
      i::WasmMemoryMapDescriptor::NewFromFileDescriptor(i_isolate, fd);
  return Utils::ToLocal(result);
#else
  Utils::ApiCheck(false, "WasmMemoryMapDescriptor::New",
                  "WebAssembly support is not enabled");
  UNREACHABLE();
#endif
}

// static
v8::ArrayBuffer::Allocator* v8::ArrayBuffer::Allocator::NewDefaultAllocator() {
#ifdef V8_ENABLE_SANDBOX
  return new ArrayBufferAllocator(i::IsolateGroup::GetDefault());
#else
  return new ArrayBufferAllocator();
#endif
}

#if defined(V8_COMPRESS_POINTERS) && \
    !defined(V8_COMPRESS_POINTERS_IN_SHARED_CAGE)
v8::ArrayBuffer::Allocator* v8::ArrayBuffer::Allocator::NewDefaultAllocator(
    const IsolateGroup& group) {
#ifdef V8_ENABLE_SANDBOX
  return new ArrayBufferAllocator(group.isolate_group_);
#else
  return new ArrayBufferAllocator();
#endif
}
#endif  // defined(V8_COMPRESS_POINTERS) &&
        // !defined(V8_COMPRESS_POINTERS_IN_SHARED_CAGE)

bool v8::ArrayBuffer::IsDetachable() const {
  return Utils::OpenDirectHandle(this)->is_detachable();
}

bool v8::ArrayBuffer::WasDetached() const {
  return Utils::OpenDirectHandle(this)->was_detached();
}

namespace {
std::shared_ptr<i::BackingStore> ToInternal(
    std::shared_ptr<i::BackingStoreBase> backing_store) {
  return std::static_pointer_cast<i::BackingStore>(backing_store);
}
}  // namespace

Maybe<bool> v8::ArrayBuffer::Detach(v8::Local<v8::Value> key) {
  auto obj = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  Utils::ApiCheck(obj->is_detachable(), "v8::ArrayBuffer::Detach",
                  "Only detachable ArrayBuffers can be detached");
  Local<Context> context =
      reinterpret_cast<v8::Isolate*>(i_isolate)->GetCurrentContext();
  // TODO(verwaest): Remove this case after forcing the embedder to enter the
  // context.
  if (context.IsEmpty()) {
    EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
    if (key.IsEmpty()) {
      i::JSArrayBuffer::Detach(obj).Check();
    } else {
      auto i_key = Utils::OpenDirectHandle(*key);
      constexpr bool kForceForWasmMemory = false;
      i::JSArrayBuffer::Detach(obj, kForceForWasmMemory, i_key).Check();
    }
    return Just(true);
  }
  EnterV8NoScriptScope<> api_scope{i_isolate, context,
                                   RCCId::kAPI_ArrayBuffer_Detach};
  if (key.IsEmpty()) return i::JSArrayBuffer::Detach(obj);
  auto i_key = Utils::OpenDirectHandle(*key);
  constexpr bool kForceForWasmMemory = false;
  return i::JSArrayBuffer::Detach(obj, kForceForWasmMemory, i_key);
}

void v8::ArrayBuffer::Detach() { Detach(Local<Value>()).Check(); }

void v8::ArrayBuffer::SetDetachKey(v8::Local<v8::Value> key) {
  auto obj = Utils::OpenDirectHandle(this);
  auto i_key = Utils::OpenDirectHandle(*key);
  obj->set_detach_key(*i_key);
}

size_t v8::ArrayBuffer::ByteLength() const {
  return Utils::OpenDirectHandle(this)->GetByteLength();
}

size_t v8::ArrayBuffer::MaxByteLength() const {
  return Utils::OpenDirectHandle(this)->max_byte_length();
}

namespace {
i::InitializedFlag GetInitializedFlag(
    BackingStoreInitializationMode initialization_mode) {
  switch (initialization_mode) {
    case BackingStoreInitializationMode::kUninitialized:
      return i::InitializedFlag::kUninitialized;
    case BackingStoreInitializationMode::kZeroInitialized:
      return i::InitializedFlag::kZeroInitialized;
  }
  UNREACHABLE();
}
}  // namespace

MaybeLocal<ArrayBuffer> v8::ArrayBuffer::MaybeNew(
    Isolate* isolate, size_t byte_length,
    BackingStoreInitializationMode initialization_mode) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_ArrayBuffer_MaybeNew);
  size_t max = i_isolate->array_buffer_allocator()->MaxAllocationSize();
  DCHECK(max <= ArrayBuffer::kMaxByteLength);
  if (byte_length > max) return {};
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::MaybeDirectHandle<i::JSArrayBuffer> result =
      i_isolate->factory()->NewJSArrayBufferAndBackingStore(
          byte_length, GetInitializedFlag(initialization_mode));

  i::DirectHandle<i::JSArrayBuffer> array_buffer;
  if (!result.ToHandle(&array_buffer)) return {};

  return Utils::ToLocal(array_buffer);
}

Local<ArrayBuffer> v8::ArrayBuffer::New(
    Isolate* v8_isolate, size_t byte_length,
    BackingStoreInitializationMode initialization_mode) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_ArrayBuffer_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::MaybeDirectHandle<i::JSArrayBuffer> result =
      i_isolate->factory()->NewJSArrayBufferAndBackingStore(
          byte_length, GetInitializedFlag(initialization_mode));

  i::DirectHandle<i::JSArrayBuffer> array_buffer;
  if (!result.ToHandle(&array_buffer)) {
    i::V8::FatalProcessOutOfMemory(i_isolate, "v8::ArrayBuffer::New");
  }

  return Utils::ToLocal(array_buffer);
}

Local<ArrayBuffer> v8::ArrayBuffer::New(
    Isolate* v8_isolate, std::shared_ptr<BackingStore> backing_store) {
  CHECK_IMPLIES(backing_store->ByteLength() != 0,
                backing_store->Data() != nullptr);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_ArrayBuffer_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  std::shared_ptr<i::BackingStore> i_backing_store(
      ToInternal(std::move(backing_store)));
  Utils::ApiCheck(
      !i_backing_store->is_shared(), "v8_ArrayBuffer_New",
      "Cannot construct ArrayBuffer with a BackingStore of SharedArrayBuffer");
  i::DirectHandle<i::JSArrayBuffer> obj =
      i_isolate->factory()->NewJSArrayBuffer(std::move(i_backing_store));
  return Utils::ToLocal(obj);
}

std::unique_ptr<v8::BackingStore> v8::ArrayBuffer::NewBackingStore(
    Isolate* v8_isolate, size_t byte_length,
    BackingStoreInitializationMode initialization_mode,
    BackingStoreOnFailureMode on_failure) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_ArrayBuffer_NewBackingStore);
  if (on_failure == BackingStoreOnFailureMode::kOutOfMemory) {
    CHECK_LE(byte_length, i::JSArrayBuffer::kMaxByteLength);
  } else if (byte_length > i::JSArrayBuffer::kMaxByteLength) {
    DCHECK(on_failure == BackingStoreOnFailureMode::kReturnNull);
    return {};
  }
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  std::unique_ptr<i::BackingStoreBase> backing_store =
      i::BackingStore::Allocate(i_isolate, byte_length,
                                i::SharedFlag::kNotShared,
                                GetInitializedFlag(initialization_mode));
  if (!backing_store) {
    if (on_failure == BackingStoreOnFailureMode::kOutOfMemory) {
      i::V8::FatalProcessOutOfMemory(i_isolate,
                                     "v8::ArrayBuffer::NewBackingStore");
    } else {
      DCHECK(on_failure == BackingStoreOnFailureMode::kReturnNull);
      return {};
    }
  }
  return std::unique_ptr<v8::BackingStore>(
      static_cast<v8::BackingStore*>(backing_store.release()));
}

#ifdef V8_ENABLE_SANDBOX
bool v8::IsolateGroup::SandboxContains(void* pointer) const {
  return isolate_group_->sandbox()->Contains(pointer);
}

VirtualAddressSpace* v8::IsolateGroup::GetSandboxAddressSpace() {
  i::Sandbox* sandbox = isolate_group_->sandbox();
  Utils::ApiCheck(sandbox->is_initialized(), "v8::V8::GetSandboxAddressSpace",
                  "The sandbox must be initialized first");
  return sandbox->address_space();
}
#endif

std::unique_ptr<v8::BackingStore> v8::ArrayBuffer::NewBackingStore(
    void* data, size_t byte_length, v8::BackingStore::DeleterCallback deleter,
    void* deleter_data) {
  CHECK_LE(byte_length, i::JSArrayBuffer::kMaxByteLength);
#ifdef V8_ENABLE_SANDBOX
  Utils::ApiCheck(!data || i::Sandbox::current()->Contains(data),
                  "v8_ArrayBuffer_NewBackingStore",
                  "When the V8 Sandbox is enabled, ArrayBuffer backing stores "
                  "must be allocated inside the sandbox address space. Please "
                  "use an appropriate ArrayBuffer::Allocator to allocate these "
                  "buffers, or disable the sandbox.");
#endif  // V8_ENABLE_SANDBOX

  std::unique_ptr<i::BackingStoreBase> backing_store =
      i::BackingStore::WrapAllocation(data, byte_length, deleter, deleter_data,
                                      i::SharedFlag::kNotShared);
  return std::unique_ptr<v8::BackingStore>(
      static_cast<v8::BackingStore*>(backing_store.release()));
}

// static
std::unique_ptr<BackingStore> v8::ArrayBuffer::NewResizableBackingStore(
    size_t byte_length, size_t max_byte_length) {
  Utils::ApiCheck(byte_length <= max_byte_length,
                  "v8::ArrayBuffer::NewResizableBackingStore",
                  "Cannot construct resizable ArrayBuffer, byte_length must be "
                  "<= max_byte_length");
  Utils::ApiCheck(
      byte_length <= i::JSArrayBuffer::kMaxByteLength,
      "v8::ArrayBuffer::NewResizableBackingStore",
      "Cannot construct resizable ArrayBuffer, requested length is too big");

  size_t page_size, initial_pages, max_pages;
  if (i::JSArrayBuffer::GetResizableBackingStorePageConfiguration(
          nullptr, byte_length, max_byte_length, i::kDontThrow, &page_size,
          &initial_pages, &max_pages)
          .IsNothing()) {
    i::V8::FatalProcessOutOfMemory(nullptr,
                                   "v8::ArrayBuffer::NewResizableBackingStore");
  }
  std::unique_ptr<i::BackingStoreBase> backing_store =
      i::BackingStore::TryAllocateAndPartiallyCommitMemory(
          nullptr, byte_length, max_byte_length, page_size, initial_pages,
          max_pages, i::WasmMemoryFlag::kNotWasm, i::SharedFlag::kNotShared);
  if (!backing_store) {
    i::V8::FatalProcessOutOfMemory(nullptr,
                                   "v8::ArrayBuffer::NewResizableBackingStore");
  }
  return std::unique_ptr<v8::BackingStore>(
      static_cast<v8::BackingStore*>(backing_store.release()));
}

Local<ArrayBuffer> v8::ArrayBufferView::Buffer() {
  auto obj = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  if (i::IsJSDataView(*obj)) {
    i::DirectHandle<i::JSDataView> data_view(i::Cast<i::JSDataView>(*obj),
                                             i_isolate);
    DCHECK(IsJSArrayBuffer(data_view->buffer()));
    return Utils::ToLocal(i::direct_handle(
        i::Cast<i::JSArrayBuffer>(data_view->buffer()), i_isolate));
  } else if (i::IsJSRabGsabDataView(*obj)) {
    i::DirectHandle<i::JSRabGsabDataView> data_view(
        i::Cast<i::JSRabGsabDataView>(*obj), i_isolate);
    DCHECK(IsJSArrayBuffer(data_view->buffer()));
    return Utils::ToLocal(i::direct_handle(
        i::Cast<i::JSArrayBuffer>(data_view->buffer()), i_isolate));
  } else {
    DCHECK(IsJSTypedArray(*obj));
    return Utils::ToLocal(i::Cast<i::JSTypedArray>(*obj)->GetBuffer(i_isolate));
  }
}

size_t v8::ArrayBufferView::CopyContents(void* dest, size_t byte_length) {
  auto self = Utils::OpenDirectHandle(this);
  size_t bytes_to_copy = std::min(byte_length, self->byte_length());
  if (bytes_to_copy) {
    i::DisallowGarbageCollection no_gc;
    const char* source;
    if (i::IsJSTypedArray(*self)) {
      i::Tagged<i::JSTypedArray> array = i::Cast<i::JSTypedArray>(*self);
      source = reinterpret_cast<char*>(array->DataPtr());
    } else {
      DCHECK(i::IsJSDataView(*self) || i::IsJSRabGsabDataView(*self));
      i::Tagged<i::JSDataViewOrRabGsabDataView> data_view =
          i::Cast<i::JSDataViewOrRabGsabDataView>(*self);
      source = reinterpret_cast<char*>(data_view->data_pointer());
    }
    memcpy(dest, source, bytes_to_copy);
  }
  return bytes_to_copy;
}

v8::MemorySpan<uint8_t> v8::ArrayBufferView::GetContents(
    v8::MemorySpan<uint8_t> storage) {
  internal::DisallowGarbageCollection no_gc;
  auto self = Utils::OpenDirectHandle(this);
  if (self->IsDetachedOrOutOfBounds()) return {};
  if (internal::IsJSTypedArray(*self)) {
    i::Tagged<i::JSTypedArray> typed_array = i::Cast<i::JSTypedArray>(*self);
    if (typed_array->is_on_heap()) {
      // The provided storage does not have enough capacity for the content of
      // the TypedArray.
      size_t bytes_to_copy = self->byte_length();
      CHECK_LE(bytes_to_copy, storage.size());
      const uint8_t* source =
          reinterpret_cast<uint8_t*>(typed_array->DataPtr());
      memcpy(reinterpret_cast<void*>(storage.data()), source, bytes_to_copy);
      return {storage.data(), bytes_to_copy};
    }
    // The TypedArray already has off-heap storage, just return a view on it.
    return {reinterpret_cast<uint8_t*>(typed_array->DataPtr()),
            typed_array->GetByteLength()};
  }
  if (i::IsJSDataView(*self)) {
    i::Tagged<i::JSDataView> data_view = i::Cast<i::JSDataView>(*self);
    return {reinterpret_cast<uint8_t*>(data_view->data_pointer()),
            data_view->byte_length()};
  }
  // Other types of ArrayBufferView always have an off-heap storage.
  DCHECK(i::IsJSRabGsabDataView(*self));
  i::Tagged<i::JSRabGsabDataView> data_view =
      i::Cast<i::JSRabGsabDataView>(*self);
  return {reinterpret_cast<uint8_t*>(data_view->data_pointer()),
          data_view->GetByteLength()};
}

bool v8::ArrayBufferView::HasBuffer() const {
  auto self = Utils::OpenDirectHandle(this);
  if (!IsJSTypedArray(*self)) return true;
  auto typed_array = i::Cast<i::JSTypedArray>(self);
  return !typed_array->is_on_heap();
}

size_t v8::ArrayBufferView::ByteOffset() {
  auto obj = Utils::OpenDirectHandle(this);
  return obj->IsDetachedOrOutOfBounds() ? 0 : obj->byte_offset();
}

size_t v8::ArrayBufferView::ByteLength() {
  i::DisallowGarbageCollection no_gc;
  i::Tagged<i::JSArrayBufferView> obj = *Utils::OpenDirectHandle(this);
  if (obj->IsDetachedOrOutOfBounds()) {
    return 0;
  }
  if (i::IsJSTypedArray(obj)) {
    return i::Cast<i::JSTypedArray>(obj)->GetByteLength();
  }
  if (i::IsJSDataView(obj)) {
    return i::Cast<i::JSDataView>(obj)->byte_length();
  }
  return i::Cast<i::JSRabGsabDataView>(obj)->GetByteLength();
}

size_t v8::TypedArray::Length() {
  i::DisallowGarbageCollection no_gc;
  i::Tagged<i::JSTypedArray> obj = *Utils::OpenDirectHandle(this);
  return obj->IsDetachedOrOutOfBounds() ? 0 : obj->GetLength();
}

static_assert(v8::TypedArray::kMaxByteLength == i::JSTypedArray::kMaxByteLength,
              "v8::TypedArray::kMaxByteLength must match "
              "i::JSTypedArray::kMaxByteLength");

#define TYPED_ARRAY_NEW(Type, type, TYPE, ctype)                            \
  Local<Type##Array> Type##Array::New(Local<ArrayBuffer> array_buffer,      \
                                      size_t byte_offset, size_t length) {  \
    i::Isolate* i_isolate = i::Isolate::Current();                          \
    ApiRuntimeCallStatsScope rcs_scope(i_isolate,                           \
                                       RCCId::kAPI_##Type##Array_New);      \
    EnterV8NoScriptNoExceptionScope api_scope(i_isolate);                   \
    if (!Utils::ApiCheck(length <= kMaxLength,                              \
                         "v8::" #Type                                       \
                         "Array::New(Local<ArrayBuffer>, size_t, size_t)",  \
                         "length exceeds max allowed value")) {             \
      return {};                                                            \
    }                                                                       \
    auto buffer = Utils::OpenDirectHandle(*array_buffer);                   \
    i::DirectHandle<i::JSTypedArray> obj =                                  \
        i_isolate->factory()->NewJSTypedArray(i::kExternal##Type##Array,    \
                                              buffer, byte_offset, length); \
    return Utils::ToLocal##Type##Array(obj);                                \
  }                                                                         \
  Local<Type##Array> Type##Array::New(                                      \
      Local<SharedArrayBuffer> shared_array_buffer, size_t byte_offset,     \
      size_t length) {                                                      \
    i::Isolate* i_isolate = i::Isolate::Current();                          \
    ApiRuntimeCallStatsScope rcs_scope(i_isolate,                           \
                                       RCCId::kAPI_##Type##Array_New);      \
    EnterV8NoScriptNoExceptionScope api_scope(i_isolate);                   \
    if (!Utils::ApiCheck(                                                   \
            length <= kMaxLength,                                           \
            "v8::" #Type                                                    \
            "Array::New(Local<SharedArrayBuffer>, size_t, size_t)",         \
            "length exceeds max allowed value")) {                          \
      return {};                                                            \
    }                                                                       \
    auto buffer = Utils::OpenDirectHandle(*shared_array_buffer);            \
    i::DirectHandle<i::JSTypedArray> obj =                                  \
        i_isolate->factory()->NewJSTypedArray(i::kExternal##Type##Array,    \
                                              buffer, byte_offset, length); \
    return Utils::ToLocal##Type##Array(obj);                                \
  }

TYPED_ARRAYS_BASE(TYPED_ARRAY_NEW)
#undef TYPED_ARRAY_NEW

Local<Float16Array> Float16Array::New(Local<ArrayBuffer> array_buffer,
                                      size_t byte_offset, size_t length) {
  Utils::ApiCheck(i::v8_flags.js_float16array, "v8::Float16Array::New",
                  "Float16Array is not supported");
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Float16Array_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  if (!Utils::ApiCheck(
          length <= kMaxLength,
          "v8::Float16Array::New(Local<ArrayBuffer>, size_t, size_t)",
          "length exceeds max allowed value")) {
    return {};
  }
  auto buffer = Utils::OpenDirectHandle(*array_buffer);
  i::DirectHandle<i::JSTypedArray> obj = i_isolate->factory()->NewJSTypedArray(
      i::kExternalFloat16Array, buffer, byte_offset, length);
  return Utils::ToLocalFloat16Array(obj);
}
Local<Float16Array> Float16Array::New(
    Local<SharedArrayBuffer> shared_array_buffer, size_t byte_offset,
    size_t length) {
  Utils::ApiCheck(i::v8_flags.js_float16array, "v8::Float16Array::New",
                  "Float16Array is not supported");
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Float16Array_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  if (!Utils::ApiCheck(
          length <= kMaxLength,
          "v8::Float16Array::New(Local<SharedArrayBuffer>, size_t, size_t)",
          "length exceeds max allowed value")) {
    return {};
  }
  auto buffer = Utils::OpenDirectHandle(*shared_array_buffer);
  i::DirectHandle<i::JSTypedArray> obj = i_isolate->factory()->NewJSTypedArray(
      i::kExternalFloat16Array, buffer, byte_offset, length);
  return Utils::ToLocalFloat16Array(obj);
}

// TODO(v8:11111): Support creating length tracking DataViews via the API.
Local<DataView> DataView::New(Local<ArrayBuffer> array_buffer,
                              size_t byte_offset, size_t byte_length) {
  auto buffer = Utils::OpenDirectHandle(*array_buffer);
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_DataView_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  auto obj = i::Cast<i::JSDataView>(
      i_isolate->factory()->NewJSDataViewOrRabGsabDataView(buffer, byte_offset,
                                                           byte_length));
  return Utils::ToLocal(obj);
}

Local<DataView> DataView::New(Local<SharedArrayBuffer> shared_array_buffer,
                              size_t byte_offset, size_t byte_length) {
  auto buffer = Utils::OpenDirectHandle(*shared_array_buffer);
  i::Isolate* i_isolate = i::Isolate::Current();
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_DataView_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  auto obj = i::Cast<i::JSDataView>(
      i_isolate->factory()->NewJSDataViewOrRabGsabDataView(buffer, byte_offset,
                                                           byte_length));
  return Utils::ToLocal(obj);
}

size_t v8::SharedArrayBuffer::ByteLength() const {
  return Utils::OpenDirectHandle(this)->GetByteLength();
}

size_t v8::SharedArrayBuffer::MaxByteLength() const {
  return Utils::OpenDirectHandle(this)->max_byte_length();
}

Local<SharedArrayBuffer> v8::SharedArrayBuffer::New(
    Isolate* v8_isolate, size_t byte_length,
    BackingStoreInitializationMode initialization_mode) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_SharedArrayBuffer_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);

  std::unique_ptr<i::BackingStore> backing_store =
      i::BackingStore::Allocate(i_isolate, byte_length, i::SharedFlag::kShared,
                                GetInitializedFlag(initialization_mode));

  if (!backing_store) {
    i::V8::FatalProcessOutOfMemory(i_isolate, "v8::SharedArrayBuffer::New");
  }

  i::DirectHandle<i::JSArrayBuffer> obj =
      i_isolate->factory()->NewJSSharedArrayBuffer(std::move(backing_store));
  return Utils::ToLocalShared(obj);
}

MaybeLocal<SharedArrayBuffer> v8::SharedArrayBuffer::MaybeNew(
    Isolate* v8_isolate, size_t byte_length,
    BackingStoreInitializationMode initialization_mode) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_SharedArrayBuffer_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);

  std::unique_ptr<i::BackingStore> backing_store =
      i::BackingStore::Allocate(i_isolate, byte_length, i::SharedFlag::kShared,
                                GetInitializedFlag(initialization_mode));

  if (!backing_store) return {};

  i::DirectHandle<i::JSArrayBuffer> obj =
      i_isolate->factory()->NewJSSharedArrayBuffer(std::move(backing_store));
  return Utils::ToLocalShared(obj);
}

Local<SharedArrayBuffer> v8::SharedArrayBuffer::New(
    Isolate* v8_isolate, std::shared_ptr<BackingStore> backing_store) {
  CHECK_IMPLIES(backing_store->ByteLength() != 0,
                backing_store->Data() != nullptr);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate,
                                     RCCId::kAPI_SharedArrayBuffer_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  std::shared_ptr<i::BackingStore> i_backing_store(ToInternal(backing_store));
  Utils::ApiCheck(
      i_backing_store->is_shared(), "v8::SharedArrayBuffer::New",
      "Cannot construct SharedArrayBuffer with BackingStore of ArrayBuffer");
  i::DirectHandle<i::JSArrayBuffer> obj =
      i_isolate->factory()->NewJSSharedArrayBuffer(std::move(i_backing_store));
  return Utils::ToLocalShared(obj);
}

std::unique_ptr<v8::BackingStore> v8::SharedArrayBuffer::NewBackingStore(
    Isolate* v8_isolate, size_t byte_length,
    BackingStoreInitializationMode initialization_mode,
    BackingStoreOnFailureMode on_failure) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(
      i_isolate, RCCId::kAPI_SharedArrayBuffer_NewBackingStore);
  if (on_failure == BackingStoreOnFailureMode::kOutOfMemory) {
    Utils::ApiCheck(
        byte_length <= i::JSArrayBuffer::kMaxByteLength,
        "v8::SharedArrayBuffer::NewBackingStore",
        "Cannot construct SharedArrayBuffer, requested length is too big");
  } else {
    DCHECK(on_failure == BackingStoreOnFailureMode::kReturnNull);
    if (byte_length > i::JSArrayBuffer::kMaxByteLength) return {};
  }
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  std::unique_ptr<i::BackingStoreBase> backing_store =
      i::BackingStore::Allocate(i_isolate, byte_length, i::SharedFlag::kShared,
                                GetInitializedFlag(initialization_mode));
  if (!backing_store) {
    if (on_failure == BackingStoreOnFailureMode::kOutOfMemory) {
      i::V8::FatalProcessOutOfMemory(i_isolate,
                                     "v8::SharedArrayBuffer::NewBackingStore");
    } else {
      DCHECK(on_failure == BackingStoreOnFailureMode::kReturnNull);
      return {};
    }
  }
  return std::unique_ptr<v8::BackingStore>(
      static_cast<v8::BackingStore*>(backing_store.release()));
}

std::unique_ptr<v8::BackingStore> v8::SharedArrayBuffer::NewBackingStore(
    void* data, size_t byte_length, v8::BackingStore::DeleterCallback deleter,
    void* deleter_data) {
  CHECK_LE(byte_length, i::JSArrayBuffer::kMaxByteLength);
  std::unique_ptr<i::BackingStoreBase> backing_store =
      i::BackingStore::WrapAllocation(data, byte_length, deleter, deleter_data,
                                      i::SharedFlag::kShared);
  return std::unique_ptr<v8::BackingStore>(
      static_cast<v8::BackingStore*>(backing_store.release()));
}

Local<Symbol> v8::Symbol::New(Isolate* v8_isolate, Local<String> name) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Symbol_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::Symbol> result = i_isolate->factory()->NewSymbol();
  if (!name.IsEmpty()) result->set_description(*Utils::OpenDirectHandle(*name));
  return Utils::ToLocal(result);
}

Local<Symbol> v8::Symbol::For(Isolate* v8_isolate, Local<String> name) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  auto i_name = Utils::OpenHandle(*name);
  return Utils::ToLocal(
      i_isolate->SymbolFor(i::RootIndex::kPublicSymbolTable, i_name, false));
}

Local<Symbol> v8::Symbol::ForApi(Isolate* v8_isolate, Local<String> name) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  auto i_name = Utils::OpenHandle(*name);
  return Utils::ToLocal(
      i_isolate->SymbolFor(i::RootIndex::kApiSymbolTable, i_name, false));
}

#define WELL_KNOWN_SYMBOLS(V)                 \
  V(AsyncIterator, async_iterator)            \
  V(HasInstance, has_instance)                \
  V(IsConcatSpreadable, is_concat_spreadable) \
  V(Iterator, iterator)                       \
  V(Match, match)                             \
  V(Replace, replace)                         \
  V(Search, search)                           \
  V(Split, split)                             \
  V(ToPrimitive, to_primitive)                \
  V(ToStringTag, to_string_tag)               \
  V(Unscopables, unscopables)                 \
  V(Dispose, dispose)                         \
  V(AsyncDispose, async_dispose)

#define SYMBOL_GETTER(Name, name)                                      \
  Local<Symbol> v8::Symbol::Get##Name(Isolate* v8_isolate) {           \
    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate); \
    return Utils::ToLocal(i_isolate->factory()->name##_symbol());      \
  }

WELL_KNOWN_SYMBOLS(SYMBOL_GETTER)

#undef SYMBOL_GETTER
#undef WELL_KNOWN_SYMBOLS

Local<Private> v8::Private::New(Isolate* v8_isolate, Local<String> name) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Private_New);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::Symbol> symbol = i_isolate->factory()->NewPrivateSymbol();
  if (!name.IsEmpty()) symbol->set_description(*Utils::OpenDirectHandle(*name));
  Local<Symbol> result = Utils::ToLocal(symbol);
  return result.UnsafeAs<Private>();
}

Local<Private> v8::Private::ForApi(Isolate* v8_isolate, Local<String> name) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::DisallowJavascriptExecutionDebugOnly no_execution(i_isolate);
  i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);
  auto i_name = Utils::OpenHandle(*name);
  Local<Symbol> result = Utils::ToLocal(
      i_isolate->SymbolFor(i::RootIndex::kApiPrivateSymbolTable, i_name, true));
  return result.UnsafeAs<Private>();
}

Local<Number> v8::Number::NewFromInt32(Isolate* v8_isolate, int32_t value) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::DisallowJavascriptExecutionDebugOnly no_execution(i_isolate);
  i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);
  i::DirectHandle<i::Object> result =
      i_isolate->factory()->NewNumberFromInt(value);
  return Utils::NumberToLocal(result);
}

Local<Number> v8::Number::NewFromUint32(Isolate* v8_isolate, uint32_t value) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::DisallowJavascriptExecutionDebugOnly no_execution(i_isolate);
  i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);
  i::DirectHandle<i::Object> result =
      i_isolate->factory()->NewNumberFromUint(value);
  return Utils::NumberToLocal(result);
}

Local<Number> v8::Number::New(Isolate* v8_isolate, double value) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::DisallowJavascriptExecutionDebugOnly no_execution(i_isolate);
  i::DisallowExceptionsDebugOnly no_exceptions(i_isolate);
  if (std::isnan(value)) {
    // Introduce only canonical NaN value into the VM, to avoid signaling NaNs.
    value = std::numeric_limits<double>::quiet_NaN();
  }
  i::DirectHandle<i::Object> result = i_isolate->factory()->NewNumber(value);
  return Utils::NumberToLocal(result);
}

Local<Integer> v8::Integer::New(Isolate* v8_isolate, int32_t value) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  if (i::Smi::IsValid(value)) {
    return Utils::IntegerToLocal(
        i::DirectHandle<i::Object>(i::Smi::FromInt(value), i_isolate));
  }
  i::DirectHandle<i::Object> result = i_isolate->factory()->NewNumber(value);
  return Utils::IntegerToLocal(result);
}

Local<Integer> v8::Integer::NewFromUnsigned(Isolate* v8_isolate,
                                            uint32_t value) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  bool fits_into_int32_t = (value & (1 << 31)) == 0;
  if (fits_into_int32_t) {
    return Integer::New(v8_isolate, static_cast<int32_t>(value));
  }
  i::DirectHandle<i::Object> result = i_isolate->factory()->NewNumber(value);
  return Utils::IntegerToLocal(result);
}

Local<BigInt> v8::BigInt::New(Isolate* v8_isolate, int64_t value) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::BigInt> result = i::BigInt::FromInt64(i_isolate, value);
  return Utils::ToLocal(result);
}

Local<BigInt> v8::BigInt::NewFromUnsigned(Isolate* v8_isolate, uint64_t value) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::DirectHandle<i::BigInt> result = i::BigInt::FromUint64(i_isolate, value);
  return Utils::ToLocal(result);
}

MaybeLocal<BigInt> v8::BigInt::NewFromWords(Local<Context> context,
                                            int sign_bit, int word_count,
                                            const uint64_t* words) {
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptScope<InternalEscapableScope> api_scope{
      i_isolate, context, RCCId::kAPI_BigInt_NewFromWords};
  return api_scope.EscapeMaybe(
      i::BigInt::FromWords64(i_isolate, sign_bit, word_count, words));
}

uint64_t v8::BigInt::Uint64Value(bool* lossless) const {
  return Utils::OpenDirectHandle(this)->AsUint64(lossless);
}

int64_t v8::BigInt::Int64Value(bool* lossless) const {
  return Utils::OpenDirectHandle(this)->AsInt64(lossless);
}

int BigInt::WordCount() const {
  return Utils::OpenDirectHandle(this)->Words64Count();
}

void BigInt::ToWordsArray(int* sign_bit, int* word_count,
                          uint64_t* words) const {
  // TODO(saelo): consider migrating the public API to also use uint32_t or
  // size_t for length and count values.
  uint32_t unsigned_word_count = *word_count;
  Utils::OpenDirectHandle(this)->ToWordsArray64(sign_bit, &unsigned_word_count,
                                                words);
  *word_count = base::checked_cast<int>(unsigned_word_count);
}

int64_t Isolate::AdjustAmountOfExternalAllocatedMemoryImpl(
    int64_t change_in_bytes) {
  // Try to check for unreasonably large or small values from the embedder.
  static constexpr int64_t kMaxReasonableBytes = int64_t(1) << 60;
  static constexpr int64_t kMinReasonableBytes = -kMaxReasonableBytes;
  static_assert(kMaxReasonableBytes >= i::JSArrayBuffer::kMaxByteLength);
  CHECK(kMinReasonableBytes <= change_in_bytes &&
        change_in_bytes < kMaxReasonableBytes);

  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  const uint64_t amount =
      i_isolate->heap()->UpdateExternalMemory(change_in_bytes);

  if (change_in_bytes <= 0) {
    return amount;
  }

  if (amount > i_isolate->heap()->external_memory_limit_for_interrupt()) {
    HandleExternalMemoryInterrupt();
  }
  return amount;
}

void Isolate::HandleExternalMemoryInterrupt() {
  i::Heap* heap = reinterpret_cast<i::Isolate*>(this)->heap();
  if (heap->gc_state() != i::Heap::NOT_IN_GC) return;
  heap->HandleExternalMemoryInterrupt();
}

IsolateGroup::IsolateGroup(i::IsolateGroup*&& isolate_group)
    : isolate_group_(isolate_group) {
  DCHECK_NOT_NULL(isolate_group_);
}

IsolateGroup::IsolateGroup(const IsolateGroup& other)
    : isolate_group_(other.isolate_group_) {
  if (isolate_group_) isolate_group_->Acquire();
}

IsolateGroup& IsolateGroup::operator=(const IsolateGroup& other) {
  if (this != &other && isolate_group_ != other.isolate_group_) {
    if (isolate_group_) isolate_group_->Release();
    isolate_group_ = other.isolate_group_;
    if (isolate_group_) isolate_group_->Acquire();
  }
  return *this;
}

IsolateGroup::~IsolateGroup() {
  if (isolate_group_) {
    isolate_group_->Release();
#ifdef DEBUG
    isolate_group_ = nullptr;
#endif
  }
}

// static
IsolateGroup IsolateGroup::GetDefault() {
  return IsolateGroup(i::IsolateGroup::AcquireDefault());
}

// static
bool IsolateGroup::CanCreateNewGroups() {
  return i::IsolateGroup::CanCreateNewGroups();
}

// static
IsolateGroup IsolateGroup::Create() {
  return IsolateGroup(i::IsolateGroup::New());
}

IsolateGroup::IsolateGroup(IsolateGroup&& other) {
  isolate_group_ = other.isolate_group_;
  other.isolate_group_ = nullptr;
}

IsolateGroup& v8::IsolateGroup::operator=(IsolateGroup&& other) {
  if (this == &other) {
    return *this;
  }

  if (isolate_group_) {
    isolate_group_->Release();
  }
  isolate_group_ = other.isolate_group_;
  other.isolate_group_ = nullptr;
  return *this;
}

HeapProfiler* Isolate::GetHeapProfiler() {
  i::HeapProfiler* heap_profiler =
      reinterpret_cast<i::Isolate*>(this)->heap()->heap_profiler();
  return reinterpret_cast<HeapProfiler*>(heap_profiler);
}

void Isolate::SetIdle(bool is_idle) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->SetIdle(is_idle);
}

ArrayBuffer::Allocator* Isolate::GetArrayBufferAllocator() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  return i_isolate->array_buffer_allocator();
}

bool Isolate::InContext() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  return !i_isolate->context().is_null();
}

void Isolate::ClearKeptObjects() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->ClearKeptObjects();
}

v8::Local<v8::Context> Isolate::GetCurrentContext() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i::Tagged<i::Context> context = i_isolate->context();
  if (context.is_null()) return {};
  i::Tagged<i::NativeContext> native_context = context->native_context();
  return Utils::ToLocal(i::direct_handle(native_context, i_isolate));
}

// TODO(ishell): rename back to GetEnteredContext().
v8::Local<v8::Context> Isolate::GetEnteredOrMicrotaskContext() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i::DirectHandle<i::NativeContext> last =
      i_isolate->handle_scope_implementer()->LastEnteredContext();
  if (last.is_null()) return {};
  return Utils::ToLocal(last);
}

v8::Local<v8::Context> Isolate::GetIncumbentContext() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i::DirectHandle<i::NativeContext> context = i_isolate->GetIncumbentContext();
  return Utils::ToLocal(context);
}

v8::MaybeLocal<v8::Data> Isolate::GetCurrentHostDefinedOptions() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i::DirectHandle<i::Script> script;
  if (!i_isolate->CurrentReferrerScript().ToHandle(&script)) return {};
  return ToApiHandle<Data>(
      i::direct_handle(script->host_defined_options(), i_isolate));
}

v8::Local<Value> Isolate::ThrowError(v8::Local<v8::String> message) {
  return ThrowException(v8::Exception::Error(message));
}

v8::Local<Value> Isolate::ThrowException(v8::Local<v8::Value> value) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  EnterV8BasicScope api_scope(i_isolate);
  i_isolate->clear_internal_exception();
  // If we're passed an empty handle, we throw an undefined exception
  // to deal more gracefully with out of memory situations.
  if (value.IsEmpty()) {
    i_isolate->Throw(i::ReadOnlyRoots(i_isolate).undefined_value());
  } else {
    i_isolate->Throw(*Utils::OpenDirectHandle(*value));
  }
  return v8::Undefined(reinterpret_cast<v8::Isolate*>(i_isolate));
}

bool Isolate::HasPendingException() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  if (i_isolate->has_exception()) {
    return true;
  }
  v8::TryCatch* try_catch_handler =
      i_isolate->thread_local_top()->try_catch_handler_;
  return try_catch_handler && try_catch_handler->HasCaught();
}

void Isolate::AddGCPrologueCallback(GCCallbackWithData callback, void* data,
                                    GCType gc_type) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->heap()->AddGCPrologueCallback(callback, gc_type, data);
}

void Isolate::RemoveGCPrologueCallback(GCCallbackWithData callback,
                                       void* data) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->heap()->RemoveGCPrologueCallback(callback, data);
}

void Isolate::AddGCEpilogueCallback(GCCallbackWithData callback, void* data,
                                    GCType gc_type) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->heap()->AddGCEpilogueCallback(callback, gc_type, data);
}

void Isolate::RemoveGCEpilogueCallback(GCCallbackWithData callback,
                                       void* data) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->heap()->RemoveGCEpilogueCallback(callback, data);
}

static void CallGCCallbackWithoutData(Isolate* v8_isolate, GCType type,
                                      GCCallbackFlags flags, void* data) {
  reinterpret_cast<Isolate::GCCallback>(data)(v8_isolate, type, flags);
}

void Isolate::AddGCPrologueCallback(GCCallback callback, GCType gc_type) {
  void* data = reinterpret_cast<void*>(callback);
  AddGCPrologueCallback(CallGCCallbackWithoutData, data, gc_type);
}

void Isolate::RemoveGCPrologueCallback(GCCallback callback) {
  void* data = reinterpret_cast<void*>(callback);
  RemoveGCPrologueCallback(CallGCCallbackWithoutData, data);
}

void Isolate::AddGCEpilogueCallback(GCCallback callback, GCType gc_type) {
  void* data = reinterpret_cast<void*>(callback);
  AddGCEpilogueCallback(CallGCCallbackWithoutData, data, gc_type);
}

void Isolate::RemoveGCEpilogueCallback(GCCallback callback) {
  void* data = reinterpret_cast<void*>(callback);
  RemoveGCEpilogueCallback(CallGCCallbackWithoutData, data);
}

void Isolate::SetEmbedderRootsHandler(EmbedderRootsHandler* handler) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->heap()->SetEmbedderRootsHandler(handler);
}

CppHeap* Isolate::GetCppHeap() const {
  const i::Isolate* i_isolate = reinterpret_cast<const i::Isolate*>(this);
  return i_isolate->heap()->cpp_heap();
}

void Isolate::SetReleaseCppHeapCallbackForTesting(
    ReleaseCppHeapCallback callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->SetReleaseCppHeapCallback(callback);
}

void Isolate::SetGetExternallyAllocatedMemoryInBytesCallback(
    GetExternallyAllocatedMemoryInBytesCallback callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->heap()->SetGetExternallyAllocatedMemoryInBytesCallback(callback);
}

void Isolate::TerminateExecution() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->stack_guard()->RequestTerminateExecution();
}

bool Isolate::IsExecutionTerminating() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  return i_isolate->is_execution_terminating();
}

void Isolate::CancelTerminateExecution() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->stack_guard()->ClearTerminateExecution();
  i_isolate->CancelTerminateExecution();
}

void Isolate::RequestInterrupt(InterruptCallback callback, void* data) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->RequestInterrupt(callback, data);
}

bool Isolate::HasPendingBackgroundTasks() {
#if V8_ENABLE_WEBASSEMBLY
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  return i::wasm::GetWasmEngine()->HasRunningCompileJob(i_isolate);
#else
  return false;
#endif  // V8_ENABLE_WEBASSEMBLY
}

void Isolate::RequestGarbageCollectionForTesting(GarbageCollectionType type) {
  Utils::ApiCheck(i::v8_flags.expose_gc,
                  "v8::Isolate::RequestGarbageCollectionForTesting",
                  "Must use --expose-gc");
  // The embedder must have entered the isolate (via `Isolate::Scope`) such that
  // the GC can get the "current" isolate from TLS.
  DCHECK_EQ(this, Isolate::TryGetCurrent());

  if (type == kMinorGarbageCollection) {
    reinterpret_cast<i::Isolate*>(this)->heap()->CollectGarbage(
        i::NEW_SPACE, i::GarbageCollectionReason::kTesting,
        kGCCallbackFlagForced);
  } else {
    DCHECK_EQ(kFullGarbageCollection, type);
    reinterpret_cast<i::Isolate*>(this)->heap()->PreciseCollectAllGarbage(
        i::GCFlag::kNoFlags, i::GarbageCollectionReason::kTesting,
        kGCCallbackFlagForced);
  }
}

void Isolate::RequestGarbageCollectionForTesting(GarbageCollectionType type,
                                                 StackState stack_state) {
  std::optional<i::EmbedderStackStateScope> stack_scope;
  if (type == kFullGarbageCollection) {
    stack_scope.emplace(reinterpret_cast<i::Isolate*>(this)->heap(),
                        i::EmbedderStackStateOrigin::kExplicitInvocation,
                        stack_state);
  }
  RequestGarbageCollectionForTesting(type);
}

Isolate* Isolate::GetCurrent() {
  i::Isolate* i_isolate = i::Isolate::Current();
  return reinterpret_cast<Isolate*>(i_isolate);
}

Isolate* Isolate::TryGetCurrent() {
  i::Isolate* i_isolate = i::Isolate::TryGetCurrent();
  return reinterpret_cast<Isolate*>(i_isolate);
}

bool Isolate::IsCurrent() const {
  return reinterpret_cast<const i::Isolate*>(this)->IsCurrent();
}

// static
Isolate* Isolate::Allocate() {
  return Isolate::Allocate(IsolateGroup::GetDefault());
}

// static
Isolate* Isolate::Allocate(const IsolateGroup& group) {
  i::IsolateGroup* isolate_group = group.isolate_group_->Acquire();
  return reinterpret_cast<Isolate*>(i::Isolate::New(isolate_group));
}

IsolateGroup Isolate::GetGroup() const {
  const i::Isolate* i_isolate = reinterpret_cast<const i::Isolate*>(this);
  return IsolateGroup(i_isolate->isolate_group()->Acquire());
}

Isolate::CreateParams::CreateParams() = default;

Isolate::CreateParams::~CreateParams() = default;

// static
// This is separate so that tests can provide a different |isolate|.
void Isolate::Initialize(Isolate* v8_isolate,
                         const v8::Isolate::CreateParams& params) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  TRACE_EVENT_CALL_STATS_SCOPED(i_isolate, "v8", "V8.IsolateInitialize");
  if (auto allocator = params.array_buffer_allocator_shared) {
    CHECK(params.array_buffer_allocator == nullptr ||
          params.array_buffer_allocator == allocator.get());
    i_isolate->set_array_buffer_allocator(allocator.get());
    i_isolate->set_array_buffer_allocator_shared(std::move(allocator));
  } else {
    CHECK_NOT_NULL(params.array_buffer_allocator);
    i_isolate->set_array_buffer_allocator(params.array_buffer_allocator);
  }
  if (params.snapshot_blob != nullptr) {
    i_isolate->set_snapshot_blob(params.snapshot_blob);
  } else {
    i_isolate->set_snapshot_blob(i::Snapshot::DefaultSnapshotBlob());
  }

  if (params.fatal_error_callback) {
    v8_isolate->SetFatalErrorHandler(params.fatal_error_callback);
  }

#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
  if (params.oom_error_callback) {
    v8_isolate->SetOOMErrorHandler(params.oom_error_callback);
  }
#if __clang__
#pragma clang diagnostic pop
#endif

  if (params.counter_lookup_callback) {
    v8_isolate->SetCounterFunction(params.counter_lookup_callback);
  }

  if (params.create_histogram_callback) {
    v8_isolate->SetCreateHistogramFunction(params.create_histogram_callback);
  }

  if (params.add_histogram_sample_callback) {
    v8_isolate->SetAddHistogramSampleFunction(
        params.add_histogram_sample_callback);
  }

  i_isolate->set_api_external_references(params.external_references);
  i_isolate->set_allow_atomics_wait(params.allow_atomics_wait);

  CppHeap* cpp_heap = params.cpp_heap;
  if (!cpp_heap) {
    cpp_heap =
        CppHeap::Create(i::V8::GetCurrentPlatform(), CppHeapCreateParams{{}})
            .release();
  }

  i_isolate->heap()->ConfigureHeap(params.constraints, cpp_heap);
  if (params.constraints.stack_limit() != nullptr) {
    uintptr_t limit =
        reinterpret_cast<uintptr_t>(params.constraints.stack_limit());
    i_isolate->stack_guard()->SetStackLimit(limit);
    i_isolate->set_stack_size(base::Stack::GetStackStart() - limit);
  }

  // TODO(v8:2487): Once we got rid of Isolate::Current(), we can remove this.
  Isolate::Scope isolate_scope(v8_isolate);
  if (i_isolate->snapshot_blob() == nullptr) {
    FATAL(
        "V8 snapshot blob was not set during initialization. This can mean "
        "that the snapshot blob file is corrupted or missing.");
  }
  if (!i::Snapshot::Initialize(i_isolate)) {
    // If snapshot data was provided and we failed to deserialize it must
    // have been corrupted.
    FATAL(
        "Failed to deserialize the V8 snapshot blob. This can mean that the "
        "snapshot blob file is corrupted or missing.");
  }

  {
    // Set up code event handlers. Needs to be after i::Snapshot::Initialize
    // because that is where we add the isolate to WasmEngine.
    auto code_event_handler = params.code_event_handler;
    if (code_event_handler) {
      v8_isolate->SetJitCodeEventHandler(kJitCodeEventEnumExisting,
                                         code_event_handler);
    }
  }

  if (!i::V8::GetCurrentPlatform()
           ->GetForegroundTaskRunner(v8_isolate)
           ->NonNestableTasksEnabled()) {
    FATAL(
        "The current platform's foreground task runner does not have "
        "non-nestable tasks enabled. The embedder must provide one.");
  }
}

Isolate* Isolate::New(const Isolate::CreateParams& params) {
  return Isolate::New(IsolateGroup::GetDefault(), params);
}

Isolate* Isolate::New(const IsolateGroup& group,
                      const Isolate::CreateParams& params) {
  Isolate* v8_isolate = Allocate(group);
  Initialize(v8_isolate, params);
  return v8_isolate;
}

void Isolate::Dispose() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  if (!Utils::ApiCheck(!i_isolate->IsInUse(), "v8::Isolate::Dispose()",
                       "Disposing the isolate that is entered by a thread")) {
    return;
  }
  i::Isolate::Delete(i_isolate);
}

void Isolate::Deinitialize() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  if (!Utils::ApiCheck(
          !i_isolate->IsInUse(), "v8::Isolate::Deinitialize()",
          "Deinitializing the isolate that is entered by a thread")) {
    return;
  }
  i::Isolate::Deinitialize(i_isolate);
}

void Isolate::Free(Isolate* isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  i::Isolate::Free(i_isolate);
}

void Isolate::DumpAndResetStats() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->DumpAndResetStats();
}

void Isolate::DiscardThreadSpecificMetadata() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->DiscardPerThreadDataForThisThread();
}

void Isolate::Enter() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->Enter();
}

void Isolate::Exit() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->Exit();
}

void Isolate::SetAbortOnUncaughtExceptionCallback(
    AbortOnUncaughtExceptionCallback callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->SetAbortOnUncaughtExceptionCallback(callback);
}

void Isolate::SetHostImportModuleDynamicallyCallback(
    HostImportModuleDynamicallyCallback callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->SetHostImportModuleDynamicallyCallback(callback);
}

void Isolate::SetHostImportModuleWithPhaseDynamicallyCallback(
    HostImportModuleWithPhaseDynamicallyCallback callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->SetHostImportModuleWithPhaseDynamicallyCallback(callback);
}

void Isolate::SetHostInitializeImportMetaObjectCallback(
    HostInitializeImportMetaObjectCallback callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->SetHostInitializeImportMetaObjectCallback(callback);
}

void Isolate::SetHostCreateShadowRealmContextCallback(
    HostCreateShadowRealmContextCallback callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->SetHostCreateShadowRealmContextCallback(callback);
}

void Isolate::SetPrepareStackTraceCallback(PrepareStackTraceCallback callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->SetPrepareStackTraceCallback(callback);
}

int Isolate::GetStackTraceLimit() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  int stack_trace_limit = 0;
  if (!i_isolate->GetStackTraceLimit(i_isolate, &stack_trace_limit)) {
    return i::v8_flags.stack_trace_limit;
  }
  return stack_trace_limit;
}

Isolate::DisallowJavascriptExecutionScope::DisallowJavascriptExecutionScope(
    Isolate* v8_isolate,
    Isolate::DisallowJavascriptExecutionScope::OnFailure on_failure)
    : v8_isolate_(v8_isolate), on_failure_(on_failure) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  switch (on_failure_) {
    case CRASH_ON_FAILURE:
      i::DisallowJavascriptExecution::Open(i_isolate, &was_execution_allowed_);
      break;
    case THROW_ON_FAILURE:
      i::ThrowOnJavascriptExecution::Open(i_isolate, &was_execution_allowed_);
      break;
    case DUMP_ON_FAILURE:
      i::DumpOnJavascriptExecution::Open(i_isolate, &was_execution_allowed_);
      break;
  }
}

Isolate::DisallowJavascriptExecutionScope::~DisallowJavascriptExecutionScope() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate_);
  switch (on_failure_) {
    case CRASH_ON_FAILURE:
      i::DisallowJavascriptExecution::Close(i_isolate, was_execution_allowed_);
      break;
    case THROW_ON_FAILURE:
      i::ThrowOnJavascriptExecution::Close(i_isolate, was_execution_allowed_);
      break;
    case DUMP_ON_FAILURE:
      i::DumpOnJavascriptExecution::Close(i_isolate, was_execution_allowed_);
      break;
  }
}

Isolate::AllowJavascriptExecutionScope::AllowJavascriptExecutionScope(
    Isolate* v8_isolate)
    : v8_isolate_(v8_isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::AllowJavascriptExecution::Open(i_isolate, &was_execution_allowed_assert_);
  i::NoThrowOnJavascriptExecution::Open(i_isolate,
                                        &was_execution_allowed_throws_);
  i::NoDumpOnJavascriptExecution::Open(i_isolate, &was_execution_allowed_dump_);
}

Isolate::AllowJavascriptExecutionScope::~AllowJavascriptExecutionScope() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate_);
  i::AllowJavascriptExecution::Close(i_isolate, was_execution_allowed_assert_);
  i::NoThrowOnJavascriptExecution::Close(i_isolate,
                                         was_execution_allowed_throws_);
  i::NoDumpOnJavascriptExecution::Close(i_isolate, was_execution_allowed_dump_);
}

Isolate::SuppressMicrotaskExecutionScope::SuppressMicrotaskExecutionScope(
    Isolate* v8_isolate, MicrotaskQueue* microtask_queue)
    : i_isolate_(reinterpret_cast<i::Isolate*>(v8_isolate)),
      microtask_queue_(microtask_queue
                           ? static_cast<i::MicrotaskQueue*>(microtask_queue)
                           : i_isolate_->default_microtask_queue()) {
  i_isolate_->thread_local_top()->IncrementCallDepth<true>(this);
  microtask_queue_->IncrementMicrotasksSuppressions();
}

Isolate::SuppressMicrotaskExecutionScope::~SuppressMicrotaskExecutionScope() {
  microtask_queue_->DecrementMicrotasksSuppressions();
  i_isolate_->thread_local_top()->DecrementCallDepth(this);
}

i::ValueHelper::InternalRepresentationType Isolate::GetDataFromSnapshotOnce(
    size_t index) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  auto list = i::Cast<i::FixedArray>(i_isolate->heap()->serialized_objects());
  return GetSerializedDataFromFixedArray(i_isolate, list, index);
}

Local<Value> Isolate::GetContinuationPreservedEmbedderData() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  return ToApiHandle<Object>(i::direct_handle(
      i_isolate->isolate_data()->continuation_preserved_embedder_data(),
      i_isolate));
#else   // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  return v8::Undefined(reinterpret_cast<v8::Isolate*>(i_isolate));
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
}

void Isolate::SetContinuationPreservedEmbedderData(Local<Value> data) {
#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  if (data.IsEmpty())
    data = v8::Undefined(reinterpret_cast<v8::Isolate*>(this));
  i_isolate->isolate_data()->set_continuation_preserved_embedder_data(
      *Utils::OpenDirectHandle(*data));
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
}

Local<Data> Isolate::GetContinuationPreservedEmbedderDataV2() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  return ToApiHandle<Data>(i::direct_handle(
      i_isolate->isolate_data()->continuation_preserved_embedder_data(),
      i_isolate));
#else   // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  return v8::Undefined(reinterpret_cast<v8::Isolate*>(i_isolate));
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
}

void Isolate::SetContinuationPreservedEmbedderDataV2(Local<Data> data) {
#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  if (data.IsEmpty()) {
    data = v8::Undefined(reinterpret_cast<v8::Isolate*>(this));
  }
  i_isolate->isolate_data()->set_continuation_preserved_embedder_data(
      *Utils::OpenDirectHandle(*data));
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
}

void Isolate::GetHeapStatistics(HeapStatistics* heap_statistics) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i::Heap* heap = i_isolate->heap();

  // Embedder may call GetHeapStatistics() from any thread. Make sure to set the
  // TLS variable to *this.
  i::SetCurrentIsolateScope set_current_isolate(i_isolate);

  heap->FreeMainThreadLinearAllocationAreas();

  // The order of acquiring memory statistics is important here. We query in
  // this order because of concurrent allocation: 1) used memory 2) committed
  // physical memory 3) committed memory. Therefore the condition used <=
  // committed physical <= committed should hold.
  heap_statistics->used_global_handles_size_ = heap->UsedGlobalHandlesSize();
  heap_statistics->total_global_handles_size_ = heap->TotalGlobalHandlesSize();
  DCHECK_LE(heap_statistics->used_global_handles_size_,
            heap_statistics->total_global_handles_size_);

  heap_statistics->used_heap_size_ = heap->SizeOfObjects();
  heap_statistics->total_physical_size_ = heap->CommittedPhysicalMemory();
  heap_statistics->total_heap_size_ = heap->CommittedMemory();

  heap_statistics->total_available_size_ = heap->Available();

  // TODO(dinfuehr): Right now used <= committed physical does not hold. Fix
  // this and add DCHECK.
  DCHECK_LE(heap_statistics->used_heap_size_,
            heap_statistics->total_heap_size_);

  heap_statistics->total_heap_size_executable_ =
      heap->CommittedMemoryExecutable();
  heap_statistics->heap_size_limit_ = heap->MaxReserved();
  // TODO(7424): There is no public API for the {WasmEngine} yet. Once such an
  // API becomes available we should report the malloced memory separately. For
  // now we just add the values, thereby over-approximating the peak slightly.
  heap_statistics->malloced_memory_ =
      i_isolate->allocator()->GetCurrentMemoryUsage() +
      i_isolate->string_table()->GetCurrentMemoryUsage();
  // On 32-bit systems backing_store_bytes() might overflow size_t temporarily
  // due to concurrent array buffer sweeping.
  heap_statistics->external_memory_ =
      i_isolate->heap()->backing_store_bytes() < SIZE_MAX
          ? static_cast<size_t>(i_isolate->heap()->backing_store_bytes())
          : SIZE_MAX;
  heap_statistics->peak_malloced_memory_ =
      i_isolate->allocator()->GetMaxMemoryUsage();
  heap_statistics->number_of_native_contexts_ = heap->NumberOfNativeContexts();
  heap_statistics->number_of_detached_contexts_ =
      heap->NumberOfDetachedContexts();
  heap_statistics->does_zap_garbage_ = i::heap::ShouldZapGarbage();

#if V8_ENABLE_WEBASSEMBLY
  heap_statistics->malloced_memory_ +=
      i::wasm::GetWasmEngine()->allocator()->GetCurrentMemoryUsage();
  heap_statistics->peak_malloced_memory_ +=
      i::wasm::GetWasmEngine()->allocator()->GetMaxMemoryUsage();
#endif  // V8_ENABLE_WEBASSEMBLY
}

size_t Isolate::NumberOfHeapSpaces() {
  return i::LAST_SPACE - i::FIRST_SPACE + 1;
}

bool Isolate::GetHeapSpaceStatistics(HeapSpaceStatistics* space_statistics,
                                     size_t index) {
  if (!space_statistics) return false;

  // Embedder may call GetHeapSpaceStatistics() from any thread. Make sure to
  // set the TLS variable to *this.
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i::SetCurrentIsolateScope set_current_isolate(i_isolate);

  if (!i::Heap::IsValidAllocationSpace(static_cast<i::AllocationSpace>(index)))
    return false;

  i::Heap* heap = i_isolate->heap();

  heap->FreeMainThreadLinearAllocationAreas();

  i::AllocationSpace allocation_space = static_cast<i::AllocationSpace>(index);
  space_statistics->space_name_ = i::ToString(allocation_space);

  if (allocation_space == i::RO_SPACE) {
    // RO_SPACE memory is shared across all isolates and accounted for
    // elsewhere.
    space_statistics->space_size_ = 0;
    space_statistics->space_used_size_ = 0;
    space_statistics->space_available_size_ = 0;
    space_statistics->physical_space_size_ = 0;
  } else {
    i::Space* space = heap->space(static_cast<int>(index));
    space_statistics->space_size_ = space ? space->CommittedMemory() : 0;
    space_statistics->space_used_size_ = space ? space->SizeOfObjects() : 0;
    space_statistics->space_available_size_ = space ? space->Available() : 0;
    space_statistics->physical_space_size_ =
        space ? space->CommittedPhysicalMemory() : 0;
  }
  return true;
}

size_t Isolate::NumberOfTrackedHeapObjectTypes() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i::Heap* heap = i_isolate->heap();
  return heap->NumberOfTrackedHeapObjectTypes();
}

bool Isolate::GetHeapObjectStatisticsAtLastGC(
    HeapObjectStatistics* object_statistics, size_t type_index) {
  if (!object_statistics) return false;
  if (V8_LIKELY(!i::TracingFlags::is_gc_stats_enabled())) return false;

  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  // Embedder may call GetHeapObjectStatisticsAtLastGC() from any thread. Make
  // sure to set the TLS variable to *this.
  i::SetCurrentIsolateScope set_current_isolate(i_isolate);

  i::Heap* heap = i_isolate->heap();
  if (type_index >= heap->NumberOfTrackedHeapObjectTypes()) return false;

  const char* object_type;
  const char* object_sub_type;
  size_t object_count = heap->ObjectCountAtLastGC(type_index);
  size_t object_size = heap->ObjectSizeAtLastGC(type_index);
  if (!heap->GetObjectTypeName(type_index, &object_type, &object_sub_type)) {
    // There should be no objects counted when the type is unknown.
    DCHECK_EQ(object_count, 0U);
    DCHECK_EQ(object_size, 0U);
    return false;
  }

  object_statistics->object_type_ = object_type;
  object_statistics->object_sub_type_ = object_sub_type;
  object_statistics->object_count_ = object_count;
  object_statistics->object_size_ = object_size;
  return true;
}

bool Isolate::GetHeapCodeAndMetadataStatistics(
    HeapCodeStatistics* code_statistics) {
  if (!code_statistics) return false;

  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->heap()->CollectCodeStatistics();

  code_statistics->code_and_metadata_size_ =
      i_isolate->code_and_metadata_size();
  code_statistics->bytecode_and_metadata_size_ =
      i_isolate->bytecode_and_metadata_size();
  code_statistics->external_script_source_size_ =
      i_isolate->external_script_source_size();
  code_statistics->cpu_profiler_metadata_size_ =
      i::CpuProfiler::GetAllProfilersMemorySize(i_isolate);

  return true;
}

bool Isolate::MeasureMemory(std::unique_ptr<MeasureMemoryDelegate> delegate,
                            MeasureMemoryExecution execution) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  return i_isolate->heap()->MeasureMemory(std::move(delegate), execution);
}

std::unique_ptr<MeasureMemoryDelegate> MeasureMemoryDelegate::Default(
    Isolate* v8_isolate, Local<Context> context,
    Local<Promise::Resolver> promise_resolver, MeasureMemoryMode mode) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  return i_isolate->heap()->CreateDefaultMeasureMemoryDelegate(
      context, promise_resolver, mode);
}

void Isolate::GetStackSample(const RegisterState& state, void** frames,
                             size_t frames_limit, SampleInfo* sample_info) {
  RegisterState regs = state;
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  if (i::TickSample::GetStackSample(i_isolate, &regs,
                                    i::TickSample::kSkipCEntryFrame, frames,
                                    frames_limit, sample_info)) {
    return;
  }
  sample_info->frames_count = 0;
  sample_info->vm_state = OTHER;
  sample_info->external_callback_entry = nullptr;
}

int64_t Isolate::AdjustAmountOfExternalAllocatedMemory(
    int64_t change_in_bytes) {
  return AdjustAmountOfExternalAllocatedMemoryImpl(change_in_bytes);
}

void Isolate::SetEventLogger(LogEventCallback that) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->set_event_logger(that);
}

void Isolate::AddBeforeCallEnteredCallback(BeforeCallEnteredCallback callback) {
  if (callback == nullptr) return;
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->AddBeforeCallEnteredCallback(callback);
}

void Isolate::RemoveBeforeCallEnteredCallback(
    BeforeCallEnteredCallback callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->RemoveBeforeCallEnteredCallback(callback);
}

void Isolate::AddCallCompletedCallback(CallCompletedCallback callback) {
  if (callback == nullptr) return;
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->AddCallCompletedCallback(callback);
}

void Isolate::RemoveCallCompletedCallback(CallCompletedCallback callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->RemoveCallCompletedCallback(callback);
}

void Isolate::SetPromiseHook(PromiseHook hook) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->SetPromiseHook(hook);
}

void Isolate::SetPromiseRejectCallback(PromiseRejectCallback callback) {
  if (callback == nullptr) return;
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->SetPromiseRejectCallback(callback);
}

void Isolate::SetExceptionPropagationCallback(
    ExceptionPropagationCallback callback) {
  if (callback == nullptr) return;
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->SetExceptionPropagationCallback(callback);
}

void Isolate::PerformMicrotaskCheckpoint() {
  DCHECK_NE(MicrotasksPolicy::kScoped, GetMicrotasksPolicy());
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->default_microtask_queue()->PerformCheckpoint(this);
}

void Isolate::EnqueueMicrotask(Local<Function> v8_function) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  auto function = Utils::OpenDirectHandle(*v8_function);
  i::DirectHandle<i::NativeContext> handler_context;
  if (!i::JSReceiver::GetContextForMicrotask(function).ToHandle(
          &handler_context))
    handler_context = i_isolate->native_context();
  MicrotaskQueue* microtask_queue = handler_context->microtask_queue();
  if (microtask_queue) microtask_queue->EnqueueMicrotask(this, v8_function);
}

void Isolate::EnqueueMicrotask(MicrotaskCallback callback, void* data) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->default_microtask_queue()->EnqueueMicrotask(this, callback, data);
}

void Isolate::SetMicrotasksPolicy(MicrotasksPolicy policy) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->default_microtask_queue()->set_microtasks_policy(policy);
}

MicrotasksPolicy Isolate::GetMicrotasksPolicy() const {
  i::Isolate* i_isolate =
      reinterpret_cast<i::Isolate*>(const_cast<Isolate*>(this));
  return i_isolate->default_microtask_queue()->microtasks_policy();
}

void Isolate::AddMicrotasksCompletedCallback(
    MicrotasksCompletedCallbackWithData callback, void* data) {
  DCHECK(callback);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->default_microtask_queue()->AddMicrotasksCompletedCallback(callback,
                                                                       data);
}

void Isolate::RemoveMicrotasksCompletedCallback(
    MicrotasksCompletedCallbackWithData callback, void* data) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->default_microtask_queue()->RemoveMicrotasksCompletedCallback(
      callback, data);
}

void Isolate::SetUseCounterCallback(UseCounterCallback callback) {
  reinterpret_cast<i::Isolate*>(this)->SetUseCounterCallback(callback);
}

void Isolate::SetCounterFunction(CounterLookupCallback callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->counters()->ResetCounterFunction(callback);
}

void Isolate::SetCreateHistogramFunction(CreateHistogramCallback callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->counters()->ResetCreateHistogramFunction(callback);
}

void Isolate::SetAddHistogramSampleFunction(
    AddHistogramSampleCallback callback) {
  reinterpret_cast<i::Isolate*>(this)
      ->counters()
      ->SetAddHistogramSampleFunction(callback);
}

void Isolate::SetMetricsRecorder(
    const std::shared_ptr<metrics::Recorder>& metrics_recorder) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->metrics_recorder()->SetEmbedderRecorder(i_isolate,
                                                     metrics_recorder);
}

void Isolate::SetAddCrashKeyCallback(AddCrashKeyCallback callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->SetAddCrashKeyCallback(callback);
}

void Isolate::LowMemoryNotification() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  {
    i::NestedTimedHistogramScope idle_notification_scope(
        i_isolate->counters()->gc_low_memory_notification());
    TRACE_EVENT0("v8", "V8.GCLowMemoryNotification");
    i_isolate->heap()->CollectAllAvailableGarbage(
        i::GarbageCollectionReason::kLowMemoryNotification);
  }
}

int Isolate::ContextDisposedNotification(bool dependant_context) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  if (V8_UNLIKELY(i::v8_flags.trace_context_disposal)) {
    i_isolate->PrintWithTimestamp("[context-disposal] Disposing %s context\n",
                                  dependant_context ? "nested" : "top-level");
  }
#if V8_ENABLE_WEBASSEMBLY
  if (!dependant_context) {
    if (!i_isolate->context().is_null()) {
      // We left the current context, we can abort all WebAssembly compilations
      // of that context.
      // A handle scope for the native context.
      i::HandleScope handle_scope(i_isolate);
      i::wasm::GetWasmEngine()->DeleteCompileJobsOnContext(
          i_isolate->native_context());
    }
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  i_isolate->AbortConcurrentOptimization(i::BlockingBehavior::kDontBlock);
  return i_isolate->heap()->NotifyContextDisposed(dependant_context);
}

void Isolate::ContextDisposedNotification(ContextDependants dependants) {
  // TODO(mlippautz): Replace implementation with the old version of
  // ContextDisposedNotification() that still has a return parameter.
  START_ALLOW_USE_DEPRECATED()
  ContextDisposedNotification(dependants == ContextDependants::kSomeDependants);
  END_ALLOW_USE_DEPRECATED()
}

void Isolate::IsolateInForegroundNotification() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  return i_isolate->SetPriority(Priority::kUserBlocking);
}

void Isolate::IsolateInBackgroundNotification() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  return i_isolate->SetPriority(Priority::kBestEffort);
}

void Isolate::SetPriority(Priority priority) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  return i_isolate->SetPriority(priority);
}

void Isolate::MemoryPressureNotification(MemoryPressureLevel level) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  bool on_isolate_thread =
      i_isolate->was_locker_ever_used()
          ? i_isolate->thread_manager()->IsLockedByCurrentThread()
          : i::ThreadId::Current() == i_isolate->thread_id();
  i_isolate->heap()->MemoryPressureNotification(level, on_isolate_thread);
}

void Isolate::SetBatterySaverMode(bool battery_saver_mode_enabled) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->set_battery_saver_mode_enabled(battery_saver_mode_enabled);
}

void Isolate::SetMemorySaverMode(bool memory_saver_mode_enabled) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->set_memory_saver_mode_enabled(memory_saver_mode_enabled);
}

void Isolate::ClearCachesForTesting() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->AbortConcurrentOptimization(i::BlockingBehavior::kBlock);
  i_isolate->ClearSerializerData();
  i_isolate->compilation_cache()->Clear();
}

void Isolate::SetIsLoading(bool is_loading) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->SetIsLoading(is_loading);
}

void Isolate::Freeze(bool is_frozen) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->Freeze(is_frozen);
}

void Isolate::IncreaseHeapLimitForDebugging() {
  // No-op.
}

void Isolate::RestoreOriginalHeapLimit() {
  // No-op.
}

bool Isolate::IsHeapLimitIncreasedForDebugging() { return false; }

void Isolate::SetJitCodeEventHandler(JitCodeEventOptions options,
                                     JitCodeEventHandler event_handler) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  // Ensure that logging is initialized for our isolate.
  i_isolate->InitializeLoggingAndCounters();
  i_isolate->v8_file_logger()->SetCodeEventHandler(options, event_handler);
}

void Isolate::SetStackLimit(uintptr_t stack_limit) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  CHECK(stack_limit);
  i_isolate->stack_guard()->SetStackLimit(stack_limit);
  i_isolate->set_stack_size(base::Stack::GetStackStart() - stack_limit);
}

void Isolate::GetCodeRange(void** start, size_t* length_in_bytes) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  const base::AddressRegion& code_region = i_isolate->heap()->code_region();
  *start = reinterpret_cast<void*>(code_region.begin());
  *length_in_bytes = code_region.size();
}

void Isolate::GetEmbeddedCodeRange(const void** start,
                                   size_t* length_in_bytes) {
  // Note, we should return the embedded code rande from the .text section here.
  i::EmbeddedData d = i::EmbeddedData::FromBlob();
  *start = reinterpret_cast<const void*>(d.code());
  *length_in_bytes = d.code_size();
}

JSEntryStubs Isolate::GetJSEntryStubs() {
  JSEntryStubs entry_stubs;

  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  std::array<std::pair<i::Builtin, JSEntryStub*>, 3> stubs = {
      {{i::Builtin::kJSEntry, &entry_stubs.js_entry_stub},
       {i::Builtin::kJSConstructEntry, &entry_stubs.js_construct_entry_stub},
       {i::Builtin::kJSRunMicrotasksEntry,
        &entry_stubs.js_run_microtasks_entry_stub}}};
  for (auto& pair : stubs) {
    i::Tagged<i::Code> js_entry = i_isolate->builtins()->code(pair.first);
    pair.second->code.start =
        reinterpret_cast<const void*>(js_entry->instruction_start());
    pair.second->code.length_in_bytes = js_entry->instruction_size();
  }

  return entry_stubs;
}

size_t Isolate::CopyCodePages(size_t capacity, MemoryRange* code_pages_out) {
#if !defined(V8_TARGET_ARCH_64_BIT) && !defined(V8_TARGET_ARCH_ARM)
  // Not implemented on other platforms.
  UNREACHABLE();
#else

  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  std::vector<MemoryRange>* code_pages = i_isolate->GetCodePages();

  DCHECK_NOT_NULL(code_pages);

  // Copy as many elements into the output vector as we can. If the
  // caller-provided buffer is not big enough, we fill it, and the caller can
  // provide a bigger one next time. We do it this way because allocation is not
  // allowed in signal handlers.
  size_t limit = std::min(capacity, code_pages->size());
  for (size_t i = 0; i < limit; i++) {
    code_pages_out[i] = code_pages->at(i);
  }
  return code_pages->size();
#endif
}

#define CALLBACK_SETTER(ExternalName, Type, InternalName)        \
  void Isolate::Set##ExternalName(Type callback) {               \
    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this); \
    i_isolate->set_##InternalName(callback);                     \
  }

CALLBACK_SETTER(FatalErrorHandler, FatalErrorCallback, exception_behavior)
CALLBACK_SETTER(OOMErrorHandler, OOMErrorCallback, oom_behavior)
CALLBACK_SETTER(ModifyCodeGenerationFromStringsCallback,
                ModifyCodeGenerationFromStringsCallback2,
                modify_code_gen_callback)
CALLBACK_SETTER(AllowWasmCodeGenerationCallback,
                AllowWasmCodeGenerationCallback, allow_wasm_code_gen_callback)

CALLBACK_SETTER(WasmModuleCallback, ExtensionCallback, wasm_module_callback)
CALLBACK_SETTER(WasmInstanceCallback, ExtensionCallback, wasm_instance_callback)

CALLBACK_SETTER(WasmStreamingCallback, WasmStreamingCallback,
                wasm_streaming_callback)

CALLBACK_SETTER(WasmAsyncResolvePromiseCallback,
                WasmAsyncResolvePromiseCallback,
                wasm_async_resolve_promise_callback)

CALLBACK_SETTER(WasmLoadSourceMapCallback, WasmLoadSourceMapCallback,
                wasm_load_source_map_callback)

CALLBACK_SETTER(WasmImportedStringsEnabledCallback,
                WasmImportedStringsEnabledCallback,
                wasm_imported_strings_enabled_callback)

CALLBACK_SETTER(WasmJSPIEnabledCallback, WasmJSPIEnabledCallback,
                wasm_jspi_enabled_callback)

CALLBACK_SETTER(SharedArrayBufferConstructorEnabledCallback,
                SharedArrayBufferConstructorEnabledCallback,
                sharedarraybuffer_constructor_enabled_callback)

CALLBACK_SETTER(IsJSApiWrapperNativeErrorCallback,
                IsJSApiWrapperNativeErrorCallback,
                is_js_api_wrapper_native_error_callback)

void Isolate::InstallConditionalFeatures(Local<Context> context) {
  v8::HandleScope handle_scope(this);
  v8::Context::Scope context_scope(context);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  if (i_isolate->is_execution_terminating()) return;
  i_isolate->InstallConditionalFeatures(Utils::OpenDirectHandle(*context));
  if (i_isolate->has_exception()) return;
#if V8_ENABLE_WEBASSEMBLY
  i::WasmJs::InstallConditionalFeatures(i_isolate,
                                        Utils::OpenDirectHandle(*context));
#endif  // V8_ENABLE_WEBASSEMBLY
}

void Isolate::AddNearHeapLimitCallback(v8::NearHeapLimitCallback callback,
                                       void* data) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->heap()->AddNearHeapLimitCallback(callback, data);
}

void Isolate::RemoveNearHeapLimitCallback(v8::NearHeapLimitCallback callback,
                                          size_t heap_limit) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->heap()->RemoveNearHeapLimitCallback(callback, heap_limit);
}

void Isolate::AutomaticallyRestoreInitialHeapLimit(double threshold_percent) {
  DCHECK_GT(threshold_percent, 0.0);
  DCHECK_LT(threshold_percent, 1.0);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->heap()->AutomaticallyRestoreInitialHeapLimit(threshold_percent);
}

bool Isolate::IsDead() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  return i_isolate->IsDead();
}

bool Isolate::AddMessageListener(MessageCallback callback, Local<Value> data) {
  return AddMessageListenerWithErrorLevel(callback, kMessageError, data);
}

bool Isolate::AddMessageListenerWithErrorLevel(MessageCallback callback,
                                               int message_levels,
                                               Local<Value> data) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScope scope(i_isolate);
  i::DirectHandle<i::ArrayList> list =
      i_isolate->factory()->message_listeners();
  i::DirectHandle<i::FixedArray> listener =
      i_isolate->factory()->NewFixedArray(3);

  i::DirectHandle<i::Object> callback_obj =
      FromCData<internal::kMessageListenerTag>(i_isolate, callback);

  listener->set(0, *callback_obj);
  listener->set(1, data.IsEmpty()
                       ? i::ReadOnlyRoots(i_isolate).undefined_value()
                       : *Utils::OpenDirectHandle(*data));
  listener->set(2, i::Smi::FromInt(message_levels));
  list = i::ArrayList::Add(i_isolate, list, listener);
  i_isolate->heap()->SetMessageListeners(*list);
  return true;
}

void Isolate::RemoveMessageListeners(MessageCallback callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScope scope(i_isolate);
  i::DisallowGarbageCollection no_gc;
  i::Tagged<i::ArrayList> listeners = i_isolate->heap()->message_listeners();
  i::ReadOnlyRoots roots(i_isolate);
  for (int i = 0; i < listeners->length(); i++) {
    if (i::IsUndefined(listeners->get(i), roots)) {
      continue;  // skip deleted ones
    }
    i::Tagged<i::FixedArray> listener =
        i::Cast<i::FixedArray>(listeners->get(i));
    v8::MessageCallback cur_callback =
        v8::ToCData<v8::MessageCallback, i::kMessageListenerTag>(
            i_isolate, listener->get(0));
    if (cur_callback == callback) {
      listeners->set(i, roots.undefined_value());
    }
  }
}

void Isolate::SetFailedAccessCheckCallbackFunction(
    FailedAccessCheckCallback callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->SetFailedAccessCheckCallback(callback);
}

void Isolate::SetCaptureStackTraceForUncaughtExceptions(
    bool capture, int frame_limit, StackTrace::StackTraceOptions options) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->SetCaptureStackTraceForUncaughtExceptions(capture, frame_limit,
                                                       options);
}

bool Isolate::IsInUse() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  return i_isolate->IsInUse();
}

void Isolate::SetAllowAtomicsWait(bool allow) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->set_allow_atomics_wait(allow);
}

void v8::Isolate::DateTimeConfigurationChangeNotification(
    TimeZoneDetection time_zone_detection) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  ApiRuntimeCallStatsScope rcs_scope(
      i_isolate, RCCId::kAPI_Isolate_DateTimeConfigurationChangeNotification);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i_isolate->date_cache()->ResetDateCache(
      static_cast<base::TimezoneCache::TimeZoneDetection>(time_zone_detection));
  i_isolate->IncreaseDateCacheStampAndInvalidateProtector();
#ifdef V8_INTL_SUPPORT
  i_isolate->clear_cached_icu_object(
      i::Isolate::ICUObjectCacheType::kDefaultSimpleDateFormat);
  i_isolate->clear_cached_icu_object(
      i::Isolate::ICUObjectCacheType::kDefaultSimpleDateFormatForTime);
  i_isolate->clear_cached_icu_object(
      i::Isolate::ICUObjectCacheType::kDefaultSimpleDateFormatForDate);
#endif  // V8_INTL_SUPPORT
}

void v8::Isolate::LocaleConfigurationChangeNotification() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  ApiRuntimeCallStatsScope rcs_scope(
      i_isolate, RCCId::kAPI_Isolate_LocaleConfigurationChangeNotification);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);

#ifdef V8_INTL_SUPPORT
  i_isolate->ResetDefaultLocale();
#endif  // V8_INTL_SUPPORT
}

std::string Isolate::GetDefaultLocale() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);

#ifdef V8_INTL_SUPPORT
  return i_isolate->DefaultLocale();
#else
  return std::string();
#endif
}

Maybe<std::string> Isolate::ValidateAndCanonicalizeUnicodeLocaleId(
    std::string_view tag) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  EnterV8NoScriptScope<> api_scope{
      i_isolate, RCCId::kAPI_Isolate_ValidateAndCanonicalizeUnicodeLocaleId};
#ifdef V8_INTL_SUPPORT
  return i::Intl::ValidateAndCanonicalizeUnicodeLocaleId(i_isolate, tag);
#else
  THROW_NEW_ERROR(
      i_isolate,
      NewRangeError(i::MessageTemplate::kInvalidLanguageTag,
                    i_isolate->factory()->NewStringFromAsciiChecked(tag)));
#endif
}

uint64_t Isolate::GetHashSeed() {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  return i::HashSeed(i_isolate).seed();
}

#if defined(V8_ENABLE_ETW_STACK_WALKING)
void Isolate::SetFilterETWSessionByURLCallback(
    FilterETWSessionByURLCallback callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->SetFilterETWSessionByURLCallback(callback);
}

void Isolate::SetFilterETWSessionByURL2Callback(
    FilterETWSessionByURL2Callback callback) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(this);
  i_isolate->SetFilterETWSessionByURL2Callback(callback);
}
#endif  // V8_ENABLE_ETW_STACK_WALKING

bool v8::Object::IsCodeLike(v8::Isolate* v8_isolate) const {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_Object_IsCodeLike);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScope scope(i_isolate);
  return Utils::OpenDirectHandle(this)->IsCodeLike(i_isolate);
}

// static
std::unique_ptr<MicrotaskQueue> MicrotaskQueue::New(Isolate* v8_isolate,
                                                    MicrotasksPolicy policy) {
  auto microtask_queue =
      i::MicrotaskQueue::New(reinterpret_cast<i::Isolate*>(v8_isolate));
  microtask_queue->set_microtasks_policy(policy);
  std::unique_ptr<MicrotaskQueue> ret(std::move(microtask_queue));
  return ret;
}

MicrotasksScope::MicrotasksScope(Local<Context> v8_context,
                                 MicrotasksScope::Type type)
    : MicrotasksScope(v8::Isolate::GetCurrent(),
                      v8_context->GetMicrotaskQueue(), type) {}

MicrotasksScope::MicrotasksScope(Isolate* v8_isolate,
                                 MicrotaskQueue* microtask_queue,
                                 MicrotasksScope::Type type)
    : i_isolate_(reinterpret_cast<i::Isolate*>(v8_isolate)),
      microtask_queue_(microtask_queue
                           ? static_cast<i::MicrotaskQueue*>(microtask_queue)
                           : i_isolate_->default_microtask_queue()),
      run_(type == MicrotasksScope::kRunMicrotasks) {
  if (run_) microtask_queue_->IncrementMicrotasksScopeDepth();
#ifdef DEBUG
  if (!run_) microtask_queue_->IncrementDebugMicrotasksScopeDepth();
#endif
}

MicrotasksScope::~MicrotasksScope() {
  if (run_) {
    microtask_queue_->DecrementMicrotasksScopeDepth();
    if (MicrotasksPolicy::kScoped == microtask_queue_->microtasks_policy() &&
        !i_isolate_->has_exception()) {
      microtask_queue_->PerformCheckpoint(
          reinterpret_cast<Isolate*>(i_isolate_));
      DCHECK_IMPLIES(i_isolate_->has_exception(),
                     i_isolate_->is_execution_terminating());
    }
  }
#ifdef DEBUG
  if (!run_) microtask_queue_->DecrementDebugMicrotasksScopeDepth();
#endif
}

// static
void MicrotasksScope::PerformCheckpoint(Isolate* v8_isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  auto* microtask_queue = i_isolate->default_microtask_queue();
  microtask_queue->PerformCheckpoint(v8_isolate);
}

// static
int MicrotasksScope::GetCurrentDepth(Isolate* v8_isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  auto* microtask_queue = i_isolate->default_microtask_queue();
  return microtask_queue->GetMicrotasksScopeDepth();
}

// static
bool MicrotasksScope::IsRunningMicrotasks(Isolate* v8_isolate) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  auto* microtask_queue = i_isolate->default_microtask_queue();
  return microtask_queue->IsRunningMicrotasks();
}

String::Utf8Value::Utf8Value(v8::Isolate* v8_isolate, v8::Local<v8::Value> obj)
    : str_(nullptr), length_(0) {
  if (obj.IsEmpty()) return;
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  Local<Context> context = v8_isolate->GetCurrentContext();
  EnterV8BasicScope api_scope(i_isolate);
  i::HandleScope scope(i_isolate);
  TryCatch try_catch(v8_isolate);
  Local<String> str;
  if (!obj->ToString(context).ToLocal(&str)) return;
  length_ = str->Utf8LengthV2(v8_isolate);
  str_ = i::NewArray<char>(length_ + 1);
  int flags = String::WriteFlags::kNullTerminate |
              String::WriteFlags::kReplaceInvalidUtf8;
  str->WriteUtf8V2(v8_isolate, str_, length_ + 1, flags);
}

String::Utf8Value::~Utf8Value() { i::DeleteArray(str_); }

String::Value::Value(v8::Isolate* v8_isolate, v8::Local<v8::Value> obj)
    : str_(nullptr), length_(0) {
  if (obj.IsEmpty()) return;
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::HandleScope scope(i_isolate);
  Local<Context> context = v8_isolate->GetCurrentContext();
  EnterV8BasicScope api_scope(i_isolate);
  TryCatch try_catch(v8_isolate);
  Local<String> str;
  if (!obj->ToString(context).ToLocal(&str)) return;
  length_ = str->Length();
  str_ = i::NewArray<uint16_t>(length_ + 1);
  str->WriteV2(v8_isolate, 0, length_, str_,
               String::WriteFlags::kNullTerminate);
}

String::Value::~Value() { i::DeleteArray(str_); }

String::ValueView::ValueView(v8::Isolate* v8_isolate,
                             v8::Local<v8::String> str) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  i::DirectHandle<i::String> i_str = Utils::OpenDirectHandle(*str);

  // If the underlying string is flat, we can access its content directly.
  // Otherwise, we need to create a handle scope to flatten the string.
  if (i_str->IsFlat()) {
    i::DisallowGarbageCollectionInRelease* no_gc =
        new (no_gc_debug_scope_) i::DisallowGarbageCollectionInRelease();
    i::String::FlatContent flat_content = i_str->GetFlatContent(*no_gc);
    flat_str_ = str;
    is_one_byte_ = flat_content.IsOneByte();
    length_ = flat_content.length();
    if (is_one_byte_) {
      data8_ = flat_content.ToOneByteVector().data();
    } else {
      data16_ = flat_content.ToUC16Vector().data();
    }
  } else {
    i::HandleScope scope(i_isolate);
    i::DirectHandle<i::String> i_flat_str =
        i::String::Flatten(i_isolate, i_str);
    flat_str_ = Utils::ToLocal(i_flat_str);
    i::DisallowGarbageCollectionInRelease* no_gc =
        new (no_gc_debug_scope_) i::DisallowGarbageCollectionInRelease();
    i::String::FlatContent flat_content = i_flat_str->GetFlatContent(*no_gc);
    DCHECK(flat_content.IsFlat());
    is_one_byte_ = flat_content.IsOneByte();
    length_ = flat_content.length();
    if (is_one_byte_) {
      data8_ = flat_content.ToOneByteVector().data();
    } else {
      data16_ = flat_content.ToUC16Vector().data();
    }
  }
}

String::ValueView::~ValueView() {
  using i::DisallowGarbageCollectionInRelease;
  DisallowGarbageCollectionInRelease* no_gc =
      reinterpret_cast<DisallowGarbageCollectionInRelease*>(no_gc_debug_scope_);
  no_gc->~DisallowGarbageCollectionInRelease();
}

void String::ValueView::CheckOneByte(bool is_one_byte) const {
  if (is_one_byte) {
    Utils::ApiCheck(is_one_byte_, "v8::String::ValueView::data8",
                    "Called the one-byte accessor on a two-byte string view.");
  } else {
    Utils::ApiCheck(!is_one_byte_, "v8::String::ValueView::data16",
                    "Called the two-byte accessor on a one-byte string view.");
  }
}

#define DEFINE_ERROR(NAME, name)                                              \
  Local<Value> Exception::NAME(v8::Local<v8::String> raw_message,             \
                               v8::Local<v8::Value> raw_options) {            \
    i::Isolate* i_isolate = i::Isolate::Current();                            \
    ApiRuntimeCallStatsScope rcs_scope(i_isolate, RCCId::kAPI_##NAME##_New);  \
    EnterV8NoScriptNoExceptionScope api_scope(i_isolate);                     \
    i::Tagged<i::Object> error;                                               \
    {                                                                         \
      i::HandleScope scope(i_isolate);                                        \
      i::DirectHandle<i::Object> options;                                     \
      if (!raw_options.IsEmpty()) {                                           \
        options = Utils::OpenDirectHandle(*raw_options);                      \
      }                                                                       \
      auto message = Utils::OpenDirectHandle(*raw_message);                   \
      i::DirectHandle<i::JSFunction> constructor =                            \
          i_isolate->name##_function();                                       \
      error = *i_isolate->factory()->NewError(constructor, message, options); \
    }                                                                         \
    return Utils::ToLocal(i::direct_handle(error, i_isolate));                \
  }

DEFINE_ERROR(RangeError, range_error)
DEFINE_ERROR(ReferenceError, reference_error)
DEFINE_ERROR(SyntaxError, syntax_error)
DEFINE_ERROR(TypeError, type_error)
DEFINE_ERROR(WasmCompileError, wasm_compile_error)
DEFINE_ERROR(WasmLinkError, wasm_link_error)
DEFINE_ERROR(WasmRuntimeError, wasm_runtime_error)
DEFINE_ERROR(WasmSuspendError, wasm_suspend_error)
DEFINE_ERROR(Error, error)

#undef DEFINE_ERROR

Local<Message> Exception::CreateMessage(Isolate* v8_isolate,
                                        Local<Value> exception) {
  auto obj = Utils::OpenHandle(*exception);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  i::HandleScope scope(i_isolate);
  return Utils::MessageToLocal(
      scope.CloseAndEscape(i_isolate->CreateMessage(obj, nullptr)));
}

Local<StackTrace> Exception::GetStackTrace(Local<Value> exception) {
  auto obj = Utils::OpenDirectHandle(*exception);
  if (!IsJSObject(*obj)) return {};
  auto js_obj = i::Cast<i::JSObject>(obj);
  i::Isolate* i_isolate = i::Isolate::Current();
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  auto stack_trace = i_isolate->GetDetailedStackTrace(js_obj);
  return Utils::StackTraceToLocal(stack_trace);
}

Maybe<bool> Exception::CaptureStackTrace(Local<Context> context,
                                         Local<Object> object) {
  auto i_isolate = i::Isolate::Current();
  EnterV8NoScriptScope<> api_scope{i_isolate, context,
                                   RCCId::kAPI_Exception_CaptureStackTrace};
  auto obj = Utils::OpenHandle(*object);
  if (!IsJSObject(*obj)) return Just(false);

  auto js_obj = i::Cast<i::JSObject>(obj);

  i::FrameSkipMode mode = i::FrameSkipMode::SKIP_FIRST;

  auto result = i::ErrorUtils::CaptureStackTrace(i_isolate, js_obj, mode, {});

  i::DirectHandle<i::Object> handle;
  if (!result.ToHandle(&handle)) return {};
  return Just(true);
}

v8::MaybeLocal<v8::Array> v8::Object::PreviewEntries(bool* is_key_value) {
  auto object = Utils::OpenDirectHandle(this);
  i::Isolate* i_isolate = i::Isolate::Current();
  if (i_isolate->is_execution_terminating()) return {};
  if (IsMap()) {
    *is_key_value = true;
    return Map::Cast(this)->AsArray();
  }
  if (IsSet()) {
    *is_key_value = false;
    return Set::Cast(this)->AsArray();
  }

  Isolate* v8_isolate = reinterpret_cast<Isolate*>(i_isolate);
  EnterV8NoScriptNoExceptionScope api_scope(i_isolate);
  if (i::IsJSWeakCollection(*object)) {
    *is_key_value = IsJSWeakMap(*object);
    return Utils::ToLocal(i::JSWeakCollection::GetEntries(
        i::Cast<i::JSWeakCollection>(object), 0));
  }
  if (i::IsJSMapIterator(*object)) {
    auto it = i::Cast<i::JSMapIterator>(object);
    MapAsArrayKind const kind =
        static_cast<MapAsArrayKind>(it->map()->instance_type());
    *is_key_value = kind == MapAsArrayKind::kEntries;
    if (!it->HasMore()) return v8::Array::New(v8_isolate);
    return Utils::ToLocal(
        MapAsArray(i_isolate, it->table(), i::Smi::ToInt(it->index()), kind));
  }
  if (i::IsJSSetIterator(*object)) {
    auto it = i::Cast<i::JSSetIterator>(object);
    SetAsArrayKind const kind =
        static_cast<SetAsArrayKind>(it->map()->instance_type());
    *is_key_value = kind == SetAsArrayKind::kEntries;
    if (!it->HasMore()) return v8::Array::New(v8_isolate);
    return Utils::ToLocal(
        SetAsArray(i_isolate, it->table(), i::Smi::ToInt(it->index()), kind));
  }
  return {};
}

Local<String> CpuProfileNode::GetFunctionName() const {
  const i::ProfileNode* node = reinterpret_cast<const i::ProfileNode*>(this);
  i::Isolate* i_isolate = node->isolate();
  const i::CodeEntry* entry = node->entry();
  i::DirectHandle<i::String> name =
      i_isolate->factory()->InternalizeUtf8String(entry->name());
  return ToApiHandle<String>(name);
}

const char* CpuProfileNode::GetFunctionNameStr() const {
  const i::ProfileNode* node = reinterpret_cast<const i::ProfileNode*>(this);
  return node->entry()->name();
}

int CpuProfileNode::GetScriptId() const {
  const i::ProfileNode* node = reinterpret_cast<const i::ProfileNode*>(this);
  const i::CodeEntry* entry = node->entry();
  return entry->script_id();
}

Local<String> CpuProfileNode::GetScriptResourceName() const {
  const i::ProfileNode* node = reinterpret_cast<const i::ProfileNode*>(this);
  i::Isolate* i_isolate = node->isolate();
  return ToApiHandle<String>(i_isolate->factory()->InternalizeUtf8String(
      node->entry()->resource_name()));
}

const char* CpuProfileNode::GetScriptResourceNameStr() const {
  const i::ProfileNode* node = reinterpret_cast<const i::ProfileNode*>(this);
  return node->entry()->resource_name();
}

bool CpuProfileNode::IsScriptSharedCrossOrigin() const {
  const i::ProfileNode* node = reinterpret_cast<const i::ProfileNode*>(this);
  return node->entry()->is_shared_cross_origin();
}

int CpuProfileNode::GetLineNumber() const {
  return reinterpret_cast<const i::ProfileNode*>(this)->line_and_column().line;
}

int CpuProfileNode::GetColumnNumber() const {
  return reinterpret_cast<const i::ProfileNode*>(this)
      ->line_and_column()
      .column;
}

unsigned int CpuProfileNode::GetHitLineCount() const {
  const i::ProfileNode* node = reinterpret_cast<const i::ProfileNode*>(this);
  return node->GetHitLineCount();
}

bool CpuProfileNode::GetLineTicks(LineTick* entries,
                                  unsigned int length) const {
  const i::ProfileNode* node = reinterpret_cast<const i::ProfileNode*>(this);
  return node->GetLineTicks(entries, length);
}

const char* CpuProfileNode::GetBailoutReason() const {
  const i::ProfileNode* node = reinterpret_cast<const i::ProfileNode*>(this);
  return node->entry()->bailout_reason();
}

unsigned CpuProfileNode::GetHitCount() const {
  return reinterpret_cast<const i::ProfileNode*>(this)->self_ticks();
}

unsigned CpuProfileNode::GetNodeId() const {
  return reinterpret_cast<const i::ProfileNode*>(this)->id();
}

CpuProfileNode::SourceType CpuProfileNode::GetSourceType() const {
  return reinterpret_cast<const i::ProfileNode*>(this)->source_type();
}

int CpuProfileNode::GetChildrenCount() const {
  return static_cast<int>(
      reinterpret_cast<const i::ProfileNode*>(this)->children()->size());
}

const CpuProfileNode* CpuProfileNode::GetChild(int index) const {
  const i::ProfileNode* child =
      reinterpret_cast<const i::ProfileNode*>(this)->children()->at(index);
  return reinterpret_cast<const CpuProfileNode*>(child);
}

const CpuProfileNode* CpuProfileNode::GetParent() const {
  const i::ProfileNode* parent =
      reinterpret_cast<const i::ProfileNode*>(this)->parent();
  return reinterpret_cast<const CpuProfileNode*>(parent);
}

const std::vector<CpuProfileDeoptInfo>& CpuProfileNode::GetDeoptInfos() const {
  const i::ProfileNode* node = reinterpret_cast<const i::ProfileNode*>(this);
  return node->deopt_infos();
}

void CpuProfile::Delete() {
  i::CpuProfile* profile = reinterpret_cast<i::CpuProfile*>(this);
  i::CpuProfiler* profiler = profile->cpu_profiler();
  DCHECK_NOT_NULL(profiler);
  profiler->DeleteProfile(profile);
}

Local<String> CpuProfile::GetTitle() const {
  const i::CpuProfile* profile = reinterpret_cast<const i::CpuProfile*>(this);
  i::Isolate* i_isolate = profile->top_down()->isolate();
  return ToApiHandle<String>(
      i_isolate->factory()->InternalizeUtf8String(profile->title()));
}

const CpuProfileNode* CpuProfile::GetTopDownRoot() const {
  const i::CpuProfile* profile = reinterpret_cast<const i::CpuProfile*>(this);
  return reinterpret_cast<const CpuProfileNode*>(profile->top_down()->root());
}

const CpuProfileNode* CpuProfile::GetSample(int index) const {
  const i::CpuProfile* profile = reinterpret_cast<const i::CpuProfile*>(this);
  return reinterpret_cast<const CpuProfileNode*>(profile->sample(index).node);
}

const int CpuProfileNode::kNoLineNumberInfo;
const int CpuProfileNode::kNoColumnNumberInfo;

int64_t CpuProfile::GetSampleTimestamp(int index) const {
  const i::CpuProfile* profile = reinterpret_cast<const i::CpuProfile*>(this);
  return profile->sample(index).timestamp.since_origin().InMicroseconds();
}

StateTag CpuProfile::GetSampleState(int index) const {
  const i::CpuProfile* profile = reinterpret_cast<const i::CpuProfile*>(this);
  return profile->sample(index).state_tag;
}

EmbedderStateTag CpuProfile::GetSampleEmbedderState(int index) const {
  const i::CpuProfile* profile = reinterpret_cast<const i::CpuProfile*>(this);
  return profile->sample(index).embedder_state_tag;
}

int64_t CpuProfile::GetStartTime() const {
  const i::CpuProfile* profile = reinterpret_cast<const i::CpuProfile*>(this);
  return profile->start_time().since_origin().InMicroseconds();
}

int64_t CpuProfile::GetEndTime() const {
  const i::CpuProfile* profile = reinterpret_cast<const i::CpuProfile*>(this);
  return profile->end_time().since_origin().InMicroseconds();
}

static i::CpuProfile* ToInternal(const CpuProfile* profile) {
  return const_cast<i::CpuProfile*>(
      reinterpret_cast<const i::CpuProfile*>(profile));
}

void CpuProfile::Serialize(OutputStream* stream,
                           CpuProfile::SerializationFormat format) const {
  Utils::ApiCheck(format == kJSON, "v8::CpuProfile::Serialize",
                  "Unknown serialization format");
  Utils::ApiCheck(stream->GetChunkSize() > 0, "v8::CpuProfile::Serialize",
                  "Invalid stream chunk size");
  i::CpuProfileJSONSerializer serializer(ToInternal(this));
  serializer.Serialize(stream);
}

int CpuProfile::GetSamplesCount() const {
  return reinterpret_cast<const i::CpuProfile*>(this)->samples_count();
}

CpuProfiler* CpuProfiler::New(Isolate* v8_isolate,
                              CpuProfilingNamingMode naming_mode,
                              CpuProfilingLoggingMode logging_mode) {
  return reinterpret_cast<CpuProfiler*>(new i::CpuProfiler(
      reinterpret_cast<i::Isolate*>(v8_isolate), naming_mode, logging_mode));
}

CpuProfilingOptions::CpuProfilingOptions(CpuProfilingMode mode,
                                         unsigned max_samples,
                                         int sampling_interval_us,
                                         MaybeLocal<Context> filter_context)
    : mode_(mode),
      max_samples_(max_samples),
      sampling_interval_us_(sampling_interval_us) {
  if (!filter_context.IsEmpty()) {
    Local<Context> local_filter_context = filter_context.ToLocalChecked();
    filter_context_.Reset(v8::Isolate::GetCurrent(), local_filter_context);
    filter_context_.SetWeak();
  }
}

void* CpuProfilingOptions::raw_filter_context() const {
  return reinterpret_cast<void*>(
      i::Cast<i::Context>(*Utils::OpenPersistent(filter_context_))
          ->native_context()
          .address());
}

void CpuProfiler::Dispose() { delete reinterpret_cast<i::CpuProfiler*>(this); }

// static
// |trace_id| is an optional identifier stored in the sample record used
// to associate the sample with a trace event.
void CpuProfiler::CollectSample(Isolate* v8_isolate,
                                const std::optional<uint64_t> trace_id) {
  i::CpuProfiler::CollectSample(reinterpret_cast<i::Isolate*>(v8_isolate),
                                trace_id);
}

void CpuProfiler::SetSamplingInterval(int us) {
  DCHECK_GE(us, 0);
  return reinterpret_cast<i::CpuProfiler*>(this)->set_sampling_interval(
      base::TimeDelta::FromMicroseconds(us));
}

void CpuProfiler::SetUsePreciseSampling(bool use_precise_sampling) {
  reinterpret_cast<i::CpuProfiler*>(this)->set_use_precise_sampling(
      use_precise_sampling);
}

CpuProfilingResult CpuProfiler::Start(
    CpuProfilingOptions options,
    std::unique_ptr<DiscardedSamplesDelegate> delegate) {
  return reinterpret_cast<i::CpuProfiler*>(this)->StartProfiling(
      std::move(options), std::move(delegate));
}

CpuProfilingResult CpuProfiler::Start(
    Local<String> title, CpuProfilingOptions options,
    std::unique_ptr<DiscardedSamplesDelegate> delegate) {
  return reinterpret_cast<i::CpuProfiler*>(this)->StartProfiling(
      *Utils::OpenDirectHandle(*title), std::move(options),
      std::move(delegate));
}

CpuProfilingResult CpuProfiler::Start(Local<String> title,
                                      bool record_samples) {
  CpuProfilingOptions options(
      kLeafNodeLineNumbers,
      record_samples ? CpuProfilingOptions::kNoSampleLimit : 0);
  return reinterpret_cast<i::CpuProfiler*>(this)->StartProfiling(
      *Utils::OpenDirectHandle(*title), std::move(options));
}

CpuProfilingResult CpuProfiler::Start(Local<String> title,
                                      CpuProfilingMode mode,
                                      bool record_samples,
                                      unsigned max_samples) {
  CpuProfilingOptions options(mode, record_samples ? max_samples : 0);
  return reinterpret_cast<i::CpuProfiler*>(this)->StartProfiling(
      *Utils::OpenDirectHandle(*title), std::move(options));
}

CpuProfilingStatus CpuProfiler::StartProfiling(
    Local<String> title, CpuProfilingOptions options,
    std::unique_ptr<DiscardedSamplesDelegate> delegate) {
  return Start(title, std::move(options), std::move(delegate)).status;
}

CpuProfilingStatus CpuProfiler::StartProfiling(Local<String> title,
                                               bool record_samples) {
  return Start(title, record_samples).status;
}

CpuProfilingStatus CpuProfiler::StartProfiling(Local<String> title,
                                               CpuProfilingMode mode,
                                               bool record_samples,
                                               unsigned max_samples) {
  return Start(title, mode, record_samples, max_samples).status;
}

CpuProfile* CpuProfiler::StopProfiling(Local<String> title) {
  return reinterpret_cast<CpuProfile*>(
      reinterpret_cast<i::CpuProfiler*>(this)->StopProfiling(
          *Utils::OpenDirectHandle(*title)));
}

CpuProfile* CpuProfiler::Stop(ProfilerId id) {
  return reinterpret_cast<CpuProfile*>(
      reinterpret_cast<i::CpuProfiler*>(this)->StopProfiling(id));
}

void CpuProfiler::UseDetailedSourcePositionsForProfiling(Isolate* v8_isolate) {
  reinterpret_cast<i::Isolate*>(v8_isolate)
      ->SetDetailedSourcePositionsForProfiling(true);
}

uintptr_t CodeEvent::GetCodeStartAddress() {
  return reinterpret_cast<i::CodeEvent*>(this)->code_start_address;
}

size_t CodeEvent::GetCodeSize() {
  return reinterpret_cast<i::CodeEvent*>(this)->code_size;
}

Local<String> CodeEvent::GetFunctionName() {
  return ToApiHandle<String>(
      reinterpret_cast<i::CodeEvent*>(this)->function_name);
}

Local<String> CodeEvent::GetScriptName() {
  return ToApiHandle<String>(
      reinterpret_cast<i::CodeEvent*>(this)->script_name);
}

int CodeEvent::GetScriptLine() {
  return reinterpret_cast<i::CodeEvent*>(this)->script_line;
}

int CodeEvent::GetScriptColumn() {
  return reinterpret_cast<i::CodeEvent*>(this)->script_column;
}

CodeEventType CodeEvent::GetCodeType() {
  return reinterpret_cast<i::CodeEvent*>(this)->code_type;
}

const char* CodeEvent::GetComment() {
  return reinterpret_cast<i::CodeEvent*>(this)->comment;
}

uintptr_t CodeEvent::GetPreviousCodeStartAddress() {
  return reinterpret_cast<i::CodeEvent*>(this)->previous_code_start_address;
}

const char* CodeEvent::GetCodeEventTypeName(CodeEventType code_event_type) {
  switch (code_event_type) {
    case kUnknownType:
      return "Unknown";
#define V(Name)       \
  case k##Name##Type: \
    return #Name;
      CODE_EVENTS_LIST(V)
#undef V
  }
  // The execution should never pass here
  UNREACHABLE();
}

CodeEventHandler::CodeEventHandler(Isolate* v8_isolate) {
  internal_listener_ = new i::ExternalLogEventListener(
      reinterpret_cast<i::Isolate*>(v8_isolate));
}

CodeEventHandler::~CodeEventHandler() {
  delete reinterpret_cast<i::ExternalLogEventListener*>(internal_listener_);
}

void CodeEventHandler::Enable() {
  reinterpret_cast<i::ExternalLogEventListener*>(internal_listener_)
      ->StartListening(this);
}

void CodeEventHandler::Disable() {
  reinterpret_cast<i::ExternalLogEventListener*>(internal_listener_)
      ->StopListening();
}

static i::HeapGraphEdge* ToInternal(const HeapGraphEdge* edge) {
  return const_cast<i::HeapGraphEdge*>(
      reinterpret_cast<const i::HeapGraphEdge*>(edge));
}

HeapGraphEdge::Type HeapGraphEdge::GetType() const {
  return static_cast<HeapGraphEdge::Type>(ToInternal(this)->type());
}

Local<Value> HeapGraphEdge::GetName() const {
  i::HeapGraphEdge* edge = ToInternal(this);
  i::Isolate* i_isolate = edge->isolate();
  switch (edge->type()) {
    case i::HeapGraphEdge::kContextVariable:
    case i::HeapGraphEdge::kInternal:
    case i::HeapGraphEdge::kProperty:
    case i::HeapGraphEdge::kShortcut:
    case i::HeapGraphEdge::kWeak:
      return ToApiHandle<String>(
          i_isolate->factory()->InternalizeUtf8String(edge->name()));
    case i::HeapGraphEdge::kElement:
    case i::HeapGraphEdge::kHidden:
      return ToApiHandle<Number>(
          i_isolate->factory()->NewNumberFromInt(edge->index()));
    default:
      UNREACHABLE();
  }
}

const HeapGraphNode* HeapGraphEdge::GetFromNode() const {
  const i::HeapEntry* from = ToInternal(this)->from();
  return reinterpret_cast<const HeapGraphNode*>(from);
}

const HeapGraphNode* HeapGraphEdge::GetToNode() const {
  const i::HeapEntry* to = ToInternal(this)->to();
  return reinterpret_cast<const HeapGraphNode*>(to);
}

static i::HeapEntry* ToInternal(const HeapGraphNode* entry) {
  return const_cast<i::HeapEntry*>(
      reinterpret_cast<const i::HeapEntry*>(entry));
}

HeapGraphNode::Type HeapGraphNode::GetType() const {
  return static_cast<HeapGraphNode::Type>(ToInternal(this)->type());
}

Local<String> HeapGraphNode::GetName() const {
  i::Isolate* i_isolate = ToInternal(this)->isolate();
  return ToApiHandle<String>(
      i_isolate->factory()->InternalizeUtf8String(ToInternal(this)->name()));
}

SnapshotObjectId HeapGraphNode::GetId() const { return ToInternal(this)->id(); }

size_t HeapGraphNode::GetShallowSize() const {
  return ToInternal(this)->self_size();
}

int HeapGraphNode::GetChildrenCount() const {
  return ToInternal(this)->children_count();
}

const HeapGraphEdge* HeapGraphNode::GetChild(int index) const {
  return reinterpret_cast<const HeapGraphEdge*>(ToInternal(this)->child(index));
}

static i::HeapSnapshot* ToInternal(const HeapSnapshot* snapshot) {
  return const_cast<i::HeapSnapshot*>(
      reinterpret_cast<const i::HeapSnapshot*>(snapshot));
}

void HeapSnapshot::Delete() {
  i::Isolate* i_isolate = ToInternal(this)->profiler()->isolate();
  if (i_isolate->heap()->heap_profiler()->GetSnapshotsCount() > 1 ||
      i_isolate->heap()->heap_profiler()->IsTakingSnapshot()) {
    ToInternal(this)->Delete();
  } else {
    // If this is the last snapshot, clean up all accessory data as well.
    i_isolate->heap()->heap_profiler()->DeleteAllSnapshots();
  }
}

const HeapGraphNode* HeapSnapshot::GetRoot() const {
  return reinterpret_cast<const HeapGraphNode*>(ToInternal(this)->root());
}

const HeapGraphNode* HeapSnapshot::GetNodeById(SnapshotObjectId id) const {
  return reinterpret_cast<const HeapGraphNode*>(
      ToInternal(this)->GetEntryById(id));
}

int HeapSnapshot::GetNodesCount() const {
  return static_cast<int>(ToInternal(this)->entries().size());
}

const HeapGraphNode* HeapSnapshot::GetNode(int index) const {
  return reinterpret_cast<const HeapGraphNode*>(
      &ToInternal(this)->entries().at(index));
}

SnapshotObjectId HeapSnapshot::GetMaxSnapshotJSObjectId() const {
  return ToInternal(this)->max_snapshot_js_object_id();
}

void HeapSnapshot::Serialize(OutputStream* stream,
                             HeapSnapshot::SerializationFormat format) const {
  Utils::ApiCheck(format == kJSON, "v8::HeapSnapshot::Serialize",
                  "Unknown serialization format");
  Utils::ApiCheck(stream->GetChunkSize() > 0, "v8::HeapSnapshot::Serialize",
                  "Invalid stream chunk size");
  i::HeapSnapshotJSONSerializer serializer(ToInternal(this));
  serializer.Serialize(stream);
}

// static
STATIC_CONST_MEMBER_DEFINITION const SnapshotObjectId
    HeapProfiler::kUnknownObjectId;

int HeapProfiler::GetSnapshotCount() {
  return reinterpret_cast<i::HeapProfiler*>(this)->GetSnapshotsCount();
}

void HeapProfiler::QueryObjects(Local<Context> v8_context,
                                QueryObjectPredicate* predicate,
                                std::vector<Global<Object>>* objects) {
  i::Isolate* isolate = i::Isolate::Current();
  i::HeapProfiler* profiler = reinterpret_cast<i::HeapProfiler*>(this);
  DCHECK_EQ(isolate, profiler->isolate());
  EnterV8NoScriptNoExceptionScope api_scope(isolate);
  profiler->QueryObjects(Utils::OpenDirectHandle(*v8_context), predicate,
                         objects);
}

const HeapSnapshot* HeapProfiler::GetHeapSnapshot(int index) {
  return reinterpret_cast<const HeapSnapshot*>(
      reinterpret_cast<i::HeapProfiler*>(this)->GetSnapshot(index));
}

SnapshotObjectId HeapProfiler::GetObjectId(Local<Value> value) {
  auto obj = Utils::OpenDirectHandle(*value);
  return reinterpret_cast<i::HeapProfiler*>(this)->GetSnapshotObjectId(obj);
}

SnapshotObjectId HeapProfiler::GetObjectId(NativeObject value) {
  return reinterpret_cast<i::HeapProfiler*>(this)->GetSnapshotObjectId(value);
}

Local<Value> HeapProfiler::FindObjectById(SnapshotObjectId id) {
  i::DirectHandle<i::Object> obj =
      reinterpret_cast<i::HeapProfiler*>(this)->FindHeapObjectById(id);
  if (obj.is_null()) return {};
  return Utils::ToLocal(obj);
}

void HeapProfiler::ClearObjectIds() {
  reinterpret_cast<i::HeapProfiler*>(this)->ClearHeapObjectMap();
}

const HeapSnapshot* HeapProfiler::TakeHeapSnapshot(
    const HeapSnapshotOptions& options) {
  return reinterpret_cast<const HeapSnapshot*>(
      reinterpret_cast<i::HeapProfiler*>(this)->TakeSnapshot(options));
}

const HeapSnapshot* HeapProfiler::TakeHeapSnapshot(ActivityControl* control,
                                                   ObjectNameResolver* resolver,
                                                   bool hide_internals,
                                                   bool capture_numeric_value) {
  HeapSnapshotOptions options;
  options.control = control;
  options.global_object_name_resolver = resolver;
  options.snapshot_mode = hide_internals ? HeapSnapshotMode::kRegular
                                         : HeapSnapshotMode::kExposeInternals;
  options.numerics_mode = capture_numeric_value
                              ? NumericsMode::kExposeNumericValues
                              : NumericsMode::kHideNumericValues;
  return TakeHeapSnapshot(options);
}

std::vector<v8::Local<v8::Value>> HeapProfiler::GetDetachedJSWrapperObjects() {
  return reinterpret_cast<i::HeapProfiler*>(this)
      ->GetDetachedJSWrapperObjects();
}

void HeapProfiler::StartTrackingHeapObjects(bool track_allocations) {
  reinterpret_cast<i::HeapProfiler*>(this)->StartHeapObjectsTracking(
      track_allocations);
}

void HeapProfiler::StopTrackingHeapObjects() {
  reinterpret_cast<i::HeapProfiler*>(this)->StopHeapObjectsTracking();
}

SnapshotObjectId HeapProfiler::GetHeapStats(OutputStream* stream,
                                            int64_t* timestamp_us) {
  i::HeapProfiler* heap_profiler = reinterpret_cast<i::HeapProfiler*>(this);
  return heap_profiler->PushHeapObjectsStats(stream, timestamp_us);
}

bool HeapProfiler::StartSamplingHeapProfiler(uint64_t sample_interval,
                                             int stack_depth,
                                             SamplingFlags flags) {
  return reinterpret_cast<i::HeapProfiler*>(this)->StartSamplingHeapProfiler(
      sample_interval, stack_depth, flags);
}

void HeapProfiler::StopSamplingHeapProfiler() {
  reinterpret_cast<i::HeapProfiler*>(this)->StopSamplingHeapProfiler();
}

AllocationProfile* HeapProfiler::GetAllocationProfile() {
  return reinterpret_cast<i::HeapProfiler*>(this)->GetAllocationProfile();
}

void HeapProfiler::DeleteAllHeapSnapshots() {
  reinterpret_cast<i::HeapProfiler*>(this)->DeleteAllSnapshots();
}

v8::EmbedderGraph::Node* v8::EmbedderGraph::V8Node(
    const v8::Local<v8::Data>& data) {
  CHECK(data->IsValue());
  return V8Node(data.As<v8::Value>());
}

void HeapProfiler::AddBuildEmbedderGraphCallback(
    BuildEmbedderGraphCallback callback, void* data) {
  reinterpret_cast<i::HeapProfiler*>(this)->AddBuildEmbedderGraphCallback(
      callback, data);
}

void HeapProfiler::RemoveBuildEmbedderGraphCallback(
    BuildEmbedderGraphCallback callback, void* data) {
  reinterpret_cast<i::HeapProfiler*>(this)->RemoveBuildEmbedderGraphCallback(
      callback, data);
}

void HeapProfiler::SetGetDetachednessCallback(GetDetachednessCallback callback,
                                              void* data) {
  reinterpret_cast<i::HeapProfiler*>(this)->SetGetDetachednessCallback(callback,
                                                                       data);
}

bool HeapProfiler::IsTakingSnapshot() {
  return reinterpret_cast<i::HeapProfiler*>(this)->IsTakingSnapshot();
}

const char* HeapProfiler::CopyNameForHeapSnapshot(const char* name) {
  return reinterpret_cast<i::HeapProfiler*>(this)->CopyNameForHeapSnapshot(
      name);
}

EmbedderStateScope::EmbedderStateScope(Isolate* v8_isolate,
                                       Local<v8::Context> context,
                                       EmbedderStateTag tag)
    : embedder_state_(new internal::EmbedderState(v8_isolate, context, tag)) {}

// std::unique_ptr's destructor is not compatible with Forward declared
// EmbedderState class.
// Default destructor must be defined in implementation file.
EmbedderStateScope::~EmbedderStateScope() = default;

void TracedReferenceBase::CheckValue() const {
#ifdef V8_HOST_ARCH_64_BIT
  if (IsEmpty()) return;

  CHECK_NE(internal::kGlobalHandleZapValue,
           *reinterpret_cast<uint64_t*>(slot()));
#endif  // V8_HOST_ARCH_64_BIT
}

CFunction::CFunction(const void* address, const CFunctionInfo* type_info)
    : address_(address), type_info_(type_info) {
  CHECK_NOT_NULL(address_);
  CHECK_NOT_NULL(type_info_);
}

CFunctionInfo::CFunctionInfo(const CTypeInfo& return_info,
                             unsigned int arg_count, const CTypeInfo* arg_info,
                             Int64Representation repr)
    : return_info_(return_info),
      repr_(repr),
      arg_count_(arg_count),
      arg_info_(arg_info) {
  DCHECK(repr == Int64Representation::kNumber ||
         repr == Int64Representation::kBigInt);
  if (arg_count_ > 0) {
    for (unsigned int i = 0; i < arg_count_ - 1; ++i) {
      DCHECK(arg_info_[i].GetType() != CTypeInfo::kCallbackOptionsType);
    }
  }
}

const CTypeInfo& CFunctionInfo::ArgumentInfo(unsigned int index) const {
  DCHECK_LT(index, ArgumentCount());
  return arg_info_[index];
}

namespace api_internal {
V8_EXPORT v8::Local<v8::Value> GetFunctionTemplateData(
    v8::Isolate* isolate, v8::Local<v8::Data> raw_target) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  i::DirectHandle<i::Object> target = Utils::OpenDirectHandle(*raw_target);
  if (i::IsFunctionTemplateInfo(*target)) {
    i::DirectHandle<i::Object> data(
        i::Cast<i::FunctionTemplateInfo>(*target)->callback_data(kAcquireLoad),
        i_isolate);
    return Utils::ToLocal(data);

  } else if (i::IsJSFunction(*target)) {
    i::DirectHandle<i::JSFunction> target_func = i::Cast<i::JSFunction>(target);
    auto shared = target_func->shared();
    if (shared->IsApiFunction()) {
      i::DirectHandle<i::Object> data(
          shared->api_func_data()->callback_data(kAcquireLoad), i_isolate);
      return Utils::ToLocal(data);
    }
  }
  Utils::ApiCheck(false, "api_internal::GetFunctionTemplateData",
                  "Target function is not an Api function");
  UNREACHABLE();
}
}  // namespace api_internal

RegisterState::RegisterState()
    : pc(nullptr), sp(nullptr), fp(nullptr), lr(nullptr) {}
RegisterState::~RegisterState() = default;

RegisterState::RegisterState(const RegisterState& other) { *this = other; }

RegisterState& RegisterState::operator=(const RegisterState& other) {
  if (&other != this) {
    pc = other.pc;
    sp = other.sp;
    fp = other.fp;
    lr = other.lr;
    if (other.callee_saved) {
      // Make a deep copy if {other.callee_saved} is non-null.
      callee_saved =
          std::make_unique<CalleeSavedRegisters>(*(other.callee_saved));
    } else {
      // Otherwise, set {callee_saved} to null to match {other}.
      callee_saved.reset();
    }
  }
  return *this;
}

#if !V8_ENABLE_WEBASSEMBLY
// If WebAssembly is disabled, we still need to provide an implementation of the
// WasmStreaming API. Since {WasmStreaming::Unpack} will always fail, all
// methods are unreachable.

class WasmStreaming::WasmStreamingImpl {};

WasmStreaming::WasmStreaming(std::unique_ptr<WasmStreamingImpl>) {
  UNREACHABLE();
}

WasmStreaming::~WasmStreaming() = default;

void WasmStreaming::OnBytesReceived(const uint8_t* bytes, size_t size) {
  UNREACHABLE();
}

void WasmStreaming::Finish(bool can_use_compiled_module) { UNREACHABLE(); }

void WasmStreaming::Abort(MaybeLocal<Value> exception) { UNREACHABLE(); }

bool WasmStreaming::SetCompiledModuleBytes(const uint8_t* bytes, size_t size) {
  UNREACHABLE();
}

void WasmStreaming::SetMoreFunctionsCanBeSerializedCallback(
    std::function<void(CompiledWasmModule)>) {
  UNREACHABLE();
}

void WasmStreaming::SetUrl(const char* url, size_t length) { UNREACHABLE(); }

// static
std::shared_ptr<WasmStreaming> WasmStreaming::Unpack(Isolate* v8_isolate,
                                                     Local<Value> value) {
  FATAL("WebAssembly is disabled");
}
#endif  // !V8_ENABLE_WEBASSEMBLY

namespace internal {

const size_t HandleScopeImplementer::kEnteredContextsOffset =
    offsetof(HandleScopeImplementer, entered_contexts_);

void HandleScopeImplementer::FreeThreadResources() { Free(); }

char* HandleScopeImplementer::ArchiveThread(char* storage) {
  HandleScopeData* current = isolate_->handle_scope_data();
  handle_scope_data_ = *current;
  MemCopy(storage, this, sizeof(*this));

  ResetAfterArchive();
  current->Initialize();

  return storage + ArchiveSpacePerThread();
}

int HandleScopeImplementer::ArchiveSpacePerThread() {
  return sizeof(HandleScopeImplementer);
}

char* HandleScopeImplementer::RestoreThread(char* storage) {
  MemCopy(this, storage, sizeof(*this));
  *isolate_->handle_scope_data() = handle_scope_data_;
  return storage + ArchiveSpacePerThread();
}

void HandleScopeImplementer::IterateThis(RootVisitor* v) {
#ifdef DEBUG
  bool found_block_before_persistent = false;
#endif
  // Iterate over all handles in the blocks except for the last.
  for (int i = static_cast<int>(blocks()->size()) - 2; i >= 0; --i) {
    Address* block = blocks()->at(i);
    // Cast possibly-unrelated pointers to plain Address before comparing them
    // to avoid undefined behavior.
    if (HasPersistentScope() &&
        (reinterpret_cast<Address>(
             last_handle_before_persistent_block_.value()) <=
         reinterpret_cast<Address>(&block[kHandleBlockSize])) &&
        (reinterpret_cast<Address>(
             last_handle_before_persistent_block_.value()) >=
         reinterpret_cast<Address>(block))) {
      v->VisitRootPointers(
          Root::kHandleScope, nullptr, FullObjectSlot(block),
          FullObjectSlot(last_handle_before_persistent_block_.value()));
      DCHECK(!found_block_before_persistent);
#ifdef DEBUG
      found_block_before_persistent = true;
#endif
    } else {
      v->VisitRootPointers(Root::kHandleScope, nullptr, FullObjectSlot(block),
                           FullObjectSlot(&block[kHandleBlockSize]));
    }
  }

  DCHECK_EQ(HasPersistentScope() &&
                last_handle_before_persistent_block_.value() != nullptr,
            found_block_before_persistent);

  // Iterate over live handles in the last block (if any).
  if (!blocks()->empty()) {
    v->VisitRootPointers(Root::kHandleScope, nullptr,
                         FullObjectSlot(blocks()->back()),
                         FullObjectSlot(handle_scope_data_.next));
  }

  saved_contexts_.shrink_to_fit();
  if (!saved_contexts_.empty()) {
    FullObjectSlot start(&saved_contexts_.front());
    v->VisitRootPointers(Root::kHandleScope, nullptr, start,
                         start + static_cast<int>(saved_contexts_.size()));
  }
  entered_contexts_.shrink_to_fit();
  if (!entered_contexts_.empty()) {
    FullObjectSlot start(&entered_contexts_.front());
    v->VisitRootPointers(Root::kHandleScope, nullptr, start,
                         start + static_cast<int>(entered_contexts_.size()));
  }
}

void HandleScopeImplementer::Iterate(RootVisitor* v) {
  HandleScopeData* current = isolate_->handle_scope_data();
  handle_scope_data_ = *current;
  IterateThis(v);
}

char* HandleScopeImplementer::Iterate(RootVisitor* v, char* storage) {
  HandleScopeImplementer* scope_implementer =
      reinterpret_cast<HandleScopeImplementer*>(storage);
  scope_implementer->IterateThis(v);
  return storage + ArchiveSpacePerThread();
}

std::unique_ptr<PersistentHandles> HandleScopeImplementer::DetachPersistent(
    Address* first_block) {
  std::unique_ptr<PersistentHandles> ph(new PersistentHandles(isolate()));
  DCHECK(HasPersistentScope());
  DCHECK_NOT_NULL(first_block);

  Address* block_start;
  do {
    block_start = blocks_.back();
    ph->blocks_.push_back(blocks_.back());
#if DEBUG
    ph->ordered_blocks_.insert(blocks_.back());
#endif
    blocks_.pop_back();
  } while (block_start != first_block);

  // ph->blocks_ now contains the blocks installed on the HandleScope stack
  // since BeginPersistentScope was called, but in reverse order.

  // Switch first and last blocks, such that the last block is the one
  // that is potentially half full.
  DCHECK(!ph->blocks_.empty());
  std::swap(ph->blocks_.front(), ph->blocks_.back());

  ph->block_next_ = isolate()->handle_scope_data()->next;
  block_start = ph->blocks_.back();
  ph->block_limit_ = block_start + kHandleBlockSize;

  DCHECK_EQ(blocks_.empty(),
            last_handle_before_persistent_block_.value() == nullptr);
  last_handle_before_persistent_block_.reset();
  return ph;
}

void InvokeAccessorGetterCallback(
    v8::Local<v8::Name> property,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
  // Leaving JavaScript.
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  RCS_SCOPE(i_isolate, RuntimeCallCounterId::kAccessorGetterCallback);

  v8::AccessorNameGetterCallback getter;
  {
    Address arg = i_isolate->isolate_data()->api_callback_thunk_argument();
    // Currently we don't call InterceptorInfo callbacks via CallApiGetter.
    DCHECK(IsAccessorInfo(Tagged<Object>(arg)));
    Tagged<AccessorInfo> accessor_info =
        Cast<AccessorInfo>(Tagged<Object>(arg));
    getter = reinterpret_cast<v8::AccessorNameGetterCallback>(
        accessor_info->getter(i_isolate));

    if (V8_UNLIKELY(i_isolate->should_check_side_effects())) {
      i::DirectHandle<Object> receiver_check_unsupported;

      if (!i_isolate->debug()->PerformSideEffectCheckForAccessor(
              direct_handle(accessor_info, i_isolate),
              receiver_check_unsupported, ACCESSOR_GETTER)) {
        return;
      }
    }
  }
  ExternalCallbackScope call_scope(i_isolate, FUNCTION_ADDR(getter),
                                   v8::ExceptionContext::kAttributeGet, &info);
  getter(property, info);
}

namespace {

inline Tagged<FunctionTemplateInfo> GetTargetFunctionTemplateInfo(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  Tagged<Object> target = FunctionCallbackArguments::GetTarget(info);
  CHECK(IsFunctionTemplateInfo(target));
  return Cast<FunctionTemplateInfo>(target);
}

inline void InvokeFunctionCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info, CallApiCallbackMode mode) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  RCS_SCOPE(i_isolate, RuntimeCallCounterId::kFunctionCallback);

  Tagged<FunctionTemplateInfo> fti = GetTargetFunctionTemplateInfo(info);
  v8::FunctionCallback callback =
      reinterpret_cast<v8::FunctionCallback>(fti->callback(i_isolate));
  switch (mode) {
    case CallApiCallbackMode::kGeneric: {
      if (V8_UNLIKELY(i_isolate->should_check_side_effects())) {
        if (!i_isolate->debug()->PerformSideEffectCheckForCallback(
                handle(fti, i_isolate))) {
          // Failed side effect check.
          return;
        }
        if (DEBUG_BOOL) {
          // Clear raw pointer to ensure it's not accidentally used after
          // potential GC in PerformSideEffectCheckForCallback.
          fti = {};
        }
      }
      break;
    }
    case CallApiCallbackMode::kOptimized:
      // CallFunction builtin should deoptimize an optimized function when
      // side effects checking is enabled, so we don't have to handle side
      // effects checking in the optimized version of the builtin.
      DCHECK(!i_isolate->should_check_side_effects());
      break;
    case CallApiCallbackMode::kOptimizedNoProfiling:
      // This mode doesn't call InvokeFunctionCallback.
      UNREACHABLE();
  }

  ExternalCallbackScope call_scope(i_isolate, FUNCTION_ADDR(callback),
                                   info.IsConstructCall()
                                       ? v8::ExceptionContext::kConstructor
                                       : v8::ExceptionContext::kOperation,
                                   &info);
  callback(info);
}
}  // namespace

void InvokeFunctionCallbackGeneric(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  InvokeFunctionCallback(info, CallApiCallbackMode::kGeneric);
}

void InvokeFunctionCallbackOptimized(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  InvokeFunctionCallback(info, CallApiCallbackMode::kOptimized);
}

void InvokeFinalizationRegistryCleanupFromTask(
    DirectHandle<NativeContext> native_context,
    DirectHandle<JSFinalizationRegistry> finalization_registry) {
  i::Isolate* i_isolate = i::Isolate::Current();
  RCS_SCOPE(i_isolate,
            RuntimeCallCounterId::kFinalizationRegistryCleanupFromTask);
  // Do not use ENTER_V8 because this is always called from a running
  // FinalizationRegistryCleanupTask within V8 and we should not log it as an
  // API call. This method is implemented here to avoid duplication of the
  // exception handling and microtask running logic in CallDepthScope.
  if (i_isolate->is_execution_terminating()) return;
  Local<v8::Context> api_context = Utils::ToLocal(native_context);
  CallDepthScope<true> call_depth_scope(i_isolate, api_context);
  VMState<OTHER> state(i_isolate);
  JSFinalizationRegistry::Cleanup(i_isolate, finalization_registry);
}

template <>
EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
int32_t ConvertDouble(double d) {
  return internal::DoubleToInt32(d);
}

template <>
EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
uint32_t ConvertDouble(double d) {
  return internal::DoubleToUint32(d);
}

template <>
EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
float ConvertDouble(double d) {
  return internal::DoubleToFloat32(d);
}

template <>
EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
double ConvertDouble(double d) {
  return d;
}

template <>
EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
int64_t ConvertDouble(double d) {
  return internal::DoubleToWebIDLInt64(d);
}

template <>
EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
uint64_t ConvertDouble(double d) {
  return internal::DoubleToWebIDLUint64(d);
}

template <>
EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
bool ConvertDouble(double d) {
  // Implements https://tc39.es/ecma262/#sec-toboolean.
  return !std::isnan(d) && d != 0;
}

// Undefine macros for jumbo build.
#undef SET_FIELD_WRAPPED
#undef NEW_STRING
#undef CALLBACK_SETTER

template <typename T>
bool ValidateFunctionCallbackInfo(const FunctionCallbackInfo<T>& info) {
  CHECK_GE(info.Length(), 0);
  // Theortically args-length is unlimited, practically we run out of stack
  // space. This should guard against accidentally used raw pointers.
  CHECK_LE(info.Length(), 0xFFFFF);
  if (info.Length() > 0) {
    CHECK(info[0]->IsValue());
    CHECK(info[info.Length() - 1]->IsValue());
  }
  auto* i_isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  CHECK_EQ(i_isolate, Isolate::Current());
  CHECK(!i_isolate->GetIncumbentContext().is_null());
  CHECK(info.This()->IsObject());
  CHECK(!info.Data().IsEmpty());
  CHECK(info.GetReturnValue().Get()->IsValue());
  return true;
}

template <typename T>
bool ValidatePropertyCallbackInfo(const PropertyCallbackInfo<T>& info) {
  auto* i_isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  CHECK_EQ(i_isolate, Isolate::Current());
  CHECK(info.This()->IsValue());
  CHECK(info.HolderV2()->IsObject());
  CHECK(!i::IsJSGlobalObject(*Utils::OpenDirectHandle(*info.HolderV2())));
  // Allow usages of v8::PropertyCallbackInfo<T>::Holder() for now.
  // TODO(https://crbug.com/333672197): remove.
  START_ALLOW_USE_DEPRECATED()
  CHECK(info.Holder()->IsObject());
  CHECK_IMPLIES(info.Holder() != info.HolderV2(),
                i::IsJSGlobalObject(*Utils::OpenDirectHandle(*info.Holder())));
  END_ALLOW_USE_DEPRECATED()
  i::Tagged<i::Object> key = i::PropertyCallbackArguments::GetPropertyKey(info);
  CHECK(i::IsSmi(key) || i::IsName(key));
  CHECK(info.Data()->IsValue());
  USE(info.ShouldThrowOnError());
  if (!std::is_same_v<T, void>) {
    CHECK(info.GetReturnValue().Get()->IsValue());
  }
  return true;
}

template <>
bool V8_EXPORT ValidateCallbackInfo(const FunctionCallbackInfo<void>& info) {
  return ValidateFunctionCallbackInfo(info);
}

template <>
bool V8_EXPORT
ValidateCallbackInfo(const FunctionCallbackInfo<v8::Value>& info) {
  return ValidateFunctionCallbackInfo(info);
}

template <>
bool V8_EXPORT
ValidateCallbackInfo(const PropertyCallbackInfo<v8::Value>& info) {
  return ValidatePropertyCallbackInfo(info);
}

template <>
bool V8_EXPORT
ValidateCallbackInfo(const PropertyCallbackInfo<v8::Array>& info) {
  return ValidatePropertyCallbackInfo(info);
}

template <>
bool V8_EXPORT
ValidateCallbackInfo(const PropertyCallbackInfo<v8::Boolean>& info) {
  return ValidatePropertyCallbackInfo(info);
}

template <>
bool V8_EXPORT
ValidateCallbackInfo(const PropertyCallbackInfo<v8::Integer>& info) {
  return ValidatePropertyCallbackInfo(info);
}

template <>
bool V8_EXPORT ValidateCallbackInfo(const PropertyCallbackInfo<void>& info) {
  return ValidatePropertyCallbackInfo(info);
}

}  // namespace internal

ExternalMemoryAccounter::~ExternalMemoryAccounter() {
#ifdef V8_ENABLE_MEMORY_ACCOUNTING_CHECKS
  DCHECK_EQ(amount_of_external_memory_, 0U);
#endif
}

ExternalMemoryAccounter::ExternalMemoryAccounter(
    ExternalMemoryAccounter&& other) {
#if V8_ENABLE_MEMORY_ACCOUNTING_CHECKS
  amount_of_external_memory_ =
      std::exchange(other.amount_of_external_memory_, 0U);
  isolate_ = std::exchange(other.isolate_, nullptr);
#endif
}

ExternalMemoryAccounter& ExternalMemoryAccounter::operator=(
    ExternalMemoryAccounter&& other) {
#if V8_ENABLE_MEMORY_ACCOUNTING_CHECKS
  if (this == &other) {
    return *this;
  }
  DCHECK_EQ(amount_of_external_memory_, 0U);
  amount_of_external_memory_ =
      std::exchange(other.amount_of_external_memory_, 0U);
  isolate_ = std::exchange(other.isolate_, nullptr);
#endif
  return *this;
}

void ExternalMemoryAccounter::Increase(Isolate* isolate, size_t size) {
#ifdef V8_ENABLE_MEMORY_ACCOUNTING_CHECKS
  DCHECK(isolate == isolate_ || isolate_ == nullptr);
  isolate_ = isolate;
  amount_of_external_memory_ += size;
#endif
  isolate->AdjustAmountOfExternalAllocatedMemoryImpl(
      static_cast<int64_t>(size));
}

void ExternalMemoryAccounter::Update(Isolate* isolate, int64_t delta) {
#ifdef V8_ENABLE_MEMORY_ACCOUNTING_CHECKS
  DCHECK(isolate == isolate_ || isolate_ == nullptr);
  DCHECK_GE(static_cast<int64_t>(amount_of_external_memory_), -delta);
  isolate_ = isolate;
  amount_of_external_memory_ += delta;
#endif
  isolate->AdjustAmountOfExternalAllocatedMemoryImpl(delta);
}

void ExternalMemoryAccounter::Decrease(Isolate* isolate, size_t size) {
  internal::DisallowGarbageCollection no_gc;
  if (size == 0) {
    return;
  }
#ifdef V8_ENABLE_MEMORY_ACCOUNTING_CHECKS
  DCHECK_EQ(isolate, isolate_);
  DCHECK_GE(amount_of_external_memory_, size);
  amount_of_external_memory_ -= size;
#endif
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  i_isolate->heap()->UpdateExternalMemory(-static_cast<int64_t>(size));
}

int64_t
ExternalMemoryAccounter::GetTotalAmountOfExternalAllocatedMemoryForTesting(
    const Isolate* isolate) {
  const i::Isolate* i_isolate = reinterpret_cast<const i::Isolate*>(isolate);
  return i_isolate->heap()->external_memory();
}

template <>
bool V8_EXPORT V8_WARN_UNUSED_RESULT
TryToCopyAndConvertArrayToCppBuffer<CTypeInfoBuilder<int32_t>::Build().GetId(),
                                    int32_t>(Local<Array> src, int32_t* dst,
                                             uint32_t max_length) {
  return CopyAndConvertArrayToCppBuffer<
      CTypeInfo(CTypeInfo::Type::kInt32).GetId(), int32_t>(src, dst,
                                                           max_length);
}

template <>
bool V8_EXPORT V8_WARN_UNUSED_RESULT
TryToCopyAndConvertArrayToCppBuffer<CTypeInfoBuilder<uint32_t>::Build().GetId(),
                                    uint32_t>(Local<Array> src, uint32_t* dst,
                                              uint32_t max_length) {
  return CopyAndConvertArrayToCppBuffer<
      CTypeInfo(CTypeInfo::Type::kUint32).GetId(), uint32_t>(src, dst,
                                                             max_length);
}

template <>
bool V8_EXPORT V8_WARN_UNUSED_RESULT
TryToCopyAndConvertArrayToCppBuffer<CTypeInfoBuilder<float>::Build().GetId(),
                                    float>(Local<Array> src, float* dst,
                                           uint32_t max_length) {
  return CopyAndConvertArrayToCppBuffer<
      CTypeInfo(CTypeInfo::Type::kFloat32).GetId(), float>(src, dst,
                                                           max_length);
}

template <>
bool V8_EXPORT V8_WARN_UNUSED_RESULT
TryToCopyAndConvertArrayToCppBuffer<CTypeInfoBuilder<double>::Build().GetId(),
                                    double>(Local<Array> src, double* dst,
                                            uint32_t max_length) {
  return CopyAndConvertArrayToCppBuffer<
      CTypeInfo(CTypeInfo::Type::kFloat64).GetId(), double>(src, dst,
                                                            max_length);
}

}  // namespace v8

#ifdef ENABLE_SLOW_DCHECKS
EXPORT_CONTEXTUAL_VARIABLE(i::StackAllocatedCheck)
#endif
