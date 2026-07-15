#ifndef ANT_GAME_PLATFORM_H
#define ANT_GAME_PLATFORM_H

#include "object.h"

#include <stddef.h>
#include <stdint.h>

typedef struct Platform Platform;
typedef uint32_t PlatformMeshIndex;

typedef struct PlatformInput {
    int quit_requested;
    float pointer_x;
    float pointer_y;
} PlatformInput;

typedef struct PlatformScene {
    float view_projection[16];
    float light_direction[4];
} PlatformScene;

typedef struct PlatformObject {
    PlatformMeshIndex mesh_index;
    /* Quaternion components are ordered x, y, z, w. */
    float rotation[4];
    float position[3];
} PlatformObject;

Platform *platform_create(const char *title, int width, int height);
void platform_destroy(Platform *platform);
int platform_add_mesh(Platform *platform, const ObjectMesh *mesh, PlatformMeshIndex *mesh_index);
int platform_update_mesh(Platform *platform, PlatformMeshIndex mesh_index, const ObjectMesh *mesh);
void platform_poll_input(Platform *platform, PlatformInput *input);
float platform_aspect_ratio(const Platform *platform);
int platform_draw(Platform *platform, const PlatformScene *scene,
                  const PlatformObject *objects, size_t object_count);

#endif
