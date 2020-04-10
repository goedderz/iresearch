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

#include "levenshtein_filter.hpp"

#include "shared.hpp"
#include "term_query.hpp"
#include "limited_sample_collector.hpp"
#include "top_terms_collector.hpp"
#include "all_terms_collector.hpp"
#include "filter_visitor.hpp"
#include "multiterm_query.hpp"
#include "index/index_reader.hpp"
#include "utils/automaton_utils.hpp"
#include "utils/levenshtein_utils.hpp"
#include "utils/levenshtein_default_pdp.hpp"
#include "utils/hash_utils.hpp"
#include "utils/noncopyable.hpp"
#include "utils/utf8_utils.hpp"
#include "utils/std.hpp"

NS_LOCAL

using namespace irs;

////////////////////////////////////////////////////////////////////////////////
/// @returns levenshtein similarity
////////////////////////////////////////////////////////////////////////////////
FORCE_INLINE boost_t similarity(uint32_t distance, uint32_t size) noexcept {
  assert(size);

  static_assert(sizeof(boost_t) == sizeof(uint32_t),
                "sizeof(boost_t) != sizeof(uint32_t)");

  return 1.f - boost_t(distance) / size;
}

template<typename Invalid, typename Term, typename Levenshtein>
inline void executeLevenshtein(byte_type max_distance,
                               by_edit_distance::pdp_f provider, bool with_transpositions,
                               Invalid inv, Term t, Levenshtein lev) {
  if (0 == max_distance) {
    t();
    return;
  }

  assert(provider);
  const auto& d = (*provider)(max_distance, with_transpositions);

  if (!d) {
    inv();
    return;
  }

  lev(d);
}

template<typename Visitor>
struct top_terms_visitor : util::noncopyable {
  explicit top_terms_visitor(Visitor& visitor) noexcept
    : visitor(&visitor) {
  }

  void operator()(const irs::sub_reader& /*segment*/,
                  const irs::term_reader& field,
                  uint32_t /*docs_count*/) const {
    it = field.iterator();
    visitor.prepare(*it);
  }

  void operator()(seek_term_iterator::cookie_ptr& cookie) const {
    if (!it->seek(irs::bytes_ref::NIL, *cookie)) {
      return;
    }

    visitor.visit();
  }

  mutable seek_term_iterator::ptr it;
  Visitor& visitor;
};

template<typename StatesType>
struct aggregated_stats_visitor : util::noncopyable {
  aggregated_stats_visitor(
      StatesType& states,
      const term_collectors& term_stats) noexcept
    : term_stats(term_stats),
      states(states) {
  }

  void operator()(const irs::sub_reader& segment,
                  const irs::term_reader& field,
                  uint32_t docs_count) const {
    it = field.iterator();
    state = &states.insert(segment);
    state->reader = &field;
    state->scored_states_estimation += docs_count;
  }

  void operator()(seek_term_iterator::cookie_ptr& cookie) const {
    if (!it->seek(irs::bytes_ref::NIL, *cookie)) {
      return;
    }

    term_stats.collect(*segment, *field, 0, it->attributes());
    state->scored_states.emplace_back(std::move(cookie), 0, boost);
  }

  const term_collectors& term_stats;
  StatesType& states;
  mutable seek_term_iterator::ptr it;
  mutable typename StatesType::state_type* state{};
  const sub_reader* segment{};
  const term_reader* field{};
  boost_t boost{ irs::no_boost() };
};

template<typename Collector>
class levenshtein_terms_visitor : public filter_visitor {
 public:
  levenshtein_terms_visitor(
      Collector& collector,
      const parametric_description& d,
      const bytes_ref& term)
    : collector_(collector),
      utf8_term_size_(std::max(1U, uint32_t(utf8_utils::utf8_length(term)))),
      no_distance_(d.max_distance() + 1) {
  }

  void prepare(const sub_reader& segment, const term_reader& field) noexcept {
    segment_ = &segment;
    field_ = &field;
  }

  template<typename Visitor>
  void visit(const Visitor& visitor) {
    collector_.visit(visitor);
  }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief makes preparations for a visitor
  //////////////////////////////////////////////////////////////////////////////
  virtual void prepare(const seek_term_iterator& terms) final {
    term_ = &terms.value();

    auto& payload = terms.attributes().get<irs::payload>();

    distance_ = &no_distance_;
    if (payload && !payload->value.empty()) {
      distance_ = &payload->value.front();
    }

    collector_.prepare(*segment_, *field_, terms);
  }

  virtual void visit() final {
     const auto utf8_value_size = static_cast<uint32_t>(utf8_utils::utf8_length(*term_));
     const auto key = ::similarity(*distance_, std::min(utf8_value_size, utf8_term_size_));

     collector_.collect(key);
  }

 private:
  Collector& collector_;
  const sub_reader* segment_{};
  const term_reader* field_{};
  const bytes_ref* term_{};
  const uint32_t utf8_term_size_;
  const byte_type no_distance_;
  const byte_type* distance_{&no_distance_};
};

template<typename Collector>
bool collect_terms(
    const index_reader& index,
    const string_ref& field,
    const bytes_ref& term,
    const parametric_description& d,
    Collector& collector) {
  levenshtein_terms_visitor<Collector> visitor(collector, d, term);

  const auto acceptor = make_levenshtein_automaton(d, term);
  auto matcher = make_automaton_matcher(acceptor);

  for (auto& segment : index) {
    auto* reader = segment.field(field);

    if (!reader) {
      continue;
    }

    visitor.prepare(segment, *reader);

    // wrong matcher
    if (!::automaton_visit(*reader, matcher, visitor)) {
      return false;
    }
  }

  return true;
}

class top_terms_collector : public irs::top_terms_collector<top_term_state<boost_t>> {
 public:
  using base_type = irs::top_terms_collector<top_term_state<boost_t>>;

  top_terms_collector(size_t size, field_collectors& field_stats)
    : base_type(size),
      field_stats_(field_stats) {
  }

  void prepare(const sub_reader& segment,
               const term_reader& field,
               const seek_term_iterator& terms) {
    field_stats_.collect(segment, field);
    base_type::prepare(segment, field, terms);
  }

 private:
  field_collectors& field_stats_;
};

template<typename Visitor>
void visit_levenshtein_terms(
    const index_reader& index,
    const string_ref& field,
    const bytes_ref& term,
    size_t terms_limit,
    const parametric_description& d,
    Visitor& visitor) {
  using top_terms_collector = irs::top_terms_collector<top_term<boost_t>>;

  top_terms_collector term_collector(terms_limit);

  if (!collect_terms(index, field, term, d, term_collector)) {
    return;
  }

  top_terms_visitor<Visitor> visit_terms(visitor);
  term_collector.visit(visit_terms);
}

filter::prepared::ptr prepare_levenshtein_filter(
    const index_reader& index,
    const order::prepared& order,
    boost_t boost,
    const string_ref& field,
    const bytes_ref& term,
    size_t terms_limit,
    const parametric_description& d) {
  field_collectors field_stats(order);
  term_collectors term_stats(order, 1);
  multiterm_query::states_t states(index.size());

  if (!terms_limit) {
    all_terms_collector<decltype(states)> term_collector(states, field_stats, term_stats);

    if (!collect_terms(index, field, term, d, term_collector)) {
      return filter::prepared::empty();
    }
  } else {
    top_terms_collector term_collector(terms_limit, field_stats);

    if (!collect_terms(index, field, term, d, term_collector)) {
      return filter::prepared::empty();
    }

    aggregated_stats_visitor<decltype(states)> aggregate_stats(states, term_stats);
    term_collector.visit([&aggregate_stats](top_term_state<boost_t>& state) {
      aggregate_stats.boost = std::max(0.f, state.key);
      state.visit(aggregate_stats);
    });
  }

  std::vector<bstring> stats(1);
  stats.back().resize(order.stats_size(), 0);
  auto* stats_buf = const_cast<byte_type*>(stats[0].data());
  term_stats.finish(stats_buf, field_stats, index);

  return memory::make_shared<multiterm_query>(
    std::move(states), std::move(stats),
    boost, sort::MergeType::MAX);
}

NS_END

NS_ROOT

// -----------------------------------------------------------------------------
// --SECTION--                                   by_edit_distance implementation
// -----------------------------------------------------------------------------

DEFINE_FILTER_TYPE(by_edit_distance)
DEFINE_FACTORY_DEFAULT(by_edit_distance)

/*static*/ filter::prepared::ptr by_edit_distance::prepare(
    const index_reader& index,
    const order::prepared& order,
    boost_t boost,
    const string_ref& field,
    const bytes_ref& term,
    size_t scored_terms_limit,
    byte_type max_distance,
    pdp_f provider,
    bool with_transpositions) {
  filter::prepared::ptr res;
  executeLevenshtein(
    max_distance, provider, with_transpositions,
    [&res]() {
      res = prepared::empty();
    },
    [&res, &index, &order, boost, &field, &term]() {
      res = term_query::make(index, order, boost, field, term);
    },
    [&res, &field, &term, scored_terms_limit, &index, &order, boost](const parametric_description& d) {
      res = prepare_levenshtein_filter(index, order, boost, field, term, scored_terms_limit, d);
    }
  );
  return res;
}

/*static*/ void by_edit_distance::visit(
    const term_reader& reader,
    const bytes_ref& term,
    byte_type max_distance,
    by_edit_distance::pdp_f provider,
    bool with_transpositions,
    filter_visitor& fv) {
  executeLevenshtein(
    max_distance, provider, with_transpositions,
    []() {},
    [&reader, &term, &fv]() {
      term_query::visit(reader, term, fv);
    },
    [&reader, &term, &fv](const parametric_description& d) {
      const auto acceptor = make_levenshtein_automaton(d, term);
      auto matcher = make_automaton_matcher(acceptor);

      automaton_visit(reader, matcher, fv);
    }
  );
}

by_edit_distance::by_edit_distance() noexcept
  : by_prefix(by_edit_distance::type()),
    provider_(&default_pdp) {
}

by_edit_distance& by_edit_distance::provider(pdp_f provider) noexcept {
  if (!provider) {
    provider_ = &default_pdp;
  } else {
    provider_ = provider;
  }
  return *this;
}

size_t by_edit_distance::hash() const noexcept {
  size_t seed = 0;
  seed = hash_combine(0, by_prefix::hash());
  seed = hash_combine(seed, max_distance_);
  seed = hash_combine(seed, with_transpositions_);
  return seed;
}

bool by_edit_distance::equals(const filter& rhs) const noexcept {
  const auto& impl = static_cast<const by_edit_distance&>(rhs);

  return by_prefix::equals(rhs) &&
    max_distance_ == impl.max_distance_ &&
    with_transpositions_ == impl.with_transpositions_;
}

NS_END
