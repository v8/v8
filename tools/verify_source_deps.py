#!/usr/bin/env python
# Copyright 2015 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Script to print potentially missing source dependencies based on the actual
.h and .cc files in the source tree and which files are included in the gyp
and gn files. The latter inclusion is overapproximated.

TODO(machenbach): Gyp files in src will point to source files in src without a
src/ prefix. For simplicity, all paths relative to src are stripped. But this
tool won't be accurate for other sources in other directories (e.g. cctest).
"""

import itertools
import re
import os
import sys


V8_BASE = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
V8_SRC_BASE = os.path.join(V8_BASE, 'src')
V8_TEST_BASE = os.path.join(V8_BASE, 'test')
V8_INCLUDE_BASE = os.path.join(V8_BASE, 'include')

GYP_FILES = [
  os.path.join(V8_BASE, 'src', 'd8.gyp'),
  os.path.join(V8_BASE, 'src', 'v8.gyp'),
  os.path.join(V8_BASE, 'src', 'inspector', 'inspector.gypi'),
  os.path.join(V8_BASE, 'src', 'third_party', 'vtune', 'v8vtune.gyp'),
  os.path.join(V8_BASE, 'test', 'cctest', 'cctest.gyp'),
  os.path.join(V8_BASE, 'test', 'fuzzer', 'fuzzer.gyp'),
  os.path.join(V8_BASE, 'test', 'unittests', 'unittests.gyp'),
  os.path.join(V8_BASE, 'tools', 'parser-shell.gyp'),
]

GN_FILES = [
  os.path.join(V8_BASE, 'BUILD.gn'),
  os.path.join(V8_BASE, 'src', 'inspector', 'BUILD.gn'),
  os.path.join(V8_BASE, 'test', 'cctest', 'BUILD.gn'),
  os.path.join(V8_BASE, 'test', 'unittests', 'BUILD.gn'),
  os.path.join(V8_BASE, 'tools', 'BUILD.gn'),
]

GN_UNSUPPORTED_FEATURES = [
  'aix',
  'cygwin',
  'freebsd',
  'openbsd',
  'ppc',
  'qnx',
  'solaris',
  'vtune',
  'x87',
]

ALL_GN_PREFIXES = [
  '..',
  os.path.join('src', 'inspector'),
  'src',
  os.path.join('test', 'cctest'),
  os.path.join('test', 'unittests'),
]

ALL_GYP_PREFIXES = [
  '..',
  'common',
  os.path.join('src', 'third_party', 'vtune'),
  'src',
  os.path.join('test', 'cctest'),
  os.path.join('test', 'common'),
  os.path.join('test', 'fuzzer'),
  os.path.join('test', 'unittests'),
]

def pathsplit(path):
  return re.split('[/\\\\]', path)

def path_no_prefix(path, prefixes):
  for prefix in prefixes:
    if path.startswith(prefix + os.sep):
      return path_no_prefix(path[len(prefix) + 1:], prefixes)
  return path


def isources(directory, prefixes):
  for root, dirs, files in os.walk(directory):
    for f in files:
      if not (f.endswith('.h') or f.endswith('.cc')):
        continue
      yield path_no_prefix(
          os.path.relpath(os.path.join(root, f), V8_BASE), prefixes)


def iflatten(obj):
  if isinstance(obj, dict):
    for value in obj.values():
      for i in iflatten(value):
        yield i
  elif isinstance(obj, list):
    for value in obj:
      for i in iflatten(value):
        yield i
  elif isinstance(obj, basestring):
    yield path_no_prefix(os.path.join(*pathsplit(obj)), ALL_GYP_PREFIXES)


def iflatten_gyp_file(gyp_file):
  """Overaproximates all values in the gyp file.

  Iterates over all string values recursively. Removes '../' path prefixes.
  """
  with open(gyp_file) as f:
    return iflatten(eval(f.read()))


def iflatten_gn_file(gn_file):
  """Overaproximates all values in the gn file.

  Iterates over all double quoted strings.
  """
  with open(gn_file) as f:
    for line in f.read().splitlines():
      match = re.match(r'.*"([^"]*)".*', line)
      if match:
        yield path_no_prefix(
            os.path.join(*pathsplit(match.group(1))), ALL_GN_PREFIXES)


def icheck_values(values, prefixes, *source_dirs):
  for source_file in itertools.chain(
      *[isources(source_dir, prefixes) for source_dir in source_dirs]
    ):
    if source_file not in values:
      yield source_file


def missing_gyp_files():
  gyp_values = set(itertools.chain(
    *[iflatten_gyp_file(gyp_file) for gyp_file in GYP_FILES]
    ))
  return sorted(icheck_values(
      gyp_values, ALL_GYP_PREFIXES, V8_SRC_BASE, V8_INCLUDE_BASE, V8_TEST_BASE))


def missing_gn_files():
  gn_values = set(itertools.chain(
    *[iflatten_gn_file(gn_file) for gn_file in GN_FILES]
    ))

  gn_files = sorted(icheck_values(
      gn_values, ALL_GN_PREFIXES, V8_SRC_BASE, V8_INCLUDE_BASE, V8_TEST_BASE))
  return filter(
      lambda x: not any(i in x for i in GN_UNSUPPORTED_FEATURES), gn_files)


def main():
  print "----------- Files not in gyp: ------------"
  for i in missing_gyp_files():
    print i

  print "\n----------- Files not in gn: -------------"
  for i in missing_gn_files():
    print i
  return 0

if '__main__' == __name__:
  sys.exit(main())
