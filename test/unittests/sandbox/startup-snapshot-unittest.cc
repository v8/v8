// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <vector>

#include "src/base/memory.h"
#include "src/base/numerics/safe_conversions.h"
#include "src/base/vector.h"
#include "src/execution/isolate.h"
#include "src/roots/roots.h"
#include "src/snapshot/serializer-deserializer.h"
#include "src/snapshot/snapshot-data.h"
#include "src/snapshot/snapshot-source-sink.h"
#include "src/snapshot/startup-deserializer.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

#ifdef V8_ENABLE_SANDBOX

namespace v8::internal {

// In official builds we drop the message in CHECK so we cannot check for it in
// these tests.
#ifdef OFFICIAL_BUILD
#define EXPECTED_CHECK_WITH_MSG(msg) ""
#else
#define EXPECTED_CHECK_WITH_MSG(msg) msg
#endif

namespace {

struct SnapshotBytecodes : public SerializerDeserializer {
  using SerializerDeserializer::Bytecode;
  using enum Bytecode;
  using SerializerDeserializer::IsTrustedBytecode;
  using SerializerDeserializer::NewObject;
};

void Pad(SnapshotByteSink* sink) {
  for (int i = 0; i < 3; i++) sink->Put(SnapshotBytecodes::kNop, "Padding");
  while (!IsAligned(sink->Position(), kPointerAlignment)) {
    sink->Put(SnapshotBytecodes::kNop, "Padding");
  }
}

std::vector<uint8_t> MakeStartupSnapshot(SnapshotByteSink* untrusted) {
  SnapshotByteSink trusted;
  trusted.PutUint30(0, "ExternalReferenceCount");
  trusted.Put(SnapshotBytecodes::kSynchronize, "EndOfTrustedExports");
  Pad(&trusted);
  Pad(untrusted);

  constexpr uint32_t kHeaderSize =
      SnapshotData::kTrustedPayloadLengthOffset + kUInt32Size;
  const uint32_t trusted_length =
      base::checked_cast<uint32_t>(trusted.data()->size());
  const uint32_t payload_length =
      trusted_length + base::checked_cast<uint32_t>(untrusted->data()->size());
  std::vector<uint8_t> bytes(kHeaderSize + payload_length);
  auto set_header = [&bytes](uint32_t offset, uint32_t value) {
    base::WriteLittleEndianValue<uint32_t>(
        reinterpret_cast<Address>(bytes.data() + offset), value);
  };
  set_header(SnapshotData::kMagicNumberOffset, SnapshotData::kMagicNumber);
  set_header(SnapshotData::kPayloadLengthOffset, payload_length);
  set_header(SnapshotData::kTrustedPayloadLengthOffset, trusted_length);
  std::copy(trusted.data()->begin(), trusted.data()->end(),
            bytes.begin() + kHeaderSize);
  std::copy(untrusted->data()->begin(), untrusted->data()->end(),
            bytes.begin() + kHeaderSize + trusted_length);
  return bytes;
}

}  // namespace

// Avoid WithIsolateScopeMixin so no HandleScope is open when
// StartupDeserializer::DeserializeIntoIsolate checks for empty handle blocks.
class StartupSnapshotUntrustedSectionTest
    : public v8::WithIsolateMixin<v8::TestWithPlatform> {
 protected:
  Isolate* i_isolate() const {
    return reinterpret_cast<Isolate*>(v8_isolate());
  }

  void Deserialize(SnapshotByteSink* untrusted) {
    std::vector<uint8_t> bytes = MakeStartupSnapshot(untrusted);
    SnapshotData snapshot(base::VectorOf(bytes));
    v8::Isolate::Scope isolate_scope(v8_isolate());
    i_isolate()->startup_object_cache()->clear();
    StartupDeserializer deserializer(i_isolate(), &snapshot,
                                     /*can_rehash=*/false);
    deserializer.DeserializeIntoIsolate();
  }
};

TEST_F(StartupSnapshotUntrustedSectionTest, TrustedBytecodes) {
  for (int i = 0; i <= kMaxUInt8; i++) {
    SnapshotBytecodes::Bytecode bytecode =
        static_cast<SnapshotBytecodes::Bytecode>(i);
    if (!SnapshotBytecodes::IsTrustedBytecode(bytecode)) continue;
    SnapshotByteSink untrusted;
    untrusted.Put(bytecode, "");
    ASSERT_DEATH_IF_SUPPORTED(Deserialize(&untrusted),
                              EXPECTED_CHECK_WITH_MSG("InTrustedSection"));
  }
}

TEST_F(StartupSnapshotUntrustedSectionTest, DisallowedSpaces) {
  for (SnapshotSpace space : {SnapshotSpace::kTrusted, SnapshotSpace::kCode}) {
    SnapshotByteSink object;
    object.Put(SnapshotBytecodes::NewObject::Encode(space), "");
    object.Put(SnapshotBytecodes::kRootArray, "");
    object.PutUint30(static_cast<uint32_t>(RootIndex::kFixedArrayMap), "");
    object.PutUint30(2, "");
    ASSERT_DEATH_IF_SUPPORTED(Deserialize(&object),
                              EXPECTED_CHECK_WITH_MSG("AllocationType::kOld"));
  }
  SnapshotByteSink meta_map;
  meta_map.Put(SnapshotBytecodes::kNewContextlessMetaMap, "");
  meta_map.PutUint30(MAP_TYPE, "");
  ASSERT_DEATH_IF_SUPPORTED(Deserialize(&meta_map),
                            EXPECTED_CHECK_WITH_MSG("AllocationType::kOld"));
}

TEST_F(StartupSnapshotUntrustedSectionTest, TrustedAndSmiRoots) {
  for (RootIndex root :
       {RootIndex::kEmptyTrustedByteArray, RootIndex::kLastScriptId}) {
    SnapshotByteSink untrusted;
    untrusted.Put(SnapshotBytecodes::kRootArray, "");
    untrusted.PutUint30(static_cast<uint32_t>(root), "");
    ASSERT_DEATH_IF_SUPPORTED(Deserialize(&untrusted),
                              EXPECTED_CHECK_WITH_MSG("OutsideSandbox"));
  }
}

}  // namespace v8::internal

#endif  // V8_ENABLE_SANDBOX
