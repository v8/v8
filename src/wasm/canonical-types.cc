// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/canonical-types.h"

#include "src/execution/isolate.h"
#include "src/handles/handles-inl.h"
#include "src/heap/heap-inl.h"
#include "src/init/v8.h"
#include "src/roots/roots-inl.h"
#include "src/utils/utils.h"
#include "src/wasm/std-object-sizes.h"
#include "src/wasm/wasm-engine.h"

namespace v8::internal::wasm {

TypeCanonicalizer* GetTypeCanonicalizer() {
  return GetWasmEngine()->type_canonicalizer();
}

TypeCanonicalizer::TypeCanonicalizer() { AddPredefinedArrayTypes(); }

void TypeCanonicalizer::CheckMaxCanonicalIndex() const {
  if (V8_UNLIKELY(canonical_supertypes_.size() > kMaxCanonicalTypes)) {
    V8::FatalProcessOutOfMemory(nullptr, "too many canonicalized types");
  }
}

void TypeCanonicalizer::AddRecursiveGroup(WasmModule* module, uint32_t size) {
  AddRecursiveGroup(module, size,
                    static_cast<uint32_t>(module->types.size() - size));
}

void TypeCanonicalizer::AddRecursiveGroup(WasmModule* module, uint32_t size,
                                          uint32_t start_index) {
  if (size == 0) return;
  // If the caller knows statically that {size == 1}, it should have called
  // {AddRecursiveSingletonGroup} directly. For cases where this is not
  // statically determined we add this dispatch here.
  if (size == 1) return AddRecursiveSingletonGroup(module, start_index);

  // Multiple threads could try to register recursive groups concurrently.
  // TODO(manoskouk): Investigate if we can fine-grain the synchronization.
  base::MutexGuard mutex_guard(&mutex_);
  DCHECK_GE(module->types.size(), start_index + size);
  CanonicalGroup group{&zone_, size};
  for (uint32_t i = 0; i < size; i++) {
    group.types[i] = CanonicalizeTypeDef(module, module->types[start_index + i],
                                         start_index);
  }
  if (CanonicalTypeIndex canonical_index = FindCanonicalGroup(group);
      canonical_index.valid()) {
    // Identical group found. Map new types to the old types's canonical
    // representatives.
    for (uint32_t i = 0; i < size; i++) {
      module->isorecursive_canonical_type_ids[start_index + i] =
          CanonicalTypeIndex{canonical_index.index + i};
    }
    // TODO(clemensb): Avoid leaking the zone storage allocated for {group}
    // (both for the {Vector} in {CanonicalGroup}, but also the storage
    // allocated in {CanonicalizeTypeDef{).
    return;
  }
  // Identical group not found. Add new canonical representatives for the new
  // types.
  uint32_t first_canonical_index =
      static_cast<uint32_t>(canonical_supertypes_.size());
  canonical_supertypes_.resize(first_canonical_index + size);
  CheckMaxCanonicalIndex();
  for (uint32_t i = 0; i < size; i++) {
    CanonicalType& canonical_type = group.types[i];
    // Compute the canonical index of the supertype: If it is relative, we
    // need to add {first_canonical_index}.
    canonical_supertypes_[first_canonical_index + i] = CanonicalTypeIndex{
        canonical_type.is_relative_supertype
            ? canonical_type.type_def.supertype + first_canonical_index
            : canonical_type.type_def.supertype};
    CanonicalTypeIndex canonical_id{first_canonical_index + i};
    module->isorecursive_canonical_type_ids[start_index + i] = canonical_id;
    if (canonical_type.type_def.kind == TypeDefinition::kFunction) {
      const FunctionSig* sig = canonical_type.type_def.function_sig;
      DCHECK(zone_.Contains(sig));
      CHECK(canonical_function_sigs_.emplace(canonical_id, sig).second);
    }
  }
  // Check that this canonical ID is not used yet.
  DCHECK(std::none_of(
      canonical_singleton_groups_.begin(), canonical_singleton_groups_.end(),
      [=](auto& entry) { return entry.second == first_canonical_index; }));
  DCHECK(std::none_of(
      canonical_groups_.begin(), canonical_groups_.end(),
      [=](auto& entry) { return entry.second == first_canonical_index; }));
  canonical_groups_.emplace(group, CanonicalTypeIndex{first_canonical_index});
}

void TypeCanonicalizer::AddRecursiveSingletonGroup(WasmModule* module) {
  uint32_t start_index = static_cast<uint32_t>(module->types.size() - 1);
  return AddRecursiveSingletonGroup(module, start_index);
}

void TypeCanonicalizer::AddRecursiveSingletonGroup(WasmModule* module,
                                                   uint32_t start_index) {
  base::MutexGuard guard(&mutex_);
  DCHECK_GT(module->types.size(), start_index);
  CanonicalTypeIndex canonical_index = AddRecursiveGroup(
      CanonicalizeTypeDef(module, module->types[start_index], start_index));
  module->isorecursive_canonical_type_ids[start_index] = canonical_index;
}

CanonicalTypeIndex TypeCanonicalizer::AddRecursiveGroup(
    const FunctionSig* sig) {
// Types in the signature must be module-independent.
#if DEBUG
  for (ValueType type : sig->all()) DCHECK(!type.has_index());
#endif
  const bool kFinal = true;
  const bool kNotShared = false;
  // Because of the checks above, we can treat the type_def as canonical.
  CanonicalType canonical{
      .type_def = TypeDefinition{sig, kNoSuperType, kFinal, kNotShared},
      .is_relative_supertype = false};
  base::MutexGuard guard(&mutex_);
  // Fast path lookup before canonicalizing (== copying into the
  // TypeCanonicalizer's zone) the function signature.
  CanonicalTypeIndex canonical_index =
      FindCanonicalGroup(CanonicalSingletonGroup{canonical});
  if (canonical_index.valid()) return canonical_index;
  // Copy into this class's zone, then call the generic {AddRecursiveGroup}.
  FunctionSig::Builder builder(&zone_, sig->return_count(),
                               sig->parameter_count());
  for (ValueType ret : sig->returns()) builder.AddReturn(ret);
  for (ValueType param : sig->parameters()) builder.AddParam(param);
  canonical.type_def.function_sig = builder.Get();
  return AddRecursiveGroup(canonical);
}

CanonicalTypeIndex TypeCanonicalizer::AddRecursiveGroup(CanonicalType type) {
  DCHECK(!mutex_.TryLock());  // The caller must hold the mutex.
  CanonicalSingletonGroup group{type};
  if (CanonicalTypeIndex canonical_index = FindCanonicalGroup(group);
      canonical_index.valid()) {
    //  Make sure this signature can be looked up later.
    DCHECK_IMPLIES(type.type_def.kind == TypeDefinition::kFunction,
                   canonical_function_sigs_.count(canonical_index));
    return canonical_index;
  }
  static_assert(kMaxCanonicalTypes <= kMaxUInt32);
  CanonicalTypeIndex canonical_index{
      static_cast<uint32_t>(canonical_supertypes_.size())};
  // Check that this canonical ID is not used yet.
  DCHECK(std::none_of(
      canonical_singleton_groups_.begin(), canonical_singleton_groups_.end(),
      [=](auto& entry) { return entry.second == canonical_index; }));
  DCHECK(std::none_of(
      canonical_groups_.begin(), canonical_groups_.end(),
      [=](auto& entry) { return entry.second == canonical_index; }));
  canonical_singleton_groups_.emplace(group, canonical_index);
  // Compute the canonical index of the supertype: If it is relative, we
  // need to add {canonical_index}.
  canonical_supertypes_.push_back(CanonicalTypeIndex{
      type.is_relative_supertype ? type.type_def.supertype + canonical_index
                                 : type.type_def.supertype});
  if (type.type_def.kind == TypeDefinition::kFunction) {
    const FunctionSig* sig = type.type_def.function_sig;
    DCHECK(zone_.Contains(sig));
    CHECK(canonical_function_sigs_.emplace(canonical_index, sig).second);
  }
  CheckMaxCanonicalIndex();
  return canonical_index;
}

const FunctionSig* TypeCanonicalizer::LookupFunctionSignature(
    uint32_t canonical_index) const {
  base::MutexGuard mutex_guard(&mutex_);
  auto it = canonical_function_sigs_.find(canonical_index);
  CHECK(it != canonical_function_sigs_.end());
  return it->second;
}

void TypeCanonicalizer::AddPredefinedArrayTypes() {
  static constexpr std::pair<CanonicalTypeIndex, ValueType>
      kPredefinedArrayTypes[] = {{kPredefinedArrayI8Index, kWasmI8},
                                 {kPredefinedArrayI16Index, kWasmI16}};
  for (auto [index, element_type] : kPredefinedArrayTypes) {
    DCHECK_EQ(index, canonical_singleton_groups_.size());
    CanonicalSingletonGroup group;
    static constexpr bool kMutable = true;
    // TODO(jkummerow): Decide whether this should be final or nonfinal.
    static constexpr bool kFinal = true;
    static constexpr bool kShared = false;  // TODO(14616): Fix this.
    ArrayType* type = zone_.New<ArrayType>(element_type, kMutable);
    group.type.type_def = TypeDefinition(type, kNoSuperType, kFinal, kShared);
    group.type.is_relative_supertype = false;
    canonical_singleton_groups_.emplace(group, index);
    canonical_supertypes_.emplace_back(CanonicalTypeIndex{kNoSuperType});
    DCHECK_LE(canonical_supertypes_.size(), kMaxCanonicalTypes);
  }
}

CanonicalValueType TypeCanonicalizer::CanonicalizeValueType(
    const WasmModule* module, ValueType type,
    uint32_t recursive_group_start) const {
  if (!type.has_index()) return CanonicalValueType{type};
  static_assert(kMaxCanonicalTypes <= (1u << ValueType::kHeapTypeBits));
  return type.ref_index() >= recursive_group_start
             ? CanonicalValueType::WithRelativeIndex(
                   type.kind(), type.ref_index() - recursive_group_start)
             : CanonicalValueType::FromIndex(
                   type.kind(),
                   module->isorecursive_canonical_type_ids[type.ref_index()]);
}

bool TypeCanonicalizer::IsCanonicalSubtype(uint32_t canonical_sub_index,
                                           uint32_t canonical_super_index) {
  // Multiple threads could try to register and access recursive groups
  // concurrently.
  // TODO(manoskouk): Investigate if we can improve this synchronization.
  base::MutexGuard mutex_guard(&mutex_);
  while (canonical_sub_index != kNoSuperType) {
    if (canonical_sub_index == canonical_super_index) return true;
    canonical_sub_index = canonical_supertypes_[canonical_sub_index];
  }
  return false;
}

bool TypeCanonicalizer::IsCanonicalSubtype(uint32_t sub_index,
                                           uint32_t super_index,
                                           const WasmModule* sub_module,
                                           const WasmModule* super_module) {
  uint32_t canonical_super =
      super_module->isorecursive_canonical_type_ids[super_index];
  uint32_t canonical_sub =
      sub_module->isorecursive_canonical_type_ids[sub_index];
  return IsCanonicalSubtype(canonical_sub, canonical_super);
}

void TypeCanonicalizer::EmptyStorageForTesting() {
  base::MutexGuard mutex_guard(&mutex_);
  canonical_supertypes_.clear();
  canonical_groups_.clear();
  canonical_singleton_groups_.clear();
  canonical_function_sigs_.clear();
  zone_.Reset();
  AddPredefinedArrayTypes();
}

TypeCanonicalizer::CanonicalType TypeCanonicalizer::CanonicalizeTypeDef(
    const WasmModule* module, TypeDefinition type,
    uint32_t recursive_group_start) {
  DCHECK(!mutex_.TryLock());  // The caller must hold the mutex.
  uint32_t canonical_supertype = kNoSuperType;
  bool is_relative_supertype = false;
  if (type.supertype < recursive_group_start) {
    canonical_supertype =
        module->isorecursive_canonical_type_ids[type.supertype];
  } else if (type.supertype != kNoSuperType) {
    canonical_supertype = type.supertype - recursive_group_start;
    is_relative_supertype = true;
  }
  TypeDefinition result;
  switch (type.kind) {
    case TypeDefinition::kFunction: {
      const FunctionSig* original_sig = type.function_sig;
      FunctionSig::Builder builder(&zone_, original_sig->return_count(),
                                   original_sig->parameter_count());
      for (ValueType ret : original_sig->returns()) {
        builder.AddReturn(
            CanonicalizeValueType(module, ret, recursive_group_start));
      }
      for (ValueType param : original_sig->parameters()) {
        builder.AddParam(
            CanonicalizeValueType(module, param, recursive_group_start));
      }
      result = TypeDefinition(builder.Get(), canonical_supertype, type.is_final,
                              type.is_shared);
      break;
    }
    case TypeDefinition::kStruct: {
      const StructType* original_type = type.struct_type;
      StructType::Builder builder(&zone_, original_type->field_count());
      for (uint32_t i = 0; i < original_type->field_count(); i++) {
        builder.AddField(CanonicalizeValueType(module, original_type->field(i),
                                               recursive_group_start),
                         original_type->mutability(i),
                         original_type->field_offset(i));
      }
      builder.set_total_fields_size(original_type->total_fields_size());
      result = TypeDefinition(
          builder.Build(StructType::Builder::kUseProvidedOffsets),
          canonical_supertype, type.is_final, type.is_shared);
      break;
    }
    case TypeDefinition::kArray: {
      ValueType element_type = CanonicalizeValueType(
          module, type.array_type->element_type(), recursive_group_start);
      result = TypeDefinition(
          zone_.New<ArrayType>(element_type, type.array_type->mutability()),
          canonical_supertype, type.is_final, type.is_shared);
      break;
    }
  }

  return {result, is_relative_supertype};
}

// Returns the index of the canonical representative of the first type in this
// group, or -1 if an identical group does not exist.
CanonicalTypeIndex TypeCanonicalizer::FindCanonicalGroup(
    const CanonicalGroup& group) const {
  // Groups of size 0 do not make sense here; groups of size 1 should use
  // {CanonicalSingletonGroup} (see below).
  DCHECK_LT(1, group.types.size());
  auto it = canonical_groups_.find(group);
  return it == canonical_groups_.end() ? CanonicalTypeIndex::Invalid()
                                       : it->second;
}

// Returns the canonical index of the given group if it already exists.
// Optionally returns the FunctionSig* providing the type definition if
// the type in the group is a function type.
CanonicalTypeIndex TypeCanonicalizer::FindCanonicalGroup(
    const CanonicalSingletonGroup& group, const FunctionSig** out_sig) const {
  auto it = canonical_singleton_groups_.find(group);
  static_assert(kMaxCanonicalTypes <= kMaxInt);
  if (it == canonical_singleton_groups_.end()) {
    return CanonicalTypeIndex::Invalid();
  }
  if (out_sig) {
    const CanonicalSingletonGroup& found = it->first;
    if (found.type.type_def.kind == TypeDefinition::kFunction) {
      *out_sig = found.type.type_def.function_sig;
    }
  }
  return it->second;
}

size_t TypeCanonicalizer::EstimateCurrentMemoryConsumption() const {
  UPDATE_WHEN_CLASS_CHANGES(TypeCanonicalizer, 296);
  // The storage of the canonical group's types is accounted for via the
  // allocator below (which tracks the zone memory).
  base::MutexGuard mutex_guard(&mutex_);
  size_t result = ContentSize(canonical_supertypes_);
  result += ContentSize(canonical_groups_);
  result += ContentSize(canonical_singleton_groups_);
  result += ContentSize(canonical_function_sigs_);
  result += allocator_.GetCurrentMemoryUsage();
  if (v8_flags.trace_wasm_offheap_memory) {
    PrintF("TypeCanonicalizer: %zu\n", result);
  }
  return result;
}

size_t TypeCanonicalizer::GetCurrentNumberOfTypes() const {
  base::MutexGuard mutex_guard(&mutex_);
  return canonical_supertypes_.size();
}

// static
void TypeCanonicalizer::PrepareForCanonicalTypeId(Isolate* isolate, int id) {
  Heap* heap = isolate->heap();
  // {2 * (id + 1)} needs to fit in an int.
  CHECK_LE(id, kMaxInt / 2 - 1);
  // Canonical types and wrappers are zero-indexed.
  const int length = id + 1;
  // The fast path is non-handlified.
  Tagged<WeakFixedArray> old_rtts_raw = heap->wasm_canonical_rtts();
  Tagged<WeakFixedArray> old_wrappers_raw = heap->js_to_wasm_wrappers();

  // Fast path: Lengths are sufficient.
  int old_length = old_rtts_raw->length();
  DCHECK_EQ(old_length, old_wrappers_raw->length());
  if (old_length >= length) return;

  // Allocate bigger WeakFixedArrays for rtts and wrappers. Grow them
  // exponentially.
  const int new_length = std::max(old_length * 3 / 2, length);
  CHECK_LT(old_length, new_length);

  // Allocation can invalidate previous unhandled pointers.
  Handle<WeakFixedArray> old_rtts{old_rtts_raw, isolate};
  Handle<WeakFixedArray> old_wrappers{old_wrappers_raw, isolate};
  old_rtts_raw = old_wrappers_raw = {};

  // We allocate the WeakFixedArray filled with undefined values, as we cannot
  // pass the cleared value in a Handle (see https://crbug.com/364591622). We
  // overwrite the new entries via {MemsetTagged} afterwards.
  Handle<WeakFixedArray> new_rtts =
      WeakFixedArray::New(isolate, new_length, AllocationType::kOld);
  WeakFixedArray::CopyElements(isolate, *new_rtts, 0, *old_rtts, 0, old_length);
  MemsetTagged(new_rtts->RawFieldOfFirstElement() + old_length,
               ClearedValue(isolate), new_length - old_length);
  Handle<WeakFixedArray> new_wrappers =
      WeakFixedArray::New(isolate, new_length, AllocationType::kOld);
  WeakFixedArray::CopyElements(isolate, *new_wrappers, 0, *old_wrappers, 0,
                               old_length);
  MemsetTagged(new_wrappers->RawFieldOfFirstElement() + old_length,
               ClearedValue(isolate), new_length - old_length);
  heap->SetWasmCanonicalRttsAndJSToWasmWrappers(*new_rtts, *new_wrappers);
}

// static
void TypeCanonicalizer::ClearWasmCanonicalTypesForTesting(Isolate* isolate) {
  ReadOnlyRoots roots(isolate);
  isolate->heap()->SetWasmCanonicalRttsAndJSToWasmWrappers(
      roots.empty_weak_fixed_array(), roots.empty_weak_fixed_array());
}

bool TypeCanonicalizer::IsFunctionSignature(uint32_t canonical_index) const {
  base::MutexGuard mutex_guard(&mutex_);
  auto it = canonical_function_sigs_.find(canonical_index);
  return it != canonical_function_sigs_.end();
}

#ifdef DEBUG
bool TypeCanonicalizer::Contains(const FunctionSig* sig) const {
  base::MutexGuard mutex_guard(&mutex_);
  return zone_.Contains(sig);
}
#endif

}  // namespace v8::internal::wasm
