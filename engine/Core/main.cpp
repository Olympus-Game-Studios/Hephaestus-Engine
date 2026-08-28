#include <iostream>
#include <string>

#include "EngineLog.h"

#if HEPHAESTUS_EDITOR_QT_UI
#include "../UI/QtEditor/EditorQtApp.h"
#endif

int main(int argc, char *argv[]) {
  Hephaestus::EngineLog::Initialize("HephaestusEngine", argc, argv);

  // Parse command-line arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Hephaestus Engine\n";
      std::cout << "Usage: HephaestusEngine [options]\n";
      std::cout << "Options:\n";
      std::cout << "  --help, -h             Show this help message\n";
      Hephaestus::EngineLog::Shutdown();
      return 0;
    }
  }

#if HEPHAESTUS_EDITOR_QT_UI
  Hephaestus::EngineLog::Info("Starting Hephaestus Qt Editor");
  std::cout << "Starting Hephaestus Qt Editor\n";
  const int result = RunQtEditor(argc, argv);
  Hephaestus::EngineLog::Info(std::string("Editor exited with code ") + std::to_string(result));
  Hephaestus::EngineLog::Shutdown();
  return result;
#else
  Hephaestus::EngineLog::Error("This build requires Qt editor support.");
  std::cerr << "[Error] This build requires Qt editor support.\n";
  std::cerr << "        Build with Qt6 support enabled.\n";
  Hephaestus::EngineLog::Shutdown();
  return 1;
#endif
}
