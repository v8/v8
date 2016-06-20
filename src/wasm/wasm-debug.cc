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
  kWasmDebugInfoNumEntries
};

ByteArray *GetOrCreateFunctionOffsetTable(Handle<WasmDebugInfo> debug_info) {
  Object *offset_table = debug_info->get(kWasmDebugInfoFunctionByteOffsets);
  Isolate *isolate = debug_info->GetIsolate();
  if (!offset_table->IsUndefined(isolate)) return ByteArray::cast(offset_table);

  FunctionOffsetsResult function_offsets;
  {
    DisallowHeapAllocation no_gc;
    SeqOneByteString *wasm_bytes =
        wasm::GetWasmBytes(debug_info->wasm_object());
    const byte *bytes_start = wasm_bytes->GetChars();
    const byte *bytes_end = bytes_start + wasm_bytes->length();
    function_offsets = wasm::DecodeWasmFunctionOffsets(bytes_start, bytes_end);
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
  SeqOneByteString *module_bytes =
      wasm::GetWasmBytes(debug_info->wasm_object());
  std::pair<int, int> offset_and_length =
      GetFunctionOffsetAndLength(debug_info, func_index);
  return Vector<const uint8_t>(
      module_bytes->GetChars() + offset_and_length.first,
      offset_and_length.second);
}

}  // namespace

Handle<WasmDebugInfo> WasmDebugInfo::New(Handle<JSObject> wasm) {
  Isolate *isolate = wasm->GetIsolate();
  Factory *factory = isolate->factory();
  Handle<FixedArray> arr =
      factory->NewFixedArray(kWasmDebugInfoNumEntries, TENURED);
  arr->set(kWasmDebugInfoWasmObj, *wasm);
  int hash = 0;
  Handle<SeqOneByteString> wasm_bytes(GetWasmBytes(*wasm), isolate);
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
          arr->get(kWasmDebugInfoFunctionByteOffsets)->IsByteArray());
}

WasmDebugInfo *WasmDebugInfo::cast(Object *object) {
  DCHECK(IsDebugInfo(object));
  return reinterpret_cast<WasmDebugInfo *>(object);
}

JSObject *WasmDebugInfo::wasm_object() {
  return JSObject::cast(get(kWasmDebugInfoWasmObj));
}

bool WasmDebugInfo::SetBreakPoint(int byte_offset) {
  // TODO(clemensh): Implement this.
  return false;
}

Handle<String> WasmDebugInfo::DisassembleFunction(
    Handle<WasmDebugInfo> debug_info, int func_index) {
  std::ostringstream disassembly_os;

  {
    Vector<const uint8_t> bytes_vec = GetFunctionBytes(debug_info, func_index);
    DisallowHeapAllocation no_gc;

    base::AccountingAllocator allocator;
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

    v8::base::AccountingAllocator allocator;
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
