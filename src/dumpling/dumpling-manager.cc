// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/dumpling/dumpling-manager.h"

#include <fstream>

#include "src/dumpling/object-dumping.h"
#include "src/execution/isolate.h"
#include "src/objects/bytecode-array-inl.h"
#include "src/objects/bytecode-array.h"

namespace v8::internal {

namespace {

V8_INLINE void MaybePrint(std::string short_name,
                          std::optional<std::string> maybe_value,
                          std::ofstream& os) {
  if (maybe_value.has_value()) {
    os << short_name << maybe_value.value() << "\n";
  }
}

}  // namespace

void DumplingManager::DoPrint(UnoptimizedJSFrame* frame,
                              Tagged<JSFunction> function, int bytecode_offset,
                              DumpFrameType frame_dump_type,
                              Handle<BytecodeArray> bytecode_array,
                              Handle<Object> accumulator) {
  DCHECK(IsDumpingEnabled());

  switch (frame_dump_type) {
    case DumpFrameType::kInterpreterFrame:
      dumpling_os_ << "---I" << '\n';
      break;
    default:
      UNREACHABLE();
  }

  MaybePrint("b:", DumpBytecodeOffset(bytecode_offset), dumpling_os_);

  int function_id = function->shared()->StartPosition();
  MaybePrint("f:", DumpFunctionId(function_id), dumpling_os_);

  int param_count = bytecode_array->parameter_count() - 1;
  MaybePrint("n:", DumpArgCount(param_count), dumpling_os_);
  int register_count = bytecode_array->register_count();
  MaybePrint("m:", DumpRegCount(register_count), dumpling_os_);

  for (int i = 0; i < param_count; i++) {
    std::stringstream check_arg;
    Tagged<Object> arg_object = frame->GetParameter(i);
    DifferentialFuzzingPrint(arg_object, check_arg);
    std::string label = "a" + std::to_string(i) + ":";
    MaybePrint(label, DumpArg(i, check_arg.str()), dumpling_os_);
  }

  for (int i = 0; i < register_count; i++) {
    std::stringstream check_reg;
    Tagged<Object> reg_object = frame->ReadInterpreterRegister(i);
    DifferentialFuzzingPrint(reg_object, check_reg);
    std::string label = "r" + std::to_string(i) + ":";
    MaybePrint(label, DumpReg(i, check_reg.str()), dumpling_os_);
  }

  std::stringstream check_acc;
  DifferentialFuzzingPrint(*accumulator, check_acc);
  MaybePrint("x:", DumpAcc(check_acc.str()), dumpling_os_);

  dumpling_os_ << "\n";
}

std::string DumplingManager::GetDumpOutFilename() const {
  return std::string(v8_flags.dump_out_filename);
}

template <typename T>
std::optional<std::string> DumplingManager::DumpValue(T value, T& last_value) {
  if (value == last_value) {
    return {};
  }
  last_value = value;
  return std::to_string(value);
}

std::optional<std::string> DumplingManager::DumpAcc(std::string acc) {
  if (acc == dumpling_last_frame_.acc) {
    return {};
  }
  dumpling_last_frame_.acc = acc;
  return acc;
}

template <typename T>
std::optional<std::string> DumplingManager::DumpValuePlain(T value,
                                                           T& last_value) {
  if (value == last_value) {
    return {};
  }
  last_value = value;
  return value;
}

std::optional<std::string> DumplingManager::DumpBytecodeOffset(
    int bytecode_offset) {
  return DumpValue(bytecode_offset, dumpling_last_frame_.bytecode_offset);
}

std::optional<std::string> DumplingManager::DumpArgCount(int arg_count) {
  return DumpValue(arg_count, dumpling_last_frame_.arg_count);
}

std::optional<std::string> DumplingManager::DumpRegCount(int reg_count) {
  return DumpValue(reg_count, dumpling_last_frame_.reg_count);
}

std::optional<std::string> DumplingManager::DumpArg(unsigned int index,
                                                    std::string arg) {
  if (index >= dumpling_last_frame_.args.size()) {
    dumpling_last_frame_.args.resize(index + 1);
  }
  return DumpValuePlain(arg, dumpling_last_frame_.args[index]);
}

std::optional<std::string> DumplingManager::DumpReg(unsigned int index,
                                                    std::string reg) {
  if (index >= dumpling_last_frame_.regs.size()) {
    dumpling_last_frame_.regs.resize(index + 1);
  }
  return DumpValuePlain(reg, dumpling_last_frame_.regs[index]);
}

std::optional<std::string> DumplingManager::DumpFunctionId(int function_id) {
  return DumpValue(function_id, dumpling_last_frame_.function_id);
}

DumplingManager::DumplingManager()
    : dumpling_os_(GetDumpOutFilename(), std::ofstream::out) {}

DumplingManager::~DumplingManager() { dumpling_os_.close(); }

bool DumplingManager::AnyDumplingFlagsSet() const {
  return v8_flags.interpreter_dumping;
}

}  // namespace v8::internal
