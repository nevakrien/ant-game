# Vulkan Utah Teapot

A small C11 Vulkan demo that loads the classic Utah teapot from an OBJ asset
and renders it through SDL2. Public headers are usable from C and C++.

## Dependencies

- CMake 3.20+
- Vulkan headers, loader, and a Vulkan-capable driver
- SDL2 development files
- `glslangValidator`

On Ubuntu, the development dependencies can be installed with:

```sh
sudo apt install build-essential cmake libsdl2-dev libvulkan-dev glslang-tools
```

## Build and run

```sh
cmake -S . -B build
cmake --build build
./build/vulkan_teapot
```

Pass another OBJ as the first argument to render a Blender export:

```sh
./build/vulkan_teapot path/to/model.obj
```

The teapot rotates automatically. The OBJ file's modification time is checked
twice per second and successfully saved changes are hot reloaded. Resize or
maximize the window to exercise swapchain recreation; press Escape to exit.

## OBJ loader

The reusable loader is exposed by `include/object.h`. Use
`object_mesh_load_obj_file()` for a path, or `object_mesh_load_obj_data()` for
a `char *` and byte count. The latter does not require null-terminated input.
Both return an `ObjectMesh` owned by the caller and released with
`object_mesh_destroy()`.
