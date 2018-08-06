// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/intl-objects.h"
#include "src/objects/intl-objects-inl.h"

#include <memory>

#include "src/api-inl.h"
#include "src/global-handles.h"
#include "src/heap/factory.h"
#include "src/intl.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/objects/managed.h"
#include "src/objects/string.h"
#include "src/property-descriptor.h"
#include "unicode/brkiter.h"
#include "unicode/bytestream.h"
#include "unicode/calendar.h"
#include "unicode/coll.h"
#include "unicode/curramt.h"
#include "unicode/dcfmtsym.h"
#include "unicode/decimfmt.h"
#include "unicode/dtfmtsym.h"
#include "unicode/dtptngen.h"
#include "unicode/gregocal.h"
#include "unicode/locid.h"
#include "unicode/numfmt.h"
#include "unicode/numsys.h"
#include "unicode/plurrule.h"
#include "unicode/rbbi.h"
#include "unicode/regex.h"
#include "unicode/smpdtfmt.h"
#include "unicode/timezone.h"
#include "unicode/uchar.h"
#include "unicode/ucol.h"
#include "unicode/ucurr.h"
#include "unicode/unum.h"
#include "unicode/upluralrules.h"
#include "unicode/ures.h"
#include "unicode/uvernum.h"
#include "unicode/uversion.h"

#if U_ICU_VERSION_MAJOR_NUM >= 59
#include "unicode/char16ptr.h"
#endif

namespace v8 {
namespace internal {

namespace {

bool ExtractStringSetting(Isolate* isolate, Handle<JSObject> options,
                          const char* key, icu::UnicodeString* setting) {
  v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  Handle<String> str = isolate->factory()->NewStringFromAsciiChecked(key);
  Handle<Object> object =
      JSReceiver::GetProperty(isolate, options, str).ToHandleChecked();
  if (object->IsString()) {
    v8::String::Utf8Value utf8_string(
        v8_isolate, v8::Utils::ToLocal(Handle<String>::cast(object)));
    *setting = icu::UnicodeString::fromUTF8(*utf8_string);
    return true;
  }
  return false;
}

bool ExtractIntegerSetting(Isolate* isolate, Handle<JSObject> options,
                           const char* key, int32_t* value) {
  Handle<String> str = isolate->factory()->NewStringFromAsciiChecked(key);
  Handle<Object> object =
      JSReceiver::GetProperty(isolate, options, str).ToHandleChecked();
  if (object->IsNumber()) {
    return object->ToInt32(value);
  }
  return false;
}

bool ExtractBooleanSetting(Isolate* isolate, Handle<JSObject> options,
                           const char* key, bool* value) {
  Handle<String> str = isolate->factory()->NewStringFromAsciiChecked(key);
  Handle<Object> object =
      JSReceiver::GetProperty(isolate, options, str).ToHandleChecked();
  if (object->IsBoolean()) {
    *value = object->BooleanValue(isolate);
    return true;
  }
  return false;
}

icu::SimpleDateFormat* CreateICUDateFormat(Isolate* isolate,
                                           const icu::Locale& icu_locale,
                                           Handle<JSObject> options) {
  // Create time zone as specified by the user. We have to re-create time zone
  // since calendar takes ownership.
  icu::TimeZone* tz = nullptr;
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

  if (calendar->getDynamicClassID() ==
      icu::GregorianCalendar::getStaticClassID()) {
    icu::GregorianCalendar* gc = (icu::GregorianCalendar*)calendar;
    UErrorCode status = U_ZERO_ERROR;
    // The beginning of ECMAScript time, namely -(2**53)
    const double start_of_time = -9007199254740992;
    gc->setGregorianChange(start_of_time, status);
    DCHECK(U_SUCCESS(status));
  }

  // Make formatter from skeleton. Calendar and numbering system are added
  // to the locale as Unicode extension (if they were specified at all).
  icu::SimpleDateFormat* date_format = nullptr;
  icu::UnicodeString skeleton;
  if (ExtractStringSetting(isolate, options, "skeleton", &skeleton)) {
    // See https://github.com/tc39/ecma402/issues/225 . The best pattern
    // generation needs to be done in the base locale according to the
    // current spec however odd it may be. See also crbug.com/826549 .
    // This is a temporary work-around to get v8's external behavior to match
    // the current spec, but does not follow the spec provisions mentioned
    // in the above Ecma 402 issue.
    // TODO(jshin): The spec may need to be revised because using the base
    // locale for the pattern match is not quite right. Moreover, what to
    // do with 'related year' part when 'chinese/dangi' calendar is specified
    // has to be discussed. Revisit once the spec is clarified/revised.
    icu::Locale no_extension_locale(icu_locale.getBaseName());
    std::unique_ptr<icu::DateTimePatternGenerator> generator(
        icu::DateTimePatternGenerator::createInstance(no_extension_locale,
                                                      status));
    icu::UnicodeString pattern;
    if (U_SUCCESS(status))
      pattern = generator->getBestPattern(skeleton, status);

    date_format = new icu::SimpleDateFormat(pattern, icu_locale, status);
    if (U_SUCCESS(status)) {
      date_format->adoptCalendar(calendar);
    }
  }

  if (U_FAILURE(status)) {
    delete calendar;
    delete date_format;
    date_format = nullptr;
  }

  return date_format;
}

void SetResolvedDateSettings(Isolate* isolate, const icu::Locale& icu_locale,
                             icu::SimpleDateFormat* date_format,
                             Handle<JSObject> resolved) {
  Factory* factory = isolate->factory();
  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString pattern;
  date_format->toPattern(pattern);
  JSObject::SetProperty(
      isolate, resolved, factory->intl_pattern_symbol(),
      factory
          ->NewStringFromTwoByte(Vector<const uint16_t>(
              reinterpret_cast<const uint16_t*>(pattern.getBuffer()),
              pattern.length()))
          .ToHandleChecked(),
      LanguageMode::kSloppy)
      .Assert();

  // Set time zone and calendar.
  const icu::Calendar* calendar = date_format->getCalendar();
  // getType() returns legacy calendar type name instead of LDML/BCP47 calendar
  // key values. intl.js maps them to BCP47 values for key "ca".
  // TODO(jshin): Consider doing it here, instead.
  const char* calendar_name = calendar->getType();
  JSObject::SetProperty(
      isolate, resolved, factory->NewStringFromStaticChars("calendar"),
      factory->NewStringFromAsciiChecked(calendar_name), LanguageMode::kSloppy)
      .Assert();

  const icu::TimeZone& tz = calendar->getTimeZone();
  icu::UnicodeString time_zone;
  tz.getID(time_zone);

  icu::UnicodeString canonical_time_zone;
  icu::TimeZone::getCanonicalID(time_zone, canonical_time_zone, status);
  if (U_SUCCESS(status)) {
    // In CLDR (http://unicode.org/cldr/trac/ticket/9943), Etc/UTC is made
    // a separate timezone ID from Etc/GMT even though they're still the same
    // timezone. We have Etc/UTC because 'UTC', 'Etc/Universal',
    // 'Etc/Zulu' and others are turned to 'Etc/UTC' by ICU. Etc/GMT comes
    // from Etc/GMT0, Etc/GMT+0, Etc/GMT-0, Etc/Greenwich.
    // ecma402##sec-canonicalizetimezonename step 3
    if (canonical_time_zone == UNICODE_STRING_SIMPLE("Etc/UTC") ||
        canonical_time_zone == UNICODE_STRING_SIMPLE("Etc/GMT")) {
      JSObject::SetProperty(
          isolate, resolved, factory->NewStringFromStaticChars("timeZone"),
          factory->NewStringFromStaticChars("UTC"), LanguageMode::kSloppy)
          .Assert();
    } else {
      JSObject::SetProperty(isolate, resolved,
                            factory->NewStringFromStaticChars("timeZone"),
                            factory
                                ->NewStringFromTwoByte(Vector<const uint16_t>(
                                    reinterpret_cast<const uint16_t*>(
                                        canonical_time_zone.getBuffer()),
                                    canonical_time_zone.length()))
                                .ToHandleChecked(),
                            LanguageMode::kSloppy)
          .Assert();
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
        isolate, resolved, factory->NewStringFromStaticChars("numberingSystem"),
        factory->NewStringFromAsciiChecked(ns), LanguageMode::kSloppy)
        .Assert();
  } else {
    JSObject::SetProperty(isolate, resolved,
                          factory->NewStringFromStaticChars("numberingSystem"),
                          factory->undefined_value(), LanguageMode::kSloppy)
        .Assert();
  }
  delete numbering_system;

  // Set the locale
  char result[ULOC_FULLNAME_CAPACITY];
  status = U_ZERO_ERROR;
  uloc_toLanguageTag(icu_locale.getName(), result, ULOC_FULLNAME_CAPACITY,
                     FALSE, &status);
  if (U_SUCCESS(status)) {
    JSObject::SetProperty(
        isolate, resolved, factory->NewStringFromStaticChars("locale"),
        factory->NewStringFromAsciiChecked(result), LanguageMode::kSloppy)
        .Assert();
  } else {
    // This would never happen, since we got the locale from ICU.
    JSObject::SetProperty(
        isolate, resolved, factory->NewStringFromStaticChars("locale"),
        factory->NewStringFromStaticChars("und"), LanguageMode::kSloppy)
        .Assert();
  }
}

void SetNumericSettings(Isolate* isolate, icu::DecimalFormat* number_format,
                        Handle<JSObject> options) {
  int32_t digits;
  if (ExtractIntegerSetting(isolate, options, "minimumIntegerDigits",
                            &digits)) {
    number_format->setMinimumIntegerDigits(digits);
  }

  if (ExtractIntegerSetting(isolate, options, "minimumFractionDigits",
                            &digits)) {
    number_format->setMinimumFractionDigits(digits);
  }

  if (ExtractIntegerSetting(isolate, options, "maximumFractionDigits",
                            &digits)) {
    number_format->setMaximumFractionDigits(digits);
  }

  bool significant_digits_used = false;
  if (ExtractIntegerSetting(isolate, options, "minimumSignificantDigits",
                            &digits)) {
    number_format->setMinimumSignificantDigits(digits);
    significant_digits_used = true;
  }

  if (ExtractIntegerSetting(isolate, options, "maximumSignificantDigits",
                            &digits)) {
    number_format->setMaximumSignificantDigits(digits);
    significant_digits_used = true;
  }

  number_format->setSignificantDigitsUsed(significant_digits_used);

  number_format->setRoundingMode(icu::DecimalFormat::kRoundHalfUp);
}

icu::DecimalFormat* CreateICUNumberFormat(Isolate* isolate,
                                          const icu::Locale& icu_locale,
                                          Handle<JSObject> options) {
  // Make formatter from options. Numbering system is added
  // to the locale as Unicode extension (if it was specified at all).
  UErrorCode status = U_ZERO_ERROR;
  icu::DecimalFormat* number_format = nullptr;
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
          icu::NumberFormat::createInstance(icu_locale, format_style, status));

      if (U_FAILURE(status)) {
        delete number_format;
        return nullptr;
      }
    } else if (style == UNICODE_STRING_SIMPLE("percent")) {
      number_format = static_cast<icu::DecimalFormat*>(
          icu::NumberFormat::createPercentInstance(icu_locale, status));
      if (U_FAILURE(status)) {
        delete number_format;
        return nullptr;
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
    return nullptr;
  }

  // Set all options.
  if (!currency.isEmpty()) {
    number_format->setCurrency(currency.getBuffer(), status);
  }

  SetNumericSettings(isolate, number_format, options);

  bool grouping;
  if (ExtractBooleanSetting(isolate, options, "useGrouping", &grouping)) {
    number_format->setGroupingUsed(grouping);
  }

  return number_format;
}

void SetResolvedNumericSettings(Isolate* isolate, const icu::Locale& icu_locale,
                                icu::DecimalFormat* number_format,
                                Handle<JSObject> resolved) {
  Factory* factory = isolate->factory();

  JSObject::SetProperty(
      isolate, resolved,
      factory->NewStringFromStaticChars("minimumIntegerDigits"),
      factory->NewNumberFromInt(number_format->getMinimumIntegerDigits()),
      LanguageMode::kSloppy)
      .Assert();

  JSObject::SetProperty(
      isolate, resolved,
      factory->NewStringFromStaticChars("minimumFractionDigits"),
      factory->NewNumberFromInt(number_format->getMinimumFractionDigits()),
      LanguageMode::kSloppy)
      .Assert();

  JSObject::SetProperty(
      isolate, resolved,
      factory->NewStringFromStaticChars("maximumFractionDigits"),
      factory->NewNumberFromInt(number_format->getMaximumFractionDigits()),
      LanguageMode::kSloppy)
      .Assert();

  Handle<String> key =
      factory->NewStringFromStaticChars("minimumSignificantDigits");
  Maybe<bool> maybe = JSReceiver::HasOwnProperty(resolved, key);
  CHECK(maybe.IsJust());
  if (maybe.FromJust()) {
    JSObject::SetProperty(
        isolate, resolved,
        factory->NewStringFromStaticChars("minimumSignificantDigits"),
        factory->NewNumberFromInt(number_format->getMinimumSignificantDigits()),
        LanguageMode::kSloppy)
        .Assert();
  }

  key = factory->NewStringFromStaticChars("maximumSignificantDigits");
  maybe = JSReceiver::HasOwnProperty(resolved, key);
  CHECK(maybe.IsJust());
  if (maybe.FromJust()) {
    JSObject::SetProperty(
        isolate, resolved,
        factory->NewStringFromStaticChars("maximumSignificantDigits"),
        factory->NewNumberFromInt(number_format->getMaximumSignificantDigits()),
        LanguageMode::kSloppy)
        .Assert();
  }

  // Set the locale
  char result[ULOC_FULLNAME_CAPACITY];
  UErrorCode status = U_ZERO_ERROR;
  uloc_toLanguageTag(icu_locale.getName(), result, ULOC_FULLNAME_CAPACITY,
                     FALSE, &status);
  if (U_SUCCESS(status)) {
    JSObject::SetProperty(
        isolate, resolved, factory->NewStringFromStaticChars("locale"),
        factory->NewStringFromAsciiChecked(result), LanguageMode::kSloppy)
        .Assert();
  } else {
    // This would never happen, since we got the locale from ICU.
    JSObject::SetProperty(
        isolate, resolved, factory->NewStringFromStaticChars("locale"),
        factory->NewStringFromStaticChars("und"), LanguageMode::kSloppy)
        .Assert();
  }
}

void SetResolvedNumberSettings(Isolate* isolate, const icu::Locale& icu_locale,
                               icu::DecimalFormat* number_format,
                               Handle<JSObject> resolved) {
  Factory* factory = isolate->factory();

  // Set resolved currency code in options.currency if not empty.
  icu::UnicodeString currency(number_format->getCurrency());
  if (!currency.isEmpty()) {
    JSObject::SetProperty(
        isolate, resolved, factory->NewStringFromStaticChars("currency"),
        factory
            ->NewStringFromTwoByte(Vector<const uint16_t>(
                reinterpret_cast<const uint16_t*>(currency.getBuffer()),
                currency.length()))
            .ToHandleChecked(),
        LanguageMode::kSloppy)
        .Assert();
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
        isolate, resolved, factory->NewStringFromStaticChars("numberingSystem"),
        factory->NewStringFromAsciiChecked(ns), LanguageMode::kSloppy)
        .Assert();
  } else {
    JSObject::SetProperty(isolate, resolved,
                          factory->NewStringFromStaticChars("numberingSystem"),
                          factory->undefined_value(), LanguageMode::kSloppy)
        .Assert();
  }
  delete numbering_system;

  JSObject::SetProperty(isolate, resolved,
                        factory->NewStringFromStaticChars("useGrouping"),
                        factory->ToBoolean(number_format->isGroupingUsed()),
                        LanguageMode::kSloppy)
      .Assert();

  SetResolvedNumericSettings(isolate, icu_locale, number_format, resolved);
}

icu::Collator* CreateICUCollator(Isolate* isolate,
                                 const icu::Locale& icu_locale,
                                 Handle<JSObject> options) {
  // Make collator from options.
  icu::Collator* collator = nullptr;
  UErrorCode status = U_ZERO_ERROR;
  collator = icu::Collator::createInstance(icu_locale, status);

  if (U_FAILURE(status)) {
    delete collator;
    return nullptr;
  }

  // Set flags first, and then override them with sensitivity if necessary.
  bool numeric;
  if (ExtractBooleanSetting(isolate, options, "numeric", &numeric)) {
    collator->setAttribute(UCOL_NUMERIC_COLLATION, numeric ? UCOL_ON : UCOL_OFF,
                           status);
  }

  // Normalization is always on, by the spec. We are free to optimize
  // if the strings are already normalized (but we don't have a way to tell
  // that right now).
  collator->setAttribute(UCOL_NORMALIZATION_MODE, UCOL_ON, status);

  icu::UnicodeString case_first;
  if (ExtractStringSetting(isolate, options, "caseFirst", &case_first)) {
    if (case_first == UNICODE_STRING_SIMPLE("upper")) {
      collator->setAttribute(UCOL_CASE_FIRST, UCOL_UPPER_FIRST, status);
    } else if (case_first == UNICODE_STRING_SIMPLE("lower")) {
      collator->setAttribute(UCOL_CASE_FIRST, UCOL_LOWER_FIRST, status);
    } else {
      // Default (false/off).
      collator->setAttribute(UCOL_CASE_FIRST, UCOL_OFF, status);
    }
  }

  icu::UnicodeString sensitivity;
  if (ExtractStringSetting(isolate, options, "sensitivity", &sensitivity)) {
    if (sensitivity == UNICODE_STRING_SIMPLE("base")) {
      collator->setStrength(icu::Collator::PRIMARY);
    } else if (sensitivity == UNICODE_STRING_SIMPLE("accent")) {
      collator->setStrength(icu::Collator::SECONDARY);
    } else if (sensitivity == UNICODE_STRING_SIMPLE("case")) {
      collator->setStrength(icu::Collator::PRIMARY);
      collator->setAttribute(UCOL_CASE_LEVEL, UCOL_ON, status);
    } else {
      // variant (default)
      collator->setStrength(icu::Collator::TERTIARY);
    }
  }

  bool ignore;
  if (ExtractBooleanSetting(isolate, options, "ignorePunctuation", &ignore)) {
    if (ignore) {
      collator->setAttribute(UCOL_ALTERNATE_HANDLING, UCOL_SHIFTED, status);
    }
  }

  return collator;
}

void SetResolvedCollatorSettings(Isolate* isolate,
                                 const icu::Locale& icu_locale,
                                 icu::Collator* collator,
                                 Handle<JSObject> resolved) {
  Factory* factory = isolate->factory();
  UErrorCode status = U_ZERO_ERROR;

  JSObject::SetProperty(
      isolate, resolved, factory->NewStringFromStaticChars("numeric"),
      factory->ToBoolean(
          collator->getAttribute(UCOL_NUMERIC_COLLATION, status) == UCOL_ON),
      LanguageMode::kSloppy)
      .Assert();

  switch (collator->getAttribute(UCOL_CASE_FIRST, status)) {
    case UCOL_LOWER_FIRST:
      JSObject::SetProperty(
          isolate, resolved, factory->NewStringFromStaticChars("caseFirst"),
          factory->NewStringFromStaticChars("lower"), LanguageMode::kSloppy)
          .Assert();
      break;
    case UCOL_UPPER_FIRST:
      JSObject::SetProperty(
          isolate, resolved, factory->NewStringFromStaticChars("caseFirst"),
          factory->NewStringFromStaticChars("upper"), LanguageMode::kSloppy)
          .Assert();
      break;
    default:
      JSObject::SetProperty(
          isolate, resolved, factory->NewStringFromStaticChars("caseFirst"),
          factory->NewStringFromStaticChars("false"), LanguageMode::kSloppy)
          .Assert();
  }

  switch (collator->getAttribute(UCOL_STRENGTH, status)) {
    case UCOL_PRIMARY: {
      JSObject::SetProperty(
          isolate, resolved, factory->NewStringFromStaticChars("strength"),
          factory->NewStringFromStaticChars("primary"), LanguageMode::kSloppy)
          .Assert();

      // case level: true + s1 -> case, s1 -> base.
      if (UCOL_ON == collator->getAttribute(UCOL_CASE_LEVEL, status)) {
        JSObject::SetProperty(
            isolate, resolved, factory->NewStringFromStaticChars("sensitivity"),
            factory->NewStringFromStaticChars("case"), LanguageMode::kSloppy)
            .Assert();
      } else {
        JSObject::SetProperty(
            isolate, resolved, factory->NewStringFromStaticChars("sensitivity"),
            factory->NewStringFromStaticChars("base"), LanguageMode::kSloppy)
            .Assert();
      }
      break;
    }
    case UCOL_SECONDARY:
      JSObject::SetProperty(
          isolate, resolved, factory->NewStringFromStaticChars("strength"),
          factory->NewStringFromStaticChars("secondary"), LanguageMode::kSloppy)
          .Assert();
      JSObject::SetProperty(
          isolate, resolved, factory->NewStringFromStaticChars("sensitivity"),
          factory->NewStringFromStaticChars("accent"), LanguageMode::kSloppy)
          .Assert();
      break;
    case UCOL_TERTIARY:
      JSObject::SetProperty(
          isolate, resolved, factory->NewStringFromStaticChars("strength"),
          factory->NewStringFromStaticChars("tertiary"), LanguageMode::kSloppy)
          .Assert();
      JSObject::SetProperty(
          isolate, resolved, factory->NewStringFromStaticChars("sensitivity"),
          factory->NewStringFromStaticChars("variant"), LanguageMode::kSloppy)
          .Assert();
      break;
    case UCOL_QUATERNARY:
      // We shouldn't get quaternary and identical from ICU, but if we do
      // put them into variant.
      JSObject::SetProperty(isolate, resolved,
                            factory->NewStringFromStaticChars("strength"),
                            factory->NewStringFromStaticChars("quaternary"),
                            LanguageMode::kSloppy)
          .Assert();
      JSObject::SetProperty(
          isolate, resolved, factory->NewStringFromStaticChars("sensitivity"),
          factory->NewStringFromStaticChars("variant"), LanguageMode::kSloppy)
          .Assert();
      break;
    default:
      JSObject::SetProperty(
          isolate, resolved, factory->NewStringFromStaticChars("strength"),
          factory->NewStringFromStaticChars("identical"), LanguageMode::kSloppy)
          .Assert();
      JSObject::SetProperty(
          isolate, resolved, factory->NewStringFromStaticChars("sensitivity"),
          factory->NewStringFromStaticChars("variant"), LanguageMode::kSloppy)
          .Assert();
  }

  JSObject::SetProperty(
      isolate, resolved, factory->NewStringFromStaticChars("ignorePunctuation"),
      factory->ToBoolean(collator->getAttribute(UCOL_ALTERNATE_HANDLING,
                                                status) == UCOL_SHIFTED),
      LanguageMode::kSloppy)
      .Assert();

  // Set the locale
  char result[ULOC_FULLNAME_CAPACITY];
  status = U_ZERO_ERROR;
  uloc_toLanguageTag(icu_locale.getName(), result, ULOC_FULLNAME_CAPACITY,
                     FALSE, &status);
  if (U_SUCCESS(status)) {
    JSObject::SetProperty(
        isolate, resolved, factory->NewStringFromStaticChars("locale"),
        factory->NewStringFromAsciiChecked(result), LanguageMode::kSloppy)
        .Assert();
  } else {
    // This would never happen, since we got the locale from ICU.
    JSObject::SetProperty(
        isolate, resolved, factory->NewStringFromStaticChars("locale"),
        factory->NewStringFromStaticChars("und"), LanguageMode::kSloppy)
        .Assert();
  }
}

icu::BreakIterator* CreateICUBreakIterator(Isolate* isolate,
                                           const icu::Locale& icu_locale,
                                           Handle<JSObject> options) {
  UErrorCode status = U_ZERO_ERROR;
  icu::BreakIterator* break_iterator = nullptr;
  icu::UnicodeString type;
  if (!ExtractStringSetting(isolate, options, "type", &type)) return nullptr;

  if (type == UNICODE_STRING_SIMPLE("character")) {
    break_iterator =
        icu::BreakIterator::createCharacterInstance(icu_locale, status);
  } else if (type == UNICODE_STRING_SIMPLE("sentence")) {
    break_iterator =
        icu::BreakIterator::createSentenceInstance(icu_locale, status);
  } else if (type == UNICODE_STRING_SIMPLE("line")) {
    break_iterator = icu::BreakIterator::createLineInstance(icu_locale, status);
  } else {
    // Defualt is word iterator.
    break_iterator = icu::BreakIterator::createWordInstance(icu_locale, status);
  }

  if (U_FAILURE(status)) {
    delete break_iterator;
    return nullptr;
  }

  isolate->CountUsage(v8::Isolate::UseCounterFeature::kBreakIterator);

  return break_iterator;
}

void SetResolvedBreakIteratorSettings(Isolate* isolate,
                                      const icu::Locale& icu_locale,
                                      icu::BreakIterator* break_iterator,
                                      Handle<JSObject> resolved) {
  Factory* factory = isolate->factory();
  UErrorCode status = U_ZERO_ERROR;

  // Set the locale
  char result[ULOC_FULLNAME_CAPACITY];
  status = U_ZERO_ERROR;
  uloc_toLanguageTag(icu_locale.getName(), result, ULOC_FULLNAME_CAPACITY,
                     FALSE, &status);
  if (U_SUCCESS(status)) {
    JSObject::SetProperty(
        isolate, resolved, factory->NewStringFromStaticChars("locale"),
        factory->NewStringFromAsciiChecked(result), LanguageMode::kSloppy)
        .Assert();
  } else {
    // This would never happen, since we got the locale from ICU.
    JSObject::SetProperty(
        isolate, resolved, factory->NewStringFromStaticChars("locale"),
        factory->NewStringFromStaticChars("und"), LanguageMode::kSloppy)
        .Assert();
  }
}
}  // namespace

icu::Locale Intl::CreateICULocale(Isolate* isolate,
                                  Handle<String> bcp47_locale_str) {
  v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  v8::String::Utf8Value bcp47_locale(v8_isolate,
                                     v8::Utils::ToLocal(bcp47_locale_str));
  CHECK_NOT_NULL(*bcp47_locale);

  DisallowHeapAllocation no_gc;

  // Convert BCP47 into ICU locale format.
  UErrorCode status = U_ZERO_ERROR;
  char icu_result[ULOC_FULLNAME_CAPACITY];
  int icu_length = 0;

  // bcp47_locale_str should be a canonicalized language tag, which
  // means this shouldn't fail.
  uloc_forLanguageTag(*bcp47_locale, icu_result, ULOC_FULLNAME_CAPACITY,
                      &icu_length, &status);
  CHECK(U_SUCCESS(status));
  CHECK_LT(0, icu_length);

  icu::Locale icu_locale(icu_result);
  if (icu_locale.isBogus()) {
    FATAL("Failed to create ICU locale, are ICU data files missing?");
  }

  return icu_locale;
}

// static
icu::SimpleDateFormat* DateFormat::InitializeDateTimeFormat(
    Isolate* isolate, Handle<String> locale, Handle<JSObject> options,
    Handle<JSObject> resolved) {
  icu::Locale icu_locale = Intl::CreateICULocale(isolate, locale);
  DCHECK(!icu_locale.isBogus());

  icu::SimpleDateFormat* date_format =
      CreateICUDateFormat(isolate, icu_locale, options);
  if (!date_format) {
    // Remove extensions and try again.
    icu::Locale no_extension_locale(icu_locale.getBaseName());
    date_format = CreateICUDateFormat(isolate, no_extension_locale, options);

    if (!date_format) {
      FATAL("Failed to create ICU date format, are ICU data files missing?");
    }

    // Set resolved settings (pattern, numbering system, calendar).
    SetResolvedDateSettings(isolate, no_extension_locale, date_format,
                            resolved);
  } else {
    SetResolvedDateSettings(isolate, icu_locale, date_format, resolved);
  }

  CHECK_NOT_NULL(date_format);
  return date_format;
}

icu::SimpleDateFormat* DateFormat::UnpackDateFormat(Handle<JSObject> obj) {
  return reinterpret_cast<icu::SimpleDateFormat*>(obj->GetEmbedderField(0));
}

void DateFormat::DeleteDateFormat(const v8::WeakCallbackInfo<void>& data) {
  delete reinterpret_cast<icu::SimpleDateFormat*>(data.GetInternalField(0));
  GlobalHandles::Destroy(reinterpret_cast<Object**>(data.GetParameter()));
}

icu::DecimalFormat* NumberFormat::InitializeNumberFormat(
    Isolate* isolate, Handle<String> locale, Handle<JSObject> options,
    Handle<JSObject> resolved) {
  icu::Locale icu_locale = Intl::CreateICULocale(isolate, locale);
  DCHECK(!icu_locale.isBogus());

  icu::DecimalFormat* number_format =
      CreateICUNumberFormat(isolate, icu_locale, options);
  if (!number_format) {
    // Remove extensions and try again.
    icu::Locale no_extension_locale(icu_locale.getBaseName());
    number_format =
        CreateICUNumberFormat(isolate, no_extension_locale, options);

    if (!number_format) {
      FATAL("Failed to create ICU number format, are ICU data files missing?");
    }

    // Set resolved settings (pattern, numbering system).
    SetResolvedNumberSettings(isolate, no_extension_locale, number_format,
                              resolved);
  } else {
    SetResolvedNumberSettings(isolate, icu_locale, number_format, resolved);
  }

  CHECK_NOT_NULL(number_format);
  return number_format;
}

icu::DecimalFormat* NumberFormat::UnpackNumberFormat(Handle<JSObject> obj) {
  return reinterpret_cast<icu::DecimalFormat*>(
      obj->GetEmbedderField(NumberFormat::kDecimalFormatIndex));
}

void NumberFormat::DeleteNumberFormat(const v8::WeakCallbackInfo<void>& data) {
  delete reinterpret_cast<icu::DecimalFormat*>(data.GetInternalField(0));
  GlobalHandles::Destroy(reinterpret_cast<Object**>(data.GetParameter()));
}

icu::Collator* Collator::InitializeCollator(Isolate* isolate,
                                            Handle<String> locale,
                                            Handle<JSObject> options,
                                            Handle<JSObject> resolved) {
  icu::Locale icu_locale = Intl::CreateICULocale(isolate, locale);
  DCHECK(!icu_locale.isBogus());

  icu::Collator* collator = CreateICUCollator(isolate, icu_locale, options);
  if (!collator) {
    // Remove extensions and try again.
    icu::Locale no_extension_locale(icu_locale.getBaseName());
    collator = CreateICUCollator(isolate, no_extension_locale, options);

    if (!collator) {
      FATAL("Failed to create ICU collator, are ICU data files missing?");
    }

    // Set resolved settings (pattern, numbering system).
    SetResolvedCollatorSettings(isolate, no_extension_locale, collator,
                                resolved);
  } else {
    SetResolvedCollatorSettings(isolate, icu_locale, collator, resolved);
  }

  CHECK_NOT_NULL(collator);
  return collator;
}

icu::Collator* Collator::UnpackCollator(Handle<JSObject> obj) {
  return Managed<icu::Collator>::cast(obj->GetEmbedderField(0))->raw();
}

icu::BreakIterator* V8BreakIterator::InitializeBreakIterator(
    Isolate* isolate, Handle<String> locale, Handle<JSObject> options,
    Handle<JSObject> resolved) {
  icu::Locale icu_locale = Intl::CreateICULocale(isolate, locale);
  DCHECK(!icu_locale.isBogus());

  icu::BreakIterator* break_iterator =
      CreateICUBreakIterator(isolate, icu_locale, options);
  if (!break_iterator) {
    // Remove extensions and try again.
    icu::Locale no_extension_locale(icu_locale.getBaseName());
    break_iterator =
        CreateICUBreakIterator(isolate, no_extension_locale, options);

    if (!break_iterator) {
      FATAL("Failed to create ICU break iterator, are ICU data files missing?");
    }

    // Set resolved settings (locale).
    SetResolvedBreakIteratorSettings(isolate, no_extension_locale,
                                     break_iterator, resolved);
  } else {
    SetResolvedBreakIteratorSettings(isolate, icu_locale, break_iterator,
                                     resolved);
  }

  CHECK_NOT_NULL(break_iterator);
  return break_iterator;
}

icu::BreakIterator* V8BreakIterator::UnpackBreakIterator(Handle<JSObject> obj) {
  return reinterpret_cast<icu::BreakIterator*>(obj->GetEmbedderField(0));
}

void V8BreakIterator::DeleteBreakIterator(
    const v8::WeakCallbackInfo<void>& data) {
  delete reinterpret_cast<icu::BreakIterator*>(data.GetInternalField(0));
  delete reinterpret_cast<icu::UnicodeString*>(data.GetInternalField(1));
  GlobalHandles::Destroy(reinterpret_cast<Object**>(data.GetParameter()));
}

// Build the shortened locale; eg, convert xx_Yyyy_ZZ  to xx_ZZ.
bool Intl::RemoveLocaleScriptTag(const std::string& icu_locale,
                                 std::string* locale_less_script) {
  icu::Locale new_locale = icu::Locale::createCanonical(icu_locale.c_str());
  const char* icu_script = new_locale.getScript();
  if (icu_script == NULL || strlen(icu_script) == 0) {
    *locale_less_script = std::string();
    return false;
  }

  const char* icu_language = new_locale.getLanguage();
  const char* icu_country = new_locale.getCountry();
  icu::Locale short_locale = icu::Locale(icu_language, icu_country);
  const char* icu_name = short_locale.getName();
  *locale_less_script = std::string(icu_name);
  return true;
}

std::set<std::string> Intl::GetAvailableLocales(const IcuService& service) {
  const icu::Locale* icu_available_locales = nullptr;
  int32_t count = 0;
  std::set<std::string> locales;

  switch (service) {
    case IcuService::kBreakIterator:
      icu_available_locales = icu::BreakIterator::getAvailableLocales(count);
      break;
    case IcuService::kCollator:
      icu_available_locales = icu::Collator::getAvailableLocales(count);
      break;
    case IcuService::kDateFormat:
      icu_available_locales = icu::DateFormat::getAvailableLocales(count);
      break;
    case IcuService::kNumberFormat:
      icu_available_locales = icu::NumberFormat::getAvailableLocales(count);
      break;
    case IcuService::kPluralRules:
      // TODO(littledan): For PluralRules, filter out locales that
      // don't support PluralRules.
      // PluralRules is missing an appropriate getAvailableLocales method,
      // so we should filter from all locales, but it's not clear how; see
      // https://ssl.icu-project.org/trac/ticket/12756
      icu_available_locales = icu::Locale::getAvailableLocales(count);
      break;
    case IcuService::kResourceBundle: {
      UErrorCode status = U_ZERO_ERROR;
      UEnumeration* en = ures_openAvailableLocales(nullptr, &status);
      int32_t length = 0;
      const char* locale_str = uenum_next(en, &length, &status);
      while (U_SUCCESS(status) && (locale_str != nullptr)) {
        std::string locale(locale_str, length);
        std::replace(locale.begin(), locale.end(), '_', '-');
        locales.insert(locale);
        std::string shortened_locale;
        if (Intl::RemoveLocaleScriptTag(locale_str, &shortened_locale)) {
          std::replace(shortened_locale.begin(), shortened_locale.end(), '_',
                       '-');
          locales.insert(shortened_locale);
        }
        locale_str = uenum_next(en, &length, &status);
      }
      uenum_close(en);
      return locales;
    }
    case IcuService::kRelativeDateTimeFormatter: {
      // ICU RelativeDateTimeFormatter does not provide a getAvailableLocales()
      // interface, because RelativeDateTimeFormatter depends on
      // 1. NumberFormat and 2. ResourceBundle, return the
      // intersection of these two set.
      // ICU FR at https://unicode-org.atlassian.net/browse/ICU-20009
      // TODO(ftang): change to call ICU's getAvailableLocales() after it is
      // added.
      std::set<std::string> number_format_set(
          Intl::GetAvailableLocales(IcuService::kNumberFormat));
      std::set<std::string> resource_bundle_set(
          Intl::GetAvailableLocales(IcuService::kResourceBundle));
      set_intersection(resource_bundle_set.begin(), resource_bundle_set.end(),
                       number_format_set.begin(), number_format_set.end(),
                       std::inserter(locales, locales.begin()));
      return locales;
    }
    case IcuService::kListFormatter: {
      // TODO(ftang): for now just use
      // icu::Locale::getAvailableLocales(count) until we migrate to
      // Intl::GetAvailableLocales().
      // ICU FR at https://unicode-org.atlassian.net/browse/ICU-20015
      icu_available_locales = icu::Locale::getAvailableLocales(count);
      break;
    }
  }

  UErrorCode error = U_ZERO_ERROR;
  char result[ULOC_FULLNAME_CAPACITY];

  for (int32_t i = 0; i < count; ++i) {
    const char* icu_name = icu_available_locales[i].getName();

    error = U_ZERO_ERROR;
    // No need to force strict BCP47 rules.
    uloc_toLanguageTag(icu_name, result, ULOC_FULLNAME_CAPACITY, FALSE, &error);
    if (U_FAILURE(error) || error == U_STRING_NOT_TERMINATED_WARNING) {
      // This shouldn't happen, but lets not break the user.
      continue;
    }
    std::string locale(result);
    locales.insert(locale);

    std::string shortened_locale;
    if (Intl::RemoveLocaleScriptTag(icu_name, &shortened_locale)) {
      std::replace(shortened_locale.begin(), shortened_locale.end(), '_', '-');
      locales.insert(shortened_locale);
    }
  }

  return locales;
}

namespace {
IcuService StringToIcuService(Handle<String> service) {
  if (service->IsUtf8EqualTo(CStrVector("collator"))) {
    return IcuService::kCollator;
  } else if (service->IsUtf8EqualTo(CStrVector("numberformat"))) {
    return IcuService::kNumberFormat;
  } else if (service->IsUtf8EqualTo(CStrVector("dateformat"))) {
    return IcuService::kDateFormat;
  } else if (service->IsUtf8EqualTo(CStrVector("breakiterator"))) {
    return IcuService::kBreakIterator;
  } else if (service->IsUtf8EqualTo(CStrVector("pluralrules"))) {
    return IcuService::kPluralRules;
  } else if (service->IsUtf8EqualTo(CStrVector("relativetimeformat"))) {
    return IcuService::kRelativeDateTimeFormatter;
  } else if (service->IsUtf8EqualTo(CStrVector("listformat"))) {
    return IcuService::kListFormatter;
  }
  UNREACHABLE();
}
}  // namespace

V8_WARN_UNUSED_RESULT MaybeHandle<JSObject> Intl::AvailableLocalesOf(
    Isolate* isolate, Handle<String> service) {
  Factory* factory = isolate->factory();
  std::set<std::string> results =
      Intl::GetAvailableLocales(StringToIcuService(service));
  Handle<JSObject> locales = factory->NewJSObject(isolate->object_function());

  int32_t i = 0;
  for (auto iter = results.begin(); iter != results.end(); ++iter) {
    RETURN_ON_EXCEPTION(
        isolate,
        JSObject::SetOwnPropertyIgnoreAttributes(
            locales, factory->NewStringFromAsciiChecked(iter->c_str()),
            factory->NewNumber(i++), NONE),
        JSObject);
  }
  return locales;
}

V8_WARN_UNUSED_RESULT Handle<String> Intl::DefaultLocale(Isolate* isolate) {
  if (isolate->default_locale().empty()) {
    icu::Locale default_locale;
    // Translate ICU's fallback locale to a well-known locale.
    if (strcmp(default_locale.getName(), "en_US_POSIX") == 0) {
      isolate->set_default_locale("en-US");
    } else {
      // Set the locale
      char result[ULOC_FULLNAME_CAPACITY];
      UErrorCode status = U_ZERO_ERROR;
      int32_t length =
          uloc_toLanguageTag(default_locale.getName(), result,
                             ULOC_FULLNAME_CAPACITY, FALSE, &status);
      isolate->set_default_locale(
          U_SUCCESS(status) ? std::string(result, length) : "und");
    }
    DCHECK(!isolate->default_locale().empty());
  }
  return isolate->factory()->NewStringFromAsciiChecked(
      isolate->default_locale().c_str());
}

bool Intl::IsObjectOfType(Isolate* isolate, Handle<Object> input,
                          Intl::Type expected_type) {
  if (!input->IsJSObject()) return false;
  Handle<JSObject> obj = Handle<JSObject>::cast(input);

  Handle<Symbol> marker = isolate->factory()->intl_initialized_marker_symbol();
  Handle<Object> tag = JSReceiver::GetDataProperty(obj, marker);

  if (!tag->IsSmi()) return false;

  Intl::Type type = Intl::TypeFromSmi(Smi::cast(*tag));
  return type == expected_type;
}

namespace {

// In ECMA 402 v1, Intl constructors supported a mode of operation
// where calling them with an existing object as a receiver would
// transform the receiver into the relevant Intl instance with all
// internal slots. In ECMA 402 v2, this capability was removed, to
// avoid adding internal slots on existing objects. In ECMA 402 v3,
// the capability was re-added as "normative optional" in a mode
// which chains the underlying Intl instance on any object, when the
// constructor is called
//
// See ecma402/#legacy-constructor.
MaybeHandle<Object> LegacyUnwrapReceiver(Isolate* isolate,
                                         Handle<JSReceiver> receiver,
                                         Handle<JSFunction> constructor,
                                         Intl::Type type) {
  bool has_initialized_slot = Intl::IsObjectOfType(isolate, receiver, type);

  Handle<Object> obj_is_instance_of;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, obj_is_instance_of,
                             Object::InstanceOf(isolate, receiver, constructor),
                             Object);
  bool is_instance_of = obj_is_instance_of->BooleanValue(isolate);

  // 2. If receiver does not have an [[Initialized...]] internal slot
  //    and ? InstanceofOperator(receiver, constructor) is true, then
  if (!has_initialized_slot && is_instance_of) {
    // 2. a. Let new_receiver be ? Get(receiver, %Intl%.[[FallbackSymbol]]).
    Handle<Object> new_receiver;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, new_receiver,
        JSReceiver::GetProperty(isolate, receiver,
                                isolate->factory()->intl_fallback_symbol()),
        Object);
    return new_receiver;
  }

  return receiver;
}

}  // namespace

MaybeHandle<JSObject> Intl::UnwrapReceiver(Isolate* isolate,
                                           Handle<JSReceiver> receiver,
                                           Handle<JSFunction> constructor,
                                           Intl::Type type,
                                           Handle<String> method_name,
                                           bool check_legacy_constructor) {
  Handle<Object> new_receiver = receiver;
  if (check_legacy_constructor) {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, new_receiver,
        LegacyUnwrapReceiver(isolate, receiver, constructor, type), JSObject);
  }

  // 3. If Type(new_receiver) is not Object or nf does not have an
  //    [[Initialized...]]  internal slot, then
  if (!Intl::IsObjectOfType(isolate, new_receiver, type)) {
    // 3. a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                                 method_name, receiver),
                    JSObject);
  }

  // The above IsObjectOfType returns true only for JSObjects, which
  // makes this cast safe.
  return Handle<JSObject>::cast(new_receiver);
}

MaybeHandle<JSObject> NumberFormat::Unwrap(Isolate* isolate,
                                           Handle<JSReceiver> receiver,
                                           const char* method_name) {
  Handle<Context> native_context =
      Handle<Context>(isolate->context()->native_context(), isolate);
  Handle<JSFunction> constructor = Handle<JSFunction>(
      JSFunction::cast(native_context->intl_number_format_function()), isolate);
  Handle<String> method_name_str =
      isolate->factory()->NewStringFromAsciiChecked(method_name);

  return Intl::UnwrapReceiver(isolate, receiver, constructor,
                              Intl::Type::kNumberFormat, method_name_str, true);
}

MaybeHandle<String> NumberFormat::FormatNumber(
    Isolate* isolate, Handle<JSObject> number_format_holder, double value) {
  icu::DecimalFormat* number_format =
      NumberFormat::UnpackNumberFormat(number_format_holder);
  CHECK_NOT_NULL(number_format);

  icu::UnicodeString result;
  number_format->format(value, result);

  return isolate->factory()->NewStringFromTwoByte(Vector<const uint16_t>(
      reinterpret_cast<const uint16_t*>(result.getBuffer()), result.length()));
}

void Intl::DefineWEProperty(Isolate* isolate, Handle<JSObject> target,
                            Handle<Name> key, Handle<Object> value) {
  PropertyDescriptor desc;
  desc.set_writable(true);
  desc.set_enumerable(true);
  desc.set_value(value);
  Maybe<bool> success =
      JSReceiver::DefineOwnProperty(isolate, target, key, &desc, kDontThrow);
  DCHECK(success.IsJust() && success.FromJust());
  USE(success);
}

namespace {

// Define general regexp macros.
// Note "(?:" means the regexp group a non-capture group.
#define REGEX_ALPHA "[a-z]"
#define REGEX_DIGIT "[0-9]"
#define REGEX_ALPHANUM "(?:" REGEX_ALPHA "|" REGEX_DIGIT ")"

void BuildLanguageTagRegexps(Isolate* isolate) {
// Define the language tag regexp macros.
// For info on BCP 47 see https://tools.ietf.org/html/bcp47 .
// Because language tags are case insensitive per BCP 47 2.1.1 and regexp's
// defined below will always be used after lowercasing the input, uppercase
// ranges in BCP 47 2.1 are dropped and grandfathered tags are all lowercased.
// clang-format off
#define BCP47_REGULAR                                          \
  "(?:art-lojban|cel-gaulish|no-bok|no-nyn|zh-guoyu|zh-hakka|" \
  "zh-min|zh-min-nan|zh-xiang)"
#define BCP47_IRREGULAR                                  \
  "(?:en-gb-oed|i-ami|i-bnn|i-default|i-enochian|i-hak|" \
  "i-klingon|i-lux|i-mingo|i-navajo|i-pwn|i-tao|i-tay|"  \
  "i-tsu|sgn-be-fr|sgn-be-nl|sgn-ch-de)"
#define BCP47_GRANDFATHERED "(?:" BCP47_IRREGULAR "|" BCP47_REGULAR ")"
#define BCP47_PRIVATE_USE "(?:x(?:-" REGEX_ALPHANUM "{1,8})+)"

#define BCP47_SINGLETON "(?:" REGEX_DIGIT "|" "[a-wy-z])"

#define BCP47_EXTENSION "(?:" BCP47_SINGLETON "(?:-" REGEX_ALPHANUM "{2,8})+)"
#define BCP47_VARIANT  \
  "(?:" REGEX_ALPHANUM "{5,8}" "|" "(?:" REGEX_DIGIT REGEX_ALPHANUM "{3}))"

#define BCP47_REGION "(?:" REGEX_ALPHA "{2}" "|" REGEX_DIGIT "{3})"
#define BCP47_SCRIPT "(?:" REGEX_ALPHA "{4})"
#define BCP47_EXT_LANG "(?:" REGEX_ALPHA "{3}(?:-" REGEX_ALPHA "{3}){0,2})"
#define BCP47_LANGUAGE "(?:" REGEX_ALPHA "{2,3}(?:-" BCP47_EXT_LANG ")?" \
  "|" REGEX_ALPHA "{4}" "|" REGEX_ALPHA "{5,8})"
#define BCP47_LANG_TAG         \
  BCP47_LANGUAGE               \
  "(?:-" BCP47_SCRIPT ")?"     \
  "(?:-" BCP47_REGION ")?"     \
  "(?:-" BCP47_VARIANT ")*"    \
  "(?:-" BCP47_EXTENSION ")*"  \
  "(?:-" BCP47_PRIVATE_USE ")?"
  // clang-format on

  constexpr char kLanguageTagSingletonRegexp[] = "^" BCP47_SINGLETON "$";
  constexpr char kLanguageTagVariantRegexp[] = "^" BCP47_VARIANT "$";
  constexpr char kLanguageTagRegexp[] =
      "^(?:" BCP47_LANG_TAG "|" BCP47_PRIVATE_USE "|" BCP47_GRANDFATHERED ")$";

  UErrorCode status = U_ZERO_ERROR;
  icu::RegexMatcher* language_singleton_regexp_matcher = new icu::RegexMatcher(
      icu::UnicodeString(kLanguageTagSingletonRegexp, -1, US_INV), 0, status);
  icu::RegexMatcher* language_tag_regexp_matcher = new icu::RegexMatcher(
      icu::UnicodeString(kLanguageTagRegexp, -1, US_INV), 0, status);
  icu::RegexMatcher* language_variant_regexp_matcher = new icu::RegexMatcher(
      icu::UnicodeString(kLanguageTagVariantRegexp, -1, US_INV), 0, status);
  CHECK(U_SUCCESS(status));

  isolate->set_language_tag_regexp_matchers(language_singleton_regexp_matcher,
                                            language_tag_regexp_matcher,
                                            language_variant_regexp_matcher);
// Undefine the language tag regexp macros.
#undef BCP47_EXTENSION
#undef BCP47_EXT_LANG
#undef BCP47_GRANDFATHERED
#undef BCP47_IRREGULAR
#undef BCP47_LANG_TAG
#undef BCP47_LANGUAGE
#undef BCP47_PRIVATE_USE
#undef BCP47_REGION
#undef BCP47_REGULAR
#undef BCP47_SCRIPT
#undef BCP47_SINGLETON
#undef BCP47_VARIANT
}

// Undefine the general regexp macros.
#undef REGEX_ALPHA
#undef REGEX_DIGIT
#undef REGEX_ALPHANUM

icu::RegexMatcher* GetLanguageSingletonRegexMatcher(Isolate* isolate) {
  icu::RegexMatcher* language_singleton_regexp_matcher =
      isolate->language_singleton_regexp_matcher();
  if (language_singleton_regexp_matcher == nullptr) {
    BuildLanguageTagRegexps(isolate);
    language_singleton_regexp_matcher =
        isolate->language_singleton_regexp_matcher();
  }
  return language_singleton_regexp_matcher;
}

icu::RegexMatcher* GetLanguageTagRegexMatcher(Isolate* isolate) {
  icu::RegexMatcher* language_tag_regexp_matcher =
      isolate->language_tag_regexp_matcher();
  if (language_tag_regexp_matcher == nullptr) {
    BuildLanguageTagRegexps(isolate);
    language_tag_regexp_matcher = isolate->language_tag_regexp_matcher();
  }
  return language_tag_regexp_matcher;
}

icu::RegexMatcher* GetLanguageVariantRegexMatcher(Isolate* isolate) {
  icu::RegexMatcher* language_variant_regexp_matcher =
      isolate->language_variant_regexp_matcher();
  if (language_variant_regexp_matcher == nullptr) {
    BuildLanguageTagRegexps(isolate);
    language_variant_regexp_matcher =
        isolate->language_variant_regexp_matcher();
  }
  return language_variant_regexp_matcher;
}

}  // anonymous namespace

MaybeHandle<JSObject> Intl::ResolveLocale(Isolate* isolate, const char* service,
                                          Handle<Object> requestedLocales,
                                          Handle<Object> options) {
  Handle<String> service_str =
      isolate->factory()->NewStringFromAsciiChecked(service);

  Handle<JSFunction> resolve_locale_function = isolate->resolve_locale();

  Handle<Object> result;
  Handle<Object> undefined_value = isolate->factory()->undefined_value();
  Handle<Object> args[] = {service_str, requestedLocales, options};
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result,
      Execution::Call(isolate, resolve_locale_function, undefined_value,
                      arraysize(args), args),
      JSObject);

  return Handle<JSObject>::cast(result);
}

MaybeHandle<JSObject> Intl::CanonicalizeLocaleList(Isolate* isolate,
                                                   Handle<Object> locales) {
  Handle<JSFunction> canonicalize_locale_list_function =
      isolate->canonicalize_locale_list();

  Handle<Object> result;
  Handle<Object> undefined_value = isolate->factory()->undefined_value();
  Handle<Object> args[] = {locales};
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result,
      Execution::Call(isolate, canonicalize_locale_list_function,
                      undefined_value, arraysize(args), args),
      JSObject);

  return Handle<JSObject>::cast(result);
}

Maybe<bool> Intl::GetStringOption(Isolate* isolate, Handle<JSReceiver> options,
                                  const char* property,
                                  std::vector<const char*> values,
                                  const char* service,
                                  std::unique_ptr<char[]>* result) {
  Handle<String> property_str =
      isolate->factory()->NewStringFromAsciiChecked(property);

  // 1. Let value be ? Get(options, property).
  Handle<Object> value;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, value,
      Object::GetPropertyOrElement(isolate, options, property_str),
      Nothing<bool>());

  if (value->IsUndefined(isolate)) {
    return Just(false);
  }

  // 2. c. Let value be ? ToString(value).
  Handle<String> value_str;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, value_str, Object::ToString(isolate, value), Nothing<bool>());
  std::unique_ptr<char[]> value_cstr = value_str->ToCString();

  // 2. d. if values is not undefined, then
  if (values.size() > 0) {
    // 2. d. i. If values does not contain an element equal to value,
    // throw a RangeError exception.
    for (size_t i = 0; i < values.size(); i++) {
      if (strcmp(values.at(i), value_cstr.get()) == 0) {
        // 2. e. return value
        *result = std::move(value_cstr);
        return Just(true);
      }
    }

    Handle<String> service_str =
        isolate->factory()->NewStringFromAsciiChecked(service);
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kValueOutOfRange, value, service_str,
                      property_str),
        Nothing<bool>());
  }

  // 2. e. return value
  *result = std::move(value_cstr);
  return Just(true);
}

V8_WARN_UNUSED_RESULT Maybe<bool> Intl::GetBoolOption(
    Isolate* isolate, Handle<JSReceiver> options, const char* property,
    const char* service, bool* result) {
  Handle<String> property_str =
      isolate->factory()->NewStringFromAsciiChecked(property);

  // 1. Let value be ? Get(options, property).
  Handle<Object> value;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, value,
      Object::GetPropertyOrElement(isolate, options, property_str),
      Nothing<bool>());

  // 2. If value is not undefined, then
  if (!value->IsUndefined(isolate)) {
    // 2. b. i. Let value be ToBoolean(value).
    *result = value->BooleanValue(isolate);

    // 2. e. return value
    return Just(true);
  }

  return Just(false);
}

namespace {

char AsciiToLower(char c) {
  if (c < 'A' || c > 'Z') {
    return c;
  }
  return c | (1 << 5);
}

/**
 * Check the structural Validity of the language tag per ECMA 402 6.2.2:
 *   - Well-formed per RFC 5646 2.1
 *   - There are no duplicate variant subtags
 *   - There are no duplicate singleton (extension) subtags
 *
 * One extra-check is done (from RFC 5646 2.2.9): the tag is compared
 * against the list of grandfathered tags. However, subtags for
 * primary/extended language, script, region, variant are not checked
 * against the IANA language subtag registry.
 *
 * ICU is too permissible and lets invalid tags, like
 * hant-cmn-cn, through.
 *
 * Returns false if the language tag is invalid.
 */
bool IsStructurallyValidLanguageTag(Isolate* isolate,
                                    const std::string& locale_in) {
  if (!String::IsAscii(locale_in.c_str(),
                       static_cast<int>(locale_in.length()))) {
    return false;
  }
  std::string locale(locale_in);
  icu::RegexMatcher* language_tag_regexp_matcher =
      GetLanguageTagRegexMatcher(isolate);

  // Check if it's well-formed, including grandfathered tags.
  icu::UnicodeString locale_uni(locale.c_str(), -1, US_INV);
  // Note: icu::RegexMatcher::reset does not make a copy of the input string
  // so cannot use a temp value; ie: cannot create it as a call parameter.
  language_tag_regexp_matcher->reset(locale_uni);
  UErrorCode status = U_ZERO_ERROR;
  bool is_valid_lang_tag = language_tag_regexp_matcher->matches(status);
  if (!is_valid_lang_tag || V8_UNLIKELY(U_FAILURE(status))) {
    return false;
  }

  // Just return if it's a x- form. It's all private.
  if (locale.find("x-") == 0) {
    return true;
  }

  // Check if there are any duplicate variants or singletons (extensions).

  // Remove private use section.
  locale = locale.substr(0, locale.find("-x-"));

  // Skip language since it can match variant regex, so we start from 1.
  // We are matching i-klingon here, but that's ok, since i-klingon-klingon
  // is not valid and would fail LANGUAGE_TAG_RE test.
  size_t pos = 0;
  std::vector<std::string> parts;
  while ((pos = locale.find("-")) != std::string::npos) {
    std::string token = locale.substr(0, pos);
    parts.push_back(token);
    locale = locale.substr(pos + 1);
  }
  if (locale.length() != 0) {
    parts.push_back(locale);
  }

  icu::RegexMatcher* language_variant_regexp_matcher =
      GetLanguageVariantRegexMatcher(isolate);

  icu::RegexMatcher* language_singleton_regexp_matcher =
      GetLanguageSingletonRegexMatcher(isolate);

  std::vector<std::string> variants;
  std::vector<std::string> extensions;
  for (auto it = parts.begin() + 1; it != parts.end(); it++) {
    icu::UnicodeString part(it->data(), -1, US_INV);
    language_variant_regexp_matcher->reset(part);
    bool is_language_variant = language_variant_regexp_matcher->matches(status);
    if (V8_UNLIKELY(U_FAILURE(status))) {
      return false;
    }
    if (is_language_variant && extensions.size() == 0) {
      if (std::find(variants.begin(), variants.end(), *it) == variants.end()) {
        variants.push_back(*it);
      } else {
        return false;
      }
    }

    language_singleton_regexp_matcher->reset(part);
    bool is_language_singleton =
        language_singleton_regexp_matcher->matches(status);
    if (V8_UNLIKELY(U_FAILURE(status))) {
      return false;
    }
    if (is_language_singleton) {
      if (std::find(extensions.begin(), extensions.end(), *it) ==
          extensions.end()) {
        extensions.push_back(*it);
      } else {
        return false;
      }
    }
  }

  return true;
}

bool IsLowerAscii(char c) { return c >= 'a' && c < 'z'; }

bool IsTwoLetterLanguage(const std::string& locale) {
  // Two letters, both in range 'a'-'z'...
  return locale.length() == 2 && IsLowerAscii(locale[0]) &&
         IsLowerAscii(locale[1]);
}

bool IsDeprecatedLanguage(const std::string& locale) {
  //  Check if locale is one of the deprecated language tags:
  return locale == "in" || locale == "iw" || locale == "ji" || locale == "jw";
}

// Reference:
// https://www.iana.org/assignments/language-subtag-registry/language-subtag-registry
bool IsGrandfatheredTagWithoutPreferredVaule(const std::string& locale) {
  if (V8_UNLIKELY(locale == "zh-min" || locale == "cel-gaulish")) return true;
  if (locale.length() > 6 /* i-mingo is 7 chars long */ &&
      V8_UNLIKELY(locale[0] == 'i' && locale[1] == '-')) {
    return locale.substr(2) == "default" || locale.substr(2) == "enochian" ||
           locale.substr(2) == "mingo";
  }
  return false;
}

}  // anonymous namespace

MaybeHandle<String> Intl::CanonicalizeLanguageTag(Isolate* isolate,
                                                  Handle<Object> locale_in) {
  Handle<String> locale_str;
  if (locale_in->IsString()) {
    locale_str = Handle<String>::cast(locale_in);
  } else if (locale_in->IsJSReceiver()) {
    ASSIGN_RETURN_ON_EXCEPTION(isolate, locale_str,
                               Object::ToString(isolate, locale_in), String);
  } else {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kLanguageID),
                    String);
  }
  std::string locale(locale_str->ToCString().get());

  // Optimize for the most common case: a 2-letter language code in the
  // canonical form/lowercase that is not one of the deprecated codes
  // (in, iw, ji, jw). Don't check for ~70 of 3-letter deprecated language
  // codes. Instead, let them be handled by ICU in the slow path. However,
  // fast-track 'fil' (3-letter canonical code).
  if ((IsTwoLetterLanguage(locale) && !IsDeprecatedLanguage(locale)) ||
      locale == "fil") {
    return locale_str;
  }

  // Because per BCP 47 2.1.1 language tags are case-insensitive, lowercase
  // the input before any more check.
  std::transform(locale.begin(), locale.end(), locale.begin(), AsciiToLower);
  if (!IsStructurallyValidLanguageTag(isolate, locale)) {
    THROW_NEW_ERROR(
        isolate,
        NewRangeError(MessageTemplate::kInvalidLanguageTag, locale_str),
        String);
  }

  // ICU maps a few grandfathered tags to what looks like a regular language
  // tag even though IANA language tag registry does not have a preferred
  // entry map for them. Return them as they're with lowercasing.
  if (IsGrandfatheredTagWithoutPreferredVaule(locale))
    return isolate->factory()->NewStringFromAsciiChecked(locale.data());

  // // ECMA 402 6.2.3
  // TODO(jshin): uloc_{for,to}TanguageTag can fail even for a structually valid
  // language tag if it's too long (much longer than 100 chars). Even if we
  // allocate a longer buffer, ICU will still fail if it's too long. Either
  // propose to Ecma 402 to put a limit on the locale length or change ICU to
  // handle long locale names better. See
  // https://unicode-org.atlassian.net/browse/ICU-13417
  UErrorCode error = U_ZERO_ERROR;
  char icu_result[ULOC_FULLNAME_CAPACITY];
  uloc_forLanguageTag(locale.c_str(), icu_result, ULOC_FULLNAME_CAPACITY,
                      nullptr, &error);
  if (U_FAILURE(error) || error == U_STRING_NOT_TERMINATED_WARNING) {
    // TODO(jshin): This should not happen because the structural validity
    // is already checked. If that's the case, remove this.
    THROW_NEW_ERROR(
        isolate,
        NewRangeError(MessageTemplate::kInvalidLanguageTag, locale_str),
        String);
  }

  // Force strict BCP47 rules.
  char result[ULOC_FULLNAME_CAPACITY];
  int32_t result_len = uloc_toLanguageTag(icu_result, result,
                                          ULOC_FULLNAME_CAPACITY, TRUE, &error);

  if (U_FAILURE(error)) {
    THROW_NEW_ERROR(
        isolate,
        NewRangeError(MessageTemplate::kInvalidLanguageTag, locale_str),
        String);
  }

  return isolate->factory()
      ->NewStringFromOneByte(OneByteVector(result, result_len), NOT_TENURED)
      .ToHandleChecked();
}

// ecma-402/#sec-currencydigits
Handle<Smi> Intl::CurrencyDigits(Isolate* isolate, Handle<String> currency) {
  v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  v8::String::Value currency_string(v8_isolate, v8::Utils::ToLocal(currency));
  CHECK_NOT_NULL(*currency_string);

  DisallowHeapAllocation no_gc;
  UErrorCode status = U_ZERO_ERROR;
  uint32_t fraction_digits = ucurr_getDefaultFractionDigits(
      reinterpret_cast<const UChar*>(*currency_string), &status);
  // For missing currency codes, default to the most common, 2
  if (U_FAILURE(status)) fraction_digits = 2;
  return Handle<Smi>(Smi::FromInt(fraction_digits), isolate);
}

MaybeHandle<JSObject> Intl::CreateNumberFormat(Isolate* isolate,
                                               Handle<String> locale,
                                               Handle<JSObject> options,
                                               Handle<JSObject> resolved) {
  Handle<JSFunction> constructor(
      isolate->native_context()->intl_number_format_function(), isolate);

  Handle<JSObject> local_object;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, local_object,
                             JSObject::New(constructor, constructor), JSObject);

  // Set number formatter as embedder field of the resulting JS object.
  icu::DecimalFormat* number_format =
      NumberFormat::InitializeNumberFormat(isolate, locale, options, resolved);

  CHECK_NOT_NULL(number_format);

  local_object->SetEmbedderField(NumberFormat::kDecimalFormatIndex,
                                 reinterpret_cast<Smi*>(number_format));

  Handle<Object> wrapper = isolate->global_handles()->Create(*local_object);
  GlobalHandles::MakeWeak(wrapper.location(), wrapper.location(),
                          NumberFormat::DeleteNumberFormat,
                          WeakCallbackType::kInternalFields);
  return local_object;
}

namespace {

// Remove the following after we port InitializeLocaleList from src/js/intl.js
// to c++ https://bugs.chromium.org/p/v8/issues/detail?id=7987
// The following are temporary function calling back into js code in
// src/js/intl.js to call pre-existing functions until they are all moved to C++
// under src/objects/*.
// TODO(ftang): remove these temp function after bstell move them from js into
// C++

MaybeHandle<JSObject> InitializeLocaleList(Isolate* isolate,
                                           Handle<Object> list) {
  Handle<Object> result;
  Handle<Object> undefined_value(ReadOnlyRoots(isolate).undefined_value(),
                                 isolate);
  Handle<Object> args[] = {list};
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result,
      Execution::Call(isolate, isolate->initialize_locale_list(),
                      undefined_value, arraysize(args), args),
      JSArray);
  return Handle<JSObject>::cast(result);
}

bool IsAToZ(char ch) {
  return (('A' <= ch) && (ch <= 'Z')) || (('a' <= ch) && (ch <= 'z'));
}

MaybeHandle<JSObject> CachedOrNewService(Isolate* isolate,
                                         Handle<String> service,
                                         Handle<Object> locales,
                                         Handle<Object> options) {
  Handle<Object> result;
  Handle<Object> undefined_value(ReadOnlyRoots(isolate).undefined_value(),
                                 isolate);
  Handle<Object> args[] = {service, locales, options};
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result,
      Execution::Call(isolate, isolate->cached_or_new_service(),
                      undefined_value, arraysize(args), args),
      JSArray);
  return Handle<JSObject>::cast(result);
}

}  // namespace

// Verifies that the input is a well-formed ISO 4217 currency code.
// ecma402/#sec-currency-codes
bool Intl::IsWellFormedCurrencyCode(Isolate* isolate, Handle<String> currency) {
  // 2. If the number of elements in normalized is not 3, return false.
  if (currency->length() != 3) return false;

  currency = String::Flatten(isolate, currency);
  {
    DisallowHeapAllocation no_gc;
    String::FlatContent flat = currency->GetFlatContent();

    // 1. Let normalized be the result of mapping currency to upper case as
    // described in 6.1. 3. If normalized contains any character that is not in
    // the range "A" to "Z" (U+0041 to U+005A), return false. 4. Return true.
    // Don't uppercase to test. It could convert invalid code into a valid one.
    // For example \u00DFP (Eszett+P) becomes SSP.
    return (IsAToZ(flat.Get(0)) && IsAToZ(flat.Get(1)) && IsAToZ(flat.Get(2)));
  }
}

// ecma402 #sup-string.prototype.tolocalelowercase
// ecma402 #sup-string.prototype.tolocaleuppercase
MaybeHandle<String> Intl::StringLocaleConvertCase(Isolate* isolate,
                                                  Handle<String> s,
                                                  bool to_upper,
                                                  Handle<Object> locales) {
  Factory* factory = isolate->factory();
  Handle<String> requested_locale;
  if (locales->IsUndefined()) {
    requested_locale = Intl::DefaultLocale(isolate);
  } else if (locales->IsString()) {
    ASSIGN_RETURN_ON_EXCEPTION(isolate, requested_locale,
                               CanonicalizeLanguageTag(isolate, locales),
                               String);
  } else {
    Handle<JSObject> list;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, list,
                               InitializeLocaleList(isolate, locales), String);
    Handle<Object> length;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, length, Object::GetLengthFromArrayLike(isolate, list), String);
    if (length->Number() > 0) {
      Handle<Object> element;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, element,
          JSObject::GetPropertyOrElement(
              isolate, list, factory->NumberToString(factory->NewNumber(0))),
          String);
      ASSIGN_RETURN_ON_EXCEPTION(isolate, requested_locale,
                                 Object::ToString(isolate, element), String);
    } else {
      requested_locale = Intl::DefaultLocale(isolate);
    }
  }
  int dash = String::IndexOf(isolate, requested_locale,
                             factory->NewStringFromStaticChars("-"), 0);
  if (dash > 0) {
    requested_locale = factory->NewSubString(requested_locale, 0, dash);
  }

  // Primary language tag can be up to 8 characters long in theory.
  // https://tools.ietf.org/html/bcp47#section-2.2.1
  DCHECK_LE(requested_locale->length(), 8);
  requested_locale = String::Flatten(isolate, requested_locale);
  s = String::Flatten(isolate, s);

  // All the languages requiring special-handling have two-letter codes.
  // Note that we have to check for '!= 2' here because private-use language
  // tags (x-foo) or grandfathered irregular tags (e.g. i-enochian) would have
  // only 'x' or 'i' when they get here.
  if (V8_UNLIKELY(requested_locale->length() != 2)) {
    Handle<Object> obj(ConvertCase(s, to_upper, isolate), isolate);
    return Object::ToString(isolate, obj);
  }

  char c1, c2;
  {
    DisallowHeapAllocation no_gc;
    String::FlatContent lang = requested_locale->GetFlatContent();
    c1 = lang.Get(0);
    c2 = lang.Get(1);
  }
  // TODO(jshin): Consider adding a fast path for ASCII or Latin-1. The fastpath
  // in the root locale needs to be adjusted for az, lt and tr because even case
  // mapping of ASCII range characters are different in those locales.
  // Greek (el) does not require any adjustment.
  if (V8_UNLIKELY(c1 == 't' && c2 == 'r')) {
    Handle<Object> obj(LocaleConvertCase(s, isolate, to_upper, "tr"), isolate);
    return Object::ToString(isolate, obj);
  }
  if (V8_UNLIKELY(c1 == 'e' && c2 == 'l')) {
    Handle<Object> obj(LocaleConvertCase(s, isolate, to_upper, "el"), isolate);
    return Object::ToString(isolate, obj);
  }
  if (V8_UNLIKELY(c1 == 'l' && c2 == 't')) {
    Handle<Object> obj(LocaleConvertCase(s, isolate, to_upper, "lt"), isolate);
    return Object::ToString(isolate, obj);
  }
  if (V8_UNLIKELY(c1 == 'a' && c2 == 'z')) {
    Handle<Object> obj(LocaleConvertCase(s, isolate, to_upper, "az"), isolate);
    return Object::ToString(isolate, obj);
  }

  Handle<Object> obj(ConvertCase(s, to_upper, isolate), isolate);
  return Object::ToString(isolate, obj);
}

MaybeHandle<Object> Intl::StringLocaleCompare(Isolate* isolate,
                                              Handle<String> string1,
                                              Handle<String> string2,
                                              Handle<Object> locales,
                                              Handle<Object> options) {
  Factory* factory = isolate->factory();
  Handle<JSObject> collator_holder;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, collator_holder,
      CachedOrNewService(isolate, factory->NewStringFromStaticChars("collator"),
                         locales, options),
      Object);
  DCHECK(Intl::IsObjectOfType(isolate, collator_holder, Intl::kCollator));
  return Intl::InternalCompare(isolate, collator_holder, string1, string2);
}

Handle<Object> Intl::InternalCompare(Isolate* isolate,
                                     Handle<JSObject> collator_holder,
                                     Handle<String> string1,
                                     Handle<String> string2) {
  Factory* factory = isolate->factory();
  icu::Collator* collator = Collator::UnpackCollator(collator_holder);
  CHECK_NOT_NULL(collator);

  string1 = String::Flatten(isolate, string1);
  string2 = String::Flatten(isolate, string2);

  UCollationResult result;
  UErrorCode status = U_ZERO_ERROR;
  {
    DisallowHeapAllocation no_gc;
    int32_t length1 = string1->length();
    int32_t length2 = string2->length();
    String::FlatContent flat1 = string1->GetFlatContent();
    String::FlatContent flat2 = string2->GetFlatContent();
    std::unique_ptr<uc16[]> sap1;
    std::unique_ptr<uc16[]> sap2;
    icu::UnicodeString string_val1(
        FALSE, GetUCharBufferFromFlat(flat1, &sap1, length1), length1);
    icu::UnicodeString string_val2(
        FALSE, GetUCharBufferFromFlat(flat2, &sap2, length2), length2);
    result = collator->compare(string_val1, string_val2, status);
  }
  DCHECK(U_SUCCESS(status));

  return factory->NewNumberFromInt(result);
}

// ecma402/#sup-properties-of-the-number-prototype-object
MaybeHandle<String> Intl::NumberToLocaleString(Isolate* isolate,
                                               Handle<Object> num,
                                               Handle<Object> locales,
                                               Handle<Object> options) {
  Factory* factory = isolate->factory();
  Handle<JSObject> number_format_holder;
  // 2. Let numberFormat be ? Construct(%NumberFormat%, « locales, options »).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, number_format_holder,
      CachedOrNewService(isolate,
                         factory->NewStringFromStaticChars("numberformat"),
                         locales, options),
      String);
  DCHECK(
      Intl::IsObjectOfType(isolate, number_format_holder, Intl::kNumberFormat));
  Handle<Object> number_obj;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, number_obj,
                             Object::ToNumber(isolate, num), String);

  // Spec treats -0 and +0 as 0.
  double number = number_obj->Number() + 0;
  // Return FormatNumber(numberFormat, x).
  return NumberFormat::FormatNumber(isolate, number_format_holder, number);
}

// ecma402/#sec-defaultnumberoption
Maybe<int> Intl::DefaultNumberOption(Isolate* isolate, Handle<Object> value,
                                     int min, int max, int fallback,
                                     Handle<String> property) {
  // 2. Else, return fallback.
  if (value->IsUndefined()) return Just(fallback);

  // 1. If value is not undefined, then
  // a. Let value be ? ToNumber(value).
  Handle<Object> value_num;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, value_num, Object::ToNumber(isolate, value), Nothing<int>());
  DCHECK(value_num->IsNumber());

  // b. If value is NaN or less than minimum or greater than maximum, throw a
  // RangeError exception.
  if (value_num->IsNaN() || value_num->Number() < min ||
      value_num->Number() > max) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kPropertyValueOutOfRange, property),
        Nothing<int>());
  }

  // The max and min arguments are integers and the above check makes
  // sure that we are within the integer range making this double to
  // int conversion safe.
  //
  // c. Return floor(value).
  return Just(FastD2I(floor(value_num->Number())));
}

// ecma402/#sec-getnumberoption
Maybe<int> Intl::GetNumberOption(Isolate* isolate, Handle<JSReceiver> options,
                                 Handle<String> property, int min, int max,
                                 int fallback) {
  // 1. Let value be ? Get(options, property).
  Handle<Object> value;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, value, JSReceiver::GetProperty(isolate, options, property),
      Nothing<int>());

  // Return ? DefaultNumberOption(value, minimum, maximum, fallback).
  return DefaultNumberOption(isolate, value, min, max, fallback, property);
}

Maybe<int> Intl::GetNumberOption(Isolate* isolate, Handle<JSReceiver> options,
                                 const char* property, int min, int max,
                                 int fallback) {
  Handle<String> property_str =
      isolate->factory()->NewStringFromAsciiChecked(property);
  return GetNumberOption(isolate, options, property_str, min, max, fallback);
}

Maybe<bool> Intl::SetNumberFormatDigitOptions(Isolate* isolate,
                                              icu::DecimalFormat* number_format,
                                              Handle<JSReceiver> options,
                                              int mnfd_default,
                                              int mxfd_default) {
  CHECK_NOT_NULL(number_format);

  // 5. Let mnid be ? GetNumberOption(options, "minimumIntegerDigits,", 1, 21,
  // 1).
  int mnid;
  if (!GetNumberOption(isolate, options, "minimumIntegerDigits", 1, 21, 1)
           .To(&mnid)) {
    return Nothing<bool>();
  }

  // 6. Let mnfd be ? GetNumberOption(options, "minimumFractionDigits", 0, 20,
  // mnfdDefault).
  int mnfd;
  if (!GetNumberOption(isolate, options, "minimumFractionDigits", 0, 20,
                       mnfd_default)
           .To(&mnfd)) {
    return Nothing<bool>();
  }

  // 7. Let mxfdActualDefault be max( mnfd, mxfdDefault ).
  int mxfd_actual_default = std::max(mnfd, mxfd_default);

  // 8. Let mxfd be ? GetNumberOption(options,
  // "maximumFractionDigits", mnfd, 20, mxfdActualDefault).
  int mxfd;
  if (!GetNumberOption(isolate, options, "maximumFractionDigits", mnfd, 20,
                       mxfd_actual_default)
           .To(&mxfd)) {
    return Nothing<bool>();
  }

  // 9.  Let mnsd be ? Get(options, "minimumSignificantDigits").
  Handle<Object> mnsd_obj;
  Handle<String> mnsd_str =
      isolate->factory()->NewStringFromStaticChars("minimumSignificantDigits");
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, mnsd_obj, JSReceiver::GetProperty(isolate, options, mnsd_str),
      Nothing<bool>());

  // 10. Let mxsd be ? Get(options, "maximumSignificantDigits").
  Handle<Object> mxsd_obj;
  Handle<String> mxsd_str =
      isolate->factory()->NewStringFromStaticChars("maximumSignificantDigits");
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, mxsd_obj, JSReceiver::GetProperty(isolate, options, mxsd_str),
      Nothing<bool>());

  // 11. Set intlObj.[[MinimumIntegerDigits]] to mnid.
  number_format->setMinimumIntegerDigits(mnid);

  // 12. Set intlObj.[[MinimumFractionDigits]] to mnfd.
  number_format->setMinimumFractionDigits(mnfd);

  // 13. Set intlObj.[[MaximumFractionDigits]] to mxfd.
  number_format->setMaximumFractionDigits(mxfd);

  bool significant_digits_used = false;
  // 14. If mnsd is not undefined or mxsd is not undefined, then
  if (!mnsd_obj->IsUndefined(isolate) || !mxsd_obj->IsUndefined(isolate)) {
    // 14. a. Let mnsd be ? DefaultNumberOption(mnsd, 1, 21, 1).
    int mnsd;
    if (!DefaultNumberOption(isolate, mnsd_obj, 1, 21, 1, mnsd_str).To(&mnsd)) {
      return Nothing<bool>();
    }

    // 14. b. Let mxsd be ? DefaultNumberOption(mxsd, mnsd, 21, 21).
    int mxsd;
    if (!DefaultNumberOption(isolate, mxsd_obj, mnsd, 21, 21, mxsd_str)
             .To(&mxsd)) {
      return Nothing<bool>();
    }

    significant_digits_used = true;

    // 14. c. Set intlObj.[[MinimumSignificantDigits]] to mnsd.
    number_format->setMinimumSignificantDigits(mnsd);

    // 14. d. Set intlObj.[[MaximumSignificantDigits]] to mxsd.
    number_format->setMaximumSignificantDigits(mxsd);
  }

  number_format->setSignificantDigitsUsed(significant_digits_used);
  number_format->setRoundingMode(icu::DecimalFormat::kRoundHalfUp);
  return Just(true);
}

}  // namespace internal
}  // namespace v8
