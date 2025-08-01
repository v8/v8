// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_WASM_INIT_EXPR_H_
#define V8_WASM_WASM_INIT_EXPR_H_

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#include <memory>

#include "src/wasm/value-type.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace wasm {

struct WasmModule;

// Representation of an constant expression. Unlike {ConstantExpression}, this
// does not use {WireBytesRef}, i.e., it does not depend on a wasm module's
// bytecode representation.
class WasmInitExpr : public ZoneObject {
 public:
  enum Operator {
    kGlobalGet,
    kI32Const,
    kI64Const,
    kF32Const,
    kF64Const,
    kS128Const,
    kI32Add,
    kI32Sub,
    kI32Mul,
    kI64Add,
    kI64Sub,
    kI64Mul,
    kRefNullConst,
    kRefFuncConst,
    kStructNew,
    kStructNewDefault,
    kArrayNew,
    kArrayNewDefault,
    kArrayNewFixed,
    kRefI31,
    kStringConst,
    kAnyConvertExtern,
    kExternConvertAny
  };

  union Immediate {
    int32_t i32_const;
    int64_t i64_const;
    float f32_const;
    double f64_const;
    std::array<uint8_t, kSimd128Size> s128_const;
    uint32_t index;
    uint32_t heap_type_;  // Read with {heap_type()}.
  };

  explicit WasmInitExpr(int32_t v) : kind_(kI32Const), operands_(nullptr) {
    immediate_.i32_const = v;
  }
  explicit WasmInitExpr(int64_t v) : kind_(kI64Const), operands_(nullptr) {
    immediate_.i64_const = v;
  }
  explicit WasmInitExpr(float v) : kind_(kF32Const), operands_(nullptr) {
    immediate_.f32_const = v;
  }
  explicit WasmInitExpr(double v) : kind_(kF64Const), operands_(nullptr) {
    immediate_.f64_const = v;
  }
  explicit WasmInitExpr(uint8_t v[kSimd128Size])
      : kind_(kS128Const), operands_(nullptr) {
    memcpy(immediate_.s128_const.data(), v, kSimd128Size);
  }

  HeapType heap_type() const {
    return HeapType::FromBits(immediate_.heap_type_);
  }

  static WasmInitExpr Binop(Zone* zone, Operator op, WasmInitExpr lhs,
                            WasmInitExpr rhs) {
    DCHECK(op == kI32Add || op == kI32Sub || op == kI32Mul || op == kI64Add ||
           op == kI64Sub || op == kI64Mul);
    return WasmInitExpr(zone, op, {lhs, rhs});
  }

  static WasmInitExpr GlobalGet(uint32_t index) {
    WasmInitExpr expr(kGlobalGet);
    expr.immediate_.index = index;
    return expr;
  }

  static WasmInitExpr RefFuncConst(uint32_t index) {
    WasmInitExpr expr(kRefFuncConst);
    expr.immediate_.index = index;
    return expr;
  }

  static WasmInitExpr RefNullConst(HeapType heap_type) {
    WasmInitExpr expr(kRefNullConst);
    expr.immediate_.heap_type_ = heap_type.raw_bit_field();
    return expr;
  }

  static WasmInitExpr StructNew(ModuleTypeIndex index,
                                ZoneVector<WasmInitExpr>* elements) {
    WasmInitExpr expr(kStructNew, elements);
    expr.immediate_.index = index.index;
    return expr;
  }

  static WasmInitExpr StructNewDefault(
      ModuleTypeIndex index,
      ZoneVector<WasmInitExpr>* opt_descriptor = nullptr) {
    WasmInitExpr expr(kStructNewDefault, opt_descriptor);
    expr.immediate_.index = index.index;
    return expr;
  }

  static WasmInitExpr ArrayNew(Zone* zone, ModuleTypeIndex index,
                               WasmInitExpr initial, WasmInitExpr length) {
    WasmInitExpr expr(zone, kArrayNew, {initial, length});
    expr.immediate_.index = index.index;
    return expr;
  }

  static WasmInitExpr ArrayNewDefault(Zone* zone, ModuleTypeIndex index,
                                      WasmInitExpr length) {
    WasmInitExpr expr(zone, kArrayNewDefault, {length});
    expr.immediate_.index = index.index;
    return expr;
  }

  static WasmInitExpr ArrayNewFixed(ModuleTypeIndex index,
                                    ZoneVector<WasmInitExpr>* elements) {
    WasmInitExpr expr(kArrayNewFixed, elements);
    expr.immediate_.index = index.index;
    return expr;
  }

  static WasmInitExpr RefI31(Zone* zone, WasmInitExpr value) {
    WasmInitExpr expr(zone, kRefI31, {value});
    return expr;
  }

  static WasmInitExpr StringConst(uint32_t index) {
    WasmInitExpr expr(kStringConst);
    expr.immediate_.index = index;
    return expr;
  }

  static WasmInitExpr AnyConvertExtern(Zone* zone, WasmInitExpr arg) {
    return WasmInitExpr(zone, kAnyConvertExtern, {arg});
  }

  static WasmInitExpr ExternConvertAny(Zone* zone, WasmInitExpr arg) {
    return WasmInitExpr(zone, kExternConvertAny, {arg});
  }

  Immediate immediate() const { return immediate_; }
  Operator kind() const { return kind_; }
  const ZoneVector<WasmInitExpr>* operands() const { return operands_; }

  static WasmInitExpr DefaultValue(ValueType type) {
    // No initializer, emit a default value.
    switch (type.kind()) {
      case kI8:
      case kI16:
      case kI32:
        return WasmInitExpr(int32_t{0});
      case kI64:
        return WasmInitExpr(int64_t{0});
      case kF16:
      case kF32:
        return WasmInitExpr(0.0f);
      case kF64:
        return WasmInitExpr(0.0);
      case kRefNull:
        return WasmInitExpr::RefNullConst(type.heap_type());
      case kS128: {
        uint8_t value[kSimd128Size] = {0};
        return WasmInitExpr(value);
      }
      case kVoid:
      case kTop:
      case kBottom:
      case kRef:
        UNREACHABLE();
    }
  }

 private:
  WasmInitExpr(Operator kind, const ZoneVector<WasmInitExpr>* operands)
      : kind_(kind), operands_(operands) {}
  explicit WasmInitExpr(Operator kind) : kind_(kind), operands_(nullptr) {}
  WasmInitExpr(Zone* zone, Operator kind,
               std::initializer_list<WasmInitExpr> operands)
      : kind_(kind),
        operands_(zone->New<ZoneVector<WasmInitExpr>>(operands, zone)) {}
  Immediate immediate_;
  Operator kind_;
  const ZoneVector<WasmInitExpr>* operands_;
};

ASSERT_TRIVIALLY_COPYABLE(WasmInitExpr);

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_INIT_EXPR_H_
