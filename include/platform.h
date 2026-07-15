#ifndef ANT_GAME_PLATFORM_H
#define ANT_GAME_PLATFORM_H

#include "object.h"

#include <stddef.h>
#include <stdint.h>

typedef struct Platform Platform;
typedef uint32_t MeshHandle;
typedef uint32_t ModelHandle;
typedef uint32_t TransformHandle;

typedef struct Model {
    MeshHandle mesh_handle;
    float base_color[3];
    float rim_color[3];
} Model;

typedef struct Input {
    int quit_requested;
    float pointer_x;
    float pointer_y;
} Input;

typedef struct Scene {
    float view_projection[16];
    float light_direction[4];
} Scene;

typedef struct Transform {
    float position[3];

    /* Quaternion components are ordered x, y, z, w. */
    float rotation[4];
} Transform;

typedef struct Ant {
    TransformHandle transform_handle;
    uint32_t current_triangle;
    float speed;
    float position[3];
    float tangent[3];
} Ant;

Platform *platform_create(const char *title, int width, int height);
void platform_destroy(Platform *platform);
int platform_add_mesh(Platform *platform, const ObjectMesh *mesh, MeshHandle *mesh_handle);
int platform_update_mesh(Platform *platform, MeshHandle mesh_handle, const ObjectMesh *mesh);
int platform_add_model(Platform *platform, const Model *model, ModelHandle *model_handle);
int platform_add_transform(Platform *platform, const Transform *transform,
                           TransformHandle *transform_handle);
int platform_update_transform(Platform *platform, TransformHandle transform_handle,
                              const Transform *transform);
int platform_add_drawable(Platform *platform, ModelHandle model_handle,
                          TransformHandle transform_handle);
int platform_add_antable(Platform *platform, TransformHandle surface_transform,
                          const ObjectNavMesh *navmesh, const Ant *ants,
                          size_t ant_count);
void platform_step_ants(Platform *platform, float delta_seconds);
void platform_poll_input(Platform *platform, Input *input);
float platform_aspect_ratio(const Platform *platform);
int platform_draw(Platform *platform, const Scene *scene);

#endif
