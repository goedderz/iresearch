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
/// @author Vasiliy Nabatchikov
////////////////////////////////////////////////////////////////////////////////

#include "fs_directory.hpp"

#include "shared.hpp"
#include "store/directory_attributes.hpp"
#include "store/directory_cleaner.hpp"
#include "error/error.hpp"
#include "utils/log.hpp"
#include "utils/object_pool.hpp"
#include "utils/string_utils.hpp"
#include "utils/file_utils.hpp"
#include "utils/crc.hpp"

#ifdef _WIN32
#include <Windows.h> // for GetLastError()
#endif

namespace {

//////////////////////////////////////////////////////////////////////////////
/// @brief converts the specified IOAdvice to corresponding posix fadvice
//////////////////////////////////////////////////////////////////////////////
inline int get_posix_fadvice(irs::IOAdvice advice) noexcept {
  switch (advice) {
    case irs::IOAdvice::NORMAL:
      return IR_FADVICE_NORMAL;
    case irs::IOAdvice::SEQUENTIAL:
      return IR_FADVICE_SEQUENTIAL;
    case irs::IOAdvice::RANDOM:
      return IR_FADVICE_RANDOM;
    case irs::IOAdvice::READONCE:
      return IR_FADVICE_DONTNEED;
    case irs::IOAdvice::READONCE_SEQUENTIAL:
      return IR_FADVICE_SEQUENTIAL | IR_FADVICE_NOREUSE;
    case irs::IOAdvice::READONCE_RANDOM:
      return IR_FADVICE_RANDOM | IR_FADVICE_NOREUSE;
  }

  IR_FRMT_ERROR(
    "fadvice '%d' is not valid (RANDOM|SEQUENTIAL), fallback to NORMAL",
    uint32_t(advice));

  return IR_FADVICE_NORMAL;
}

}

namespace iresearch {
MSVC_ONLY(__pragma(warning(push)))
MSVC_ONLY(__pragma(warning(disable: 4996))) // the compiler encountered a deprecated declaration

//////////////////////////////////////////////////////////////////////////////
/// @class fs_lock
//////////////////////////////////////////////////////////////////////////////

class fs_lock : public index_lock {
 public:
  fs_lock(const irs::utf8_path& dir, std::string_view file)
    : dir_{dir}, file_{file} {
  }

  virtual bool lock() override {
    if (handle_) {
      // don't allow self obtaining
      return false;
    }

    bool exists;


    if (!file_utils::exists(exists, dir_.c_str())) {
      IR_FRMT_ERROR("Error caught in: %s", __FUNCTION__);
      return false;
    } 

    // create directory if it is not exists
    if (!exists && !file_utils::mkdir(dir_.c_str(), true)) {
      IR_FRMT_ERROR("Error caught in: %s", __FUNCTION__);
      return false;
    }

    const auto path = dir_ / file_;

    // create lock file
    if (!file_utils::verify_lock_file(path.c_str())) {
      if (!file_utils::exists(exists, path.c_str()) || (exists && !file_utils::remove(path.c_str()))) {
        IR_FRMT_ERROR("Error caught in: %s", __FUNCTION__);
        return false;
      }

      handle_ = file_utils::create_lock_file(path.c_str());
    }

    return handle_ != nullptr;
  }

  virtual bool is_locked(bool& result) const noexcept override {
    if (handle_ != nullptr) {
      result = true;
      return true;
    }

    try {
      const auto path = dir_ / file_;

      result = file_utils::verify_lock_file(path.c_str());
      return true;
    } catch (...) {
    }

    return false;
  }

  virtual bool unlock() noexcept override {
    if (handle_ != nullptr) {
      handle_ = nullptr;
#ifdef _WIN32
      // file will be automatically removed on close
      return true;
#else
      const auto path = dir_ / file_;

      return file_utils::remove(path.c_str());
#endif
    }

    return false;
  }

 private:
  irs::utf8_path dir_;
  std::string file_;
  file_utils::lock_handle_t handle_;
}; // fs_lock

//////////////////////////////////////////////////////////////////////////////
/// @class fs_index_output
//////////////////////////////////////////////////////////////////////////////
class fs_index_output : public buffered_index_output {
 public:
  DEFINE_FACTORY_INLINE(index_output) // cppcheck-suppress unknownMacro

  static index_output::ptr open(const file_path_t name) noexcept {
    assert(name);

    file_utils::handle_t handle(file_utils::open(name,
                                                 file_utils::OpenMode::Write,
                                                 IR_FADVICE_NORMAL));

    if (nullptr == handle) {
#ifdef _WIN32
      IR_FRMT_ERROR("Failed to open output file, error: %d, path: %s",
                    GetLastError(), irs::utf8_path{name}.c_str());
#else
      IR_FRMT_ERROR("Failed to open output file, error: %d, path: %s",
                    errno, irs::utf8_path{name}.c_str());
#endif

      return nullptr;
    }

    try {
      return fs_index_output::make<fs_index_output>(std::move(handle));
    } catch(...) {
    }

    return nullptr;
  }

  virtual void close() override {
    buffered_index_output::close();
    handle.reset(nullptr);
  }

  virtual int64_t checksum() const override {
    const_cast<fs_index_output*>(this)->flush();
    return crc.checksum();
  }

 protected:
  virtual void flush_buffer(const byte_type* b, size_t len) override {
    assert(handle);

    const auto len_written = irs::file_utils::fwrite(handle.get(), b, sizeof(byte_type) * len);
    crc.process_bytes(b, len_written);

    if (len && len_written != len) {
      throw io_error(string_utils::to_string(
        "failed to write buffer, written '" IR_SIZE_T_SPECIFIER "' out of '" IR_SIZE_T_SPECIFIER "' bytes",
        len_written, len));
    }
  }

 private:
  fs_index_output(file_utils::handle_t&& handle) noexcept
    : handle(std::move(handle)) {
    buffered_index_output::reset(buf_, sizeof buf_);
  }

  byte_type buf_[1024];
  file_utils::handle_t handle;
  crc32c crc;
}; // fs_index_output

//////////////////////////////////////////////////////////////////////////////
/// @class fs_index_input
//////////////////////////////////////////////////////////////////////////////
class pooled_fs_index_input; // predeclaration used by fs_index_input
class fs_index_input : public buffered_index_input {
 public:
  using buffered_index_input::read_internal;

  virtual int64_t checksum(size_t offset) const override final {
    // "read_internal" modifies pos_
    auto restore_position = make_finally(
        [pos = this->pos_, this]() noexcept {
      const_cast<fs_index_input*>(this)->pos_ = pos;
    });

    const auto begin = pos_;
    const auto end = (std::min)(begin + offset, handle_->size);

    crc32c crc;
    byte_type buf[sizeof buf_];

    for (auto pos = begin; pos < end; ) {
      const auto to_read = (std::min)(end - pos, sizeof buf);
      pos += const_cast<fs_index_input*>(this)->read_internal(buf, to_read);
      crc.process_bytes(buf, to_read);
    }

    return crc.checksum();
  }

  virtual ptr dup() const override {
    return ptr(new fs_index_input(*this));
  }

  static index_input::ptr open(
      const file_path_t name, size_t pool_size, IOAdvice advice) noexcept {
    assert(name);

    auto handle = file_handle::make();
    handle->posix_open_advice = get_posix_fadvice(advice);
    handle->handle = irs::file_utils::open(name, irs::file_utils::OpenMode::Read, handle->posix_open_advice);

    if (nullptr == handle->handle) {
#ifdef _WIN32
      IR_FRMT_ERROR("Failed to open input file, error: %d, path: %s",
                    GetLastError(), irs::utf8_path{name}.c_str());
#else
      IR_FRMT_ERROR("Failed to open input file, error: %d, path: %s",
                    errno, irs::utf8_path{name}.c_str());
#endif

      return nullptr;
    }

    uint64_t size;
    if (!file_utils::byte_size(size, *handle)) {
      #ifdef _WIN32
        auto error = GetLastError();
      #else
        auto error = errno;
      #endif

      IR_FRMT_ERROR("Failed to get stat for input file, error: %d, path: %s",
                    error, irs::utf8_path{name}.c_str());

      return nullptr;
    }

    handle->size = size;

    try {
      return ptr(new fs_index_input(
        std::move(handle),
        pool_size));
    } catch(...) {
    }

    return nullptr;
  }

  virtual size_t length() const override {
    return handle_->size;
  }

  virtual ptr reopen() const override;

 protected:
  virtual void seek_internal(size_t pos) override final {
    if (pos > handle_->size) {
      throw io_error(string_utils::to_string(
        "seek out of range for input file, length '" IR_SIZE_T_SPECIFIER "', position '" IR_SIZE_T_SPECIFIER "'",
        handle_->size, pos));
    }

    pos_ = pos;
  }

  virtual size_t read_internal(byte_type* b, size_t len) override final {
    assert(b);
    assert(handle_->handle);

    void* fd = *handle_;

    if (handle_->pos != pos_) {
      if (irs::file_utils::fseek(fd, static_cast<long>(pos_), SEEK_SET) != 0) {
        throw io_error(string_utils::to_string(
          "failed to seek to '" IR_SIZE_T_SPECIFIER "' for input file, error '%d'",
          pos_, irs::file_utils::ferror(fd)));
      }
      handle_->pos = pos_;
    }

    const size_t read = irs::file_utils::fread(fd, b, sizeof(byte_type) * len);
    pos_ = handle_->pos += read;

    assert(handle_->pos == pos_);
    return read;
  }

 private:
  friend pooled_fs_index_input;

  /* use shared wrapper here since we don't want to
  * call "ftell" every time we need to know current 
  * position */
  struct file_handle {
    using ptr = std::shared_ptr<file_handle>;
    static ptr make();

    operator void*() const { return handle.get(); }

    file_utils::handle_t handle; /* native file handle */
    size_t size{}; /* file size */
    size_t pos{}; /* current file position*/
    int posix_open_advice{ IR_FADVICE_NORMAL };
  }; // file_handle

  fs_index_input(
      file_handle::ptr&& handle,
      size_t pool_size) noexcept
    : handle_(std::move(handle)),
      pool_size_(pool_size),
      pos_(0) {
    assert(handle_);
    buffered_index_input::reset(buf_, sizeof buf_, 0);
  }

  fs_index_input(const fs_index_input& rhs) noexcept
    : handle_(rhs.handle_),
      pool_size_(rhs.pool_size_),
      pos_(rhs.file_pointer()) {
    buffered_index_input::reset(buf_, sizeof buf_, pos_);
  }

  fs_index_input& operator=(const fs_index_input&) = delete;

  byte_type buf_[1024];
  file_handle::ptr handle_; // shared file handle
  size_t pool_size_; // size of pool for instances of pooled_fs_index_input
  size_t pos_; // current input stream position
}; // fs_index_input

DEFINE_FACTORY_DEFAULT(fs_index_input::file_handle)

class pooled_fs_index_input final : public fs_index_input {
 public:
  explicit pooled_fs_index_input(const fs_index_input& in);
  virtual ~pooled_fs_index_input() noexcept;
  virtual index_input::ptr dup() const override {
    return ptr(new pooled_fs_index_input(*this));
  }
  virtual index_input::ptr reopen() const override;

 private:
  struct builder {
    using ptr = std::unique_ptr<file_handle>;

    static std::unique_ptr<file_handle> make() {
      return memory::make_unique<file_handle>();
    }
  };

  using fd_pool_t = unbounded_object_pool<builder>;
  std::shared_ptr<fd_pool_t> fd_pool_;

  pooled_fs_index_input(const pooled_fs_index_input& in) = default;
  file_handle::ptr reopen(const file_handle& src) const;
}; // pooled_fs_index_input

index_input::ptr fs_index_input::reopen() const {
  return memory::make_unique<pooled_fs_index_input>(*this);
}

pooled_fs_index_input::pooled_fs_index_input(const fs_index_input& in)
  : fs_index_input(in),
    fd_pool_(memory::make_shared<fd_pool_t>(pool_size_)) {
  handle_ = reopen(*handle_);
}

pooled_fs_index_input::~pooled_fs_index_input() noexcept {
  handle_.reset(); // release handle before the fs_pool_ is deallocated
}

index_input::ptr pooled_fs_index_input::reopen() const {
  auto ptr = dup();
  assert(ptr);

  auto& in = static_cast<pooled_fs_index_input&>(*ptr);
  in.handle_ = reopen(*handle_); // reserve a new handle from pool
  assert(in.handle_ && in.handle_->handle);

  return ptr;
}

fs_index_input::file_handle::ptr pooled_fs_index_input::reopen(const file_handle& src) const {
  // reserve a new handle from the pool
  auto handle = const_cast<pooled_fs_index_input*>(this)->fd_pool_->emplace().release();

  if (!handle->handle) {
    handle->handle = irs::file_utils::open(src, irs::file_utils::OpenMode::Read, src.posix_open_advice); // same permission as in fs_index_input::open(...)

    if (!handle->handle) {
      // even win32 uses 'errno' for error codes in calls to file_open(...)
      throw io_error(string_utils::to_string(
        "Failed to reopen input file, error: %d",
#ifdef _WIN32
        GetLastError()
#else
        errno
#endif
      ));
    }
    handle->posix_open_advice = src.posix_open_advice;
  }

  const auto pos = irs::file_utils::ftell(handle->handle.get()); // match position of file descriptor

  if (pos < 0) {
    throw io_error(string_utils::to_string(
      "Failed to obtain current position of input file, error: %d",
#ifdef _WIN32
      GetLastError()
#else
      errno
#endif
    ));
  }

  handle->pos = pos;
  handle->size = src.size;

  return handle;
}

// -----------------------------------------------------------------------------
// --SECTION--                                       fs_directory implementation
// -----------------------------------------------------------------------------

fs_directory::fs_directory(
    irs::utf8_path dir,
    directory_attributes attrs,
    size_t fd_pool_size)
  : attrs_{std::move(attrs)},
    dir_{std::move(dir)},
    fd_pool_size_{fd_pool_size} {
}

index_output::ptr fs_directory::create(std::string_view name) noexcept {
  try {
    irs::utf8_path path{dir_};
    path /= name;

    auto out = fs_index_output::open(path.c_str());

    if (!out) {
      IR_FRMT_ERROR("Failed to open output file, path: %s", std::string{name}.c_str());
    }

    return out;
  } catch(...) {
  }

  return nullptr;
}

const irs::utf8_path& fs_directory::directory() const noexcept {
  return dir_;
}

bool fs_directory::exists(
    bool& result, std::string_view name) const noexcept {
  auto path = dir_;
  path /= name;

  return file_utils::exists(result, path.c_str());
}

bool fs_directory::length(
    uint64_t& result, std::string_view name) const noexcept {
  auto path = dir_;
  path /= name;

  return file_utils::byte_size(result, path.c_str());
}

index_lock::ptr fs_directory::make_lock(std::string_view name) noexcept {
  return index_lock::make<fs_lock>(dir_, name);
}

bool fs_directory::mtime(
    std::time_t& result,
    std::string_view name) const noexcept {
  auto path = dir_;
  path /= name;

  return file_utils::mtime(result, path.c_str());
}

bool fs_directory::remove(std::string_view name) noexcept {
  try {
    auto path = dir_;
    path /= name;

    return file_utils::remove(path.c_str());
  } catch (...) {
  }

  return false;
}

index_input::ptr fs_directory::open(
    std::string_view name,
    IOAdvice advice) const noexcept {
  try {
    auto path = dir_;
    path /= name;

    return fs_index_input::open(path.c_str(), fd_pool_size_, advice);
  } catch(...) {
  }

  return nullptr;
}

bool fs_directory::rename(
    std::string_view src,
    std::string_view dst) noexcept {
  try {
    auto src_path = dir_;
    src_path /= src;

    auto dst_path = dir_;
    dst_path /= dst;

    return file_utils::move(src_path.c_str(), dst_path.c_str());
  } catch (...) {
  }

  return false;
}

bool fs_directory::visit(const directory::visitor_f& visitor) const {
  bool exists;

  if (!file_utils::exists_directory(exists, dir_.c_str()) || !exists) {
    return false;
  }

#ifdef _WIN32
  irs::utf8_path path;
  auto dir_visitor = [&path, &visitor](const file_path_t name) {
    path = name;

    auto filename = path.string();
    return visitor(filename);
  };
#else
  std::string filename;
  auto dir_visitor = [&filename, &visitor] (const file_path_t name) {
    filename.assign(name);
    return visitor(filename);
  };
#endif

  return file_utils::visit_directory(dir_.c_str(), dir_visitor, false);
}

bool fs_directory::sync(std::string_view name) noexcept {
  try {
    auto path = dir_;
    path /= name;

    if (file_utils::file_sync(path.c_str())) {
      return true;
    }

    #ifdef _WIN32
      auto error = GetLastError();
    #else
      auto error = errno;
    #endif

    IR_FRMT_ERROR("Failed to sync file, error: %d, path: %s", error, path.u8string().c_str());
  } catch (...) {
    IR_FRMT_ERROR("Failed to sync file, name : %s", std::string{name}.c_str());
  }

  return false;
}

MSVC_ONLY(__pragma(warning(pop)))

}
