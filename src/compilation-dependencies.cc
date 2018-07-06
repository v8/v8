// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compilation-dependencies.h"

#include "src/handles-inl.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

// TODO(neis): Move these to the DependentCode class.
namespace {
DependentCode* GetDependentCode(Handle<Object> object) {
  if (object->IsMap()) {
    return Handle<Map>::cast(object)->dependent_code();
  } else if (object->IsPropertyCell()) {
    return Handle<PropertyCell>::cast(object)->dependent_code();
  } else if (object->IsAllocationSite()) {
    return Handle<AllocationSite>::cast(object)->dependent_code();
  }
  UNREACHABLE();
}

void SetDependentCode(Handle<Object> object, Handle<DependentCode> dep) {
  if (object->IsMap()) {
    Handle<Map>::cast(object)->set_dependent_code(*dep);
  } else if (object->IsPropertyCell()) {
    Handle<PropertyCell>::cast(object)->set_dependent_code(*dep);
  } else if (object->IsAllocationSite()) {
    Handle<AllocationSite>::cast(object)->set_dependent_code(*dep);
  } else {
    UNREACHABLE();
  }
}

void InstallDependency(Isolate* isolate, Handle<WeakCell> source,
                       Handle<HeapObject> target,
                       DependentCode::DependencyGroup group) {
  Handle<DependentCode> old_deps(GetDependentCode(target), isolate);
  Handle<DependentCode> new_deps =
      DependentCode::InsertWeakCode(old_deps, group, source);
  // Update the list head if necessary.
  if (!new_deps.is_identical_to(old_deps)) SetDependentCode(target, new_deps);
}
}  // namespace

CompilationDependencies::CompilationDependencies(Isolate* isolate, Zone* zone)
    : isolate_(isolate), zone_(zone), dependencies_(zone) {}

class CompilationDependencies::Dependency : public ZoneObject {
 public:
  virtual bool IsValid() const = 0;
  virtual void Install(Isolate* isolate, Handle<WeakCell> code) = 0;
};

class InitialMapDependency final : public CompilationDependencies::Dependency {
 public:
  InitialMapDependency(Handle<JSFunction> function, Handle<Map> initial_map)
      : function_(function), initial_map_(initial_map) {
    DCHECK(IsValid());
  }

  bool IsValid() const override {
    DisallowHeapAllocation no_heap_allocation;
    DCHECK(function_->has_initial_map());
    return *initial_map_ == function_->initial_map();
  }

  void Install(Isolate* isolate, Handle<WeakCell> code) override {
    DCHECK(IsValid());
    InstallDependency(isolate, code, initial_map_,
                      DependentCode::kInitialMapChangedGroup);
  }

 private:
  Handle<JSFunction> function_;
  Handle<Map> initial_map_;
};

class StableMapDependency final : public CompilationDependencies::Dependency {
 public:
  explicit StableMapDependency(Handle<Map> map) : map_(map) {
    DCHECK(IsValid());
  }

  bool IsValid() const override {
    DisallowHeapAllocation no_heap_allocation;
    return map_->is_stable();
  }

  void Install(Isolate* isolate, Handle<WeakCell> code) override {
    DCHECK(IsValid());
    InstallDependency(isolate, code, map_, DependentCode::kPrototypeCheckGroup);
  }

 private:
  Handle<Map> map_;
};

class TransitionDependency final : public CompilationDependencies::Dependency {
 public:
  explicit TransitionDependency(Handle<Map> map) : map_(map) {
    DCHECK(IsValid());
  }

  bool IsValid() const override {
    DisallowHeapAllocation no_heap_allocation;
    return !map_->is_deprecated();
  }

  void Install(Isolate* isolate, Handle<WeakCell> code) override {
    DCHECK(IsValid());
    InstallDependency(isolate, code, map_, DependentCode::kTransitionGroup);
  }

 private:
  Handle<Map> map_;
};

class PretenureModeDependency final
    : public CompilationDependencies::Dependency {
 public:
  PretenureModeDependency(Handle<AllocationSite> site, PretenureFlag mode)
      : site_(site), mode_(mode) {
    DCHECK(IsValid());
  }

  bool IsValid() const override {
    DisallowHeapAllocation no_heap_allocation;
    return mode_ == site_->GetPretenureMode();
  }

  void Install(Isolate* isolate, Handle<WeakCell> code) override {
    DCHECK(IsValid());
    InstallDependency(isolate, code, site_,
                      DependentCode::kAllocationSiteTenuringChangedGroup);
  }

 private:
  Handle<AllocationSite> site_;
  PretenureFlag mode_;
};

class FieldTypeDependency final : public CompilationDependencies::Dependency {
 public:
  FieldTypeDependency(Isolate* isolate, Handle<Map> owner, int descriptor,
                      Handle<FieldType> type)
      : isolate_(isolate), owner_(owner), descriptor_(descriptor), type_(type) {
    DCHECK(IsValid());
  }

  bool IsValid() const override {
    DisallowHeapAllocation no_heap_allocation;
    CHECK_EQ(*owner_, owner_->FindFieldOwner(isolate_, descriptor_));
    return *type_ == owner_->instance_descriptors()->GetFieldType(descriptor_);
  }

  void Install(Isolate* isolate, Handle<WeakCell> code) override {
    DCHECK(IsValid());
    InstallDependency(isolate, code, owner_, DependentCode::kFieldOwnerGroup);
  }

 private:
  Isolate* isolate_;
  Handle<Map> owner_;
  int descriptor_;
  Handle<FieldType> type_;
};

class GlobalPropertyDependency final
    : public CompilationDependencies::Dependency {
 public:
  GlobalPropertyDependency(Handle<PropertyCell> cell, PropertyCellType type,
                           bool read_only)
      : cell_(cell), type_(type), read_only_(read_only) {
    DCHECK(IsValid());
  }

  bool IsValid() const override {
    DisallowHeapAllocation no_heap_allocation;
    return type_ == cell_->property_details().cell_type() &&
           read_only_ == cell_->property_details().IsReadOnly();
  }

  void Install(Isolate* isolate, Handle<WeakCell> code) override {
    DCHECK(IsValid());
    InstallDependency(isolate, code, cell_,
                      DependentCode::kPropertyCellChangedGroup);
  }

 private:
  Handle<PropertyCell> cell_;
  PropertyCellType type_;
  bool read_only_;
};

class ProtectorDependency final : public CompilationDependencies::Dependency {
 public:
  explicit ProtectorDependency(Handle<PropertyCell> cell) : cell_(cell) {
    DCHECK(IsValid());
  }

  bool IsValid() const override {
    DisallowHeapAllocation no_heap_allocation;
    return cell_->value() == Smi::FromInt(Isolate::kProtectorValid);
  }

  void Install(Isolate* isolate, Handle<WeakCell> code) override {
    DCHECK(IsValid());
    InstallDependency(isolate, code, cell_,
                      DependentCode::kPropertyCellChangedGroup);
  }

 private:
  Handle<PropertyCell> cell_;
};

class ElementsKindDependency final
    : public CompilationDependencies::Dependency {
 public:
  ElementsKindDependency(Handle<AllocationSite> site, ElementsKind kind)
      : site_(site), kind_(kind) {
    DCHECK(IsValid());
  }

  bool IsValid() const override {
    DisallowHeapAllocation no_heap_allocation;
    DCHECK(AllocationSite::ShouldTrack(kind_));
    ElementsKind kind = site_->PointsToLiteral()
                            ? site_->boilerplate()->GetElementsKind()
                            : site_->GetElementsKind();
    return kind_ == kind;
  }

  void Install(Isolate* isolate, Handle<WeakCell> code) override {
    DCHECK(IsValid());
    InstallDependency(isolate, code, site_,
                      DependentCode::kAllocationSiteTransitionChangedGroup);
  }

 private:
  Handle<AllocationSite> site_;
  ElementsKind kind_;
};

Handle<Map> CompilationDependencies::DependOnInitialMap(
    Handle<JSFunction> function) {
  Handle<Map> map(function->initial_map(), function->GetIsolate());
  dependencies_.push_front(new (zone_) InitialMapDependency(function, map));
  return map;
}

void CompilationDependencies::DependOnStableMap(Handle<Map> map) {
  if (map->CanTransition()) {
    dependencies_.push_front(new (zone_) StableMapDependency(map));
  } else {
    DCHECK(map->is_stable());
  }
}

void CompilationDependencies::DependOnTransition(Handle<Map> target_map) {
  if (target_map->CanBeDeprecated()) {
    dependencies_.push_front(new (zone_) TransitionDependency(target_map));
  } else {
    DCHECK(!target_map->is_deprecated());
  }
}

PretenureFlag CompilationDependencies::DependOnPretenureMode(
    Handle<AllocationSite> site) {
  PretenureFlag mode = site->GetPretenureMode();
  dependencies_.push_front(new (zone_) PretenureModeDependency(site, mode));
  return mode;
}

void CompilationDependencies::DependOnFieldType(Handle<Map> map,
                                                int descriptor) {
  Handle<Map> owner(map->FindFieldOwner(isolate_, descriptor), isolate_);
  Handle<FieldType> type(
      owner->instance_descriptors()->GetFieldType(descriptor), isolate_);
  DCHECK_EQ(*type, map->instance_descriptors()->GetFieldType(descriptor));
  dependencies_.push_front(
      new (zone_) FieldTypeDependency(isolate_, owner, descriptor, type));
}

void CompilationDependencies::DependOnFieldType(const LookupIterator* it) {
  Handle<Map> owner = it->GetFieldOwnerMap();
  int descriptor = it->GetFieldDescriptorIndex();
  Handle<FieldType> type = it->GetFieldType();
  CHECK_EQ(*type,
           it->GetHolder<Map>()->map()->instance_descriptors()->GetFieldType(
               descriptor));
  dependencies_.push_front(
      new (zone_) FieldTypeDependency(isolate_, owner, descriptor, type));
}

void CompilationDependencies::DependOnGlobalProperty(
    Handle<PropertyCell> cell) {
  PropertyCellType type = cell->property_details().cell_type();
  bool read_only = cell->property_details().IsReadOnly();
  dependencies_.push_front(new (zone_)
                               GlobalPropertyDependency(cell, type, read_only));
}

void CompilationDependencies::DependOnProtector(Handle<PropertyCell> cell) {
  dependencies_.push_front(new (zone_) ProtectorDependency(cell));
}

void CompilationDependencies::DependOnElementsKind(
    Handle<AllocationSite> site) {
  // Do nothing if the object doesn't have any useful element transitions left.
  ElementsKind kind = site->PointsToLiteral()
                          ? site->boilerplate()->GetElementsKind()
                          : site->GetElementsKind();
  if (AllocationSite::ShouldTrack(kind)) {
    dependencies_.push_front(new (zone_) ElementsKindDependency(site, kind));
  }
}

bool CompilationDependencies::AreValid() const {
  for (auto dep : dependencies_) {
    if (!dep->IsValid()) return false;
  }
  return true;
}

bool CompilationDependencies::Commit(Handle<Code> code) {
  // Check validity of all dependencies first, such that we can abort before
  // installing anything.
  if (!AreValid()) {
    dependencies_.clear();
    return false;
  }

  Handle<WeakCell> cell = Code::WeakCellFor(code);
  for (auto dep : dependencies_) {
    dep->Install(isolate_, cell);
  }
  dependencies_.clear();
  return true;
}

namespace {
void DependOnStablePrototypeChain(CompilationDependencies* deps,
                                  Handle<Map> map,
                                  MaybeHandle<JSReceiver> last_prototype) {
  for (PrototypeIterator i(map); !i.IsAtEnd(); i.Advance()) {
    Handle<JSReceiver> const current =
        PrototypeIterator::GetCurrent<JSReceiver>(i);
    deps->DependOnStableMap(handle(current->map(), current->GetIsolate()));
    Handle<JSReceiver> last;
    if (last_prototype.ToHandle(&last) && last.is_identical_to(current)) {
      break;
    }
  }
}
}  // namespace

void CompilationDependencies::DependOnStablePrototypeChains(
    Handle<Context> native_context,
    std::vector<Handle<Map>> const& receiver_maps, Handle<JSObject> holder) {
  // Determine actual holder and perform prototype chain checks.
  for (auto map : receiver_maps) {
    // Perform the implicit ToObject for primitives here.
    // Implemented according to ES6 section 7.3.2 GetV (V, P).
    Handle<JSFunction> constructor;
    if (Map::GetConstructorFunction(map, native_context)
            .ToHandle(&constructor)) {
      map = handle(constructor->initial_map(), isolate_);
    }
    DependOnStablePrototypeChain(this, map, holder);
  }
}

void CompilationDependencies::DependOnElementsKinds(
    Handle<AllocationSite> site) {
  while (true) {
    DependOnElementsKind(site);
    if (!site->nested_site()->IsAllocationSite()) break;
    site = handle(AllocationSite::cast(site->nested_site()), isolate_);
  }
  CHECK_EQ(site->nested_site(), Smi::kZero);
}

}  // namespace internal
}  // namespace v8
