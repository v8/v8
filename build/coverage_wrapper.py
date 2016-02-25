#!/usr/bin/env python
# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# CC/CXX wrapper script that excludes certain file patterns from coverage
# instrumentation.

import re
import subprocess
import sys

exclusions = [
  'src/third_party',
  'third_party',
  'test',
  'testing',
]

args = sys.argv[1:]
text = ' '.join(sys.argv[2:])
for exclusion in exclusions:
  if re.search(r'\-o obj/%s[^ ]*\.o' % exclusion, text):
    args.remove('-fprofile-arcs')
    args.remove('-ftest-coverage')
    break

sys.exit(subprocess.check_call(args))
