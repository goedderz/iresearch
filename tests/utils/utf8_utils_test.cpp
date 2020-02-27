////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2019 ArangoDB GmbH, Cologne, Germany
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
/// @author Andrey Abramov
////////////////////////////////////////////////////////////////////////////////

#include "tests_shared.hpp"
#include "utils/string.hpp"
#include "utils/std.hpp"
#include "utils/utf8_utils.hpp"

TEST(utf8_utils_test, static_const) {
  ASSERT_EQ(4, size_t(irs::utf8_utils::MAX_CODE_POINT_SIZE));
  ASSERT_EQ(0, size_t(irs::utf8_utils::MIN_CODE_POINT));
  ASSERT_EQ(0x10FFFF, size_t(irs::utf8_utils::MAX_CODE_POINT));
}

TEST(utf8_utils_test, test) {
  // ascii sequence
  {
    const irs::bytes_ref str = irs::ref_cast<irs::byte_type>(irs::string_ref("abcd"));
    const std::vector<uint32_t> expected = { 0x0061, 0x0062, 0x0063, 0x0064 };

    {
      auto expected_begin = expected.begin();
      auto begin = str.begin();
      auto end = str.end();
      for (; begin != str.end(); ) {
        auto next = irs::utf8_utils::next(begin, end);
        ASSERT_EQ(1, std::distance(begin, next));
        ASSERT_EQ(*expected_begin, irs::utf8_utils::next(begin));
        ASSERT_EQ(begin, next);
        ++expected_begin;
        if (expected_begin == expected.end()) {
          ASSERT_EQ(end, next);
        } else {
          ASSERT_NE(end, next);
        }
      }
      ASSERT_EQ(expected.end(), expected_begin);
    }

    {
      auto expected_begin = expected.begin();
      for (auto begin = str.begin(), end = str.end(); begin != end; ++expected_begin) {
        ASSERT_EQ(*expected_begin, irs::utf8_utils::next(begin));
      }
      ASSERT_EQ(expected.end(), expected_begin);
    }

    {
      auto expected_begin = expected.begin();
      for (auto begin = str.begin(), end = str.end(); begin != end; ++expected_begin) {
        const auto cp = irs::utf8_utils::next_checked(begin, end);
        ASSERT_EQ(*expected_begin, cp);
      }
      ASSERT_EQ(expected.end(), expected_begin);
    }

    {
      std::vector<uint32_t> actual;
      irs::utf8_utils::utf8_to_utf32(str, irs::irstd::back_emplacer(actual));
      ASSERT_EQ(expected, actual);
    }

    {
      std::vector<uint32_t> actual;
      ASSERT_TRUE(irs::utf8_utils::utf8_to_utf32_checked(str, irs::irstd::back_emplacer(actual)));
      ASSERT_EQ(expected, actual);
    }
  }

  // 2-bytes sequence
  {
    const irs::bytes_ref str = irs::ref_cast<irs::byte_type>(irs::string_ref("\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"));
    const std::vector<uint32_t> expected = { 0x043F, 0x0440, 0x0438, 0x0432, 0x0435, 0x0442};

    {
      auto expected_begin = expected.begin();
      auto begin = str.begin();
      auto end = str.end();
      for (; begin != str.end(); ) {
        auto next = irs::utf8_utils::next(begin, end);
        ASSERT_EQ(2, std::distance(begin, next));
        ASSERT_EQ(*expected_begin, irs::utf8_utils::next(begin));
        ASSERT_EQ(begin, next);
        ++expected_begin;
        if (expected_begin == expected.end()) {
          ASSERT_EQ(end, next);
        } else {
          ASSERT_NE(end, next);
        }
      }
      ASSERT_EQ(expected.end(), expected_begin);
    }

    {
      auto expected_begin = expected.begin();
      for (auto begin = str.begin(), end = str.end(); begin != end; ++expected_begin) {
        ASSERT_EQ(*expected_begin, irs::utf8_utils::next(begin));
      }
      ASSERT_EQ(expected.end(), expected_begin);
    }

    {
      auto expected_begin = expected.begin();
      for (auto begin = str.begin(), end = str.end(); begin != end; ++expected_begin) {
        const auto cp = irs::utf8_utils::next_checked(begin, end);
        ASSERT_EQ(*expected_begin, cp);
      }
      ASSERT_EQ(expected.end(), expected_begin);
    }

    {
      std::vector<uint32_t> actual;
      irs::utf8_utils::utf8_to_utf32(str, irs::irstd::back_emplacer(actual));
      ASSERT_EQ(expected, actual);
    }

    {
      std::vector<uint32_t> actual;
      ASSERT_TRUE(irs::utf8_utils::utf8_to_utf32_checked(str, irs::irstd::back_emplacer(actual)));
      ASSERT_EQ(expected, actual);
    }
  }

  // 3-bytes sequence
  {
    const irs::bytes_ref str = irs::ref_cast<irs::byte_type>(irs::string_ref("\xE2\x9E\x96\xE2\x9D\xA4"));
    const std::vector<uint32_t> expected = {
      0x2796, // heavy minus sign
      0x2764  // heavy black heart
    };

    {
      auto expected_begin = expected.begin();
      auto begin = str.begin();
      auto end = str.end();
      for (; begin != str.end(); ) {
        auto next = irs::utf8_utils::next(begin, end);
        ASSERT_EQ(3, std::distance(begin, next));
        ASSERT_EQ(*expected_begin, irs::utf8_utils::next(begin));
        ASSERT_EQ(begin, next);
        ++expected_begin;
        if (expected_begin == expected.end()) {
          ASSERT_EQ(end, next);
        } else {
          ASSERT_NE(end, next);
        }
      }
      ASSERT_EQ(expected.end(), expected_begin);
    }

    {
      auto expected_begin = expected.begin();
      for (auto begin = str.begin(), end = str.end(); begin != end; ++expected_begin) {
        ASSERT_EQ(*expected_begin, irs::utf8_utils::next(begin));
      }
      ASSERT_EQ(expected.end(), expected_begin);
    }

    {
      auto expected_begin = expected.begin();
      for (auto begin = str.begin(), end = str.end(); begin != end; ++expected_begin) {
        const auto cp = irs::utf8_utils::next_checked(begin, end);
        ASSERT_EQ(*expected_begin, cp);
      }
      ASSERT_EQ(expected.end(), expected_begin);
    }

    {
      std::vector<uint32_t> actual;
      irs::utf8_utils::utf8_to_utf32(str, irs::irstd::back_emplacer(actual));
      ASSERT_EQ(expected, actual);
    }

    {
      std::vector<uint32_t> actual;
      ASSERT_TRUE(irs::utf8_utils::utf8_to_utf32_checked(str, irs::irstd::back_emplacer(actual)));
      ASSERT_EQ(expected, actual);
    }
  }

  // 4-bytes sequence
  {
    const irs::bytes_ref str = irs::ref_cast<irs::byte_type>(irs::string_ref("\xF0\x9F\x98\x81\xF0\x9F\x98\x82"));
    const std::vector<uint32_t> expected = {
      0x1F601, // grinning face with smiling eyes
      0x1F602, // face with tears of joy
    };

    {
      auto expected_begin = expected.begin();
      auto begin = str.begin();
      auto end = str.end();
      for (; begin != str.end(); ) {
        auto next = irs::utf8_utils::next(begin, end);
        ASSERT_EQ(4, std::distance(begin, next));
        ASSERT_EQ(*expected_begin, irs::utf8_utils::next(begin));
        ASSERT_EQ(begin, next);
        ++expected_begin;
        if (expected_begin == expected.end()) {
          ASSERT_EQ(end, next);
        } else {
          ASSERT_NE(end, next);
        }
      }
      ASSERT_EQ(expected.end(), expected_begin);
    }

    {
      auto expected_begin = expected.begin();
      for (auto begin = str.begin(), end = str.end(); begin != end; ++expected_begin) {
        ASSERT_EQ(*expected_begin, irs::utf8_utils::next(begin));
      }
      ASSERT_EQ(expected.end(), expected_begin);
    }

    {
      auto expected_begin = expected.begin();
      for (auto begin = str.begin(), end = str.end(); begin != end; ++expected_begin) {
        const auto cp = irs::utf8_utils::next_checked(begin, end);
        ASSERT_EQ(*expected_begin, cp);
      }
      ASSERT_EQ(expected.end(), expected_begin);
    }

    {
      std::vector<uint32_t> actual;
      irs::utf8_utils::utf8_to_utf32(str, irs::irstd::back_emplacer(actual));
      ASSERT_EQ(expected, actual);
    }

    {
      std::vector<uint32_t> actual;
      ASSERT_TRUE(irs::utf8_utils::utf8_to_utf32_checked(str, irs::irstd::back_emplacer(actual)));
      ASSERT_EQ(expected, actual);
    }
  }
}

TEST(utf8_utils_test, utf32_to_utf8) {
  irs::byte_type buf[irs::utf8_utils::MAX_CODE_POINT_SIZE];

  // 1 byte
  ASSERT_EQ(1, irs::utf8_utils::utf32_to_utf8(0x46, buf));
  ASSERT_EQ(buf[0], 0x46);

  // 2 bytes
  ASSERT_EQ(2, irs::utf8_utils::utf32_to_utf8(0xA9, buf));
  ASSERT_EQ(buf[0], 0xC2);
  ASSERT_EQ(buf[1], 0xA9);

  // 3 bytes
  ASSERT_EQ(3, irs::utf8_utils::utf32_to_utf8(0x08F1, buf));
  ASSERT_EQ(buf[0], 0xE0);
  ASSERT_EQ(buf[1], 0xA3);
  ASSERT_EQ(buf[2], 0xB1);

  // 4 bytes
  ASSERT_EQ(4, irs::utf8_utils::utf32_to_utf8(0x1F996, buf));
  ASSERT_EQ(buf[0], 0xF0);
  ASSERT_EQ(buf[1], 0x9F);
  ASSERT_EQ(buf[2], 0xA6);
  ASSERT_EQ(buf[3], 0x96);
}