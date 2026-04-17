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
src
├── app
├── engine
│   ├── components
│   ├── ecs
│   └── systems
├── entities
├── tests
└── main.cpp
```

### Core Layout

* **app/** -- application lifecycle, simulation state, platform entrypoints
* **engine/ecs/** -- ECS engine itself
* **engine/components/** -- ECS component definitions
* **engine/systems/** -- simulation, physics, and UI systems
* **entities/** -- reusable entity builders
* **tests/** -- ECS and sparse set benchmarks/tests

---

## Architecture

Fluvius uses **[Motrix](https://github.com/gabrielzschmitz/Motrix)** as its ECS
backbone:

* **Entities** represent fluid particles, boundaries, and UI controls
* **Components** store physics, rendering, and interaction state
* **Systems** update simulation behavior and rendering independently

This separation keeps simulation logic modular, cache-friendly, and easy to
extend.

---

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file
for details.
