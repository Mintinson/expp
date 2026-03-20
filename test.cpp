#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>


namespace fs = std::filesystem;

bool is_executable(const fs::path& filepath) noexcept {
    try {
        if (!fs::is_regular_file(filepath)) return false;

#ifdef _WIN32
// Windows: Check extension against PATHEXT environment variable
// This is a common convention on Windows, but not foolproof. For a more robust solution, you might need to use Windows API calls.
// But slower and more complex, so we stick to extension check for simplicity.
        std::string ext = filepath.extension().string();
        if (ext.empty()) return false;
        
        std::ranges::transform(ext, ext.begin(), ::toupper);

        size_t required_size;
        char* pathext_buffer = nullptr;
        _dupenv_s(&pathext_buffer, &required_size, "PATHEXT");
        
        std::string pathext = pathext_buffer ? pathext_buffer : ".COM;.EXE;.BAT;.CMD;.VBS;.JS;.WSF";
        if (pathext_buffer) free(pathext_buffer);

        size_t pos = pathext.find(ext);
        while (pos != std::string::npos) {
            bool start_valid = (pos == 0) || (pathext[pos - 1] == ';');
            bool end_valid   = (pos + ext.length() == pathext.length()) || (pathext[pos + ext.length()] == ';');
            if (start_valid && end_valid) return true;
            pos = pathext.find(ext, pos + 1);
        }
        return false;
#else
        // Linux/macOS: Rely on standard POSIX execute bits
        auto perm = fs::status(filepath).permissions();
        return (perm & fs::perms::owner_exec) != fs::perms::none || 
               (perm & fs::perms::group_exec) != fs::perms::none ||
               (perm & fs::perms::others_exec) != fs::perms::none;
#endif
    } catch (...) {
        return false;
    }
}

int main() {
    int* p = 0;
    std::cout << "Hello, World!" << std::endl;
    fs::path testFile = "test.txt";
    std::ofstream file(testFile);
    file << "Test content";
    std::cout << "Is executable: " << is_executable(testFile) << std::endl;
}