// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --enable-inspector --sandbox-testing

function receive(message) {}

send(JSON.stringify({id: 1, method: 'Debugger.enable'}));

const script =
    'function donor(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,aa,bb,cc) {\n' +
    '  return corrupt();\n' +
    '}\n' +
    'function victim() {\n' +
    '  corrupt();\n' +
    '  return 0;\n' +
    '}\n' +
    '//# sourceURL=test.js';
(0, eval)(script);

function corrupt() {
  const donor_sfi = Sandbox.readObjectField(donor, 'shared_function_info');
  Sandbox.corruptObjectField(victim, 'shared_function_info', donor_sfi);
}

send(JSON.stringify({
  id: 2,
  method: 'Debugger.setBreakpointByUrl',
  params: {url: 'test.js', lineNumber: 1, columnNumber: 0}
}));

send(JSON.stringify({
  id: 3,
  method: 'Debugger.setBreakpointByUrl',
  params: {url: 'test.js', lineNumber: 5, columnNumber: 0}
}));

function trigger() {
  victim();
}

trigger();
