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
  result.Add('mode', 'compilation mode (debug, release)', 'release')
  result.Add('toolchain', 'the toolchain to use (gcc, msvc)', toolchain_guess)
  result.Add('os', 'the os to build for (linux, macos, win32)', os_guess)
  result.Add('processor', 'the processor to build for (arm, ia32)', processor_guess)
  result.Add('snapshot', 'build using snapshots for faster start-up (on, off)', 'off')
  result.Add('library', 'which type of library to produce (static, shared, default)', 'default')
  result.Add('sample', 'build sample (process, shell)', '')
  return result


def VerifyOptions(env):
  if not env['mode'] in ['debug', 'release']:
    Abort("Unknown build mode '%s'." % env['mode'])
  if not env['toolchain'] in ['gcc', 'msvc']:
    Abort("Unknown toolchain '%s'." % env['toolchain'])
  if not env['os'] in ['linux', 'macos', 'win32']:
    Abort("Unknown os '%s'." % env['os'])
  if not env['processor'] in ['arm', 'ia32']:
    Abort("Unknown processor '%s'." % env['processor'])
  if not env['snapshot'] in ['on', 'off']:
    Abort("Illegal value for option snapshot: '%s'." % env['snapshot'])
  if not env['library'] in ['static', 'shared', 'default']:
    Abort("Illegal value for option library: '%s'." % env['library'])
  if not env['sample'] in ['', 'process', 'shell']:
    Abort("Illegal value for option sample: '%s'." % env['sample'])


def Build():
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

  # Build the object files by invoking SCons recursively.
  object_files = env.SConscript(
    join('src', 'SConscript'),
    build_dir='build',
    exports='toolchain arch os mode use_snapshot library_type',
    duplicate=False
  )

  # Link the object files into a library.
  if library_type == 'static':
    library = env.StaticLibrary('v8', object_files)
  elif library_type == 'shared':
    # There seems to be a glitch in the way scons decides where to put
    # PDB files when compiling using MSVC so we specify it manually.
    # This should not affect any other platforms.
    library = env.SharedLibrary('v8', object_files, PDB='v8.dll.pdb')
  else:
    library = env.Library('v8', object_files)

  # Bail out if we're not building any sample.
  sample = env['sample']
  if not sample: return

  # Build the sample.
  env.Replace(CPPPATH='public')
  object_path = join('build', 'samples', sample)
  source_path = join('samples', sample + '.cc')
  object = env.Object(object_path, source_path)
  if toolchain == 'gcc':
    env.Program(sample, [object, library], LIBS='pthread')
  else:
    env.Program(sample, [object, library], LIBS='WS2_32')


Build()
