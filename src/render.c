#include "render.h"
#include "platform_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static float vector_dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void vector_cross(float out[3], const float a[3], const float b[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static int vector_normalize(float value[3])
{
    float length_squared = vector_dot(value, value);
    if (!isfinite(length_squared) || length_squared < 1e-12f) return -1;
    float inverse_length = 1.0f / sqrtf(length_squared);
    for (uint32_t axis = 0; axis < 3; ++axis) value[axis] *= inverse_length;
    return 0;
}

static void rotate_vector(float out[3], const float rotation[4], const float value[3])
{
    float q[4];
    memcpy(q, rotation, sizeof(q));
    float length_squared = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if (length_squared < 1e-12f) {
        memcpy(out, value, sizeof(float) * 3);
        return;
    }
    float inverse_length = 1.0f / sqrtf(length_squared);
    for (uint32_t i = 0; i < 4; ++i) q[i] *= inverse_length;
    float q_vector[3] = {q[0], q[1], q[2]};
    float first_cross[3], second_cross[3], intermediate[3];
    vector_cross(first_cross, q_vector, value);
    for (uint32_t axis = 0; axis < 3; ++axis)
        intermediate[axis] = first_cross[axis] + q[3] * value[axis];
    vector_cross(second_cross, q_vector, intermediate);
    for (uint32_t axis = 0; axis < 3; ++axis)
        out[axis] = value[axis] + 2.0f * second_cross[axis];
}

static void quaternion_from_basis(float out[4], const float x[3], const float y[3],
                                  const float z[3])
{
    float trace = x[0] + y[1] + z[2];
    if (trace > 0.0f) {
        float s = sqrtf(trace + 1.0f) * 2.0f;
        out[0] = (y[2] - z[1]) / s;
        out[1] = (z[0] - x[2]) / s;
        out[2] = (x[1] - y[0]) / s;
        out[3] = 0.25f * s;
    } else if (x[0] > y[1] && x[0] > z[2]) {
        float s = sqrtf(1.0f + x[0] - y[1] - z[2]) * 2.0f;
        out[0] = 0.25f * s;
        out[1] = (x[1] + y[0]) / s;
        out[2] = (x[2] + z[0]) / s;
        out[3] = (y[2] - z[1]) / s;
    } else if (y[1] > z[2]) {
        float s = sqrtf(1.0f + y[1] - x[0] - z[2]) * 2.0f;
        out[0] = (x[1] + y[0]) / s;
        out[1] = 0.25f * s;
        out[2] = (y[2] + z[1]) / s;
        out[3] = (z[0] - x[2]) / s;
    } else {
        float s = sqrtf(1.0f + z[2] - x[0] - y[1]) * 2.0f;
        out[0] = (x[2] + z[0]) / s;
        out[1] = (y[2] + z[1]) / s;
        out[2] = 0.25f * s;
        out[3] = (x[1] - y[0]) / s;
    }
    float length = sqrtf(out[0] * out[0] + out[1] * out[1]
        + out[2] * out[2] + out[3] * out[3]);
    for (uint32_t i = 0; i < 4; ++i) out[i] /= length;
}

static int plane_world_pose(const Platform *platform, const AntPlane *plane,
                            float position[3], float rotation[4], float normal[3])
{
    float forward[3];
    if (plane->transform_handle == ANT_WORLD_PLANE_TRANSFORM) {
        memcpy(position, plane->position, sizeof(plane->position));
        memcpy(normal, plane->normal, sizeof(plane->normal));
        memcpy(forward, plane->forward, sizeof(plane->forward));
    } else {
        const Transform *parent = &platform->transforms[plane->transform_handle].transform;
        rotate_vector(position, parent->rotation, plane->position);
        for (uint32_t axis = 0; axis < 3; ++axis) position[axis] += parent->position[axis];
        rotate_vector(normal, parent->rotation, plane->normal);
        rotate_vector(forward, parent->rotation, plane->forward);
    }
    if (vector_normalize(normal)) return -1;
    float along_normal = vector_dot(forward, normal);
    for (uint32_t axis = 0; axis < 3; ++axis)
        forward[axis] -= normal[axis] * along_normal;
    if (vector_normalize(forward)) return -1;
    float right[3];
    vector_cross(right, normal, forward);
    if (vector_normalize(right)) return -1;
    quaternion_from_basis(rotation, right, normal, forward);
    return 0;
}

static int valid_plane(const Platform *platform, const AntPlane *plane)
{
    if (!plane || (plane->transform_handle != ANT_WORLD_PLANE_TRANSFORM &&
        plane->transform_handle >= platform->transform_count)) return 0;
    for (uint32_t axis = 0; axis < 3; ++axis)
        if (!isfinite(plane->position[axis]) || !isfinite(plane->normal[axis]) ||
            !isfinite(plane->forward[axis])) return 0;
    float normal[3], rotation[4], position[3];
    return plane_world_pose(platform, plane, position, rotation, normal) == 0;
}

static void quaternion_slerp(float out[4], const float start[4], const float end[4], float t)
{
    float adjusted_end[4];
    memcpy(adjusted_end, end, sizeof(adjusted_end));
    float dot = start[0] * end[0] + start[1] * end[1]
        + start[2] * end[2] + start[3] * end[3];
    if (dot < 0.0f) {
        dot = -dot;
        for (uint32_t i = 0; i < 4; ++i) adjusted_end[i] = -adjusted_end[i];
    }
    if (dot > 0.9995f) {
        float length_squared = 0.0f;
        for (uint32_t i = 0; i < 4; ++i) {
            out[i] = start[i] + (adjusted_end[i] - start[i]) * t;
            length_squared += out[i] * out[i];
        }
        float inverse_length = 1.0f / sqrtf(length_squared);
        for (uint32_t i = 0; i < 4; ++i) out[i] *= inverse_length;
        return;
    }
    float angle = acosf(fminf(dot, 1.0f));
    float inverse_sine = 1.0f / sinf(angle);
    float start_weight = sinf((1.0f - t) * angle) * inverse_sine;
    float end_weight = sinf(t * angle) * inverse_sine;
    for (uint32_t i = 0; i < 4; ++i)
        out[i] = start[i] * start_weight + adjusted_end[i] * end_weight;
}

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

int render_animate_ant_between_planes(Platform *platform, TransformHandle ant_transform,
                                      const AntPlane *source, const AntPlane *destination,
                                      float duration_seconds, float jump_height,
                                      AntAnimationHandle *animation_handle)
{
    if (!platform || !animation_handle || ant_transform >= platform->transform_count ||
        !platform->transforms[ant_transform].ant_owned || !valid_plane(platform, source) ||
        !valid_plane(platform, destination) || source->transform_handle == ant_transform ||
        destination->transform_handle == ant_transform || !isfinite(duration_seconds) ||
        duration_seconds <= 0.0f || !isfinite(jump_height) || jump_height < 0.0f ||
        platform->ant_animation_count >= UINT32_MAX) return -1;

    if (platform->ant_animation_count == platform->ant_animation_capacity) {
        size_t capacity = platform->ant_animation_capacity
            ? platform->ant_animation_capacity * 2 : 8;
        AntAnimation *animations = realloc(platform->ant_animations,
                                            capacity * sizeof(*animations));
        if (!animations) return -1;
        platform->ant_animations = animations;
        platform->ant_animation_capacity = capacity;
    }
    for (size_t i = 0; i < platform->ant_animation_count; ++i) {
        AntAnimation *animation = &platform->ant_animations[i];
        if (animation->ant_transform != ant_transform) continue;
        if (animation->status != ANT_ANIMATION_FINISHED) return -1;
        animation->holds_transform = 0;
    }

    *animation_handle = (AntAnimationHandle)platform->ant_animation_count;
    platform->ant_animations[platform->ant_animation_count++] = (AntAnimation){
        .ant_transform = ant_transform,
        .source = *source,
        .destination = *destination,
        .duration_seconds = duration_seconds,
        .jump_height = jump_height,
        .status = ANT_ANIMATION_PENDING,
        .holds_transform = 1
    };
    return 0;
}

int render_get_ant_animation_status(const Platform *platform,
                                    AntAnimationHandle animation_handle,
                                    AntAnimationStatus *status)
{
    if (!platform || !status || animation_handle >= platform->ant_animation_count) return -1;
    *status = platform->ant_animations[animation_handle].status;
    return 0;
}

static int remove_swarm_ant(Platform *platform, TransformHandle ant_transform)
{
    for (size_t swarm_index = 0; swarm_index < platform->swarm_count; ++swarm_index) {
        AntSwarm *swarm = &platform->swarms[swarm_index];
        GpuAnt *ants = NULL;
        VkDeviceSize size = (VkDeviceSize)swarm->ant_count * sizeof(*ants);
        if (!size) continue;
        if (vkMapMemory(platform->device, swarm->ant_memory, 0, size, 0,
                        (void **)&ants) != VK_SUCCESS) return -1;
        for (uint32_t ant_index = 0; ant_index < swarm->ant_count; ++ant_index) {
            if (ants[ant_index].data[0] != ant_transform) continue;
            ants[ant_index] = ants[swarm->ant_count - 1];
            --swarm->ant_count;
            break;
        }
        vkUnmapMemory(platform->device, swarm->ant_memory);
    }
    return 0;
}

int render_update_ant_animations(Platform *platform, float delta_seconds)
{
    for (size_t i = 0; i < platform->ant_animation_count; ++i) {
        AntAnimation *animation = &platform->ant_animations[i];
        if (!animation->holds_transform) continue;
        if (animation->status == ANT_ANIMATION_PENDING) {
            if (remove_swarm_ant(platform, animation->ant_transform)) return -1;
            animation->status = ANT_ANIMATION_RUNNING;
        } else if (animation->status == ANT_ANIMATION_RUNNING) {
            animation->elapsed_seconds += delta_seconds;
            if (animation->elapsed_seconds >= animation->duration_seconds) {
                animation->elapsed_seconds = animation->duration_seconds;
                animation->status = ANT_ANIMATION_FINISHED;
            }
        }

        float source_position[3], source_rotation[4], source_normal[3];
        float destination_position[3], destination_rotation[4], destination_normal[3];
        if (plane_world_pose(platform, &animation->source, source_position,
                             source_rotation, source_normal) ||
            plane_world_pose(platform, &animation->destination, destination_position,
                             destination_rotation, destination_normal)) return -1;
        float t = animation->status == ANT_ANIMATION_FINISHED ? 1.0f
            : animation->elapsed_seconds / animation->duration_seconds;
        float blend = t * t * (3.0f - 2.0f * t);
        float arc_normal[3];
        for (uint32_t axis = 0; axis < 3; ++axis)
            arc_normal[axis] = source_normal[axis] * (1.0f - blend)
                + destination_normal[axis] * blend;
        if (vector_normalize(arc_normal)) memcpy(arc_normal, source_normal, sizeof(arc_normal));

        Transform transform;
        float arc = sinf(3.14159265358979323846f * t) * animation->jump_height;
        for (uint32_t axis = 0; axis < 3; ++axis)
            transform.position[axis] = source_position[axis]
                + (destination_position[axis] - source_position[axis]) * blend
                + arc_normal[axis] * arc;
        quaternion_slerp(transform.rotation, source_rotation, destination_rotation, blend);
        platform->transforms[animation->ant_transform].transform = transform;
        platform->transforms[animation->ant_transform].dirty = 1;
    }
    return 0;
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
