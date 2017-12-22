// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>
#include <functional>
#include <limits>
#include <memory>

#include "src/assembler.h"
#include "src/base/bits.h"
#include "src/base/utils/random-number-generator.h"
#include "src/codegen.h"
#include "src/compiler.h"
#include "src/compiler/linkage.h"
#include "src/machine-type.h"
#include "src/macro-assembler.h"
#include "src/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/codegen-tester.h"
#include "test/cctest/compiler/value-helper.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

int index(MachineType type) { return static_cast<int>(type.representation()); }

int size(MachineType type) {
  return 1 << ElementSizeLog2Of(type.representation());
}

bool is_float(MachineType type) {
  MachineRepresentation rep = type.representation();
  return rep == MachineRepresentation::kFloat32 ||
         rep == MachineRepresentation::kFloat64;
}

int num_registers(MachineType type) {
  const RegisterConfiguration* config = RegisterConfiguration::Default();
  switch (type.representation()) {
    case MachineRepresentation::kWord32:
    case MachineRepresentation::kWord64:
      return config->num_allocatable_general_registers();
    case MachineRepresentation::kFloat32:
      return config->num_allocatable_float_registers();
    case MachineRepresentation::kFloat64:
      return config->num_allocatable_double_registers();
    default:
      UNREACHABLE();
  }
}

const int* codes(MachineType type) {
  const RegisterConfiguration* config = RegisterConfiguration::Default();
  switch (type.representation()) {
    case MachineRepresentation::kWord32:
    case MachineRepresentation::kWord64:
      return config->allocatable_general_codes();
    case MachineRepresentation::kFloat32:
      return config->allocatable_float_codes();
    case MachineRepresentation::kFloat64:
      return config->allocatable_double_codes();
    default:
      UNREACHABLE();
  }
}

CallDescriptor* CreateMonoCallDescriptor(Zone* zone, int return_count,
                                         int param_count, MachineType type) {
  LocationSignature::Builder locations(zone, return_count, param_count);

  int span = std::max(1, size(type) / kPointerSize);
  int stack_params = 0;
  for (int i = 0; i < param_count; i++) {
    LinkageLocation location = LinkageLocation::ForAnyRegister();
    if (i < num_registers(type)) {
      location = LinkageLocation::ForRegister(codes(type)[i], type);
    } else {
      int slot = span * (i - param_count);
      location = LinkageLocation::ForCallerFrameSlot(slot, type);
      stack_params += span;
    }
    locations.AddParam(location);
  }

  int stack_returns = 0;
  for (int i = 0; i < return_count; i++) {
    LinkageLocation location = LinkageLocation::ForAnyRegister();
    if (i < num_registers(type)) {
      location = LinkageLocation::ForRegister(codes(type)[i], type);
    } else {
      int slot = span * (num_registers(type) - i) - stack_params - 1;
      location = LinkageLocation::ForCallerFrameSlot(slot, type);
      stack_returns += span;
    }
    locations.AddReturn(location);
  }

  const RegList kCalleeSaveRegisters = 0;
  const RegList kCalleeSaveFPRegisters = 0;

  MachineType target_type = MachineType::AnyTagged();
  LinkageLocation target_loc = LinkageLocation::ForAnyRegister(target_type);
  return new (zone) CallDescriptor(       // --
      CallDescriptor::kCallCodeObject,    // kind
      target_type,                        // target MachineType
      target_loc,                         // target location
      locations.Build(),                  // location_sig
      stack_params,                       // on-stack parameter count
      compiler::Operator::kNoProperties,  // properties
      kCalleeSaveRegisters,               // callee-saved registers
      kCalleeSaveFPRegisters,             // callee-saved fp regs
      CallDescriptor::kNoFlags,           // flags
      "c-call",                           // debug name
      0,                                  // allocatable registers
      stack_returns);                     // on-stack return count
}

MachineType RandomType(v8::base::RandomNumberGenerator* rng) {
  switch (rng->NextInt(4)) {
    case 0:
#if (!V8_TARGET_ARCH_32_BIT)
      return MachineType::Int64();
// Else fall through.
#endif
    case 1:
      return MachineType::Int32();
    case 2:
      return MachineType::Float32();
    case 3:
      return MachineType::Float64();
    default:
      UNREACHABLE();
  }
}

LinkageLocation alloc(MachineType type, int* int_count, int* float_count,
                      int* stack_slots) {
  int* count = is_float(type) ? float_count : int_count;
  LinkageLocation location = LinkageLocation::ForAnyRegister();  // Dummy.
  if (*count < num_registers(type)) {
    location = LinkageLocation::ForRegister(codes(type)[*count], type);
  } else {
    location = LinkageLocation::ForCallerFrameSlot(-*stack_slots - 1, type);
    *stack_slots += std::max(1, size(type) / kPointerSize);
  }
  ++*count;
  return location;
}

CallDescriptor* CreateRandomCallDescriptor(
    Zone* zone, int return_count, int param_count,
    v8::base::RandomNumberGenerator* rng) {
  LocationSignature::Builder locations(zone, return_count, param_count);

  int stack_slots = 0;
  int int_params = 0;
  int float_params = 0;
  for (int i = 0; i < param_count; i++) {
    MachineType type = RandomType(rng);
    LinkageLocation location =
        alloc(type, &int_params, &float_params, &stack_slots);
    locations.AddParam(location);
  }
  int stack_params = stack_slots;

  int int_returns = 0;
  int float_returns = 0;
  for (int i = 0; i < return_count; i++) {
    MachineType type = RandomType(rng);
    LinkageLocation location =
        alloc(type, &int_returns, &float_returns, &stack_slots);
    locations.AddReturn(location);
  }
  int stack_returns = stack_slots - stack_params;

  MachineType target_type = MachineType::AnyTagged();
  LinkageLocation target_loc = LinkageLocation::ForAnyRegister(target_type);
  return new (zone) CallDescriptor(       // --
      CallDescriptor::kCallCodeObject,    // kind
      target_type,                        // target MachineType
      target_loc,                         // target location
      locations.Build(),                  // location_sig
      stack_params,                       // on-stack parameter count
      compiler::Operator::kNoProperties,  // properties
      0,                                  // callee-saved registers
      0,                                  // callee-saved fp regs
      CallDescriptor::kNoFlags,           // flags
      "c-call",                           // debug name
      0,                                  // allocatable registers
      stack_returns);                     // on-stack return count
}

}  // namespace

Node* Constant(RawMachineAssembler& m, MachineType type, int value) {
  switch (type.representation()) {
    case MachineRepresentation::kWord32:
      return m.Int32Constant(static_cast<int32_t>(value));
    case MachineRepresentation::kWord64:
      return m.Int64Constant(static_cast<int64_t>(value));
    case MachineRepresentation::kFloat32:
      return m.Float32Constant(static_cast<float>(value));
    case MachineRepresentation::kFloat64:
      return m.Float64Constant(static_cast<double>(value));
    default:
      UNREACHABLE();
  }
}

Node* Add(RawMachineAssembler& m, MachineType type, Node* a, Node* b) {
  switch (type.representation()) {
    case MachineRepresentation::kWord32:
      return m.Int32Add(a, b);
    case MachineRepresentation::kWord64:
      return m.Int64Add(a, b);
    case MachineRepresentation::kFloat32:
      return m.Float32Add(a, b);
    case MachineRepresentation::kFloat64:
      return m.Float64Add(a, b);
    default:
      UNREACHABLE();
  }
}

Node* Sub(RawMachineAssembler& m, MachineType type, Node* a, Node* b) {
  switch (type.representation()) {
    case MachineRepresentation::kWord32:
      return m.Int32Sub(a, b);
    case MachineRepresentation::kWord64:
      return m.Int64Sub(a, b);
    case MachineRepresentation::kFloat32:
      return m.Float32Sub(a, b);
    case MachineRepresentation::kFloat64:
      return m.Float64Sub(a, b);
    default:
      UNREACHABLE();
  }
}

Node* Mul(RawMachineAssembler& m, MachineType type, Node* a, Node* b) {
  switch (type.representation()) {
    case MachineRepresentation::kWord32:
      return m.Int32Mul(a, b);
    case MachineRepresentation::kWord64:
      return m.Int64Mul(a, b);
    case MachineRepresentation::kFloat32:
      return m.Float32Mul(a, b);
    case MachineRepresentation::kFloat64:
      return m.Float64Mul(a, b);
    default:
      UNREACHABLE();
  }
}

Node* ToInt32(RawMachineAssembler& m, MachineType type, Node* a) {
  switch (type.representation()) {
    case MachineRepresentation::kWord32:
      return a;
    case MachineRepresentation::kWord64:
      return m.TruncateInt64ToInt32(a);
    case MachineRepresentation::kFloat32:
      return m.TruncateFloat32ToInt32(a);
    case MachineRepresentation::kFloat64:
      return m.RoundFloat64ToInt32(a);
    default:
      UNREACHABLE();
  }
}

void TestReturnMultipleValues(MachineType type) {
  const int kMaxCount = 20;
  for (int count = 0; count < kMaxCount; ++count) {
    printf("\n==== type = %s, count = %d ====\n\n\n",
           MachineReprToString(type.representation()), count);
    v8::internal::AccountingAllocator allocator;
    Zone zone(&allocator, ZONE_NAME);
    CallDescriptor* desc = CreateMonoCallDescriptor(&zone, count, 2, type);
    HandleAndZoneScope handles;
    RawMachineAssembler m(handles.main_isolate(),
                          new (handles.main_zone()) Graph(handles.main_zone()),
                          desc, MachineType::PointerRepresentation(),
                          InstructionSelector::SupportedMachineOperatorFlags());

    Node* p0 = m.Parameter(0);
    Node* p1 = m.Parameter(1);
    typedef Node* Node_ptr;
    std::unique_ptr<Node_ptr[]> returns(new Node_ptr[count]);
    for (int i = 0; i < count; ++i) {
      if (i % 3 == 0) returns[i] = Add(m, type, p0, p1);
      if (i % 3 == 1) returns[i] = Sub(m, type, p0, p1);
      if (i % 3 == 2) returns[i] = Mul(m, type, p0, p1);
    }
    m.Return(count, returns.get());

    CompilationInfo info(ArrayVector("testing"), handles.main_zone(),
                         Code::STUB);
    Handle<Code> code = Pipeline::GenerateCodeForTesting(
        &info, handles.main_isolate(), desc, m.graph(), m.Export());
#ifdef ENABLE_DISASSEMBLER
    if (FLAG_print_code) {
      OFStream os(stdout);
      code->Disassemble("multi_value", os);
    }
#endif

    const int a = 47, b = 12;
    int expect = 0;
    for (int i = 0, sign = +1; i < count; ++i) {
      if (i % 3 == 0) expect += sign * (a + b);
      if (i % 3 == 1) expect += sign * (a - b);
      if (i % 3 == 2) expect += sign * (a * b);
      if (i % 4 == 0) sign = -sign;
    }

    RawMachineAssemblerTester<int32_t> mt;
    Node* na = Constant(mt, type, a);
    Node* nb = Constant(mt, type, b);
    Node* ret_multi =
        mt.AddNode(mt.common()->Call(desc), mt.HeapConstant(code), na, nb);
    Node* ret = Constant(mt, type, 0);
    bool sign = false;
    for (int i = 0; i < count; ++i) {
      Node* x = (count == 1)
                    ? ret_multi
                    : mt.AddNode(mt.common()->Projection(i), ret_multi);
      ret = sign ? Sub(mt, type, ret, x) : Add(mt, type, ret, x);
      if (i % 4 == 0) sign = !sign;
    }
    mt.Return(ToInt32(mt, type, ret));
#ifdef ENABLE_DISASSEMBLER
    Handle<Code> code2 = mt.GetCode();
    if (FLAG_print_code) {
      OFStream os(stdout);
      code2->Disassemble("multi_value_call", os);
    }
#endif
    CHECK_EQ(expect, mt.Call());
  }
}

#define TEST_MULTI(Type, type) \
  TEST(ReturnMultiple##Type) { TestReturnMultipleValues(type); }

TEST_MULTI(Int32, MachineType::Int32())
#if (!V8_TARGET_ARCH_32_BIT)
TEST_MULTI(Int64, MachineType::Int64())
#endif
TEST_MULTI(Float32, MachineType::Float32())
TEST_MULTI(Float64, MachineType::Float64())

#undef TEST_MULTI

TEST(ReturnMultipleRandom) {
  // TODO(titzer): Test without RNG?
  v8::base::RandomNumberGenerator* rng(CcTest::random_number_generator());

  const int kNumberOfRuns = 10;
  for (int run = 0; run < kNumberOfRuns; ++run) {
    printf("\n==== Run %d ====\n\n", run);

    v8::internal::AccountingAllocator allocator;
    Zone zone(&allocator, ZONE_NAME);

    // Create randomized descriptor.
    int param_count = rng->NextInt(20);
    int return_count = rng->NextInt(10);
    CallDescriptor* desc =
        CreateRandomCallDescriptor(&zone, return_count, param_count, rng);

    printf("[");
    for (size_t j = 0; j < desc->ParameterCount(); ++j) {
      printf(" %s",
             MachineReprToString(desc->GetParameterType(j).representation()));
    }
    printf(" ] -> [");
    for (size_t j = 0; j < desc->ReturnCount(); ++j) {
      printf(" %s",
             MachineReprToString(desc->GetReturnType(j).representation()));
    }
    printf(" ]\n\n");

    // Count parameters of each type.
    const size_t num_types =
        static_cast<size_t>(MachineRepresentation::kLastRepresentation) + 1;
    std::unique_ptr<int[]> counts(new int[num_types]);
    for (size_t i = 0; i < num_types; ++i) {
      counts[i] = 0;
    }
    for (size_t i = 0; i < desc->ParameterCount(); ++i) {
      ++counts[index(desc->GetParameterType(i))];
    }

    // Generate random inputs.
    std::unique_ptr<int[]> inputs(new int[desc->ParameterCount()]);
    std::unique_ptr<int[]> outputs(new int[desc->ReturnCount()]);
    for (size_t i = 0; i < desc->ParameterCount(); ++i) {
      inputs[i] = rng->NextInt(10000);
    }

    HandleAndZoneScope handles;
    RawMachineAssembler m(handles.main_isolate(),
                          new (handles.main_zone()) Graph(handles.main_zone()),
                          desc, MachineType::PointerRepresentation(),
                          InstructionSelector::SupportedMachineOperatorFlags());

    // Generate Callee, returning random picks of its parameters.
    typedef Node* Node_ptr;
    std::unique_ptr<Node_ptr[]> params(
        new Node_ptr[desc->ParameterCount() + 1]);
    std::unique_ptr<Node_ptr[]> returns(new Node_ptr[desc->ReturnCount()]);
    for (size_t i = 0; i < desc->ParameterCount(); ++i) {
      params[i] = m.Parameter(i);
    }
    for (size_t i = 0; i < desc->ReturnCount(); ++i) {
      MachineType type = desc->GetReturnType(i);
      // Find a random same-type parameter to return. Use a constant if none.
      if (counts[index(type)] == 0) {
        returns[i] = Constant(m, type, 42);
        outputs[i] = 42;
      } else {
        int n = rng->NextInt(counts[index(type)]);
        int k;
        for (k = 0;; ++k) {
          if (desc->GetParameterType(k) == desc->GetReturnType(i) && --n < 0) {
            break;
          }
        }
        returns[i] = params[k];
        outputs[i] = inputs[k];
      }
    }
    m.Return(static_cast<int>(desc->ReturnCount()), returns.get());

    CompilationInfo info(ArrayVector("testing"), handles.main_zone(),
                         Code::STUB);
    Handle<Code> code = Pipeline::GenerateCodeForTesting(
        &info, handles.main_isolate(), desc, m.graph(), m.Export());
#ifdef ENABLE_DISASSEMBLER
    if (FLAG_print_code) {
      OFStream os(stdout);
      code->Disassemble("multi_value", os);
    }
#endif

    // Generate caller.
    int expect = 0;
    RawMachineAssemblerTester<int32_t> mt;
    params[0] = mt.HeapConstant(code);
    for (size_t i = 0; i < desc->ParameterCount(); ++i) {
      params[i + 1] = Constant(mt, desc->GetParameterType(i), inputs[i]);
    }
    Node* ret_multi =
        mt.AddNode(mt.common()->Call(desc),
                   static_cast<int>(desc->ParameterCount() + 1), params.get());
    Node* ret = Constant(mt, MachineType::Int32(), 0);
    for (size_t i = 0; i < desc->ReturnCount(); ++i) {
      if (rng->NextInt(3) == 0) continue;  // Skip random outputs.
      Node* x = (desc->ReturnCount() == 1)
                    ? ret_multi
                    : mt.AddNode(mt.common()->Projection(i), ret_multi);
      ret = mt.Int32Add(ret, ToInt32(mt, desc->GetReturnType(i), x));
      expect += outputs[i];
    }
    mt.Return(ret);
#ifdef ENABLE_DISASSEMBLER
    Handle<Code> code2 = mt.GetCode();
    if (FLAG_print_code) {
      OFStream os(stdout);
      code2->Disassemble("multi_value_call", os);
    }
#endif
    CHECK_EQ(expect, mt.Call());
  }
}

void ReturnLastValue(MachineType type) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  // Let 2 returns be on the stack.
  const int return_count = num_registers(type) + 2;

  CallDescriptor* desc = CreateMonoCallDescriptor(&zone, return_count, 0, type);

  HandleAndZoneScope handles;
  RawMachineAssembler m(handles.main_isolate(),
                        new (handles.main_zone()) Graph(handles.main_zone()),
                        desc, MachineType::PointerRepresentation(),
                        InstructionSelector::SupportedMachineOperatorFlags());

  std::unique_ptr<Node* []> returns(new Node*[return_count]);

  for (int i = 0; i < return_count; ++i) {
    returns[i] = Constant(m, type, i);
  }

  m.Return(return_count, returns.get());

  CompilationInfo info(ArrayVector("testing"), handles.main_zone(), Code::STUB);
  Handle<Code> code = Pipeline::GenerateCodeForTesting(
      &info, handles.main_isolate(), desc, m.graph(), m.Export());

  // Generate caller.
  int expect = return_count - 1;
  RawMachineAssemblerTester<int32_t> mt;
  Node* code_node = mt.HeapConstant(code);

  Node* call = mt.AddNode(mt.common()->Call(desc), 1, &code_node);

  mt.Return(ToInt32(
      mt, type, mt.AddNode(mt.common()->Projection(return_count - 1), call)));

  CHECK_EQ(expect, mt.Call());
}

TEST(ReturnLastValueInt32) { ReturnLastValue(MachineType::Int32()); }
#if (!V8_TARGET_ARCH_32_BIT)
TEST(ReturnLastValueInt64) { ReturnLastValue(MachineType::Int64()); }
#endif
TEST(ReturnLastValueFloat32) { ReturnLastValue(MachineType::Float32()); }
TEST(ReturnLastValueFloat64) { ReturnLastValue(MachineType::Float64()); }

}  // namespace compiler
}  // namespace internal
}  // namespace v8
