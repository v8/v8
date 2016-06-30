// The following is adapted from fdlibm (http://www.netlib.org/fdlibm),
//
// ====================================================
// Copyright (C) 1993-2004 by Sun Microsystems, Inc. All rights reserved.
//
// Developed at SunSoft, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this
// software is freely granted, provided that this notice
// is preserved.
// ====================================================
//
// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2014 the V8 project authors. All rights reserved.
//
// The following is a straightforward translation of fdlibm routines
// by Raymond Toy (rtoy@google.com).

(function(global, utils) {
  
"use strict";

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

var GlobalMath = global.Math;

utils.Import(function(from) {});

//-------------------------------------------------------------------

utils.InstallFunctions(GlobalMath, DONT_ENUM, []);

})
