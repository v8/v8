// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_PUBLIC_STRINGVIEW_H_
#define V8_INSPECTOR_PUBLIC_STRINGVIEW_H_

#include <cctype>
#include <stdint.h>

namespace v8_inspector {

class PLATFORM_EXPORT StringView {
public:
    StringView()
        : m_is8Bit(true)
        , m_length(0)
        , m_characters8(nullptr) {}

    StringView(const uint8_t* characters, unsigned length)
        : m_is8Bit(true)
        , m_length(length)
        , m_characters8(characters) {}

    StringView(const uint16_t* characters, unsigned length)
        : m_is8Bit(false)
        , m_length(length)
        , m_characters16(characters) {}

    bool is8Bit() const { return m_is8Bit; }
    unsigned length() const { return m_length; }

    // TODO(dgozman): add DCHECK(m_is8Bit) to accessors once platform can be used here.
    const uint8_t* characters8() const { return m_characters8; }
    const uint16_t* characters16() const { return m_characters16; }

private:
    bool m_is8Bit;
    unsigned m_length;
    union {
        const uint8_t* m_characters8;
        const uint16_t* m_characters16;
    };
};

} // namespace v8_inspector

#endif // V8_INSPECTOR_PUBLIC_STRINGVIEW_H_
