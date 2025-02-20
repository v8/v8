// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Analyzer.
 * Mutator that acquires additional information used in subsequent mutators.
 * This mutator doesn't change the structure, but only marks nodes with
 * meta information.
 */

'use strict';

const assert = require('assert');
const babelTypes = require('@babel/types');

const common = require('./common.js');
const mutator = require('./mutator.js');

// Additional resource needed if the AsyncIterator ID is found in the code.
const ASYNC_ITERATOR_ID = 'AsyncIterator'
const ASYNC_ITERATOR_RESOURCE = 'async_iterator.js'

// State for keeping track of loops and loop bodies during traversal.
const LoopState = Object.freeze({
    LOOP:   Symbol("loop"),
    BLOCK:  Symbol("block"),
});

class ContextAnalyzer extends mutator.Mutator {
  constructor() {
    super();
    this.loopStack = [];
  }

  /**
   * Return true if the traversal is currently within a loop test part.
   */
  isLoopTest() {
    return this.loopStack.at(-1) === LoopState.LOOP;
  }

  get visitor() {
    const thisMutator = this;
    const loopStack = this.loopStack;

    const loopStatement = {
      enter(path) {
        loopStack.push(LoopState.LOOP);

        // Mark functions that contain empty infinite loops.
        if (common.isInfiniteLoop(path.node) &&
            path.node.body &&
            babelTypes.isBlockStatement(path.node.body) &&
            !path.node.body.body.length) {
          const fun = path.findParent((p) => p.isFunctionDeclaration());
          if (fun && fun.node && fun.node.id &&
              babelTypes.isIdentifier(fun.node.id) &&
              common.isFunctionIdentifier(fun.node.id.name)) {
            thisMutator.context.infiniteFunctions.add(fun.node.id.name);
          }
        }
      },
      exit(path) {
        assert(loopStack.pop() === LoopState.LOOP);
      }
    };

    return {
      Identifier(path) {
        if (path.node.name === ASYNC_ITERATOR_ID) {
          thisMutator.context.extraResources.add(ASYNC_ITERATOR_RESOURCE);
        }
        if (thisMutator.isLoopTest() &&
            common.isVariableIdentifier(path.node.name)) {
          thisMutator.context.loopVariables.add(path.node.name);
        }
      },
      FunctionDeclaration(path) {
        if (path.node.id && babelTypes.isIdentifier(path.node.id)) {
          if (common.isFunctionIdentifier(path.node.id.name) &&
              path.node.async) {
            thisMutator.context.asyncFunctions.add(path.node.id.name);
          }
        }
      },
      WhileStatement: loopStatement,
      DoWhileStatement: loopStatement,
      ForStatement: loopStatement,
      BlockStatement: {
        enter(path) {
          loopStack.push(LoopState.BLOCK);
        },
        exit(path) {
          assert(loopStack.pop() === LoopState.BLOCK);
        }
      },
    };
  }
}

module.exports = {
  ContextAnalyzer: ContextAnalyzer,
};
