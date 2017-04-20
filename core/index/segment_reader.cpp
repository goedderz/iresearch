//
// IResearch search engine 
// 
// Copyright (c) 2016 by EMC Corporation, All Rights Reserved
// 
// This software contains the intellectual property of EMC Corporation or is licensed to
// EMC Corporation from third parties. Use of this software and the intellectual property
// contained therein is expressly limited to the terms and conditions of the License
// Agreement under which it is provided by or on behalf of EMC.
// 

#include "shared.hpp"
#include "segment_reader.hpp"

#include "index/index_meta.hpp"

#include "formats/format_utils.hpp"
#include "utils/index_utils.hpp"
#include "utils/singleton.hpp"
#include "utils/type_limits.hpp"

#include <unordered_map>

NS_LOCAL

class iterator_impl: public iresearch::index_reader::reader_iterator_impl {
 public:
  explicit iterator_impl(const iresearch::sub_reader* rdr = nullptr) NOEXCEPT
    : rdr_(rdr) {
  }

  virtual void operator++() override { rdr_ = nullptr; }
  virtual reference operator*() override {
    return *const_cast<iresearch::sub_reader*>(rdr_);
  }
  virtual const_reference operator*() const override { return *rdr_; }
  virtual bool operator==(
    const iresearch::index_reader::reader_iterator_impl& rhs
  ) override {
    return rdr_ == static_cast<const iterator_impl&>(rhs).rdr_;
  }

 private:
  const iresearch::sub_reader* rdr_;
};

class masked_docs_iterator 
    : public iresearch::segment_reader::docs_iterator_t,
      private iresearch::util::noncopyable {
 public:
  masked_docs_iterator(
    iresearch::doc_id_t begin,
    iresearch::doc_id_t end,
    const iresearch::document_mask& docs_mask
  ) :
    current_(iresearch::type_limits<iresearch::type_t::doc_id_t>::invalid()),
    docs_mask_(docs_mask),
    end_(end),
    next_(begin) {
  }

  virtual ~masked_docs_iterator() {}

  virtual bool next() override {
    while (next_ < end_) {
      current_ = next_++;

      if (docs_mask_.find(current_) == docs_mask_.end()) {
        return true;
      }
    }

    current_ = iresearch::type_limits<iresearch::type_t::doc_id_t>::eof();

    return false;
  }

  virtual iresearch::doc_id_t value() const override {
    return current_;
  }

 private:
  iresearch::doc_id_t current_;
  const iresearch::document_mask& docs_mask_;
  const iresearch::doc_id_t end_; // past last valid doc_id
  iresearch::doc_id_t next_;
};

bool read_columns_meta(
    const iresearch::format& codec, 
    const iresearch::directory& dir,
    const irs::segment_meta& meta,
    std::vector<iresearch::column_meta>& columns,
    std::vector<iresearch::column_meta*>& id_to_column,
    std::unordered_map<iresearch::hashed_string_ref, iresearch::column_meta*>& name_to_column
) {
  auto reader = codec.get_column_meta_reader();
  iresearch::field_id count = 0;

  if (!reader->prepare(dir, meta, count)) {
    return false;
  }

  columns.reserve(count);
  id_to_column.resize(count);
  name_to_column.reserve(count);

  for (iresearch::column_meta meta; reader->read(meta);) {
    columns.emplace_back(std::move(meta));

    auto& column = columns.back();
    id_to_column[column.id] = &column;

    const auto res = name_to_column.emplace(
      irs::make_hashed_ref(iresearch::string_ref(column.name), std::hash<irs::string_ref>()),
      &column
    );

    if (!res.second) {
      // duplicate field
      return false;
    }
  }

  assert(std::is_sorted(
    columns.begin(), columns.end(),
    [] (const iresearch::column_meta& lhs, const iresearch::column_meta& rhs) {
      return lhs.name < rhs.name;
  }));

  return true;
}

NS_END // NS_LOCAL

NS_ROOT

// -------------------------------------------------------------------
// segment_reader
// -------------------------------------------------------------------

class segment_reader::atomic_helper:
  public atomic_base<segment_reader::impl_ptr>,
  public singleton<segment_reader::atomic_helper> {
};

class segment_reader::segment_reader_impl: public sub_reader {
 public:
  virtual index_reader::reader_iterator begin() const override;
  virtual const column_meta* column(const string_ref& name) const override;
  virtual column_iterator::ptr columns() const override;
  const directory& dir() const NOEXCEPT;
  virtual uint64_t docs_count() const override;
  virtual docs_iterator_t::ptr docs_iterator() const override;
  virtual index_reader::reader_iterator end() const override;
  virtual const term_reader* field(const string_ref& name) const override;
  virtual field_iterator::ptr fields() const override;
  virtual uint64_t live_docs_count() const override;
  uint64_t meta_version() const NOEXCEPT;
  static segment_reader open(const directory& dir, const segment_meta& meta);
  virtual size_t size() const override;
  virtual columnstore_reader::values_reader_f values(field_id field) const override;
  virtual bool visit(
    field_id field, const columnstore_reader::values_visitor_f& reader
  ) const override;

 private:
  DECLARE_SPTR(segment_reader_impl); // required for NAMED_PTR(...)
  std::vector<column_meta> columns_;
  columnstore_reader::ptr columnstore_reader_;
  const directory& dir_;
  uint64_t docs_count_;
  document_mask docs_mask_;
  field_reader::ptr field_reader_;
  std::vector<column_meta*> id_to_column_;
  uint64_t meta_version_;
  std::unordered_map<hashed_string_ref, column_meta*> name_to_column_;

  segment_reader_impl(
    const directory& dir,
    uint64_t meta_version,
    uint64_t docs_count
  );
};

segment_reader::segment_reader(const impl_ptr& impl): impl_(impl) {
}

segment_reader::segment_reader(const segment_reader& other) {
  *this = other;
}

segment_reader::segment_reader(segment_reader&& other) NOEXCEPT {
  *this = std::move(other);
}

segment_reader& segment_reader::operator=(const segment_reader& other) {
  if (this != &other) {
    auto impl = atomic_helper::instance().atomic_load(&(other.impl_));

    atomic_helper::instance().atomic_exchange(&impl_, impl);
  }

  return *this;
}

segment_reader& segment_reader::operator=(segment_reader&& other) NOEXCEPT {
  if (this != &other) {
    atomic_helper::instance().atomic_exchange(&impl_, other.impl_);
  }

  return *this;
}

segment_reader::operator bool() const NOEXCEPT {
  return impl_ ? true : false;
}

segment_reader& segment_reader::operator*() NOEXCEPT {
  return *this;
}

const segment_reader& segment_reader::operator*() const NOEXCEPT {
  return *this;
}

segment_reader* segment_reader::operator->() NOEXCEPT {
  return this;
}

const segment_reader* segment_reader::operator->() const NOEXCEPT {
  return this;
}

index_reader::reader_iterator segment_reader::begin() const {
  return index_reader::reader_iterator(new iterator_impl(this));
}

const column_meta* segment_reader::column(const string_ref& name) const {
  return impl_->column(name);
}

column_iterator::ptr segment_reader::columns() const {
  return impl_->columns();
}

uint64_t segment_reader::docs_count() const {
  return impl_->docs_count();
}

sub_reader::docs_iterator_t::ptr segment_reader::docs_iterator() const {
  return impl_->docs_iterator();
}

index_reader::reader_iterator segment_reader::end() const {
  return index_reader::reader_iterator(new iterator_impl());
}

const term_reader* segment_reader::field(const string_ref& name) const {
  return impl_->field(name);
}

field_iterator::ptr segment_reader::fields() const {
  return impl_->fields();
}

uint64_t segment_reader::live_docs_count() const {
  return impl_->live_docs_count();
}

/*static*/ segment_reader segment_reader::open(
  const directory& dir, const segment_meta& meta
) {
  return segment_reader_impl::open(dir, meta);
}

segment_reader segment_reader::reopen(const segment_meta& meta) const {
  // reuse self if no changes to meta
  return impl_->meta_version() == meta.version
    ? *this : segment_reader_impl::open(impl_->dir(), meta);
}

void segment_reader::reset() NOEXCEPT {
  impl_.reset();
}

size_t segment_reader::size() const {
  return impl_->size();
}

columnstore_reader::values_reader_f segment_reader::values(field_id field) const {
  return impl_->values(field);
}

bool segment_reader::visit(
  field_id field, const columnstore_reader::values_visitor_f& reader
) const {
  return impl_->visit(field, reader);
}

// -------------------------------------------------------------------
// segment_reader_impl
// -------------------------------------------------------------------

segment_reader::segment_reader_impl::segment_reader_impl(
  const directory& dir, uint64_t meta_version, uint64_t docs_count
): dir_(dir), docs_count_(docs_count), meta_version_(meta_version) {
}

index_reader::reader_iterator segment_reader::segment_reader_impl::begin() const {
  return index_reader::reader_iterator(new iterator_impl(this));
}

const column_meta* segment_reader::segment_reader_impl::column(
  const string_ref& name
) const {
  auto it = name_to_column_.find(make_hashed_ref(name, std::hash<irs::string_ref>()));
  return it == name_to_column_.end() ? nullptr : it->second;
}

column_iterator::ptr segment_reader::segment_reader_impl::columns() const {
  const auto* begin = columns_.data() - 1;
  return column_iterator::make<iterator_adapter<decltype(begin), column_iterator>>(
    begin, begin + columns_.size()
  );
}

const directory& segment_reader::segment_reader_impl::dir() const NOEXCEPT {
  return dir_;
}

uint64_t segment_reader::segment_reader_impl::docs_count() const {
  return docs_count_;
}

sub_reader::docs_iterator_t::ptr segment_reader::segment_reader_impl::docs_iterator() const {
  // the implementation generates doc_ids sequentially
  return memory::make_unique<masked_docs_iterator>(
    type_limits<type_t::doc_id_t>::min(),
    doc_id_t(type_limits<type_t::doc_id_t>::min() + docs_count_),
    docs_mask_
  );
}

index_reader::reader_iterator segment_reader::segment_reader_impl::end() const {
  return index_reader::reader_iterator(new iterator_impl());
}

const term_reader* segment_reader::segment_reader_impl::field(
  const string_ref& name
) const {
  return field_reader_->field(name);
}

field_iterator::ptr segment_reader::segment_reader_impl::fields() const {
  return field_reader_->iterator();
}

uint64_t segment_reader::segment_reader_impl::live_docs_count() const {
  return docs_count_ - docs_mask_.size();
}

uint64_t segment_reader::segment_reader_impl::meta_version() const NOEXCEPT {
  return meta_version_;
}

/*static*/ segment_reader segment_reader::segment_reader_impl::open(
  const directory& dir, const segment_meta& meta
) {
  PTR_NAMED(segment_reader_impl, reader, dir, meta.version, meta.docs_count);

  index_utils::read_document_mask(reader->docs_mask_, dir, meta);

  auto& codec = *meta.codec;
  auto field_reader = codec.get_field_reader();

  // initialize field reader
  if (!field_reader->prepare(dir, meta, reader->docs_mask_)) {
    return segment_reader(); // i.e. nullptr, field reader required
  }

  reader->field_reader_ = std::move(field_reader);

  auto columnstore_reader = codec.get_columnstore_reader();

  // initialize column reader (if available)
  if (segment_reader::has<irs::columnstore_reader>(meta)
      && columnstore_reader->prepare(dir, meta)) {
    reader->columnstore_reader_ = std::move(columnstore_reader);
  }

  // initialize columns meta
  read_columns_meta(
    codec,
    dir,
    meta,
    reader->columns_,
    reader->id_to_column_,
    reader->name_to_column_
  );

  return reader;
}

size_t segment_reader::segment_reader_impl::size() const {
  return 1; // only 1 segment
}

columnstore_reader::values_reader_f segment_reader::segment_reader_impl::values(
  field_id field
) const {
  if (!columnstore_reader_) {
    // NOOP reader
    return [](doc_id_t, bytes_ref&) { return false; };
  }

  return columnstore_reader_->values(field);
}

bool segment_reader::segment_reader_impl::visit(
  field_id field, const columnstore_reader::values_visitor_f& reader
) const {
  return columnstore_reader_
    ? columnstore_reader_->visit(field, reader) : false;
}

NS_END