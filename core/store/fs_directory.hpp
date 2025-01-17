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

#ifndef IRESEARCH_FILE_SYSTEM_DIRECTORY_H
#define IRESEARCH_FILE_SYSTEM_DIRECTORY_H

#include "store/directory.hpp"
#include "store/directory_attributes.hpp"
#include "utils/string.hpp"
#include "utils/utf8_path.hpp"

namespace iresearch {

//////////////////////////////////////////////////////////////////////////////
/// @class fs_directory
//////////////////////////////////////////////////////////////////////////////
class fs_directory : public directory {
 public:
  static constexpr size_t DEFAULT_POOL_SIZE = 8;

  explicit fs_directory(
    utf8_path dir,
    directory_attributes attrs = directory_attributes{},
    size_t fd_pool_size = DEFAULT_POOL_SIZE);

  using directory::attributes;
  virtual directory_attributes& attributes() noexcept override {
    return attrs_;
  }

  virtual index_output::ptr create(std::string_view name) noexcept override;

  const utf8_path& directory() const noexcept;

  virtual bool exists(
    bool& result,
    std::string_view name) const noexcept override;

  virtual bool length(
    uint64_t& result,
    std::string_view name) const noexcept override;

  virtual index_lock::ptr make_lock(
    std::string_view name) noexcept override;

  virtual bool mtime(
    std::time_t& result,
    std::string_view name) const noexcept override;

  virtual index_input::ptr open(
    std::string_view name,
    IOAdvice advice) const noexcept override;

  virtual bool remove(
    std::string_view name) noexcept override;

  virtual bool rename(
    std::string_view src,
    std::string_view dst) noexcept override;

  virtual bool sync(std::string_view name) noexcept override;

  virtual bool visit(const visitor_f& visitor) const override;

 private:
  directory_attributes attrs_;
  utf8_path dir_;
  size_t fd_pool_size_;
}; // fs_directory

}

#endif
