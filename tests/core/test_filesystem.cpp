#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "expp/core/filesystem.hpp"

#include <filesystem>
#include <fstream>

using namespace expp::core;
namespace fs = std::filesystem;

// Helper to create a temporary test directory
class TempDirectory{
public:
    TempDirectory() {
        path_ = fs::temp_directory_path() / "expp_test_";
        fs::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    [[nodiscard]] const fs::path& path() const { return path_; }
    // Create a test file
    fs::path createFile(const std::string& name, const std::string& content = "") {
        auto file_path = path_ / name;
        fs::create_directories(file_path.parent_path());
        std::ofstream file(file_path);
        file << content;
        return file_path;
    }

    // Create a test directory
    fs::path createDir(const std::string& name) {
        auto dir_path = path_ / name;
        fs::create_directories(dir_path);
        return dir_path;
    }
private: 
    fs::path path_;
};

TEST_CASE("isExecutable", "[core][filesystem]") {
    TempDirectory tmpDir;

    SECTION("regular file is not executable by default") {
        auto file = tmpDir.createFile("test.txt", "content");
        CHECK_FALSE(filesystem::is_executable(file));
    }

#ifndef _WIN32
    SECTION("executable file is detected") {
        auto file = tmpDir.createFile("script.sh", "#!/bin/bash");
        fs::permissions(file, fs::perms::owner_exec, fs::perm_options::add);
        CHECK(filesystem::is_executable(file));
    }
#endif
}

TEST_CASE("classifyFile", "[core][filesystem]") {
    TempDirectory tmpDir;

    SECTION("directory is classified correctly") {
        auto dir = tmpDir.createDir("mydir");
        auto entry = fs::directory_entry(dir);
        CHECK(filesystem::classify_file(entry) == filesystem::FileType::Directory);
    }

    SECTION("regular file is classified correctly") {
        auto file = tmpDir.createFile("main.cpp", "int main() { return 0; }");
        auto entry = fs::directory_entry(file);
        CHECK(filesystem::classify_file(entry) == filesystem::FileType::SourceCode);
    }

    SECTION("executable file is classified correctly") {
        #ifndef _WIN32  
        auto file = tmpDir.createFile("script.sh", "#!/bin/bash");
        #else
        auto file = tmpDir.createFile("program.exe", "binary content");
        #endif 
        fs::permissions(file, fs::perms::owner_exec, fs::perm_options::add);
        auto entry = fs::directory_entry(file);
        CHECK(filesystem::classify_file(entry) == filesystem::FileType::Executable);
    }

    SECTION("archive file is classified correctly") {
        auto file = tmpDir.createFile("archive.zip");
        auto entry = fs::directory_entry(file);
        CHECK(filesystem::classify_file(entry) == filesystem::FileType::Archive);
    }

    SECTION("image file is classified correctly") {
        auto file = tmpDir.createFile("photo.jpg");
        auto entry = fs::directory_entry(file);
        CHECK(filesystem::classify_file(entry) == filesystem::FileType::Image);
    }

    SECTION("document file is classified correctly") {
        auto file = tmpDir.createFile("report.pdf");
        auto entry = fs::directory_entry(file);
        CHECK(filesystem::classify_file(entry) == filesystem::FileType::Document);
    }

    SECTION("regular file with unknown extension is classified as regular") {
        auto file = tmpDir.createFile("data.bin");
        auto entry = fs::directory_entry(file);
        CHECK(filesystem::classify_file(entry) == filesystem::FileType::RegularFile);
    }

#ifndef _WIN32
    SECTION("symlink is classified correctly") {
        auto target = tmpDir.createFile("target.txt", "hello");
        auto link = tmpDir.path() / "target_link";
        fs::create_symlink(target, link);

        auto entry = fs::directory_entry(link);
        CHECK(filesystem::classify_file(entry) == filesystem::FileType::Symlink);
    }
#endif
}


TEST_CASE("isPreviewable", "[core][filesystem]") {
    CHECK(filesystem::is_previewable("file.txt"));
    CHECK(filesystem::is_previewable("main.cpp"));
    CHECK(filesystem::is_previewable("config.toml"));
    CHECK(filesystem::is_previewable("script.sh"));

    CHECK_FALSE(filesystem::is_previewable("image.png"));
    CHECK_FALSE(filesystem::is_previewable("archive.zip"));
    CHECK_FALSE(filesystem::is_previewable("binary.exe"));
}

TEST_CASE("listDirectory", "[core][filesystem]") {
    TempDirectory tmpDir;

    SECTION("empty directory") {
        auto result = filesystem::list_directory(tmpDir.path());
        REQUIRE(result.has_value());
        CHECK(result->empty());
    }

    SECTION("directory with files") {
        tmpDir.createFile("a.txt");
        tmpDir.createFile("b.cpp");
        tmpDir.createDir("subdir");

        auto result = filesystem::list_directory(tmpDir.path());
        REQUIRE(result.has_value());
        CHECK(result->size() == 3);

        // Directories should come first
        CHECK((*result)[0].isDirectory());
        CHECK((*result)[0].filename() == "subdir");
    }

    SECTION("hidden files excluded by default") {
        tmpDir.createFile(".hidden");
        tmpDir.createFile("visible.txt");

        auto result = filesystem::list_directory(tmpDir.path(), false);
        REQUIRE(result.has_value());
        CHECK(result->size() == 1);
        CHECK((*result)[0].filename() == "visible.txt");
    }

    SECTION("hidden files included when requested") {
        tmpDir.createFile(".hidden");
        tmpDir.createFile("visible.txt");

        auto result = filesystem::list_directory(tmpDir.path(), true);
        REQUIRE(result.has_value());
        CHECK(result->size() == 2);
    }

    SECTION("non-existent directory returns error") {
        auto result = filesystem::list_directory(tmpDir.path() / "nonexistent");
        CHECK_FALSE(result.has_value());
        CHECK(result.error().is_category(ErrorCategory::NotFound));
    }

#ifndef _WIN32
    SECTION("symlink metadata is populated") {
        auto target = tmpDir.createFile("real.txt", "content");
        auto link = tmpDir.path() / "real_link";
        fs::create_symlink(target, link);

        auto result = filesystem::list_directory(tmpDir.path(), true);
        REQUIRE(result.has_value());

        const auto it = std::find_if(result->begin(), result->end(), [](const filesystem::FileEntry& entry) {
            return entry.filename() == "real_link";
        });
        REQUIRE(it != result->end());
        CHECK(it->isSymlink());
        CHECK_FALSE(it->isBrokenSymlink);
        CHECK_FALSE(it->isRecursiveSymlink);
    }
#endif
}

TEST_CASE("createFile and createDirectory", "[core][filesystem]") {
    TempDirectory tmpDir;

    SECTION("create file") {
        auto path = tmpDir.path() / "newfile.txt";
        auto result = filesystem::create_file(path);

        CHECK(result.has_value());
        CHECK(fs::exists(path));
    }

    SECTION("create directory") {
        auto path = tmpDir.path() / "newdir";
        auto result = filesystem::create_directory(path);

        CHECK(result.has_value());
        CHECK(fs::is_directory(path));
    }

    SECTION("create nested directories") {
        auto path = tmpDir.path() / "a" / "b" / "c";
        auto result = filesystem::create_directory(path);

        CHECK(result.has_value());
        CHECK(fs::is_directory(path));
    }

    SECTION("create file with parent directories") {
        auto path = tmpDir.path() / "nested" / "dir" / "file.txt";
        auto result = filesystem::create_file(path);

        CHECK(result.has_value());
        CHECK(fs::exists(path));
    }
}

TEST_CASE("rename", "[core][filesystem]") {
    TempDirectory tmpDir;

    SECTION("rename file") {
        auto oldPath = tmpDir.createFile("old.txt", "content");
        auto newPath = tmpDir.path() / "new.txt";

        auto result = filesystem::rename(oldPath, newPath);

        CHECK(result.has_value());
        CHECK_FALSE(fs::exists(oldPath));
        CHECK(fs::exists(newPath));
    }

    SECTION("rename non-existent file returns error") {
        auto result = filesystem::rename(tmpDir.path() / "missing", tmpDir.path() / "new");
        CHECK_FALSE(result.has_value());
    }
}

TEST_CASE("removeFile and removeDirectory", "[core][filesystem]") {
    TempDirectory tmpDir;

    SECTION("remove file") {
        auto file = tmpDir.createFile("todelete.txt");
        CHECK(fs::exists(file));

        auto result = filesystem::remove_file(file);

        CHECK(result.has_value());
        CHECK_FALSE(fs::exists(file));
    }

    SECTION("remove directory with contents") {
        auto dir = tmpDir.createDir("toremove");
        tmpDir.createFile("toremove/file.txt");

        auto result = filesystem::remove_directory(dir);

        CHECK(result.has_value());
        CHECK_FALSE(fs::exists(dir));
    }
}

TEST_CASE("readPreview", "[core][filesystem]") {
    TempDirectory tmpDir;

    SECTION("text file preview") {
        std::string content = "line1\nline2\nline3";
        auto file = tmpDir.createFile("test.txt", content);

        auto result = filesystem::read_preview(file, 10);

        REQUIRE(result.has_value());
        CHECK(result->size() == 3);
        CHECK((*result)[0] == "line1");
    }

    SECTION("directory preview shows contents") {
        auto dir = tmpDir.createDir("preview_test");
        tmpDir.createFile("preview_test/file1.txt");
        tmpDir.createFile("preview_test/file2.cpp");

        auto result = filesystem::read_preview(dir, 10);

        REQUIRE(result.has_value());
        CHECK(result->size() >= 2);  // Header + files
    }

    SECTION("binary file shows info") {
        auto file = tmpDir.createFile("binary.bin", "data");

        auto result = filesystem::read_preview(file, 10);

        REQUIRE(result.has_value());
        CHECK((*result)[0] == "[Binary or unsupported file]");
    }

#ifndef _WIN32
    SECTION("recursive symlink preview is reported safely") {
        auto loop_link = tmpDir.path() / "loop";
        fs::create_symlink(loop_link, loop_link);

        auto result = filesystem::read_preview(loop_link, 10);
        REQUIRE(result.has_value());

        const bool found_recursive_msg =
            std::any_of(result->begin(), result->end(), [](const std::string& line) {
                return line == "[Recursive symlink detected]";
            });
        CHECK(found_recursive_msg);
    }

    SECTION("broken symlink preview is reported safely") {
        auto broken_link = tmpDir.path() / "missing_link";
        fs::create_symlink(tmpDir.path() / "does_not_exist.txt", broken_link);

        auto result = filesystem::read_preview(broken_link, 10);
        REQUIRE(result.has_value());

        const bool found_broken_msg =
            std::any_of(result->begin(), result->end(), [](const std::string& line) {
                return line == "[Broken symlink]";
            });
        CHECK(found_broken_msg);
    }
#endif
}

TEST_CASE("formatFileSize", "[core][filesystem]") {
    CHECK(filesystem::format_file_size(0) == "0 B");
    CHECK(filesystem::format_file_size(100) == "100 B");
    CHECK(filesystem::format_file_size(1024) == "1.00 KB");
    CHECK(filesystem::format_file_size(1536) == "1.50 KB");
    CHECK(filesystem::format_file_size(1048576) == "1.00 MB");
    CHECK(filesystem::format_file_size(1073741824) == "1.00 GB");
}
