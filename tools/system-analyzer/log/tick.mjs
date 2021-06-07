// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import {Profile} from '../../profile.mjs'

import {LogEntry} from './log.mjs';

export class TickLogEntry extends LogEntry {
  constructor(time, vmState, processedStack) {
    super(TickLogEntry.extractType(processedStack), time);
    this.state = vmState;
    this.stack = processedStack;
  }

  static extractType(processedStack) {
    if (processedStack.length == 0) return 'idle';
    const topOfStack = processedStack[processedStack.length - 1];
    if (topOfStack?.state) {
      return Profile.getKindFromState(topOfStack.state);
    }
    return 'native';
  }
}