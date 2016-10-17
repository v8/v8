// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-debug.h"

#include "src/assert-scope.h"
#include "src/debug/debug.h"
#include "src/factory.h"
#include "src/isolate.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-module.h"

using namespace v8::internal;
using namespace v8::internal::wasm;

namespace {

enum {
  kWasmDebugInfoWasmObj,
  kWasmDebugInfoWasmBytesHash,
  kWasmDebugInfoFunctionByteOffsets,
  kWasmDebugInfoFunctionScripts,
  kWasmDebugInfoAsmJsOffsets,
  kWasmDebugInfoNumEntries
};

ByteArray *GetOrCreateFunctionOffsetTable(Handle<WasmDebugInfo> debug_info) {
  Object *offset_table = debug_info->get(kWasmDebugInfoFunctionByteOffsets);
  Isolate *isolate = debug_info->GetIsolate();
  if (!offset_table->IsUndefined(isolate)) return ByteArray::cast(offset_table);

  FunctionOffsetsResult function_offsets;
  {
    Handle<JSObject> wasm_object(debug_info->wasm_object(), isolate);
    uint32_t num_imported_functions =
        static_cast<uint32_t>(wasm::GetNumImportedFunctions(wasm_object));
    Handle<SeqOneByteString> wasm_bytes = wasm::GetWasmBytes(wasm_object);
    DisallowHeapAllocation no_gc;
    const byte *bytes_start = wasm_bytes->GetChars();
    const byte *bytes_end = bytes_start + wasm_bytes->length();
    function_offsets = wasm::DecodeWasmFunctionOffsets(bytes_start, bytes_end,
                                                       num_imported_functions);
  }
  DCHECK(function_offsets.ok());
  size_t array_size = 2 * kIntSize * function_offsets.val.size();
  CHECK_LE(array_size, static_cast<size_t>(kMaxInt));
  ByteArray *arr =
      *isolate->factory()->NewByteArray(static_cast<int>(array_size));
  int idx = 0;
  for (std::pair<int, int> p : function_offsets.val) {
    arr->set_int(idx++, p.first);
    arr->set_int(idx++, p.second);
  }
  DCHECK_EQ(arr->length(), idx * kIntSize);
  debug_info->set(kWasmDebugInfoFunctionByteOffsets, arr);

  return arr;
}

std::pair<int, int> GetFunctionOffsetAndLength(Handle<WasmDebugInfo> debug_info,
                                               int func_index) {
  ByteArray *arr = GetOrCreateFunctionOffsetTable(debug_info);
  DCHECK(func_index >= 0 && func_index < arr->length() / kIntSize / 2);

  int offset = arr->get_int(2 * func_index);
  int length = arr->get_int(2 * func_index + 1);
  // Assert that it's distinguishable from the "illegal function index" return.
  DCHECK(offset > 0 && length > 0);
  return {offset, length};
}

Vector<const uint8_t> GetFunctionBytes(Handle<WasmDebugInfo> debug_info,
                                       int func_index) {
  Handle<JSObject> wasm_object(debug_info->wasm_object());
  Handle<SeqOneByteString> module_bytes = wasm::GetWasmBytes(wasm_object);
  std::pair<int, int> offset_and_length =
      GetFunctionOffsetAndLength(debug_info, func_index);
  return Vector<const uint8_t>(
      module_bytes->GetChars() + offset_and_length.first,
      offset_and_length.second);
}

FixedArray *GetOffsetTables(Handle<WasmDebugInfo> debug_info,
                            Isolate *isolate) {
  Object *offset_tables = debug_info->get(kWasmDebugInfoAsmJsOffsets);
  if (!offset_tables->IsUndefined(isolate)) {
    return FixedArray::cast(offset_tables);
  }

  AsmJsOffsetsResult asm_offsets;
  {
    Handle<JSObject> wasm_object(debug_info->wasm_object(), isolate);
    Handle<WasmCompiledModule> compiled_module =
        handle(GetCompiledModule(*wasm_object), isolate);
    DCHECK(compiled_module->has_asm_js_offset_tables());
    Handle<ByteArray> asm_offset_tables =
        compiled_module->asm_js_offset_tables();
    uint32_t num_imported_functions =
        static_cast<uint32_t>(wasm::GetNumImportedFunctions(wasm_object));
    DisallowHeapAllocation no_gc;
    const byte *bytes_start = asm_offset_tables->GetDataStartAddress();
    const byte *bytes_end = bytes_start + asm_offset_tables->length();
    asm_offsets = wasm::DecodeAsmJsOffsets(bytes_start, bytes_end,
                                           num_imported_functions);
  }
  // Wasm bytes must be valid and must contain asm.js offset table.
  DCHECK(asm_offsets.ok());
  DCHECK_GE(static_cast<size_t>(kMaxInt), asm_offsets.val.size());
  int num_functions = static_cast<int>(asm_offsets.val.size());
  DCHECK_EQ(wasm::GetNumberOfFunctions(handle(debug_info->wasm_object())),
            num_functions);
  Handle<FixedArray> all_tables =
      isolate->factory()->NewFixedArray(num_functions);
  debug_info->set(kWasmDebugInfoAsmJsOffsets, *all_tables);
  for (int func = 0; func < num_functions; ++func) {
    std::vector<std::pair<int, int>> &func_asm_offsets = asm_offsets.val[func];
    if (func_asm_offsets.empty()) continue;
    size_t array_size = 2 * kIntSize * func_asm_offsets.size();
    CHECK_LE(array_size, static_cast<size_t>(kMaxInt));
    ByteArray *arr =
        *isolate->factory()->NewByteArray(static_cast<int>(array_size));
    all_tables->set(func, arr);
    int idx = 0;
    for (std::pair<int, int> p : func_asm_offsets) {
      // Byte offsets must be strictly monotonously increasing:
      DCHECK(idx == 0 || p.first > arr->get_int(idx - 2));
      arr->set_int(idx++, p.first);
      arr->set_int(idx++, p.second);
    }
    DCHECK_EQ(arr->length(), idx * kIntSize);
  }
  return *all_tables;
}
}  // namespace

Handle<WasmDebugInfo> WasmDebugInfo::New(Handle<JSObject> wasm) {
  Isolate *isolate = wasm->GetIsolate();
  Factory *factory = isolate->factory();
  Handle<FixedArray> arr =
      factory->NewFixedArray(kWasmDebugInfoNumEntries, TENURED);
  arr->set(kWasmDebugInfoWasmObj, *wasm);
  int hash = 0;
  Handle<SeqOneByteString> wasm_bytes = GetWasmBytes(wasm);
  {
    DisallowHeapAllocation no_gc;
    hash = StringHasher::HashSequentialString(
        wasm_bytes->GetChars(), wasm_bytes->length(), kZeroHashSeed);
  }
  Handle<Object> hash_obj = factory->NewNumberFromInt(hash, TENURED);
  arr->set(kWasmDebugInfoWasmBytesHash, *hash_obj);

  return Handle<WasmDebugInfo>::cast(arr);
}

bool WasmDebugInfo::IsDebugInfo(Object *object) {
  if (!object->IsFixedArray()) return false;
  FixedArray *arr = FixedArray::cast(object);
  Isolate *isolate = arr->GetIsolate();
  return arr->length() == kWasmDebugInfoNumEntries &&
         IsWasmObject(arr->get(kWasmDebugInfoWasmObj)) &&
         arr->get(kWasmDebugInfoWasmBytesHash)->IsNumber() &&
         (arr->get(kWasmDebugInfoFunctionByteOffsets)->IsUndefined(isolate) ||
          arr->get(kWasmDebugInfoFunctionByteOffsets)->IsByteArray()) &&
         (arr->get(kWasmDebugInfoFunctionScripts)->IsUndefined(isolate) ||
          arr->get(kWasmDebugInfoFunctionScripts)->IsFixedArray());
}

WasmDebugInfo *WasmDebugInfo::cast(Object *object) {
  DCHECK(IsDebugInfo(object));
  return reinterpret_cast<WasmDebugInfo *>(object);
}

JSObject *WasmDebugInfo::wasm_object() {
  return JSObject::cast(get(kWasmDebugInfoWasmObj));
}

Script *WasmDebugInfo::GetFunctionScript(Handle<WasmDebugInfo> debug_info,
                                         int func_index) {
  Isolate *isolate = debug_info->GetIsolate();
  Object *scripts_obj = debug_info->get(kWasmDebugInfoFunctionScripts);
  Handle<FixedArray> scripts;
  if (scripts_obj->IsUndefined(isolate)) {
    Handle<JSObject> wasm_object(debug_info->wasm_object(), isolate);
    int num_functions = wasm::GetNumberOfFunctions(wasm_object);
    scripts = isolate->factory()->NewFixedArray(num_functions, TENURED);
    debug_info->set(kWasmDebugInfoFunctionScripts, *scripts);
  } else {
    scripts = handle(FixedArray::cast(scripts_obj), isolate);
  }

  DCHECK(func_index >= 0 && func_index < scripts->length());
  Object *script_or_undef = scripts->get(func_index);
  if (!script_or_undef->IsUndefined(isolate)) {
    return Script::cast(script_or_undef);
  }

  Handle<Script> script =
      isolate->factory()->NewScript(isolate->factory()->empty_string());
  scripts->set(func_index, *script);

  script->set_type(Script::TYPE_WASM);
  script->set_wasm_object(debug_info->wasm_object());
  script->set_wasm_function_index(func_index);

  int hash = 0;
  debug_info->get(kWasmDebugInfoWasmBytesHash)->ToInt32(&hash);
  char buffer[32];
  SNPrintF(ArrayVector(buffer), "wasm://%08x/%d", hash, func_index);
  Handle<String> source_url =
      isolate->factory()->NewStringFromAsciiChecked(buffer, TENURED);
  script->set_source_url(*source_url);

  int func_bytes_len =
      GetFunctionOffsetAndLength(debug_info, func_index).second;
  Handle<FixedArray> line_ends = isolate->factory()->NewFixedArray(1, TENURED);
  line_ends->set(0, Smi::FromInt(func_bytes_len));
  line_ends->set_map(isolate->heap()->fixed_cow_array_map());
  script->set_line_ends(*line_ends);

  // TODO(clemensh): Register with the debugger. Note that we cannot call into
  // JS at this point since this function is called from within stack trace
  // collection (which means we cannot call Debug::OnAfterCompile in its
  // current form). See crbug.com/641065.
  if (false) isolate->debug()->OnAfterCompile(script);

  return *script;
}

Handle<String> WasmDebugInfo::DisassembleFunction(
    Handle<WasmDebugInfo> debug_info, int func_index) {
  std::ostringstream disassembly_os;

  {
    Vector<const uint8_t> bytes_vec = GetFunctionBytes(debug_info, func_index);
    DisallowHeapAllocation no_gc;

    AccountingAllocator allocator;
    bool ok = PrintAst(
        &allocator, FunctionBodyForTesting(bytes_vec.start(), bytes_vec.end()),
        disassembly_os, nullptr);
    DCHECK(ok);
    USE(ok);
  }

  // Unfortunately, we have to copy the string here.
  std::string code_str = disassembly_os.str();
  CHECK_LE(code_str.length(), static_cast<size_t>(kMaxInt));
  Factory *factory = debug_info->GetIsolate()->factory();
  Vector<const char> code_vec(code_str.data(),
                              static_cast<int>(code_str.length()));
  return factory->NewStringFromAscii(code_vec).ToHandleChecked();
}

Handle<FixedArray> WasmDebugInfo::GetFunctionOffsetTable(
    Handle<WasmDebugInfo> debug_info, int func_index) {
  class NullBuf : public std::streambuf {};
  NullBuf null_buf;
  std::ostream null_stream(&null_buf);

  std::vector<std::tuple<uint32_t, int, int>> offset_table_vec;

  {
    Vector<const uint8_t> bytes_vec = GetFunctionBytes(debug_info, func_index);
    DisallowHeapAllocation no_gc;

    AccountingAllocator allocator;
    bool ok = PrintAst(
        &allocator, FunctionBodyForTesting(bytes_vec.start(), bytes_vec.end()),
        null_stream, &offset_table_vec);
    DCHECK(ok);
    USE(ok);
  }

  size_t arr_size = 3 * offset_table_vec.size();
  CHECK_LE(arr_size, static_cast<size_t>(kMaxInt));
  Factory *factory = debug_info->GetIsolate()->factory();
  Handle<FixedArray> offset_table =
      factory->NewFixedArray(static_cast<int>(arr_size), TENURED);

  int idx = 0;
  for (std::tuple<uint32_t, int, int> elem : offset_table_vec) {
    offset_table->set(idx++, Smi::FromInt(std::get<0>(elem)));
    offset_table->set(idx++, Smi::FromInt(std::get<1>(elem)));
    offset_table->set(idx++, Smi::FromInt(std::get<2>(elem)));
  }
  DCHECK_EQ(idx, offset_table->length());

  return offset_table;
}

int WasmDebugInfo::GetAsmJsSourcePosition(Handle<WasmDebugInfo> debug_info,
                                          int func_index, int byte_offset) {
  Isolate *isolate = debug_info->GetIsolate();
  FixedArray *offset_tables = GetOffsetTables(debug_info, isolate);

  DCHECK_LT(func_index, offset_tables->length());
  ByteArray *offset_table = ByteArray::cast(offset_tables->get(func_index));

  // Binary search for the current byte offset.
  int left = 0;                                       // inclusive
  int right = offset_table->length() / kIntSize / 2;  // exclusive
  DCHECK_LT(left, right);
  while (right - left > 1) {
    int mid = left + (right - left) / 2;
    if (offset_table->get_int(2 * mid) < byte_offset) {
      left = mid;
    } else {
      right = mid;
    }
  }
  // There should be an entry for each position that could show up on the stack
  // trace:
  DCHECK_EQ(byte_offset, offset_table->get_int(2 * left));
  return offset_table->get_int(2 * left + 1);
}
