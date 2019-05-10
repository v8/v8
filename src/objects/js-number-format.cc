// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-number-format.h"

#include <set>
#include <string>

#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-number-format-inl.h"
#include "unicode/currunit.h"
#include "unicode/decimfmt.h"
#include "unicode/locid.h"
#include "unicode/nounit.h"
#include "unicode/numberformatter.h"
#include "unicode/numfmt.h"
#include "unicode/ucurr.h"
#include "unicode/uloc.h"
#include "unicode/unumberformatter.h"
#include "unicode/uvernum.h"  // for U_ICU_VERSION_MAJOR_NUM

namespace v8 {
namespace internal {

namespace {

// [[Style]] is one of the values "decimal", "percent", "currency",
// or "unit" identifying the style of the number format.
// Note: "unit" is added in proposal-unified-intl-numberformat
enum class Style {
  DECIMAL,
  PERCENT,
  CURRENCY,
  UNIT,
};

// [[CurrencyDisplay]] is one of the values "code", "symbol", "name",
// or "narrow-symbol" identifying the display of the currency number format.
// Note: "narrow-symbol" is added in proposal-unified-intl-numberformat
enum class CurrencyDisplay {
  CODE,
  SYMBOL,
  NAME,
  NARROW_SYMBOL,
};

// [[CurrencySign]] is one of the String values "standard" or "accounting",
// specifying whether to render negative numbers in accounting format, often
// signified by parenthesis. It is only used when [[Style]] has the value
// "currency" and when [[SignDisplay]] is not "never".
enum class CurrencySign {
  STANDARD,
  ACCOUNTING,
};

// [[UnitDisplay]] is one of the String values "short", "narrow", or "long",
// specifying whether to display the unit as a symbol, narrow symbol, or
// localized long name if formatting with the "unit" or "percent" style. It is
// only used when [[Style]] has the value "unit" or "percent".
enum class UnitDisplay {
  SHORT,
  NARROW,
  LONG,
};

// [[Notation]] is one of the String values "standard", "scientific",
// "engineering", or "compact", specifying whether the number should be
// displayed without scaling, scaled to the units place with the power of ten
// in scientific notation, scaled to the nearest thousand with the power of
// ten in scientific notation, or scaled to the nearest locale-dependent
// compact decimal notation power of ten with the corresponding compact
// decimal notation affix.

enum class Notation {
  STANDARD,
  SCIENTIFIC,
  ENGINEERING,
  COMPACT,
};

// [[CompactDisplay]] is one of the String values "short" or "long",
// specifying whether to display compact notation affixes in short form ("5K")
// or long form ("5 thousand") if formatting with the "compact" notation. It
// is only used when [[Notation]] has the value "compact".
enum class CompactDisplay {
  SHORT,
  LONG,
};

// [[SignDisplay]] is one of the String values "auto", "always", "never", or
// "except-zero", specifying whether to show the sign on negative numbers
// only, positive and negative numbers including zero, neither positive nor
// negative numbers, or positive and negative numbers but not zero.
enum class SignDisplay {
  AUTO,
  ALWAYS,
  NEVER,
  EXCEPT_ZERO,
};

UNumberUnitWidth ToUNumberUnitWidth(CurrencyDisplay currency_display) {
  switch (currency_display) {
    case CurrencyDisplay::SYMBOL:
      return UNumberUnitWidth::UNUM_UNIT_WIDTH_SHORT;
    case CurrencyDisplay::CODE:
      return UNumberUnitWidth::UNUM_UNIT_WIDTH_ISO_CODE;
    case CurrencyDisplay::NAME:
      return UNumberUnitWidth::UNUM_UNIT_WIDTH_FULL_NAME;
    case CurrencyDisplay::NARROW_SYMBOL:
      return UNumberUnitWidth::UNUM_UNIT_WIDTH_NARROW;
  }
}

// ecma-402/#sec-currencydigits
// The currency is expected to an all upper case string value.
int CurrencyDigits(const icu::UnicodeString& currency) {
  UErrorCode status = U_ZERO_ERROR;
  uint32_t fraction_digits = ucurr_getDefaultFractionDigits(
      reinterpret_cast<const UChar*>(currency.getBuffer()), &status);
  // For missing currency codes, default to the most common, 2
  return U_SUCCESS(status) ? fraction_digits : 2;
}

bool IsAToZ(char ch) { return IsInRange(AsciiAlphaToLower(ch), 'a', 'z'); }

// ecma402/#sec-iswellformedcurrencycode
bool IsWellFormedCurrencyCode(const std::string& currency) {
  // Verifies that the input is a well-formed ISO 4217 currency code.
  // ecma402/#sec-currency-codes
  // 2. If the number of elements in normalized is not 3, return false.
  if (currency.length() != 3) return false;
  // 1. Let normalized be the result of mapping currency to upper case as
  //   described in 6.1.
  //
  // 3. If normalized contains any character that is not in
  // the range "A" to "Z" (U+0041 to U+005A), return false.
  //
  // 4. Return true.
  // Don't uppercase to test. It could convert invalid code into a valid one.
  // For example \u00DFP (Eszett+P) becomes SSP.
  return (IsAToZ(currency[0]) && IsAToZ(currency[1]) && IsAToZ(currency[2]));
}

// Parse the 'style' from the skeleton.
Handle<String> StyleString(Isolate* isolate,
                           const icu::UnicodeString& skeleton) {
  // Ex: skeleton as
  // "percent precision-integer rounding-mode-half-up scale/100"
  if (skeleton.indexOf("percent") >= 0) {
    return ReadOnlyRoots(isolate).percent_string_handle();
  }
  // Ex: skeleton as "currency/TWD .00 rounding-mode-half-up"
  if (skeleton.indexOf("currency") >= 0) {
    return ReadOnlyRoots(isolate).currency_string_handle();
  }
  // Ex: skeleton as
  // "measure-unit/length-meter .### rounding-mode-half-up unit-width-narrow"
  if (skeleton.indexOf("measure-unit") >= 0) {
    return ReadOnlyRoots(isolate).unit_string_handle();
  }
  // Ex: skeleton as ".### rounding-mode-half-up"
  return ReadOnlyRoots(isolate).decimal_string_handle();
}

// Parse the 'currencyDisplay' from the skeleton.
Handle<String> CurrencyDisplayString(Isolate* isolate,
                                     const icu::UnicodeString& skeleton) {
  // Ex: skeleton as
  // "currency/TWD .00 rounding-mode-half-up unit-width-iso-code"
  if (skeleton.indexOf("unit-width-iso-code") >= 0) {
    return ReadOnlyRoots(isolate).code_string_handle();
  }
  // Ex: skeleton as
  // "currency/TWD .00 rounding-mode-half-up unit-width-full-name;
  if (skeleton.indexOf("unit-width-full-name") >= 0) {
    return ReadOnlyRoots(isolate).name_string_handle();
  }
  // Ex: skeleton as
  // "currency/TWD .00 rounding-mode-half-up unit-width-narrow;
  if (skeleton.indexOf("unit-width-narrow") >= 0) {
    return ReadOnlyRoots(isolate).narrow_symbol_string_handle();
  }
  // Ex: skeleton as "currency/TWD .00 rounding-mode-half-up"
  return ReadOnlyRoots(isolate).symbol_string_handle();
}

// Return true if there are no "group-off" in the skeleton.
bool UseGroupingFromSkeleton(const icu::UnicodeString& skeleton) {
  return skeleton.indexOf("group-off") == -1;
}

// Parse currency code from skeleton. For example, skeleton as
// "currency/TWD .00 rounding-mode-half-up unit-width-full-name;
std::string CurrencyFromSkeleton(const icu::UnicodeString& skeleton) {
  std::string str;
  str = skeleton.toUTF8String<std::string>(str);
  std::string search("currency/");
  size_t index = str.find(search);
  if (index == str.npos) return "";
  return str.substr(index + search.size(), 3);
}

// Return the minimum integer digits by counting the number of '0' after
// "integer-width/+" in the skeleton.
// Ex: Return 15 for skeleton as
// “currency/TWD .00 rounding-mode-half-up integer-width/+000000000000000”
//                                                                 1
//                                                        123456789012345
// Return default value as 1 if there are no "integer-width/+".
int32_t MinimumIntegerDigitsFromSkeleton(const icu::UnicodeString& skeleton) {
  // count the number of 0 after "integer-width/+"
  icu::UnicodeString search("integer-width/+");
  int32_t index = skeleton.indexOf(search);
  if (index < 0) return 1;  // return 1 if cannot find it.
  index += search.length();
  int32_t matched = 0;
  while (index < skeleton.length() && skeleton[index] == '0') {
    matched++;
    index++;
  }
  CHECK_GT(matched, 0);
  return matched;
}

// Return true if there are fraction digits, false if not.
// The minimum fraction digits is the number of '0' after '.' in the skeleton
// The maximum fraction digits is the number of '#' after the above '0's plus
// the minimum fraction digits.
// For example, as skeleton “.000#### rounding-mode-half-up”
//                            123
//                               4567
// Set The minimum as 3 and maximum as 7.
bool FractionDigitsFromSkeleton(const icu::UnicodeString& skeleton,
                                int32_t* minimum, int32_t* maximum) {
  icu::UnicodeString search(".");
  int32_t index = skeleton.indexOf(search);
  if (index < 0) return false;
  *minimum = 0;
  index++;  // skip the '.'
  while (index < skeleton.length() && skeleton[index] == '0') {
    (*minimum)++;
    index++;
  }
  *maximum = *minimum;
  while (index < skeleton.length() && skeleton[index] == '#') {
    (*maximum)++;
    index++;
  }
  return true;
}

// Return true if there are significant digits, false if not.
// The minimum significant digits is the number of '@' in the skeleton
// The maximum significant digits is the number of '#' after these '@'s plus
// the minimum significant digits.
// Ex: Skeleton as "@@@@@####### rounding-mode-half-up"
//                  12345
//                       6789012
// Set The minimum as 5 and maximum as 12.
bool SignificantDigitsFromSkeleton(const icu::UnicodeString& skeleton,
                                   int32_t* minimum, int32_t* maximum) {
  icu::UnicodeString search("@");
  int32_t index = skeleton.indexOf(search);
  if (index < 0) return false;
  *minimum = 1;
  index++;  // skip the first '@'
  while (index < skeleton.length() && skeleton[index] == '@') {
    (*minimum)++;
    index++;
  }
  *maximum = *minimum;
  while (index < skeleton.length() && skeleton[index] == '#') {
    (*maximum)++;
    index++;
  }
  return true;
}

}  // anonymous namespace

// static
// ecma402 #sec-intl.numberformat.prototype.resolvedoptions
Handle<JSObject> JSNumberFormat::ResolvedOptions(
    Isolate* isolate, Handle<JSNumberFormat> number_format) {
  Factory* factory = isolate->factory();

  UErrorCode status = U_ZERO_ERROR;
  icu::number::LocalizedNumberFormatter* icu_number_formatter =
      number_format->icu_number_formatter()->raw();
  icu::UnicodeString skeleton = icu_number_formatter->toSkeleton(status);
  CHECK(U_SUCCESS(status));

  std::string s_str;
  s_str = skeleton.toUTF8String<std::string>(s_str);

  // 4. Let options be ! ObjectCreate(%ObjectPrototype%).
  Handle<JSObject> options = factory->NewJSObject(isolate->object_function());

  Handle<String> locale = Handle<String>(number_format->locale(), isolate);

  std::unique_ptr<char[]> locale_str = locale->ToCString();
  icu::Locale icu_locale = Intl::CreateICULocale(locale_str.get());

  std::string numbering_system = Intl::GetNumberingSystem(icu_locale);

  // 5. For each row of Table 4, except the header row, in table order, do
  // Table 4: Resolved Options of NumberFormat Instances
  //  Internal Slot                    Property
  //    [[Locale]]                      "locale"
  //    [[NumberingSystem]]             "numberingSystem"
  //    [[Style]]                       "style"
  //    [[Currency]]                    "currency"
  //    [[CurrencyDisplay]]             "currencyDisplay"
  //    [[MinimumIntegerDigits]]        "minimumIntegerDigits"
  //    [[MinimumFractionDigits]]       "minimumFractionDigits"
  //    [[MaximumFractionDigits]]       "maximumFractionDigits"
  //    [[MinimumSignificantDigits]]    "minimumSignificantDigits"
  //    [[MaximumSignificantDigits]]    "maximumSignificantDigits"
  //    [[UseGrouping]]                 "useGrouping"
  CHECK(JSReceiver::CreateDataProperty(isolate, options,
                                       factory->locale_string(), locale,
                                       Just(kDontThrow))
            .FromJust());
  if (!numbering_system.empty()) {
    CHECK(JSReceiver::CreateDataProperty(
              isolate, options, factory->numberingSystem_string(),
              factory->NewStringFromAsciiChecked(numbering_system.c_str()),
              Just(kDontThrow))
              .FromJust());
  }
  CHECK(JSReceiver::CreateDataProperty(
            isolate, options, factory->style_string(),
            StyleString(isolate, skeleton), Just(kDontThrow))
            .FromJust());
  std::string currency = CurrencyFromSkeleton(skeleton);
  if (!currency.empty()) {
    CHECK(JSReceiver::CreateDataProperty(
              isolate, options, factory->currency_string(),
              factory->NewStringFromAsciiChecked(currency.c_str()),
              Just(kDontThrow))
              .FromJust());

    CHECK(JSReceiver::CreateDataProperty(
              isolate, options, factory->currencyDisplay_string(),
              CurrencyDisplayString(isolate, skeleton), Just(kDontThrow))
              .FromJust());
  }
  CHECK(
      JSReceiver::CreateDataProperty(
          isolate, options, factory->minimumIntegerDigits_string(),
          factory->NewNumberFromInt(MinimumIntegerDigitsFromSkeleton(skeleton)),
          Just(kDontThrow))
          .FromJust());
  int32_t minimum = 0, maximum = 0;
  if (!FractionDigitsFromSkeleton(skeleton, &minimum, &maximum)) {
    // Currenct ECMA 402 spec mandate to record (Min|Max)imumFractionDigits
    // uncondictionally while the unified number proposal eventually will only
    // record either (Min|Max)imumFractionDigits or
    // (Min|Max)imumSignaficantDigits Since LocalizedNumberFormatter can only
    // remember one set, and during 2019-1-17 ECMA402 meeting that the committee
    // decide not to take a PR to address that prior to the unified number
    // proposal, we have to add these two 5 bits int into flags to remember the
    // (Min|Max)imumFractionDigits while (Min|Max)imumSignaficantDigits is
    // present.
    // TODO(ftang) remove the following two lines once we ship
    // int-number-format-unified
    minimum = number_format->minimum_fraction_digits();
    maximum = number_format->maximum_fraction_digits();
  }
  CHECK(JSReceiver::CreateDataProperty(
            isolate, options, factory->minimumFractionDigits_string(),
            factory->NewNumberFromInt(minimum), Just(kDontThrow))
            .FromJust());
  CHECK(JSReceiver::CreateDataProperty(
            isolate, options, factory->maximumFractionDigits_string(),
            factory->NewNumberFromInt(maximum), Just(kDontThrow))
            .FromJust());
  minimum = 0;
  maximum = 0;
  if (SignificantDigitsFromSkeleton(skeleton, &minimum, &maximum)) {
    CHECK(JSReceiver::CreateDataProperty(
              isolate, options, factory->minimumSignificantDigits_string(),
              factory->NewNumberFromInt(minimum), Just(kDontThrow))
              .FromJust());
    CHECK(JSReceiver::CreateDataProperty(
              isolate, options, factory->maximumSignificantDigits_string(),
              factory->NewNumberFromInt(maximum), Just(kDontThrow))
              .FromJust());
  }

  CHECK(JSReceiver::CreateDataProperty(
            isolate, options, factory->useGrouping_string(),
            factory->ToBoolean(UseGroupingFromSkeleton(skeleton)),
            Just(kDontThrow))
            .FromJust());
  return options;
}

// ecma402/#sec-unwrapnumberformat
MaybeHandle<JSNumberFormat> JSNumberFormat::UnwrapNumberFormat(
    Isolate* isolate, Handle<JSReceiver> format_holder) {
  // old code copy from NumberFormat::Unwrap that has no spec comment and
  // compiled but fail unit tests.
  Handle<Context> native_context =
      Handle<Context>(isolate->context()->native_context(), isolate);
  Handle<JSFunction> constructor = Handle<JSFunction>(
      JSFunction::cast(native_context->intl_number_format_function()), isolate);
  Handle<Object> object;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, object,
      Intl::LegacyUnwrapReceiver(isolate, format_holder, constructor,
                                 format_holder->IsJSNumberFormat()),
      JSNumberFormat);
  // 4. If ... or nf does not have an [[InitializedNumberFormat]] internal slot,
  // then
  if (!object->IsJSNumberFormat()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     "UnwrapNumberFormat")),
                    JSNumberFormat);
  }
  // 5. Return nf.
  return Handle<JSNumberFormat>::cast(object);
}

// static
MaybeHandle<JSNumberFormat> JSNumberFormat::Initialize(
    Isolate* isolate, Handle<JSNumberFormat> number_format,
    Handle<Object> locales, Handle<Object> options_obj) {
  number_format->set_flags(0);
  // set the flags to 0 ASAP.
  Factory* factory = isolate->factory();

  // 1. Let requestedLocales be ? CanonicalizeLocaleList(locales).
  Maybe<std::vector<std::string>> maybe_requested_locales =
      Intl::CanonicalizeLocaleList(isolate, locales);
  MAYBE_RETURN(maybe_requested_locales, Handle<JSNumberFormat>());
  std::vector<std::string> requested_locales =
      maybe_requested_locales.FromJust();

  // 2. If options is undefined, then
  if (options_obj->IsUndefined(isolate)) {
    // 2. a. Let options be ObjectCreate(null).
    options_obj = isolate->factory()->NewJSObjectWithNullProto();
  } else {
    // 3. Else
    // 3. a. Let options be ? ToObject(options).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, options_obj,
        Object::ToObject(isolate, options_obj, "Intl.NumberFormat"),
        JSNumberFormat);
  }

  // At this point, options_obj can either be a JSObject or a JSProxy only.
  Handle<JSReceiver> options = Handle<JSReceiver>::cast(options_obj);

  // 4. Let opt be a new Record.
  // 5. Let matcher be ? GetOption(options, "localeMatcher", "string", «
  // "lookup", "best fit" », "best fit").
  // 6. Set opt.[[localeMatcher]] to matcher.
  Maybe<Intl::MatcherOption> maybe_locale_matcher =
      Intl::GetLocaleMatcher(isolate, options, "Intl.NumberFormat");
  MAYBE_RETURN(maybe_locale_matcher, MaybeHandle<JSNumberFormat>());
  Intl::MatcherOption matcher = maybe_locale_matcher.FromJust();

  std::unique_ptr<char[]> numbering_system_str = nullptr;
  if (FLAG_harmony_intl_add_calendar_numbering_system) {
    // 7. Let _numberingSystem_ be ? GetOption(_options_, `"numberingSystem"`,
    //    `"string"`, *undefined*, *undefined*).
    Maybe<bool> maybe_numberingSystem = Intl::GetNumberingSystem(
        isolate, options, "Intl.RelativeTimeFormat", &numbering_system_str);
    // 8. If _numberingSystem_ is not *undefined*, then
    // a. If _numberingSystem_ does not match the
    //    `(3*8alphanum) *("-" (3*8alphanum))` sequence, throw a *RangeError*
    //     exception.
    MAYBE_RETURN(maybe_numberingSystem, MaybeHandle<JSNumberFormat>());
  }

  // 7. Let localeData be %NumberFormat%.[[LocaleData]].
  // 8. Let r be ResolveLocale(%NumberFormat%.[[AvailableLocales]],
  // requestedLocales, opt,  %NumberFormat%.[[RelevantExtensionKeys]],
  // localeData).
  std::set<std::string> relevant_extension_keys{"nu"};
  Intl::ResolvedLocale r =
      Intl::ResolveLocale(isolate, JSNumberFormat::GetAvailableLocales(),
                          requested_locales, matcher, relevant_extension_keys);

  UErrorCode status = U_ZERO_ERROR;
  if (numbering_system_str != nullptr) {
    r.icu_locale.setUnicodeKeywordValue("nu", numbering_system_str.get(),
                                        status);
    CHECK(U_SUCCESS(status));
    r.locale = Intl::ToLanguageTag(r.icu_locale).FromJust();
  }

  // 9. Set numberFormat.[[Locale]] to r.[[locale]].
  Handle<String> locale_str =
      isolate->factory()->NewStringFromAsciiChecked(r.locale.c_str());
  number_format->set_locale(*locale_str);

  // 11. Let dataLocale be r.[[dataLocale]].
  //
  // 12. Let style be ? GetOption(options, "style", "string",  « "decimal",
  // "percent", "currency" », "decimal").
  const char* service = "Intl.NumberFormat";
  Maybe<Style> maybe_style = Intl::GetStringOption<Style>(
      isolate, options, "style", service, {"decimal", "percent", "currency"},
      {Style::DECIMAL, Style::PERCENT, Style::CURRENCY}, Style::DECIMAL);
  MAYBE_RETURN(maybe_style, MaybeHandle<JSNumberFormat>());
  Style style = maybe_style.FromJust();

  // 13. Set numberFormat.[[Style]] to style.

  // 14. Let currency be ? GetOption(options, "currency", "string", undefined,
  // undefined).
  std::unique_ptr<char[]> currency_cstr;
  const std::vector<const char*> empty_values = {};
  Maybe<bool> found_currency = Intl::GetStringOption(
      isolate, options, "currency", empty_values, service, &currency_cstr);
  MAYBE_RETURN(found_currency, MaybeHandle<JSNumberFormat>());

  std::string currency;
  // 15. If currency is not undefined, then
  if (found_currency.FromJust()) {
    DCHECK_NOT_NULL(currency_cstr.get());
    currency = currency_cstr.get();
    // 15. a. If the result of IsWellFormedCurrencyCode(currency) is false,
    // throw a RangeError exception.
    if (!IsWellFormedCurrencyCode(currency)) {
      THROW_NEW_ERROR(
          isolate,
          NewRangeError(MessageTemplate::kInvalid,
                        factory->NewStringFromStaticChars("currency code"),
                        factory->NewStringFromAsciiChecked(currency.c_str())),
          JSNumberFormat);
    }
  }

  // 16. If style is "currency" and currency is undefined, throw a TypeError
  // exception.
  if (style == Style::CURRENCY && !found_currency.FromJust()) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kCurrencyCode),
                    JSNumberFormat);
  }
  // 17. If style is "currency", then
  int c_digits = 0;
  icu::UnicodeString currency_ustr;
  if (style == Style::CURRENCY) {
    // a. Let currency be the result of converting currency to upper case as
    //    specified in 6.1
    std::transform(currency.begin(), currency.end(), currency.begin(), toupper);
    // c. Let cDigits be CurrencyDigits(currency).
    currency_ustr = currency.c_str();
    c_digits = CurrencyDigits(currency_ustr);
  }

  // 18. Let currencyDisplay be ? GetOption(options, "currencyDisplay",
  // "string", « "code",  "symbol", "name" », "symbol").
  Maybe<CurrencyDisplay> maybe_currencyDisplay =
      Intl::GetStringOption<CurrencyDisplay>(
          isolate, options, "currencyDisplay", service,
          {"code", "symbol", "name"},
          {CurrencyDisplay::CODE, CurrencyDisplay::SYMBOL,
           CurrencyDisplay::NAME},
          CurrencyDisplay::SYMBOL);
  MAYBE_RETURN(maybe_currencyDisplay, MaybeHandle<JSNumberFormat>());
  CurrencyDisplay currency_display = maybe_currencyDisplay.FromJust();

  icu::number::LocalizedNumberFormatter icu_number_formatter =
      icu::number::NumberFormatter::withLocale(r.icu_locale)
          .roundingMode(UNUM_ROUND_HALFUP);
  if (style == Style::PERCENT) {
    icu_number_formatter = icu_number_formatter.unit(icu::NoUnit::percent())
                               .scale(icu::number::Scale::powerOfTen(2));
  }

  if (style == Style::CURRENCY) {
    // 19. If style is "currency", set  numberFormat.[[CurrencyDisplay]] to
    // currencyDisplay.

    // 17.b. Set numberFormat.[[Currency]] to currency.
    if (!currency_ustr.isEmpty()) {
      Handle<String> currency_string;
      ASSIGN_RETURN_ON_EXCEPTION(isolate, currency_string,
                                 Intl::ToString(isolate, currency_ustr),
                                 JSNumberFormat);

      icu_number_formatter = icu_number_formatter.unit(
          icu::CurrencyUnit(currency_ustr.getBuffer(), status));
      CHECK(U_SUCCESS(status));
      // The default unitWidth is SHORT in ICU and that mapped from
      // Symbol so we can skip the setting for optimization.
      if (currency_display != CurrencyDisplay::SYMBOL) {
        icu_number_formatter = icu_number_formatter.unitWidth(
            ToUNumberUnitWidth(currency_display));
      }
      CHECK(U_SUCCESS(status));
    }
  }

  // 20. If style is "currency", then
  int mnfd_default, mxfd_default;
  if (style == Style::CURRENCY) {
    //  a. Let mnfdDefault be cDigits.
    //  b. Let mxfdDefault be cDigits.
    mnfd_default = c_digits;
    mxfd_default = c_digits;
  } else {
    // 21. Else,
    // a. Let mnfdDefault be 0.
    mnfd_default = 0;
    // b. If style is "percent", then
    if (style == Style::PERCENT) {
      // i. Let mxfdDefault be 0.
      mxfd_default = 0;
    } else {
      // c. Else,
      // i. Let mxfdDefault be 3.
      mxfd_default = 3;
    }
  }
  // 22. Perform ? SetNumberFormatDigitOptions(numberFormat, options,
  // mnfdDefault, mxfdDefault).
  Maybe<Intl::NumberFormatDigitOptions> maybe_digit_options =
      Intl::SetNumberFormatDigitOptions(isolate, options, mnfd_default,
                                        mxfd_default);
  MAYBE_RETURN(maybe_digit_options, Handle<JSNumberFormat>());
  Intl::NumberFormatDigitOptions digit_options = maybe_digit_options.FromJust();

  icu::number::Precision precision =
      (digit_options.minimum_significant_digits > 0)
          ? icu::number::Precision::minMaxSignificantDigits(
                digit_options.minimum_significant_digits,
                digit_options.maximum_significant_digits)
          : icu::number::Precision::minMaxFraction(
                digit_options.minimum_fraction_digits,
                digit_options.maximum_fraction_digits);

  if (digit_options.minimum_significant_digits > 0) {
    // Currenct ECMA 402 spec mandate to record (Min|Max)imumFractionDigits
    // uncondictionally while the unified number proposal eventually will only
    // record either (Min|Max)imumFractionDigits or
    // (Min|Max)imumSignaficantDigits Since LocalizedNumberFormatter can only
    // remember one set, and during 2019-1-17 ECMA402 meeting that the committee
    // decide not to take a PR to address that prior to the unified number
    // proposal, we have to add these two 5 bits int into flags to remember the
    // (Min|Max)imumFractionDigits while (Min|Max)imumSignaficantDigits is
    // present.
    // TODO(ftang) remove the following two lines once we ship
    // int-number-format-unified
    number_format->set_minimum_fraction_digits(
        digit_options.minimum_fraction_digits);
    number_format->set_maximum_fraction_digits(
        digit_options.maximum_fraction_digits);
  }

  icu_number_formatter = icu_number_formatter.precision(precision);
  if (digit_options.minimum_integer_digits > 1) {
    icu_number_formatter =
        icu_number_formatter.integerWidth(icu::number::IntegerWidth::zeroFillTo(
            digit_options.minimum_integer_digits));
  }

  // 23. Let useGrouping be ? GetOption(options, "useGrouping", "boolean",
  // undefined, true).
  bool use_grouping = true;
  Maybe<bool> found_use_grouping = Intl::GetBoolOption(
      isolate, options, "useGrouping", service, &use_grouping);
  MAYBE_RETURN(found_use_grouping, MaybeHandle<JSNumberFormat>());
  // 24. Set numberFormat.[[UseGrouping]] to useGrouping.
  if (!use_grouping) {
    icu_number_formatter = icu_number_formatter.grouping(
        UNumberGroupingStrategy::UNUM_GROUPING_OFF);
  }

  // 25. Let dataLocaleData be localeData.[[<dataLocale>]].
  //
  // 26. Let patterns be dataLocaleData.[[patterns]].
  //
  // 27. Assert: patterns is a record (see 11.3.3).
  //
  // 28. Let stylePatterns be patterns.[[<style>]].
  //
  // 29. Set numberFormat.[[PositivePattern]] to
  // stylePatterns.[[positivePattern]].
  //
  // 30. Set numberFormat.[[NegativePattern]] to
  // stylePatterns.[[negativePattern]].
  //
  Handle<Managed<icu::number::LocalizedNumberFormatter>>
      managed_number_formatter =
          Managed<icu::number::LocalizedNumberFormatter>::FromRawPtr(
              isolate, 0,
              new icu::number::LocalizedNumberFormatter(icu_number_formatter));
  number_format->set_icu_number_formatter(*managed_number_formatter);
  number_format->set_bound_format(*factory->undefined_value());

  // 31. Return numberFormat.
  return number_format;
}

namespace {
Maybe<icu::UnicodeString> IcuFormatNumber(
    Isolate* isolate,
    const icu::number::LocalizedNumberFormatter& number_format,
    Handle<Object> numeric_obj, icu::FieldPositionIterator* fp_iter) {
  // If it is BigInt, handle it differently.
  UErrorCode status = U_ZERO_ERROR;
  icu::number::FormattedNumber formatted;
  if (numeric_obj->IsBigInt()) {
    Handle<BigInt> big_int = Handle<BigInt>::cast(numeric_obj);
    Handle<String> big_int_string;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, big_int_string,
                                     BigInt::ToString(isolate, big_int),
                                     Nothing<icu::UnicodeString>());
    formatted = number_format.formatDecimal(
        {big_int_string->ToCString().get(), big_int_string->length()}, status);
  } else {
    double number = numeric_obj->Number();
    formatted = number_format.formatDouble(number, status);
  }
  formatted.getAllFieldPositions(*fp_iter, status);
  icu::UnicodeString result = formatted.toString(status);
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NewTypeError(MessageTemplate::kIcuError),
                                 Nothing<icu::UnicodeString>());
  }
  return Just(result);
}

}  // namespace

MaybeHandle<String> JSNumberFormat::FormatNumeric(
    Isolate* isolate,
    const icu::number::LocalizedNumberFormatter& number_format,
    Handle<Object> numeric_obj) {
  DCHECK(numeric_obj->IsNumeric());

  Maybe<icu::UnicodeString> maybe_format =
      IcuFormatNumber(isolate, number_format, numeric_obj, nullptr);
  MAYBE_RETURN(maybe_format, Handle<String>());
  return Intl::ToString(isolate, maybe_format.FromJust());
}

namespace {

bool cmp_NumberFormatSpan(const NumberFormatSpan& a,
                          const NumberFormatSpan& b) {
  // Regions that start earlier should be encountered earlier.
  if (a.begin_pos < b.begin_pos) return true;
  if (a.begin_pos > b.begin_pos) return false;
  // For regions that start in the same place, regions that last longer should
  // be encountered earlier.
  if (a.end_pos < b.end_pos) return false;
  if (a.end_pos > b.end_pos) return true;
  // For regions that are exactly the same, one of them must be the "literal"
  // backdrop we added, which has a field_id of -1, so consider higher field_ids
  // to be later.
  return a.field_id < b.field_id;
}

}  // namespace

// Flattens a list of possibly-overlapping "regions" to a list of
// non-overlapping "parts". At least one of the input regions must span the
// entire space of possible indexes. The regions parameter will sorted in-place
// according to some criteria; this is done for performance to avoid copying the
// input.
std::vector<NumberFormatSpan> FlattenRegionsToParts(
    std::vector<NumberFormatSpan>* regions) {
  // The intention of this algorithm is that it's used to translate ICU "fields"
  // to JavaScript "parts" of a formatted string. Each ICU field and JavaScript
  // part has an integer field_id, which corresponds to something like "grouping
  // separator", "fraction", or "percent sign", and has a begin and end
  // position. Here's a diagram of:

  // var nf = new Intl.NumberFormat(['de'], {style:'currency',currency:'EUR'});
  // nf.formatToParts(123456.78);

  //               :       6
  //  input regions:    0000000211 7
  // ('-' means -1):    ------------
  // formatted string: "123.456,78 €"
  // output parts:      0006000211-7

  // To illustrate the requirements of this algorithm, here's a contrived and
  // convoluted example of inputs and expected outputs:

  //              :          4
  //              :      22 33    3
  //              :      11111   22
  // input regions:     0000000  111
  //              :     ------------
  // formatted string: "abcdefghijkl"
  // output parts:      0221340--231
  // (The characters in the formatted string are irrelevant to this function.)

  // We arrange the overlapping input regions like a mountain range where
  // smaller regions are "on top" of larger regions, and we output a birds-eye
  // view of the mountains, so that smaller regions take priority over larger
  // regions.
  std::sort(regions->begin(), regions->end(), cmp_NumberFormatSpan);
  std::vector<size_t> overlapping_region_index_stack;
  // At least one item in regions must be a region spanning the entire string.
  // Due to the sorting above, the first item in the vector will be one of them.
  overlapping_region_index_stack.push_back(0);
  NumberFormatSpan top_region = regions->at(0);
  size_t region_iterator = 1;
  int32_t entire_size = top_region.end_pos;

  std::vector<NumberFormatSpan> out_parts;

  // The "climber" is a cursor that advances from left to right climbing "up"
  // and "down" the mountains. Whenever the climber moves to the right, that
  // represents an item of output.
  int32_t climber = 0;
  while (climber < entire_size) {
    int32_t next_region_begin_pos;
    if (region_iterator < regions->size()) {
      next_region_begin_pos = regions->at(region_iterator).begin_pos;
    } else {
      // finish off the rest of the input by proceeding to the end.
      next_region_begin_pos = entire_size;
    }

    if (climber < next_region_begin_pos) {
      while (top_region.end_pos < next_region_begin_pos) {
        if (climber < top_region.end_pos) {
          // step down
          out_parts.push_back(NumberFormatSpan(top_region.field_id, climber,
                                               top_region.end_pos));
          climber = top_region.end_pos;
        } else {
          // drop down
        }
        overlapping_region_index_stack.pop_back();
        top_region = regions->at(overlapping_region_index_stack.back());
      }
      if (climber < next_region_begin_pos) {
        // cross a plateau/mesa/valley
        out_parts.push_back(NumberFormatSpan(top_region.field_id, climber,
                                             next_region_begin_pos));
        climber = next_region_begin_pos;
      }
    }
    if (region_iterator < regions->size()) {
      overlapping_region_index_stack.push_back(region_iterator++);
      top_region = regions->at(overlapping_region_index_stack.back());
    }
  }
  return out_parts;
}

namespace {
Maybe<int> ConstructParts(Isolate* isolate, const icu::UnicodeString& formatted,
                          icu::FieldPositionIterator* fp_iter,
                          Handle<JSArray> result, int start_index,
                          Handle<Object> numeric_obj, Handle<String> unit) {
  DCHECK(numeric_obj->IsNumeric());
  int32_t length = formatted.length();
  int index = start_index;
  if (length == 0) return Just(index);

  std::vector<NumberFormatSpan> regions;
  // Add a "literal" backdrop for the entire string. This will be used if no
  // other region covers some part of the formatted string. It's possible
  // there's another field with exactly the same begin and end as this backdrop,
  // in which case the backdrop's field_id of -1 will give it lower priority.
  regions.push_back(NumberFormatSpan(-1, 0, formatted.length()));

  {
    icu::FieldPosition fp;
    while (fp_iter->next(fp)) {
      regions.push_back(NumberFormatSpan(fp.getField(), fp.getBeginIndex(),
                                         fp.getEndIndex()));
    }
  }

  std::vector<NumberFormatSpan> parts = FlattenRegionsToParts(&regions);

  for (auto it = parts.begin(); it < parts.end(); it++) {
    NumberFormatSpan part = *it;
    Handle<String> field_type_string =
        part.field_id == -1
            ? isolate->factory()->literal_string()
            : Intl::NumberFieldToType(isolate, numeric_obj, part.field_id);
    Handle<String> substring;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, substring,
        Intl::ToString(isolate, formatted, part.begin_pos, part.end_pos),
        Nothing<int>());
    if (unit.is_null()) {
      Intl::AddElement(isolate, result, index, field_type_string, substring);
    } else {
      Intl::AddElement(isolate, result, index, field_type_string, substring,
                       isolate->factory()->unit_string(), unit);
    }
    ++index;
  }
  JSObject::ValidateElements(*result);
  return Just(index);
}

}  // namespace

MaybeHandle<JSArray> JSNumberFormat::FormatToParts(
    Isolate* isolate, Handle<JSNumberFormat> number_format,
    Handle<Object> numeric_obj) {
  CHECK(numeric_obj->IsNumeric());
  Factory* factory = isolate->factory();
  icu::number::LocalizedNumberFormatter* fmt =
      number_format->icu_number_formatter()->raw();
  CHECK_NOT_NULL(fmt);

  icu::FieldPositionIterator fp_iter;
  Maybe<icu::UnicodeString> maybe_format =
      IcuFormatNumber(isolate, *fmt, numeric_obj, &fp_iter);
  MAYBE_RETURN(maybe_format, Handle<JSArray>());

  Handle<JSArray> result = factory->NewJSArray(0);
  Maybe<int> maybe_format_to_parts =
      ConstructParts(isolate, maybe_format.FromJust(), &fp_iter, result, 0,
                     numeric_obj, Handle<String>());
  MAYBE_RETURN(maybe_format_to_parts, Handle<JSArray>());

  return result;
}

const std::set<std::string>& JSNumberFormat::GetAvailableLocales() {
  static base::LazyInstance<Intl::AvailableLocales<icu::NumberFormat>>::type
      available_locales = LAZY_INSTANCE_INITIALIZER;
  return available_locales.Pointer()->Get();
}

}  // namespace internal
}  // namespace v8
