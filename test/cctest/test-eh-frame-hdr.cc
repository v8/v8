// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/eh-frame.h"
#include "src/objects.h"
#include "test/cctest/cctest.h"

using namespace v8::internal;

TEST(EhFrameHdr) {
  CcTest::InitializeVM();
  HandleScope handle_scope(CcTest::i_isolate());

  // The content is not relevant in this test
  byte buffer[10] = {0};
  byte unwinding_info[30 + EhFrameHdr::kRecordSize] = {0};

  CodeDesc code_desc;
  code_desc.buffer = &buffer[0];
  code_desc.buffer_size = sizeof(buffer);
  code_desc.constant_pool_size = 0;
  code_desc.instr_size = sizeof(buffer);
  code_desc.reloc_size = 0;
  code_desc.origin = nullptr;
  code_desc.unwinding_info = &unwinding_info[0];
  code_desc.unwinding_info_size = sizeof(unwinding_info);

  Handle<Code> code = CcTest::i_isolate()->factory()->NewCode(
      code_desc, 0, Handle<Object>::null());

  EhFrameHdr eh_frame_hdr(*code);
  CHECK_EQ(eh_frame_hdr.lut_entries_number(), 1);

  //
  // Plugging some numbers in the DSO layout shown in eh-frame.cc:
  //
  //  |      ...      |
  //  +---------------+ <-- (E) --------
  //  |               |                ^
  //  |  Instructions |  10 bytes      | .text
  //  |               |                v
  //  +---------------+ <---------------
  //  |///////////////|
  //  |////Padding////|   6 bytes
  //  |///////////////|
  //  +---------------+ <---(D)---------
  //  |               |                ^
  //  |      CIE      |   N bytes*     |
  //  |               |                |
  //  +---------------+ <-- (C)        | .eh_frame
  //  |               |                |
  //  |      FDE      |  30 - N bytes  |
  //  |               |                v
  //  +---------------+ <-- (B) --------
  //  |    version    |                ^
  //  +---------------+   4 bytes      |
  //  |   encoding    |                |
  //  |  specifiers   |                |
  //  +---------------+ <---(A)        | .eh_frame_hdr
  //  |   offset to   |                |
  //  |   .eh_frame   |                |
  //  +---------------+                |
  //  |      ...      |               ...
  //
  //  (*) the size of the CIE is platform dependent.
  //
  CHECK_EQ(eh_frame_hdr.offset_to_eh_frame(), -(4 + 30));        // A -> D
  CHECK_EQ(eh_frame_hdr.offset_to_procedure(), -(30 + 6 + 10));  // B -> E
  CHECK_EQ(eh_frame_hdr.offset_to_fde(),
           -(30 - EhFrameHdr::kCIESize));  // B -> C
}

TEST(DummyEhFrameHdr) {
  CcTest::InitializeVM();
  HandleScope handle_scope(CcTest::i_isolate());

  byte buffer[10] = {0};  // The content is not relevant in this test

  CodeDesc code_desc;
  code_desc.buffer = &buffer[0];
  code_desc.buffer_size = sizeof(buffer);
  code_desc.constant_pool_size = 0;
  code_desc.instr_size = sizeof(buffer);
  code_desc.reloc_size = 0;
  code_desc.origin = nullptr;
  code_desc.unwinding_info = nullptr;
  code_desc.unwinding_info_size = 0;

  Handle<Code> code = CcTest::i_isolate()->factory()->NewCode(
      code_desc, 0, Handle<Object>::null());

  EhFrameHdr eh_frame_hdr(*code);
  // A dummy header has an empty LUT
  CHECK_EQ(eh_frame_hdr.lut_entries_number(), 0);
  // These values should be irrelevant, but check that they have been zeroed.
  CHECK_EQ(eh_frame_hdr.offset_to_eh_frame(), 0);
  CHECK_EQ(eh_frame_hdr.offset_to_procedure(), 0);
  CHECK_EQ(eh_frame_hdr.offset_to_fde(), 0);
}
