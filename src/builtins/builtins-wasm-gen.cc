// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/codegen/code-stub-assembler.h"
#include "src/codegen/interface-descriptors.h"
#include "src/objects/objects-inl.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-opcodes.h"

namespace v8 {
namespace internal {

class WasmBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit WasmBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

 protected:
  TNode<WasmInstanceObject> LoadInstanceFromFrame() {
    return CAST(
        LoadFromParentFrame(WasmCompiledFrameConstants::kWasmInstanceOffset));
  }

  TNode<Context> LoadContextFromInstance(TNode<WasmInstanceObject> instance) {
    return CAST(Load(MachineType::AnyTagged(), instance,
                     IntPtrConstant(WasmInstanceObject::kNativeContextOffset -
                                    kHeapObjectTag)));
  }

  TNode<Smi> SmiFromUint32WithSaturation(TNode<Uint32T> value, uint32_t max) {
    DCHECK_LE(max, static_cast<uint32_t>(Smi::kMaxValue));
    TNode<Uint32T> capped_value = SelectConstant(
        Uint32LessThan(value, Uint32Constant(max)), value, Uint32Constant(max));
    return SmiFromUint32(capped_value);
  }
};

TF_BUILTIN(WasmInt32ToHeapNumber, WasmBuiltinsAssembler) {
  TNode<Int32T> val = UncheckedCast<Int32T>(Parameter(Descriptor::kValue));
  Return(AllocateHeapNumberWithValue(ChangeInt32ToFloat64(val)));
}

TF_BUILTIN(WasmTaggedNonSmiToInt32, WasmBuiltinsAssembler) {
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));
  Return(
      ChangeTaggedNonSmiToInt32(context, CAST(Parameter(Descriptor::kValue))));
}

TF_BUILTIN(WasmFloat32ToNumber, WasmBuiltinsAssembler) {
  TNode<Float32T> val = UncheckedCast<Float32T>(Parameter(Descriptor::kValue));
  Return(ChangeFloat32ToTagged(val));
}

TF_BUILTIN(WasmFloat64ToNumber, WasmBuiltinsAssembler) {
  TNode<Float64T> val = UncheckedCast<Float64T>(Parameter(Descriptor::kValue));
  Return(ChangeFloat64ToTagged(val));
}

TF_BUILTIN(WasmTaggedToFloat64, WasmBuiltinsAssembler) {
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));
  Return(ChangeTaggedToFloat64(context, CAST(Parameter(Descriptor::kValue))));
}

TF_BUILTIN(WasmStackGuard, WasmBuiltinsAssembler) {
  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();
  TNode<Context> context = LoadContextFromInstance(instance);
  TailCallRuntime(Runtime::kWasmStackGuard, context);
}

TF_BUILTIN(WasmStackOverflow, WasmBuiltinsAssembler) {
  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();
  TNode<Context> context = LoadContextFromInstance(instance);
  TailCallRuntime(Runtime::kThrowWasmStackOverflow, context);
}

TF_BUILTIN(WasmThrow, WasmBuiltinsAssembler) {
  TNode<Object> exception = CAST(Parameter(Descriptor::kException));
  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();
  TNode<Context> context = LoadContextFromInstance(instance);
  TailCallRuntime(Runtime::kThrow, context, exception);
}

TF_BUILTIN(WasmRethrow, WasmBuiltinsAssembler) {
  TNode<Object> exception = CAST(Parameter(Descriptor::kException));
  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();
  TNode<Context> context = LoadContextFromInstance(instance);

  Label nullref(this, Label::kDeferred);
  GotoIf(TaggedEqual(NullConstant(), exception), &nullref);

  TailCallRuntime(Runtime::kReThrow, context, exception);

  BIND(&nullref);
  MessageTemplate message_id = MessageTemplate::kWasmTrapRethrowNullRef;
  TailCallRuntime(Runtime::kThrowWasmError, context,
                  SmiConstant(static_cast<int>(message_id)));
}

TF_BUILTIN(WasmTraceMemory, WasmBuiltinsAssembler) {
  TNode<Smi> info = CAST(Parameter(Descriptor::kMemoryTracingInfo));
  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();
  TNode<Context> context = LoadContextFromInstance(instance);
  TailCallRuntime(Runtime::kWasmTraceMemory, context, info);
}

TF_BUILTIN(WasmAllocateJSArray, WasmBuiltinsAssembler) {
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));
  TNode<Smi> array_size = CAST(Parameter(Descriptor::kArraySize));

  TNode<Map> array_map = CAST(
      LoadContextElement(context, Context::JS_ARRAY_PACKED_ELEMENTS_MAP_INDEX));

  Return(CodeStubAssembler::AllocateJSArray(PACKED_ELEMENTS, array_map,
                                            array_size, array_size));
}

TF_BUILTIN(WasmAtomicNotify, WasmBuiltinsAssembler) {
  TNode<Uint32T> address =
      UncheckedCast<Uint32T>(Parameter(Descriptor::kAddress));
  TNode<Uint32T> count = UncheckedCast<Uint32T>(Parameter(Descriptor::kCount));

  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();
  TNode<Number> address_number = ChangeUint32ToTagged(address);
  TNode<Number> count_number = ChangeUint32ToTagged(count);
  TNode<Context> context = LoadContextFromInstance(instance);

  TNode<Smi> result_smi =
      CAST(CallRuntime(Runtime::kWasmAtomicNotify, context, instance,
                       address_number, count_number));
  Return(Unsigned(SmiToInt32(result_smi)));
}

TF_BUILTIN(WasmI32AtomicWait32, WasmBuiltinsAssembler) {
  if (!Is32()) {
    Unreachable();
    return;
  }

  TNode<Uint32T> address =
      UncheckedCast<Uint32T>(Parameter(Descriptor::kAddress));
  TNode<Number> address_number = ChangeUint32ToTagged(address);

  TNode<Int32T> expected_value =
      UncheckedCast<Int32T>(Parameter(Descriptor::kExpectedValue));
  TNode<Number> expected_value_number = ChangeInt32ToTagged(expected_value);

  TNode<IntPtrT> timeout_low =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kTimeoutLow));
  TNode<IntPtrT> timeout_high =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kTimeoutHigh));
  TNode<BigInt> timeout = BigIntFromInt32Pair(timeout_low, timeout_high);

  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();
  TNode<Context> context = LoadContextFromInstance(instance);

  TNode<Smi> result_smi =
      CAST(CallRuntime(Runtime::kWasmI32AtomicWait, context, instance,
                       address_number, expected_value_number, timeout));
  Return(Unsigned(SmiToInt32(result_smi)));
}

TF_BUILTIN(WasmI32AtomicWait64, WasmBuiltinsAssembler) {
  if (!Is64()) {
    Unreachable();
    return;
  }

  TNode<Uint32T> address =
      UncheckedCast<Uint32T>(Parameter(Descriptor::kAddress));
  TNode<Number> address_number = ChangeUint32ToTagged(address);

  TNode<Int32T> expected_value =
      UncheckedCast<Int32T>(Parameter(Descriptor::kExpectedValue));
  TNode<Number> expected_value_number = ChangeInt32ToTagged(expected_value);

  TNode<IntPtrT> timeout_raw =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kTimeout));
  TNode<BigInt> timeout = BigIntFromInt64(timeout_raw);

  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();
  TNode<Context> context = LoadContextFromInstance(instance);

  TNode<Smi> result_smi =
      CAST(CallRuntime(Runtime::kWasmI32AtomicWait, context, instance,
                       address_number, expected_value_number, timeout));
  Return(Unsigned(SmiToInt32(result_smi)));
}

TF_BUILTIN(WasmI64AtomicWait32, WasmBuiltinsAssembler) {
  if (!Is32()) {
    Unreachable();
    return;
  }

  TNode<Uint32T> address =
      UncheckedCast<Uint32T>(Parameter(Descriptor::kAddress));
  TNode<Number> address_number = ChangeUint32ToTagged(address);

  TNode<IntPtrT> expected_value_low =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kExpectedValueLow));
  TNode<IntPtrT> expected_value_high =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kExpectedValueHigh));
  TNode<BigInt> expected_value =
      BigIntFromInt32Pair(expected_value_low, expected_value_high);

  TNode<IntPtrT> timeout_low =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kTimeoutLow));
  TNode<IntPtrT> timeout_high =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kTimeoutHigh));
  TNode<BigInt> timeout = BigIntFromInt32Pair(timeout_low, timeout_high);

  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();
  TNode<Context> context = LoadContextFromInstance(instance);

  TNode<Smi> result_smi =
      CAST(CallRuntime(Runtime::kWasmI64AtomicWait, context, instance,
                       address_number, expected_value, timeout));
  Return(Unsigned(SmiToInt32(result_smi)));
}

TF_BUILTIN(WasmI64AtomicWait64, WasmBuiltinsAssembler) {
  if (!Is64()) {
    Unreachable();
    return;
  }

  TNode<Uint32T> address =
      UncheckedCast<Uint32T>(Parameter(Descriptor::kAddress));
  TNode<Number> address_number = ChangeUint32ToTagged(address);

  TNode<IntPtrT> expected_value_raw =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kExpectedValue));
  TNode<BigInt> expected_value = BigIntFromInt64(expected_value_raw);

  TNode<IntPtrT> timeout_raw =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kTimeout));
  TNode<BigInt> timeout = BigIntFromInt64(timeout_raw);

  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();
  TNode<Context> context = LoadContextFromInstance(instance);

  TNode<Smi> result_smi =
      CAST(CallRuntime(Runtime::kWasmI64AtomicWait, context, instance,
                       address_number, expected_value, timeout));
  Return(Unsigned(SmiToInt32(result_smi)));
}

TF_BUILTIN(WasmMemoryGrow, WasmBuiltinsAssembler) {
  TNode<Int32T> num_pages =
      UncheckedCast<Int32T>(Parameter(Descriptor::kNumPages));
  Label num_pages_out_of_range(this, Label::kDeferred);

  TNode<BoolT> num_pages_fits_in_smi =
      IsValidPositiveSmi(ChangeInt32ToIntPtr(num_pages));
  GotoIfNot(num_pages_fits_in_smi, &num_pages_out_of_range);

  TNode<Smi> num_pages_smi = SmiFromInt32(num_pages);
  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();
  TNode<Context> context = LoadContextFromInstance(instance);
  TNode<Smi> ret_smi = CAST(
      CallRuntime(Runtime::kWasmMemoryGrow, context, instance, num_pages_smi));
  Return(SmiToInt32(ret_smi));

  BIND(&num_pages_out_of_range);
  Return(Int32Constant(-1));
}

TF_BUILTIN(WasmRefFunc, WasmBuiltinsAssembler) {
  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();

  Label call_runtime(this, Label::kDeferred);

  TNode<Uint32T> raw_index =
      UncheckedCast<Uint32T>(Parameter(Descriptor::kFunctionIndex));
  TNode<FixedArray> table = LoadObjectField<FixedArray>(
      instance, WasmInstanceObject::kWasmExternalFunctionsOffset);
  GotoIf(IsUndefined(table), &call_runtime);

  TNode<IntPtrT> function_index =
      UncheckedCast<IntPtrT>(ChangeUint32ToWord(raw_index));
  // Function index should be in range.
  TNode<Object> result = LoadFixedArrayElement(table, function_index);
  GotoIf(IsUndefined(result), &call_runtime);

  Return(result);

  BIND(&call_runtime);
  // Fall back to the runtime call for more complex cases.
  // function_index is known to be in Smi range.
  TailCallRuntime(Runtime::kWasmRefFunc, LoadContextFromInstance(instance),
                  instance, SmiFromUint32(raw_index));
}

TF_BUILTIN(WasmTableInit, WasmBuiltinsAssembler) {
  TNode<Uint32T> dst_raw =
      UncheckedCast<Uint32T>(Parameter(Descriptor::kDestination));
  // We cap {dst}, {src}, and {size} by {wasm::kV8MaxWasmTableSize + 1} to make
  // sure that the values fit into a Smi.
  STATIC_ASSERT(static_cast<size_t>(Smi::kMaxValue) >=
                wasm::kV8MaxWasmTableSize + 1);
  constexpr uint32_t kCap =
      static_cast<uint32_t>(wasm::kV8MaxWasmTableSize + 1);
  TNode<Smi> dst = SmiFromUint32WithSaturation(dst_raw, kCap);
  TNode<Uint32T> src_raw =
      UncheckedCast<Uint32T>(Parameter(Descriptor::kSource));
  TNode<Smi> src = SmiFromUint32WithSaturation(src_raw, kCap);
  TNode<Uint32T> size_raw =
      UncheckedCast<Uint32T>(Parameter(Descriptor::kSize));
  TNode<Smi> size = SmiFromUint32WithSaturation(size_raw, kCap);
  TNode<Smi> table_index =
      UncheckedCast<Smi>(Parameter(Descriptor::kTableIndex));
  TNode<Smi> segment_index =
      UncheckedCast<Smi>(Parameter(Descriptor::kSegmentIndex));
  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();
  TNode<Context> context = LoadContextFromInstance(instance);

  TailCallRuntime(Runtime::kWasmTableInit, context, instance, table_index,
                  segment_index, dst, src, size);
}

TF_BUILTIN(WasmTableCopy, WasmBuiltinsAssembler) {
  // We cap {dst}, {src}, and {size} by {wasm::kV8MaxWasmTableSize + 1} to make
  // sure that the values fit into a Smi.
  STATIC_ASSERT(static_cast<size_t>(Smi::kMaxValue) >=
                wasm::kV8MaxWasmTableSize + 1);
  constexpr uint32_t kCap =
      static_cast<uint32_t>(wasm::kV8MaxWasmTableSize + 1);

  TNode<Uint32T> dst_raw =
      UncheckedCast<Uint32T>(Parameter(Descriptor::kDestination));
  TNode<Smi> dst = SmiFromUint32WithSaturation(dst_raw, kCap);

  TNode<Uint32T> src_raw =
      UncheckedCast<Uint32T>(Parameter(Descriptor::kSource));
  TNode<Smi> src = SmiFromUint32WithSaturation(src_raw, kCap);

  TNode<Uint32T> size_raw =
      UncheckedCast<Uint32T>(Parameter(Descriptor::kSize));
  TNode<Smi> size = SmiFromUint32WithSaturation(size_raw, kCap);

  TNode<Smi> dst_table =
      UncheckedCast<Smi>(Parameter(Descriptor::kDestinationTable));

  TNode<Smi> src_table =
      UncheckedCast<Smi>(Parameter(Descriptor::kSourceTable));

  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();
  TNode<Context> context = LoadContextFromInstance(instance);

  TailCallRuntime(Runtime::kWasmTableCopy, context, instance, dst_table,
                  src_table, dst, src, size);
}

TF_BUILTIN(WasmTableGet, WasmBuiltinsAssembler) {
  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();

  Label call_runtime(this, Label::kDeferred),
      index_out_of_range(this, Label::kDeferred);

  TNode<IntPtrT> table_index =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kTableIndex));
  GotoIfNot(IsValidPositiveSmi(table_index), &index_out_of_range);
  TNode<IntPtrT> entry_index = ChangeInt32ToIntPtr(
      UncheckedCast<Int32T>(Parameter(Descriptor::kEntryIndex)));
  GotoIfNot(IsValidPositiveSmi(entry_index), &index_out_of_range);

  TNode<FixedArray> tables_array =
      LoadObjectField<FixedArray>(instance, WasmInstanceObject::kTablesOffset);
  TNode<WasmTableObject> table =
      CAST(LoadFixedArrayElement(tables_array, table_index));
  TNode<IntPtrT> entries_length =
      LoadAndUntagObjectField(table, WasmTableObject::kCurrentLengthOffset);
  GotoIfNot(IntPtrLessThan(entry_index, entries_length), &index_out_of_range);

  TNode<FixedArray> entries_array =
      LoadObjectField<FixedArray>(table, WasmTableObject::kEntriesOffset);

  TNode<Object> entry = LoadFixedArrayElement(entries_array, entry_index);

  // If the entry is our placeholder for lazy function initialization, then we
  // fall back to the runtime call.
  TNode<Map> map = LoadReceiverMap(entry);
  GotoIf(IsTuple2Map(map), &call_runtime);

  Return(entry);

  BIND(&call_runtime);
  // Fall back to the runtime call for more complex cases.
  // table_index and entry_index must be in Smi range, due to checks above.
  TailCallRuntime(Runtime::kWasmFunctionTableGet,
                  LoadContextFromInstance(instance), instance,
                  SmiFromIntPtr(table_index), SmiFromIntPtr(entry_index));

  BIND(&index_out_of_range);
  MessageTemplate message_id =
      wasm::WasmOpcodes::TrapReasonToMessageId(wasm::kTrapTableOutOfBounds);
  TailCallRuntime(Runtime::kThrowWasmError, LoadContextFromInstance(instance),
                  SmiConstant(static_cast<int>(message_id)));
}

TF_BUILTIN(WasmTableSet, WasmBuiltinsAssembler) {
  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();

  Label call_runtime(this, Label::kDeferred),
      index_out_of_range(this, Label::kDeferred);

  TNode<IntPtrT> table_index =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kTableIndex));
  GotoIfNot(IsValidPositiveSmi(table_index), &index_out_of_range);
  TNode<IntPtrT> entry_index = ChangeInt32ToIntPtr(
      UncheckedCast<Int32T>(Parameter(Descriptor::kEntryIndex)));
  GotoIfNot(IsValidPositiveSmi(entry_index), &index_out_of_range);

  TNode<Object> value = CAST(Parameter(Descriptor::kValue));

  TNode<FixedArray> tables_array =
      LoadObjectField<FixedArray>(instance, WasmInstanceObject::kTablesOffset);
  TNode<WasmTableObject> table =
      CAST(LoadFixedArrayElement(tables_array, table_index));
  // Fall back to the runtime to set funcrefs, since we have to update function
  // dispatch tables.
  TNode<Smi> table_type =
      LoadObjectField<Smi>(table, WasmTableObject::kRawTypeOffset);
  GotoIf(SmiEqual(table_type, SmiConstant(wasm::ValueType::Kind::kFuncRef)),
         &call_runtime);

  TNode<IntPtrT> entries_length =
      LoadAndUntagObjectField(table, WasmTableObject::kCurrentLengthOffset);
  GotoIfNot(IntPtrLessThan(entry_index, entries_length), &index_out_of_range);

  TNode<FixedArray> entries_array =
      LoadObjectField<FixedArray>(table, WasmTableObject::kEntriesOffset);

  StoreFixedArrayElement(entries_array, entry_index, value);
  Return(UndefinedConstant());

  BIND(&call_runtime);
  // Fall back to the runtime call for more complex cases.
  // table_index and entry_index must be in Smi range, due to checks above.
  TailCallRuntime(
      Runtime::kWasmFunctionTableSet, LoadContextFromInstance(instance),
      instance, SmiFromIntPtr(table_index), SmiFromIntPtr(entry_index), value);

  BIND(&index_out_of_range);
  MessageTemplate message_id =
      wasm::WasmOpcodes::TrapReasonToMessageId(wasm::kTrapTableOutOfBounds);
  TailCallRuntime(Runtime::kThrowWasmError, LoadContextFromInstance(instance),
                  SmiConstant(static_cast<int>(message_id)));
}

#define DECLARE_THROW_RUNTIME_FN(name)                            \
  TF_BUILTIN(ThrowWasm##name, WasmBuiltinsAssembler) {            \
    TNode<WasmInstanceObject> instance = LoadInstanceFromFrame(); \
    TNode<Context> context = LoadContextFromInstance(instance);   \
    MessageTemplate message_id =                                  \
        wasm::WasmOpcodes::TrapReasonToMessageId(wasm::k##name);  \
    TailCallRuntime(Runtime::kThrowWasmError, context,            \
                    SmiConstant(static_cast<int>(message_id)));   \
  }
FOREACH_WASM_TRAPREASON(DECLARE_THROW_RUNTIME_FN)
#undef DECLARE_THROW_RUNTIME_FN

}  // namespace internal
}  // namespace v8
