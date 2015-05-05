// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HANDLES_H_
#define V8_HANDLES_H_

#include "src/objects.h"

namespace v8 {
namespace internal {

// Forward declarations.
class DeferredHandles;
class HandleScopeImplementer;


// A Handle can be converted into a MaybeHandle. Converting a MaybeHandle
// into a Handle requires checking that it does not point to NULL.  This
// ensures NULL checks before use.
// Do not use MaybeHandle as argument type.
template<typename T>
class MaybeHandle {
 public:
  V8_INLINE MaybeHandle() : location_(nullptr) {}

  // Constructor for handling automatic up casting from Handle.
  // Ex. Handle<JSArray> can be passed when MaybeHandle<Object> is expected.
  template <class S> MaybeHandle(Handle<S> handle) {
#ifdef DEBUG
    T* a = nullptr;
    S* b = nullptr;
    a = b;  // Fake assignment to enforce type checks.
    USE(a);
#endif
    this->location_ = reinterpret_cast<T**>(handle.location());
  }

  // Constructor for handling automatic up casting.
  // Ex. MaybeHandle<JSArray> can be passed when Handle<Object> is expected.
  template <class S> MaybeHandle(MaybeHandle<S> maybe_handle) {
#ifdef DEBUG
    T* a = NULL;
    S* b = NULL;
    a = b;  // Fake assignment to enforce type checks.
    USE(a);
#endif
    location_ = reinterpret_cast<T**>(maybe_handle.location_);
  }

  V8_INLINE void Assert() const { DCHECK_NOT_NULL(location_); }
  V8_INLINE void Check() const { CHECK_NOT_NULL(location_); }

  V8_INLINE Handle<T> ToHandleChecked() const {
    Check();
    return Handle<T>(location_);
  }

  // Convert to a Handle with a type that can be upcasted to.
  template <class S>
  V8_INLINE bool ToHandle(Handle<S>* out) const {
    if (location_ == NULL) {
      *out = Handle<T>::null();
      return false;
    } else {
      *out = Handle<T>(location_);
      return true;
    }
  }

  bool is_null() const { return location_ == NULL; }

 protected:
  T** location_;

  // MaybeHandles of different classes are allowed to access each
  // other's location_.
  template<class S> friend class MaybeHandle;
};


// Base class for Handles. Don't use this directly.
class HandleBase {
 public:
  V8_INLINE explicit HandleBase(Object** location = nullptr)
      : location_(location) {}
  V8_INLINE explicit HandleBase(HandleBase const& other)
      : location_(other.location_) {}
  explicit HandleBase(HeapObject* object);
  explicit HandleBase(Object* object, Isolate* isolate);
  V8_INLINE ~HandleBase() {}

  // Check if this handle refers to the exact same object as the other handle.
  V8_INLINE bool is_identical_to(HandleBase const& other) const {
    // Dereferencing deferred handles to check object equality is safe.
    SLOW_DCHECK(is_null() || IsDereferenceAllowed(NO_DEFERRED_CHECK));
    SLOW_DCHECK(other.is_null() ||
                other.IsDereferenceAllowed(NO_DEFERRED_CHECK));
    if (location_ == other.location_) return true;
    if (location_ == nullptr || other.location_ == nullptr) return false;
    return *location_ == *other.location_;
  }

  // Check if this handle is a NULL handle.
  V8_INLINE bool is_null() const { return location_ == nullptr; }

 protected:
  // Provides the C++ deference operator.
  V8_INLINE Object* operator*() const {
    SLOW_DCHECK(IsDereferenceAllowed(INCLUDE_DEFERRED_CHECK));
    return *location_;
  }

  // Returns the address to where the raw pointer is stored.
  V8_INLINE Object** location() const {
    SLOW_DCHECK(location_ == nullptr ||
                IsDereferenceAllowed(INCLUDE_DEFERRED_CHECK));
    return location_;
  }

  enum DereferenceCheckMode { INCLUDE_DEFERRED_CHECK, NO_DEFERRED_CHECK };
#ifdef DEBUG
  bool IsDereferenceAllowed(DereferenceCheckMode mode) const;
#else
  V8_INLINE bool IsDereferenceAllowed(DereferenceCheckMode) const {
    return true;
  }
#endif  // DEBUG

  Object** location_;
};


// ----------------------------------------------------------------------------
// A Handle provides a reference to an object that survives relocation by
// the garbage collector.
// Handles are only valid within a HandleScope.
// When a handle is created for an object a cell is allocated in the heap.
template <typename T>
class Handle final : public HandleBase {
 public:
  V8_INLINE explicit Handle(T** location)
      : HandleBase(reinterpret_cast<Object**>(location)) {}
  V8_INLINE explicit Handle(T* object) : HandleBase(object) {}
  V8_INLINE Handle(T* object, Isolate* isolate) : HandleBase(object, isolate) {}

  // TODO(yangguo): Values that contain empty handles should be declared as
  // MaybeHandle to force validation before being used as handles.
  V8_INLINE Handle() {}

  // Constructor for handling automatic up casting.
  // Ex. Handle<JSFunction> can be passed when Handle<Object> is expected.
  template <class S>
  V8_INLINE Handle(Handle<S> const& other)
      : HandleBase(other) {
    T* a = nullptr;
    S* b = nullptr;
    a = b;  // Fake assignment to enforce type checks.
    USE(a);
  }

  V8_INLINE T* operator->() const { return operator*(); }

  // Provides the C++ dereference operator.
  V8_INLINE T* operator*() const {
    return reinterpret_cast<T*>(HandleBase::operator*());
  }

  // Returns the address to where the raw pointer is stored.
  V8_INLINE T** location() const {
    return reinterpret_cast<T**>(HandleBase::location());
  }

  template <class S>
  V8_INLINE static Handle<T> cast(Handle<S> const& other) {
    T::cast(*reinterpret_cast<T**>(other.location_));
    return Handle<T>(reinterpret_cast<T**>(other.location_));
  }

  // TODO(yangguo): Values that contain empty handles should be declared as
  // MaybeHandle to force validation before being used as handles.
  static Handle<T> null() { return Handle<T>(); }

  // Closes the given scope, but lets this handle escape. See
  // implementation in api.h.
  inline Handle<T> EscapeFrom(v8::EscapableHandleScope* scope);

 private:
  // Handles of different classes are allowed to access each other's location_.
  template<class S> friend class Handle;
};


// Convenience wrapper.
template<class T>
inline Handle<T> handle(T* t, Isolate* isolate) {
  return Handle<T>(t, isolate);
}


// Convenience wrapper.
template <class T>
inline Handle<T> handle(T* t) {
  return Handle<T>(t);
}


// Key comparison function for Map handles.
inline bool operator<(const Handle<Map>& lhs, const Handle<Map>& rhs) {
  // This is safe because maps don't move.
  return *lhs < *rhs;
}


// A stack-allocated class that governs a number of local handles.
// After a handle scope has been created, all local handles will be
// allocated within that handle scope until either the handle scope is
// deleted or another handle scope is created.  If there is already a
// handle scope and a new one is created, all allocations will take
// place in the new handle scope until it is deleted.  After that,
// new handles will again be allocated in the original handle scope.
//
// After the handle scope of a local handle has been deleted the
// garbage collector will no longer track the object stored in the
// handle and may deallocate it.  The behavior of accessing a handle
// for which the handle scope has been deleted is undefined.
class HandleScope final {
 public:
  explicit HandleScope(Isolate* isolate);
  ~HandleScope();

  // Counts the number of allocated handles.
  static int NumberOfHandles(Isolate* isolate);

  // Creates a new handle with the given value.
  static Object** CreateHandle(Isolate* isolate, Object* value);
  template <typename T>
  static T** CreateHandle(Isolate* isolate, T* value) {
    return reinterpret_cast<T**>(
        CreateHandle(isolate, static_cast<Object*>(value)));
  }

  // Deallocates any extensions used by the current scope.
  static void DeleteExtensions(Isolate* isolate);

  static Address current_next_address(Isolate* isolate);
  static Address current_limit_address(Isolate* isolate);
  static Address current_level_address(Isolate* isolate);

  // Closes the HandleScope (invalidating all handles
  // created in the scope of the HandleScope) and returns
  // a Handle backed by the parent scope holding the
  // value of the argument handle.
  Handle<Object> CloseAndEscape(Handle<Object> handle);
  template <typename T>
  Handle<T> CloseAndEscape(Handle<T> handle) {
    return Handle<T>::cast(CloseAndEscape(Handle<Object>::cast(handle)));
  }

  Isolate* isolate() const { return isolate_; }

 private:
  friend class v8::HandleScope;
  friend class v8::internal::DeferredHandles;
  friend class v8::internal::HandleScopeImplementer;
  friend class v8::internal::Isolate;

  // Prevent heap allocation or illegal handle scopes.
  void* operator new(size_t size) = delete;
  void operator delete(void* size_t) = delete;

  // Close the handle scope resetting limits to a previous state.
  static void CloseScope(Isolate* isolate, Object** prev_next,
                         Object** prev_limit);

  // Extend the handle scope making room for more handles.
  static Object** Extend(Isolate* isolate);

#ifdef ENABLE_HANDLE_ZAPPING
  // Zaps the handles in the half-open interval [start, end).
  static void ZapRange(Object** start, Object** end);
#endif

  Isolate* const isolate_;
  Object** prev_next_;
  Object** prev_limit_;

  DISALLOW_COPY_AND_ASSIGN(HandleScope);
};


class DeferredHandleScope {
 public:
  explicit DeferredHandleScope(Isolate* isolate);
  // The DeferredHandles object returned stores the Handles created
  // since the creation of this DeferredHandleScope.  The Handles are
  // alive as long as the DeferredHandles object is alive.
  DeferredHandles* Detach();
  ~DeferredHandleScope();

 private:
  Object** prev_limit_;
  Object** prev_next_;
  HandleScopeImplementer* impl_;

#ifdef DEBUG
  bool handles_detached_;
  int prev_level_;
#endif

  friend class HandleScopeImplementer;
};


// Seal off the current HandleScope so that new handles can only be created
// if a new HandleScope is entered.
class SealHandleScope final {
 public:
#ifndef DEBUG
  explicit SealHandleScope(Isolate* isolate) {}
  ~SealHandleScope() {}
#else
  explicit SealHandleScope(Isolate* isolate);
  ~SealHandleScope();

 private:
  Isolate* const isolate_;
  Object** limit_;
  int level_;
#endif  // DEBUG

 private:
  DISALLOW_COPY_AND_ASSIGN(SealHandleScope);
};


struct HandleScopeData {
  internal::Object** next;
  internal::Object** limit;
  int level;

  void Initialize() {
    next = limit = nullptr;
    level = 0;
  }
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HANDLES_H_
