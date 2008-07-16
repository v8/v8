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

import platform
import re
import sys
from os.path import join, dirname, abspath
root_dir = dirname(File('SConstruct').rfile().abspath)
sys.path.append(join(root_dir, 'tools'))
import js2c


def Abort(message):
  print message
  sys.exit(1)


def GuessOS():
  id = platform.system()
  if id == 'Linux':
    return 'linux'
  elif id == 'Darwin':
    return 'macos'
  elif id == 'Windows':
    return 'win32'
  else:
    return '<none>'


def GuessProcessor():
  id = platform.machine()
  if id.startswith('arm'):
    return 'arm'
  elif (not id) or (not re.match('(x|i[3-6])86', id) is None):
    return 'ia32'
  else:
    return '<none>'


def GuessToolchain(os):
  tools = Environment()['TOOLS']
  if 'gcc' in tools:
    if os == 'macos' and 'Kernel Version 8' in platform.version():
      return 'gcc-darwin'
    else:
      return 'gcc'
  elif 'msvc' in tools:
    return 'msvc'
  else:
    return '<none>'


def GetOptions():
  result = Options()
  os_guess = GuessOS()
  toolchain_guess = GuessToolchain(os_guess)
  processor_guess = GuessProcessor()
  result.Add('mode', 'debug or release', 'release')
  result.Add('toolchain', 'the toolchain to use (gcc, gcc-darwin or msvc)', toolchain_guess)
  result.Add('os', 'the os to build for (linux, macos or win32)', os_guess)
  result.Add('processor', 'the processor to build for (arm or ia32)', processor_guess)
  result.Add('snapshot', 'build using snapshots for faster start-up (on, off)', 'off')
  result.Add('library', 'which type of library to produce (static, shared, default)', 'default')
  return result


def VerifyOptions(env):
  if not env['mode'] in ['debug', 'release']:
    Abort("Unknown build mode '%s'." % env['mode'])
  if not env['toolchain'] in ['gcc', 'gcc-darwin', 'msvc']:
    Abort("Unknown toolchain '%s'." % env['toolchain'])
  if not env['os'] in ['linux', 'macos', 'win32']:
    Abort("Unknown os '%s'." % env['os'])
  if not env['processor'] in ['arm', 'ia32']:
    Abort("Unknown processor '%s'." % env['processor'])
  if not env['snapshot'] in ['on', 'off']:
    Abort("Illegal value for option snapshot: '%s'." % env['snapshot'])
  if not env['library'] in ['static', 'shared', 'default']:
    Abort("Illegal value for option library: '%s'." % env['library'])


def Start():
  opts = GetOptions()
  env = Environment(options=opts)
  Help(opts.GenerateHelpText(env))
  VerifyOptions(env)

  os = env['os']
  arch = env['processor']
  toolchain = env['toolchain']
  mode = env['mode']
  use_snapshot = (env['snapshot'] == 'on')
  library_type = env['library']

  env.SConscript(
    join('src', 'SConscript'),
    build_dir=mode,
    exports='toolchain arch os mode use_snapshot library_type',
    duplicate=False
  )


Start()
