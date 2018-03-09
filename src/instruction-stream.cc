// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/instruction-stream.h"

#include "src/builtins/builtins.h"
#include "src/heap/heap.h"
#include "src/objects-inl.h"
#include "src/objects/code-inl.h"
#include "src/snapshot/snapshot.h"

namespace v8 {
namespace internal {

InstructionStream::InstructionStream(uint8_t* bytes, size_t byte_length,
                                     int builtin_index)
    : byte_length_(byte_length), bytes_(bytes), builtin_index_(builtin_index) {
  DCHECK(Builtins::IsBuiltinId(builtin_index_));
  DCHECK_NOT_NULL(bytes_);
}

// static
bool InstructionStream::PcIsOffHeap(Isolate* isolate, Address pc) {
#ifdef V8_EMBEDDED_BUILTINS
  const uint8_t* start = isolate->embedded_blob();
  return start <= pc && pc < start + isolate->embedded_blob_size();
#else
  return false;
#endif
}

// static
Code* InstructionStream::TryLookupCode(Isolate* isolate, Address address) {
#ifdef V8_EMBEDDED_BUILTINS
  DCHECK(FLAG_stress_off_heap_code);

  if (!PcIsOffHeap(isolate, address)) return nullptr;

  EmbeddedData d = EmbeddedData::FromBlob(isolate->embedded_blob(),
                                          isolate->embedded_blob_size());

  int l = 0, r = Builtins::builtin_count;
  while (l < r) {
    const int mid = (l + r) / 2;
    const uint8_t* start = d.InstructionStartOfBuiltin(mid);
    const uint8_t* end = start + d.InstructionSizeOfBuiltin(mid);

    if (address < start) {
      r = mid;
    } else if (address >= end) {
      l = mid + 1;
    } else {
      return isolate->builtins()->builtin(mid);
    }
  }

  UNREACHABLE();
#else
  return nullptr;
#endif
}

}  // namespace internal
}  // namespace v8
