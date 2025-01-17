////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2016 by EMC Corporation, All Rights Reserved
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
/// Copyright holder is EMC Corporation
///
/// @author Andrey Abramov
////////////////////////////////////////////////////////////////////////////////

#ifndef IRESEARCH_TOKEN_ATTRIBUTES_H
#define IRESEARCH_TOKEN_ATTRIBUTES_H

#include "store/data_input.hpp"

#include "index/index_reader.hpp"
#include "index/iterators.hpp"

#include "utils/attribute_provider.hpp"
#include "utils/attributes.hpp"
#include "utils/string.hpp"
#include "utils/type_limits.hpp"
#include "utils/iterator.hpp"

namespace iresearch {

//////////////////////////////////////////////////////////////////////////////
/// @class offset 
/// @brief represents token offset in a stream 
//////////////////////////////////////////////////////////////////////////////
struct offset final : attribute {
  static constexpr string_ref type_name() noexcept { return "offset"; }

  void clear() noexcept {
    start = 0;
    end = 0;
  }

  uint32_t start{0};
  uint32_t end{0};
};

//////////////////////////////////////////////////////////////////////////////
/// @class increment 
/// @brief represents token increment in a stream 
//////////////////////////////////////////////////////////////////////////////
struct increment final : attribute {
  static constexpr string_ref type_name() noexcept { return "increment"; }

  uint32_t value{1};
};

//////////////////////////////////////////////////////////////////////////////
/// @class term_attribute 
/// @brief represents term value in a stream 
//////////////////////////////////////////////////////////////////////////////
struct term_attribute final : attribute {
  static constexpr string_ref type_name() noexcept { return "term_attribute"; }

  bytes_ref value;
};

//////////////////////////////////////////////////////////////////////////////
/// @class payload
/// @brief represents an arbitrary byte sequence associated with
///        the particular term position in a field
//////////////////////////////////////////////////////////////////////////////
struct payload final : attribute {
  // DO NOT CHANGE NAME
  static constexpr string_ref type_name() noexcept { return "payload"; }

  bytes_ref value;
};

//////////////////////////////////////////////////////////////////////////////
/// @class document 
/// @brief contains a document identifier
//////////////////////////////////////////////////////////////////////////////
struct document : attribute {
  // DO NOT CHANGE NAME
  static constexpr string_ref type_name() noexcept { return "document"; }

  explicit document(irs::doc_id_t doc = irs::doc_limits::invalid()) noexcept
    : value(doc) {
  }

  doc_id_t value;
};

//////////////////////////////////////////////////////////////////////////////
/// @class frequency 
/// @brief how many times term appears in a document
//////////////////////////////////////////////////////////////////////////////
struct frequency final : attribute {
  // DO NOT CHANGE NAME
  static constexpr string_ref type_name() noexcept { return "frequency"; }

  uint32_t value{0};
}; // frequency

//////////////////////////////////////////////////////////////////////////////
/// @class granularity_prefix
/// @brief indexed tokens are prefixed with one byte indicating granularity
///        this is marker attribute only used in field::features and by_range
///        exact values are prefixed with 0
///        the less precise the token the greater its granularity prefix value
//////////////////////////////////////////////////////////////////////////////
struct granularity_prefix final {
  // DO NOT CHANGE NAME
  static constexpr string_ref type_name() noexcept {
    return "iresearch::granularity_prefix";
  }
}; // granularity_prefix

//////////////////////////////////////////////////////////////////////////////
/// @class position 
/// @brief iterator represents term positions in a document
//////////////////////////////////////////////////////////////////////////////
class position
  : public attribute,
    public attribute_provider {
 public:
  using value_t = uint32_t;
  using ref = std::reference_wrapper<position>;

  // DO NOT CHANGE NAME
  static constexpr string_ref type_name() noexcept { return "position"; }

  static position* empty() noexcept;

  template<typename Provider>
  static position& get_mutable(Provider& attrs) {
    auto* pos = irs::get_mutable<position>(&attrs);
    return pos ? *pos : *empty();
  }

  virtual value_t seek(value_t target) {
    while ((value_< target) && next());
    return value_;
  }

  value_t value() const noexcept {
    return value_;
  }

  virtual void reset() = 0;

  virtual bool next() = 0;

 protected:
  value_t value_{ pos_limits::invalid() };
}; // position

//////////////////////////////////////////////////////////////////////////////
/// @class attribute_provider_change
/// @brief subscription for attribute provider change
//////////////////////////////////////////////////////////////////////////////
class attribute_provider_change final : public attribute {
 public:
  using callback_f = std::function<void(attribute_provider&)>;

  static constexpr string_ref type_name() noexcept {
    return "attribute_provider_change";
  }

  void subscribe(callback_f&& callback) const {
    callback_ = std::move(callback);

    if (IRS_UNLIKELY(!callback_)) {
      callback_ = &noop;
    }
  }

  void operator()(attribute_provider& attrs) const {
    assert(callback_);
    callback_(attrs);
  }

 private:
  static void noop(attribute_provider&) noexcept { }

  mutable callback_f callback_{&noop};
}; // attribute_provider_change

} // ROOT

#endif
