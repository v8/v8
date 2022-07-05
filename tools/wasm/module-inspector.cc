// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include "include/libplatform/libplatform.h"
#include "include/v8-initialization.h"
#include "src/wasm/module-decoder-impl.h"
#include "src/wasm/names-provider.h"
#include "src/wasm/string-builder.h"
#include "src/wasm/wasm-opcodes-inl.h"

int PrintHelp(char** argv) {
  std::cerr << "Usage: Specify an action and a module name in any order.\n"
            << "The action can be any of:\n"

            << " --help\n"
            << "     Print this help and exit.\n"

            << " --list-functions\n"
            << "     List functions in the given module\n"

            << " --section-stats\n"
            << "     Show information about sections in the given module\n"

            << "The module name must be a file name.\n";
  return 1;
}

namespace v8 {
namespace internal {
namespace wasm {

class FormatConverter {
 public:
  explicit FormatConverter(std::string path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
      std::cerr << "Failed to open " << path << "!\n";
      return;
    }
    raw_bytes_ = std::vector<byte>(std::istreambuf_iterator<char>(input), {});
    if (raw_bytes_.size() < 8 || raw_bytes_[0] != 0 || raw_bytes_[1] != 'a' ||
        raw_bytes_[2] != 's' || raw_bytes_[3] != 'm') {
      std::cerr << "That's not a Wasm module!\n";
      return;
    }
    base::Vector<const byte> wire_bytes(raw_bytes_.data(), raw_bytes_.size());
    ModuleResult result =
        DecodeWasmModuleForDisassembler(start(), end(), &allocator_);
    if (result.failed()) {
      WasmError error = result.error();
      std::cerr << "Decoding error: " << error.message() << " at offset "
                << error.offset() << "\n";
      // TODO(jkummerow): Show some disassembly.
      return;
    }
    ok_ = true;
    module_ = result.value();
    names_provider_ =
        std::make_unique<NamesProvider>(module_.get(), wire_bytes);
  }

  bool ok() const { return ok_; }

  void ListFunctions() {
    DCHECK(ok_);
    const WasmModule* m = module();
    uint32_t num_functions = static_cast<uint32_t>(m->functions.size());
    std::cout << "There are " << num_functions << " functions ("
              << m->num_imported_functions << " imported, "
              << m->num_declared_functions
              << " locally defined); the following have names:\n";
    for (uint32_t i = 0; i < num_functions; i++) {
      StringBuilder sb;
      names()->PrintFunctionName(sb, i);
      if (sb.length() == 0) continue;
      std::string name(sb.start(), sb.length());
      std::cout << i << " " << name << "\n";
    }
  }

  void SectionStats() {
    DCHECK(ok_);
    Decoder decoder(start(), end());
    static constexpr int kModuleHeaderSize = 8;
    decoder.consume_bytes(kModuleHeaderSize, "module header");

    uint32_t module_size = static_cast<uint32_t>(end() - start());
    int digits = 2;
    for (uint32_t comparator = 100; module_size >= comparator;
         comparator *= 10) {
      digits++;
    }
    size_t kMinNameLength = 8;
    // 18 = kMinNameLength + strlen(" section: ").
    std::cout << std::setw(18) << std::left << "Module size: ";
    std::cout << std::setw(digits) << std::right << module_size << " bytes\n";
    for (WasmSectionIterator it(&decoder); it.more(); it.advance(true)) {
      const char* name = SectionName(it.section_code());
      size_t name_len = strlen(name);
      std::cout << SectionName(it.section_code()) << " section: ";
      for (; name_len < kMinNameLength; name_len++) std::cout << " ";

      uint32_t length = it.section_length();
      std::cout << std::setw(name_len > kMinNameLength ? 0 : digits) << length
                << " bytes / ";

      std::cout << std::fixed << std::setprecision(1) << std::setw(4)
                << 100.0 * length / module_size;
      std::cout << "% of total\n";
    }
  }

 private:
  byte* start() { return raw_bytes_.data(); }
  byte* end() { return start() + raw_bytes_.size(); }
  const WasmModule* module() { return module_.get(); }
  NamesProvider* names() { return names_provider_.get(); }

  AccountingAllocator allocator_;
  bool ok_{false};
  std::vector<byte> raw_bytes_;
  std::shared_ptr<WasmModule> module_;
  std::unique_ptr<NamesProvider> names_provider_;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

using FormatConverter = v8::internal::wasm::FormatConverter;

enum class Action {
  kUnset,
  kHelp,
  kListFunctions,
  kSectionStats,
};

struct Options {
  const char* filename = nullptr;
  Action action = Action::kUnset;
};

void ListFunctions(const Options& options) {
  FormatConverter fc(options.filename);
  if (fc.ok()) fc.ListFunctions();
}

void SectionStats(const Options& options) {
  FormatConverter fc(options.filename);
  if (fc.ok()) fc.SectionStats();
}

int ParseOptions(int argc, char** argv, Options* options) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 ||
        strcmp(argv[i], "help") == 0) {
      options->action = Action::kHelp;
    } else if (strcmp(argv[i], "--list-functions") == 0) {
      options->action = Action::kListFunctions;
    } else if (strcmp(argv[i], "--section-stats") == 0) {
      options->action = Action::kSectionStats;
    } else if (options->filename != nullptr) {
      return PrintHelp(argv);
    } else {
      options->filename = argv[i];
    }
  }
  if (options->action == Action::kUnset || options->filename == nullptr) {
    return PrintHelp(argv);
  }
  return 0;
}

int main(int argc, char** argv) {
  Options options;
  if (ParseOptions(argc, argv, &options) != 0) return 1;
  // Bootstrap the basics.
  v8::V8::InitializeICUDefaultLocation(argv[0]);
  v8::V8::InitializeExternalStartupData(argv[0]);
  std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
  v8::V8::InitializePlatform(platform.get());
#ifdef V8_ENABLE_SANDBOX
  if (!v8::V8::InitializeSandbox()) {
    fprintf(stderr, "Error initializing the V8 sandbox\n");
    return 1;
  }
#endif
  v8::V8::Initialize();

  switch (options.action) {
    // clang-format off
    case Action::kHelp:          PrintHelp(argv);             break;
    case Action::kListFunctions: ListFunctions(options);      break;
    case Action::kSectionStats:  SectionStats(options);       break;
    case Action::kUnset:         UNREACHABLE();
      // clang-format on
  }

  v8::V8::Dispose();
  v8::V8::DisposePlatform();
  return 0;
}
