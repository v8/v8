// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_REMEMBERED_SET_H_
#define V8_HEAP_CPPGC_REMEMBERED_SET_H_

#include <set>

#include "src/base/macros.h"

namespace cppgc {

class Visitor;

namespace internal {

class HeapBase;
class HeapObjectHeader;
class MutatorMarkingState;

class V8_EXPORT_PRIVATE OldToNewRememberedSet final {
 public:
  explicit OldToNewRememberedSet(const HeapBase& heap) : heap_(heap) {}

  OldToNewRememberedSet(const OldToNewRememberedSet&) = delete;
  OldToNewRememberedSet& operator=(const OldToNewRememberedSet&) = delete;

  void AddSlot(void* slot);
  void AddSourceObject(HeapObjectHeader& source_hoh);

  void InvalidateRememberedSlotsInRange(void* begin, void* end);
  void InvalidateRememberedSourceObject(HeapObjectHeader& source_hoh);

  void Visit(Visitor&, MutatorMarkingState&);

  void Reset();

 private:
  friend class MinorGCTest;

  const HeapBase& heap_;
  std::set<void*> remembered_slots_;
  std::set<HeapObjectHeader*> remembered_source_objects_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_REMEMBERED_SET_H_
