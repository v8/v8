// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iomanip>

#include "src/execution/arguments-inl.h"
#include "src/execution/frames-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-decoder.h"
#include "src/interpreter/bytecode-flags-and-tokens.h"
#include "src/interpreter/bytecode-register.h"
#include "src/interpreter/bytecodes.h"
#include "src/interpreter/interpreter.h"
#include "src/logging/counters.h"
#include "src/runtime/runtime-utils.h"
#include "src/snapshot/snapshot.h"
#include "src/utils/ostreams.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/memory-tracing.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

#ifdef V8_TRACE_UNOPTIMIZED

namespace {

void AdvanceToOffsetForTracing(
    interpreter::BytecodeArrayIterator& bytecode_iterator, int offset) {
  while (bytecode_iterator.current_offset() +
             bytecode_iterator.current_bytecode_size() <=
         offset) {
    bytecode_iterator.Advance();
  }
  DCHECK(bytecode_iterator.current_offset() == offset ||
         ((bytecode_iterator.current_offset() + 1) == offset &&
          bytecode_iterator.current_operand_scale() >
              interpreter::OperandScale::kSingle));
}

void PrintRegisterRange(UnoptimizedJSFrame* frame, std::ostream& os,
                        interpreter::BytecodeArrayIterator& bytecode_iterator,
                        const int& reg_field_width, const char* arrow_direction,
                        interpreter::Register first_reg, int range) {
  for (int reg_index = first_reg.index(); reg_index < first_reg.index() + range;
       reg_index++) {
    Tagged<Object> reg_object = frame->ReadInterpreterRegister(reg_index);
    os << "      [ " << std::setw(reg_field_width)
       << interpreter::Register(reg_index).ToString() << arrow_direction;
    ShortPrint(reg_object, os);
    os << " ]" << std::endl;
  }
}

void PrintRegisters(UnoptimizedJSFrame* frame, std::ostream& os, bool is_input,
                    interpreter::BytecodeArrayIterator& bytecode_iterator,
                    Handle<Object> accumulator) {
  static const char kAccumulator[] = "accumulator";
  static const int kRegFieldWidth = static_cast<int>(sizeof(kAccumulator) - 1);
  static const char* kInputColourCode = "\033[0;36m";
  static const char* kOutputColourCode = "\033[0;35m";
  static const char* kNormalColourCode = "\033[0;m";
  const char* kArrowDirection = is_input ? " -> " : " <- ";
  if (v8_flags.log_colour) {
    os << (is_input ? kInputColourCode : kOutputColourCode);
  }

  interpreter::Bytecode bytecode = bytecode_iterator.current_bytecode();

  // Print accumulator.
  if ((is_input && interpreter::Bytecodes::ReadsAccumulator(bytecode)) ||
      (!is_input &&
       interpreter::Bytecodes::WritesOrClobbersAccumulator(bytecode))) {
    os << "      [ " << kAccumulator << kArrowDirection;
    ShortPrint(*accumulator, os);
    os << " ]" << std::endl;
  }

  // Print the registers.
  int operand_count = interpreter::Bytecodes::NumberOfOperands(bytecode);
  for (int operand_index = 0; operand_index < operand_count; operand_index++) {
    interpreter::OperandType operand_type =
        interpreter::Bytecodes::GetOperandType(bytecode, operand_index);
    bool should_print =
        is_input
            ? interpreter::Bytecodes::IsRegisterInputOperandType(operand_type)
            : interpreter::Bytecodes::IsRegisterOutputOperandType(operand_type);
    if (should_print) {
      interpreter::Register first_reg =
          bytecode_iterator.GetRegisterOperand(operand_index);
      int range = bytecode_iterator.GetRegisterOperandRange(operand_index);
      PrintRegisterRange(frame, os, bytecode_iterator, kRegFieldWidth,
                         kArrowDirection, first_reg, range);
    }
  }
  if (!is_input && interpreter::Bytecodes::IsShortStar(bytecode)) {
    PrintRegisterRange(frame, os, bytecode_iterator, kRegFieldWidth,
                       kArrowDirection,
                       interpreter::Register::FromShortStar(bytecode), 1);
  }
  if (v8_flags.log_colour) {
    os << kNormalColourCode;
  }
}

}  // namespace

RUNTIME_FUNCTION(Runtime_TraceUnoptimizedBytecodeEntry) {
  if (!v8_flags.trace_ignition && !v8_flags.trace_baseline_exec) {
    return ReadOnlyRoots(isolate).undefined_value();
  }

  JavaScriptStackFrameIterator frame_iterator(isolate);
  UnoptimizedJSFrame* frame =
      reinterpret_cast<UnoptimizedJSFrame*>(frame_iterator.frame());

  if (frame->is_interpreted() && !v8_flags.trace_ignition) {
    return ReadOnlyRoots(isolate).undefined_value();
  }
  if (frame->is_baseline() && !v8_flags.trace_baseline_exec) {
    return ReadOnlyRoots(isolate).undefined_value();
  }

  SealHandleScope shs(isolate);
  DCHECK_EQ(3, args.length());
  Handle<BytecodeArray> bytecode_array = CheckedCast<BytecodeArray>(args.at(0));
  int bytecode_offset = args.smi_value_at(1);
  Handle<Object> accumulator = args.at(2);

  int offset = bytecode_offset - BytecodeArray::kHeaderSize + kHeapObjectTag;
  interpreter::BytecodeArrayIterator bytecode_iterator(bytecode_array);
  AdvanceToOffsetForTracing(bytecode_iterator, offset);
  if (offset == bytecode_iterator.current_offset()) {
    StdoutStream os;

    // Print bytecode.
    const uint8_t* base_address = reinterpret_cast<const uint8_t*>(
        bytecode_array->GetFirstBytecodeAddress());
    const uint8_t* bytecode_address = base_address + offset;

    if (frame->is_baseline()) {
      os << "B-> ";
    } else {
      os << " -> ";
    }
    os << static_cast<const void*>(bytecode_address) << " @ " << std::setw(4)
       << offset << " : ";
    interpreter::BytecodeDecoder::Decode(os, bytecode_address);
    os << std::endl;
    // Print all input registers and accumulator.
    PrintRegisters(frame, os, true, bytecode_iterator, accumulator);

    os << std::flush;
  }
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_TraceUnoptimizedBytecodeExit) {
  if (!v8_flags.trace_ignition && !v8_flags.trace_baseline_exec) {
    return ReadOnlyRoots(isolate).undefined_value();
  }

  JavaScriptStackFrameIterator frame_iterator(isolate);
  UnoptimizedJSFrame* frame =
      reinterpret_cast<UnoptimizedJSFrame*>(frame_iterator.frame());

  if (frame->is_interpreted() && !v8_flags.trace_ignition) {
    return ReadOnlyRoots(isolate).undefined_value();
  }
  if (frame->is_baseline() && !v8_flags.trace_baseline_exec) {
    return ReadOnlyRoots(isolate).undefined_value();
  }

  SealHandleScope shs(isolate);
  DCHECK_EQ(3, args.length());
  Handle<BytecodeArray> bytecode_array = CheckedCast<BytecodeArray>(args.at(0));
  int bytecode_offset = args.smi_value_at(1);
  Handle<Object> accumulator = args.at(2);

  int offset = bytecode_offset - BytecodeArray::kHeaderSize + kHeapObjectTag;
  interpreter::BytecodeArrayIterator bytecode_iterator(bytecode_array);
  AdvanceToOffsetForTracing(bytecode_iterator, offset);
  // The offset comparison here ensures registers only printed when the
  // (potentially) widened bytecode has completed. The iterator reports
  // the offset as the offset of the prefix bytecode.
  if (bytecode_iterator.current_operand_scale() ==
          interpreter::OperandScale::kSingle ||
      offset > bytecode_iterator.current_offset()) {
    StdoutStream os;

    // Print all output registers and accumulator.
    PrintRegisters(frame, os, false, bytecode_iterator, accumulator);
    os << std::flush;
  }
  return ReadOnlyRoots(isolate).undefined_value();
}

#endif  // V8_TRACE_UNOPTIMIZED

#ifdef V8_TRACE_FEEDBACK_UPDATES

RUNTIME_FUNCTION(Runtime_TraceUpdateFeedback) {
  if (!v8_flags.trace_feedback_updates) {
    return ReadOnlyRoots(isolate).undefined_value();
  }

  SealHandleScope shs(isolate);
  DCHECK_EQ(3, args.length());
  Handle<FeedbackVector> vector = args.at<FeedbackVector>(0);
  int slot = args.smi_value_at(1);
  auto reason = Cast<String>(args[2]);

  FeedbackVector::TraceFeedbackChange(isolate, *vector, FeedbackSlot(slot),
                                      reason->ToCString().get());

  return ReadOnlyRoots(isolate).undefined_value();
}

#endif  // V8_TRACE_FEEDBACK_UPDATES

namespace {

V8_WARN_UNUSED_RESULT Tagged<Object> CrashUnlessFuzzing(Isolate* isolate) {
  CHECK(v8_flags.fuzzing);
  return ReadOnlyRoots(isolate).undefined_value();
}

int StackSize(Isolate* isolate) {
  int n = 0;
  for (JavaScriptStackFrameIterator it(isolate); !it.done(); it.Advance()) n++;
  return n;
}

void PrintIndentation(int stack_size) {
  const int max_display = 80;
  if (stack_size <= max_display) {
    PrintF("%4d:%*s", stack_size, stack_size, "");
  } else {
    PrintF("%4d:%*s", stack_size, max_display, "...");
  }
}

}  // namespace

RUNTIME_FUNCTION(Runtime_TraceEnter) {
  SealHandleScope shs(isolate);
  PrintIndentation(StackSize(isolate));
  JavaScriptFrame::PrintTop(isolate, stdout, true, false);
  PrintF(" {\n");
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_TraceExit) {
  SealHandleScope shs(isolate);
  if (args.length() != 1) {
    return CrashUnlessFuzzing(isolate);
  }
  Tagged<Object> obj = args[0];
  PrintIndentation(StackSize(isolate));
  PrintF("} -> ");
  ShortPrint(obj);
  PrintF("\n");
  return obj;  // return TOS
}

#if V8_ENABLE_WEBASSEMBLY

namespace {

int WasmStackSize(Isolate* isolate) {
  // TODO(wasm): Fix this for mixed JS/Wasm stacks with both --trace and
  // --trace-wasm.
  int n = 0;
  for (DebuggableStackFrameIterator it(isolate); !it.done(); it.Advance()) {
    if (it.is_wasm()) n++;
  }
  return n;
}

template <typename T1, typename T2 = T1>
void PrintRep(Address address, const char* str) {
  PrintF("%4s:", str);
  const auto t1 = base::ReadLittleEndianValue<T1>(address);
  if constexpr (std::is_floating_point_v<T1>) {
    PrintF("%f", t1);
  } else if constexpr (sizeof(T1) > sizeof(uint32_t)) {
    PrintF("%" PRIu64, t1);
  } else {
    PrintF("%u", t1);
  }
  const auto t2 = base::ReadLittleEndianValue<T2>(address);
  if constexpr (sizeof(T1) > sizeof(uint32_t)) {
    PrintF(" / %016" PRIx64 "\n", t2);
  } else {
    PrintF(" / %0*x\n", static_cast<int>(2 * sizeof(T2)), t2);
  }
}

}  // namespace

RUNTIME_FUNCTION(Runtime_WasmTraceEnter) {
  HandleScope shs(isolate);
  // This isn't exposed to fuzzers so doesn't need to handle invalid arguments.
  DCHECK_EQ(0, args.length());
  PrintIndentation(WasmStackSize(isolate));

  // Find the caller wasm frame.
  wasm::WasmCodeRefScope wasm_code_ref_scope;
  DebuggableStackFrameIterator it(isolate);
  DCHECK(!it.done());
  DCHECK(it.is_wasm());
#if V8_ENABLE_DRUMBRAKE
  DCHECK(!it.is_wasm_interpreter_entry());
#endif  // V8_ENABLE_DRUMBRAKE
  WasmFrame* frame = WasmFrame::cast(it.frame());

  // Find the function name.
  int func_index = frame->function_index();
  const wasm::WasmModule* module = frame->trusted_instance_data()->module();
  wasm::ModuleWireBytes wire_bytes =
      wasm::ModuleWireBytes(frame->native_module()->wire_bytes());
  wasm::WireBytesRef name_ref =
      module->lazily_generated_names.LookupFunctionName(wire_bytes, func_index);
  wasm::WasmName name = wire_bytes.GetNameOrNull(name_ref);

  wasm::WasmCode* code = frame->wasm_code();
  PrintF(code->is_liftoff() ? "~" : "*");

  if (name.empty()) {
    PrintF("wasm-function[%d] {\n", func_index);
  } else {
    PrintF("wasm-function[%d] \"%.*s\" {\n", func_index, name.length(),
           name.begin());
  }

  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmTraceExit) {
  HandleScope shs(isolate);
  // This isn't exposed to fuzzers so doesn't need to handle invalid arguments.
  DCHECK_EQ(1, args.length());
  Tagged<Smi> return_addr_smi = Cast<Smi>(args[0]);

  PrintIndentation(WasmStackSize(isolate));
  PrintF("}");

  // Find the caller wasm frame.
  wasm::WasmCodeRefScope wasm_code_ref_scope;
  DebuggableStackFrameIterator it(isolate);
  DCHECK(!it.done());
  DCHECK(it.is_wasm());
#if V8_ENABLE_DRUMBRAKE
  DCHECK(!it.is_wasm_interpreter_entry());
#endif  // V8_ENABLE_DRUMBRAKE
  WasmFrame* frame = WasmFrame::cast(it.frame());
  int func_index = frame->function_index();
  const wasm::WasmModule* module = frame->trusted_instance_data()->module();
  const wasm::FunctionSig* sig = module->functions[func_index].sig;

  size_t num_returns = sig->return_count();
  // If we have no returns, we should have passed {Smi::zero()}.
  DCHECK_IMPLIES(num_returns == 0, IsZero(return_addr_smi));
  if (num_returns == 1) {
    wasm::ValueType return_type = sig->GetReturn(0);
    switch (return_type.kind()) {
      case wasm::kI32: {
        int32_t value =
            base::ReadUnalignedValue<int32_t>(return_addr_smi.ptr());
        PrintF(" -> %d\n", value);
        break;
      }
      case wasm::kI64: {
        int64_t value =
            base::ReadUnalignedValue<int64_t>(return_addr_smi.ptr());
        PrintF(" -> %" PRId64 "\n", value);
        break;
      }
      case wasm::kF32: {
        float value = base::ReadUnalignedValue<float>(return_addr_smi.ptr());
        PrintF(" -> %f\n", value);
        break;
      }
      case wasm::kF64: {
        double value = base::ReadUnalignedValue<double>(return_addr_smi.ptr());
        PrintF(" -> %f\n", value);
        break;
      }
      default:
        PrintF(" -> Unsupported type\n");
        break;
    }
  } else {
    // TODO(wasm) Handle multiple return values.
    PrintF("\n");
  }

  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmTraceGlobal) {
  CHECK(v8_flags.trace_wasm_globals);

  SealHandleScope scope(isolate);
  if (args.length() != 1 || !IsSmi(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  DisallowGarbageCollection no_gc;
  auto info_addr = Cast<Smi>(args[0]);

  wasm::GlobalTracingInfo* info =
      reinterpret_cast<wasm::GlobalTracingInfo*>(info_addr.ptr());

  wasm::WasmCodeRefScope wasm_code_ref_scope;
  DebuggableStackFrameIterator it(isolate);
  DCHECK(!it.done());
  DCHECK(it.is_wasm());
  WasmFrame* frame = WasmFrame::cast(it.frame());
  Tagged<WasmInstanceObject> instance = frame->wasm_instance();

  const wasm::WasmGlobal& global =
      instance->module()->globals[info->global_index];

  const char* tier = wasm::ExecutionTierToString(frame->wasm_code()->tier());

  wasm::WasmValue value =
      instance->trusted_data(isolate)->GetGlobalValue(isolate, global);

  PrintF("%-11s func:%6d:0x%-4x global.%s %d val: %s\n", tier,
         frame->function_index(), frame->position(),
         info->is_store ? "set" : "get", info->global_index,
         value.to_string().c_str());

  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmTraceMemory) {
  SealHandleScope scope(isolate);
  if (args.length() != 1 || !IsSmi(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  DisallowGarbageCollection no_gc;
  auto info_addr = Cast<Smi>(args[0]);

  wasm::MemoryTracingInfo* info =
      reinterpret_cast<wasm::MemoryTracingInfo*>(info_addr.ptr());

  // Find the caller wasm frame.
  wasm::WasmCodeRefScope wasm_code_ref_scope;
  DebuggableStackFrameIterator it(isolate);
  DCHECK(!it.done());
  DCHECK(it.is_wasm());
#if V8_ENABLE_DRUMBRAKE
  DCHECK(!it.is_wasm_interpreter_entry());
#endif  // V8_ENABLE_DRUMBRAKE
  WasmFrame* frame = WasmFrame::cast(it.frame());

  const char* tier = wasm::ExecutionTierToString(frame->wasm_code()->tier());

  PrintF("%-11s func:%6d:0x%-4x mem:%d %s %016" PRIuPTR " val: ", tier,
         frame->function_index(), frame->position(), info->mem_index,
         // Note: The extra leading space makes " store to" the same width as
         // "load from".
         info->is_store ? " store to" : "load from", info->offset);
  const Address address =
      reinterpret_cast<Address>(frame->trusted_instance_data()
                                    ->memory_object(info->mem_index)
                                    ->array_buffer()
                                    ->backing_store()) +
      info->offset;
  switch (static_cast<MachineRepresentation>(info->mem_rep)) {
    case MachineRepresentation::kWord8:
      PrintRep<uint8_t>(address, "i8");
      break;
    case MachineRepresentation::kWord16:
      PrintRep<uint16_t>(address, "i16");
      break;
    case MachineRepresentation::kWord32:
      PrintRep<uint32_t>(address, "i32");
      break;
    case MachineRepresentation::kWord64:
      PrintRep<uint64_t>(address, "i64");
      break;
    case MachineRepresentation::kFloat32:
      PrintRep<float, uint32_t>(address, "f32");
      break;
    case MachineRepresentation::kFloat64:
      PrintRep<double, uint64_t>(address, "f64");
      break;
    case MachineRepresentation::kSimd128: {
      const auto a = base::ReadLittleEndianValue<uint32_t>(address);
      const auto b = base::ReadLittleEndianValue<uint32_t>(address + 4);
      const auto c = base::ReadLittleEndianValue<uint32_t>(address + 8);
      const auto d = base::ReadLittleEndianValue<uint32_t>(address + 12);
      PrintF("s128:%u %u %u %u / %08x %08x %08x %08x\n", a, b, c, d, a, b, c,
             d);
      break;
    }
    default:
      PrintF("unknown\n");
      break;
  }

  return ReadOnlyRoots(isolate).undefined_value();
}

#endif  // V8_ENABLE_WEBASSEMBLY

}  // namespace internal
}  // namespace v8
