// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_CANONICAL_TYPES_H_
#define V8_WASM_CANONICAL_TYPES_H_

#include <unordered_map>

#include "src/base/bounds.h"
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
  V8_EXPORT_PRIVATE bool IsCanonicalSubtype(ModuleTypeIndex sub_index,
                                            ModuleTypeIndex super_index,
                                            const WasmModule* sub_module,
                                            const WasmModule* super_module);

  // Deletes recursive groups. Used by fuzzers to avoid accumulating memory, and
  // used by specific tests e.g. for serialization / deserialization.
  V8_EXPORT_PRIVATE void EmptyStorageForTesting();

  size_t EstimateCurrentMemoryConsumption() const;

  size_t GetCurrentNumberOfTypes() const;

  // Prepares wasm for the provided canonical type index. This reserves enough
  // space in the canonical rtts and the JSToWasm wrappers on the isolate roots.
  V8_EXPORT_PRIVATE static void PrepareForCanonicalTypeId(
      Isolate* isolate, CanonicalTypeIndex id);
  // Reset the canonical rtts and JSToWasm wrappers on the isolate roots for
  // testing purposes (in production cases canonical type ids are never freed).
  V8_EXPORT_PRIVATE static void ClearWasmCanonicalTypesForTesting(
      Isolate* isolate);

  bool IsFunctionSignature(CanonicalTypeIndex index) const;

  CanonicalTypeIndex FindIndex_Slow(const CanonicalSig* sig) const;

#if DEBUG
  // Check whether a supposedly-canonicalized function signature does indeed
  // live in this class's storage. Useful for guarding casts of signatures
  // that are entering the typed world.
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

    constexpr CanonicalType(const CanonicalSig* sig,
                            CanonicalTypeIndex supertype, bool is_final,
                            bool is_shared)
        : function_sig(sig),
          supertype(supertype),
          kind(kFunction),
          is_final(is_final),
          is_shared(is_shared) {}

    constexpr CanonicalType(const CanonicalStructType* type,
                            CanonicalTypeIndex supertype, bool is_final,
                            bool is_shared)
        : struct_type(type),
          supertype(supertype),
          kind(kStruct),
          is_final(is_final),
          is_shared(is_shared) {}

    constexpr CanonicalType(const CanonicalArrayType* type,
                            CanonicalTypeIndex supertype, bool is_final,
                            bool is_shared)
        : array_type(type),
          supertype(supertype),
          kind(kArray),
          is_final(is_final),
          is_shared(is_shared) {}

    constexpr CanonicalType() = default;
  };

  // Define the range of a recursion group; for use in {CanonicalHashing} and
  // {CanonicalEquality}.
  struct RecursionGroupRange {
    const CanonicalTypeIndex start;
    const CanonicalTypeIndex end;

    bool Contains(CanonicalTypeIndex index) const {
      return base::IsInRange(index.index, start.index, end.index);
    }

    CanonicalTypeIndex RelativeIndex(CanonicalTypeIndex index) const {
      return Contains(index)
                 // Make the value_type relative within the recursion group.
                 ? CanonicalTypeIndex{index.index - start.index}
                 : index;
    }

    CanonicalValueType RelativeType(CanonicalValueType type) const {
      return type.has_index()
                 ? CanonicalValueType::FromIndex(
                       type.kind(), RelativeIndex(type.ref_index()))
                 : type;
    }
  };

  // Support for hashing of recursion groups, where type indexes have to be
  // hashed relative to the recursion group.
  struct CanonicalHashing {
    base::Hasher hasher;
    const RecursionGroupRange recgroup;

    explicit CanonicalHashing(RecursionGroupRange recgroup)
        : recgroup{recgroup} {}

    void Add(CanonicalType type) {
      CanonicalTypeIndex relative_supertype =
          recgroup.RelativeIndex(type.supertype);
      uint32_t metadata =
          (relative_supertype.index << 1) | (type.is_final ? 1 : 0);
      hasher.Add(metadata);
      switch (type.kind) {
        case CanonicalType::kFunction:
          Add(*type.function_sig);
          break;
        case CanonicalType::kStruct:
          Add(*type.struct_type);
          break;
        case CanonicalType::kArray:
          Add(*type.array_type);
          break;
      }
    }

    void Add(CanonicalValueType value_type) {
      hasher.Add(recgroup.RelativeType(value_type));
    }

    void Add(const CanonicalSig& sig) {
      hasher.Add(sig.parameter_count());
      for (CanonicalValueType type : sig.all()) Add(type);
    }

    void Add(const CanonicalStructType& struct_type) {
      hasher.AddRange(struct_type.mutabilities());
      for (const ValueTypeBase& field : struct_type.fields()) {
        Add(CanonicalValueType{field});
      }
    }

    void Add(const CanonicalArrayType& array_type) {
      hasher.Add(array_type.mutability());
      Add(array_type.element_type());
    }

    size_t hash() const { return hasher.hash(); }
  };

  // Support for equality checking of recursion groups, where type indexes have
  // to be compared relative to their respective recursion group.
  struct CanonicalEquality {
    // Recursion group bounds for LHS and RHS.
    const RecursionGroupRange recgroup1;
    const RecursionGroupRange recgroup2;

    CanonicalEquality(RecursionGroupRange recgroup1,
                      RecursionGroupRange recgroup2)
        : recgroup1{recgroup1}, recgroup2{recgroup2} {}

    bool EqualType(const CanonicalType& type1,
                   const CanonicalType& type2) const {
      if (recgroup1.RelativeIndex(type1.supertype) !=
          recgroup2.RelativeIndex(type2.supertype)) {
        return false;
      }
      if (type1.is_final != type2.is_final) return false;
      if (type1.is_shared != type2.is_shared) return false;
      switch (type1.kind) {
        case CanonicalType::kFunction:
          return type2.kind == CanonicalType::kFunction &&
                 EqualSig(*type1.function_sig, *type2.function_sig);
        case CanonicalType::kStruct:
          return type2.kind == CanonicalType::kStruct &&
                 EqualStructType(*type1.struct_type, *type2.struct_type);
        case CanonicalType::kArray:
          return type2.kind == CanonicalType::kArray &&
                 EqualArrayType(*type1.array_type, *type2.array_type);
      }
    }

    bool EqualTypes(base::Vector<const CanonicalType> types1,
                    base::Vector<const CanonicalType> types2) const {
      return std::equal(types1.begin(), types1.end(), types2.begin(),
                        types2.end(),
                        std::bind_front(&CanonicalEquality::EqualType, this));
    }

    bool EqualValueType(CanonicalValueType type1,
                        CanonicalValueType type2) const {
      return recgroup1.RelativeType(type1) == recgroup2.RelativeType(type2);
    }

    bool EqualSig(const CanonicalSig& sig1, const CanonicalSig& sig2) const {
      if (sig1.parameter_count() != sig2.parameter_count()) return false;
      return std::equal(
          sig1.all().begin(), sig1.all().end(), sig2.all().begin(),
          sig2.all().end(),
          std::bind_front(&CanonicalEquality::EqualValueType, this));
    }

    bool EqualStructType(const CanonicalStructType& type1,
                         const CanonicalStructType& type2) const {
      return std::equal(
          type1.fields().begin(), type1.fields().end(), type2.fields().begin(),
          type2.fields().end(),
          std::bind_front(&CanonicalEquality::EqualValueType, this));
    }

    bool EqualArrayType(const CanonicalArrayType& type1,
                        const CanonicalArrayType& type2) const {
      return type1.mutability() == type2.mutability() &&
             EqualValueType(type1.element_type(), type2.element_type());
    }
  };

  struct CanonicalGroup {
    CanonicalGroup(Zone* zone, size_t size, CanonicalTypeIndex start)
        : types(zone->AllocateVector<CanonicalType>(size)), start(start) {
      // size >= 2; otherwise a `CanonicalSingletonGroup` should have been used.
      DCHECK_LE(2, size);
    }

    bool operator==(const CanonicalGroup& other) const {
      CanonicalTypeIndex end{start.index +
                             static_cast<uint32_t>(types.size() - 1)};
      CanonicalTypeIndex other_end{
          other.start.index + static_cast<uint32_t>(other.types.size() - 1)};
      CanonicalEquality equality{{start, end}, {other.start, other_end}};
      return equality.EqualTypes(types, other.types);
    }

    size_t hash_value() const {
      CanonicalTypeIndex end{start.index + static_cast<uint32_t>(types.size()) -
                             1};
      CanonicalHashing hasher{{start, end}};
      for (CanonicalType t : types) {
        hasher.Add(t);
      }
      return hasher.hash();
    }

    // The storage of this vector is the TypeCanonicalizer's zone_.
    const base::Vector<CanonicalType> types;
    const CanonicalTypeIndex start;
  };

  struct CanonicalSingletonGroup {
    bool operator==(const CanonicalSingletonGroup& other) const {
      CanonicalEquality equality{{index, index}, {other.index, other.index}};
      return equality.EqualType(type, other.type);
    }

    size_t hash_value() const {
      CanonicalHashing hasher{{index, index}};
      hasher.Add(type);
      return hasher.hash();
    }

    CanonicalType type;
    CanonicalTypeIndex index;
  };

  void AddPredefinedArrayTypes();

  CanonicalTypeIndex FindCanonicalGroup(const CanonicalGroup&) const;
  CanonicalTypeIndex FindCanonicalGroup(const CanonicalSingletonGroup&) const;

  // Canonicalize the module-specific type at `module_type_idx` within the
  // recursion group starting at `recursion_group_start`, using
  // `canonical_recgroup_start` as the start offset of types within the
  // recursion group.
  CanonicalType CanonicalizeTypeDef(
      const WasmModule* module, ModuleTypeIndex module_type_idx,
      ModuleTypeIndex recgroup_start,
      CanonicalTypeIndex canonical_recgroup_start);

  CanonicalTypeIndex AddRecursiveGroup(CanonicalType type);

  void CheckMaxCanonicalIndex() const;

  std::vector<CanonicalTypeIndex> canonical_supertypes_;
  // Set of all known canonical recgroups of size >=2.
  std::unordered_set<CanonicalGroup, base::hash<CanonicalGroup>>
      canonical_groups_;
  // Set of all known canonical recgroups of size 1.
  std::unordered_set<CanonicalSingletonGroup,
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
