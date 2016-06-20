// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/wasm/asm-types.h"

namespace v8 {
namespace internal {
namespace wasm {

AsmCallableType* AsmType::AsCallableType() {
  if (AsValueType() != nullptr) {
    return nullptr;
  }

  DCHECK(this->AsFunctionType() != nullptr ||
         this->AsOverloadedFunctionType() != nullptr ||
         this->AsFFIType() != nullptr ||
         this->AsFunctionTableType() != nullptr);
  return reinterpret_cast<AsmCallableType*>(this);
}

std::string AsmType::Name() {
  AsmValueType* avt = this->AsValueType();
  if (avt != nullptr) {
    switch (avt->Bitset()) {
#define RETURN_TYPE_NAME(CamelName, string_name, number, parent_types) \
  case AsmValueType::kAsm##CamelName:                                  \
    return string_name;
      FOR_EACH_ASM_VALUE_TYPE_LIST(RETURN_TYPE_NAME)
#undef RETURN_TYPE_NAME
      default:
        UNREACHABLE();
    }
  }

  return this->AsCallableType()->Name();
}

bool AsmType::IsExactly(AsmType* that) {
  // TODO(jpp): maybe this can become this == that.
  AsmValueType* avt = this->AsValueType();
  if (avt != nullptr) {
    AsmValueType* tavt = that->AsValueType();
    if (tavt == nullptr) {
      return false;
    }
    return avt->Bitset() == tavt->Bitset();
  }

  // TODO(jpp): is it useful to allow non-value types to be tested with
  // IsExactly?
  return that == this;
}

bool AsmType::IsA(AsmType* that) {
  // IsA is used for querying inheritance relationships. Therefore it is only
  // meaningful for basic types.
  AsmValueType* tavt = that->AsValueType();
  if (tavt != nullptr) {
    AsmValueType* avt = this->AsValueType();
    if (avt == nullptr) {
      return false;
    }
    return (avt->Bitset() & tavt->Bitset()) == tavt->Bitset();
  }

  // TODO(jpp): is it useful to allow non-value types to be tested with IsA?
  return that == this;
}

int32_t AsmType::ElementSizeInBytes() {
  auto* value = AsValueType();
  if (value == nullptr) {
    return AsmType::kNotHeapType;
  }
  switch (value->Bitset()) {
    case AsmValueType::kAsmInt8Array:
    case AsmValueType::kAsmUint8Array:
      return 1;
    case AsmValueType::kAsmInt16Array:
    case AsmValueType::kAsmUint16Array:
      return 2;
    case AsmValueType::kAsmInt32Array:
    case AsmValueType::kAsmUint32Array:
    case AsmValueType::kAsmFloat32Array:
      return 4;
    case AsmValueType::kAsmFloat64Array:
      return 8;
    default:
      return AsmType::kNotHeapType;
  }
}

AsmType* AsmType::LoadType() {
  auto* value = AsValueType();
  if (value == nullptr) {
    return AsmType::None();
  }
  switch (value->Bitset()) {
    case AsmValueType::kAsmInt8Array:
    case AsmValueType::kAsmUint8Array:
    case AsmValueType::kAsmInt16Array:
    case AsmValueType::kAsmUint16Array:
    case AsmValueType::kAsmInt32Array:
    case AsmValueType::kAsmUint32Array:
      return AsmType::Intish();
    case AsmValueType::kAsmFloat32Array:
      return AsmType::FloatQ();
    case AsmValueType::kAsmFloat64Array:
      return AsmType::DoubleQ();
    default:
      return AsmType::None();
  }
}

AsmType* AsmType::StoreType() {
  auto* value = AsValueType();
  if (value == nullptr) {
    return AsmType::None();
  }
  switch (value->Bitset()) {
    case AsmValueType::kAsmInt8Array:
    case AsmValueType::kAsmUint8Array:
    case AsmValueType::kAsmInt16Array:
    case AsmValueType::kAsmUint16Array:
    case AsmValueType::kAsmInt32Array:
    case AsmValueType::kAsmUint32Array:
      return AsmType::Intish();
    case AsmValueType::kAsmFloat32Array:
      return AsmType::FloatishDoubleQ();
    case AsmValueType::kAsmFloat64Array:
      return AsmType::FloatQDoubleQ();
    default:
      return AsmType::None();
  }
}

std::string AsmFunctionType::Name() {
  if (IsFroundType()) {
    return "fround";
  }

  std::string ret;
  ret += "(";
  for (size_t ii = 0; ii < args_.size(); ++ii) {
    ret += args_[ii]->Name();
    if (ii != args_.size() - 1) {
      ret += ", ";
    }
  }
  if (IsMinMaxType()) {
    DCHECK_EQ(args_.size(), 2);
    ret += "...";
  }
  ret += ") -> ";
  ret += return_type_->Name();
  return ret;
}

namespace {
class AsmFroundType final : public AsmFunctionType {
 public:
  bool IsFroundType() const override { return true; }

 private:
  friend AsmType;

  explicit AsmFroundType(Zone* zone)
      : AsmFunctionType(zone, AsmType::Float()) {}

  AsmType* ValidateCall(AsmType* return_type,
                        const ZoneVector<AsmType*>& args) override;
};
}  // namespace

AsmType* AsmType::FroundType(Zone* zone) {
  auto* Fround = new (zone) AsmFroundType(zone);
  return reinterpret_cast<AsmType*>(Fround);
}

AsmType* AsmFroundType::ValidateCall(AsmType* return_type,
                                     const ZoneVector<AsmType*>& args) {
  if (args.size() != 1) {
    return AsmType::None();
  }

  auto* arg = args[0];
  if (!arg->IsA(AsmType::Floatish()) && !arg->IsA(AsmType::DoubleQ()) &&
      !arg->IsA(AsmType::Signed()) && !arg->IsA(AsmType::Unsigned())) {
    return AsmType::None();
  }

  return AsmType::Float();
}

namespace {
class AsmMinMaxType final : public AsmFunctionType {
 public:
  bool IsMinMaxType() const override { return true; }

 private:
  friend AsmType;

  AsmMinMaxType(Zone* zone, AsmType* dest, AsmType* src)
      : AsmFunctionType(zone, dest) {
    AddArgument(src);
    AddArgument(src);
  }

  AsmType* ValidateCall(AsmType* return_type,
                        const ZoneVector<AsmType*>& args) override {
    if (!ReturnType()->IsExactly(return_type)) {
      return AsmType::None();
    }

    if (args.size() < 2) {
      return AsmType::None();
    }

    for (size_t ii = 0; ii < Arguments().size(); ++ii) {
      if (!Arguments()[0]->IsExactly(args[ii])) {
        return AsmType::None();
      }
    }

    return ReturnType();
  }
};
}  // namespace

AsmType* AsmType::MinMaxType(Zone* zone, AsmType* dest, AsmType* src) {
  DCHECK(dest->AsValueType() != nullptr);
  DCHECK(src->AsValueType() != nullptr);
  auto* MinMax = new (zone) AsmMinMaxType(zone, dest, src);
  return reinterpret_cast<AsmType*>(MinMax);
}

AsmType* AsmFFIType::ValidateCall(AsmType* return_type,
                                  const ZoneVector<AsmType*>& args) {
  for (size_t ii = 0; ii < args.size(); ++ii) {
    if (!args[ii]->IsA(AsmType::Extern())) {
      return AsmType::None();
    }
  }

  return return_type;
}

AsmType* AsmFunctionType::ValidateCall(AsmType* return_type,
                                       const ZoneVector<AsmType*>& args) {
  if (!return_type_->IsExactly(return_type)) {
    return AsmType::None();
  }

  if (args_.size() != args.size()) {
    return AsmType::None();
  }

  for (size_t ii = 0; ii < args_.size(); ++ii) {
    if (!args_[ii]->IsExactly(args[ii])) {
      return AsmType::None();
    }
  }

  return return_type_;
}

std::string AsmOverloadedFunctionType::Name() {
  std::string ret;

  for (size_t ii = 0; ii < overloads_.size(); ++ii) {
    if (ii != 0) {
      ret += " /\\ ";
    }
    ret += overloads_[ii]->Name();
  }

  return ret;
}

AsmType* AsmOverloadedFunctionType::ValidateCall(
    AsmType* return_type, const ZoneVector<AsmType*>& args) {
  for (size_t ii = 0; ii < overloads_.size(); ++ii) {
    auto* validated_type =
        overloads_[ii]->AsCallableType()->ValidateCall(return_type, args);
    if (validated_type != AsmType::None()) {
      return validated_type;
    }
  }

  return AsmType::None();
}

void AsmOverloadedFunctionType::AddOverload(AsmType* overload) {
  DCHECK(overload->AsFunctionType() != nullptr);
  overloads_.push_back(overload);
}

AsmFunctionTableType::AsmFunctionTableType(size_t length, AsmType* signature)
    : length_(length), signature_(signature) {
  DCHECK(signature_ != nullptr);
  DCHECK(signature_->AsFunctionType() != nullptr);
}

std::string AsmFunctionTableType::Name() {
  return signature_->Name() + "[" + std::to_string(length_) + "]";
}

AsmType* AsmFunctionTableType::ValidateCall(AsmType* return_type,
                                            const ZoneVector<AsmType*>& args) {
  return signature_->AsCallableType()->ValidateCall(return_type, args);
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
