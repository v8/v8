// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_INTL_OBJECTS_INL_H_
#define V8_OBJECTS_INTL_OBJECTS_INL_H_

#include "src/objects/intl-objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

#define PTR_ACCESSORS(holder, name, type, offset)   \
  inline type* holder::name() const {               \
    Object* obj = READ_FIELD(this, offset);         \
    DCHECK(obj->IsSmi());                           \
    return reinterpret_cast<type*>(obj);            \
  }                                                 \
  inline void holder::set_##name(type* value) {     \
    Object* obj = reinterpret_cast<Object*>(value); \
    DCHECK(obj->IsSmi());                           \
    WRITE_FIELD(this, offset, obj);                 \
  }

namespace v8 {
namespace internal {

PTR_ACCESSORS(JSIntlDateTimeFormat, simple_date_format, icu::SimpleDateFormat,
              kSimpleDateFormat)

PTR_ACCESSORS(JSIntlNumberFormat, decimal_format, icu::DecimalFormat,
              kDecimalFormat)

PTR_ACCESSORS(JSIntlCollator, collator, icu::Collator, kCollator)

PTR_ACCESSORS(JSIntlV8BreakIterator, break_iterator, icu::BreakIterator,
              kBreakIterator)
PTR_ACCESSORS(JSIntlV8BreakIterator, unicode_string, icu::UnicodeString,
              kUnicodeString)

}  // namespace internal
}  // namespace v8

#undef PTR_ACCESSORS

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_INTL_OBJECTS_INL_H_
