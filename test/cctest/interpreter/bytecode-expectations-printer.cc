// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/interpreter/bytecode-expectations-printer.h"

#include <iostream>

#include "include/libplatform/libplatform.h"
#include "include/v8.h"

#include "src/base/logging.h"
#include "src/base/smart-pointers.h"
#include "src/compiler.h"

#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-generator.h"
#include "src/interpreter/bytecodes.h"
#include "src/interpreter/interpreter.h"

namespace v8 {
namespace internal {
namespace interpreter {

v8::Local<v8::String> BytecodeExpectationsPrinter::V8StringFromUTF8(
    const char* data) const {
  return v8::String::NewFromUtf8(isolate_, data, v8::NewStringType::kNormal)
      .ToLocalChecked();
}

std::string BytecodeExpectationsPrinter::WrapCodeInFunction(
    const char* function_name, const std::string& function_body) const {
  std::ostringstream program_stream;
  program_stream << "function " << function_name << "() {" << function_body
                 << "}\n"
                 << function_name << "();";

  return program_stream.str();
}

v8::Local<v8::Value> BytecodeExpectationsPrinter::CompileAndRun(
    const char* program) const {
  v8::Local<v8::String> source = V8StringFromUTF8(program);
  v8::Local<v8::Script> script =
      v8::Script::Compile(isolate_->GetCurrentContext(), source)
          .ToLocalChecked();

  v8::Local<v8::Value> result;
  CHECK(script->Run(isolate_->GetCurrentContext()).ToLocal(&result));

  return result;
}

i::Handle<v8::internal::BytecodeArray>
BytecodeExpectationsPrinter::GetBytecodeArrayForGlobal(
    const char* global_name) const {
  const v8::Local<v8::Context>& context = isolate_->GetCurrentContext();
  v8::Local<v8::String> v8_global_name = V8StringFromUTF8(global_name);
  v8::Local<v8::Function> function = v8::Local<v8::Function>::Cast(
      context->Global()->Get(context, v8_global_name).ToLocalChecked());
  i::Handle<i::JSFunction> js_function =
      i::Handle<i::JSFunction>::cast(v8::Utils::OpenHandle(*function));

  i::Handle<i::BytecodeArray> bytecodes =
      i::handle(js_function->shared()->bytecode_array(), i_isolate());

  return bytecodes;
}

void BytecodeExpectationsPrinter::PrintEscapedString(
    std::ostream& stream, const std::string& string) const {
  for (char c : string) {
    switch (c) {
      case '"':
        stream << "\\\"";
        break;
      case '\\':
        stream << "\\\\";
        break;
      default:
        stream << c;
        break;
    }
  }
}

void BytecodeExpectationsPrinter::PrintBytecodeOperand(
    std::ostream& stream, const BytecodeArrayIterator& bytecode_iter,
    const Bytecode& bytecode, int op_index) const {
  OperandType op_type = Bytecodes::GetOperandType(bytecode, op_index);
  OperandSize op_size = Bytecodes::GetOperandSize(bytecode, op_index);

  const char* size_tag;
  switch (op_size) {
    case OperandSize::kByte:
      size_tag = "8";
      break;
    case OperandSize::kShort:
      size_tag = "16";
      break;
    default:
      UNREACHABLE();
      return;
  }

  if (Bytecodes::IsRegisterOperandType(op_type)) {
    Register register_value = bytecode_iter.GetRegisterOperand(op_index);
    stream << 'R';
    if (op_size != OperandSize::kByte) stream << size_tag;
    stream << '(' << register_value.index() << ')';
  } else {
    stream << 'U' << size_tag << '(';

    if (Bytecodes::IsImmediateOperandType(op_type)) {
      // We need a cast, otherwise the result is printed as char.
      stream << static_cast<int>(bytecode_iter.GetImmediateOperand(op_index));
    } else if (Bytecodes::IsRegisterCountOperandType(op_type)) {
      stream << bytecode_iter.GetRegisterCountOperand(op_index);
    } else if (Bytecodes::IsIndexOperandType(op_type)) {
      stream << bytecode_iter.GetIndexOperand(op_index);
    } else {
      UNREACHABLE();
    }

    stream << ')';
  }
}

void BytecodeExpectationsPrinter::PrintBytecode(
    std::ostream& stream, const BytecodeArrayIterator& bytecode_iter) const {
  Bytecode bytecode = bytecode_iter.current_bytecode();

  stream << "B(" << Bytecodes::ToString(bytecode) << ')';

  int operands_count = Bytecodes::NumberOfOperands(bytecode);
  for (int op_index = 0; op_index < operands_count; ++op_index) {
    stream << ", ";
    PrintBytecodeOperand(stream, bytecode_iter, bytecode, op_index);
  }
}

void BytecodeExpectationsPrinter::PrintV8String(std::ostream& stream,
                                                i::String* string) const {
  stream << '"';
  for (int i = 0, length = string->length(); i < length; ++i) {
    stream << i::AsEscapedUC16ForJSON(string->Get(i));
  }
  stream << '"';
}

void BytecodeExpectationsPrinter::PrintConstant(
    std::ostream& stream, i::Handle<i::Object> constant) const {
  switch (const_pool_type_) {
    case ConstantPoolType::kString:
      CHECK(constant->IsString());
      PrintV8String(stream, i::String::cast(*constant));
      break;
    case ConstantPoolType::kInteger:
      if (constant->IsSmi()) {
        i::Smi::cast(*constant)->SmiPrint(stream);
      } else if (constant->IsHeapNumber()) {
        i::HeapNumber::cast(*constant)->HeapNumberPrint(stream);
      } else {
        UNREACHABLE();
      }
      break;
    case ConstantPoolType::kDouble:
      i::HeapNumber::cast(*constant)->HeapNumberPrint(stream);
      break;
    case ConstantPoolType::kMixed:
      if (constant->IsSmi()) {
        stream << "kInstanceTypeDontCare";
      } else {
        stream << "InstanceType::"
               << i::HeapObject::cast(*constant)->map()->instance_type();
      }
      break;
    case ConstantPoolType::kUnknown:
    default:
      UNREACHABLE();
      return;
  }
}

void BytecodeExpectationsPrinter::PrintFrameSize(
    std::ostream& stream, i::Handle<i::BytecodeArray> bytecode_array) const {
  const int kPointerSize = sizeof(void*);
  int frame_size = bytecode_array->frame_size();

  DCHECK_EQ(frame_size % kPointerSize, 0);
  stream << "frame size: " << frame_size / kPointerSize;
  if (frame_size > 0) stream << "  # in multiples of sizeof(void*)";
  stream << "\nparameter count: " << bytecode_array->parameter_count() << '\n';
}

void BytecodeExpectationsPrinter::PrintBytecodeSequence(
    std::ostream& stream, i::Handle<i::BytecodeArray> bytecode_array) const {
  stream << "bytecodes: [\n";
  BytecodeArrayIterator bytecode_iter(bytecode_array);
  for (; !bytecode_iter.done(); bytecode_iter.Advance()) {
    stream << "  ";
    PrintBytecode(stream, bytecode_iter);
    stream << ",\n";
  }
  stream << "]\n";
}

void BytecodeExpectationsPrinter::PrintConstantPool(
    std::ostream& stream, i::FixedArray* constant_pool) const {
  stream << "constant pool: [\n";
  int num_constants = constant_pool->length();
  if (num_constants > 0) {
    for (int i = 0; i < num_constants; ++i) {
      stream << "  ";
      PrintConstant(stream, i::FixedArray::get(constant_pool, i, i_isolate()));
      stream << ",\n";
    }
  }
  stream << "]\n";
}

void BytecodeExpectationsPrinter::PrintCodeSnippet(
    std::ostream& stream, const std::string& body) const {
  stream << "snippet: \"\n";
  std::stringstream body_stream(body);
  std::string body_line;
  while (std::getline(body_stream, body_line)) {
    stream << "  ";
    PrintEscapedString(stream, body_line);
    stream << '\n';
  }
  stream << "\"\n";
}

void BytecodeExpectationsPrinter::PrintBytecodeArray(
    std::ostream& stream, const std::string& body,
    i::Handle<i::BytecodeArray> bytecode_array) const {
  stream << "---\n";
  PrintCodeSnippet(stream, body);
  PrintFrameSize(stream, bytecode_array);
  PrintBytecodeSequence(stream, bytecode_array);
  PrintConstantPool(stream, bytecode_array->constant_pool());

  // TODO(ssanfilippo) print handlers.
  i::HandlerTable* handlers =
      i::HandlerTable::cast(bytecode_array->handler_table());
  CHECK_EQ(handlers->NumberOfRangeEntries(), 0);
}

void BytecodeExpectationsPrinter::PrintExpectation(
    std::ostream& stream, const std::string& snippet) const {
  const char* wrapper_function_name = "__genbckexp_wrapper__";

  std::string source_code = WrapCodeInFunction(wrapper_function_name, snippet);
  CompileAndRun(source_code.c_str());

  i::Handle<i::BytecodeArray> bytecode_array =
      GetBytecodeArrayForGlobal(wrapper_function_name);

  PrintBytecodeArray(stream, snippet, bytecode_array);
  stream << '\n';
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
