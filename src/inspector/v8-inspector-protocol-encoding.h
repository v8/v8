// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_V8_INSPECTOR_PROTOCOL_ENCODING_H_
#define V8_INSPECTOR_V8_INSPECTOR_PROTOCOL_ENCODING_H_

#include "../../third_party/inspector_protocol/crdtp/span.h"
#include "../../third_party/inspector_protocol/crdtp/status.h"

namespace v8_inspector {

v8_crdtp::Status ConvertCBORToJSON(v8_crdtp::span<uint8_t> cbor,
                                   std::vector<uint8_t>* json);

v8_crdtp::Status ConvertJSONToCBOR(v8_crdtp::span<uint8_t> json,
                                   std::vector<uint8_t>* cbor);

v8_crdtp::Status ConvertJSONToCBOR(v8_crdtp::span<uint16_t> json,
                                   std::vector<uint8_t>* cbor);

}  // namespace v8_inspector

#endif  // V8_INSPECTOR_V8_INSPECTOR_PROTOCOL_ENCODING_H_
