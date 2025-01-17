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

#ifndef IRESEARCH_BOOLEAN_FILTER_H
#define IRESEARCH_BOOLEAN_FILTER_H

#include <vector>

#include "all_filter.hpp"
#include "filter.hpp"
#include "utils/iterator.hpp"

namespace iresearch {

//////////////////////////////////////////////////////////////////////////////
/// @class boolean_filter
/// @brief defines user-side boolean filter, as the container for other
/// filters
//////////////////////////////////////////////////////////////////////////////
class boolean_filter : public filter, private util::noncopyable {
 public:
  typedef std::vector<filter::ptr> filters_t;
  typedef ptr_iterator<filters_t::const_iterator> const_iterator;
  typedef ptr_iterator<filters_t::iterator> iterator;

  const_iterator begin() const { return const_iterator(filters_.begin()); }
  const_iterator end() const { return const_iterator(filters_.end()); }

  iterator begin() { return iterator(filters_.begin()); }
  iterator end() { return iterator(filters_.end()); }

  template<typename T>
  T& add() {
    using type = typename std::enable_if_t<std::is_base_of_v<filter, T>, T>;

    filters_.emplace_back(type::make());
    return static_cast<type&>(*filters_.back());
  }

  virtual size_t hash() const noexcept override;

  void clear() { return filters_.clear(); }
  bool empty() const { return filters_.empty(); }
  size_t size() const { return filters_.size(); }

  virtual filter::prepared::ptr prepare(
      const index_reader& rdr, const order::prepared& ord, boost_t boost,
      const attribute_provider* ctx) const override final;

 protected:
  explicit boolean_filter(const type_info& type) noexcept;
  virtual bool equals(const filter& rhs) const noexcept override;

  virtual filter::prepared::ptr prepare(
      std::vector<const filter*>& incl, std::vector<const filter*>& excl,
      const index_reader& rdr, const order::prepared& ord, boost_t boost,
      const attribute_provider* ctx) const = 0;

 private:
  void group_filters(std::vector<const filter*>& incl,
                     std::vector<const filter*>& excl) const;

  filters_t filters_;
};

//////////////////////////////////////////////////////////////////////////////
/// @class And
//////////////////////////////////////////////////////////////////////////////
class And : public boolean_filter {
 public:
  static ptr make();

  And() noexcept;

  using filter::prepare;

 protected:
  virtual filter::prepared::ptr prepare(
      std::vector<const filter*>& incl, std::vector<const filter*>& excl,
      const index_reader& rdr, const order::prepared& ord, boost_t boost,
      const attribute_provider* ctx) const override;
};  // And

//////////////////////////////////////////////////////////////////////////////
/// @class Or
//////////////////////////////////////////////////////////////////////////////
class Or : public boolean_filter {
 public:
  static ptr make();

  Or() noexcept;

  using filter::prepare;

  //////////////////////////////////////////////////////////////////////////////
  /// @return minimum number of subqueries which must be satisfied
  //////////////////////////////////////////////////////////////////////////////
  size_t min_match_count() const { return min_match_count_; }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief sets minimum number of subqueries which must be satisfied
  //////////////////////////////////////////////////////////////////////////////
  Or& min_match_count(size_t count) {
    min_match_count_ = count;
    return *this;
  }

 protected:
  virtual filter::prepared::ptr prepare(
      std::vector<const filter*>& incl, std::vector<const filter*>& excl,
      const index_reader& rdr, const order::prepared& ord, boost_t boost,
      const attribute_provider* ctx) const override;

 private:
  size_t min_match_count_;
};  // Or

//////////////////////////////////////////////////////////////////////////////
/// @class not
//////////////////////////////////////////////////////////////////////////////
class Not : public filter {
 public:
  static ptr make();

  Not() noexcept;

  const irs::filter* filter() const { return filter_.get(); }

  template<typename T>
  const T* filter() const {
    using type = typename std::enable_if_t<std::is_base_of_v<irs::filter, T>, T>;

    return static_cast<const type*>(filter_.get());
  }

  template<typename T>
  T& filter() {
    using type = typename std::enable_if_t<std::is_base_of_v<irs::filter, T>, T>;

    filter_ = type::make();
    return static_cast<type&>(*filter_);
  }

  void clear() { filter_.reset(); }
  bool empty() const { return nullptr == filter_; }

  using filter::prepare;

  virtual filter::prepared::ptr prepare(
      const index_reader& rdr, const order::prepared& ord, boost_t boost,
      const attribute_provider* ctx) const override;

  virtual size_t hash() const noexcept override;

 protected:
  virtual bool equals(const irs::filter& rhs) const noexcept override;

 private:
  filter::ptr filter_;
};

}  // namespace iresearch

#endif
