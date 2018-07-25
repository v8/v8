#!/usr/bin/env python
# vim:fenc=utf-8:shiftwidth=2

# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Check that each header can be included in isolation.

For each header we generate one .cc file which only includes this one header.
All these .cc files are then added to a sources.gni file which is included in
BUILD.gn. Just compile to check whether there are any violations to the rule
that each header must be includable in isolation.
"""

import argparse
import os
import os.path
import re
import sys

# TODO(clemensh): Extend to tests.
DEFAULT_INPUT = ['base', 'src']
DEFAULT_GN_FILE = 'BUILD.gn'
MY_DIR = os.path.dirname(os.path.realpath(__file__))
V8_DIR = os.path.dirname(MY_DIR)
OUT_DIR = os.path.join(V8_DIR, 'check-header-includes')
AUTO_EXCLUDE = [
  # flag-definitions.h needs a mode set for being included.
  'src/flag-definitions.h',
  # blacklist of headers we need to fix (https://crbug.com/v8/7965).
  'src/allocation-site-scopes.h',
  'src/arguments.h',
  'src/ast/prettyprinter.h',
  'src/builtins/builtins-constructor.h',
  'src/builtins/builtins-utils.h',
  'src/compiler/allocation-builder.h',
  'src/compiler/graph-visualizer.h',
  'src/compiler/js-context-specialization.h',
  'src/compiler/raw-machine-assembler.h',
  'src/dateparser-inl.h',
  'src/debug/debug-frames.h',
  'src/debug/debug-scope-iterator.h',
  'src/debug/debug-scopes.h',
  'src/debug/debug-stack-trace-iterator.h',
  'src/deoptimizer.h',
  'src/elements.h',
  'src/elements-inl.h',
  'src/field-type.h',
  'src/heap/incremental-marking.h',
  'src/heap/incremental-marking-inl.h',
  'src/heap/local-allocator.h',
  'src/heap/mark-compact.h',
  'src/heap/objects-visiting.h',
  'src/heap/scavenger.h',
  'src/heap/store-buffer.h',
  'src/ic/ic.h',
  'src/json-stringifier.h',
  'src/keys.h',
  'src/layout-descriptor.h',
  'src/lookup.h',
  'src/lookup-inl.h',
  'src/map-updater.h',
  'src/objects/arguments.h',
  'src/objects/arguments-inl.h',
  'src/objects/compilation-cache-inl.h',
  'src/objects/data-handler-inl.h',
  'src/objects/hash-table-inl.h',
  'src/objects/intl-objects-inl.h',
  'src/objects/js-collection.h',
  'src/objects/js-collection-inl.h',
  'src/objects/js-regexp-string-iterator-inl.h',
  'src/objects/microtask-inl.h',
  'src/objects/module-inl.h',
  'src/objects/ordered-hash-table-inl.h',
  'src/objects/promise-inl.h',
  'src/objects/property-descriptor-object.h',
  'src/objects/prototype-info-inl.h',
  'src/objects/regexp-match-info.h',
  'src/objects/shared-function-info-inl.h',
  'src/parsing/parse-info.h',
  'src/parsing/parser.h',
  'src/parsing/preparsed-scope-data.h',
  'src/parsing/preparser.h',
  'src/profiler/heap-profiler.h',
  'src/profiler/heap-snapshot-generator.h',
  'src/profiler/heap-snapshot-generator-inl.h',
  'src/property.h',
  'src/prototype.h',
  'src/prototype-inl.h',
  'src/regexp/jsregexp.h',
  'src/regexp/jsregexp-inl.h',
  'src/snapshot/object-deserializer.h',
  'src/string-builder.h',
  'src/string-hasher-inl.h',
  'src/third_party/utf8-decoder/utf8-decoder.h',
  'src/transitions.h',
  'src/v8memory.h',
]
AUTO_EXCLUDE_PATTERNS = [
  'src/base/atomicops_internals_.*',
] + [
  # platform-specific headers
  '\\b{}\\b'.format(p) for p in
    ('win32', 'ia32', 'x64', 'arm', 'arm64', 'mips', 'mips64', 's390', 'ppc')]

args = None
def parse_args():
  global args
  parser = argparse.ArgumentParser()
  parser.add_argument('-i', '--input', type=str, action='append',
                      help='Headers or directories to check (directories '
                           'are scanned for headers recursively); default: ' +
                           ','.join(DEFAULT_INPUT))
  parser.add_argument('-x', '--exclude', type=str, action='append',
                      help='Add an exclude pattern (regex)')
  parser.add_argument('-v', '--verbose', action='store_true',
                      help='Be verbose')
  args = parser.parse_args()
  args.exclude = (args.exclude or []) + AUTO_EXCLUDE_PATTERNS
  args.exclude += ['^' + re.escape(x) + '$' for x in AUTO_EXCLUDE]
  if not args.input:
    args.input=DEFAULT_INPUT


def printv(line):
  if args.verbose:
    print line


def find_all_headers():
  printv('Searching for headers...')
  header_files = []
  exclude_patterns = [re.compile(x) for x in args.exclude]
  def add_recursively(filename):
    full_name = os.path.join(V8_DIR, filename)
    if not os.path.exists(full_name):
      sys.exit('File does not exist: {}'.format(full_name))
    if os.path.isdir(full_name):
      for subfile in os.listdir(full_name):
        full_name = os.path.join(filename, subfile)
        printv('Scanning {}'.format(full_name))
        add_recursively(full_name)
    elif filename.endswith('.h'):
      printv('--> Found header file {}'.format(filename))
      for p in exclude_patterns:
        if p.search(filename):
          printv('--> EXCLUDED (matches {})'.format(p.pattern))
          return
      header_files.append(filename)

  for filename in args.input:
    add_recursively(filename)

  return header_files


def get_cc_file_name(header):
  split = os.path.split(header)
  header_dir = os.path.relpath(split[0], V8_DIR)
  # Prefix with the directory name, to avoid collisions in the object files.
  prefix = header_dir.replace(os.path.sep, '-')
  cc_file_name = 'test-include-' + prefix + '-' + split[1][:-1] + 'cc'
  return os.path.join(OUT_DIR, cc_file_name)


def create_including_cc_files(header_files):
  for header in header_files:
    cc_file_name = get_cc_file_name(header)
    printv('Creating file {}'.format(os.path.relpath(cc_file_name, V8_DIR)))
    with open(cc_file_name, 'w') as cc_file:
      cc_file.write('#include "{}"  // check including this header in '
                    'isolation\n'.format(header))


def generate_gni(header_files):
  gni_file = os.path.join(OUT_DIR, 'sources.gni')
  printv('Generating file "{}"'.format(os.path.relpath(gni_file, V8_DIR)))
  with open(gni_file, 'w') as gn:
    gn.write("""\
# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This list is filled automatically by tools/check_header_includes.py.
check_header_includes_sources = [
""");
    for header in header_files:
      cc_file_name = get_cc_file_name(header)
      gn.write('    "{}",\n'.format(os.path.relpath(cc_file_name, V8_DIR)))
    gn.write(']\n')


def main():
  parse_args()
  header_files = find_all_headers()
  if not os.path.exists(OUT_DIR):
    os.mkdir(OUT_DIR)
  create_including_cc_files(header_files)
  generate_gni(header_files)

if __name__ == '__main__':
  main()
