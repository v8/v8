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

import test
import os
from os.path import join, dirname
import re


FLAGS_PATTERN = re.compile(r"//\s+Flags:(.*)")


class MjsunitTestCase(test.TestCase):

  def __init__(self, path, file, config):
    super(MjsunitTestCase, self).__init__(path)
    self.file = file
    self.config = config
  
  def GetName(self):
    return self.path[-1]
  
  def GetCommand(self):
    result = [self.config.context.vm]
    source = open(self.file).read()
    flags_match = FLAGS_PATTERN.search(source)
    if flags_match:
      runtime_flags = flags_match.group(1).strip().split()
      result += ["--runtime-flags", " ".join(runtime_flags)]
    framework = join(dirname(self.config.root), 'mjsunit', 'mjsunit.js')
    result += [framework, self.file]
    return result


class MjsunitTestConfiguration(test.TestConfiguration):

  def __init__(self, context, root):
    super(MjsunitTestConfiguration, self).__init__(context, root)
  
  def Ls(self, path):
    def SelectTest(name):
      return name.endswith('.js') and name != 'mjsunit.js'
    return [f[:-3] for f in os.listdir(path) if SelectTest(f)]
  
  def Contains(self, path, file):
    if len(path) > len(file):
      return False
    for i in xrange(len(path)):
      if path[i] != file[i]:
        return False
    return True
  
  def ListTests(self, current_path, path, mode):
    mjsunit = [[t] for t in self.Ls(self.root)]
    regress = [['regress', t] for t in self.Ls(join(self.root, 'regress'))]
    all_tests = mjsunit + regress
    result = []
    for test in all_tests:
      if self.Contains(path, test):
        full_name = current_path + test
        file_path = join(self.root, reduce(join, test, "") + ".js")
        result.append(MjsunitTestCase(full_name, file_path, self))
    return result

  def GetBuildRequirements(self):
    return ['sample=shell']


def GetConfiguration(context, root):
  return MjsunitTestConfiguration(context, root)
