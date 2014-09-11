// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_SIMPLIFIED_OPERATOR_H_
#define V8_COMPILER_SIMPLIFIED_OPERATOR_H_

#include "src/compiler/machine-operator.h"
#include "src/compiler/opcodes.h"
#include "src/zone.h"

namespace v8 {
namespace internal {

// Forward declarations.
template <class>
class TypeImpl;
struct ZoneTypeConfig;
typedef TypeImpl<ZoneTypeConfig> Type;


namespace compiler {

enum BaseTaggedness { kUntaggedBase, kTaggedBase };

// An access descriptor for loads/stores of fixed structures like field
// accesses of heap objects. Accesses from either tagged or untagged base
// pointers are supported; untagging is done automatically during lowering.
struct FieldAccess {
  BaseTaggedness base_is_tagged;  // specifies if the base pointer is tagged.
  int offset;                     // offset of the field, without tag.
  Handle<Name> name;              // debugging only.
  Type* type;                     // type of the field.
  MachineType machine_type;       // machine type of the field.

  int tag() const { return base_is_tagged == kTaggedBase ? kHeapObjectTag : 0; }
};


// An access descriptor for loads/stores of indexed structures like characters
// in strings or off-heap backing stores. Accesses from either tagged or
// untagged base pointers are supported; untagging is done automatically during
// lowering.
struct ElementAccess {
  BaseTaggedness base_is_tagged;  // specifies if the base pointer is tagged.
  int header_size;                // size of the header, without tag.
  Type* type;                     // type of the element.
  MachineType machine_type;       // machine type of the element.

  int tag() const { return base_is_tagged == kTaggedBase ? kHeapObjectTag : 0; }
};


// If the accessed object is not a heap object, add this to the header_size.
static const int kNonHeapObjectHeaderSize = kHeapObjectTag;


// Specialization for static parameters of type {FieldAccess}.
template <>
struct StaticParameterTraits<FieldAccess> {
  static OStream& PrintTo(OStream& os, const FieldAccess& val) {  // NOLINT
    return os << val.offset;
  }
  static int HashCode(const FieldAccess& val) {
    return (val.offset < 16) | (val.machine_type & 0xffff);
  }
  static bool Equals(const FieldAccess& lhs, const FieldAccess& rhs);
};


// Specialization for static parameters of type {ElementAccess}.
template <>
struct StaticParameterTraits<ElementAccess> {
  static OStream& PrintTo(OStream& os, const ElementAccess& val) {  // NOLINT
    return os << val.header_size;
  }
  static int HashCode(const ElementAccess& val) {
    return (val.header_size < 16) | (val.machine_type & 0xffff);
  }
  static bool Equals(const ElementAccess& lhs, const ElementAccess& rhs);
};


inline const FieldAccess FieldAccessOf(const Operator* op) {
  DCHECK(op->opcode() == IrOpcode::kLoadField ||
         op->opcode() == IrOpcode::kStoreField);
  return OpParameter<FieldAccess>(op);
}


inline const ElementAccess ElementAccessOf(const Operator* op) {
  DCHECK(op->opcode() == IrOpcode::kLoadElement ||
         op->opcode() == IrOpcode::kStoreElement);
  return OpParameter<ElementAccess>(op);
}


// Interface for building simplified operators, which represent the
// medium-level operations of V8, including adding numbers, allocating objects,
// indexing into objects and arrays, etc.
// All operators are typed but many are representation independent.

// Number values from JS can be in one of these representations:
//   - Tagged: word-sized integer that is either
//     - a signed small integer (31 or 32 bits plus a tag)
//     - a tagged pointer to a HeapNumber object that has a float64 field
//   - Int32: an untagged signed 32-bit integer
//   - Uint32: an untagged unsigned 32-bit integer
//   - Float64: an untagged float64

// Additional representations for intermediate code or non-JS code:
//   - Int64: an untagged signed 64-bit integer
//   - Uint64: an untagged unsigned 64-bit integer
//   - Float32: an untagged float32

// Boolean values can be:
//   - Bool: a tagged pointer to either the canonical JS #false or
//           the canonical JS #true object
//   - Bit: an untagged integer 0 or 1, but word-sized
class SimplifiedOperatorBuilder {
 public:
  explicit inline SimplifiedOperatorBuilder(Zone* zone) : zone_(zone) {}

#define SIMPLE(name, properties, inputs, outputs) \
  return new (zone_)                              \
      SimpleOperator(IrOpcode::k##name, properties, inputs, outputs, #name);

#define OP1(name, ptype, pname, properties, inputs, outputs)               \
  return new (zone_)                                                       \
      Operator1<ptype>(IrOpcode::k##name, properties | Operator::kNoThrow, \
                       inputs, outputs, #name, pname)

#define UNOP(name) SIMPLE(name, Operator::kPure, 1, 1)
#define BINOP(name) SIMPLE(name, Operator::kPure, 2, 1)

  const Operator* BooleanNot() const { UNOP(BooleanNot); }

  const Operator* NumberEqual() const { BINOP(NumberEqual); }
  const Operator* NumberLessThan() const { BINOP(NumberLessThan); }
  const Operator* NumberLessThanOrEqual() const {
    BINOP(NumberLessThanOrEqual);
  }
  const Operator* NumberAdd() const { BINOP(NumberAdd); }
  const Operator* NumberSubtract() const { BINOP(NumberSubtract); }
  const Operator* NumberMultiply() const { BINOP(NumberMultiply); }
  const Operator* NumberDivide() const { BINOP(NumberDivide); }
  const Operator* NumberModulus() const { BINOP(NumberModulus); }
  const Operator* NumberToInt32() const { UNOP(NumberToInt32); }
  const Operator* NumberToUint32() const { UNOP(NumberToUint32); }

  const Operator* ReferenceEqual(Type* type) const { BINOP(ReferenceEqual); }

  const Operator* StringEqual() const { BINOP(StringEqual); }
  const Operator* StringLessThan() const { BINOP(StringLessThan); }
  const Operator* StringLessThanOrEqual() const {
    BINOP(StringLessThanOrEqual);
  }
  const Operator* StringAdd() const { BINOP(StringAdd); }

  const Operator* ChangeTaggedToInt32() const { UNOP(ChangeTaggedToInt32); }
  const Operator* ChangeTaggedToUint32() const { UNOP(ChangeTaggedToUint32); }
  const Operator* ChangeTaggedToFloat64() const { UNOP(ChangeTaggedToFloat64); }
  const Operator* ChangeInt32ToTagged() const { UNOP(ChangeInt32ToTagged); }
  const Operator* ChangeUint32ToTagged() const { UNOP(ChangeUint32ToTagged); }
  const Operator* ChangeFloat64ToTagged() const { UNOP(ChangeFloat64ToTagged); }
  const Operator* ChangeBoolToBit() const { UNOP(ChangeBoolToBit); }
  const Operator* ChangeBitToBool() const { UNOP(ChangeBitToBool); }

  const Operator* LoadField(const FieldAccess& access) const {
    OP1(LoadField, FieldAccess, access, Operator::kNoWrite, 1, 1);
  }
  const Operator* StoreField(const FieldAccess& access) const {
    OP1(StoreField, FieldAccess, access, Operator::kNoRead, 2, 0);
  }
  const Operator* LoadElement(const ElementAccess& access) const {
    OP1(LoadElement, ElementAccess, access, Operator::kNoWrite, 2, 1);
  }
  const Operator* StoreElement(const ElementAccess& access) const {
    OP1(StoreElement, ElementAccess, access, Operator::kNoRead, 3, 0);
  }

#undef BINOP
#undef UNOP
#undef OP1
#undef SIMPLE

 private:
  Zone* zone_;
};
}
}
}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_SIMPLIFIED_OPERATOR_H_
