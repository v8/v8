// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ROOTS_INL_H_
#define V8_ROOTS_INL_H_

#include "src/roots.h"

#include "src/heap/heap-inl.h"

namespace v8 {

namespace internal {

ReadOnlyRoots::ReadOnlyRoots(Isolate* isolate) : heap_(isolate->heap()) {}

#define ROOT_ACCESSOR(type, name, camel_name) \
  type* ReadOnlyRoots::name() { return heap_->name(); }
STRONG_READ_ONLY_ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

#define STRING_ACCESSOR(name, str) \
  String* ReadOnlyRoots::name() { return heap_->name(); }
INTERNALIZED_STRING_LIST(STRING_ACCESSOR)
#undef STRING_ACCESSOR

#define SYMBOL_ACCESSOR(name) \
  Symbol* ReadOnlyRoots::name() { return heap_->name(); }
PRIVATE_SYMBOL_LIST(SYMBOL_ACCESSOR)
#undef SYMBOL_ACCESSOR

#define SYMBOL_ACCESSOR(name, description) \
  Symbol* ReadOnlyRoots::name() { return heap_->name(); }
PUBLIC_SYMBOL_LIST(SYMBOL_ACCESSOR)
WELL_KNOWN_SYMBOL_LIST(SYMBOL_ACCESSOR)
#undef SYMBOL_ACCESSOR

#define STRUCT_MAP_ACCESSOR(NAME, Name, name) \
  Map* ReadOnlyRoots::name##_map() { return heap_->name##_map(); }
STRUCT_LIST(STRUCT_MAP_ACCESSOR)
#undef STRUCT_MAP_ACCESSOR

#define ALLOCATION_SITE_MAP_ACCESSOR(NAME, Name, Size, name) \
  Map* ReadOnlyRoots::name##_map() { return heap_->name##_map(); }
ALLOCATION_SITE_LIST(ALLOCATION_SITE_MAP_ACCESSOR)
#undef ALLOCATION_SITE_MAP_ACCESSOR

}  // namespace internal

}  // namespace v8

#endif  // V8_ROOTS_INL_H_
