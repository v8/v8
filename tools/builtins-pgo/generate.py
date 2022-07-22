#!/usr/bin/env python

# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can
# be found in the LICENSE file.

import subprocess
import argparse
import os

parser = argparse.ArgumentParser(
    description='Generate builtin PGO profiles. ' +
    'The script has to be run from the root of a V8 checkout and updates the profiles in `tools/builtins-pgo`.'
)
parser.add_argument(
    'benchmark_path',
    help='path to benchmark runner .js file, usually JetStream2\'s `cli.js`')
parser.add_argument(
    '--out-path',
    default="out",
    help='directory to be used for building V8, by default `./out`')

args = parser.parse_args()


def try_start_goma():
  res = subprocess.run(["goma_ctl", "ensure_start"])
  print(res.returncode)
  has_goma = res.returncode == 0
  print("Detected Goma:", has_goma)
  return has_goma


def _Write(path, content):
  with open(path, "w") as f:
    f.write(content)


def build_d8(path, gn_args):
  if not os.path.exists(path):
    os.makedirs(path)
  _Write(os.path.join(path, "args.gn"), gn_args)
  subprocess.run(["gn", "gen", path])
  subprocess.run(["autoninja", "-C", path, "d8"])
  return os.path.abspath(os.path.join(path, "d8"))


if not os.path.exists(os.path.join("tools", "builtins-pgo", "generate.py")):
  print("Please run this script from the root of a V8 checkout.")
  exit(1)

if not os.path.splitext(args.benchmark_path)[1] != ".js":
  print("\"")

v8_path = os.getcwd()

has_goma = try_start_goma()

X64_ARGS_TEMPLATE = """\
is_debug = false
target_cpu = "x64"
use_goma = {has_goma}
v8_enable_builtins_profiling = true
""".format(has_goma="true" if has_goma else "false")

for arch, gn_args in [("x64", X64_ARGS_TEMPLATE)]:
  build_dir = os.path.join(args.out_path,
                           arch + ".release.generate_builtin_pgo_profile")
  d8_path = build_d8(build_dir, gn_args)
  benchmark_dir = os.path.dirname(args.benchmark_path)
  benchmark_file = os.path.basename(args.benchmark_path)
  subprocess.run([d8_path, "--turbo-profiling-log-builtins", benchmark_file],
                 cwd=benchmark_dir)
  get_hints_path = os.path.join("tools", "builtins-pgo", "get_hints.py")
  log_path = os.path.join(benchmark_dir, "v8.log")
  profile_path = os.path.join("tools", "builtins-pgo", arch + ".profile")
  subprocess.run([get_hints_path, log_path, profile_path])
