#!/usr/bin/env python
# Copyright 2015 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Adaptor script called through build/isolate.gypi.

Slimmed down version of chromium's isolate driver that doesn't process dynamic
dependencies.
"""

import json
import logging
import os
import subprocess
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))


def prepare_isolate_call(args, output):
  """Gathers all information required to run isolate.py later.

  Dumps it as JSON to |output| file.
  """
  with open(output, 'wb') as f:
    json.dump({
      'args': args,
      'dir': os.getcwd(),
      'version': 1,
    }, f, indent=2, sort_keys=True)

def rebase_directories(args, abs_base):
  """Rebases all paths to be relative to abs_base."""
  def replace(index):
    args[index] = os.path.relpath(os.path.abspath(args[index]), abs_base)
  for i, arg in enumerate(args):
    if arg in ['--isolate', '--isolated']:
      replace(i + 1)
    if arg == '--path-variable':
      # Path variables have a triple form: --path-variable NAME <path>.
      replace(i + 2)

def main():
  logging.basicConfig(level=logging.ERROR, format='%(levelname)7s %(message)s')
  if len(sys.argv) < 2:
    print >> sys.stderr, 'Internal failure; mode required'
    return 1
  mode = sys.argv[1]
  args = sys.argv[1:]
  isolate = None
  isolated = None
  for i, arg in enumerate(args):
    if arg == '--isolate':
      isolate = i + 1
    if arg == '--isolated':
      isolated = i + 1
  if not isolate or not isolated:
    print >> sys.stderr, 'Internal failure'
    return 1

  # Make sure all paths are relative to the isolate file. This is an
  # expectation of the go binaries. In gn, this script is not called
  # relative to the isolate file, but relative to the product dir.
  new_base = os.path.abspath(os.path.dirname(args[isolate]))
  rebase_directories(args, new_base)
  assert args[isolate] == os.path.basename(args[isolate])
  os.chdir(new_base)

  # In 'prepare' mode just collect all required information for postponed
  # isolated.py invocation later, store it in *.isolated.gen.json file.
  if mode == 'prepare':
    prepare_isolate_call(args[1:], args[isolated] + '.gen.json')
    return 0

  swarming_client = os.path.join(TOOLS_DIR, 'swarming_client')
  sys.stdout.flush()
  return subprocess.call(
      [sys.executable, os.path.join(swarming_client, 'isolate.py')] + args)


if __name__ == '__main__':
  sys.exit(main())
