This directory contains the V8 GYP files used to generate actual project files
for different build systems.

This is currently work in progress but this is expected to replace the SCons
based build system.

To use this a checkout of GYP is needed inside this directory. From the root of
the V8 project do the following

$ svn co http://gyp.googlecode.com/svn/trunk build/gyp

To generate Makefiles and build 32-bit version on Linux:

$ GYP_DEFINES=target_arch=ia32 build/gyp_v8
$ make

To generate Makefiles and build 64-bit version on Linux:

$ GYP_DEFINES=target_arch=x64 build/gyp_v8
$ make

To generate Makefiles and build for the arm simulator on Linux:

$ build/gyp_v8 -I build/arm.gypi
$ make
