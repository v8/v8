// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/code-stub-assembler.h"
#include "src/macro-assembler.h"
#include "src/runtime/runtime.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// Interrupt and stack checks.

void Builtins::Generate_InterruptCheck(MacroAssembler* masm) {
  masm->TailCallRuntime(Runtime::kInterrupt);
}

void Builtins::Generate_StackCheck(MacroAssembler* masm) {
  masm->TailCallRuntime(Runtime::kStackGuard);
}

// -----------------------------------------------------------------------------
// TurboFan support builtins.

TF_BUILTIN(CopyFastSmiOrObjectElements, CodeStubAssembler) {
  Node* object = Parameter(Descriptor::kObject);

  // Load the {object}s elements.
  Node* source = LoadObjectField(object, JSObject::kElementsOffset);

  ParameterMode mode = OptimalParameterMode();
  Node* length = TaggedToParameter(LoadFixedArrayBaseLength(source), mode);

  // Check if we can allocate in new space.
  ElementsKind kind = FAST_ELEMENTS;
  int max_elements = FixedArrayBase::GetMaxLengthForNewSpaceAllocation(kind);
  Label if_newspace(this), if_oldspace(this);
  Branch(UintPtrOrSmiLessThan(length, IntPtrOrSmiConstant(max_elements, mode),
                              mode),
         &if_newspace, &if_oldspace);

  BIND(&if_newspace);
  {
    Node* target = AllocateFixedArray(kind, length, mode);
    CopyFixedArrayElements(kind, source, target, length, SKIP_WRITE_BARRIER,
                           mode);
    StoreObjectField(object, JSObject::kElementsOffset, target);
    Return(target);
  }

  BIND(&if_oldspace);
  {
    Node* target = AllocateFixedArray(kind, length, mode, kPretenured);
    CopyFixedArrayElements(kind, source, target, length, UPDATE_WRITE_BARRIER,
                           mode);
    StoreObjectField(object, JSObject::kElementsOffset, target);
    Return(target);
  }
}

TF_BUILTIN(GrowFastDoubleElements, CodeStubAssembler) {
  Node* object = Parameter(Descriptor::kObject);
  Node* key = Parameter(Descriptor::kKey);
  Node* context = Parameter(Descriptor::kContext);

  Label runtime(this, Label::kDeferred);
  Node* elements = LoadElements(object);
  elements = TryGrowElementsCapacity(object, elements, FAST_DOUBLE_ELEMENTS,
                                     key, &runtime);
  Return(elements);

  BIND(&runtime);
  TailCallRuntime(Runtime::kGrowArrayElements, context, object, key);
}

TF_BUILTIN(GrowFastSmiOrObjectElements, CodeStubAssembler) {
  Node* object = Parameter(Descriptor::kObject);
  Node* key = Parameter(Descriptor::kKey);
  Node* context = Parameter(Descriptor::kContext);

  Label runtime(this, Label::kDeferred);
  Node* elements = LoadElements(object);
  elements =
      TryGrowElementsCapacity(object, elements, FAST_ELEMENTS, key, &runtime);
  Return(elements);

  BIND(&runtime);
  TailCallRuntime(Runtime::kGrowArrayElements, context, object, key);
}

TF_BUILTIN(NewUnmappedArgumentsElements, CodeStubAssembler) {
  Node* frame = Parameter(Descriptor::kFrame);
  Node* length = SmiToWord(Parameter(Descriptor::kLength));

  // Check if we can allocate in new space.
  ElementsKind kind = FAST_ELEMENTS;
  int max_elements = FixedArray::GetMaxLengthForNewSpaceAllocation(kind);
  Label if_newspace(this), if_oldspace(this, Label::kDeferred);
  Branch(IntPtrLessThan(length, IntPtrConstant(max_elements)), &if_newspace,
         &if_oldspace);

  BIND(&if_newspace);
  {
    // Prefer EmptyFixedArray in case of non-positive {length} (the {length}
    // can be negative here for rest parameters).
    Label if_empty(this), if_notempty(this);
    Branch(IntPtrLessThanOrEqual(length, IntPtrConstant(0)), &if_empty,
           &if_notempty);

    BIND(&if_empty);
    Return(EmptyFixedArrayConstant());

    BIND(&if_notempty);
    {
      // Allocate a FixedArray in new space.
      Node* result = AllocateFixedArray(kind, length);

      // Compute the effective {offset} into the {frame}.
      Node* offset = IntPtrAdd(length, IntPtrConstant(1));

      // Copy the parameters from {frame} (starting at {offset}) to {result}.
      VARIABLE(var_index, MachineType::PointerRepresentation());
      Label loop(this, &var_index), done_loop(this);
      var_index.Bind(IntPtrConstant(0));
      Goto(&loop);
      BIND(&loop);
      {
        // Load the current {index}.
        Node* index = var_index.value();

        // Check if we are done.
        GotoIf(WordEqual(index, length), &done_loop);

        // Load the parameter at the given {index}.
        Node* value = Load(MachineType::AnyTagged(), frame,
                           WordShl(IntPtrSub(offset, index),
                                   IntPtrConstant(kPointerSizeLog2)));

        // Store the {value} into the {result}.
        StoreFixedArrayElement(result, index, value, SKIP_WRITE_BARRIER);

        // Continue with next {index}.
        var_index.Bind(IntPtrAdd(index, IntPtrConstant(1)));
        Goto(&loop);
      }

      BIND(&done_loop);
      Return(result);
    }
  }

  BIND(&if_oldspace);
  {
    // Allocate in old space (or large object space).
    TailCallRuntime(Runtime::kNewArgumentsElements, NoContextConstant(),
                    BitcastWordToTagged(frame), SmiFromWord(length));
  }
}

TF_BUILTIN(ReturnReceiver, CodeStubAssembler) {
  Return(Parameter(Descriptor::kReceiver));
}

class DeletePropertyBaseAssembler : public CodeStubAssembler {
 public:
  explicit DeletePropertyBaseAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  void DeleteFastProperty(Node* receiver, Node* receiver_map, Node* properties,
                          Node* name, Label* dont_delete, Label* not_found,
                          Label* slow) {
    // This builtin implements a special case for fast property deletion:
    // when the last property in an object is deleted, then instead of
    // normalizing the properties, we can undo the last map transition,
    // with a few prerequisites:
    // (1) The current map must not be marked stable. Otherwise there could
    // be optimized code that depends on the assumption that no object that
    // reached this map transitions away from it (without triggering the
    // "deoptimize dependent code" mechanism).
    Node* bitfield3 = LoadMapBitField3(receiver_map);
    GotoIfNot(IsSetWord32<Map::IsUnstable>(bitfield3), slow);
    // (2) The property to be deleted must be the last property.
    Node* descriptors = LoadMapDescriptors(receiver_map);
    Node* nof = DecodeWord32<Map::NumberOfOwnDescriptorsBits>(bitfield3);
    GotoIf(Word32Equal(nof, Int32Constant(0)), not_found);
    Node* descriptor_number = Int32Sub(nof, Int32Constant(1));
    Node* key_index = DescriptorArrayToKeyIndex(descriptor_number);
    Node* actual_key = LoadFixedArrayElement(descriptors, key_index);
    // TODO(jkummerow): We could implement full descriptor search in order
    // to avoid the runtime call for deleting nonexistent properties, but
    // that's probably a rare case.
    GotoIf(WordNotEqual(actual_key, name), slow);
    // (3) The property to be deleted must be deletable.
    Node* details =
        LoadDetailsByKeyIndex<DescriptorArray>(descriptors, key_index);
    GotoIf(IsSetWord32(details, PropertyDetails::kAttributesDontDeleteMask),
           dont_delete);
    // (4) The map must have a back pointer.
    Node* backpointer =
        LoadObjectField(receiver_map, Map::kConstructorOrBackPointerOffset);
    GotoIfNot(IsMap(backpointer), slow);
    // (5) The last transition must have been caused by adding a property
    // (and not any kind of special transition).
    Node* previous_nof = DecodeWord32<Map::NumberOfOwnDescriptorsBits>(
        LoadMapBitField3(backpointer));
    GotoIfNot(Word32Equal(previous_nof, descriptor_number), slow);

    // Preconditions successful, perform the map rollback!
    // Zap the property to avoid keeping objects alive.
    // Zapping is not necessary for properties stored in the descriptor array.
    Label zapping_done(this);
    GotoIf(Word32NotEqual(DecodeWord32<PropertyDetails::LocationField>(details),
                          Int32Constant(kField)),
           &zapping_done);
    Node* field_index =
        DecodeWordFromWord32<PropertyDetails::FieldIndexField>(details);
    Node* inobject_properties = LoadMapInobjectProperties(receiver_map);
    Label inobject(this), backing_store(this);
    // Due to inobject slack tracking, a field currently within the object
    // could later be between objects. Use the one pointer filler map for
    // zapping the deleted field to make this safe.
    Node* filler = LoadRoot(Heap::kOnePointerFillerMapRootIndex);
    DCHECK(Heap::RootIsImmortalImmovable(Heap::kOnePointerFillerMapRootIndex));
    Branch(UintPtrLessThan(field_index, inobject_properties), &inobject,
           &backing_store);
    BIND(&inobject);
    {
      Node* field_offset =
          IntPtrMul(IntPtrSub(LoadMapInstanceSize(receiver_map),
                              IntPtrSub(inobject_properties, field_index)),
                    IntPtrConstant(kPointerSize));
      StoreObjectFieldNoWriteBarrier(receiver, field_offset, filler);
      Goto(&zapping_done);
    }
    BIND(&backing_store);
    {
      Node* backing_store_index = IntPtrSub(field_index, inobject_properties);
      StoreFixedArrayElement(properties, backing_store_index, filler,
                             SKIP_WRITE_BARRIER);
      Goto(&zapping_done);
    }
    BIND(&zapping_done);
    StoreMap(receiver, backpointer);
    Return(TrueConstant());
  }

  void DeleteDictionaryProperty(Node* receiver, Node* properties, Node* name,
                                Node* context, Label* dont_delete,
                                Label* notfound) {
    VARIABLE(var_name_index, MachineType::PointerRepresentation());
    Label dictionary_found(this, &var_name_index);
    NameDictionaryLookup<NameDictionary>(properties, name, &dictionary_found,
                                         &var_name_index, notfound);

    BIND(&dictionary_found);
    Node* key_index = var_name_index.value();
    Node* details =
        LoadDetailsByKeyIndex<NameDictionary>(properties, key_index);
    GotoIf(IsSetWord32(details, PropertyDetails::kAttributesDontDeleteMask),
           dont_delete);
    // Overwrite the entry itself (see NameDictionary::SetEntry).
    Node* filler = TheHoleConstant();
    DCHECK(Heap::RootIsImmortalImmovable(Heap::kTheHoleValueRootIndex));
    StoreFixedArrayElement(properties, key_index, filler, SKIP_WRITE_BARRIER);
    StoreValueByKeyIndex<NameDictionary>(properties, key_index, filler,
                                         SKIP_WRITE_BARRIER);
    StoreDetailsByKeyIndex<NameDictionary>(properties, key_index,
                                           SmiConstant(Smi::kZero));

    // Update bookkeeping information (see NameDictionary::ElementRemoved).
    Node* nof = GetNumberOfElements<NameDictionary>(properties);
    Node* new_nof = SmiSub(nof, SmiConstant(1));
    SetNumberOfElements<NameDictionary>(properties, new_nof);
    Node* num_deleted = GetNumberOfDeletedElements<NameDictionary>(properties);
    Node* new_deleted = SmiAdd(num_deleted, SmiConstant(1));
    SetNumberOfDeletedElements<NameDictionary>(properties, new_deleted);

    // Shrink the dictionary if necessary (see NameDictionary::Shrink).
    Label shrinking_done(this);
    Node* capacity = GetCapacity<NameDictionary>(properties);
    GotoIf(SmiGreaterThan(new_nof, SmiShr(capacity, 2)), &shrinking_done);
    GotoIf(SmiLessThan(new_nof, SmiConstant(16)), &shrinking_done);
    CallRuntime(Runtime::kShrinkPropertyDictionary, context, receiver, name);
    Goto(&shrinking_done);
    BIND(&shrinking_done);

    Return(TrueConstant());
  }
};

TF_BUILTIN(DeleteProperty, DeletePropertyBaseAssembler) {
  Node* receiver = Parameter(Descriptor::kObject);
  Node* key = Parameter(Descriptor::kKey);
  Node* language_mode = Parameter(Descriptor::kLanguageMode);
  Node* context = Parameter(Descriptor::kContext);

  VARIABLE(var_index, MachineType::PointerRepresentation());
  VARIABLE(var_unique, MachineRepresentation::kTagged, key);
  Label if_index(this), if_unique_name(this), if_notunique(this),
      if_notfound(this), slow(this);

  GotoIf(TaggedIsSmi(receiver), &slow);
  Node* receiver_map = LoadMap(receiver);
  Node* instance_type = LoadMapInstanceType(receiver_map);
  GotoIf(Int32LessThanOrEqual(instance_type,
                              Int32Constant(LAST_CUSTOM_ELEMENTS_RECEIVER)),
         &slow);
  TryToName(key, &if_index, &var_index, &if_unique_name, &var_unique, &slow,
            &if_notunique);

  BIND(&if_index);
  {
    Comment("integer index");
    Goto(&slow);  // TODO(jkummerow): Implement more smarts here.
  }

  BIND(&if_unique_name);
  {
    Comment("key is unique name");
    Node* unique = var_unique.value();
    CheckForAssociatedProtector(unique, &slow);

    Label dictionary(this), dont_delete(this);
    Node* properties = LoadProperties(receiver);
    Node* properties_map = LoadMap(properties);
    GotoIf(WordEqual(properties_map, LoadRoot(Heap::kHashTableMapRootIndex)),
           &dictionary);
    DeleteFastProperty(receiver, receiver_map, properties, unique, &dont_delete,
                       &if_notfound, &slow);

    BIND(&dictionary);
    {
      DeleteDictionaryProperty(receiver, properties, unique, context,
                               &dont_delete, &if_notfound);
    }

    BIND(&dont_delete);
    {
      STATIC_ASSERT(LANGUAGE_END == 2);
      GotoIf(SmiNotEqual(language_mode, SmiConstant(SLOPPY)), &slow);
      Return(FalseConstant());
    }
  }

  BIND(&if_notunique);
  {
    // If the string was not found in the string table, then no object can
    // have a property with that name.
    TryInternalizeString(key, &if_index, &var_index, &if_unique_name,
                         &var_unique, &if_notfound, &slow);
  }

  BIND(&if_notfound);
  Return(TrueConstant());

  BIND(&slow);
  {
    TailCallRuntime(Runtime::kDeleteProperty, context, receiver, key,
                    language_mode);
  }
}

}  // namespace internal
}  // namespace v8
