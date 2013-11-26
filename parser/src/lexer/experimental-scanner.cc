// Copyright 2013 the V8 project authors. All rights reserved.
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

#include "experimental-scanner.h"

namespace v8 {
namespace internal {

std::set<ScannerBase*>* ScannerBase::scanners_ = NULL;

void ScannerBase::UpdateBuffersAfterGC(v8::Isolate*, GCType, GCCallbackFlags) {
  if (!scanners_) return;
  for (std::set<ScannerBase*>::const_iterator it = scanners_->begin();
       it != scanners_->end();
       ++it)
    (*it)->SetBufferBasedOnHandle();
}


template<>
const uint8_t* ExperimentalScanner<uint8_t>::GetNewBufferBasedOnHandle() const {
  String::FlatContent content = source_handle_->GetFlatContent();
  return content.ToOneByteVector().start();
}


template <>
const uint16_t* ExperimentalScanner<uint16_t>::GetNewBufferBasedOnHandle()
    const {
  String::FlatContent content = source_handle_->GetFlatContent();
  return content.ToUC16Vector().start();
}


template<>
const int8_t* ExperimentalScanner<int8_t>::GetNewBufferBasedOnHandle() const {
  String::FlatContent content = source_handle_->GetFlatContent();
  return reinterpret_cast<const int8_t*>(content.ToOneByteVector().start());
}

}
}
