// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_PROCESSED_FEEDBACK_H_
#define V8_COMPILER_PROCESSED_FEEDBACK_H_

#include "src/compiler/heap-refs.h"

namespace v8 {
namespace internal {
namespace compiler {

class BinaryOperationFeedback;
class CallFeedback;
class CompareOperationFeedback;
class ElementAccessFeedback;
class ForInFeedback;
class InstanceOfFeedback;
class NamedAccessFeedback;

class ProcessedFeedback : public ZoneObject {
 public:
  enum Kind {
    kInsufficient,
    kBinaryOperation,
    kCall,
    kCompareOperation,
    kElementAccess,
    kForIn,
    kGlobalAccess,
    kInstanceOf,
    kNamedAccess,
  };
  Kind kind() const { return kind_; }

  bool IsInsufficient() const { return kind() == kInsufficient; }

  BinaryOperationFeedback const* AsBinaryOperation() const;
  CallFeedback const* AsCall() const;
  CompareOperationFeedback const* AsCompareOperation() const;
  ElementAccessFeedback const* AsElementAccess() const;
  ForInFeedback const* AsForIn() const;
  InstanceOfFeedback const* AsInstanceOf() const;
  NamedAccessFeedback const* AsNamedAccess() const;

 protected:
  explicit ProcessedFeedback(Kind kind) : kind_(kind) {}

 private:
  Kind const kind_;
};

class InsufficientFeedback final : public ProcessedFeedback {
 public:
  InsufficientFeedback();
};

class GlobalAccessFeedback : public ProcessedFeedback {
 public:
  explicit GlobalAccessFeedback(PropertyCellRef cell);
  GlobalAccessFeedback(ContextRef script_context, int slot_index,
                       bool immutable);

  bool IsPropertyCell() const;
  PropertyCellRef property_cell() const;

  bool IsScriptContextSlot() const { return !IsPropertyCell(); }
  ContextRef script_context() const;
  int slot_index() const;
  bool immutable() const;

  base::Optional<ObjectRef> GetConstantHint() const;

 private:
  ObjectRef const cell_or_context_;
  int const index_and_immutable_;
};

class KeyedAccessMode {
 public:
  static KeyedAccessMode FromNexus(FeedbackNexus const& nexus);

  AccessMode access_mode() const;
  bool IsLoad() const;
  bool IsStore() const;
  KeyedAccessLoadMode load_mode() const;
  KeyedAccessStoreMode store_mode() const;

 private:
  AccessMode const access_mode_;
  union LoadStoreMode {
    LoadStoreMode(KeyedAccessLoadMode load_mode);
    LoadStoreMode(KeyedAccessStoreMode store_mode);
    KeyedAccessLoadMode load_mode;
    KeyedAccessStoreMode store_mode;
  } const load_store_mode_;

  KeyedAccessMode(AccessMode access_mode, KeyedAccessLoadMode load_mode);
  KeyedAccessMode(AccessMode access_mode, KeyedAccessStoreMode store_mode);
};

class ElementAccessFeedback : public ProcessedFeedback {
 public:
  ElementAccessFeedback(Zone* zone, KeyedAccessMode const& keyed_mode);

  // No transition sources appear in {receiver_maps}.
  // All transition targets appear in {receiver_maps}.
  ZoneVector<Handle<Map>> receiver_maps;
  ZoneVector<std::pair<Handle<Map>, Handle<Map>>> transitions;

  KeyedAccessMode const keyed_mode;

  class MapIterator {
   public:
    bool done() const;
    void advance();
    MapRef current() const;

   private:
    friend class ElementAccessFeedback;

    explicit MapIterator(ElementAccessFeedback const& processed,
                         JSHeapBroker* broker);

    ElementAccessFeedback const& processed_;
    JSHeapBroker* const broker_;
    size_t index_ = 0;
  };

  // Iterator over all maps: first {receiver_maps}, then transition sources.
  MapIterator all_maps(JSHeapBroker* broker) const;
};

class NamedAccessFeedback : public ProcessedFeedback {
 public:
  NamedAccessFeedback(NameRef const& name,
                      ZoneVector<PropertyAccessInfo> const& access_infos);

  NameRef const& name() const { return name_; }
  ZoneVector<PropertyAccessInfo> const& access_infos() const {
    return access_infos_;
  }

 private:
  NameRef const name_;
  ZoneVector<PropertyAccessInfo> const access_infos_;
};

class CallFeedback : public ProcessedFeedback {
 public:
  CallFeedback(base::Optional<HeapObjectRef> target, float frequency,
               SpeculationMode mode)
      : ProcessedFeedback(kCall),
        target_(target),
        frequency_(frequency),
        mode_(mode) {}

  base::Optional<HeapObjectRef> target() const { return target_; }
  float frequency() const { return frequency_; }
  SpeculationMode speculation_mode() const { return mode_; }

 private:
  base::Optional<HeapObjectRef> const target_;
  float const frequency_;
  SpeculationMode const mode_;
};

template <class T, ProcessedFeedback::Kind K>
class SingleValueFeedback : public ProcessedFeedback {
 public:
  explicit SingleValueFeedback(T value) : ProcessedFeedback(K), value_(value) {}

  T value() const { return value_; }

 private:
  T const value_;
};

class InstanceOfFeedback
    : public SingleValueFeedback<base::Optional<JSObjectRef>,
                                 ProcessedFeedback::kInstanceOf> {
  using SingleValueFeedback::SingleValueFeedback;
};

class BinaryOperationFeedback
    : public SingleValueFeedback<BinaryOperationHint,
                                 ProcessedFeedback::kBinaryOperation> {
  using SingleValueFeedback::SingleValueFeedback;
};

class CompareOperationFeedback
    : public SingleValueFeedback<CompareOperationHint,
                                 ProcessedFeedback::kCompareOperation> {
  using SingleValueFeedback::SingleValueFeedback;
};

class ForInFeedback
    : public SingleValueFeedback<ForInHint, ProcessedFeedback::kForIn> {
  using SingleValueFeedback::SingleValueFeedback;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_PROCESSED_FEEDBACK_H_
