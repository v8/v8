// Copyright 2007-2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// This file was generated at 2010-07-29 14:28:54.987073

#include "unicode-inl.h"
#include <stdlib.h>
#include <stdio.h>

namespace unibrow {

static const int kStartBit = (1 << 30);
static const int kChunkBits = (1 << 15);

/**
 * \file
 * Implementations of functions for working with unicode.
 */

typedef signed short int16_t;  // NOLINT
typedef unsigned short uint16_t;  // NOLINT
typedef int int32_t;  // NOLINT

// All access to the character table should go through this function.
template <int D>
static inline uchar TableGet(const int32_t* table, int index) {
  return table[D * index];
}

static inline uchar GetEntry(int32_t entry) {
  return entry & (kStartBit - 1);
}

static inline bool IsStart(int32_t entry) {
  return (entry & kStartBit) != 0;
}

/**
 * Look up a character in the unicode table using a mix of binary and
 * interpolation search.  For a uniformly distributed array
 * interpolation search beats binary search by a wide margin.  However,
 * in this case interpolation search degenerates because of some very
 * high values in the lower end of the table so this function uses a
 * combination.  The average number of steps to look up the information
 * about a character is around 10, slightly higher if there is no
 * information available about the character.
 */
static bool LookupPredicate(const int32_t* table, uint16_t size, uchar chr) {
  static const int kEntryDist = 1;
  uint16_t value = chr & (kChunkBits - 1);
  unsigned int low = 0;
  unsigned int high = size - 1;
  while (high != low) {
    unsigned int mid = low + ((high - low) >> 1);
    uchar current_value = GetEntry(TableGet<kEntryDist>(table, mid));
    // If we've found an entry less than or equal to this one, and the
    // next one is not also less than this one, we've arrived.
    if ((current_value <= value) &&
        (mid + 1 == size ||
         GetEntry(TableGet<kEntryDist>(table, mid + 1)) > value)) {
      low = mid;
      break;
    } else if (current_value < value) {
      low = mid + 1;
    } else if (current_value > value) {
      // If we've just checked the bottom-most value and it's not
      // the one we're looking for, we're done.
      if (mid == 0) break;
      high = mid - 1;
    }
  }
  int32_t field = TableGet<kEntryDist>(table, low);
  uchar entry = GetEntry(field);
  bool is_start = IsStart(field);
  return (entry == value) ||
          (entry < value && is_start);
}

template <int kW>
struct MultiCharacterSpecialCase {
  uint16_t length;
  uchar chars[kW];
};

// Look up the mapping for the given character in the specified table,
// which is of the specified length and uses the specified special case
// mapping for multi-char mappings.  The next parameter is the character
// following the one to map.  The result will be written in to the result
// buffer and the number of characters written will be returned.  Finally,
// if the allow_caching_ptr is non-null then false will be stored in
// it if the result contains multiple characters or depends on the
// context.
template <int kW>
static int LookupMapping(const int32_t* table,
                         uint16_t size,
                         const MultiCharacterSpecialCase<kW>* multi_chars,
                         uchar chr,
                         uchar next,
                         uchar* result,
                         bool* allow_caching_ptr) {
  static const int kEntryDist = 2;
  uint16_t value = chr & (kChunkBits - 1);
  unsigned int low = 0;
  unsigned int high = size - 1;
  while (high != low) {
    unsigned int mid = low + ((high - low) >> 1);
    uchar current_value = GetEntry(TableGet<kEntryDist>(table, mid));
    // If we've found an entry less than or equal to this one, and the next one
    // is not also less than this one, we've arrived.
    if ((current_value <= value) &&
        (mid + 1 == size ||
         GetEntry(TableGet<kEntryDist>(table, mid + 1)) > value)) {
      low = mid;
      break;
    } else if (current_value < value) {
      low = mid + 1;
    } else if (current_value > value) {
      // If we've just checked the bottom-most value and it's not
      // the one we're looking for, we're done.
      if (mid == 0) break;
      high = mid - 1;
    }
  }
  int32_t field = TableGet<kEntryDist>(table, low);
  uchar entry = GetEntry(field);
  bool is_start = IsStart(field);
  bool found = (entry == value) || (entry < value && is_start);
  if (found) {
    int32_t value = table[2 * low + 1];
    if (value == 0) {
      // 0 means not present
      return 0;
    } else if ((value & 3) == 0) {
      // Low bits 0 means a constant offset from the given character.
      result[0] = chr + (value >> 2);
      return 1;
    } else if ((value & 3) == 1) {
      // Low bits 1 means a special case mapping
      if (allow_caching_ptr) *allow_caching_ptr = false;
      const MultiCharacterSpecialCase<kW>& mapping = multi_chars[value >> 2];
      for (int i = 0; i < mapping.length; i++)
        result[i] = mapping.chars[i];
      return mapping.length;
    } else {
      // Low bits 2 means a really really special case
      if (allow_caching_ptr) *allow_caching_ptr = false;
      // The cases of this switch are defined in unicode.py in the
      // really_special_cases mapping.
      switch (value >> 2) {
        case 1:
          // Really special case 1: upper case sigma.  This letter
          // converts to two different lower case sigmas depending on
          // whether or not it occurs at the end of a word.
          if (next != 0 && Letter::Is(next)) {
            result[0] = 0x03C3;
          } else {
            result[0] = 0x03C2;
          }
          return 1;
        default:
          return 0;
      }
      return -1;
    }
  } else {
    return 0;
  }
}

uchar Utf8::CalculateValue(const byte* str,
                           unsigned length,
                           unsigned* cursor) {
  // We only get called for non-ascii characters.
  if (length == 1) {
    *cursor += 1;
    return kBadChar;
  }
  byte first = str[0];
  byte second = str[1] ^ 0x80;
  if (second & 0xC0) {
    *cursor += 1;
    return kBadChar;
  }
  if (first < 0xE0) {
    if (first < 0xC0) {
      *cursor += 1;
      return kBadChar;
    }
    uchar l = ((first << 6) | second) & kMaxTwoByteChar;
    if (l <= kMaxOneByteChar) {
      *cursor += 1;
      return kBadChar;
    }
    *cursor += 2;
    return l;
  }
  if (length == 2) {
    *cursor += 1;
    return kBadChar;
  }
  byte third = str[2] ^ 0x80;
  if (third & 0xC0) {
    *cursor += 1;
    return kBadChar;
  }
  if (first < 0xF0) {
    uchar l = ((((first << 6) | second) << 6) | third) & kMaxThreeByteChar;
    if (l <= kMaxTwoByteChar) {
      *cursor += 1;
      return kBadChar;
    }
    *cursor += 3;
    return l;
  }
  if (length == 3) {
    *cursor += 1;
    return kBadChar;
  }
  byte fourth = str[3] ^ 0x80;
  if (fourth & 0xC0) {
    *cursor += 1;
    return kBadChar;
  }
  if (first < 0xF8) {
    uchar l = (((((first << 6 | second) << 6) | third) << 6) | fourth) &
              kMaxFourByteChar;
    if (l <= kMaxThreeByteChar) {
      *cursor += 1;
      return kBadChar;
    }
    *cursor += 4;
    return l;
  }
  *cursor += 1;
  return kBadChar;
}

const byte* Utf8::ReadBlock(Buffer<const char*> str, byte* buffer,
    unsigned capacity, unsigned* chars_read_ptr, unsigned* offset_ptr) {
  unsigned offset = *offset_ptr;
  // Bail out early if we've reached the end of the string.
  if (offset == str.length()) {
    *chars_read_ptr = 0;
    return NULL;
  }
  const byte* data = reinterpret_cast<const byte*>(str.data());
  if (data[offset] <= kMaxOneByteChar) {
    // The next character is an ascii char so we scan forward over
    // the following ascii characters and return the next pure ascii
    // substring
    const byte* result = data + offset;
    offset++;
    while ((offset < str.length()) && (data[offset] <= kMaxOneByteChar))
      offset++;
    *chars_read_ptr = offset - *offset_ptr;
    *offset_ptr = offset;
    return result;
  } else {
    // The next character is non-ascii so we just fill the buffer
    unsigned cursor = 0;
    unsigned chars_read = 0;
    while (offset < str.length()) {
      uchar c = data[offset];
      if (c <= kMaxOneByteChar) {
        // Fast case for ascii characters
        if (!CharacterStream::EncodeAsciiCharacter(c,
                                                   buffer,
                                                   capacity,
                                                   cursor))
          break;
        offset += 1;
      } else {
        unsigned chars = 0;
        c = Utf8::ValueOf(data + offset, str.length() - offset, &chars);
        if (!CharacterStream::EncodeNonAsciiCharacter(c,
                                                      buffer,
                                                      capacity,
                                                      cursor))
          break;
        offset += chars;
      }
      chars_read++;
    }
    *offset_ptr = offset;
    *chars_read_ptr = chars_read;
    return buffer;
  }
}

unsigned CharacterStream::Length() {
  unsigned result = 0;
  while (has_more()) {
    result++;
    GetNext();
  }
  Rewind();
  return result;
}

void CharacterStream::Seek(unsigned position) {
  Rewind();
  for (unsigned i = 0; i < position; i++) {
    GetNext();
  }
}

// Uppercase:            point.category == 'Lu'

static const uint16_t kUppercaseTable0Size = 509;
static const int32_t kUppercaseTable0[509] = {
  1073741889, 90, 1073742016, 214, 1073742040, 222, 256, 258,  // NOLINT
  260, 262, 264, 266, 268, 270, 272, 274,  // NOLINT
  276, 278, 280, 282, 284, 286, 288, 290,  // NOLINT
  292, 294, 296, 298, 300, 302, 304, 306,  // NOLINT
  308, 310, 313, 315, 317, 319, 321, 323,  // NOLINT
  325, 327, 330, 332, 334, 336, 338, 340,  // NOLINT
  342, 344, 346, 348, 350, 352, 354, 356,  // NOLINT
  358, 360, 362, 364, 366, 368, 370, 372,  // NOLINT
  374, 1073742200, 377, 379, 381, 1073742209, 386, 388,  // NOLINT
  1073742214, 391, 1073742217, 395, 1073742222, 401, 1073742227, 404,  // NOLINT
  1073742230, 408, 1073742236, 413, 1073742239, 416, 418, 420,  // NOLINT
  1073742246, 423, 425, 428, 1073742254, 431, 1073742257, 435,  // NOLINT
  437, 1073742263, 440, 444, 452, 455, 458, 461,  // NOLINT
  463, 465, 467, 469, 471, 473, 475, 478,  // NOLINT
  480, 482, 484, 486, 488, 490, 492, 494,  // NOLINT
  497, 500, 1073742326, 504, 506, 508, 510, 512,  // NOLINT
  514, 516, 518, 520, 522, 524, 526, 528,  // NOLINT
  530, 532, 534, 536, 538, 540, 542, 544,  // NOLINT
  546, 548, 550, 552, 554, 556, 558, 560,  // NOLINT
  562, 1073742394, 571, 1073742397, 574, 577, 1073742403, 582,  // NOLINT
  584, 586, 588, 590, 902, 1073742728, 906, 908,  // NOLINT
  1073742734, 911, 1073742737, 929, 1073742755, 939, 1073742802, 980,  // NOLINT
  984, 986, 988, 990, 992, 994, 996, 998,  // NOLINT
  1000, 1002, 1004, 1006, 1012, 1015, 1073742841, 1018,  // NOLINT
  1073742845, 1071, 1120, 1122, 1124, 1126, 1128, 1130,  // NOLINT
  1132, 1134, 1136, 1138, 1140, 1142, 1144, 1146,  // NOLINT
  1148, 1150, 1152, 1162, 1164, 1166, 1168, 1170,  // NOLINT
  1172, 1174, 1176, 1178, 1180, 1182, 1184, 1186,  // NOLINT
  1188, 1190, 1192, 1194, 1196, 1198, 1200, 1202,  // NOLINT
  1204, 1206, 1208, 1210, 1212, 1214, 1073743040, 1217,  // NOLINT
  1219, 1221, 1223, 1225, 1227, 1229, 1232, 1234,  // NOLINT
  1236, 1238, 1240, 1242, 1244, 1246, 1248, 1250,  // NOLINT
  1252, 1254, 1256, 1258, 1260, 1262, 1264, 1266,  // NOLINT
  1268, 1270, 1272, 1274, 1276, 1278, 1280, 1282,  // NOLINT
  1284, 1286, 1288, 1290, 1292, 1294, 1296, 1298,  // NOLINT
  1073743153, 1366, 1073746080, 4293, 7680, 7682, 7684, 7686,  // NOLINT
  7688, 7690, 7692, 7694, 7696, 7698, 7700, 7702,  // NOLINT
  7704, 7706, 7708, 7710, 7712, 7714, 7716, 7718,  // NOLINT
  7720, 7722, 7724, 7726, 7728, 7730, 7732, 7734,  // NOLINT
  7736, 7738, 7740, 7742, 7744, 7746, 7748, 7750,  // NOLINT
  7752, 7754, 7756, 7758, 7760, 7762, 7764, 7766,  // NOLINT
  7768, 7770, 7772, 7774, 7776, 7778, 7780, 7782,  // NOLINT
  7784, 7786, 7788, 7790, 7792, 7794, 7796, 7798,  // NOLINT
  7800, 7802, 7804, 7806, 7808, 7810, 7812, 7814,  // NOLINT
  7816, 7818, 7820, 7822, 7824, 7826, 7828, 7840,  // NOLINT
  7842, 7844, 7846, 7848, 7850, 7852, 7854, 7856,  // NOLINT
  7858, 7860, 7862, 7864, 7866, 7868, 7870, 7872,  // NOLINT
  7874, 7876, 7878, 7880, 7882, 7884, 7886, 7888,  // NOLINT
  7890, 7892, 7894, 7896, 7898, 7900, 7902, 7904,  // NOLINT
  7906, 7908, 7910, 7912, 7914, 7916, 7918, 7920,  // NOLINT
  7922, 7924, 7926, 7928, 1073749768, 7951, 1073749784, 7965,  // NOLINT
  1073749800, 7983, 1073749816, 7999, 1073749832, 8013, 8025, 8027,  // NOLINT
  8029, 8031, 1073749864, 8047, 1073749944, 8123, 1073749960, 8139,  // NOLINT
  1073749976, 8155, 1073749992, 8172, 1073750008, 8187, 8450, 8455,  // NOLINT
  1073750283, 8461, 1073750288, 8466, 8469, 1073750297, 8477, 8484,  // NOLINT
  8486, 8488, 1073750314, 8493, 1073750320, 8499, 1073750334, 8511,  // NOLINT
  8517, 8579, 1073753088, 11310, 11360, 1073753186, 11364, 11367,  // NOLINT
  11369, 11371, 11381, 11392, 11394, 11396, 11398, 11400,  // NOLINT
  11402, 11404, 11406, 11408, 11410, 11412, 11414, 11416,  // NOLINT
  11418, 11420, 11422, 11424, 11426, 11428, 11430, 11432,  // NOLINT
  11434, 11436, 11438, 11440, 11442, 11444, 11446, 11448,  // NOLINT
  11450, 11452, 11454, 11456, 11458, 11460, 11462, 11464,  // NOLINT
  11466, 11468, 11470, 11472, 11474, 11476, 11478, 11480,  // NOLINT
  11482, 11484, 11486, 11488, 11490 };  // NOLINT
static const uint16_t kUppercaseTable1Size = 2;
static const int32_t kUppercaseTable1[2] = {
  1073774369, 32570 };  // NOLINT
bool Uppercase::Is(uchar c) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupPredicate(kUppercaseTable0,
                                       kUppercaseTable0Size,
                                       c);
    case 1: return LookupPredicate(kUppercaseTable1,
                                       kUppercaseTable1Size,
                                       c);
    default: return false;
  }
}

// Lowercase:            point.category == 'Ll'

static const uint16_t kLowercaseTable0Size = 528;
static const int32_t kLowercaseTable0[528] = {
  1073741921, 122, 170, 181, 186, 1073742047, 246, 1073742072,  // NOLINT
  255, 257, 259, 261, 263, 265, 267, 269,  // NOLINT
  271, 273, 275, 277, 279, 281, 283, 285,  // NOLINT
  287, 289, 291, 293, 295, 297, 299, 301,  // NOLINT
  303, 305, 307, 309, 1073742135, 312, 314, 316,  // NOLINT
  318, 320, 322, 324, 326, 1073742152, 329, 331,  // NOLINT
  333, 335, 337, 339, 341, 343, 345, 347,  // NOLINT
  349, 351, 353, 355, 357, 359, 361, 363,  // NOLINT
  365, 367, 369, 371, 373, 375, 378, 380,  // NOLINT
  1073742206, 384, 387, 389, 392, 1073742220, 397, 402,  // NOLINT
  405, 1073742233, 411, 414, 417, 419, 421, 424,  // NOLINT
  1073742250, 427, 429, 432, 436, 438, 1073742265, 442,  // NOLINT
  1073742269, 447, 454, 457, 460, 462, 464, 466,  // NOLINT
  468, 470, 472, 474, 1073742300, 477, 479, 481,  // NOLINT
  483, 485, 487, 489, 491, 493, 1073742319, 496,  // NOLINT
  499, 501, 505, 507, 509, 511, 513, 515,  // NOLINT
  517, 519, 521, 523, 525, 527, 529, 531,  // NOLINT
  533, 535, 537, 539, 541, 543, 545, 547,  // NOLINT
  549, 551, 553, 555, 557, 559, 561, 1073742387,  // NOLINT
  569, 572, 1073742399, 576, 578, 583, 585, 587,  // NOLINT
  589, 1073742415, 659, 1073742485, 687, 1073742715, 893, 912,  // NOLINT
  1073742764, 974, 1073742800, 977, 1073742805, 983, 985, 987,  // NOLINT
  989, 991, 993, 995, 997, 999, 1001, 1003,  // NOLINT
  1005, 1073742831, 1011, 1013, 1016, 1073742843, 1020, 1073742896,  // NOLINT
  1119, 1121, 1123, 1125, 1127, 1129, 1131, 1133,  // NOLINT
  1135, 1137, 1139, 1141, 1143, 1145, 1147, 1149,  // NOLINT
  1151, 1153, 1163, 1165, 1167, 1169, 1171, 1173,  // NOLINT
  1175, 1177, 1179, 1181, 1183, 1185, 1187, 1189,  // NOLINT
  1191, 1193, 1195, 1197, 1199, 1201, 1203, 1205,  // NOLINT
  1207, 1209, 1211, 1213, 1215, 1218, 1220, 1222,  // NOLINT
  1224, 1226, 1228, 1073743054, 1231, 1233, 1235, 1237,  // NOLINT
  1239, 1241, 1243, 1245, 1247, 1249, 1251, 1253,  // NOLINT
  1255, 1257, 1259, 1261, 1263, 1265, 1267, 1269,  // NOLINT
  1271, 1273, 1275, 1277, 1279, 1281, 1283, 1285,  // NOLINT
  1287, 1289, 1291, 1293, 1295, 1297, 1299, 1073743201,  // NOLINT
  1415, 1073749248, 7467, 1073749346, 7543, 1073749369, 7578, 7681,  // NOLINT
  7683, 7685, 7687, 7689, 7691, 7693, 7695, 7697,  // NOLINT
  7699, 7701, 7703, 7705, 7707, 7709, 7711, 7713,  // NOLINT
  7715, 7717, 7719, 7721, 7723, 7725, 7727, 7729,  // NOLINT
  7731, 7733, 7735, 7737, 7739, 7741, 7743, 7745,  // NOLINT
  7747, 7749, 7751, 7753, 7755, 7757, 7759, 7761,  // NOLINT
  7763, 7765, 7767, 7769, 7771, 7773, 7775, 7777,  // NOLINT
  7779, 7781, 7783, 7785, 7787, 7789, 7791, 7793,  // NOLINT
  7795, 7797, 7799, 7801, 7803, 7805, 7807, 7809,  // NOLINT
  7811, 7813, 7815, 7817, 7819, 7821, 7823, 7825,  // NOLINT
  7827, 1073749653, 7835, 7841, 7843, 7845, 7847, 7849,  // NOLINT
  7851, 7853, 7855, 7857, 7859, 7861, 7863, 7865,  // NOLINT
  7867, 7869, 7871, 7873, 7875, 7877, 7879, 7881,  // NOLINT
  7883, 7885, 7887, 7889, 7891, 7893, 7895, 7897,  // NOLINT
  7899, 7901, 7903, 7905, 7907, 7909, 7911, 7913,  // NOLINT
  7915, 7917, 7919, 7921, 7923, 7925, 7927, 7929,  // NOLINT
  1073749760, 7943, 1073749776, 7957, 1073749792, 7975, 1073749808, 7991,  // NOLINT
  1073749824, 8005, 1073749840, 8023, 1073749856, 8039, 1073749872, 8061,  // NOLINT
  1073749888, 8071, 1073749904, 8087, 1073749920, 8103, 1073749936, 8116,  // NOLINT
  1073749942, 8119, 8126, 1073749954, 8132, 1073749958, 8135, 1073749968,  // NOLINT
  8147, 1073749974, 8151, 1073749984, 8167, 1073750002, 8180, 1073750006,  // NOLINT
  8183, 8305, 8319, 8458, 1073750286, 8463, 8467, 8495,  // NOLINT
  8500, 8505, 1073750332, 8509, 1073750342, 8521, 8526, 8580,  // NOLINT
  1073753136, 11358, 11361, 1073753189, 11366, 11368, 11370, 11372,  // NOLINT
  11380, 1073753206, 11383, 11393, 11395, 11397, 11399, 11401,  // NOLINT
  11403, 11405, 11407, 11409, 11411, 11413, 11415, 11417,  // NOLINT
  11419, 11421, 11423, 11425, 11427, 11429, 11431, 11433,  // NOLINT
  11435, 11437, 11439, 11441, 11443, 11445, 11447, 11449,  // NOLINT
  11451, 11453, 11455, 11457, 11459, 11461, 11463, 11465,  // NOLINT
  11467, 11469, 11471, 11473, 11475, 11477, 11479, 11481,  // NOLINT
  11483, 11485, 11487, 11489, 1073753315, 11492, 1073753344, 11557 };  // NOLINT
static const uint16_t kLowercaseTable1Size = 6;
static const int32_t kLowercaseTable1[6] = {
  1073773312, 31494, 1073773331, 31511, 1073774401, 32602 };  // NOLINT
bool Lowercase::Is(uchar c) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupPredicate(kLowercaseTable0,
                                       kLowercaseTable0Size,
                                       c);
    case 1: return LookupPredicate(kLowercaseTable1,
                                       kLowercaseTable1Size,
                                       c);
    default: return false;
  }
}

// Letter:               point.category in ['Lu', 'Ll', 'Lt', 'Lm', 'Lo', 'Nl' ]

static const uint16_t kLetterTable0Size = 480;
static const int32_t kLetterTable0[480] = {
  1073741889, 90, 1073741921, 122, 170, 181, 186, 1073742016,  // NOLINT
  214, 1073742040, 246, 1073742072, 705, 1073742534, 721, 1073742560,  // NOLINT
  740, 750, 1073742714, 893, 902, 1073742728, 906, 908,  // NOLINT
  1073742734, 929, 1073742755, 974, 1073742800, 1013, 1073742839, 1153,  // NOLINT
  1073742986, 1299, 1073743153, 1366, 1369, 1073743201, 1415, 1073743312,  // NOLINT
  1514, 1073743344, 1522, 1073743393, 1594, 1073743424, 1610, 1073743470,  // NOLINT
  1647, 1073743473, 1747, 1749, 1073743589, 1766, 1073743598, 1775,  // NOLINT
  1073743610, 1788, 1791, 1808, 1073743634, 1839, 1073743693, 1901,  // NOLINT
  1073743744, 1957, 1969, 1073743818, 2026, 1073743860, 2037, 2042,  // NOLINT
  1073744132, 2361, 2365, 2384, 1073744216, 2401, 1073744251, 2431,  // NOLINT
  1073744261, 2444, 1073744271, 2448, 1073744275, 2472, 1073744298, 2480,  // NOLINT
  2482, 1073744310, 2489, 2493, 2510, 1073744348, 2525, 1073744351,  // NOLINT
  2529, 1073744368, 2545, 1073744389, 2570, 1073744399, 2576, 1073744403,  // NOLINT
  2600, 1073744426, 2608, 1073744434, 2611, 1073744437, 2614, 1073744440,  // NOLINT
  2617, 1073744473, 2652, 2654, 1073744498, 2676, 1073744517, 2701,  // NOLINT
  1073744527, 2705, 1073744531, 2728, 1073744554, 2736, 1073744562, 2739,  // NOLINT
  1073744565, 2745, 2749, 2768, 1073744608, 2785, 1073744645, 2828,  // NOLINT
  1073744655, 2832, 1073744659, 2856, 1073744682, 2864, 1073744690, 2867,  // NOLINT
  1073744693, 2873, 2877, 1073744732, 2909, 1073744735, 2913, 2929,  // NOLINT
  2947, 1073744773, 2954, 1073744782, 2960, 1073744786, 2965, 1073744793,  // NOLINT
  2970, 2972, 1073744798, 2975, 1073744803, 2980, 1073744808, 2986,  // NOLINT
  1073744814, 3001, 1073744901, 3084, 1073744910, 3088, 1073744914, 3112,  // NOLINT
  1073744938, 3123, 1073744949, 3129, 1073744992, 3169, 1073745029, 3212,  // NOLINT
  1073745038, 3216, 1073745042, 3240, 1073745066, 3251, 1073745077, 3257,  // NOLINT
  3261, 3294, 1073745120, 3297, 1073745157, 3340, 1073745166, 3344,  // NOLINT
  1073745170, 3368, 1073745194, 3385, 1073745248, 3425, 1073745285, 3478,  // NOLINT
  1073745306, 3505, 1073745331, 3515, 3517, 1073745344, 3526, 1073745409,  // NOLINT
  3632, 1073745458, 3635, 1073745472, 3654, 1073745537, 3714, 3716,  // NOLINT
  1073745543, 3720, 3722, 3725, 1073745556, 3735, 1073745561, 3743,  // NOLINT
  1073745569, 3747, 3749, 3751, 1073745578, 3755, 1073745581, 3760,  // NOLINT
  1073745586, 3763, 3773, 1073745600, 3780, 3782, 1073745628, 3805,  // NOLINT
  3840, 1073745728, 3911, 1073745737, 3946, 1073745800, 3979, 1073745920,  // NOLINT
  4129, 1073745955, 4135, 1073745961, 4138, 1073746000, 4181, 1073746080,  // NOLINT
  4293, 1073746128, 4346, 4348, 1073746176, 4441, 1073746271, 4514,  // NOLINT
  1073746344, 4601, 1073746432, 4680, 1073746506, 4685, 1073746512, 4694,  // NOLINT
  4696, 1073746522, 4701, 1073746528, 4744, 1073746570, 4749, 1073746576,  // NOLINT
  4784, 1073746610, 4789, 1073746616, 4798, 4800, 1073746626, 4805,  // NOLINT
  1073746632, 4822, 1073746648, 4880, 1073746706, 4885, 1073746712, 4954,  // NOLINT
  1073746816, 5007, 1073746848, 5108, 1073746945, 5740, 1073747567, 5750,  // NOLINT
  1073747585, 5786, 1073747616, 5866, 1073747694, 5872, 1073747712, 5900,  // NOLINT
  1073747726, 5905, 1073747744, 5937, 1073747776, 5969, 1073747808, 5996,  // NOLINT
  1073747822, 6000, 1073747840, 6067, 6103, 6108, 1073748000, 6263,  // NOLINT
  1073748096, 6312, 1073748224, 6428, 1073748304, 6509, 1073748336, 6516,  // NOLINT
  1073748352, 6569, 1073748417, 6599, 1073748480, 6678, 1073748741, 6963,  // NOLINT
  1073748805, 6987, 1073749248, 7615, 1073749504, 7835, 1073749664, 7929,  // NOLINT
  1073749760, 7957, 1073749784, 7965, 1073749792, 8005, 1073749832, 8013,  // NOLINT
  1073749840, 8023, 8025, 8027, 8029, 1073749855, 8061, 1073749888,  // NOLINT
  8116, 1073749942, 8124, 8126, 1073749954, 8132, 1073749958, 8140,  // NOLINT
  1073749968, 8147, 1073749974, 8155, 1073749984, 8172, 1073750002, 8180,  // NOLINT
  1073750006, 8188, 8305, 8319, 1073750160, 8340, 8450, 8455,  // NOLINT
  1073750282, 8467, 8469, 1073750297, 8477, 8484, 8486, 8488,  // NOLINT
  1073750314, 8493, 1073750319, 8505, 1073750332, 8511, 1073750341, 8521,  // NOLINT
  8526, 1073750368, 8580, 1073753088, 11310, 1073753136, 11358, 1073753184,  // NOLINT
  11372, 1073753204, 11383, 1073753216, 11492, 1073753344, 11557, 1073753392,  // NOLINT
  11621, 11631, 1073753472, 11670, 1073753504, 11686, 1073753512, 11694,  // NOLINT
  1073753520, 11702, 1073753528, 11710, 1073753536, 11718, 1073753544, 11726,  // NOLINT
  1073753552, 11734, 1073753560, 11742, 1073754117, 12295, 1073754145, 12329,  // NOLINT
  1073754161, 12341, 1073754168, 12348, 1073754177, 12438, 1073754269, 12447,  // NOLINT
  1073754273, 12538, 1073754364, 12543, 1073754373, 12588, 1073754417, 12686,  // NOLINT
  1073754528, 12727, 1073754608, 12799, 1073755136, 19893, 1073761792, 32767 };  // NOLINT
static const uint16_t kLetterTable1Size = 68;
static const int32_t kLetterTable1[68] = {
  1073741824, 8123, 1073750016, 9356, 1073751831, 10010, 1073752064, 10241,  // NOLINT
  1073752067, 10245, 1073752071, 10250, 1073752076, 10274, 1073752128, 10355,  // NOLINT
  1073753088, 22435, 1073772800, 31277, 1073773104, 31338, 1073773168, 31449,  // NOLINT
  1073773312, 31494, 1073773331, 31511, 31517, 1073773343, 31528, 1073773354,  // NOLINT
  31542, 1073773368, 31548, 31550, 1073773376, 31553, 1073773379, 31556,  // NOLINT
  1073773382, 31665, 1073773523, 32061, 1073773904, 32143, 1073773970, 32199,  // NOLINT
  1073774064, 32251, 1073774192, 32372, 1073774198, 32508, 1073774369, 32570,  // NOLINT
  1073774401, 32602, 1073774438, 32702, 1073774530, 32711, 1073774538, 32719,  // NOLINT
  1073774546, 32727, 1073774554, 32732 };  // NOLINT
bool Letter::Is(uchar c) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupPredicate(kLetterTable0,
                                       kLetterTable0Size,
                                       c);
    case 1: return LookupPredicate(kLetterTable1,
                                       kLetterTable1Size,
                                       c);
    default: return false;
  }
}

// Space:                point.category == 'Zs'

static const uint16_t kSpaceTable0Size = 9;
static const int32_t kSpaceTable0[9] = {
  32, 160, 5760, 6158, 1073750016, 8202, 8239, 8287,  // NOLINT
  12288 };  // NOLINT
bool Space::Is(uchar c) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupPredicate(kSpaceTable0,
                                       kSpaceTable0Size,
                                       c);
    default: return false;
  }
}

// Number:               point.category == 'Nd'

static const uint16_t kNumberTable0Size = 44;
static const int32_t kNumberTable0[44] = {
  1073741872, 57, 1073743456, 1641, 1073743600, 1785, 1073743808, 1993,  // NOLINT
  1073744230, 2415, 1073744358, 2543, 1073744486, 2671, 1073744614, 2799,  // NOLINT
  1073744742, 2927, 1073744870, 3055, 1073744998, 3183, 1073745126, 3311,  // NOLINT
  1073745254, 3439, 1073745488, 3673, 1073745616, 3801, 1073745696, 3881,  // NOLINT
  1073745984, 4169, 1073747936, 6121, 1073747984, 6169, 1073748294, 6479,  // NOLINT
  1073748432, 6617, 1073748816, 7001 };  // NOLINT
static const uint16_t kNumberTable1Size = 2;
static const int32_t kNumberTable1[2] = {
  1073774352, 32537 };  // NOLINT
bool Number::Is(uchar c) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupPredicate(kNumberTable0,
                                       kNumberTable0Size,
                                       c);
    case 1: return LookupPredicate(kNumberTable1,
                                       kNumberTable1Size,
                                       c);
    default: return false;
  }
}

// WhiteSpace:           'Ws' in point.properties

static const uint16_t kWhiteSpaceTable0Size = 14;
static const int32_t kWhiteSpaceTable0[14] = {
  1073741833, 13, 32, 133, 160, 5760, 6158, 1073750016,  // NOLINT
  8202, 1073750056, 8233, 8239, 8287, 12288 };  // NOLINT
bool WhiteSpace::Is(uchar c) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupPredicate(kWhiteSpaceTable0,
                                       kWhiteSpaceTable0Size,
                                       c);
    default: return false;
  }
}

// LineTerminator:       'Lt' in point.properties

static const uint16_t kLineTerminatorTable0Size = 4;
static const int32_t kLineTerminatorTable0[4] = {
  10, 13, 1073750056, 8233 };  // NOLINT
bool LineTerminator::Is(uchar c) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupPredicate(kLineTerminatorTable0,
                                       kLineTerminatorTable0Size,
                                       c);
    default: return false;
  }
}

// CombiningMark:        point.category in ['Mn', 'Mc']

static const uint16_t kCombiningMarkTable0Size = 214;
static const int32_t kCombiningMarkTable0[214] = {
  1073742592, 879, 1073742979, 1158, 1073743249, 1469, 1471, 1073743297,  // NOLINT
  1474, 1073743300, 1477, 1479, 1073743376, 1557, 1073743435, 1630,  // NOLINT
  1648, 1073743574, 1756, 1073743583, 1764, 1073743591, 1768, 1073743594,  // NOLINT
  1773, 1809, 1073743664, 1866, 1073743782, 1968, 1073743851, 2035,  // NOLINT
  1073744129, 2307, 2364, 1073744190, 2381, 1073744209, 2388, 1073744226,  // NOLINT
  2403, 1073744257, 2435, 2492, 1073744318, 2500, 1073744327, 2504,  // NOLINT
  1073744331, 2509, 2519, 1073744354, 2531, 1073744385, 2563, 2620,  // NOLINT
  1073744446, 2626, 1073744455, 2632, 1073744459, 2637, 1073744496, 2673,  // NOLINT
  1073744513, 2691, 2748, 1073744574, 2757, 1073744583, 2761, 1073744587,  // NOLINT
  2765, 1073744610, 2787, 1073744641, 2819, 2876, 1073744702, 2883,  // NOLINT
  1073744711, 2888, 1073744715, 2893, 1073744726, 2903, 2946, 1073744830,  // NOLINT
  3010, 1073744838, 3016, 1073744842, 3021, 3031, 1073744897, 3075,  // NOLINT
  1073744958, 3140, 1073744966, 3144, 1073744970, 3149, 1073744981, 3158,  // NOLINT
  1073745026, 3203, 3260, 1073745086, 3268, 1073745094, 3272, 1073745098,  // NOLINT
  3277, 1073745109, 3286, 1073745122, 3299, 1073745154, 3331, 1073745214,  // NOLINT
  3395, 1073745222, 3400, 1073745226, 3405, 3415, 1073745282, 3459,  // NOLINT
  3530, 1073745359, 3540, 3542, 1073745368, 3551, 1073745394, 3571,  // NOLINT
  3633, 1073745460, 3642, 1073745479, 3662, 3761, 1073745588, 3769,  // NOLINT
  1073745595, 3772, 1073745608, 3789, 1073745688, 3865, 3893, 3895,  // NOLINT
  3897, 1073745726, 3903, 1073745777, 3972, 1073745798, 3975, 1073745808,  // NOLINT
  3991, 1073745817, 4028, 4038, 1073745964, 4146, 1073745974, 4153,  // NOLINT
  1073746006, 4185, 4959, 1073747730, 5908, 1073747762, 5940, 1073747794,  // NOLINT
  5971, 1073747826, 6003, 1073747894, 6099, 6109, 1073747979, 6157,  // NOLINT
  6313, 1073748256, 6443, 1073748272, 6459, 1073748400, 6592, 1073748424,  // NOLINT
  6601, 1073748503, 6683, 1073748736, 6916, 1073748788, 6980, 1073748843,  // NOLINT
  7027, 1073749440, 7626, 1073749502, 7679, 1073750224, 8412, 8417,  // NOLINT
  1073750245, 8431, 1073754154, 12335, 1073754265, 12442 };  // NOLINT
static const uint16_t kCombiningMarkTable1Size = 10;
static const int32_t kCombiningMarkTable1[10] = {
  10242, 10246, 10251, 1073752099, 10279, 31518, 1073774080, 32271,  // NOLINT
  1073774112, 32291 };  // NOLINT
bool CombiningMark::Is(uchar c) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupPredicate(kCombiningMarkTable0,
                                       kCombiningMarkTable0Size,
                                       c);
    case 1: return LookupPredicate(kCombiningMarkTable1,
                                       kCombiningMarkTable1Size,
                                       c);
    default: return false;
  }
}

// ConnectorPunctuation: point.category == 'Pc'

static const uint16_t kConnectorPunctuationTable0Size = 4;
static const int32_t kConnectorPunctuationTable0[4] = {
  95, 1073750079, 8256, 8276 };  // NOLINT
static const uint16_t kConnectorPunctuationTable1Size = 5;
static const int32_t kConnectorPunctuationTable1[5] = {
  1073774131, 32308, 1073774157, 32335, 32575 };  // NOLINT
bool ConnectorPunctuation::Is(uchar c) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupPredicate(kConnectorPunctuationTable0,
                                       kConnectorPunctuationTable0Size,
                                       c);
    case 1: return LookupPredicate(kConnectorPunctuationTable1,
                                       kConnectorPunctuationTable1Size,
                                       c);
    default: return false;
  }
}

static const MultiCharacterSpecialCase<3> kToLowercaseMultiStrings0[] = {  // NOLINT
  {2, {105, 775}}, {0, {0}} }; // NOLINT
static const uint16_t kToLowercaseTable0Size = 531;
static const int32_t kToLowercaseTable0[1062] = {
  1073741889, 128, 90, 128, 1073742016, 128, 214, 128, 1073742040, 128, 222, 128, 256, 4, 258, 4,  // NOLINT
  260, 4, 262, 4, 264, 4, 266, 4, 268, 4, 270, 4, 272, 4, 274, 4,  // NOLINT
  276, 4, 278, 4, 280, 4, 282, 4, 284, 4, 286, 4, 288, 4, 290, 4,  // NOLINT
  292, 4, 294, 4, 296, 4, 298, 4, 300, 4, 302, 4, 304, 1, 306, 4,  // NOLINT
  308, 4, 310, 4, 313, 4, 315, 4, 317, 4, 319, 4, 321, 4, 323, 4,  // NOLINT
  325, 4, 327, 4, 330, 4, 332, 4, 334, 4, 336, 4, 338, 4, 340, 4,  // NOLINT
  342, 4, 344, 4, 346, 4, 348, 4, 350, 4, 352, 4, 354, 4, 356, 4,  // NOLINT
  358, 4, 360, 4, 362, 4, 364, 4, 366, 4, 368, 4, 370, 4, 372, 4,  // NOLINT
  374, 4, 376, -484, 377, 4, 379, 4, 381, 4, 385, 840, 386, 4, 388, 4,  // NOLINT
  390, 824, 391, 4, 1073742217, 820, 394, 820, 395, 4, 398, 316, 399, 808, 400, 812,  // NOLINT
  401, 4, 403, 820, 404, 828, 406, 844, 407, 836, 408, 4, 412, 844, 413, 852,  // NOLINT
  415, 856, 416, 4, 418, 4, 420, 4, 422, 872, 423, 4, 425, 872, 428, 4,  // NOLINT
  430, 872, 431, 4, 1073742257, 868, 434, 868, 435, 4, 437, 4, 439, 876, 440, 4,  // NOLINT
  444, 4, 452, 8, 453, 4, 455, 8, 456, 4, 458, 8, 459, 4, 461, 4,  // NOLINT
  463, 4, 465, 4, 467, 4, 469, 4, 471, 4, 473, 4, 475, 4, 478, 4,  // NOLINT
  480, 4, 482, 4, 484, 4, 486, 4, 488, 4, 490, 4, 492, 4, 494, 4,  // NOLINT
  497, 8, 498, 4, 500, 4, 502, -388, 503, -224, 504, 4, 506, 4, 508, 4,  // NOLINT
  510, 4, 512, 4, 514, 4, 516, 4, 518, 4, 520, 4, 522, 4, 524, 4,  // NOLINT
  526, 4, 528, 4, 530, 4, 532, 4, 534, 4, 536, 4, 538, 4, 540, 4,  // NOLINT
  542, 4, 544, -520, 546, 4, 548, 4, 550, 4, 552, 4, 554, 4, 556, 4,  // NOLINT
  558, 4, 560, 4, 562, 4, 570, 43180, 571, 4, 573, -652, 574, 43168, 577, 4,  // NOLINT
  579, -780, 580, 276, 581, 284, 582, 4, 584, 4, 586, 4, 588, 4, 590, 4,  // NOLINT
  902, 152, 1073742728, 148, 906, 148, 908, 256, 1073742734, 252, 911, 252, 1073742737, 128, 929, 128,  // NOLINT
  1073742755, 6, 939, 128, 984, 4, 986, 4, 988, 4, 990, 4, 992, 4, 994, 4,  // NOLINT
  996, 4, 998, 4, 1000, 4, 1002, 4, 1004, 4, 1006, 4, 1012, -240, 1015, 4,  // NOLINT
  1017, -28, 1018, 4, 1073742845, -520, 1023, -520, 1073742848, 320, 1039, 320, 1073742864, 128, 1071, 128,  // NOLINT
  1120, 4, 1122, 4, 1124, 4, 1126, 4, 1128, 4, 1130, 4, 1132, 4, 1134, 4,  // NOLINT
  1136, 4, 1138, 4, 1140, 4, 1142, 4, 1144, 4, 1146, 4, 1148, 4, 1150, 4,  // NOLINT
  1152, 4, 1162, 4, 1164, 4, 1166, 4, 1168, 4, 1170, 4, 1172, 4, 1174, 4,  // NOLINT
  1176, 4, 1178, 4, 1180, 4, 1182, 4, 1184, 4, 1186, 4, 1188, 4, 1190, 4,  // NOLINT
  1192, 4, 1194, 4, 1196, 4, 1198, 4, 1200, 4, 1202, 4, 1204, 4, 1206, 4,  // NOLINT
  1208, 4, 1210, 4, 1212, 4, 1214, 4, 1216, 60, 1217, 4, 1219, 4, 1221, 4,  // NOLINT
  1223, 4, 1225, 4, 1227, 4, 1229, 4, 1232, 4, 1234, 4, 1236, 4, 1238, 4,  // NOLINT
  1240, 4, 1242, 4, 1244, 4, 1246, 4, 1248, 4, 1250, 4, 1252, 4, 1254, 4,  // NOLINT
  1256, 4, 1258, 4, 1260, 4, 1262, 4, 1264, 4, 1266, 4, 1268, 4, 1270, 4,  // NOLINT
  1272, 4, 1274, 4, 1276, 4, 1278, 4, 1280, 4, 1282, 4, 1284, 4, 1286, 4,  // NOLINT
  1288, 4, 1290, 4, 1292, 4, 1294, 4, 1296, 4, 1298, 4, 1073743153, 192, 1366, 192,  // NOLINT
  1073746080, 29056, 4293, 29056, 7680, 4, 7682, 4, 7684, 4, 7686, 4, 7688, 4, 7690, 4,  // NOLINT
  7692, 4, 7694, 4, 7696, 4, 7698, 4, 7700, 4, 7702, 4, 7704, 4, 7706, 4,  // NOLINT
  7708, 4, 7710, 4, 7712, 4, 7714, 4, 7716, 4, 7718, 4, 7720, 4, 7722, 4,  // NOLINT
  7724, 4, 7726, 4, 7728, 4, 7730, 4, 7732, 4, 7734, 4, 7736, 4, 7738, 4,  // NOLINT
  7740, 4, 7742, 4, 7744, 4, 7746, 4, 7748, 4, 7750, 4, 7752, 4, 7754, 4,  // NOLINT
  7756, 4, 7758, 4, 7760, 4, 7762, 4, 7764, 4, 7766, 4, 7768, 4, 7770, 4,  // NOLINT
  7772, 4, 7774, 4, 7776, 4, 7778, 4, 7780, 4, 7782, 4, 7784, 4, 7786, 4,  // NOLINT
  7788, 4, 7790, 4, 7792, 4, 7794, 4, 7796, 4, 7798, 4, 7800, 4, 7802, 4,  // NOLINT
  7804, 4, 7806, 4, 7808, 4, 7810, 4, 7812, 4, 7814, 4, 7816, 4, 7818, 4,  // NOLINT
  7820, 4, 7822, 4, 7824, 4, 7826, 4, 7828, 4, 7840, 4, 7842, 4, 7844, 4,  // NOLINT
  7846, 4, 7848, 4, 7850, 4, 7852, 4, 7854, 4, 7856, 4, 7858, 4, 7860, 4,  // NOLINT
  7862, 4, 7864, 4, 7866, 4, 7868, 4, 7870, 4, 7872, 4, 7874, 4, 7876, 4,  // NOLINT
  7878, 4, 7880, 4, 7882, 4, 7884, 4, 7886, 4, 7888, 4, 7890, 4, 7892, 4,  // NOLINT
  7894, 4, 7896, 4, 7898, 4, 7900, 4, 7902, 4, 7904, 4, 7906, 4, 7908, 4,  // NOLINT
  7910, 4, 7912, 4, 7914, 4, 7916, 4, 7918, 4, 7920, 4, 7922, 4, 7924, 4,  // NOLINT
  7926, 4, 7928, 4, 1073749768, -32, 7951, -32, 1073749784, -32, 7965, -32, 1073749800, -32, 7983, -32,  // NOLINT
  1073749816, -32, 7999, -32, 1073749832, -32, 8013, -32, 8025, -32, 8027, -32, 8029, -32, 8031, -32,  // NOLINT
  1073749864, -32, 8047, -32, 1073749896, -32, 8079, -32, 1073749912, -32, 8095, -32, 1073749928, -32, 8111, -32,  // NOLINT
  1073749944, -32, 8121, -32, 1073749946, -296, 8123, -296, 8124, -36, 1073749960, -344, 8139, -344, 8140, -36,  // NOLINT
  1073749976, -32, 8153, -32, 1073749978, -400, 8155, -400, 1073749992, -32, 8169, -32, 1073749994, -448, 8171, -448,  // NOLINT
  8172, -28, 1073750008, -512, 8185, -512, 1073750010, -504, 8187, -504, 8188, -36, 8486, -30068, 8490, -33532,  // NOLINT
  8491, -33048, 8498, 112, 1073750368, 64, 8559, 64, 8579, 4, 1073751222, 104, 9423, 104, 1073753088, 192,  // NOLINT
  11310, 192, 11360, 4, 11362, -42972, 11363, -15256, 11364, -42908, 11367, 4, 11369, 4, 11371, 4,  // NOLINT
  11381, 4, 11392, 4, 11394, 4, 11396, 4, 11398, 4, 11400, 4, 11402, 4, 11404, 4,  // NOLINT
  11406, 4, 11408, 4, 11410, 4, 11412, 4, 11414, 4, 11416, 4, 11418, 4, 11420, 4,  // NOLINT
  11422, 4, 11424, 4, 11426, 4, 11428, 4, 11430, 4, 11432, 4, 11434, 4, 11436, 4,  // NOLINT
  11438, 4, 11440, 4, 11442, 4, 11444, 4, 11446, 4, 11448, 4, 11450, 4, 11452, 4,  // NOLINT
  11454, 4, 11456, 4, 11458, 4, 11460, 4, 11462, 4, 11464, 4, 11466, 4, 11468, 4,  // NOLINT
  11470, 4, 11472, 4, 11474, 4, 11476, 4, 11478, 4, 11480, 4, 11482, 4, 11484, 4,  // NOLINT
  11486, 4, 11488, 4, 11490, 4 };  // NOLINT
static const MultiCharacterSpecialCase<3> kToLowercaseMultiStrings1[] = {  // NOLINT
  {0, {0}} }; // NOLINT
static const uint16_t kToLowercaseTable1Size = 2;
static const int32_t kToLowercaseTable1[4] = {
  1073774369, 128, 32570, 128 };  // NOLINT
int ToLowercase::Convert(uchar c,
                      uchar n,
                      uchar* result,
                      bool* allow_caching_ptr) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupMapping(kToLowercaseTable0,
                                     kToLowercaseTable0Size,
                                     kToLowercaseMultiStrings0,
                                     c,
                                     n,
                                     result,
                                     allow_caching_ptr);
    case 1: return LookupMapping(kToLowercaseTable1,
                                     kToLowercaseTable1Size,
                                     kToLowercaseMultiStrings1,
                                     c,
                                     n,
                                     result,
                                     allow_caching_ptr);
    default: return 0;
  }
}

static const MultiCharacterSpecialCase<3> kToUppercaseMultiStrings0[] = {  // NOLINT
  {2, {83, 83}}, {2, {700, 78}}, {2, {74, 780}}, {3, {921, 776, 769}},  // NOLINT
  {3, {933, 776, 769}}, {2, {1333, 1362}}, {2, {72, 817}}, {2, {84, 776}},  // NOLINT
  {2, {87, 778}}, {2, {89, 778}}, {2, {65, 702}}, {2, {933, 787}},  // NOLINT
  {3, {933, 787, 768}}, {3, {933, 787, 769}}, {3, {933, 787, 834}}, {2, {7944, 921}},  // NOLINT
  {2, {7945, 921}}, {2, {7946, 921}}, {2, {7947, 921}}, {2, {7948, 921}},  // NOLINT
  {2, {7949, 921}}, {2, {7950, 921}}, {2, {7951, 921}}, {2, {7976, 921}},  // NOLINT
  {2, {7977, 921}}, {2, {7978, 921}}, {2, {7979, 921}}, {2, {7980, 921}},  // NOLINT
  {2, {7981, 921}}, {2, {7982, 921}}, {2, {7983, 921}}, {2, {8040, 921}},  // NOLINT
  {2, {8041, 921}}, {2, {8042, 921}}, {2, {8043, 921}}, {2, {8044, 921}},  // NOLINT
  {2, {8045, 921}}, {2, {8046, 921}}, {2, {8047, 921}}, {2, {8122, 921}},  // NOLINT
  {2, {913, 921}}, {2, {902, 921}}, {2, {913, 834}}, {3, {913, 834, 921}},  // NOLINT
  {2, {8138, 921}}, {2, {919, 921}}, {2, {905, 921}}, {2, {919, 834}},  // NOLINT
  {3, {919, 834, 921}}, {3, {921, 776, 768}}, {2, {921, 834}}, {3, {921, 776, 834}},  // NOLINT
  {3, {933, 776, 768}}, {2, {929, 787}}, {2, {933, 834}}, {3, {933, 776, 834}},  // NOLINT
  {2, {8186, 921}}, {2, {937, 921}}, {2, {911, 921}}, {2, {937, 834}},  // NOLINT
  {3, {937, 834, 921}}, {0, {0}} }; // NOLINT
static const uint16_t kToUppercaseTable0Size = 621;
static const int32_t kToUppercaseTable0[1242] = {
  1073741921, -128, 122, -128, 181, 2972, 223, 1, 1073742048, -128, 246, -128, 1073742072, -128, 254, -128,  // NOLINT
  255, 484, 257, -4, 259, -4, 261, -4, 263, -4, 265, -4, 267, -4, 269, -4,  // NOLINT
  271, -4, 273, -4, 275, -4, 277, -4, 279, -4, 281, -4, 283, -4, 285, -4,  // NOLINT
  287, -4, 289, -4, 291, -4, 293, -4, 295, -4, 297, -4, 299, -4, 301, -4,  // NOLINT
  303, -4, 305, -928, 307, -4, 309, -4, 311, -4, 314, -4, 316, -4, 318, -4,  // NOLINT
  320, -4, 322, -4, 324, -4, 326, -4, 328, -4, 329, 5, 331, -4, 333, -4,  // NOLINT
  335, -4, 337, -4, 339, -4, 341, -4, 343, -4, 345, -4, 347, -4, 349, -4,  // NOLINT
  351, -4, 353, -4, 355, -4, 357, -4, 359, -4, 361, -4, 363, -4, 365, -4,  // NOLINT
  367, -4, 369, -4, 371, -4, 373, -4, 375, -4, 378, -4, 380, -4, 382, -4,  // NOLINT
  383, -1200, 384, 780, 387, -4, 389, -4, 392, -4, 396, -4, 402, -4, 405, 388,  // NOLINT
  409, -4, 410, 652, 414, 520, 417, -4, 419, -4, 421, -4, 424, -4, 429, -4,  // NOLINT
  432, -4, 436, -4, 438, -4, 441, -4, 445, -4, 447, 224, 453, -4, 454, -8,  // NOLINT
  456, -4, 457, -8, 459, -4, 460, -8, 462, -4, 464, -4, 466, -4, 468, -4,  // NOLINT
  470, -4, 472, -4, 474, -4, 476, -4, 477, -316, 479, -4, 481, -4, 483, -4,  // NOLINT
  485, -4, 487, -4, 489, -4, 491, -4, 493, -4, 495, -4, 496, 9, 498, -4,  // NOLINT
  499, -8, 501, -4, 505, -4, 507, -4, 509, -4, 511, -4, 513, -4, 515, -4,  // NOLINT
  517, -4, 519, -4, 521, -4, 523, -4, 525, -4, 527, -4, 529, -4, 531, -4,  // NOLINT
  533, -4, 535, -4, 537, -4, 539, -4, 541, -4, 543, -4, 547, -4, 549, -4,  // NOLINT
  551, -4, 553, -4, 555, -4, 557, -4, 559, -4, 561, -4, 563, -4, 572, -4,  // NOLINT
  578, -4, 583, -4, 585, -4, 587, -4, 589, -4, 591, -4, 595, -840, 596, -824,  // NOLINT
  1073742422, -820, 599, -820, 601, -808, 603, -812, 608, -820, 611, -828, 616, -836, 617, -844,  // NOLINT
  619, 42972, 623, -844, 626, -852, 629, -856, 637, 42908, 640, -872, 643, -872, 648, -872,  // NOLINT
  649, -276, 1073742474, -868, 651, -868, 652, -284, 658, -876, 837, 336, 1073742715, 520, 893, 520,  // NOLINT
  912, 13, 940, -152, 1073742765, -148, 943, -148, 944, 17, 1073742769, -128, 961, -128, 962, -124,  // NOLINT
  1073742787, -128, 971, -128, 972, -256, 1073742797, -252, 974, -252, 976, -248, 977, -228, 981, -188,  // NOLINT
  982, -216, 985, -4, 987, -4, 989, -4, 991, -4, 993, -4, 995, -4, 997, -4,  // NOLINT
  999, -4, 1001, -4, 1003, -4, 1005, -4, 1007, -4, 1008, -344, 1009, -320, 1010, 28,  // NOLINT
  1013, -384, 1016, -4, 1019, -4, 1073742896, -128, 1103, -128, 1073742928, -320, 1119, -320, 1121, -4,  // NOLINT
  1123, -4, 1125, -4, 1127, -4, 1129, -4, 1131, -4, 1133, -4, 1135, -4, 1137, -4,  // NOLINT
  1139, -4, 1141, -4, 1143, -4, 1145, -4, 1147, -4, 1149, -4, 1151, -4, 1153, -4,  // NOLINT
  1163, -4, 1165, -4, 1167, -4, 1169, -4, 1171, -4, 1173, -4, 1175, -4, 1177, -4,  // NOLINT
  1179, -4, 1181, -4, 1183, -4, 1185, -4, 1187, -4, 1189, -4, 1191, -4, 1193, -4,  // NOLINT
  1195, -4, 1197, -4, 1199, -4, 1201, -4, 1203, -4, 1205, -4, 1207, -4, 1209, -4,  // NOLINT
  1211, -4, 1213, -4, 1215, -4, 1218, -4, 1220, -4, 1222, -4, 1224, -4, 1226, -4,  // NOLINT
  1228, -4, 1230, -4, 1231, -60, 1233, -4, 1235, -4, 1237, -4, 1239, -4, 1241, -4,  // NOLINT
  1243, -4, 1245, -4, 1247, -4, 1249, -4, 1251, -4, 1253, -4, 1255, -4, 1257, -4,  // NOLINT
  1259, -4, 1261, -4, 1263, -4, 1265, -4, 1267, -4, 1269, -4, 1271, -4, 1273, -4,  // NOLINT
  1275, -4, 1277, -4, 1279, -4, 1281, -4, 1283, -4, 1285, -4, 1287, -4, 1289, -4,  // NOLINT
  1291, -4, 1293, -4, 1295, -4, 1297, -4, 1299, -4, 1073743201, -192, 1414, -192, 1415, 21,  // NOLINT
  7549, 15256, 7681, -4, 7683, -4, 7685, -4, 7687, -4, 7689, -4, 7691, -4, 7693, -4,  // NOLINT
  7695, -4, 7697, -4, 7699, -4, 7701, -4, 7703, -4, 7705, -4, 7707, -4, 7709, -4,  // NOLINT
  7711, -4, 7713, -4, 7715, -4, 7717, -4, 7719, -4, 7721, -4, 7723, -4, 7725, -4,  // NOLINT
  7727, -4, 7729, -4, 7731, -4, 7733, -4, 7735, -4, 7737, -4, 7739, -4, 7741, -4,  // NOLINT
  7743, -4, 7745, -4, 7747, -4, 7749, -4, 7751, -4, 7753, -4, 7755, -4, 7757, -4,  // NOLINT
  7759, -4, 7761, -4, 7763, -4, 7765, -4, 7767, -4, 7769, -4, 7771, -4, 7773, -4,  // NOLINT
  7775, -4, 7777, -4, 7779, -4, 7781, -4, 7783, -4, 7785, -4, 7787, -4, 7789, -4,  // NOLINT
  7791, -4, 7793, -4, 7795, -4, 7797, -4, 7799, -4, 7801, -4, 7803, -4, 7805, -4,  // NOLINT
  7807, -4, 7809, -4, 7811, -4, 7813, -4, 7815, -4, 7817, -4, 7819, -4, 7821, -4,  // NOLINT
  7823, -4, 7825, -4, 7827, -4, 7829, -4, 7830, 25, 7831, 29, 7832, 33, 7833, 37,  // NOLINT
  7834, 41, 7835, -236, 7841, -4, 7843, -4, 7845, -4, 7847, -4, 7849, -4, 7851, -4,  // NOLINT
  7853, -4, 7855, -4, 7857, -4, 7859, -4, 7861, -4, 7863, -4, 7865, -4, 7867, -4,  // NOLINT
  7869, -4, 7871, -4, 7873, -4, 7875, -4, 7877, -4, 7879, -4, 7881, -4, 7883, -4,  // NOLINT
  7885, -4, 7887, -4, 7889, -4, 7891, -4, 7893, -4, 7895, -4, 7897, -4, 7899, -4,  // NOLINT
  7901, -4, 7903, -4, 7905, -4, 7907, -4, 7909, -4, 7911, -4, 7913, -4, 7915, -4,  // NOLINT
  7917, -4, 7919, -4, 7921, -4, 7923, -4, 7925, -4, 7927, -4, 7929, -4, 1073749760, 32,  // NOLINT
  7943, 32, 1073749776, 32, 7957, 32, 1073749792, 32, 7975, 32, 1073749808, 32, 7991, 32, 1073749824, 32,  // NOLINT
  8005, 32, 8016, 45, 8017, 32, 8018, 49, 8019, 32, 8020, 53, 8021, 32, 8022, 57,  // NOLINT
  8023, 32, 1073749856, 32, 8039, 32, 1073749872, 296, 8049, 296, 1073749874, 344, 8053, 344, 1073749878, 400,  // NOLINT
  8055, 400, 1073749880, 512, 8057, 512, 1073749882, 448, 8059, 448, 1073749884, 504, 8061, 504, 8064, 61,  // NOLINT
  8065, 65, 8066, 69, 8067, 73, 8068, 77, 8069, 81, 8070, 85, 8071, 89, 8072, 61,  // NOLINT
  8073, 65, 8074, 69, 8075, 73, 8076, 77, 8077, 81, 8078, 85, 8079, 89, 8080, 93,  // NOLINT
  8081, 97, 8082, 101, 8083, 105, 8084, 109, 8085, 113, 8086, 117, 8087, 121, 8088, 93,  // NOLINT
  8089, 97, 8090, 101, 8091, 105, 8092, 109, 8093, 113, 8094, 117, 8095, 121, 8096, 125,  // NOLINT
  8097, 129, 8098, 133, 8099, 137, 8100, 141, 8101, 145, 8102, 149, 8103, 153, 8104, 125,  // NOLINT
  8105, 129, 8106, 133, 8107, 137, 8108, 141, 8109, 145, 8110, 149, 8111, 153, 1073749936, 32,  // NOLINT
  8113, 32, 8114, 157, 8115, 161, 8116, 165, 8118, 169, 8119, 173, 8124, 161, 8126, -28820,  // NOLINT
  8130, 177, 8131, 181, 8132, 185, 8134, 189, 8135, 193, 8140, 181, 1073749968, 32, 8145, 32,  // NOLINT
  8146, 197, 8147, 13, 8150, 201, 8151, 205, 1073749984, 32, 8161, 32, 8162, 209, 8163, 17,  // NOLINT
  8164, 213, 8165, 28, 8166, 217, 8167, 221, 8178, 225, 8179, 229, 8180, 233, 8182, 237,  // NOLINT
  8183, 241, 8188, 229, 8526, -112, 1073750384, -64, 8575, -64, 8580, -4, 1073751248, -104, 9449, -104,  // NOLINT
  1073753136, -192, 11358, -192, 11361, -4, 11365, -43180, 11366, -43168, 11368, -4, 11370, -4, 11372, -4,  // NOLINT
  11382, -4, 11393, -4, 11395, -4, 11397, -4, 11399, -4, 11401, -4, 11403, -4, 11405, -4,  // NOLINT
  11407, -4, 11409, -4, 11411, -4, 11413, -4, 11415, -4, 11417, -4, 11419, -4, 11421, -4,  // NOLINT
  11423, -4, 11425, -4, 11427, -4, 11429, -4, 11431, -4, 11433, -4, 11435, -4, 11437, -4,  // NOLINT
  11439, -4, 11441, -4, 11443, -4, 11445, -4, 11447, -4, 11449, -4, 11451, -4, 11453, -4,  // NOLINT
  11455, -4, 11457, -4, 11459, -4, 11461, -4, 11463, -4, 11465, -4, 11467, -4, 11469, -4,  // NOLINT
  11471, -4, 11473, -4, 11475, -4, 11477, -4, 11479, -4, 11481, -4, 11483, -4, 11485, -4,  // NOLINT
  11487, -4, 11489, -4, 11491, -4, 1073753344, -29056, 11557, -29056 };  // NOLINT
static const MultiCharacterSpecialCase<3> kToUppercaseMultiStrings1[] = {  // NOLINT
  {2, {70, 70}}, {2, {70, 73}}, {2, {70, 76}}, {3, {70, 70, 73}},  // NOLINT
  {3, {70, 70, 76}}, {2, {83, 84}}, {2, {1348, 1350}}, {2, {1348, 1333}},  // NOLINT
  {2, {1348, 1339}}, {2, {1358, 1350}}, {2, {1348, 1341}}, {0, {0}} }; // NOLINT
static const uint16_t kToUppercaseTable1Size = 14;
static const int32_t kToUppercaseTable1[28] = {
  31488, 1, 31489, 5, 31490, 9, 31491, 13, 31492, 17, 31493, 21, 31494, 21, 31507, 25,  // NOLINT
  31508, 29, 31509, 33, 31510, 37, 31511, 41, 1073774401, -128, 32602, -128 };  // NOLINT
int ToUppercase::Convert(uchar c,
                      uchar n,
                      uchar* result,
                      bool* allow_caching_ptr) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupMapping(kToUppercaseTable0,
                                     kToUppercaseTable0Size,
                                     kToUppercaseMultiStrings0,
                                     c,
                                     n,
                                     result,
                                     allow_caching_ptr);
    case 1: return LookupMapping(kToUppercaseTable1,
                                     kToUppercaseTable1Size,
                                     kToUppercaseMultiStrings1,
                                     c,
                                     n,
                                     result,
                                     allow_caching_ptr);
    default: return 0;
  }
}

static const MultiCharacterSpecialCase<1> kEcma262CanonicalizeMultiStrings0[] = {  // NOLINT
  {0, {0}} }; // NOLINT
static const uint16_t kEcma262CanonicalizeTable0Size = 529;
static const int32_t kEcma262CanonicalizeTable0[1058] = {
  1073741921, -128, 122, -128, 181, 2972, 1073742048, -128, 246, -128, 1073742072, -128, 254, -128, 255, 484,  // NOLINT
  257, -4, 259, -4, 261, -4, 263, -4, 265, -4, 267, -4, 269, -4, 271, -4,  // NOLINT
  273, -4, 275, -4, 277, -4, 279, -4, 281, -4, 283, -4, 285, -4, 287, -4,  // NOLINT
  289, -4, 291, -4, 293, -4, 295, -4, 297, -4, 299, -4, 301, -4, 303, -4,  // NOLINT
  307, -4, 309, -4, 311, -4, 314, -4, 316, -4, 318, -4, 320, -4, 322, -4,  // NOLINT
  324, -4, 326, -4, 328, -4, 331, -4, 333, -4, 335, -4, 337, -4, 339, -4,  // NOLINT
  341, -4, 343, -4, 345, -4, 347, -4, 349, -4, 351, -4, 353, -4, 355, -4,  // NOLINT
  357, -4, 359, -4, 361, -4, 363, -4, 365, -4, 367, -4, 369, -4, 371, -4,  // NOLINT
  373, -4, 375, -4, 378, -4, 380, -4, 382, -4, 384, 780, 387, -4, 389, -4,  // NOLINT
  392, -4, 396, -4, 402, -4, 405, 388, 409, -4, 410, 652, 414, 520, 417, -4,  // NOLINT
  419, -4, 421, -4, 424, -4, 429, -4, 432, -4, 436, -4, 438, -4, 441, -4,  // NOLINT
  445, -4, 447, 224, 453, -4, 454, -8, 456, -4, 457, -8, 459, -4, 460, -8,  // NOLINT
  462, -4, 464, -4, 466, -4, 468, -4, 470, -4, 472, -4, 474, -4, 476, -4,  // NOLINT
  477, -316, 479, -4, 481, -4, 483, -4, 485, -4, 487, -4, 489, -4, 491, -4,  // NOLINT
  493, -4, 495, -4, 498, -4, 499, -8, 501, -4, 505, -4, 507, -4, 509, -4,  // NOLINT
  511, -4, 513, -4, 515, -4, 517, -4, 519, -4, 521, -4, 523, -4, 525, -4,  // NOLINT
  527, -4, 529, -4, 531, -4, 533, -4, 535, -4, 537, -4, 539, -4, 541, -4,  // NOLINT
  543, -4, 547, -4, 549, -4, 551, -4, 553, -4, 555, -4, 557, -4, 559, -4,  // NOLINT
  561, -4, 563, -4, 572, -4, 578, -4, 583, -4, 585, -4, 587, -4, 589, -4,  // NOLINT
  591, -4, 595, -840, 596, -824, 1073742422, -820, 599, -820, 601, -808, 603, -812, 608, -820,  // NOLINT
  611, -828, 616, -836, 617, -844, 619, 42972, 623, -844, 626, -852, 629, -856, 637, 42908,  // NOLINT
  640, -872, 643, -872, 648, -872, 649, -276, 1073742474, -868, 651, -868, 652, -284, 658, -876,  // NOLINT
  837, 336, 1073742715, 520, 893, 520, 940, -152, 1073742765, -148, 943, -148, 1073742769, -128, 961, -128,  // NOLINT
  962, -124, 1073742787, -128, 971, -128, 972, -256, 1073742797, -252, 974, -252, 976, -248, 977, -228,  // NOLINT
  981, -188, 982, -216, 985, -4, 987, -4, 989, -4, 991, -4, 993, -4, 995, -4,  // NOLINT
  997, -4, 999, -4, 1001, -4, 1003, -4, 1005, -4, 1007, -4, 1008, -344, 1009, -320,  // NOLINT
  1010, 28, 1013, -384, 1016, -4, 1019, -4, 1073742896, -128, 1103, -128, 1073742928, -320, 1119, -320,  // NOLINT
  1121, -4, 1123, -4, 1125, -4, 1127, -4, 1129, -4, 1131, -4, 1133, -4, 1135, -4,  // NOLINT
  1137, -4, 1139, -4, 1141, -4, 1143, -4, 1145, -4, 1147, -4, 1149, -4, 1151, -4,  // NOLINT
  1153, -4, 1163, -4, 1165, -4, 1167, -4, 1169, -4, 1171, -4, 1173, -4, 1175, -4,  // NOLINT
  1177, -4, 1179, -4, 1181, -4, 1183, -4, 1185, -4, 1187, -4, 1189, -4, 1191, -4,  // NOLINT
  1193, -4, 1195, -4, 1197, -4, 1199, -4, 1201, -4, 1203, -4, 1205, -4, 1207, -4,  // NOLINT
  1209, -4, 1211, -4, 1213, -4, 1215, -4, 1218, -4, 1220, -4, 1222, -4, 1224, -4,  // NOLINT
  1226, -4, 1228, -4, 1230, -4, 1231, -60, 1233, -4, 1235, -4, 1237, -4, 1239, -4,  // NOLINT
  1241, -4, 1243, -4, 1245, -4, 1247, -4, 1249, -4, 1251, -4, 1253, -4, 1255, -4,  // NOLINT
  1257, -4, 1259, -4, 1261, -4, 1263, -4, 1265, -4, 1267, -4, 1269, -4, 1271, -4,  // NOLINT
  1273, -4, 1275, -4, 1277, -4, 1279, -4, 1281, -4, 1283, -4, 1285, -4, 1287, -4,  // NOLINT
  1289, -4, 1291, -4, 1293, -4, 1295, -4, 1297, -4, 1299, -4, 1073743201, -192, 1414, -192,  // NOLINT
  7549, 15256, 7681, -4, 7683, -4, 7685, -4, 7687, -4, 7689, -4, 7691, -4, 7693, -4,  // NOLINT
  7695, -4, 7697, -4, 7699, -4, 7701, -4, 7703, -4, 7705, -4, 7707, -4, 7709, -4,  // NOLINT
  7711, -4, 7713, -4, 7715, -4, 7717, -4, 7719, -4, 7721, -4, 7723, -4, 7725, -4,  // NOLINT
  7727, -4, 7729, -4, 7731, -4, 7733, -4, 7735, -4, 7737, -4, 7739, -4, 7741, -4,  // NOLINT
  7743, -4, 7745, -4, 7747, -4, 7749, -4, 7751, -4, 7753, -4, 7755, -4, 7757, -4,  // NOLINT
  7759, -4, 7761, -4, 7763, -4, 7765, -4, 7767, -4, 7769, -4, 7771, -4, 7773, -4,  // NOLINT
  7775, -4, 7777, -4, 7779, -4, 7781, -4, 7783, -4, 7785, -4, 7787, -4, 7789, -4,  // NOLINT
  7791, -4, 7793, -4, 7795, -4, 7797, -4, 7799, -4, 7801, -4, 7803, -4, 7805, -4,  // NOLINT
  7807, -4, 7809, -4, 7811, -4, 7813, -4, 7815, -4, 7817, -4, 7819, -4, 7821, -4,  // NOLINT
  7823, -4, 7825, -4, 7827, -4, 7829, -4, 7835, -236, 7841, -4, 7843, -4, 7845, -4,  // NOLINT
  7847, -4, 7849, -4, 7851, -4, 7853, -4, 7855, -4, 7857, -4, 7859, -4, 7861, -4,  // NOLINT
  7863, -4, 7865, -4, 7867, -4, 7869, -4, 7871, -4, 7873, -4, 7875, -4, 7877, -4,  // NOLINT
  7879, -4, 7881, -4, 7883, -4, 7885, -4, 7887, -4, 7889, -4, 7891, -4, 7893, -4,  // NOLINT
  7895, -4, 7897, -4, 7899, -4, 7901, -4, 7903, -4, 7905, -4, 7907, -4, 7909, -4,  // NOLINT
  7911, -4, 7913, -4, 7915, -4, 7917, -4, 7919, -4, 7921, -4, 7923, -4, 7925, -4,  // NOLINT
  7927, -4, 7929, -4, 1073749760, 32, 7943, 32, 1073749776, 32, 7957, 32, 1073749792, 32, 7975, 32,  // NOLINT
  1073749808, 32, 7991, 32, 1073749824, 32, 8005, 32, 8017, 32, 8019, 32, 8021, 32, 8023, 32,  // NOLINT
  1073749856, 32, 8039, 32, 1073749872, 296, 8049, 296, 1073749874, 344, 8053, 344, 1073749878, 400, 8055, 400,  // NOLINT
  1073749880, 512, 8057, 512, 1073749882, 448, 8059, 448, 1073749884, 504, 8061, 504, 1073749936, 32, 8113, 32,  // NOLINT
  8126, -28820, 1073749968, 32, 8145, 32, 1073749984, 32, 8161, 32, 8165, 28, 8526, -112, 1073750384, -64,  // NOLINT
  8575, -64, 8580, -4, 1073751248, -104, 9449, -104, 1073753136, -192, 11358, -192, 11361, -4, 11365, -43180,  // NOLINT
  11366, -43168, 11368, -4, 11370, -4, 11372, -4, 11382, -4, 11393, -4, 11395, -4, 11397, -4,  // NOLINT
  11399, -4, 11401, -4, 11403, -4, 11405, -4, 11407, -4, 11409, -4, 11411, -4, 11413, -4,  // NOLINT
  11415, -4, 11417, -4, 11419, -4, 11421, -4, 11423, -4, 11425, -4, 11427, -4, 11429, -4,  // NOLINT
  11431, -4, 11433, -4, 11435, -4, 11437, -4, 11439, -4, 11441, -4, 11443, -4, 11445, -4,  // NOLINT
  11447, -4, 11449, -4, 11451, -4, 11453, -4, 11455, -4, 11457, -4, 11459, -4, 11461, -4,  // NOLINT
  11463, -4, 11465, -4, 11467, -4, 11469, -4, 11471, -4, 11473, -4, 11475, -4, 11477, -4,  // NOLINT
  11479, -4, 11481, -4, 11483, -4, 11485, -4, 11487, -4, 11489, -4, 11491, -4, 1073753344, -29056,  // NOLINT
  11557, -29056 };  // NOLINT
static const MultiCharacterSpecialCase<1> kEcma262CanonicalizeMultiStrings1[] = {  // NOLINT
  {0, {0}} }; // NOLINT
static const uint16_t kEcma262CanonicalizeTable1Size = 2;
static const int32_t kEcma262CanonicalizeTable1[4] = {
  1073774401, -128, 32602, -128 };  // NOLINT
int Ecma262Canonicalize::Convert(uchar c,
                      uchar n,
                      uchar* result,
                      bool* allow_caching_ptr) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupMapping(kEcma262CanonicalizeTable0,
                                     kEcma262CanonicalizeTable0Size,
                                     kEcma262CanonicalizeMultiStrings0,
                                     c,
                                     n,
                                     result,
                                     allow_caching_ptr);
    case 1: return LookupMapping(kEcma262CanonicalizeTable1,
                                     kEcma262CanonicalizeTable1Size,
                                     kEcma262CanonicalizeMultiStrings1,
                                     c,
                                     n,
                                     result,
                                     allow_caching_ptr);
    default: return 0;
  }
}

static const MultiCharacterSpecialCase<4> kEcma262UnCanonicalizeMultiStrings0[] = {  // NOLINT
  {2, {65, 97}}, {2, {66, 98}}, {2, {67, 99}}, {2, {68, 100}},  // NOLINT
  {2, {69, 101}}, {2, {70, 102}}, {2, {71, 103}}, {2, {72, 104}},  // NOLINT
  {2, {73, 105}}, {2, {74, 106}}, {2, {75, 107}}, {2, {76, 108}},  // NOLINT
  {2, {77, 109}}, {2, {78, 110}}, {2, {79, 111}}, {2, {80, 112}},  // NOLINT
  {2, {81, 113}}, {2, {82, 114}}, {2, {83, 115}}, {2, {84, 116}},  // NOLINT
  {2, {85, 117}}, {2, {86, 118}}, {2, {87, 119}}, {2, {88, 120}},  // NOLINT
  {2, {89, 121}}, {2, {90, 122}}, {3, {181, 924, 956}}, {2, {192, 224}},  // NOLINT
  {2, {193, 225}}, {2, {194, 226}}, {2, {195, 227}}, {2, {196, 228}},  // NOLINT
  {2, {197, 229}}, {2, {198, 230}}, {2, {199, 231}}, {2, {200, 232}},  // NOLINT
  {2, {201, 233}}, {2, {202, 234}}, {2, {203, 235}}, {2, {204, 236}},  // NOLINT
  {2, {205, 237}}, {2, {206, 238}}, {2, {207, 239}}, {2, {208, 240}},  // NOLINT
  {2, {209, 241}}, {2, {210, 242}}, {2, {211, 243}}, {2, {212, 244}},  // NOLINT
  {2, {213, 245}}, {2, {214, 246}}, {2, {216, 248}}, {2, {217, 249}},  // NOLINT
  {2, {218, 250}}, {2, {219, 251}}, {2, {220, 252}}, {2, {221, 253}},  // NOLINT
  {2, {222, 254}}, {2, {255, 376}}, {2, {256, 257}}, {2, {258, 259}},  // NOLINT
  {2, {260, 261}}, {2, {262, 263}}, {2, {264, 265}}, {2, {266, 267}},  // NOLINT
  {2, {268, 269}}, {2, {270, 271}}, {2, {272, 273}}, {2, {274, 275}},  // NOLINT
  {2, {276, 277}}, {2, {278, 279}}, {2, {280, 281}}, {2, {282, 283}},  // NOLINT
  {2, {284, 285}}, {2, {286, 287}}, {2, {288, 289}}, {2, {290, 291}},  // NOLINT
  {2, {292, 293}}, {2, {294, 295}}, {2, {296, 297}}, {2, {298, 299}},  // NOLINT
  {2, {300, 301}}, {2, {302, 303}}, {2, {306, 307}}, {2, {308, 309}},  // NOLINT
  {2, {310, 311}}, {2, {313, 314}}, {2, {315, 316}}, {2, {317, 318}},  // NOLINT
  {2, {319, 320}}, {2, {321, 322}}, {2, {323, 324}}, {2, {325, 326}},  // NOLINT
  {2, {327, 328}}, {2, {330, 331}}, {2, {332, 333}}, {2, {334, 335}},  // NOLINT
  {2, {336, 337}}, {2, {338, 339}}, {2, {340, 341}}, {2, {342, 343}},  // NOLINT
  {2, {344, 345}}, {2, {346, 347}}, {2, {348, 349}}, {2, {350, 351}},  // NOLINT
  {2, {352, 353}}, {2, {354, 355}}, {2, {356, 357}}, {2, {358, 359}},  // NOLINT
  {2, {360, 361}}, {2, {362, 363}}, {2, {364, 365}}, {2, {366, 367}},  // NOLINT
  {2, {368, 369}}, {2, {370, 371}}, {2, {372, 373}}, {2, {374, 375}},  // NOLINT
  {2, {377, 378}}, {2, {379, 380}}, {2, {381, 382}}, {2, {384, 579}},  // NOLINT
  {2, {385, 595}}, {2, {386, 387}}, {2, {388, 389}}, {2, {390, 596}},  // NOLINT
  {2, {391, 392}}, {2, {393, 598}}, {2, {394, 599}}, {2, {395, 396}},  // NOLINT
  {2, {398, 477}}, {2, {399, 601}}, {2, {400, 603}}, {2, {401, 402}},  // NOLINT
  {2, {403, 608}}, {2, {404, 611}}, {2, {405, 502}}, {2, {406, 617}},  // NOLINT
  {2, {407, 616}}, {2, {408, 409}}, {2, {410, 573}}, {2, {412, 623}},  // NOLINT
  {2, {413, 626}}, {2, {414, 544}}, {2, {415, 629}}, {2, {416, 417}},  // NOLINT
  {2, {418, 419}}, {2, {420, 421}}, {2, {422, 640}}, {2, {423, 424}},  // NOLINT
  {2, {425, 643}}, {2, {428, 429}}, {2, {430, 648}}, {2, {431, 432}},  // NOLINT
  {2, {433, 650}}, {2, {434, 651}}, {2, {435, 436}}, {2, {437, 438}},  // NOLINT
  {2, {439, 658}}, {2, {440, 441}}, {2, {444, 445}}, {2, {447, 503}},  // NOLINT
  {3, {452, 453, 454}}, {3, {455, 456, 457}}, {3, {458, 459, 460}}, {2, {461, 462}},  // NOLINT
  {2, {463, 464}}, {2, {465, 466}}, {2, {467, 468}}, {2, {469, 470}},  // NOLINT
  {2, {471, 472}}, {2, {473, 474}}, {2, {475, 476}}, {2, {478, 479}},  // NOLINT
  {2, {480, 481}}, {2, {482, 483}}, {2, {484, 485}}, {2, {486, 487}},  // NOLINT
  {2, {488, 489}}, {2, {490, 491}}, {2, {492, 493}}, {2, {494, 495}},  // NOLINT
  {3, {497, 498, 499}}, {2, {500, 501}}, {2, {504, 505}}, {2, {506, 507}},  // NOLINT
  {2, {508, 509}}, {2, {510, 511}}, {2, {512, 513}}, {2, {514, 515}},  // NOLINT
  {2, {516, 517}}, {2, {518, 519}}, {2, {520, 521}}, {2, {522, 523}},  // NOLINT
  {2, {524, 525}}, {2, {526, 527}}, {2, {528, 529}}, {2, {530, 531}},  // NOLINT
  {2, {532, 533}}, {2, {534, 535}}, {2, {536, 537}}, {2, {538, 539}},  // NOLINT
  {2, {540, 541}}, {2, {542, 543}}, {2, {546, 547}}, {2, {548, 549}},  // NOLINT
  {2, {550, 551}}, {2, {552, 553}}, {2, {554, 555}}, {2, {556, 557}},  // NOLINT
  {2, {558, 559}}, {2, {560, 561}}, {2, {562, 563}}, {2, {570, 11365}},  // NOLINT
  {2, {571, 572}}, {2, {574, 11366}}, {2, {577, 578}}, {2, {580, 649}},  // NOLINT
  {2, {581, 652}}, {2, {582, 583}}, {2, {584, 585}}, {2, {586, 587}},  // NOLINT
  {2, {588, 589}}, {2, {590, 591}}, {2, {619, 11362}}, {2, {637, 11364}},  // NOLINT
  {4, {837, 921, 953, 8126}}, {2, {891, 1021}}, {2, {892, 1022}}, {2, {893, 1023}},  // NOLINT
  {2, {902, 940}}, {2, {904, 941}}, {2, {905, 942}}, {2, {906, 943}},  // NOLINT
  {2, {908, 972}}, {2, {910, 973}}, {2, {911, 974}}, {2, {913, 945}},  // NOLINT
  {3, {914, 946, 976}}, {2, {915, 947}}, {2, {916, 948}}, {3, {917, 949, 1013}},  // NOLINT
  {2, {918, 950}}, {2, {919, 951}}, {3, {920, 952, 977}}, {3, {922, 954, 1008}},  // NOLINT
  {2, {923, 955}}, {2, {925, 957}}, {2, {926, 958}}, {2, {927, 959}},  // NOLINT
  {3, {928, 960, 982}}, {3, {929, 961, 1009}}, {3, {931, 962, 963}}, {2, {932, 964}},  // NOLINT
  {2, {933, 965}}, {3, {934, 966, 981}}, {2, {935, 967}}, {2, {936, 968}},  // NOLINT
  {2, {937, 969}}, {2, {938, 970}}, {2, {939, 971}}, {2, {984, 985}},  // NOLINT
  {2, {986, 987}}, {2, {988, 989}}, {2, {990, 991}}, {2, {992, 993}},  // NOLINT
  {2, {994, 995}}, {2, {996, 997}}, {2, {998, 999}}, {2, {1000, 1001}},  // NOLINT
  {2, {1002, 1003}}, {2, {1004, 1005}}, {2, {1006, 1007}}, {2, {1010, 1017}},  // NOLINT
  {2, {1015, 1016}}, {2, {1018, 1019}}, {2, {1024, 1104}}, {2, {1025, 1105}},  // NOLINT
  {2, {1026, 1106}}, {2, {1027, 1107}}, {2, {1028, 1108}}, {2, {1029, 1109}},  // NOLINT
  {2, {1030, 1110}}, {2, {1031, 1111}}, {2, {1032, 1112}}, {2, {1033, 1113}},  // NOLINT
  {2, {1034, 1114}}, {2, {1035, 1115}}, {2, {1036, 1116}}, {2, {1037, 1117}},  // NOLINT
  {2, {1038, 1118}}, {2, {1039, 1119}}, {2, {1040, 1072}}, {2, {1041, 1073}},  // NOLINT
  {2, {1042, 1074}}, {2, {1043, 1075}}, {2, {1044, 1076}}, {2, {1045, 1077}},  // NOLINT
  {2, {1046, 1078}}, {2, {1047, 1079}}, {2, {1048, 1080}}, {2, {1049, 1081}},  // NOLINT
  {2, {1050, 1082}}, {2, {1051, 1083}}, {2, {1052, 1084}}, {2, {1053, 1085}},  // NOLINT
  {2, {1054, 1086}}, {2, {1055, 1087}}, {2, {1056, 1088}}, {2, {1057, 1089}},  // NOLINT
  {2, {1058, 1090}}, {2, {1059, 1091}}, {2, {1060, 1092}}, {2, {1061, 1093}},  // NOLINT
  {2, {1062, 1094}}, {2, {1063, 1095}}, {2, {1064, 1096}}, {2, {1065, 1097}},  // NOLINT
  {2, {1066, 1098}}, {2, {1067, 1099}}, {2, {1068, 1100}}, {2, {1069, 1101}},  // NOLINT
  {2, {1070, 1102}}, {2, {1071, 1103}}, {2, {1120, 1121}}, {2, {1122, 1123}},  // NOLINT
  {2, {1124, 1125}}, {2, {1126, 1127}}, {2, {1128, 1129}}, {2, {1130, 1131}},  // NOLINT
  {2, {1132, 1133}}, {2, {1134, 1135}}, {2, {1136, 1137}}, {2, {1138, 1139}},  // NOLINT
  {2, {1140, 1141}}, {2, {1142, 1143}}, {2, {1144, 1145}}, {2, {1146, 1147}},  // NOLINT
  {2, {1148, 1149}}, {2, {1150, 1151}}, {2, {1152, 1153}}, {2, {1162, 1163}},  // NOLINT
  {2, {1164, 1165}}, {2, {1166, 1167}}, {2, {1168, 1169}}, {2, {1170, 1171}},  // NOLINT
  {2, {1172, 1173}}, {2, {1174, 1175}}, {2, {1176, 1177}}, {2, {1178, 1179}},  // NOLINT
  {2, {1180, 1181}}, {2, {1182, 1183}}, {2, {1184, 1185}}, {2, {1186, 1187}},  // NOLINT
  {2, {1188, 1189}}, {2, {1190, 1191}}, {2, {1192, 1193}}, {2, {1194, 1195}},  // NOLINT
  {2, {1196, 1197}}, {2, {1198, 1199}}, {2, {1200, 1201}}, {2, {1202, 1203}},  // NOLINT
  {2, {1204, 1205}}, {2, {1206, 1207}}, {2, {1208, 1209}}, {2, {1210, 1211}},  // NOLINT
  {2, {1212, 1213}}, {2, {1214, 1215}}, {2, {1216, 1231}}, {2, {1217, 1218}},  // NOLINT
  {2, {1219, 1220}}, {2, {1221, 1222}}, {2, {1223, 1224}}, {2, {1225, 1226}},  // NOLINT
  {2, {1227, 1228}}, {2, {1229, 1230}}, {2, {1232, 1233}}, {2, {1234, 1235}},  // NOLINT
  {2, {1236, 1237}}, {2, {1238, 1239}}, {2, {1240, 1241}}, {2, {1242, 1243}},  // NOLINT
  {2, {1244, 1245}}, {2, {1246, 1247}}, {2, {1248, 1249}}, {2, {1250, 1251}},  // NOLINT
  {2, {1252, 1253}}, {2, {1254, 1255}}, {2, {1256, 1257}}, {2, {1258, 1259}},  // NOLINT
  {2, {1260, 1261}}, {2, {1262, 1263}}, {2, {1264, 1265}}, {2, {1266, 1267}},  // NOLINT
  {2, {1268, 1269}}, {2, {1270, 1271}}, {2, {1272, 1273}}, {2, {1274, 1275}},  // NOLINT
  {2, {1276, 1277}}, {2, {1278, 1279}}, {2, {1280, 1281}}, {2, {1282, 1283}},  // NOLINT
  {2, {1284, 1285}}, {2, {1286, 1287}}, {2, {1288, 1289}}, {2, {1290, 1291}},  // NOLINT
  {2, {1292, 1293}}, {2, {1294, 1295}}, {2, {1296, 1297}}, {2, {1298, 1299}},  // NOLINT
  {2, {1329, 1377}}, {2, {1330, 1378}}, {2, {1331, 1379}}, {2, {1332, 1380}},  // NOLINT
  {2, {1333, 1381}}, {2, {1334, 1382}}, {2, {1335, 1383}}, {2, {1336, 1384}},  // NOLINT
  {2, {1337, 1385}}, {2, {1338, 1386}}, {2, {1339, 1387}}, {2, {1340, 1388}},  // NOLINT
  {2, {1341, 1389}}, {2, {1342, 1390}}, {2, {1343, 1391}}, {2, {1344, 1392}},  // NOLINT
  {2, {1345, 1393}}, {2, {1346, 1394}}, {2, {1347, 1395}}, {2, {1348, 1396}},  // NOLINT
  {2, {1349, 1397}}, {2, {1350, 1398}}, {2, {1351, 1399}}, {2, {1352, 1400}},  // NOLINT
  {2, {1353, 1401}}, {2, {1354, 1402}}, {2, {1355, 1403}}, {2, {1356, 1404}},  // NOLINT
  {2, {1357, 1405}}, {2, {1358, 1406}}, {2, {1359, 1407}}, {2, {1360, 1408}},  // NOLINT
  {2, {1361, 1409}}, {2, {1362, 1410}}, {2, {1363, 1411}}, {2, {1364, 1412}},  // NOLINT
  {2, {1365, 1413}}, {2, {1366, 1414}}, {2, {4256, 11520}}, {2, {4257, 11521}},  // NOLINT
  {2, {4258, 11522}}, {2, {4259, 11523}}, {2, {4260, 11524}}, {2, {4261, 11525}},  // NOLINT
  {2, {4262, 11526}}, {2, {4263, 11527}}, {2, {4264, 11528}}, {2, {4265, 11529}},  // NOLINT
  {2, {4266, 11530}}, {2, {4267, 11531}}, {2, {4268, 11532}}, {2, {4269, 11533}},  // NOLINT
  {2, {4270, 11534}}, {2, {4271, 11535}}, {2, {4272, 11536}}, {2, {4273, 11537}},  // NOLINT
  {2, {4274, 11538}}, {2, {4275, 11539}}, {2, {4276, 11540}}, {2, {4277, 11541}},  // NOLINT
  {2, {4278, 11542}}, {2, {4279, 11543}}, {2, {4280, 11544}}, {2, {4281, 11545}},  // NOLINT
  {2, {4282, 11546}}, {2, {4283, 11547}}, {2, {4284, 11548}}, {2, {4285, 11549}},  // NOLINT
  {2, {4286, 11550}}, {2, {4287, 11551}}, {2, {4288, 11552}}, {2, {4289, 11553}},  // NOLINT
  {2, {4290, 11554}}, {2, {4291, 11555}}, {2, {4292, 11556}}, {2, {4293, 11557}},  // NOLINT
  {2, {7549, 11363}}, {2, {7680, 7681}}, {2, {7682, 7683}}, {2, {7684, 7685}},  // NOLINT
  {2, {7686, 7687}}, {2, {7688, 7689}}, {2, {7690, 7691}}, {2, {7692, 7693}},  // NOLINT
  {2, {7694, 7695}}, {2, {7696, 7697}}, {2, {7698, 7699}}, {2, {7700, 7701}},  // NOLINT
  {2, {7702, 7703}}, {2, {7704, 7705}}, {2, {7706, 7707}}, {2, {7708, 7709}},  // NOLINT
  {2, {7710, 7711}}, {2, {7712, 7713}}, {2, {7714, 7715}}, {2, {7716, 7717}},  // NOLINT
  {2, {7718, 7719}}, {2, {7720, 7721}}, {2, {7722, 7723}}, {2, {7724, 7725}},  // NOLINT
  {2, {7726, 7727}}, {2, {7728, 7729}}, {2, {7730, 7731}}, {2, {7732, 7733}},  // NOLINT
  {2, {7734, 7735}}, {2, {7736, 7737}}, {2, {7738, 7739}}, {2, {7740, 7741}},  // NOLINT
  {2, {7742, 7743}}, {2, {7744, 7745}}, {2, {7746, 7747}}, {2, {7748, 7749}},  // NOLINT
  {2, {7750, 7751}}, {2, {7752, 7753}}, {2, {7754, 7755}}, {2, {7756, 7757}},  // NOLINT
  {2, {7758, 7759}}, {2, {7760, 7761}}, {2, {7762, 7763}}, {2, {7764, 7765}},  // NOLINT
  {2, {7766, 7767}}, {2, {7768, 7769}}, {2, {7770, 7771}}, {2, {7772, 7773}},  // NOLINT
  {2, {7774, 7775}}, {3, {7776, 7777, 7835}}, {2, {7778, 7779}}, {2, {7780, 7781}},  // NOLINT
  {2, {7782, 7783}}, {2, {7784, 7785}}, {2, {7786, 7787}}, {2, {7788, 7789}},  // NOLINT
  {2, {7790, 7791}}, {2, {7792, 7793}}, {2, {7794, 7795}}, {2, {7796, 7797}},  // NOLINT
  {2, {7798, 7799}}, {2, {7800, 7801}}, {2, {7802, 7803}}, {2, {7804, 7805}},  // NOLINT
  {2, {7806, 7807}}, {2, {7808, 7809}}, {2, {7810, 7811}}, {2, {7812, 7813}},  // NOLINT
  {2, {7814, 7815}}, {2, {7816, 7817}}, {2, {7818, 7819}}, {2, {7820, 7821}},  // NOLINT
  {2, {7822, 7823}}, {2, {7824, 7825}}, {2, {7826, 7827}}, {2, {7828, 7829}},  // NOLINT
  {2, {7840, 7841}}, {2, {7842, 7843}}, {2, {7844, 7845}}, {2, {7846, 7847}},  // NOLINT
  {2, {7848, 7849}}, {2, {7850, 7851}}, {2, {7852, 7853}}, {2, {7854, 7855}},  // NOLINT
  {2, {7856, 7857}}, {2, {7858, 7859}}, {2, {7860, 7861}}, {2, {7862, 7863}},  // NOLINT
  {2, {7864, 7865}}, {2, {7866, 7867}}, {2, {7868, 7869}}, {2, {7870, 7871}},  // NOLINT
  {2, {7872, 7873}}, {2, {7874, 7875}}, {2, {7876, 7877}}, {2, {7878, 7879}},  // NOLINT
  {2, {7880, 7881}}, {2, {7882, 7883}}, {2, {7884, 7885}}, {2, {7886, 7887}},  // NOLINT
  {2, {7888, 7889}}, {2, {7890, 7891}}, {2, {7892, 7893}}, {2, {7894, 7895}},  // NOLINT
  {2, {7896, 7897}}, {2, {7898, 7899}}, {2, {7900, 7901}}, {2, {7902, 7903}},  // NOLINT
  {2, {7904, 7905}}, {2, {7906, 7907}}, {2, {7908, 7909}}, {2, {7910, 7911}},  // NOLINT
  {2, {7912, 7913}}, {2, {7914, 7915}}, {2, {7916, 7917}}, {2, {7918, 7919}},  // NOLINT
  {2, {7920, 7921}}, {2, {7922, 7923}}, {2, {7924, 7925}}, {2, {7926, 7927}},  // NOLINT
  {2, {7928, 7929}}, {2, {7936, 7944}}, {2, {7937, 7945}}, {2, {7938, 7946}},  // NOLINT
  {2, {7939, 7947}}, {2, {7940, 7948}}, {2, {7941, 7949}}, {2, {7942, 7950}},  // NOLINT
  {2, {7943, 7951}}, {2, {7952, 7960}}, {2, {7953, 7961}}, {2, {7954, 7962}},  // NOLINT
  {2, {7955, 7963}}, {2, {7956, 7964}}, {2, {7957, 7965}}, {2, {7968, 7976}},  // NOLINT
  {2, {7969, 7977}}, {2, {7970, 7978}}, {2, {7971, 7979}}, {2, {7972, 7980}},  // NOLINT
  {2, {7973, 7981}}, {2, {7974, 7982}}, {2, {7975, 7983}}, {2, {7984, 7992}},  // NOLINT
  {2, {7985, 7993}}, {2, {7986, 7994}}, {2, {7987, 7995}}, {2, {7988, 7996}},  // NOLINT
  {2, {7989, 7997}}, {2, {7990, 7998}}, {2, {7991, 7999}}, {2, {8000, 8008}},  // NOLINT
  {2, {8001, 8009}}, {2, {8002, 8010}}, {2, {8003, 8011}}, {2, {8004, 8012}},  // NOLINT
  {2, {8005, 8013}}, {2, {8017, 8025}}, {2, {8019, 8027}}, {2, {8021, 8029}},  // NOLINT
  {2, {8023, 8031}}, {2, {8032, 8040}}, {2, {8033, 8041}}, {2, {8034, 8042}},  // NOLINT
  {2, {8035, 8043}}, {2, {8036, 8044}}, {2, {8037, 8045}}, {2, {8038, 8046}},  // NOLINT
  {2, {8039, 8047}}, {2, {8048, 8122}}, {2, {8049, 8123}}, {2, {8050, 8136}},  // NOLINT
  {2, {8051, 8137}}, {2, {8052, 8138}}, {2, {8053, 8139}}, {2, {8054, 8154}},  // NOLINT
  {2, {8055, 8155}}, {2, {8056, 8184}}, {2, {8057, 8185}}, {2, {8058, 8170}},  // NOLINT
  {2, {8059, 8171}}, {2, {8060, 8186}}, {2, {8061, 8187}}, {2, {8112, 8120}},  // NOLINT
  {2, {8113, 8121}}, {2, {8144, 8152}}, {2, {8145, 8153}}, {2, {8160, 8168}},  // NOLINT
  {2, {8161, 8169}}, {2, {8165, 8172}}, {2, {8498, 8526}}, {2, {8544, 8560}},  // NOLINT
  {2, {8545, 8561}}, {2, {8546, 8562}}, {2, {8547, 8563}}, {2, {8548, 8564}},  // NOLINT
  {2, {8549, 8565}}, {2, {8550, 8566}}, {2, {8551, 8567}}, {2, {8552, 8568}},  // NOLINT
  {2, {8553, 8569}}, {2, {8554, 8570}}, {2, {8555, 8571}}, {2, {8556, 8572}},  // NOLINT
  {2, {8557, 8573}}, {2, {8558, 8574}}, {2, {8559, 8575}}, {2, {8579, 8580}},  // NOLINT
  {2, {9398, 9424}}, {2, {9399, 9425}}, {2, {9400, 9426}}, {2, {9401, 9427}},  // NOLINT
  {2, {9402, 9428}}, {2, {9403, 9429}}, {2, {9404, 9430}}, {2, {9405, 9431}},  // NOLINT
  {2, {9406, 9432}}, {2, {9407, 9433}}, {2, {9408, 9434}}, {2, {9409, 9435}},  // NOLINT
  {2, {9410, 9436}}, {2, {9411, 9437}}, {2, {9412, 9438}}, {2, {9413, 9439}},  // NOLINT
  {2, {9414, 9440}}, {2, {9415, 9441}}, {2, {9416, 9442}}, {2, {9417, 9443}},  // NOLINT
  {2, {9418, 9444}}, {2, {9419, 9445}}, {2, {9420, 9446}}, {2, {9421, 9447}},  // NOLINT
  {2, {9422, 9448}}, {2, {9423, 9449}}, {2, {11264, 11312}}, {2, {11265, 11313}},  // NOLINT
  {2, {11266, 11314}}, {2, {11267, 11315}}, {2, {11268, 11316}}, {2, {11269, 11317}},  // NOLINT
  {2, {11270, 11318}}, {2, {11271, 11319}}, {2, {11272, 11320}}, {2, {11273, 11321}},  // NOLINT
  {2, {11274, 11322}}, {2, {11275, 11323}}, {2, {11276, 11324}}, {2, {11277, 11325}},  // NOLINT
  {2, {11278, 11326}}, {2, {11279, 11327}}, {2, {11280, 11328}}, {2, {11281, 11329}},  // NOLINT
  {2, {11282, 11330}}, {2, {11283, 11331}}, {2, {11284, 11332}}, {2, {11285, 11333}},  // NOLINT
  {2, {11286, 11334}}, {2, {11287, 11335}}, {2, {11288, 11336}}, {2, {11289, 11337}},  // NOLINT
  {2, {11290, 11338}}, {2, {11291, 11339}}, {2, {11292, 11340}}, {2, {11293, 11341}},  // NOLINT
  {2, {11294, 11342}}, {2, {11295, 11343}}, {2, {11296, 11344}}, {2, {11297, 11345}},  // NOLINT
  {2, {11298, 11346}}, {2, {11299, 11347}}, {2, {11300, 11348}}, {2, {11301, 11349}},  // NOLINT
  {2, {11302, 11350}}, {2, {11303, 11351}}, {2, {11304, 11352}}, {2, {11305, 11353}},  // NOLINT
  {2, {11306, 11354}}, {2, {11307, 11355}}, {2, {11308, 11356}}, {2, {11309, 11357}},  // NOLINT
  {2, {11310, 11358}}, {2, {11360, 11361}}, {2, {11367, 11368}}, {2, {11369, 11370}},  // NOLINT
  {2, {11371, 11372}}, {2, {11381, 11382}}, {2, {11392, 11393}}, {2, {11394, 11395}},  // NOLINT
  {2, {11396, 11397}}, {2, {11398, 11399}}, {2, {11400, 11401}}, {2, {11402, 11403}},  // NOLINT
  {2, {11404, 11405}}, {2, {11406, 11407}}, {2, {11408, 11409}}, {2, {11410, 11411}},  // NOLINT
  {2, {11412, 11413}}, {2, {11414, 11415}}, {2, {11416, 11417}}, {2, {11418, 11419}},  // NOLINT
  {2, {11420, 11421}}, {2, {11422, 11423}}, {2, {11424, 11425}}, {2, {11426, 11427}},  // NOLINT
  {2, {11428, 11429}}, {2, {11430, 11431}}, {2, {11432, 11433}}, {2, {11434, 11435}},  // NOLINT
  {2, {11436, 11437}}, {2, {11438, 11439}}, {2, {11440, 11441}}, {2, {11442, 11443}},  // NOLINT
  {2, {11444, 11445}}, {2, {11446, 11447}}, {2, {11448, 11449}}, {2, {11450, 11451}},  // NOLINT
  {2, {11452, 11453}}, {2, {11454, 11455}}, {2, {11456, 11457}}, {2, {11458, 11459}},  // NOLINT
  {2, {11460, 11461}}, {2, {11462, 11463}}, {2, {11464, 11465}}, {2, {11466, 11467}},  // NOLINT
  {2, {11468, 11469}}, {2, {11470, 11471}}, {2, {11472, 11473}}, {2, {11474, 11475}},  // NOLINT
  {2, {11476, 11477}}, {2, {11478, 11479}}, {2, {11480, 11481}}, {2, {11482, 11483}},  // NOLINT
  {2, {11484, 11485}}, {2, {11486, 11487}}, {2, {11488, 11489}}, {2, {11490, 11491}},  // NOLINT
  {0, {0}} }; // NOLINT
static const uint16_t kEcma262UnCanonicalizeTable0Size = 1656;
static const int32_t kEcma262UnCanonicalizeTable0[3312] = {
  65, 1, 66, 5, 67, 9, 68, 13, 69, 17, 70, 21, 71, 25, 72, 29,  // NOLINT
  73, 33, 74, 37, 75, 41, 76, 45, 77, 49, 78, 53, 79, 57, 80, 61,  // NOLINT
  81, 65, 82, 69, 83, 73, 84, 77, 85, 81, 86, 85, 87, 89, 88, 93,  // NOLINT
  89, 97, 90, 101, 97, 1, 98, 5, 99, 9, 100, 13, 101, 17, 102, 21,  // NOLINT
  103, 25, 104, 29, 105, 33, 106, 37, 107, 41, 108, 45, 109, 49, 110, 53,  // NOLINT
  111, 57, 112, 61, 113, 65, 114, 69, 115, 73, 116, 77, 117, 81, 118, 85,  // NOLINT
  119, 89, 120, 93, 121, 97, 122, 101, 181, 105, 192, 109, 193, 113, 194, 117,  // NOLINT
  195, 121, 196, 125, 197, 129, 198, 133, 199, 137, 200, 141, 201, 145, 202, 149,  // NOLINT
  203, 153, 204, 157, 205, 161, 206, 165, 207, 169, 208, 173, 209, 177, 210, 181,  // NOLINT
  211, 185, 212, 189, 213, 193, 214, 197, 216, 201, 217, 205, 218, 209, 219, 213,  // NOLINT
  220, 217, 221, 221, 222, 225, 224, 109, 225, 113, 226, 117, 227, 121, 228, 125,  // NOLINT
  229, 129, 230, 133, 231, 137, 232, 141, 233, 145, 234, 149, 235, 153, 236, 157,  // NOLINT
  237, 161, 238, 165, 239, 169, 240, 173, 241, 177, 242, 181, 243, 185, 244, 189,  // NOLINT
  245, 193, 246, 197, 248, 201, 249, 205, 250, 209, 251, 213, 252, 217, 253, 221,  // NOLINT
  254, 225, 255, 229, 256, 233, 257, 233, 258, 237, 259, 237, 260, 241, 261, 241,  // NOLINT
  262, 245, 263, 245, 264, 249, 265, 249, 266, 253, 267, 253, 268, 257, 269, 257,  // NOLINT
  270, 261, 271, 261, 272, 265, 273, 265, 274, 269, 275, 269, 276, 273, 277, 273,  // NOLINT
  278, 277, 279, 277, 280, 281, 281, 281, 282, 285, 283, 285, 284, 289, 285, 289,  // NOLINT
  286, 293, 287, 293, 288, 297, 289, 297, 290, 301, 291, 301, 292, 305, 293, 305,  // NOLINT
  294, 309, 295, 309, 296, 313, 297, 313, 298, 317, 299, 317, 300, 321, 301, 321,  // NOLINT
  302, 325, 303, 325, 306, 329, 307, 329, 308, 333, 309, 333, 310, 337, 311, 337,  // NOLINT
  313, 341, 314, 341, 315, 345, 316, 345, 317, 349, 318, 349, 319, 353, 320, 353,  // NOLINT
  321, 357, 322, 357, 323, 361, 324, 361, 325, 365, 326, 365, 327, 369, 328, 369,  // NOLINT
  330, 373, 331, 373, 332, 377, 333, 377, 334, 381, 335, 381, 336, 385, 337, 385,  // NOLINT
  338, 389, 339, 389, 340, 393, 341, 393, 342, 397, 343, 397, 344, 401, 345, 401,  // NOLINT
  346, 405, 347, 405, 348, 409, 349, 409, 350, 413, 351, 413, 352, 417, 353, 417,  // NOLINT
  354, 421, 355, 421, 356, 425, 357, 425, 358, 429, 359, 429, 360, 433, 361, 433,  // NOLINT
  362, 437, 363, 437, 364, 441, 365, 441, 366, 445, 367, 445, 368, 449, 369, 449,  // NOLINT
  370, 453, 371, 453, 372, 457, 373, 457, 374, 461, 375, 461, 376, 229, 377, 465,  // NOLINT
  378, 465, 379, 469, 380, 469, 381, 473, 382, 473, 384, 477, 385, 481, 386, 485,  // NOLINT
  387, 485, 388, 489, 389, 489, 390, 493, 391, 497, 392, 497, 393, 501, 394, 505,  // NOLINT
  395, 509, 396, 509, 398, 513, 399, 517, 400, 521, 401, 525, 402, 525, 403, 529,  // NOLINT
  404, 533, 405, 537, 406, 541, 407, 545, 408, 549, 409, 549, 410, 553, 412, 557,  // NOLINT
  413, 561, 414, 565, 415, 569, 416, 573, 417, 573, 418, 577, 419, 577, 420, 581,  // NOLINT
  421, 581, 422, 585, 423, 589, 424, 589, 425, 593, 428, 597, 429, 597, 430, 601,  // NOLINT
  431, 605, 432, 605, 433, 609, 434, 613, 435, 617, 436, 617, 437, 621, 438, 621,  // NOLINT
  439, 625, 440, 629, 441, 629, 444, 633, 445, 633, 447, 637, 452, 641, 453, 641,  // NOLINT
  454, 641, 455, 645, 456, 645, 457, 645, 458, 649, 459, 649, 460, 649, 461, 653,  // NOLINT
  462, 653, 463, 657, 464, 657, 465, 661, 466, 661, 467, 665, 468, 665, 469, 669,  // NOLINT
  470, 669, 471, 673, 472, 673, 473, 677, 474, 677, 475, 681, 476, 681, 477, 513,  // NOLINT
  478, 685, 479, 685, 480, 689, 481, 689, 482, 693, 483, 693, 484, 697, 485, 697,  // NOLINT
  486, 701, 487, 701, 488, 705, 489, 705, 490, 709, 491, 709, 492, 713, 493, 713,  // NOLINT
  494, 717, 495, 717, 497, 721, 498, 721, 499, 721, 500, 725, 501, 725, 502, 537,  // NOLINT
  503, 637, 504, 729, 505, 729, 506, 733, 507, 733, 508, 737, 509, 737, 510, 741,  // NOLINT
  511, 741, 512, 745, 513, 745, 514, 749, 515, 749, 516, 753, 517, 753, 518, 757,  // NOLINT
  519, 757, 520, 761, 521, 761, 522, 765, 523, 765, 524, 769, 525, 769, 526, 773,  // NOLINT
  527, 773, 528, 777, 529, 777, 530, 781, 531, 781, 532, 785, 533, 785, 534, 789,  // NOLINT
  535, 789, 536, 793, 537, 793, 538, 797, 539, 797, 540, 801, 541, 801, 542, 805,  // NOLINT
  543, 805, 544, 565, 546, 809, 547, 809, 548, 813, 549, 813, 550, 817, 551, 817,  // NOLINT
  552, 821, 553, 821, 554, 825, 555, 825, 556, 829, 557, 829, 558, 833, 559, 833,  // NOLINT
  560, 837, 561, 837, 562, 841, 563, 841, 570, 845, 571, 849, 572, 849, 573, 553,  // NOLINT
  574, 853, 577, 857, 578, 857, 579, 477, 580, 861, 581, 865, 582, 869, 583, 869,  // NOLINT
  584, 873, 585, 873, 586, 877, 587, 877, 588, 881, 589, 881, 590, 885, 591, 885,  // NOLINT
  595, 481, 596, 493, 598, 501, 599, 505, 601, 517, 603, 521, 608, 529, 611, 533,  // NOLINT
  616, 545, 617, 541, 619, 889, 623, 557, 626, 561, 629, 569, 637, 893, 640, 585,  // NOLINT
  643, 593, 648, 601, 649, 861, 650, 609, 651, 613, 652, 865, 658, 625, 837, 897,  // NOLINT
  891, 901, 892, 905, 893, 909, 902, 913, 904, 917, 905, 921, 906, 925, 908, 929,  // NOLINT
  910, 933, 911, 937, 913, 941, 914, 945, 915, 949, 916, 953, 917, 957, 918, 961,  // NOLINT
  919, 965, 920, 969, 921, 897, 922, 973, 923, 977, 924, 105, 925, 981, 926, 985,  // NOLINT
  927, 989, 928, 993, 929, 997, 931, 1001, 932, 1005, 933, 1009, 934, 1013, 935, 1017,  // NOLINT
  936, 1021, 937, 1025, 938, 1029, 939, 1033, 940, 913, 941, 917, 942, 921, 943, 925,  // NOLINT
  945, 941, 946, 945, 947, 949, 948, 953, 949, 957, 950, 961, 951, 965, 952, 969,  // NOLINT
  953, 897, 954, 973, 955, 977, 956, 105, 957, 981, 958, 985, 959, 989, 960, 993,  // NOLINT
  961, 997, 962, 1001, 963, 1001, 964, 1005, 965, 1009, 966, 1013, 967, 1017, 968, 1021,  // NOLINT
  969, 1025, 970, 1029, 971, 1033, 972, 929, 973, 933, 974, 937, 976, 945, 977, 969,  // NOLINT
  981, 1013, 982, 993, 984, 1037, 985, 1037, 986, 1041, 987, 1041, 988, 1045, 989, 1045,  // NOLINT
  990, 1049, 991, 1049, 992, 1053, 993, 1053, 994, 1057, 995, 1057, 996, 1061, 997, 1061,  // NOLINT
  998, 1065, 999, 1065, 1000, 1069, 1001, 1069, 1002, 1073, 1003, 1073, 1004, 1077, 1005, 1077,  // NOLINT
  1006, 1081, 1007, 1081, 1008, 973, 1009, 997, 1010, 1085, 1013, 957, 1015, 1089, 1016, 1089,  // NOLINT
  1017, 1085, 1018, 1093, 1019, 1093, 1021, 901, 1022, 905, 1023, 909, 1024, 1097, 1025, 1101,  // NOLINT
  1026, 1105, 1027, 1109, 1028, 1113, 1029, 1117, 1030, 1121, 1031, 1125, 1032, 1129, 1033, 1133,  // NOLINT
  1034, 1137, 1035, 1141, 1036, 1145, 1037, 1149, 1038, 1153, 1039, 1157, 1040, 1161, 1041, 1165,  // NOLINT
  1042, 1169, 1043, 1173, 1044, 1177, 1045, 1181, 1046, 1185, 1047, 1189, 1048, 1193, 1049, 1197,  // NOLINT
  1050, 1201, 1051, 1205, 1052, 1209, 1053, 1213, 1054, 1217, 1055, 1221, 1056, 1225, 1057, 1229,  // NOLINT
  1058, 1233, 1059, 1237, 1060, 1241, 1061, 1245, 1062, 1249, 1063, 1253, 1064, 1257, 1065, 1261,  // NOLINT
  1066, 1265, 1067, 1269, 1068, 1273, 1069, 1277, 1070, 1281, 1071, 1285, 1072, 1161, 1073, 1165,  // NOLINT
  1074, 1169, 1075, 1173, 1076, 1177, 1077, 1181, 1078, 1185, 1079, 1189, 1080, 1193, 1081, 1197,  // NOLINT
  1082, 1201, 1083, 1205, 1084, 1209, 1085, 1213, 1086, 1217, 1087, 1221, 1088, 1225, 1089, 1229,  // NOLINT
  1090, 1233, 1091, 1237, 1092, 1241, 1093, 1245, 1094, 1249, 1095, 1253, 1096, 1257, 1097, 1261,  // NOLINT
  1098, 1265, 1099, 1269, 1100, 1273, 1101, 1277, 1102, 1281, 1103, 1285, 1104, 1097, 1105, 1101,  // NOLINT
  1106, 1105, 1107, 1109, 1108, 1113, 1109, 1117, 1110, 1121, 1111, 1125, 1112, 1129, 1113, 1133,  // NOLINT
  1114, 1137, 1115, 1141, 1116, 1145, 1117, 1149, 1118, 1153, 1119, 1157, 1120, 1289, 1121, 1289,  // NOLINT
  1122, 1293, 1123, 1293, 1124, 1297, 1125, 1297, 1126, 1301, 1127, 1301, 1128, 1305, 1129, 1305,  // NOLINT
  1130, 1309, 1131, 1309, 1132, 1313, 1133, 1313, 1134, 1317, 1135, 1317, 1136, 1321, 1137, 1321,  // NOLINT
  1138, 1325, 1139, 1325, 1140, 1329, 1141, 1329, 1142, 1333, 1143, 1333, 1144, 1337, 1145, 1337,  // NOLINT
  1146, 1341, 1147, 1341, 1148, 1345, 1149, 1345, 1150, 1349, 1151, 1349, 1152, 1353, 1153, 1353,  // NOLINT
  1162, 1357, 1163, 1357, 1164, 1361, 1165, 1361, 1166, 1365, 1167, 1365, 1168, 1369, 1169, 1369,  // NOLINT
  1170, 1373, 1171, 1373, 1172, 1377, 1173, 1377, 1174, 1381, 1175, 1381, 1176, 1385, 1177, 1385,  // NOLINT
  1178, 1389, 1179, 1389, 1180, 1393, 1181, 1393, 1182, 1397, 1183, 1397, 1184, 1401, 1185, 1401,  // NOLINT
  1186, 1405, 1187, 1405, 1188, 1409, 1189, 1409, 1190, 1413, 1191, 1413, 1192, 1417, 1193, 1417,  // NOLINT
  1194, 1421, 1195, 1421, 1196, 1425, 1197, 1425, 1198, 1429, 1199, 1429, 1200, 1433, 1201, 1433,  // NOLINT
  1202, 1437, 1203, 1437, 1204, 1441, 1205, 1441, 1206, 1445, 1207, 1445, 1208, 1449, 1209, 1449,  // NOLINT
  1210, 1453, 1211, 1453, 1212, 1457, 1213, 1457, 1214, 1461, 1215, 1461, 1216, 1465, 1217, 1469,  // NOLINT
  1218, 1469, 1219, 1473, 1220, 1473, 1221, 1477, 1222, 1477, 1223, 1481, 1224, 1481, 1225, 1485,  // NOLINT
  1226, 1485, 1227, 1489, 1228, 1489, 1229, 1493, 1230, 1493, 1231, 1465, 1232, 1497, 1233, 1497,  // NOLINT
  1234, 1501, 1235, 1501, 1236, 1505, 1237, 1505, 1238, 1509, 1239, 1509, 1240, 1513, 1241, 1513,  // NOLINT
  1242, 1517, 1243, 1517, 1244, 1521, 1245, 1521, 1246, 1525, 1247, 1525, 1248, 1529, 1249, 1529,  // NOLINT
  1250, 1533, 1251, 1533, 1252, 1537, 1253, 1537, 1254, 1541, 1255, 1541, 1256, 1545, 1257, 1545,  // NOLINT
  1258, 1549, 1259, 1549, 1260, 1553, 1261, 1553, 1262, 1557, 1263, 1557, 1264, 1561, 1265, 1561,  // NOLINT
  1266, 1565, 1267, 1565, 1268, 1569, 1269, 1569, 1270, 1573, 1271, 1573, 1272, 1577, 1273, 1577,  // NOLINT
  1274, 1581, 1275, 1581, 1276, 1585, 1277, 1585, 1278, 1589, 1279, 1589, 1280, 1593, 1281, 1593,  // NOLINT
  1282, 1597, 1283, 1597, 1284, 1601, 1285, 1601, 1286, 1605, 1287, 1605, 1288, 1609, 1289, 1609,  // NOLINT
  1290, 1613, 1291, 1613, 1292, 1617, 1293, 1617, 1294, 1621, 1295, 1621, 1296, 1625, 1297, 1625,  // NOLINT
  1298, 1629, 1299, 1629, 1329, 1633, 1330, 1637, 1331, 1641, 1332, 1645, 1333, 1649, 1334, 1653,  // NOLINT
  1335, 1657, 1336, 1661, 1337, 1665, 1338, 1669, 1339, 1673, 1340, 1677, 1341, 1681, 1342, 1685,  // NOLINT
  1343, 1689, 1344, 1693, 1345, 1697, 1346, 1701, 1347, 1705, 1348, 1709, 1349, 1713, 1350, 1717,  // NOLINT
  1351, 1721, 1352, 1725, 1353, 1729, 1354, 1733, 1355, 1737, 1356, 1741, 1357, 1745, 1358, 1749,  // NOLINT
  1359, 1753, 1360, 1757, 1361, 1761, 1362, 1765, 1363, 1769, 1364, 1773, 1365, 1777, 1366, 1781,  // NOLINT
  1377, 1633, 1378, 1637, 1379, 1641, 1380, 1645, 1381, 1649, 1382, 1653, 1383, 1657, 1384, 1661,  // NOLINT
  1385, 1665, 1386, 1669, 1387, 1673, 1388, 1677, 1389, 1681, 1390, 1685, 1391, 1689, 1392, 1693,  // NOLINT
  1393, 1697, 1394, 1701, 1395, 1705, 1396, 1709, 1397, 1713, 1398, 1717, 1399, 1721, 1400, 1725,  // NOLINT
  1401, 1729, 1402, 1733, 1403, 1737, 1404, 1741, 1405, 1745, 1406, 1749, 1407, 1753, 1408, 1757,  // NOLINT
  1409, 1761, 1410, 1765, 1411, 1769, 1412, 1773, 1413, 1777, 1414, 1781, 4256, 1785, 4257, 1789,  // NOLINT
  4258, 1793, 4259, 1797, 4260, 1801, 4261, 1805, 4262, 1809, 4263, 1813, 4264, 1817, 4265, 1821,  // NOLINT
  4266, 1825, 4267, 1829, 4268, 1833, 4269, 1837, 4270, 1841, 4271, 1845, 4272, 1849, 4273, 1853,  // NOLINT
  4274, 1857, 4275, 1861, 4276, 1865, 4277, 1869, 4278, 1873, 4279, 1877, 4280, 1881, 4281, 1885,  // NOLINT
  4282, 1889, 4283, 1893, 4284, 1897, 4285, 1901, 4286, 1905, 4287, 1909, 4288, 1913, 4289, 1917,  // NOLINT
  4290, 1921, 4291, 1925, 4292, 1929, 4293, 1933, 7549, 1937, 7680, 1941, 7681, 1941, 7682, 1945,  // NOLINT
  7683, 1945, 7684, 1949, 7685, 1949, 7686, 1953, 7687, 1953, 7688, 1957, 7689, 1957, 7690, 1961,  // NOLINT
  7691, 1961, 7692, 1965, 7693, 1965, 7694, 1969, 7695, 1969, 7696, 1973, 7697, 1973, 7698, 1977,  // NOLINT
  7699, 1977, 7700, 1981, 7701, 1981, 7702, 1985, 7703, 1985, 7704, 1989, 7705, 1989, 7706, 1993,  // NOLINT
  7707, 1993, 7708, 1997, 7709, 1997, 7710, 2001, 7711, 2001, 7712, 2005, 7713, 2005, 7714, 2009,  // NOLINT
  7715, 2009, 7716, 2013, 7717, 2013, 7718, 2017, 7719, 2017, 7720, 2021, 7721, 2021, 7722, 2025,  // NOLINT
  7723, 2025, 7724, 2029, 7725, 2029, 7726, 2033, 7727, 2033, 7728, 2037, 7729, 2037, 7730, 2041,  // NOLINT
  7731, 2041, 7732, 2045, 7733, 2045, 7734, 2049, 7735, 2049, 7736, 2053, 7737, 2053, 7738, 2057,  // NOLINT
  7739, 2057, 7740, 2061, 7741, 2061, 7742, 2065, 7743, 2065, 7744, 2069, 7745, 2069, 7746, 2073,  // NOLINT
  7747, 2073, 7748, 2077, 7749, 2077, 7750, 2081, 7751, 2081, 7752, 2085, 7753, 2085, 7754, 2089,  // NOLINT
  7755, 2089, 7756, 2093, 7757, 2093, 7758, 2097, 7759, 2097, 7760, 2101, 7761, 2101, 7762, 2105,  // NOLINT
  7763, 2105, 7764, 2109, 7765, 2109, 7766, 2113, 7767, 2113, 7768, 2117, 7769, 2117, 7770, 2121,  // NOLINT
  7771, 2121, 7772, 2125, 7773, 2125, 7774, 2129, 7775, 2129, 7776, 2133, 7777, 2133, 7778, 2137,  // NOLINT
  7779, 2137, 7780, 2141, 7781, 2141, 7782, 2145, 7783, 2145, 7784, 2149, 7785, 2149, 7786, 2153,  // NOLINT
  7787, 2153, 7788, 2157, 7789, 2157, 7790, 2161, 7791, 2161, 7792, 2165, 7793, 2165, 7794, 2169,  // NOLINT
  7795, 2169, 7796, 2173, 7797, 2173, 7798, 2177, 7799, 2177, 7800, 2181, 7801, 2181, 7802, 2185,  // NOLINT
  7803, 2185, 7804, 2189, 7805, 2189, 7806, 2193, 7807, 2193, 7808, 2197, 7809, 2197, 7810, 2201,  // NOLINT
  7811, 2201, 7812, 2205, 7813, 2205, 7814, 2209, 7815, 2209, 7816, 2213, 7817, 2213, 7818, 2217,  // NOLINT
  7819, 2217, 7820, 2221, 7821, 2221, 7822, 2225, 7823, 2225, 7824, 2229, 7825, 2229, 7826, 2233,  // NOLINT
  7827, 2233, 7828, 2237, 7829, 2237, 7835, 2133, 7840, 2241, 7841, 2241, 7842, 2245, 7843, 2245,  // NOLINT
  7844, 2249, 7845, 2249, 7846, 2253, 7847, 2253, 7848, 2257, 7849, 2257, 7850, 2261, 7851, 2261,  // NOLINT
  7852, 2265, 7853, 2265, 7854, 2269, 7855, 2269, 7856, 2273, 7857, 2273, 7858, 2277, 7859, 2277,  // NOLINT
  7860, 2281, 7861, 2281, 7862, 2285, 7863, 2285, 7864, 2289, 7865, 2289, 7866, 2293, 7867, 2293,  // NOLINT
  7868, 2297, 7869, 2297, 7870, 2301, 7871, 2301, 7872, 2305, 7873, 2305, 7874, 2309, 7875, 2309,  // NOLINT
  7876, 2313, 7877, 2313, 7878, 2317, 7879, 2317, 7880, 2321, 7881, 2321, 7882, 2325, 7883, 2325,  // NOLINT
  7884, 2329, 7885, 2329, 7886, 2333, 7887, 2333, 7888, 2337, 7889, 2337, 7890, 2341, 7891, 2341,  // NOLINT
  7892, 2345, 7893, 2345, 7894, 2349, 7895, 2349, 7896, 2353, 7897, 2353, 7898, 2357, 7899, 2357,  // NOLINT
  7900, 2361, 7901, 2361, 7902, 2365, 7903, 2365, 7904, 2369, 7905, 2369, 7906, 2373, 7907, 2373,  // NOLINT
  7908, 2377, 7909, 2377, 7910, 2381, 7911, 2381, 7912, 2385, 7913, 2385, 7914, 2389, 7915, 2389,  // NOLINT
  7916, 2393, 7917, 2393, 7918, 2397, 7919, 2397, 7920, 2401, 7921, 2401, 7922, 2405, 7923, 2405,  // NOLINT
  7924, 2409, 7925, 2409, 7926, 2413, 7927, 2413, 7928, 2417, 7929, 2417, 7936, 2421, 7937, 2425,  // NOLINT
  7938, 2429, 7939, 2433, 7940, 2437, 7941, 2441, 7942, 2445, 7943, 2449, 7944, 2421, 7945, 2425,  // NOLINT
  7946, 2429, 7947, 2433, 7948, 2437, 7949, 2441, 7950, 2445, 7951, 2449, 7952, 2453, 7953, 2457,  // NOLINT
  7954, 2461, 7955, 2465, 7956, 2469, 7957, 2473, 7960, 2453, 7961, 2457, 7962, 2461, 7963, 2465,  // NOLINT
  7964, 2469, 7965, 2473, 7968, 2477, 7969, 2481, 7970, 2485, 7971, 2489, 7972, 2493, 7973, 2497,  // NOLINT
  7974, 2501, 7975, 2505, 7976, 2477, 7977, 2481, 7978, 2485, 7979, 2489, 7980, 2493, 7981, 2497,  // NOLINT
  7982, 2501, 7983, 2505, 7984, 2509, 7985, 2513, 7986, 2517, 7987, 2521, 7988, 2525, 7989, 2529,  // NOLINT
  7990, 2533, 7991, 2537, 7992, 2509, 7993, 2513, 7994, 2517, 7995, 2521, 7996, 2525, 7997, 2529,  // NOLINT
  7998, 2533, 7999, 2537, 8000, 2541, 8001, 2545, 8002, 2549, 8003, 2553, 8004, 2557, 8005, 2561,  // NOLINT
  8008, 2541, 8009, 2545, 8010, 2549, 8011, 2553, 8012, 2557, 8013, 2561, 8017, 2565, 8019, 2569,  // NOLINT
  8021, 2573, 8023, 2577, 8025, 2565, 8027, 2569, 8029, 2573, 8031, 2577, 8032, 2581, 8033, 2585,  // NOLINT
  8034, 2589, 8035, 2593, 8036, 2597, 8037, 2601, 8038, 2605, 8039, 2609, 8040, 2581, 8041, 2585,  // NOLINT
  8042, 2589, 8043, 2593, 8044, 2597, 8045, 2601, 8046, 2605, 8047, 2609, 8048, 2613, 8049, 2617,  // NOLINT
  8050, 2621, 8051, 2625, 8052, 2629, 8053, 2633, 8054, 2637, 8055, 2641, 8056, 2645, 8057, 2649,  // NOLINT
  8058, 2653, 8059, 2657, 8060, 2661, 8061, 2665, 8112, 2669, 8113, 2673, 8120, 2669, 8121, 2673,  // NOLINT
  8122, 2613, 8123, 2617, 8126, 897, 8136, 2621, 8137, 2625, 8138, 2629, 8139, 2633, 8144, 2677,  // NOLINT
  8145, 2681, 8152, 2677, 8153, 2681, 8154, 2637, 8155, 2641, 8160, 2685, 8161, 2689, 8165, 2693,  // NOLINT
  8168, 2685, 8169, 2689, 8170, 2653, 8171, 2657, 8172, 2693, 8184, 2645, 8185, 2649, 8186, 2661,  // NOLINT
  8187, 2665, 8498, 2697, 8526, 2697, 8544, 2701, 8545, 2705, 8546, 2709, 8547, 2713, 8548, 2717,  // NOLINT
  8549, 2721, 8550, 2725, 8551, 2729, 8552, 2733, 8553, 2737, 8554, 2741, 8555, 2745, 8556, 2749,  // NOLINT
  8557, 2753, 8558, 2757, 8559, 2761, 8560, 2701, 8561, 2705, 8562, 2709, 8563, 2713, 8564, 2717,  // NOLINT
  8565, 2721, 8566, 2725, 8567, 2729, 8568, 2733, 8569, 2737, 8570, 2741, 8571, 2745, 8572, 2749,  // NOLINT
  8573, 2753, 8574, 2757, 8575, 2761, 8579, 2765, 8580, 2765, 9398, 2769, 9399, 2773, 9400, 2777,  // NOLINT
  9401, 2781, 9402, 2785, 9403, 2789, 9404, 2793, 9405, 2797, 9406, 2801, 9407, 2805, 9408, 2809,  // NOLINT
  9409, 2813, 9410, 2817, 9411, 2821, 9412, 2825, 9413, 2829, 9414, 2833, 9415, 2837, 9416, 2841,  // NOLINT
  9417, 2845, 9418, 2849, 9419, 2853, 9420, 2857, 9421, 2861, 9422, 2865, 9423, 2869, 9424, 2769,  // NOLINT
  9425, 2773, 9426, 2777, 9427, 2781, 9428, 2785, 9429, 2789, 9430, 2793, 9431, 2797, 9432, 2801,  // NOLINT
  9433, 2805, 9434, 2809, 9435, 2813, 9436, 2817, 9437, 2821, 9438, 2825, 9439, 2829, 9440, 2833,  // NOLINT
  9441, 2837, 9442, 2841, 9443, 2845, 9444, 2849, 9445, 2853, 9446, 2857, 9447, 2861, 9448, 2865,  // NOLINT
  9449, 2869, 11264, 2873, 11265, 2877, 11266, 2881, 11267, 2885, 11268, 2889, 11269, 2893, 11270, 2897,  // NOLINT
  11271, 2901, 11272, 2905, 11273, 2909, 11274, 2913, 11275, 2917, 11276, 2921, 11277, 2925, 11278, 2929,  // NOLINT
  11279, 2933, 11280, 2937, 11281, 2941, 11282, 2945, 11283, 2949, 11284, 2953, 11285, 2957, 11286, 2961,  // NOLINT
  11287, 2965, 11288, 2969, 11289, 2973, 11290, 2977, 11291, 2981, 11292, 2985, 11293, 2989, 11294, 2993,  // NOLINT
  11295, 2997, 11296, 3001, 11297, 3005, 11298, 3009, 11299, 3013, 11300, 3017, 11301, 3021, 11302, 3025,  // NOLINT
  11303, 3029, 11304, 3033, 11305, 3037, 11306, 3041, 11307, 3045, 11308, 3049, 11309, 3053, 11310, 3057,  // NOLINT
  11312, 2873, 11313, 2877, 11314, 2881, 11315, 2885, 11316, 2889, 11317, 2893, 11318, 2897, 11319, 2901,  // NOLINT
  11320, 2905, 11321, 2909, 11322, 2913, 11323, 2917, 11324, 2921, 11325, 2925, 11326, 2929, 11327, 2933,  // NOLINT
  11328, 2937, 11329, 2941, 11330, 2945, 11331, 2949, 11332, 2953, 11333, 2957, 11334, 2961, 11335, 2965,  // NOLINT
  11336, 2969, 11337, 2973, 11338, 2977, 11339, 2981, 11340, 2985, 11341, 2989, 11342, 2993, 11343, 2997,  // NOLINT
  11344, 3001, 11345, 3005, 11346, 3009, 11347, 3013, 11348, 3017, 11349, 3021, 11350, 3025, 11351, 3029,  // NOLINT
  11352, 3033, 11353, 3037, 11354, 3041, 11355, 3045, 11356, 3049, 11357, 3053, 11358, 3057, 11360, 3061,  // NOLINT
  11361, 3061, 11362, 889, 11363, 1937, 11364, 893, 11365, 845, 11366, 853, 11367, 3065, 11368, 3065,  // NOLINT
  11369, 3069, 11370, 3069, 11371, 3073, 11372, 3073, 11381, 3077, 11382, 3077, 11392, 3081, 11393, 3081,  // NOLINT
  11394, 3085, 11395, 3085, 11396, 3089, 11397, 3089, 11398, 3093, 11399, 3093, 11400, 3097, 11401, 3097,  // NOLINT
  11402, 3101, 11403, 3101, 11404, 3105, 11405, 3105, 11406, 3109, 11407, 3109, 11408, 3113, 11409, 3113,  // NOLINT
  11410, 3117, 11411, 3117, 11412, 3121, 11413, 3121, 11414, 3125, 11415, 3125, 11416, 3129, 11417, 3129,  // NOLINT
  11418, 3133, 11419, 3133, 11420, 3137, 11421, 3137, 11422, 3141, 11423, 3141, 11424, 3145, 11425, 3145,  // NOLINT
  11426, 3149, 11427, 3149, 11428, 3153, 11429, 3153, 11430, 3157, 11431, 3157, 11432, 3161, 11433, 3161,  // NOLINT
  11434, 3165, 11435, 3165, 11436, 3169, 11437, 3169, 11438, 3173, 11439, 3173, 11440, 3177, 11441, 3177,  // NOLINT
  11442, 3181, 11443, 3181, 11444, 3185, 11445, 3185, 11446, 3189, 11447, 3189, 11448, 3193, 11449, 3193,  // NOLINT
  11450, 3197, 11451, 3197, 11452, 3201, 11453, 3201, 11454, 3205, 11455, 3205, 11456, 3209, 11457, 3209,  // NOLINT
  11458, 3213, 11459, 3213, 11460, 3217, 11461, 3217, 11462, 3221, 11463, 3221, 11464, 3225, 11465, 3225,  // NOLINT
  11466, 3229, 11467, 3229, 11468, 3233, 11469, 3233, 11470, 3237, 11471, 3237, 11472, 3241, 11473, 3241,  // NOLINT
  11474, 3245, 11475, 3245, 11476, 3249, 11477, 3249, 11478, 3253, 11479, 3253, 11480, 3257, 11481, 3257,  // NOLINT
  11482, 3261, 11483, 3261, 11484, 3265, 11485, 3265, 11486, 3269, 11487, 3269, 11488, 3273, 11489, 3273,  // NOLINT
  11490, 3277, 11491, 3277, 11520, 1785, 11521, 1789, 11522, 1793, 11523, 1797, 11524, 1801, 11525, 1805,  // NOLINT
  11526, 1809, 11527, 1813, 11528, 1817, 11529, 1821, 11530, 1825, 11531, 1829, 11532, 1833, 11533, 1837,  // NOLINT
  11534, 1841, 11535, 1845, 11536, 1849, 11537, 1853, 11538, 1857, 11539, 1861, 11540, 1865, 11541, 1869,  // NOLINT
  11542, 1873, 11543, 1877, 11544, 1881, 11545, 1885, 11546, 1889, 11547, 1893, 11548, 1897, 11549, 1901,  // NOLINT
  11550, 1905, 11551, 1909, 11552, 1913, 11553, 1917, 11554, 1921, 11555, 1925, 11556, 1929, 11557, 1933 };  // NOLINT
static const MultiCharacterSpecialCase<4> kEcma262UnCanonicalizeMultiStrings1[] = {  // NOLINT
  {2, {65313, 65345}}, {2, {65314, 65346}}, {2, {65315, 65347}}, {2, {65316, 65348}},  // NOLINT
  {2, {65317, 65349}}, {2, {65318, 65350}}, {2, {65319, 65351}}, {2, {65320, 65352}},  // NOLINT
  {2, {65321, 65353}}, {2, {65322, 65354}}, {2, {65323, 65355}}, {2, {65324, 65356}},  // NOLINT
  {2, {65325, 65357}}, {2, {65326, 65358}}, {2, {65327, 65359}}, {2, {65328, 65360}},  // NOLINT
  {2, {65329, 65361}}, {2, {65330, 65362}}, {2, {65331, 65363}}, {2, {65332, 65364}},  // NOLINT
  {2, {65333, 65365}}, {2, {65334, 65366}}, {2, {65335, 65367}}, {2, {65336, 65368}},  // NOLINT
  {2, {65337, 65369}}, {2, {65338, 65370}}, {0, {0}} }; // NOLINT
static const uint16_t kEcma262UnCanonicalizeTable1Size = 52;
static const int32_t kEcma262UnCanonicalizeTable1[104] = {
  32545, 1, 32546, 5, 32547, 9, 32548, 13, 32549, 17, 32550, 21, 32551, 25, 32552, 29,  // NOLINT
  32553, 33, 32554, 37, 32555, 41, 32556, 45, 32557, 49, 32558, 53, 32559, 57, 32560, 61,  // NOLINT
  32561, 65, 32562, 69, 32563, 73, 32564, 77, 32565, 81, 32566, 85, 32567, 89, 32568, 93,  // NOLINT
  32569, 97, 32570, 101, 32577, 1, 32578, 5, 32579, 9, 32580, 13, 32581, 17, 32582, 21,  // NOLINT
  32583, 25, 32584, 29, 32585, 33, 32586, 37, 32587, 41, 32588, 45, 32589, 49, 32590, 53,  // NOLINT
  32591, 57, 32592, 61, 32593, 65, 32594, 69, 32595, 73, 32596, 77, 32597, 81, 32598, 85,  // NOLINT
  32599, 89, 32600, 93, 32601, 97, 32602, 101 };  // NOLINT
int Ecma262UnCanonicalize::Convert(uchar c,
                      uchar n,
                      uchar* result,
                      bool* allow_caching_ptr) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupMapping(kEcma262UnCanonicalizeTable0,
                                     kEcma262UnCanonicalizeTable0Size,
                                     kEcma262UnCanonicalizeMultiStrings0,
                                     c,
                                     n,
                                     result,
                                     allow_caching_ptr);
    case 1: return LookupMapping(kEcma262UnCanonicalizeTable1,
                                     kEcma262UnCanonicalizeTable1Size,
                                     kEcma262UnCanonicalizeMultiStrings1,
                                     c,
                                     n,
                                     result,
                                     allow_caching_ptr);
    default: return 0;
  }
}

static const MultiCharacterSpecialCase<1> kCanonicalizationRangeMultiStrings0[] = {  // NOLINT
  {0, {0}} }; // NOLINT
static const uint16_t kCanonicalizationRangeTable0Size = 720;
static const int32_t kCanonicalizationRangeTable0[1440] = {
  65, 100, 66, 96, 67, 92, 68, 88, 69, 84, 70, 80, 71, 76, 72, 72,  // NOLINT
  73, 68, 74, 64, 75, 60, 76, 56, 77, 52, 78, 48, 79, 44, 80, 40,  // NOLINT
  81, 36, 82, 32, 83, 28, 84, 24, 85, 20, 86, 16, 87, 12, 88, 8,  // NOLINT
  89, 4, 90, 0, 97, 100, 98, 96, 99, 92, 100, 88, 101, 84, 102, 80,  // NOLINT
  103, 76, 104, 72, 105, 68, 106, 64, 107, 60, 108, 56, 109, 52, 110, 48,  // NOLINT
  111, 44, 112, 40, 113, 36, 114, 32, 115, 28, 116, 24, 117, 20, 118, 16,  // NOLINT
  119, 12, 120, 8, 121, 4, 122, 0, 192, 88, 193, 84, 194, 80, 195, 76,  // NOLINT
  196, 72, 197, 68, 198, 64, 199, 60, 200, 56, 201, 52, 202, 48, 203, 44,  // NOLINT
  204, 40, 205, 36, 206, 32, 207, 28, 208, 24, 209, 20, 210, 16, 211, 12,  // NOLINT
  212, 8, 213, 4, 214, 0, 216, 24, 217, 20, 218, 16, 219, 12, 220, 8,  // NOLINT
  221, 4, 222, 0, 224, 88, 225, 84, 226, 80, 227, 76, 228, 72, 229, 68,  // NOLINT
  230, 64, 231, 60, 232, 56, 233, 52, 234, 48, 235, 44, 236, 40, 237, 36,  // NOLINT
  238, 32, 239, 28, 240, 24, 241, 20, 242, 16, 243, 12, 244, 8, 245, 4,  // NOLINT
  246, 0, 248, 24, 249, 20, 250, 16, 251, 12, 252, 8, 253, 4, 254, 0,  // NOLINT
  393, 4, 394, 0, 433, 4, 434, 0, 598, 4, 599, 0, 650, 4, 651, 0,  // NOLINT
  891, 8, 892, 4, 893, 0, 904, 8, 905, 4, 906, 0, 910, 4, 911, 0,  // NOLINT
  915, 4, 916, 0, 918, 4, 919, 0, 925, 8, 926, 4, 927, 0, 932, 4,  // NOLINT
  933, 0, 935, 16, 936, 12, 937, 8, 938, 4, 939, 0, 941, 8, 942, 4,  // NOLINT
  943, 0, 947, 4, 948, 0, 950, 4, 951, 0, 957, 8, 958, 4, 959, 0,  // NOLINT
  964, 4, 965, 0, 967, 16, 968, 12, 969, 8, 970, 4, 971, 0, 973, 4,  // NOLINT
  974, 0, 1021, 8, 1022, 4, 1023, 0, 1024, 60, 1025, 56, 1026, 52, 1027, 48,  // NOLINT
  1028, 44, 1029, 40, 1030, 36, 1031, 32, 1032, 28, 1033, 24, 1034, 20, 1035, 16,  // NOLINT
  1036, 12, 1037, 8, 1038, 4, 1039, 0, 1040, 124, 1041, 120, 1042, 116, 1043, 112,  // NOLINT
  1044, 108, 1045, 104, 1046, 100, 1047, 96, 1048, 92, 1049, 88, 1050, 84, 1051, 80,  // NOLINT
  1052, 76, 1053, 72, 1054, 68, 1055, 64, 1056, 60, 1057, 56, 1058, 52, 1059, 48,  // NOLINT
  1060, 44, 1061, 40, 1062, 36, 1063, 32, 1064, 28, 1065, 24, 1066, 20, 1067, 16,  // NOLINT
  1068, 12, 1069, 8, 1070, 4, 1071, 0, 1072, 124, 1073, 120, 1074, 116, 1075, 112,  // NOLINT
  1076, 108, 1077, 104, 1078, 100, 1079, 96, 1080, 92, 1081, 88, 1082, 84, 1083, 80,  // NOLINT
  1084, 76, 1085, 72, 1086, 68, 1087, 64, 1088, 60, 1089, 56, 1090, 52, 1091, 48,  // NOLINT
  1092, 44, 1093, 40, 1094, 36, 1095, 32, 1096, 28, 1097, 24, 1098, 20, 1099, 16,  // NOLINT
  1100, 12, 1101, 8, 1102, 4, 1103, 0, 1104, 60, 1105, 56, 1106, 52, 1107, 48,  // NOLINT
  1108, 44, 1109, 40, 1110, 36, 1111, 32, 1112, 28, 1113, 24, 1114, 20, 1115, 16,  // NOLINT
  1116, 12, 1117, 8, 1118, 4, 1119, 0, 1329, 148, 1330, 144, 1331, 140, 1332, 136,  // NOLINT
  1333, 132, 1334, 128, 1335, 124, 1336, 120, 1337, 116, 1338, 112, 1339, 108, 1340, 104,  // NOLINT
  1341, 100, 1342, 96, 1343, 92, 1344, 88, 1345, 84, 1346, 80, 1347, 76, 1348, 72,  // NOLINT
  1349, 68, 1350, 64, 1351, 60, 1352, 56, 1353, 52, 1354, 48, 1355, 44, 1356, 40,  // NOLINT
  1357, 36, 1358, 32, 1359, 28, 1360, 24, 1361, 20, 1362, 16, 1363, 12, 1364, 8,  // NOLINT
  1365, 4, 1366, 0, 1377, 148, 1378, 144, 1379, 140, 1380, 136, 1381, 132, 1382, 128,  // NOLINT
  1383, 124, 1384, 120, 1385, 116, 1386, 112, 1387, 108, 1388, 104, 1389, 100, 1390, 96,  // NOLINT
  1391, 92, 1392, 88, 1393, 84, 1394, 80, 1395, 76, 1396, 72, 1397, 68, 1398, 64,  // NOLINT
  1399, 60, 1400, 56, 1401, 52, 1402, 48, 1403, 44, 1404, 40, 1405, 36, 1406, 32,  // NOLINT
  1407, 28, 1408, 24, 1409, 20, 1410, 16, 1411, 12, 1412, 8, 1413, 4, 1414, 0,  // NOLINT
  4256, 148, 4257, 144, 4258, 140, 4259, 136, 4260, 132, 4261, 128, 4262, 124, 4263, 120,  // NOLINT
  4264, 116, 4265, 112, 4266, 108, 4267, 104, 4268, 100, 4269, 96, 4270, 92, 4271, 88,  // NOLINT
  4272, 84, 4273, 80, 4274, 76, 4275, 72, 4276, 68, 4277, 64, 4278, 60, 4279, 56,  // NOLINT
  4280, 52, 4281, 48, 4282, 44, 4283, 40, 4284, 36, 4285, 32, 4286, 28, 4287, 24,  // NOLINT
  4288, 20, 4289, 16, 4290, 12, 4291, 8, 4292, 4, 4293, 0, 7936, 28, 7937, 24,  // NOLINT
  7938, 20, 7939, 16, 7940, 12, 7941, 8, 7942, 4, 7943, 0, 7944, 28, 7945, 24,  // NOLINT
  7946, 20, 7947, 16, 7948, 12, 7949, 8, 7950, 4, 7951, 0, 7952, 20, 7953, 16,  // NOLINT
  7954, 12, 7955, 8, 7956, 4, 7957, 0, 7960, 20, 7961, 16, 7962, 12, 7963, 8,  // NOLINT
  7964, 4, 7965, 0, 7968, 28, 7969, 24, 7970, 20, 7971, 16, 7972, 12, 7973, 8,  // NOLINT
  7974, 4, 7975, 0, 7976, 28, 7977, 24, 7978, 20, 7979, 16, 7980, 12, 7981, 8,  // NOLINT
  7982, 4, 7983, 0, 7984, 28, 7985, 24, 7986, 20, 7987, 16, 7988, 12, 7989, 8,  // NOLINT
  7990, 4, 7991, 0, 7992, 28, 7993, 24, 7994, 20, 7995, 16, 7996, 12, 7997, 8,  // NOLINT
  7998, 4, 7999, 0, 8000, 20, 8001, 16, 8002, 12, 8003, 8, 8004, 4, 8005, 0,  // NOLINT
  8008, 20, 8009, 16, 8010, 12, 8011, 8, 8012, 4, 8013, 0, 8032, 28, 8033, 24,  // NOLINT
  8034, 20, 8035, 16, 8036, 12, 8037, 8, 8038, 4, 8039, 0, 8040, 28, 8041, 24,  // NOLINT
  8042, 20, 8043, 16, 8044, 12, 8045, 8, 8046, 4, 8047, 0, 8048, 4, 8049, 0,  // NOLINT
  8050, 12, 8051, 8, 8052, 4, 8053, 0, 8054, 4, 8055, 0, 8056, 4, 8057, 0,  // NOLINT
  8058, 4, 8059, 0, 8060, 4, 8061, 0, 8112, 4, 8113, 0, 8120, 4, 8121, 0,  // NOLINT
  8122, 4, 8123, 0, 8136, 12, 8137, 8, 8138, 4, 8139, 0, 8144, 4, 8145, 0,  // NOLINT
  8152, 4, 8153, 0, 8154, 4, 8155, 0, 8160, 4, 8161, 0, 8168, 4, 8169, 0,  // NOLINT
  8170, 4, 8171, 0, 8184, 4, 8185, 0, 8186, 4, 8187, 0, 8490, 4, 8491, 0,  // NOLINT
  8544, 60, 8545, 56, 8546, 52, 8547, 48, 8548, 44, 8549, 40, 8550, 36, 8551, 32,  // NOLINT
  8552, 28, 8553, 24, 8554, 20, 8555, 16, 8556, 12, 8557, 8, 8558, 4, 8559, 0,  // NOLINT
  8560, 60, 8561, 56, 8562, 52, 8563, 48, 8564, 44, 8565, 40, 8566, 36, 8567, 32,  // NOLINT
  8568, 28, 8569, 24, 8570, 20, 8571, 16, 8572, 12, 8573, 8, 8574, 4, 8575, 0,  // NOLINT
  9398, 100, 9399, 96, 9400, 92, 9401, 88, 9402, 84, 9403, 80, 9404, 76, 9405, 72,  // NOLINT
  9406, 68, 9407, 64, 9408, 60, 9409, 56, 9410, 52, 9411, 48, 9412, 44, 9413, 40,  // NOLINT
  9414, 36, 9415, 32, 9416, 28, 9417, 24, 9418, 20, 9419, 16, 9420, 12, 9421, 8,  // NOLINT
  9422, 4, 9423, 0, 9424, 100, 9425, 96, 9426, 92, 9427, 88, 9428, 84, 9429, 80,  // NOLINT
  9430, 76, 9431, 72, 9432, 68, 9433, 64, 9434, 60, 9435, 56, 9436, 52, 9437, 48,  // NOLINT
  9438, 44, 9439, 40, 9440, 36, 9441, 32, 9442, 28, 9443, 24, 9444, 20, 9445, 16,  // NOLINT
  9446, 12, 9447, 8, 9448, 4, 9449, 0, 11264, 184, 11265, 180, 11266, 176, 11267, 172,  // NOLINT
  11268, 168, 11269, 164, 11270, 160, 11271, 156, 11272, 152, 11273, 148, 11274, 144, 11275, 140,  // NOLINT
  11276, 136, 11277, 132, 11278, 128, 11279, 124, 11280, 120, 11281, 116, 11282, 112, 11283, 108,  // NOLINT
  11284, 104, 11285, 100, 11286, 96, 11287, 92, 11288, 88, 11289, 84, 11290, 80, 11291, 76,  // NOLINT
  11292, 72, 11293, 68, 11294, 64, 11295, 60, 11296, 56, 11297, 52, 11298, 48, 11299, 44,  // NOLINT
  11300, 40, 11301, 36, 11302, 32, 11303, 28, 11304, 24, 11305, 20, 11306, 16, 11307, 12,  // NOLINT
  11308, 8, 11309, 4, 11310, 0, 11312, 184, 11313, 180, 11314, 176, 11315, 172, 11316, 168,  // NOLINT
  11317, 164, 11318, 160, 11319, 156, 11320, 152, 11321, 148, 11322, 144, 11323, 140, 11324, 136,  // NOLINT
  11325, 132, 11326, 128, 11327, 124, 11328, 120, 11329, 116, 11330, 112, 11331, 108, 11332, 104,  // NOLINT
  11333, 100, 11334, 96, 11335, 92, 11336, 88, 11337, 84, 11338, 80, 11339, 76, 11340, 72,  // NOLINT
  11341, 68, 11342, 64, 11343, 60, 11344, 56, 11345, 52, 11346, 48, 11347, 44, 11348, 40,  // NOLINT
  11349, 36, 11350, 32, 11351, 28, 11352, 24, 11353, 20, 11354, 16, 11355, 12, 11356, 8,  // NOLINT
  11357, 4, 11358, 0, 11520, 148, 11521, 144, 11522, 140, 11523, 136, 11524, 132, 11525, 128,  // NOLINT
  11526, 124, 11527, 120, 11528, 116, 11529, 112, 11530, 108, 11531, 104, 11532, 100, 11533, 96,  // NOLINT
  11534, 92, 11535, 88, 11536, 84, 11537, 80, 11538, 76, 11539, 72, 11540, 68, 11541, 64,  // NOLINT
  11542, 60, 11543, 56, 11544, 52, 11545, 48, 11546, 44, 11547, 40, 11548, 36, 11549, 32,  // NOLINT
  11550, 28, 11551, 24, 11552, 20, 11553, 16, 11554, 12, 11555, 8, 11556, 4, 11557, 0 };  // NOLINT
static const MultiCharacterSpecialCase<1> kCanonicalizationRangeMultiStrings1[] = {  // NOLINT
  {0, {0}} }; // NOLINT
static const uint16_t kCanonicalizationRangeTable1Size = 52;
static const int32_t kCanonicalizationRangeTable1[104] = {
  32545, 100, 32546, 96, 32547, 92, 32548, 88, 32549, 84, 32550, 80, 32551, 76, 32552, 72,  // NOLINT
  32553, 68, 32554, 64, 32555, 60, 32556, 56, 32557, 52, 32558, 48, 32559, 44, 32560, 40,  // NOLINT
  32561, 36, 32562, 32, 32563, 28, 32564, 24, 32565, 20, 32566, 16, 32567, 12, 32568, 8,  // NOLINT
  32569, 4, 32570, 0, 32577, 100, 32578, 96, 32579, 92, 32580, 88, 32581, 84, 32582, 80,  // NOLINT
  32583, 76, 32584, 72, 32585, 68, 32586, 64, 32587, 60, 32588, 56, 32589, 52, 32590, 48,  // NOLINT
  32591, 44, 32592, 40, 32593, 36, 32594, 32, 32595, 28, 32596, 24, 32597, 20, 32598, 16,  // NOLINT
  32599, 12, 32600, 8, 32601, 4, 32602, 0 };  // NOLINT
int CanonicalizationRange::Convert(uchar c,
                      uchar n,
                      uchar* result,
                      bool* allow_caching_ptr) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupMapping(kCanonicalizationRangeTable0,
                                     kCanonicalizationRangeTable0Size,
                                     kCanonicalizationRangeMultiStrings0,
                                     c,
                                     n,
                                     result,
                                     allow_caching_ptr);
    case 1: return LookupMapping(kCanonicalizationRangeTable1,
                                     kCanonicalizationRangeTable1Size,
                                     kCanonicalizationRangeMultiStrings1,
                                     c,
                                     n,
                                     result,
                                     allow_caching_ptr);
    default: return 0;
  }
}


uchar UnicodeData::kMaxCodePoint = 65533;

int UnicodeData::GetByteCount() {
  return 0 + (sizeof(int32_t) * kUppercaseTable0Size) + (sizeof(int32_t) * kUppercaseTable1Size) + (sizeof(int32_t) * kLowercaseTable0Size) + (sizeof(int32_t) * kLowercaseTable1Size) + (sizeof(int32_t) * kLetterTable0Size) + (sizeof(int32_t) * kLetterTable1Size) + (sizeof(int32_t) * kSpaceTable0Size) + (sizeof(int32_t) * kNumberTable0Size) + (sizeof(int32_t) * kNumberTable1Size) + (sizeof(int32_t) * kWhiteSpaceTable0Size) + (sizeof(int32_t) * kLineTerminatorTable0Size) + (sizeof(int32_t) * kCombiningMarkTable0Size) + (sizeof(int32_t) * kCombiningMarkTable1Size) + (sizeof(int32_t) * kConnectorPunctuationTable0Size) + (sizeof(int32_t) * kConnectorPunctuationTable1Size) + (sizeof(int32_t) * kToLowercaseTable0Size) + (sizeof(int32_t) * kToLowercaseTable1Size) + (sizeof(int32_t) * kToUppercaseTable0Size) + (sizeof(int32_t) * kToUppercaseTable1Size) + (sizeof(int32_t) * kEcma262CanonicalizeTable0Size) + (sizeof(int32_t) * kEcma262CanonicalizeTable1Size) + (sizeof(int32_t) * kEcma262UnCanonicalizeTable0Size) + (sizeof(int32_t) * kEcma262UnCanonicalizeTable1Size) + (sizeof(int32_t) * kCanonicalizationRangeTable0Size) + (sizeof(int32_t) * kCanonicalizationRangeTable1Size); // NOLINT
}

}  // namespace unicode
