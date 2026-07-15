#include "object.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    ObjectMesh source;
    ObjectNavMesh navmesh;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s input.obj output.nav\n", argv[0]);
        return 2;
    }
    if (object_mesh_load_obj_file(argv[1], &source)) {
        fprintf(stderr, "Could not load OBJ: %s\n", argv[1]);
        return 1;
    }
    if (object_navmesh_build(&source, &navmesh)) {
        fprintf(stderr, "Could not build navmesh (the mesh may be non-manifold)\n");
        object_mesh_destroy(&source);
        return 1;
    }
    if (object_navmesh_save_file(argv[2], &navmesh)) {
        fprintf(stderr, "Could not write navmesh: %s\n", argv[2]);
        object_navmesh_destroy(&navmesh);
        object_mesh_destroy(&source);
        return 1;
    }

    printf("Wrote %u vertices and %u triangles to %s\n",
           navmesh.mesh.vertex_count, navmesh.mesh.index_count / 3, argv[2]);
    object_navmesh_destroy(&navmesh);
    object_mesh_destroy(&source);
    return 0;
}
