// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/handles.h"

#include "src/api.h"
#include "src/isolate.h"

namespace v8 {
namespace internal {

HandleBase::HandleBase(HeapObject* object)
    : HandleBase(object, object->GetIsolate()) {}


HandleBase::HandleBase(Object* object, Isolate* isolate)
    : HandleBase(HandleScope::CreateHandle(isolate, object)) {}


#ifdef DEBUG

bool HandleBase::IsDereferenceAllowed(DereferenceCheckMode mode) const {
  DCHECK_NOT_NULL(location_);
  Object* const object = *location_;
  if (object->IsSmi()) return true;
  HeapObject* const heap_object = HeapObject::cast(object);
  Heap* const heap = heap_object->GetHeap();
  Object** roots_array_start = heap->roots_array_start();
  if (roots_array_start <= location_ &&
      location_ < roots_array_start + Heap::kStrongRootListLength &&
      heap->RootCanBeTreatedAsConstant(
          static_cast<Heap::RootListIndex>(location_ - roots_array_start))) {
    return true;
  }
  if (!AllowHandleDereference::IsAllowed()) return false;
  if (mode == INCLUDE_DEFERRED_CHECK &&
      !AllowDeferredHandleDereference::IsAllowed()) {
    // Accessing cells, maps and internalized strings is safe.
    if (heap_object->IsCell()) return true;
    if (heap_object->IsMap()) return true;
    if (heap_object->IsInternalizedString()) return true;
    return !heap->isolate()->IsDeferredHandle(location_);
  }
  return true;
}

#endif  // DEBUG


HandleScope::HandleScope(Isolate* isolate) : isolate_(isolate) {
  HandleScopeData* const current = isolate->handle_scope_data();
  prev_next_ = current->next;
  prev_limit_ = current->limit;
  current->level++;
}


HandleScope::~HandleScope() { CloseScope(isolate_, prev_next_, prev_limit_); }


// static
int HandleScope::NumberOfHandles(Isolate* isolate) {
  HandleScopeImplementer* impl = isolate->handle_scope_implementer();
  int n = impl->blocks()->length();
  if (n == 0) return 0;
  return ((n - 1) * kHandleBlockSize) + static_cast<int>(
      (isolate->handle_scope_data()->next - impl->blocks()->last()));
}


// static
Object** HandleScope::CreateHandle(Isolate* isolate, Object* value) {
  DCHECK(AllowHandleAllocation::IsAllowed());
  HandleScopeData* const current = isolate->handle_scope_data();

  Object** result = current->next;
  if (result == current->limit) result = Extend(isolate);
  // Update the current next field, set the value in the created
  // handle, and return the result.
  DCHECK_LT(result, current->limit);
  current->next = result + 1;

  *result = value;
  return result;
}


// static
void HandleScope::DeleteExtensions(Isolate* isolate) {
  HandleScopeData* const current = isolate->handle_scope_data();
  isolate->handle_scope_implementer()->DeleteExtensions(current->limit);
}


Handle<Object> HandleScope::CloseAndEscape(Handle<Object> handle) {
  HandleScopeData* const current = isolate_->handle_scope_data();

  Object* value = *handle;
  // Throw away all handles in the current scope.
  CloseScope(isolate_, prev_next_, prev_limit_);
  // Allocate one handle in the parent scope.
  DCHECK_LT(0, current->level);
  Handle<Object> result(CreateHandle(isolate_, value));
  // Reinitialize the current scope (so that it's ready
  // to be used or closed again).
  prev_next_ = current->next;
  prev_limit_ = current->limit;
  current->level++;
  return result;
}


// static
void HandleScope::CloseScope(Isolate* isolate, Object** prev_next,
                             Object** prev_limit) {
  HandleScopeData* const current = isolate->handle_scope_data();

  std::swap(current->next, prev_next);
  current->level--;
  if (current->limit != prev_limit) {
    current->limit = prev_limit;
    DeleteExtensions(isolate);
#ifdef ENABLE_HANDLE_ZAPPING
    ZapRange(current->next, prev_limit);
  } else {
    ZapRange(current->next, prev_next);
#endif
  }
}


// static
Object** HandleScope::Extend(Isolate* isolate) {
  HandleScopeData* current = isolate->handle_scope_data();

  Object** result = current->next;
  DCHECK_EQ(result, current->limit);
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
      DCHECK(limit - current->next < kHandleBlockSize);
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


#ifdef ENABLE_HANDLE_ZAPPING
void HandleScope::ZapRange(Object** start, Object** end) {
  DCHECK(end - start <= kHandleBlockSize);
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


DeferredHandleScope::DeferredHandleScope(Isolate* isolate)
    : impl_(isolate->handle_scope_implementer()) {
  impl_->BeginDeferredScope();
  HandleScopeData* data = impl_->isolate()->handle_scope_data();
  Object** new_next = impl_->GetSpareOrNewBlock();
  Object** new_limit = &new_next[kHandleBlockSize];
  DCHECK(data->limit == &impl_->blocks()->last()[kHandleBlockSize]);
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
  DCHECK(handles_detached_);
  DCHECK(impl_->isolate()->handle_scope_data()->level == prev_level_);
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


#ifdef DEBUG

SealHandleScope::SealHandleScope(Isolate* isolate) : isolate_(isolate) {
  // Make sure the current thread is allowed to create handles to begin with.
  CHECK(AllowHandleAllocation::IsAllowed());
  HandleScopeData* const current = isolate_->handle_scope_data();
  // Shrink the current handle scope to make it impossible to do
  // handle allocations without an explicit handle scope.
  limit_ = current->limit;
  current->limit = current->next;
  level_ = current->level;
  current->level = 0;
}


SealHandleScope::~SealHandleScope() {
  // Restore state in current handle scope to re-enable handle
  // allocations.
  HandleScopeData* const current = isolate_->handle_scope_data();
  DCHECK_EQ(0, current->level);
  current->level = level_;
  DCHECK_EQ(current->next, current->limit);
  current->limit = limit_;
}

#endif  // DEBUG

}  // namespace internal
}  // namespace v8
