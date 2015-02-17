// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/interface.h"

#include "src/ast-value-factory.h"

namespace v8 {
namespace internal {

// ---------------------------------------------------------------------------
// Addition.

#ifdef DEBUG
// Current nesting depth for debug output.
class Nesting {
 public:
  Nesting()  { current_ += 2; }
  ~Nesting() { current_ -= 2; }
  static int current() { return current_; }
 private:
  static int current_;
};

int Nesting::current_ = 0;
#endif


void Interface::Add(const AstRawString* name, Zone* zone, bool* ok) {
  void* key = const_cast<AstRawString*>(name);

#ifdef DEBUG
  if (FLAG_print_interface_details) {
    PrintF("%*s# Adding...\n", Nesting::current(), "");
    PrintF("%*sthis = ", Nesting::current(), "");
    this->Print(Nesting::current());
    PrintF("%*s%.*s : ", Nesting::current(), "", name->length(),
           name->raw_data());
  }
#endif

  ZoneHashMap** map = &exports_;
  ZoneAllocationPolicy allocator(zone);

  if (*map == nullptr) {
    *map = new(zone->New(sizeof(ZoneHashMap)))
        ZoneHashMap(ZoneHashMap::PointersMatch,
                    ZoneHashMap::kDefaultHashMapCapacity, allocator);
  }

  ZoneHashMap::Entry* p =
      (*map)->Lookup(key, name->hash(), !IsFrozen(), allocator);
  if (p == nullptr || p->value != nullptr) {
    *ok = false;
  }

  p->value = key;

#ifdef DEBUG
  if (FLAG_print_interface_details) {
    PrintF("%*sthis' = ", Nesting::current(), "");
    this->Print(Nesting::current());
    PrintF("%*s# Added.\n", Nesting::current(), "");
  }
#endif
}


// ---------------------------------------------------------------------------
// Printing.

#ifdef DEBUG
void Interface::Print(int n) {
  int n0 = n > 0 ? n : 0;

  if (FLAG_print_interface_details) {
    PrintF("%p ", static_cast<void*>(this));
  }

  PrintF("module %d %s{", Index(), IsFrozen() ? "" : "(unresolved) ");
  ZoneHashMap* map = exports_;
  if (map == nullptr || map->occupancy() == 0) {
    PrintF("}\n");
  } else if (n < 0 || n0 >= 2 * FLAG_print_interface_depth) {
    // Avoid infinite recursion on cyclic types.
    PrintF("...}\n");
  } else {
    PrintF("\n");
    for (ZoneHashMap::Entry* p = map->Start(); p != nullptr; p = map->Next(p)) {
      String* name = *static_cast<String**>(p->key);
      PrintF("%*s%s : ", n0 + 2, "", name->ToAsciiArray());
    }
    PrintF("%*s}\n", n0, "");
  }
}
#endif

} }  // namespace v8::internal
