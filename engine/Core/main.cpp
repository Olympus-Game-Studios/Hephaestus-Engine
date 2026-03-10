#include <iostream>
#include <string>

#if HEPHAESTUS_EDITOR_QT_UI
#include "../UI/QtEditor/EditorQtApp.h"
#endif

int main(int argc, char *argv[]) {
  // Parse command-line arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Hephaestus Engine\n";
      std::cout << "Usage: HephaestusEngine [options]\n";
      std::cout << "Options:\n";
      std::cout << "  --help, -h             Show this help message\n";
      return 0;
    }
  }

#if HEPHAESTUS_EDITOR_QT_UI
  std::cout << "Starting Hephaestus Qt Editor\n";
  return RunQtEditor(argc, argv);
#else
  std::cerr << "[Error] This build requires Qt editor support.\n";
  std::cerr << "        Build with Qt6 support enabled.\n";
  return 1;
#endif
}
