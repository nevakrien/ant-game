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

Three instances of the teapot rotate automatically. The OBJ file's modification time is checked
twice per second and successfully saved changes are hot reloaded. Resize or
maximize the window to exercise swapchain recreation; press Escape to exit.

## Drawing objects

Meshes are uploaded once with `platform_add_mesh()` and kept in the renderer's
mesh registry. It returns a stable `PlatformMeshIndex` that can be reused by any
number of `PlatformObject` entries. Each object contains only that mesh index, a
quaternion in `x, y, z, w` order, and a position. A zero quaternion is treated
as the identity rotation. Pass the frame's object array
to `platform_draw()`; the renderer binds each stored mesh and issues one
`vkCmdDrawIndexed()` call per object. `platform_update_mesh()` replaces an
uploaded mesh without invalidating its index.

## OBJ loader

The reusable loader is exposed by `include/object.h`. Use
`object_mesh_load_obj_file()` for a path, or `object_mesh_load_obj_data()` for
a `char *` and byte count. The latter does not require null-terminated input.
Both return an `ObjectMesh` owned by the caller and released with
`object_mesh_destroy()`.

## Navmesh conversion

`navmesh_tool` converts an OBJ into a conforming triangle navmesh. It splits
triangle edges at existing vertices (resolving T-junctions), computes one
neighbor triangle index per edge, and saves both the refined mesh and neighbor
array in a versioned `.nav` file:

```sh
./build/navmesh_tool path/to/navigation.obj path/to/navigation.nav
```

Load the result with `object_navmesh_load_file()` and release it with
`object_navmesh_destroy()`. `ObjectNavMesh.neighbors` has three entries per
triangle in index order: entry `triangle * 3 + edge` describes the edge from
index `edge` to `(edge + 1) % 3`. Boundary edges contain
`OBJECT_NAVMESH_NO_NEIGHBOR`. The existing `ObjectMesh` APIs and rendering path
are unchanged. `object_navmesh_build()` refines an `ObjectMesh` in place and,
on success, transfers its vertex and index buffers into the resulting
`ObjectNavMesh` without copying them; the source mesh is then cleared.
