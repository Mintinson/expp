#include <string>

int main() {
    // Tree-sitter should identify keywords, types, strings, and numbers.
    const std::string message{"hello"};
    return message.size() == 5 ? 0 : 1;
}
