// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
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

using namespace i::interpreter;

namespace {

const char* kIndent = "       ";

class ArrayBufferAllocator final : public v8::ArrayBuffer::Allocator {
 public:
  void* Allocate(size_t length) override {
    void* data = AllocateUninitialized(length);
    if (data != nullptr) memset(data, 0, length);
    return data;
  }
  void* AllocateUninitialized(size_t length) override { return malloc(length); }
  void Free(void* data, size_t) override { free(data); }
};

class V8InitializationScope final {
 public:
  explicit V8InitializationScope(const char* exec_path);
  ~V8InitializationScope();

  v8::Platform* platform() const { return platform_.get(); }
  v8::Isolate* isolate() const { return isolate_; }

 private:
  v8::base::SmartPointer<v8::Platform> platform_;
  v8::Isolate* isolate_;

  DISALLOW_COPY_AND_ASSIGN(V8InitializationScope);
};

i::Isolate* GetInternalIsolate(v8::Isolate* isolate) {
  return reinterpret_cast<i::Isolate*>(isolate);
}

V8InitializationScope::V8InitializationScope(const char* exec_path)
    : platform_(v8::platform::CreateDefaultPlatform()) {
  i::FLAG_ignition = true;
  i::FLAG_always_opt = false;
  i::FLAG_allow_natives_syntax = true;

  v8::V8::InitializeICU();
  v8::V8::InitializeExternalStartupData(exec_path);
  v8::V8::InitializePlatform(platform_.get());
  v8::V8::Initialize();

  ArrayBufferAllocator allocator;
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = &allocator;

  isolate_ = v8::Isolate::New(create_params);
  GetInternalIsolate(isolate_)->interpreter()->Initialize();
}

V8InitializationScope::~V8InitializationScope() {
  isolate_->Dispose();
  v8::V8::Dispose();
  v8::V8::ShutdownPlatform();
}

v8::Local<v8::String> V8StringFromUTF8(v8::Isolate* isolate, const char* data) {
  return v8::String::NewFromUtf8(isolate, data, v8::NewStringType::kNormal)
      .ToLocalChecked();
}

std::string WrapCodeInFunction(const char* function_name,
                               const std::string& function_body) {
  std::ostringstream program_stream;
  program_stream << "function " << function_name << "() {" << function_body
                 << "}\n"
                 << function_name << "();";

  return program_stream.str();
}

v8::Local<v8::Value> CompileAndRun(v8::Isolate* isolate,
                                   const v8::Local<v8::Context>& context,
                                   const char* program) {
  v8::Local<v8::String> source = V8StringFromUTF8(isolate, program);
  v8::Local<v8::Script> script =
      v8::Script::Compile(context, source).ToLocalChecked();

  v8::Local<v8::Value> result;
  CHECK(script->Run(context).ToLocal(&result));

  return result;
}

i::Handle<v8::internal::BytecodeArray> GetBytecodeArrayForGlobal(
    v8::Isolate* isolate, const v8::Local<v8::Context>& context,
    const char* global_name) {
  v8::Local<v8::String> v8_global_name = V8StringFromUTF8(isolate, global_name);
  v8::Local<v8::Function> function = v8::Local<v8::Function>::Cast(
      context->Global()->Get(context, v8_global_name).ToLocalChecked());
  i::Handle<i::JSFunction> js_function =
      i::Handle<i::JSFunction>::cast(v8::Utils::OpenHandle(*function));

  i::Handle<i::BytecodeArray> bytecodes = i::handle(
      js_function->shared()->bytecode_array(), GetInternalIsolate(isolate));

  return bytecodes;
}

void PrintBytecodeOperand(std::ostream& stream,
                          const BytecodeArrayIterator& bytecode_iter,
                          const Bytecode& bytecode, int op_index) {
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
      stream << bytecode_iter.GetCountOperand(op_index);
    } else if (Bytecodes::IsIndexOperandType(op_type)) {
      stream << bytecode_iter.GetIndexOperand(op_index);
    } else {
      UNREACHABLE();
    }

    stream << ')';
  }
}

void PrintBytecode(std::ostream& stream,
                   const BytecodeArrayIterator& bytecode_iter) {
  Bytecode bytecode = bytecode_iter.current_bytecode();

  stream << "B(" << Bytecodes::ToString(bytecode) << ')';

  int operands_count = Bytecodes::NumberOfOperands(bytecode);
  for (int op_index = 0; op_index < operands_count; ++op_index) {
    stream << ", ";
    PrintBytecodeOperand(stream, bytecode_iter, bytecode, op_index);
  }
}

std::string QuoteCString(const std::string& source) {
  std::string quoted_buffer;
  for (char c : source) {
    switch (c) {
      case '"':
        quoted_buffer += "\\\"";
        break;
      case '\n':
        quoted_buffer += "\\n";
        break;
      case '\t':
        quoted_buffer += "\\t";
        break;
      case '\\':
        quoted_buffer += "\\\\";
        break;
      default:
        quoted_buffer += c;
        break;
    }
  }
  return quoted_buffer;
}

void PrintBytecodeArray(std::ostream& stream,
                        i::Handle<i::BytecodeArray> bytecode_array,
                        const std::string& body, bool print_banner = true) {
  const int kPointerSize = sizeof(void*);

  if (print_banner) {
    stream << kIndent << "// === ExpectedSnippet generated by "
                         "generate-bytecode-expectations. ===\n";
  }

  // Print the code snippet as a quoted C string.
  stream << kIndent << "{" << '"' << QuoteCString(body) << "\",\n" << kIndent;

  // Print the frame size, in multiples of kPointerSize.
  int frame_size = bytecode_array->frame_size();
  DCHECK(frame_size % kPointerSize == 0);
  if (frame_size > kPointerSize) {
    stream << ' ' << frame_size / kPointerSize << " * kPointerSize,\n"
           << kIndent;
  } else if (frame_size == kPointerSize) {
    stream << " kPointerSize,\n" << kIndent;
  } else if (frame_size == 0) {
    stream << " 0,\n" << kIndent;
  }

  // Print parameter count and size of the bytecode array.
  stream << ' ' << bytecode_array->parameter_count() << ",\n"
         << kIndent << ' ' << bytecode_array->length() << ",\n"
         << kIndent << " {\n"
         << kIndent << "     ";

  // Print bytecodes.
  BytecodeArrayIterator bytecode_iter{bytecode_array};
  for (; !bytecode_iter.done(); bytecode_iter.Advance()) {
    // Print separator before each instruction, except the first one.
    if (bytecode_iter.current_offset() > 0) {
      stream << ",\n" << kIndent << "     ";
    }
    PrintBytecode(stream, bytecode_iter);
  }

  stream << "\n" << kIndent << " },\n";
  // TODO(ssanfilippo) add representation of constant pool and handlers.
  stream << kIndent << " // constant pool and handlers here\n";
  stream << kIndent << "}\n";
}

bool ReadFromFileOrStdin(std::string* body, const char* body_filename) {
  std::stringstream body_buffer;
  if (strcmp(body_filename, "-") == 0) {
    body_buffer << std::cin.rdbuf();
  } else {
    std::ifstream body_file{body_filename};
    if (!body_file) return false;
    body_buffer << body_file.rdbuf();
  }
  *body = body_buffer.str();
  return true;
}

void PrintUsage(const char* exec_path) {
  std::cerr << "Usage: " << exec_path
            << " [filename.js|-]\n\n"
               "No arguments or - reads from standard input.\n"
               "Anything else is interpreted as a filename.\n\n"
               "This tool is intended as a help in writing tests.\n"
               "Please, DO NOT blindly copy and paste the output "
               "into the test suite.\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && strcmp(argv[1], "--help") == 0) {
    PrintUsage(argv[0]);
    return 0;
  }

  const char* body_filename = (argc > 1 ? argv[1] : "-");
  const char* wrapper_function_name = "__genbckexp_wrapper__";

  std::string body;
  if (!ReadFromFileOrStdin(&body, body_filename)) {
    std::cerr << "Could not open '" << body_filename << "'.\n\n";
    PrintUsage(argv[0]);
    return 1;
  }

  V8InitializationScope platform(argv[0]);
  {
    v8::Isolate::Scope isolate_scope(platform.isolate());
    v8::HandleScope handle_scope(platform.isolate());
    v8::Local<v8::Context> context = v8::Context::New(platform.isolate());
    v8::Context::Scope context_scope(context);

    std::string source_code = WrapCodeInFunction(wrapper_function_name, body);
    CompileAndRun(platform.isolate(), context, source_code.c_str());

    i::Handle<i::BytecodeArray> bytecode_array = GetBytecodeArrayForGlobal(
        platform.isolate(), context, wrapper_function_name);

    PrintBytecodeArray(std::cout, bytecode_array, body);
  }
}
