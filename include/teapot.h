#ifndef ANT_GAME_TEAPOT_H
#define ANT_GAME_TEAPOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TeapotVertex {
    float position[3];
    float normal[3];
} TeapotVertex;

typedef struct TeapotMesh {
    TeapotVertex *vertices;
    uint32_t *indices;
    uint32_t vertex_count;
    uint32_t index_count;
} TeapotMesh;

/* Loads an OBJ, triangulates polygon faces, and generates smooth normals. */
int teapot_mesh_load_obj(const char *path, TeapotMesh *mesh);
void teapot_mesh_destroy(TeapotMesh *mesh);

#ifdef __cplusplus
}
#endif

#endif
