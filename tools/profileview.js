// Copyright 2009 the V8 project authors. All rights reserved.
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


// Initlialize namespaces
var devtools = devtools || {};
devtools.profiler = devtools.profiler || {};


/**
 * Creates a Profile View builder object.
 *
 * @param {number} samplingRate Number of ms between profiler ticks.
 * @constructor
 */
devtools.profiler.ViewBuilder = function(samplingRate) {
  this.samplingRate = samplingRate;
};


/**
 * Builds a profile view for the specified call tree.
 *
 * @param {devtools.profiler.CallTree} callTree A call tree.
 */
devtools.profiler.ViewBuilder.prototype.buildView = function(
    callTree) {
  var head;
  var samplingRate = this.samplingRate;
  callTree.traverse(function(node, viewParent) {
    var viewNode = new devtools.profiler.ProfileView.Node(
        node.label, node.totalWeight * samplingRate,
        node.selfWeight * samplingRate, head);
    if (viewParent) {
      viewParent.addChild(viewNode);
    } else {
      head = viewNode;
    }
    return viewNode;
  });
  var view = new devtools.profiler.ProfileView(head);
  return view;
};


/**
 * Creates a Profile View object. It allows to perform sorting
 * and filtering actions on the profile. Profile View mimicks
 * the Profile object from WebKit's JSC profiler.
 *
 * @param {devtools.profiler.ProfileView.Node} head Head (root) node.
 * @constructor
 */
devtools.profiler.ProfileView = function(head) {
  this.head = head;
};


/**
 * Sorts the profile view using the specified sort function.
 *
 * @param {function(devtools.profiler.ProfileView.Node,
 *     devtools.profiler.ProfileView.Node):number} sortFunc A sorting
 *     functions. Must comply with Array.sort sorting function requirements.
 */
devtools.profiler.ProfileView.prototype.sort = function(sortFunc) {
  this.traverse(function (node) {
    node.sortChildren(sortFunc);
  });
};


/**
 * Traverses profile view nodes in preorder.
 *
 * @param {function(devtools.profiler.ProfileView.Node)} f Visitor function.
 */
devtools.profiler.ProfileView.prototype.traverse = function(f) {
  var nodesToTraverse = [this.head];
  while (nodesToTraverse.length > 0) {
    var node = nodesToTraverse.shift();
    f(node);
    nodesToTraverse = nodesToTraverse.concat(node.children);
  }
};


/**
 * Constructs a Profile View node object. Each node object corresponds to
 * a function call.
 *
 * @param {string} internalFuncName A fully qualified function name.
 * @param {number} totalTime Amount of time that application spent in the
 *     corresponding function and its descendants (not that depending on
 *     profile they can be either callees or callers.)
 * @param {number} selfTime Amount of time that application spent in the
 *     corresponding function only.
 * @param {devtools.profiler.ProfileView.Node} head Profile view head.
 * @constructor
 */
devtools.profiler.ProfileView.Node = function(
    internalFuncName, totalTime, selfTime, head) {
  this.internalFuncName = internalFuncName;
  this.totalTime = totalTime;
  this.selfTime = selfTime;
  this.head = head;
  this.parent = null;
  this.children = [];
};


/**
 * Returns a share of the function's total time in application's total time.
 */
devtools.profiler.ProfileView.Node.prototype.__defineGetter__(
    'totalPercent',
    function() { return this.totalTime /
      (this.head ? this.head.totalTime : this.totalTime) * 100.0; });


/**
 * Returns a share of the function's self time in application's total time.
 */
devtools.profiler.ProfileView.Node.prototype.__defineGetter__(
    'selfPercent',
    function() { return this.selfTime /
      (this.head ? this.head.totalTime : this.totalTime) * 100.0; });


/**
 * Returns a share of the function's total time in its parent's total time.
 */
devtools.profiler.ProfileView.Node.prototype.__defineGetter__(
    'parentTotalPercent',
    function() { return this.totalTime /
      (this.parent ? this.parent.totalTime : this.totalTime) * 100.0; });


/**
 * Adds a child to the node.
 *
 * @param {devtools.profiler.ProfileView.Node} node Child node.
 */
devtools.profiler.ProfileView.Node.prototype.addChild = function(node) {
  node.parent = this;
  this.children.push(node);
};


/**
 * Sorts all the node's children recursively.
 *
 * @param {function(devtools.profiler.ProfileView.Node,
 *     devtools.profiler.ProfileView.Node):number} sortFunc A sorting
 *     functions. Must comply with Array.sort sorting function requirements.
 */
devtools.profiler.ProfileView.Node.prototype.sortChildren = function(
    sortFunc) {
  this.children.sort(sortFunc);
};
