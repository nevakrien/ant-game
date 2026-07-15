#include "teapot.h"

#include <ctype.h>
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
    long value = strtol(token, &end, 10);
    if (end == token || value == 0) return -1;
    long resolved = value > 0 ? value - 1 : (long)position_count + value;
    if (resolved < 0 || (size_t)resolved >= position_count) return -1;
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

static void center_and_scale(TeapotMesh *mesh)
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

static void generate_normals(TeapotMesh *mesh)
{
    for (uint32_t i = 0; i + 2 < mesh->index_count; i += 3) {
        TeapotVertex *a = &mesh->vertices[mesh->indices[i]];
        TeapotVertex *b = &mesh->vertices[mesh->indices[i + 1]];
        TeapotVertex *c = &mesh->vertices[mesh->indices[i + 2]];
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

int teapot_mesh_load_obj(const char *path, TeapotMesh *mesh)
{
    FILE *file;
    FloatArray positions = {0};
    IndexArray indices = {0};
    char line[4096];
    int failed = 0;
    if (!path || !mesh) return -1;
    memset(mesh, 0, sizeof(*mesh));
    file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "Could not open OBJ asset: %s\n", path);
        return -1;
    }

    while (!failed && fgets(line, sizeof(line), file)) {
        if (line[0] == 'v' && isspace((unsigned char)line[1])) {
            float value[3];
            if (sscanf(line + 1, "%f %f %f", &value[0], &value[1], &value[2]) == 3)
                failed = append_floats(&positions, value);
        } else if (line[0] == 'f' && isspace((unsigned char)line[1])) {
            uint32_t face[128];
            uint32_t face_count = 0;
            char *cursor = line + 1;
            while (*cursor && face_count < 128) {
                while (isspace((unsigned char)*cursor)) ++cursor;
                if (!*cursor || *cursor == '\n' || *cursor == '\r') break;
                if (parse_face_index(cursor, positions.count / 3, &face[face_count++])) { failed = -1; break; }
                while (*cursor && !isspace((unsigned char)*cursor)) ++cursor;
            }
            for (uint32_t i = 1; !failed && i + 1 < face_count; ++i) {
                failed = append_index(&indices, face[0]) || append_index(&indices, face[i]) ||
                    append_index(&indices, face[i + 1]);
            }
        }
    }
    fclose(file);
    if (failed || positions.count == 0 || indices.count == 0 || positions.count / 3 > UINT32_MAX || indices.count > UINT32_MAX) {
        fprintf(stderr, "Invalid or unsupported OBJ asset: %s\n", path);
        free(positions.data); free(indices.data);
        return -1;
    }

    mesh->vertex_count = (uint32_t)(positions.count / 3);
    mesh->index_count = (uint32_t)indices.count;
    mesh->vertices = calloc(mesh->vertex_count, sizeof(*mesh->vertices));
    mesh->indices = indices.data;
    if (!mesh->vertices) {
        free(positions.data);
        teapot_mesh_destroy(mesh);
        return -1;
    }
    for (uint32_t i = 0; i < mesh->vertex_count; ++i)
        memcpy(mesh->vertices[i].position, positions.data + i*3, sizeof(mesh->vertices[i].position));
    free(positions.data);
    center_and_scale(mesh);
    generate_normals(mesh);
    return 0;
}

void teapot_mesh_destroy(TeapotMesh *mesh)
{
    if (!mesh) return;
    free(mesh->vertices);
    free(mesh->indices);
    memset(mesh, 0, sizeof(*mesh));
}
