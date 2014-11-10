// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "src/v8.h"

#include "src/layout-descriptor.h"

namespace v8 {
namespace internal {

Handle<LayoutDescriptor> LayoutDescriptor::New(
    Handle<Map> map, Handle<DescriptorArray> descriptors, int num_descriptors) {
  Isolate* isolate = descriptors->GetIsolate();
  if (!FLAG_unbox_double_fields) return handle(FastPointerLayout(), isolate);

  int inobject_properties = map->inobject_properties();
  if (inobject_properties == 0) return handle(FastPointerLayout(), isolate);

  DCHECK(num_descriptors <= descriptors->number_of_descriptors());

  int layout_descriptor_length;
  const int kMaxWordsPerField = kDoubleSize / kPointerSize;

  if (num_descriptors <= kSmiValueSize / kMaxWordsPerField) {
    // Even in the "worst" case (all fields are doubles) it would fit into
    // a Smi, so no need to calculate length.
    layout_descriptor_length = kSmiValueSize;

  } else {
    layout_descriptor_length = 0;

    for (int i = 0; i < num_descriptors; i++) {
      PropertyDetails details = descriptors->GetDetails(i);
      if (!InobjectUnboxedField(inobject_properties, details)) continue;
      int field_index = details.field_index();
      int field_width_in_words = details.field_width_in_words();
      layout_descriptor_length =
          Max(layout_descriptor_length, field_index + field_width_in_words);
    }

    if (layout_descriptor_length == 0) {
      // No double fields were found, use fast pointer layout.
      return handle(FastPointerLayout(), isolate);
    }
  }
  layout_descriptor_length = Min(layout_descriptor_length, inobject_properties);

  // Initially, layout descriptor corresponds to an object with all fields
  // tagged.
  Handle<LayoutDescriptor> layout_descriptor_handle =
      LayoutDescriptor::New(isolate, layout_descriptor_length);

  DisallowHeapAllocation no_allocation;
  LayoutDescriptor* layout_descriptor = *layout_descriptor_handle;

  for (int i = 0; i < num_descriptors; i++) {
    PropertyDetails details = descriptors->GetDetails(i);
    if (!InobjectUnboxedField(inobject_properties, details)) continue;
    int field_index = details.field_index();
    layout_descriptor = layout_descriptor->SetRawData(field_index);
    if (details.field_width_in_words() > 1) {
      layout_descriptor = layout_descriptor->SetRawData(field_index + 1);
    }
  }
  return handle(layout_descriptor, isolate);
}


Handle<LayoutDescriptor> LayoutDescriptor::Append(Handle<Map> map,
                                                  PropertyDetails details) {
  Handle<LayoutDescriptor> layout_descriptor = map->GetLayoutDescriptor();

  if (!InobjectUnboxedField(map->inobject_properties(), details)) {
    return layout_descriptor;
  }
  Isolate* isolate = map->GetIsolate();
  int field_index = details.field_index();
  layout_descriptor = LayoutDescriptor::EnsureCapacity(
      isolate, layout_descriptor, field_index + details.field_width_in_words());

  DisallowHeapAllocation no_allocation;
  LayoutDescriptor* layout_desc = *layout_descriptor;
  layout_desc = layout_desc->SetRawData(field_index);
  if (details.field_width_in_words() > 1) {
    layout_desc = layout_desc->SetRawData(field_index + 1);
  }
  return handle(layout_desc, isolate);
}


Handle<LayoutDescriptor> LayoutDescriptor::AppendIfFastOrUseFull(
    Handle<Map> map, PropertyDetails details,
    Handle<LayoutDescriptor> full_layout_descriptor) {
  DisallowHeapAllocation no_allocation;
  LayoutDescriptor* layout_descriptor = map->layout_descriptor();
  if (layout_descriptor->IsSlowLayout()) {
    return full_layout_descriptor;
  }
  if (!InobjectUnboxedField(map->inobject_properties(), details)) {
    return handle(layout_descriptor, map->GetIsolate());
  }
  int field_index = details.field_index();
  int new_capacity = field_index + details.field_width_in_words();
  if (new_capacity > layout_descriptor->capacity()) {
    // Current map's layout descriptor runs out of space, so use the full
    // layout descriptor.
    return full_layout_descriptor;
  }

  layout_descriptor = layout_descriptor->SetRawData(field_index);
  if (details.field_width_in_words() > 1) {
    layout_descriptor = layout_descriptor->SetRawData(field_index + 1);
  }
  return handle(layout_descriptor, map->GetIsolate());
}


Handle<LayoutDescriptor> LayoutDescriptor::EnsureCapacity(
    Isolate* isolate, Handle<LayoutDescriptor> layout_descriptor,
    int new_capacity) {
  int old_capacity = layout_descriptor->capacity();
  if (new_capacity <= old_capacity) {
    // Nothing to do with layout in Smi-form.
    return layout_descriptor;
  }
  Handle<LayoutDescriptor> new_layout_descriptor =
      LayoutDescriptor::New(isolate, new_capacity);
  DCHECK(new_layout_descriptor->IsSlowLayout());

  if (layout_descriptor->IsSlowLayout()) {
    memcpy(new_layout_descriptor->DataPtr(), layout_descriptor->DataPtr(),
           layout_descriptor->DataSize());
    return new_layout_descriptor;
  } else {
    // Fast layout.
    uint32_t value =
        static_cast<uint32_t>(Smi::cast(*layout_descriptor)->value());
    new_layout_descriptor->set(0, value);
    return new_layout_descriptor;
  }
}
}
}  // namespace v8::internal
