#include "Core/AppPaths.h"

#include <iostream>

int main()
{
    const std::filesystem::path root = std::filesystem::path("test-root") / "user-data";
    Hephaestus::SetUserDataRootOverride(root);
    bool ok = Hephaestus::UserDataFor("Game") == root / "Game";
    ok &= Hephaestus::LocalAppDataFor("Game") == root / "Game";
    Hephaestus::SetUserDataRootOverride({});
    ok &= Hephaestus::UserDataFor("Game") != root / "Game";
    if (!ok) {
        std::cerr << "[FAIL] the user data root override is not applied or not cleared\n";
        return 1;
    }
    std::cout << "AppPathsTests passed\n";
    return 0;
}
