// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_HEAP_PAGE_H_
#define V8_HEAP_CPPGC_HEAP_PAGE_H_

#include "src/base/iterator.h"
#include "src/base/macros.h"
#include "src/heap/cppgc/heap-object-header.h"

namespace cppgc {
namespace internal {

class BaseSpace;
class NormalPageSpace;
class LargePageSpace;
class Heap;
class PageBackend;

class V8_EXPORT_PRIVATE BasePage {
 public:
  static BasePage* FromPayload(void*);
  static const BasePage* FromPayload(const void*);

  BasePage(const BasePage&) = delete;
  BasePage& operator=(const BasePage&) = delete;

  Heap* heap() { return heap_; }
  const Heap* heap() const { return heap_; }

  BaseSpace* space() { return space_; }
  const BaseSpace* space() const { return space_; }
  void set_space(BaseSpace* space) { space_ = space; }

  bool is_large() const { return type_ == PageType::kLarge; }

 protected:
  enum class PageType { kNormal, kLarge };
  BasePage(Heap*, BaseSpace*, PageType);

 private:
  Heap* heap_;
  BaseSpace* space_;
  PageType type_;
};

class V8_EXPORT_PRIVATE NormalPage final : public BasePage {
  template <typename T>
  class IteratorImpl : v8::base::iterator<std::forward_iterator_tag, T> {
   public:
    explicit IteratorImpl(T* p) : p_(p) {}

    T& operator*() { return *p_; }
    const T& operator*() const { return *p_; }

    bool operator==(IteratorImpl other) const { return p_ == other.p_; }
    bool operator!=(IteratorImpl other) const { return !(*this == other); }

    IteratorImpl& operator++() {
      p_ += (p_->GetSize() / sizeof(T));
      return *this;
    }
    IteratorImpl operator++(int) {
      IteratorImpl temp(*this);
      p_ += (p_->GetSize() / sizeof(T));
      return temp;
    }

    T* base() { return p_; }

   private:
    T* p_;
  };

 public:
  using iterator = IteratorImpl<HeapObjectHeader>;
  using const_iterator = IteratorImpl<const HeapObjectHeader>;

  // Allocates a new page.
  static NormalPage* Create(NormalPageSpace*);
  // Destroys and frees the page. The page must be detached from the
  // corresponding space (i.e. be swept when called).
  static void Destroy(NormalPage*);

  iterator begin() {
    return iterator(reinterpret_cast<HeapObjectHeader*>(PayloadStart()));
  }
  const_iterator begin() const {
    return const_iterator(
        reinterpret_cast<const HeapObjectHeader*>(PayloadStart()));
  }
  iterator end() {
    return iterator(reinterpret_cast<HeapObjectHeader*>(PayloadEnd()));
  }
  const_iterator end() const {
    return const_iterator(
        reinterpret_cast<const HeapObjectHeader*>(PayloadEnd()));
  }

  Address PayloadStart();
  ConstAddress PayloadStart() const;
  Address PayloadEnd();
  ConstAddress PayloadEnd() const;

  static size_t PayloadSize();

 private:
  NormalPage(Heap* heap, BaseSpace* space);
  ~NormalPage();
};

class V8_EXPORT_PRIVATE LargePage final : public BasePage {
 public:
  // Allocates a new page.
  static LargePage* Create(LargePageSpace*, size_t);
  // Destroys and frees the page. The page must be detached from the
  // corresponding space (i.e. be swept when called).
  static void Destroy(LargePage*);

  HeapObjectHeader* ObjectHeader();
  const HeapObjectHeader* ObjectHeader() const;

  Address PayloadStart();
  ConstAddress PayloadStart() const;
  Address PayloadEnd();
  ConstAddress PayloadEnd() const;

  size_t PayloadSize() const { return payload_size_; }

 private:
  LargePage(Heap* heap, BaseSpace* space, size_t);
  ~LargePage();

  size_t payload_size_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_HEAP_PAGE_H_
