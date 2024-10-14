// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_CANONICAL_TYPES_H_
#define V8_WASM_CANONICAL_TYPES_H_

#include <unordered_map>

#include "src/base/functional.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-module.h"

namespace v8::internal::wasm {

// We use ValueType instances constructed from canonical type indices, so we
// can't let them get bigger than what we have storage space for.
// TODO(jkummerow): Raise this limit. Possible options:
// - increase the size of ValueType::HeapTypeField, using currently-unused bits.
// - change the encoding of ValueType: one bit says whether it's a ref type,
//   the other bits then encode the index or the kind of non-ref type.
// - refactor the TypeCanonicalizer's internals to no longer use ValueTypes
//   and related infrastructure, and use a wider encoding of canonicalized
//   type indices only here.
// - wait for 32-bit platforms to no longer be relevant, and increase the
//   size of ValueType to 64 bits.
// None of this seems urgent, as we have no evidence of the current limit
// being an actual limitation in practice.
static constexpr size_t kMaxCanonicalTypes = kV8MaxWasmTypes;
// We don't want any valid modules to fail canonicalization.
static_assert(kMaxCanonicalTypes >= kV8MaxWasmTypes);
// We want the invalid index to fail any range checks.
static_assert(kInvalidCanonicalIndex > kMaxCanonicalTypes);
// Ensure that ValueType can hold all canonical type indexes.
static_assert(kMaxCanonicalTypes <= (1 << ValueType::kHeapTypeBits));

// A singleton class, responsible for isorecursive canonicalization of wasm
// types.
// A recursive group is a subsequence of types explicitly marked in the type
// section of a wasm module. Identical recursive groups have to be canonicalized
// to a single canonical group. Respective types in two identical groups are
// considered identical for all purposes.
// Two groups are considered identical if they have the same shape, and all
// type indices referenced in the same position in both groups reference:
// - identical types, if those do not belong to the rec. group,
// - types in the same relative position in the group, if those belong to the
//   rec. group.
class TypeCanonicalizer {
 public:
  static constexpr CanonicalTypeIndex kPredefinedArrayI8Index{0};
  static constexpr CanonicalTypeIndex kPredefinedArrayI16Index{1};
  static constexpr uint32_t kNumberOfPredefinedTypes = 2;

  TypeCanonicalizer();

  // Singleton class; no copying or moving allowed.
  TypeCanonicalizer(const TypeCanonicalizer& other) = delete;
  TypeCanonicalizer& operator=(const TypeCanonicalizer& other) = delete;
  TypeCanonicalizer(TypeCanonicalizer&& other) = delete;
  TypeCanonicalizer& operator=(TypeCanonicalizer&& other) = delete;

  // Registers {size} types of {module} as a recursive group, starting at
  // {start_index}, and possibly canonicalizes it if an identical one has been
  // found. Modifies {module->isorecursive_canonical_type_ids}.
  V8_EXPORT_PRIVATE void AddRecursiveGroup(WasmModule* module, uint32_t size,
                                           uint32_t start_index);

  // Same as above, except it registers the last {size} types in the module.
  V8_EXPORT_PRIVATE void AddRecursiveGroup(WasmModule* module, uint32_t size);

  // Same as above, but for a group of size 1 (using the last type in the
  // module).
  V8_EXPORT_PRIVATE void AddRecursiveSingletonGroup(WasmModule* module);

  // Same as above, but receives an explicit start index.
  V8_EXPORT_PRIVATE void AddRecursiveSingletonGroup(WasmModule* module,
                                                    uint32_t start_index);

  // Adds a module-independent signature as a recursive group, and canonicalizes
  // it if an identical is found. Returns the canonical index of the added
  // signature.
  V8_EXPORT_PRIVATE CanonicalTypeIndex
  AddRecursiveGroup(const FunctionSig* sig);

  // Retrieve back a function signature from a canonical index later.
  V8_EXPORT_PRIVATE const CanonicalSig* LookupFunctionSignature(
      CanonicalTypeIndex index) const;

  // Returns if {canonical_sub_index} is a canonical subtype of
  // {canonical_super_index}.
  V8_EXPORT_PRIVATE bool IsCanonicalSubtype(CanonicalTypeIndex sub_index,
                                            CanonicalTypeIndex super_index);

  // Returns if the type at {sub_index} in {sub_module} is a subtype of the
  // type at {super_index} in {super_module} after canonicalization.
  V8_EXPORT_PRIVATE bool IsCanonicalSubtype(uint32_t sub_index,
                                            uint32_t super_index,
                                            const WasmModule* sub_module,
                                            const WasmModule* super_module);

  // Deletes recursive groups. Used by fuzzers to avoid accumulating memory, and
  // used by specific tests e.g. for serialization / deserialization.
  V8_EXPORT_PRIVATE void EmptyStorageForTesting();

  size_t EstimateCurrentMemoryConsumption() const;

  size_t GetCurrentNumberOfTypes() const;

  // Prepares wasm for the provided canonical type index. This reserves enough
  // space in the canonical rtts and the JSToWasm wrappers on the isolate roots.
  V8_EXPORT_PRIVATE static void PrepareForCanonicalTypeId(Isolate* isolate,
                                                          int id);
  // Reset the canonical rtts and JSToWasm wrappers on the isolate roots for
  // testing purposes (in production cases canonical type ids are never freed).
  V8_EXPORT_PRIVATE static void ClearWasmCanonicalTypesForTesting(
      Isolate* isolate);

  bool IsFunctionSignature(CanonicalTypeIndex index) const;

#if DEBUG
  // Check whether a function signature is canonicalized by checking whether the
  // pointer points into this class's storage.
  V8_EXPORT_PRIVATE bool Contains(const FunctionSig* sig) const;
  // TODO(366180605): We probably won't need this, because static typing
  // provides more reliable guarantees than DCHECKs.
  V8_EXPORT_PRIVATE bool Contains(const CanonicalSig* sig) const;
#endif

 private:
  struct CanonicalType {
    enum Kind : int8_t { kFunction, kStruct, kArray };

    union {
      const CanonicalSig* function_sig = nullptr;
      const CanonicalStructType* struct_type;
      const CanonicalArrayType* array_type;
    };
    CanonicalTypeIndex supertype{kNoSuperType};
    Kind kind = kFunction;
    bool is_final = false;
    bool is_shared = false;
    uint8_t subtyping_depth = 0;
    bool is_relative_supertype;

    constexpr CanonicalType(const CanonicalSig* sig,
                            CanonicalTypeIndex supertype, bool is_final,
                            bool is_shared, bool is_relative_supertype)
        : function_sig(sig),
          supertype(supertype),
          kind(kFunction),
          is_final(is_final),
          is_shared(is_shared),
          is_relative_supertype(is_relative_supertype) {}

    constexpr CanonicalType(const CanonicalStructType* type,
                            CanonicalTypeIndex supertype, bool is_final,
                            bool is_shared, bool is_relative_supertype)
        : struct_type(type),
          supertype(supertype),
          kind(kStruct),
          is_final(is_final),
          is_shared(is_shared),
          is_relative_supertype(is_relative_supertype) {}

    constexpr CanonicalType(const CanonicalArrayType* type,
                            CanonicalTypeIndex supertype, bool is_final,
                            bool is_shared, bool is_relative_supertype)
        : array_type(type),
          supertype(supertype),
          kind(kArray),
          is_final(is_final),
          is_shared(is_shared),
          is_relative_supertype(is_relative_supertype) {}

    constexpr CanonicalType() = default;

    bool operator==(const CanonicalType& other) const {
      if (supertype != other.supertype) return false;
      if (kind != other.kind) return false;
      if (is_final != other.is_final) return false;
      if (is_shared != other.is_shared) return false;
      if (is_relative_supertype != other.is_relative_supertype) return false;
      if (kind == kFunction) return *function_sig == *other.function_sig;
      if (kind == kStruct) return *struct_type == *other.struct_type;
      DCHECK_EQ(kArray, kind);
      return *array_type == *other.array_type;
    }

    bool operator!=(const CanonicalType& other) const {
      return !operator==(other);
    }

    size_t hash_value() const {
      uint32_t metadata = (supertype.index << 2) | (is_final ? 2 : 0) |
                          (is_relative_supertype ? 1 : 0);
      base::Hasher hasher;
      hasher.Add(metadata);
      if (kind == kFunction) {
        hasher.Add(*function_sig);
      } else if (kind == kStruct) {
        hasher.Add(*struct_type);
      } else {
        DCHECK_EQ(kArray, kind);
        hasher.Add(*array_type);
      }
      return hasher.hash();
    }
  };
  struct CanonicalGroup {
    CanonicalGroup(Zone* zone, size_t size)
        : types(zone->AllocateVector<CanonicalType>(size)) {}

    bool operator==(const CanonicalGroup& other) const {
      return types == other.types;
    }

    bool operator!=(const CanonicalGroup& other) const {
      return types != other.types;
    }

    size_t hash_value() const {
      return base::Hasher{}.AddRange(types.begin(), types.end()).hash();
    }

    // The storage of this vector is the TypeCanonicalizer's zone_.
    base::Vector<CanonicalType> types;
  };

  struct CanonicalSingletonGroup {
    struct hash {
      size_t operator()(const CanonicalSingletonGroup& group) const {
        return group.hash_value();
      }
    };

    bool operator==(const CanonicalSingletonGroup& other) const {
      return type == other.type;
    }

    size_t hash_value() const { return type.hash_value(); }

    CanonicalType type;
  };

  void AddPredefinedArrayTypes();

  CanonicalTypeIndex FindCanonicalGroup(const CanonicalGroup&) const;
  CanonicalTypeIndex FindCanonicalGroup(const CanonicalSingletonGroup&) const;

  // Canonicalize all types present in {type} (including supertype) according to
  // {CanonicalizeValueType}.
  CanonicalType CanonicalizeTypeDef(const WasmModule* module,
                                    TypeDefinition type,
                                    uint32_t recursive_group_start);

  // An indexed type gets mapped to a {CanonicalValueType::WithRelativeIndex}
  // if its index points inside the new canonical group; otherwise, the index
  // gets mapped to its canonical representative.
  CanonicalValueType CanonicalizeValueType(
      const WasmModule* module, ValueType type,
      uint32_t recursive_group_start) const;

  CanonicalTypeIndex AddRecursiveGroup(CanonicalType type);

  void CheckMaxCanonicalIndex() const;

  std::vector<CanonicalTypeIndex> canonical_supertypes_;
  // Maps groups of size >=2 to the canonical id of the first type.
  std::unordered_map<CanonicalGroup, CanonicalTypeIndex,
                     base::hash<CanonicalGroup>>
      canonical_groups_;
  // Maps group of size 1 to the canonical id of the type.
  std::unordered_map<CanonicalSingletonGroup, CanonicalTypeIndex,
                     base::hash<CanonicalSingletonGroup>>
      canonical_singleton_groups_;
  // Maps canonical indices back to the function signature.
  std::unordered_map<CanonicalTypeIndex, const CanonicalSig*,
                     base::hash<CanonicalTypeIndex>>
      canonical_function_sigs_;
  AccountingAllocator allocator_;
  Zone zone_{&allocator_, "canonical type zone"};
  mutable base::Mutex mutex_;
};

// Returns a reference to the TypeCanonicalizer shared by the entire process.
V8_EXPORT_PRIVATE TypeCanonicalizer* GetTypeCanonicalizer();

}  // namespace v8::internal::wasm

#endif  // V8_WASM_CANONICAL_TYPES_H_
