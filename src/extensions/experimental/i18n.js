// Copyright 2006-2011 the V8 project authors. All rights reserved.
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

// TODO(cira): Rename v8Locale into LocaleInfo once we have stable API.
/**
 * LocaleInfo class is an aggregate class of all i18n API calls.
 * @param {Object} settings - localeID and regionID to create LocaleInfo from.
 *   {Array.<string>|string} settings.localeID -
 *     Unicode identifier of the locale.
 *     See http://unicode.org/reports/tr35/#BCP_47_Conformance
 *   {string} settings.regionID - ISO3166 region ID with addition of
 *     invalid, undefined and reserved region codes.
 * @constructor
 */
v8Locale = function(settings) {
  native function NativeJSLocale();

  // Assume user wanted to do v8Locale("sr");
  if (typeof(settings) === "string") {
    settings = {'localeID': settings};
  }

  var properties = NativeJSLocale(
      v8Locale.createSettingsOrDefault_(settings, {'localeID': 'root'}));

  // Keep the resolved ICU locale ID around to avoid resolving localeID to
  // ICU locale ID every time BreakIterator, Collator and so forth are called.
  this.__icuLocaleID__ = properties.icuLocaleID;
  this.options = {'localeID': properties.localeID,
                  'regionID': properties.regionID};
};

/**
 * Clones existing locale with possible overrides for some of the options.
 * @param {!Object} settings - overrides for current locale settings.
 * @returns {Object} - new LocaleInfo object.
 */
v8Locale.prototype.derive = function(settings) {
  return new v8Locale(
      v8Locale.createSettingsOrDefault_(settings, this.options));
};

/**
 * v8BreakIterator class implements locale aware segmenatation.
 * It is not part of EcmaScript proposal.
 * @param {Object} locale - locale object to pass to break
 *   iterator implementation.
 * @param {string} type - type of segmenatation:
 *   - character
 *   - word
 *   - sentence
 *   - line
 * @constructor
 */
v8Locale.v8BreakIterator = function(locale, type) {
  native function NativeJSBreakIterator();

  locale = v8Locale.createLocaleOrDefault_(locale);
  // BCP47 ID would work in this case, but we use ICU locale for consistency.
  var iterator = NativeJSBreakIterator(locale.__icuLocaleID__, type);
  iterator.type = type;
  return iterator;
};

/**
 * Type of the break we encountered during previous iteration.
 * @type{Enum}
 */
v8Locale.v8BreakIterator.BreakType = {
  'unknown': -1,
  'none': 0,
  'number': 100,
  'word': 200,
  'kana': 300,
  'ideo': 400
};

/**
 * Creates new v8BreakIterator based on current locale.
 * @param {string} - type of segmentation. See constructor.
 * @returns {Object} - new v8BreakIterator object.
 */
v8Locale.prototype.v8CreateBreakIterator = function(type) {
  return new v8Locale.v8BreakIterator(this, type);
};

// TODO(jungshik): Set |collator.options| to actually recognized / resolved
// values.
/**
 * Collator class implements locale-aware sort.
 * @param {Object} locale - locale object to pass to collator implementation.
 * @param {Object} settings - collation flags:
 *   - ignoreCase
 *   - ignoreAccents
 *   - numeric
 * @constructor
 */
v8Locale.Collator = function(locale, settings) {
  native function NativeJSCollator();

  locale = v8Locale.createLocaleOrDefault_(locale);
  var collator = NativeJSCollator(
      locale.__icuLocaleID__, v8Locale.createSettingsOrDefault_(settings, {}));
  return collator;
};

/**
 * Creates new Collator based on current locale.
 * @param {Object} - collation flags. See constructor.
 * @returns {Object} - new v8BreakIterator object.
 */
v8Locale.prototype.createCollator = function(settings) {
  return new v8Locale.Collator(this, settings);
};

/**
 * Merges user settings and defaults.
 * Settings that are not of object type are rejected.
 * Actual property values are not validated, but whitespace is trimmed if they
 * are strings.
 * @param {!Object} settings - user provided settings.
 * @param {!Object} defaults - default values for this type of settings.
 * @returns {Object} - valid settings object.
 */
v8Locale.createSettingsOrDefault_ = function(settings, defaults) {
  if (!settings || typeof(settings) !== 'object' ) {
    return defaults;
  }
  for (var key in defaults) {
    if (!settings.hasOwnProperty(key)) {
      settings[key] = defaults[key];
    }
  }
  // Clean up values, like trimming whitespace.
  for (var key in settings) {
    if (typeof(settings[key]) === "string") {
      settings[key] = settings[key].trim();
    }
  }

  return settings;
};

/**
 * If locale is valid (defined and of v8Locale type) we return it. If not
 * we create default locale and return it.
 * @param {!Object} locale - user provided locale.
 * @returns {Object} - v8Locale object.
 */
v8Locale.createLocaleOrDefault_ = function(locale) {
  if (!locale || !(locale instanceof v8Locale)) {
    return new v8Locale();
  } else {
    return locale;
  }
};
