// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Regression tests.
 */

'use strict';

const assert = require('assert');
const { execSync } = require("child_process");
const fs = require('fs');
const sinon = require('sinon');
const tempfile = require('tempfile');
const tempy = require('tempy');

const db = require('../db.js');
const exceptions = require('../exceptions.js');
const helpers = require('./helpers.js');
const random = require('../random.js');
const scriptMutator = require('../script_mutator.js');
const sourceHelpers = require('../source_helpers.js');
const functionCallMutator = require('../mutators/function_call_mutator.js');

const {CrossOverMutator} = require('../mutators/crossover_mutator.js');

const sandbox = sinon.createSandbox();

const SYNTAX_ERROR_RE = /.*SyntaxError.*/

function createFuzzTest(fake_db, settings, inputFiles) {
  const sources = inputFiles.map(input => helpers.loadV8TestData(input));

  const mutator = new scriptMutator.ScriptMutator(settings, fake_db);
  const result = mutator.mutateMultiple(sources);

  const output_file = tempfile('.js');
  fs.writeFileSync(output_file, result.code);
  return { file:output_file, flags:result.flags };
}

function execFile(jsFile) {
  execSync("node " + jsFile, {stdio: ['pipe']});
}

describe('Regression tests', () => {
  beforeEach(() => {
    helpers.deterministicRandom(sandbox);

    this.settings = {
      ADD_VAR_OR_OBJ_MUTATIONS: 0.0,
      MUTATE_CROSSOVER_INSERT: 0.0,
      MUTATE_EXPRESSIONS: 0.0,
      MUTATE_FUNCTION_CALLS: 0.0,
      MUTATE_NUMBERS: 0.0,
      MUTATE_VARIABLES: 0.0,
      engine: 'v8',
      testing: true,
    }
  });

  afterEach(() => {
    sandbox.restore();
  });

  it('combine strict and with', () => {
    // Test that when a file with "use strict" is used in the inputs,
    // the result is only strict if no other file contains anything
    // prohibited in strict mode (here a with statement).
    // It is assumed that such input files are marked as sloppy in the
    // auto generated exceptions.
    sandbox.stub(exceptions, 'getGeneratedSloppy').callsFake(
        () => { return new Set(['regress/strict/input_with.js']); });
    const {file, flags} = createFuzzTest(
        'test_data/regress/empty_db',
        this.settings,
        ['regress/strict/input_strict.js', 'regress/strict/input_with.js']);
    execFile(file);
  });

  it('combine strict and with, life analysis', () => {
    // As above, but without the sloppy file being marked. "Strict"
    // incompatibility will also be detected at parse time.
    const {file, flags} = createFuzzTest(
        'test_data/regress/empty_db',
        this.settings,
        ['regress/strict/input_strict.js', 'regress/strict/input_with.js']);
    execFile(file);
  });

  it('combine strict and delete', () => {
    // As above with unqualified delete.
    sandbox.stub(exceptions, 'getGeneratedSloppy').callsFake(
        () => { return new Set(['regress/strict/input_delete.js']); });
    const {file, flags} = createFuzzTest(
        'test_data/regress/empty_db',
        this.settings,
        ['regress/strict/input_strict.js', 'regress/strict/input_delete.js']);
    execFile(file);
  });

  it('combine strict and delete, life analysis', () => {
    // As above, but without the sloppy file being marked. "Strict"
    // incompatibility will also be detected at parse time.
    const {file, flags} = createFuzzTest(
        'test_data/regress/empty_db',
        this.settings,
        ['regress/strict/input_strict.js', 'regress/strict/input_delete.js']);
    execFile(file);
  });

  it('mutates negative value', () => {
    // This tests that the combination of number, function call and expression
    // mutator does't produce an update expression.
    // Previously the 1 in -1 was replaced with another negative number leading
    // to e.g. -/*comment/*-2. Then cloning the expression removed the
    // comment and produced --2 in the end.
    this.settings['MUTATE_NUMBERS'] = 1.0;
    this.settings['MUTATE_FUNCTION_CALLS'] = 1.0;
    this.settings['MUTATE_EXPRESSIONS'] = 1.0;
    const {file, flags} = createFuzzTest(
        'test_data/regress/empty_db',
        this.settings,
        ['regress/numbers/input_negative.js']);
    execFile(file);
  });

  it('mutates indices', () => {
    // Test that indices are not replaced with a negative number causing a
    // syntax error (e.g. {-1: ""}).
    this.settings['MUTATE_NUMBERS'] = 1.0;
    const {file, flags} = createFuzzTest(
        'test_data/regress/empty_db',
        this.settings,
        ['regress/numbers/input_indices.js']);
    execFile(file);
  });

  it('resolves flag contradictions', () => {
    sandbox.stub(exceptions, 'CONTRADICTORY_FLAGS').value(
        [['--flag1', '--flag2']])
    const {file, flags} = createFuzzTest(
        'test_data/regress/empty_db',
        this.settings,
        ['v8/regress/contradictions/input1.js',
         'v8/regress/contradictions/input2.js']);
    assert.deepEqual(['--flag1'], flags);
  });

  function testSuper(settings, db_path, expected) {
    // Enforce mutations at every possible location.
    settings['MUTATE_CROSSOVER_INSERT'] = 1.0;
    // Choose the only-super-statments path. This also fixed the insertion
    // order to only insert before a statement.
    sandbox.stub(random, 'choose').callsFake(() => { return true; });

    const fakeDb = new db.MutateDb(db_path);
    const mutator = new CrossOverMutator(settings, fakeDb);

    // An input with a couple of insertion spots in constructors and
    // methods of two classes. One root and one a subclass.
    const source = helpers.loadTestData('regress/super/input.js');
    mutator.mutate(source);

    const mutated = sourceHelpers.generateCode(source);
    helpers.assertExpectedResult(expected, mutated);
  }

  it('mutates super call', () => {
    // Ensure that a super() call expression isn't added to a
    // non-constructor class member or to a root class.
    testSuper(
        this.settings,
        'test_data/regress/super/super_call_db',
        'regress/super/call_expected.js');
  });

  it('mutates super member expression', () => {
    // Ensure that a super.x member expression isn't added to a
    // root class.
    testSuper(
        this.settings,
        'test_data/regress/super/super_member_db',
        'regress/super/member_expected.js');
  });

  it('does not cross-insert duplicate variables', () => {
    // Ensure we don't declare a duplicate variable when the
    // declaration is part of a cross-over inserted snippet.
    this.settings['MUTATE_CROSSOVER_INSERT'] = 1.0;
    const fakeDb = new db.MutateDb(
        'test_data/regress/duplicates/duplicates_db');
    const mutator = new CrossOverMutator(this.settings, fakeDb);

    const source = helpers.loadTestData('regress/duplicates/input.js');
    mutator.mutate(source);

    const mutated = sourceHelpers.generateCode(source);
    helpers.assertExpectedResult(
        'regress/duplicates/duplicates_expected.js', mutated);
  });

  function testAsyncReplacements(settings, expected) {
    settings['MUTATE_FUNCTION_CALLS'] = 1.0;
    sandbox.stub(sourceHelpers, 'loadResource').callsFake(() => {
      return helpers.loadTestData('differential_fuzz/fake_resource.js');
    });

    // Go only into function-call replacements.
    sandbox.stub(random, 'random').callsFake(() => { return 0.2; });

    // Input with several async and non-async replacement sites.
    const source = helpers.loadTestData('regress/async/input.js');
    const mutator = new scriptMutator.ScriptMutator(
        settings, 'test_data/regress/empty_db');
    const mutated = mutator.mutateMultiple([source]).code;
    helpers.assertExpectedResult(expected, mutated);
  }

  it('makes no cross-async replacements', () => {
    // Test to show that when REPLACE_CROSS_ASYNC_PROB isn't chosen, there
    // are no replacements that change the async property.
    sandbox.stub(functionCallMutator, 'REPLACE_CROSS_ASYNC_PROB').value(0);
    testAsyncReplacements(
        this.settings, 'regress/async/no_async_expected.js');
  });

  it('makes full cross-async replacements', () => {
    // Test to show that when REPLACE_CROSS_ASYNC_PROB is chosen, the
    // replacement functions are from the pool of all functions (both
    // maintaining and not maintaining the async property).
    sandbox.stub(functionCallMutator, 'REPLACE_CROSS_ASYNC_PROB').value(1);
    testAsyncReplacements(
        this.settings, 'regress/async/full_async_expected.js');
  });
});
