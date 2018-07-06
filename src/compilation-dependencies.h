// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILATION_DEPENDENCIES_H_
#define V8_COMPILATION_DEPENDENCIES_H_

#include "src/objects.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

// Collects and installs dependencies of the code that is being generated.
class V8_EXPORT_PRIVATE CompilationDependencies {
 public:
  CompilationDependencies(Isolate* isolate, Zone* zone);

  V8_WARN_UNUSED_RESULT bool Commit(Handle<Code> code);

  // Return the initial map of {function} and record the assumption that it
  // stays the intial map.
  Handle<Map> DependOnInitialMap(Handle<JSFunction> function);

  // Record the assumption that {map} stays stable.
  void DependOnStableMap(Handle<Map> map);

  // Record the assumption that {target_map} can be transitioned to, i.e., that
  // it does not become deprecated.
  void DependOnTransition(Handle<Map> target_map);

  // Return the pretenure mode of {site} and record the assumption that it does
  // not change.
  PretenureFlag DependOnPretenureMode(Handle<AllocationSite> site);

  // Record the assumption that the field type of a field does not change. The
  // field is identified by the argument(s).
  void DependOnFieldType(Handle<Map> map, int descriptor);
  void DependOnFieldType(const LookupIterator* it);

  // Record the assumption that neither {cell}'s {CellType} changes, nor the
  // {IsReadOnly()} flag of {cell}'s {PropertyDetails}.
  void DependOnGlobalProperty(Handle<PropertyCell> cell);

  // Record the assumption that the protector remains valid.
  void DependOnProtector(Handle<PropertyCell> cell);

  // Record the assumption that {site}'s {ElementsKind} doesn't change.
  void DependOnElementsKind(Handle<AllocationSite> site);

  // Depend on the stability of (the maps of) all prototypes of every class in
  // {receiver_type} up to (and including) the {holder}.
  void DependOnStablePrototypeChains(
      Handle<Context> native_context,
      std::vector<Handle<Map>> const& receiver_maps, Handle<JSObject> holder);

  // Like DependOnElementsKind but also applies to all nested allocation sites.
  void DependOnElementsKinds(Handle<AllocationSite> site);

  // Exposed only for testing purposes.
  bool AreValid() const;

  // Exposed only because C++.
  class Dependency;

 private:
  Isolate* isolate_;
  Zone* zone_;
  ZoneForwardList<Dependency*> dependencies_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILATION_DEPENDENCIES_H_
