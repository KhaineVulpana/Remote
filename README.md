# Remote Desktop

This project demonstrates a simple remote desktop client and server.

## Building

Use CMake to configure and build the project.

On Windows:

```bash
cmake -S . -B build -A x64
cmake --build build --config Release
```

On non-Windows platforms the project builds placeholder executables so CI can run:

```bash
cmake -S . -B build
cmake --build build
```

The resulting executables will be located in `build` (or `build/Release` on Windows).
