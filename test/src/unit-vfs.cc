/**
 * @file   unit-vfs.cc
 *
 * @section LICENSE
 *
 * The MIT License
 *
 * @copyright Copyright (c) 2018-2023 TileDB, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * @section DESCRIPTION
 *
 * Tests the `VFS` class.
 */

#include <test/support/tdb_catch.h>
#include <atomic>
#include "test/support/src/helpers.h"
#include "tiledb/sm/filesystem/vfs.h"
#ifdef _WIN32
#include "tiledb/sm/filesystem/path_win.h"
#endif
#include "test/support/src/vfs_helpers.h"
#include "tiledb/sm/tile/tile.h"

using namespace tiledb::common;
using namespace tiledb::sm;
using namespace tiledb::test;

#ifdef _WIN32

TEST_CASE("VFS: Test long paths (Win32)", "[vfs][windows]") {
  ThreadPool compute_tp(4);
  ThreadPool io_tp(4);
  VFS vfs_long_path_win{&g_helper_stats, &compute_tp, &io_tp, Config{}};
  std::string tmpdir_base = tiledb::sm::Win::current_dir() + "\\tiledb_test\\";
  REQUIRE(vfs_long_path_win.create_dir(URI(tmpdir_base)).ok());

  SECTION("- Deep hierarchy") {
    // On some Windows platforms, the path length of a directory must be <= 248
    // chars. On others (that have opted in to a configuration that allows
    // long paths) the limit is ~32,767. Here we check for either case.
    std::string tmpdir = tmpdir_base;
    bool success = true;
    while (tmpdir.size() < 512) {
      tmpdir += "subdir\\";
      success &= vfs_long_path_win.create_dir(URI(tmpdir)).ok();
    }

    if (success) {
      // Check we can create files within the deep hierarchy
      URI testfile(tmpdir + "file.txt");
      REQUIRE(!testfile.is_invalid());
      bool exists = false;
      REQUIRE(vfs_long_path_win.is_file(testfile, &exists).ok());
      if (exists)
        REQUIRE(vfs_long_path_win.remove_file(testfile).ok());
      REQUIRE(vfs_long_path_win.touch(testfile).ok());
      REQUIRE(vfs_long_path_win.remove_file(testfile).ok());
    } else {
      // Don't check anything; directory creation failed.
    }
  }

  SECTION("- Too long name") {
    std::string name;
    for (unsigned i = 0; i < 256; i++)
      name += "x";

    // Creating the URI is invalid on Win32 (failure to canonicalize path)
    URI testfile(tmpdir_base + name);
    REQUIRE(testfile.is_invalid());
  }

  REQUIRE(vfs_long_path_win.remove_dir(URI(tmpdir_base)).ok());
}

#else

TEST_CASE("VFS: Test long posix paths", "[vfs]") {
  ThreadPool compute_tp(4);
  ThreadPool io_tp(4);
  VFS vfs_long_path_pos{&g_helper_stats, &compute_tp, &io_tp, Config{}};

  std::string tmpdir_base = Posix::current_dir() + "/tiledb_test/";
  REQUIRE(vfs_long_path_pos.create_dir(URI(tmpdir_base)).ok());

  SECTION("- Deep hierarchy") {
    // Create a nested path with a long total length
    std::string tmpdir = tmpdir_base;
    while (tmpdir.size() < 512) {
      tmpdir += "subdir/";
      REQUIRE(vfs_long_path_pos.create_dir(URI(tmpdir)).ok());
    }

    // Check we can create files within the deep hierarchy
    URI testfile("file://" + tmpdir + "file.txt");
    REQUIRE(!testfile.is_invalid());
    bool exists = false;
    REQUIRE(vfs_long_path_pos.is_file(testfile, &exists).ok());
    if (exists)
      REQUIRE(vfs_long_path_pos.remove_file(testfile).ok());
    REQUIRE(vfs_long_path_pos.touch(testfile).ok());
    REQUIRE(vfs_long_path_pos.remove_file(testfile).ok());
  }

  SECTION("- Too long name") {
    // This may not be long enough on some filesystems to pass the fail check.
    std::string name;
    for (unsigned i = 0; i < 256; i++)
      name += "x";

    // Creating the URI and checking its existence is fine
    URI testfile("file://" + tmpdir_base + name);
    REQUIRE(!testfile.is_invalid());
    bool exists = false;
    REQUIRE(vfs_long_path_pos.is_file(testfile, &exists).ok());

    // Creating the file is not
    REQUIRE(!vfs_long_path_pos.touch(testfile).ok());
  }

  REQUIRE(vfs_long_path_pos.remove_dir(URI(tmpdir_base)).ok());
}

#endif

TEST_CASE("VFS: URI semantics", "[vfs][uri]") {
  ThreadPool compute_tp(4);
  ThreadPool io_tp(4);
  std::vector<std::pair<URI, Config>> root_pairs;

  if constexpr (tiledb::sm::filesystem::s3_enabled) {
    Config config;
    REQUIRE(config.set("vfs.s3.endpoint_override", "localhost:9999").ok());
    REQUIRE(config.set("vfs.s3.scheme", "https").ok());
    REQUIRE(config.set("vfs.s3.use_virtual_addressing", "false").ok());
    REQUIRE(config.set("vfs.s3.verify_ssl", "false").ok());

    root_pairs.emplace_back(
        URI("s3://" + random_label("vfs-") + "/"), std::move(config));
  }
  if constexpr (tiledb::sm::filesystem::hdfs_enabled) {
    Config config;
    root_pairs.emplace_back(
        URI("hdfs:///" + random_label("vfs-") + "/"), std::move(config));
  }
  if constexpr (tiledb::sm::filesystem::azure_enabled) {
    Config config;
    REQUIRE(
        config.set("vfs.azure.storage_account_name", "devstoreaccount1").ok());
    REQUIRE(config
                .set(
                    "vfs.azure.storage_account_key",
                    "Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4"
                    "I6tq/"
                    "K1SZFPTOtr/KBHBeksoGMGw==")
                .ok());
    REQUIRE(config
                .set(
                    "vfs.azure.blob_endpoint",
                    "http://127.0.0.1:10000/devstoreaccount1")
                .ok());

    root_pairs.emplace_back(
        URI("azure://" + random_label("vfs-") + "/"), std::move(config));
  }

  Config config;
#ifdef _WIN32
  root_pairs.emplace_back(
      URI(tiledb::sm::Win::current_dir() + "\\" + random_label("vfs-") + "\\"),
      std::move(config));
#else
  root_pairs.emplace_back(
      URI(Posix::current_dir() + "/" + random_label("vfs-") + "/"),
      std::move(config));
#endif

  for (const auto& root_pair : root_pairs) {
    const URI& root = root_pair.first;
    const Config& config = root_pair.second;

    VFS vfs_uri{&g_helper_stats, &compute_tp, &io_tp, config};

    bool exists = false;
    if (root.is_s3() || root.is_azure()) {
      REQUIRE(vfs_uri.is_bucket(root, &exists).ok());
      if (exists) {
        REQUIRE(vfs_uri.remove_bucket(root).ok());
      }
      REQUIRE(vfs_uri.create_bucket(root).ok());
    } else {
      REQUIRE(vfs_uri.is_dir(root, &exists).ok());
      if (exists) {
        REQUIRE(vfs_uri.remove_dir(root).ok());
      }
      REQUIRE(vfs_uri.create_dir(root).ok());
    }

    std::string dir1 = root.to_string() + "dir1";
    REQUIRE(vfs_uri.create_dir(URI(dir1)).ok());

    std::string dir2 = root.to_string() + "dir1/dir2/";
    REQUIRE(vfs_uri.create_dir(URI(dir2)).ok());

    URI file1(root.to_string() + "file1");
    REQUIRE(vfs_uri.touch(file1).ok());

    URI file2(root.to_string() + "file2");
    REQUIRE(vfs_uri.touch(file2).ok());

    URI file3(root.to_string() + "dir1/file3");
    REQUIRE(vfs_uri.touch(file3).ok());

    URI file4(root.to_string() + "dir1/dir2/file4");
    REQUIRE(vfs_uri.touch(file4).ok());

    URI file5(root.to_string() + "file5/");
    REQUIRE(!vfs_uri.touch(file5).ok());

    std::vector<URI> uris;
    REQUIRE(vfs_uri.ls(root, &uris).ok());

    std::vector<std::string> expected_uri_names = {"file1", "file2", "dir1"};

    for (const auto& uri : uris) {
      // Ensure that the URIs do not contain a trailing backslash.
      REQUIRE(uri.to_string().back() != '/');

      // Get the trailing file/dir name.
      const size_t idx = uri.to_string().find_last_of('/');
      REQUIRE(idx != std::string::npos);
      const std::string trailing_name =
          uri.to_string().substr(idx + 1, uri.to_string().length());

      // Verify we expected this file/dir name.
      const auto iter = std::find(
          expected_uri_names.begin(), expected_uri_names.end(), trailing_name);
      REQUIRE(iter != expected_uri_names.end());

      // Erase it from the expected names vector to ensure
      // we see each expected name exactly once.
      REQUIRE(
          std::count(
              expected_uri_names.begin(),
              expected_uri_names.end(),
              trailing_name) == 1);
      expected_uri_names.erase(iter);
    }

    // Verify we found all expected file/dir names.
    REQUIRE(expected_uri_names.empty());

    if (root.is_s3() || root.is_azure()) {
      REQUIRE(vfs_uri.remove_bucket(root).ok());
    } else {
      REQUIRE(vfs_uri.remove_dir(root).ok());
    }
  }
}

TEST_CASE("VFS: test ls_with_sizes", "[vfs][ls-with-sizes]") {
  ThreadPool compute_tp(4);
  ThreadPool io_tp(4);
  VFS vfs_ls{&g_helper_stats, &compute_tp, &io_tp, Config{}};

#ifdef _WIN32
  std::string path = tiledb::sm::Win::current_dir() + "\\vfs_test\\";
#else
  std::string path =
      std::string("file://") + tiledb::sm::Posix::current_dir() + "/vfs_test/";
#endif

  // Clean up
  bool is_dir = false;
  REQUIRE(vfs_ls.is_dir(URI(path), &is_dir).ok());
  if (is_dir)
    REQUIRE(vfs_ls.remove_dir(URI(path)).ok());

  std::string dir = path + "ls_dir";
  std::string file = dir + "/file";
  std::string subdir = dir + "/subdir";
  std::string subdir_file = subdir + "/file";

  // Create directories and files
  REQUIRE(vfs_ls.create_dir(URI(path)).ok());
  REQUIRE(vfs_ls.create_dir(URI(dir)).ok());
  REQUIRE(vfs_ls.create_dir(URI(subdir)).ok());
  REQUIRE(vfs_ls.touch(URI(file)).ok());
  REQUIRE(vfs_ls.touch(URI(subdir_file)).ok());

  // Write to file
  std::string s1 = "abcdef";
  REQUIRE(vfs_ls.write(URI(file), s1.data(), s1.size()).ok());

  // Write to subdir file
  std::string s2 = "abcdef";
  REQUIRE(vfs_ls.write(URI(subdir_file), s2.data(), s2.size()).ok());

  // List
  auto&& [status, rv] = vfs_ls.ls_with_sizes(URI(dir));
  auto children = *rv;

  REQUIRE(status.ok());

#ifdef _WIN32
  // Normalization only for Windows
  file = tiledb::sm::path_win::uri_from_path(file);
  subdir = tiledb::sm::path_win::uri_from_path(subdir);
#endif

  // Check results
  REQUIRE(children.size() == 2);

  REQUIRE(children[0].path().native() == URI(file).to_path());
  REQUIRE(children[1].path().native() == URI(subdir).to_path());

  REQUIRE(children[0].file_size() == 6);

  // Directories don't get a size
  REQUIRE(children[1].file_size() == 0);

  // Clean up
  REQUIRE(vfs_ls.remove_dir(URI(path)).ok());
}

// Currently only S3 is supported for VFS::ls_recursive.
using TestBackends = std::tuple<S3Test>;
TEMPLATE_LIST_TEST_CASE(
    "VFS: Test internal ls_filtered recursion argument",
    "[vfs][ls_filtered][recursion]",
    TestBackends) {
  TestType fs({10, 50});
  if (!fs.is_supported()) {
    return;
  }

  bool recursive = GENERATE(true, false);
  DYNAMIC_SECTION(
      fs.temp_dir_.backend_name()
      << " ls_filtered with recursion: " << (recursive ? "true" : "false")) {
#ifdef HAVE_S3
    // If testing with recursion use the root directory, otherwise use a subdir.
    auto path = recursive ? fs.temp_dir_ : fs.temp_dir_.join_path("subdir_1");
    auto ls_objects = fs.get_s3().ls_filtered(
        path, VFSTestBase::accept_all_files, accept_all_dirs, recursive);

    auto expected = fs.expected_results();
    if (!recursive) {
      // If non-recursive, all objects in the first directory should be
      // returned.
      std::erase_if(expected, [](const auto& p) {
        return p.first.find("subdir_1") == std::string::npos;
      });
    }

    CHECK(ls_objects.size() == expected.size());
    CHECK(ls_objects == expected);
#endif
  }
}

TEST_CASE(
    "VFS: ls_recursive throws for unsupported backends",
    "[vfs][ls_recursive]") {
  // Local and mem fs tests are in tiledb/sm/filesystem/test/unit_ls_filtered.cc
  std::string prefix = GENERATE("s3://", "hdfs://", "azure://", "gcs://");
  VFSTest vfs_test({1}, prefix);
  if (!vfs_test.is_supported()) {
    return;
  }
  std::string backend = vfs_test.temp_dir_.backend_name();

  if (vfs_test.temp_dir_.is_s3()) {
    DYNAMIC_SECTION(backend << " supported backend should not throw") {
      CHECK_NOTHROW(vfs_test.vfs_.ls_recursive(
          vfs_test.temp_dir_, VFSTestBase::accept_all_files));
    }
  } else {
    DYNAMIC_SECTION(backend << " unsupported backend should throw") {
      CHECK_THROWS_WITH(
          vfs_test.vfs_.ls_recursive(
              vfs_test.temp_dir_, VFSTestBase::accept_all_files),
          Catch::Matchers::ContainsSubstring(
              "storage backend is not supported"));
    }
  }
}

TEST_CASE(
    "VFS: Throwing FileFilter for ls_recursive",
    "[vfs][ls_recursive][file-filter]") {
  std::string prefix = "s3://";
  VFSTest vfs_test({0}, prefix);
  if (!vfs_test.is_supported()) {
    return;
  }

  auto file_filter = [](const std::string_view&, uint64_t) -> bool {
    throw std::logic_error("Throwing FileFilter");
  };
  SECTION("Throwing FileFilter with 0 objects should not throw") {
    CHECK_NOTHROW(vfs_test.vfs_.ls_recursive(
        vfs_test.temp_dir_, file_filter, tiledb::sm::accept_all_dirs));
  }
  SECTION("Throwing FileFilter with N objects should throw") {
    vfs_test.vfs_.touch(vfs_test.temp_dir_.join_path("file")).ok();
    CHECK_THROWS_AS(
        vfs_test.vfs_.ls_recursive(vfs_test.temp_dir_, file_filter),
        std::logic_error);
    CHECK_THROWS_WITH(
        vfs_test.vfs_.ls_recursive(vfs_test.temp_dir_, file_filter),
        Catch::Matchers::ContainsSubstring("Throwing FileFilter"));
  }
}
