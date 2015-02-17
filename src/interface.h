// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERFACE_H_
#define V8_INTERFACE_H_

#include "src/zone.h"

namespace v8 {
namespace internal {


class AstRawString;


// This class represents the interface of a module: a set of exported names.
//
// TODO(adamk): Rename this to ModuleRecord, ModuleDescriptor, or similar.
class Interface : public ZoneObject {
 public:
  // ---------------------------------------------------------------------------
  // Factory methods.

  static Interface* New(Zone* zone) { return new (zone) Interface(); }

  // ---------------------------------------------------------------------------
  // Mutators.

  // Add a name to the list of exports. If it already exists, or this interface
  // is frozen, that's an error.
  void Add(const AstRawString* name, Zone* zone, bool* ok);

  // Do not allow any further refinements, directly or through unification.
  void Freeze() { frozen_ = true; }

  // Assign an index.
  void Allocate(int index) {
    DCHECK(IsFrozen() && index_ == -1);
    index_ = index;
  }

  // ---------------------------------------------------------------------------
  // Accessors.

  // Check whether this is closed (i.e. fully determined).
  bool IsFrozen() { return frozen_; }

  int Length() {
    DCHECK(IsFrozen());
    ZoneHashMap* exports = exports_;
    return exports ? exports->occupancy() : 0;
  }

  // The context slot in the hosting script context pointing to this module.
  int Index() {
    DCHECK(IsFrozen());
    return index_;
  }

  // ---------------------------------------------------------------------------
  // Iterators.

  // Use like:
  //   for (auto it = interface->iterator(); !it.done(); it.Advance()) {
  //     ... it.name() ... it.interface() ...
  //   }
  class Iterator {
   public:
    bool done() const { return entry_ == NULL; }
    const AstRawString* name() const {
      DCHECK(!done());
      return static_cast<const AstRawString*>(entry_->key);
    }
    void Advance() { entry_ = exports_->Next(entry_); }

   private:
    friend class Interface;
    explicit Iterator(const ZoneHashMap* exports)
        : exports_(exports), entry_(exports ? exports->Start() : NULL) {}

    const ZoneHashMap* exports_;
    ZoneHashMap::Entry* entry_;
  };

  Iterator iterator() const { return Iterator(this->exports_); }

  // ---------------------------------------------------------------------------
  // Debugging.
#ifdef DEBUG
  void Print(int n = 0);  // n = indentation; n < 0 => don't print recursively
#endif

  // ---------------------------------------------------------------------------
  // Implementation.
 private:
  bool frozen_;
  ZoneHashMap* exports_;   // Module exports and their types (allocated lazily)
  int index_;

  Interface() : frozen_(false), exports_(NULL), index_(-1) {
#ifdef DEBUG
    if (FLAG_print_interface_details)
      PrintF("# Creating %p\n", static_cast<void*>(this));
#endif
  }
};

} }  // namespace v8::internal

#endif  // V8_INTERFACE_H_
