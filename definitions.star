# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

beta_version = "8.6"
stable_version = "8.5"

branch_names = [
    "ci",               # master
    "ci.br.stable",     # stable
    "ci.br.beta",       # beta
]

beta_re = beta_version.replace(".", "\\.")
stable_re = stable_version.replace(".", "\\.")
