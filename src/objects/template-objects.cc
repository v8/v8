// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/template-objects.h"

#include "src/base/functional.h"
#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/objects-inl.h"
#include "src/objects/property-descriptor.h"
#include "src/objects/template-objects-inl.h"

namespace v8 {
namespace internal {

// static
Handle<JSArray> TemplateObjectDescription::GetTemplateObject(
    Isolate* isolate, Handle<NativeContext> native_context,
    Handle<TemplateObjectDescription> description,
    Handle<SharedFunctionInfo> shared_info, int slot_id) {
  uint32_t hash = shared_info->Hash();
  int function_literal_id = shared_info->function_literal_id();

  // Check the template weakmap to see if the template object already exists.
  Handle<EphemeronHashTable> template_weakmap;
  Handle<Script> script(Script::cast(shared_info->script(isolate)), isolate);
  MaybeHandle<CachedTemplateObject> existing_cached_template;

  if (native_context->template_weakmap().IsUndefined(isolate)) {
    template_weakmap = EphemeronHashTable::New(isolate, 1);
  } else {
    DisallowGarbageCollection no_gc;
    ReadOnlyRoots roots(isolate);
    template_weakmap = handle(
        EphemeronHashTable::cast(native_context->template_weakmap()), isolate);
    Object maybe_cached_template =
        template_weakmap->Lookup(isolate, script, hash);
    while (!maybe_cached_template.IsTheHole(roots)) {
      CachedTemplateObject cached_template =
          CachedTemplateObject::cast(maybe_cached_template);
      if (cached_template.function_literal_id() == function_literal_id &&
          cached_template.slot_id() == slot_id) {
        HeapObject template_object;
        if (!cached_template.template_object(isolate).GetHeapObject(
                &template_object)) {
          // If the existing cached template is a cleared ref, update it
          // in-place.
          existing_cached_template = handle(cached_template, isolate);
          break;
        }
        return handle(JSArray::cast(template_object), isolate);
      }
      // TODO(leszeks): Clean up entries with cleared object refs.
      maybe_cached_template = cached_template.next();
    }
  }

  // Create the raw object from the {raw_strings}.
  Handle<FixedArray> raw_strings(description->raw_strings(), isolate);
  Handle<JSArray> raw_object = isolate->factory()->NewJSArrayWithElements(
      raw_strings, PACKED_ELEMENTS, raw_strings->length(),
      AllocationType::kOld);

  // Create the template object from the {cooked_strings}.
  Handle<FixedArray> cooked_strings(description->cooked_strings(), isolate);
  Handle<JSArray> template_object = isolate->factory()->NewJSArrayWithElements(
      cooked_strings, PACKED_ELEMENTS, cooked_strings->length(),
      AllocationType::kOld);

  // Freeze the {raw_object}.
  JSObject::SetIntegrityLevel(raw_object, FROZEN, kThrowOnError).ToChecked();

  // Install a "raw" data property for {raw_object} on {template_object}.
  PropertyDescriptor raw_desc;
  raw_desc.set_value(raw_object);
  raw_desc.set_configurable(false);
  raw_desc.set_enumerable(false);
  raw_desc.set_writable(false);
  JSArray::DefineOwnProperty(isolate, template_object,
                             isolate->factory()->raw_string(), &raw_desc,
                             Just(kThrowOnError))
      .ToChecked();

  // Freeze the {template_object} as well.
  JSObject::SetIntegrityLevel(template_object, FROZEN, kThrowOnError)
      .ToChecked();

  // Insert the template object into the template weakmap.
  Handle<HeapObject> previous_cached_templates =
      handle(HeapObject::cast(template_weakmap->Lookup(script, hash)), isolate);
  Handle<CachedTemplateObject> cached_template;
  if (existing_cached_template.ToHandle(&cached_template)) {
    cached_template->set_template_object(
        HeapObjectReference::Weak(*template_object));
  } else {
    cached_template = isolate->factory()->NewCachedTemplateObject(
        function_literal_id, slot_id, previous_cached_templates,
        template_object);
  }
  template_weakmap = EphemeronHashTable::Put(isolate, template_weakmap, script,
                                             cached_template, hash);
  native_context->set_template_weakmap(*template_weakmap);

  return template_object;
}

}  // namespace internal
}  // namespace v8
