# Python Plugin for Yetty

This plugin embeds a Python interpreter into yetty, allowing Python scripts to render graphics directly using yetty's WebGPU context. It provides seamless integration with **pygfx** and **fastplotlib** for GPU-accelerated scientific visualization.

## Features

- Embedded Python 3.13 interpreter
- Direct WebGPU rendering (no offscreen copying)
- Integration with pygfx and fastplotlib
- Shared wgpu-native library between C++ and Python
- Automatic package installation (pygfx, fastplotlib, wgpu, etc.)

## Usage

### Running a Python Script

```bash
./yetty-plugin-tester -d ./plugins run python -f my_script.py
```

### Running Inline Code

```bash
./yetty-plugin-tester -d ./plugins run python -c "print('Hello from Python!')"
```

### With Duration Limit

```bash
./yetty-plugin-tester -d ./plugins run python -t 10000 -f demo.py  # Run for 10 seconds
```

## Using pygfx

[pygfx](https://github.com/pygfx/pygfx) is a modern GPU-accelerated rendering engine for Python. The plugin provides direct integration via `yetty_pygfx`.

### Basic pygfx Example

```python
import numpy as np
import yetty_pygfx

# Initialize the integration (must be called first!)
yetty_pygfx.init()

# Get the texture to render to
texture = yetty_pygfx.get_texture()

# Create a pygfx renderer targeting our texture
import pygfx

renderer = pygfx.WgpuRenderer(texture)
scene = pygfx.Scene()
camera = pygfx.PerspectiveCamera(70, 16/9)

# Add a mesh
geometry = pygfx.box_geometry(1, 1, 1)
material = pygfx.MeshPhongMaterial(color=(1, 0.5, 0))
mesh = pygfx.Mesh(geometry, material)
scene.add(mesh)

# Add light
scene.add(pygfx.AmbientLight())
scene.add(pygfx.DirectionalLight())

camera.position.z = 3

# The render callback is called every frame
def animate():
    mesh.rotation.y += 0.01
    renderer.render(scene, camera)

texture.request_draw(animate)
```

## Using fastplotlib

[fastplotlib](https://github.com/fastplotlib/fastplotlib) is built on pygfx and provides a high-level API for scientific plotting.

### Basic fastplotlib Example

```python
import numpy as np
import yetty_pygfx

# Initialize yetty-pygfx integration
yetty_pygfx.init()

# Create a figure using yetty's WebGPU context
fig = yetty_pygfx.create_figure()

# Create data
x = np.linspace(0, 4*np.pi, 500)
y = np.sin(x)

# Add a line plot
line = fig[0, 0].add_line(np.column_stack([x, y]), thickness=5, cmap='viridis')

# Show the figure (registers the render callback)
fig.show()
```

### Multi-Panel Example

```python
import numpy as np
import yetty_pygfx

yetty_pygfx.init()

# Create a 2x2 grid of subplots
fig = yetty_pygfx.create_figure()

# Panel 1: Line plot
x = np.linspace(0, 10, 100)
fig[0, 0].add_line(np.column_stack([x, np.sin(x)]), cmap='hot')

# Panel 2: Scatter plot
points = np.random.randn(1000, 2)
fig[0, 1].add_scatter(points, sizes=5)

# Panel 3: Image
image = np.random.rand(100, 100)
fig[1, 0].add_image(image, cmap='viridis')

# Panel 4: Heatmap
heatmap = np.random.rand(50, 50)
fig[1, 1].add_heatmap(heatmap)

fig.show()
```

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        yetty (C++)                               │
│  ┌─────────────┐    ┌──────────────┐    ┌───────────────────┐  │
│  │ WebGPUContext│    │ PythonPlugin │    │   PythonLayer     │  │
│  │  - device   │────│  - Python    │────│  - render()       │  │
│  │  - queue    │    │    interp    │    │  - blitTexture()  │  │
│  │  - surface  │    │              │    │                   │  │
│  └─────────────┘    └──────────────┘    └───────────────────┘  │
│         │                  │                     │              │
│         │     ┌────────────┴────────────┐       │              │
│         │     │      yetty_wgpu.cpp     │       │              │
│         │     │  - get_device_handle()  │       │              │
│         │     │  - get_texture_handle() │       │              │
│         │     │  - create_render_texture│       │              │
│         │     └────────────┬────────────┘       │              │
└─────────│──────────────────│────────────────────│──────────────┘
          │                  │                    │
          ▼                  ▼                    ▼
┌─────────────────────────────────────────────────────────────────┐
│                     wgpu-native (shared)                         │
│                   libwgpu_native.so                              │
└─────────────────────────────────────────────────────────────────┘
          │                  │                    │
          │                  ▼                    │
          │    ┌─────────────────────────┐       │
          │    │    yetty_pygfx.py       │       │
          │    │  - YettyGPUAdapter      │       │
          │    │  - create_yetty_device()│       │
          │    │  - create_wgpu_texture()│       │
          │    │  - render_frame()       │       │
          │    └────────────┬────────────┘       │
          │                 │                    │
          │                 ▼                    │
          │    ┌─────────────────────────┐       │
          │    │       wgpu-py           │       │
          │    │  Uses same wgpu-native  │       │
          │    └────────────┬────────────┘       │
          │                 │                    │
          │                 ▼                    │
          │    ┌─────────────────────────┐       │
          │    │        pygfx            │       │
          │    │  - WgpuRenderer         │       │
          │    │  - Scene, Camera, etc   │       │
          │    └────────────┬────────────┘       │
          │                 │                    │
          │                 ▼                    │
          │    ┌─────────────────────────┐       │
          │    │     fastplotlib         │◄──────┘
          │    │  - Figure               │  (blit to screen)
          │    │  - Plots, Images, etc   │
          │    └─────────────────────────┘
          │
          └──────────────────────────────────────►  Screen
```

### Key Components

| File | Purpose |
|------|---------|
| `python.cpp` | Plugin implementation, Python interpreter embedding |
| `python.h` | Plugin and layer class declarations |
| `yetty_wgpu.cpp` | C extension module exposing WebGPU handles to Python |
| `yetty_wgpu.h` | C API for handle management |
| `yetty_pygfx.py` | Python module for pygfx/fastplotlib integration |

### Data Flow

1. **Initialization**: yetty creates WebGPU device/queue, creates render texture
2. **Handle Passing**: `yetty_wgpu` module exposes handles as integers to Python
3. **Adapter Injection**: `yetty_pygfx` wraps handles and injects into pygfx's `Shared` singleton
4. **Rendering**: pygfx renders to yetty's texture via shared wgpu-native
5. **Blitting**: C++ blit shader copies texture to screen surface

## Adding Other Python Modules

### Method 1: pip install (Recommended)

Packages are automatically installed to `~/.yetty/python-packages/`. To add more:

```python
# In your Python script
import subprocess
import sys

subprocess.check_call([
    sys.executable, '-m', 'pip', 'install',
    '--target', '/home/user/.yetty/python-packages',
    'your-package-name'
])
```

Or modify `setupPythonPackages()` in `python.cpp`:

```cpp
std::string installCmd = ldPath + embeddedPip +
    " install --target=" + pkgPath +
    " --quiet pygfx fastplotlib wgpu YOUR_PACKAGE_HERE 2>&1";
```

### Method 2: Creating a C Extension Module

For modules that need direct WebGPU access, create a C extension similar to `yetty_wgpu`:

1. **Create the module file** (`my_module.cpp`):

```cpp
#define PY_SSIZE_T_CLEAN
#include <Python.h>

static PyObject* my_function(PyObject* self, PyObject* args) {
    // Your implementation
    Py_RETURN_NONE;
}

static PyMethodDef MyModuleMethods[] = {
    {"my_function", my_function, METH_NOARGS, "Description"},
    {nullptr, nullptr, 0, nullptr}
};

static struct PyModuleDef my_module = {
    PyModuleDef_HEAD_INIT,
    "my_module",
    "My custom module",
    -1,
    MyModuleMethods
};

PyMODINIT_FUNC PyInit_my_module(void) {
    return PyModule_Create(&my_module);
}
```

2. **Register in `python.cpp`**:

```cpp
// Before Py_Initialize()
extern "C" PyObject* PyInit_my_module(void);
PyImport_AppendInittab("my_module", PyInit_my_module);
```

3. **Update CMakeLists.txt** to compile the new file.

### Method 3: Pure Python Module

Place Python files in `build-desktop-release/python/` and they'll be importable:

```python
# my_helper.py in build/python/
def helper_function():
    return "Hello from helper!"
```

```python
# In your script
import my_helper
print(my_helper.helper_function())
```

## Environment Variables

| Variable | Purpose |
|----------|---------|
| `YETTY_WGPU_LIB_PATH` | Path to shared wgpu-native library (set automatically) |
| `RENDERCANVAS_FORCE_OFFSCREEN` | Forces offscreen mode for rendercanvas (set to `1`) |

## Troubleshooting

### "yetty_wgpu not initialized"
Ensure you're running through yetty, not standalone Python.

### Blank screen
- Check that `yetty_pygfx.init()` is called before creating any pygfx objects
- Verify `fig.show()` is called for fastplotlib

### Import errors
Packages are installed to `~/.yetty/python-packages/`. Delete this folder to force reinstall.

### WebGPU errors
Ensure your GPU supports Vulkan. Check with:
```bash
vulkaninfo | head -20
```

## API Reference

### yetty_pygfx module

```python
yetty_pygfx.init()
    # Initialize the integration. Must be called first!
    # Returns: True on success

yetty_pygfx.create_figure(width=None, height=None)
    # Create a fastplotlib Figure
    # Returns: fastplotlib.Figure

yetty_pygfx.get_device()
    # Get the wrapped GPUDevice
    # Returns: wgpu.GPUDevice

yetty_pygfx.get_texture()
    # Get the pygfx Texture (render target)
    # Returns: pygfx.Texture

yetty_pygfx.render_frame()
    # Render one frame (called automatically by yetty)
    # Returns: True if rendered, False otherwise

yetty_pygfx.cleanup()
    # Clean up resources (called automatically)
```

### yetty_wgpu module (low-level)

```python
yetty_wgpu.get_device_handle()      # WGPUDevice as int
yetty_wgpu.get_queue_handle()       # WGPUQueue as int
yetty_wgpu.get_render_texture_handle()  # WGPUTexture as int
yetty_wgpu.get_render_texture_size()    # (width, height) tuple
yetty_wgpu.is_initialized()         # True if handles are set
```
