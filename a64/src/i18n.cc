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
// limitations under the License.

#include "i18n.h"

#include "unicode/calendar.h"
#include "unicode/curramt.h"
#include "unicode/dcfmtsym.h"
#include "unicode/decimfmt.h"
#include "unicode/dtfmtsym.h"
#include "unicode/dtptngen.h"
#include "unicode/locid.h"
#include "unicode/numfmt.h"
#include "unicode/numsys.h"
#include "unicode/smpdtfmt.h"
#include "unicode/timezone.h"
#include "unicode/uchar.h"
#include "unicode/ucurr.h"
#include "unicode/unum.h"
#include "unicode/uversion.h"

namespace v8 {
namespace internal {

namespace {

bool ExtractStringSetting(Isolate* isolate,
                          Handle<JSObject> options,
                          const char* key,
                          icu::UnicodeString* setting) {
  MaybeObject* maybe_object = options->GetProperty(
      *isolate->factory()->NewStringFromAscii(CStrVector(key)));
  Object* object;
  if (maybe_object->ToObject(&object) && object->IsString()) {
    v8::String::Utf8Value utf8_string(
        v8::Utils::ToLocal(Handle<String>(String::cast(object))));
    *setting = icu::UnicodeString::fromUTF8(*utf8_string);
    return true;
  }
  return false;
}


bool ExtractIntegerSetting(Isolate* isolate,
                           Handle<JSObject> options,
                           const char* key,
                           int32_t* value) {
  MaybeObject* maybe_object = options->GetProperty(
      *isolate->factory()->NewStringFromAscii(CStrVector(key)));
  Object* object;
  if (maybe_object->ToObject(&object) && object->IsNumber()) {
    object->ToInt32(value);
    return true;
  }
  return false;
}


bool ExtractBooleanSetting(Isolate* isolate,
                           Handle<JSObject> options,
                           const char* key,
                           bool* value) {
  MaybeObject* maybe_object = options->GetProperty(
      *isolate->factory()->NewStringFromAscii(CStrVector(key)));
  Object* object;
  if (maybe_object->ToObject(&object) && object->IsBoolean()) {
    *value = object->BooleanValue();
    return true;
  }
  return false;
}


icu::SimpleDateFormat* CreateICUDateFormat(
    Isolate* isolate,
    const icu::Locale& icu_locale,
    Handle<JSObject> options) {
  // Create time zone as specified by the user. We have to re-create time zone
  // since calendar takes ownership.
  icu::TimeZone* tz = NULL;
  icu::UnicodeString timezone;
  if (ExtractStringSetting(isolate, options, "timeZone", &timezone)) {
    tz = icu::TimeZone::createTimeZone(timezone);
  } else {
    tz = icu::TimeZone::createDefault();
  }

  // Create a calendar using locale, and apply time zone to it.
  UErrorCode status = U_ZERO_ERROR;
  icu::Calendar* calendar =
      icu::Calendar::createInstance(tz, icu_locale, status);

  // Make formatter from skeleton. Calendar and numbering system are added
  // to the locale as Unicode extension (if they were specified at all).
  icu::SimpleDateFormat* date_format = NULL;
  icu::UnicodeString skeleton;
  if (ExtractStringSetting(isolate, options, "skeleton", &skeleton)) {
    icu::DateTimePatternGenerator* generator =
        icu::DateTimePatternGenerator::createInstance(icu_locale, status);
    icu::UnicodeString pattern;
    if (U_SUCCESS(status)) {
      pattern = generator->getBestPattern(skeleton, status);
      delete generator;
    }

    date_format = new icu::SimpleDateFormat(pattern, icu_locale, status);
    if (U_SUCCESS(status)) {
      date_format->adoptCalendar(calendar);
    }
  }

  if (U_FAILURE(status)) {
    delete calendar;
    delete date_format;
    date_format = NULL;
  }

  return date_format;
}


void SetResolvedDateSettings(Isolate* isolate,
                             const icu::Locale& icu_locale,
                             icu::SimpleDateFormat* date_format,
                             Handle<JSObject> resolved) {
  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString pattern;
  date_format->toPattern(pattern);
  JSObject::SetProperty(
      resolved,
      isolate->factory()->NewStringFromAscii(CStrVector("pattern")),
      isolate->factory()->NewStringFromTwoByte(
        Vector<const uint16_t>(
            reinterpret_cast<const uint16_t*>(pattern.getBuffer()),
            pattern.length())),
      NONE,
      kNonStrictMode);

  // Set time zone and calendar.
  const icu::Calendar* calendar = date_format->getCalendar();
  const char* calendar_name = calendar->getType();
  JSObject::SetProperty(
      resolved,
      isolate->factory()->NewStringFromAscii(CStrVector("calendar")),
      isolate->factory()->NewStringFromAscii(CStrVector(calendar_name)),
      NONE,
      kNonStrictMode);

  const icu::TimeZone& tz = calendar->getTimeZone();
  icu::UnicodeString time_zone;
  tz.getID(time_zone);

  icu::UnicodeString canonical_time_zone;
  icu::TimeZone::getCanonicalID(time_zone, canonical_time_zone, status);
  if (U_SUCCESS(status)) {
    if (canonical_time_zone == UNICODE_STRING_SIMPLE("Etc/GMT")) {
      JSObject::SetProperty(
          resolved,
          isolate->factory()->NewStringFromAscii(CStrVector("timeZone")),
          isolate->factory()->NewStringFromAscii(CStrVector("UTC")),
          NONE,
          kNonStrictMode);
    } else {
      JSObject::SetProperty(
          resolved,
          isolate->factory()->NewStringFromAscii(CStrVector("timeZone")),
          isolate->factory()->NewStringFromTwoByte(
            Vector<const uint16_t>(
                reinterpret_cast<const uint16_t*>(
                    canonical_time_zone.getBuffer()),
                canonical_time_zone.length())),
          NONE,
          kNonStrictMode);
    }
  }

  // Ugly hack. ICU doesn't expose numbering system in any way, so we have
  // to assume that for given locale NumberingSystem constructor produces the
  // same digits as NumberFormat/Calendar would.
  status = U_ZERO_ERROR;
  icu::NumberingSystem* numbering_system =
      icu::NumberingSystem::createInstance(icu_locale, status);
  if (U_SUCCESS(status)) {
    const char* ns = numbering_system->getName();
    JSObject::SetProperty(
        resolved,
        isolate->factory()->NewStringFromAscii(CStrVector("numberingSystem")),
        isolate->factory()->NewStringFromAscii(CStrVector(ns)),
        NONE,
        kNonStrictMode);
  } else {
    JSObject::SetProperty(
        resolved,
        isolate->factory()->NewStringFromAscii(CStrVector("numberingSystem")),
        isolate->factory()->undefined_value(),
        NONE,
        kNonStrictMode);
  }
  delete numbering_system;

  // Set the locale
  char result[ULOC_FULLNAME_CAPACITY];
  status = U_ZERO_ERROR;
  uloc_toLanguageTag(
      icu_locale.getName(), result, ULOC_FULLNAME_CAPACITY, FALSE, &status);
  if (U_SUCCESS(status)) {
    JSObject::SetProperty(
        resolved,
        isolate->factory()->NewStringFromAscii(CStrVector("locale")),
        isolate->factory()->NewStringFromAscii(CStrVector(result)),
        NONE,
        kNonStrictMode);
  } else {
    // This would never happen, since we got the locale from ICU.
    JSObject::SetProperty(
        resolved,
        isolate->factory()->NewStringFromAscii(CStrVector("locale")),
        isolate->factory()->NewStringFromAscii(CStrVector("und")),
        NONE,
        kNonStrictMode);
  }
}


template<int internal_fields, EternalHandles::SingletonHandle field>
Handle<ObjectTemplateInfo> GetEternal(Isolate* isolate) {
  if (isolate->eternal_handles()->Exists(field)) {
    return Handle<ObjectTemplateInfo>::cast(
        isolate->eternal_handles()->GetSingleton(field));
  }
  v8::Local<v8::ObjectTemplate> raw_template(v8::ObjectTemplate::New());
  raw_template->SetInternalFieldCount(internal_fields);
  return Handle<ObjectTemplateInfo>::cast(
      isolate->eternal_handles()->CreateSingleton(
        isolate,
        *v8::Utils::OpenHandle(*raw_template),
        field));
}


icu::DecimalFormat* CreateICUNumberFormat(
    Isolate* isolate,
    const icu::Locale& icu_locale,
    Handle<JSObject> options) {
  // Make formatter from options. Numbering system is added
  // to the locale as Unicode extension (if it was specified at all).
  UErrorCode status = U_ZERO_ERROR;
  icu::DecimalFormat* number_format = NULL;
  icu::UnicodeString style;
  icu::UnicodeString currency;
  if (ExtractStringSetting(isolate, options, "style", &style)) {
    if (style == UNICODE_STRING_SIMPLE("currency")) {
      icu::UnicodeString display;
      ExtractStringSetting(isolate, options, "currency", &currency);
      ExtractStringSetting(isolate, options, "currencyDisplay", &display);

#if (U_ICU_VERSION_MAJOR_NUM == 4) && (U_ICU_VERSION_MINOR_NUM <= 6)
      icu::NumberFormat::EStyles format_style;
      if (display == UNICODE_STRING_SIMPLE("code")) {
        format_style = icu::NumberFormat::kIsoCurrencyStyle;
      } else if (display == UNICODE_STRING_SIMPLE("name")) {
        format_style = icu::NumberFormat::kPluralCurrencyStyle;
      } else {
        format_style = icu::NumberFormat::kCurrencyStyle;
      }
#else  // ICU version is 4.8 or above (we ignore versions below 4.0).
      UNumberFormatStyle format_style;
      if (display == UNICODE_STRING_SIMPLE("code")) {
        format_style = UNUM_CURRENCY_ISO;
      } else if (display == UNICODE_STRING_SIMPLE("name")) {
        format_style = UNUM_CURRENCY_PLURAL;
      } else {
        format_style = UNUM_CURRENCY;
      }
#endif

      number_format = static_cast<icu::DecimalFormat*>(
          icu::NumberFormat::createInstance(icu_locale, format_style,  status));
    } else if (style == UNICODE_STRING_SIMPLE("percent")) {
      number_format = static_cast<icu::DecimalFormat*>(
          icu::NumberFormat::createPercentInstance(icu_locale, status));
      if (U_FAILURE(status)) {
        delete number_format;
        return NULL;
      }
      // Make sure 1.1% doesn't go into 2%.
      number_format->setMinimumFractionDigits(1);
    } else {
      // Make a decimal instance by default.
      number_format = static_cast<icu::DecimalFormat*>(
          icu::NumberFormat::createInstance(icu_locale, status));
    }
  }

  if (U_FAILURE(status)) {
    delete number_format;
    return NULL;
  }

  // Set all options.
  if (!currency.isEmpty()) {
    number_format->setCurrency(currency.getBuffer(), status);
  }

  int32_t digits;
  if (ExtractIntegerSetting(
          isolate, options, "minimumIntegerDigits", &digits)) {
    number_format->setMinimumIntegerDigits(digits);
  }

  if (ExtractIntegerSetting(
          isolate, options, "minimumFractionDigits", &digits)) {
    number_format->setMinimumFractionDigits(digits);
  }

  if (ExtractIntegerSetting(
          isolate, options, "maximumFractionDigits", &digits)) {
    number_format->setMaximumFractionDigits(digits);
  }

  bool significant_digits_used = false;
  if (ExtractIntegerSetting(
          isolate, options, "minimumSignificantDigits", &digits)) {
    number_format->setMinimumSignificantDigits(digits);
    significant_digits_used = true;
  }

  if (ExtractIntegerSetting(
          isolate, options, "maximumSignificantDigits", &digits)) {
    number_format->setMaximumSignificantDigits(digits);
    significant_digits_used = true;
  }

  number_format->setSignificantDigitsUsed(significant_digits_used);

  bool grouping;
  if (ExtractBooleanSetting(isolate, options, "useGrouping", &grouping)) {
    number_format->setGroupingUsed(grouping);
  }

  // Set rounding mode.
  number_format->setRoundingMode(icu::DecimalFormat::kRoundHalfUp);

  return number_format;
}


void SetResolvedNumberSettings(Isolate* isolate,
                               const icu::Locale& icu_locale,
                               icu::DecimalFormat* number_format,
                               Handle<JSObject> resolved) {
  icu::UnicodeString pattern;
  number_format->toPattern(pattern);
  JSObject::SetProperty(
      resolved,
      isolate->factory()->NewStringFromAscii(CStrVector("pattern")),
      isolate->factory()->NewStringFromTwoByte(
        Vector<const uint16_t>(
            reinterpret_cast<const uint16_t*>(pattern.getBuffer()),
            pattern.length())),
      NONE,
      kNonStrictMode);

  // Set resolved currency code in options.currency if not empty.
  icu::UnicodeString currency(number_format->getCurrency());
  if (!currency.isEmpty()) {
    JSObject::SetProperty(
        resolved,
        isolate->factory()->NewStringFromAscii(CStrVector("currency")),
        isolate->factory()->NewStringFromTwoByte(
          Vector<const uint16_t>(
              reinterpret_cast<const uint16_t*>(currency.getBuffer()),
              currency.length())),
        NONE,
        kNonStrictMode);
  }

  // Ugly hack. ICU doesn't expose numbering system in any way, so we have
  // to assume that for given locale NumberingSystem constructor produces the
  // same digits as NumberFormat/Calendar would.
  UErrorCode status = U_ZERO_ERROR;
  icu::NumberingSystem* numbering_system =
      icu::NumberingSystem::createInstance(icu_locale, status);
  if (U_SUCCESS(status)) {
    const char* ns = numbering_system->getName();
    JSObject::SetProperty(
        resolved,
        isolate->factory()->NewStringFromAscii(CStrVector("numberingSystem")),
        isolate->factory()->NewStringFromAscii(CStrVector(ns)),
        NONE,
        kNonStrictMode);
  } else {
    JSObject::SetProperty(
        resolved,
        isolate->factory()->NewStringFromAscii(CStrVector("numberingSystem")),
        isolate->factory()->undefined_value(),
        NONE,
        kNonStrictMode);
  }
  delete numbering_system;

  JSObject::SetProperty(
      resolved,
      isolate->factory()->NewStringFromAscii(CStrVector("useGrouping")),
      isolate->factory()->ToBoolean(number_format->isGroupingUsed()),
      NONE,
      kNonStrictMode);

  JSObject::SetProperty(
      resolved,
      isolate->factory()->NewStringFromAscii(
          CStrVector("minimumIntegerDigits")),
      isolate->factory()->NewNumberFromInt(
          number_format->getMinimumIntegerDigits()),
      NONE,
      kNonStrictMode);

  JSObject::SetProperty(
      resolved,
      isolate->factory()->NewStringFromAscii(
          CStrVector("minimumFractionDigits")),
      isolate->factory()->NewNumberFromInt(
          number_format->getMinimumFractionDigits()),
      NONE,
      kNonStrictMode);

  JSObject::SetProperty(
      resolved,
      isolate->factory()->NewStringFromAscii(
          CStrVector("maximumFractionDigits")),
      isolate->factory()->NewNumberFromInt(
          number_format->getMaximumFractionDigits()),
      NONE,
      kNonStrictMode);

  if (resolved->HasLocalProperty(*isolate->factory()->NewStringFromAscii(
          CStrVector("minimumSignificantDigits")))) {
    JSObject::SetProperty(
        resolved,
        isolate->factory()->NewStringFromAscii(
            CStrVector("minimumSignificantDigits")),
        isolate->factory()->NewNumberFromInt(
            number_format->getMinimumSignificantDigits()),
        NONE,
        kNonStrictMode);
  }

  if (resolved->HasLocalProperty(*isolate->factory()->NewStringFromAscii(
          CStrVector("maximumSignificantDigits")))) {
    JSObject::SetProperty(
        resolved,
        isolate->factory()->NewStringFromAscii(
            CStrVector("maximumSignificantDigits")),
        isolate->factory()->NewNumberFromInt(
            number_format->getMaximumSignificantDigits()),
        NONE,
        kNonStrictMode);
  }

  // Set the locale
  char result[ULOC_FULLNAME_CAPACITY];
  status = U_ZERO_ERROR;
  uloc_toLanguageTag(
      icu_locale.getName(), result, ULOC_FULLNAME_CAPACITY, FALSE, &status);
  if (U_SUCCESS(status)) {
    JSObject::SetProperty(
        resolved,
        isolate->factory()->NewStringFromAscii(CStrVector("locale")),
        isolate->factory()->NewStringFromAscii(CStrVector(result)),
        NONE,
        kNonStrictMode);
  } else {
    // This would never happen, since we got the locale from ICU.
    JSObject::SetProperty(
        resolved,
        isolate->factory()->NewStringFromAscii(CStrVector("locale")),
        isolate->factory()->NewStringFromAscii(CStrVector("und")),
        NONE,
        kNonStrictMode);
  }
}

}  // namespace


// static
Handle<ObjectTemplateInfo> I18N::GetTemplate(Isolate* isolate) {
  return GetEternal<1, i::EternalHandles::I18N_TEMPLATE_ONE>(isolate);
}


// static
Handle<ObjectTemplateInfo> I18N::GetTemplate2(Isolate* isolate) {
  return GetEternal<2, i::EternalHandles::I18N_TEMPLATE_TWO>(isolate);
}


// static
icu::SimpleDateFormat* DateFormat::InitializeDateTimeFormat(
    Isolate* isolate,
    Handle<String> locale,
    Handle<JSObject> options,
    Handle<JSObject> resolved) {
  // Convert BCP47 into ICU locale format.
  UErrorCode status = U_ZERO_ERROR;
  icu::Locale icu_locale;
  char icu_result[ULOC_FULLNAME_CAPACITY];
  int icu_length = 0;
  v8::String::Utf8Value bcp47_locale(v8::Utils::ToLocal(locale));
  if (bcp47_locale.length() != 0) {
    uloc_forLanguageTag(*bcp47_locale, icu_result, ULOC_FULLNAME_CAPACITY,
                        &icu_length, &status);
    if (U_FAILURE(status) || icu_length == 0) {
      return NULL;
    }
    icu_locale = icu::Locale(icu_result);
  }

  icu::SimpleDateFormat* date_format = CreateICUDateFormat(
      isolate, icu_locale, options);
  if (!date_format) {
    // Remove extensions and try again.
    icu::Locale no_extension_locale(icu_locale.getBaseName());
    date_format = CreateICUDateFormat(isolate, no_extension_locale, options);

    // Set resolved settings (pattern, numbering system, calendar).
    SetResolvedDateSettings(
        isolate, no_extension_locale, date_format, resolved);
  } else {
    SetResolvedDateSettings(isolate, icu_locale, date_format, resolved);
  }

  return date_format;
}


icu::SimpleDateFormat* DateFormat::UnpackDateFormat(
    Isolate* isolate,
    Handle<JSObject> obj) {
  if (obj->HasLocalProperty(
          *isolate->factory()->NewStringFromAscii(CStrVector("dateFormat")))) {
    return reinterpret_cast<icu::SimpleDateFormat*>(
        obj->GetInternalField(0));
  }

  return NULL;
}


void DateFormat::DeleteDateFormat(v8::Isolate* isolate,
                                  Persistent<v8::Object>* object,
                                  void* param) {
  // First delete the hidden C++ object.
  delete reinterpret_cast<icu::SimpleDateFormat*>(Handle<JSObject>::cast(
      v8::Utils::OpenPersistent(object))->GetInternalField(0));

  // Then dispose of the persistent handle to JS object.
  object->Dispose(isolate);
}


icu::DecimalFormat* NumberFormat::InitializeNumberFormat(
    Isolate* isolate,
    Handle<String> locale,
    Handle<JSObject> options,
    Handle<JSObject> resolved) {
  // Convert BCP47 into ICU locale format.
  UErrorCode status = U_ZERO_ERROR;
  icu::Locale icu_locale;
  char icu_result[ULOC_FULLNAME_CAPACITY];
  int icu_length = 0;
  v8::String::Utf8Value bcp47_locale(v8::Utils::ToLocal(locale));
  if (bcp47_locale.length() != 0) {
    uloc_forLanguageTag(*bcp47_locale, icu_result, ULOC_FULLNAME_CAPACITY,
                        &icu_length, &status);
    if (U_FAILURE(status) || icu_length == 0) {
      return NULL;
    }
    icu_locale = icu::Locale(icu_result);
  }

  icu::DecimalFormat* number_format =
      CreateICUNumberFormat(isolate, icu_locale, options);
  if (!number_format) {
    // Remove extensions and try again.
    icu::Locale no_extension_locale(icu_locale.getBaseName());
    number_format = CreateICUNumberFormat(
        isolate, no_extension_locale, options);

    // Set resolved settings (pattern, numbering system).
    SetResolvedNumberSettings(
        isolate, no_extension_locale, number_format, resolved);
  } else {
    SetResolvedNumberSettings(isolate, icu_locale, number_format, resolved);
  }

  return number_format;
}


icu::DecimalFormat* NumberFormat::UnpackNumberFormat(
    Isolate* isolate,
    Handle<JSObject> obj) {
  if (obj->HasLocalProperty(*isolate->factory()->NewStringFromAscii(
          CStrVector("numberFormat")))) {
    return reinterpret_cast<icu::DecimalFormat*>(obj->GetInternalField(0));
  }

  return NULL;
}


void NumberFormat::DeleteNumberFormat(v8::Isolate* isolate,
                                      Persistent<v8::Object>* object,
                                      void* param) {
  // First delete the hidden C++ object.
  delete reinterpret_cast<icu::DecimalFormat*>(Handle<JSObject>::cast(
      v8::Utils::OpenPersistent(object))->GetInternalField(0));

  // Then dispose of the persistent handle to JS object.
  object->Dispose(isolate);
}

} }  // namespace v8::internal
