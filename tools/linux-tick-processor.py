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

# Usage: process-ticks.py <logfile>
# Where <logfile> is the log file name (eg, v8.log).

import os, re, sys, tickprocessor, getopt;

class LinuxTickProcessor(tickprocessor.TickProcessor):

  def ParseVMSymbols(self, filename, start, end):
    """Extract symbols and add them to the cpp entries."""
    pipe = os.popen('nm -n %s | c++filt' % filename, 'r')
    try:
      for line in pipe:
        row = re.match('^([0-9a-fA-F]{8}) . (.*)$', line)
        if row:
          addr = int(row.group(1), 16)
          if addr < start and addr < end - start:
            addr += start
          self.cpp_entries.Insert(addr, tickprocessor.CodeEntry(addr, row.group(2)))
    finally:
      pipe.close()


def Usage():
  print("Usage: linux-tick-processor.py --{js,gc,compiler,other}  logfile-name");
  sys.exit(2)

def Main():
  # parse command line options
  state = None;
  try:
    opts, args = getopt.getopt(sys.argv[1:], "jgco", ["js", "gc", "compiler", "other"])
  except getopt.GetoptError:
    usage()
  # process options.
  for key, value in opts:
    if key in ("-j", "--js"):
      state = 0
    if key in ("-g", "--gc"):
      state = 1
    if key in ("-c", "--compiler"):
      state = 2
    if key in ("-o", "--other"):
      state = 3
  # do the processing.
  if len(args) != 1:
      Usage();
  tick_processor = LinuxTickProcessor()
  tick_processor.ProcessLogfile(args[0], state)
  tick_processor.PrintResults()

if __name__ == '__main__':
  Main()
