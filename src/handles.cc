// Copyright 2006-2008 Google Inc. All Rights Reserved.
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
#include "bootstrapper.h"
#include "compiler.h"
#include "debug.h"
#include "execution.h"
#include "global-handles.h"
#include "natives.h"
#include "runtime.h"

namespace v8 { namespace internal {

DECLARE_bool(allow_natives_syntax);

#ifdef DEBUG
DECLARE_bool(gc_greedy);
#endif

#define CALL_GC(RESULT)                                             \
  {                                                                 \
    Failure* __failure__ = Failure::cast(RESULT);                   \
    if (!Heap::CollectGarbage(__failure__->requested(),             \
                              __failure__->allocation_space())) {   \
       /* TODO(1181417): Fix this. */                               \
       V8::FatalProcessOutOfMemory("Handles");                      \
    }                                                               \
  }


// Don't use the following names: __object__, __failure__.
#define CALL_HEAP_FUNCTION_VOID(FUNCTION_CALL)                      \
  GC_GREEDY_CHECK();                                                \
  Object* __object__ = FUNCTION_CALL;                               \
  if (__object__->IsFailure()) {                                    \
    if (__object__->IsRetryAfterGC()) {                             \
      Failure* __failure__ = Failure::cast(__object__);             \
      if (!Heap::CollectGarbage(__failure__->requested(),           \
                                __failure__->allocation_space())) { \
         /* TODO(1181417): Fix this. */                             \
         V8::FatalProcessOutOfMemory("Handles");                    \
      }                                                             \
      __object__ = FUNCTION_CALL;                                   \
      if (__object__->IsFailure()) {                                \
        if (__object__->IsRetryAfterGC()) {                         \
           /* TODO(1181417): Fix this. */                           \
           V8::FatalProcessOutOfMemory("Handles");                  \
        }                                                           \
        return;                                                     \
      }                                                             \
    } else {                                                        \
      return;                                                       \
    }                                                               \
  }




Handle<FixedArray> AddKeysFromJSArray(Handle<FixedArray> content,
                                      Handle<JSArray> array) {
  CALL_HEAP_FUNCTION(content->AddKeysFromJSArray(*array), FixedArray);
}


Handle<FixedArray> UnionOfKeys(Handle<FixedArray> first,
                               Handle<FixedArray> second) {
  CALL_HEAP_FUNCTION(first->UnionOfKeys(*second), FixedArray);
}


Handle<JSGlobalObject> ReinitializeJSGlobalObject(
    Handle<JSFunction> constructor,
    Handle<JSGlobalObject> global) {
  CALL_HEAP_FUNCTION(Heap::ReinitializeJSGlobalObject(*constructor, *global),
                     JSGlobalObject);
}


void SetExpectedNofProperties(Handle<JSFunction> func, int nof) {
  func->shared()->set_expected_nof_properties(nof);
  if (func->has_initial_map()) {
    Handle<Map> new_initial_map =
        Factory::CopyMapDropTransitions(Handle<Map>(func->initial_map()));
    new_initial_map->set_unused_property_fields(nof);
    func->set_initial_map(*new_initial_map);
  }
}


void SetPrototypeProperty(Handle<JSFunction> func, Handle<JSObject> value) {
  CALL_HEAP_FUNCTION_VOID(func->SetPrototype(*value));
}


void SetExpectedNofPropertiesFromEstimate(Handle<SharedFunctionInfo> shared,
                                          int estimate) {
  // TODO(1231235): We need dynamic feedback to estimate the number
  // of expected properties in an object. The static hack below
  // is barely a solution.
  shared->set_expected_nof_properties(estimate + 2);
}


void SetExpectedNofPropertiesFromEstimate(Handle<JSFunction> func,
                                          int estimate) {
  // TODO(1231235): We need dynamic feedback to estimate the number
  // of expected properties in an object. The static hack below
  // is barely a solution.
  SetExpectedNofProperties(func, estimate + 2);
}


void NormalizeProperties(Handle<JSObject> object) {
  CALL_HEAP_FUNCTION_VOID(object->NormalizeProperties());
}


void NormalizeElements(Handle<JSObject> object) {
  CALL_HEAP_FUNCTION_VOID(object->NormalizeElements());
}


void TransformToFastProperties(Handle<JSObject> object,
                               int unused_property_fields) {
  CALL_HEAP_FUNCTION_VOID(
      object->TransformToFastProperties(unused_property_fields));
}


void FlattenString(Handle<String> string) {
  if (string->IsFlat()) return;
  CALL_HEAP_FUNCTION_VOID(String::cast(*string)->Flatten());
  ASSERT(string->IsFlat());
}


Handle<Object> SetPrototype(Handle<JSFunction> function,
                            Handle<Object> prototype) {
  CALL_HEAP_FUNCTION(Accessors::FunctionSetPrototype(*function,
                                                     *prototype,
                                                     NULL),
                     Object);
}


void AddProperty(Handle<JSObject> object,
                 Handle<String> key,
                 Handle<Object> value,
                 PropertyAttributes attributes) {
  CALL_HEAP_FUNCTION_VOID(object->AddProperty(*key, *value, attributes));
}

Handle<Object> SetProperty(Handle<JSObject> object,
                           Handle<String> key,
                           Handle<Object> value,
                           PropertyAttributes attributes) {
  CALL_HEAP_FUNCTION(object->SetProperty(*key, *value, attributes), Object);
}


Handle<Object> SetProperty(Handle<Object> object,
                           Handle<Object> key,
                           Handle<Object> value,
                           PropertyAttributes attributes) {
  CALL_HEAP_FUNCTION(Runtime::SetObjectProperty(object, key, value, attributes),
                     Object);
}


Handle<Object> SetPropertyWithInterceptor(Handle<JSObject> object,
                                          Handle<String> key,
                                          Handle<Object> value,
                                          PropertyAttributes attributes) {
  CALL_HEAP_FUNCTION(object->SetPropertyWithInterceptor(*key,
                                                        *value,
                                                        attributes),
                     Object);
}


Handle<Object> GetProperty(Handle<JSObject> obj,
                           const char* name) {
  Handle<String> str = Factory::LookupAsciiSymbol(name);
  CALL_HEAP_FUNCTION(obj->GetProperty(*str), Object);
}


Handle<Object> GetProperty(Handle<Object> obj,
                           Handle<Object> key) {
  CALL_HEAP_FUNCTION(Runtime::GetObjectProperty(obj, *key), Object);
}


Handle<Object> GetPropertyWithInterceptor(Handle<JSObject> receiver,
                                          Handle<JSObject> holder,
                                          Handle<String> name,
                                          PropertyAttributes* attributes) {
  CALL_HEAP_FUNCTION(holder->GetPropertyWithInterceptor(*receiver,
                                                        *name,
                                                        attributes),
                     Object);
}


Handle<Object> GetPrototype(Handle<Object> obj) {
  Handle<Object> result(obj->GetPrototype());
  return result;
}


Handle<Object> DeleteElement(Handle<JSObject> obj,
                             uint32_t index) {
  CALL_HEAP_FUNCTION(obj->DeleteElement(index), Object);
}


Handle<Object> DeleteProperty(Handle<JSObject> obj,
                              Handle<String> prop) {
  CALL_HEAP_FUNCTION(obj->DeleteProperty(*prop), Object);
}


Handle<String> SubString(Handle<String> str, int start, int end) {
  CALL_HEAP_FUNCTION(str->Slice(start, end), String);
}


Handle<Object> SetElement(Handle<JSObject> object,
                          uint32_t index,
                          Handle<Object> value) {
  GC_GREEDY_CHECK();
  Object* obj = object->SetElement(index, *value);
  // If you set an element then the object may need to get a new map
  // which will cause it to grow, which will cause an allocation.
  // If you know that the object will not grow then perhaps this check
  // does not apply and you may have to split this method into two
  // versions.
  ASSERT(Heap::IsAllocationAllowed());
  if (obj->IsFailure()) {
    CALL_GC(obj);
    obj = object->SetElement(index, *value);
    if (obj->IsFailure()) {
      V8::FatalProcessOutOfMemory("Handles");  // TODO(1181417): Fix this.
    }
  }
  return value;
}


Handle<JSObject> Copy(Handle<JSObject> obj, PretenureFlag pretenure) {
  CALL_HEAP_FUNCTION(obj->Copy(pretenure), JSObject);
}


// Wrappers for scripts are kept alive and cached in weak global
// handles referred from proxy objects held by the scripts as long as
// they are used. When they are not used anymore, the garbage
// collector will call the weak callback on the global handle
// associated with the wrapper and get rid of both the wrapper and the
// handle.
static void ClearWrapperCache(Persistent<v8::Object> handle, void*) {
  Handle<Object> cache = Utils::OpenHandle(*handle);
  JSValue* wrapper = JSValue::cast(*cache);
  Proxy* proxy = Script::cast(wrapper->value())->wrapper();
  ASSERT(proxy->proxy() == reinterpret_cast<Address>(cache.location()));
  proxy->set_proxy(0);
  GlobalHandles::Destroy(cache.location());
  Counters::script_wrappers.Decrement();
}


Handle<JSValue> GetScriptWrapper(Handle<Script> script) {
  Handle<Object> cache(reinterpret_cast<Object**>(script->wrapper()->proxy()));
  if (!cache.is_null()) {
    // Return the script wrapper directly from the cache.
    return Handle<JSValue>(JSValue::cast(*cache));
  }

  // Construct a new script wrapper.
  Counters::script_wrappers.Increment();
  Handle<JSFunction> constructor = Top::script_function();
  Handle<JSValue> result =
      Handle<JSValue>::cast(Factory::NewJSObject(constructor));
  result->set_value(*script);

  // Create a new weak global handle and use it to cache the wrapper
  // for future use. The cache will automatically be cleared by the
  // garbage collector when it is not used anymore.
  Handle<Object> handle = GlobalHandles::Create(*result);
  GlobalHandles::MakeWeak(handle.location(), NULL, &ClearWrapperCache);
  script->wrapper()->set_proxy(reinterpret_cast<Address>(handle.location()));
  return result;
}


#undef CALL_HEAP_FUNCTION
#undef CALL_GC


// Compute the property keys from the interceptor.
v8::Handle<v8::Array> GetKeysForNamedInterceptor(Handle<JSObject> receiver,
                                                 Handle<JSObject> object) {
  Handle<InterceptorInfo> interceptor(object->GetNamedInterceptor());
  Handle<Object> data(interceptor->data());
  v8::AccessorInfo info(
    v8::Utils::ToLocal(receiver),
    v8::Utils::ToLocal(data),
    v8::Utils::ToLocal(object));
  v8::Handle<v8::Array> result;
  if (!interceptor->enumerator()->IsUndefined()) {
    v8::NamedPropertyEnumerator enum_fun =
        v8::ToCData<v8::NamedPropertyEnumerator>(interceptor->enumerator());
    LOG(ApiObjectAccess("interceptor-named-enum", *object));
    {
      // Leaving JavaScript.
      VMState state(OTHER);
      result = enum_fun(info);
    }
  }
  return result;
}


// Compute the element keys from the interceptor.
v8::Handle<v8::Array> GetKeysForIndexedInterceptor(Handle<JSObject> receiver,
                                                   Handle<JSObject> object) {
  Handle<InterceptorInfo> interceptor(object->GetIndexedInterceptor());
  Handle<Object> data(interceptor->data());
  v8::AccessorInfo info(
    v8::Utils::ToLocal(receiver),
    v8::Utils::ToLocal(data),
    v8::Utils::ToLocal(object));
  v8::Handle<v8::Array> result;
  if (!interceptor->enumerator()->IsUndefined()) {
    v8::IndexedPropertyEnumerator enum_fun =
        v8::ToCData<v8::IndexedPropertyEnumerator>(interceptor->enumerator());
    LOG(ApiObjectAccess("interceptor-indexed-enum", *object));
    {
      // Leaving JavaScript.
      VMState state(OTHER);
      result = enum_fun(info);
    }
  }
  return result;
}


Handle<FixedArray> GetKeysInFixedArrayFor(Handle<JSObject> object) {
  Handle<FixedArray> content = Factory::empty_fixed_array();

  // Check access rights if required.
  if (object->IsAccessCheckNeeded() &&
    !Top::MayNamedAccess(*object, Heap::undefined_value(), v8::ACCESS_KEYS)) {
    Top::ReportFailedAccessCheck(*object, v8::ACCESS_KEYS);
    return content;
  }

  JSObject* arguments_boilerplate =
      Top::context()->global_context()->arguments_boilerplate();
  JSFunction* arguments_function =
      JSFunction::cast(arguments_boilerplate->map()->constructor());
  bool allow_enumeration = (object->map()->constructor() != arguments_function);

  // Only collect keys if access is permitted.
  if (allow_enumeration) {
    for (Handle<Object> p = object;
         *p != Heap::null_value();
         p = Handle<Object>(p->GetPrototype())) {
      Handle<JSObject> current(JSObject::cast(*p));

      // Compute the property keys.
      content = UnionOfKeys(content, GetEnumPropertyKeys(current));

      // Add the property keys from the interceptor.
      if (current->HasNamedInterceptor()) {
        v8::Handle<v8::Array> result =
            GetKeysForNamedInterceptor(object, current);
        if (!result.IsEmpty())
          content = AddKeysFromJSArray(content, v8::Utils::OpenHandle(*result));
      }

      // Compute the element keys.
      Handle<FixedArray> element_keys =
          Factory::NewFixedArray(current->NumberOfEnumElements());
      current->GetEnumElementKeys(*element_keys);
      content = UnionOfKeys(content, element_keys);

      // Add the element keys from the interceptor.
      if (current->HasIndexedInterceptor()) {
        v8::Handle<v8::Array> result =
            GetKeysForIndexedInterceptor(object, current);
        if (!result.IsEmpty())
          content = AddKeysFromJSArray(content, v8::Utils::OpenHandle(*result));
      }
    }
  }
  return content;
}


Handle<JSArray> GetKeysFor(Handle<JSObject> object) {
  Counters::for_in.Increment();

  Handle<FixedArray> content = GetKeysInFixedArrayFor(object);

  // Allocate the JSArray with the result.
  Handle<JSArray> obj = Factory::NewJSArray(content->length());
  Handle<JSArray>::cast(obj)->SetContent(*content);
  return Handle<JSArray>::cast(obj);
}


Handle<FixedArray> GetEnumPropertyKeys(Handle<JSObject> object) {
  int index = 0;
  if (object->HasFastProperties()) {
    if (object->map()->instance_descriptors()->HasEnumCache()) {
      Counters::enum_cache_hits.Increment();
      DescriptorArray* desc = object->map()->instance_descriptors();
      return Handle<FixedArray>(FixedArray::cast(desc->GetEnumCache()));
    }
    Counters::enum_cache_misses.Increment();
    int num_enum = object->NumberOfEnumProperties();
    Handle<FixedArray> storage = Factory::NewFixedArray(num_enum);
    Handle<FixedArray> sort_array = Factory::NewFixedArray(num_enum);
    for (DescriptorReader r(object->map()->instance_descriptors());
         !r.eos();
         r.advance()) {
      if (!r.IsTransition() && !r.IsDontEnum()) {
        (*storage)->set(index, r.GetKey());
        (*sort_array)->set(index, Smi::FromInt(r.GetDetails().index()));
        index++;
      }
    }
    (*storage)->SortPairs(*sort_array);
    Handle<FixedArray> bridge_storage =
        Factory::NewFixedArray(DescriptorArray::kEnumCacheBridgeLength);
    DescriptorArray* desc = object->map()->instance_descriptors();
    desc->SetEnumCache(*bridge_storage, *storage);
    ASSERT(storage->length() == index);
    return storage;
  } else {
    int num_enum = object->NumberOfEnumProperties();
    Handle<FixedArray> storage = Factory::NewFixedArray(num_enum);
    Handle<FixedArray> sort_array = Factory::NewFixedArray(num_enum);
    object->property_dictionary()->CopyEnumKeysTo(*storage, *sort_array);
    return storage;
  }
}


bool CompileLazyShared(Handle<SharedFunctionInfo> shared,
                       ClearExceptionFlag flag) {
  // Compile the source information to a code object.
  ASSERT(!shared->is_compiled());
  bool result = Compiler::CompileLazy(shared);
  ASSERT(result != Top::has_pending_exception());
  if (!result && flag == CLEAR_EXCEPTION) Top::clear_pending_exception();
  return result;
}


bool CompileLazy(Handle<JSFunction> function, ClearExceptionFlag flag) {
  // Compile the source information to a code object.
  Handle<SharedFunctionInfo> shared(function->shared());
  return CompileLazyShared(shared, flag);
}


OptimizedObjectForAddingMultipleProperties::
OptimizedObjectForAddingMultipleProperties(Handle<JSObject> object,
                                           bool condition) {
  object_ = object;
  if (condition && object_->HasFastProperties()) {
    // Normalize the properties of object to avoid n^2 behavior
    // when extending the object multiple properties.
    unused_property_fields_ = object->map()->unused_property_fields();
    NormalizeProperties(object_);
    has_been_transformed_ = true;

  } else {
    has_been_transformed_ = false;
  }
}


OptimizedObjectForAddingMultipleProperties::
~OptimizedObjectForAddingMultipleProperties() {
  // Reoptimize the object to allow fast property access.
  if (has_been_transformed_) {
    TransformToFastProperties(object_, unused_property_fields_);
  }
}


void LoadLazy(Handle<JSFunction> fun, bool* pending_exception) {
  HandleScope scope;
  Handle<FixedArray> info(FixedArray::cast(fun->shared()->lazy_load_data()));
  int index = Smi::cast(info->get(0))->value();
  ASSERT(index >= 0);
  Handle<Context> compile_context(Context::cast(info->get(1)));
  Handle<Context> function_context(Context::cast(info->get(2)));
  Handle<Context> security_context(Context::cast(info->get(3)));
  Handle<Object> receiver(compile_context->global()->builtins());

  Vector<const char> name = Natives::GetScriptName(index);

  Handle<JSFunction> boilerplate;

  if (!Bootstrapper::NativesCacheLookup(name, &boilerplate)) {
    Handle<String> source_code = Bootstrapper::NativesSourceLookup(index);
    Handle<String> script_name = Factory::NewStringFromAscii(name);
    bool allow_natives_syntax = FLAG_allow_natives_syntax;
    FLAG_allow_natives_syntax = true;
    boilerplate = Compiler::Compile(source_code, script_name, 0, 0, NULL, NULL);
    FLAG_allow_natives_syntax = allow_natives_syntax;
    // If the compilation failed (possibly due to stack overflows), we
    // should never enter the result in the natives cache. Instead we
    // return from the function without marking the function as having
    // been lazily loaded.
    if (boilerplate.is_null()) {
      *pending_exception = true;
      return;
    }
    Bootstrapper::NativesCacheAdd(name, boilerplate);
  }

  // We shouldn't get here if compiling the script failed.
  ASSERT(!boilerplate.is_null());

  // When the debugger running in its own context touches lazy loaded
  // functions loading can be triggered. In that case ensure that the
  // execution of the boilerplate is in the correct context.
  SaveContext save;
  if (!Debug::debug_context().is_null() &&
      Top::context() == *Debug::debug_context()) {
    Top::set_context(*compile_context);
    Top::set_security_context(*security_context);
  }

  // Reset the lazy load data before running the script to make sure
  // not to get recursive lazy loading.
  fun->shared()->set_lazy_load_data(Heap::undefined_value());

  // Run the script.
  Handle<JSFunction> script_fun(
      Factory::NewFunctionFromBoilerplate(boilerplate, function_context));
  Execution::Call(script_fun, receiver, 0, NULL, pending_exception);

  // If lazy loading failed, restore the unloaded state of fun.
  if (*pending_exception) fun->shared()->set_lazy_load_data(*info);
}


void SetupLazy(Handle<JSFunction> fun,
               int index,
               Handle<Context> compile_context,
               Handle<Context> function_context,
               Handle<Context> security_context) {
  Handle<FixedArray> arr = Factory::NewFixedArray(4);
  arr->set(0, Smi::FromInt(index));
  arr->set(1, *compile_context);  // Compile in this context
  arr->set(2, *function_context);  // Set function context to this
  arr->set(3, *security_context);  // Receiver for call
  fun->shared()->set_lazy_load_data(*arr);
}

} }  // namespace v8::internal
