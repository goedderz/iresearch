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

#include "index/index_meta.hpp"
#include "formats/formats.hpp"
#include "utils/attributes.hpp"
#include "utils/log.hpp"

#include "directory_utils.hpp"

NS_ROOT
NS_BEGIN(directory_utils)

// return a reference to a file or empty() if not found
index_file_refs::ref_t reference(
    directory& dir, 
    const std::string& name,
    bool include_missing /*= false*/) {
  if (include_missing) {
    return dir.attributes().add<index_file_refs>()->add(name);
  }

  bool exists;

  // do not add an attribute if the file definitly does not exist
  if (!dir.exists(exists, name) || !exists) {
    return nullptr;
  }

  auto ref = dir.attributes().add<index_file_refs>()->add(name);

  return dir.exists(exists, name) && exists
    ? ref : index_file_refs::ref_t(nullptr);
}

#if defined(_MSC_VER)
  #pragma warning(disable : 4706)
#elif defined (__GNUC__)
  #pragma GCC diagnostic ignored "-Wparentheses"
#endif

// return success, visitor gets passed references to files retrieved from source
bool reference(
    directory& dir,
    const std::function<const std::string*()>& source,
    const std::function<bool(index_file_refs::ref_t&& ref)>& visitor,
    bool include_missing /*= false*/
) {
  auto& attribute = dir.attributes().add<index_file_refs>();

  for (const std::string* file; file = source();) {
    if (include_missing) {
      if (!visitor(attribute->add(*file))) {
        return false;
      }

      continue;
    }

    bool exists;

    // do not add an attribute if the file definitly does not exist
    if (!dir.exists(exists, *file) || !exists) {
      continue;
    }

    auto ref = attribute->add(*file);

    if (dir.exists(exists, *file) && exists && !visitor(std::move(ref))) {
      return false;
    }
  }

  return true;
}

#if defined(_MSC_VER)
  #pragma warning(default : 4706)
#elif defined (__GNUC__)
  #pragma GCC diagnostic pop
#endif

// return success, visitor gets passed references to files registered with index_meta
bool reference(
    directory& dir,
    const index_meta& meta,
    const std::function<bool(index_file_refs::ref_t&& ref)>& visitor,
    bool include_missing /*= false*/
) {
  if (meta.empty()) {
    return true;
  }

  auto& attribute = dir.attributes().add<index_file_refs>();

  return meta.visit_files([include_missing, &attribute, &dir, &visitor](const std::string& file) {
    if (include_missing) {
      return visitor(attribute->add(file));
    }

    bool exists;

    // do not add an attribute if the file definitly does not exist
    if (!dir.exists(exists, file) || !exists) {
      return true;
    }

    auto ref = attribute->add(file);

    if (dir.exists(exists, file) && exists) {
      return visitor(std::move(ref));
    }

    return true;
  });
}

// return success, visitor gets passed references to files registered with segment_meta
bool reference(
  directory& dir,
  const segment_meta& meta,
  const std::function<bool(index_file_refs::ref_t&& ref)>& visitor,
  bool include_missing /*= false*/
) {
  auto files = meta.files;

  if (files.empty()) {
    return true;
  }

  auto& attribute = dir.attributes().add<index_file_refs>();

  for (auto& file: files) {
    if (include_missing) {
      if (!visitor(attribute->add(file))) {
        return false;
      }

      continue;
    }

    bool exists;

    // do not add an attribute if the file definitly does not exist
    if (!dir.exists(exists, file) || !exists) {
      continue;
    }

    auto ref = attribute->add(file);

    if (dir.exists(exists, file) && exists && !visitor(std::move(ref))) {
      return false;
    }
  }

  return true;
}

void remove_all_unreferenced(directory& dir) {
  auto& attribute = dir.attributes().add<index_file_refs>();

  dir.visit([&attribute] (std::string& name) {
    attribute->add(std::move(name)); // ensure all files in dir are tracked
    return true;
  });

  directory_cleaner::clean(dir);
}

directory_cleaner::removal_acceptor_t remove_except_current_segments(
  const directory& dir, format& codec
) {
  static const auto acceptor = [](
      const std::string& filename, 
      const std::unordered_set<std::string>& retain) {
    return retain.find(filename) == retain.end();
  };

  index_meta meta;
  auto reader = codec.get_index_meta_reader();

  std::string segment_file;
  const bool index_exists = reader->last_segments_file(dir, segment_file);

  if (!index_exists) {
    // can't find segments file
    return [](const std::string&)->bool { return true; };
  }

  reader->read(dir, meta, segment_file);

  std::unordered_set<std::string> retain;
  retain.reserve(meta.size());

  meta.visit_files([&retain] (std::string& file) {
    retain.emplace(std::move(file));
    return true;
  });

  retain.emplace(std::move(segment_file));

  return std::bind(acceptor, std::placeholders::_1, std::move(retain));
}

NS_END

// -----------------------------------------------------------------------------
// --SECTION--                                                tracking_directory
// -----------------------------------------------------------------------------

tracking_directory::tracking_directory(
  directory& impl, bool track_open /*= false*/):
  impl_(impl), track_open_(track_open) {
}

tracking_directory::~tracking_directory() {}

directory& tracking_directory::operator*() {
  return impl_;
}

attributes& tracking_directory::attributes() NOEXCEPT {
  return impl_.attributes();
}

void tracking_directory::close() NOEXCEPT {
  impl_.close();
}

index_output::ptr tracking_directory::create(
  const std::string& name
) NOEXCEPT {
  try {
    files_.emplace(name);

    return impl_.create(name);
  } catch (...) {
    IR_ERROR() << "Expcetion caught in " << __FUNCTION__ << ":" << __LINE__;
  }

  try {
    files_.erase(name); // revert change
  } catch (...) {
    IR_ERROR() << "Expcetion caught in " << __FUNCTION__ << ":" << __LINE__;
  }

  return nullptr;
}

bool tracking_directory::exists(
  bool& result, const std::string& name
) const NOEXCEPT {
  return impl_.exists(result, name);
}

bool tracking_directory::length(
  uint64_t& result, const std::string& name
) const NOEXCEPT {
  return impl_.length(result, name);
}

bool tracking_directory::visit(const directory::visitor_f& visitor) const {
  return impl_.visit(visitor);
}

index_lock::ptr tracking_directory::make_lock(
  const std::string& name
) NOEXCEPT {
  return impl_.make_lock(name);
}

bool tracking_directory::mtime(
  std::time_t& result, const std::string& name
) const NOEXCEPT {
  return impl_.mtime(result, name);
}

index_input::ptr tracking_directory::open(
  const std::string& name
) const NOEXCEPT {
  if (track_open_) {
    try {
      files_.emplace(name);
    } catch (...) {
      IR_ERROR() << "Expcetion caught in " << __FUNCTION__ << ":" << __LINE__;

      return nullptr;
    }
  }

  return impl_.open(name);
}

bool tracking_directory::remove(const std::string& name) NOEXCEPT {
  bool result = impl_.remove(name);

  try {
    files_.erase(name);
  } catch (...) {
    IR_ERROR() << "Expcetion caught in " << __FUNCTION__ << ":" << __LINE__;
    // ignore failure since removal from impl_ was sucessful
  }

  return result;
}

bool tracking_directory::rename(
  const std::string& src, const std::string& dst
) NOEXCEPT {
  if (!impl_.rename(src, dst)) {
    return false;
  }

  try {
    if (files_.emplace(dst).second) {
      files_.erase(src);
    }

    return true;
  } catch (...) {
    IR_ERROR() << "Expcetion caught in " << __FUNCTION__ << ":" << __LINE__;
    impl_.rename(dst, src); // revert
  }

  return false;
}

bool tracking_directory::swap_tracked(file_set& other) {
  try {
    files_.swap(other);

    return true;
  } catch (...) { // may throw exceptions until C++17
    IR_ERROR() << "Expcetion caught in " << __FUNCTION__ << ":" << __LINE__;
  }

  return false;
}

bool tracking_directory::swap_tracked(tracking_directory& other) {
  return swap_tracked(other.files_);
}

bool tracking_directory::sync(const std::string& name) NOEXCEPT {
  return impl_.sync(name);
}

// -----------------------------------------------------------------------------
// --SECTION--                                            ref_tracking_directory
// -----------------------------------------------------------------------------

ref_tracking_directory::ref_tracking_directory(
  directory& impl, bool track_open /*= false*/
):
  attribute_(impl.attributes().add<index_file_refs>()),
  impl_(impl),
  track_open_(track_open) {
}

ref_tracking_directory::ref_tracking_directory(ref_tracking_directory&& other):
  attribute_(other.attribute_), // references do not require std::move(...)
  impl_(other.impl_), // references do not require std::move(...)
  refs_(std::move(other.refs_)),
  track_open_(std::move(other.track_open_)) {
}

ref_tracking_directory::~ref_tracking_directory() {}

directory& ref_tracking_directory::operator*() {
  return impl_;
}

attributes& ref_tracking_directory::attributes() NOEXCEPT {
  return impl_.attributes();
}

void ref_tracking_directory::clear_refs() const NOEXCEPT {
  SCOPED_LOCK(mutex_);
  refs_.clear();
}

void ref_tracking_directory::close() NOEXCEPT {
  impl_.close();
}

index_output::ptr ref_tracking_directory::create(
  const std::string& name
) NOEXCEPT {
  try {
    auto ref = attribute_->add(name);
    SCOPED_LOCK(mutex_);
    auto result = impl_.create(name);

    // only track ref on successful call to impl_
    if (result) {
      refs_.emplace(*ref, std::move(ref));
    }

    return result;
  } catch (...) {
    IR_ERROR() << "Expcetion caught in " << __FUNCTION__ << ":" << __LINE__;
  }

  return nullptr;
}

bool ref_tracking_directory::exists(
  bool& result, const std::string& name
) const NOEXCEPT {
  return impl_.exists(result, name);
}

bool ref_tracking_directory::length(
  uint64_t& result, const std::string& name
) const NOEXCEPT {
  return impl_.length(result, name);
}

index_lock::ptr ref_tracking_directory::make_lock(
  const std::string& name
) NOEXCEPT {
  return impl_.make_lock(name);
}

bool ref_tracking_directory::mtime(
  std::time_t& result, const std::string& name
) const NOEXCEPT {
  return impl_.mtime(result, name);
}

index_input::ptr ref_tracking_directory::open(
  const std::string& name
) const NOEXCEPT {
  if (!track_open_) {
    return impl_.open(name);
  }

  auto result = impl_.open(name);

  // only track ref on successful call to impl_
  if (result) {
    try {
      auto ref = attribute_->add(name);
      SCOPED_LOCK(mutex_);

      refs_.emplace(*ref, std::move(ref));
    } catch (...) {
      IR_ERROR() << "Expcetion caught in " << __FUNCTION__ << ":" << __LINE__;

      return nullptr;
    }
  }

  return result;
}

bool ref_tracking_directory::remove(const std::string& name) NOEXCEPT {
  bool result = impl_.remove(name);

  try {
    SCOPED_LOCK(mutex_);

    refs_.erase(name);
  } catch (...) {
    IR_ERROR() << "Expcetion caught in " << __FUNCTION__ << ":" << __LINE__;
    // ignore failure since removal from impl_ was sucessful
  }

  return result;
}

bool ref_tracking_directory::rename(
  const std::string& src, const std::string& dst
) NOEXCEPT {
  if (!impl_.rename(src, dst)) {
    return false;
  }

  try {
    SCOPED_LOCK(mutex_);

    if (refs_.emplace(dst, attribute_->add(dst)).second) {
      refs_.erase(src);
    }

    return true;
  } catch (...) {
    IR_ERROR() << "Expcetion caught in " << __FUNCTION__ << ":" << __LINE__;
    impl_.rename(dst, src); // revert
  }

  return false;
}

bool ref_tracking_directory::sync(const std::string& name) NOEXCEPT {
  return impl_.sync(name);
}

bool ref_tracking_directory::visit(const visitor_f& visitor) const {
  return impl_.visit(visitor);
}

bool ref_tracking_directory::visit_refs(
  const std::function<bool(const index_file_refs::ref_t& ref)>& visitor
) const {
  SCOPED_LOCK(mutex_);

  for (const auto& ref: refs_) {
    if (!visitor(ref.second)) {
      return false;
    }
  }

  return true;
}

NS_END