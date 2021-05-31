# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

versions = {
    "beta": "9.2",
    "stable": "9.1",
    "extended" : "9.0",
}

branch_names = [
    "ci",  # master
    "ci.br.beta",
    "ci.br.stable",
    "ci.br.extended",
]

beta_re = versions["beta"].replace(".", "\\.")
stable_re = versions["stable"].replace(".", "\\.")
extended_re = versions["extended"].replace(".", "\\.")