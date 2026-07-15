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

#define OBJECT_NAVMESH_NO_NEIGHBOR UINT32_MAX

typedef struct ObjectNavMesh {
    ObjectMesh mesh;
    /* Three entries per triangle, one for each (i, i + 1) edge. */
    uint32_t *neighbors;
} ObjectNavMesh;

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

/*
 * Builds a conforming triangle mesh by splitting edges at vertices that lie
 * inside them. On success, ownership of mesh's buffers moves to navmesh and
 * mesh is cleared. On failure, mesh remains caller-owned and valid, though it
 * may have been partially refined. Neighbor entries contain triangle indices,
 * or OBJECT_NAVMESH_NO_NEIGHBOR for boundary edges.
 */
int object_navmesh_build(ObjectMesh *mesh, ObjectNavMesh *navmesh);
int object_navmesh_save_file(const char *path, const ObjectNavMesh *navmesh);
int object_navmesh_load_file(const char *path, ObjectNavMesh *navmesh);
void object_navmesh_destroy(ObjectNavMesh *navmesh);

#ifdef __cplusplus
}
#endif

#endif
