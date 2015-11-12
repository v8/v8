// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-proxies --harmony-object-observe

// TODO(neis): These tests are temporarily commented out because of ongoing
// changes to the implementation of proxies.

//var proxy = Proxy.create({ fix: function() { return {}; } });
//Object.preventExtensions(proxy);
//Object.observe(proxy, function(){});
//
//var functionProxy = Proxy.createFunction({ fix: function() { return {}; } }, function(){});
//Object.preventExtensions(functionProxy);
//Object.observe(functionProxy, function(){});
