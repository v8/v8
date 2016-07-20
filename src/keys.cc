// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/keys.h"

#include "src/api-arguments.h"
#include "src/elements.h"
#include "src/factory.h"
#include "src/identity-map.h"
#include "src/isolate-inl.h"
#include "src/objects-inl.h"
#include "src/property-descriptor.h"
#include "src/prototype.h"

namespace v8 {
namespace internal {

KeyAccumulator::~KeyAccumulator() {
}

namespace {

static bool ContainsOnlyValidKeys(Handle<FixedArray> array) {
  int len = array->length();
  for (int i = 0; i < len; i++) {
    Object* e = array->get(i);
    if (!(e->IsName() || e->IsNumber())) return false;
  }
  return true;
}

}  // namespace
MaybeHandle<FixedArray> KeyAccumulator::GetKeys(
    Handle<JSReceiver> object, KeyCollectionMode mode, PropertyFilter filter,
    GetKeysConversion keys_conversion, bool filter_proxy_keys, bool is_for_in) {
  Isolate* isolate = object->GetIsolate();
  KeyAccumulator accumulator(isolate, mode, filter);
  accumulator.set_filter_proxy_keys(filter_proxy_keys);
  accumulator.set_is_for_in(is_for_in);
  MAYBE_RETURN(accumulator.CollectKeys(object, object),
               MaybeHandle<FixedArray>());
  return accumulator.GetKeys(keys_conversion);
}

Handle<FixedArray> KeyAccumulator::GetKeys(GetKeysConversion convert) {
  if (keys_.is_null()) {
    return isolate_->factory()->empty_fixed_array();
  }
  if (mode_ == KeyCollectionMode::kOwnOnly &&
      keys_->map() == isolate_->heap()->fixed_array_map()) {
    return Handle<FixedArray>::cast(keys_);
  }
  USE(ContainsOnlyValidKeys);
  Handle<FixedArray> result =
      OrderedHashSet::ConvertToKeysArray(keys(), convert);
  DCHECK(ContainsOnlyValidKeys(result));
  return result;
}

void KeyAccumulator::AddKey(Object* key, AddKeyConversion convert) {
  AddKey(handle(key, isolate_), convert);
}

void KeyAccumulator::AddKey(Handle<Object> key, AddKeyConversion convert) {
  if (key->IsSymbol()) {
    if (filter_ & SKIP_SYMBOLS) return;
    if (Handle<Symbol>::cast(key)->is_private()) return;
  } else if (filter_ & SKIP_STRINGS) {
    return;
  }
  if (keys_.is_null()) {
    keys_ = OrderedHashSet::Allocate(isolate_, 16);
  }
  uint32_t index;
  if (convert == CONVERT_TO_ARRAY_INDEX && key->IsString() &&
      Handle<String>::cast(key)->AsArrayIndex(&index)) {
    key = isolate_->factory()->NewNumberFromUint(index);
  }
  keys_ = OrderedHashSet::Add(keys(), key);
}

void KeyAccumulator::AddKeys(Handle<FixedArray> array,
                             AddKeyConversion convert) {
  int add_length = array->length();
  for (int i = 0; i < add_length; i++) {
    Handle<Object> current(array->get(i), isolate_);
    AddKey(current, convert);
  }
}

void KeyAccumulator::AddKeys(Handle<JSObject> array_like,
                             AddKeyConversion convert) {
  DCHECK(array_like->IsJSArray() || array_like->HasSloppyArgumentsElements());
  ElementsAccessor* accessor = array_like->GetElementsAccessor();
  accessor->AddElementsToKeyAccumulator(array_like, this, convert);
}

MaybeHandle<FixedArray> FilterProxyKeys(Isolate* isolate, Handle<JSProxy> owner,
                                        Handle<FixedArray> keys,
                                        PropertyFilter filter) {
  if (filter == ALL_PROPERTIES) {
    // Nothing to do.
    return keys;
  }
  int store_position = 0;
  for (int i = 0; i < keys->length(); ++i) {
    Handle<Name> key(Name::cast(keys->get(i)), isolate);
    if (key->FilterKey(filter)) continue;  // Skip this key.
    if (filter & ONLY_ENUMERABLE) {
      PropertyDescriptor desc;
      Maybe<bool> found =
          JSProxy::GetOwnPropertyDescriptor(isolate, owner, key, &desc);
      MAYBE_RETURN(found, MaybeHandle<FixedArray>());
      if (!found.FromJust() || !desc.enumerable()) continue;  // Skip this key.
    }
    // Keep this key.
    if (store_position != i) {
      keys->set(store_position, *key);
    }
    store_position++;
  }
  if (store_position == 0) return isolate->factory()->empty_fixed_array();
  keys->Shrink(store_position);
  return keys;
}

// Returns "nothing" in case of exception, "true" on success.
Maybe<bool> KeyAccumulator::AddKeysFromJSProxy(Handle<JSProxy> proxy,
                                               Handle<FixedArray> keys) {
  if (filter_proxy_keys_) {
    DCHECK(!is_for_in_);
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, keys, FilterProxyKeys(isolate_, proxy, keys, filter_),
        Nothing<bool>());
  }
  if (mode_ == KeyCollectionMode::kOwnOnly && !is_for_in_) {
    // If we collect only the keys from a JSProxy do not sort or deduplicate it.
    keys_ = keys;
    return Just(true);
  }
  AddKeys(keys, is_for_in_ ? CONVERT_TO_ARRAY_INDEX : DO_NOT_CONVERT);
  return Just(true);
}

Maybe<bool> KeyAccumulator::CollectKeys(Handle<JSReceiver> receiver,
                                        Handle<JSReceiver> object) {
  // Proxies have no hidden prototype and we should not trigger the
  // [[GetPrototypeOf]] trap on the last iteration when using
  // AdvanceFollowingProxies.
  if (mode_ == KeyCollectionMode::kOwnOnly && object->IsJSProxy()) {
    MAYBE_RETURN(CollectOwnJSProxyKeys(receiver, Handle<JSProxy>::cast(object)),
                 Nothing<bool>());
    return Just(true);
  }

  PrototypeIterator::WhereToEnd end = mode_ == KeyCollectionMode::kOwnOnly
                                          ? PrototypeIterator::END_AT_NON_HIDDEN
                                          : PrototypeIterator::END_AT_NULL;
  for (PrototypeIterator iter(isolate_, object, kStartAtReceiver, end);
       !iter.IsAtEnd();) {
    Handle<JSReceiver> current =
        PrototypeIterator::GetCurrent<JSReceiver>(iter);
    Maybe<bool> result = Just(false);  // Dummy initialization.
    if (current->IsJSProxy()) {
      result = CollectOwnJSProxyKeys(receiver, Handle<JSProxy>::cast(current));
    } else {
      DCHECK(current->IsJSObject());
      result = CollectOwnKeys(receiver, Handle<JSObject>::cast(current));
    }
    MAYBE_RETURN(result, Nothing<bool>());
    if (!result.FromJust()) break;  // |false| means "stop iterating".
    // Iterate through proxies but ignore access checks for the ALL_CAN_READ
    // case on API objects for OWN_ONLY keys handled in CollectOwnKeys.
    if (!iter.AdvanceFollowingProxiesIgnoringAccessChecks()) {
      return Nothing<bool>();
    }
    if (!last_non_empty_prototype_.is_null() &&
        *last_non_empty_prototype_ == *current) {
      break;
    }
  }
  return Just(true);
}

namespace {

void TrySettingEmptyEnumCache(JSReceiver* object) {
  Map* map = object->map();
  DCHECK_EQ(kInvalidEnumCacheSentinel, map->EnumLength());
  if (!map->OnlyHasSimpleProperties()) return;
  if (map->IsJSProxyMap()) return;
  if (map->NumberOfOwnDescriptors() > 0) {
    int number_of_enumerable_own_properties =
        map->NumberOfDescribedProperties(OWN_DESCRIPTORS, ENUMERABLE_STRINGS);
    if (number_of_enumerable_own_properties > 0) return;
  }
  DCHECK(object->IsJSObject());
  map->SetEnumLength(0);
}

bool CheckAndInitalizeSimpleEnumCache(JSReceiver* object) {
  if (object->map()->EnumLength() == kInvalidEnumCacheSentinel) {
    TrySettingEmptyEnumCache(object);
  }
  if (object->map()->EnumLength() != 0) return false;
  DCHECK(object->IsJSObject());
  return !JSObject::cast(object)->HasEnumerableElements();
}
}  // namespace

void FastKeyAccumulator::Prepare() {
  DisallowHeapAllocation no_gc;
  // Directly go for the fast path for OWN_ONLY keys.
  if (mode_ == KeyCollectionMode::kOwnOnly) return;
  // Fully walk the prototype chain and find the last prototype with keys.
  is_receiver_simple_enum_ = false;
  has_empty_prototype_ = true;
  JSReceiver* last_prototype = nullptr;
  for (PrototypeIterator iter(isolate_, *receiver_); !iter.IsAtEnd();
       iter.Advance()) {
    JSReceiver* current = iter.GetCurrent<JSReceiver>();
    bool has_no_properties = CheckAndInitalizeSimpleEnumCache(current);
    if (has_no_properties) continue;
    last_prototype = current;
    has_empty_prototype_ = false;
  }
  if (has_empty_prototype_) {
    is_receiver_simple_enum_ =
        receiver_->map()->EnumLength() != kInvalidEnumCacheSentinel &&
        !JSObject::cast(*receiver_)->HasEnumerableElements();
  } else if (last_prototype != nullptr) {
    last_non_empty_prototype_ = handle(last_prototype, isolate_);
  }
}

namespace {
static Handle<FixedArray> ReduceFixedArrayTo(Isolate* isolate,
                                             Handle<FixedArray> array,
                                             int length) {
  DCHECK_LE(length, array->length());
  if (array->length() == length) return array;
  return isolate->factory()->CopyFixedArrayUpTo(array, length);
}

Handle<FixedArray> GetFastEnumPropertyKeys(Isolate* isolate,
                                           Handle<JSObject> object) {
  Handle<Map> map(object->map());
  bool cache_enum_length = map->OnlyHasSimpleProperties();

  Handle<DescriptorArray> descs =
      Handle<DescriptorArray>(map->instance_descriptors(), isolate);
  int own_property_count = map->EnumLength();
  // If the enum length of the given map is set to kInvalidEnumCache, this
  // means that the map itself has never used the present enum cache. The
  // first step to using the cache is to set the enum length of the map by
  // counting the number of own descriptors that are ENUMERABLE_STRINGS.
  if (own_property_count == kInvalidEnumCacheSentinel) {
    own_property_count =
        map->NumberOfDescribedProperties(OWN_DESCRIPTORS, ENUMERABLE_STRINGS);
  } else {
    DCHECK(
        own_property_count ==
        map->NumberOfDescribedProperties(OWN_DESCRIPTORS, ENUMERABLE_STRINGS));
  }

  if (descs->HasEnumCache()) {
    Handle<FixedArray> keys(descs->GetEnumCache(), isolate);
    // In case the number of properties required in the enum are actually
    // present, we can reuse the enum cache. Otherwise, this means that the
    // enum cache was generated for a previous (smaller) version of the
    // Descriptor Array. In that case we regenerate the enum cache.
    if (own_property_count <= keys->length()) {
      isolate->counters()->enum_cache_hits()->Increment();
      if (cache_enum_length) map->SetEnumLength(own_property_count);
      return ReduceFixedArrayTo(isolate, keys, own_property_count);
    }
  }

  if (descs->IsEmpty()) {
    isolate->counters()->enum_cache_hits()->Increment();
    if (cache_enum_length) map->SetEnumLength(0);
    return isolate->factory()->empty_fixed_array();
  }

  isolate->counters()->enum_cache_misses()->Increment();

  Handle<FixedArray> storage =
      isolate->factory()->NewFixedArray(own_property_count);
  Handle<FixedArray> indices =
      isolate->factory()->NewFixedArray(own_property_count);

  int size = map->NumberOfOwnDescriptors();
  int index = 0;

  for (int i = 0; i < size; i++) {
    PropertyDetails details = descs->GetDetails(i);
    if (details.IsDontEnum()) continue;
    Object* key = descs->GetKey(i);
    if (key->IsSymbol()) continue;
    storage->set(index, key);
    if (!indices.is_null()) {
      if (details.type() != DATA) {
        indices = Handle<FixedArray>();
      } else {
        FieldIndex field_index = FieldIndex::ForDescriptor(*map, i);
        int load_by_field_index = field_index.GetLoadByFieldIndex();
        indices->set(index, Smi::FromInt(load_by_field_index));
      }
    }
    index++;
  }
  DCHECK(index == storage->length());

  DescriptorArray::SetEnumCache(descs, isolate, storage, indices);
  if (cache_enum_length) {
    map->SetEnumLength(own_property_count);
  }
  return storage;
}

template <bool fast_properties>
Handle<FixedArray> GetOwnKeysWithElements(Isolate* isolate,
                                          Handle<JSObject> object,
                                          GetKeysConversion convert) {
  Handle<FixedArray> keys;
  ElementsAccessor* accessor = object->GetElementsAccessor();
  if (fast_properties) {
    keys = GetFastEnumPropertyKeys(isolate, object);
  } else {
    // TODO(cbruni): preallocate big enough array to also hold elements.
    keys = KeyAccumulator::GetEnumPropertyKeys(isolate, object);
  }
  Handle<FixedArray> result =
      accessor->PrependElementIndices(object, keys, convert, ONLY_ENUMERABLE);

  if (FLAG_trace_for_in_enumerate) {
    PrintF("| strings=%d symbols=0 elements=%u || prototypes>=1 ||\n",
           keys->length(), result->length() - keys->length());
  }
  return result;
}

MaybeHandle<FixedArray> GetOwnKeysWithUninitializedEnumCache(
    Isolate* isolate, Handle<JSObject> object) {
  // Uninitalized enum cache
  Map* map = object->map();
  if (object->elements() != isolate->heap()->empty_fixed_array() ||
      object->elements() != isolate->heap()->empty_slow_element_dictionary()) {
    // Assume that there are elements.
    return MaybeHandle<FixedArray>();
  }
  int number_of_own_descriptors = map->NumberOfOwnDescriptors();
  if (number_of_own_descriptors == 0) {
    map->SetEnumLength(0);
    return isolate->factory()->empty_fixed_array();
  }
  // We have no elements but possibly enumerable property keys, hence we can
  // directly initialize the enum cache.
  return GetFastEnumPropertyKeys(isolate, object);
}

bool OnlyHasSimpleProperties(Map* map) {
  return map->instance_type() > LAST_CUSTOM_ELEMENTS_RECEIVER;
}

}  // namespace

MaybeHandle<FixedArray> FastKeyAccumulator::GetKeys(
    GetKeysConversion keys_conversion) {
  Handle<FixedArray> keys;
  if (filter_ == ENUMERABLE_STRINGS &&
      GetKeysFast(keys_conversion).ToHandle(&keys)) {
    return keys;
  }
  return GetKeysSlow(keys_conversion);
}

MaybeHandle<FixedArray> FastKeyAccumulator::GetKeysFast(
    GetKeysConversion keys_conversion) {
  bool own_only = has_empty_prototype_ || mode_ == KeyCollectionMode::kOwnOnly;
  Map* map = receiver_->map();
  if (!own_only || !OnlyHasSimpleProperties(map)) {
    return MaybeHandle<FixedArray>();
  }

  // From this point on we are certiain to only collect own keys.
  DCHECK(receiver_->IsJSObject());
  Handle<JSObject> object = Handle<JSObject>::cast(receiver_);

  // Do not try to use the enum-cache for dict-mode objects.
  if (map->is_dictionary_map()) {
    return GetOwnKeysWithElements<false>(isolate_, object, keys_conversion);
  }
  int enum_length = receiver_->map()->EnumLength();
  if (enum_length == kInvalidEnumCacheSentinel) {
    Handle<FixedArray> keys;
    // Try initializing the enum cache and return own properties.
    if (GetOwnKeysWithUninitializedEnumCache(isolate_, object)
            .ToHandle(&keys)) {
      if (FLAG_trace_for_in_enumerate) {
        PrintF("| strings=%d symbols=0 elements=0 || prototypes>=1 ||\n",
               keys->length());
      }
      is_receiver_simple_enum_ =
          object->map()->EnumLength() != kInvalidEnumCacheSentinel;
      return keys;
    }
  }
  // The properties-only case failed because there were probably elements on the
  // receiver.
  return GetOwnKeysWithElements<true>(isolate_, object, keys_conversion);
}

MaybeHandle<FixedArray> FastKeyAccumulator::GetKeysSlow(
    GetKeysConversion keys_conversion) {
  KeyAccumulator accumulator(isolate_, mode_, filter_);
  accumulator.set_filter_proxy_keys(filter_proxy_keys_);
  accumulator.set_is_for_in(is_for_in_);
  accumulator.set_last_non_empty_prototype(last_non_empty_prototype_);

  MAYBE_RETURN(accumulator.CollectKeys(receiver_, receiver_),
               MaybeHandle<FixedArray>());
  return accumulator.GetKeys(keys_conversion);
}

namespace {

enum IndexedOrNamed { kIndexed, kNamed };

// Returns |true| on success, |nothing| on exception.
template <class Callback, IndexedOrNamed type>
Maybe<bool> CollectInterceptorKeysInternal(Handle<JSReceiver> receiver,
                                           Handle<JSObject> object,
                                           Handle<InterceptorInfo> interceptor,
                                           KeyAccumulator* accumulator) {
  Isolate* isolate = accumulator->isolate();
  PropertyCallbackArguments args(isolate, interceptor->data(), *receiver,
                                 *object, Object::DONT_THROW);
  Handle<JSObject> result;
  if (!interceptor->enumerator()->IsUndefined(isolate)) {
    Callback enum_fun = v8::ToCData<Callback>(interceptor->enumerator());
    const char* log_tag = type == kIndexed ? "interceptor-indexed-enum"
                                           : "interceptor-named-enum";
    LOG(isolate, ApiObjectAccess(log_tag, *object));
    result = args.Call(enum_fun);
  }
  RETURN_VALUE_IF_SCHEDULED_EXCEPTION(isolate, Nothing<bool>());
  if (result.is_null()) return Just(true);
  accumulator->AddKeys(
      result, type == kIndexed ? CONVERT_TO_ARRAY_INDEX : DO_NOT_CONVERT);
  return Just(true);
}

template <class Callback, IndexedOrNamed type>
Maybe<bool> CollectInterceptorKeys(Handle<JSReceiver> receiver,
                                   Handle<JSObject> object,
                                   KeyAccumulator* accumulator) {
  Isolate* isolate = accumulator->isolate();
  if (type == kIndexed) {
    if (!object->HasIndexedInterceptor()) return Just(true);
  } else {
    if (!object->HasNamedInterceptor()) return Just(true);
  }
  Handle<InterceptorInfo> interceptor(type == kIndexed
                                          ? object->GetIndexedInterceptor()
                                          : object->GetNamedInterceptor(),
                                      isolate);
  if ((accumulator->filter() & ONLY_ALL_CAN_READ) &&
      !interceptor->all_can_read()) {
    return Just(true);
  }
  return CollectInterceptorKeysInternal<Callback, type>(
      receiver, object, interceptor, accumulator);
}

}  // namespace

Maybe<bool> KeyAccumulator::CollectOwnElementIndices(
    Handle<JSReceiver> receiver, Handle<JSObject> object) {
  if (filter_ & SKIP_STRINGS || skip_indices_) return Just(true);

  ElementsAccessor* accessor = object->GetElementsAccessor();
  accessor->CollectElementIndices(object, this);

  return CollectInterceptorKeys<v8::IndexedPropertyEnumeratorCallback,
                                kIndexed>(receiver, object, this);
}

namespace {

template <bool skip_symbols>
int CollectOwnPropertyNamesInternal(Handle<JSObject> object,
                                    KeyAccumulator* keys,
                                    Handle<DescriptorArray> descs,
                                    int start_index, int limit) {
  int first_skipped = -1;
  for (int i = start_index; i < limit; i++) {
    PropertyDetails details = descs->GetDetails(i);
    if ((details.attributes() & keys->filter()) != 0) continue;
    if (keys->filter() & ONLY_ALL_CAN_READ) {
      if (details.kind() != kAccessor) continue;
      Object* accessors = descs->GetValue(i);
      if (!accessors->IsAccessorInfo()) continue;
      if (!AccessorInfo::cast(accessors)->all_can_read()) continue;
    }
    Name* key = descs->GetKey(i);
    if (skip_symbols == key->IsSymbol()) {
      if (first_skipped == -1) first_skipped = i;
      continue;
    }
    if (key->FilterKey(keys->filter())) continue;
    keys->AddKey(key, DO_NOT_CONVERT);
  }
  return first_skipped;
}

}  // namespace

Maybe<bool> KeyAccumulator::CollectOwnPropertyNames(Handle<JSReceiver> receiver,
                                                    Handle<JSObject> object) {
  if (filter_ == ENUMERABLE_STRINGS) {
    Handle<FixedArray> enum_keys =
        KeyAccumulator::GetEnumPropertyKeys(isolate_, object);
    AddKeys(enum_keys, DO_NOT_CONVERT);
  } else {
    if (object->HasFastProperties()) {
      int limit = object->map()->NumberOfOwnDescriptors();
      Handle<DescriptorArray> descs(object->map()->instance_descriptors(),
                                    isolate_);
      // First collect the strings,
      int first_symbol =
          CollectOwnPropertyNamesInternal<true>(object, this, descs, 0, limit);
      // then the symbols.
      if (first_symbol != -1) {
        CollectOwnPropertyNamesInternal<false>(object, this, descs,
                                               first_symbol, limit);
      }
    } else if (object->IsJSGlobalObject()) {
      GlobalDictionary::CollectKeysTo(
          handle(object->global_dictionary(), isolate_), this, filter_);
    } else {
      NameDictionary::CollectKeysTo(
          handle(object->property_dictionary(), isolate_), this, filter_);
    }
  }
  // Add the property keys from the interceptor.
  return CollectInterceptorKeys<v8::GenericNamedPropertyEnumeratorCallback,
                                kNamed>(receiver, object, this);
}

Maybe<bool> KeyAccumulator::CollectAccessCheckInterceptorKeys(
    Handle<AccessCheckInfo> access_check_info, Handle<JSReceiver> receiver,
    Handle<JSObject> object) {
  MAYBE_RETURN(
      (CollectInterceptorKeysInternal<v8::IndexedPropertyEnumeratorCallback,
                                      kIndexed>(
          receiver, object,
          handle(
              InterceptorInfo::cast(access_check_info->indexed_interceptor()),
              isolate_),
          this)),
      Nothing<bool>());
  MAYBE_RETURN(
      (CollectInterceptorKeysInternal<
          v8::GenericNamedPropertyEnumeratorCallback, kNamed>(
          receiver, object,
          handle(InterceptorInfo::cast(access_check_info->named_interceptor()),
                 isolate_),
          this)),
      Nothing<bool>());
  return Just(true);
}

// Returns |true| on success, |false| if prototype walking should be stopped,
// |nothing| if an exception was thrown.
Maybe<bool> KeyAccumulator::CollectOwnKeys(Handle<JSReceiver> receiver,
                                           Handle<JSObject> object) {
  // Check access rights if required.
  if (object->IsAccessCheckNeeded() &&
      !isolate_->MayAccess(handle(isolate_->context()), object)) {
    // The cross-origin spec says that [[Enumerate]] shall return an empty
    // iterator when it doesn't have access...
    if (mode_ == KeyCollectionMode::kIncludePrototypes) {
      return Just(false);
    }
    // ...whereas [[OwnPropertyKeys]] shall return whitelisted properties.
    DCHECK(KeyCollectionMode::kOwnOnly == mode_);
    Handle<AccessCheckInfo> access_check_info;
    {
      DisallowHeapAllocation no_gc;
      AccessCheckInfo* maybe_info = AccessCheckInfo::Get(isolate_, object);
      if (maybe_info) access_check_info = handle(maybe_info, isolate_);
    }
    // We always have both kinds of interceptors or none.
    if (!access_check_info.is_null() &&
        access_check_info->named_interceptor()) {
      MAYBE_RETURN(CollectAccessCheckInterceptorKeys(access_check_info,
                                                     receiver, object),
                   Nothing<bool>());
      return Just(false);
    }
    filter_ = static_cast<PropertyFilter>(filter_ | ONLY_ALL_CAN_READ);
  }
  MAYBE_RETURN(CollectOwnElementIndices(receiver, object), Nothing<bool>());
  MAYBE_RETURN(CollectOwnPropertyNames(receiver, object), Nothing<bool>());
  return Just(true);
}

// static
Handle<FixedArray> KeyAccumulator::GetEnumPropertyKeys(
    Isolate* isolate, Handle<JSObject> object) {
  if (object->HasFastProperties()) {
    return GetFastEnumPropertyKeys(isolate, object);
  } else if (object->IsJSGlobalObject()) {
    Handle<GlobalDictionary> dictionary(object->global_dictionary(), isolate);
    int length = dictionary->NumberOfEnumElements();
    if (length == 0) {
      return isolate->factory()->empty_fixed_array();
    }
    Handle<FixedArray> storage = isolate->factory()->NewFixedArray(length);
    dictionary->CopyEnumKeysTo(*storage);
    return storage;
  } else {
    Handle<NameDictionary> dictionary(object->property_dictionary(), isolate);
    int length = dictionary->NumberOfEnumElements();
    if (length == 0) {
      return isolate->factory()->empty_fixed_array();
    }
    Handle<FixedArray> storage = isolate->factory()->NewFixedArray(length);
    dictionary->CopyEnumKeysTo(*storage);
    return storage;
  }
}

// ES6 9.5.12
// Returns |true| on success, |nothing| in case of exception.
Maybe<bool> KeyAccumulator::CollectOwnJSProxyKeys(Handle<JSReceiver> receiver,
                                                  Handle<JSProxy> proxy) {
  STACK_CHECK(isolate_, Nothing<bool>());
  // 1. Let handler be the value of the [[ProxyHandler]] internal slot of O.
  Handle<Object> handler(proxy->handler(), isolate_);
  // 2. If handler is null, throw a TypeError exception.
  // 3. Assert: Type(handler) is Object.
  if (proxy->IsRevoked()) {
    isolate_->Throw(*isolate_->factory()->NewTypeError(
        MessageTemplate::kProxyRevoked, isolate_->factory()->ownKeys_string()));
    return Nothing<bool>();
  }
  // 4. Let target be the value of the [[ProxyTarget]] internal slot of O.
  Handle<JSReceiver> target(proxy->target(), isolate_);
  // 5. Let trap be ? GetMethod(handler, "ownKeys").
  Handle<Object> trap;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate_, trap, Object::GetMethod(Handle<JSReceiver>::cast(handler),
                                        isolate_->factory()->ownKeys_string()),
      Nothing<bool>());
  // 6. If trap is undefined, then
  if (trap->IsUndefined(isolate_)) {
    // 6a. Return target.[[OwnPropertyKeys]]().
    return CollectOwnJSProxyTargetKeys(proxy, target);
  }
  // 7. Let trapResultArray be Call(trap, handler, «target»).
  Handle<Object> trap_result_array;
  Handle<Object> args[] = {target};
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate_, trap_result_array,
      Execution::Call(isolate_, trap, handler, arraysize(args), args),
      Nothing<bool>());
  // 8. Let trapResult be ? CreateListFromArrayLike(trapResultArray,
  //    «String, Symbol»).
  Handle<FixedArray> trap_result;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate_, trap_result,
      Object::CreateListFromArrayLike(isolate_, trap_result_array,
                                      ElementTypes::kStringAndSymbol),
      Nothing<bool>());
  // 9. Let extensibleTarget be ? IsExtensible(target).
  Maybe<bool> maybe_extensible = JSReceiver::IsExtensible(target);
  MAYBE_RETURN(maybe_extensible, Nothing<bool>());
  bool extensible_target = maybe_extensible.FromJust();
  // 10. Let targetKeys be ? target.[[OwnPropertyKeys]]().
  Handle<FixedArray> target_keys;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate_, target_keys,
                                   JSReceiver::OwnPropertyKeys(target),
                                   Nothing<bool>());
  // 11. (Assert)
  // 12. Let targetConfigurableKeys be an empty List.
  // To save memory, we're re-using target_keys and will modify it in-place.
  Handle<FixedArray> target_configurable_keys = target_keys;
  // 13. Let targetNonconfigurableKeys be an empty List.
  Handle<FixedArray> target_nonconfigurable_keys =
      isolate_->factory()->NewFixedArray(target_keys->length());
  int nonconfigurable_keys_length = 0;
  // 14. Repeat, for each element key of targetKeys:
  for (int i = 0; i < target_keys->length(); ++i) {
    // 14a. Let desc be ? target.[[GetOwnProperty]](key).
    PropertyDescriptor desc;
    Maybe<bool> found = JSReceiver::GetOwnPropertyDescriptor(
        isolate_, target, handle(target_keys->get(i), isolate_), &desc);
    MAYBE_RETURN(found, Nothing<bool>());
    // 14b. If desc is not undefined and desc.[[Configurable]] is false, then
    if (found.FromJust() && !desc.configurable()) {
      // 14b i. Append key as an element of targetNonconfigurableKeys.
      target_nonconfigurable_keys->set(nonconfigurable_keys_length,
                                       target_keys->get(i));
      nonconfigurable_keys_length++;
      // The key was moved, null it out in the original list.
      target_keys->set(i, Smi::FromInt(0));
    } else {
      // 14c. Else,
      // 14c i. Append key as an element of targetConfigurableKeys.
      // (No-op, just keep it in |target_keys|.)
    }
  }
  // 15. If extensibleTarget is true and targetNonconfigurableKeys is empty,
  //     then:
  if (extensible_target && nonconfigurable_keys_length == 0) {
    // 15a. Return trapResult.
    return AddKeysFromJSProxy(proxy, trap_result);
  }
  // 16. Let uncheckedResultKeys be a new List which is a copy of trapResult.
  Zone set_zone(isolate_->allocator());
  const int kPresent = 1;
  const int kGone = 0;
  IdentityMap<int> unchecked_result_keys(isolate_->heap(), &set_zone);
  int unchecked_result_keys_size = 0;
  for (int i = 0; i < trap_result->length(); ++i) {
    DCHECK(trap_result->get(i)->IsUniqueName());
    Object* key = trap_result->get(i);
    int* entry = unchecked_result_keys.Get(key);
    if (*entry != kPresent) {
      *entry = kPresent;
      unchecked_result_keys_size++;
    }
  }
  // 17. Repeat, for each key that is an element of targetNonconfigurableKeys:
  for (int i = 0; i < nonconfigurable_keys_length; ++i) {
    Object* key = target_nonconfigurable_keys->get(i);
    // 17a. If key is not an element of uncheckedResultKeys, throw a
    //      TypeError exception.
    int* found = unchecked_result_keys.Find(key);
    if (found == nullptr || *found == kGone) {
      isolate_->Throw(*isolate_->factory()->NewTypeError(
          MessageTemplate::kProxyOwnKeysMissing, handle(key, isolate_)));
      return Nothing<bool>();
    }
    // 17b. Remove key from uncheckedResultKeys.
    *found = kGone;
    unchecked_result_keys_size--;
  }
  // 18. If extensibleTarget is true, return trapResult.
  if (extensible_target) {
    return AddKeysFromJSProxy(proxy, trap_result);
  }
  // 19. Repeat, for each key that is an element of targetConfigurableKeys:
  for (int i = 0; i < target_configurable_keys->length(); ++i) {
    Object* key = target_configurable_keys->get(i);
    if (key->IsSmi()) continue;  // Zapped entry, was nonconfigurable.
    // 19a. If key is not an element of uncheckedResultKeys, throw a
    //      TypeError exception.
    int* found = unchecked_result_keys.Find(key);
    if (found == nullptr || *found == kGone) {
      isolate_->Throw(*isolate_->factory()->NewTypeError(
          MessageTemplate::kProxyOwnKeysMissing, handle(key, isolate_)));
      return Nothing<bool>();
    }
    // 19b. Remove key from uncheckedResultKeys.
    *found = kGone;
    unchecked_result_keys_size--;
  }
  // 20. If uncheckedResultKeys is not empty, throw a TypeError exception.
  if (unchecked_result_keys_size != 0) {
    DCHECK_GT(unchecked_result_keys_size, 0);
    isolate_->Throw(*isolate_->factory()->NewTypeError(
        MessageTemplate::kProxyOwnKeysNonExtensible));
    return Nothing<bool>();
  }
  // 21. Return trapResult.
  return AddKeysFromJSProxy(proxy, trap_result);
}

Maybe<bool> KeyAccumulator::CollectOwnJSProxyTargetKeys(
    Handle<JSProxy> proxy, Handle<JSReceiver> target) {
  // TODO(cbruni): avoid creating another KeyAccumulator
  Handle<FixedArray> keys;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate_, keys,
      KeyAccumulator::GetKeys(target, KeyCollectionMode::kOwnOnly, filter_,
                              GetKeysConversion::kConvertToString,
                              filter_proxy_keys_, is_for_in_),
      Nothing<bool>());
  bool prev_filter_proxy_keys_ = filter_proxy_keys_;
  filter_proxy_keys_ = false;
  Maybe<bool> result = AddKeysFromJSProxy(proxy, keys);
  filter_proxy_keys_ = prev_filter_proxy_keys_;
  return result;
}

}  // namespace internal
}  // namespace v8
