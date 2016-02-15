// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>
#include <fstream>

#include "test/cctest/interpreter/bytecode-expectations-printer.h"

#include "include/libplatform/libplatform.h"
#include "include/v8.h"

#include "src/base/logging.h"
#include "src/base/smart-pointers.h"
#include "src/compiler.h"
#include "src/interpreter/interpreter.h"

using v8::internal::interpreter::BytecodeExpectationsPrinter;

namespace {

class ProgramOptions {
 public:
  static ProgramOptions FromCommandLine(int argc, char** argv);

  ProgramOptions()
      : parsing_failed_(false),
        print_help_(false),
        read_raw_js_snippet_(false),
        read_from_stdin_(false),
        const_pool_type_(
            BytecodeExpectationsPrinter::ConstantPoolType::kMixed) {}

  bool Validate() const;

  bool parsing_failed() const { return parsing_failed_; }
  bool print_help() const { return print_help_; }
  bool read_raw_js_snippet() const { return read_raw_js_snippet_; }
  bool read_from_stdin() const { return read_from_stdin_; }
  std::string filename() const { return filename_; }
  BytecodeExpectationsPrinter::ConstantPoolType const_pool_type() const {
    return const_pool_type_;
  }

 private:
  bool parsing_failed_;
  bool print_help_;
  bool read_raw_js_snippet_;
  bool read_from_stdin_;
  BytecodeExpectationsPrinter::ConstantPoolType const_pool_type_;
  std::string filename_;
};

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

BytecodeExpectationsPrinter::ConstantPoolType ParseConstantPoolType(
    const char* type_string) {
  if (strcmp(type_string, "int") == 0) {
    return BytecodeExpectationsPrinter::ConstantPoolType::kInteger;
  } else if (strcmp(type_string, "double") == 0) {
    return BytecodeExpectationsPrinter::ConstantPoolType::kDouble;
  } else if (strcmp(type_string, "string") == 0) {
    return BytecodeExpectationsPrinter::ConstantPoolType::kString;
  } else if (strcmp(type_string, "mixed") == 0) {
    return BytecodeExpectationsPrinter::ConstantPoolType::kMixed;
  }
  return BytecodeExpectationsPrinter::ConstantPoolType::kUnknown;
}

// static
ProgramOptions ProgramOptions::FromCommandLine(int argc, char** argv) {
  ProgramOptions options;

  if (argc <= 1) return options;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--help") == 0) {
      options.print_help_ = true;
    } else if (strcmp(argv[i], "--raw-js") == 0) {
      options.read_raw_js_snippet_ = true;
    } else if (strncmp(argv[i], "--pool-type=", 12) == 0) {
      options.const_pool_type_ = ParseConstantPoolType(argv[i] + 12);
    } else if (strcmp(argv[i], "--stdin") == 0) {
      options.read_from_stdin_ = true;
    } else if (strncmp(argv[i], "--", 2) != 0) {  // It doesn't start with --
      if (!options.filename_.empty()) {
        std::cerr << "ERROR: More than one input file specified\n";
        options.parsing_failed_ = true;
        break;
      }
      options.filename_ = argv[i];
    } else {
      std::cerr << "ERROR: Unknonwn option " << argv[i] << "\n";
      options.parsing_failed_ = true;
      break;
    }
  }

  return options;
}

bool ProgramOptions::Validate() const {
  if (parsing_failed_) return false;
  if (print_help_) return true;

  if (const_pool_type_ ==
      BytecodeExpectationsPrinter::ConstantPoolType::kUnknown) {
    std::cerr << "ERROR: Unknown constant pool type.\n";
    return false;
  }

  if (!read_from_stdin_ && filename_.empty()) {
    std::cerr << "ERROR: No input file specified.\n";
    return false;
  }

  if (read_from_stdin_ && !filename_.empty()) {
    std::cerr << "ERROR: Reading from stdin, but input files supplied.\n";
    return false;
  }

  return true;
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
}

V8InitializationScope::~V8InitializationScope() {
  isolate_->Dispose();
  v8::V8::Dispose();
  v8::V8::ShutdownPlatform();
}

std::string ReadRawJSSnippet(std::istream& stream) {  // NOLINT
  std::stringstream body_buffer;
  CHECK(body_buffer << stream.rdbuf());
  return body_buffer.str();
}

bool ReadNextSnippet(std::istream& stream, std::string* string_out) {  // NOLINT
  std::string line;
  bool found_begin_snippet = false;
  string_out->clear();
  while (std::getline(stream, line)) {
    if (line == "snippet: \"") {
      found_begin_snippet = true;
      continue;
    }
    if (!found_begin_snippet) continue;
    if (line == "\"") return true;
    CHECK_GE(line.size(), 2u);  // We should have the indent
    string_out->append(line.begin() + 2, line.end());
    *string_out += '\n';
  }
  return false;
}

void ExtractSnippetsFromStream(std::vector<std::string>* snippet_list,
                               std::istream& body_stream,  // NOLINT
                               bool read_raw_js_snippet) {
  if (read_raw_js_snippet) {
    snippet_list->push_back(ReadRawJSSnippet(body_stream));
  } else {
    std::string snippet;
    while (ReadNextSnippet(body_stream, &snippet)) {
      snippet_list->push_back(snippet);
    }
  }
}

bool ExtractSnippets(std::vector<std::string>* snippet_list,
                     const ProgramOptions& options) {
  if (options.read_from_stdin()) {
    ExtractSnippetsFromStream(snippet_list, std::cin,
                              options.read_raw_js_snippet());
  } else {
    std::ifstream body_file(options.filename().c_str());
    if (!body_file.is_open()) {
      std::cerr << "ERROR: Could not open '" << options.filename() << "'.\n";
      return false;
    }
    ExtractSnippetsFromStream(snippet_list, body_file,
                              options.read_raw_js_snippet());
  }
  return true;
}

void GenerateExpectationsFile(
    std::ostream& stream,  // NOLINT
    const std::vector<std::string>& snippet_list,
    BytecodeExpectationsPrinter::ConstantPoolType const_pool_type,
    const char* exec_path) {
  V8InitializationScope platform(exec_path);
  {
    v8::Isolate::Scope isolate_scope(platform.isolate());
    v8::HandleScope handle_scope(platform.isolate());
    v8::Local<v8::Context> context = v8::Context::New(platform.isolate());
    v8::Context::Scope context_scope(context);

    stream << "#\n# Autogenerated by generate-bytecode-expectations\n#\n\n";

    BytecodeExpectationsPrinter printer(platform.isolate(), const_pool_type);
    for (const std::string& snippet : snippet_list) {
      printer.PrintExpectation(stream, snippet);
    }
  }
}

void PrintUsage(const char* exec_path) {
  std::cerr
      << "\nUsage: " << exec_path
      << " [OPTIONS]... [INPUT FILE]\n\n"
         "Options:\n"
         "  --help    Print this help message.\n"
         "  --raw-js  Read raw JavaScript, instead of the output format.\n"
         "  --stdin   Read from standard input instead of file.\n"
         "  --pool-type=(int|double|string|mixed)\n"
         "      specify the type of the entries in the constant pool "
         "(default: mixed).\n"
         "\n"
         "Each raw JavaScript file is interpreted as a single snippet.\n\n"
         "This tool is intended as a help in writing tests.\n"
         "Please, DO NOT blindly copy and paste the output "
         "into the test suite.\n";
}

}  // namespace

int main(int argc, char** argv) {
  ProgramOptions options = ProgramOptions::FromCommandLine(argc, argv);

  if (!options.Validate() || options.print_help()) {
    PrintUsage(argv[0]);
    return options.print_help() ? 0 : 1;
  }

  std::vector<std::string> snippet_list;
  if (!ExtractSnippets(&snippet_list, options)) {
    return 2;
  }

  GenerateExpectationsFile(std::cout, snippet_list, options.const_pool_type(),
                           argv[0]);
}
