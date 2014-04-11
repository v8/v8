// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "accessors.h"
#include "api.h"
#include "arguments.h"
#include "bootstrapper.h"
#include "compiler.h"
#include "debug.h"
#include "execution.h"
#include "global-handles.h"
#include "natives.h"
#include "runtime.h"
#include "string-search.h"
#include "stub-cache.h"
#include "vm-state-inl.h"

namespace v8 {
namespace internal {


int HandleScope::NumberOfHandles(Isolate* isolate) {
  HandleScopeImplementer* impl = isolate->handle_scope_implementer();
  int n = impl->blocks()->length();
  if (n == 0) return 0;
  return ((n - 1) * kHandleBlockSize) + static_cast<int>(
      (isolate->handle_scope_data()->next - impl->blocks()->last()));
}


Object** HandleScope::Extend(Isolate* isolate) {
  HandleScopeData* current = isolate->handle_scope_data();

  Object** result = current->next;

  ASSERT(result == current->limit);
  // Make sure there's at least one scope on the stack and that the
  // top of the scope stack isn't a barrier.
  if (!Utils::ApiCheck(current->level != 0,
                       "v8::HandleScope::CreateHandle()",
                       "Cannot create a handle without a HandleScope")) {
    return NULL;
  }
  HandleScopeImplementer* impl = isolate->handle_scope_implementer();
  // If there's more room in the last block, we use that. This is used
  // for fast creation of scopes after scope barriers.
  if (!impl->blocks()->is_empty()) {
    Object** limit = &impl->blocks()->last()[kHandleBlockSize];
    if (current->limit != limit) {
      current->limit = limit;
      ASSERT(limit - current->next < kHandleBlockSize);
    }
  }

  // If we still haven't found a slot for the handle, we extend the
  // current handle scope by allocating a new handle block.
  if (result == current->limit) {
    // If there's a spare block, use it for growing the current scope.
    result = impl->GetSpareOrNewBlock();
    // Add the extension to the global list of blocks, but count the
    // extension as part of the current scope.
    impl->blocks()->Add(result);
    current->limit = &result[kHandleBlockSize];
  }

  return result;
}


void HandleScope::DeleteExtensions(Isolate* isolate) {
  HandleScopeData* current = isolate->handle_scope_data();
  isolate->handle_scope_implementer()->DeleteExtensions(current->limit);
}


#ifdef ENABLE_HANDLE_ZAPPING
void HandleScope::ZapRange(Object** start, Object** end) {
  ASSERT(end - start <= kHandleBlockSize);
  for (Object** p = start; p != end; p++) {
    *reinterpret_cast<Address*>(p) = v8::internal::kHandleZapValue;
  }
}
#endif


Address HandleScope::current_level_address(Isolate* isolate) {
  return reinterpret_cast<Address>(&isolate->handle_scope_data()->level);
}


Address HandleScope::current_next_address(Isolate* isolate) {
  return reinterpret_cast<Address>(&isolate->handle_scope_data()->next);
}


Address HandleScope::current_limit_address(Isolate* isolate) {
  return reinterpret_cast<Address>(&isolate->handle_scope_data()->limit);
}


Handle<JSGlobalProxy> ReinitializeJSGlobalProxy(
    Handle<JSFunction> constructor,
    Handle<JSGlobalProxy> global) {
  CALL_HEAP_FUNCTION(
      constructor->GetIsolate(),
      constructor->GetHeap()->ReinitializeJSGlobalProxy(*constructor, *global),
      JSGlobalProxy);
}


MaybeHandle<Object> GetProperty(Handle<JSReceiver> obj,
                                const char* name) {
  Isolate* isolate = obj->GetIsolate();
  Handle<String> str = isolate->factory()->InternalizeUtf8String(name);
  ASSERT(!str.is_null());
  return Object::GetPropertyOrElement(obj, str);
}


// Wrappers for scripts are kept alive and cached in weak global
// handles referred from foreign objects held by the scripts as long as
// they are used. When they are not used anymore, the garbage
// collector will call the weak callback on the global handle
// associated with the wrapper and get rid of both the wrapper and the
// handle.
static void ClearWrapperCache(
    const v8::WeakCallbackData<v8::Value, void>& data) {
  Object** location = reinterpret_cast<Object**>(data.GetParameter());
  JSValue* wrapper = JSValue::cast(*location);
  Foreign* foreign = Script::cast(wrapper->value())->wrapper();
  ASSERT_EQ(foreign->foreign_address(), reinterpret_cast<Address>(location));
  foreign->set_foreign_address(0);
  GlobalHandles::Destroy(location);
  Isolate* isolate = reinterpret_cast<Isolate*>(data.GetIsolate());
  isolate->counters()->script_wrappers()->Decrement();
}


Handle<JSValue> GetScriptWrapper(Handle<Script> script) {
  if (script->wrapper()->foreign_address() != NULL) {
    // Return a handle for the existing script wrapper from the cache.
    return Handle<JSValue>(
        *reinterpret_cast<JSValue**>(script->wrapper()->foreign_address()));
  }
  Isolate* isolate = script->GetIsolate();
  // Construct a new script wrapper.
  isolate->counters()->script_wrappers()->Increment();
  Handle<JSFunction> constructor = isolate->script_function();
  Handle<JSValue> result =
      Handle<JSValue>::cast(isolate->factory()->NewJSObject(constructor));

  result->set_value(*script);

  // Create a new weak global handle and use it to cache the wrapper
  // for future use. The cache will automatically be cleared by the
  // garbage collector when it is not used anymore.
  Handle<Object> handle = isolate->global_handles()->Create(*result);
  GlobalHandles::MakeWeak(handle.location(),
                          reinterpret_cast<void*>(handle.location()),
                          &ClearWrapperCache);
  script->wrapper()->set_foreign_address(
      reinterpret_cast<Address>(handle.location()));
  return result;
}


// Init line_ends array with code positions of line ends inside script
// source.
void InitScriptLineEnds(Handle<Script> script) {
  if (!script->line_ends()->IsUndefined()) return;

  Isolate* isolate = script->GetIsolate();

  if (!script->source()->IsString()) {
    ASSERT(script->source()->IsUndefined());
    Handle<FixedArray> empty = isolate->factory()->NewFixedArray(0);
    script->set_line_ends(*empty);
    ASSERT(script->line_ends()->IsFixedArray());
    return;
  }

  Handle<String> src(String::cast(script->source()), isolate);

  Handle<FixedArray> array = CalculateLineEnds(src, true);

  if (*array != isolate->heap()->empty_fixed_array()) {
    array->set_map(isolate->heap()->fixed_cow_array_map());
  }

  script->set_line_ends(*array);
  ASSERT(script->line_ends()->IsFixedArray());
}


template <typename SourceChar>
static void CalculateLineEnds(Isolate* isolate,
                              List<int>* line_ends,
                              Vector<const SourceChar> src,
                              bool with_last_line) {
  const int src_len = src.length();
  StringSearch<uint8_t, SourceChar> search(isolate, STATIC_ASCII_VECTOR("\n"));

  // Find and record line ends.
  int position = 0;
  while (position != -1 && position < src_len) {
    position = search.Search(src, position);
    if (position != -1) {
      line_ends->Add(position);
      position++;
    } else if (with_last_line) {
      // Even if the last line misses a line end, it is counted.
      line_ends->Add(src_len);
      return;
    }
  }
}


Handle<FixedArray> CalculateLineEnds(Handle<String> src,
                                     bool with_last_line) {
  src = String::Flatten(src);
  // Rough estimate of line count based on a roughly estimated average
  // length of (unpacked) code.
  int line_count_estimate = src->length() >> 4;
  List<int> line_ends(line_count_estimate);
  Isolate* isolate = src->GetIsolate();
  {
    DisallowHeapAllocation no_allocation;  // ensure vectors stay valid.
    // Dispatch on type of strings.
    String::FlatContent content = src->GetFlatContent();
    ASSERT(content.IsFlat());
    if (content.IsAscii()) {
      CalculateLineEnds(isolate,
                        &line_ends,
                        content.ToOneByteVector(),
                        with_last_line);
    } else {
      CalculateLineEnds(isolate,
                        &line_ends,
                        content.ToUC16Vector(),
                        with_last_line);
    }
  }
  int line_count = line_ends.length();
  Handle<FixedArray> array = isolate->factory()->NewFixedArray(line_count);
  for (int i = 0; i < line_count; i++) {
    array->set(i, Smi::FromInt(line_ends[i]));
  }
  return array;
}


// Convert code position into line number.
int GetScriptLineNumber(Handle<Script> script, int code_pos) {
  InitScriptLineEnds(script);
  DisallowHeapAllocation no_allocation;
  FixedArray* line_ends_array = FixedArray::cast(script->line_ends());
  const int line_ends_len = line_ends_array->length();

  if (!line_ends_len) return -1;

  if ((Smi::cast(line_ends_array->get(0)))->value() >= code_pos) {
    return script->line_offset()->value();
  }

  int left = 0;
  int right = line_ends_len;
  while (int half = (right - left) / 2) {
    if ((Smi::cast(line_ends_array->get(left + half)))->value() > code_pos) {
      right -= half;
    } else {
      left += half;
    }
  }
  return right + script->line_offset()->value();
}


// Convert code position into column number.
int GetScriptColumnNumber(Handle<Script> script, int code_pos) {
  int line_number = GetScriptLineNumber(script, code_pos);
  if (line_number == -1) return -1;

  DisallowHeapAllocation no_allocation;
  FixedArray* line_ends_array = FixedArray::cast(script->line_ends());
  line_number = line_number - script->line_offset()->value();
  if (line_number == 0) return code_pos + script->column_offset()->value();
  int prev_line_end_pos =
      Smi::cast(line_ends_array->get(line_number - 1))->value();
  return code_pos - (prev_line_end_pos + 1);
}


int GetScriptLineNumberSafe(Handle<Script> script, int code_pos) {
  DisallowHeapAllocation no_allocation;
  if (!script->line_ends()->IsUndefined()) {
    return GetScriptLineNumber(script, code_pos);
  }
  // Slow mode: we do not have line_ends. We have to iterate through source.
  if (!script->source()->IsString()) {
    return -1;
  }
  String* source = String::cast(script->source());
  int line = 0;
  int len = source->length();
  for (int pos = 0; pos < len; pos++) {
    if (pos == code_pos) {
      break;
    }
    if (source->Get(pos) == '\n') {
      line++;
    }
  }
  return line;
}


// Compute the property keys from the interceptor.
// TODO(rossberg): support symbols in API, and filter here if needed.
v8::Handle<v8::Array> GetKeysForNamedInterceptor(Handle<JSReceiver> receiver,
                                                 Handle<JSObject> object) {
  Isolate* isolate = receiver->GetIsolate();
  Handle<InterceptorInfo> interceptor(object->GetNamedInterceptor());
  PropertyCallbackArguments
      args(isolate, interceptor->data(), *receiver, *object);
  v8::Handle<v8::Array> result;
  if (!interceptor->enumerator()->IsUndefined()) {
    v8::NamedPropertyEnumeratorCallback enum_fun =
        v8::ToCData<v8::NamedPropertyEnumeratorCallback>(
            interceptor->enumerator());
    LOG(isolate, ApiObjectAccess("interceptor-named-enum", *object));
    result = args.Call(enum_fun);
  }
#if ENABLE_EXTRA_CHECKS
  CHECK(result.IsEmpty() || v8::Utils::OpenHandle(*result)->IsJSObject());
#endif
  return v8::Local<v8::Array>::New(reinterpret_cast<v8::Isolate*>(isolate),
                                   result);
}


// Compute the element keys from the interceptor.
v8::Handle<v8::Array> GetKeysForIndexedInterceptor(Handle<JSReceiver> receiver,
                                                   Handle<JSObject> object) {
  Isolate* isolate = receiver->GetIsolate();
  Handle<InterceptorInfo> interceptor(object->GetIndexedInterceptor());
  PropertyCallbackArguments
      args(isolate, interceptor->data(), *receiver, *object);
  v8::Handle<v8::Array> result;
  if (!interceptor->enumerator()->IsUndefined()) {
    v8::IndexedPropertyEnumeratorCallback enum_fun =
        v8::ToCData<v8::IndexedPropertyEnumeratorCallback>(
            interceptor->enumerator());
    LOG(isolate, ApiObjectAccess("interceptor-indexed-enum", *object));
    result = args.Call(enum_fun);
#if ENABLE_EXTRA_CHECKS
    CHECK(result.IsEmpty() || v8::Utils::OpenHandle(*result)->IsJSObject());
#endif
  }
  return v8::Local<v8::Array>::New(reinterpret_cast<v8::Isolate*>(isolate),
                                   result);
}


Handle<Object> GetScriptNameOrSourceURL(Handle<Script> script) {
  Isolate* isolate = script->GetIsolate();
  Handle<String> name_or_source_url_key =
      isolate->factory()->InternalizeOneByteString(
          STATIC_ASCII_VECTOR("nameOrSourceURL"));
  Handle<JSValue> script_wrapper = GetScriptWrapper(script);
  Handle<Object> property = Object::GetProperty(
      script_wrapper, name_or_source_url_key).ToHandleChecked();
  ASSERT(property->IsJSFunction());
  Handle<JSFunction> method = Handle<JSFunction>::cast(property);
  Handle<Object> result;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result,
      Execution::TryCall(method, script_wrapper, 0, NULL),
      isolate->factory()->undefined_value());
  return result;
}


static bool ContainsOnlyValidKeys(Handle<FixedArray> array) {
  int len = array->length();
  for (int i = 0; i < len; i++) {
    Object* e = array->get(i);
    if (!(e->IsString() || e->IsNumber())) return false;
  }
  return true;
}


MaybeHandle<FixedArray> GetKeysInFixedArrayFor(Handle<JSReceiver> object,
                                               KeyCollectionType type) {
  USE(ContainsOnlyValidKeys);
  Isolate* isolate = object->GetIsolate();
  Handle<FixedArray> content = isolate->factory()->empty_fixed_array();
  Handle<JSObject> arguments_boilerplate = Handle<JSObject>(
      isolate->context()->native_context()->sloppy_arguments_boilerplate(),
      isolate);
  Handle<JSFunction> arguments_function = Handle<JSFunction>(
      JSFunction::cast(arguments_boilerplate->map()->constructor()),
      isolate);

  // Only collect keys if access is permitted.
  for (Handle<Object> p = object;
       *p != isolate->heap()->null_value();
       p = Handle<Object>(p->GetPrototype(isolate), isolate)) {
    if (p->IsJSProxy()) {
      Handle<JSProxy> proxy(JSProxy::cast(*p), isolate);
      Handle<Object> args[] = { proxy };
      Handle<Object> names;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, names,
          Execution::Call(isolate,
                          isolate->proxy_enumerate(),
                          object,
                          ARRAY_SIZE(args),
                          args),
          FixedArray);
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, content,
          FixedArray::AddKeysFromJSArray(
              content, Handle<JSArray>::cast(names)),
          FixedArray);
      break;
    }

    Handle<JSObject> current(JSObject::cast(*p), isolate);

    // Check access rights if required.
    if (current->IsAccessCheckNeeded() &&
        !isolate->MayNamedAccessWrapper(current,
                                        isolate->factory()->undefined_value(),
                                        v8::ACCESS_KEYS)) {
      isolate->ReportFailedAccessCheckWrapper(current, v8::ACCESS_KEYS);
      RETURN_EXCEPTION_IF_SCHEDULED_EXCEPTION(isolate, FixedArray);
      break;
    }

    // Compute the element keys.
    Handle<FixedArray> element_keys =
        isolate->factory()->NewFixedArray(current->NumberOfEnumElements());
    current->GetEnumElementKeys(*element_keys);
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, content,
        FixedArray::UnionOfKeys(content, element_keys),
        FixedArray);
    ASSERT(ContainsOnlyValidKeys(content));

    // Add the element keys from the interceptor.
    if (current->HasIndexedInterceptor()) {
      v8::Handle<v8::Array> result =
          GetKeysForIndexedInterceptor(object, current);
      if (!result.IsEmpty()) {
        ASSIGN_RETURN_ON_EXCEPTION(
            isolate, content,
            FixedArray::AddKeysFromJSArray(
                content, v8::Utils::OpenHandle(*result)),
            FixedArray);
      }
      ASSERT(ContainsOnlyValidKeys(content));
    }

    // We can cache the computed property keys if access checks are
    // not needed and no interceptors are involved.
    //
    // We do not use the cache if the object has elements and
    // therefore it does not make sense to cache the property names
    // for arguments objects.  Arguments objects will always have
    // elements.
    // Wrapped strings have elements, but don't have an elements
    // array or dictionary.  So the fast inline test for whether to
    // use the cache says yes, so we should not create a cache.
    bool cache_enum_keys =
        ((current->map()->constructor() != *arguments_function) &&
         !current->IsJSValue() &&
         !current->IsAccessCheckNeeded() &&
         !current->HasNamedInterceptor() &&
         !current->HasIndexedInterceptor());
    // Compute the property keys and cache them if possible.
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, content,
        FixedArray::UnionOfKeys(
            content, GetEnumPropertyKeys(current, cache_enum_keys)),
        FixedArray);
    ASSERT(ContainsOnlyValidKeys(content));

    // Add the property keys from the interceptor.
    if (current->HasNamedInterceptor()) {
      v8::Handle<v8::Array> result =
          GetKeysForNamedInterceptor(object, current);
      if (!result.IsEmpty()) {
        ASSIGN_RETURN_ON_EXCEPTION(
            isolate, content,
            FixedArray::AddKeysFromJSArray(
                content, v8::Utils::OpenHandle(*result)),
            FixedArray);
      }
      ASSERT(ContainsOnlyValidKeys(content));
    }

    // If we only want local properties we bail out after the first
    // iteration.
    if (type == LOCAL_ONLY) break;
  }
  return content;
}


Handle<FixedArray> ReduceFixedArrayTo(Handle<FixedArray> array, int length) {
  ASSERT(array->length() >= length);
  if (array->length() == length) return array;

  Handle<FixedArray> new_array =
      array->GetIsolate()->factory()->NewFixedArray(length);
  for (int i = 0; i < length; ++i) new_array->set(i, array->get(i));
  return new_array;
}


Handle<FixedArray> GetEnumPropertyKeys(Handle<JSObject> object,
                                       bool cache_result) {
  Isolate* isolate = object->GetIsolate();
  if (object->HasFastProperties()) {
    int own_property_count = object->map()->EnumLength();
    // If the enum length of the given map is set to kInvalidEnumCache, this
    // means that the map itself has never used the present enum cache. The
    // first step to using the cache is to set the enum length of the map by
    // counting the number of own descriptors that are not DONT_ENUM or
    // SYMBOLIC.
    if (own_property_count == kInvalidEnumCacheSentinel) {
      own_property_count = object->map()->NumberOfDescribedProperties(
          OWN_DESCRIPTORS, DONT_SHOW);
    } else {
      ASSERT(own_property_count == object->map()->NumberOfDescribedProperties(
          OWN_DESCRIPTORS, DONT_SHOW));
    }

    if (object->map()->instance_descriptors()->HasEnumCache()) {
      DescriptorArray* desc = object->map()->instance_descriptors();
      Handle<FixedArray> keys(desc->GetEnumCache(), isolate);

      // In case the number of properties required in the enum are actually
      // present, we can reuse the enum cache. Otherwise, this means that the
      // enum cache was generated for a previous (smaller) version of the
      // Descriptor Array. In that case we regenerate the enum cache.
      if (own_property_count <= keys->length()) {
        if (cache_result) object->map()->SetEnumLength(own_property_count);
        isolate->counters()->enum_cache_hits()->Increment();
        return ReduceFixedArrayTo(keys, own_property_count);
      }
    }

    Handle<Map> map(object->map());

    if (map->instance_descriptors()->IsEmpty()) {
      isolate->counters()->enum_cache_hits()->Increment();
      if (cache_result) map->SetEnumLength(0);
      return isolate->factory()->empty_fixed_array();
    }

    isolate->counters()->enum_cache_misses()->Increment();

    Handle<FixedArray> storage = isolate->factory()->NewFixedArray(
        own_property_count);
    Handle<FixedArray> indices = isolate->factory()->NewFixedArray(
        own_property_count);

    Handle<DescriptorArray> descs =
        Handle<DescriptorArray>(object->map()->instance_descriptors(), isolate);

    int size = map->NumberOfOwnDescriptors();
    int index = 0;

    for (int i = 0; i < size; i++) {
      PropertyDetails details = descs->GetDetails(i);
      Object* key = descs->GetKey(i);
      if (!(details.IsDontEnum() || key->IsSymbol())) {
        storage->set(index, key);
        if (!indices.is_null()) {
          if (details.type() != FIELD) {
            indices = Handle<FixedArray>();
          } else {
            int field_index = descs->GetFieldIndex(i);
            if (field_index >= map->inobject_properties()) {
              field_index = -(field_index - map->inobject_properties() + 1);
            }
            field_index = field_index << 1;
            if (details.representation().IsDouble()) {
              field_index |= 1;
            }
            indices->set(index, Smi::FromInt(field_index));
          }
        }
        index++;
      }
    }
    ASSERT(index == storage->length());

    Handle<FixedArray> bridge_storage =
        isolate->factory()->NewFixedArray(
            DescriptorArray::kEnumCacheBridgeLength);
    DescriptorArray* desc = object->map()->instance_descriptors();
    desc->SetEnumCache(*bridge_storage,
                       *storage,
                       indices.is_null() ? Object::cast(Smi::FromInt(0))
                                         : Object::cast(*indices));
    if (cache_result) {
      object->map()->SetEnumLength(own_property_count);
    }
    return storage;
  } else {
    Handle<NameDictionary> dictionary(object->property_dictionary());
    int length = dictionary->NumberOfEnumElements();
    if (length == 0) {
      return Handle<FixedArray>(isolate->heap()->empty_fixed_array());
    }
    Handle<FixedArray> storage = isolate->factory()->NewFixedArray(length);
    dictionary->CopyEnumKeysTo(*storage);
    return storage;
  }
}


DeferredHandleScope::DeferredHandleScope(Isolate* isolate)
    : impl_(isolate->handle_scope_implementer()) {
  impl_->BeginDeferredScope();
  HandleScopeData* data = impl_->isolate()->handle_scope_data();
  Object** new_next = impl_->GetSpareOrNewBlock();
  Object** new_limit = &new_next[kHandleBlockSize];
  ASSERT(data->limit == &impl_->blocks()->last()[kHandleBlockSize]);
  impl_->blocks()->Add(new_next);

#ifdef DEBUG
  prev_level_ = data->level;
#endif
  data->level++;
  prev_limit_ = data->limit;
  prev_next_ = data->next;
  data->next = new_next;
  data->limit = new_limit;
}


DeferredHandleScope::~DeferredHandleScope() {
  impl_->isolate()->handle_scope_data()->level--;
  ASSERT(handles_detached_);
  ASSERT(impl_->isolate()->handle_scope_data()->level == prev_level_);
}


DeferredHandles* DeferredHandleScope::Detach() {
  DeferredHandles* deferred = impl_->Detach(prev_limit_);
  HandleScopeData* data = impl_->isolate()->handle_scope_data();
  data->next = prev_next_;
  data->limit = prev_limit_;
#ifdef DEBUG
  handles_detached_ = true;
#endif
  return deferred;
}


void AddWeakObjectToCodeDependency(Heap* heap,
                                   Handle<Object> object,
                                   Handle<Code> code) {
  heap->EnsureWeakObjectToCodeTable();
  Handle<DependentCode> dep(heap->LookupWeakObjectToCodeDependency(*object));
  dep = DependentCode::Insert(dep, DependentCode::kWeakCodeGroup, code);
  CALL_HEAP_FUNCTION_VOID(heap->isolate(),
                          heap->AddWeakObjectToCodeDependency(*object, *dep));
}


} }  // namespace v8::internal
