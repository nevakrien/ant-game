#include "object.h"

#include <stdio.h>
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

int main(int argc, char **argv)
{
    ObjectMesh mesh;

    if (test_data_loader()) {
        fprintf(stderr, "counted OBJ data test failed\n");
        return 1;
    }
    if (argc != 2 || object_mesh_load_obj_file(argv[1], &mesh)) {
        fprintf(stderr, "OBJ file test failed\n");
        return 1;
    }
    if (!mesh.vertex_count || !mesh.index_count) {
        object_mesh_destroy(&mesh);
        return 1;
    }
    object_mesh_destroy(&mesh);
    return 0;
}
