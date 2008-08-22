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


LIBRARY_FLAGS = {
  'all': {
    'CPPDEFINES':   ['ENABLE_LOGGING_AND_PROFILING']
  },
  'gcc': {
    'all': {
      'DIALECTFLAGS': ['-ansi'],
      'CCFLAGS':      ['$DIALECTFLAGS', '$WARNINGFLAGS'],
      'CXXFLAGS':     ['$CCFLAGS', '-fno-rtti', '-fno-exceptions'],
      'LIBS':         ['pthread']
    },
    'mode:debug': {
      'CCFLAGS':      ['-g', '-O0'],
      'CPPDEFINES':   ['ENABLE_DISASSEMBLER', 'DEBUG']
    },
    'mode:release': {
      'CCFLAGS':      ['-O2']
    },
  },
  'msvc': {
    'all': {
      'DIALECTFLAGS': ['/nologo'],
      'WARNINGFLAGS': ['/W3', '/WX', '/wd4355', '/wd4800'],
      'CCFLAGS':      ['$DIALECTFLAGS', '$WARNINGFLAGS'],
      'CXXFLAGS':     ['$CCFLAGS', '/GS-', '/GR-', '/Gy'],
      'CPPDEFINES':   ['WIN32', '_CRT_SECURE_NO_DEPRECATE',
          '_CRT_NONSTDC_NO_DEPRECATE', '_USE_32BIT_TIME_T',
          'PCRE_STATIC'],
      'LINKFLAGS':    ['/NOLOGO', '/MACHINE:X86', '/INCREMENTAL:NO',
          '/NXCOMPAT', '/IGNORE:4221'],
      'ARFLAGS':      ['/NOLOGO'],
      'CCPDBFLAGS':   ['/Zi']
    },
    'mode:debug': {
      'CCFLAGS':      ['/Od', '/Gm', '/MTd'],
      'CPPDEFINES':   ['_DEBUG', 'ENABLE_DISASSEMBLER', 'DEBUG'],
      'LINKFLAGS':    ['/DEBUG']
    },
    'mode:release': {
      'CCFLAGS':      ['/Ox', '/MT', '/Ob2', '/Oi', '/Oy'],
      'LINKFLAGS':    ['/OPT:REF']
    }
  }
}


V8_EXTRA_FLAGS = {
  'gcc': {
    'all': {
      'CXXFLAGS':     ['-fvisibility=hidden'],
      'WARNINGFLAGS': ['-pedantic', '-Wall', '-Werror', '-W',
          '-Wno-unused-parameter']
    },
  },
  'msvc': {
    'all': {
      'WARNINGFLAGS': ['/W3', '/WX', '/wd4355', '/wd4800']
    },
    'library:shared': {
      'CPPDEFINES':   ['BUILDING_V8_SHARED']
    },
  }
}


JSCRE_EXTRA_FLAGS = {
  'gcc': {
    'all': {
      'CPPDEFINES':   ['SUPPORT_UTF8', 'NO_RECURSE', 'SUPPORT_UCP'],
      'WARNINGFLAGS': ['-w']
    },
  },
  'msvc': {
    'all': {
      'CPPDEFINES':   ['SUPPORT_UTF8', 'NO_RECURSE', 'SUPPORT_UCP'],
      'WARNINGFLAGS': ['/W3', '/WX', '/wd4355', '/wd4800']
    },
    'library:shared': {
      'CPPDEFINES':   ['BUILDING_V8_SHARED']
    }
  }
}


DTOA_EXTRA_FLAGS = {
  'gcc': {
    'all': {
      'WARNINGFLAGS': ['-Werror']
    }  
  },
  'msvc': {
    'all': {
      'WARNINGFLAGS': ['/WX', '/wd4018', '/wd4244']
    }
  }
}


CCTEST_EXTRA_FLAGS = {
  'all': {
    'CPPPATH': [join(root_dir, 'src')],
    'LIBS': ['$LIBRARY']
  },
  'gcc': {
    'all': {
      'LIBPATH': [abspath('.')]
    }
  },
  'msvc': {
    'library:shared': {
      'CPPDEFINES': ['USING_V8_SHARED']
    }
  }
}


SAMPLE_FLAGS = {
  'all': {
    'CPPPATH': [join(abspath('.'), 'public')],
    'LIBS': ['$LIBRARY'],
  },
  'gcc': {
    'all': {
      'LIBS': ['pthread'],
      'LIBPATH': ['.']
    },
  },
  'msvc': {
    'all': {
      'CCFLAGS': ['/nologo'],
    },
    'library:shared': {
      'CPPDEFINES': ['USING_V8_SHARED']
    },
    'mode:release': {
      'CCFLAGS': ['/MT'],
    },
    'mode:debug': {
      'CCFLAGS': ['/MTd']
    }
  }
}


SUFFIXES = {
  'release': '',
  'debug': '_g'
}


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
  result.Add('sample', 'build sample (shell, process)', '')
  return result


def SplitList(str):
  return [ s for s in str.split(",") if len(s) > 0 ]


def IsLegal(env, option, values):
  str = env[option]
  for s in SplitList(str):
    if not s in values:
      Abort("Illegal value for option %s '%s'." % (option, s))
      return False
  return True


def VerifyOptions(env):
  if not IsLegal(env, 'mode', ['debug', 'release']):
    return False
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
  if not IsLegal(env, 'sample', ["shell", "process"]):
    return False


class BuildContext(object):

  def __init__(self, os, arch, toolchain, snapshot, library, samples, mode):
    self.library_targets = []
    self.cctest_targets = []
    self.sample_targets = []
    self.os = os
    self.arch = arch
    self.toolchain = toolchain
    self.snapshot = snapshot
    self.library = library
    self.samples = samples
    self.mode = mode
    self.use_snapshot = (snapshot == 'on')
    self.flags = None
  
  def AddRelevantFlags(self, initial, flags):
    result = initial.copy()
    self.AppendFlags(result, flags.get('all'))
    self.AppendFlags(result, flags[self.toolchain].get('all'))
    self.AppendFlags(result, flags[self.toolchain].get('mode:' + self.mode))
    self.AppendFlags(result, flags[self.toolchain].get('library:' + self.library))
    return result
  
  def GetRelevantSources(self, source):
    result = []
    result += source.get('all', [])
    result += source.get('arch:' + self.arch, [])
    result += source.get('os:' + self.os, [])
    result += source.get('mode:' + self.mode, [])
    return result 

  def AppendFlags(self, options, added):
    if not added:
      return
    for (key, value) in added.items():
      if not key in options:
        options[key] = value
      else:
        options[key] = options[key] + value
  
  def ConfigureObject(self, env, input, **kw):
    if self.library == 'static':
      return env.StaticObject(input, **kw)
    elif self.library == 'shared':
      return env.SharedObject(input, **kw)
    else:
      return env.Object(input, **kw)


def BuildSpecific(env, mode):
  context = BuildContext(os=env['os'], arch=env['processor'],
      toolchain=env['toolchain'], snapshot=env['snapshot'],
      library=env['library'], samples=SplitList(env['sample']),
      mode=mode)

  library_flags = context.AddRelevantFlags({}, LIBRARY_FLAGS)
  v8_flags = context.AddRelevantFlags(library_flags, V8_EXTRA_FLAGS)
  jscre_flags = context.AddRelevantFlags(library_flags, JSCRE_EXTRA_FLAGS)
  dtoa_flags = context.AddRelevantFlags(library_flags, DTOA_EXTRA_FLAGS)
  cctest_flags = context.AddRelevantFlags(v8_flags, CCTEST_EXTRA_FLAGS)  
  sample_flags = context.AddRelevantFlags({}, SAMPLE_FLAGS)

  context.flags = {
    'v8': v8_flags,
    'jscre': jscre_flags,
    'dtoa': dtoa_flags,
    'cctest': cctest_flags,
    'sample': sample_flags
  }
  
  target_id = mode
  suffix = SUFFIXES[target_id]
  library_name = 'v8' + suffix
  env['LIBRARY'] = library_name

  # Build the object files by invoking SCons recursively.  
  object_files = env.SConscript(
    join('src', 'SConscript'),
    build_dir=join('obj', target_id),
    exports='context',
    duplicate=False
  )
  
  # Link the object files into a library.
  if context.library == 'static':
    library = env.StaticLibrary(library_name, object_files)
  elif context.library == 'shared':
    # There seems to be a glitch in the way scons decides where to put
    # PDB files when compiling using MSVC so we specify it manually.
    # This should not affect any other platforms.
    pdb_name = library_name + '.dll.pdb'
    library = env.SharedLibrary(library_name, object_files, PDB=pdb_name)
  else:
    library = env.Library(library_name, object_files)
  context.library_targets.append(library)
  
  for sample in context.samples:
    sample_env = Environment(LIBRARY=library_name)
    sample_env.Replace(**context.flags['sample'])
    sample_object = sample_env.SConscript(
      join('samples', 'SConscript'),
      build_dir=join('obj', 'sample', sample, target_id),
      exports='sample context',
      duplicate=False
    )
    sample_name = sample + suffix
    sample_program = sample_env.Program(sample_name, sample_object)
    sample_env.Depends(sample_program, library)
    context.sample_targets.append(sample_program)
  
  cctest_program = env.SConscript(
    join('test', 'cctest', 'SConscript'),
    build_dir=join('obj', 'test', target_id),
    exports='context object_files',
    duplicate=False
  )
  context.cctest_targets.append(cctest_program)
  
  return context


def Build():
  opts = GetOptions()
  env = Environment(options=opts)
  Help(opts.GenerateHelpText(env))
  VerifyOptions(env)
  
  libraries = []
  cctests = []
  samples = []
  modes = SplitList(env['mode'])
  for mode in modes:
    context = BuildSpecific(env.Copy(), mode)
    libraries += context.library_targets
    cctests += context.cctest_targets
    samples += context.sample_targets

  env.Alias('library', libraries)
  env.Alias('cctests', cctests)
  env.Alias('sample', samples)
  
  if env['sample']:
    env.Default('sample')
  else:
    env.Default('library')


Build()
