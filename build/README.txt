This directory contains the V8 GYP files used to generate actual project files
for different build systems.

This is currently work in progress but this is expected to replace the SCons
based build system.

To use this a checkout of GYP is needed inside this directory. From the root of
the V8 project do the following:

$ svn co http://gyp.googlecode.com/svn/trunk build/gyp

To generate Makefiles and build 32-bit version on Linux:
--------------------------------------------------------

$ GYP_DEFINES=target_arch=ia32 build/gyp_v8
$ make

To generate Makefiles and build 64-bit version on Linux:
--------------------------------------------------------

$ GYP_DEFINES=target_arch=x64 build/gyp_v8
$ make

To generate Makefiles and build for the arm simulator on Linux:
---------------------------------------------------------------

$ build/gyp_v8 -I build/arm.gypi
$ make

To generate Visual Studio solution and project files on Windows:
----------------------------------------------------------------

On Windows an additional third party component is required. This is cygwin in
the same version as is used by the Chromium project. This can be checked out
from the Chromium repository. From the root of the V8 project do the following:

> svn co http://src.chromium.org/svn/trunk/deps/third_party/cygwin@66844 third_party/cygwin

To run GYP Python is required and it is reccomended to use the same version as
is used by the Chromium project. This can also be checked out from the Chromium
repository. From the root of the V8 project do the following:

> svn co http://src.chromium.org/svn/trunk/tools/third_party/python_26@70627 third_party/python_26

Now generate Visual Studio solution and project files:

> third_party\python_26\python build/gyp_v8 -D target_arch=ia32

Now open build\All.sln in Visual Studio.
