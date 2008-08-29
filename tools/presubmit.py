#!/usr/bin/env python
#
# Copyright 2008 Google Inc.  All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of Google Inc. nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


import optparse
import os
from os.path import abspath, join, dirname
import sys
import subprocess


ENABLED_LINT_RULES = """
build/class
build/deprecated
build/endif_comment
build/forward_decl
build/include_order
build/include_what_you_use
build/printf_format
build/storage_class
legal/copyright
readability/boost
readability/braces
readability/casting
readability/check
readability/constructors
readability/fn_size
readability/function
readability/multiline_comment
readability/multiline_string
readability/streams
readability/todo
readability/utf8
runtime/arrays
runtime/casting
runtime/deprecated_fn
runtime/explicit
runtime/int
runtime/memset
runtime/mutex
runtime/nonconf
runtime/printf
runtime/printf_format
runtime/references
runtime/rtti
runtime/sizeof
runtime/string
runtime/virtual
runtime/vlog
whitespace/blank_line
whitespace/braces
whitespace/comma
whitespace/comments
whitespace/end_of_line
whitespace/ending_newline
whitespace/indent
whitespace/labels
whitespace/line_length
whitespace/newline
whitespace/operators
whitespace/parens
whitespace/tab
whitespace/todo
""".split()


class SourceFileProcessor(object):
  """
  Utility class that can run through a directory structure, find all relevant
  files and invoke a custom check on the files.
  """

  def Run(self, path):
    all_files = []
    for file in self.GetPathsToSearch():
      all_files += self.FindFilesIn(join(path, file))
    if not self.ProcessFiles(all_files):
      return False
    return True

  def IgnoreDir(self, name):
    return name.startswith('.')

  def IgnoreFile(self, name):
    return name.startswith('.')

  def FindFilesIn(self, path):
    result = []
    for (root, dirs, files) in os.walk(path):
      for ignored in [x for x in dirs if self.IgnoreDir(x)]:
        dirs.remove(ignored)
      for file in files:
        if not self.IgnoreFile(file) and self.IsRelevant(file):
          result.append(join(root, file))
    return result


class CppLintProcessor(SourceFileProcessor):
  """
  Lint files to check that they follow the google code style.
  """

  def IsRelevant(self, name):
    return name.endswith('.cc') or name.endswith('.h')

  def IgnoreDir(self, name):
    return (super(CppLintProcessor, self).IgnoreDir(name)
              or (name == 'third_party'))

  def GetPathsToSearch(self):
    return ['src', 'public', 'samples', join('test', 'cctest')]

  def ProcessFiles(self, files):
    filt = '-,' + ",".join(['+' + n for n in ENABLED_LINT_RULES])
    command = ['cpplint.py', '--filter', filt] + join(files)
    process = subprocess.Popen(command)
    return process.wait() == 0


class CopyrightProcessor(SourceFileProcessor):
  """
  Check that all files include a copyright notice.
  """

  RELEVANT_EXTENSIONS = ['.js', '.cc', '.h', '.py', '.c', 'SConscript',
      'SConstruct', '.status']
  FILES_TO_IGNORE = ['pcre_chartables.c', 'config.h']
  def IsRelevant(self, name):
    if name in CopyrightProcessor.FILES_TO_IGNORE:
      return False
    for ext in CopyrightProcessor.RELEVANT_EXTENSIONS:
      if name.endswith(ext):
        return True
    return False

  def GetPathsToSearch(self):
    return ['.']

  def ProcessContents(self, name, contents):
    if not 'Copyright' in contents:
      print "No copyright in %s." % name
      return False
    return True

  def ProcessFiles(self, files):
    success = True
    for file in files:
      try:
        handle = open(file)
        contents = handle.read()
        success = self.ProcessContents(file, contents) and success
      finally:
        handle.close()
    return success


def GetOptions():
  result = optparse.OptionParser()
  result.add_option('--no-lint', help="Do not run cpplint", default=False,
                    action="store_true")
  return result


def Main():
  workspace = abspath(join(dirname(sys.argv[0]), '..'))
  parser = GetOptions()
  (options, args) = parser.parse_args()
  success = True
  if not options.no_lint:
    success = CppLintProcessor().Run(workspace) and success
  success = CopyrightProcessor().Run(workspace) and success
  if success:
    return 0
  else:
    return 1


if __name__ == '__main__':
  sys.exit(Main())
