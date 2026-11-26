// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --enable-os-system

if (this.os && os.system) {
  const out = os.system('sh', [
    '-c',
    'echo "20 + 22" | "$1" --stress-runs=2 --shell ' +
        '-e "throw new Error(\'fail\')"; echo "EXIT:$?"',
    'sh',
    os.d8Path,
  ]);
  assertTrue(out.includes('d8> 42\n'), out);
  assertTrue(out.includes('EXIT:1\n'), out);
}
