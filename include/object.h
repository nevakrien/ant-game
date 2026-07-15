#ifndef ANT_GAME_OBJECT_H
#define ANT_GAME_OBJECT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ObjectVertex {
    float position[3];
    float normal[3];
} ObjectVertex;

typedef struct ObjectMesh {
    ObjectVertex *vertices;
    uint32_t *indices;
    uint32_t vertex_count;
    uint32_t index_count;
} ObjectMesh;

/*
 * Loads an OBJ from a file or a byte buffer. The data buffer does not need to
 * be null terminated and is only borrowed for the duration of the call.
 *
 * Both functions triangulate polygon faces, generate smooth normals, and
 * center and scale the result. Call object_mesh_destroy() on a loaded mesh.
 */
int object_mesh_load_obj_file(const char *path, ObjectMesh *mesh);
int object_mesh_load_obj_data(const char *data, size_t count, ObjectMesh *mesh);
void object_mesh_destroy(ObjectMesh *mesh);

#ifdef __cplusplus
}
#endif

#endif
