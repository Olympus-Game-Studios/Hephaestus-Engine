#include <filesystem>
#include <iostream>
int main() {
    std::filesystem::path p("builtin://cube");
    std::cout << "is_absolute: " << p.is_absolute() << std::endl;
    std::cout << "generic: " << p.generic_string() << std::endl;
    std::cout << "filename: " << p.filename().generic_string() << std::endl;
    return 0;
}
