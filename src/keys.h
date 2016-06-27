// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_KEYS_H_
#define V8_KEYS_H_

#include "src/isolate.h"
#include "src/objects.h"

namespace v8 {
namespace internal {

enum AddKeyConversion { DO_NOT_CONVERT, CONVERT_TO_ARRAY_INDEX };

// This is a helper class for JSReceiver::GetKeys which collects and sorts keys.
// GetKeys needs to sort keys per prototype level, first showing the integer
// indices from elements then the strings from the properties. However, this
// does not apply to proxies which are in full control of how the keys are
// sorted.
//
// For performance reasons the KeyAccumulator internally separates integer keys
// in |elements_| into sorted lists per prototype level. String keys are
// collected in |string_properties_|, a single OrderedHashSet (similar for
// Symbols in |symbol_properties_|. To separate the keys per level later when
// assembling the final list, |levelLengths_| keeps track of the number of
// String and Symbol keys per level.
//
// Only unique keys are kept by the KeyAccumulator, strings are stored in a
// HashSet for inexpensive lookups. Integer keys are kept in sorted lists which
// are more compact and allow for reasonably fast includes check.
class KeyAccumulator final BASE_EMBEDDED {
 public:
  KeyAccumulator(Isolate* isolate, KeyCollectionMode mode,
                 PropertyFilter filter)
      : isolate_(isolate), mode_(mode), filter_(filter) {}
  ~KeyAccumulator();

  static MaybeHandle<FixedArray> GetKeys(
      Handle<JSReceiver> object, KeyCollectionMode mode, PropertyFilter filter,
      GetKeysConversion keys_conversion = GetKeysConversion::kKeepNumbers,
      bool filter_proxy_keys = true, bool is_for_in = false);

  Handle<FixedArray> GetKeys(
      GetKeysConversion convert = GetKeysConversion::kKeepNumbers);
  Maybe<bool> CollectKeys(Handle<JSReceiver> receiver,
                          Handle<JSReceiver> object);
  Maybe<bool> CollectOwnElementIndices(Handle<JSReceiver> receiver,
                                       Handle<JSObject> object);
  Maybe<bool> CollectOwnPropertyNames(Handle<JSReceiver> receiver,
                                      Handle<JSObject> object);
  Maybe<bool> CollectAccessCheckInterceptorKeys(
      Handle<AccessCheckInfo> access_check_info, Handle<JSReceiver> receiver,
      Handle<JSObject> object);

  static Handle<FixedArray> GetEnumPropertyKeys(Isolate* isolate,
                                                Handle<JSObject> object);

  void AddKey(Object* key, AddKeyConversion convert = DO_NOT_CONVERT);
  void AddKey(Handle<Object> key, AddKeyConversion convert = DO_NOT_CONVERT);
  void AddKeys(Handle<FixedArray> array, AddKeyConversion convert);
  void AddKeys(Handle<JSObject> array_like, AddKeyConversion convert);

  // Jump to the next level, pushing the current |levelLength_| to
  // |levelLengths_| and adding a new list to |elements_|.
  Isolate* isolate() { return isolate_; }
  PropertyFilter filter() { return filter_; }
  void set_filter_proxy_keys(bool filter) { filter_proxy_keys_ = filter; }
  void set_is_for_in(bool value) { is_for_in_ = value; }
  void set_skip_indices(bool value) { skip_indices_ = value; }
  void set_last_non_empty_prototype(Handle<JSReceiver> object) {
    last_non_empty_prototype_ = object;
  }

 private:
  Maybe<bool> CollectOwnKeys(Handle<JSReceiver> receiver,
                             Handle<JSObject> object);
  Maybe<bool> CollectOwnJSProxyKeys(Handle<JSReceiver> receiver,
                                    Handle<JSProxy> proxy);
  Maybe<bool> CollectOwnJSProxyTargetKeys(Handle<JSProxy> proxy,
                                          Handle<JSReceiver> target);

  Maybe<bool> AddKeysFromJSProxy(Handle<JSProxy> proxy,
                                 Handle<FixedArray> keys);

  Handle<OrderedHashSet> keys() { return Handle<OrderedHashSet>::cast(keys_); }

  Isolate* isolate_;
  // keys_ is either an Handle<OrderedHashSet> or in the case of own JSProxy
  // keys a Handle<FixedArray>.
  Handle<FixedArray> keys_;
  Handle<JSReceiver> last_non_empty_prototype_;
  KeyCollectionMode mode_;
  PropertyFilter filter_;
  bool filter_proxy_keys_ = true;
  bool is_for_in_ = false;
  bool skip_indices_ = false;

  DISALLOW_COPY_AND_ASSIGN(KeyAccumulator);
};

// The FastKeyAccumulator handles the cases where there are no elements on the
// prototype chain and forwords the complex/slow cases to the normal
// KeyAccumulator.
class FastKeyAccumulator {
 public:
  FastKeyAccumulator(Isolate* isolate, Handle<JSReceiver> receiver,
                     KeyCollectionMode mode, PropertyFilter filter)
      : isolate_(isolate), receiver_(receiver), mode_(mode), filter_(filter) {
    Prepare();
  }

  bool is_receiver_simple_enum() { return is_receiver_simple_enum_; }
  bool has_empty_prototype() { return has_empty_prototype_; }
  void set_filter_proxy_keys(bool filter) { filter_proxy_keys_ = filter; }
  void set_is_for_in(bool value) { is_for_in_ = value; }

  MaybeHandle<FixedArray> GetKeys(
      GetKeysConversion convert = GetKeysConversion::kKeepNumbers);

 private:
  void Prepare();
  MaybeHandle<FixedArray> GetKeysFast(GetKeysConversion convert);
  MaybeHandle<FixedArray> GetKeysSlow(GetKeysConversion convert);

  Isolate* isolate_;
  Handle<JSReceiver> receiver_;
  Handle<JSReceiver> last_non_empty_prototype_;
  KeyCollectionMode mode_;
  PropertyFilter filter_;
  bool filter_proxy_keys_ = true;
  bool is_for_in_ = false;
  bool is_receiver_simple_enum_ = false;
  bool has_empty_prototype_ = false;

  DISALLOW_COPY_AND_ASSIGN(FastKeyAccumulator);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_KEYS_H_
