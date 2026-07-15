#include "object.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct FloatArray {
    float *data;
    size_t count;
    size_t capacity;
} FloatArray;

typedef struct IndexArray {
    uint32_t *data;
    size_t count;
    size_t capacity;
} IndexArray;

typedef struct NavEdge {
    uint32_t first;
    uint32_t second;
    uint32_t triangle;
    uint32_t edge;
} NavEdge;

static const unsigned char navmesh_magic[8] = {'A', 'N', 'T', 'N', 'A', 'V', 1, 0};

static int append_floats(FloatArray *array, const float values[3])
{
    if (array->count + 3 > array->capacity) {
        size_t capacity = array->capacity ? array->capacity * 2 : 3072;
        float *data = realloc(array->data, capacity * sizeof(*data));
        if (!data) return -1;
        array->data = data;
        array->capacity = capacity;
    }
    memcpy(array->data + array->count, values, 3 * sizeof(*values));
    array->count += 3;
    return 0;
}

static int append_index(IndexArray *array, uint32_t value)
{
    if (array->count == array->capacity) {
        size_t capacity = array->capacity ? array->capacity * 2 : 6144;
        uint32_t *data = realloc(array->data, capacity * sizeof(*data));
        if (!data) return -1;
        array->data = data;
        array->capacity = capacity;
    }
    array->data[array->count++] = value;
    return 0;
}

static int parse_face_index(const char *token, size_t position_count, uint32_t *index)
{
    char *end;
    errno = 0;
    long value = strtol(token, &end, 10);
    if (end == token || errno == ERANGE || value == 0 || position_count > LONG_MAX ||
        (*end && *end != '/' && !isspace((unsigned char)*end)))
        return -1;

    long resolved = value > 0 ? value - 1 : (long)position_count + value;
    if (resolved < 0 || (size_t)resolved >= position_count || (unsigned long)resolved > UINT32_MAX)
        return -1;
    *index = (uint32_t)resolved;
    return 0;
}

static void normalize(float vector[3])
{
    float length = sqrtf(vector[0]*vector[0] + vector[1]*vector[1] + vector[2]*vector[2]);
    if (length > 0.000001f) {
        vector[0] /= length;
        vector[1] /= length;
        vector[2] /= length;
    }
}

static void center_and_scale(ObjectMesh *mesh)
{
    float minimum[3], maximum[3];
    memcpy(minimum, mesh->vertices[0].position, sizeof(minimum));
    memcpy(maximum, mesh->vertices[0].position, sizeof(maximum));
    for (uint32_t i = 1; i < mesh->vertex_count; ++i) {
        for (int axis = 0; axis < 3; ++axis) {
            float value = mesh->vertices[i].position[axis];
            if (value < minimum[axis]) minimum[axis] = value;
            if (value > maximum[axis]) maximum[axis] = value;
        }
    }
    float largest_span = maximum[0] - minimum[0];
    for (int axis = 1; axis < 3; ++axis) {
        float span = maximum[axis] - minimum[axis];
        if (span > largest_span) largest_span = span;
    }
    float scale = largest_span > 0.0f ? 3.5f / largest_span : 1.0f;
    for (uint32_t i = 0; i < mesh->vertex_count; ++i)
        for (int axis = 0; axis < 3; ++axis)
            mesh->vertices[i].position[axis] =
                (mesh->vertices[i].position[axis] - (minimum[axis] + maximum[axis]) * 0.5f) * scale;
}

static void generate_normals(ObjectMesh *mesh)
{
    for (uint32_t i = 0; i + 2 < mesh->index_count; i += 3) {
        ObjectVertex *a = &mesh->vertices[mesh->indices[i]];
        ObjectVertex *b = &mesh->vertices[mesh->indices[i + 1]];
        ObjectVertex *c = &mesh->vertices[mesh->indices[i + 2]];
        float ab[3], ac[3], normal[3];
        for (int axis = 0; axis < 3; ++axis) {
            ab[axis] = b->position[axis] - a->position[axis];
            ac[axis] = c->position[axis] - a->position[axis];
        }
        normal[0] = ab[1]*ac[2] - ab[2]*ac[1];
        normal[1] = ab[2]*ac[0] - ab[0]*ac[2];
        normal[2] = ab[0]*ac[1] - ab[1]*ac[0];
        for (int axis = 0; axis < 3; ++axis) {
            a->normal[axis] += normal[axis];
            b->normal[axis] += normal[axis];
            c->normal[axis] += normal[axis];
        }
    }
    for (uint32_t i = 0; i < mesh->vertex_count; ++i) normalize(mesh->vertices[i].normal);
}

static int parse_face(char *cursor, size_t position_count, IndexArray *indices)
{
    IndexArray face = {0};
    int failed = 0;

    while (!failed) {
        while (isspace((unsigned char)*cursor)) ++cursor;
        if (!*cursor || *cursor == '#') break;

        uint32_t index;
        if (parse_face_index(cursor, position_count, &index) || append_index(&face, index)) {
            failed = -1;
            break;
        }
        while (*cursor && !isspace((unsigned char)*cursor)) ++cursor;
    }

    if (face.count < 3) failed = -1;
    for (size_t i = 1; !failed && i + 1 < face.count; ++i) {
        if (append_index(indices, face.data[0]) || append_index(indices, face.data[i]) ||
            append_index(indices, face.data[i + 1]))
            failed = -1;
    }
    free(face.data);
    return failed;
}

static int parse_obj(char *text, size_t count, FloatArray *positions, IndexArray *indices)
{
    char *cursor = text;
    char *end = text + count;

    /*
     * This renderer needs only positions and triangle indices, so the accepted
     * OBJ subset is deliberately small:
     *
     *   v x y z
     *   f p1 p2 p3 ...
     *
     * Face references may also use p/t, p//n, or p/t/n; only the position (p)
     * is read. Positive OBJ indices are one-based and negative indices are
     * relative to the positions parsed so far. Polygon faces are converted to
     * a triangle fan. Other records (vt, vn, o, g, usemtl, etc.) are ignored.
     * Normals are regenerated because this compact mesh stores one vertex per
     * position and therefore cannot preserve OBJ's separate normal indices.
     */
    while (cursor < end) {
        char *line = cursor;
        while (cursor < end && *cursor != '\n') ++cursor;
        if (cursor < end) *cursor++ = '\0';

        while (isspace((unsigned char)*line)) ++line;
        if (line[0] == 'v' && isspace((unsigned char)line[1])) {
            float values[3];
            char *value = line + 1;
            for (int i = 0; i < 3; ++i) {
                char *next;
                values[i] = strtof(value, &next);
                if (next == value) return -1;
                value = next;
            }
            if (append_floats(positions, values)) return -1;
        } else if (line[0] == 'f' && isspace((unsigned char)line[1])) {
            if (parse_face(line + 1, positions->count / 3, indices)) return -1;
        }
    }
    return 0;
}

int object_mesh_load_obj_data(const char *data, size_t count, ObjectMesh *mesh)
{
    FloatArray positions = {0};
    IndexArray indices = {0};
    char *text;
    int failed;

    if (!data || !count || !mesh || count == SIZE_MAX) return -1;
    memset(mesh, 0, sizeof(*mesh));

    text = malloc(count + 1);
    if (!text) return -1;
    memcpy(text, data, count);
    text[count] = '\0';

    failed = parse_obj(text, count, &positions, &indices);
    free(text);
    if (failed || positions.count == 0 || indices.count == 0 ||
        positions.count / 3 > UINT32_MAX || indices.count > UINT32_MAX) {
        free(positions.data);
        free(indices.data);
        return -1;
    }

    mesh->vertex_count = (uint32_t)(positions.count / 3);
    mesh->index_count = (uint32_t)indices.count;
    mesh->vertices = calloc(mesh->vertex_count, sizeof(*mesh->vertices));
    mesh->indices = indices.data;
    if (!mesh->vertices) {
        free(positions.data);
        object_mesh_destroy(mesh);
        return -1;
    }
    for (uint32_t i = 0; i < mesh->vertex_count; ++i)
        memcpy(mesh->vertices[i].position, positions.data + i*3, sizeof(mesh->vertices[i].position));
    free(positions.data);

    center_and_scale(mesh);
    generate_normals(mesh);
    return 0;
}

int object_mesh_load_obj_file(const char *path, ObjectMesh *mesh)
{
    FILE *file;
    char *data;
    long length;
    int result;

    if (!path || !mesh) return -1;
    memset(mesh, 0, sizeof(*mesh));

    file = fopen(path, "rb");
    if (!file) return -1;
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return -1;
    }

    data = malloc((size_t)length);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return -1;
    }
    fclose(file);

    result = object_mesh_load_obj_data(data, (size_t)length, mesh);
    free(data);
    return result;
}

void object_mesh_destroy(ObjectMesh *mesh)
{
    if (!mesh) return;
    free(mesh->vertices);
    free(mesh->indices);
    memset(mesh, 0, sizeof(*mesh));
}

static int point_inside_edge(const float point[3], const float start[3], const float end[3],
                             float *distance)
{
    float direction[3], offset[3];
    float length_squared = 0.0f;
    float projection = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        direction[axis] = end[axis] - start[axis];
        offset[axis] = point[axis] - start[axis];
        length_squared += direction[axis] * direction[axis];
        projection += offset[axis] * direction[axis];
    }
    if (length_squared <= 1.0e-12f) return 0;

    float t = projection / length_squared;
    if (t <= 1.0e-5f || t >= 1.0f - 1.0e-5f) return 0;

    float error_squared = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        float error = offset[axis] - t * direction[axis];
        error_squared += error * error;
    }
    if (error_squared > length_squared * 1.0e-10f) return 0;
    *distance = t;
    return 1;
}

static int split_edges(ObjectMesh *mesh)
{
    for (;;) {
        uint32_t split_triangle = UINT32_MAX;
        uint32_t split_edge = 0;
        uint32_t split_vertex = 0;
        float nearest = 2.0f;

        for (uint32_t triangle = 0; triangle < mesh->index_count / 3; ++triangle) {
            uint32_t *indices = mesh->indices + triangle * 3;
            for (uint32_t edge = 0; edge < 3; ++edge) {
                uint32_t start = indices[edge];
                uint32_t end = indices[(edge + 1) % 3];
                for (uint32_t vertex = 0; vertex < mesh->vertex_count; ++vertex) {
                    float distance;
                    if (vertex != start && vertex != end &&
                        point_inside_edge(mesh->vertices[vertex].position,
                                          mesh->vertices[start].position,
                                          mesh->vertices[end].position, &distance) &&
                        distance < nearest) {
                        split_triangle = triangle;
                        split_edge = edge;
                        split_vertex = vertex;
                        nearest = distance;
                    }
                }
                if (split_triangle != UINT32_MAX) break;
            }
            if (split_triangle != UINT32_MAX) break;
        }
        if (split_triangle == UINT32_MAX) return 0;
        if (mesh->index_count > UINT32_MAX - 3) return -1;

        uint32_t *indices = realloc(mesh->indices,
                                    ((size_t)mesh->index_count + 3) * sizeof(*mesh->indices));
        if (!indices) return -1;
        mesh->indices = indices;

        uint32_t *triangle = mesh->indices + split_triangle * 3;
        uint32_t start = triangle[split_edge];
        uint32_t end = triangle[(split_edge + 1) % 3];
        uint32_t opposite = triangle[(split_edge + 2) % 3];
        triangle[0] = start;
        triangle[1] = split_vertex;
        triangle[2] = opposite;
        mesh->indices[mesh->index_count] = split_vertex;
        mesh->indices[mesh->index_count + 1] = end;
        mesh->indices[mesh->index_count + 2] = opposite;
        mesh->index_count += 3;
    }
}

static int compare_edges(const void *left_pointer, const void *right_pointer)
{
    const NavEdge *left = left_pointer;
    const NavEdge *right = right_pointer;
    if (left->first != right->first) return left->first < right->first ? -1 : 1;
    if (left->second != right->second) return left->second < right->second ? -1 : 1;
    return 0;
}

static int build_neighbors(ObjectNavMesh *navmesh)
{
    uint32_t edge_count = navmesh->mesh.index_count;
    NavEdge *edges = malloc((size_t)edge_count * sizeof(*edges));
    navmesh->neighbors = malloc((size_t)edge_count * sizeof(*navmesh->neighbors));
    if (!edges || !navmesh->neighbors) {
        free(edges);
        return -1;
    }

    for (uint32_t i = 0; i < edge_count; ++i) {
        uint32_t triangle = i / 3;
        uint32_t edge = i % 3;
        uint32_t first = navmesh->mesh.indices[i];
        uint32_t second = navmesh->mesh.indices[triangle * 3 + (edge + 1) % 3];
        edges[i].first = first < second ? first : second;
        edges[i].second = first < second ? second : first;
        edges[i].triangle = triangle;
        edges[i].edge = edge;
        navmesh->neighbors[i] = OBJECT_NAVMESH_NO_NEIGHBOR;
    }
    qsort(edges, edge_count, sizeof(*edges), compare_edges);

    for (uint32_t i = 0; i < edge_count;) {
        uint32_t end = i + 1;
        while (end < edge_count && edges[end].first == edges[i].first &&
               edges[end].second == edges[i].second)
            ++end;
        if (end - i > 2) {
            free(edges);
            return -1;
        }
        if (end - i == 2) {
            NavEdge *a = &edges[i];
            NavEdge *b = &edges[i + 1];
            if (a->triangle == b->triangle) {
                free(edges);
                return -1;
            }
            navmesh->neighbors[a->triangle * 3 + a->edge] = b->triangle;
            navmesh->neighbors[b->triangle * 3 + b->edge] = a->triangle;
        }
        i = end;
    }
    free(edges);
    return 0;
}

int object_navmesh_build(ObjectMesh *mesh, ObjectNavMesh *navmesh)
{
    if (!mesh || !navmesh || !mesh->vertices || !mesh->indices ||
        !mesh->vertex_count || !mesh->index_count || mesh->index_count % 3)
        return -1;
    memset(navmesh, 0, sizeof(*navmesh));
    for (uint32_t i = 0; i < mesh->index_count; ++i)
        if (mesh->indices[i] >= mesh->vertex_count) return -1;

    if (split_edges(mesh)) return -1;
    navmesh->mesh = *mesh;
    if (build_neighbors(navmesh)) {
        free(navmesh->neighbors);
        memset(navmesh, 0, sizeof(*navmesh));
        return -1;
    }
    memset(mesh, 0, sizeof(*mesh));
    return 0;
}

static int write_bytes(FILE *file, const void *data, size_t size)
{
    return fwrite(data, 1, size, file) == size ? 0 : -1;
}

static int write_u32(FILE *file, uint32_t value)
{
    unsigned char bytes[4] = {
        (unsigned char)value, (unsigned char)(value >> 8),
        (unsigned char)(value >> 16), (unsigned char)(value >> 24)
    };
    return write_bytes(file, bytes, sizeof(bytes));
}

static int write_float(FILE *file, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return write_u32(file, bits);
}

int object_navmesh_save_file(const char *path, const ObjectNavMesh *navmesh)
{
    if (!path || !navmesh || !navmesh->mesh.vertices || !navmesh->mesh.indices ||
        !navmesh->neighbors || !navmesh->mesh.vertex_count || !navmesh->mesh.index_count ||
        navmesh->mesh.index_count % 3)
        return -1;

    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    int failed = write_bytes(file, navmesh_magic, sizeof(navmesh_magic)) ||
                 write_u32(file, navmesh->mesh.vertex_count) ||
                 write_u32(file, navmesh->mesh.index_count / 3);
    for (uint32_t i = 0; !failed && i < navmesh->mesh.vertex_count; ++i)
        for (int component = 0; !failed && component < 3; ++component) {
            failed = write_float(file, navmesh->mesh.vertices[i].position[component]);
            if (!failed) failed = write_float(file, navmesh->mesh.vertices[i].normal[component]);
        }
    for (uint32_t i = 0; !failed && i < navmesh->mesh.index_count; ++i)
        failed = write_u32(file, navmesh->mesh.indices[i]);
    for (uint32_t i = 0; !failed && i < navmesh->mesh.index_count; ++i)
        failed = write_u32(file, navmesh->neighbors[i]);
    if (fclose(file)) failed = -1;
    return failed ? -1 : 0;
}

static int read_bytes(FILE *file, void *data, size_t size)
{
    return fread(data, 1, size, file) == size ? 0 : -1;
}

static int read_u32(FILE *file, uint32_t *value)
{
    unsigned char bytes[4];
    if (read_bytes(file, bytes, sizeof(bytes))) return -1;
    *value = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
             (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
    return 0;
}

static int read_float(FILE *file, float *value)
{
    uint32_t bits;
    if (read_u32(file, &bits)) return -1;
    memcpy(value, &bits, sizeof(bits));
    return 0;
}

int object_navmesh_load_file(const char *path, ObjectNavMesh *navmesh)
{
    unsigned char magic[sizeof(navmesh_magic)];
    uint32_t triangle_count;
    if (!path || !navmesh) return -1;
    memset(navmesh, 0, sizeof(*navmesh));

    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    int failed = read_bytes(file, magic, sizeof(magic)) ||
                 memcmp(magic, navmesh_magic, sizeof(magic)) ||
                 read_u32(file, &navmesh->mesh.vertex_count) ||
                 read_u32(file, &triangle_count) || !navmesh->mesh.vertex_count ||
                 !triangle_count || triangle_count > UINT32_MAX / 3;
    if (!failed) {
        navmesh->mesh.index_count = triangle_count * 3;
        navmesh->mesh.vertices = malloc((size_t)navmesh->mesh.vertex_count *
                                        sizeof(*navmesh->mesh.vertices));
        navmesh->mesh.indices = malloc((size_t)navmesh->mesh.index_count *
                                       sizeof(*navmesh->mesh.indices));
        navmesh->neighbors = malloc((size_t)navmesh->mesh.index_count *
                                    sizeof(*navmesh->neighbors));
        if (!navmesh->mesh.vertices || !navmesh->mesh.indices || !navmesh->neighbors)
            failed = -1;
    }
    for (uint32_t i = 0; !failed && i < navmesh->mesh.vertex_count; ++i)
        for (int component = 0; !failed && component < 3; ++component) {
            failed = read_float(file, &navmesh->mesh.vertices[i].position[component]);
            if (!failed) failed = read_float(file, &navmesh->mesh.vertices[i].normal[component]);
        }
    for (uint32_t i = 0; !failed && i < navmesh->mesh.index_count; ++i) {
        failed = read_u32(file, &navmesh->mesh.indices[i]);
        if (!failed && navmesh->mesh.indices[i] >= navmesh->mesh.vertex_count) failed = -1;
    }
    for (uint32_t i = 0; !failed && i < navmesh->mesh.index_count; ++i) {
        failed = read_u32(file, &navmesh->neighbors[i]);
        if (!failed && navmesh->neighbors[i] != OBJECT_NAVMESH_NO_NEIGHBOR &&
            navmesh->neighbors[i] >= triangle_count)
            failed = -1;
    }
    fclose(file);
    if (failed) object_navmesh_destroy(navmesh);
    return failed ? -1 : 0;
}

void object_navmesh_destroy(ObjectNavMesh *navmesh)
{
    if (!navmesh) return;
    object_mesh_destroy(&navmesh->mesh);
    free(navmesh->neighbors);
    memset(navmesh, 0, sizeof(*navmesh));
}
