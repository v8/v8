// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/init-expr-interface.h"

#include "src/execution/isolate.h"
#include "src/handles/handles-inl.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/oddball.h"
#include "src/roots/roots-inl.h"
#include "src/wasm/decoder.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/wasm-objects.h"

namespace v8 {
namespace internal {
namespace wasm {

void InitExprInterface::I32Const(FullDecoder* decoder, Value* result,
                                 int32_t value) {
  if (generate_result()) result->runtime_value = WasmValue(value);
}

void InitExprInterface::I64Const(FullDecoder* decoder, Value* result,
                                 int64_t value) {
  if (generate_result()) result->runtime_value = WasmValue(value);
}

void InitExprInterface::F32Const(FullDecoder* decoder, Value* result,
                                 float value) {
  if (generate_result()) result->runtime_value = WasmValue(value);
}

void InitExprInterface::F64Const(FullDecoder* decoder, Value* result,
                                 double value) {
  if (generate_result()) result->runtime_value = WasmValue(value);
}

void InitExprInterface::S128Const(FullDecoder* decoder,
                                  Simd128Immediate<validate>& imm,
                                  Value* result) {
  if (!generate_result()) return;
  result->runtime_value = WasmValue(imm.value, kWasmS128);
}

void InitExprInterface::RefNull(FullDecoder* decoder, ValueType type,
                                Value* result) {
  if (!generate_result()) return;
  result->runtime_value = WasmValue(isolate_->factory()->null_value(), type);
}

void InitExprInterface::RefFunc(FullDecoder* decoder, uint32_t function_index,
                                Value* result) {
  if (isolate_ == nullptr) {
    outer_module_->functions[function_index].declared = true;
    return;
  }
  if (!generate_result()) return;
  ValueType type = ValueType::Ref(module_->functions[function_index].sig_index,
                                  kNonNullable);
  Handle<WasmInternalFunction> internal =
      WasmInstanceObject::GetOrCreateWasmInternalFunction(isolate_, instance_,
                                                          function_index);
  result->runtime_value = WasmValue(internal, type);
}

void InitExprInterface::GlobalGet(FullDecoder* decoder, Value* result,
                                  const GlobalIndexImmediate<validate>& imm) {
  if (!generate_result()) return;
  const WasmGlobal& global = module_->globals[imm.index];
  DCHECK(!global.mutability);
  result->runtime_value =
      global.type.is_numeric()
          ? WasmValue(
                reinterpret_cast<byte*>(
                    instance_->untagged_globals_buffer().backing_store()) +
                    global.offset,
                global.type)
          : WasmValue(
                handle(instance_->tagged_globals_buffer().get(global.offset),
                       isolate_),
                global.type);
}

void InitExprInterface::StructNewWithRtt(
    FullDecoder* decoder, const StructIndexImmediate<validate>& imm,
    const Value& rtt, const Value args[], Value* result) {
  if (!generate_result()) return;
  std::vector<WasmValue> field_values(imm.struct_type->field_count());
  for (size_t i = 0; i < field_values.size(); i++) {
    field_values[i] = args[i].runtime_value;
  }
  result->runtime_value =
      WasmValue(isolate_->factory()->NewWasmStruct(
                    imm.struct_type, field_values.data(),
                    Handle<Map>::cast(rtt.runtime_value.to_ref())),
                ValueType::Ref(HeapType(imm.index), kNonNullable));
}

namespace {
WasmValue DefaultValueForType(ValueType type, Isolate* isolate) {
  switch (type.kind()) {
    case kI32:
    case kI8:
    case kI16:
      return WasmValue(0);
    case kI64:
      return WasmValue(int64_t{0});
    case kF32:
      return WasmValue(0.0f);
    case kF64:
      return WasmValue(0.0);
    case kS128:
      return WasmValue(Simd128());
    case kOptRef:
      return WasmValue(isolate->factory()->null_value(), type);
    case kVoid:
    case kRtt:
    case kRttWithDepth:
    case kRef:
    case kBottom:
      UNREACHABLE();
  }
}
}  // namespace

void InitExprInterface::StructNewDefault(
    FullDecoder* decoder, const StructIndexImmediate<validate>& imm,
    const Value& rtt, Value* result) {
  if (!generate_result()) return;
  std::vector<WasmValue> field_values(imm.struct_type->field_count());
  for (uint32_t i = 0; i < field_values.size(); i++) {
    field_values[i] = DefaultValueForType(imm.struct_type->field(i), isolate_);
  }
  result->runtime_value =
      WasmValue(isolate_->factory()->NewWasmStruct(
                    imm.struct_type, field_values.data(),
                    Handle<Map>::cast(rtt.runtime_value.to_ref())),
                ValueType::Ref(HeapType(imm.index), kNonNullable));
}

void InitExprInterface::ArrayInit(FullDecoder* decoder,
                                  const ArrayIndexImmediate<validate>& imm,
                                  const base::Vector<Value>& elements,
                                  const Value& rtt, Value* result) {
  if (!generate_result()) return;
  std::vector<WasmValue> element_values;
  for (Value elem : elements) element_values.push_back(elem.runtime_value);
  result->runtime_value =
      WasmValue(isolate_->factory()->NewWasmArrayFromElements(
                    imm.array_type, element_values,
                    Handle<Map>::cast(rtt.runtime_value.to_ref())),
                ValueType::Ref(HeapType(imm.index), kNonNullable));
}

void InitExprInterface::RttCanon(FullDecoder* decoder, uint32_t type_index,
                                 Value* result) {
  if (!generate_result()) return;
  result->runtime_value = WasmValue(
      handle(instance_->managed_object_maps().get(type_index), isolate_),
      ValueType::Rtt(type_index, 0));
}

void InitExprInterface::RttSub(FullDecoder* decoder, uint32_t type_index,
                               const Value& parent, Value* result,
                               WasmRttSubMode mode) {
  if (!generate_result()) return;
  ValueType type = parent.type.has_depth()
                       ? ValueType::Rtt(type_index, parent.type.depth() + 1)
                       : ValueType::Rtt(type_index);
  result->runtime_value =
      WasmValue(Handle<Object>::cast(AllocateSubRtt(
                    isolate_, instance_, type_index,
                    Handle<Map>::cast(parent.runtime_value.to_ref()), mode)),
                type);
}

void InitExprInterface::DoReturn(FullDecoder* decoder,
                                 uint32_t /*drop_values*/) {
  end_found_ = true;
  // End decoding on "end".
  decoder->set_end(decoder->pc() + 1);
  if (generate_result()) result_ = decoder->stack_value(1)->runtime_value;
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
