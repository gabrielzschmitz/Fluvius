# Installation Guide

This guide covers how to build and run this project on all supported
platforms including web builds.

---

## Prerequisites

* **C++ compiler toolchain** suitable for your platform
* **Emscripten SDK** (optional, for web builds only)
* [VSCode](https://code.visualstudio.com/) (optional, for editing and building)

---

## Quick Start

### Desktop Builds (Linux/Windows/macOS)

1. Navigate to the `build` directory:
   ```bash
   cd build
   ```

2. Generate build files using Premake5:
   ```bash
   # Linux
   ./premake5 gmake
   
   # macOS
   ./premake5.osx gmake
   
   # Windows (Visual Studio 2022)
   premake5.exe vs2022
   
   # Windows (MinGW Makefiles)
   premake5.exe gmake
   ```

3. Return to the root directory:
   ```bash
   cd ..
   ```

4. Build the project:
   ```bash
   # Linux/macOS/Windows (Make)
   make config=release_x64
   
   # Windows (Visual Studio)
   # Open the generated .sln file and build in Visual Studio
   ```

5. Run the game:
   ```bash
   ./bin/Release/RECS
   ```

---

## Platform-Specific Instructions

### Windows

#### MinGW-W64 (GCC)

1. Ensure MinGW-W64 is in your PATH or use W64devkit terminal
2. Navigate to `build` directory and run:
   ```bash
   premake5.exe gmake
   cd ..
   mingw32-make config=release_x64
   ```
3. Run the executable:
   ```cmd
   bin\Release\RECS.exe
   ```

> **Note:** Use modern MinGW-W64 from
> [W64devkit](https://github.com/skeeto/w64devkit/releases) or the raylib
> installer. Avoid mixing MinGW distributions.

#### Microsoft Visual Studio (2017/2019/2022)

1. Navigate to `build` directory:
   ```cmd
   cd build
   ```
2. Generate Visual Studio solution:
   ```cmd
   premake5.exe vs2022
   ```
3. Open the generated `.sln` file in Visual Studio
4. Build using `Ctrl+Shift+B` or press `F5` to build and run
5. Executable will be in `bin\Release\` or `bin\Debug\`

---

### Linux

#### Standard Build

1. Navigate to `build` directory:
   ```bash
   cd build
   ```

2. Generate Makefiles:
   ```bash
   ./premake5 gmake
   ```

3. Return to root and build:
   ```bash
   cd ..
   make config=release_x64
   ```

4. Run the game:
   ```bash
   ./bin/Release/RECS
   ```

#### Wayland Support

For Wayland instead of X11:
```bash
cd build
./premake5 gmake --wayland=on
cd ..
make config=release_x64
```

#### With Compilation Database (for IDE support)

```bash
bear -- make config=release_x64 VERBOSE=1
```

---

### macOS

1. Navigate to `build` directory:
   ```bash
   cd build
   ```

2. Generate Makefiles:
   ```bash
   ./premake5.osx gmake
   ```

3. Return to root and build:
   ```bash
   cd ..
   make config=release_x64
   ```

4. Run the game:
   ```bash
   ./bin/Release/RECS
   ```

#### Alternative: Xcode

```bash
cd build
./premake5.osx xcode4
```
Then open the generated Xcode project.

---

## Web Build (Emscripten)

### Prerequisites

1. Install and activate the Emscripten SDK:
   ```bash
   # Clone emsdk repository
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   
   # Install latest version
   ./emsdk install latest
   ./emsdk activate latest
   
   # Set up environment variables (run this in each new terminal)
   source ./emsdk_env.sh
   ```

### Building for Web

1. Navigate to `build` directory:
   ```bash
   cd build
   ```

2. Generate Makefiles with Emscripten flag:
   ```bash
   ./premake5 gmake --with-emscripten --graphics=openges2
   ```

3. Return to root directory:
   ```bash
   cd ..
   ```

4. Build with Emscripten:
   ```bash
   emmake make config=release_web
   ```

5. Run locally:
   ```bash
   emrun bin/Release/RECS.html
   ```

The web build will generate:
- `RECS.html` - Main page
- `RECS.js` - JavaScript runtime
- `RECS.wasm` - WebAssembly binary
- `RECS.data` - Packaged resources

You can host these files on any web server to deploy your game online.

---

## Build Configurations

### Available Configurations

- **Debug**: Includes debug symbols, no optimization
  ```bash
  make config=debug_x64
  ```

- **Release**: Optimized for performance
  ```bash
  make config=release_x64
  ```

### Available Platforms

- `x64` - 64-bit Intel/AMD (default)
- `x86` - 32-bit Intel/AMD
- `ARM64` - ARM 64-bit (Apple Silicon, etc.)
- `Web` - WebAssembly (Emscripten only)

Example:
```bash
make config=release_arm64
```

### Graphics API Options

Available via `--graphics` flag:
- `opengl33` - OpenGL 3.3 (default for desktop)
- `opengl43` - OpenGL 4.3
- `opengl21` - OpenGL 2.1
- `opengl11` - OpenGL 1.1
- `openges2` - OpenGL ES 2.0 (recommended for web)
- `openges3` - OpenGL ES 3.0

Example:
```bash
./premake5 gmake --graphics=opengl43
```

### Backend Options

Available via `--backend` flag:
- `glfw` - GLFW (default, cross-platform)
- `rgfw` - RGFW (lightweight alternative)
- `win32` - Native Win32 (Windows only)

Example:
```bash
./premake5 gmake --backend=win32
```

---

## VSCode Integration

### Building in VSCode

1. Open the project folder in VSCode
2. Install recommended extensions (C/C++, Makefile Tools)
3. Press `Ctrl+Shift+B` to build
4. Press `F5` to build and run with debugger

### Tasks

The project includes VSCode tasks for:
- Building Debug/Release configurations
- Cleaning build artifacts
- Generating project files

Access via `Terminal > Run Task...`

---

## Troubleshooting

### "raylib not found"

The build system automatically downloads raylib to `external/raylib-master`. If
download fails, manually download from [raylib
releases](https://github.com/raysan5/raylib/archive/refs/heads/master.zip) and
extract to `external/raylib-master`.

### "Missing X11 headers" (Linux)

Install development packages:
```bash
# Ubuntu/Debian
sudo apt-get install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev

# Fedora
sudo dnf install libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel

# Arch
sudo pacman -S libx11 libxrandr libxinerama libxcursor libxi
```

### "Wayland headers missing" (Linux with Wayland)

Install Wayland development packages:
```bash
# Ubuntu/Debian
sudo apt-get install libwayland-dev wayland-protocols libxkbcommon-dev

# Fedora
sudo dnf install wayland-devel wayland-protocols-devel libxkbcommon-devel
```

### Web build shows black screen

Ensure you're running with `emrun` or a proper web server. Opening the HTML
file directly (`file://`) won't work due to browser security restrictions.

### Resources not loading on web

Verify the `--preload-file` path in premake5.lua matches your resources folder
structure. The default is `../resources@/resources`.

---

## Clean Build

To remove all build artifacts and start fresh:

```bash
# Remove build output
make clean

# Remove generated project files
rm -rf build_files/
rm -rf bin/
rm Makefile *.make

# Regenerate
cd build
./premake5 gmake
cd ..
make config=release_x64
```

---

## Project Structure

```
RECS/
├── build/               # Premake executables
├── build_files/         # Generated project files (created by Premake)
├── bin/                 # Compiled binaries
│   ├── Debug/
│   └── Release/
├── external/            # External dependencies (raylib)
├── include/             # Header files
├── resources/           # Game assets (sounds, textures, etc.)
├── src/                 # Source code
└── premake5.lua         # Build configuration
```

---

## Additional Information

- **Raylib Version**: 5.6-dev (automatically downloaded)
- **C Standard**: C17/GNU17 (GNU17 for web builds)
- **C++ Standard**: C++17/GNU++17 (GNU++17 for web builds)
- **License**: Check LICENSE file in repository

For issues or contributions, visit the project repository.
