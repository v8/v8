// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_UTILS_H_
#define V8_TORQUE_UTILS_H_

#include <string>
#include <vector>

namespace v8 {
namespace internal {
namespace torque {

typedef std::vector<std::string> NameVector;

void ReportError(const std::string& error);

std::string CamelifyString(const std::string& underscore_string);
std::string DashifyString(const std::string& underscore_string);

void ReplaceFileContentsIfDifferent(const std::string& file_path,
                                    const std::string& contents);

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_UTILS_H_
