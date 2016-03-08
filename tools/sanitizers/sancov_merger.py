#!/usr/bin/env python
# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Script for merging sancov files in parallel.

The sancov files are expected
to be located in one directory with the file-name pattern:
<executable name>.test.<id>.sancov

For each executable, this script writes a new file:
<executable name>.result.sancov

The sancov tool is expected to be in the llvm compiler-rt third-party
directory. It's not checked out by default and must be added as a custom deps:
'v8/third_party/llvm/projects/compiler-rt':
    'https://chromium.googlesource.com/external/llvm.org/compiler-rt.git'
"""

import argparse
import logging
import math
import os
import re
import subprocess
import sys

from multiprocessing import Pool, cpu_count


logging.basicConfig(level=logging.INFO)

# V8 checkout directory.
BASE_DIR = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))

# The sancov tool location.
SANCOV_TOOL = os.path.join(
    BASE_DIR, 'third_party', 'llvm', 'projects', 'compiler-rt',
    'lib', 'sanitizer_common', 'scripts', 'sancov.py')

# Number of cpus.
CPUS = cpu_count()

# Regexp to find sancov file as output by the v8 test runner. Also grabs the
# executable name in group 1.
SANCOV_FILE_RE = re.compile(r'^(.*)\.test\.\d+\.sancov$')


def merge(args):
  """Merge several sancov files into one.

  Called trough multiprocessing pool. The args are expected to unpack to:
    keep: Option if source and intermediate sancov files should be kept.
    coverage_dir: Folder where to find the sancov files.
    executable: Name of the executable whose sancov files should be merged.
    index: A number to be put into the intermediate result file name.
           If None, this is a final result.
    bucket: The list of sancov files to be merged.
  Returns: A tuple with the executable name and the result file name.
  """
  keep, coverage_dir, executable, index, bucket = args
  process = subprocess.Popen(
      [SANCOV_TOOL, 'merge'] + bucket,
      stdout=subprocess.PIPE,
      stderr=subprocess.PIPE,
      cwd=coverage_dir,
  )
  output, _ = process.communicate()
  assert process.returncode == 0
  if index is not None:
    # This is an intermediate result, add the bucket index to the file name.
    result_file_name = '%s.result.%d.sancov' % (executable, index)
  else:
    # This is the final result without bucket index.
    result_file_name = '%s.result.sancov' % executable
  with open(os.path.join(coverage_dir, result_file_name), "wb") as f:
    f.write(output)
  if not keep:
    for f in bucket:
      os.remove(os.path.join(coverage_dir, f))
  return executable, result_file_name


def generate_inputs(keep, coverage_dir, file_map, cpus):
  """Generate inputs for multiprocessed merging.

  Splits the sancov files into several buckets, so that each bucket can be
  merged in a separate process. We have only few executables in total with
  mostly lots of associated files. In the general case, with many executables
  we might need to avoid splitting buckets of executables with few files.

  Returns: List of args as expected by merge above.
  """
  inputs = []
  for executable, files in file_map.iteritems():
    # What's the bucket size for distributing files for merging? E.g. with
    # 2 cpus and 9 files we want bucket size 5.
    n = max(2, int(math.ceil(len(files) / float(cpus))))

    # Chop files into buckets.
    buckets = [files[i:i+n] for i in xrange(0, len(files), n)]

    # Inputs for multiprocessing. List of tuples containing:
    # Keep-files option, base path, executable name, index of bucket,
    # list of files.
    inputs.extend([(keep, coverage_dir, executable, i, b)
                   for i, b in enumerate(buckets)])
  return inputs


def merge_parallel(inputs):
  """Process several merge jobs in parallel."""
  pool = Pool(CPUS)
  try:
    return pool.map(merge, inputs)
  finally:
    pool.close()


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--coverage-dir', required=True,
                      help='Path to the sancov output files.')
  parser.add_argument('--keep', default=False, action='store_true',
                      help='Keep sancov output files after merging.')
  options = parser.parse_args()

  # Check if folder with coverage output exists.
  assert (os.path.exists(options.coverage_dir) and
          os.path.isdir(options.coverage_dir))

  # Map executable names to their respective sancov files.
  file_map = {}
  for f in os.listdir(options.coverage_dir):
    match = SANCOV_FILE_RE.match(f)
    if match:
      file_map.setdefault(match.group(1), []).append(f)

  inputs = generate_inputs(
      options.keep, options.coverage_dir, file_map, CPUS)

  logging.info('Executing %d merge jobs in parallel for %d executables.' %
               (len(inputs), len(file_map)))

  results = merge_parallel(inputs)

  # Map executable names to intermediate bucket result files.
  file_map = {}
  for executable, f in results:
    file_map.setdefault(executable, []).append(f)

  # Merge the bucket results for each executable.
  # The final result has index None, so no index will appear in the
  # file name.
  inputs = [(options.keep, options.coverage_dir, executable, None, files)
             for executable, files in file_map.iteritems()]

  logging.info('Merging %d intermediate results.' % len(inputs))

  merge_parallel(inputs)
  return 0


if __name__ == '__main__':
  sys.exit(main())
