// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/assembler.h"
#include "src/codegen.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-type.h"
#include "src/compiler/raw-machine-assembler.h"

#include "test/cctest/cctest.h"
#include "test/cctest/compiler/codegen-tester.h"
#include "test/cctest/compiler/graph-builder-tester.h"
#include "test/cctest/compiler/value-helper.h"

using namespace v8::base;
using namespace v8::internal;
using namespace v8::internal::compiler;

typedef RawMachineAssembler::Label MLabel;

#if !V8_TARGET_ARCH_ARM64
// TODO(titzer): fix native stack parameters on arm64
#define NATIVE_STACK_PARAMS_OK
#endif

namespace {
// Picks a representative set of registers from the allocatable set.
// If there are less than 100 possible pairs, do them all, otherwise try
// to select a representative set.
class RegisterPairs {
 public:
  RegisterPairs()
      : max_(std::min(100, Register::kMaxNumAllocatableRegisters *
                               Register::kMaxNumAllocatableRegisters)),
        counter_(0) {}

  bool More() { return counter_ < max_; }

  void Next(int* r0, int* r1, bool same_is_ok) {
    do {
      // Find the next pair.
      if (exhaustive()) {
        *r0 = counter_ % Register::kMaxNumAllocatableRegisters;
        *r1 = counter_ / Register::kMaxNumAllocatableRegisters;
      } else {
        // Try each register at least once for both r0 and r1.
        int index = counter_ / 2;
        if (counter_ & 1) {
          *r0 = index % Register::kMaxNumAllocatableRegisters;
          *r1 = index / Register::kMaxNumAllocatableRegisters;
        } else {
          *r1 = index % Register::kMaxNumAllocatableRegisters;
          *r0 = index / Register::kMaxNumAllocatableRegisters;
        }
      }
      counter_++;
      if (same_is_ok) break;
      if (*r0 == *r1) {
        if (counter_ >= max_) {
          // For the last hurrah, reg#0 with reg#n-1
          *r0 = 0;
          *r1 = Register::kMaxNumAllocatableRegisters - 1;
          break;
        }
      }
    } while (true);

    DCHECK(*r0 >= 0 && *r0 < Register::kMaxNumAllocatableRegisters);
    DCHECK(*r1 >= 0 && *r1 < Register::kMaxNumAllocatableRegisters);
    printf("pair = %d, %d\n", *r0, *r1);
  }

 private:
  int max_;
  int counter_;
  bool exhaustive() {
    return max_ == (Register::kMaxNumAllocatableRegisters *
                    Register::kMaxNumAllocatableRegisters);
  }
};


// Helper for allocating either an GP or FP reg, or the next stack slot.
struct Allocator {
  Allocator(int* gp, int gpc, int* fp, int fpc)
      : gp_count(gpc),
        gp_offset(0),
        gp_regs(gp),
        fp_count(fpc),
        fp_offset(0),
        fp_regs(fp),
        stack_offset(0) {}

  int gp_count;
  int gp_offset;
  int* gp_regs;

  int fp_count;
  int fp_offset;
  int* fp_regs;

  int stack_offset;

  LinkageLocation Next(MachineType type) {
    if (IsFloatingPoint(type)) {
      // Allocate a floating point register/stack location.
      if (fp_offset < fp_count) {
        return LinkageLocation::ForRegister(fp_regs[fp_offset++]);
      } else {
        int offset = -1 - stack_offset;
        stack_offset += Words(type);
        return LinkageLocation::ForCallerFrameSlot(offset);
      }
    } else {
      // Allocate a general purpose register/stack location.
      if (gp_offset < gp_count) {
        return LinkageLocation::ForRegister(gp_regs[gp_offset++]);
      } else {
        int offset = -1 - stack_offset;
        stack_offset += Words(type);
        return LinkageLocation::ForCallerFrameSlot(offset);
      }
    }
  }
  bool IsFloatingPoint(MachineType type) {
    return RepresentationOf(type) == kRepFloat32 ||
           RepresentationOf(type) == kRepFloat64;
  }
  int Words(MachineType type) {
    int size = ElementSizeOf(type);
    return size <= kPointerSize ? 1 : size / kPointerSize;
  }
  void Reset() {
    fp_offset = 0;
    gp_offset = 0;
    stack_offset = 0;
  }
};


class RegisterConfig {
 public:
  RegisterConfig(Allocator& p, Allocator& r) : params(p), rets(r) {}

  CallDescriptor* Create(Zone* zone, MachineSignature* msig) {
    rets.Reset();
    params.Reset();

    LocationSignature::Builder locations(zone, msig->return_count(),
                                         msig->parameter_count());
    // Add return location(s).
    const int return_count = static_cast<int>(locations.return_count_);
    for (int i = 0; i < return_count; i++) {
      locations.AddReturn(rets.Next(msig->GetReturn(i)));
    }

    // Add register and/or stack parameter(s).
    const int parameter_count = static_cast<int>(msig->parameter_count());
    for (int i = 0; i < parameter_count; i++) {
      locations.AddParam(params.Next(msig->GetParam(i)));
    }

    const RegList kCalleeSaveRegisters = 0;
    const RegList kCalleeSaveFPRegisters = 0;

    MachineType target_type = compiler::kMachAnyTagged;
    LinkageLocation target_loc = LinkageLocation::ForAnyRegister();
    int stack_param_count = params.stack_offset;
    return new (zone) CallDescriptor(       // --
        CallDescriptor::kCallCodeObject,    // kind
        target_type,                        // target MachineType
        target_loc,                         // target location
        msig,                               // machine_sig
        locations.Build(),                  // location_sig
        stack_param_count,                  // stack_parameter_count
        compiler::Operator::kNoProperties,  // properties
        kCalleeSaveRegisters,               // callee-saved registers
        kCalleeSaveFPRegisters,             // callee-saved fp regs
        CallDescriptor::kNoFlags,           // flags
        "c-call");
  }

 private:
  Allocator& params;
  Allocator& rets;
};

const int kMaxParamCount = 64;

MachineType kIntTypes[kMaxParamCount + 1] = {
    kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32,
    kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32,
    kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32,
    kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32,
    kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32,
    kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32,
    kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32,
    kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32,
    kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32,
    kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32,
    kMachInt32, kMachInt32, kMachInt32, kMachInt32, kMachInt32};


// For making uniform int32 signatures shorter.
class Int32Signature : public MachineSignature {
 public:
  explicit Int32Signature(int param_count)
      : MachineSignature(1, param_count, kIntTypes) {
    CHECK(param_count <= kMaxParamCount);
  }
};


Handle<Code> CompileGraph(const char* name, CallDescriptor* desc, Graph* graph,
                          Schedule* schedule = nullptr) {
  Isolate* isolate = CcTest::InitIsolateOnce();
  Handle<Code> code =
      Pipeline::GenerateCodeForTesting(isolate, desc, graph, schedule);
  CHECK(!code.is_null());
#ifdef ENABLE_DISASSEMBLER
  if (FLAG_print_opt_code) {
    OFStream os(stdout);
    code->Disassemble(name, os);
  }
#endif
  return code;
}


Handle<Code> WrapWithCFunction(Handle<Code> inner, CallDescriptor* desc) {
  Zone zone;
  MachineSignature* msig =
      const_cast<MachineSignature*>(desc->GetMachineSignature());
  int param_count = static_cast<int>(msig->parameter_count());
  GraphAndBuilders caller(&zone);
  {
    GraphAndBuilders& b = caller;
    Node* start = b.graph()->NewNode(b.common()->Start(param_count + 3));
    b.graph()->SetStart(start);
    Unique<HeapObject> unique = Unique<HeapObject>::CreateUninitialized(inner);
    Node* target = b.graph()->NewNode(b.common()->HeapConstant(unique));

    // Add arguments to the call.
    Node** args = zone.NewArray<Node*>(param_count + 3);
    int index = 0;
    args[index++] = target;
    for (int i = 0; i < param_count; i++) {
      args[index] = b.graph()->NewNode(b.common()->Parameter(i), start);
      index++;
    }
    args[index++] = start;  // effect.
    args[index++] = start;  // control.

    // Build the call and return nodes.
    Node* call =
        b.graph()->NewNode(b.common()->Call(desc), param_count + 3, args);
    Node* ret = b.graph()->NewNode(b.common()->Return(), call, call, start);
    b.graph()->SetEnd(ret);
  }

  CallDescriptor* cdesc = Linkage::GetSimplifiedCDescriptor(&zone, msig);

  return CompileGraph("wrapper", cdesc, caller.graph());
}


}  // namespace


static void TestInt32Sub(CallDescriptor* desc) {
  Isolate* isolate = CcTest::InitIsolateOnce();
  HandleScope scope(isolate);
  Zone zone;
  GraphAndBuilders inner(&zone);
  {
    // Build the add function.
    GraphAndBuilders& b = inner;
    Node* start = b.graph()->NewNode(b.common()->Start(5));
    b.graph()->SetStart(start);
    Node* p0 = b.graph()->NewNode(b.common()->Parameter(0), start);
    Node* p1 = b.graph()->NewNode(b.common()->Parameter(1), start);
    Node* add = b.graph()->NewNode(b.machine()->Int32Sub(), p0, p1);
    Node* ret = b.graph()->NewNode(b.common()->Return(), add, start, start);
    b.graph()->SetEnd(ret);
  }

  Handle<Code> inner_code = CompileGraph("Int32Sub", desc, inner.graph());
  Handle<Code> wrapper = WrapWithCFunction(inner_code, desc);
  MachineSignature* msig =
      const_cast<MachineSignature*>(desc->GetMachineSignature());
  CodeRunner<int32_t> runnable(isolate, wrapper,
                               CSignature::FromMachine(&zone, msig));

  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) {
      int32_t expected = static_cast<int32_t>(static_cast<uint32_t>(*i) -
                                              static_cast<uint32_t>(*j));
      int32_t result = runnable.Call(*i, *j);
      CHECK_EQ(expected, result);
    }
  }
}


#ifdef NATIVE_STACK_PARAMS_OK
static void CopyTwentyInt32(CallDescriptor* desc) {
  const int kNumParams = 20;
  int32_t input[kNumParams];
  int32_t output[kNumParams];
  Isolate* isolate = CcTest::InitIsolateOnce();
  HandleScope scope(isolate);
  Handle<Code> inner = Handle<Code>::null();
  {
    // Writes all parameters into the output buffer.
    Zone zone;
    Graph graph(&zone);
    RawMachineAssembler raw(isolate, &graph, desc);
    Node* base = raw.PointerConstant(output);
    for (int i = 0; i < kNumParams; i++) {
      Node* offset = raw.Int32Constant(i * sizeof(int32_t));
      raw.Store(kMachInt32, base, offset, raw.Parameter(i));
    }
    raw.Return(raw.Int32Constant(42));
    inner = CompileGraph("CopyTwentyInt32", desc, &graph, raw.Export());
  }

  CSignature0<int32_t> csig;
  Handle<Code> wrapper = Handle<Code>::null();
  {
    // Loads parameters from the input buffer and calls the above code.
    Zone zone;
    Graph graph(&zone);
    CallDescriptor* cdesc = Linkage::GetSimplifiedCDescriptor(&zone, &csig);
    RawMachineAssembler raw(isolate, &graph, cdesc);
    Node* base = raw.PointerConstant(input);
    Unique<HeapObject> unique = Unique<HeapObject>::CreateUninitialized(inner);
    Node* target = raw.HeapConstant(unique);
    Node** args = zone.NewArray<Node*>(kNumParams);
    for (int i = 0; i < kNumParams; i++) {
      Node* offset = raw.Int32Constant(i * sizeof(int32_t));
      args[i] = raw.Load(kMachInt32, base, offset);
    }

    Node* call = raw.CallN(desc, target, args);
    raw.Return(call);
    wrapper =
        CompileGraph("CopyTwentyInt32-wrapper", cdesc, &graph, raw.Export());
  }

  CodeRunner<int32_t> runnable(isolate, wrapper, &csig);

  // Run the code, checking it correctly implements the memcpy.
  for (int i = 0; i < 5; i++) {
    uint32_t base = 1111111111u * i;
    for (int j = 0; j < kNumParams; j++) {
      input[j] = static_cast<int32_t>(base + 13 * j);
    }

    memset(output, 0, sizeof(output));
    CHECK_EQ(42, runnable.Call());

    for (int j = 0; j < kNumParams; j++) {
      CHECK_EQ(input[j], output[j]);
    }
  }
}
#endif  // NATIVE_STACK_PARAMS_OK


static void Test_RunInt32SubWithRet(int retreg) {
  Int32Signature sig(2);
  Zone zone;
  RegisterPairs pairs;
  while (pairs.More()) {
    int parray[2];
    int rarray[] = {retreg};
    pairs.Next(&parray[0], &parray[1], false);
    Allocator params(parray, 2, nullptr, 0);
    Allocator rets(rarray, 1, nullptr, 0);
    RegisterConfig config(params, rets);
    CallDescriptor* desc = config.Create(&zone, &sig);
    TestInt32Sub(desc);
  }
}


// Separate tests for parallelization.
#define TEST_INT32_SUB_WITH_RET(x)                                             \
  TEST(Run_Int32Sub_all_allocatable_pairs_##x) {                               \
    if (Register::kMaxNumAllocatableRegisters > x) Test_RunInt32SubWithRet(x); \
  }


TEST_INT32_SUB_WITH_RET(0)
TEST_INT32_SUB_WITH_RET(1)
TEST_INT32_SUB_WITH_RET(2)
TEST_INT32_SUB_WITH_RET(3)
TEST_INT32_SUB_WITH_RET(4)
TEST_INT32_SUB_WITH_RET(5)
TEST_INT32_SUB_WITH_RET(6)
TEST_INT32_SUB_WITH_RET(7)
TEST_INT32_SUB_WITH_RET(8)
TEST_INT32_SUB_WITH_RET(9)
TEST_INT32_SUB_WITH_RET(10)
TEST_INT32_SUB_WITH_RET(11)
TEST_INT32_SUB_WITH_RET(12)
TEST_INT32_SUB_WITH_RET(13)
TEST_INT32_SUB_WITH_RET(14)
TEST_INT32_SUB_WITH_RET(15)
TEST_INT32_SUB_WITH_RET(16)
TEST_INT32_SUB_WITH_RET(17)
TEST_INT32_SUB_WITH_RET(18)
TEST_INT32_SUB_WITH_RET(19)


TEST(Run_Int32Sub_all_allocatable_single) {
#ifdef NATIVE_STACK_PARAMS_OK
  Int32Signature sig(2);
  RegisterPairs pairs;
  while (pairs.More()) {
    Zone zone;
    int parray[1];
    int rarray[1];
    pairs.Next(&rarray[0], &parray[0], true);
    Allocator params(parray, 1, nullptr, 0);
    Allocator rets(rarray, 1, nullptr, 0);
    RegisterConfig config(params, rets);
    CallDescriptor* desc = config.Create(&zone, &sig);
    TestInt32Sub(desc);
  }
#endif  // NATIVE_STACK_PARAMS_OK
}


TEST(Run_CopyTwentyInt32_all_allocatable_pairs) {
#ifdef NATIVE_STACK_PARAMS_OK
  Int32Signature sig(20);
  RegisterPairs pairs;
  while (pairs.More()) {
    Zone zone;
    int parray[2];
    int rarray[] = {0};
    pairs.Next(&parray[0], &parray[1], false);
    Allocator params(parray, 2, nullptr, 0);
    Allocator rets(rarray, 1, nullptr, 0);
    RegisterConfig config(params, rets);
    CallDescriptor* desc = config.Create(&zone, &sig);
    CopyTwentyInt32(desc);
  }
#endif  // NATIVE_STACK_PARAMS_OK
}


#ifdef NATIVE_STACK_PARAMS_OK
int ParamCount(CallDescriptor* desc) {
  return static_cast<int>(desc->GetMachineSignature()->parameter_count());
}


// Super mega helper routine to generate a computation with a given
// call descriptor, compile the code, wrap the code, and pass various inputs,
// comparing against a reference implementation.
static void Run_Int32_Computation(
    CallDescriptor* desc, void (*build)(CallDescriptor*, RawMachineAssembler&),
    int32_t (*compute)(CallDescriptor*, int32_t* inputs), int seed = 1) {
  int num_params = ParamCount(desc);
  CHECK_LE(num_params, kMaxParamCount);
  int32_t input[kMaxParamCount];
  Isolate* isolate = CcTest::InitIsolateOnce();
  HandleScope scope(isolate);
  Handle<Code> inner = Handle<Code>::null();
  {
    // Build the graph for the computation.
    Zone zone;
    Graph graph(&zone);
    RawMachineAssembler raw(isolate, &graph, desc);
    build(desc, raw);
    inner = CompileGraph("Compute", desc, &graph, raw.Export());
  }

  CSignature0<int32_t> csig;

  if (false) {
    // constant mode.
    Handle<Code> wrapper = Handle<Code>::null();
    {
      // Wrap the above code with a callable function that passes constants.
      Zone zone;
      Graph graph(&zone);
      CallDescriptor* cdesc = Linkage::GetSimplifiedCDescriptor(&zone, &csig);
      RawMachineAssembler raw(isolate, &graph, cdesc);
      Unique<HeapObject> unique =
          Unique<HeapObject>::CreateUninitialized(inner);
      Node* target = raw.HeapConstant(unique);
      Node** args = zone.NewArray<Node*>(kMaxParamCount);
      for (int i = 0; i < num_params; i++) {
        args[i] = raw.Int32Constant(0x100 + i);
      }

      Node* call = raw.CallN(desc, target, args);
      raw.Return(call);
      wrapper = CompileGraph("Compute-wrapper", cdesc, &graph, raw.Export());
    }

    CodeRunner<int32_t> runnable(isolate, wrapper, &csig);

    // Run the code, checking it against the reference.
    for (int j = 0; j < kMaxParamCount; j++) {
      input[j] = 0x100 + j;
    }
    int32_t expected = compute(desc, input);
    int32_t result = runnable.Call();

    CHECK_EQ(expected, result);
  }

  {
    // buffer mode.
    Handle<Code> wrapper = Handle<Code>::null();
    {
      // Wrap the above code with a callable function that loads from {input}.
      Zone zone;
      Graph graph(&zone);
      CallDescriptor* cdesc = Linkage::GetSimplifiedCDescriptor(&zone, &csig);
      RawMachineAssembler raw(isolate, &graph, cdesc);
      Node* base = raw.PointerConstant(input);
      Unique<HeapObject> unique =
          Unique<HeapObject>::CreateUninitialized(inner);
      Node* target = raw.HeapConstant(unique);
      Node** args = zone.NewArray<Node*>(kMaxParamCount);
      for (int i = 0; i < num_params; i++) {
        Node* offset = raw.Int32Constant(i * sizeof(int32_t));
        args[i] = raw.Load(kMachInt32, base, offset);
      }

      Node* call = raw.CallN(desc, target, args);
      raw.Return(call);
      wrapper = CompileGraph("Compute-wrapper", cdesc, &graph, raw.Export());
    }

    CodeRunner<int32_t> runnable(isolate, wrapper, &csig);

    // Run the code, checking it against the reference.
    for (int i = 0; i < 5; i++) {
      // Use pseudo-random values for each run, but the first run gets args
      // 100, 101, 102, 103... for easier diagnosis.
      uint32_t base = 1111111111u * i * seed;
      for (int j = 0; j < kMaxParamCount; j++) {
        input[j] = static_cast<int32_t>(100 + base + j);
      }
      int32_t expected = compute(desc, input);
      int32_t result = runnable.Call();

      CHECK_EQ(expected, result);
    }
  }
}


static uint32_t coeff[] = {1,  2,  3,  5,  7,   11,  13,  17,  19, 23, 29,
                           31, 37, 41, 43, 47,  53,  59,  61,  67, 71, 73,
                           79, 83, 89, 97, 101, 103, 107, 109, 113};


static void Build_Int32_WeightedSum(CallDescriptor* desc,
                                    RawMachineAssembler& raw) {
  Node* result = raw.Int32Constant(0);
  for (int i = 0; i < ParamCount(desc); i++) {
    Node* term = raw.Int32Mul(raw.Parameter(i), raw.Int32Constant(coeff[i]));
    result = raw.Int32Add(result, term);
  }
  raw.Return(result);
}


static int32_t Compute_Int32_WeightedSum(CallDescriptor* desc, int32_t* input) {
  uint32_t result = 0;
  for (int i = 0; i < ParamCount(desc); i++) {
    result += static_cast<uint32_t>(input[i]) * coeff[i];
  }
  return static_cast<int32_t>(result);
}


static void Test_Int32_WeightedSum_of_size(int count) {
  Int32Signature sig(count);
  for (int p0 = 0; p0 < Register::kMaxNumAllocatableRegisters; p0++) {
    Zone zone;

    int parray[] = {p0};
    int rarray[] = {0};
    Allocator params(parray, 1, nullptr, 0);
    Allocator rets(rarray, 1, nullptr, 0);
    RegisterConfig config(params, rets);
    CallDescriptor* desc = config.Create(&zone, &sig);
    Run_Int32_Computation(desc, Build_Int32_WeightedSum,
                          Compute_Int32_WeightedSum, 257 + count);
  }
}


// Separate tests for parallelization.
#define TEST_INT32_WEIGHTEDSUM(x) \
  TEST(Run_Int32_WeightedSum_##x) { Test_Int32_WeightedSum_of_size(x); }


TEST_INT32_WEIGHTEDSUM(1)
TEST_INT32_WEIGHTEDSUM(2)
TEST_INT32_WEIGHTEDSUM(3)
TEST_INT32_WEIGHTEDSUM(4)
TEST_INT32_WEIGHTEDSUM(5)
TEST_INT32_WEIGHTEDSUM(7)
TEST_INT32_WEIGHTEDSUM(9)
TEST_INT32_WEIGHTEDSUM(11)
TEST_INT32_WEIGHTEDSUM(17)
TEST_INT32_WEIGHTEDSUM(19)


template <int which>
static void Build_Int32_Select(CallDescriptor* desc, RawMachineAssembler& raw) {
  raw.Return(raw.Parameter(which));
}


template <int which>
static int32_t Compute_Int32_Select(CallDescriptor* desc, int32_t* inputs) {
  return inputs[which];
}


template <int which>
void Test_Int32_Select() {
  int parray[] = {0};
  int rarray[] = {0};
  Allocator params(parray, 1, nullptr, 0);
  Allocator rets(rarray, 1, nullptr, 0);
  RegisterConfig config(params, rets);

  Zone zone;

  for (int i = which + 1; i <= 64; i++) {
    Int32Signature sig(i);
    CallDescriptor* desc = config.Create(&zone, &sig);
    Run_Int32_Computation(desc, Build_Int32_Select<which>,
                          Compute_Int32_Select<which>, 1025 + which);
  }
}


// Separate tests for parallelization.
#define TEST_INT32_SELECT(x) \
  TEST(Run_Int32_Select_##x) { Test_Int32_Select<x>(); }


TEST_INT32_SELECT(0)
TEST_INT32_SELECT(1)
TEST_INT32_SELECT(2)
TEST_INT32_SELECT(3)
TEST_INT32_SELECT(4)
TEST_INT32_SELECT(5)
TEST_INT32_SELECT(6)
TEST_INT32_SELECT(11)
TEST_INT32_SELECT(15)
TEST_INT32_SELECT(19)
TEST_INT32_SELECT(45)
TEST_INT32_SELECT(62)
TEST_INT32_SELECT(63)
#endif  // NATIVE_STACK_PARAMS_OK


TEST(TheLastTestForLint) {
  // Yes, thank you.
}
