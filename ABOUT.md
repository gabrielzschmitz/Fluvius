# About Fluvius

Fluvius is a lightweight real-time 2D fluid simulation that demonstrates the
power of an Entity-Component System (ECS) architecture for physics-based
applications. Built with raylib for rendering and Motrix as its ECS backbone,
it efficiently simulates thousands of fluid particles while maintaining high
frame rates.

---

## Core Features

### Fluid Simulation Engine

Fluvius implements a complete **Smoothed Particle Hydrodynamics (SPH)** fluid
simulation:

- **Particle-based simulation** - Up to 10,000+ fluid particles with real-time
  physics
- **SPH Kernels**:
  - Poly6 kernel for density computation
  - Spiky kernel gradient for pressure forces
  - Viscosity kernel for internal friction
- **Spatial hashing** - Optimized neighbor search using grid-based spatial
  partitioning
- **Pressure system** - Dynamic pressure calculation based on target density
- **Boundary collisions** - Configurable wall bouncing with damping
- **Gravity** - Adjustable downward force with simulation speed control
- **Pause/Play** - Toggle simulation at any time

### Rendering Modes

Multiple visualization options for the fluid:

- **Particle rendering** - Individual particles colored by velocity (blue →
  green → yellow → red gradient)
- **Fluid surface** - Field-based surface reconstruction with alpha blending
- **Marching squares** - Contour line extraction for fluid boundaries
- **Filled marching squares** - Solid fill with speed-based color mapping
- **Pressure field** - Visualize pressure distribution across particles (blue =
  low, white = mid, red = high)

### Entity-Component System (Motrix)

A custom-built ECS architecture providing:

- **Stable entity handles** - Index + generation counters prevent stale references
- **Sparse set storage** - O(1) component insertion, removal, and lookup
- **Bitmask signatures** - Fast component matching for entity queries
- **Group views** - Cached multi-component iteration for performance
- **Packed dense iteration** - Cache-friendly sequential memory access
- **Modular architecture** - Clear separation between entities, components, and
  systems

### Interactive UI System

A complete immediate-mode GUI built from scratch:

- **Windows** - Draggable windows with title bars and close buttons
- **Sliders** - Float sliders with configurable min/max/step values
- **Checkboxes** - Toggle boolean settings
- **Buttons** - Clickable action triggers
- **Dropdowns** - Selection lists with expand/collapse
- **Groups** - Layout containers with padding, spacing, and titles
- **Scrollbars** - Vertical scrolling for overflow content
- **Tooltips** - Delayed hover text display
- **Win95-inspired styling** - Retro aesthetic with proper bevels

### Camera System

Interactive 2D camera controls:

- **Zoom** - Mouse wheel zooms centered on cursor position
- **Pan** - Middle mouse button drags the view
- **Reset** - Press `=` to reset camera to default view
- **Auto-centering** - Camera automatically centers the simulation area

### Particle Interaction

Direct manipulation of fluid particles:

- **Selection** - Left-click to select individual particles
- **Selection visualization** - Highlighted particle with velocity arrow
- **Density inspection** - View particle density values in real-time
- **Right-click reset** - Clear selection

---

## Technical Implementation

### Physics Pipeline

The simulation follows a multi-step physics pipeline executed each frame:

1. **Position prediction** - Apply velocity and gravity to estimate new
   positions
2. **Spatial grid build** - Hash particles into grid cells for fast lookup
3. **Density computation** - Calculate density using Poly6 kernel
4. **Pressure calculation** - Convert density to pressure values
5. **Force computation** - Apply pressure gradient and viscosity forces
6. **Integration** - Update velocities and positions
7. **Collision resolution** - Handle boundary and particle-particle collisions

### Performance Optimizations

- **O(n) neighbor search** via spatial hashing instead of O(n²) brute force
- **Group caching** - Pre-computed entity groups minimize signature checks
- **Dense iteration** - Sequential memory access patterns for cache efficiency
- **Selective rendering** - Toggle visualization layers independently

---

## Project Structure

```
src/
├── app/           # Application entry point and state management
├── engine/
│   ├── components/  # Physics, UI, camera components
│   ├── ecs/         # Core ECS implementation (Motrix)
│   ├── systems/    # Physics, UI, camera systems
│   └── globals.h   # Global constants and configuration
├── entities/      # Fluid particle entity builders
└── tests/         # ECS and sparse set benchmarks
```

---

## Build Targets

Fluvius supports multiple platforms:

- **Windows** (Visual Studio, MinGW)
- **macOS** (Xcode, Make)
- **Linux** (Make, CMake)
- **WebAssembly** (HTML5)

---

## Controls

| Input        | Action          |
|--------------|-----------------|
| Left Click   | Select particle |
| Right Click  | Clear selection |
| Mouse Wheel  | Zoom in/out     |
| Middle Mouse | Pan camera      |
| `=`          | Reset camera    |
| Space        | Toggle pause    |

---

Fluvius showcases how modern game engine patterns can be applied to scientific
visualization, creating an interactive tool that makes fluid dynamics
accessible and visually engaging.
