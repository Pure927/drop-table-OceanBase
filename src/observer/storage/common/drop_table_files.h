/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

/** Exact manifest of table-owned files. No directory scan or wildcard deletion.
 * stage() renames files in their own directory; a failed rename rolls back.
 * Once the table is closed, commit() unlinks staged files. A failed unlink is
 * reported; remaining .drop files are intentionally retained for diagnosis.
 * This is not a crash-atomic DDL protocol and requires external serialization.
 */
class DropTableFiles
{
public:
  struct Entry {
    std::filesystem::path source;
    bool required;
  };

  explicit DropTableFiles(std::vector<Entry> entries) : entries_(std::move(entries)) {}

  std::error_code stage()
  {
    namespace fs = std::filesystem;
    if (staged_ || !moved_.empty()) {
      return std::make_error_code(std::errc::operation_in_progress);
    }
    std::vector<fs::path> present;
    // Validate the entire manifest before the first rename.
    for (const Entry &entry : entries_) {
      if (entry.source.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
      }
      for (const fs::path &path : present) {
        if (path == entry.source) {
          return std::make_error_code(std::errc::invalid_argument);
        }
      }
      std::error_code ec;
      auto status = fs::symlink_status(entry.source, ec);
      if (ec == std::errc::no_such_file_or_directory || (!ec && !fs::exists(status))) {
        if (entry.required) {
          return std::make_error_code(std::errc::no_such_file_or_directory);
        }
        continue;
      }
      if (ec) {
        return ec;
      }
      if (!fs::is_regular_file(status)) {
        return std::make_error_code(std::errc::invalid_argument);
      }
      auto target = tombstone(entry.source);
      auto target_status = fs::symlink_status(target, ec);
      if (ec && ec != std::errc::no_such_file_or_directory) {
        return ec;
      }
      if (fs::exists(target_status)) {
        return std::make_error_code(std::errc::file_exists);
      }
      present.push_back(entry.source);
    }
    for (const fs::path &source : present) {
      std::error_code ec;
      fs::rename(source, tombstone(source), ec);
      if (ec) {
        auto rollback_error = rollback();
        return rollback_error ? rollback_error : ec;
      }
      moved_.push_back(source);
    }
    staged_ = true;
    return {};
  }

  std::error_code rollback()
  {
    std::error_code first_error;
    std::vector<std::filesystem::path> remaining;
    for (auto it = moved_.rbegin(); it != moved_.rend(); ++it) {
      std::error_code ec;
      // Never overwrite a file that appeared since stage().
      auto status = std::filesystem::symlink_status(*it, ec);
      if (ec == std::errc::no_such_file_or_directory) {
        ec.clear();
      } else if (!ec && std::filesystem::exists(status)) {
        ec = std::make_error_code(std::errc::file_exists);
      }
      if (!ec) {
        std::filesystem::rename(tombstone(*it), *it, ec);
      }
      if (ec) {
        if (!first_error) first_error = ec;
        remaining.push_back(*it);
      }
    }
    moved_ = std::move(remaining);
    staged_ = !moved_.empty();
    return first_error;
  }

  std::error_code commit()
  {
    if (!staged_) {
      return std::make_error_code(std::errc::operation_not_permitted);
    }
    std::error_code first_error;
    std::vector<std::filesystem::path> remaining;
    for (const auto &source : moved_) {
      std::error_code ec;
      auto path = tombstone(source);
      auto status = std::filesystem::symlink_status(path, ec);
      if (ec == std::errc::no_such_file_or_directory) {
        ec.clear();
      } else if (!ec && !std::filesystem::is_regular_file(status)) {
        ec = std::make_error_code(std::errc::invalid_argument);
      } else if (!ec) {
        std::filesystem::remove(path, ec);
      }
      if (ec) {
        if (!first_error) first_error = ec;
        remaining.push_back(source);
      }
    }
    moved_ = std::move(remaining);
    staged_ = !moved_.empty();
    return first_error;
  }

  static std::filesystem::path tombstone(std::filesystem::path path)
  {
    path += ".drop";
    return path;
  }

private:
  std::vector<Entry> entries_;
  std::vector<std::filesystem::path> moved_;
  bool staged_ = false;
};
