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
import os
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
      'CCFLAGS':      ['$DIALECTFLAGS', '$WARNINGFLAGS',
          '-fno-strict-aliasing'],
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
    'wordsize:64': {
      'CCFLAGS':      ['-m32']
    },
  },
  'msvc': {
    'all': {
      'DIALECTFLAGS': ['/nologo'],
      'WARNINGFLAGS': ['/W3', '/WX', '/wd4355', '/wd4800'],
      'CCFLAGS':      ['$DIALECTFLAGS', '$WARNINGFLAGS'],
      'CXXFLAGS':     ['$CCFLAGS', '/GR-', '/Gy'],
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
      'LINKFLAGS':    ['/OPT:REF', '/OPT:ICF']
    }
  }
}


V8_EXTRA_FLAGS = {
  'gcc': {
    'all': {
      'CXXFLAGS':     [], #['-fvisibility=hidden'],
      'WARNINGFLAGS': ['-pedantic', '-Wall', '-Werror', '-W',
          '-Wno-unused-parameter']
    },
    'arch:arm': {
      'CPPDEFINES':   ['ARM']
    },
  },
  'msvc': {
    'all': {
      'WARNINGFLAGS': ['/W3', '/WX', '/wd4355', '/wd4800']
    },
    'library:shared': {
      'CPPDEFINES':   ['BUILDING_V8_SHARED']
    },
    'arch:arm': {
      'CPPDEFINES':   ['ARM']
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
    },
    'wordsize:64': {
      'CCFLAGS':      ['-m32'],
      'LINKFLAGS':    ['-m32']
    },
  },
  'msvc': {
    'all': {
      'CPPDEFINES': ['_HAS_EXCEPTIONS=0']
    },
    'library:shared': {
      'CPPDEFINES': ['USING_V8_SHARED']
    }
  }
}


SAMPLE_FLAGS = {
  'all': {
    'CPPPATH': [join(abspath('.'), 'include')],
    'LIBS': ['$LIBRARY'],
  },
  'gcc': {
    'all': {
      'LIBS': ['pthread'],
      'LIBPATH': ['.']
    },
    'wordsize:64': {
      'CCFLAGS':      ['-m32'],
      'LINKFLAGS':    ['-m32']
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
    return None


def GuessArchitecture():
  id = platform.machine()
  if id.startswith('arm'):
    return 'arm'
  elif (not id) or (not re.match('(x|i[3-6])86', id) is None):
    return 'ia32'
  else:
    return None


def GuessWordsize():
  if '64' in platform.machine():
    return '64'
  else:
    return '32'


def GuessToolchain(os):
  tools = Environment()['TOOLS']
  if 'gcc' in tools:
    return 'gcc'
  elif 'msvc' in tools:
    return 'msvc'
  else:
    return None


OS_GUESS = GuessOS()
TOOLCHAIN_GUESS = GuessToolchain(OS_GUESS)
ARCH_GUESS = GuessArchitecture()
WORDSIZE_GUESS = GuessWordsize()


SIMPLE_OPTIONS = {
  'toolchain': {
    'values': ['gcc', 'msvc'],
    'default': TOOLCHAIN_GUESS,
    'help': 'the toolchain to use'
  },
  'os': {
    'values': ['linux', 'macos', 'win32'],
    'default': OS_GUESS,
    'help': 'the os to build for'
  },
  'arch': {
    'values':['arm', 'ia32'],
    'default': ARCH_GUESS,
    'help': 'the architecture to build for'
  },
  'snapshot': {
    'values': ['on', 'off'],
    'default': 'off',
    'help': 'build using snapshots for faster start-up'
  },
  'library': {
    'values': ['static', 'shared'],
    'default': 'static',
    'help': 'the type of library to produce'
  },
  'wordsize': {
    'values': ['64', '32'],
    'default': WORDSIZE_GUESS,
    'help': 'the word size'
  },
  'simulator': {
    'values': ['arm', 'none'],
    'default': 'none',
    'help': 'build with simulator'
  }
}


def GetOptions():
  result = Options()
  result.Add('mode', 'compilation mode (debug, release)', 'release')
  result.Add('sample', 'build sample (shell, process)', '')
  for (name, option) in SIMPLE_OPTIONS.items():
    help = '%s (%s)' % (name, ", ".join(option['values']))
    result.Add(name, help, option.get('default'))
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
  if not IsLegal(env, 'sample', ["shell", "process"]):
    return False
  for (name, option) in SIMPLE_OPTIONS.items():
    if (not option.get('default')) and (name not in ARGUMENTS):
      message = ("A value for option %s must be specified (%s)." %
          (name, ", ".join(option['values'])))
      Abort(message)
    if not env[name] in option['values']:
      message = ("Unknown %s value '%s'.  Possible values are (%s)." %
          (name, env[name], ", ".join(option['values'])))
      Abort(message)


class BuildContext(object):

  def __init__(self, options, samples):
    self.library_targets = []
    self.cctest_targets = []
    self.sample_targets = []
    self.options = options
    self.samples = samples
    self.use_snapshot = (options['snapshot'] == 'on')
    self.flags = None
  
  def AddRelevantFlags(self, initial, flags):
    result = initial.copy()
    self.AppendFlags(result, flags.get('all'))
    toolchain = self.options['toolchain']
    self.AppendFlags(result, flags[toolchain].get('all'))
    for option in sorted(self.options.keys()):
      value = self.options[option]
      self.AppendFlags(result, flags[toolchain].get(option + ':' + value))
    return result
  
  def GetRelevantSources(self, source):
    result = []
    result += source.get('all', [])
    for (name, value) in self.options.items():
      result += source.get(name + ':' + value, [])
    return sorted(result)

  def AppendFlags(self, options, added):
    if not added:
      return
    for (key, value) in added.items():
      if not key in options:
        options[key] = value
      else:
        options[key] = options[key] + value

  def ConfigureObject(self, env, input, **kw):
    if self.options['library'] == 'static':
      return env.StaticObject(input, **kw)
    else:
      return env.SharedObject(input, **kw)


def PostprocessOptions(options):
  # Adjust architecture if the simulator option has been set
  if (options['simulator'] != 'none') and (options['arch'] != options['simulator']):
    if 'arch' in ARGUMENTS:
      # Print a warning if arch has explicitly been set
      print "Warning: forcing architecture to match simulator (%s)" % options['simulator']
    options['arch'] = options['simulator']


def BuildSpecific(env, mode):
  options = {'mode': mode}
  for option in SIMPLE_OPTIONS:
    options[option] = env[option]
  PostprocessOptions(options)

  context = BuildContext(options, samples=SplitList(env['sample']))

  library_flags = context.AddRelevantFlags(os.environ, LIBRARY_FLAGS)
  v8_flags = context.AddRelevantFlags(library_flags, V8_EXTRA_FLAGS)
  jscre_flags = context.AddRelevantFlags(library_flags, JSCRE_EXTRA_FLAGS)
  dtoa_flags = context.AddRelevantFlags(library_flags, DTOA_EXTRA_FLAGS)
  cctest_flags = context.AddRelevantFlags(v8_flags, CCTEST_EXTRA_FLAGS)  
  sample_flags = context.AddRelevantFlags(os.environ, SAMPLE_FLAGS)

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
  if context.options['library'] == 'static':
    library = env.StaticLibrary(library_name, object_files)
  else:
    # There seems to be a glitch in the way scons decides where to put
    # PDB files when compiling using MSVC so we specify it manually.
    # This should not affect any other platforms.
    pdb_name = library_name + '.dll.pdb'
    library = env.SharedLibrary(library_name, object_files, PDB=pdb_name)
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


# We disable deprecation warnings because we need to be able to use
# env.Copy without getting warnings for compatibility with older
# version of scons.  Also, there's a bug in some revisions that
# doesn't allow this flag to be set, so we swallow any exceptions.
# Lovely.
try:
  SetOption('warn', 'no-deprecated')
except:
  pass


Build()
