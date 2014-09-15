// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_MACHINE_OPERATOR_H_
#define V8_COMPILER_MACHINE_OPERATOR_H_

#include "src/compiler/machine-type.h"

namespace v8 {
namespace internal {
namespace compiler {

// Forward declarations.
struct MachineOperatorBuilderImpl;
class Operator;


// Supported write barrier modes.
enum WriteBarrierKind { kNoWriteBarrier, kFullWriteBarrier };

OStream& operator<<(OStream& os, const WriteBarrierKind& write_barrier_kind);


typedef MachineType LoadRepresentation;


// A Store needs a MachineType and a WriteBarrierKind
// in order to emit the correct write barrier.
class StoreRepresentation FINAL {
 public:
  StoreRepresentation(MachineType machine_type,
                      WriteBarrierKind write_barrier_kind)
      : machine_type_(machine_type), write_barrier_kind_(write_barrier_kind) {}

  MachineType machine_type() const { return machine_type_; }
  WriteBarrierKind write_barrier_kind() const { return write_barrier_kind_; }

 private:
  MachineType machine_type_;
  WriteBarrierKind write_barrier_kind_;
};

inline bool operator==(const StoreRepresentation& rep1,
                       const StoreRepresentation& rep2) {
  return rep1.machine_type() == rep2.machine_type() &&
         rep1.write_barrier_kind() == rep2.write_barrier_kind();
}

inline bool operator!=(const StoreRepresentation& rep1,
                       const StoreRepresentation& rep2) {
  return !(rep1 == rep2);
}

OStream& operator<<(OStream& os, const StoreRepresentation& rep);


// Interface for building machine-level operators. These operators are
// machine-level but machine-independent and thus define a language suitable
// for generating code to run on architectures such as ia32, x64, arm, etc.
class MachineOperatorBuilder FINAL {
 public:
  explicit MachineOperatorBuilder(MachineType word = kMachPtr);

  const Operator* Word32And() const WARN_UNUSED_RESULT;
  const Operator* Word32Or() const WARN_UNUSED_RESULT;
  const Operator* Word32Xor() const WARN_UNUSED_RESULT;
  const Operator* Word32Shl() const WARN_UNUSED_RESULT;
  const Operator* Word32Shr() const WARN_UNUSED_RESULT;
  const Operator* Word32Sar() const WARN_UNUSED_RESULT;
  const Operator* Word32Ror() const WARN_UNUSED_RESULT;
  const Operator* Word32Equal() const WARN_UNUSED_RESULT;

  const Operator* Word64And() const WARN_UNUSED_RESULT;
  const Operator* Word64Or() const WARN_UNUSED_RESULT;
  const Operator* Word64Xor() const WARN_UNUSED_RESULT;
  const Operator* Word64Shl() const WARN_UNUSED_RESULT;
  const Operator* Word64Shr() const WARN_UNUSED_RESULT;
  const Operator* Word64Sar() const WARN_UNUSED_RESULT;
  const Operator* Word64Ror() const WARN_UNUSED_RESULT;
  const Operator* Word64Equal() const WARN_UNUSED_RESULT;

  const Operator* Int32Add() const WARN_UNUSED_RESULT;
  const Operator* Int32AddWithOverflow() const WARN_UNUSED_RESULT;
  const Operator* Int32Sub() const WARN_UNUSED_RESULT;
  const Operator* Int32SubWithOverflow() const WARN_UNUSED_RESULT;
  const Operator* Int32Mul() const WARN_UNUSED_RESULT;
  const Operator* Int32Div() const WARN_UNUSED_RESULT;
  const Operator* Int32UDiv() const WARN_UNUSED_RESULT;
  const Operator* Int32Mod() const WARN_UNUSED_RESULT;
  const Operator* Int32UMod() const WARN_UNUSED_RESULT;
  const Operator* Int32LessThan() const WARN_UNUSED_RESULT;
  const Operator* Int32LessThanOrEqual() const WARN_UNUSED_RESULT;
  const Operator* Uint32LessThan() const WARN_UNUSED_RESULT;
  const Operator* Uint32LessThanOrEqual() const WARN_UNUSED_RESULT;

  const Operator* Int64Add() const WARN_UNUSED_RESULT;
  const Operator* Int64Sub() const WARN_UNUSED_RESULT;
  const Operator* Int64Mul() const WARN_UNUSED_RESULT;
  const Operator* Int64Div() const WARN_UNUSED_RESULT;
  const Operator* Int64UDiv() const WARN_UNUSED_RESULT;
  const Operator* Int64Mod() const WARN_UNUSED_RESULT;
  const Operator* Int64UMod() const WARN_UNUSED_RESULT;
  const Operator* Int64LessThan() const WARN_UNUSED_RESULT;
  const Operator* Int64LessThanOrEqual() const WARN_UNUSED_RESULT;

  // Convert representation of integers between float64 and int32/uint32.
  // The precise rounding mode and handling of out of range inputs are *not*
  // defined for these operators, since they are intended only for use with
  // integers.
  const Operator* ChangeInt32ToFloat64() const WARN_UNUSED_RESULT;
  const Operator* ChangeUint32ToFloat64() const WARN_UNUSED_RESULT;
  const Operator* ChangeFloat64ToInt32() const WARN_UNUSED_RESULT;
  const Operator* ChangeFloat64ToUint32() const WARN_UNUSED_RESULT;

  // Sign/zero extend int32/uint32 to int64/uint64.
  const Operator* ChangeInt32ToInt64() const WARN_UNUSED_RESULT;
  const Operator* ChangeUint32ToUint64() const WARN_UNUSED_RESULT;

  // Truncate double to int32 using JavaScript semantics.
  const Operator* TruncateFloat64ToInt32() const WARN_UNUSED_RESULT;

  // Truncate the high order bits and convert the remaining bits to int32.
  const Operator* TruncateInt64ToInt32() const WARN_UNUSED_RESULT;

  // Floating point operators always operate with IEEE 754 round-to-nearest.
  const Operator* Float64Add() const WARN_UNUSED_RESULT;
  const Operator* Float64Sub() const WARN_UNUSED_RESULT;
  const Operator* Float64Mul() const WARN_UNUSED_RESULT;
  const Operator* Float64Div() const WARN_UNUSED_RESULT;
  const Operator* Float64Mod() const WARN_UNUSED_RESULT;

  // Floating point comparisons complying to IEEE 754.
  const Operator* Float64Equal() const WARN_UNUSED_RESULT;
  const Operator* Float64LessThan() const WARN_UNUSED_RESULT;
  const Operator* Float64LessThanOrEqual() const WARN_UNUSED_RESULT;

  // load [base + index]
  const Operator* Load(LoadRepresentation rep) const WARN_UNUSED_RESULT;

  // store [base + index], value
  const Operator* Store(StoreRepresentation rep) const WARN_UNUSED_RESULT;

  // Target machine word-size assumed by this builder.
  bool Is32() const { return word() == kRepWord32; }
  bool Is64() const { return word() == kRepWord64; }
  MachineType word() const { return word_; }

// Pseudo operators that translate to 32/64-bit operators depending on the
// word-size of the target machine assumed by this builder.
#define PSEUDO_OP_LIST(V) \
  V(Word, And)            \
  V(Word, Or)             \
  V(Word, Xor)            \
  V(Word, Shl)            \
  V(Word, Shr)            \
  V(Word, Sar)            \
  V(Word, Ror)            \
  V(Word, Equal)          \
  V(Int, Add)             \
  V(Int, Sub)             \
  V(Int, Mul)             \
  V(Int, Div)             \
  V(Int, UDiv)            \
  V(Int, Mod)             \
  V(Int, UMod)            \
  V(Int, LessThan)        \
  V(Int, LessThanOrEqual)
#define PSEUDO_OP(Prefix, Suffix)                                \
  const Operator* Prefix##Suffix() const {                       \
    return Is32() ? Prefix##32##Suffix() : Prefix##64##Suffix(); \
  }
  PSEUDO_OP_LIST(PSEUDO_OP)
#undef PSEUDO_OP
#undef PSEUDO_OP_LIST

 private:
  const MachineOperatorBuilderImpl& impl_;
  const MachineType word_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_MACHINE_OPERATOR_H_
