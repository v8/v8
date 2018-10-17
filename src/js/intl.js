// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ECMAScript 402 API implementation.

/**
 * Intl object is a single object that has some named properties,
 * all of which are constructors.
 */
(function(global, utils) {

"use strict";

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

var ArrayPush;
var GlobalIntl = global.Intl;
var GlobalIntlDateTimeFormat = GlobalIntl.DateTimeFormat;
var GlobalIntlNumberFormat = GlobalIntl.NumberFormat;
var GlobalIntlCollator = GlobalIntl.Collator;
var GlobalIntlPluralRules = GlobalIntl.PluralRules;
var GlobalIntlv8BreakIterator = GlobalIntl.v8BreakIterator;
var InternalArray = utils.InternalArray;

utils.Import(function(from) {
  ArrayPush = from.ArrayPush;
});

// -------------------------------------------------------------------

/**
 * Caches available locales for each service.
 */
var AVAILABLE_LOCALES = {
   __proto__ : null,
  'collator': UNDEFINED,
  'numberformat': UNDEFINED,
  'dateformat': UNDEFINED,
  'breakiterator': UNDEFINED,
  'pluralrules': UNDEFINED,
  'relativetimeformat': UNDEFINED,
  'listformat': UNDEFINED,
  'segmenter': UNDEFINED,
};

/* Make JS array[] out of InternalArray */
function makeArray(input) {
  var array = [];
  %MoveArrayContents(input, array);
  return array;
}

/**
 * Returns an InternalArray where all locales are canonicalized and duplicates
 * removed.
 * Throws on locales that are not well formed BCP47 tags.
 * ECMA 402 8.2.1 steps 1 (ECMA 402 9.2.1) and 2.
 */
function canonicalizeLocaleList(locales) {
  var seen = new InternalArray();
  if (!IS_UNDEFINED(locales)) {
    // We allow single string localeID.
    if (typeof locales === 'string') {
      %_Call(ArrayPush, seen, %CanonicalizeLanguageTag(locales));
      return seen;
    }

    var o = TO_OBJECT(locales);
    var len = TO_LENGTH(o.length);

    for (var k = 0; k < len; k++) {
      if (k in o) {
        var value = o[k];

        var tag = %CanonicalizeLanguageTag(value);

        if (%ArrayIndexOf(seen, tag, 0) === -1) {
          %_Call(ArrayPush, seen, tag);
        }
      }
    }
  }

  return seen;
}

// ECMA 402 section 8.2.1
DEFINE_METHOD(
  GlobalIntl,
  getCanonicalLocales(locales) {
    return makeArray(canonicalizeLocaleList(locales));
  }
);

// Save references to Intl objects and methods we use, for added security.
var savedObjects = {
  __proto__: null,
  'collator': GlobalIntlCollator,
  'numberformat': GlobalIntlNumberFormat,
  'dateformatall': GlobalIntlDateTimeFormat,
  'dateformatdate': GlobalIntlDateTimeFormat,
  'dateformattime': GlobalIntlDateTimeFormat
};


// Default (created with undefined locales and options parameters) collator,
// number and date format instances. They'll be created as needed.
var defaultObjects = {
  __proto__: null,
  'collator': UNDEFINED,
  'numberformat': UNDEFINED,
  'dateformatall': UNDEFINED,
  'dateformatdate': UNDEFINED,
  'dateformattime': UNDEFINED,
};

function clearDefaultObjects() {
  defaultObjects['dateformatall'] = UNDEFINED;
  defaultObjects['dateformatdate'] = UNDEFINED;
  defaultObjects['dateformattime'] = UNDEFINED;
}

var date_cache_version = 0;

function checkDateCacheCurrent() {
  var new_date_cache_version = %DateCacheVersion();
  if (new_date_cache_version == date_cache_version) {
    return;
  }
  date_cache_version = new_date_cache_version;

  clearDefaultObjects();
}

/**
 * Returns cached or newly created instance of a given service.
 * We cache only default instances (where no locales or options are provided).
 */
function cachedOrNewService(service, locales, options, defaults) {
  var useOptions = (IS_UNDEFINED(defaults)) ? options : defaults;
  if (IS_UNDEFINED(locales) && IS_UNDEFINED(options)) {
    checkDateCacheCurrent();
    if (IS_UNDEFINED(defaultObjects[service])) {
      defaultObjects[service] = new savedObjects[service](locales, useOptions);
    }
    return defaultObjects[service];
  }
  return new savedObjects[service](locales, useOptions);
}

// TODO(ftang) remove the %InstallToContext once
// cachedOrNewService is available in C++
%InstallToContext([
  "cached_or_new_service", cachedOrNewService
]);

})
