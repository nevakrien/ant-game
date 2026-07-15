#include "render.h"
#include "platform_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int render_add_model(Platform *platform, const Model *model, ModelHandle *model_handle)
{
    if (!platform || !model || !model_handle || model->mesh_handle >= platform->mesh_count ||
        platform->model_count >= UINT32_MAX) return -1;
    for (uint32_t i = 0; i < 3; ++i)
        if (!isfinite(model->base_color[i]) || !isfinite(model->rim_color[i]) ||
            model->base_color[i] < 0.0f || model->rim_color[i] < 0.0f) return -1;
    if (platform->model_count == platform->model_capacity) {
        size_t capacity = platform->model_capacity ? platform->model_capacity * 2 : 4;
        PlatformModel *models = realloc(platform->models, capacity * sizeof(*models));
        if (!models) return -1;
        platform->models = models;
        platform->model_capacity = capacity;
    }
    *model_handle = (ModelHandle)platform->model_count;
    platform->models[platform->model_count++] = (PlatformModel){.model = *model};
    platform->instances_dirty = 1;
    return 0;
}

int render_add_transform(Platform *platform, const Transform *transform,
                         TransformHandle *transform_handle)
{
    if (!platform || !transform || !transform_handle ||
        platform->transform_count >= MAX_TRANSFORMS) return -1;
    for (uint32_t i = 0; i < 3; ++i)
        if (!isfinite(transform->position[i]) || !isfinite(transform->rotation[i])) return -1;
    if (!isfinite(transform->rotation[3])) return -1;
    if (platform->transform_count == platform->transform_capacity) {
        size_t capacity = platform->transform_capacity ? platform->transform_capacity * 2 : 16;
        TransformRecord *transforms = realloc(platform->transforms,
                                               capacity * sizeof(*transforms));
        if (!transforms) return -1;
        platform->transforms = transforms;
        platform->transform_capacity = capacity;
    }
    *transform_handle = (TransformHandle)platform->transform_count;
    platform->transforms[platform->transform_count++] =
        (TransformRecord){.transform = *transform, .dirty = 1};
    return 0;
}

int render_update_transform(Platform *platform, TransformHandle transform_handle,
                            const Transform *transform)
{
    if (!platform || !transform || transform_handle >= platform->transform_count) return -1;
    if (platform->transforms[transform_handle].ant_owned) return -1;
    for (uint32_t i = 0; i < 3; ++i)
        if (!isfinite(transform->position[i]) || !isfinite(transform->rotation[i])) return -1;
    if (!isfinite(transform->rotation[3])) return -1;
    platform->transforms[transform_handle].transform = *transform;
    platform->transforms[transform_handle].dirty = 1;
    return 0;
}

int render_add_drawable(Platform *platform, ModelHandle model_handle,
                        TransformHandle transform_handle)
{
    if (!platform || model_handle >= platform->model_count ||
        transform_handle >= platform->transform_count ||
        platform->drawable_count >= MAX_TRANSFORMS) return -1;
    if (platform->drawable_count == platform->drawable_capacity) {
        size_t capacity = platform->drawable_capacity ? platform->drawable_capacity * 2 : 16;
        PlatformDrawable *drawables = realloc(platform->drawables,
                                               capacity * sizeof(*drawables));
        if (!drawables) return -1;
        platform->drawables = drawables;
        platform->drawable_capacity = capacity;
    }
    platform->drawables[platform->drawable_count++] =
        (PlatformDrawable){model_handle, transform_handle};
    platform->instances_dirty = 1;
    return 0;
}

void render_step_ants(Platform *platform, float delta_seconds)
{
    if (!platform || !isfinite(delta_seconds) || delta_seconds <= 0.0f) return;
    platform->swarm_delta_seconds += delta_seconds;
    if (platform->swarm_delta_seconds > 0.1f) platform->swarm_delta_seconds = 0.1f;
}

int render_build_ant_buffers(Platform *platform, TransformHandle surface_transform,
                             const ObjectNavMesh *navmesh, const Ant *ants,
                             size_t ant_count, uint32_t triangle_count,
                             GpuTriangle **triangles_out, GpuAnt **gpu_ants_out)
{
    GpuTriangle *triangles = calloc(triangle_count, sizeof(*triangles));
    GpuAnt *gpu_ants = calloc(ant_count, sizeof(*gpu_ants));
    if (!triangles || !gpu_ants) goto error;

    for (uint32_t triangle = 0; triangle < triangle_count; ++triangle) {
        for (uint32_t vertex = 0; vertex < 3; ++vertex) {
            uint32_t index = navmesh->mesh.indices[triangle * 3 + vertex];
            uint32_t neighbor = navmesh->neighbors[triangle * 3 + vertex];
            if (index >= navmesh->mesh.vertex_count ||
                (neighbor != OBJECT_NAVMESH_NO_NEIGHBOR && neighbor >= triangle_count))
                goto error;
            memcpy(triangles[triangle].vertices[vertex],
                   navmesh->mesh.vertices[index].position, sizeof(float) * 3);
            triangles[triangle].vertices[vertex][3] = 1.0f;
            triangles[triangle].neighbors[vertex] = neighbor;
            if (neighbor != OBJECT_NAVMESH_NO_NEIGHBOR) {
                uint32_t edge_a = navmesh->mesh.indices[triangle * 3 + vertex];
                uint32_t edge_b = navmesh->mesh.indices[triangle * 3 + (vertex + 1) % 3];
                int reciprocal = 0;
                for (uint32_t neighbor_edge = 0; neighbor_edge < 3; ++neighbor_edge) {
                    uint32_t neighbor_a = navmesh->mesh.indices[neighbor * 3 + neighbor_edge];
                    uint32_t neighbor_b =
                        navmesh->mesh.indices[neighbor * 3 + (neighbor_edge + 1) % 3];
                    if (((edge_a == neighbor_a && edge_b == neighbor_b) ||
                         (edge_a == neighbor_b && edge_b == neighbor_a)) &&
                        navmesh->neighbors[neighbor * 3 + neighbor_edge] == triangle) {
                        reciprocal = 1;
                        break;
                    }
                }
                if (!reciprocal) goto error;
            }
        }
        float *a = triangles[triangle].vertices[0];
        float *b = triangles[triangle].vertices[1];
        float *c = triangles[triangle].vertices[2];
        float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        float ac[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        float normal[3] = {
            ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2],
            ab[0] * ac[1] - ab[1] * ac[0]
        };
        float area_squared = normal[0] * normal[0] + normal[1] * normal[1]
            + normal[2] * normal[2];
        if (!isfinite(area_squared) || area_squared < 1e-16f) goto error;
        triangles[triangle].neighbors[3] = OBJECT_NAVMESH_NO_NEIGHBOR;
    }

    for (size_t i = 0; i < ant_count; ++i) {
        if (ants[i].transform_handle >= platform->transform_count ||
            ants[i].transform_handle == surface_transform ||
            platform->transforms[ants[i].transform_handle].ant_owned ||
            platform->transforms[ants[i].transform_handle].ant_surface ||
            ants[i].current_triangle >= triangle_count || !isfinite(ants[i].speed) ||
            ants[i].speed < 0.0f ||
            !isfinite(ants[i].position[0]) || !isfinite(ants[i].position[1]) ||
            !isfinite(ants[i].position[2]) || !isfinite(ants[i].tangent[0]) ||
            !isfinite(ants[i].tangent[1]) || !isfinite(ants[i].tangent[2]) ||
            (ants[i].tangent[0] == 0.0f && ants[i].tangent[1] == 0.0f &&
             ants[i].tangent[2] == 0.0f)) goto error;
        for (size_t j = 0; j < i; ++j)
            if (ants[j].transform_handle == ants[i].transform_handle) goto error;

        const GpuTriangle *triangle = &triangles[ants[i].current_triangle];
        const float *a = triangle->vertices[0];
        const float *b = triangle->vertices[1];
        const float *c = triangle->vertices[2];
        float v0[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        float v1[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        float v2[3] = {ants[i].position[0] - a[0], ants[i].position[1] - a[1],
                       ants[i].position[2] - a[2]};
        float d00 = v0[0] * v0[0] + v0[1] * v0[1] + v0[2] * v0[2];
        float d01 = v0[0] * v1[0] + v0[1] * v1[1] + v0[2] * v1[2];
        float d11 = v1[0] * v1[0] + v1[1] * v1[1] + v1[2] * v1[2];
        float d20 = v2[0] * v0[0] + v2[1] * v0[1] + v2[2] * v0[2];
        float d21 = v2[0] * v1[0] + v2[1] * v1[1] + v2[2] * v1[2];
        float denominator = d00 * d11 - d01 * d01;
        float bary_y = (d11 * d20 - d01 * d21) / denominator;
        float bary_z = (d00 * d21 - d01 * d20) / denominator;
        float bary_x = 1.0f - bary_y - bary_z;
        float normal[3] = {
            v0[1] * v1[2] - v0[2] * v1[1],
            v0[2] * v1[0] - v0[0] * v1[2],
            v0[0] * v1[1] - v0[1] * v1[0]
        };
        float normal_squared = normal[0] * normal[0] + normal[1] * normal[1]
            + normal[2] * normal[2];
        float tangent_dot_normal = ants[i].tangent[0] * normal[0]
            + ants[i].tangent[1] * normal[1] + ants[i].tangent[2] * normal[2];
        float tangent_cross_squared = 0.0f;
        for (uint32_t axis = 0; axis < 3; ++axis) {
            float projected = ants[i].tangent[axis]
                - normal[axis] * tangent_dot_normal / normal_squared;
            tangent_cross_squared += projected * projected;
        }
        float plane_distance = fabsf(v2[0] * normal[0] + v2[1] * normal[1]
            + v2[2] * normal[2]) / sqrtf(normal_squared);
        float triangle_scale = sqrtf(fmaxf(d00, d11));
        if (bary_x < -1e-4f || bary_y < -1e-4f || bary_z < -1e-4f ||
            !isfinite(bary_x) || !isfinite(bary_y) || !isfinite(bary_z) ||
            !isfinite(tangent_cross_squared) || tangent_cross_squared < 1e-12f ||
            !isfinite(plane_distance) || plane_distance > triangle_scale * 1e-4f)
            goto error;
        gpu_ants[i].data[0] = ants[i].transform_handle;
        gpu_ants[i].data[1] = ants[i].current_triangle;
        memcpy(gpu_ants[i].position_speed, ants[i].position, sizeof(ants[i].position));
        gpu_ants[i].position_speed[3] = ants[i].speed;
        memcpy(gpu_ants[i].tangent, ants[i].tangent, sizeof(ants[i].tangent));
    }

    *triangles_out = triangles;
    *gpu_ants_out = gpu_ants;
    return 0;

error:
    free(triangles);
    free(gpu_ants);
    return -1;
}
