// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/atomic-utils.h"
#include "src/macro-assembler.h"
#include "src/objects.h"
#include "src/property-descriptor.h"
#include "src/v8.h"

#include "src/simulator.h"

#include "src/wasm/ast-decoder.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-debug.h"
#include "src/wasm/wasm-function-name-table.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-result.h"

#include "src/compiler/wasm-compiler.h"

namespace v8 {
namespace internal {
namespace wasm {

static const int kPlaceholderMarker = 1000000000;

static const char* wasmSections[] = {
#define F(enumerator, order, string) string,
    FOR_EACH_WASM_SECTION_TYPE(F)
#undef F
        "<unknown>"  // entry for "Max"
};

static uint8_t wasmSectionsLengths[]{
#define F(enumerator, order, string) sizeof(string) - 1,
    FOR_EACH_WASM_SECTION_TYPE(F)
#undef F
        9  // entry for "Max"
};

static uint8_t wasmSectionsOrders[]{
#define F(enumerator, order, string) order,
    FOR_EACH_WASM_SECTION_TYPE(F)
#undef F
        0  // entry for "Max"
};

static_assert(sizeof(wasmSections) / sizeof(wasmSections[0]) ==
                  (size_t)WasmSection::Code::Max + 1,
              "expected enum WasmSection::Code to be monotonic from 0");

WasmSection::Code WasmSection::begin() { return (WasmSection::Code)0; }
WasmSection::Code WasmSection::end() { return WasmSection::Code::Max; }
WasmSection::Code WasmSection::next(WasmSection::Code code) {
  return (WasmSection::Code)(1 + (uint32_t)code);
}

const char* WasmSection::getName(WasmSection::Code code) {
  return wasmSections[(size_t)code];
}

size_t WasmSection::getNameLength(WasmSection::Code code) {
  return wasmSectionsLengths[(size_t)code];
}

int WasmSection::getOrder(WasmSection::Code code) {
  return wasmSectionsOrders[(size_t)code];
}

WasmSection::Code WasmSection::lookup(const byte* string, uint32_t length) {
  // TODO(jfb) Linear search, it may be better to do a common-prefix search.
  for (Code i = begin(); i != end(); i = next(i)) {
    if (getNameLength(i) == length && 0 == memcmp(getName(i), string, length)) {
      return i;
    }
  }
  return Code::Max;
}

std::ostream& operator<<(std::ostream& os, const WasmModule& module) {
  os << "WASM module with ";
  os << (module.min_mem_pages * module.kPageSize) << " min mem";
  os << (module.max_mem_pages * module.kPageSize) << " max mem";
  os << module.functions.size() << " functions";
  os << module.functions.size() << " globals";
  os << module.functions.size() << " data segments";
  return os;
}

std::ostream& operator<<(std::ostream& os, const WasmFunction& function) {
  os << "WASM function with signature " << *function.sig;

  os << " code bytes: "
     << (function.code_end_offset - function.code_start_offset);
  return os;
}

std::ostream& operator<<(std::ostream& os, const WasmFunctionName& pair) {
  os << "#" << pair.function_->func_index << ":";
  if (pair.function_->name_offset > 0) {
    if (pair.module_) {
      WasmName name = pair.module_->GetName(pair.function_->name_offset,
                                            pair.function_->name_length);
      os.write(name.start(), name.length());
    } else {
      os << "+" << pair.function_->func_index;
    }
  } else {
    os << "?";
  }
  return os;
}

namespace {
// Internal constants for the layout of the module object.
const int kWasmModuleFunctionTable = 0;
const int kWasmModuleCodeTable = 1;
const int kWasmMemArrayBuffer = 2;
const int kWasmGlobalsArrayBuffer = 3;
// TODO(clemensh): Remove function name array, extract names from module bytes.
const int kWasmFunctionNamesArray = 4;
const int kWasmModuleBytesString = 5;
const int kWasmDebugInfo = 6;
const int kWasmModuleInternalFieldCount = 7;

// TODO(mtrofin): Unnecessary once we stop using JS Heap for wasm code.
// For now, each field is expected to have the type commented by its side.
// The elements typed as "maybe" are optional. The others are mandatory. Since
// the compiled module is either obtained from the current v8 instance, or from
// a snapshot produced by a compatible (==identical) v8 instance, we simply
// fail at instantiation time, in the face of invalid data.
enum CompiledWasmObjectFields {
  kFunctions,          // FixedArray of Code
  kImportData,         // maybe FixedArray of FixedArray respecting the
                       // WasmImportMetadata structure.
  kExports,            // maybe FixedArray of JSFunction
  kStartupFunction,    // maybe JSFunction
  kModuleBytes,        // maybe String
  kFunctionNameTable,  // maybe ByteArray
  kMinRequiredMemory,  // Smi. an uint32_t
  // The following 2 are either together present or absent:
  kDataSegmentsInfo,  // maybe FixedArray of FixedArray respecting the
                      // WasmSegmentInfo structure
  kDataSegments,      // maybe ByteArray.

  kGlobalsSize,                 // Smi. an uint32_t
  kExportMem,                   // Smi. bool
  kOrigin,                      // Smi. ModuleOrigin
  kCompiledWasmObjectTableSize  // Sentinel value.
};

enum WasmImportMetadata {
  kModuleName,              // String
  kFunctionName,            // maybe String
  kOutputCount,             // Smi. an uint32_t
  kSignature,               // ByteArray. A copy of the data in FunctionSig
  kWasmImportDataTableSize  // Sentinel value.
};

enum WasmSegmentInfo {
  kDestAddr,            // Smi. an uint32_t
  kSourceSize,          // Smi. an uint32_t
  kWasmSegmentInfoSize  // Sentinel value.
};

uint32_t GetMinModuleMemSize(const WasmModule* module) {
  return WasmModule::kPageSize * module->min_mem_pages;
}

void LoadDataSegments(Handle<FixedArray> compiled_module, Address mem_addr,
                      size_t mem_size) {
  MaybeHandle<ByteArray> maybe_data =
      compiled_module->GetValue<ByteArray>(kDataSegments);
  MaybeHandle<FixedArray> maybe_segments =
      compiled_module->GetValue<FixedArray>(kDataSegmentsInfo);

  // We either have both or neither.
  CHECK(maybe_data.is_null() == maybe_segments.is_null());
  // If we have neither, we're done.
  if (maybe_data.is_null()) return;

  Handle<ByteArray> data = maybe_data.ToHandleChecked();
  Handle<FixedArray> segments = maybe_segments.ToHandleChecked();

  uint32_t last_extraction_pos = 0;
  for (int i = 0; i < segments->length(); ++i) {
    Handle<ByteArray> segment =
        Handle<ByteArray>(ByteArray::cast(segments->get(i)));
    uint32_t dest_addr = static_cast<uint32_t>(segment->get_int(kDestAddr));
    uint32_t source_size = static_cast<uint32_t>(segment->get_int(kSourceSize));
    CHECK_LT(dest_addr, mem_size);
    CHECK_LE(source_size, mem_size);
    CHECK_LE(dest_addr, mem_size - source_size);
    byte* addr = mem_addr + dest_addr;
    data->copy_out(last_extraction_pos, addr, source_size);
    last_extraction_pos += source_size;
  }
}

void SaveDataSegmentInfo(Factory* factory, const WasmModule* module,
                         Handle<FixedArray> compiled_module) {
  Handle<FixedArray> segments = factory->NewFixedArray(
      static_cast<int>(module->data_segments.size()), TENURED);
  uint32_t data_size = 0;
  for (const WasmDataSegment& segment : module->data_segments) {
    if (!segment.init) continue;
    if (segment.source_size == 0) continue;
    data_size += segment.source_size;
  }
  Handle<ByteArray> data = factory->NewByteArray(data_size, TENURED);

  uint32_t last_insertion_pos = 0;
  for (uint32_t i = 0; i < module->data_segments.size(); ++i) {
    const WasmDataSegment& segment = module->data_segments[i];
    if (!segment.init) continue;
    if (segment.source_size == 0) continue;
    Handle<ByteArray> js_segment =
        factory->NewByteArray(kWasmSegmentInfoSize * sizeof(uint32_t), TENURED);
    js_segment->set_int(kDestAddr, segment.dest_addr);
    js_segment->set_int(kSourceSize, segment.source_size);
    segments->set(i, *js_segment);
    data->copy_in(last_insertion_pos,
                  module->module_start + segment.source_offset,
                  segment.source_size);
    last_insertion_pos += segment.source_size;
  }
  compiled_module->set(kDataSegmentsInfo, *segments);
  compiled_module->set(kDataSegments, *data);
}

Handle<FixedArray> BuildFunctionTable(Isolate* isolate,
                                      const WasmModule* module) {
  // Compute the size of the indirect function table
  uint32_t table_size = module->FunctionTableSize();
  if (table_size == 0) {
    return Handle<FixedArray>::null();
  }

  Handle<FixedArray> fixed = isolate->factory()->NewFixedArray(2 * table_size);
  for (uint32_t i = 0;
       i < static_cast<uint32_t>(module->function_table.size());
       ++i) {
    const WasmFunction* function =
        &module->functions[module->function_table[i]];
    fixed->set(i, Smi::FromInt(function->sig_index));
  }
  // Set the remaining elements to -1 (instead of "undefined"). These
  // elements are accessed directly as SMIs (without a check). On 64-bit
  // platforms, it is possible to have the top bits of "undefined" take
  // small integer values (or zero), which are more likely to be equal to
  // the signature index we check against.
  for (uint32_t i = static_cast<uint32_t>(module->function_table.size());
       i < table_size;
       ++i) {
    fixed->set(i, Smi::FromInt(-1));
  }
  return fixed;
}

Handle<JSArrayBuffer> NewArrayBuffer(Isolate* isolate, size_t size) {
  if (size > (WasmModule::kMaxMemPages * WasmModule::kPageSize)) {
    // TODO(titzer): lift restriction on maximum memory allocated here.
    return Handle<JSArrayBuffer>::null();
  }
  void* memory = isolate->array_buffer_allocator()->Allocate(size);
  if (memory == nullptr) {
    return Handle<JSArrayBuffer>::null();
  }

#if DEBUG
  // Double check the API allocator actually zero-initialized the memory.
  const byte* bytes = reinterpret_cast<const byte*>(memory);
  for (size_t i = 0; i < size; ++i) {
    DCHECK_EQ(0, bytes[i]);
  }
#endif

  Handle<JSArrayBuffer> buffer = isolate->factory()->NewJSArrayBuffer();
  JSArrayBuffer::Setup(buffer, isolate, false, memory, static_cast<int>(size));
  buffer->set_is_neuterable(false);
  return buffer;
}

void RelocateInstanceCode(Handle<JSObject> instance, Address start,
                          uint32_t prev_size, uint32_t new_size) {
  Handle<FixedArray> functions = Handle<FixedArray>(
      FixedArray::cast(instance->GetInternalField(kWasmModuleCodeTable)));
  for (int i = 0; i < functions->length(); ++i) {
    Handle<Code> function = Handle<Code>(Code::cast(functions->get(i)));
    AllowDeferredHandleDereference embedding_raw_address;
    int mask = (1 << RelocInfo::WASM_MEMORY_REFERENCE) |
               (1 << RelocInfo::WASM_MEMORY_SIZE_REFERENCE);
    for (RelocIterator it(*function, mask); !it.done(); it.next()) {
      it.rinfo()->update_wasm_memory_reference(nullptr, start, prev_size,
                                               new_size);
    }
  }
}

// Allocate memory for a module instance as a new JSArrayBuffer.
Handle<JSArrayBuffer> AllocateMemory(ErrorThrower* thrower, Isolate* isolate,
                                     uint32_t min_mem_pages) {
  if (min_mem_pages > WasmModule::kMaxMemPages) {
    thrower->Error("Out of memory: wasm memory too large");
    return Handle<JSArrayBuffer>::null();
  }
  Handle<JSArrayBuffer> mem_buffer =
      NewArrayBuffer(isolate, min_mem_pages * WasmModule::kPageSize);

  if (mem_buffer.is_null()) {
    thrower->Error("Out of memory: wasm memory");
  }
  return mem_buffer;
}

void RelocateGlobals(Handle<JSObject> instance, Address globals_start) {
  Handle<FixedArray> functions = Handle<FixedArray>(
      FixedArray::cast(instance->GetInternalField(kWasmModuleCodeTable)));
  uint32_t function_count = static_cast<uint32_t>(functions->length());
  for (uint32_t i = 0; i < function_count; ++i) {
    Handle<Code> function = Handle<Code>(Code::cast(functions->get(i)));
    AllowDeferredHandleDereference embedding_raw_address;
    int mask = 1 << RelocInfo::WASM_GLOBAL_REFERENCE;
    for (RelocIterator it(*function, mask); !it.done(); it.next()) {
      it.rinfo()->update_wasm_global_reference(nullptr, globals_start);
    }
  }
}

Handle<Code> CreatePlaceholder(Factory* factory, uint32_t index,
                               Code::Kind kind) {
  // Create a placeholder code object and encode the corresponding index in
  // the {constant_pool_offset} field of the code object.
  // TODO(titzer): placeholder code objects are somewhat dangerous.
  static byte buffer[] = {0, 0, 0, 0, 0, 0, 0, 0};  // fake instructions.
  static CodeDesc desc = {buffer, 8, 8, 0, 0, nullptr, 0, nullptr};
  Handle<Code> code = factory->NewCode(desc, Code::KindField::encode(kind),
                                       Handle<Object>::null());
  code->set_constant_pool_offset(static_cast<int>(index) + kPlaceholderMarker);
  return code;
}

// TODO(mtrofin): remove when we stop relying on placeholders.
void InitializePlaceholders(Factory* factory,
                            std::vector<Handle<Code>>* placeholders,
                            size_t size) {
  DCHECK(placeholders->empty());
  placeholders->reserve(size);

  for (uint32_t i = 0; i < size; ++i) {
    placeholders->push_back(CreatePlaceholder(factory, i, Code::WASM_FUNCTION));
  }
}

bool LinkFunction(Handle<Code> unlinked,
                  const std::vector<Handle<Code>>& code_targets,
                  Code::Kind kind) {
  bool modified = false;
  int mode_mask = RelocInfo::kCodeTargetMask;
  AllowDeferredHandleDereference embedding_raw_address;
  for (RelocIterator it(*unlinked, mode_mask); !it.done(); it.next()) {
    RelocInfo::Mode mode = it.rinfo()->rmode();
    if (RelocInfo::IsCodeTarget(mode)) {
      Code* target =
          Code::GetCodeFromTargetAddress(it.rinfo()->target_address());
      if (target->kind() == kind &&
          target->constant_pool_offset() >= kPlaceholderMarker) {
        // Patch direct calls to placeholder code objects.
        uint32_t index = target->constant_pool_offset() - kPlaceholderMarker;
        CHECK(index < code_targets.size());
        Handle<Code> new_target = code_targets[index];
        if (target != *new_target) {
          it.rinfo()->set_target_address(new_target->instruction_start(),
                                         SKIP_WRITE_BARRIER, SKIP_ICACHE_FLUSH);
          modified = true;
        }
      }
    }
  }
  return modified;
}

void LinkModuleFunctions(Isolate* isolate,
                         std::vector<Handle<Code>>& functions) {
  for (size_t i = 0; i < functions.size(); ++i) {
    Handle<Code> code = functions[i];
    bool modified = LinkFunction(code, functions, Code::WASM_FUNCTION);
    if (modified) {
      Assembler::FlushICache(isolate, code->instruction_start(),
                             code->instruction_size());
    }
  }
}

void LinkImports(Isolate* isolate, std::vector<Handle<Code>>& functions,
                 const std::vector<Handle<Code>>& imports) {
  for (uint32_t i = 0; i < functions.size(); ++i) {
    Handle<Code> code = functions[i];
    bool modified = LinkFunction(code, imports, Code::WASM_TO_JS_FUNCTION);
    if (modified) {
      Assembler::FlushICache(isolate, code->instruction_start(),
                             code->instruction_size());
    }
  }
}

}  // namespace

WasmModule::WasmModule()
    : module_start(nullptr),
      module_end(nullptr),
      min_mem_pages(0),
      max_mem_pages(0),
      mem_export(false),
      mem_external(false),
      start_function_index(-1),
      origin(kWasmOrigin),
      globals_size(0),
      indirect_table_size(0),
      pending_tasks(new base::Semaphore(0)) {}

static MaybeHandle<JSFunction> ReportFFIError(
    ErrorThrower& thrower, const char* error, uint32_t index,
    Handle<String> module_name, MaybeHandle<String> function_name) {
  if (!function_name.is_null()) {
    Handle<String> function_name_handle = function_name.ToHandleChecked();
    thrower.Error("Import #%d module=\"%.*s\" function=\"%.*s\" error: %s",
                  index, module_name->length(), module_name->ToCString().get(),
                  function_name_handle->length(),
                  function_name_handle->ToCString().get(), error);
  } else {
    thrower.Error("Import #%d module=\"%.*s\" error: %s", index,
                  module_name->length(), module_name->ToCString().get(), error);
  }
  thrower.Error("Import ");
  return MaybeHandle<JSFunction>();
}

static MaybeHandle<JSFunction> LookupFunction(
    ErrorThrower& thrower, Factory* factory, Handle<JSReceiver> ffi,
    uint32_t index, Handle<String> module_name,
    MaybeHandle<String> function_name) {
  if (ffi.is_null()) {
    return ReportFFIError(thrower, "FFI is not an object", index, module_name,
                          function_name);
  }

  // Look up the module first.
  MaybeHandle<Object> result = Object::GetProperty(ffi, module_name);
  if (result.is_null()) {
    return ReportFFIError(thrower, "module not found", index, module_name,
                          function_name);
  }

  Handle<Object> module = result.ToHandleChecked();

  if (!module->IsJSReceiver()) {
    return ReportFFIError(thrower, "module is not an object or function", index,
                          module_name, function_name);
  }

  Handle<Object> function;
  if (!function_name.is_null()) {
    // Look up the function in the module.
    MaybeHandle<Object> result =
        Object::GetProperty(module, function_name.ToHandleChecked());
    if (result.is_null()) {
      return ReportFFIError(thrower, "function not found", index, module_name,
                            function_name);
    }
    function = result.ToHandleChecked();
  } else {
    // No function specified. Use the "default export".
    function = module;
  }

  if (!function->IsJSFunction()) {
    return ReportFFIError(thrower, "not a function", index, module_name,
                          function_name);
  }

  return Handle<JSFunction>::cast(function);
}

namespace {
// Fetches the compilation unit of a wasm function and executes its parallel
// phase.
bool FetchAndExecuteCompilationUnit(
    Isolate* isolate,
    std::vector<compiler::WasmCompilationUnit*>* compilation_units,
    std::queue<compiler::WasmCompilationUnit*>* executed_units,
    base::Mutex* result_mutex, base::AtomicNumber<size_t>* next_unit) {
  DisallowHeapAllocation no_allocation;
  DisallowHandleAllocation no_handles;
  DisallowHandleDereference no_deref;
  DisallowCodeDependencyChange no_dependency_change;

  // - 1 because AtomicIntrement returns the value after the atomic increment.
  size_t index = next_unit->Increment(1) - 1;
  if (index >= compilation_units->size()) {
    return false;
  }

  compiler::WasmCompilationUnit* unit = compilation_units->at(index);
  if (unit != nullptr) {
    unit->ExecuteCompilation();
    {
      base::LockGuard<base::Mutex> guard(result_mutex);
      executed_units->push(unit);
    }
  }
  return true;
}

class WasmCompilationTask : public CancelableTask {
 public:
  WasmCompilationTask(
      Isolate* isolate,
      std::vector<compiler::WasmCompilationUnit*>* compilation_units,
      std::queue<compiler::WasmCompilationUnit*>* executed_units,
      base::Semaphore* on_finished, base::Mutex* result_mutex,
      base::AtomicNumber<size_t>* next_unit)
      : CancelableTask(isolate),
        isolate_(isolate),
        compilation_units_(compilation_units),
        executed_units_(executed_units),
        on_finished_(on_finished),
        result_mutex_(result_mutex),
        next_unit_(next_unit) {}

  void RunInternal() override {
    while (FetchAndExecuteCompilationUnit(isolate_, compilation_units_,
                                          executed_units_, result_mutex_,
                                          next_unit_)) {
    }
    on_finished_->Signal();
  }

  Isolate* isolate_;
  std::vector<compiler::WasmCompilationUnit*>* compilation_units_;
  std::queue<compiler::WasmCompilationUnit*>* executed_units_;
  base::Semaphore* on_finished_;
  base::Mutex* result_mutex_;
  base::AtomicNumber<size_t>* next_unit_;
};

// Records statistics on the code generated by compiling WASM functions.
struct CodeStats {
  size_t code_size;
  size_t reloc_size;

  inline CodeStats() : code_size(0), reloc_size(0) {}

  inline void Record(Code* code) {
    if (FLAG_print_wasm_code_size) {
      code_size += code->body_size();
      reloc_size += code->relocation_info()->length();
    }
  }

  void Record(const std::vector<Handle<Code>>& functions) {
    if (FLAG_print_wasm_code_size) {
      for (Handle<Code> c : functions) Record(*c);
    }
  }

  void Record(Handle<FixedArray> functions) {
    if (FLAG_print_wasm_code_size) {
      DisallowHeapAllocation no_gc;
      for (int i = 0; i < functions->length(); ++i) {
        Code* code = Code::cast(functions->get(i));
        Record(code);
      }
    }
  }

  inline void Report() {
    if (FLAG_print_wasm_code_size) {
      PrintF("Total generated wasm code: %zu bytes\n", code_size);
      PrintF("Total generated wasm reloc: %zu bytes\n", reloc_size);
    }
  }
};

Handle<FixedArray> GetImportsMetadata(Factory* factory,
                                      const WasmModule* module) {
  Handle<FixedArray> ret = factory->NewFixedArray(
      static_cast<int>(module->import_table.size()), TENURED);
  for (size_t i = 0; i < module->import_table.size(); ++i) {
    const WasmImport& import = module->import_table[i];
    WasmName module_name = module->GetNameOrNull(import.module_name_offset,
                                                 import.module_name_length);
    WasmName function_name = module->GetNameOrNull(import.function_name_offset,
                                                   import.function_name_length);

    Handle<String> module_name_string =
        factory->InternalizeUtf8String(module_name);
    Handle<String> function_name_string =
        function_name.is_empty()
            ? Handle<String>::null()
            : factory->InternalizeUtf8String(function_name);
    Handle<ByteArray> sig =
        factory->NewByteArray(static_cast<int>(import.sig->parameter_count() +
                                               import.sig->return_count()),
                              TENURED);
    sig->copy_in(0, reinterpret_cast<const byte*>(import.sig->raw_data()),
                 sig->length());
    Handle<FixedArray> encoded_import =
        factory->NewFixedArray(kWasmImportDataTableSize, TENURED);
    encoded_import->set(kModuleName, *module_name_string);
    if (!function_name_string.is_null()) {
      encoded_import->set(kFunctionName, *function_name_string);
    }
    encoded_import->set(
        kOutputCount,
        Smi::FromInt(static_cast<int>(import.sig->return_count())));
    encoded_import->set(kSignature, *sig);
    ret->set(static_cast<int>(i), *encoded_import);
  }
  return ret;
}

bool CompileWrappersToImportedFunctions(Isolate* isolate,
                                        const Handle<JSReceiver> ffi,
                                        std::vector<Handle<Code>>& imports,
                                        Handle<FixedArray> import_data,
                                        ErrorThrower* thrower) {
  uint32_t import_count = static_cast<uint32_t>(import_data->length());
  if (import_count > 0) {
    imports.reserve(import_count);
    for (uint32_t index = 0; index < import_count; ++index) {
      Handle<FixedArray> data = import_data->GetValueChecked<FixedArray>(index);
      Handle<String> module_name = data->GetValueChecked<String>(kModuleName);
      MaybeHandle<String> function_name = data->GetValue<String>(kFunctionName);

      // TODO(mtrofin): this is an uint32_t, actually. We should rationalize
      // it when we rationalize signed/unsigned stuff.
      int ret_count = Smi::cast(data->get(kOutputCount))->value();
      CHECK(ret_count >= 0);
      Handle<ByteArray> sig_data = data->GetValueChecked<ByteArray>(kSignature);
      int sig_data_size = sig_data->length();
      int param_count = sig_data_size - ret_count;
      CHECK(param_count >= 0);

      MaybeHandle<JSFunction> function = LookupFunction(
          *thrower, isolate->factory(), ffi, index, module_name, function_name);
      if (function.is_null()) return false;

      FunctionSig sig(
          ret_count, param_count,
          reinterpret_cast<const MachineRepresentation*>(sig_data->data()));

      Handle<Code> code = compiler::CompileWasmToJSWrapper(
          isolate, function.ToHandleChecked(), &sig, index, module_name,
          function_name);

      imports.push_back(code);
    }
  }
  return true;
}

void InitializeParallelCompilation(
    Isolate* isolate, const std::vector<WasmFunction>& functions,
    std::vector<compiler::WasmCompilationUnit*>& compilation_units,
    ModuleEnv& module_env, ErrorThrower& thrower) {
  for (uint32_t i = FLAG_skip_compiling_wasm_funcs; i < functions.size(); ++i) {
    compilation_units[i] = new compiler::WasmCompilationUnit(
        &thrower, isolate, &module_env, &functions[i], i);
  }
}

uint32_t* StartCompilationTasks(
    Isolate* isolate,
    std::vector<compiler::WasmCompilationUnit*>& compilation_units,
    std::queue<compiler::WasmCompilationUnit*>& executed_units,
    base::Semaphore* pending_tasks, base::Mutex& result_mutex,
    base::AtomicNumber<size_t>& next_unit) {
  const size_t num_tasks =
      Min(static_cast<size_t>(FLAG_wasm_num_compilation_tasks),
          V8::GetCurrentPlatform()->NumberOfAvailableBackgroundThreads());
  uint32_t* task_ids = new uint32_t[num_tasks];
  for (size_t i = 0; i < num_tasks; ++i) {
    WasmCompilationTask* task =
        new WasmCompilationTask(isolate, &compilation_units, &executed_units,
                                pending_tasks, &result_mutex, &next_unit);
    task_ids[i] = task->id();
    V8::GetCurrentPlatform()->CallOnBackgroundThread(
        task, v8::Platform::kShortRunningTask);
  }
  return task_ids;
}

void WaitForCompilationTasks(Isolate* isolate, uint32_t* task_ids,
                             base::Semaphore* pending_tasks) {
  const size_t num_tasks =
      Min(static_cast<size_t>(FLAG_wasm_num_compilation_tasks),
          V8::GetCurrentPlatform()->NumberOfAvailableBackgroundThreads());
  for (size_t i = 0; i < num_tasks; ++i) {
    // If the task has not started yet, then we abort it. Otherwise we wait for
    // it to finish.
    if (!isolate->cancelable_task_manager()->TryAbort(task_ids[i])) {
      pending_tasks->Wait();
    }
  }
}

void FinishCompilationUnits(
    std::queue<compiler::WasmCompilationUnit*>& executed_units,
    std::vector<Handle<Code>>& results, base::Mutex& result_mutex) {
  while (true) {
    compiler::WasmCompilationUnit* unit = nullptr;
    {
      base::LockGuard<base::Mutex> guard(&result_mutex);
      if (executed_units.empty()) {
        break;
      }
      unit = executed_units.front();
      executed_units.pop();
    }
    int j = unit->index();
    results[j] = unit->FinishCompilation();
    delete unit;
  }
}

void CompileInParallel(Isolate* isolate, const WasmModule* module,
                       std::vector<Handle<Code>>& functions,
                       ErrorThrower* thrower, ModuleEnv* module_env) {
  // Data structures for the parallel compilation.
  std::vector<compiler::WasmCompilationUnit*> compilation_units(
      module->functions.size());
  std::queue<compiler::WasmCompilationUnit*> executed_units;

  //-----------------------------------------------------------------------
  // For parallel compilation:
  // 1) The main thread allocates a compilation unit for each wasm function
  //    and stores them in the vector {compilation_units}.
  // 2) The main thread spawns {WasmCompilationTask} instances which run on
  //    the background threads.
  // 3.a) The background threads and the main thread pick one compilation
  //      unit at a time and execute the parallel phase of the compilation
  //      unit. After finishing the execution of the parallel phase, the
  //      result is enqueued in {executed_units}.
  // 3.b) If {executed_units} contains a compilation unit, the main thread
  //      dequeues it and finishes the compilation.
  // 4) After the parallel phase of all compilation units has started, the
  //    main thread waits for all {WasmCompilationTask} instances to finish.
  // 5) The main thread finishes the compilation.

  // Turn on the {CanonicalHandleScope} so that the background threads can
  // use the node cache.
  CanonicalHandleScope canonical(isolate);

  // 1) The main thread allocates a compilation unit for each wasm function
  //    and stores them in the vector {compilation_units}.
  InitializeParallelCompilation(isolate, module->functions, compilation_units,
                                *module_env, *thrower);

  // Objects for the synchronization with the background threads.
  base::Mutex result_mutex;
  base::AtomicNumber<size_t> next_unit(
      static_cast<size_t>(FLAG_skip_compiling_wasm_funcs));

  // 2) The main thread spawns {WasmCompilationTask} instances which run on
  //    the background threads.
  base::SmartArrayPointer<uint32_t> task_ids(StartCompilationTasks(
      isolate, compilation_units, executed_units, module->pending_tasks.get(),
      result_mutex, next_unit));

  // 3.a) The background threads and the main thread pick one compilation
  //      unit at a time and execute the parallel phase of the compilation
  //      unit. After finishing the execution of the parallel phase, the
  //      result is enqueued in {executed_units}.
  while (FetchAndExecuteCompilationUnit(isolate, &compilation_units,
                                        &executed_units, &result_mutex,
                                        &next_unit)) {
    // 3.b) If {executed_units} contains a compilation unit, the main thread
    //      dequeues it and finishes the compilation unit. Compilation units
    //      are finished concurrently to the background threads to save
    //      memory.
    FinishCompilationUnits(executed_units, functions, result_mutex);
  }
  // 4) After the parallel phase of all compilation units has started, the
  //    main thread waits for all {WasmCompilationTask} instances to finish.
  WaitForCompilationTasks(isolate, task_ids.get(), module->pending_tasks.get());
  // Finish the compilation of the remaining compilation units.
  FinishCompilationUnits(executed_units, functions, result_mutex);
}

void CompileSequentially(Isolate* isolate, const WasmModule* module,
                         std::vector<Handle<Code>>& functions,
                         ErrorThrower* thrower, ModuleEnv* module_env) {
  DCHECK(!thrower->error());

  for (uint32_t i = FLAG_skip_compiling_wasm_funcs;
       i < module->functions.size(); ++i) {
    const WasmFunction& func = module->functions[i];

    DCHECK_EQ(i, func.func_index);
    WasmName str = module->GetName(func.name_offset, func.name_length);
    Handle<Code> code = Handle<Code>::null();
    // Compile the function.
    code = compiler::WasmCompilationUnit::CompileWasmFunction(
        thrower, isolate, module_env, &func);
    if (code.is_null()) {
      thrower->Error("Compilation of #%d:%.*s failed.", i, str.length(),
                     str.start());
      break;
    }
      // Install the code into the linker table.
    functions[i] = code;
  }
}

void PopulateFunctionTable(WasmModuleInstance* instance) {
  if (!instance->function_table.is_null()) {
    uint32_t table_size = instance->module->FunctionTableSize();
    DCHECK_EQ(table_size * 2, instance->function_table->length());
    uint32_t populated_table_size =
        static_cast<uint32_t>(instance->module->function_table.size());
    for (uint32_t i = 0; i < populated_table_size; ++i) {
    instance->function_table->set(
        i + table_size,
        *instance->function_code[instance->module->function_table[i]]);
    }
  }
}

void SetDebugSupport(Factory* factory, Handle<FixedArray> compiled_module,
                     Handle<JSObject> js_object) {
  MaybeHandle<String> module_bytes_string =
      compiled_module->GetValue<String>(kModuleBytes);
  if (!module_bytes_string.is_null()) {
    js_object->SetInternalField(kWasmModuleBytesString,
                                *module_bytes_string.ToHandleChecked());
  }
  Handle<FixedArray> functions = Handle<FixedArray>(
      FixedArray::cast(js_object->GetInternalField(kWasmModuleCodeTable)));

  for (int i = FLAG_skip_compiling_wasm_funcs; i < functions->length(); ++i) {
    Handle<Code> code = functions->GetValueChecked<Code>(i);
    DCHECK(code->deoptimization_data() == nullptr ||
           code->deoptimization_data()->length() == 0);
    Handle<FixedArray> deopt_data = factory->NewFixedArray(2, TENURED);
    if (!js_object.is_null()) {
      deopt_data->set(0, *js_object);
    }
    deopt_data->set(1, Smi::FromInt(static_cast<int>(i)));
    deopt_data->set_length(2);
    code->set_deoptimization_data(*deopt_data);
  }

  MaybeHandle<ByteArray> function_name_table =
      compiled_module->GetValue<ByteArray>(kFunctionNameTable);
  if (!function_name_table.is_null()) {
    js_object->SetInternalField(kWasmFunctionNamesArray,
                                *function_name_table.ToHandleChecked());
  }
}

bool SetupGlobals(Isolate* isolate, Handle<FixedArray> compiled_module,
                  Handle<JSObject> instance, ErrorThrower* thrower) {
  uint32_t globals_size = static_cast<uint32_t>(
      Smi::cast(compiled_module->get(kGlobalsSize))->value());
  if (globals_size > 0) {
    Handle<JSArrayBuffer> globals_buffer =
        NewArrayBuffer(isolate, globals_size);
    if (globals_buffer.is_null()) {
      thrower->Error("Out of memory: wasm globals");
      return false;
    }
    RelocateGlobals(instance,
                    static_cast<Address>(globals_buffer->backing_store()));
    instance->SetInternalField(kWasmGlobalsArrayBuffer, *globals_buffer);
  }
  return true;
}

bool SetupInstanceHeap(Isolate* isolate, Handle<FixedArray> compiled_module,
                       Handle<JSObject> instance, Handle<JSArrayBuffer> memory,
                       ErrorThrower* thrower) {
  uint32_t min_mem_pages = static_cast<uint32_t>(
      Smi::cast(compiled_module->get(kMinRequiredMemory))->value());
  isolate->counters()->wasm_min_mem_pages_count()->AddSample(min_mem_pages);
  // TODO(wasm): re-enable counter for max_mem_pages when we use that field.

  if (memory.is_null() && min_mem_pages > 0) {
    memory = AllocateMemory(thrower, isolate, min_mem_pages);
    if (memory.is_null()) {
      return false;
    }
  }

  if (!memory.is_null()) {
    instance->SetInternalField(kWasmMemArrayBuffer, *memory);
    Address mem_start = static_cast<Address>(memory->backing_store());
    uint32_t mem_size = static_cast<uint32_t>(memory->byte_length()->Number());
    RelocateInstanceCode(instance, mem_start,
                         WasmModule::kPageSize * min_mem_pages, mem_size);
    LoadDataSegments(compiled_module, mem_start, mem_size);
  }
  return true;
}

bool SetupImports(Isolate* isolate, Handle<FixedArray> compiled_module,
                  Handle<JSObject> instance, ErrorThrower* thrower,
                  Handle<JSReceiver> ffi, CodeStats* stats) {
  //-------------------------------------------------------------------------
  // Compile wrappers to imported functions.
  //-------------------------------------------------------------------------
  std::vector<Handle<Code>> import_code;
  MaybeHandle<FixedArray> maybe_import_data =
      compiled_module->GetValue<FixedArray>(kImportData);
  if (!maybe_import_data.is_null()) {
    Handle<FixedArray> import_data = maybe_import_data.ToHandleChecked();
    if (!CompileWrappersToImportedFunctions(isolate, ffi, import_code,
                                            import_data, thrower)) {
      return false;
    }
  }

  stats->Record(import_code);

  Handle<FixedArray> code_table = Handle<FixedArray>(
      FixedArray::cast(instance->GetInternalField(kWasmModuleCodeTable)));
  // TODO(mtrofin): get the code off std::vector and on FixedArray, for
  // consistency.
  std::vector<Handle<Code>> function_code(code_table->length());
  for (int i = 0; i < code_table->length(); ++i) {
    Handle<Code> code = Handle<Code>(Code::cast(code_table->get(i)));
    function_code[i] = code;
  }

  instance->SetInternalField(kWasmModuleFunctionTable, Smi::FromInt(0));
  LinkImports(isolate, function_code, import_code);
  return true;
}

bool SetupExportsObject(Handle<FixedArray> compiled_module, Isolate* isolate,
                        Handle<JSObject> instance, ErrorThrower* thrower,
                        CodeStats* stats) {
  Factory* factory = isolate->factory();
  bool mem_export =
      static_cast<bool>(Smi::cast(compiled_module->get(kExportMem))->value());
  ModuleOrigin origin = static_cast<ModuleOrigin>(
      Smi::cast(compiled_module->get(kOrigin))->value());

  MaybeHandle<FixedArray> maybe_exports =
      compiled_module->GetValue<FixedArray>(kExports);
  if (!maybe_exports.is_null() || mem_export) {
    PropertyDescriptor desc;
    desc.set_writable(false);

    Handle<JSObject> exports_object = instance;
    if (origin == kWasmOrigin) {
      // Create the "exports" object.
      Handle<JSFunction> object_function = Handle<JSFunction>(
          isolate->native_context()->object_function(), isolate);
      exports_object = factory->NewJSObject(object_function, TENURED);
      Handle<String> exports_name = factory->InternalizeUtf8String("exports");
      JSObject::AddProperty(instance, exports_name, exports_object, READ_ONLY);
    }
    if (!maybe_exports.is_null()) {
      Handle<FixedArray> exports = maybe_exports.ToHandleChecked();

      int exports_size = exports->length();
      for (int i = 0; i < exports_size; ++i) {
        if (thrower->error()) return false;
        Handle<JSFunction> function = exports->GetValueChecked<JSFunction>(i);
        stats->Record(function->code());
        Handle<String> name =
            Handle<String>(String::cast(function->shared()->name()));
        function->SetInternalField(0, *instance);

        desc.set_value(function);
        Maybe<bool> status = JSReceiver::DefineOwnProperty(
            isolate, exports_object, name, &desc, Object::THROW_ON_ERROR);
        if (!status.IsJust()) {
          thrower->Error("export of %.*s failed.", name->length(),
                         name->ToCString().get());
          return false;
        }
      }
    }
    if (mem_export) {
      // Export the memory as a named property.
      Handle<String> name = factory->InternalizeUtf8String("memory");
      Handle<JSArrayBuffer> memory = Handle<JSArrayBuffer>(
          JSArrayBuffer::cast(instance->GetInternalField(kWasmMemArrayBuffer)));
      JSObject::AddProperty(exports_object, name, memory, READ_ONLY);
    }
  }
  return true;
}

}  // namespace

MaybeHandle<FixedArray> WasmModule::CompileFunctions(Isolate* isolate) const {
  Factory* factory = isolate->factory();
  ErrorThrower thrower(isolate, "WasmModule::CompileFunctions()");

  MaybeHandle<FixedArray> nothing;

  WasmModuleInstance temp_instance_for_compilation(this);
  temp_instance_for_compilation.function_table =
      BuildFunctionTable(isolate, this);
  temp_instance_for_compilation.context = isolate->native_context();
  temp_instance_for_compilation.mem_size = GetMinModuleMemSize(this);
  temp_instance_for_compilation.mem_start = nullptr;
  temp_instance_for_compilation.globals_start = nullptr;

  HistogramTimerScope wasm_compile_module_time_scope(
      isolate->counters()->wasm_compile_module_time());

  ModuleEnv module_env;
  module_env.module = this;
  module_env.instance = &temp_instance_for_compilation;
  module_env.origin = origin;
  InitializePlaceholders(factory, &module_env.placeholders, functions.size());

  Handle<FixedArray> compiled_functions =
      factory->NewFixedArray(static_cast<int>(functions.size()), TENURED);

  temp_instance_for_compilation.import_code.resize(import_table.size());
  for (uint32_t i = 0; i < import_table.size(); ++i) {
    temp_instance_for_compilation.import_code[i] =
        CreatePlaceholder(factory, i, Code::WASM_TO_JS_FUNCTION);
  }
  isolate->counters()->wasm_functions_per_module()->AddSample(
      static_cast<int>(functions.size()));
  if (FLAG_wasm_num_compilation_tasks != 0) {
    CompileInParallel(isolate, this,
                      temp_instance_for_compilation.function_code, &thrower,
                      &module_env);
  } else {
    CompileSequentially(isolate, this,
                        temp_instance_for_compilation.function_code, &thrower,
                        &module_env);
  }
  if (thrower.error()) return nothing;

  LinkModuleFunctions(isolate, temp_instance_for_compilation.function_code);

  // At this point, compilation has completed. Update the code table.
  for (size_t i = FLAG_skip_compiling_wasm_funcs;
       i < temp_instance_for_compilation.function_code.size(); ++i) {
    Code* code = *temp_instance_for_compilation.function_code[i];
    compiled_functions->set(static_cast<int>(i), code);
  }

  PopulateFunctionTable(&temp_instance_for_compilation);

  // Create the compiled module object, and populate with compiled functions
  // and information needed at instantiation time. This object needs to be
  // serializable. Instantiation may occur off a deserialized version of this
  // object.
  Handle<FixedArray> ret =
      factory->NewFixedArray(kCompiledWasmObjectTableSize, TENURED);
  ret->set(kFunctions, *compiled_functions);

  Handle<FixedArray> import_data = GetImportsMetadata(factory, this);
  ret->set(kImportData, *import_data);

  // Compile export functions.
  int export_size = static_cast<int>(export_table.size());
  Handle<JSFunction> startup_fct;
  if (export_size > 0) {
    Handle<FixedArray> exports = factory->NewFixedArray(export_size, TENURED);
    for (int i = 0; i < export_size; ++i) {
      const WasmExport& exp = export_table[i];
      WasmName str = GetName(exp.name_offset, exp.name_length);
      Handle<String> name = factory->InternalizeUtf8String(str);
      Handle<Code> code =
          temp_instance_for_compilation.function_code[exp.func_index];
      Handle<JSFunction> function = compiler::CompileJSToWasmWrapper(
          isolate, &module_env, name, code, exp.func_index);
      if (thrower.error()) return nothing;
      exports->set(i, *function);
      if (exp.func_index == start_function_index) {
        startup_fct = function;
      }
    }
    ret->set(kExports, *exports);
  }

  // Compile startup function, if we haven't already.
  if (start_function_index >= 0) {
    HandleScope scope(isolate);
    if (startup_fct.is_null()) {
      uint32_t index = static_cast<uint32_t>(start_function_index);
      Handle<String> name = factory->NewStringFromStaticChars("start");
      Handle<Code> code = temp_instance_for_compilation.function_code[index];
      startup_fct = compiler::CompileJSToWasmWrapper(isolate, &module_env, name,
                                                     code, index);
    }
    ret->set(kStartupFunction, *startup_fct);
  }

  // TODO(wasm): saving the module bytes for debugging is wasteful. We should
  // consider downloading this on-demand.
  {
    size_t module_bytes_len = module_end - module_start;
    DCHECK_LE(module_bytes_len, static_cast<size_t>(kMaxInt));
    Vector<const uint8_t> module_bytes_vec(module_start,
                                           static_cast<int>(module_bytes_len));
    Handle<String> module_bytes_string =
        factory->NewStringFromOneByte(module_bytes_vec, TENURED)
            .ToHandleChecked();
    ret->set(kModuleBytes, *module_bytes_string);
  }

  Handle<ByteArray> function_name_table =
      BuildFunctionNamesTable(isolate, module_env.module);
  ret->set(kFunctionNameTable, *function_name_table);
  ret->set(kMinRequiredMemory, Smi::FromInt(min_mem_pages));
  if (data_segments.size() > 0) SaveDataSegmentInfo(factory, this, ret);
  ret->set(kGlobalsSize, Smi::FromInt(globals_size));
  ret->set(kExportMem, Smi::FromInt(mem_export));
  ret->set(kOrigin, Smi::FromInt(origin));
  return ret;
}

// Instantiates a wasm module as a JSObject.
//  * allocates a backing store of {mem_size} bytes.
//  * installs a named property "memory" for that buffer if exported
//  * installs named properties on the object for exported functions
//  * compiles wasm code to machine code
MaybeHandle<JSObject> WasmModule::Instantiate(
    Isolate* isolate, Handle<FixedArray> compiled_module,
    Handle<JSReceiver> ffi, Handle<JSArrayBuffer> memory) {
  HistogramTimerScope wasm_instantiate_module_time_scope(
      isolate->counters()->wasm_instantiate_module_time());
  ErrorThrower thrower(isolate, "WasmModule::Instantiate()");
  Factory* factory = isolate->factory();

  CodeStats code_stats;
  // These fields are compulsory.
  Handle<FixedArray> code_table =
      compiled_module->GetValueChecked<FixedArray>(kFunctions);

  code_stats.Record(code_table);

  MaybeHandle<JSObject> nothing;

  Handle<Map> map = factory->NewMap(
      JS_OBJECT_TYPE,
      JSObject::kHeaderSize + kWasmModuleInternalFieldCount * kPointerSize);
  Handle<JSObject> js_object = factory->NewJSObjectFromMap(map, TENURED);
  js_object->SetInternalField(kWasmModuleCodeTable, *code_table);

  if (!(SetupInstanceHeap(isolate, compiled_module, js_object, memory,
                          &thrower) &&
        SetupGlobals(isolate, compiled_module, js_object, &thrower) &&
        SetupImports(isolate, compiled_module, js_object, &thrower, ffi,
                     &code_stats) &&
        SetupExportsObject(compiled_module, isolate, js_object, &thrower,
                           &code_stats))) {
    return nothing;
  }

  SetDebugSupport(factory, compiled_module, js_object);

  // Run the start function if one was specified.
  MaybeHandle<JSFunction> maybe_startup_fct =
      compiled_module->GetValue<JSFunction>(kStartupFunction);
  if (!maybe_startup_fct.is_null()) {
    HandleScope scope(isolate);
    Handle<JSFunction> startup_fct = maybe_startup_fct.ToHandleChecked();
    code_stats.Record(startup_fct->code());
    startup_fct->SetInternalField(0, *js_object);
    // Call the JS function.
    Handle<Object> undefined = isolate->factory()->undefined_value();
    MaybeHandle<Object> retval =
        Execution::Call(isolate, startup_fct, undefined, 0, nullptr);

    if (retval.is_null()) {
      thrower.Error("WASM.instantiateModule(): start function failed");
      return nothing;
    }
  }

  code_stats.Report();

  DCHECK(wasm::IsWasmObject(*js_object));
  return js_object;
}

// TODO(mtrofin): remove this once we move to WASM_DIRECT_CALL
Handle<Code> ModuleEnv::GetCodeOrPlaceholder(uint32_t index) const {
  DCHECK(IsValidFunction(index));
  if (!placeholders.empty()) return placeholders[index];
  DCHECK_NOT_NULL(instance);
  return instance->function_code[index];
}

Handle<Code> ModuleEnv::GetImportCode(uint32_t index) {
  DCHECK(IsValidImport(index));
  return instance ? instance->import_code[index] : Handle<Code>::null();
}

compiler::CallDescriptor* ModuleEnv::GetCallDescriptor(Zone* zone,
                                                       uint32_t index) {
  DCHECK(IsValidFunction(index));
  // Always make a direct call to whatever is in the table at that location.
  // A wrapper will be generated for FFI calls.
  const WasmFunction* function = &module->functions[index];
  return GetWasmCallDescriptor(zone, function->sig);
}

Handle<Object> GetWasmFunctionNameOrNull(Isolate* isolate, Handle<Object> wasm,
                                         uint32_t func_index) {
  if (!wasm->IsUndefined(isolate)) {
    Handle<ByteArray> func_names_arr_obj(
        ByteArray::cast(Handle<JSObject>::cast(wasm)->GetInternalField(
            kWasmFunctionNamesArray)),
        isolate);
    // TODO(clemens): Extract this from the module bytes; skip whole function
    // name table.
    Handle<Object> name;
    if (GetWasmFunctionNameFromTable(func_names_arr_obj, func_index)
            .ToHandle(&name)) {
      return name;
    }
  }
  return isolate->factory()->null_value();
}

Handle<String> GetWasmFunctionName(Isolate* isolate, Handle<Object> wasm,
                                   uint32_t func_index) {
  Handle<Object> name_or_null =
      GetWasmFunctionNameOrNull(isolate, wasm, func_index);
  if (!name_or_null->IsNull(isolate)) {
    return Handle<String>::cast(name_or_null);
  }
  return isolate->factory()->NewStringFromStaticChars("<WASM UNNAMED>");
}

bool IsWasmObject(Object* object) {
  if (!object->IsJSObject()) return false;

  JSObject* obj = JSObject::cast(object);
  Isolate* isolate = obj->GetIsolate();
  if (obj->GetInternalFieldCount() != kWasmModuleInternalFieldCount) {
    return false;
  }

  Object* mem = obj->GetInternalField(kWasmMemArrayBuffer);
  if (obj->GetInternalField(kWasmModuleCodeTable)->IsFixedArray() &&
      (mem->IsUndefined(isolate) || mem->IsJSArrayBuffer()) &&
      obj->GetInternalField(kWasmFunctionNamesArray)->IsByteArray()) {
    Object* debug_bytes = obj->GetInternalField(kWasmModuleBytesString);
    if (!debug_bytes->IsUndefined(isolate)) {
      if (!debug_bytes->IsSeqOneByteString()) {
        return false;
      }
      DisallowHeapAllocation no_gc;
      SeqOneByteString* bytes = SeqOneByteString::cast(debug_bytes);
      if (bytes->length() < 4) return false;
      if (memcmp(bytes->GetChars(), "\0asm", 4)) return false;
      // All checks passed.
    }
    return true;
  }
  return false;
}

SeqOneByteString* GetWasmBytes(JSObject* wasm) {
  return SeqOneByteString::cast(wasm->GetInternalField(kWasmModuleBytesString));
}

WasmDebugInfo* GetDebugInfo(JSObject* wasm) {
  Object* info = wasm->GetInternalField(kWasmDebugInfo);
  if (!info->IsUndefined(wasm->GetIsolate())) return WasmDebugInfo::cast(info);
  Handle<WasmDebugInfo> new_info = WasmDebugInfo::New(handle(wasm));
  wasm->SetInternalField(kWasmDebugInfo, *new_info);
  return *new_info;
}

bool UpdateWasmModuleMemory(Handle<JSObject> object, Address old_start,
                            Address new_start, uint32_t old_size,
                            uint32_t new_size) {
  DisallowHeapAllocation no_allocation;
  if (!IsWasmObject(*object)) {
    return false;
  }

  // Get code table associated with the module js_object
  Object* obj = object->GetInternalField(kWasmModuleCodeTable);
  Handle<FixedArray> code_table(FixedArray::cast(obj));

  // Iterate through the code objects in the code table and update relocation
  // information
  for (int i = 0; i < code_table->length(); i++) {
    obj = code_table->get(i);
    Handle<Code> code(Code::cast(obj));

    int mode_mask = RelocInfo::ModeMask(RelocInfo::WASM_MEMORY_REFERENCE) |
                    RelocInfo::ModeMask(RelocInfo::WASM_MEMORY_SIZE_REFERENCE);
    for (RelocIterator it(*code, mode_mask); !it.done(); it.next()) {
      RelocInfo::Mode mode = it.rinfo()->rmode();
      if (RelocInfo::IsWasmMemoryReference(mode) ||
          RelocInfo::IsWasmMemorySizeReference(mode)) {
        it.rinfo()->update_wasm_memory_reference(old_start, new_start, old_size,
                                                 new_size);
      }
    }
  }
  return true;
}

int GetNumberOfFunctions(JSObject* wasm) {
  Object* func_names_obj = wasm->GetInternalField(kWasmFunctionNamesArray);
  // TODO(clemensh): this looks inside an array constructed elsewhere. Refactor.
  return ByteArray::cast(func_names_obj)->get_int(0);
}

namespace testing {

int32_t CompileAndRunWasmModule(Isolate* isolate, const byte* module_start,
                                const byte* module_end, bool asm_js) {
  HandleScope scope(isolate);
  Zone zone(isolate->allocator());
  ErrorThrower thrower(isolate, "CompileAndRunWasmModule");

  // Decode the module, but don't verify function bodies, since we'll
  // be compiling them anyway.
  ModuleResult decoding_result =
      DecodeWasmModule(isolate, &zone, module_start, module_end, false,
                       asm_js ? kAsmJsOrigin : kWasmOrigin);

  std::unique_ptr<const WasmModule> module(decoding_result.val);
  if (decoding_result.failed()) {
    // Module verification failed. throw.
    thrower.Error("WASM.compileRun() failed: %s",
                  decoding_result.error_msg.get());
    return -1;
  }

  if (module->import_table.size() > 0) {
    thrower.Error("Not supported: module has imports.");
  }
  if (module->export_table.size() == 0) {
    thrower.Error("Not supported: module has no exports.");
  }

  if (thrower.error()) return -1;
  MaybeHandle<FixedArray> compiled_module = module->CompileFunctions(isolate);

  if (compiled_module.is_null()) return -1;
  Handle<JSObject> instance =
      module
          ->Instantiate(isolate, compiled_module.ToHandleChecked(),
                        Handle<JSReceiver>::null(),
                        Handle<JSArrayBuffer>::null())
          .ToHandleChecked();

  Handle<Name> exports = isolate->factory()->InternalizeUtf8String("exports");
  Handle<JSObject> exports_object = Handle<JSObject>::cast(
      JSObject::GetProperty(instance, exports).ToHandleChecked());
  Handle<Name> main_name = isolate->factory()->NewStringFromStaticChars("main");
  PropertyDescriptor desc;
  Maybe<bool> property_found = JSReceiver::GetOwnPropertyDescriptor(
      isolate, exports_object, main_name, &desc);
  if (!property_found.FromMaybe(false)) return -1;

  Handle<JSFunction> main_export = Handle<JSFunction>::cast(desc.value());

  // Call the JS function.
  Handle<Object> undefined = isolate->factory()->undefined_value();
  MaybeHandle<Object> retval =
      Execution::Call(isolate, main_export, undefined, 0, nullptr);

  // The result should be a number.
  if (retval.is_null()) {
    thrower.Error("WASM.compileRun() failed: Invocation was null");
    return -1;
  }
  Handle<Object> result = retval.ToHandleChecked();
  if (result->IsSmi()) {
    return Smi::cast(*result)->value();
  }
  if (result->IsHeapNumber()) {
    return static_cast<int32_t>(HeapNumber::cast(*result)->value());
  }
  thrower.Error("WASM.compileRun() failed: Return value should be number");
  return -1;
}

}  // namespace testing
}  // namespace wasm
}  // namespace internal
}  // namespace v8
