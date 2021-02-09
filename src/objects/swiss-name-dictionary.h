// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SWISS_NAME_DICTIONARY_H_
#define V8_OBJECTS_SWISS_NAME_DICTIONARY_H_

#include "src/base/export-template.h"
#include "src/common/globals.h"
#include "src/objects/fixed-array.h"
#include "src/objects/internal-index.h"
#include "src/objects/js-objects.h"
#include "src/roots/roots.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

// A property backing store based on Swiss Tables/Abseil's flat_hash_map. The
// implementation is heavily based on Abseil's raw_hash_set.h.
//
// Memory layout (see below for detailed description of parts):
//   Prefix:                      [table type dependent part, can have 0 size]
//   Capacity:                    4 bytes, raw int32_t
//   Meta table pointer:          kTaggedSize bytes
//   Data table:                  2 * |capacity| * |kTaggedSize| bytes
//   Ctrl table:                  |capacity| + |kGroupWidth| uint8_t entries
//   PropertyDetails table:       |capacity| uint_8 entries
//
// Note that because of |kInitialCapacity| == 4 there is no need for padding.
//
// Description of parts directly contained in SwissNameDictionary allocation:
//   Prefix:
//     In case of SwissNameDictionary:
//       identity hash: 4 bytes, raw int32_t
//   Meta table pointer: kTaggedSize bytes.
//     See below for explanation of the meta table.
//     For capacity 0, this contains the Smi |kNoMetaTableSentinel| instead.
//   Data table:
//     For each logical bucket of the hash table, contains the corresponding key
//     and value.
//   Ctrl table:
//     The control table is used to implement a Swiss Table: Each byte is either
//     Ctrl::kEmpty, Ctrl::kDeleted, or in case of a bucket denoting a present
//     entry in the hash table, the 7 lowest bits of the key's hash. The first
//     |capacity| entries are the actual control table. The additional
//     |kGroupWidth| bytes contain a copy of the first min(capacity,
//     kGroupWidth) bytes of the table.
//   PropertyDetails table:
//     Each byte contains the PropertyDetails for the corresponding bucket of
//     the ctrl table. Entries may contain unitialized data if the corresponding
//     bucket hasn't been used before.
//
// Meta table:
//   The meta table (not to be confused with the control table used in any
//   Swiss Table design!) is a separate ByteArray. Here, the "X" in "uintX_t"
//   depends on the capacity of the swiss table. For capacities <= 256 we have X
//   = 8, for 256 < |capacity| <= 2^16 we have X = 16, and otherwise X = 32 (see
//   MetaTableSizePerEntryFor). It contais the following data:
//     Number of Entries: uintX_t.
//     Number of Deleted Entries: uintX_t.
//     Enumeration table: max_load_factor * Capacity() entries of type uintX_t:
//       The i-th entry in the enumeration table
//       contains the number of the bucket representing the i-th entry of the
//       table in enumeration order. Entries may contain unitialized data if the
//       corresponding bucket  hasn't been used before.
class SwissNameDictionary : public HeapObject {
 public:
  inline int Capacity();

  inline static constexpr bool IsValidCapacity(int capacity);

  // Returns total size in bytes required for a table of given capacity.
  inline static constexpr int SizeFor(int capacity);

  // TODO(v8:11388) This is a temporary placeholder for the actual value, which
  // is added here in a follow-up CL.
  static const int kGroupWidth = 8;

  class BodyDescriptor;

  // Note that 0 is also a valid capacity. Changing this value to a smaller one
  // may make some padding necessary in the data layout.
  static constexpr int kInitialCapacity = kSwissNameDictionaryInitialCapacity;

  // Defines how many kTaggedSize sized values are associcated which each entry
  // in the data table.
  static constexpr int kDataTableEntryCount = 2;

  inline static constexpr int DataTableSize(int capacity);
  inline static constexpr int CtrlTableSize(int capacity);

  // TODO(v8:11388) We would like to use Torque-generated constants here, but
  // those are currently incorrect.
  // Offset into the overall table, starting at HeapObject standard fields,
  // in bytes. This means that the map is stored at offset 0.
  using Offset = int;
  inline static constexpr Offset PrefixOffset();
  inline static constexpr Offset CapacityOffset();
  inline static constexpr Offset MetaTablePointerOffset();
  inline static constexpr Offset DataTableStartOffset();
  inline static constexpr Offset DataTableEndOffset(int capacity);
  inline static constexpr Offset CtrlTableStartOffset(int capacity);
  inline static constexpr Offset PropertyDetailsTableStartOffset(int capacity);

  DECL_VERIFIER(SwissNameDictionary)
  DECL_PRINTER(SwissNameDictionary)
  DECL_CAST(SwissNameDictionary)
  OBJECT_CONSTRUCTORS(SwissNameDictionary, HeapObject);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_SWISS_NAME_DICTIONARY_H_
