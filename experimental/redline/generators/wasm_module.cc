// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "experimental/redline/generators/wasm_module.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "experimental/redline/core/random.h"
#include "experimental/redline/core/tunable.h"

namespace redline {

REDLINE_TUNABLE(kWasmMaxImports, "generators.WasmModule.max_imports", 5, 0, 20,
                "max imported functions");
REDLINE_TUNABLE(kWasmMaxFunctions, "generators.WasmModule.max_functions", 5, 1,
                50, "max defined functions");
REDLINE_TUNABLE(kWasmMaxGlobals, "generators.WasmModule.max_globals", 10, 0, 50,
                "max globals");
REDLINE_TUNABLE(kWasmMaxMemoryPages, "generators.WasmModule.max_memory_pages",
                2, 1, 10, "max memory pages");
REDLINE_TUNABLE(kWasmMemoryProb, "generators.WasmModule.memory_probability",
                0.5, 0, 1, "probability of memory");
REDLINE_TUNABLE(kWasmTableProb, "generators.WasmModule.table_probability", 0.5,
                0, 1, "probability of table");
REDLINE_TUNABLE(kWasmMaxParams, "generators.WasmModule.max_params", 3, 0, 10,
                "max function parameters");
REDLINE_TUNABLE(kWasmMaxLocals, "generators.WasmModule.max_locals", 5, 0, 20,
                "max function locals");
REDLINE_TUNABLE(kWasmMaxExprDepth, "generators.WasmModule.max_expr_depth", 5, 1,
                20, "max expression depth");
REDLINE_TUNABLE(kWasmMaxBodySize, "generators.WasmModule.max_body_size", 20, 1,
                200, "max body instructions");
REDLINE_TUNABLE(kWasmMaxLoopCount, "generators.WasmModule.max_loop_count", 16,
                1, 64, "max iterations of a counted Wasm loop");
REDLINE_TUNABLE(kWasmInterestingConstProb,
                "generators.WasmModule.interesting_const_probability", 0.3, 0,
                1, "probability of an edge-case numeric constant");
REDLINE_TUNABLE(kWasmTrapMemoryProb,
                "generators.WasmModule.trap_memory_probability", 0.05, 0, 1,
                "probability of unmasked memory access");
REDLINE_TUNABLE(kWasmTrapDivProb, "generators.WasmModule.trap_div_probability",
                0.05, 0, 1, "probability of non-guarded div/rem");
REDLINE_TUNABLE(kWasmTrapConvertProb,
                "generators.WasmModule.trap_convert_probability", 0.05, 0, 1,
                "probability of non-saturating convert");

namespace {

constexpr int kMaxLoopNesting = 2;
constexpr uint32_t kSafeMemoryMask = 0x7FFF;
constexpr uint32_t kMaxMemoryExtraPages = 4;

void EncodeU32(std::vector<uint8_t>& buf, uint32_t value) {
  do {
    uint8_t byte = static_cast<uint8_t>(value & 0x7F);
    value >>= 7;
    if (value != 0) {
      byte |= 0x80;
    }
    buf.push_back(byte);
  } while (value != 0);
}

void EncodeI32(std::vector<uint8_t>& buf, int32_t value) {
  bool more = true;
  while (more) {
    uint8_t byte = static_cast<uint8_t>(value & 0x7F);
    value >>= 7;
    bool sign_bit = (byte & 0x40) != 0;
    if ((value == 0 && !sign_bit) || (value == -1 && sign_bit)) {
      more = false;
    } else {
      byte |= 0x80;
    }
    buf.push_back(byte);
  }
}

void EncodeI64(std::vector<uint8_t>& buf, int64_t value) {
  bool more = true;
  while (more) {
    uint8_t byte = static_cast<uint8_t>(value & 0x7F);
    value >>= 7;
    bool sign_bit = (byte & 0x40) != 0;
    if ((value == 0 && !sign_bit) || (value == -1 && sign_bit)) {
      more = false;
    } else {
      byte |= 0x80;
    }
    buf.push_back(byte);
  }
}

void EncodeF32(std::vector<uint8_t>& buf, float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  for (size_t i = 0; i < 4; ++i) {
    buf.push_back(static_cast<uint8_t>((bits >> (8 * i)) & 0xFF));
  }
}

void EncodeF64(std::vector<uint8_t>& buf, double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  for (size_t i = 0; i < 8; ++i) {
    buf.push_back(static_cast<uint8_t>((bits >> (8 * i)) & 0xFF));
  }
}

void EncodeString(std::vector<uint8_t>& buf, const std::string& str) {
  EncodeU32(buf, static_cast<uint32_t>(str.size()));
  for (char c : str) {
    buf.push_back(static_cast<uint8_t>(c));
  }
}

void EmitSection(std::vector<uint8_t>& out, uint8_t section_id,
                 const std::vector<uint8_t>& body) {
  out.push_back(section_id);
  EncodeU32(out, static_cast<uint32_t>(body.size()));
  out.insert(out.end(), body.begin(), body.end());
}

struct BlockLabel {
  bool is_loop = false;
  bool has_result = false;
  WasmType result = WasmType::kI32;
};

struct FuncState {
  std::vector<WasmType> user_locals;
  std::vector<BlockLabel> block_stack;
  size_t my_func = 0;
  size_t my_def_idx = 0;
  size_t loop_counter_base = 0;
  int loop_depth = 0;
  int budget = 1;
  bool allow_calls = true;
};

class WasmBuilder {
 public:
  explicit WasmBuilder(Random& r) : r_(r) {}

  WasmModule Build() {
    WasmModule m;

    const size_t max_imports = static_cast<size_t>(kWasmMaxImports.AsInt());
    const size_t max_funcs = static_cast<size_t>(kWasmMaxFunctions.AsInt());
    const size_t max_globals = static_cast<size_t>(kWasmMaxGlobals.AsInt());

    const size_t num_imports = r_.Index(max_imports + 1);
    size_t num_funcs = 1 + r_.Index(max_funcs);
    m.memory = r_.Chance(kWasmMemoryProb());
    m.table = r_.Chance(kWasmTableProb());
    const size_t num_globals = r_.Index(max_globals + 1);

    // Reserve at least 3 functions when a table exists so 2 or more callees
    // share one signature while a higher-index function executes
    // `call_indirect` without recursion.
    if (m.table && max_funcs >= 3 && num_funcs < 3) {
      num_funcs = 3;
    }

    for (size_t i = 0; i < num_imports; ++i) {
      WasmSignature sig = RandomSignature();
      m.imports.push_back(sig);
      import_type_idx_.push_back(AddType(sig));
      all_func_sigs_.push_back(sig);
    }

    // Put multiple callees with one shared signature at the start of the
    // defined functions when `m.table` is true.
    if (m.table && num_funcs >= 3) {
      const size_t max_callees = std::min<size_t>(num_funcs - 1, 3);
      table_callees_ = 2 + r_.Index(max_callees - 1);
      callee_type_idx_ = AddType(RandomSignature());
    } else if (m.table && num_funcs == 2) {
      table_callees_ = 1;
      callee_type_idx_ = AddType(RandomSignature());
    }

    for (size_t i = 0; i < num_funcs; ++i) {
      WasmSignature sig;
      uint32_t type_idx = 0;
      if (i < table_callees_) {
        type_idx = callee_type_idx_;
        sig = types_[type_idx];
      } else {
        sig = RandomSignature();
        if (m.table && table_callees_ >= 2 && i + 1 == num_funcs &&
            kWasmMaxParams.AsInt() >= 1) {
          if (sig.params.empty()) {
            sig.params.push_back(WasmType::kI32);
          } else {
            sig.params[0] = WasmType::kI32;
          }
          m.indirect_caller = static_cast<int>(i);
        }
        type_idx = AddType(sig);
      }
      m.functions.push_back(sig);
      func_type_idx_.push_back(type_idx);
      all_func_sigs_.push_back(sig);
    }

    std::vector<uint32_t> table_entries;
    if (m.table) {
      const size_t count = table_callees_ > 0 ? table_callees_ : num_funcs;
      for (size_t i = 0; i < count; ++i) {
        table_entries.push_back(static_cast<uint32_t>(num_imports + i));
      }
      m.table_size = static_cast<uint32_t>(table_entries.size());
      m.table_callees = static_cast<uint32_t>(table_callees_);
    }

    for (size_t i = 0; i < num_globals; ++i) {
      WasmType type = RandomType();
      bool is_mut = r_.Coin();
      if (num_globals >= 2 && i == 0) {
        is_mut = true;
      } else if (num_globals >= 2 && i == 1) {
        is_mut = false;
      }
      globals_.push_back({type, is_mut});
      if (is_mut) {
        m.globals.push_back(type);
      }
    }

    std::vector<uint8_t>& b = m.bytes;
    b.insert(b.end(), {0x00, 0x61, 0x73, 0x6D});  // Magic header.
    b.insert(b.end(), {0x01, 0x00, 0x00, 0x00});  // Version 1.

    // Section 1: Type section.
    {
      std::vector<uint8_t> sec;
      EncodeU32(sec, static_cast<uint32_t>(types_.size()));
      for (const WasmSignature& sig : types_) {
        sec.push_back(0x60);
        EncodeU32(sec, static_cast<uint32_t>(sig.params.size()));
        for (WasmType t : sig.params) {
          sec.push_back(static_cast<uint8_t>(t));
        }
        EncodeU32(sec, static_cast<uint32_t>(sig.results.size()));
        for (WasmType t : sig.results) {
          sec.push_back(static_cast<uint8_t>(t));
        }
      }
      EmitSection(b, 1, sec);
    }

    // Section 2: Import section.
    if (num_imports > 0) {
      std::vector<uint8_t> sec;
      EncodeU32(sec, static_cast<uint32_t>(num_imports));
      for (size_t i = 0; i < num_imports; ++i) {
        EncodeString(sec, "m");
        EncodeString(sec, "f" + std::to_string(i));
        sec.push_back(0x00);
        EncodeU32(sec, import_type_idx_[i]);
      }
      EmitSection(b, 2, sec);
    }

    // Section 3: Function section.
    if (num_funcs > 0) {
      std::vector<uint8_t> sec;
      EncodeU32(sec, static_cast<uint32_t>(num_funcs));
      for (size_t i = 0; i < num_funcs; ++i) {
        EncodeU32(sec, func_type_idx_[i]);
      }
      EmitSection(b, 3, sec);
    }

    // Section 4: Table section (`funcref`).
    if (m.table) {
      std::vector<uint8_t> sec;
      EncodeU32(sec, 1);
      sec.push_back(0x70);  // `funcref`.
      sec.push_back(0x00);  // Limits without maximum.
      EncodeU32(sec, m.table_size);
      EmitSection(b, 4, sec);
    }

    // Section 5: Memory section with bounded maximum pages.
    if (m.memory) {
      std::vector<uint8_t> sec;
      EncodeU32(sec, 1);
      sec.push_back(0x01);  // Limits with minimum and maximum.
      mem_pages_ =
          1 + r_.Index(static_cast<size_t>(kWasmMaxMemoryPages.AsInt()));
      EncodeU32(sec, static_cast<uint32_t>(mem_pages_));
      EncodeU32(sec, static_cast<uint32_t>(mem_pages_ + kMaxMemoryExtraPages));
      EmitSection(b, 5, sec);
    }

    // Section 6: Global section (mutable and immutable globals).
    if (num_globals > 0) {
      std::vector<uint8_t> sec;
      EncodeU32(sec, static_cast<uint32_t>(num_globals));
      for (const Global& g : globals_) {
        sec.push_back(static_cast<uint8_t>(g.type));
        sec.push_back(g.mut ? 0x01 : 0x00);
        EmitConst(sec, g.type);
        sec.push_back(0x0B);
      }
      EmitSection(b, 6, sec);
    }

    // Section 7: Export section.
    {
      std::vector<uint8_t> sec;
      const uint32_t num_exports =
          static_cast<uint32_t>(num_funcs + (m.memory ? 1 : 0) +
                                (m.table ? 1 : 0) + m.globals.size());
      EncodeU32(sec, num_exports);
      for (size_t i = 0; i < num_funcs; ++i) {
        EncodeString(sec, "e" + std::to_string(i));
        sec.push_back(0x00);
        EncodeU32(sec, static_cast<uint32_t>(num_imports + i));
      }
      if (m.memory) {
        EncodeString(sec, "mem");
        sec.push_back(0x02);
        EncodeU32(sec, 0);
      }
      if (m.table) {
        EncodeString(sec, "tab");
        sec.push_back(0x01);
        EncodeU32(sec, 0);
      }
      size_t g_idx = 0;
      size_t g_exp = 0;
      for (const Global& g : globals_) {
        if (g.mut) {
          EncodeString(sec, "g" + std::to_string(g_exp++));
          sec.push_back(0x03);
          EncodeU32(sec, static_cast<uint32_t>(g_idx));
        }
        g_idx++;
      }
      EmitSection(b, 7, sec);
    }

    // Section 9: Element section populating `tab` at offset 0.
    if (m.table && !table_entries.empty()) {
      std::vector<uint8_t> sec;
      EncodeU32(sec, 1);
      sec.push_back(0x00);  // Active segment for table 0.
      sec.push_back(0x41);  // `i32.const 0`.
      EncodeI32(sec, 0);
      sec.push_back(0x0B);
      EncodeU32(sec, static_cast<uint32_t>(table_entries.size()));
      for (uint32_t fn_idx : table_entries) {
        EncodeU32(sec, fn_idx);
      }
      EmitSection(b, 9, sec);
    }

    // Section 10: Code section.
    if (num_funcs > 0) {
      std::vector<uint8_t> sec;
      EncodeU32(sec, static_cast<uint32_t>(num_funcs));
      for (size_t i = 0; i < num_funcs; ++i) {
        std::vector<uint8_t> code = BuildFunctionBody(i, num_imports, m);
        EncodeU32(sec, static_cast<uint32_t>(code.size()));
        sec.insert(sec.end(), code.begin(), code.end());
      }
      EmitSection(b, 10, sec);
    }

    // Section 11: Data section initializing the first 32 bytes of memory.
    if (m.memory) {
      std::vector<uint8_t> sec;
      EncodeU32(sec, 1);
      sec.push_back(0x00);  // Active segment for memory 0.
      sec.push_back(0x41);  // `i32.const 0`.
      EncodeI32(sec, 0);
      sec.push_back(0x0B);
      EncodeU32(sec, 32);
      for (uint32_t i = 0; i < 32; ++i) {
        sec.push_back(static_cast<uint8_t>((i * 37 + 0x42) & 0xFF));
      }
      EmitSection(b, 11, sec);
    }

    return m;
  }

 private:
  struct Global {
    WasmType type;
    bool mut;
  };

  Random& r_;
  std::vector<WasmSignature> types_;
  std::vector<uint32_t> import_type_idx_;
  std::vector<uint32_t> func_type_idx_;
  std::vector<WasmSignature> all_func_sigs_;
  std::vector<Global> globals_;
  size_t mem_pages_ = 0;
  size_t table_callees_ = 0;
  uint32_t callee_type_idx_ = 0;

  uint32_t AddType(const WasmSignature& sig) {
    const uint32_t idx = static_cast<uint32_t>(types_.size());
    types_.push_back(sig);
    return idx;
  }

  WasmType RandomType() {
    static constexpr WasmType kTypes[] = {WasmType::kI32, WasmType::kI64,
                                          WasmType::kF32, WasmType::kF64};
    return kTypes[r_.Index(4)];
  }

  WasmSignature RandomSignature() {
    WasmSignature sig;
    const size_t params =
        r_.Index(static_cast<size_t>(kWasmMaxParams.AsInt()) + 1);
    sig.params.reserve(params);
    for (size_t p = 0; p < params; ++p) {
      sig.params.push_back(RandomType());
    }
    if (r_.Coin()) {
      sig.results.push_back(RandomType());
    }
    return sig;
  }

  void EmitConst(std::vector<uint8_t>& code, WasmType type) {
    const bool edge = r_.Chance(kWasmInterestingConstProb());
    if (type == WasmType::kI32) {
      static constexpr int32_t kEdges[] = {
          0,  1,  -1,  2,     7,          16,
          31, 32, 255, 65535, 0x7FFFFFFF, std::numeric_limits<int32_t>::min()};
      const int32_t val =
          edge ? kEdges[r_.Index(sizeof(kEdges) / sizeof(kEdges[0]))]
               : static_cast<int32_t>(r_.Range(-1000, 1000));
      code.push_back(0x41);
      EncodeI32(code, val);
    } else if (type == WasmType::kI64) {
      static constexpr int64_t kEdges[] = {0,
                                           1,
                                           -1,
                                           32,
                                           63,
                                           64,
                                           0x7FFFFFFFLL,
                                           -2147483648LL,
                                           0xFFFFFFFFLL,
                                           0x100000000LL,
                                           std::numeric_limits<int64_t>::max(),
                                           std::numeric_limits<int64_t>::min()};
      const int64_t val =
          edge ? kEdges[r_.Index(sizeof(kEdges) / sizeof(kEdges[0]))]
               : r_.Range(-1000, 1000);
      code.push_back(0x42);
      EncodeI64(code, val);
    } else if (type == WasmType::kF32) {
      static constexpr float kEdges[] = {
          0.0f,
          -0.0f,
          1.0f,
          -1.0f,
          0.5f,
          1e20f,
          -1e20f,
          std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity(),
          std::numeric_limits<float>::quiet_NaN()};
      const float val =
          edge ? kEdges[r_.Index(sizeof(kEdges) / sizeof(kEdges[0]))]
               : static_cast<float>(r_.Double() * 100.0);
      code.push_back(0x43);
      EncodeF32(code, val);
    } else {
      static constexpr double kEdges[] = {
          0.0,
          -0.0,
          1.0,
          -1.0,
          0.5,
          9007199254740992.0,
          -9007199254740992.0,
          std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::quiet_NaN()};
      const double val =
          edge ? kEdges[r_.Index(sizeof(kEdges) / sizeof(kEdges[0]))]
               : r_.Double() * 100.0;
      code.push_back(0x44);
      EncodeF64(code, val);
    }
  }

  std::vector<uint8_t> BuildFunctionBody(size_t def_idx, size_t num_imports,
                                         const WasmModule& m) {
    std::vector<uint8_t> code;
    const WasmSignature& sig = m.functions[def_idx];
    const size_t extra_locals =
        r_.Index(static_cast<size_t>(kWasmMaxLocals.AsInt()) + 1);

    std::vector<WasmType> local_decls;
    local_decls.reserve(extra_locals);
    for (size_t l = 0; l < extra_locals; ++l) {
      local_decls.push_back(RandomType());
    }

    // Reserve dedicated `i32` countdown locals after user locals so nested
    // loops never overwrite each other's counters.
    EncodeU32(code, static_cast<uint32_t>(local_decls.size() + 1));
    for (WasmType t : local_decls) {
      EncodeU32(code, 1);
      code.push_back(static_cast<uint8_t>(t));
    }
    EncodeU32(code, static_cast<uint32_t>(kMaxLoopNesting));
    code.push_back(static_cast<uint8_t>(WasmType::kI32));

    FuncState fs;
    fs.user_locals = sig.params;
    fs.user_locals.insert(fs.user_locals.end(), local_decls.begin(),
                          local_decls.end());
    fs.my_func = num_imports + def_idx;
    fs.my_def_idx = def_idx;
    fs.loop_counter_base = fs.user_locals.size();

    const bool has_result = !sig.results.empty();
    const WasmType res = has_result ? sig.results[0] : WasmType::kI32;
    fs.block_stack.push_back({/*is_loop=*/false, has_result, res});

    const int max_body = kWasmMaxBodySize.AsInt();
    const int min_body = std::max(1, max_body / 2);
    fs.budget = min_body + static_cast<int>(r_.Index(
                               static_cast<size_t>(max_body - min_body + 1)));

    const int max_depth = kWasmMaxExprDepth.AsInt();

    // Emit one `call_indirect` using parameter 0 in the designated indirect
    // caller so speculative inlining and Wasm deopts trigger from JS.
    if (static_cast<int>(def_idx) == m.indirect_caller && table_callees_ >= 2 &&
        max_body >= 2) {
      fs.budget = std::max(fs.budget, 2);
      --fs.budget;
      EmitCallIndirectStmt(code, fs, max_depth, m, /*use_param0=*/true);
    }

    while (fs.budget > 1) {
      BuildStmt(code, fs, max_depth, m);
    }

    if (has_result) {
      BuildExpr(code, res, fs, max_depth, m);
    }
    code.push_back(0x0B);  // End of function body.
    return code;
  }

  void EmitSafeAddress(std::vector<uint8_t>& code, FuncState& fs, int depth,
                       const WasmModule& m) {
    BuildExpr(code, WasmType::kI32, fs, depth - 1, m);
    if (!r_.Chance(kWasmTrapMemoryProb())) {
      code.push_back(0x41);
      EncodeI32(code, static_cast<int32_t>(kSafeMemoryMask));
      code.push_back(0x71);  // `i32.and`.
    }
  }

  uint32_t PickMemOffset() {
    static constexpr uint32_t kOffsets[] = {0, 4, 8, 16};
    return kOffsets[r_.Index(4)];
  }

  void EmitStoreStmt(std::vector<uint8_t>& code, FuncState& fs, int depth,
                     const WasmModule& m) {
    WasmType type = RandomType();
    EmitSafeAddress(code, fs, depth, m);
    BuildExpr(code, type, fs, depth - 1, m);
    uint8_t opcode = 0x36;
    uint32_t max_align = 2;
    if (type == WasmType::kI32) {
      const size_t k = r_.Index(3);
      if (k == 0) {
        opcode = 0x36;  // `i32.store`.
        max_align = 2;
      } else if (k == 1) {
        opcode = 0x3A;  // `i32.store8`.
        max_align = 0;
      } else {
        opcode = 0x3B;  // `i32.store16`.
        max_align = 1;
      }
    } else if (type == WasmType::kI64) {
      const size_t k = r_.Index(4);
      if (k == 0) {
        opcode = 0x37;  // `i64.store`.
        max_align = 3;
      } else if (k == 1) {
        opcode = 0x3C;  // `i64.store8`.
        max_align = 0;
      } else if (k == 2) {
        opcode = 0x3D;  // `i64.store16`.
        max_align = 1;
      } else {
        opcode = 0x3E;  // `i64.store32`.
        max_align = 2;
      }
    } else if (type == WasmType::kF32) {
      opcode = 0x38;  // `f32.store`.
      max_align = 2;
    } else {
      opcode = 0x39;  // `f64.store`.
      max_align = 3;
    }
    code.push_back(opcode);
    EncodeU32(code, static_cast<uint32_t>(r_.Index(max_align + 1)));
    EncodeU32(code, PickMemOffset());
  }

  void EmitLoadExpr(std::vector<uint8_t>& code, WasmType res, FuncState& fs,
                    int depth, const WasmModule& m) {
    if (res == WasmType::kI32 && r_.Chance(0.15)) {
      code.push_back(0x3F);  // `memory.size`.
      code.push_back(0x00);
      return;
    }
    EmitSafeAddress(code, fs, depth, m);
    uint8_t opcode = 0x28;
    uint32_t max_align = 2;
    if (res == WasmType::kI32) {
      static constexpr uint8_t kOps[] = {0x28, 0x2C, 0x2D, 0x2E, 0x2F};
      static constexpr uint32_t kAligns[] = {2, 0, 0, 1, 1};
      const size_t k = r_.Index(5);
      opcode = kOps[k];
      max_align = kAligns[k];
    } else if (res == WasmType::kI64) {
      static constexpr uint8_t kOps[] = {0x29, 0x30, 0x31, 0x32,
                                         0x33, 0x34, 0x35};
      static constexpr uint32_t kAligns[] = {3, 0, 0, 1, 1, 2, 2};
      const size_t k = r_.Index(7);
      opcode = kOps[k];
      max_align = kAligns[k];
    } else if (res == WasmType::kF32) {
      opcode = 0x2A;
      max_align = 2;
    } else {
      opcode = 0x2B;
      max_align = 3;
    }
    code.push_back(opcode);
    EncodeU32(code, static_cast<uint32_t>(r_.Index(max_align + 1)));
    EncodeU32(code, PickMemOffset());
  }

  void ConsumeResult(std::vector<uint8_t>& code, WasmType type,
                     const FuncState& fs) {
    // Store the call result in a matching local or mutable global when one
    // exists, or drop it to keep the stack empty.
    if (r_.Coin()) {
      std::vector<size_t> cands;
      for (size_t i = 0; i < fs.user_locals.size(); ++i) {
        if (fs.user_locals[i] == type) {
          cands.push_back(i);
        }
      }
      if (!cands.empty()) {
        code.push_back(0x21);  // `local.set`.
        EncodeU32(code, static_cast<uint32_t>(r_.Pick(cands)));
        return;
      }
    }
    if (r_.Coin()) {
      std::vector<size_t> cands;
      for (size_t i = 0; i < globals_.size(); ++i) {
        if (globals_[i].mut && globals_[i].type == type) {
          cands.push_back(i);
        }
      }
      if (!cands.empty()) {
        code.push_back(0x24);  // `global.set`.
        EncodeU32(code, static_cast<uint32_t>(r_.Pick(cands)));
        return;
      }
    }
    code.push_back(0x1A);  // `drop`.
  }

  void EmitCallIndirect(std::vector<uint8_t>& code, FuncState& fs, int depth,
                        const WasmModule& m, bool use_param0) {
    const WasmSignature& sig = types_[callee_type_idx_];
    for (WasmType p : sig.params) {
      BuildExpr(code, p, fs, depth - 1, m);
    }
    if (use_param0 && !m.functions[fs.my_def_idx].params.empty() &&
        m.functions[fs.my_def_idx].params[0] == WasmType::kI32) {
      code.push_back(0x20);  // `local.get 0`.
      EncodeU32(code, 0);
    } else {
      BuildExpr(code, WasmType::kI32, fs, depth - 1, m);
    }
    code.push_back(0x41);  // `i32.const table_callees_`.
    EncodeI32(code, static_cast<int32_t>(table_callees_));
    code.push_back(0x70);  // `i32.rem_u` keeps the slot inside bounds.
    code.push_back(0x11);  // `call_indirect`.
    EncodeU32(code, callee_type_idx_);
    EncodeU32(code, 0);
  }

  void EmitCallIndirectStmt(std::vector<uint8_t>& code, FuncState& fs,
                            int depth, const WasmModule& m, bool use_param0) {
    EmitCallIndirect(code, fs, depth, m, use_param0);
    const WasmSignature& sig = types_[callee_type_idx_];
    if (!sig.results.empty()) {
      ConsumeResult(code, sig.results[0], fs);
    }
  }

  void BuildStmt(std::vector<uint8_t>& code, FuncState& fs, int depth,
                 const WasmModule& m) {
    if (fs.budget <= 0) {
      return;
    }
    --fs.budget;

    const size_t choice = r_.Index(9);
    switch (choice) {
      case 0: {  // `local.set`.
        if (!fs.user_locals.empty()) {
          const size_t idx = r_.Index(fs.user_locals.size());
          BuildExpr(code, fs.user_locals[idx], fs, depth - 1, m);
          code.push_back(0x21);
          EncodeU32(code, static_cast<uint32_t>(idx));
          return;
        }
        break;
      }
      case 1: {  // `global.set` on a mutable global.
        std::vector<size_t> cands;
        for (size_t i = 0; i < globals_.size(); ++i) {
          if (globals_[i].mut) {
            cands.push_back(i);
          }
        }
        if (!cands.empty()) {
          const size_t idx = r_.Pick(cands);
          BuildExpr(code, globals_[idx].type, fs, depth - 1, m);
          code.push_back(0x24);
          EncodeU32(code, static_cast<uint32_t>(idx));
          return;
        }
        break;
      }
      case 2: {  // Masked memory store.
        if (m.memory) {
          EmitStoreStmt(code, fs, depth, m);
          return;
        }
        break;
      }
      case 3: {  // Bounded `loop` with a dedicated countdown local.
        if (fs.loop_depth < kMaxLoopNesting && depth > 1 && fs.budget >= 2) {
          const uint32_t ctr = static_cast<uint32_t>(
              fs.loop_counter_base + static_cast<size_t>(fs.loop_depth));
          const bool prev_allow_calls = fs.allow_calls;
          const bool allow_loop_calls =
              prev_allow_calls && fs.loop_depth == 0 && r_.Chance(0.25);
          const int max_iters =
              allow_loop_calls
                  ? std::min(4, kWasmMaxLoopCount.AsInt())
                  : (fs.loop_depth > 0 ? std::min(4, kWasmMaxLoopCount.AsInt())
                                       : kWasmMaxLoopCount.AsInt());
          const int32_t count =
              1 +
              static_cast<int32_t>(r_.Index(static_cast<size_t>(max_iters)));
          code.push_back(0x41);  // `i32.const count`.
          EncodeI32(code, count);
          code.push_back(0x21);  // `local.set ctr`.
          EncodeU32(code, ctr);
          code.push_back(0x03);  // `loop`.
          code.push_back(0x40);  // Void block type.
          fs.block_stack.push_back(
              {/*is_loop=*/true, /*has_result=*/false, WasmType::kI32});
          ++fs.loop_depth;
          fs.allow_calls = allow_loop_calls;

          const int body_stmts = 1 + static_cast<int>(r_.Index(3));
          for (int s = 0; s < body_stmts && fs.budget > 1; ++s) {
            BuildStmt(code, fs, depth - 1, m);
          }

          fs.allow_calls = prev_allow_calls;
          --fs.loop_depth;
          fs.block_stack.pop_back();

          code.push_back(0x20);  // `local.get ctr`.
          EncodeU32(code, ctr);
          code.push_back(0x41);  // `i32.const 1`.
          EncodeI32(code, 1);
          code.push_back(0x6B);  // `i32.sub`.
          code.push_back(0x22);  // `local.tee ctr`.
          EncodeU32(code, ctr);
          code.push_back(0x0D);  // `br_if 0`.
          EncodeU32(code, 0);
          code.push_back(0x0B);  // `end`.
          return;
        }
        break;
      }
      case 4: {  // Void `if`/`else`.
        if (depth > 1 && fs.budget >= 2) {
          BuildExpr(code, WasmType::kI32, fs, depth - 1, m);
          code.push_back(0x04);  // `if`.
          code.push_back(0x40);  // Void block type.
          fs.block_stack.push_back(
              {/*is_loop=*/false, /*has_result=*/false, WasmType::kI32});
          const int then_n = 1 + static_cast<int>(r_.Index(2));
          for (int s = 0; s < then_n && fs.budget > 1; ++s) {
            BuildStmt(code, fs, depth - 1, m);
          }
          code.push_back(0x05);  // `else`.
          const int else_n = 1 + static_cast<int>(r_.Index(2));
          for (int s = 0; s < else_n && fs.budget > 1; ++s) {
            BuildStmt(code, fs, depth - 1, m);
          }
          code.push_back(0x0B);  // `end`.
          fs.block_stack.pop_back();
          return;
        }
        break;
      }
      case 5: {  // Void `block`.
        if (depth > 1 && fs.budget >= 2) {
          code.push_back(0x02);  // `block`.
          code.push_back(0x40);  // Void block type.
          fs.block_stack.push_back(
              {/*is_loop=*/false, /*has_result=*/false, WasmType::kI32});
          const int stmts = 1 + static_cast<int>(r_.Index(3));
          for (int s = 0; s < stmts && fs.budget > 1; ++s) {
            BuildStmt(code, fs, depth - 1, m);
          }
          code.push_back(0x0B);  // `end`.
          fs.block_stack.pop_back();
          return;
        }
        break;
      }
      case 6: {  // Forward `br_if` to an enclosing non-loop label.
        std::vector<size_t> cands;
        for (size_t i = 0; i < fs.block_stack.size(); ++i) {
          if (!fs.block_stack[i].is_loop) {
            cands.push_back(i);
          }
        }
        if (!cands.empty()) {
          const size_t target_idx = r_.Pick(cands);
          const uint32_t rel_depth =
              static_cast<uint32_t>(fs.block_stack.size() - 1 - target_idx);
          const BlockLabel target = fs.block_stack[target_idx];
          if (target.has_result) {
            BuildExpr(code, target.result, fs, depth - 1, m);
          }
          BuildExpr(code, WasmType::kI32, fs, depth - 1, m);
          code.push_back(0x0D);  // `br_if`.
          EncodeU32(code, rel_depth);
          if (target.has_result) {
            code.push_back(0x1A);  // `drop` if branch is not taken.
          }
          return;
        }
        break;
      }
      case 7: {  // Direct `call` to a lower function index.
        if (fs.allow_calls && fs.my_func > 0) {
          const size_t callee = r_.Index(fs.my_func);
          const WasmSignature& sig = all_func_sigs_[callee];
          for (WasmType p : sig.params) {
            BuildExpr(code, p, fs, depth - 1, m);
          }
          code.push_back(0x10);  // `call`.
          EncodeU32(code, static_cast<uint32_t>(callee));
          if (!sig.results.empty()) {
            ConsumeResult(code, sig.results[0], fs);
          }
          return;
        }
        break;
      }
      case 8: {  // `call_indirect` into the function table.
        if (fs.allow_calls && m.table && table_callees_ > 0 &&
            fs.my_def_idx >= table_callees_) {
          EmitCallIndirectStmt(code, fs, depth, m, /*use_param0=*/r_.Coin());
          return;
        }
        break;
      }
    }

    // Fallback: mutate a local or emit `nop` to keep the stack neutral.
    if (!fs.user_locals.empty()) {
      const size_t idx = r_.Index(fs.user_locals.size());
      BuildExpr(code, fs.user_locals[idx], fs, depth - 1, m);
      code.push_back(0x21);
      EncodeU32(code, static_cast<uint32_t>(idx));
    } else {
      code.push_back(0x01);  // `nop`.
    }
  }

  void EmitUnaryOp(std::vector<uint8_t>& code, WasmType res, FuncState& fs,
                   int depth, const WasmModule& m) {
    if (res == WasmType::kI32 && r_.Chance(0.35)) {
      if (r_.Coin()) {
        BuildExpr(code, WasmType::kI32, fs, depth - 1, m);
        code.push_back(0x45);  // `i32.eqz`.
      } else {
        BuildExpr(code, WasmType::kI64, fs, depth - 1, m);
        code.push_back(0x50);  // `i64.eqz`.
      }
      return;
    }
    BuildExpr(code, res, fs, depth - 1, m);
    if (res == WasmType::kI32) {
      static constexpr uint8_t kOps[] = {0x67, 0x68, 0x69, 0xC0, 0xC1};
      code.push_back(kOps[r_.Index(5)]);
    } else if (res == WasmType::kI64) {
      static constexpr uint8_t kOps[] = {0x79, 0x7A, 0x7B, 0xC2, 0xC3, 0xC4};
      code.push_back(kOps[r_.Index(6)]);
    } else if (res == WasmType::kF32) {
      static constexpr uint8_t kOps[] = {0x8B, 0x8C, 0x8D, 0x8E,
                                         0x8F, 0x90, 0x91};
      code.push_back(kOps[r_.Index(7)]);
    } else {
      static constexpr uint8_t kOps[] = {0x99, 0x9A, 0x9B, 0x9C,
                                         0x9D, 0x9E, 0x9F};
      code.push_back(kOps[r_.Index(7)]);
    }
  }

  void EmitBinaryOp(std::vector<uint8_t>& code, WasmType res, FuncState& fs,
                    int depth, const WasmModule& m) {
    if (res == WasmType::kI32 && r_.Chance(0.35)) {
      WasmType arg_type = RandomType();
      BuildExpr(code, arg_type, fs, depth - 1, m);
      BuildExpr(code, arg_type, fs, depth - 1, m);
      if (arg_type == WasmType::kI32) {
        code.push_back(static_cast<uint8_t>(0x46 + r_.Index(10)));
      } else if (arg_type == WasmType::kI64) {
        code.push_back(static_cast<uint8_t>(0x51 + r_.Index(10)));
      } else if (arg_type == WasmType::kF32) {
        code.push_back(static_cast<uint8_t>(0x5B + r_.Index(6)));
      } else {
        code.push_back(static_cast<uint8_t>(0x61 + r_.Index(6)));
      }
      return;
    }

    BuildExpr(code, res, fs, depth - 1, m);
    BuildExpr(code, res, fs, depth - 1, m);

    if (res == WasmType::kI32) {
      if (r_.Chance(0.2)) {
        if (r_.Chance(kWasmTrapDivProb())) {
          code.push_back(r_.Coin() ? 0x6D : 0x6F);  // `i32.div_s` / `rem_s`.
        } else {
          code.push_back(0x41);
          EncodeI32(code, 1);
          code.push_back(0x72);                     // `i32.or 1`.
          code.push_back(r_.Coin() ? 0x6E : 0x70);  // `i32.div_u` / `rem_u`.
        }
      } else {
        static constexpr uint8_t kOps[] = {0x6A, 0x6B, 0x6C, 0x71, 0x72, 0x73,
                                           0x74, 0x75, 0x76, 0x77, 0x78};
        code.push_back(kOps[r_.Index(11)]);
      }
    } else if (res == WasmType::kI64) {
      if (r_.Chance(0.2)) {
        if (r_.Chance(kWasmTrapDivProb())) {
          code.push_back(r_.Coin() ? 0x7F : 0x81);  // `i64.div_s` / `rem_s`.
        } else {
          code.push_back(0x42);
          EncodeI64(code, 1);
          code.push_back(0x84);                     // `i64.or 1`.
          code.push_back(r_.Coin() ? 0x80 : 0x82);  // `i64.div_u` / `rem_u`.
        }
      } else {
        static constexpr uint8_t kOps[] = {0x7C, 0x7D, 0x7E, 0x83, 0x84, 0x85,
                                           0x86, 0x87, 0x88, 0x89, 0x8A};
        code.push_back(kOps[r_.Index(11)]);
      }
    } else if (res == WasmType::kF32) {
      static constexpr uint8_t kOps[] = {0x92, 0x93, 0x94, 0x95,
                                         0x96, 0x97, 0x98};
      code.push_back(kOps[r_.Index(7)]);
    } else {
      static constexpr uint8_t kOps[] = {0xA0, 0xA1, 0xA2, 0xA3,
                                         0xA4, 0xA5, 0xA6};
      code.push_back(kOps[r_.Index(7)]);
    }
  }

  void EmitConversion(std::vector<uint8_t>& code, WasmType res, FuncState& fs,
                      int depth, const WasmModule& m) {
    WasmType src = RandomType();
    if (src == res) {
      static constexpr WasmType kOthers[4][3] = {
          {WasmType::kI64, WasmType::kF32, WasmType::kF64},
          {WasmType::kI32, WasmType::kF32, WasmType::kF64},
          {WasmType::kI32, WasmType::kI64, WasmType::kF64},
          {WasmType::kI32, WasmType::kI64, WasmType::kF32},
      };
      const size_t idx = res == WasmType::kI32   ? 0
                         : res == WasmType::kI64 ? 1
                         : res == WasmType::kF32 ? 2
                                                 : 3;
      src = kOthers[idx][r_.Index(3)];
    }
    BuildExpr(code, src, fs, depth - 1, m);

    if (res == WasmType::kI32 && src == WasmType::kI64) {
      code.push_back(0xA7);  // `i32.wrap_i64`.
    } else if (res == WasmType::kI32 && src == WasmType::kF32) {
      if (r_.Chance(0.35)) {
        code.push_back(0xBC);  // `i32.reinterpret_f32`.
      } else if (r_.Chance(kWasmTrapConvertProb())) {
        code.push_back(r_.Coin() ? 0xA8 : 0xA9);  // `i32.trunc_f32_s/u`.
      } else {
        code.push_back(0xFC);
        EncodeU32(code, r_.Coin() ? 0 : 1);  // `i32.trunc_sat_f32_s/u`.
      }
    } else if (res == WasmType::kI32 && src == WasmType::kF64) {
      if (r_.Chance(kWasmTrapConvertProb())) {
        code.push_back(r_.Coin() ? 0xAA : 0xAB);  // `i32.trunc_f64_s/u`.
      } else {
        code.push_back(0xFC);
        EncodeU32(code, r_.Coin() ? 2 : 3);  // `i32.trunc_sat_f64_s/u`.
      }
    } else if (res == WasmType::kI64 && src == WasmType::kI32) {
      code.push_back(r_.Coin() ? 0xAC : 0xAD);  // `i64.extend_i32_s/u`.
    } else if (res == WasmType::kI64 && src == WasmType::kF32) {
      if (r_.Chance(kWasmTrapConvertProb())) {
        code.push_back(r_.Coin() ? 0xAE : 0xAF);  // `i64.trunc_f32_s/u`.
      } else {
        code.push_back(0xFC);
        EncodeU32(code, r_.Coin() ? 4 : 5);  // `i64.trunc_sat_f32_s/u`.
      }
    } else if (res == WasmType::kI64 && src == WasmType::kF64) {
      if (r_.Chance(0.35)) {
        code.push_back(0xBD);  // `i64.reinterpret_f64`.
      } else if (r_.Chance(kWasmTrapConvertProb())) {
        code.push_back(r_.Coin() ? 0xB0 : 0xB1);  // `i64.trunc_f64_s/u`.
      } else {
        code.push_back(0xFC);
        EncodeU32(code, r_.Coin() ? 6 : 7);  // `i64.trunc_sat_f64_s/u`.
      }
    } else if (res == WasmType::kF32 && src == WasmType::kI32) {
      static constexpr uint8_t kOps[] = {0xB2, 0xB3, 0xBE};
      code.push_back(kOps[r_.Index(3)]);
    } else if (res == WasmType::kF32 && src == WasmType::kI64) {
      code.push_back(r_.Coin() ? 0xB4 : 0xB5);
    } else if (res == WasmType::kF32 && src == WasmType::kF64) {
      code.push_back(0xB6);  // `f32.demote_f64`.
    } else if (res == WasmType::kF64 && src == WasmType::kI32) {
      code.push_back(r_.Coin() ? 0xB7 : 0xB8);
    } else if (res == WasmType::kF64 && src == WasmType::kI64) {
      static constexpr uint8_t kOps[] = {0xB9, 0xBA, 0xBF};
      code.push_back(kOps[r_.Index(3)]);
    } else {
      code.push_back(0xBB);  // `f64.promote_f32`.
    }
  }

  void BuildExpr(std::vector<uint8_t>& code, WasmType res, FuncState& fs,
                 int depth, const WasmModule& m) {
    if (depth <= 0 || fs.budget <= 0) {
      EmitConst(code, res);
      return;
    }
    --fs.budget;

    const size_t choice = r_.Index(12);
    switch (choice) {
      case 0: {  // Constant.
        EmitConst(code, res);
        return;
      }
      case 1: {  // `local.get`.
        std::vector<size_t> cands;
        for (size_t i = 0; i < fs.user_locals.size(); ++i) {
          if (fs.user_locals[i] == res) {
            cands.push_back(i);
          }
        }
        if (!cands.empty()) {
          code.push_back(0x20);
          EncodeU32(code, static_cast<uint32_t>(r_.Pick(cands)));
          return;
        }
        break;
      }
      case 2: {  // `local.tee`.
        std::vector<size_t> cands;
        for (size_t i = 0; i < fs.user_locals.size(); ++i) {
          if (fs.user_locals[i] == res) {
            cands.push_back(i);
          }
        }
        if (!cands.empty()) {
          BuildExpr(code, res, fs, depth - 1, m);
          code.push_back(0x22);
          EncodeU32(code, static_cast<uint32_t>(r_.Pick(cands)));
          return;
        }
        break;
      }
      case 3: {  // `global.get`.
        std::vector<size_t> cands;
        for (size_t i = 0; i < globals_.size(); ++i) {
          if (globals_[i].type == res) {
            cands.push_back(i);
          }
        }
        if (!cands.empty()) {
          code.push_back(0x23);
          EncodeU32(code, static_cast<uint32_t>(r_.Pick(cands)));
          return;
        }
        break;
      }
      case 4: {  // Unary numeric operator.
        EmitUnaryOp(code, res, fs, depth, m);
        return;
      }
      case 5: {  // Binary numeric operator or comparison.
        EmitBinaryOp(code, res, fs, depth, m);
        return;
      }
      case 6: {  // Numeric conversion, reinterpret, or saturating truncation.
        EmitConversion(code, res, fs, depth, m);
        return;
      }
      case 7: {  // Masked memory load or `memory.size`.
        if (m.memory) {
          EmitLoadExpr(code, res, fs, depth, m);
          return;
        }
        break;
      }
      case 8: {  // `select`.
        BuildExpr(code, res, fs, depth - 1, m);
        BuildExpr(code, res, fs, depth - 1, m);
        BuildExpr(code, WasmType::kI32, fs, depth - 1, m);
        code.push_back(0x1B);
        return;
      }
      case 9: {  // Valued `if`/`else` or valued `block`.
        if (r_.Coin()) {
          BuildExpr(code, WasmType::kI32, fs, depth - 1, m);
          fs.block_stack.push_back(
              {/*is_loop=*/false, /*has_result=*/true, res});
          code.push_back(0x04);
          code.push_back(static_cast<uint8_t>(res));
          BuildExpr(code, res, fs, depth - 1, m);
          code.push_back(0x05);
          BuildExpr(code, res, fs, depth - 1, m);
          code.push_back(0x0B);
          fs.block_stack.pop_back();
        } else {
          fs.block_stack.push_back(
              {/*is_loop=*/false, /*has_result=*/true, res});
          code.push_back(0x02);
          code.push_back(static_cast<uint8_t>(res));
          BuildExpr(code, res, fs, depth - 1, m);
          code.push_back(0x0B);
          fs.block_stack.pop_back();
        }
        return;
      }
      case 10: {  // Forward `br_if` to a valued non-loop label of type `res`.
        std::vector<size_t> cands;
        for (size_t i = 0; i < fs.block_stack.size(); ++i) {
          if (!fs.block_stack[i].is_loop && fs.block_stack[i].has_result &&
              fs.block_stack[i].result == res) {
            cands.push_back(i);
          }
        }
        if (!cands.empty()) {
          const size_t target_idx = r_.Pick(cands);
          const uint32_t rel_depth =
              static_cast<uint32_t>(fs.block_stack.size() - 1 - target_idx);
          BuildExpr(code, res, fs, depth - 1, m);
          BuildExpr(code, WasmType::kI32, fs, depth - 1, m);
          code.push_back(0x0D);
          EncodeU32(code, rel_depth);
          return;
        }
        break;
      }
      case 11: {  // Direct `call` or `call_indirect` returning `res`.
        if (!fs.allow_calls) {
          break;
        }
        if (m.table && table_callees_ > 0 && fs.my_def_idx >= table_callees_ &&
            !types_[callee_type_idx_].results.empty() &&
            types_[callee_type_idx_].results[0] == res && r_.Coin()) {
          EmitCallIndirect(code, fs, depth, m, /*use_param0=*/r_.Coin());
          return;
        }
        std::vector<size_t> cands;
        for (size_t i = 0; i < fs.my_func; ++i) {
          if (!all_func_sigs_[i].results.empty() &&
              all_func_sigs_[i].results[0] == res) {
            cands.push_back(i);
          }
        }
        if (!cands.empty()) {
          const size_t callee = r_.Pick(cands);
          for (WasmType p : all_func_sigs_[callee].params) {
            BuildExpr(code, p, fs, depth - 1, m);
          }
          code.push_back(0x10);
          EncodeU32(code, static_cast<uint32_t>(callee));
          return;
        }
        break;
      }
    }
    EmitConst(code, res);
  }
};

}  // namespace

WasmModule RandomWasmModule(Random& r) {
  WasmBuilder builder(r);
  return builder.Build();
}

}  // namespace redline
