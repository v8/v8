// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_URI_H_
#define V8_URI_H_

#include "src/allocation.h"
#include "src/objects.h"

namespace v8 {
namespace internal {

class Uri : public AllStatic {
 public:
  // ES6 section 18.2.6.2 decodeURI (encodedURI)
  static MaybeHandle<String> DecodeUri(Isolate* isolate, Handle<String> uri) {
    return Decode(isolate, uri, true);
  }

  // ES6 section 18.2.6.3 decodeURIComponent (encodedURIComponent)
  static MaybeHandle<String> DecodeUriComponent(Isolate* isolate,
                                                Handle<String> component) {
    return Decode(isolate, component, false);
  }

  // ES6 section 18.2.6.4 encodeURI (uri)
  static MaybeHandle<String> EncodeUri(Isolate* isolate, Handle<String> uri) {
    return Encode(isolate, uri, true);
  }

  // ES6 section 18.2.6.5 encodeURIComponenet (uriComponent)
  static MaybeHandle<String> EncodeUriComponent(Isolate* isolate,
                                                Handle<String> component) {
    return Encode(isolate, component, false);
  }

 private:
  static MaybeHandle<String> Decode(Isolate* isolate, Handle<String> uri,
                                    bool is_uri);
  static MaybeHandle<String> Encode(Isolate* isolate, Handle<String> uri,
                                    bool is_uri);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_URI_H_
