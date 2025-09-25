// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Test string-unicode mutator.
 */

'use strict';

const sinon = require('sinon');

const helpers = require('./helpers.js');
const scriptMutator = require('../script_mutator.js');
const stringUnicode = require('../mutators/string_unicode_mutator.js');
const random = require('../random.js');
const sourceHelpers = require('../source_helpers.js');

const sandbox = sinon.createSandbox();



describe('String Unicode mutator', () => {

  afterEach(() => {
    sandbox.restore();
  });

  it('mutates all eligible string characters with unicode escapes', () => {
    sandbox.restore();

    const source = helpers.loadTestData('mutate_string.js');

    const settings = scriptMutator.defaultSettings();
    settings.MUTATE_UNICODE_ESCAPE_PROB = 1.0;
    settings.ENABLE_UNICODE_ESCAPE_MUTATOR = 1.0;

    const mutator = new stringUnicode.StringUnicodeMutator(settings);
    mutator.mutate(source);

    const mutated = sourceHelpers.generateCode(source);

    helpers.assertExpectedResult('mutate_string_expected.js', mutated, true);
  });

  it('mutates some characters with pseudo-random behavior', () => {
    sandbox.restore();
    helpers.deterministicRandom(sandbox);

    const source = helpers.loadTestData('mutate_string.js');

    const settings = scriptMutator.defaultSettings();
    settings.MUTATE_UNICODE_ESCAPE_PROB = 0.2;
    settings.ENABLE_UNICODE_ESCAPE_MUTATOR = 1.0;

    const mutator = new stringUnicode.StringUnicodeMutator(settings);
    mutator.mutate(source);

    const mutated = sourceHelpers.generateCode(source);

    helpers.assertExpectedResult('mutate_string_mixed_expected.js', mutated, true);
  });

  it('does not mutate string when globally disabled', () => {
    sandbox.restore();

    const source = helpers.loadTestData('mutate_string.js');

    const settings = scriptMutator.defaultSettings();
    settings.MUTATE_UNICODE_ESCAPE_PROB = 0.0;
    settings.ENABLE_UNICODE_ESCAPE_MUTATOR = 1.0;

    const mutator = new stringUnicode.StringUnicodeMutator(settings);
    mutator.mutate(source);

    const mutated = sourceHelpers.generateCode(source);

    helpers.assertExpectedResult('mutate_string_no_change_expected.js', mutated, false);
  });
});
