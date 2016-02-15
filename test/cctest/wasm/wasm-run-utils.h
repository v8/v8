// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WASM_RUN_UTILS_H
#define WASM_RUN_UTILS_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "src/base/utils/random-number-generator.h"

#include "src/compiler/graph-visualizer.h"
#include "src/compiler/int64-lowering.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/wasm-compiler.h"

#include "src/wasm/ast-decoder.h"
#include "src/wasm/wasm-js.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-opcodes.h"

#include "test/cctest/cctest.h"
#include "test/cctest/compiler/codegen-tester.h"
#include "test/cctest/compiler/graph-builder-tester.h"

// TODO(titzer): pull WASM_64 up to a common header.
#if !V8_TARGET_ARCH_32_BIT || V8_TARGET_ARCH_X64
#define WASM_64 1
#else
#define WASM_64 0
#endif

// TODO(titzer): check traps more robustly in tests.
// Currently, in tests, we just return 0xdeadbeef from the function in which
// the trap occurs if the runtime context is not available to throw a JavaScript
// exception.
#define CHECK_TRAP32(x) \
  CHECK_EQ(0xdeadbeef, (bit_cast<uint32_t>(x)) & 0xFFFFFFFF)
#define CHECK_TRAP64(x) \
  CHECK_EQ(0xdeadbeefdeadbeef, (bit_cast<uint64_t>(x)) & 0xFFFFFFFFFFFFFFFF)
#define CHECK_TRAP(x) CHECK_TRAP32(x)

namespace {
using namespace v8::base;
using namespace v8::internal;
using namespace v8::internal::compiler;
using namespace v8::internal::wasm;

inline void init_env(FunctionEnv* env, FunctionSig* sig) {
  env->module = nullptr;
  env->sig = sig;
  env->local_i32_count = 0;
  env->local_i64_count = 0;
  env->local_f32_count = 0;
  env->local_f64_count = 0;
  env->SumLocals();
}

const uint32_t kMaxGlobalsSize = 128;

// A helper for module environments that adds the ability to allocate memory
// and global variables. Contains a built-in {WasmModuleInstance}.
class TestingModule : public ModuleEnv {
 public:
  TestingModule() : instance_(nullptr), global_offset(0) {
    instance = &instance_;
    instance->globals_start = global_data;
    instance->globals_size = kMaxGlobalsSize;
    instance->mem_start = nullptr;
    instance->mem_size = 0;
    instance->function_code = nullptr;
    module = nullptr;
    linker = nullptr;
    asm_js = false;
    memset(global_data, 0, sizeof(global_data));
  }

  ~TestingModule() {
    if (instance->mem_start) {
      free(instance->mem_start);
    }
    if (instance->function_code) delete instance->function_code;
    if (module) delete module;
  }

  byte* AddMemory(size_t size) {
    CHECK_NULL(instance->mem_start);
    CHECK_EQ(0, instance->mem_size);
    instance->mem_start = reinterpret_cast<byte*>(malloc(size));
    CHECK(instance->mem_start);
    memset(instance->mem_start, 0, size);
    instance->mem_size = size;
    return raw_mem_start<byte>();
  }

  template <typename T>
  T* AddMemoryElems(size_t count) {
    AddMemory(count * sizeof(T));
    return raw_mem_start<T>();
  }

  template <typename T>
  T* AddGlobal(MachineType mem_type) {
    WasmGlobal* global = AddGlobal(mem_type);
    return reinterpret_cast<T*>(instance->globals_start + global->offset);
  }

  byte AddSignature(FunctionSig* sig) {
    AllocModule();
    if (!module->signatures) {
      module->signatures = new std::vector<FunctionSig*>();
    }
    module->signatures->push_back(sig);
    size_t size = module->signatures->size();
    CHECK(size < 127);
    return static_cast<byte>(size - 1);
  }

  template <typename T>
  T* raw_mem_start() {
    DCHECK(instance->mem_start);
    return reinterpret_cast<T*>(instance->mem_start);
  }

  template <typename T>
  T* raw_mem_end() {
    DCHECK(instance->mem_start);
    return reinterpret_cast<T*>(instance->mem_start + instance->mem_size);
  }

  template <typename T>
  T raw_mem_at(int i) {
    DCHECK(instance->mem_start);
    return reinterpret_cast<T*>(instance->mem_start)[i];
  }

  template <typename T>
  T raw_val_at(int i) {
    T val;
    memcpy(&val, reinterpret_cast<void*>(instance->mem_start + i), sizeof(T));
    return val;
  }

  // Zero-initialize the memory.
  void BlankMemory() {
    byte* raw = raw_mem_start<byte>();
    memset(raw, 0, instance->mem_size);
  }

  // Pseudo-randomly intialize the memory.
  void RandomizeMemory(unsigned int seed = 88) {
    byte* raw = raw_mem_start<byte>();
    byte* end = raw_mem_end<byte>();
    v8::base::RandomNumberGenerator rng;
    rng.SetSeed(seed);
    rng.NextBytes(raw, end - raw);
  }

  WasmFunction* AddFunction(FunctionSig* sig, Handle<Code> code) {
    AllocModule();
    if (module->functions == nullptr) {
      module->functions = new std::vector<WasmFunction>();
      instance->function_code = new std::vector<Handle<Code>>();
    }
    uint32_t index = static_cast<uint32_t>(module->functions->size());
    module->functions->push_back(
        {sig, index, 0, 0, 0, 0, 0, 0, 0, false, false});
    instance->function_code->push_back(code);
    return &module->functions->back();
  }

  void AddIndirectFunctionTable(int* functions, int table_size) {
    AllocModule();
    Isolate* isolate = module->shared_isolate;
    Handle<FixedArray> fixed =
        isolate->factory()->NewFixedArray(2 * table_size);
    instance->function_table = fixed;
    module->function_table = new std::vector<uint16_t>();
    for (int i = 0; i < table_size; i++) {
      module->function_table->push_back(functions[i]);
    }
  }

  void PopulateIndirectFunctionTable() {
    if (instance->function_table.is_null()) return;
    int table_size = static_cast<int>(module->function_table->size());
    for (int i = 0; i < table_size; i++) {
      int function_index = module->function_table->at(i);
      WasmFunction* function = &module->functions->at(function_index);
      instance->function_table->set(i, Smi::FromInt(function->sig_index));
      instance->function_table->set(
          i + table_size, *instance->function_code->at(function_index));
    }
  }


 private:
  WasmModuleInstance instance_;
  uint32_t global_offset;
  V8_ALIGNED(8) byte global_data[kMaxGlobalsSize];  // preallocated global data.

  WasmGlobal* AddGlobal(MachineType mem_type) {
    AllocModule();
    if (!module->globals) {
      module->globals = new std::vector<WasmGlobal>();
    }
    byte size = WasmOpcodes::MemSize(mem_type);
    global_offset = (global_offset + size - 1) & ~(size - 1);  // align
    module->globals->push_back({0, mem_type, global_offset, false});
    global_offset += size;
    // limit number of globals.
    CHECK_LT(global_offset, kMaxGlobalsSize);
    return &module->globals->back();
  }
  void AllocModule() {
    if (module == nullptr) {
      module = new WasmModule();
      module->shared_isolate = CcTest::InitIsolateOnce();
      module->globals = nullptr;
      module->functions = nullptr;
      module->data_segments = nullptr;
    }
  }
};


inline void TestBuildingGraph(Zone* zone, JSGraph* jsgraph, FunctionEnv* env,
                              const byte* start, const byte* end) {
  compiler::WasmGraphBuilder builder(zone, jsgraph, env->sig);
  TreeResult result = BuildTFGraph(&builder, env, start, end);
  if (result.failed()) {
    ptrdiff_t pc = result.error_pc - result.start;
    ptrdiff_t pt = result.error_pt - result.start;
    std::ostringstream str;
    str << "Verification failed: " << result.error_code << " pc = +" << pc;
    if (result.error_pt) str << ", pt = +" << pt;
    str << ", msg = " << result.error_msg.get();
    FATAL(str.str().c_str());
  }
  builder.Int64LoweringForTesting();
  if (FLAG_trace_turbo_graph) {
    OFStream os(stdout);
    os << AsRPO(*jsgraph->graph());
  }
}


// A helper for compiling functions that are only internally callable WASM code.
class WasmFunctionCompiler : public HandleAndZoneScope,
                             private GraphAndBuilders {
 public:
  explicit WasmFunctionCompiler(FunctionSig* sig, ModuleEnv* module = nullptr)
      : GraphAndBuilders(main_zone()),
        jsgraph(this->isolate(), this->graph(), this->common(), nullptr,
                nullptr, this->machine()),
        descriptor_(nullptr) {
    init_env(&env, sig);
    env.module = module;
  }

  JSGraph jsgraph;
  FunctionEnv env;
  // The call descriptor is initialized when the function is compiled.
  CallDescriptor* descriptor_;

  Isolate* isolate() { return main_isolate(); }
  Graph* graph() const { return main_graph_; }
  Zone* zone() const { return graph()->zone(); }
  CommonOperatorBuilder* common() { return &main_common_; }
  MachineOperatorBuilder* machine() { return &main_machine_; }
  CallDescriptor* descriptor() { return descriptor_; }

  void Build(const byte* start, const byte* end) {
    TestBuildingGraph(main_zone(), &jsgraph, &env, start, end);
  }

  byte AllocateLocal(LocalType type) {
    int result = static_cast<int>(env.total_locals);
    env.AddLocals(type, 1);
    byte b = static_cast<byte>(result);
    CHECK_EQ(result, b);
    return b;
  }

  Handle<Code> Compile(ModuleEnv* module) {
    descriptor_ = module->GetWasmCallDescriptor(this->zone(), env.sig);
    CompilationInfo info("wasm compile", this->isolate(), this->zone());
    Handle<Code> result =
        Pipeline::GenerateCodeForTesting(&info, descriptor_, this->graph());
#ifdef ENABLE_DISASSEMBLER
    if (!result.is_null() && FLAG_print_opt_code) {
      OFStream os(stdout);
      result->Disassemble("wasm code", os);
    }
#endif

    return result;
  }

  uint32_t CompileAndAdd(TestingModule* module, int sig_index = 0) {
    uint32_t index = 0;
    if (module->module && module->module->functions) {
      index = static_cast<uint32_t>(module->module->functions->size());
    }
    WasmFunction* function = module->AddFunction(env.sig, Compile(module));
    function->sig_index = sig_index;
    return index;
  }
};


// A helper class to build graphs from Wasm bytecode, generate machine
// code, and run that code.
template <typename ReturnType>
class WasmRunner {
 public:
  WasmRunner(MachineType p0 = MachineType::None(),
             MachineType p1 = MachineType::None(),
             MachineType p2 = MachineType::None(),
             MachineType p3 = MachineType::None())
      : signature_(MachineTypeForC<ReturnType>() == MachineType::None() ? 0 : 1,
                   GetParameterCount(p0, p1, p2, p3), storage_),
        compiler_(&signature_),
        call_wrapper_(p0, p1, p2, p3),
        compilation_done_(false) {
    int index = 0;
    MachineType ret = MachineTypeForC<ReturnType>();
    if (ret != MachineType::None()) {
      storage_[index++] = WasmOpcodes::LocalTypeFor(ret);
    }
    if (p0 != MachineType::None())
      storage_[index++] = WasmOpcodes::LocalTypeFor(p0);
    if (p1 != MachineType::None())
      storage_[index++] = WasmOpcodes::LocalTypeFor(p1);
    if (p2 != MachineType::None())
      storage_[index++] = WasmOpcodes::LocalTypeFor(p2);
    if (p3 != MachineType::None())
      storage_[index++] = WasmOpcodes::LocalTypeFor(p3);
  }


  FunctionEnv* env() { return &compiler_.env; }


  // Builds a graph from the given Wasm code, and generates the machine
  // code and call wrapper for that graph. This method must not be called
  // more than once.
  void Build(const byte* start, const byte* end) {
    DCHECK(!compilation_done_);
    compilation_done_ = true;
    // Build the TF graph.
    compiler_.Build(start, end);
    // Generate code.
    Handle<Code> code = compiler_.Compile(env()->module);

    // Construct the call wrapper.
    Node* inputs[5];
    int input_count = 0;
    inputs[input_count++] = call_wrapper_.HeapConstant(code);
    for (size_t i = 0; i < signature_.parameter_count(); i++) {
      inputs[input_count++] = call_wrapper_.Parameter(i);
    }

    call_wrapper_.Return(call_wrapper_.AddNode(
        call_wrapper_.common()->Call(compiler_.descriptor()), input_count,
        inputs));
  }

  ReturnType Call() { return call_wrapper_.Call(); }

  template <typename P0>
  ReturnType Call(P0 p0) {
    return call_wrapper_.Call(p0);
  }

  template <typename P0, typename P1>
  ReturnType Call(P0 p0, P1 p1) {
    return call_wrapper_.Call(p0, p1);
  }

  template <typename P0, typename P1, typename P2>
  ReturnType Call(P0 p0, P1 p1, P2 p2) {
    return call_wrapper_.Call(p0, p1, p2);
  }

  template <typename P0, typename P1, typename P2, typename P3>
  ReturnType Call(P0 p0, P1 p1, P2 p2, P3 p3) {
    return call_wrapper_.Call(p0, p1, p2, p3);
  }

  byte AllocateLocal(LocalType type) {
    int result = static_cast<int>(env()->total_locals);
    env()->AddLocals(type, 1);
    byte b = static_cast<byte>(result);
    CHECK_EQ(result, b);
    return b;
  }

 private:
  LocalType storage_[5];
  FunctionSig signature_;
  WasmFunctionCompiler compiler_;
  BufferedRawMachineAssemblerTester<ReturnType> call_wrapper_;
  bool compilation_done_;

  static size_t GetParameterCount(MachineType p0, MachineType p1,
                                  MachineType p2, MachineType p3) {
    if (p0 == MachineType::None()) return 0;
    if (p1 == MachineType::None()) return 1;
    if (p2 == MachineType::None()) return 2;
    if (p3 == MachineType::None()) return 3;
    return 4;
  }
};

}  // namespace

#endif
