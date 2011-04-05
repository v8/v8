# Copyright 2011 the V8 project authors. All rights reserved.
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
from os.path import join, dirname, exists
import platform
import utils


class PreparserTestCase(test.TestCase):

  def __init__(self, root, path, executable, mode, context):
    super(PreparserTestCase, self).__init__(context, path, mode)
    self.executable = executable
    self.root = root

  def GetLabel(self):
    return "%s %s %s" % (self.mode, self.path[-2], self.path[-1])

  def GetName(self):
    return self.path[-1]

  def BuildCommand(self, path):
    testfile = join(self.root, self.GetName()) + ".js"
    result = [self.executable, testfile]
    return result

  def GetCommand(self):
    return self.BuildCommand(self.path)

  def Run(self):
    return test.TestCase.Run(self)


class PreparserTestConfiguration(test.TestConfiguration):

  def __init__(self, context, root):
    super(PreparserTestConfiguration, self).__init__(context, root)

  def GetBuildRequirements(self):
    return ['preparser']

  def ListTests(self, current_path, path, mode, variant_flags):
    executable = join('obj', 'preparser', mode, 'preparser')
    if utils.IsWindows():
      executable += '.exe'
    executable = join(self.context.buildspace, executable)
    # Find all .js files in tests/preparser directory.
    filenames = [f[:-3] for f in os.listdir(self.root) if f.endswith(".js")]
    filenames.sort()
    result = []
    for file in filenames:
      result.append(PreparserTestCase(self.root,
                                      current_path + [file], executable,
                                      mode, self.context))
    return result

  def GetTestStatus(self, sections, defs):
    status_file = join(self.root, 'preparser.status')
    if exists(status_file):
      test.ReadConfigurationInto(status_file, sections, defs)


def GetConfiguration(context, root):
  return PreparserTestConfiguration(context, root)
