# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

versions = {
    "beta": "9.1",
    "stable": "9.0",
}

branch_names = [
    "ci",  # master
    "ci.br.stable",  # stable
    "ci.br.beta",  # beta
]

beta_re = versions["beta"].replace(".", "\\.")
stable_re = versions["stable"].replace(".", "\\.")
