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

Pass another OBJ as the first argument to render a Blender export. Generate its
navmesh once with `navmesh_tool`; the game loads the sibling `.nav` file and
does not calculate navmeshes during startup:

```sh
./build/navmesh_tool path/to/model.obj path/to/model.nav
./build/vulkan_teapot path/to/model.obj
```

Three instances of the teapot rotate automatically, each carrying a GPU-driven
swarm of ants. Resize or maximize the window to exercise swapchain recreation;
press Escape to exit.

## Drawing meshes

Meshes are uploaded once with `render_add_mesh()` and kept in the renderer's
mesh registry. Transforms are stored separately with `render_add_transform()`
and updated with `render_update_transform()`. `render_add_drawable()` pairs
a mesh handle with a transform handle for rendering. This keeps mesh selection
out of simulation-facing transform handles while still letting multiple meshes
share one transform. Transforms live in one GPU storage buffer, and a zero
quaternion is treated as the identity rotation.

## Antable objects

`render_add_antable()` associates one surface transform with a navmesh and a
buffer of compact ant tokens. The container stores the surface handle and
navmesh once. Each token contains only its rendered transform handle, current
triangle, speed, navmesh-local position, and tangent. `render_step_ants()`
queues elapsed time for the next draw. Before the render pass, the ant compute
shader traverses triangle neighbors, parallel-transports each tangent across
folds, composes the local ant pose with the current surface transform, and
writes the ant's normal transform. Consequently, ants follow moving and
rotating surface instances without a separate ant graphics pipeline.

A future world-space token container can hold free-moving ants. Token transfer
between that container and an antable would provide explicit attach and detach
behavior without making every surface token carry a parent or navmesh handle.

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

The normal build runs this conversion for `assets/teapot.obj`, producing
`build/assets/teapot.nav`. CMake regenerates it only when the source OBJ or
converter changes; the game only loads the generated binary at startup.

Load the result with `object_navmesh_load_file()` and release it with
`object_navmesh_destroy()`. `ObjectNavMesh.neighbors` has three entries per
triangle in index order: entry `triangle * 3 + edge` describes the edge from
index `edge` to `(edge + 1) % 3`. Boundary edges contain
`OBJECT_NAVMESH_NO_NEIGHBOR`. The existing `ObjectMesh` APIs and rendering path
are unchanged. `object_navmesh_build()` refines an `ObjectMesh` in place and,
on success, transfers its vertex and index buffers into the resulting
`ObjectNavMesh` without copying them; the source mesh is then cleared.
