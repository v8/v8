// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <utility>

#include "src/v8.h"

#include "src/compilation-cache.h"
#include "src/execution.h"
#include "src/factory.h"
#include "src/global-handles.h"
#include "src/ic/ic.h"
#include "src/macro-assembler.h"
#include "test/cctest/cctest.h"

using namespace v8::base;
using namespace v8::internal;

#if (V8_DOUBLE_FIELDS_UNBOXING)


static double GetDoubleFieldValue(JSObject* obj, FieldIndex field_index) {
  if (obj->IsUnboxedDoubleField(field_index)) {
    return obj->RawFastDoublePropertyAt(field_index);
  } else {
    Object* value = obj->RawFastPropertyAt(field_index);
    DCHECK(value->IsMutableHeapNumber());
    return HeapNumber::cast(value)->value();
  }
}


enum PropertyKind {
  PROP_CONSTANT,
  PROP_SMI,
  PROP_DOUBLE,
  PROP_TAGGED,
  PROP_KIND_NUMBER,
};

static Representation representations[PROP_KIND_NUMBER] = {
    Representation::None(), Representation::Smi(), Representation::Double(),
    Representation::Tagged()};


static Handle<DescriptorArray> CreateDescriptorArray(Isolate* isolate,
                                                     PropertyKind* props,
                                                     int kPropsCount) {
  Factory* factory = isolate->factory();

  Handle<String> func_name = factory->InternalizeUtf8String("func");
  Handle<JSFunction> func = factory->NewFunction(func_name);

  Handle<DescriptorArray> descriptors =
      DescriptorArray::Allocate(isolate, 0, kPropsCount);

  int next_field_offset = 0;
  for (int i = 0; i < kPropsCount; i++) {
    EmbeddedVector<char, 64> buffer;
    SNPrintF(buffer, "prop%d", i);
    Handle<String> name = factory->InternalizeUtf8String(buffer.start());

    PropertyKind kind = props[i];

    if (kind == PROP_CONSTANT) {
      ConstantDescriptor d(name, func, NONE);
      descriptors->Append(&d);

    } else {
      FieldDescriptor f(name, next_field_offset, NONE, representations[kind]);
      next_field_offset += f.GetDetails().field_width_in_words();
      descriptors->Append(&f);
    }
  }
  return descriptors;
}


TEST(LayoutDescriptorBasicFast) {
  CcTest::InitializeVM();
  v8::HandleScope scope(CcTest::isolate());

  LayoutDescriptor* layout_desc = LayoutDescriptor::FastPointerLayout();

  CHECK(!layout_desc->IsSlowLayout());
  CHECK(layout_desc->IsFastPointerLayout());
  CHECK_EQ(kSmiValueSize, layout_desc->capacity());

  for (int i = 0; i < kSmiValueSize + 13; i++) {
    CHECK_EQ(true, layout_desc->IsTagged(i));
  }
  CHECK_EQ(true, layout_desc->IsTagged(-1));
  CHECK_EQ(true, layout_desc->IsTagged(-12347));
  CHECK_EQ(true, layout_desc->IsTagged(15635));
  CHECK(layout_desc->IsFastPointerLayout());

  for (int i = 0; i < kSmiValueSize; i++) {
    layout_desc = layout_desc->SetTaggedForTesting(i, false);
    CHECK_EQ(false, layout_desc->IsTagged(i));
    layout_desc = layout_desc->SetTaggedForTesting(i, true);
    CHECK_EQ(true, layout_desc->IsTagged(i));
  }
  CHECK(layout_desc->IsFastPointerLayout());
}


TEST(LayoutDescriptorBasicSlow) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  v8::HandleScope scope(CcTest::isolate());

  Handle<LayoutDescriptor> layout_descriptor;
  const int kPropsCount = kSmiValueSize * 3;
  PropertyKind props[kPropsCount];
  for (int i = 0; i < kPropsCount; i++) {
    // All properties tagged.
    props[i] = PROP_TAGGED;
  }

  {
    Handle<DescriptorArray> descriptors =
        CreateDescriptorArray(isolate, props, kPropsCount);

    Handle<Map> map = Map::Create(isolate, kPropsCount);

    layout_descriptor = LayoutDescriptor::New(map, descriptors, kPropsCount);
    CHECK_EQ(LayoutDescriptor::FastPointerLayout(), *layout_descriptor);
    CHECK_EQ(kSmiValueSize, layout_descriptor->capacity());
    map->InitializeDescriptors(*descriptors, *layout_descriptor);
    DCHECK(layout_descriptor->IsConsistentWithMap(*map));
  }

  props[0] = PROP_DOUBLE;
  props[kPropsCount - 1] = PROP_DOUBLE;

  Handle<DescriptorArray> descriptors =
      CreateDescriptorArray(isolate, props, kPropsCount);

  {
    int inobject_properties = kPropsCount - 1;
    Handle<Map> map = Map::Create(isolate, inobject_properties);

    // Should be fast as the only double property is the first one.
    layout_descriptor = LayoutDescriptor::New(map, descriptors, kPropsCount);
    CHECK_NE(LayoutDescriptor::FastPointerLayout(), *layout_descriptor);
    CHECK(!layout_descriptor->IsSlowLayout());
    CHECK(!layout_descriptor->IsFastPointerLayout());

    CHECK_EQ(false, layout_descriptor->IsTagged(0));
    for (int i = 1; i < kPropsCount; i++) {
      CHECK_EQ(true, layout_descriptor->IsTagged(i));
    }
    map->InitializeDescriptors(*descriptors, *layout_descriptor);
    DCHECK(layout_descriptor->IsConsistentWithMap(*map));
  }

  {
    int inobject_properties = kPropsCount;
    Handle<Map> map = Map::Create(isolate, inobject_properties);

    layout_descriptor = LayoutDescriptor::New(map, descriptors, kPropsCount);
    CHECK_NE(LayoutDescriptor::FastPointerLayout(), *layout_descriptor);
    CHECK(layout_descriptor->IsSlowLayout());
    CHECK(!layout_descriptor->IsFastPointerLayout());
    CHECK(layout_descriptor->capacity() > kSmiValueSize);

    CHECK_EQ(false, layout_descriptor->IsTagged(0));
    CHECK_EQ(false, layout_descriptor->IsTagged(kPropsCount - 1));
    for (int i = 1; i < kPropsCount - 1; i++) {
      CHECK_EQ(true, layout_descriptor->IsTagged(i));
    }

    map->InitializeDescriptors(*descriptors, *layout_descriptor);
    DCHECK(layout_descriptor->IsConsistentWithMap(*map));

    // Here we have truly slow layout descriptor, so play with the bits.
    CHECK_EQ(true, layout_descriptor->IsTagged(-1));
    CHECK_EQ(true, layout_descriptor->IsTagged(-12347));
    CHECK_EQ(true, layout_descriptor->IsTagged(15635));

    LayoutDescriptor* layout_desc = *layout_descriptor;
    // Play with the bits but leave it in consistent state with map at the end.
    for (int i = 1; i < kPropsCount - 1; i++) {
      layout_desc = layout_desc->SetTaggedForTesting(i, false);
      CHECK_EQ(false, layout_desc->IsTagged(i));
      layout_desc = layout_desc->SetTaggedForTesting(i, true);
      CHECK_EQ(true, layout_desc->IsTagged(i));
    }
    CHECK(layout_desc->IsSlowLayout());
    CHECK(!layout_desc->IsFastPointerLayout());

    DCHECK(layout_descriptor->IsConsistentWithMap(*map));
  }
}


TEST(LayoutDescriptorCreateNewFast) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  v8::HandleScope scope(CcTest::isolate());

  Handle<LayoutDescriptor> layout_descriptor;
  PropertyKind props[] = {
      PROP_CONSTANT,
      PROP_TAGGED,  // field #0
      PROP_CONSTANT,
      PROP_DOUBLE,  // field #1
      PROP_CONSTANT,
      PROP_TAGGED,  // field #2
      PROP_CONSTANT,
  };
  const int kPropsCount = arraysize(props);

  Handle<DescriptorArray> descriptors =
      CreateDescriptorArray(isolate, props, kPropsCount);

  {
    Handle<Map> map = Map::Create(isolate, 0);
    layout_descriptor = LayoutDescriptor::New(map, descriptors, kPropsCount);
    CHECK_EQ(LayoutDescriptor::FastPointerLayout(), *layout_descriptor);
    map->InitializeDescriptors(*descriptors, *layout_descriptor);
    DCHECK(layout_descriptor->IsConsistentWithMap(*map));
  }

  {
    Handle<Map> map = Map::Create(isolate, 1);
    layout_descriptor = LayoutDescriptor::New(map, descriptors, kPropsCount);
    CHECK_EQ(LayoutDescriptor::FastPointerLayout(), *layout_descriptor);
    map->InitializeDescriptors(*descriptors, *layout_descriptor);
    DCHECK(layout_descriptor->IsConsistentWithMap(*map));
  }

  {
    Handle<Map> map = Map::Create(isolate, 2);
    layout_descriptor = LayoutDescriptor::New(map, descriptors, kPropsCount);
    CHECK_NE(LayoutDescriptor::FastPointerLayout(), *layout_descriptor);
    CHECK(!layout_descriptor->IsSlowLayout());
    CHECK_EQ(true, layout_descriptor->IsTagged(0));
    CHECK_EQ(false, layout_descriptor->IsTagged(1));
    CHECK_EQ(true, layout_descriptor->IsTagged(2));
    CHECK_EQ(true, layout_descriptor->IsTagged(125));
    map->InitializeDescriptors(*descriptors, *layout_descriptor);
    DCHECK(layout_descriptor->IsConsistentWithMap(*map));
  }
}


TEST(LayoutDescriptorCreateNewSlow) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  v8::HandleScope scope(CcTest::isolate());

  Handle<LayoutDescriptor> layout_descriptor;
  const int kPropsCount = kSmiValueSize * 3;
  PropertyKind props[kPropsCount];
  for (int i = 0; i < kPropsCount; i++) {
    props[i] = static_cast<PropertyKind>(i % PROP_KIND_NUMBER);
  }

  Handle<DescriptorArray> descriptors =
      CreateDescriptorArray(isolate, props, kPropsCount);

  {
    Handle<Map> map = Map::Create(isolate, 0);
    layout_descriptor = LayoutDescriptor::New(map, descriptors, kPropsCount);
    CHECK_EQ(LayoutDescriptor::FastPointerLayout(), *layout_descriptor);
    map->InitializeDescriptors(*descriptors, *layout_descriptor);
    DCHECK(layout_descriptor->IsConsistentWithMap(*map));
  }

  {
    Handle<Map> map = Map::Create(isolate, 1);
    layout_descriptor = LayoutDescriptor::New(map, descriptors, kPropsCount);
    CHECK_EQ(LayoutDescriptor::FastPointerLayout(), *layout_descriptor);
    map->InitializeDescriptors(*descriptors, *layout_descriptor);
    DCHECK(layout_descriptor->IsConsistentWithMap(*map));
  }

  {
    Handle<Map> map = Map::Create(isolate, 2);
    layout_descriptor = LayoutDescriptor::New(map, descriptors, kPropsCount);
    CHECK_NE(LayoutDescriptor::FastPointerLayout(), *layout_descriptor);
    CHECK(!layout_descriptor->IsSlowLayout());
    CHECK_EQ(true, layout_descriptor->IsTagged(0));
    CHECK_EQ(false, layout_descriptor->IsTagged(1));
    CHECK_EQ(true, layout_descriptor->IsTagged(2));
    CHECK_EQ(true, layout_descriptor->IsTagged(125));
    map->InitializeDescriptors(*descriptors, *layout_descriptor);
    DCHECK(layout_descriptor->IsConsistentWithMap(*map));
  }

  {
    int inobject_properties = kPropsCount / 2;
    Handle<Map> map = Map::Create(isolate, inobject_properties);
    layout_descriptor = LayoutDescriptor::New(map, descriptors, kPropsCount);
    CHECK_NE(LayoutDescriptor::FastPointerLayout(), *layout_descriptor);
    CHECK(layout_descriptor->IsSlowLayout());
    for (int i = 0; i < inobject_properties; i++) {
      // PROP_DOUBLE has index 1 among FIELD properties.
      const bool tagged = (i % (PROP_KIND_NUMBER - 1)) != 1;
      CHECK_EQ(tagged, layout_descriptor->IsTagged(i));
    }
    // Every property after inobject_properties must be tagged.
    for (int i = inobject_properties; i < kPropsCount; i++) {
      CHECK_EQ(true, layout_descriptor->IsTagged(i));
    }
    map->InitializeDescriptors(*descriptors, *layout_descriptor);
    DCHECK(layout_descriptor->IsConsistentWithMap(*map));

    // Now test LayoutDescriptor::cast_gc_safe().
    Handle<LayoutDescriptor> layout_descriptor_copy =
        LayoutDescriptor::New(map, descriptors, kPropsCount);

    LayoutDescriptor* layout_desc = *layout_descriptor;
    CHECK_EQ(layout_desc, LayoutDescriptor::cast(layout_desc));
    CHECK_EQ(layout_desc, LayoutDescriptor::cast_gc_safe(layout_desc));
    CHECK(layout_descriptor->IsFixedTypedArrayBase());
    // Now make it look like a forwarding pointer to layout_descriptor_copy.
    MapWord map_word = layout_desc->map_word();
    CHECK(!map_word.IsForwardingAddress());
    layout_desc->set_map_word(
        MapWord::FromForwardingAddress(*layout_descriptor_copy));
    CHECK(layout_desc->map_word().IsForwardingAddress());
    CHECK_EQ(*layout_descriptor_copy,
             LayoutDescriptor::cast_gc_safe(layout_desc));

    // Restore it back.
    layout_desc->set_map_word(map_word);
    CHECK_EQ(layout_desc, LayoutDescriptor::cast(layout_desc));
  }
}


static Handle<LayoutDescriptor> TestLayoutDescriptorAppend(
    Isolate* isolate, int inobject_properties, PropertyKind* props,
    int kPropsCount) {
  Factory* factory = isolate->factory();

  Handle<String> func_name = factory->InternalizeUtf8String("func");
  Handle<JSFunction> func = factory->NewFunction(func_name);

  Handle<DescriptorArray> descriptors =
      DescriptorArray::Allocate(isolate, 0, kPropsCount);

  Handle<Map> map = Map::Create(isolate, inobject_properties);
  map->InitializeDescriptors(*descriptors,
                             LayoutDescriptor::FastPointerLayout());

  int next_field_offset = 0;
  for (int i = 0; i < kPropsCount; i++) {
    EmbeddedVector<char, 64> buffer;
    SNPrintF(buffer, "prop%d", i);
    Handle<String> name = factory->InternalizeUtf8String(buffer.start());

    Handle<LayoutDescriptor> layout_descriptor;
    PropertyKind kind = props[i];
    if (kind == PROP_CONSTANT) {
      ConstantDescriptor d(name, func, NONE);
      layout_descriptor = LayoutDescriptor::Append(map, d.GetDetails());
      descriptors->Append(&d);

    } else {
      FieldDescriptor f(name, next_field_offset, NONE, representations[kind]);
      int field_width_in_words = f.GetDetails().field_width_in_words();
      next_field_offset += field_width_in_words;
      layout_descriptor = LayoutDescriptor::Append(map, f.GetDetails());
      descriptors->Append(&f);

      int field_index = f.GetDetails().field_index();
      bool is_inobject = field_index < map->inobject_properties();
      for (int bit = 0; bit < field_width_in_words; bit++) {
        CHECK_EQ(is_inobject && (kind == PROP_DOUBLE),
                 !layout_descriptor->IsTagged(field_index + bit));
      }
      CHECK(layout_descriptor->IsTagged(next_field_offset));
    }
    map->InitializeDescriptors(*descriptors, *layout_descriptor);
  }
  Handle<LayoutDescriptor> layout_descriptor(map->layout_descriptor(), isolate);
  DCHECK(layout_descriptor->IsConsistentWithMap(*map));
  return layout_descriptor;
}


TEST(LayoutDescriptorAppend) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  v8::HandleScope scope(CcTest::isolate());

  Handle<LayoutDescriptor> layout_descriptor;
  const int kPropsCount = kSmiValueSize * 3;
  PropertyKind props[kPropsCount];
  for (int i = 0; i < kPropsCount; i++) {
    props[i] = static_cast<PropertyKind>(i % PROP_KIND_NUMBER);
  }

  layout_descriptor =
      TestLayoutDescriptorAppend(isolate, 0, props, kPropsCount);
  CHECK(!layout_descriptor->IsSlowLayout());

  layout_descriptor =
      TestLayoutDescriptorAppend(isolate, 13, props, kPropsCount);
  CHECK(!layout_descriptor->IsSlowLayout());

  layout_descriptor =
      TestLayoutDescriptorAppend(isolate, kSmiValueSize, props, kPropsCount);
  CHECK(!layout_descriptor->IsSlowLayout());

  layout_descriptor = TestLayoutDescriptorAppend(isolate, kSmiValueSize * 2,
                                                 props, kPropsCount);
  CHECK(layout_descriptor->IsSlowLayout());

  layout_descriptor =
      TestLayoutDescriptorAppend(isolate, kPropsCount, props, kPropsCount);
  CHECK(layout_descriptor->IsSlowLayout());
}


TEST(LayoutDescriptorAppendAllDoubles) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  v8::HandleScope scope(CcTest::isolate());

  Handle<LayoutDescriptor> layout_descriptor;
  const int kPropsCount = kSmiValueSize * 3;
  PropertyKind props[kPropsCount];
  for (int i = 0; i < kPropsCount; i++) {
    props[i] = PROP_DOUBLE;
  }

  layout_descriptor =
      TestLayoutDescriptorAppend(isolate, 0, props, kPropsCount);
  CHECK(!layout_descriptor->IsSlowLayout());

  layout_descriptor =
      TestLayoutDescriptorAppend(isolate, 13, props, kPropsCount);
  CHECK(!layout_descriptor->IsSlowLayout());

  layout_descriptor =
      TestLayoutDescriptorAppend(isolate, kSmiValueSize, props, kPropsCount);
  CHECK(!layout_descriptor->IsSlowLayout());

  layout_descriptor = TestLayoutDescriptorAppend(isolate, kSmiValueSize + 1,
                                                 props, kPropsCount);
  CHECK(layout_descriptor->IsSlowLayout());

  layout_descriptor = TestLayoutDescriptorAppend(isolate, kSmiValueSize * 2,
                                                 props, kPropsCount);
  CHECK(layout_descriptor->IsSlowLayout());

  layout_descriptor =
      TestLayoutDescriptorAppend(isolate, kPropsCount, props, kPropsCount);
  CHECK(layout_descriptor->IsSlowLayout());

  {
    // Ensure layout descriptor switches into slow mode at the right moment.
    layout_descriptor =
        TestLayoutDescriptorAppend(isolate, kPropsCount, props, kSmiValueSize);
    CHECK(!layout_descriptor->IsSlowLayout());

    layout_descriptor = TestLayoutDescriptorAppend(isolate, kPropsCount, props,
                                                   kSmiValueSize + 1);
    CHECK(layout_descriptor->IsSlowLayout());
  }
}


static Handle<LayoutDescriptor> TestLayoutDescriptorAppendIfFastOrUseFull(
    Isolate* isolate, int inobject_properties,
    Handle<DescriptorArray> descriptors, int number_of_descriptors) {
  Handle<Map> map = Map::Create(isolate, inobject_properties);

  Handle<LayoutDescriptor> full_layout_descriptor = LayoutDescriptor::New(
      map, descriptors, descriptors->number_of_descriptors());

  int nof = 0;
  bool switched_to_slow_mode = false;

  for (int i = 0; i < number_of_descriptors; i++) {
    PropertyDetails details = descriptors->GetDetails(i);

    // This method calls LayoutDescriptor::AppendIfFastOrUseFull() internally
    // and does all the required map-descriptors related book keeping.
    map = Map::CopyInstallDescriptorsForTesting(map, i, descriptors,
                                                full_layout_descriptor);

    LayoutDescriptor* layout_desc = map->layout_descriptor();

    if (layout_desc->IsSlowLayout()) {
      switched_to_slow_mode = true;
      CHECK_EQ(*full_layout_descriptor, layout_desc);
    } else {
      CHECK(!switched_to_slow_mode);
      if (details.type() == FIELD) {
        nof++;
        int field_index = details.field_index();
        int field_width_in_words = details.field_width_in_words();

        bool is_inobject = field_index < map->inobject_properties();
        for (int bit = 0; bit < field_width_in_words; bit++) {
          CHECK_EQ(is_inobject && details.representation().IsDouble(),
                   !layout_desc->IsTagged(field_index + bit));
        }
        CHECK(layout_desc->IsTagged(field_index + field_width_in_words));
      }
    }
    DCHECK(map->layout_descriptor()->IsConsistentWithMap(*map));
  }

  Handle<LayoutDescriptor> layout_descriptor(map->GetLayoutDescriptor(),
                                             isolate);
  DCHECK(layout_descriptor->IsConsistentWithMap(*map));
  return layout_descriptor;
}


TEST(LayoutDescriptorAppendIfFastOrUseFull) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  v8::HandleScope scope(CcTest::isolate());

  Handle<LayoutDescriptor> layout_descriptor;
  const int kPropsCount = kSmiValueSize * 3;
  PropertyKind props[kPropsCount];
  for (int i = 0; i < kPropsCount; i++) {
    props[i] = static_cast<PropertyKind>(i % PROP_KIND_NUMBER);
  }
  Handle<DescriptorArray> descriptors =
      CreateDescriptorArray(isolate, props, kPropsCount);

  layout_descriptor = TestLayoutDescriptorAppendIfFastOrUseFull(
      isolate, 0, descriptors, kPropsCount);
  CHECK(!layout_descriptor->IsSlowLayout());

  layout_descriptor = TestLayoutDescriptorAppendIfFastOrUseFull(
      isolate, 13, descriptors, kPropsCount);
  CHECK(!layout_descriptor->IsSlowLayout());

  layout_descriptor = TestLayoutDescriptorAppendIfFastOrUseFull(
      isolate, kSmiValueSize, descriptors, kPropsCount);
  CHECK(!layout_descriptor->IsSlowLayout());

  layout_descriptor = TestLayoutDescriptorAppendIfFastOrUseFull(
      isolate, kSmiValueSize * 2, descriptors, kPropsCount);
  CHECK(layout_descriptor->IsSlowLayout());

  layout_descriptor = TestLayoutDescriptorAppendIfFastOrUseFull(
      isolate, kPropsCount, descriptors, kPropsCount);
  CHECK(layout_descriptor->IsSlowLayout());
}


TEST(LayoutDescriptorAppendIfFastOrUseFullAllDoubles) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  v8::HandleScope scope(CcTest::isolate());

  Handle<LayoutDescriptor> layout_descriptor;
  const int kPropsCount = kSmiValueSize * 3;
  PropertyKind props[kPropsCount];
  for (int i = 0; i < kPropsCount; i++) {
    props[i] = PROP_DOUBLE;
  }
  Handle<DescriptorArray> descriptors =
      CreateDescriptorArray(isolate, props, kPropsCount);

  layout_descriptor = TestLayoutDescriptorAppendIfFastOrUseFull(
      isolate, 0, descriptors, kPropsCount);
  CHECK(!layout_descriptor->IsSlowLayout());

  layout_descriptor = TestLayoutDescriptorAppendIfFastOrUseFull(
      isolate, 13, descriptors, kPropsCount);
  CHECK(!layout_descriptor->IsSlowLayout());

  layout_descriptor = TestLayoutDescriptorAppendIfFastOrUseFull(
      isolate, kSmiValueSize, descriptors, kPropsCount);
  CHECK(!layout_descriptor->IsSlowLayout());

  layout_descriptor = TestLayoutDescriptorAppendIfFastOrUseFull(
      isolate, kSmiValueSize + 1, descriptors, kPropsCount);
  CHECK(layout_descriptor->IsSlowLayout());

  layout_descriptor = TestLayoutDescriptorAppendIfFastOrUseFull(
      isolate, kSmiValueSize * 2, descriptors, kPropsCount);
  CHECK(layout_descriptor->IsSlowLayout());

  layout_descriptor = TestLayoutDescriptorAppendIfFastOrUseFull(
      isolate, kPropsCount, descriptors, kPropsCount);
  CHECK(layout_descriptor->IsSlowLayout());

  {
    // Ensure layout descriptor switches into slow mode at the right moment.
    layout_descriptor = TestLayoutDescriptorAppendIfFastOrUseFull(
        isolate, kPropsCount, descriptors, kSmiValueSize);
    CHECK(!layout_descriptor->IsSlowLayout());

    layout_descriptor = TestLayoutDescriptorAppendIfFastOrUseFull(
        isolate, kPropsCount, descriptors, kSmiValueSize + 1);
    CHECK(layout_descriptor->IsSlowLayout());
  }
}


TEST(StoreBufferScanOnScavenge) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  v8::HandleScope scope(CcTest::isolate());

  CompileRun(
      "function A() {"
      "  this.x = 42.5;"
      "  this.o = {};"
      "};"
      "var o = new A();");

  Handle<String> obj_name = factory->InternalizeUtf8String("o");

  Handle<Object> obj_value =
      Object::GetProperty(isolate->global_object(), obj_name).ToHandleChecked();
  CHECK(obj_value->IsJSObject());
  Handle<JSObject> obj = Handle<JSObject>::cast(obj_value);

  {
    // Ensure the object is properly set up.
    Map* map = obj->map();
    DescriptorArray* descriptors = map->instance_descriptors();
    CHECK(map->NumberOfOwnDescriptors() == 2);
    CHECK(descriptors->GetDetails(0).representation().IsDouble());
    CHECK(descriptors->GetDetails(1).representation().IsHeapObject());
    FieldIndex field_index = FieldIndex::ForDescriptor(map, 0);
    CHECK(field_index.is_inobject() && field_index.is_double());
    CHECK_EQ(FLAG_unbox_double_fields, map->IsUnboxedDoubleField(field_index));
    CHECK_EQ(42.5, GetDoubleFieldValue(*obj, field_index));
  }
  CHECK(isolate->heap()->new_space()->Contains(*obj));

  // Trigger GCs so that the newly allocated object moves to old gen.
  CcTest::heap()->CollectGarbage(i::NEW_SPACE);  // in survivor space now
  CcTest::heap()->CollectGarbage(i::NEW_SPACE);  // in old gen now

  CHECK(isolate->heap()->old_pointer_space()->Contains(*obj));

  // Create temp object in the new space.
  Handle<JSArray> temp = factory->NewJSArray(FAST_ELEMENTS, NOT_TENURED);
  CHECK(isolate->heap()->new_space()->Contains(*temp));

  // Construct a double value that looks like a pointer to the new space object
  // and store it into the obj.
  Address fake_object = reinterpret_cast<Address>(*temp) + kPointerSize;
  double boom_value = bit_cast<double>(fake_object);

  FieldIndex field_index = FieldIndex::ForDescriptor(obj->map(), 0);
  obj->FastPropertyAtPut(field_index,
                         *factory->NewHeapNumber(boom_value, MUTABLE));

  // Enforce scan on scavenge for the obj's page.
  MemoryChunk* chunk = MemoryChunk::FromAddress(obj->address());
  chunk->set_scan_on_scavenge(true);

  // Trigger GCs and force evacuation. Should not crash there.
  CcTest::heap()->CollectAllGarbage(i::Heap::kNoGCFlags);

  CHECK_EQ(boom_value, GetDoubleFieldValue(*obj, field_index));
}

#endif
