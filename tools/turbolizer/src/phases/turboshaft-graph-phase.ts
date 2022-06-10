// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { Phase, PhaseType } from "./phase";

export class TurboshaftGraphPhase extends Phase {
  constructor(name: string) {
    super(name, PhaseType.TurboshaftGraph);
    // To be continued ...
  }
}
