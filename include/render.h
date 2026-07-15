#ifndef ANT_GAME_RENDER_H
#define ANT_GAME_RENDER_H

#include "object.h"
#include "platform.h"

#include <stddef.h>
#include <stdint.h>

typedef uint32_t MeshHandle;
typedef uint32_t ModelHandle;
typedef uint32_t TransformHandle;

typedef struct Model {
    MeshHandle mesh_handle;
    float base_color[3];
    float rim_color[3];
} Model;

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

int render_add_mesh(Platform *platform, const ObjectMesh *mesh, MeshHandle *mesh_handle);
int render_update_mesh(Platform *platform, MeshHandle mesh_handle, const ObjectMesh *mesh);
int render_add_model(Platform *platform, const Model *model, ModelHandle *model_handle);
int render_add_transform(Platform *platform, const Transform *transform,
                         TransformHandle *transform_handle);
int render_update_transform(Platform *platform, TransformHandle transform_handle,
                            const Transform *transform);
int render_add_drawable(Platform *platform, ModelHandle model_handle,
                        TransformHandle transform_handle);
int render_add_antable(Platform *platform, TransformHandle surface_transform,
                       const ObjectNavMesh *navmesh, const Ant *ants,
                       size_t ant_count);
void render_step_ants(Platform *platform, float delta_seconds);
int render_draw(Platform *platform, const Scene *scene);

#endif
