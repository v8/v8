// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/simd-lowering.h"

namespace v8 {
namespace internal {
namespace compiler {

static ConversionSignature* SigCreateInt32x4;
static ConversionSignature* SigCreateFloat32x4;
static ConversionSignature* SigCreateInt16x8;
static ConversionSignature* SigCreateInt8x16;
static ConversionSignature* SigExtractLaneInt;
static ConversionSignature* SigExtractLaneFloat;
static ConversionSignature* SigDefault;

SimdLowering::~SimdLowering() {}

void SimdLowering::InitializeSignatures() {
  // Initialize signatures for runtime calls
  const int kReturnCount = 1;
  const int kBinop = 2;
  const int kSimd32x4 = 4;
  const int kSimd16x8 = 8;
  const int kSimd8x16 = 16;

  ConversionSignature::Builder CreateInt32x4Builder(zone_, kReturnCount,
                                                    kSimd32x4);
  ConversionSignature::Builder CreateFloat32x4Builder(zone_, kReturnCount,
                                                      kSimd32x4);
  ConversionSignature::Builder CreateInt16x8Builder(zone_, kReturnCount,
                                                    kSimd16x8);
  ConversionSignature::Builder CreateInt8x16Builder(zone_, kReturnCount,
                                                    kSimd8x16);
  ConversionSignature::Builder ExtractLaneIntBuilder(zone_, kReturnCount,
                                                     kBinop);
  ConversionSignature::Builder ExtractLaneFloatBuilder(zone_, kReturnCount,
                                                       kBinop);
  ConversionSignature::Builder DefaultBuilder(zone_, kReturnCount, kSimd8x16);

  // Initialize Signatures for create functions
  for (int i = 0; i < kSimd32x4; i++) {
    CreateInt32x4Builder.AddParam(Conversion::kInt32);
    CreateFloat32x4Builder.AddParam(Conversion::kFloat32);
  }
  CreateInt32x4Builder.AddReturn(Conversion::kOpaque);
  SigCreateInt32x4 = CreateInt32x4Builder.Build();

  CreateFloat32x4Builder.AddReturn(Conversion::kOpaque);
  SigCreateFloat32x4 = CreateFloat32x4Builder.Build();

  for (int i = 0; i < kSimd16x8; i++) {
    CreateInt16x8Builder.AddParam(Conversion::kInt32);
  }
  CreateInt16x8Builder.AddReturn(Conversion::kOpaque);
  SigCreateInt16x8 = CreateInt16x8Builder.Build();

  for (int i = 0; i < kSimd8x16; i++) {
    CreateInt8x16Builder.AddParam(Conversion::kInt32);
  }
  CreateInt8x16Builder.AddReturn(Conversion::kOpaque);
  SigCreateInt8x16 = CreateInt8x16Builder.Build();

  // Initialize signatures for ExtractLane functions
  ExtractLaneIntBuilder.AddParam(Conversion::kOpaque);
  ExtractLaneIntBuilder.AddParam(Conversion::kInt32);
  ExtractLaneIntBuilder.AddReturn(Conversion::kInt32);
  SigExtractLaneInt = ExtractLaneIntBuilder.Build();

  ExtractLaneFloatBuilder.AddParam(Conversion::kOpaque);
  ExtractLaneFloatBuilder.AddParam(Conversion::kFloat32);
  ExtractLaneFloatBuilder.AddReturn(Conversion::kFloat32);
  SigExtractLaneFloat = ExtractLaneFloatBuilder.Build();

  // Initialize default signature.
  for (int i = 0; i < kSimd8x16; i++) {
    DefaultBuilder.AddParam(Conversion::kNone);
  }
  DefaultBuilder.AddReturn(Conversion::kNone);
  SigDefault = DefaultBuilder.Build();
}

Reduction SimdLowering::Reduce(Node* node) {
  // For now lower everything to runtime calls.
  switch (node->opcode()) {
    case IrOpcode::kCreateInt32x4: {
      return Replace(builder_->ChangeToRuntimeCall(
          node, Runtime::kCreateInt32x4, SigCreateInt32x4));
    }
    case IrOpcode::kCreateInt16x8: {
      return Replace(builder_->ChangeToRuntimeCall(
          node, Runtime::kCreateInt16x8, SigCreateInt16x8));
    }
    case IrOpcode::kCreateInt8x16: {
      return Replace(builder_->ChangeToRuntimeCall(
          node, Runtime::kCreateInt8x16, SigCreateInt8x16));
    }
    case IrOpcode::kCreateFloat32x4: {
      return Replace(builder_->ChangeToRuntimeCall(
          node, Runtime::kCreateFloat32x4, SigCreateFloat32x4));
    }
    case IrOpcode::kInt8x16ExtractLane:
    case IrOpcode::kInt16x8ExtractLane:
    case IrOpcode::kInt32x4ExtractLane: {
      return Replace(builder_->ChangeToRuntimeCall(
          node, Runtime::kInt32x4ExtractLane, SigExtractLaneInt));
    }
    case IrOpcode::kFloat32x4ExtractLane: {
      return Replace(builder_->ChangeToRuntimeCall(
          node, Runtime::kFloat32x4ExtractLane, SigExtractLaneFloat));
    }
    default: { break; }
  }

  // TODO(gdeepti): Implement and test.
  // Assume the others are all just simd in and out.
  switch (node->opcode()) {
#define F(Opcode)                                                             \
  case IrOpcode::k##Opcode: {                                                 \
    return Replace(                                                           \
        builder_->ChangeToRuntimeCall(node, Runtime::k##Opcode, SigDefault)); \
  }
    MACHINE_SIMD_RETURN_SIMD_OP_LIST(F)
    MACHINE_SIMD_RETURN_BOOL_OP_LIST(F)
#undef F
    default: { return NoChange(); }
  }
  UNREACHABLE();
  return NoChange();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
