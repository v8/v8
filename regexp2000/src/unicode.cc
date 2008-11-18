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
// This file was generated at 2008-11-18 11:25:57.962030

#include "unicode-inl.h"
#include <cstdlib>
#include <cstdio>

namespace unibrow {

/**
 * \file
 * Implementations of functions for working with unicode.
 */

typedef signed short int16_t;  // NOLINT
typedef unsigned short uint16_t;  // NOLINT

// All access to the character table should go through this function.
template <int D>
static inline uchar TableGet(const uint16_t* table, int index) {
  return table[D * index];
}

static inline uchar GetEntry(uint16_t entry) {
  return entry & 0x7fff;
}

static inline bool IsStart(uint16_t entry) {
  return (entry & (1 << 15)) != 0;
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
static bool LookupPredicate(const uint16_t* table, uint16_t size, uchar chr) {
  static const int kEntryDist = 1;
  uint16_t value = chr & 0x7fff;
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
  uint16_t field = TableGet<kEntryDist>(table, low);
  return (GetEntry(field) == value) ||
          (GetEntry(field) < value && IsStart(field));
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
static int LookupMapping(const uint16_t* table,
                         uint16_t size,
                         const MultiCharacterSpecialCase<kW>* multi_chars,
                         uchar chr,
                         uchar next,
                         uchar* result,
                         bool* allow_caching_ptr) {
  static const int kEntryDist = 2;
  uint16_t value = chr & 0x7fff;
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
  uint16_t field = TableGet<kEntryDist>(table, low);
  bool found = (GetEntry(field) == value) ||
               (GetEntry(field) < value && IsStart(field));
  if (found) {
    int16_t value = table[2 * low + 1];
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
  static const uchar kMaxOneByteChar = 0x7F;
  static const uchar kMaxTwoByteChar = 0x7FF;
  static const uchar kMaxThreeByteChar = 0xFFFF;
  static const uchar kMaxFourByteChar = 0x1FFFFF;

  // We only get called for non-ascii characters.
  if (length == 1) {
    *cursor += 1;
    return kBadChar;
  }
  int first = str[0];
  int second = str[1] ^ 0x80;
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
  int third = str[2] ^ 0x80;
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
  int fourth = str[3] ^ 0x80;
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
static const uint16_t kUppercaseTable0[509] = { 32833, 90, 32960, 214, 32984, 222, 256, 258, 260, 262, 264, 266, 268, 270, 272, 274, 276, 278, 280, 282, 284, 286, 288, 290, 292, 294, 296, 298, 300, 302, 304, 306, 308, 310, 313, 315, 317, 319, 321, 323, 325, 327, 330, 332, 334, 336, 338, 340, 342, 344, 346, 348, 350, 352, 354, 356, 358, 360, 362, 364, 366, 368, 370, 372, 374, 33144, 377, 379, 381, 33153, 386, 388, 33158, 391, 33161, 395, 33166, 401, 33171, 404, 33174, 408, 33180, 413, 33183, 416, 418, 420, 33190, 423, 425, 428, 33198, 431, 33201, 435, 437, 33207, 440, 444, 452, 455, 458, 461, 463, 465, 467, 469, 471, 473, 475, 478, 480, 482, 484, 486, 488, 490, 492, 494, 497, 500, 33270, 504, 506, 508, 510, 512, 514, 516, 518, 520, 522, 524, 526, 528, 530, 532, 534, 536, 538, 540, 542, 544, 546, 548, 550, 552, 554, 556, 558, 560, 562, 33338, 571, 33341, 574, 577, 33347, 582, 584, 586, 588, 590, 902, 33672, 906, 908, 33678, 911, 33681, 929, 33699, 939, 33746, 980, 984, 986, 988, 990, 992, 994, 996, 998, 1000, 1002, 1004, 1006, 1012, 1015, 33785, 1018, 33789, 1071, 1120, 1122, 1124, 1126, 1128, 1130, 1132, 1134, 1136, 1138, 1140, 1142, 1144, 1146, 1148, 1150, 1152, 1162, 1164, 1166, 1168, 1170, 1172, 1174, 1176, 1178, 1180, 1182, 1184, 1186, 1188, 1190, 1192, 1194, 1196, 1198, 1200, 1202, 1204, 1206, 1208, 1210, 1212, 1214, 33984, 1217, 1219, 1221, 1223, 1225, 1227, 1229, 1232, 1234, 1236, 1238, 1240, 1242, 1244, 1246, 1248, 1250, 1252, 1254, 1256, 1258, 1260, 1262, 1264, 1266, 1268, 1270, 1272, 1274, 1276, 1278, 1280, 1282, 1284, 1286, 1288, 1290, 1292, 1294, 1296, 1298, 34097, 1366, 37024, 4293, 7680, 7682, 7684, 7686, 7688, 7690, 7692, 7694, 7696, 7698, 7700, 7702, 7704, 7706, 7708, 7710, 7712, 7714, 7716, 7718, 7720, 7722, 7724, 7726, 7728, 7730, 7732, 7734, 7736, 7738, 7740, 7742, 7744, 7746, 7748, 7750, 7752, 7754, 7756, 7758, 7760, 7762, 7764, 7766, 7768, 7770, 7772, 7774, 7776, 7778, 7780, 7782, 7784, 7786, 7788, 7790, 7792, 7794, 7796, 7798, 7800, 7802, 7804, 7806, 7808, 7810, 7812, 7814, 7816, 7818, 7820, 7822, 7824, 7826, 7828, 7840, 7842, 7844, 7846, 7848, 7850, 7852, 7854, 7856, 7858, 7860, 7862, 7864, 7866, 7868, 7870, 7872, 7874, 7876, 7878, 7880, 7882, 7884, 7886, 7888, 7890, 7892, 7894, 7896, 7898, 7900, 7902, 7904, 7906, 7908, 7910, 7912, 7914, 7916, 7918, 7920, 7922, 7924, 7926, 7928, 40712, 7951, 40728, 7965, 40744, 7983, 40760, 7999, 40776, 8013, 8025, 8027, 8029, 8031, 40808, 8047, 40888, 8123, 40904, 8139, 40920, 8155, 40936, 8172, 40952, 8187, 8450, 8455, 41227, 8461, 41232, 8466, 8469, 41241, 8477, 8484, 8486, 8488, 41258, 8493, 41264, 8499, 41278, 8511, 8517, 8579, 44032, 11310, 11360, 44130, 11364, 11367, 11369, 11371, 11381, 11392, 11394, 11396, 11398, 11400, 11402, 11404, 11406, 11408, 11410, 11412, 11414, 11416, 11418, 11420, 11422, 11424, 11426, 11428, 11430, 11432, 11434, 11436, 11438, 11440, 11442, 11444, 11446, 11448, 11450, 11452, 11454, 11456, 11458, 11460, 11462, 11464, 11466, 11468, 11470, 11472, 11474, 11476, 11478, 11480, 11482, 11484, 11486, 11488, 11490 }; // NOLINT
static const uint16_t kUppercaseTable1Size = 2;
static const uint16_t kUppercaseTable1[2] = { 65313, 32570 }; // NOLINT
static const uint16_t kUppercaseTable2Size = 2;
static const uint16_t kUppercaseTable2[2] = { 33792, 1063 }; // NOLINT
static const uint16_t kUppercaseTable3Size = 58;
static const uint16_t kUppercaseTable3[58] = { 54272, 21529, 54324, 21581, 54376, 21633, 21660, 54430, 21663, 21666, 54437, 21670, 54441, 21676, 54446, 21685, 54480, 21737, 54532, 21765, 54535, 21770, 54541, 21780, 54550, 21788, 54584, 21817, 54587, 21822, 54592, 21828, 21830, 54602, 21840, 54636, 21893, 54688, 21945, 54740, 21997, 54792, 22049, 54844, 22101, 54896, 22153, 54952, 22208, 55010, 22266, 55068, 22324, 55126, 22382, 55184, 22440, 22474 }; // NOLINT
bool Uppercase::Is(uchar c) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupPredicate(kUppercaseTable0,
                                       kUppercaseTable0Size,
                                       c);
    case 1: return LookupPredicate(kUppercaseTable1,
                                       kUppercaseTable1Size,
                                       c);
    case 2: return LookupPredicate(kUppercaseTable2,
                                       kUppercaseTable2Size,
                                       c);
    case 3: return LookupPredicate(kUppercaseTable3,
                                       kUppercaseTable3Size,
                                       c);
    default: return false;
  }
}

// Lowercase:            point.category == 'Ll'

static const uint16_t kLowercaseTable0Size = 528;
static const uint16_t kLowercaseTable0[528] = { 32865, 122, 170, 181, 186, 32991, 246, 33016, 255, 257, 259, 261, 263, 265, 267, 269, 271, 273, 275, 277, 279, 281, 283, 285, 287, 289, 291, 293, 295, 297, 299, 301, 303, 305, 307, 309, 33079, 312, 314, 316, 318, 320, 322, 324, 326, 33096, 329, 331, 333, 335, 337, 339, 341, 343, 345, 347, 349, 351, 353, 355, 357, 359, 361, 363, 365, 367, 369, 371, 373, 375, 378, 380, 33150, 384, 387, 389, 392, 33164, 397, 402, 405, 33177, 411, 414, 417, 419, 421, 424, 33194, 427, 429, 432, 436, 438, 33209, 442, 33213, 447, 454, 457, 460, 462, 464, 466, 468, 470, 472, 474, 33244, 477, 479, 481, 483, 485, 487, 489, 491, 493, 33263, 496, 499, 501, 505, 507, 509, 511, 513, 515, 517, 519, 521, 523, 525, 527, 529, 531, 533, 535, 537, 539, 541, 543, 545, 547, 549, 551, 553, 555, 557, 559, 561, 33331, 569, 572, 33343, 576, 578, 583, 585, 587, 589, 33359, 659, 33429, 687, 33659, 893, 912, 33708, 974, 33744, 977, 33749, 983, 985, 987, 989, 991, 993, 995, 997, 999, 1001, 1003, 1005, 33775, 1011, 1013, 1016, 33787, 1020, 33840, 1119, 1121, 1123, 1125, 1127, 1129, 1131, 1133, 1135, 1137, 1139, 1141, 1143, 1145, 1147, 1149, 1151, 1153, 1163, 1165, 1167, 1169, 1171, 1173, 1175, 1177, 1179, 1181, 1183, 1185, 1187, 1189, 1191, 1193, 1195, 1197, 1199, 1201, 1203, 1205, 1207, 1209, 1211, 1213, 1215, 1218, 1220, 1222, 1224, 1226, 1228, 33998, 1231, 1233, 1235, 1237, 1239, 1241, 1243, 1245, 1247, 1249, 1251, 1253, 1255, 1257, 1259, 1261, 1263, 1265, 1267, 1269, 1271, 1273, 1275, 1277, 1279, 1281, 1283, 1285, 1287, 1289, 1291, 1293, 1295, 1297, 1299, 34145, 1415, 40192, 7467, 40290, 7543, 40313, 7578, 7681, 7683, 7685, 7687, 7689, 7691, 7693, 7695, 7697, 7699, 7701, 7703, 7705, 7707, 7709, 7711, 7713, 7715, 7717, 7719, 7721, 7723, 7725, 7727, 7729, 7731, 7733, 7735, 7737, 7739, 7741, 7743, 7745, 7747, 7749, 7751, 7753, 7755, 7757, 7759, 7761, 7763, 7765, 7767, 7769, 7771, 7773, 7775, 7777, 7779, 7781, 7783, 7785, 7787, 7789, 7791, 7793, 7795, 7797, 7799, 7801, 7803, 7805, 7807, 7809, 7811, 7813, 7815, 7817, 7819, 7821, 7823, 7825, 7827, 40597, 7835, 7841, 7843, 7845, 7847, 7849, 7851, 7853, 7855, 7857, 7859, 7861, 7863, 7865, 7867, 7869, 7871, 7873, 7875, 7877, 7879, 7881, 7883, 7885, 7887, 7889, 7891, 7893, 7895, 7897, 7899, 7901, 7903, 7905, 7907, 7909, 7911, 7913, 7915, 7917, 7919, 7921, 7923, 7925, 7927, 7929, 40704, 7943, 40720, 7957, 40736, 7975, 40752, 7991, 40768, 8005, 40784, 8023, 40800, 8039, 40816, 8061, 40832, 8071, 40848, 8087, 40864, 8103, 40880, 8116, 40886, 8119, 8126, 40898, 8132, 40902, 8135, 40912, 8147, 40918, 8151, 40928, 8167, 40946, 8180, 40950, 8183, 8305, 8319, 8458, 41230, 8463, 8467, 8495, 8500, 8505, 41276, 8509, 41286, 8521, 8526, 8580, 44080, 11358, 11361, 44133, 11366, 11368, 11370, 11372, 11380, 44150, 11383, 11393, 11395, 11397, 11399, 11401, 11403, 11405, 11407, 11409, 11411, 11413, 11415, 11417, 11419, 11421, 11423, 11425, 11427, 11429, 11431, 11433, 11435, 11437, 11439, 11441, 11443, 11445, 11447, 11449, 11451, 11453, 11455, 11457, 11459, 11461, 11463, 11465, 11467, 11469, 11471, 11473, 11475, 11477, 11479, 11481, 11483, 11485, 11487, 11489, 44259, 11492, 44288, 11557 }; // NOLINT
static const uint16_t kLowercaseTable1Size = 6;
static const uint16_t kLowercaseTable1[6] = { 64256, 31494, 64275, 31511, 65345, 32602 }; // NOLINT
static const uint16_t kLowercaseTable2Size = 2;
static const uint16_t kLowercaseTable2[2] = { 33832, 1103 }; // NOLINT
static const uint16_t kLowercaseTable3Size = 54;
static const uint16_t kLowercaseTable3[54] = { 54298, 21555, 54350, 21588, 54358, 21607, 54402, 21659, 54454, 21689, 21691, 54461, 21699, 54469, 21711, 54506, 21763, 54558, 21815, 54610, 21867, 54662, 21919, 54714, 21971, 54766, 22023, 54818, 22075, 54870, 22127, 54922, 22181, 54978, 22234, 55004, 22241, 55036, 22292, 55062, 22299, 55094, 22350, 55120, 22357, 55152, 22408, 55178, 22415, 55210, 22466, 55236, 22473, 22475 }; // NOLINT
bool Lowercase::Is(uchar c) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupPredicate(kLowercaseTable0,
                                       kLowercaseTable0Size,
                                       c);
    case 1: return LookupPredicate(kLowercaseTable1,
                                       kLowercaseTable1Size,
                                       c);
    case 2: return LookupPredicate(kLowercaseTable2,
                                       kLowercaseTable2Size,
                                       c);
    case 3: return LookupPredicate(kLowercaseTable3,
                                       kLowercaseTable3Size,
                                       c);
    default: return false;
  }
}

// Letter:               point.category in ['Lu', 'Ll', 'Lt', 'Lm', 'Lo' ]

static const uint16_t kLetterTable0Size = 476;
static const uint16_t kLetterTable0[476] = { 32833, 90, 32865, 122, 170, 181, 186, 32960, 214, 32984, 246, 33016, 705, 33478, 721, 33504, 740, 750, 33658, 893, 902, 33672, 906, 908, 33678, 929, 33699, 974, 33744, 1013, 33783, 1153, 33930, 1299, 34097, 1366, 1369, 34145, 1415, 34256, 1514, 34288, 1522, 34337, 1594, 34368, 1610, 34414, 1647, 34417, 1747, 1749, 34533, 1766, 34542, 1775, 34554, 1788, 1791, 1808, 34578, 1839, 34637, 1901, 34688, 1957, 1969, 34762, 2026, 34804, 2037, 2042, 35076, 2361, 2365, 2384, 35160, 2401, 35195, 2431, 35205, 2444, 35215, 2448, 35219, 2472, 35242, 2480, 2482, 35254, 2489, 2493, 2510, 35292, 2525, 35295, 2529, 35312, 2545, 35333, 2570, 35343, 2576, 35347, 2600, 35370, 2608, 35378, 2611, 35381, 2614, 35384, 2617, 35417, 2652, 2654, 35442, 2676, 35461, 2701, 35471, 2705, 35475, 2728, 35498, 2736, 35506, 2739, 35509, 2745, 2749, 2768, 35552, 2785, 35589, 2828, 35599, 2832, 35603, 2856, 35626, 2864, 35634, 2867, 35637, 2873, 2877, 35676, 2909, 35679, 2913, 2929, 2947, 35717, 2954, 35726, 2960, 35730, 2965, 35737, 2970, 2972, 35742, 2975, 35747, 2980, 35752, 2986, 35758, 3001, 35845, 3084, 35854, 3088, 35858, 3112, 35882, 3123, 35893, 3129, 35936, 3169, 35973, 3212, 35982, 3216, 35986, 3240, 36010, 3251, 36021, 3257, 3261, 3294, 36064, 3297, 36101, 3340, 36110, 3344, 36114, 3368, 36138, 3385, 36192, 3425, 36229, 3478, 36250, 3505, 36275, 3515, 3517, 36288, 3526, 36353, 3632, 36402, 3635, 36416, 3654, 36481, 3714, 3716, 36487, 3720, 3722, 3725, 36500, 3735, 36505, 3743, 36513, 3747, 3749, 3751, 36522, 3755, 36525, 3760, 36530, 3763, 3773, 36544, 3780, 3782, 36572, 3805, 3840, 36672, 3911, 36681, 3946, 36744, 3979, 36864, 4129, 36899, 4135, 36905, 4138, 36944, 4181, 37024, 4293, 37072, 4346, 4348, 37120, 4441, 37215, 4514, 37288, 4601, 37376, 4680, 37450, 4685, 37456, 4694, 4696, 37466, 4701, 37472, 4744, 37514, 4749, 37520, 4784, 37554, 4789, 37560, 4798, 4800, 37570, 4805, 37576, 4822, 37592, 4880, 37650, 4885, 37656, 4954, 37760, 5007, 37792, 5108, 37889, 5740, 38511, 5750, 38529, 5786, 38560, 5866, 38656, 5900, 38670, 5905, 38688, 5937, 38720, 5969, 38752, 5996, 38766, 6000, 38784, 6067, 6103, 6108, 38944, 6263, 39040, 6312, 39168, 6428, 39248, 6509, 39280, 6516, 39296, 6569, 39361, 6599, 39424, 6678, 39685, 6963, 39749, 6987, 40192, 7615, 40448, 7835, 40608, 7929, 40704, 7957, 40728, 7965, 40736, 8005, 40776, 8013, 40784, 8023, 8025, 8027, 8029, 40799, 8061, 40832, 8116, 40886, 8124, 8126, 40898, 8132, 40902, 8140, 40912, 8147, 40918, 8155, 40928, 8172, 40946, 8180, 40950, 8188, 8305, 8319, 41104, 8340, 8450, 8455, 41226, 8467, 8469, 41241, 8477, 8484, 8486, 8488, 41258, 8493, 41263, 8505, 41276, 8511, 41285, 8521, 8526, 41347, 8580, 44032, 11310, 44080, 11358, 44128, 11372, 44148, 11383, 44160, 11492, 44288, 11557, 44336, 11621, 11631, 44416, 11670, 44448, 11686, 44456, 11694, 44464, 11702, 44472, 11710, 44480, 11718, 44488, 11726, 44496, 11734, 44504, 11742, 45061, 12294, 45105, 12341, 45115, 12348, 45121, 12438, 45213, 12447, 45217, 12538, 45308, 12543, 45317, 12588, 45361, 12686, 45472, 12727, 45552, 12799, 46080, 19893, 52736, 32767 }; // NOLINT
static const uint16_t kLetterTable1Size = 68;
static const uint16_t kLetterTable1[68] = { 32768, 8123, 40960, 9356, 42775, 10010, 43008, 10241, 43011, 10245, 43015, 10250, 43020, 10274, 43072, 10355, 44032, 22435, 63744, 31277, 64048, 31338, 64112, 31449, 64256, 31494, 64275, 31511, 31517, 64287, 31528, 64298, 31542, 64312, 31548, 31550, 64320, 31553, 64323, 31556, 64326, 31665, 64467, 32061, 64848, 32143, 64914, 32199, 65008, 32251, 65136, 32372, 65142, 32508, 65313, 32570, 65345, 32602, 65382, 32702, 65474, 32711, 65482, 32719, 65490, 32727, 65498, 32732 }; // NOLINT
static const uint16_t kLetterTable2Size = 48;
static const uint16_t kLetterTable2[48] = { 32768, 11, 32781, 38, 32808, 58, 32828, 61, 32831, 77, 32848, 93, 32896, 250, 33536, 798, 33584, 832, 33602, 841, 33664, 925, 33696, 963, 33736, 975, 33792, 1181, 34816, 2053, 2056, 34826, 2101, 34871, 2104, 2108, 2111, 35072, 2325, 2560, 35344, 2579, 35349, 2583, 35353, 2611, 40960, 9070 }; // NOLINT
static const uint16_t kLetterTable3Size = 57;
static const uint16_t kLetterTable3[57] = { 54272, 21588, 54358, 21660, 54430, 21663, 21666, 54437, 21670, 54441, 21676, 54446, 21689, 21691, 54461, 21699, 54469, 21765, 54535, 21770, 54541, 21780, 54550, 21788, 54558, 21817, 54587, 21822, 54592, 21828, 21830, 54602, 21840, 54610, 22181, 54952, 22208, 54978, 22234, 55004, 22266, 55036, 22292, 55062, 22324, 55094, 22350, 55120, 22382, 55152, 22408, 55178, 22440, 55210, 22466, 55236, 22475 }; // NOLINT
static const uint16_t kLetterTable4Size = 2;
static const uint16_t kLetterTable4[2] = { 32768, 32767 }; // NOLINT
static const uint16_t kLetterTable5Size = 4;
static const uint16_t kLetterTable5[4] = { 32768, 9942, 63488, 31261 }; // NOLINT
bool Letter::Is(uchar c) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupPredicate(kLetterTable0,
                                       kLetterTable0Size,
                                       c);
    case 1: return LookupPredicate(kLetterTable1,
                                       kLetterTable1Size,
                                       c);
    case 2: return LookupPredicate(kLetterTable2,
                                       kLetterTable2Size,
                                       c);
    case 3: return LookupPredicate(kLetterTable3,
                                       kLetterTable3Size,
                                       c);
    case 4: return LookupPredicate(kLetterTable4,
                                       kLetterTable4Size,
                                       c);
    case 5: return LookupPredicate(kLetterTable5,
                                       kLetterTable5Size,
                                       c);
    default: return false;
  }
}

// Space:                point.category == 'Zs'

static const uint16_t kSpaceTable0Size = 9;
static const uint16_t kSpaceTable0[9] = { 32, 160, 5760, 6158, 40960, 8202, 8239, 8287, 12288 }; // NOLINT
bool Space::Is(uchar c) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupPredicate(kSpaceTable0,
                                       kSpaceTable0Size,
                                       c);
    default: return false;
  }
}

// Number:               point.category in ['Nd', 'Nl', 'No' ]

static const uint16_t kNumberTable0Size = 86;
static const uint16_t kNumberTable0[86] = { 32816, 57, 32946, 179, 185, 32956, 190, 34400, 1641, 34544, 1785, 34752, 1993, 35174, 2415, 35302, 2543, 35316, 2553, 35430, 2671, 35558, 2799, 35686, 2927, 35814, 3058, 35942, 3183, 36070, 3311, 36198, 3439, 36432, 3673, 36560, 3801, 36640, 3891, 36928, 4169, 37737, 4988, 38638, 5872, 38880, 6121, 38896, 6137, 38928, 6169, 39238, 6479, 39376, 6617, 39760, 7001, 8304, 41076, 8313, 41088, 8329, 41299, 8578, 42080, 9371, 42218, 9471, 42870, 10131, 11517, 12295, 45089, 12329, 45112, 12346, 45458, 12693, 45600, 12841, 45649, 12895, 45696, 12937, 45745, 12991 }; // NOLINT
static const uint16_t kNumberTable1Size = 2;
static const uint16_t kNumberTable1[2] = { 65296, 32537 }; // NOLINT
static const uint16_t kNumberTable2Size = 19;
static const uint16_t kNumberTable2[19] = { 33031, 307, 33088, 376, 394, 33568, 803, 833, 842, 33745, 981, 33952, 1193, 35094, 2329, 35392, 2631, 41984, 9314 }; // NOLINT
static const uint16_t kNumberTable3Size = 4;
static const uint16_t kNumberTable3[4] = { 54112, 21361, 55246, 22527 }; // NOLINT
bool Number::Is(uchar c) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupPredicate(kNumberTable0,
                                       kNumberTable0Size,
                                       c);
    case 1: return LookupPredicate(kNumberTable1,
                                       kNumberTable1Size,
                                       c);
    case 2: return LookupPredicate(kNumberTable2,
                                       kNumberTable2Size,
                                       c);
    case 3: return LookupPredicate(kNumberTable3,
                                       kNumberTable3Size,
                                       c);
    default: return false;
  }
}

// WhiteSpace:           'Ws' in point.properties

static const uint16_t kWhiteSpaceTable0Size = 14;
static const uint16_t kWhiteSpaceTable0[14] = { 32777, 13, 32, 133, 160, 5760, 6158, 40960, 8202, 41000, 8233, 8239, 8287, 12288 }; // NOLINT
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
static const uint16_t kLineTerminatorTable0[4] = { 10, 13, 41000, 8233 }; // NOLINT
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
static const uint16_t kCombiningMarkTable0[214] = { 33536, 879, 33923, 1158, 34193, 1469, 1471, 34241, 1474, 34244, 1477, 1479, 34320, 1557, 34379, 1630, 1648, 34518, 1756, 34527, 1764, 34535, 1768, 34538, 1773, 1809, 34608, 1866, 34726, 1968, 34795, 2035, 35073, 2307, 2364, 35134, 2381, 35153, 2388, 35170, 2403, 35201, 2435, 2492, 35262, 2500, 35271, 2504, 35275, 2509, 2519, 35298, 2531, 35329, 2563, 2620, 35390, 2626, 35399, 2632, 35403, 2637, 35440, 2673, 35457, 2691, 2748, 35518, 2757, 35527, 2761, 35531, 2765, 35554, 2787, 35585, 2819, 2876, 35646, 2883, 35655, 2888, 35659, 2893, 35670, 2903, 2946, 35774, 3010, 35782, 3016, 35786, 3021, 3031, 35841, 3075, 35902, 3140, 35910, 3144, 35914, 3149, 35925, 3158, 35970, 3203, 3260, 36030, 3268, 36038, 3272, 36042, 3277, 36053, 3286, 36066, 3299, 36098, 3331, 36158, 3395, 36166, 3400, 36170, 3405, 3415, 36226, 3459, 3530, 36303, 3540, 3542, 36312, 3551, 36338, 3571, 3633, 36404, 3642, 36423, 3662, 3761, 36532, 3769, 36539, 3772, 36552, 3789, 36632, 3865, 3893, 3895, 3897, 36670, 3903, 36721, 3972, 36742, 3975, 36752, 3991, 36761, 4028, 4038, 36908, 4146, 36918, 4153, 36950, 4185, 4959, 38674, 5908, 38706, 5940, 38738, 5971, 38770, 6003, 38838, 6099, 6109, 38923, 6157, 6313, 39200, 6443, 39216, 6459, 39344, 6592, 39368, 6601, 39447, 6683, 39680, 6916, 39732, 6980, 39787, 7027, 40384, 7626, 40446, 7679, 41168, 8412, 8417, 41189, 8431, 45098, 12335, 45209, 12442 }; // NOLINT
static const uint16_t kCombiningMarkTable1Size = 10;
static const uint16_t kCombiningMarkTable1[10] = { 10242, 10246, 10251, 43043, 10279, 31518, 65024, 32271, 65056, 32291 }; // NOLINT
static const uint16_t kCombiningMarkTable2Size = 9;
static const uint16_t kCombiningMarkTable2[9] = { 35329, 2563, 35333, 2566, 35340, 2575, 35384, 2618, 2623 }; // NOLINT
static const uint16_t kCombiningMarkTable3Size = 12;
static const uint16_t kCombiningMarkTable3[12] = { 53605, 20841, 53613, 20850, 53627, 20866, 53637, 20875, 53674, 20909, 53826, 21060 }; // NOLINT
static const uint16_t kCombiningMarkTable28Size = 2;
static const uint16_t kCombiningMarkTable28[2] = { 33024, 495 }; // NOLINT
bool CombiningMark::Is(uchar c) {
  int chunk_index = c >> 15;
  switch (chunk_index) {
    case 0: return LookupPredicate(kCombiningMarkTable0,
                                       kCombiningMarkTable0Size,
                                       c);
    case 1: return LookupPredicate(kCombiningMarkTable1,
                                       kCombiningMarkTable1Size,
                                       c);
    case 2: return LookupPredicate(kCombiningMarkTable2,
                                       kCombiningMarkTable2Size,
                                       c);
    case 3: return LookupPredicate(kCombiningMarkTable3,
                                       kCombiningMarkTable3Size,
                                       c);
    case 28: return LookupPredicate(kCombiningMarkTable28,
                                       kCombiningMarkTable28Size,
                                       c);
    default: return false;
  }
}

// ConnectorPunctuation: point.category == 'Pc'

static const uint16_t kConnectorPunctuationTable0Size = 4;
static const uint16_t kConnectorPunctuationTable0[4] = { 95, 41023, 8256, 8276 }; // NOLINT
static const uint16_t kConnectorPunctuationTable1Size = 5;
static const uint16_t kConnectorPunctuationTable1[5] = { 65075, 32308, 65101, 32335, 32575 }; // NOLINT
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

static const MultiCharacterSpecialCase<3> kToLowercaseMultiStrings0[] = { {2, {105, 775}}, {0, {0}} }; // NOLINT
static const uint16_t kToLowercaseTable0Size = 531;
static const uint16_t kToLowercaseTable0[1062] = { 32833, 128, 90, 128, 32960, 128, 214, 128, 32984, 128, 222, 128, 256, 4, 258, 4, 260, 4, 262, 4, 264, 4, 266, 4, 268, 4, 270, 4, 272, 4, 274, 4, 276, 4, 278, 4, 280, 4, 282, 4, 284, 4, 286, 4, 288, 4, 290, 4, 292, 4, 294, 4, 296, 4, 298, 4, 300, 4, 302, 4, 304, 1, 306, 4, 308, 4, 310, 4, 313, 4, 315, 4, 317, 4, 319, 4, 321, 4, 323, 4, 325, 4, 327, 4, 330, 4, 332, 4, 334, 4, 336, 4, 338, 4, 340, 4, 342, 4, 344, 4, 346, 4, 348, 4, 350, 4, 352, 4, 354, 4, 356, 4, 358, 4, 360, 4, 362, 4, 364, 4, 366, 4, 368, 4, 370, 4, 372, 4, 374, 4, 376, static_cast<uint16_t>(-484), 377, 4, 379, 4, 381, 4, 385, 840, 386, 4, 388, 4, 390, 824, 391, 4, 33161, 820, 394, 820, 395, 4, 398, 316, 399, 808, 400, 812, 401, 4, 403, 820, 404, 828, 406, 844, 407, 836, 408, 4, 412, 844, 413, 852, 415, 856, 416, 4, 418, 4, 420, 4, 422, 872, 423, 4, 425, 872, 428, 4, 430, 872, 431, 4, 33201, 868, 434, 868, 435, 4, 437, 4, 439, 876, 440, 4, 444, 4, 452, 8, 453, 4, 455, 8, 456, 4, 458, 8, 459, 4, 461, 4, 463, 4, 465, 4, 467, 4, 469, 4, 471, 4, 473, 4, 475, 4, 478, 4, 480, 4, 482, 4, 484, 4, 486, 4, 488, 4, 490, 4, 492, 4, 494, 4, 497, 8, 498, 4, 500, 4, 502, static_cast<uint16_t>(-388), 503, static_cast<uint16_t>(-224), 504, 4, 506, 4, 508, 4, 510, 4, 512, 4, 514, 4, 516, 4, 518, 4, 520, 4, 522, 4, 524, 4, 526, 4, 528, 4, 530, 4, 532, 4, 534, 4, 536, 4, 538, 4, 540, 4, 542, 4, 544, static_cast<uint16_t>(-520), 546, 4, 548, 4, 550, 4, 552, 4, 554, 4, 556, 4, 558, 4, 560, 4, 562, 4, 570, 43180, 571, 4, 573, static_cast<uint16_t>(-652), 574, 43168, 577, 4, 579, static_cast<uint16_t>(-780), 580, 276, 581, 284, 582, 4, 584, 4, 586, 4, 588, 4, 590, 4, 902, 152, 33672, 148, 906, 148, 908, 256, 33678, 252, 911, 252, 33681, 128, 929, 128, 33699, 6, 939, 128, 984, 4, 986, 4, 988, 4, 990, 4, 992, 4, 994, 4, 996, 4, 998, 4, 1000, 4, 1002, 4, 1004, 4, 1006, 4, 1012, static_cast<uint16_t>(-240), 1015, 4, 1017, static_cast<uint16_t>(-28), 1018, 4, 33789, static_cast<uint16_t>(-520), 1023, static_cast<uint16_t>(-520), 33792, 320, 1039, 320, 33808, 128, 1071, 128, 1120, 4, 1122, 4, 1124, 4, 1126, 4, 1128, 4, 1130, 4, 1132, 4, 1134, 4, 1136, 4, 1138, 4, 1140, 4, 1142, 4, 1144, 4, 1146, 4, 1148, 4, 1150, 4, 1152, 4, 1162, 4, 1164, 4, 1166, 4, 1168, 4, 1170, 4, 1172, 4, 1174, 4, 1176, 4, 1178, 4, 1180, 4, 1182, 4, 1184, 4, 1186, 4, 1188, 4, 1190, 4, 1192, 4, 1194, 4, 1196, 4, 1198, 4, 1200, 4, 1202, 4, 1204, 4, 1206, 4, 1208, 4, 1210, 4, 1212, 4, 1214, 4, 1216, 60, 1217, 4, 1219, 4, 1221, 4, 1223, 4, 1225, 4, 1227, 4, 1229, 4, 1232, 4, 1234, 4, 1236, 4, 1238, 4, 1240, 4, 1242, 4, 1244, 4, 1246, 4, 1248, 4, 1250, 4, 1252, 4, 1254, 4, 1256, 4, 1258, 4, 1260, 4, 1262, 4, 1264, 4, 1266, 4, 1268, 4, 1270, 4, 1272, 4, 1274, 4, 1276, 4, 1278, 4, 1280, 4, 1282, 4, 1284, 4, 1286, 4, 1288, 4, 1290, 4, 1292, 4, 1294, 4, 1296, 4, 1298, 4, 34097, 192, 1366, 192, 37024, 29056, 4293, 29056, 7680, 4, 7682, 4, 7684, 4, 7686, 4, 7688, 4, 7690, 4, 7692, 4, 7694, 4, 7696, 4, 7698, 4, 7700, 4, 7702, 4, 7704, 4, 7706, 4, 7708, 4, 7710, 4, 7712, 4, 7714, 4, 7716, 4, 7718, 4, 7720, 4, 7722, 4, 7724, 4, 7726, 4, 7728, 4, 7730, 4, 7732, 4, 7734, 4, 7736, 4, 7738, 4, 7740, 4, 7742, 4, 7744, 4, 7746, 4, 7748, 4, 7750, 4, 7752, 4, 7754, 4, 7756, 4, 7758, 4, 7760, 4, 7762, 4, 7764, 4, 7766, 4, 7768, 4, 7770, 4, 7772, 4, 7774, 4, 7776, 4, 7778, 4, 7780, 4, 7782, 4, 7784, 4, 7786, 4, 7788, 4, 7790, 4, 7792, 4, 7794, 4, 7796, 4, 7798, 4, 7800, 4, 7802, 4, 7804, 4, 7806, 4, 7808, 4, 7810, 4, 7812, 4, 7814, 4, 7816, 4, 7818, 4, 7820, 4, 7822, 4, 7824, 4, 7826, 4, 7828, 4, 7840, 4, 7842, 4, 7844, 4, 7846, 4, 7848, 4, 7850, 4, 7852, 4, 7854, 4, 7856, 4, 7858, 4, 7860, 4, 7862, 4, 7864, 4, 7866, 4, 7868, 4, 7870, 4, 7872, 4, 7874, 4, 7876, 4, 7878, 4, 7880, 4, 7882, 4, 7884, 4, 7886, 4, 7888, 4, 7890, 4, 7892, 4, 7894, 4, 7896, 4, 7898, 4, 7900, 4, 7902, 4, 7904, 4, 7906, 4, 7908, 4, 7910, 4, 7912, 4, 7914, 4, 7916, 4, 7918, 4, 7920, 4, 7922, 4, 7924, 4, 7926, 4, 7928, 4, 40712, static_cast<uint16_t>(-32), 7951, static_cast<uint16_t>(-32), 40728, static_cast<uint16_t>(-32), 7965, static_cast<uint16_t>(-32), 40744, static_cast<uint16_t>(-32), 7983, static_cast<uint16_t>(-32), 40760, static_cast<uint16_t>(-32), 7999, static_cast<uint16_t>(-32), 40776, static_cast<uint16_t>(-32), 8013, static_cast<uint16_t>(-32), 8025, static_cast<uint16_t>(-32), 8027, static_cast<uint16_t>(-32), 8029, static_cast<uint16_t>(-32), 8031, static_cast<uint16_t>(-32), 40808, static_cast<uint16_t>(-32), 8047, static_cast<uint16_t>(-32), 40840, static_cast<uint16_t>(-32), 8079, static_cast<uint16_t>(-32), 40856, static_cast<uint16_t>(-32), 8095, static_cast<uint16_t>(-32), 40872, static_cast<uint16_t>(-32), 8111, static_cast<uint16_t>(-32), 40888, static_cast<uint16_t>(-32), 8121, static_cast<uint16_t>(-32), 40890, static_cast<uint16_t>(-296), 8123, static_cast<uint16_t>(-296), 8124, static_cast<uint16_t>(-36), 40904, static_cast<uint16_t>(-344), 8139, static_cast<uint16_t>(-344), 8140, static_cast<uint16_t>(-36), 40920, static_cast<uint16_t>(-32), 8153, static_cast<uint16_t>(-32), 40922, static_cast<uint16_t>(-400), 8155, static_cast<uint16_t>(-400), 40936, static_cast<uint16_t>(-32), 8169, static_cast<uint16_t>(-32), 40938, static_cast<uint16_t>(-448), 8171, static_cast<uint16_t>(-448), 8172, static_cast<uint16_t>(-28), 40952, static_cast<uint16_t>(-512), 8185, static_cast<uint16_t>(-512), 40954, static_cast<uint16_t>(-504), 8187, static_cast<uint16_t>(-504), 8188, static_cast<uint16_t>(-36), 8486, static_cast<uint16_t>(-30068), 8490, static_cast<uint16_t>(-33532), 8491, static_cast<uint16_t>(-33048), 8498, 112, 41312, 64, 8559, 64, 8579, 4, 42166, 104, 9423, 104, 44032, 192, 11310, 192, 11360, 4, 11362, static_cast<uint16_t>(-42972), 11363, static_cast<uint16_t>(-15256), 11364, static_cast<uint16_t>(-42908), 11367, 4, 11369, 4, 11371, 4, 11381, 4, 11392, 4, 11394, 4, 11396, 4, 11398, 4, 11400, 4, 11402, 4, 11404, 4, 11406, 4, 11408, 4, 11410, 4, 11412, 4, 11414, 4, 11416, 4, 11418, 4, 11420, 4, 11422, 4, 11424, 4, 11426, 4, 11428, 4, 11430, 4, 11432, 4, 11434, 4, 11436, 4, 11438, 4, 11440, 4, 11442, 4, 11444, 4, 11446, 4, 11448, 4, 11450, 4, 11452, 4, 11454, 4, 11456, 4, 11458, 4, 11460, 4, 11462, 4, 11464, 4, 11466, 4, 11468, 4, 11470, 4, 11472, 4, 11474, 4, 11476, 4, 11478, 4, 11480, 4, 11482, 4, 11484, 4, 11486, 4, 11488, 4, 11490, 4 }; // NOLINT
static const MultiCharacterSpecialCase<3> kToLowercaseMultiStrings1[] = { {0, {0}} }; // NOLINT
static const uint16_t kToLowercaseTable1Size = 2;
static const uint16_t kToLowercaseTable1[4] = { 65313, 128, 32570, 128 }; // NOLINT
static const MultiCharacterSpecialCase<3> kToLowercaseMultiStrings2[] = { {0, {0}} }; // NOLINT
static const uint16_t kToLowercaseTable2Size = 2;
static const uint16_t kToLowercaseTable2[4] = { 33792, 160, 1063, 160 }; // NOLINT
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
    case 2: return LookupMapping(kToLowercaseTable2,
                                     kToLowercaseTable2Size,
                                     kToLowercaseMultiStrings2,
                                     c,
                                     n,
                                     result,
                                     allow_caching_ptr);
    default: return 0;
  }
}

static const MultiCharacterSpecialCase<3> kToUppercaseMultiStrings0[] = { {2, {83, 83}}, {2, {700, 78}}, {2, {74, 780}}, {3, {921, 776, 769}}, {3, {933, 776, 769}}, {2, {1333, 1362}}, {2, {72, 817}}, {2, {84, 776}}, {2, {87, 778}}, {2, {89, 778}}, {2, {65, 702}}, {2, {933, 787}}, {3, {933, 787, 768}}, {3, {933, 787, 769}}, {3, {933, 787, 834}}, {2, {7944, 921}}, {2, {7945, 921}}, {2, {7946, 921}}, {2, {7947, 921}}, {2, {7948, 921}}, {2, {7949, 921}}, {2, {7950, 921}}, {2, {7951, 921}}, {2, {7944, 921}}, {2, {7945, 921}}, {2, {7946, 921}}, {2, {7947, 921}}, {2, {7948, 921}}, {2, {7949, 921}}, {2, {7950, 921}}, {2, {7951, 921}}, {2, {7976, 921}}, {2, {7977, 921}}, {2, {7978, 921}}, {2, {7979, 921}}, {2, {7980, 921}}, {2, {7981, 921}}, {2, {7982, 921}}, {2, {7983, 921}}, {2, {7976, 921}}, {2, {7977, 921}}, {2, {7978, 921}}, {2, {7979, 921}}, {2, {7980, 921}}, {2, {7981, 921}}, {2, {7982, 921}}, {2, {7983, 921}}, {2, {8040, 921}}, {2, {8041, 921}}, {2, {8042, 921}}, {2, {8043, 921}}, {2, {8044, 921}}, {2, {8045, 921}}, {2, {8046, 921}}, {2, {8047, 921}}, {2, {8040, 921}}, {2, {8041, 921}}, {2, {8042, 921}}, {2, {8043, 921}}, {2, {8044, 921}}, {2, {8045, 921}}, {2, {8046, 921}}, {2, {8047, 921}}, {2, {8122, 921}}, {2, {913, 921}}, {2, {902, 921}}, {2, {913, 834}}, {3, {913, 834, 921}}, {2, {913, 921}}, {2, {8138, 921}}, {2, {919, 921}}, {2, {905, 921}}, {2, {919, 834}}, {3, {919, 834, 921}}, {2, {919, 921}}, {3, {921, 776, 768}}, {3, {921, 776, 769}}, {2, {921, 834}}, {3, {921, 776, 834}}, {3, {933, 776, 768}}, {3, {933, 776, 769}}, {2, {929, 787}}, {2, {933, 834}}, {3, {933, 776, 834}}, {2, {8186, 921}}, {2, {937, 921}}, {2, {911, 921}}, {2, {937, 834}}, {3, {937, 834, 921}}, {2, {937, 921}}, {0, {0}} }; // NOLINT
static const uint16_t kToUppercaseTable0Size = 621;
static const uint16_t kToUppercaseTable0[1242] = { 32865, static_cast<uint16_t>(-128), 122, static_cast<uint16_t>(-128), 181, 2972, 223, 1, 32992, static_cast<uint16_t>(-128), 246, static_cast<uint16_t>(-128), 33016, static_cast<uint16_t>(-128), 254, static_cast<uint16_t>(-128), 255, 484, 257, static_cast<uint16_t>(-4), 259, static_cast<uint16_t>(-4), 261, static_cast<uint16_t>(-4), 263, static_cast<uint16_t>(-4), 265, static_cast<uint16_t>(-4), 267, static_cast<uint16_t>(-4), 269, static_cast<uint16_t>(-4), 271, static_cast<uint16_t>(-4), 273, static_cast<uint16_t>(-4), 275, static_cast<uint16_t>(-4), 277, static_cast<uint16_t>(-4), 279, static_cast<uint16_t>(-4), 281, static_cast<uint16_t>(-4), 283, static_cast<uint16_t>(-4), 285, static_cast<uint16_t>(-4), 287, static_cast<uint16_t>(-4), 289, static_cast<uint16_t>(-4), 291, static_cast<uint16_t>(-4), 293, static_cast<uint16_t>(-4), 295, static_cast<uint16_t>(-4), 297, static_cast<uint16_t>(-4), 299, static_cast<uint16_t>(-4), 301, static_cast<uint16_t>(-4), 303, static_cast<uint16_t>(-4), 305, static_cast<uint16_t>(-928), 307, static_cast<uint16_t>(-4), 309, static_cast<uint16_t>(-4), 311, static_cast<uint16_t>(-4), 314, static_cast<uint16_t>(-4), 316, static_cast<uint16_t>(-4), 318, static_cast<uint16_t>(-4), 320, static_cast<uint16_t>(-4), 322, static_cast<uint16_t>(-4), 324, static_cast<uint16_t>(-4), 326, static_cast<uint16_t>(-4), 328, static_cast<uint16_t>(-4), 329, 5, 331, static_cast<uint16_t>(-4), 333, static_cast<uint16_t>(-4), 335, static_cast<uint16_t>(-4), 337, static_cast<uint16_t>(-4), 339, static_cast<uint16_t>(-4), 341, static_cast<uint16_t>(-4), 343, static_cast<uint16_t>(-4), 345, static_cast<uint16_t>(-4), 347, static_cast<uint16_t>(-4), 349, static_cast<uint16_t>(-4), 351, static_cast<uint16_t>(-4), 353, static_cast<uint16_t>(-4), 355, static_cast<uint16_t>(-4), 357, static_cast<uint16_t>(-4), 359, static_cast<uint16_t>(-4), 361, static_cast<uint16_t>(-4), 363, static_cast<uint16_t>(-4), 365, static_cast<uint16_t>(-4), 367, static_cast<uint16_t>(-4), 369, static_cast<uint16_t>(-4), 371, static_cast<uint16_t>(-4), 373, static_cast<uint16_t>(-4), 375, static_cast<uint16_t>(-4), 378, static_cast<uint16_t>(-4), 380, static_cast<uint16_t>(-4), 382, static_cast<uint16_t>(-4), 383, static_cast<uint16_t>(-1200), 384, 780, 387, static_cast<uint16_t>(-4), 389, static_cast<uint16_t>(-4), 392, static_cast<uint16_t>(-4), 396, static_cast<uint16_t>(-4), 402, static_cast<uint16_t>(-4), 405, 388, 409, static_cast<uint16_t>(-4), 410, 652, 414, 520, 417, static_cast<uint16_t>(-4), 419, static_cast<uint16_t>(-4), 421, static_cast<uint16_t>(-4), 424, static_cast<uint16_t>(-4), 429, static_cast<uint16_t>(-4), 432, static_cast<uint16_t>(-4), 436, static_cast<uint16_t>(-4), 438, static_cast<uint16_t>(-4), 441, static_cast<uint16_t>(-4), 445, static_cast<uint16_t>(-4), 447, 224, 453, static_cast<uint16_t>(-4), 454, static_cast<uint16_t>(-8), 456, static_cast<uint16_t>(-4), 457, static_cast<uint16_t>(-8), 459, static_cast<uint16_t>(-4), 460, static_cast<uint16_t>(-8), 462, static_cast<uint16_t>(-4), 464, static_cast<uint16_t>(-4), 466, static_cast<uint16_t>(-4), 468, static_cast<uint16_t>(-4), 470, static_cast<uint16_t>(-4), 472, static_cast<uint16_t>(-4), 474, static_cast<uint16_t>(-4), 476, static_cast<uint16_t>(-4), 477, static_cast<uint16_t>(-316), 479, static_cast<uint16_t>(-4), 481, static_cast<uint16_t>(-4), 483, static_cast<uint16_t>(-4), 485, static_cast<uint16_t>(-4), 487, static_cast<uint16_t>(-4), 489, static_cast<uint16_t>(-4), 491, static_cast<uint16_t>(-4), 493, static_cast<uint16_t>(-4), 495, static_cast<uint16_t>(-4), 496, 9, 498, static_cast<uint16_t>(-4), 499, static_cast<uint16_t>(-8), 501, static_cast<uint16_t>(-4), 505, static_cast<uint16_t>(-4), 507, static_cast<uint16_t>(-4), 509, static_cast<uint16_t>(-4), 511, static_cast<uint16_t>(-4), 513, static_cast<uint16_t>(-4), 515, static_cast<uint16_t>(-4), 517, static_cast<uint16_t>(-4), 519, static_cast<uint16_t>(-4), 521, static_cast<uint16_t>(-4), 523, static_cast<uint16_t>(-4), 525, static_cast<uint16_t>(-4), 527, static_cast<uint16_t>(-4), 529, static_cast<uint16_t>(-4), 531, static_cast<uint16_t>(-4), 533, static_cast<uint16_t>(-4), 535, static_cast<uint16_t>(-4), 537, static_cast<uint16_t>(-4), 539, static_cast<uint16_t>(-4), 541, static_cast<uint16_t>(-4), 543, static_cast<uint16_t>(-4), 547, static_cast<uint16_t>(-4), 549, static_cast<uint16_t>(-4), 551, static_cast<uint16_t>(-4), 553, static_cast<uint16_t>(-4), 555, static_cast<uint16_t>(-4), 557, static_cast<uint16_t>(-4), 559, static_cast<uint16_t>(-4), 561, static_cast<uint16_t>(-4), 563, static_cast<uint16_t>(-4), 572, static_cast<uint16_t>(-4), 578, static_cast<uint16_t>(-4), 583, static_cast<uint16_t>(-4), 585, static_cast<uint16_t>(-4), 587, static_cast<uint16_t>(-4), 589, static_cast<uint16_t>(-4), 591, static_cast<uint16_t>(-4), 595, static_cast<uint16_t>(-840), 596, static_cast<uint16_t>(-824), 33366, static_cast<uint16_t>(-820), 599, static_cast<uint16_t>(-820), 601, static_cast<uint16_t>(-808), 603, static_cast<uint16_t>(-812), 608, static_cast<uint16_t>(-820), 611, static_cast<uint16_t>(-828), 616, static_cast<uint16_t>(-836), 617, static_cast<uint16_t>(-844), 619, 42972, 623, static_cast<uint16_t>(-844), 626, static_cast<uint16_t>(-852), 629, static_cast<uint16_t>(-856), 637, 42908, 640, static_cast<uint16_t>(-872), 643, static_cast<uint16_t>(-872), 648, static_cast<uint16_t>(-872), 649, static_cast<uint16_t>(-276), 33418, static_cast<uint16_t>(-868), 651, static_cast<uint16_t>(-868), 652, static_cast<uint16_t>(-284), 658, static_cast<uint16_t>(-876), 837, 336, 33659, 520, 893, 520, 912, 13, 940, static_cast<uint16_t>(-152), 33709, static_cast<uint16_t>(-148), 943, static_cast<uint16_t>(-148), 944, 17, 33713, static_cast<uint16_t>(-128), 961, static_cast<uint16_t>(-128), 962, static_cast<uint16_t>(-124), 33731, static_cast<uint16_t>(-128), 971, static_cast<uint16_t>(-128), 972, static_cast<uint16_t>(-256), 33741, static_cast<uint16_t>(-252), 974, static_cast<uint16_t>(-252), 976, static_cast<uint16_t>(-248), 977, static_cast<uint16_t>(-228), 981, static_cast<uint16_t>(-188), 982, static_cast<uint16_t>(-216), 985, static_cast<uint16_t>(-4), 987, static_cast<uint16_t>(-4), 989, static_cast<uint16_t>(-4), 991, static_cast<uint16_t>(-4), 993, static_cast<uint16_t>(-4), 995, static_cast<uint16_t>(-4), 997, static_cast<uint16_t>(-4), 999, static_cast<uint16_t>(-4), 1001, static_cast<uint16_t>(-4), 1003, static_cast<uint16_t>(-4), 1005, static_cast<uint16_t>(-4), 1007, static_cast<uint16_t>(-4), 1008, static_cast<uint16_t>(-344), 1009, static_cast<uint16_t>(-320), 1010, 28, 1013, static_cast<uint16_t>(-384), 1016, static_cast<uint16_t>(-4), 1019, static_cast<uint16_t>(-4), 33840, static_cast<uint16_t>(-128), 1103, static_cast<uint16_t>(-128), 33872, static_cast<uint16_t>(-320), 1119, static_cast<uint16_t>(-320), 1121, static_cast<uint16_t>(-4), 1123, static_cast<uint16_t>(-4), 1125, static_cast<uint16_t>(-4), 1127, static_cast<uint16_t>(-4), 1129, static_cast<uint16_t>(-4), 1131, static_cast<uint16_t>(-4), 1133, static_cast<uint16_t>(-4), 1135, static_cast<uint16_t>(-4), 1137, static_cast<uint16_t>(-4), 1139, static_cast<uint16_t>(-4), 1141, static_cast<uint16_t>(-4), 1143, static_cast<uint16_t>(-4), 1145, static_cast<uint16_t>(-4), 1147, static_cast<uint16_t>(-4), 1149, static_cast<uint16_t>(-4), 1151, static_cast<uint16_t>(-4), 1153, static_cast<uint16_t>(-4), 1163, static_cast<uint16_t>(-4), 1165, static_cast<uint16_t>(-4), 1167, static_cast<uint16_t>(-4), 1169, static_cast<uint16_t>(-4), 1171, static_cast<uint16_t>(-4), 1173, static_cast<uint16_t>(-4), 1175, static_cast<uint16_t>(-4), 1177, static_cast<uint16_t>(-4), 1179, static_cast<uint16_t>(-4), 1181, static_cast<uint16_t>(-4), 1183, static_cast<uint16_t>(-4), 1185, static_cast<uint16_t>(-4), 1187, static_cast<uint16_t>(-4), 1189, static_cast<uint16_t>(-4), 1191, static_cast<uint16_t>(-4), 1193, static_cast<uint16_t>(-4), 1195, static_cast<uint16_t>(-4), 1197, static_cast<uint16_t>(-4), 1199, static_cast<uint16_t>(-4), 1201, static_cast<uint16_t>(-4), 1203, static_cast<uint16_t>(-4), 1205, static_cast<uint16_t>(-4), 1207, static_cast<uint16_t>(-4), 1209, static_cast<uint16_t>(-4), 1211, static_cast<uint16_t>(-4), 1213, static_cast<uint16_t>(-4), 1215, static_cast<uint16_t>(-4), 1218, static_cast<uint16_t>(-4), 1220, static_cast<uint16_t>(-4), 1222, static_cast<uint16_t>(-4), 1224, static_cast<uint16_t>(-4), 1226, static_cast<uint16_t>(-4), 1228, static_cast<uint16_t>(-4), 1230, static_cast<uint16_t>(-4), 1231, static_cast<uint16_t>(-60), 1233, static_cast<uint16_t>(-4), 1235, static_cast<uint16_t>(-4), 1237, static_cast<uint16_t>(-4), 1239, static_cast<uint16_t>(-4), 1241, static_cast<uint16_t>(-4), 1243, static_cast<uint16_t>(-4), 1245, static_cast<uint16_t>(-4), 1247, static_cast<uint16_t>(-4), 1249, static_cast<uint16_t>(-4), 1251, static_cast<uint16_t>(-4), 1253, static_cast<uint16_t>(-4), 1255, static_cast<uint16_t>(-4), 1257, static_cast<uint16_t>(-4), 1259, static_cast<uint16_t>(-4), 1261, static_cast<uint16_t>(-4), 1263, static_cast<uint16_t>(-4), 1265, static_cast<uint16_t>(-4), 1267, static_cast<uint16_t>(-4), 1269, static_cast<uint16_t>(-4), 1271, static_cast<uint16_t>(-4), 1273, static_cast<uint16_t>(-4), 1275, static_cast<uint16_t>(-4), 1277, static_cast<uint16_t>(-4), 1279, static_cast<uint16_t>(-4), 1281, static_cast<uint16_t>(-4), 1283, static_cast<uint16_t>(-4), 1285, static_cast<uint16_t>(-4), 1287, static_cast<uint16_t>(-4), 1289, static_cast<uint16_t>(-4), 1291, static_cast<uint16_t>(-4), 1293, static_cast<uint16_t>(-4), 1295, static_cast<uint16_t>(-4), 1297, static_cast<uint16_t>(-4), 1299, static_cast<uint16_t>(-4), 34145, static_cast<uint16_t>(-192), 1414, static_cast<uint16_t>(-192), 1415, 21, 7549, 15256, 7681, static_cast<uint16_t>(-4), 7683, static_cast<uint16_t>(-4), 7685, static_cast<uint16_t>(-4), 7687, static_cast<uint16_t>(-4), 7689, static_cast<uint16_t>(-4), 7691, static_cast<uint16_t>(-4), 7693, static_cast<uint16_t>(-4), 7695, static_cast<uint16_t>(-4), 7697, static_cast<uint16_t>(-4), 7699, static_cast<uint16_t>(-4), 7701, static_cast<uint16_t>(-4), 7703, static_cast<uint16_t>(-4), 7705, static_cast<uint16_t>(-4), 7707, static_cast<uint16_t>(-4), 7709, static_cast<uint16_t>(-4), 7711, static_cast<uint16_t>(-4), 7713, static_cast<uint16_t>(-4), 7715, static_cast<uint16_t>(-4), 7717, static_cast<uint16_t>(-4), 7719, static_cast<uint16_t>(-4), 7721, static_cast<uint16_t>(-4), 7723, static_cast<uint16_t>(-4), 7725, static_cast<uint16_t>(-4), 7727, static_cast<uint16_t>(-4), 7729, static_cast<uint16_t>(-4), 7731, static_cast<uint16_t>(-4), 7733, static_cast<uint16_t>(-4), 7735, static_cast<uint16_t>(-4), 7737, static_cast<uint16_t>(-4), 7739, static_cast<uint16_t>(-4), 7741, static_cast<uint16_t>(-4), 7743, static_cast<uint16_t>(-4), 7745, static_cast<uint16_t>(-4), 7747, static_cast<uint16_t>(-4), 7749, static_cast<uint16_t>(-4), 7751, static_cast<uint16_t>(-4), 7753, static_cast<uint16_t>(-4), 7755, static_cast<uint16_t>(-4), 7757, static_cast<uint16_t>(-4), 7759, static_cast<uint16_t>(-4), 7761, static_cast<uint16_t>(-4), 7763, static_cast<uint16_t>(-4), 7765, static_cast<uint16_t>(-4), 7767, static_cast<uint16_t>(-4), 7769, static_cast<uint16_t>(-4), 7771, static_cast<uint16_t>(-4), 7773, static_cast<uint16_t>(-4), 7775, static_cast<uint16_t>(-4), 7777, static_cast<uint16_t>(-4), 7779, static_cast<uint16_t>(-4), 7781, static_cast<uint16_t>(-4), 7783, static_cast<uint16_t>(-4), 7785, static_cast<uint16_t>(-4), 7787, static_cast<uint16_t>(-4), 7789, static_cast<uint16_t>(-4), 7791, static_cast<uint16_t>(-4), 7793, static_cast<uint16_t>(-4), 7795, static_cast<uint16_t>(-4), 7797, static_cast<uint16_t>(-4), 7799, static_cast<uint16_t>(-4), 7801, static_cast<uint16_t>(-4), 7803, static_cast<uint16_t>(-4), 7805, static_cast<uint16_t>(-4), 7807, static_cast<uint16_t>(-4), 7809, static_cast<uint16_t>(-4), 7811, static_cast<uint16_t>(-4), 7813, static_cast<uint16_t>(-4), 7815, static_cast<uint16_t>(-4), 7817, static_cast<uint16_t>(-4), 7819, static_cast<uint16_t>(-4), 7821, static_cast<uint16_t>(-4), 7823, static_cast<uint16_t>(-4), 7825, static_cast<uint16_t>(-4), 7827, static_cast<uint16_t>(-4), 7829, static_cast<uint16_t>(-4), 7830, 25, 7831, 29, 7832, 33, 7833, 37, 7834, 41, 7835, static_cast<uint16_t>(-236), 7841, static_cast<uint16_t>(-4), 7843, static_cast<uint16_t>(-4), 7845, static_cast<uint16_t>(-4), 7847, static_cast<uint16_t>(-4), 7849, static_cast<uint16_t>(-4), 7851, static_cast<uint16_t>(-4), 7853, static_cast<uint16_t>(-4), 7855, static_cast<uint16_t>(-4), 7857, static_cast<uint16_t>(-4), 7859, static_cast<uint16_t>(-4), 7861, static_cast<uint16_t>(-4), 7863, static_cast<uint16_t>(-4), 7865, static_cast<uint16_t>(-4), 7867, static_cast<uint16_t>(-4), 7869, static_cast<uint16_t>(-4), 7871, static_cast<uint16_t>(-4), 7873, static_cast<uint16_t>(-4), 7875, static_cast<uint16_t>(-4), 7877, static_cast<uint16_t>(-4), 7879, static_cast<uint16_t>(-4), 7881, static_cast<uint16_t>(-4), 7883, static_cast<uint16_t>(-4), 7885, static_cast<uint16_t>(-4), 7887, static_cast<uint16_t>(-4), 7889, static_cast<uint16_t>(-4), 7891, static_cast<uint16_t>(-4), 7893, static_cast<uint16_t>(-4), 7895, static_cast<uint16_t>(-4), 7897, static_cast<uint16_t>(-4), 7899, static_cast<uint16_t>(-4), 7901, static_cast<uint16_t>(-4), 7903, static_cast<uint16_t>(-4), 7905, static_cast<uint16_t>(-4), 7907, static_cast<uint16_t>(-4), 7909, static_cast<uint16_t>(-4), 7911, static_cast<uint16_t>(-4), 7913, static_cast<uint16_t>(-4), 7915, static_cast<uint16_t>(-4), 7917, static_cast<uint16_t>(-4), 7919, static_cast<uint16_t>(-4), 7921, static_cast<uint16_t>(-4), 7923, static_cast<uint16_t>(-4), 7925, static_cast<uint16_t>(-4), 7927, static_cast<uint16_t>(-4), 7929, static_cast<uint16_t>(-4), 40704, 32, 7943, 32, 40720, 32, 7957, 32, 40736, 32, 7975, 32, 40752, 32, 7991, 32, 40768, 32, 8005, 32, 8016, 45, 8017, 32, 8018, 49, 8019, 32, 8020, 53, 8021, 32, 8022, 57, 8023, 32, 40800, 32, 8039, 32, 40816, 296, 8049, 296, 40818, 344, 8053, 344, 40822, 400, 8055, 400, 40824, 512, 8057, 512, 40826, 448, 8059, 448, 40828, 504, 8061, 504, 8064, 61, 8065, 65, 8066, 69, 8067, 73, 8068, 77, 8069, 81, 8070, 85, 8071, 89, 8072, 93, 8073, 97, 8074, 101, 8075, 105, 8076, 109, 8077, 113, 8078, 117, 8079, 121, 8080, 125, 8081, 129, 8082, 133, 8083, 137, 8084, 141, 8085, 145, 8086, 149, 8087, 153, 8088, 157, 8089, 161, 8090, 165, 8091, 169, 8092, 173, 8093, 177, 8094, 181, 8095, 185, 8096, 189, 8097, 193, 8098, 197, 8099, 201, 8100, 205, 8101, 209, 8102, 213, 8103, 217, 8104, 221, 8105, 225, 8106, 229, 8107, 233, 8108, 237, 8109, 241, 8110, 245, 8111, 249, 40880, 32, 8113, 32, 8114, 253, 8115, 257, 8116, 261, 8118, 265, 8119, 269, 8124, 273, 8126, static_cast<uint16_t>(-28820), 8130, 277, 8131, 281, 8132, 285, 8134, 289, 8135, 293, 8140, 297, 40912, 32, 8145, 32, 8146, 301, 8147, 305, 8150, 309, 8151, 313, 40928, 32, 8161, 32, 8162, 317, 8163, 321, 8164, 325, 8165, 28, 8166, 329, 8167, 333, 8178, 337, 8179, 341, 8180, 345, 8182, 349, 8183, 353, 8188, 357, 8526, static_cast<uint16_t>(-112), 41328, static_cast<uint16_t>(-64), 8575, static_cast<uint16_t>(-64), 8580, static_cast<uint16_t>(-4), 42192, static_cast<uint16_t>(-104), 9449, static_cast<uint16_t>(-104), 44080, static_cast<uint16_t>(-192), 11358, static_cast<uint16_t>(-192), 11361, static_cast<uint16_t>(-4), 11365, static_cast<uint16_t>(-43180), 11366, static_cast<uint16_t>(-43168), 11368, static_cast<uint16_t>(-4), 11370, static_cast<uint16_t>(-4), 11372, static_cast<uint16_t>(-4), 11382, static_cast<uint16_t>(-4), 11393, static_cast<uint16_t>(-4), 11395, static_cast<uint16_t>(-4), 11397, static_cast<uint16_t>(-4), 11399, static_cast<uint16_t>(-4), 11401, static_cast<uint16_t>(-4), 11403, static_cast<uint16_t>(-4), 11405, static_cast<uint16_t>(-4), 11407, static_cast<uint16_t>(-4), 11409, static_cast<uint16_t>(-4), 11411, static_cast<uint16_t>(-4), 11413, static_cast<uint16_t>(-4), 11415, static_cast<uint16_t>(-4), 11417, static_cast<uint16_t>(-4), 11419, static_cast<uint16_t>(-4), 11421, static_cast<uint16_t>(-4), 11423, static_cast<uint16_t>(-4), 11425, static_cast<uint16_t>(-4), 11427, static_cast<uint16_t>(-4), 11429, static_cast<uint16_t>(-4), 11431, static_cast<uint16_t>(-4), 11433, static_cast<uint16_t>(-4), 11435, static_cast<uint16_t>(-4), 11437, static_cast<uint16_t>(-4), 11439, static_cast<uint16_t>(-4), 11441, static_cast<uint16_t>(-4), 11443, static_cast<uint16_t>(-4), 11445, static_cast<uint16_t>(-4), 11447, static_cast<uint16_t>(-4), 11449, static_cast<uint16_t>(-4), 11451, static_cast<uint16_t>(-4), 11453, static_cast<uint16_t>(-4), 11455, static_cast<uint16_t>(-4), 11457, static_cast<uint16_t>(-4), 11459, static_cast<uint16_t>(-4), 11461, static_cast<uint16_t>(-4), 11463, static_cast<uint16_t>(-4), 11465, static_cast<uint16_t>(-4), 11467, static_cast<uint16_t>(-4), 11469, static_cast<uint16_t>(-4), 11471, static_cast<uint16_t>(-4), 11473, static_cast<uint16_t>(-4), 11475, static_cast<uint16_t>(-4), 11477, static_cast<uint16_t>(-4), 11479, static_cast<uint16_t>(-4), 11481, static_cast<uint16_t>(-4), 11483, static_cast<uint16_t>(-4), 11485, static_cast<uint16_t>(-4), 11487, static_cast<uint16_t>(-4), 11489, static_cast<uint16_t>(-4), 11491, static_cast<uint16_t>(-4), 44288, static_cast<uint16_t>(-29056), 11557, static_cast<uint16_t>(-29056) }; // NOLINT
static const MultiCharacterSpecialCase<3> kToUppercaseMultiStrings1[] = { {2, {70, 70}}, {2, {70, 73}}, {2, {70, 76}}, {3, {70, 70, 73}}, {3, {70, 70, 76}}, {2, {83, 84}}, {2, {83, 84}}, {2, {1348, 1350}}, {2, {1348, 1333}}, {2, {1348, 1339}}, {2, {1358, 1350}}, {2, {1348, 1341}}, {0, {0}} }; // NOLINT
static const uint16_t kToUppercaseTable1Size = 14;
static const uint16_t kToUppercaseTable1[28] = { 31488, 1, 31489, 5, 31490, 9, 31491, 13, 31492, 17, 31493, 21, 31494, 25, 31507, 29, 31508, 33, 31509, 37, 31510, 41, 31511, 45, 65345, static_cast<uint16_t>(-128), 32602, static_cast<uint16_t>(-128) }; // NOLINT
static const MultiCharacterSpecialCase<3> kToUppercaseMultiStrings2[] = { {0, {0}} }; // NOLINT
static const uint16_t kToUppercaseTable2Size = 2;
static const uint16_t kToUppercaseTable2[4] = { 33832, static_cast<uint16_t>(-160), 1103, static_cast<uint16_t>(-160) }; // NOLINT
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
    case 2: return LookupMapping(kToUppercaseTable2,
                                     kToUppercaseTable2Size,
                                     kToUppercaseMultiStrings2,
                                     c,
                                     n,
                                     result,
                                     allow_caching_ptr);
    default: return 0;
  }
}

static const MultiCharacterSpecialCase<1> kEcma262CanonicalizeMultiStrings0[] = { {0, {0}} }; // NOLINT
static const uint16_t kEcma262CanonicalizeTable0Size = 530;
static const uint16_t kEcma262CanonicalizeTable0[1060] = { 32865, static_cast<uint16_t>(-128), 122, static_cast<uint16_t>(-128), 181, 2972, 32992, static_cast<uint16_t>(-128), 246, static_cast<uint16_t>(-128), 33016, static_cast<uint16_t>(-128), 254, static_cast<uint16_t>(-128), 255, 484, 257, static_cast<uint16_t>(-4), 259, static_cast<uint16_t>(-4), 261, static_cast<uint16_t>(-4), 263, static_cast<uint16_t>(-4), 265, static_cast<uint16_t>(-4), 267, static_cast<uint16_t>(-4), 269, static_cast<uint16_t>(-4), 271, static_cast<uint16_t>(-4), 273, static_cast<uint16_t>(-4), 275, static_cast<uint16_t>(-4), 277, static_cast<uint16_t>(-4), 279, static_cast<uint16_t>(-4), 281, static_cast<uint16_t>(-4), 283, static_cast<uint16_t>(-4), 285, static_cast<uint16_t>(-4), 287, static_cast<uint16_t>(-4), 289, static_cast<uint16_t>(-4), 291, static_cast<uint16_t>(-4), 293, static_cast<uint16_t>(-4), 295, static_cast<uint16_t>(-4), 297, static_cast<uint16_t>(-4), 299, static_cast<uint16_t>(-4), 301, static_cast<uint16_t>(-4), 303, static_cast<uint16_t>(-4), 304, 0, 307, static_cast<uint16_t>(-4), 309, static_cast<uint16_t>(-4), 311, static_cast<uint16_t>(-4), 314, static_cast<uint16_t>(-4), 316, static_cast<uint16_t>(-4), 318, static_cast<uint16_t>(-4), 320, static_cast<uint16_t>(-4), 322, static_cast<uint16_t>(-4), 324, static_cast<uint16_t>(-4), 326, static_cast<uint16_t>(-4), 328, static_cast<uint16_t>(-4), 331, static_cast<uint16_t>(-4), 333, static_cast<uint16_t>(-4), 335, static_cast<uint16_t>(-4), 337, static_cast<uint16_t>(-4), 339, static_cast<uint16_t>(-4), 341, static_cast<uint16_t>(-4), 343, static_cast<uint16_t>(-4), 345, static_cast<uint16_t>(-4), 347, static_cast<uint16_t>(-4), 349, static_cast<uint16_t>(-4), 351, static_cast<uint16_t>(-4), 353, static_cast<uint16_t>(-4), 355, static_cast<uint16_t>(-4), 357, static_cast<uint16_t>(-4), 359, static_cast<uint16_t>(-4), 361, static_cast<uint16_t>(-4), 363, static_cast<uint16_t>(-4), 365, static_cast<uint16_t>(-4), 367, static_cast<uint16_t>(-4), 369, static_cast<uint16_t>(-4), 371, static_cast<uint16_t>(-4), 373, static_cast<uint16_t>(-4), 375, static_cast<uint16_t>(-4), 378, static_cast<uint16_t>(-4), 380, static_cast<uint16_t>(-4), 382, static_cast<uint16_t>(-4), 384, 780, 387, static_cast<uint16_t>(-4), 389, static_cast<uint16_t>(-4), 392, static_cast<uint16_t>(-4), 396, static_cast<uint16_t>(-4), 402, static_cast<uint16_t>(-4), 405, 388, 409, static_cast<uint16_t>(-4), 410, 652, 414, 520, 417, static_cast<uint16_t>(-4), 419, static_cast<uint16_t>(-4), 421, static_cast<uint16_t>(-4), 424, static_cast<uint16_t>(-4), 429, static_cast<uint16_t>(-4), 432, static_cast<uint16_t>(-4), 436, static_cast<uint16_t>(-4), 438, static_cast<uint16_t>(-4), 441, static_cast<uint16_t>(-4), 445, static_cast<uint16_t>(-4), 447, 224, 453, static_cast<uint16_t>(-4), 454, static_cast<uint16_t>(-8), 456, static_cast<uint16_t>(-4), 457, static_cast<uint16_t>(-8), 459, static_cast<uint16_t>(-4), 460, static_cast<uint16_t>(-8), 462, static_cast<uint16_t>(-4), 464, static_cast<uint16_t>(-4), 466, static_cast<uint16_t>(-4), 468, static_cast<uint16_t>(-4), 470, static_cast<uint16_t>(-4), 472, static_cast<uint16_t>(-4), 474, static_cast<uint16_t>(-4), 476, static_cast<uint16_t>(-4), 477, static_cast<uint16_t>(-316), 479, static_cast<uint16_t>(-4), 481, static_cast<uint16_t>(-4), 483, static_cast<uint16_t>(-4), 485, static_cast<uint16_t>(-4), 487, static_cast<uint16_t>(-4), 489, static_cast<uint16_t>(-4), 491, static_cast<uint16_t>(-4), 493, static_cast<uint16_t>(-4), 495, static_cast<uint16_t>(-4), 498, static_cast<uint16_t>(-4), 499, static_cast<uint16_t>(-8), 501, static_cast<uint16_t>(-4), 505, static_cast<uint16_t>(-4), 507, static_cast<uint16_t>(-4), 509, static_cast<uint16_t>(-4), 511, static_cast<uint16_t>(-4), 513, static_cast<uint16_t>(-4), 515, static_cast<uint16_t>(-4), 517, static_cast<uint16_t>(-4), 519, static_cast<uint16_t>(-4), 521, static_cast<uint16_t>(-4), 523, static_cast<uint16_t>(-4), 525, static_cast<uint16_t>(-4), 527, static_cast<uint16_t>(-4), 529, static_cast<uint16_t>(-4), 531, static_cast<uint16_t>(-4), 533, static_cast<uint16_t>(-4), 535, static_cast<uint16_t>(-4), 537, static_cast<uint16_t>(-4), 539, static_cast<uint16_t>(-4), 541, static_cast<uint16_t>(-4), 543, static_cast<uint16_t>(-4), 547, static_cast<uint16_t>(-4), 549, static_cast<uint16_t>(-4), 551, static_cast<uint16_t>(-4), 553, static_cast<uint16_t>(-4), 555, static_cast<uint16_t>(-4), 557, static_cast<uint16_t>(-4), 559, static_cast<uint16_t>(-4), 561, static_cast<uint16_t>(-4), 563, static_cast<uint16_t>(-4), 572, static_cast<uint16_t>(-4), 578, static_cast<uint16_t>(-4), 583, static_cast<uint16_t>(-4), 585, static_cast<uint16_t>(-4), 587, static_cast<uint16_t>(-4), 589, static_cast<uint16_t>(-4), 591, static_cast<uint16_t>(-4), 595, static_cast<uint16_t>(-840), 596, static_cast<uint16_t>(-824), 33366, static_cast<uint16_t>(-820), 599, static_cast<uint16_t>(-820), 601, static_cast<uint16_t>(-808), 603, static_cast<uint16_t>(-812), 608, static_cast<uint16_t>(-820), 611, static_cast<uint16_t>(-828), 616, static_cast<uint16_t>(-836), 617, static_cast<uint16_t>(-844), 619, 42972, 623, static_cast<uint16_t>(-844), 626, static_cast<uint16_t>(-852), 629, static_cast<uint16_t>(-856), 637, 42908, 640, static_cast<uint16_t>(-872), 643, static_cast<uint16_t>(-872), 648, static_cast<uint16_t>(-872), 649, static_cast<uint16_t>(-276), 33418, static_cast<uint16_t>(-868), 651, static_cast<uint16_t>(-868), 652, static_cast<uint16_t>(-284), 658, static_cast<uint16_t>(-876), 837, 336, 33659, 520, 893, 520, 940, static_cast<uint16_t>(-152), 33709, static_cast<uint16_t>(-148), 943, static_cast<uint16_t>(-148), 33713, static_cast<uint16_t>(-128), 961, static_cast<uint16_t>(-128), 962, static_cast<uint16_t>(-124), 33731, static_cast<uint16_t>(-128), 971, static_cast<uint16_t>(-128), 972, static_cast<uint16_t>(-256), 33741, static_cast<uint16_t>(-252), 974, static_cast<uint16_t>(-252), 976, static_cast<uint16_t>(-248), 977, static_cast<uint16_t>(-228), 981, static_cast<uint16_t>(-188), 982, static_cast<uint16_t>(-216), 985, static_cast<uint16_t>(-4), 987, static_cast<uint16_t>(-4), 989, static_cast<uint16_t>(-4), 991, static_cast<uint16_t>(-4), 993, static_cast<uint16_t>(-4), 995, static_cast<uint16_t>(-4), 997, static_cast<uint16_t>(-4), 999, static_cast<uint16_t>(-4), 1001, static_cast<uint16_t>(-4), 1003, static_cast<uint16_t>(-4), 1005, static_cast<uint16_t>(-4), 1007, static_cast<uint16_t>(-4), 1008, static_cast<uint16_t>(-344), 1009, static_cast<uint16_t>(-320), 1010, 28, 1013, static_cast<uint16_t>(-384), 1016, static_cast<uint16_t>(-4), 1019, static_cast<uint16_t>(-4), 33840, static_cast<uint16_t>(-128), 1103, static_cast<uint16_t>(-128), 33872, static_cast<uint16_t>(-320), 1119, static_cast<uint16_t>(-320), 1121, static_cast<uint16_t>(-4), 1123, static_cast<uint16_t>(-4), 1125, static_cast<uint16_t>(-4), 1127, static_cast<uint16_t>(-4), 1129, static_cast<uint16_t>(-4), 1131, static_cast<uint16_t>(-4), 1133, static_cast<uint16_t>(-4), 1135, static_cast<uint16_t>(-4), 1137, static_cast<uint16_t>(-4), 1139, static_cast<uint16_t>(-4), 1141, static_cast<uint16_t>(-4), 1143, static_cast<uint16_t>(-4), 1145, static_cast<uint16_t>(-4), 1147, static_cast<uint16_t>(-4), 1149, static_cast<uint16_t>(-4), 1151, static_cast<uint16_t>(-4), 1153, static_cast<uint16_t>(-4), 1163, static_cast<uint16_t>(-4), 1165, static_cast<uint16_t>(-4), 1167, static_cast<uint16_t>(-4), 1169, static_cast<uint16_t>(-4), 1171, static_cast<uint16_t>(-4), 1173, static_cast<uint16_t>(-4), 1175, static_cast<uint16_t>(-4), 1177, static_cast<uint16_t>(-4), 1179, static_cast<uint16_t>(-4), 1181, static_cast<uint16_t>(-4), 1183, static_cast<uint16_t>(-4), 1185, static_cast<uint16_t>(-4), 1187, static_cast<uint16_t>(-4), 1189, static_cast<uint16_t>(-4), 1191, static_cast<uint16_t>(-4), 1193, static_cast<uint16_t>(-4), 1195, static_cast<uint16_t>(-4), 1197, static_cast<uint16_t>(-4), 1199, static_cast<uint16_t>(-4), 1201, static_cast<uint16_t>(-4), 1203, static_cast<uint16_t>(-4), 1205, static_cast<uint16_t>(-4), 1207, static_cast<uint16_t>(-4), 1209, static_cast<uint16_t>(-4), 1211, static_cast<uint16_t>(-4), 1213, static_cast<uint16_t>(-4), 1215, static_cast<uint16_t>(-4), 1218, static_cast<uint16_t>(-4), 1220, static_cast<uint16_t>(-4), 1222, static_cast<uint16_t>(-4), 1224, static_cast<uint16_t>(-4), 1226, static_cast<uint16_t>(-4), 1228, static_cast<uint16_t>(-4), 1230, static_cast<uint16_t>(-4), 1231, static_cast<uint16_t>(-60), 1233, static_cast<uint16_t>(-4), 1235, static_cast<uint16_t>(-4), 1237, static_cast<uint16_t>(-4), 1239, static_cast<uint16_t>(-4), 1241, static_cast<uint16_t>(-4), 1243, static_cast<uint16_t>(-4), 1245, static_cast<uint16_t>(-4), 1247, static_cast<uint16_t>(-4), 1249, static_cast<uint16_t>(-4), 1251, static_cast<uint16_t>(-4), 1253, static_cast<uint16_t>(-4), 1255, static_cast<uint16_t>(-4), 1257, static_cast<uint16_t>(-4), 1259, static_cast<uint16_t>(-4), 1261, static_cast<uint16_t>(-4), 1263, static_cast<uint16_t>(-4), 1265, static_cast<uint16_t>(-4), 1267, static_cast<uint16_t>(-4), 1269, static_cast<uint16_t>(-4), 1271, static_cast<uint16_t>(-4), 1273, static_cast<uint16_t>(-4), 1275, static_cast<uint16_t>(-4), 1277, static_cast<uint16_t>(-4), 1279, static_cast<uint16_t>(-4), 1281, static_cast<uint16_t>(-4), 1283, static_cast<uint16_t>(-4), 1285, static_cast<uint16_t>(-4), 1287, static_cast<uint16_t>(-4), 1289, static_cast<uint16_t>(-4), 1291, static_cast<uint16_t>(-4), 1293, static_cast<uint16_t>(-4), 1295, static_cast<uint16_t>(-4), 1297, static_cast<uint16_t>(-4), 1299, static_cast<uint16_t>(-4), 34145, static_cast<uint16_t>(-192), 1414, static_cast<uint16_t>(-192), 7549, 15256, 7681, static_cast<uint16_t>(-4), 7683, static_cast<uint16_t>(-4), 7685, static_cast<uint16_t>(-4), 7687, static_cast<uint16_t>(-4), 7689, static_cast<uint16_t>(-4), 7691, static_cast<uint16_t>(-4), 7693, static_cast<uint16_t>(-4), 7695, static_cast<uint16_t>(-4), 7697, static_cast<uint16_t>(-4), 7699, static_cast<uint16_t>(-4), 7701, static_cast<uint16_t>(-4), 7703, static_cast<uint16_t>(-4), 7705, static_cast<uint16_t>(-4), 7707, static_cast<uint16_t>(-4), 7709, static_cast<uint16_t>(-4), 7711, static_cast<uint16_t>(-4), 7713, static_cast<uint16_t>(-4), 7715, static_cast<uint16_t>(-4), 7717, static_cast<uint16_t>(-4), 7719, static_cast<uint16_t>(-4), 7721, static_cast<uint16_t>(-4), 7723, static_cast<uint16_t>(-4), 7725, static_cast<uint16_t>(-4), 7727, static_cast<uint16_t>(-4), 7729, static_cast<uint16_t>(-4), 7731, static_cast<uint16_t>(-4), 7733, static_cast<uint16_t>(-4), 7735, static_cast<uint16_t>(-4), 7737, static_cast<uint16_t>(-4), 7739, static_cast<uint16_t>(-4), 7741, static_cast<uint16_t>(-4), 7743, static_cast<uint16_t>(-4), 7745, static_cast<uint16_t>(-4), 7747, static_cast<uint16_t>(-4), 7749, static_cast<uint16_t>(-4), 7751, static_cast<uint16_t>(-4), 7753, static_cast<uint16_t>(-4), 7755, static_cast<uint16_t>(-4), 7757, static_cast<uint16_t>(-4), 7759, static_cast<uint16_t>(-4), 7761, static_cast<uint16_t>(-4), 7763, static_cast<uint16_t>(-4), 7765, static_cast<uint16_t>(-4), 7767, static_cast<uint16_t>(-4), 7769, static_cast<uint16_t>(-4), 7771, static_cast<uint16_t>(-4), 7773, static_cast<uint16_t>(-4), 7775, static_cast<uint16_t>(-4), 7777, static_cast<uint16_t>(-4), 7779, static_cast<uint16_t>(-4), 7781, static_cast<uint16_t>(-4), 7783, static_cast<uint16_t>(-4), 7785, static_cast<uint16_t>(-4), 7787, static_cast<uint16_t>(-4), 7789, static_cast<uint16_t>(-4), 7791, static_cast<uint16_t>(-4), 7793, static_cast<uint16_t>(-4), 7795, static_cast<uint16_t>(-4), 7797, static_cast<uint16_t>(-4), 7799, static_cast<uint16_t>(-4), 7801, static_cast<uint16_t>(-4), 7803, static_cast<uint16_t>(-4), 7805, static_cast<uint16_t>(-4), 7807, static_cast<uint16_t>(-4), 7809, static_cast<uint16_t>(-4), 7811, static_cast<uint16_t>(-4), 7813, static_cast<uint16_t>(-4), 7815, static_cast<uint16_t>(-4), 7817, static_cast<uint16_t>(-4), 7819, static_cast<uint16_t>(-4), 7821, static_cast<uint16_t>(-4), 7823, static_cast<uint16_t>(-4), 7825, static_cast<uint16_t>(-4), 7827, static_cast<uint16_t>(-4), 7829, static_cast<uint16_t>(-4), 7835, static_cast<uint16_t>(-236), 7841, static_cast<uint16_t>(-4), 7843, static_cast<uint16_t>(-4), 7845, static_cast<uint16_t>(-4), 7847, static_cast<uint16_t>(-4), 7849, static_cast<uint16_t>(-4), 7851, static_cast<uint16_t>(-4), 7853, static_cast<uint16_t>(-4), 7855, static_cast<uint16_t>(-4), 7857, static_cast<uint16_t>(-4), 7859, static_cast<uint16_t>(-4), 7861, static_cast<uint16_t>(-4), 7863, static_cast<uint16_t>(-4), 7865, static_cast<uint16_t>(-4), 7867, static_cast<uint16_t>(-4), 7869, static_cast<uint16_t>(-4), 7871, static_cast<uint16_t>(-4), 7873, static_cast<uint16_t>(-4), 7875, static_cast<uint16_t>(-4), 7877, static_cast<uint16_t>(-4), 7879, static_cast<uint16_t>(-4), 7881, static_cast<uint16_t>(-4), 7883, static_cast<uint16_t>(-4), 7885, static_cast<uint16_t>(-4), 7887, static_cast<uint16_t>(-4), 7889, static_cast<uint16_t>(-4), 7891, static_cast<uint16_t>(-4), 7893, static_cast<uint16_t>(-4), 7895, static_cast<uint16_t>(-4), 7897, static_cast<uint16_t>(-4), 7899, static_cast<uint16_t>(-4), 7901, static_cast<uint16_t>(-4), 7903, static_cast<uint16_t>(-4), 7905, static_cast<uint16_t>(-4), 7907, static_cast<uint16_t>(-4), 7909, static_cast<uint16_t>(-4), 7911, static_cast<uint16_t>(-4), 7913, static_cast<uint16_t>(-4), 7915, static_cast<uint16_t>(-4), 7917, static_cast<uint16_t>(-4), 7919, static_cast<uint16_t>(-4), 7921, static_cast<uint16_t>(-4), 7923, static_cast<uint16_t>(-4), 7925, static_cast<uint16_t>(-4), 7927, static_cast<uint16_t>(-4), 7929, static_cast<uint16_t>(-4), 40704, 32, 7943, 32, 40720, 32, 7957, 32, 40736, 32, 7975, 32, 40752, 32, 7991, 32, 40768, 32, 8005, 32, 8017, 32, 8019, 32, 8021, 32, 8023, 32, 40800, 32, 8039, 32, 40816, 296, 8049, 296, 40818, 344, 8053, 344, 40822, 400, 8055, 400, 40824, 512, 8057, 512, 40826, 448, 8059, 448, 40828, 504, 8061, 504, 40880, 32, 8113, 32, 8126, static_cast<uint16_t>(-28820), 40912, 32, 8145, 32, 40928, 32, 8161, 32, 8165, 28, 8526, static_cast<uint16_t>(-112), 41328, static_cast<uint16_t>(-64), 8575, static_cast<uint16_t>(-64), 8580, static_cast<uint16_t>(-4), 42192, static_cast<uint16_t>(-104), 9449, static_cast<uint16_t>(-104), 44080, static_cast<uint16_t>(-192), 11358, static_cast<uint16_t>(-192), 11361, static_cast<uint16_t>(-4), 11365, static_cast<uint16_t>(-43180), 11366, static_cast<uint16_t>(-43168), 11368, static_cast<uint16_t>(-4), 11370, static_cast<uint16_t>(-4), 11372, static_cast<uint16_t>(-4), 11382, static_cast<uint16_t>(-4), 11393, static_cast<uint16_t>(-4), 11395, static_cast<uint16_t>(-4), 11397, static_cast<uint16_t>(-4), 11399, static_cast<uint16_t>(-4), 11401, static_cast<uint16_t>(-4), 11403, static_cast<uint16_t>(-4), 11405, static_cast<uint16_t>(-4), 11407, static_cast<uint16_t>(-4), 11409, static_cast<uint16_t>(-4), 11411, static_cast<uint16_t>(-4), 11413, static_cast<uint16_t>(-4), 11415, static_cast<uint16_t>(-4), 11417, static_cast<uint16_t>(-4), 11419, static_cast<uint16_t>(-4), 11421, static_cast<uint16_t>(-4), 11423, static_cast<uint16_t>(-4), 11425, static_cast<uint16_t>(-4), 11427, static_cast<uint16_t>(-4), 11429, static_cast<uint16_t>(-4), 11431, static_cast<uint16_t>(-4), 11433, static_cast<uint16_t>(-4), 11435, static_cast<uint16_t>(-4), 11437, static_cast<uint16_t>(-4), 11439, static_cast<uint16_t>(-4), 11441, static_cast<uint16_t>(-4), 11443, static_cast<uint16_t>(-4), 11445, static_cast<uint16_t>(-4), 11447, static_cast<uint16_t>(-4), 11449, static_cast<uint16_t>(-4), 11451, static_cast<uint16_t>(-4), 11453, static_cast<uint16_t>(-4), 11455, static_cast<uint16_t>(-4), 11457, static_cast<uint16_t>(-4), 11459, static_cast<uint16_t>(-4), 11461, static_cast<uint16_t>(-4), 11463, static_cast<uint16_t>(-4), 11465, static_cast<uint16_t>(-4), 11467, static_cast<uint16_t>(-4), 11469, static_cast<uint16_t>(-4), 11471, static_cast<uint16_t>(-4), 11473, static_cast<uint16_t>(-4), 11475, static_cast<uint16_t>(-4), 11477, static_cast<uint16_t>(-4), 11479, static_cast<uint16_t>(-4), 11481, static_cast<uint16_t>(-4), 11483, static_cast<uint16_t>(-4), 11485, static_cast<uint16_t>(-4), 11487, static_cast<uint16_t>(-4), 11489, static_cast<uint16_t>(-4), 11491, static_cast<uint16_t>(-4), 44288, static_cast<uint16_t>(-29056), 11557, static_cast<uint16_t>(-29056) }; // NOLINT
static const MultiCharacterSpecialCase<1> kEcma262CanonicalizeMultiStrings1[] = { {0, {0}} }; // NOLINT
static const uint16_t kEcma262CanonicalizeTable1Size = 2;
static const uint16_t kEcma262CanonicalizeTable1[4] = { 65345, static_cast<uint16_t>(-128), 32602, static_cast<uint16_t>(-128) }; // NOLINT
static const MultiCharacterSpecialCase<1> kEcma262CanonicalizeMultiStrings2[] = { {0, {0}} }; // NOLINT
static const uint16_t kEcma262CanonicalizeTable2Size = 2;
static const uint16_t kEcma262CanonicalizeTable2[4] = { 33832, static_cast<uint16_t>(-160), 1103, static_cast<uint16_t>(-160) }; // NOLINT
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
    case 2: return LookupMapping(kEcma262CanonicalizeTable2,
                                     kEcma262CanonicalizeTable2Size,
                                     kEcma262CanonicalizeMultiStrings2,
                                     c,
                                     n,
                                     result,
                                     allow_caching_ptr);
    default: return 0;
  }
}

static const MultiCharacterSpecialCase<4> kEcma262UnCanonicalizeMultiStrings0[] = { {2, {65, 97}}, {2, {66, 98}}, {2, {67, 99}}, {2, {68, 100}}, {2, {69, 101}}, {2, {70, 102}}, {2, {71, 103}}, {2, {72, 104}}, {2, {73, 105}}, {2, {74, 106}}, {2, {75, 107}}, {2, {76, 108}}, {2, {77, 109}}, {2, {78, 110}}, {2, {79, 111}}, {2, {80, 112}}, {2, {81, 113}}, {2, {82, 114}}, {2, {83, 115}}, {2, {84, 116}}, {2, {85, 117}}, {2, {86, 118}}, {2, {87, 119}}, {2, {88, 120}}, {2, {89, 121}}, {2, {90, 122}}, {2, {181, 924}}, {2, {192, 224}}, {2, {193, 225}}, {2, {194, 226}}, {2, {195, 227}}, {2, {196, 228}}, {2, {197, 229}}, {2, {198, 230}}, {2, {199, 231}}, {2, {200, 232}}, {2, {201, 233}}, {2, {202, 234}}, {2, {203, 235}}, {2, {204, 236}}, {2, {205, 237}}, {2, {206, 238}}, {2, {207, 239}}, {2, {208, 240}}, {2, {209, 241}}, {2, {210, 242}}, {2, {211, 243}}, {2, {212, 244}}, {2, {213, 245}}, {2, {214, 246}}, {2, {216, 248}}, {2, {217, 249}}, {2, {218, 250}}, {2, {219, 251}}, {2, {220, 252}}, {2, {221, 253}}, {2, {222, 254}}, {2, {255, 376}}, {2, {256, 257}}, {2, {258, 259}}, {2, {260, 261}}, {2, {262, 263}}, {2, {264, 265}}, {2, {266, 267}}, {2, {268, 269}}, {2, {270, 271}}, {2, {272, 273}}, {2, {274, 275}}, {2, {276, 277}}, {2, {278, 279}}, {2, {280, 281}}, {2, {282, 283}}, {2, {284, 285}}, {2, {286, 287}}, {2, {288, 289}}, {2, {290, 291}}, {2, {292, 293}}, {2, {294, 295}}, {2, {296, 297}}, {2, {298, 299}}, {2, {300, 301}}, {2, {302, 303}}, {2, {304, 304}}, {2, {306, 307}}, {2, {308, 309}}, {2, {310, 311}}, {2, {313, 314}}, {2, {315, 316}}, {2, {317, 318}}, {2, {319, 320}}, {2, {321, 322}}, {2, {323, 324}}, {2, {325, 326}}, {2, {327, 328}}, {2, {330, 331}}, {2, {332, 333}}, {2, {334, 335}}, {2, {336, 337}}, {2, {338, 339}}, {2, {340, 341}}, {2, {342, 343}}, {2, {344, 345}}, {2, {346, 347}}, {2, {348, 349}}, {2, {350, 351}}, {2, {352, 353}}, {2, {354, 355}}, {2, {356, 357}}, {2, {358, 359}}, {2, {360, 361}}, {2, {362, 363}}, {2, {364, 365}}, {2, {366, 367}}, {2, {368, 369}}, {2, {370, 371}}, {2, {372, 373}}, {2, {374, 375}}, {2, {377, 378}}, {2, {379, 380}}, {2, {381, 382}}, {2, {384, 579}}, {2, {386, 387}}, {2, {388, 389}}, {2, {391, 392}}, {2, {395, 396}}, {2, {401, 402}}, {2, {405, 502}}, {2, {408, 409}}, {2, {410, 573}}, {2, {414, 544}}, {2, {416, 417}}, {2, {418, 419}}, {2, {420, 421}}, {2, {423, 424}}, {2, {428, 429}}, {2, {431, 432}}, {2, {435, 436}}, {2, {437, 438}}, {2, {440, 441}}, {2, {444, 445}}, {2, {447, 503}}, {2, {452, 453}}, {2, {452, 454}}, {2, {455, 456}}, {2, {455, 457}}, {2, {458, 459}}, {2, {458, 460}}, {2, {461, 462}}, {2, {463, 464}}, {2, {465, 466}}, {2, {467, 468}}, {2, {469, 470}}, {2, {471, 472}}, {2, {473, 474}}, {2, {475, 476}}, {2, {398, 477}}, {2, {478, 479}}, {2, {480, 481}}, {2, {482, 483}}, {2, {484, 485}}, {2, {486, 487}}, {2, {488, 489}}, {2, {490, 491}}, {2, {492, 493}}, {2, {494, 495}}, {2, {497, 498}}, {2, {497, 499}}, {2, {500, 501}}, {2, {504, 505}}, {2, {506, 507}}, {2, {508, 509}}, {2, {510, 511}}, {2, {512, 513}}, {2, {514, 515}}, {2, {516, 517}}, {2, {518, 519}}, {2, {520, 521}}, {2, {522, 523}}, {2, {524, 525}}, {2, {526, 527}}, {2, {528, 529}}, {2, {530, 531}}, {2, {532, 533}}, {2, {534, 535}}, {2, {536, 537}}, {2, {538, 539}}, {2, {540, 541}}, {2, {542, 543}}, {2, {546, 547}}, {2, {548, 549}}, {2, {550, 551}}, {2, {552, 553}}, {2, {554, 555}}, {2, {556, 557}}, {2, {558, 559}}, {2, {560, 561}}, {2, {562, 563}}, {2, {571, 572}}, {2, {577, 578}}, {2, {582, 583}}, {2, {584, 585}}, {2, {586, 587}}, {2, {588, 589}}, {2, {590, 591}}, {2, {385, 595}}, {2, {390, 596}}, {2, {393, 598}}, {2, {394, 599}}, {2, {399, 601}}, {2, {400, 603}}, {2, {403, 608}}, {2, {404, 611}}, {2, {407, 616}}, {2, {406, 617}}, {2, {619, 11362}}, {2, {412, 623}}, {2, {413, 626}}, {2, {415, 629}}, {2, {637, 11364}}, {2, {422, 640}}, {2, {425, 643}}, {2, {430, 648}}, {2, {580, 649}}, {2, {433, 650}}, {2, {434, 651}}, {2, {581, 652}}, {2, {439, 658}}, {2, {837, 921}}, {2, {891, 1021}}, {2, {892, 1022}}, {2, {893, 1023}}, {2, {902, 940}}, {2, {904, 941}}, {2, {905, 942}}, {2, {906, 943}}, {2, {913, 945}}, {2, {914, 946}}, {2, {915, 947}}, {2, {916, 948}}, {2, {917, 949}}, {2, {918, 950}}, {2, {919, 951}}, {2, {920, 952}}, {2, {921, 953}}, {2, {922, 954}}, {2, {923, 955}}, {2, {924, 956}}, {2, {925, 957}}, {2, {926, 958}}, {2, {927, 959}}, {2, {928, 960}}, {2, {929, 961}}, {2, {931, 962}}, {2, {931, 963}}, {2, {932, 964}}, {2, {933, 965}}, {2, {934, 966}}, {2, {935, 967}}, {2, {936, 968}}, {2, {937, 969}}, {2, {938, 970}}, {2, {939, 971}}, {2, {908, 972}}, {2, {910, 973}}, {2, {911, 974}}, {2, {914, 976}}, {2, {920, 977}}, {2, {934, 981}}, {2, {928, 982}}, {2, {984, 985}}, {2, {986, 987}}, {2, {988, 989}}, {2, {990, 991}}, {2, {992, 993}}, {2, {994, 995}}, {2, {996, 997}}, {2, {998, 999}}, {2, {1000, 1001}}, {2, {1002, 1003}}, {2, {1004, 1005}}, {2, {1006, 1007}}, {2, {922, 1008}}, {2, {929, 1009}}, {2, {1010, 1017}}, {2, {917, 1013}}, {2, {1015, 1016}}, {2, {1018, 1019}}, {2, {1040, 1072}}, {2, {1041, 1073}}, {2, {1042, 1074}}, {2, {1043, 1075}}, {2, {1044, 1076}}, {2, {1045, 1077}}, {2, {1046, 1078}}, {2, {1047, 1079}}, {2, {1048, 1080}}, {2, {1049, 1081}}, {2, {1050, 1082}}, {2, {1051, 1083}}, {2, {1052, 1084}}, {2, {1053, 1085}}, {2, {1054, 1086}}, {2, {1055, 1087}}, {2, {1056, 1088}}, {2, {1057, 1089}}, {2, {1058, 1090}}, {2, {1059, 1091}}, {2, {1060, 1092}}, {2, {1061, 1093}}, {2, {1062, 1094}}, {2, {1063, 1095}}, {2, {1064, 1096}}, {2, {1065, 1097}}, {2, {1066, 1098}}, {2, {1067, 1099}}, {2, {1068, 1100}}, {2, {1069, 1101}}, {2, {1070, 1102}}, {2, {1071, 1103}}, {2, {1024, 1104}}, {2, {1025, 1105}}, {2, {1026, 1106}}, {2, {1027, 1107}}, {2, {1028, 1108}}, {2, {1029, 1109}}, {2, {1030, 1110}}, {2, {1031, 1111}}, {2, {1032, 1112}}, {2, {1033, 1113}}, {2, {1034, 1114}}, {2, {1035, 1115}}, {2, {1036, 1116}}, {2, {1037, 1117}}, {2, {1038, 1118}}, {2, {1039, 1119}}, {2, {1120, 1121}}, {2, {1122, 1123}}, {2, {1124, 1125}}, {2, {1126, 1127}}, {2, {1128, 1129}}, {2, {1130, 1131}}, {2, {1132, 1133}}, {2, {1134, 1135}}, {2, {1136, 1137}}, {2, {1138, 1139}}, {2, {1140, 1141}}, {2, {1142, 1143}}, {2, {1144, 1145}}, {2, {1146, 1147}}, {2, {1148, 1149}}, {2, {1150, 1151}}, {2, {1152, 1153}}, {2, {1162, 1163}}, {2, {1164, 1165}}, {2, {1166, 1167}}, {2, {1168, 1169}}, {2, {1170, 1171}}, {2, {1172, 1173}}, {2, {1174, 1175}}, {2, {1176, 1177}}, {2, {1178, 1179}}, {2, {1180, 1181}}, {2, {1182, 1183}}, {2, {1184, 1185}}, {2, {1186, 1187}}, {2, {1188, 1189}}, {2, {1190, 1191}}, {2, {1192, 1193}}, {2, {1194, 1195}}, {2, {1196, 1197}}, {2, {1198, 1199}}, {2, {1200, 1201}}, {2, {1202, 1203}}, {2, {1204, 1205}}, {2, {1206, 1207}}, {2, {1208, 1209}}, {2, {1210, 1211}}, {2, {1212, 1213}}, {2, {1214, 1215}}, {2, {1217, 1218}}, {2, {1219, 1220}}, {2, {1221, 1222}}, {2, {1223, 1224}}, {2, {1225, 1226}}, {2, {1227, 1228}}, {2, {1229, 1230}}, {2, {1216, 1231}}, {2, {1232, 1233}}, {2, {1234, 1235}}, {2, {1236, 1237}}, {2, {1238, 1239}}, {2, {1240, 1241}}, {2, {1242, 1243}}, {2, {1244, 1245}}, {2, {1246, 1247}}, {2, {1248, 1249}}, {2, {1250, 1251}}, {2, {1252, 1253}}, {2, {1254, 1255}}, {2, {1256, 1257}}, {2, {1258, 1259}}, {2, {1260, 1261}}, {2, {1262, 1263}}, {2, {1264, 1265}}, {2, {1266, 1267}}, {2, {1268, 1269}}, {2, {1270, 1271}}, {2, {1272, 1273}}, {2, {1274, 1275}}, {2, {1276, 1277}}, {2, {1278, 1279}}, {2, {1280, 1281}}, {2, {1282, 1283}}, {2, {1284, 1285}}, {2, {1286, 1287}}, {2, {1288, 1289}}, {2, {1290, 1291}}, {2, {1292, 1293}}, {2, {1294, 1295}}, {2, {1296, 1297}}, {2, {1298, 1299}}, {2, {1329, 1377}}, {2, {1330, 1378}}, {2, {1331, 1379}}, {2, {1332, 1380}}, {2, {1333, 1381}}, {2, {1334, 1382}}, {2, {1335, 1383}}, {2, {1336, 1384}}, {2, {1337, 1385}}, {2, {1338, 1386}}, {2, {1339, 1387}}, {2, {1340, 1388}}, {2, {1341, 1389}}, {2, {1342, 1390}}, {2, {1343, 1391}}, {2, {1344, 1392}}, {2, {1345, 1393}}, {2, {1346, 1394}}, {2, {1347, 1395}}, {2, {1348, 1396}}, {2, {1349, 1397}}, {2, {1350, 1398}}, {2, {1351, 1399}}, {2, {1352, 1400}}, {2, {1353, 1401}}, {2, {1354, 1402}}, {2, {1355, 1403}}, {2, {1356, 1404}}, {2, {1357, 1405}}, {2, {1358, 1406}}, {2, {1359, 1407}}, {2, {1360, 1408}}, {2, {1361, 1409}}, {2, {1362, 1410}}, {2, {1363, 1411}}, {2, {1364, 1412}}, {2, {1365, 1413}}, {2, {1366, 1414}}, {2, {7549, 11363}}, {2, {7680, 7681}}, {2, {7682, 7683}}, {2, {7684, 7685}}, {2, {7686, 7687}}, {2, {7688, 7689}}, {2, {7690, 7691}}, {2, {7692, 7693}}, {2, {7694, 7695}}, {2, {7696, 7697}}, {2, {7698, 7699}}, {2, {7700, 7701}}, {2, {7702, 7703}}, {2, {7704, 7705}}, {2, {7706, 7707}}, {2, {7708, 7709}}, {2, {7710, 7711}}, {2, {7712, 7713}}, {2, {7714, 7715}}, {2, {7716, 7717}}, {2, {7718, 7719}}, {2, {7720, 7721}}, {2, {7722, 7723}}, {2, {7724, 7725}}, {2, {7726, 7727}}, {2, {7728, 7729}}, {2, {7730, 7731}}, {2, {7732, 7733}}, {2, {7734, 7735}}, {2, {7736, 7737}}, {2, {7738, 7739}}, {2, {7740, 7741}}, {2, {7742, 7743}}, {2, {7744, 7745}}, {2, {7746, 7747}}, {2, {7748, 7749}}, {2, {7750, 7751}}, {2, {7752, 7753}}, {2, {7754, 7755}}, {2, {7756, 7757}}, {2, {7758, 7759}}, {2, {7760, 7761}}, {2, {7762, 7763}}, {2, {7764, 7765}}, {2, {7766, 7767}}, {2, {7768, 7769}}, {2, {7770, 7771}}, {2, {7772, 7773}}, {2, {7774, 7775}}, {2, {7776, 7777}}, {2, {7778, 7779}}, {2, {7780, 7781}}, {2, {7782, 7783}}, {2, {7784, 7785}}, {2, {7786, 7787}}, {2, {7788, 7789}}, {2, {7790, 7791}}, {2, {7792, 7793}}, {2, {7794, 7795}}, {2, {7796, 7797}}, {2, {7798, 7799}}, {2, {7800, 7801}}, {2, {7802, 7803}}, {2, {7804, 7805}}, {2, {7806, 7807}}, {2, {7808, 7809}}, {2, {7810, 7811}}, {2, {7812, 7813}}, {2, {7814, 7815}}, {2, {7816, 7817}}, {2, {7818, 7819}}, {2, {7820, 7821}}, {2, {7822, 7823}}, {2, {7824, 7825}}, {2, {7826, 7827}}, {2, {7828, 7829}}, {2, {7776, 7835}}, {2, {7840, 7841}}, {2, {7842, 7843}}, {2, {7844, 7845}}, {2, {7846, 7847}}, {2, {7848, 7849}}, {2, {7850, 7851}}, {2, {7852, 7853}}, {2, {7854, 7855}}, {2, {7856, 7857}}, {2, {7858, 7859}}, {2, {7860, 7861}}, {2, {7862, 7863}}, {2, {7864, 7865}}, {2, {7866, 7867}}, {2, {7868, 7869}}, {2, {7870, 7871}}, {2, {7872, 7873}}, {2, {7874, 7875}}, {2, {7876, 7877}}, {2, {7878, 7879}}, {2, {7880, 7881}}, {2, {7882, 7883}}, {2, {7884, 7885}}, {2, {7886, 7887}}, {2, {7888, 7889}}, {2, {7890, 7891}}, {2, {7892, 7893}}, {2, {7894, 7895}}, {2, {7896, 7897}}, {2, {7898, 7899}}, {2, {7900, 7901}}, {2, {7902, 7903}}, {2, {7904, 7905}}, {2, {7906, 7907}}, {2, {7908, 7909}}, {2, {7910, 7911}}, {2, {7912, 7913}}, {2, {7914, 7915}}, {2, {7916, 7917}}, {2, {7918, 7919}}, {2, {7920, 7921}}, {2, {7922, 7923}}, {2, {7924, 7925}}, {2, {7926, 7927}}, {2, {7928, 7929}}, {2, {7936, 7944}}, {2, {7937, 7945}}, {2, {7938, 7946}}, {2, {7939, 7947}}, {2, {7940, 7948}}, {2, {7941, 7949}}, {2, {7942, 7950}}, {2, {7943, 7951}}, {2, {7952, 7960}}, {2, {7953, 7961}}, {2, {7954, 7962}}, {2, {7955, 7963}}, {2, {7956, 7964}}, {2, {7957, 7965}}, {2, {7968, 7976}}, {2, {7969, 7977}}, {2, {7970, 7978}}, {2, {7971, 7979}}, {2, {7972, 7980}}, {2, {7973, 7981}}, {2, {7974, 7982}}, {2, {7975, 7983}}, {2, {7984, 7992}}, {2, {7985, 7993}}, {2, {7986, 7994}}, {2, {7987, 7995}}, {2, {7988, 7996}}, {2, {7989, 7997}}, {2, {7990, 7998}}, {2, {7991, 7999}}, {2, {8000, 8008}}, {2, {8001, 8009}}, {2, {8002, 8010}}, {2, {8003, 8011}}, {2, {8004, 8012}}, {2, {8005, 8013}}, {2, {8017, 8025}}, {2, {8019, 8027}}, {2, {8021, 8029}}, {2, {8023, 8031}}, {2, {8032, 8040}}, {2, {8033, 8041}}, {2, {8034, 8042}}, {2, {8035, 8043}}, {2, {8036, 8044}}, {2, {8037, 8045}}, {2, {8038, 8046}}, {2, {8039, 8047}}, {2, {8048, 8122}}, {2, {8049, 8123}}, {2, {8050, 8136}}, {2, {8051, 8137}}, {2, {8052, 8138}}, {2, {8053, 8139}}, {2, {8054, 8154}}, {2, {8055, 8155}}, {2, {8056, 8184}}, {2, {8057, 8185}}, {2, {8058, 8170}}, {2, {8059, 8171}}, {2, {8060, 8186}}, {2, {8061, 8187}}, {2, {8112, 8120}}, {2, {8113, 8121}}, {2, {921, 8126}}, {2, {8144, 8152}}, {2, {8145, 8153}}, {2, {8160, 8168}}, {2, {8161, 8169}}, {2, {8165, 8172}}, {2, {8498, 8526}}, {2, {8544, 8560}}, {2, {8545, 8561}}, {2, {8546, 8562}}, {2, {8547, 8563}}, {2, {8548, 8564}}, {2, {8549, 8565}}, {2, {8550, 8566}}, {2, {8551, 8567}}, {2, {8552, 8568}}, {2, {8553, 8569}}, {2, {8554, 8570}}, {2, {8555, 8571}}, {2, {8556, 8572}}, {2, {8557, 8573}}, {2, {8558, 8574}}, {2, {8559, 8575}}, {2, {8579, 8580}}, {2, {9398, 9424}}, {2, {9399, 9425}}, {2, {9400, 9426}}, {2, {9401, 9427}}, {2, {9402, 9428}}, {2, {9403, 9429}}, {2, {9404, 9430}}, {2, {9405, 9431}}, {2, {9406, 9432}}, {2, {9407, 9433}}, {2, {9408, 9434}}, {2, {9409, 9435}}, {2, {9410, 9436}}, {2, {9411, 9437}}, {2, {9412, 9438}}, {2, {9413, 9439}}, {2, {9414, 9440}}, {2, {9415, 9441}}, {2, {9416, 9442}}, {2, {9417, 9443}}, {2, {9418, 9444}}, {2, {9419, 9445}}, {2, {9420, 9446}}, {2, {9421, 9447}}, {2, {9422, 9448}}, {2, {9423, 9449}}, {2, {11264, 11312}}, {2, {11265, 11313}}, {2, {11266, 11314}}, {2, {11267, 11315}}, {2, {11268, 11316}}, {2, {11269, 11317}}, {2, {11270, 11318}}, {2, {11271, 11319}}, {2, {11272, 11320}}, {2, {11273, 11321}}, {2, {11274, 11322}}, {2, {11275, 11323}}, {2, {11276, 11324}}, {2, {11277, 11325}}, {2, {11278, 11326}}, {2, {11279, 11327}}, {2, {11280, 11328}}, {2, {11281, 11329}}, {2, {11282, 11330}}, {2, {11283, 11331}}, {2, {11284, 11332}}, {2, {11285, 11333}}, {2, {11286, 11334}}, {2, {11287, 11335}}, {2, {11288, 11336}}, {2, {11289, 11337}}, {2, {11290, 11338}}, {2, {11291, 11339}}, {2, {11292, 11340}}, {2, {11293, 11341}}, {2, {11294, 11342}}, {2, {11295, 11343}}, {2, {11296, 11344}}, {2, {11297, 11345}}, {2, {11298, 11346}}, {2, {11299, 11347}}, {2, {11300, 11348}}, {2, {11301, 11349}}, {2, {11302, 11350}}, {2, {11303, 11351}}, {2, {11304, 11352}}, {2, {11305, 11353}}, {2, {11306, 11354}}, {2, {11307, 11355}}, {2, {11308, 11356}}, {2, {11309, 11357}}, {2, {11310, 11358}}, {2, {11360, 11361}}, {2, {570, 11365}}, {2, {574, 11366}}, {2, {11367, 11368}}, {2, {11369, 11370}}, {2, {11371, 11372}}, {2, {11381, 11382}}, {2, {11392, 11393}}, {2, {11394, 11395}}, {2, {11396, 11397}}, {2, {11398, 11399}}, {2, {11400, 11401}}, {2, {11402, 11403}}, {2, {11404, 11405}}, {2, {11406, 11407}}, {2, {11408, 11409}}, {2, {11410, 11411}}, {2, {11412, 11413}}, {2, {11414, 11415}}, {2, {11416, 11417}}, {2, {11418, 11419}}, {2, {11420, 11421}}, {2, {11422, 11423}}, {2, {11424, 11425}}, {2, {11426, 11427}}, {2, {11428, 11429}}, {2, {11430, 11431}}, {2, {11432, 11433}}, {2, {11434, 11435}}, {2, {11436, 11437}}, {2, {11438, 11439}}, {2, {11440, 11441}}, {2, {11442, 11443}}, {2, {11444, 11445}}, {2, {11446, 11447}}, {2, {11448, 11449}}, {2, {11450, 11451}}, {2, {11452, 11453}}, {2, {11454, 11455}}, {2, {11456, 11457}}, {2, {11458, 11459}}, {2, {11460, 11461}}, {2, {11462, 11463}}, {2, {11464, 11465}}, {2, {11466, 11467}}, {2, {11468, 11469}}, {2, {11470, 11471}}, {2, {11472, 11473}}, {2, {11474, 11475}}, {2, {11476, 11477}}, {2, {11478, 11479}}, {2, {11480, 11481}}, {2, {11482, 11483}}, {2, {11484, 11485}}, {2, {11486, 11487}}, {2, {11488, 11489}}, {2, {11490, 11491}}, {2, {4256, 11520}}, {2, {4257, 11521}}, {2, {4258, 11522}}, {2, {4259, 11523}}, {2, {4260, 11524}}, {2, {4261, 11525}}, {2, {4262, 11526}}, {2, {4263, 11527}}, {2, {4264, 11528}}, {2, {4265, 11529}}, {2, {4266, 11530}}, {2, {4267, 11531}}, {2, {4268, 11532}}, {2, {4269, 11533}}, {2, {4270, 11534}}, {2, {4271, 11535}}, {2, {4272, 11536}}, {2, {4273, 11537}}, {2, {4274, 11538}}, {2, {4275, 11539}}, {2, {4276, 11540}}, {2, {4277, 11541}}, {2, {4278, 11542}}, {2, {4279, 11543}}, {2, {4280, 11544}}, {2, {4281, 11545}}, {2, {4282, 11546}}, {2, {4283, 11547}}, {2, {4284, 11548}}, {2, {4285, 11549}}, {2, {4286, 11550}}, {2, {4287, 11551}}, {2, {4288, 11552}}, {2, {4289, 11553}}, {2, {4290, 11554}}, {2, {4291, 11555}}, {2, {4292, 11556}}, {2, {4293, 11557}}, {0, {0}} }; // NOLINT
static const uint16_t kEcma262UnCanonicalizeTable0Size = 837;
static const uint16_t kEcma262UnCanonicalizeTable0[1674] = { 97, 1, 98, 5, 99, 9, 100, 13, 101, 17, 102, 21, 103, 25, 104, 29, 105, 33, 106, 37, 107, 41, 108, 45, 109, 49, 110, 53, 111, 57, 112, 61, 113, 65, 114, 69, 115, 73, 116, 77, 117, 81, 118, 85, 119, 89, 120, 93, 121, 97, 122, 101, 181, 105, 224, 109, 225, 113, 226, 117, 227, 121, 228, 125, 229, 129, 230, 133, 231, 137, 232, 141, 233, 145, 234, 149, 235, 153, 236, 157, 237, 161, 238, 165, 239, 169, 240, 173, 241, 177, 242, 181, 243, 185, 244, 189, 245, 193, 246, 197, 248, 201, 249, 205, 250, 209, 251, 213, 252, 217, 253, 221, 254, 225, 255, 229, 257, 233, 259, 237, 261, 241, 263, 245, 265, 249, 267, 253, 269, 257, 271, 261, 273, 265, 275, 269, 277, 273, 279, 277, 281, 281, 283, 285, 285, 289, 287, 293, 289, 297, 291, 301, 293, 305, 295, 309, 297, 313, 299, 317, 301, 321, 303, 325, 304, 329, 307, 333, 309, 337, 311, 341, 314, 345, 316, 349, 318, 353, 320, 357, 322, 361, 324, 365, 326, 369, 328, 373, 331, 377, 333, 381, 335, 385, 337, 389, 339, 393, 341, 397, 343, 401, 345, 405, 347, 409, 349, 413, 351, 417, 353, 421, 355, 425, 357, 429, 359, 433, 361, 437, 363, 441, 365, 445, 367, 449, 369, 453, 371, 457, 373, 461, 375, 465, 378, 469, 380, 473, 382, 477, 384, 481, 387, 485, 389, 489, 392, 493, 396, 497, 402, 501, 405, 505, 409, 509, 410, 513, 414, 517, 417, 521, 419, 525, 421, 529, 424, 533, 429, 537, 432, 541, 436, 545, 438, 549, 441, 553, 445, 557, 447, 561, 453, 565, 454, 569, 456, 573, 457, 577, 459, 581, 460, 585, 462, 589, 464, 593, 466, 597, 468, 601, 470, 605, 472, 609, 474, 613, 476, 617, 477, 621, 479, 625, 481, 629, 483, 633, 485, 637, 487, 641, 489, 645, 491, 649, 493, 653, 495, 657, 498, 661, 499, 665, 501, 669, 505, 673, 507, 677, 509, 681, 511, 685, 513, 689, 515, 693, 517, 697, 519, 701, 521, 705, 523, 709, 525, 713, 527, 717, 529, 721, 531, 725, 533, 729, 535, 733, 537, 737, 539, 741, 541, 745, 543, 749, 547, 753, 549, 757, 551, 761, 553, 765, 555, 769, 557, 773, 559, 777, 561, 781, 563, 785, 572, 789, 578, 793, 583, 797, 585, 801, 587, 805, 589, 809, 591, 813, 595, 817, 596, 821, 598, 825, 599, 829, 601, 833, 603, 837, 608, 841, 611, 845, 616, 849, 617, 853, 619, 857, 623, 861, 626, 865, 629, 869, 637, 873, 640, 877, 643, 881, 648, 885, 649, 889, 650, 893, 651, 897, 652, 901, 658, 905, 837, 909, 891, 913, 892, 917, 893, 921, 940, 925, 941, 929, 942, 933, 943, 937, 945, 941, 946, 945, 947, 949, 948, 953, 949, 957, 950, 961, 951, 965, 952, 969, 953, 973, 954, 977, 955, 981, 956, 985, 957, 989, 958, 993, 959, 997, 960, 1001, 961, 1005, 962, 1009, 963, 1013, 964, 1017, 965, 1021, 966, 1025, 967, 1029, 968, 1033, 969, 1037, 970, 1041, 971, 1045, 972, 1049, 973, 1053, 974, 1057, 976, 1061, 977, 1065, 981, 1069, 982, 1073, 985, 1077, 987, 1081, 989, 1085, 991, 1089, 993, 1093, 995, 1097, 997, 1101, 999, 1105, 1001, 1109, 1003, 1113, 1005, 1117, 1007, 1121, 1008, 1125, 1009, 1129, 1010, 1133, 1013, 1137, 1016, 1141, 1019, 1145, 1072, 1149, 1073, 1153, 1074, 1157, 1075, 1161, 1076, 1165, 1077, 1169, 1078, 1173, 1079, 1177, 1080, 1181, 1081, 1185, 1082, 1189, 1083, 1193, 1084, 1197, 1085, 1201, 1086, 1205, 1087, 1209, 1088, 1213, 1089, 1217, 1090, 1221, 1091, 1225, 1092, 1229, 1093, 1233, 1094, 1237, 1095, 1241, 1096, 1245, 1097, 1249, 1098, 1253, 1099, 1257, 1100, 1261, 1101, 1265, 1102, 1269, 1103, 1273, 1104, 1277, 1105, 1281, 1106, 1285, 1107, 1289, 1108, 1293, 1109, 1297, 1110, 1301, 1111, 1305, 1112, 1309, 1113, 1313, 1114, 1317, 1115, 1321, 1116, 1325, 1117, 1329, 1118, 1333, 1119, 1337, 1121, 1341, 1123, 1345, 1125, 1349, 1127, 1353, 1129, 1357, 1131, 1361, 1133, 1365, 1135, 1369, 1137, 1373, 1139, 1377, 1141, 1381, 1143, 1385, 1145, 1389, 1147, 1393, 1149, 1397, 1151, 1401, 1153, 1405, 1163, 1409, 1165, 1413, 1167, 1417, 1169, 1421, 1171, 1425, 1173, 1429, 1175, 1433, 1177, 1437, 1179, 1441, 1181, 1445, 1183, 1449, 1185, 1453, 1187, 1457, 1189, 1461, 1191, 1465, 1193, 1469, 1195, 1473, 1197, 1477, 1199, 1481, 1201, 1485, 1203, 1489, 1205, 1493, 1207, 1497, 1209, 1501, 1211, 1505, 1213, 1509, 1215, 1513, 1218, 1517, 1220, 1521, 1222, 1525, 1224, 1529, 1226, 1533, 1228, 1537, 1230, 1541, 1231, 1545, 1233, 1549, 1235, 1553, 1237, 1557, 1239, 1561, 1241, 1565, 1243, 1569, 1245, 1573, 1247, 1577, 1249, 1581, 1251, 1585, 1253, 1589, 1255, 1593, 1257, 1597, 1259, 1601, 1261, 1605, 1263, 1609, 1265, 1613, 1267, 1617, 1269, 1621, 1271, 1625, 1273, 1629, 1275, 1633, 1277, 1637, 1279, 1641, 1281, 1645, 1283, 1649, 1285, 1653, 1287, 1657, 1289, 1661, 1291, 1665, 1293, 1669, 1295, 1673, 1297, 1677, 1299, 1681, 1377, 1685, 1378, 1689, 1379, 1693, 1380, 1697, 1381, 1701, 1382, 1705, 1383, 1709, 1384, 1713, 1385, 1717, 1386, 1721, 1387, 1725, 1388, 1729, 1389, 1733, 1390, 1737, 1391, 1741, 1392, 1745, 1393, 1749, 1394, 1753, 1395, 1757, 1396, 1761, 1397, 1765, 1398, 1769, 1399, 1773, 1400, 1777, 1401, 1781, 1402, 1785, 1403, 1789, 1404, 1793, 1405, 1797, 1406, 1801, 1407, 1805, 1408, 1809, 1409, 1813, 1410, 1817, 1411, 1821, 1412, 1825, 1413, 1829, 1414, 1833, 7549, 1837, 7681, 1841, 7683, 1845, 7685, 1849, 7687, 1853, 7689, 1857, 7691, 1861, 7693, 1865, 7695, 1869, 7697, 1873, 7699, 1877, 7701, 1881, 7703, 1885, 7705, 1889, 7707, 1893, 7709, 1897, 7711, 1901, 7713, 1905, 7715, 1909, 7717, 1913, 7719, 1917, 7721, 1921, 7723, 1925, 7725, 1929, 7727, 1933, 7729, 1937, 7731, 1941, 7733, 1945, 7735, 1949, 7737, 1953, 7739, 1957, 7741, 1961, 7743, 1965, 7745, 1969, 7747, 1973, 7749, 1977, 7751, 1981, 7753, 1985, 7755, 1989, 7757, 1993, 7759, 1997, 7761, 2001, 7763, 2005, 7765, 2009, 7767, 2013, 7769, 2017, 7771, 2021, 7773, 2025, 7775, 2029, 7777, 2033, 7779, 2037, 7781, 2041, 7783, 2045, 7785, 2049, 7787, 2053, 7789, 2057, 7791, 2061, 7793, 2065, 7795, 2069, 7797, 2073, 7799, 2077, 7801, 2081, 7803, 2085, 7805, 2089, 7807, 2093, 7809, 2097, 7811, 2101, 7813, 2105, 7815, 2109, 7817, 2113, 7819, 2117, 7821, 2121, 7823, 2125, 7825, 2129, 7827, 2133, 7829, 2137, 7835, 2141, 7841, 2145, 7843, 2149, 7845, 2153, 7847, 2157, 7849, 2161, 7851, 2165, 7853, 2169, 7855, 2173, 7857, 2177, 7859, 2181, 7861, 2185, 7863, 2189, 7865, 2193, 7867, 2197, 7869, 2201, 7871, 2205, 7873, 2209, 7875, 2213, 7877, 2217, 7879, 2221, 7881, 2225, 7883, 2229, 7885, 2233, 7887, 2237, 7889, 2241, 7891, 2245, 7893, 2249, 7895, 2253, 7897, 2257, 7899, 2261, 7901, 2265, 7903, 2269, 7905, 2273, 7907, 2277, 7909, 2281, 7911, 2285, 7913, 2289, 7915, 2293, 7917, 2297, 7919, 2301, 7921, 2305, 7923, 2309, 7925, 2313, 7927, 2317, 7929, 2321, 7936, 2325, 7937, 2329, 7938, 2333, 7939, 2337, 7940, 2341, 7941, 2345, 7942, 2349, 7943, 2353, 7952, 2357, 7953, 2361, 7954, 2365, 7955, 2369, 7956, 2373, 7957, 2377, 7968, 2381, 7969, 2385, 7970, 2389, 7971, 2393, 7972, 2397, 7973, 2401, 7974, 2405, 7975, 2409, 7984, 2413, 7985, 2417, 7986, 2421, 7987, 2425, 7988, 2429, 7989, 2433, 7990, 2437, 7991, 2441, 8000, 2445, 8001, 2449, 8002, 2453, 8003, 2457, 8004, 2461, 8005, 2465, 8017, 2469, 8019, 2473, 8021, 2477, 8023, 2481, 8032, 2485, 8033, 2489, 8034, 2493, 8035, 2497, 8036, 2501, 8037, 2505, 8038, 2509, 8039, 2513, 8048, 2517, 8049, 2521, 8050, 2525, 8051, 2529, 8052, 2533, 8053, 2537, 8054, 2541, 8055, 2545, 8056, 2549, 8057, 2553, 8058, 2557, 8059, 2561, 8060, 2565, 8061, 2569, 8112, 2573, 8113, 2577, 8126, 2581, 8144, 2585, 8145, 2589, 8160, 2593, 8161, 2597, 8165, 2601, 8526, 2605, 8560, 2609, 8561, 2613, 8562, 2617, 8563, 2621, 8564, 2625, 8565, 2629, 8566, 2633, 8567, 2637, 8568, 2641, 8569, 2645, 8570, 2649, 8571, 2653, 8572, 2657, 8573, 2661, 8574, 2665, 8575, 2669, 8580, 2673, 9424, 2677, 9425, 2681, 9426, 2685, 9427, 2689, 9428, 2693, 9429, 2697, 9430, 2701, 9431, 2705, 9432, 2709, 9433, 2713, 9434, 2717, 9435, 2721, 9436, 2725, 9437, 2729, 9438, 2733, 9439, 2737, 9440, 2741, 9441, 2745, 9442, 2749, 9443, 2753, 9444, 2757, 9445, 2761, 9446, 2765, 9447, 2769, 9448, 2773, 9449, 2777, 11312, 2781, 11313, 2785, 11314, 2789, 11315, 2793, 11316, 2797, 11317, 2801, 11318, 2805, 11319, 2809, 11320, 2813, 11321, 2817, 11322, 2821, 11323, 2825, 11324, 2829, 11325, 2833, 11326, 2837, 11327, 2841, 11328, 2845, 11329, 2849, 11330, 2853, 11331, 2857, 11332, 2861, 11333, 2865, 11334, 2869, 11335, 2873, 11336, 2877, 11337, 2881, 11338, 2885, 11339, 2889, 11340, 2893, 11341, 2897, 11342, 2901, 11343, 2905, 11344, 2909, 11345, 2913, 11346, 2917, 11347, 2921, 11348, 2925, 11349, 2929, 11350, 2933, 11351, 2937, 11352, 2941, 11353, 2945, 11354, 2949, 11355, 2953, 11356, 2957, 11357, 2961, 11358, 2965, 11361, 2969, 11365, 2973, 11366, 2977, 11368, 2981, 11370, 2985, 11372, 2989, 11382, 2993, 11393, 2997, 11395, 3001, 11397, 3005, 11399, 3009, 11401, 3013, 11403, 3017, 11405, 3021, 11407, 3025, 11409, 3029, 11411, 3033, 11413, 3037, 11415, 3041, 11417, 3045, 11419, 3049, 11421, 3053, 11423, 3057, 11425, 3061, 11427, 3065, 11429, 3069, 11431, 3073, 11433, 3077, 11435, 3081, 11437, 3085, 11439, 3089, 11441, 3093, 11443, 3097, 11445, 3101, 11447, 3105, 11449, 3109, 11451, 3113, 11453, 3117, 11455, 3121, 11457, 3125, 11459, 3129, 11461, 3133, 11463, 3137, 11465, 3141, 11467, 3145, 11469, 3149, 11471, 3153, 11473, 3157, 11475, 3161, 11477, 3165, 11479, 3169, 11481, 3173, 11483, 3177, 11485, 3181, 11487, 3185, 11489, 3189, 11491, 3193, 11520, 3197, 11521, 3201, 11522, 3205, 11523, 3209, 11524, 3213, 11525, 3217, 11526, 3221, 11527, 3225, 11528, 3229, 11529, 3233, 11530, 3237, 11531, 3241, 11532, 3245, 11533, 3249, 11534, 3253, 11535, 3257, 11536, 3261, 11537, 3265, 11538, 3269, 11539, 3273, 11540, 3277, 11541, 3281, 11542, 3285, 11543, 3289, 11544, 3293, 11545, 3297, 11546, 3301, 11547, 3305, 11548, 3309, 11549, 3313, 11550, 3317, 11551, 3321, 11552, 3325, 11553, 3329, 11554, 3333, 11555, 3337, 11556, 3341, 11557, 3345 }; // NOLINT
static const MultiCharacterSpecialCase<4> kEcma262UnCanonicalizeMultiStrings1[] = { {2, {65313, 65345}}, {2, {65314, 65346}}, {2, {65315, 65347}}, {2, {65316, 65348}}, {2, {65317, 65349}}, {2, {65318, 65350}}, {2, {65319, 65351}}, {2, {65320, 65352}}, {2, {65321, 65353}}, {2, {65322, 65354}}, {2, {65323, 65355}}, {2, {65324, 65356}}, {2, {65325, 65357}}, {2, {65326, 65358}}, {2, {65327, 65359}}, {2, {65328, 65360}}, {2, {65329, 65361}}, {2, {65330, 65362}}, {2, {65331, 65363}}, {2, {65332, 65364}}, {2, {65333, 65365}}, {2, {65334, 65366}}, {2, {65335, 65367}}, {2, {65336, 65368}}, {2, {65337, 65369}}, {2, {65338, 65370}}, {0, {0}} }; // NOLINT
static const uint16_t kEcma262UnCanonicalizeTable1Size = 26;
static const uint16_t kEcma262UnCanonicalizeTable1[52] = { 32577, 1, 32578, 5, 32579, 9, 32580, 13, 32581, 17, 32582, 21, 32583, 25, 32584, 29, 32585, 33, 32586, 37, 32587, 41, 32588, 45, 32589, 49, 32590, 53, 32591, 57, 32592, 61, 32593, 65, 32594, 69, 32595, 73, 32596, 77, 32597, 81, 32598, 85, 32599, 89, 32600, 93, 32601, 97, 32602, 101 }; // NOLINT
static const MultiCharacterSpecialCase<4> kEcma262UnCanonicalizeMultiStrings2[] = { {2, {66560, 66600}}, {2, {66561, 66601}}, {2, {66562, 66602}}, {2, {66563, 66603}}, {2, {66564, 66604}}, {2, {66565, 66605}}, {2, {66566, 66606}}, {2, {66567, 66607}}, {2, {66568, 66608}}, {2, {66569, 66609}}, {2, {66570, 66610}}, {2, {66571, 66611}}, {2, {66572, 66612}}, {2, {66573, 66613}}, {2, {66574, 66614}}, {2, {66575, 66615}}, {2, {66576, 66616}}, {2, {66577, 66617}}, {2, {66578, 66618}}, {2, {66579, 66619}}, {2, {66580, 66620}}, {2, {66581, 66621}}, {2, {66582, 66622}}, {2, {66583, 66623}}, {2, {66584, 66624}}, {2, {66585, 66625}}, {2, {66586, 66626}}, {2, {66587, 66627}}, {2, {66588, 66628}}, {2, {66589, 66629}}, {2, {66590, 66630}}, {2, {66591, 66631}}, {2, {66592, 66632}}, {2, {66593, 66633}}, {2, {66594, 66634}}, {2, {66595, 66635}}, {2, {66596, 66636}}, {2, {66597, 66637}}, {2, {66598, 66638}}, {2, {66599, 66639}}, {0, {0}} }; // NOLINT
static const uint16_t kEcma262UnCanonicalizeTable2Size = 40;
static const uint16_t kEcma262UnCanonicalizeTable2[80] = { 1064, 1, 1065, 5, 1066, 9, 1067, 13, 1068, 17, 1069, 21, 1070, 25, 1071, 29, 1072, 33, 1073, 37, 1074, 41, 1075, 45, 1076, 49, 1077, 53, 1078, 57, 1079, 61, 1080, 65, 1081, 69, 1082, 73, 1083, 77, 1084, 81, 1085, 85, 1086, 89, 1087, 93, 1088, 97, 1089, 101, 1090, 105, 1091, 109, 1092, 113, 1093, 117, 1094, 121, 1095, 125, 1096, 129, 1097, 133, 1098, 137, 1099, 141, 1100, 145, 1101, 149, 1102, 153, 1103, 157 }; // NOLINT
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
    case 2: return LookupMapping(kEcma262UnCanonicalizeTable2,
                                     kEcma262UnCanonicalizeTable2Size,
                                     kEcma262UnCanonicalizeMultiStrings2,
                                     c,
                                     n,
                                     result,
                                     allow_caching_ptr);
    default: return 0;
  }
}


uchar UnicodeData::kMaxCodePoint = 1114109;

int UnicodeData::GetByteCount() {
  return 0 + (sizeof(uint16_t) * kUppercaseTable0Size) + (sizeof(uint16_t) * kUppercaseTable1Size) + (sizeof(uint16_t) * kUppercaseTable2Size) + (sizeof(uint16_t) * kUppercaseTable3Size) + (sizeof(uint16_t) * kLowercaseTable0Size) + (sizeof(uint16_t) * kLowercaseTable1Size) + (sizeof(uint16_t) * kLowercaseTable2Size) + (sizeof(uint16_t) * kLowercaseTable3Size) + (sizeof(uint16_t) * kLetterTable0Size) + (sizeof(uint16_t) * kLetterTable1Size) + (sizeof(uint16_t) * kLetterTable2Size) + (sizeof(uint16_t) * kLetterTable3Size) + (sizeof(uint16_t) * kLetterTable4Size) + (sizeof(uint16_t) * kLetterTable5Size) + (sizeof(uint16_t) * kSpaceTable0Size) + (sizeof(uint16_t) * kNumberTable0Size) + (sizeof(uint16_t) * kNumberTable1Size) + (sizeof(uint16_t) * kNumberTable2Size) + (sizeof(uint16_t) * kNumberTable3Size) + (sizeof(uint16_t) * kWhiteSpaceTable0Size) + (sizeof(uint16_t) * kLineTerminatorTable0Size) + (sizeof(uint16_t) * kCombiningMarkTable0Size) + (sizeof(uint16_t) * kCombiningMarkTable1Size) + (sizeof(uint16_t) * kCombiningMarkTable2Size) + (sizeof(uint16_t) * kCombiningMarkTable3Size) + (sizeof(uint16_t) * kCombiningMarkTable28Size) + (sizeof(uint16_t) * kConnectorPunctuationTable0Size) + (sizeof(uint16_t) * kConnectorPunctuationTable1Size) + (sizeof(uint16_t) * kToLowercaseTable0Size) + (sizeof(uint16_t) * kToLowercaseTable1Size) + (sizeof(uint16_t) * kToLowercaseTable2Size) + (sizeof(uint16_t) * kToUppercaseTable0Size) + (sizeof(uint16_t) * kToUppercaseTable1Size) + (sizeof(uint16_t) * kToUppercaseTable2Size) + (sizeof(uint16_t) * kEcma262CanonicalizeTable0Size) + (sizeof(uint16_t) * kEcma262CanonicalizeTable1Size) + (sizeof(uint16_t) * kEcma262CanonicalizeTable2Size) + (sizeof(uint16_t) * kEcma262UnCanonicalizeTable0Size) + (sizeof(uint16_t) * kEcma262UnCanonicalizeTable1Size) + (sizeof(uint16_t) * kEcma262UnCanonicalizeTable2Size); // NOLINT
}

}  // namespace unicode
