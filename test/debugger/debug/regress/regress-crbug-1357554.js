// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(crbug.com/1357554): Enable for Sparkplug once we can step into a
//     a sparkplug function paused during the sparkplug prolog builtin.

// Flags: --no-sparkplug

var Debug = debug.Debug;

Debug.setListener(function (event, exec_state, event_data, data) {
  if (event == Debug.DebugEvent.Break) {
    Debug.setListener(null);
    Debug.stepInto();
  }
});

%ScheduleBreak();
(function foo() {
  return 5;
})();
