// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_PUBLIC_STRINGBUFFER_H_
#define V8_INSPECTOR_PUBLIC_STRINGBUFFER_H_

#include "src/inspector/public/StringView.h"

#include <memory>

namespace v8_inspector {

class PLATFORM_EXPORT StringBuffer {
 public:
  virtual ~StringBuffer() {}
  virtual const StringView& string() = 0;
  // This method copies contents.
  static std::unique_ptr<StringBuffer> create(const StringView&);
};

}  // namespace v8_inspector

#endif  // V8_INSPECTOR_PUBLIC_STRINGBUFFER_H_
