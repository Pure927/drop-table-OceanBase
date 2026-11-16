#include "storage/common/drop_table_files.h"
#ifndef _WIN32
#include "storage/common/meta_util.h"
#endif

#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
#define CHECK(expr) do { if (!(expr)) throw std::runtime_error(#expr); } while (false)

static void write(const fs::path &p, const std::string &s = "original")
{
  std::ofstream out(p, std::ios::binary);
  out << s;
  CHECK(out.good());
}
static std::string read(const fs::path &p)
{
  std::ifstream in(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int main()
{
  auto id = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = fs::temp_directory_path() / ("miniob_drop_test_" + std::to_string(id));
  fs::create_directories(root);
  int passed = 0, failed = 0;
  auto test = [&](const char *name, const std::function<void(const fs::path &)> &fn) {
    const auto dir = root / std::to_string(passed + failed);
    fs::create_directory(dir);
    try { fn(dir); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception &e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
  };

  test("all_table_resources", [](const auto &d) {
    std::vector<DropTableFiles::Entry> entries;
    for (const auto &name : {"t.table", "t.data", "t.lob", "t-id.index", "t-name.index", "t.table.tmp"}) {
      write(d / name); entries.push_back({d / name, true});
    }
    DropTableFiles files(entries);
    CHECK(!files.stage());
    for (const auto &e : entries) { CHECK(!fs::exists(e.source)); CHECK(fs::exists(DropTableFiles::tombstone(e.source))); }
    CHECK(!files.commit());
    CHECK(fs::is_empty(d));
  });
  test("optional_files_absent", [](const auto &d) {
    write(d / "t.table"); write(d / "t.data");
    DropTableFiles files({{d / "t.table", true}, {d / "t.data", true}, {d / "t.lob", false}, {d / "t.table.tmp", false}});
    CHECK(!files.stage()); CHECK(!files.commit()); CHECK(fs::is_empty(d));
  });
  test("missing_required_preserves_all_sources", [](const auto &d) {
    write(d / "t.table");
    DropTableFiles files({{d / "t.table", true}, {d / "t.data", true}});
    CHECK(files.stage() == std::errc::no_such_file_or_directory);
    CHECK(read(d / "t.table") == "original"); CHECK(!fs::exists(d / "t.table.drop"));
  });
  test("existing_tombstone_is_not_overwritten", [](const auto &d) {
    write(d / "t.table"); write(d / "t.data"); write(d / "t.data.drop", "retained");
    DropTableFiles files({{d / "t.table", true}, {d / "t.data", true}});
    CHECK(files.stage() == std::errc::file_exists);
    CHECK(read(d / "t.data.drop") == "retained"); CHECK(fs::exists(d / "t.table"));
  });
  test("directories_are_rejected", [](const auto &d) {
    write(d / "t.table"); fs::create_directory(d / "t.data"); write(d / "t.data" / "keep");
    DropTableFiles files({{d / "t.table", true}, {d / "t.data", true}});
    CHECK(files.stage() == std::errc::invalid_argument);
    CHECK(fs::exists(d / "t.data" / "keep")); CHECK(fs::exists(d / "t.table"));
  });
  test("duplicate_manifest_is_rejected", [](const auto &d) {
    write(d / "t.table");
    DropTableFiles files({{d / "t.table", true}, {d / "t.table", true}});
    CHECK(files.stage() == std::errc::invalid_argument); CHECK(fs::exists(d / "t.table"));
  });
  test("commit_requires_successful_stage", [](const auto &d) {
    write(d / "t.table"); DropTableFiles files({{d / "t.table", true}});
    CHECK(files.commit() == std::errc::operation_not_permitted); CHECK(fs::exists(d / "t.table"));
  });
  test("double_stage_is_rejected", [](const auto &d) {
    write(d / "t.table"); DropTableFiles files({{d / "t.table", true}});
    CHECK(!files.stage()); CHECK(files.stage() == std::errc::operation_in_progress); CHECK(!files.commit());
  });
  test("rollback_restores_names_and_contents", [](const auto &d) {
    write(d / "t.table", "schema"); write(d / "t.data", "rows");
    DropTableFiles files({{d / "t.table", true}, {d / "t.data", true}});
    CHECK(!files.stage()); CHECK(!files.rollback());
    CHECK(read(d / "t.table") == "schema"); CHECK(read(d / "t.data") == "rows");
    CHECK(!fs::exists(d / "t.table.drop")); CHECK(!files.stage()); CHECK(!files.commit());
  });
  test("paths_with_spaces", [](const auto &d) {
    fs::create_directory(d / "database files"); const auto p = d / "database files" / "t.table";
    write(p); DropTableFiles files({{p, true}}); CHECK(!files.stage()); CHECK(!files.commit()); CHECK(!fs::exists(p));
  });
  test("drop_then_recreate_has_no_old_rows", [](const auto &d) {
    write(d / "t.table", "schema1"); write(d / "t.data", "old rows");
    DropTableFiles files({{d / "t.table", true}, {d / "t.data", true}});
    CHECK(!files.stage()); CHECK(!files.commit());
    write(d / "t.table", "schema2"); write(d / "t.data", "new rows");
    CHECK(read(d / "t.table") == "schema2"); CHECK(read(d / "t.data") == "new rows");
  });
  test("similar_prefixes_and_database_files_survive", [](const auto &d) {
    write(d / "t.table"); write(d / "t2.table", "neighbor"); write(d / "t2-id.index", "index");
    write(d / "sys.db", "checkpoint"); write(d / "dblwr.db", "shared");
    DropTableFiles files({{d / "t.table", true}}); CHECK(!files.stage()); CHECK(!files.commit());
    CHECK(read(d / "t2.table") == "neighbor"); CHECK(read(d / "t2-id.index") == "index");
    CHECK(read(d / "sys.db") == "checkpoint"); CHECK(read(d / "dblwr.db") == "shared");
  });
  test("failed_unlink_is_reported_and_retryable", [](const auto &d) {
    write(d / "t.table"); write(d / "t.data");
    DropTableFiles files({{d / "t.table", true}, {d / "t.data", true}}); CHECK(!files.stage());
    fs::rename(d / "t.data.drop", d / "saved"); fs::create_directory(d / "t.data.drop");
    CHECK(files.commit() == std::errc::invalid_argument); CHECK(!fs::exists(d / "t.table.drop"));
    CHECK(fs::exists(d / "saved")); fs::remove(d / "t.data.drop"); fs::rename(d / "saved", d / "t.data.drop");
    CHECK(!files.commit()); CHECK(fs::is_empty(d));
  });
  test("rollback_does_not_overwrite_new_source", [](const auto &d) {
    write(d / "t.table", "old"); DropTableFiles files({{d / "t.table", true}}); CHECK(!files.stage());
    write(d / "t.table", "new"); CHECK(files.rollback() == std::errc::file_exists);
    CHECK(read(d / "t.table") == "new"); CHECK(read(d / "t.table.drop") == "old");
    fs::remove(d / "t.table"); CHECK(!files.rollback()); CHECK(read(d / "t.table") == "old");
  });
  test("empty_path_is_rejected", [](const auto &) {
    DropTableFiles files({{fs::path(), true}}); CHECK(files.stage() == std::errc::invalid_argument);
  });
#ifndef _WIN32
  test("official_filename_helpers_match_manifest", [](const auto &d) {
    const auto base = d.string();
    CHECK(fs::path(table_meta_file(base.c_str(), "t")) == d / "t.table");
    CHECK(fs::path(table_data_file(base.c_str(), "t")) == d / "t.data");
    CHECK(fs::path(table_lob_file(base.c_str(), "t")) == d / "t.lob");
    CHECK(fs::path(table_index_file(base.c_str(), "t", "id")) == d / "t-id.index");
  });
#else
  std::cout << "SKIP official_filename_helpers_match_manifest (upstream Linux-only headers)\n";
#endif
  std::cout << "RESULT " << passed << " passed, " << failed << " failed\n";
  // Only the unique temporary directory created by this test is removed.
  fs::remove_all(root);
  return failed ? 1 : 0;
}
