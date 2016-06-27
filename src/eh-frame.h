// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EH_FRAME_H_
#define V8_EH_FRAME_H_

#include <cstdint>

namespace v8 {
namespace internal {

class Code;

class EhFrameHdr final {
 public:
  static const int kRecordSize = 20;
  static const int kCIESize;

  explicit EhFrameHdr(Code* code);

  int32_t offset_to_eh_frame() const { return offset_to_eh_frame_; }
  uint32_t lut_entries_number() const { return lut_entries_number_; }
  int32_t offset_to_procedure() const { return offset_to_procedure_; }
  int32_t offset_to_fde() const { return offset_to_fde_; }

 private:
  uint8_t version_;
  uint8_t eh_frame_ptr_encoding_;
  uint8_t lut_size_encoding_;
  uint8_t lut_entries_encoding_;
  int32_t offset_to_eh_frame_;
  uint32_t lut_entries_number_;
  int32_t offset_to_procedure_;
  int32_t offset_to_fde_;
};

}  // namespace internal
}  // namespace v8

#endif
