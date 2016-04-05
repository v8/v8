// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

Error.prepareStackTrace = function(exception, frames) {
  return frames[0].getEvalOrigin();
}

var source = "new Error()";
var eval_origin;
var geval = eval;
var log = [];

(function() {
  log.push([geval(source).stack, "15:13"]);
  log.push([geval(source).stack, "16:13"]);
  log.push([geval(source).stack, "17:13"]);
})();

(function() {
  log.push([eval(source).stack, "21:13"]);
  log.push([eval(source).stack, "22:13"]);
  log.push([eval(source).stack, "23:13"]);
})();

log.push([eval(source).stack, "26:11"]);
log.push([eval(source).stack, "27:11"]);
log.push([eval(source).stack, "28:11"]);

Error.prepareStackTrace = undefined;

for (var item of log) {
  var stacktraceline = item[0];
  var expectation = item[1];
  var re = new RegExp(`:${expectation}\\)$`);
  assertTrue(re.test(stacktraceline));
}
