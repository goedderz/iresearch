//
// IResearch search engine 
// 
// Copyright � 2016 by EMC Corporation, All Rights Reserved
// 
// This software contains the intellectual property of EMC Corporation or is licensed to
// EMC Corporation from third parties. Use of this software and the intellectual property
// contained therein is expressly limited to the terms and conditions of the License
// Agreement under which it is provided by or on behalf of EMC.
// 

#include "shared.hpp"
#include "segment_writer.hpp"
#include "store/store_utils.hpp"
#include "index_meta.hpp"
#include "analysis/token_stream.hpp"
#include "analysis/token_attributes.hpp"
#include "utils/log.hpp"
#include "utils/timer_utils.hpp"
#include "utils/type_limits.hpp"
#include "utils/version_utils.hpp"

#include <math.h>

NS_ROOT

bool segment_writer::norm_factor::write(data_output& out) const {
  write_zvfloat(out, value);
  return true;
}

bool segment_writer::doc_header::write(data_output& out) const {
  return stored::write_header(out, doc_fields.begin(), doc_fields.end());
}

segment_writer::ptr segment_writer::make(directory& dir, format::ptr codec) {
  return ptr(new segment_writer(dir, codec));
}

segment_writer::segment_writer(directory& dir, format::ptr codec) NOEXCEPT
  : codec_(codec), dir_(dir), initialized_(false) {
}

bool segment_writer::remove(doc_id_t doc_id) {
  return
    doc_id < (type_limits<type_t::doc_id_t>::min() + num_docs_cached_)
    && docs_mask_.insert(doc_id).second;
}

bool segment_writer::index_field(
    field_data& slot,
    doc_id_t doc_id,
    token_stream& tokens,
    const flags& features,
    float_t boost
) {
  REGISTER_TIMER_DETAILED();


  // invert only if new field features are a subset of slot features
  if ((slot.empty() || features.is_subset_of(slot.meta().features)) &&
      slot.invert(&tokens, slot.empty() ? features : slot.meta().features, boost, doc_id)
  ) {
    if (features.check<norm>()) {
      norm_fields_.insert(&slot);
    }

    fields_ += features; // accumulate segment features
    return true;
  }

  return false;
}

bool segment_writer::store_field(
    field_data& slot,
    doc_id_t doc_id,
    const serializer& serializer
) {
  REGISTER_TIMER_DETAILED();
  if (sf_writer_->write(serializer)) {
    // store field id
    header_.doc_fields.push_back(slot.meta().id);
    return true;
  }

  return false;
}

bool segment_writer::store_attribute(
    doc_id_t doc_id,
    const string_ref& name,
    const serializer& serializer
) {
  REGISTER_TIMER_DETAILED();
  auto res = columns_.emplace(
    std::piecewise_construct,
    std::forward_as_tuple(make_hashed_ref(name, string_ref_hash_t())),
    std::forward_as_tuple()
  );

  auto& it = res.first;
  auto& column = it->second;

  if (res.second) {
    // we have never seen it before
    column.handle = col_writer_->push_column();
    column.name = std::string(name.c_str(), name.size());

    auto& key = const_cast<hashed_string_ref&>(it->first);
    key = hashed_string_ref(it->first.hash(), column.name);
  }

  return column.handle.second(doc_id, serializer);
}

void segment_writer::finish(doc_id_t doc_id, const update_context& ctx) {
  REGISTER_TIMER_DETAILED();

  // write document normalization factors (for each field marked for normalization))
  for (auto* field : norm_fields_) {
    norm_.value  = field->boost() / float_t(std::sqrt(double_t(field->size())));
    if (norm_.value != norm::DEFAULT()) {
      field->write_norm(norm_, *col_writer_);
    }
  }
  norm_fields_.clear(); // clear normalized fields

  // finish stored fields 
  sf_writer_->end(&header_); 
  header_.doc_fields.clear(); // clear stored document header

  docs_context_[doc_id] = ctx;
}

bool segment_writer::flush(std::string& filename, segment_meta& meta) {
  REGISTER_TIMER_DETAILED();

  // flush stored fields
  sf_writer_->finish();
  sf_writer_->reset();

  // flush columnstore
  col_writer_->flush();

  // flush columns indices
  if (!columns_.empty()) {
    // flush columns meta
    col_meta_writer_->prepare(dir_, seg_name_);
    for (auto& entry : columns_) {
      auto& column = entry.second;
      col_meta_writer_->write(column.name, column.handle.first);
    }
    col_meta_writer_->flush();
    columns_.clear();
  }

  // flush fields metadata & inverted data
  {
    flush_state state;
    state.dir = &dir_;
    state.doc_count = num_docs_cached_;
    state.name = seg_name_;
    state.ver = IRESEARCH_VERSION;

    fields_.flush(*field_meta_writer_, *field_writer_, state);
  }

  meta.docs_count = num_docs_cached_;
  meta.files.clear(); // prepare empy set to be swaped into dir_

  if (!dir_.swap_tracked(meta.files)) {
    IR_ERROR() << "Failed to swap list of tracked files in: " << __FUNCTION__ << ":" << __LINE__;

    return false;
  }

  // flush segment metadata
  {
    segment_meta_writer::ptr writer = codec_->get_segment_meta_writer();
    writer->write(dir_, meta);

    filename = writer->filename(meta);
  }

  return true;
}

void segment_writer::reset() {
  initialized_ = false;

  if (!dir_.swap_tracked(tracking_directory(*dir_))) {
    // on failre next segment might have extra files which will fail to get refs
    IR_ERROR() << "Failed to swap list of tracked files in: " << __FUNCTION__ << ":" << __LINE__;
  }

  docs_context_.clear();
  docs_mask_.clear();
  fields_.reset();
  num_docs_cached_ = 0;
}

void segment_writer::reset(std::string seg_name) {
  reset();

  seg_name_ = std::move(seg_name);

  if (!field_meta_writer_) {
    field_meta_writer_ = codec_->get_field_meta_writer();
  }

  if (!field_writer_) {
    field_writer_ = codec_->get_field_writer();
  }

  if (!sf_writer_) {
    sf_writer_ = codec_->get_stored_fields_writer();
  }

  if (!col_meta_writer_) {
    col_meta_writer_ = codec_->get_column_meta_writer();
  }

  if (!col_writer_) {
    col_writer_ = codec_->get_columnstore_writer();
  }

  const string_ref ref_seg_name = seg_name_;
  sf_writer_->prepare(dir_, ref_seg_name);
  col_writer_->prepare(dir_, ref_seg_name);

  initialized_ = true;
}

NS_END