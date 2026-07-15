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
