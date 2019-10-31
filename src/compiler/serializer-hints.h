// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines the hints classed gathered temporarily by the
// SerializerForBackgroundCompilation while it's analysing the bytecode
// and copying the necessary data to the JSHeapBroker for further usage
// by the reducers that run on the background thread.

#ifndef V8_COMPILER_SERIALIZER_HINTS_H_
#define V8_COMPILER_SERIALIZER_HINTS_H_

#include "src/compiler/functional-list.h"
#include "src/handles/handles.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

class Context;
class Object;
class Map;

namespace compiler {

template <typename T, typename EqualTo>
class FunctionalSet {
 public:
  void Add(T const& elem, Zone* zone) {
    for (auto const& l : data_) {
      if (equal_to(l, elem)) return;
    }
    data_.PushFront(elem, zone);
  }

  bool Includes(FunctionalSet<T, EqualTo> const& other) const {
    return std::all_of(other.begin(), other.end(), [&](T const& other_elem) {
      return std::any_of(this->begin(), this->end(), [&](T const& this_elem) {
        return equal_to(this_elem, other_elem);
      });
    });
  }

  bool IsEmpty() const { return data_.begin() == data_.end(); }

  void Clear() { data_.Clear(); }

  // Warning: quadratic time complexity.
  bool operator==(const FunctionalSet<T, EqualTo>& other) const {
    return this->data_.Size() == other.data_.Size() && this->Includes(other) &&
           other.Includes(*this);
  }
  bool operator!=(const FunctionalSet<T, EqualTo>& other) const {
    return !(*this == other);
  }

  using iterator = typename FunctionalList<T>::iterator;

  iterator begin() const { return data_.begin(); }
  iterator end() const { return data_.end(); }

 private:
  static EqualTo equal_to;
  FunctionalList<T> data_;
};

template <typename T, typename EqualTo>
EqualTo FunctionalSet<T, EqualTo>::equal_to;

struct VirtualContext {
  unsigned int distance;
  Handle<Context> context;

  VirtualContext(unsigned int distance_in, Handle<Context> context_in)
      : distance(distance_in), context(context_in) {
    CHECK_GT(distance, 0);
  }
  bool operator==(const VirtualContext& other) const {
    return context.equals(other.context) && distance == other.distance;
  }
};

class FunctionBlueprint;
struct VirtualBoundFunction;

using ConstantsSet = FunctionalSet<Handle<Object>, Handle<Object>::equal_to>;
using VirtualContextsSet =
    FunctionalSet<VirtualContext, std::equal_to<VirtualContext>>;
using MapsSet = FunctionalSet<Handle<Map>, Handle<Map>::equal_to>;
using BlueprintsSet =
    FunctionalSet<FunctionBlueprint, std::equal_to<FunctionBlueprint>>;
using BoundFunctionsSet =
    FunctionalSet<VirtualBoundFunction, std::equal_to<VirtualBoundFunction>>;

class Hints {
 public:
  Hints() = default;

  static Hints SingleConstant(Handle<Object> constant, Zone* zone);

  const ConstantsSet& constants() const;
  const MapsSet& maps() const;
  const BlueprintsSet& function_blueprints() const;
  const VirtualContextsSet& virtual_contexts() const;
  const BoundFunctionsSet& virtual_bound_functions() const;

  void AddConstant(Handle<Object> constant, Zone* zone);
  void AddMap(Handle<Map> map, Zone* zone);
  void AddFunctionBlueprint(FunctionBlueprint const& function_blueprint,
                            Zone* zone);
  void AddVirtualContext(VirtualContext const& virtual_context, Zone* zone);
  void AddVirtualBoundFunction(VirtualBoundFunction const& bound_function,
                               Zone* zone);

  void Add(const Hints& other, Zone* zone);
  void AddFromChildSerializer(const Hints& other, Zone* zone);

  void Clear();
  bool IsEmpty() const;

  bool operator==(Hints const& other) const;
  bool operator!=(Hints const& other) const { return !(*this == other); }

#ifdef ENABLE_SLOW_DCHECKS
  bool Includes(Hints const& other) const;
  bool Equals(Hints const& other) const;
#endif

 private:
  VirtualContextsSet virtual_contexts_;
  ConstantsSet constants_;
  MapsSet maps_;
  BlueprintsSet function_blueprints_;
  BoundFunctionsSet virtual_bound_functions_;
};

using HintsVector = ZoneVector<Hints>;

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_SERIALIZER_HINTS_H_
