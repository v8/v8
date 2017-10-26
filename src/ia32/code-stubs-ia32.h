// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IA32_CODE_STUBS_IA32_H_
#define V8_IA32_CODE_STUBS_IA32_H_

namespace v8 {
namespace internal {


class NameDictionaryLookupStub: public PlatformCodeStub {
 public:
  NameDictionaryLookupStub(Isolate* isolate, Register dictionary,
                           Register result, Register index)
      : PlatformCodeStub(isolate) {
    minor_key_ = DictionaryBits::encode(dictionary.code()) |
                 ResultBits::encode(result.code()) |
                 IndexBits::encode(index.code());
  }

  static void GenerateNegativeLookup(MacroAssembler* masm,
                                     Label* miss,
                                     Label* done,
                                     Register properties,
                                     Handle<Name> name,
                                     Register r0);

  bool SometimesSetsUpAFrame() override { return false; }

 private:
  static const int kInlinedProbes = 4;
  static const int kTotalProbes = 20;

  static const int kCapacityOffset =
      NameDictionary::kHeaderSize +
      NameDictionary::kCapacityIndex * kPointerSize;

  static const int kElementsStartOffset =
      NameDictionary::kHeaderSize +
      NameDictionary::kElementsStartIndex * kPointerSize;

  Register dictionary() const {
    return Register::from_code(DictionaryBits::decode(minor_key_));
  }

  Register result() const {
    return Register::from_code(ResultBits::decode(minor_key_));
  }

  Register index() const {
    return Register::from_code(IndexBits::decode(minor_key_));
  }

  class DictionaryBits: public BitField<int, 0, 3> {};
  class ResultBits: public BitField<int, 3, 3> {};
  class IndexBits: public BitField<int, 6, 3> {};

  DEFINE_NULL_CALL_INTERFACE_DESCRIPTOR();
  DEFINE_PLATFORM_CODE_STUB(NameDictionaryLookup, PlatformCodeStub);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_IA32_CODE_STUBS_IA32_H_
