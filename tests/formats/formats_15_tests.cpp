/// Copyright 2020 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Andrei Abramov
////////////////////////////////////////////////////////////////////////////////

#include "tests_shared.hpp"
#include "formats_test_case_base.hpp"
#include "store/directory_attributes.hpp"

namespace {

// 1.5 specific tests
class format_15_test_case : public tests::directory_test_case_base<> { };

TEST_P(format_15_test_case, Wanderator) {

}

INSTANTIATE_TEST_SUITE_P(
  format_15_test,
  format_15_test_case,
  ::testing::Combine(
    ::testing::Values(
      &tests::directory<&tests::memory_directory>,
      &tests::directory<&tests::fs_directory>,
      &tests::directory<&tests::mmap_directory>,
      &tests::rot13_directory<&tests::fs_directory, 16>,
      &tests::rot13_directory<&tests::mmap_directory, 16>,
      &tests::rot13_directory<&tests::memory_directory, 7>,
      &tests::rot13_directory<&tests::fs_directory, 7>,
      &tests::rot13_directory<&tests::mmap_directory, 7>),
    ::testing::Values(tests::format_info{"1_4", "1_4simd"})),
  format_15_test_case::to_string);

// generic tests
using tests::format_test_case;

INSTANTIATE_TEST_SUITE_P(
  format_15_test,
  format_test_case,
  ::testing::Combine(
    ::testing::Values(
      &tests::directory<&tests::memory_directory>,
      &tests::directory<&tests::fs_directory>,
      &tests::directory<&tests::mmap_directory>,
      &tests::rot13_directory<&tests::fs_directory, 16>,
      &tests::rot13_directory<&tests::mmap_directory, 16>,
      &tests::rot13_directory<&tests::memory_directory, 7>,
      &tests::rot13_directory<&tests::fs_directory, 7>,
      &tests::rot13_directory<&tests::mmap_directory, 7>),
    ::testing::Values(tests::format_info{"1_5", "1_5simd"})),
  format_test_case::to_string);

}
