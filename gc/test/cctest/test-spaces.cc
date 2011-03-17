// Copyright 2006-2008 the V8 project authors. All rights reserved.
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

#include <stdlib.h>

#include "v8.h"
#include "cctest.h"

using namespace v8::internal;

#if 0
static void VerifyRegionMarking(Address page_start) {
#ifdef ENABLE_CARDMARKING_WRITE_BARRIER
  Page* p = Page::FromAddress(page_start);

  p->SetRegionMarks(Page::kAllRegionsCleanMarks);

  for (Address addr = p->ObjectAreaStart();
       addr < p->ObjectAreaEnd();
       addr += kPointerSize) {
    CHECK(!Page::FromAddress(addr)->IsRegionDirty(addr));
  }

  for (Address addr = p->ObjectAreaStart();
       addr < p->ObjectAreaEnd();
       addr += kPointerSize) {
    Page::FromAddress(addr)->MarkRegionDirty(addr);
  }

  for (Address addr = p->ObjectAreaStart();
       addr < p->ObjectAreaEnd();
       addr += kPointerSize) {
    CHECK(Page::FromAddress(addr)->IsRegionDirty(addr));
  }
#endif
}
#endif


// TODO(gc) you can no longer allocate pages like this. Details are hidden.
#if 0
TEST(Page) {
  byte* mem = NewArray<byte>(2*Page::kPageSize);
  CHECK(mem != NULL);

  Address start = reinterpret_cast<Address>(mem);
  Address page_start = RoundUp(start, Page::kPageSize);

  Page* p = Page::FromAddress(page_start);
  CHECK(p->address() == page_start);
  CHECK(p->is_valid());

  p->opaque_header = 0;
  p->SetIsLargeObjectPage(false);
  CHECK(!p->next_page()->is_valid());

  CHECK(p->ObjectAreaStart() == page_start + Page::kObjectStartOffset);
  CHECK(p->ObjectAreaEnd() == page_start + Page::kPageSize);

  CHECK(p->Offset(page_start + Page::kObjectStartOffset) ==
        Page::kObjectStartOffset);
  CHECK(p->Offset(page_start + Page::kPageSize) == Page::kPageSize);

  CHECK(p->OffsetToAddress(Page::kObjectStartOffset) == p->ObjectAreaStart());
  CHECK(p->OffsetToAddress(Page::kPageSize) == p->ObjectAreaEnd());

  // test region marking
  VerifyRegionMarking(page_start);

  DeleteArray(mem);
}
#endif


TEST(MemoryAllocator) {
  CHECK(Heap::ConfigureHeapDefault());
  CHECK(MemoryAllocator::Setup(Heap::MaxReserved(), Heap::MaxExecutableSize()));

  int total_pages = 0;
  OldSpace faked_space(Heap::MaxReserved(), OLD_POINTER_SPACE, NOT_EXECUTABLE);
  Page* first_page =
      MemoryAllocator::AllocatePage(&faked_space, NOT_EXECUTABLE);
  first_page->InsertAfter(faked_space.anchor()->prev_page());
  CHECK(first_page->is_valid());
  CHECK(first_page->next_page() == faked_space.anchor());
  total_pages++;

  for (Page* p = first_page; p != faked_space.anchor(); p = p->next_page()) {
    CHECK(p->owner() == &faked_space);
  }

  // Again, we should get n or n - 1 pages.
  Page* other =
      MemoryAllocator::AllocatePage(&faked_space, NOT_EXECUTABLE);
  CHECK(other->is_valid());
  total_pages++;
  other->InsertAfter(first_page);
  int page_count = 0;
  for (Page* p = first_page; p != faked_space.anchor(); p = p->next_page()) {
    CHECK(p->owner() == &faked_space);
    page_count++;
  }
  CHECK(total_pages == page_count);

  Page* second_page = first_page->next_page();
  CHECK(second_page->is_valid());
  MemoryAllocator::Free(first_page);
  MemoryAllocator::Free(second_page);
  MemoryAllocator::TearDown();
}


TEST(NewSpace) {
  CHECK(Heap::ConfigureHeapDefault());
  CHECK(MemoryAllocator::Setup(Heap::MaxReserved(), Heap::MaxExecutableSize()));

  NewSpace new_space;

  CHECK(new_space.Setup(Heap::ReservedSemiSpaceSize()));
  CHECK(new_space.HasBeenSetup());

  while (new_space.Available() >= Page::kMaxHeapObjectSize) {
    Object* obj =
        new_space.AllocateRaw(Page::kMaxHeapObjectSize)->ToObjectUnchecked();
    CHECK(new_space.Contains(HeapObject::cast(obj)));
  }

  new_space.TearDown();
  MemoryAllocator::TearDown();
}


TEST(OldSpace) {
  CHECK(Heap::ConfigureHeapDefault());
  CHECK(MemoryAllocator::Setup(Heap::MaxReserved(), Heap::MaxExecutableSize()));

  OldSpace* s = new OldSpace(Heap::MaxOldGenerationSize(),
                             OLD_POINTER_SPACE,
                             NOT_EXECUTABLE);
  CHECK(s != NULL);

  CHECK(s->Setup());

  while (s->Available() > 0) {
    s->AllocateRaw(Page::kMaxHeapObjectSize)->ToObjectUnchecked();
  }

  s->TearDown();
  delete s;
  MemoryAllocator::TearDown();
}


TEST(LargeObjectSpace) {
  CHECK(Heap::Setup(false));

  LargeObjectSpace* lo = Heap::lo_space();
  CHECK(lo != NULL);

  Map* faked_map = reinterpret_cast<Map*>(HeapObject::FromAddress(0));
  int lo_size = Page::kPageSize;

  Object* obj = lo->AllocateRaw(lo_size)->ToObjectUnchecked();
  CHECK(obj->IsHeapObject());

  HeapObject* ho = HeapObject::cast(obj);
  ho->set_map(faked_map);

  CHECK(lo->Contains(HeapObject::cast(obj)));

  CHECK(lo->FindObject(ho->address()) == obj);

  CHECK(lo->Contains(ho));

  while (true) {
    intptr_t available = lo->Available();
    { MaybeObject* maybe_obj = lo->AllocateRaw(lo_size);
      if (!maybe_obj->ToObject(&obj)) break;
    }
    HeapObject::cast(obj)->set_map(faked_map);
    CHECK(lo->Available() < available);
  };

  CHECK(!lo->IsEmpty());

  CHECK(lo->AllocateRaw(lo_size)->IsFailure());

  Heap::TearDown();
}
