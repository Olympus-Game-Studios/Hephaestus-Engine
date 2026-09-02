# Headless Project Validation and Packaging

The `HephaestusBuild` command uses the same non-Qt `BuildService` as the
editor's Build Project window. It packages the generic game player and all
project assets; it does not compile project-specific gameplay code.

```powershell
# Validate only
.\build\Release\HephaestusBuild.exe `
  --project C:\Projects\MyGame `
  --validate-only

# Create a transactional Release package
.\build\Release\HephaestusBuild.exe `
  --project C:\Projects\MyGame `
  --config Release `
  --output C:\Builds

# Force a clean package and optionally create a ZIP
.\build\Release\HephaestusBuild.exe `
  --project C:\Projects\MyGame `
  --config Release `
  --output C:\Builds `
  --clean `
  --zip
```

Exit code `0` means validation or packaging succeeded. A non-zero exit code
means the operation failed or Release validation found blocking errors. Full
diagnostics are written to standard output, making the command suitable for
CI jobs.

The build machine only needs CMake and MSVC when a matching
`HephaestusGame.exe` is unavailable. Portable editor distributions include
the player, shaders, and Lua runtime so data-only projects can package without
developer tools.

Release automation should retain the produced directory as an artifact and
run it on a clean Windows x64 machine with a Vulkan-capable GPU and the Visual
C++ 2015–2022 Redistributable.
