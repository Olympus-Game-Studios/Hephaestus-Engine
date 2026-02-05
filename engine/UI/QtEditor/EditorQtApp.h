#pragma once

/**
 * @file EditorQtApp.h
 * @brief Qt-based Editor UI entry point for Hephaestus Engine.
 *
 * This provides a standalone Qt Widgets application that runs as an alternative
 * to the GLFW/Vulkan runtime path. The Qt editor is UI-only and does not
 * integrate with the renderer.
 *
 * Usage: HephaestusEngine.exe --editor
 */

#if HEPHAESTUS_EDITOR_QT_UI

/**
 * @brief Run the Qt-based editor application.
 *
 * This function creates a QApplication, shows the main editor window,
 * and enters the Qt event loop. It returns when the user closes the editor.
 *
 * @param argc Command line argument count (from main)
 * @param argv Command line arguments (from main)
 * @return Exit code (0 for success)
 */
int RunQtEditor(int argc, char** argv);

#endif // HEPHAESTUS_EDITOR_QT_UI
