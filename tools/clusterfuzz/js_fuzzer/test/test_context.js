// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Tests for analyzing the mutation context.
 */

'use strict';

const babelTraverse = require('@babel/traverse').default;

const common = require('../mutators/common.js');
const helpers = require('./helpers.js');
const sourceHelpers = require('../source_helpers.js');

const { analyzeContext } = require('../script_mutator.js');
const { IdentifierNormalizer } = require('../mutators/normalizer.js');

describe('Mutation context', () => {
  it('determines loop variables', () => {
    const source = helpers.loadTestData('loop_variables.js');
    const normalizer = new IdentifierNormalizer();
    normalizer.mutate(source);
    const context = analyzeContext(source);

    const code = sourceHelpers.generateCode(source, []);
    helpers.assertExpectedResult(
        'loop_variables_source_expected.js', code);
    helpers.assertExpectedResult(
        'loop_variables_expected.js',
        JSON.stringify(Array.from(context.loopVariables), null, 2));
  });
});
