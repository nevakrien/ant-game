#include "object.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_data_loader(void)
{
    const char obj[] =
        "# A quad using relative position/texture/normal references.\n"
        "  v 0 0 0\n"
        "v 1 0 0\n"
        "v 1 1 0\n"
        "v 0 1 0\n"
        "f -4/1/1 -3/2/1 -2/3/1 -1/4/1 # triangulated as a fan";
    ObjectMesh mesh;

    /* Excluding the string terminator verifies the counted-buffer contract. */
    if (object_mesh_load_obj_data(obj, sizeof(obj) - 1, &mesh)) return -1;
    if (mesh.vertex_count != 4 || mesh.index_count != 6) {
        object_mesh_destroy(&mesh);
        return -1;
    }
    object_mesh_destroy(&mesh);
    return mesh.vertices || mesh.indices || mesh.vertex_count || mesh.index_count ? -1 : 0;
}

static int test_navmesh(const char *path)
{
    const char obj[] =
        "v 0 0 0\n"
        "v 2 0 0\n"
        "v 0 1 0\n"
        "v 1 0 0\n"
        "v 1 -1 0\n"
        "f 1 2 3\n"
        "f 4 1 5\n"
        "f 2 4 5\n";
    ObjectMesh source;
    ObjectNavMesh built;
    ObjectNavMesh loaded;
    ObjectVertex *source_vertices;
    int result = -1;

    if (object_mesh_load_obj_data(obj, sizeof(obj) - 1, &source)) return -1;
    source_vertices = source.vertices;
    if (object_navmesh_build(&source, &built)) goto destroy_source;
    if (built.mesh.vertices != source_vertices || built.mesh.index_count != 12 ||
        source.vertices || source.indices || source.vertex_count || source.index_count)
        goto destroy_built;

    uint32_t connected_edges = 0;
    for (uint32_t i = 0; i < built.mesh.index_count; ++i) {
        uint32_t neighbor = built.neighbors[i];
        if (neighbor == OBJECT_NAVMESH_NO_NEIGHBOR) continue;
        if (neighbor >= built.mesh.index_count / 3) goto destroy_built;
        ++connected_edges;
    }
    if (connected_edges != 8 || object_navmesh_save_file(path, &built) ||
        object_navmesh_load_file(path, &loaded))
        goto destroy_built;
    if (loaded.mesh.vertex_count != built.mesh.vertex_count ||
        loaded.mesh.index_count != built.mesh.index_count ||
        memcmp(loaded.mesh.vertices, built.mesh.vertices,
               (size_t)built.mesh.vertex_count * sizeof(*built.mesh.vertices)) ||
        memcmp(loaded.mesh.indices, built.mesh.indices,
               (size_t)built.mesh.index_count * sizeof(*built.mesh.indices)) ||
        memcmp(loaded.neighbors, built.neighbors,
               (size_t)built.mesh.index_count * sizeof(*built.neighbors)))
        goto destroy_loaded;
    result = 0;

destroy_loaded:
    object_navmesh_destroy(&loaded);
destroy_built:
    object_navmesh_destroy(&built);
destroy_source:
    object_mesh_destroy(&source);
    remove(path);
    return result;
}

int main(int argc, char **argv)
{
    ObjectMesh mesh;

    if (test_data_loader()) {
        fprintf(stderr, "counted OBJ data test failed\n");
        return 1;
    }
    if (argc != 3 || object_mesh_load_obj_file(argv[1], &mesh)) {
        fprintf(stderr, "OBJ file test failed\n");
        return 1;
    }
    if (!mesh.vertex_count || !mesh.index_count) {
        object_mesh_destroy(&mesh);
        return 1;
    }
    object_mesh_destroy(&mesh);
    if (test_navmesh(argv[2])) {
        fprintf(stderr, "navmesh test failed\n");
        return 1;
    }
    return 0;
}
