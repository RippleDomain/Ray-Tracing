# Real-Time Vulkan Ray Tracer

A C++/Vulkan compute ray tracer that renders a sphere scene in real time, accumulates multi-sample frames, and exposes camera, depth-of-field, and ray tracing controls through ImGui.

This project is the **compute shader** implementation of Vulkan ray tracing. The **Vulkan Ray Tracing Pipeline** version lives in a separate repo: **https://github.com/RippleDomain/Ray-Tracing-2.0**.

## Images
**Ray Traced Spheres with Depth of Field**

<img width="1919" height="1048" alt="Ekran görüntüsü 2026-01-10 231124" src="https://github.com/user-attachments/assets/724b5754-96fe-4031-903a-347185d2ee4b" />

---

## Features

- **Rendering**
  - Vulkan compute ray tracer; writes to a storage image then blits to the swapchain.
  - Analytic geometry: diffuse/metal/dielectric spheres with checker-flag support.
  - Per-pixel RNG, multi-bounce transport, Schlick-based fresnel, and fuzzed metals.
  - Depth of field via thin-lens camera; adjustable aperture/focus distance/FOV.
  - Temporal accumulation across frames; resets automatically on camera/setting changes.

- **Camera & Controls**
  - Free-fly WASD + Space/Shift with mouse-look.
  - ESC toggles camera pause so ImGui can capture the cursor cleanly.
  - ImGui overlay shows FPS; settings window tunes sampling and camera parameters.

- **UI Toggles**
  - Samples per pixel, aperture, focus distance, vertical FOV, and max bounce depth.
  - Swapchain-aware: resize recreates swapchain and accumulation targets.

- **Data & Pipeline**
  - GPU scene buffer for spheres plus uniform params buffer (camera, frame counters, resolution).
  - Single compute pipeline with descriptor sets for params, spheres, accum image, and output image.
  - Accumulation image persists across frames until invalidated.

---

## Build (Windows, Visual Studio 2022)

1. Install **Visual Studio 2022** with "Desktop development with C++".
2. Install a **Vulkan 1.3** capable driver (NVIDIA/AMD/Intel) and runtime (Vulkan SDK recommended for validation layers).
3. Open the solution: `Ray-Tracing.sln`.
4. Select **x64** and **Debug** or **Release**.
5. Build & Run.

All third-party dependencies are vendored in the **`external/`** directory and pre-wired in the project. Opening and building the solution in VS2022 is sufficient-no extra CMake step required.

### Dependencies (vendored under `external/`)
- **GLFW** (windowing, input)
- **Vulkan SDK headers/libs** (core + shaderc/spirv-tools/dxc)
- **ImGui** (+ GLFW/Vulkan backends)
- **GLM** (math)
- **VMA** (Vulkan Memory Allocator)

The app targets **Vulkan 1.3** and dispatches a compute shader (`#version 460`) for tracing.

---

## Run-time Usage

### Camera & Movement
- **Mouse look**: move the mouse while capture is active.
- **W / A / S / D**: move forward/left/back/right.
- **Space / Left Shift**: move up/down.
- **ESC**: toggle camera pause; when paused the cursor is released for UI.

### ImGui (top-left overlays)
- **Overlay window**: FPS + quick hint about ESC for cursor toggle.
- **Ray Tracer window**:
  - **Samples**: integer samples per pixel (per frame).
  - **Aperture**: lens radius for depth of field.
  - **Focus Dist**: focal distance.
  - **FOV**: vertical field of view.
  - **Max Depth**: max bounce depth for the integrator.

Changes to camera, sampling, or window size reset accumulation to keep results coherent.
> Tip: keep the window focused and stay still for a few seconds to let accumulation converge; move or tweak sliders to restart sampling when exploring the scene.
