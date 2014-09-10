// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_MACHINE_OPERATOR_H_
#define V8_COMPILER_MACHINE_OPERATOR_H_

#include "src/compiler/machine-type.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

// TODO(turbofan): other write barriers are possible based on type
enum WriteBarrierKind { kNoWriteBarrier, kFullWriteBarrier };


// A Store needs a MachineType and a WriteBarrierKind
// in order to emit the correct write barrier.
struct StoreRepresentation {
  MachineType machine_type;
  WriteBarrierKind write_barrier_kind;
};


// TODO(bmeurer): Phi will probably also need this in the future.
template <>
struct StaticParameterTraits<MachineType> {
  static OStream& PrintTo(OStream& os, MachineType type) {  // NOLINT
    return os << type;
  }
  static int HashCode(MachineType type) { return type; }
  static bool Equals(MachineType lhs, MachineType rhs) { return lhs == rhs; }
};


// Interface for building machine-level operators. These operators are
// machine-level but machine-independent and thus define a language suitable
// for generating code to run on architectures such as ia32, x64, arm, etc.
class MachineOperatorBuilder {
 public:
  explicit MachineOperatorBuilder(Zone* zone, MachineType word = kMachPtr)
      : zone_(zone), word_(word) {
    CHECK(word == kRepWord32 || word == kRepWord64);
  }

#define SIMPLE(name, properties, inputs, outputs) \
  return new (zone_)                              \
      SimpleOperator(IrOpcode::k##name, properties, inputs, outputs, #name);

#define OP1(name, ptype, pname, properties, inputs, outputs)               \
  return new (zone_)                                                       \
      Operator1<ptype>(IrOpcode::k##name, properties | Operator::kNoThrow, \
                       inputs, outputs, #name, pname)

#define BINOP(name) SIMPLE(name, Operator::kPure, 2, 1)
#define BINOP_O(name) SIMPLE(name, Operator::kPure, 2, 2)
#define BINOP_C(name) \
  SIMPLE(name, Operator::kCommutative | Operator::kPure, 2, 1)
#define BINOP_AC(name)                                                         \
  SIMPLE(name,                                                                 \
         Operator::kAssociative | Operator::kCommutative | Operator::kPure, 2, \
         1)
#define BINOP_ACO(name)                                                        \
  SIMPLE(name,                                                                 \
         Operator::kAssociative | Operator::kCommutative | Operator::kPure, 2, \
         2)
#define UNOP(name) SIMPLE(name, Operator::kPure, 1, 1)

#define WORD_SIZE(x) return is64() ? Word64##x() : Word32##x()
#define INT_SIZE(x) return is64() ? Int64##x() : Int32##x()

  const Operator* Load(MachineType rep) {  // load [base + index]
    OP1(Load, MachineType, rep, Operator::kNoWrite, 2, 1);
  }
  // store [base + index], value
  const Operator* Store(MachineType rep, WriteBarrierKind kind) {
    StoreRepresentation store_rep = {rep, kind};
    OP1(Store, StoreRepresentation, store_rep, Operator::kNoRead, 3, 0);
  }

  const Operator* WordAnd() { WORD_SIZE(And); }
  const Operator* WordOr() { WORD_SIZE(Or); }
  const Operator* WordXor() { WORD_SIZE(Xor); }
  const Operator* WordShl() { WORD_SIZE(Shl); }
  const Operator* WordShr() { WORD_SIZE(Shr); }
  const Operator* WordSar() { WORD_SIZE(Sar); }
  const Operator* WordRor() { WORD_SIZE(Ror); }
  const Operator* WordEqual() { WORD_SIZE(Equal); }

  const Operator* Word32And() { BINOP_AC(Word32And); }
  const Operator* Word32Or() { BINOP_AC(Word32Or); }
  const Operator* Word32Xor() { BINOP_AC(Word32Xor); }
  const Operator* Word32Shl() { BINOP(Word32Shl); }
  const Operator* Word32Shr() { BINOP(Word32Shr); }
  const Operator* Word32Sar() { BINOP(Word32Sar); }
  const Operator* Word32Ror() { BINOP(Word32Ror); }
  const Operator* Word32Equal() { BINOP_C(Word32Equal); }

  const Operator* Word64And() { BINOP_AC(Word64And); }
  const Operator* Word64Or() { BINOP_AC(Word64Or); }
  const Operator* Word64Xor() { BINOP_AC(Word64Xor); }
  const Operator* Word64Shl() { BINOP(Word64Shl); }
  const Operator* Word64Shr() { BINOP(Word64Shr); }
  const Operator* Word64Sar() { BINOP(Word64Sar); }
  const Operator* Word64Ror() { BINOP(Word64Ror); }
  const Operator* Word64Equal() { BINOP_C(Word64Equal); }

  const Operator* Int32Add() { BINOP_AC(Int32Add); }
  const Operator* Int32AddWithOverflow() { BINOP_ACO(Int32AddWithOverflow); }
  const Operator* Int32Sub() { BINOP(Int32Sub); }
  const Operator* Int32SubWithOverflow() { BINOP_O(Int32SubWithOverflow); }
  const Operator* Int32Mul() { BINOP_AC(Int32Mul); }
  const Operator* Int32Div() { BINOP(Int32Div); }
  const Operator* Int32UDiv() { BINOP(Int32UDiv); }
  const Operator* Int32Mod() { BINOP(Int32Mod); }
  const Operator* Int32UMod() { BINOP(Int32UMod); }
  const Operator* Int32LessThan() { BINOP(Int32LessThan); }
  const Operator* Int32LessThanOrEqual() { BINOP(Int32LessThanOrEqual); }
  const Operator* Uint32LessThan() { BINOP(Uint32LessThan); }
  const Operator* Uint32LessThanOrEqual() { BINOP(Uint32LessThanOrEqual); }

  const Operator* Int64Add() { BINOP_AC(Int64Add); }
  const Operator* Int64Sub() { BINOP(Int64Sub); }
  const Operator* Int64Mul() { BINOP_AC(Int64Mul); }
  const Operator* Int64Div() { BINOP(Int64Div); }
  const Operator* Int64UDiv() { BINOP(Int64UDiv); }
  const Operator* Int64Mod() { BINOP(Int64Mod); }
  const Operator* Int64UMod() { BINOP(Int64UMod); }
  const Operator* Int64LessThan() { BINOP(Int64LessThan); }
  const Operator* Int64LessThanOrEqual() { BINOP(Int64LessThanOrEqual); }

  // Signed comparison of word-sized integer values, translates to int32/int64
  // comparisons depending on the word-size of the machine.
  const Operator* IntLessThan() { INT_SIZE(LessThan); }
  const Operator* IntLessThanOrEqual() { INT_SIZE(LessThanOrEqual); }

  // Convert representation of integers between float64 and int32/uint32.
  // The precise rounding mode and handling of out of range inputs are *not*
  // defined for these operators, since they are intended only for use with
  // integers.
  const Operator* ChangeInt32ToFloat64() { UNOP(ChangeInt32ToFloat64); }
  const Operator* ChangeUint32ToFloat64() { UNOP(ChangeUint32ToFloat64); }
  const Operator* ChangeFloat64ToInt32() { UNOP(ChangeFloat64ToInt32); }
  const Operator* ChangeFloat64ToUint32() { UNOP(ChangeFloat64ToUint32); }

  // Sign/zero extend int32/uint32 to int64/uint64.
  const Operator* ChangeInt32ToInt64() { UNOP(ChangeInt32ToInt64); }
  const Operator* ChangeUint32ToUint64() { UNOP(ChangeUint32ToUint64); }

  // Truncate double to int32 using JavaScript semantics.
  const Operator* TruncateFloat64ToInt32() { UNOP(TruncateFloat64ToInt32); }

  // Truncate the high order bits and convert the remaining bits to int32.
  const Operator* TruncateInt64ToInt32() { UNOP(TruncateInt64ToInt32); }

  // Floating point operators always operate with IEEE 754 round-to-nearest.
  const Operator* Float64Add() { BINOP_C(Float64Add); }
  const Operator* Float64Sub() { BINOP(Float64Sub); }
  const Operator* Float64Mul() { BINOP_C(Float64Mul); }
  const Operator* Float64Div() { BINOP(Float64Div); }
  const Operator* Float64Mod() { BINOP(Float64Mod); }

  // Floating point comparisons complying to IEEE 754.
  const Operator* Float64Equal() { BINOP_C(Float64Equal); }
  const Operator* Float64LessThan() { BINOP(Float64LessThan); }
  const Operator* Float64LessThanOrEqual() { BINOP(Float64LessThanOrEqual); }

  inline bool is32() const { return word_ == kRepWord32; }
  inline bool is64() const { return word_ == kRepWord64; }
  inline MachineType word() const { return word_; }

#undef WORD_SIZE
#undef INT_SIZE
#undef UNOP
#undef BINOP
#undef OP1
#undef SIMPLE

 private:
  Zone* zone_;
  MachineType word_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_MACHINE_OPERATOR_H_
