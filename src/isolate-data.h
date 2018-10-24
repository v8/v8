// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ISOLATE_DATA_H_
#define V8_ISOLATE_DATA_H_

#include "src/builtins/builtins.h"
#include "src/constants-arch.h"
#include "src/external-reference-table.h"
#include "src/roots.h"
#include "src/utils.h"

namespace v8 {
namespace internal {

class Isolate;

// This class contains a collection of data accessible from both C++ runtime
// and compiled code (including assembly stubs, builtins, interpreter bytecode
// handlers and optimized code).
// In particular, it contains pointer to the V8 heap roots table, external
// reference table and builtins array.
// The compiled code accesses the isolate data fields indirectly via the root
// register.
class IsolateData final {
 public:
  IsolateData() = default;

  // The value of the kRootRegister.
  Address isolate_root() const {
    return reinterpret_cast<Address>(this) + kIsolateRootBias;
  }

  // Root-register-relative offset of the roots table.
  static constexpr int roots_table_offset() {
    return kRootsTableOffset - kIsolateRootBias;
  }

  // Root-register-relative offset of the given root table entry.
  static constexpr int root_slot_offset(RootIndex root_index) {
    return roots_table_offset() + RootsTable::offset_of(root_index);
  }

  // Root-register-relative offset of the external reference table.
  static constexpr int external_reference_table_offset() {
    return kExternalReferenceTableOffset - kIsolateRootBias;
  }

  // Root-register-relative offset of the builtins table.
  static constexpr int builtins_table_offset() {
    return kBuiltinsTableOffset - kIsolateRootBias;
  }

  // Root-register-relative offset of the given builtin table entry.
  // TODO(ishell): remove in favour of typified id version.
  static int builtin_slot_offset(int builtin_index) {
    DCHECK(Builtins::IsBuiltinId(builtin_index));
    return builtins_table_offset() + builtin_index * kPointerSize;
  }

  // Root-register-relative offset of the builtin table entry.
  static int builtin_slot_offset(Builtins::Name id) {
    return builtins_table_offset() + id * kPointerSize;
  }

  // Root-register-relative offset of the magic number value.
  static constexpr int magic_number_offset() {
    return kMagicNumberOffset - kIsolateRootBias;
  }

  // Root-register-relative offset of the virtual call target register value.
  static constexpr int virtual_call_target_register_offset() {
    return kVirtualCallTargetRegisterOffset - kIsolateRootBias;
  }

  // Returns true if this address points to data stored in this instance.
  // If it's the case then the value can be accessed indirectly through the
  // root register.
  bool contains(Address address) const {
    STATIC_ASSERT(std::is_unsigned<Address>::value);
    Address start = reinterpret_cast<Address>(this);
    return (address - start) < sizeof(*this);
  }

  RootsTable& roots() { return roots_; }
  const RootsTable& roots() const { return roots_; }

  ExternalReferenceTable* external_reference_table() {
    return &external_reference_table_;
  }

  Object** builtins() { return &builtins_[0]; }

  // For root register verification.
  // TODO(v8:6666): Remove once the root register is fully supported on ia32.
  static constexpr intptr_t kRootRegisterSentinel = 0xcafeca11;

 private:
  static constexpr intptr_t kIsolateRootBias = kRootRegisterBias;

// Static layout definition.
#define FIELDS(V)                                                         \
  V(kRootsTableOffset, RootsTable::kEntriesCount* kPointerSize)           \
  V(kExternalReferenceTableOffset, ExternalReferenceTable::SizeInBytes()) \
  V(kBuiltinsTableOffset, Builtins::builtin_count* kPointerSize)          \
  V(kMagicNumberOffset, kIntptrSize)                                      \
  V(kVirtualCallTargetRegisterOffset, kPointerSize)                       \
  /* Total size. */                                                       \
  V(kSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(0, FIELDS)
#undef FIELDS

  RootsTable roots_;

  ExternalReferenceTable external_reference_table_;

  Object* builtins_[Builtins::builtin_count];

  // For root register verification.
  // TODO(v8:6666): Remove once the root register is fully supported on ia32.
  const intptr_t magic_number_ = kRootRegisterSentinel;

  // For isolate-independent calls on ia32.
  // TODO(v8:6666): Remove once wasm supports pc-relative jumps to builtins on
  // ia32 (otherwise the arguments adaptor call runs out of registers).
  void* virtual_call_target_register_ = nullptr;

  V8_INLINE static void AssertPredictableLayout();

  friend class Isolate;

  DISALLOW_COPY_AND_ASSIGN(IsolateData);
};

// IsolateData object must have "predictable" layout which does not change when
// cross-compiling to another platform. Otherwise there may be compatibility
// issues because of different compilers used for snapshot generator and
// actual V8 code.
void IsolateData::AssertPredictableLayout() {
  STATIC_ASSERT(offsetof(IsolateData, roots_) ==
                IsolateData::kRootsTableOffset);
  STATIC_ASSERT(offsetof(IsolateData, external_reference_table_) ==
                IsolateData::kExternalReferenceTableOffset);
  STATIC_ASSERT(offsetof(IsolateData, builtins_) ==
                IsolateData::kBuiltinsTableOffset);
  STATIC_ASSERT(sizeof(IsolateData) == IsolateData::kSize);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ISOLATE_DATA_H_
