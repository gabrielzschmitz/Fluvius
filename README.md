# Fluvius

<img align="right" width="192px" src="./resources/icon.svg" alt="Fluvius Logo">

<a href="./LICENSE"><img src="https://img.shields.io/badge/license-MIT-green" alt="License"></a> <a href="https://www.buymeacoffee.com/gabrielzschmitz" target="_blank"><img src="https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png" alt="Buy Me A Coffee" style="height: 20px !important;width: 87px;" ></a> <a href="https://github.com/gabrielzschmitz/Fluvius"><img src="https://img.shields.io/github/stars/gabrielzschmitz/Fluvius?style=social" alt="Give me a Star"></a>

**Fluvius** is a lightweight real-time 2D fluid simulation built with
**raylib** and powered by
**[Motrix](https://github.com/gabrielzschmitz/Motrix)**, a minimal Entity
Component System designed for high-performance interactive applications.

It demonstrates how an ECS architecture can efficiently drive particle-based
fluid dynamics, real-time rendering, and UI interaction in a compact C++
codebase.

---

## Quick Start

### 0. Run in Your Browser

A [_WebGPU_](https://webgpu.org/) build of Fluvius is available on itch.io,
allowing you to try the simulation directly in your browser without installing
or building the project.

Note that the WebGPU build is intended primarily for convenience and
experimentation. Due to the additional overhead of running through the browser,
expect approximately _4× lower performance_ compared to the native build.

**[Check Fluvius on itch.io](https://gabrielzschmitz.itch.io/fluvius)** — the
itch.io build is a single self-contained `index.html`, so you can also download
it and run the simulation by simply double-clicking the file, no server or
build required.

Or run the same WebGL build locally in a container — the release package
([ghcr.io/gabrielzschmitz/fluvius](https://github.com/gabrielzschmitz/Fluvius/pkgs/container/fluvius))
serves it via nginx and is rebuilt on every release tag:

```sh
docker pull ghcr.io/gabrielzschmitz/fluvius:latest
docker run --rm -p 8080:80 ghcr.io/gabrielzschmitz/fluvius:latest
```

Then open <http://localhost:8080>.


### 1. Clone the repository

```sh
git clone https://github.com/gabrielzschmitz/Fluvius.git
cd Fluvius
```

### 2. Build and run

Follow the platform-specific build instructions in `INSTALL.md`.

---

## Features

* Real-time 2D fluid simulation
* ECS-driven architecture powered by Motrix
* Interactive UI controls for simulation parameters
* Lightweight rendering pipeline built with raylib
* Modular separation between engine, entities, and application logic

<p align="center">
  <img src="./resources/demo.gif" alt="Demo Screenshot" style="border-radius: 8px;">
</p>
<p align="center">
  <em>Fluvius simulates thousands of fluid particles in real time while
  maintaining high frame rates.</em>
</p>

---

## Project Structure

```text
article/    # LaTeX paper (sources, figures, compiled main.pdf)
build/      # premake5 build scripts
include/    # shared headers
resources/  # icons, fonts, and demo assets
scripts/    # packaging/release tooling
src/        # C++ sources
├── app/        # application lifecycle, scenes, platform entrypoints
├── engine/     # ECS engine, components, and systems
├── entities/   # reusable entity builders
├── tests/      # tests and benchmarks
└── main.cpp
```

### Architecture

Fluvius uses **[Motrix](https://github.com/gabrielzschmitz/Motrix)** as its ECS
backbone:

* **Entities** represent fluid particles, boundaries, and UI controls
* **Components** store physics, rendering, and interaction state
* **Systems** update simulation behavior and rendering independently

This separation keeps simulation logic modular, cache-friendly, and easy to
extend.

---

## Article

The article **“Simulação de Fluídos SPH usando ECS”** (SPH fluid simulation
using ECS) is included under `article/` together with its LaTeX sources and
figures. The compiled manuscript is available as `article/main.pdf`.

---

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file
for details.
