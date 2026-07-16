#include "app.h"
#include "object.h"
#include "platform.h"
#include "render.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef DEFAULT_OBJECT_PATH
#define DEFAULT_OBJECT_PATH "assets/teapot.obj"
#endif

#ifndef DEFAULT_NAVMESH_PATH
#define DEFAULT_NAVMESH_PATH "assets/teapot.nav"
#endif

typedef struct App {
    Platform *platform;
    MeshHandle teapot_mesh;
    ModelHandle teapot_model;
    ModelHandle ant_models[3];
    TransformHandle teapot_transforms[3];
    TransformHandle showcase_ants[3];
    AntPlane teapot_planes[3];
    AntPlane floor_planes[3];
    AntAnimationHandle showcase_animations[3];
    float showcase_ready_times[3];
    uint32_t showcase_surfaces[3];
    int showcase_on_floor[3];
    int showcase_waiting[3];
    int mesh_loaded;
    const char *asset_path;
    const char *navmesh_path;
    float camera_position[3];
    float camera_rotation[3];
    float light_direction[3];
    float light_update_time;
} App;

static const float TEAPOT_POSITIONS[3][3] = {
    {-1.6f, -0.35f, -0.5f},
    { 0.0f,  0.15f,  0.0f},
    { 1.6f, -0.35f, -0.5f}
};

static const float TEAPOT_ANGLE_OFFSETS[3] = {-0.7f, 0.0f, 0.7f};

enum { ANTS_PER_TEAPOT = 64 };

static const float FLOOR_HEIGHT = -1.25f;

static void mat4_identity(float m[16])
{
    memset(m, 0, sizeof(float) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_multiply(float out[16], const float a[16], const float b[16])
{
    float result[16];
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            result[column * 4 + row] =
                a[row] * b[column * 4] +
                a[4 + row] * b[column * 4 + 1] +
                a[8 + row] * b[column * 4 + 2] +
                a[12 + row] * b[column * 4 + 3];
    memcpy(out, result, sizeof(result));
}

// static void mat4_perspective(float m[16], float aspect)
// {
//     const float near_plane = 0.1f, far_plane = 200.0f;
//     const float f = 1.0f / tanf(45.0f * 3.14159265f / 360.0f);
//     memset(m, 0, sizeof(float) * 16);
//     m[0] = f / aspect;
//     m[5] = f;
//     m[10] = far_plane / (near_plane - far_plane);
//     m[11] = -1.0f;
//     m[14] = (far_plane * near_plane) / (near_plane - far_plane);
// }


static void mat4_perspective(float m[16], float aspect)
{
    const float near_plane = 1.0f;
    const float f = 1.0f / tanf(45.0f * 3.14159265f / 360.0f);
    memset(m, 0, sizeof(float) * 16);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = -1.0f;
    m[11] = -1.0f;
    m[14] = -near_plane;
}


static void mat4_view(float m[16], const float eye[3], const float rotation[3])
{
    float cos_pitch = cosf(rotation[0]);
    float sin_pitch = sinf(rotation[0]);
    float cos_yaw = cosf(rotation[1]);
    float sin_yaw = sinf(rotation[1]);
    float cos_roll = cosf(rotation[2]);
    float sin_roll = sinf(rotation[2]);
    float f[3] = {
        cos_pitch * sin_yaw,
        sin_pitch,
        -cos_pitch * cos_yaw
    };
    float level_right[3] = {cos_yaw, 0.0f, sin_yaw};
    float level_up[3] = {
        -sin_pitch * sin_yaw,
        cos_pitch,
        sin_pitch * cos_yaw
    };
    float s[3], u[3];
    for (uint32_t axis = 0; axis < 3; ++axis) {
        s[axis] = level_right[axis] * cos_roll + level_up[axis] * sin_roll;
        u[axis] = -(level_up[axis] * cos_roll - level_right[axis] * sin_roll);
    }

    mat4_identity(m);
    m[0] = s[0]; m[4] = s[1]; m[8] = s[2];
    m[1] = u[0]; m[5] = u[1]; m[9] = u[2];
    m[2] = -f[0]; m[6] = -f[1]; m[10] = -f[2];
    m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    m[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
}

static float elapsed_seconds(const struct timespec *start)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (float)(now.tv_sec - start->tv_sec) + (float)(now.tv_nsec - start->tv_nsec) / 1000000000.0f;
}

static int create_ant_mesh(ObjectMesh *mesh, uint32_t variant)
{
    static const float centers[3][3] = {
        {0.0f, 0.022f, -0.045f},
        {0.0f, 0.022f,  0.015f},
        {0.0f, 0.022f,  0.080f}
    };
    static const float radii[3][3] = {
        {0.025f, 0.018f, 0.030f},
        {0.020f, 0.014f, 0.025f},
        {0.030f, 0.022f, 0.035f}
    };
    static const uint32_t octahedron_indices[24] = {
        0, 2, 4, 2, 1, 4, 1, 3, 4, 3, 0, 4,
        2, 0, 5, 1, 2, 5, 3, 1, 5, 0, 3, 5
    };
    static const float directions[6][3] = {
        { 1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
        { 0,-1, 0}, { 0, 0, 1}, {0, 0,-1}
    };
    static const float variant_scale[3][3] = {
        {1.00f, 1.00f, 1.00f},
        {0.82f, 0.90f, 1.25f},
        {1.22f, 1.12f, 0.86f}
    };
    if (variant >= 3) return -1;
    memset(mesh, 0, sizeof(*mesh));
    mesh->vertex_count = 18;
    mesh->index_count = 72;
    mesh->vertices = calloc(mesh->vertex_count, sizeof(*mesh->vertices));
    mesh->indices = malloc((size_t)mesh->index_count * sizeof(*mesh->indices));
    if (!mesh->vertices || !mesh->indices) {
        object_mesh_destroy(mesh);
        return -1;
    }
    for (uint32_t body = 0; body < 3; ++body) {
        for (uint32_t vertex = 0; vertex < 6; ++vertex) {
            ObjectVertex *out = &mesh->vertices[body * 6 + vertex];
            for (uint32_t axis = 0; axis < 3; ++axis) {
                out->position[axis] = centers[body][axis]
                    + directions[vertex][axis] * radii[body][axis];
                out->position[axis] *= variant_scale[variant][axis];
                out->normal[axis] = directions[vertex][axis];
            }
        }
        for (uint32_t index = 0; index < 24; ++index)
            mesh->indices[body * 24 + index] = body * 6 + octahedron_indices[index];
    }
    return 0;
}

static int add_floor(App *app)
{
    ObjectVertex vertices[4] = {
        {.position = {-4.5f, FLOOR_HEIGHT, -2.5f}, .normal = {0.0f, 1.0f, 0.0f}},
        {.position = { 4.5f, FLOOR_HEIGHT, -2.5f}, .normal = {0.0f, 1.0f, 0.0f}},
        {.position = { 4.5f, FLOOR_HEIGHT,  2.0f}, .normal = {0.0f, 1.0f, 0.0f}},
        {.position = {-4.5f, FLOOR_HEIGHT,  2.0f}, .normal = {0.0f, 1.0f, 0.0f}}
    };
    uint32_t indices[6] = {0, 2, 1, 0, 3, 2};
    ObjectMesh mesh = {
        .vertices = vertices,
        .vertex_count = 4,
        .indices = indices,
        .index_count = 6
    };
    MeshHandle mesh_handle;
    ModelHandle model_handle;
    TransformHandle transform_handle;
    Model model = {
        .base_color = {0.12f, 0.17f, 0.20f},
        .rim_color = {0.28f, 0.38f, 0.42f}
    };
    Transform transform = {.rotation = {0.0f, 0.0f, 0.0f, 1.0f}};
    if (render_add_mesh(app->platform, &mesh, &mesh_handle)) return -1;
    model.mesh_handle = mesh_handle;
    if (render_add_model(app->platform, &model, &model_handle) ||
        render_add_transform(app->platform, &transform, &transform_handle) ||
        render_add_drawable(app->platform, model_handle, transform_handle)) return -1;
    return 0;
}

static uint32_t find_showcase_triangle(const ObjectNavMesh *navmesh)
{
    uint32_t triangle_count = navmesh->mesh.index_count / 3;
    uint32_t best_triangle = 0;
    float best_height = -INFINITY;
    for (uint32_t triangle = 0; triangle < triangle_count; ++triangle) {
        const float *a = navmesh->mesh.vertices[navmesh->mesh.indices[triangle * 3]].position;
        const float *b = navmesh->mesh.vertices[navmesh->mesh.indices[triangle * 3 + 1]].position;
        const float *c = navmesh->mesh.vertices[navmesh->mesh.indices[triangle * 3 + 2]].position;
        float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        float ac[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        float normal_y = ab[2] * ac[0] - ab[0] * ac[2];
        float height = (a[1] + b[1] + c[1]) / 3.0f;
        if (normal_y > 0.0f && height > best_height) {
            best_height = height;
            best_triangle = triangle;
        }
    }
    return best_triangle;
}

static int add_ant_swarms(App *app)
{
    ObjectNavMesh navmesh = {0};
    if (object_navmesh_load_file(app->navmesh_path, &navmesh)) {
        fprintf(stderr, "Could not load navmesh: %s\n", app->navmesh_path);
        return -1;
    }
    static const float base_colors[3][3] = {
        {0.95f, 0.22f, 0.08f},
        {0.98f, 0.72f, 0.12f},
        {0.18f, 0.82f, 0.72f}
    };
    static const float rim_colors[3][3] = {
        {0.90f, 0.35f, 0.12f},
        {1.00f, 0.90f, 0.35f},
        {0.35f, 1.00f, 0.90f}
    };
    for (uint32_t variant = 0; variant < 3; ++variant) {
        ObjectMesh ant_mesh = {0};
        MeshHandle mesh_handle;
        if (create_ant_mesh(&ant_mesh, variant) ||
            render_add_mesh(app->platform, &ant_mesh, &mesh_handle)) {
            object_mesh_destroy(&ant_mesh);
            object_navmesh_destroy(&navmesh);
            return -1;
        }
        object_mesh_destroy(&ant_mesh);
        Model model = {.mesh_handle = mesh_handle};
        memcpy(model.base_color, base_colors[variant], sizeof(model.base_color));
        memcpy(model.rim_color, rim_colors[variant], sizeof(model.rim_color));
        if (render_add_model(app->platform, &model, &app->ant_models[variant])) {
            object_navmesh_destroy(&navmesh);
            return -1;
        }
    }

    uint32_t triangle_count = navmesh.mesh.index_count / 3;
    uint32_t showcase_triangle = find_showcase_triangle(&navmesh);
    for (uint32_t surface = 0; surface < 3; ++surface) {
        Ant ants[ANTS_PER_TEAPOT] = {0};
        for (uint32_t i = 0; i < ANTS_PER_TEAPOT; ++i) {
            uint32_t triangle = i == 0 ? showcase_triangle
                : (i * 97u + surface * 31u) % triangle_count;
            const ObjectVertex *vertices[3];
            for (uint32_t corner = 0; corner < 3; ++corner)
                vertices[corner] = &navmesh.mesh.vertices[navmesh.mesh.indices[triangle * 3 + corner]];
            float edge_a[3], edge_b[3], normal[3];
            for (uint32_t axis = 0; axis < 3; ++axis) {
                ants[i].position[axis] = (vertices[0]->position[axis]
                    + vertices[1]->position[axis] + vertices[2]->position[axis]) / 3.0f;
                edge_a[axis] = vertices[1]->position[axis] - vertices[0]->position[axis];
                edge_b[axis] = vertices[2]->position[axis] - vertices[0]->position[axis];
            }
            normal[0] = edge_a[1] * edge_b[2] - edge_a[2] * edge_b[1];
            normal[1] = edge_a[2] * edge_b[0] - edge_a[0] * edge_b[2];
            normal[2] = edge_a[0] * edge_b[1] - edge_a[1] * edge_b[0];
            ants[i].tangent[0] = normal[1] * ants[i].position[2] - normal[2] * ants[i].position[1];
            ants[i].tangent[1] = normal[2] * ants[i].position[0] - normal[0] * ants[i].position[2];
            ants[i].tangent[2] = normal[0] * ants[i].position[1] - normal[1] * ants[i].position[0];
            float tangent_length = sqrtf(ants[i].tangent[0] * ants[i].tangent[0]
                + ants[i].tangent[1] * ants[i].tangent[1]
                + ants[i].tangent[2] * ants[i].tangent[2]);
            if (tangent_length < 1e-5f) {
                ants[i].tangent[0] = edge_a[0];
                ants[i].tangent[1] = edge_a[1];
                ants[i].tangent[2] = edge_a[2];
                tangent_length = sqrtf(edge_a[0] * edge_a[0] + edge_a[1] * edge_a[1]
                    + edge_a[2] * edge_a[2]);
            }
            for (uint32_t axis = 0; axis < 3; ++axis) ants[i].tangent[axis] /= tangent_length;
            ants[i].current_triangle = triangle;
            ants[i].speed = 0.18f + 0.06f * (float)(i % 7) / 6.0f;
            Transform transform = {.rotation = {0.0f, 0.0f, 0.0f, 1.0f}};
            if (render_add_transform(app->platform, &transform, &ants[i].transform_handle) ||
                render_add_drawable(app->platform, app->ant_models[(i + surface) % 3],
                                      ants[i].transform_handle)) {
                object_navmesh_destroy(&navmesh);
                return -1;
            }
            if (i == 0) {
                float normal_length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1]
                    + normal[2] * normal[2]);
                app->showcase_ants[surface] = ants[i].transform_handle;
                app->teapot_planes[surface].transform_handle = app->teapot_transforms[surface];
                memcpy(app->teapot_planes[surface].position, ants[i].position,
                       sizeof(ants[i].position));
                memcpy(app->teapot_planes[surface].forward, ants[i].tangent,
                       sizeof(ants[i].tangent));
                for (uint32_t axis = 0; axis < 3; ++axis)
                    app->teapot_planes[surface].normal[axis] = normal[axis] / normal_length;
            }
        }
        if (render_add_antable(app->platform, app->teapot_transforms[surface], &navmesh,
            ants, ANTS_PER_TEAPOT)) {
            object_navmesh_destroy(&navmesh);
            return -1;
        }
    }
    object_navmesh_destroy(&navmesh);
    return 0;
}

static int start_showcase(App *app)
{
    for (uint32_t i = 0; i < 3; ++i) {
        app->floor_planes[i] = (AntPlane){
            .transform_handle = ANT_WORLD_PLANE_TRANSFORM,
            .position = {TEAPOT_POSITIONS[i][0], FLOOR_HEIGHT + 0.01f, 0.55f},
            .normal = {0.0f, 1.0f, 0.0f},
            .forward = {0.0f, 0.0f, 1.0f}
        };
        if (render_animate_ant_between_planes(app->platform, app->showcase_ants[i],
            &app->teapot_planes[i], &app->floor_planes[i], 1.0f + 0.12f * i, 0.08f,
            &app->showcase_animations[i])) return -1;
        app->showcase_surfaces[i] = i;
        app->showcase_on_floor[i] = 1;
    }
    return 0;
}

static int update_showcase(App *app, float seconds)
{
    for (uint32_t i = 0; i < 3; ++i) {
        AntAnimationStatus status;
        if (render_get_ant_animation_status(app->platform, app->showcase_animations[i],
                                             &status)) return -1;
        if (status != ANT_ANIMATION_FINISHED) continue;
        if (!app->showcase_waiting[i]) {
            app->showcase_waiting[i] = 1;
            app->showcase_ready_times[i] = seconds + 0.55f + 0.18f * i;
            continue;
        }
        if (seconds < app->showcase_ready_times[i]) continue;

        uint32_t surface = app->showcase_surfaces[i];
        const AntPlane *source;
        const AntPlane *destination;
        float duration;
        float height;
        if (app->showcase_on_floor[i]) {
            surface = (surface + 1) % 3;
            source = &app->floor_planes[app->showcase_surfaces[i]];
            destination = &app->teapot_planes[surface];
            duration = 1.35f;
            height = 0.42f;
            app->showcase_on_floor[i] = 0;
            app->showcase_surfaces[i] = surface;
        } else {
            source = &app->teapot_planes[surface];
            destination = &app->floor_planes[surface];
            duration = 0.9f;
            height = 0.06f;
            app->showcase_on_floor[i] = 1;
        }
        if (render_animate_ant_between_planes(app->platform, app->showcase_ants[i],
            source, destination, duration, height, &app->showcase_animations[i])) return -1;
        app->showcase_waiting[i] = 0;
    }
    return 0;
}

static int load_mesh(App *app)
{
    ObjectMesh mesh;
    if (object_mesh_load_obj_file(app->asset_path, &mesh)) {
        fprintf(stderr, "Could not load OBJ mesh: %s\n", app->asset_path);
        return -1;
    }
    int result;
    if (app->mesh_loaded) result = render_update_mesh(app->platform, app->teapot_mesh, &mesh);
    else {
        result = render_add_mesh(app->platform, &mesh, &app->teapot_mesh);
        if (!result) {
            Model model = {
                .mesh_handle = app->teapot_mesh,
                .base_color = {0.72f, 0.25f, 0.10f},
                .rim_color = {0.35f, 0.12f, 0.05f}
            };
            result = render_add_model(app->platform, &model, &app->teapot_model);
            if (!result) app->mesh_loaded = 1;
        }
    }
    object_mesh_destroy(&mesh);
    return result;
}

static char *navmesh_path_for_object(const char *object_path)
{
    size_t length = strlen(object_path);
    size_t stem_length = length;
    const char *slash = strrchr(object_path, '/');
    const char *dot = strrchr(object_path, '.');
    if (dot && (!slash || dot > slash)) stem_length = (size_t)(dot - object_path);

    char *path = malloc(stem_length + sizeof(".nav"));
    if (!path) return NULL;
    memcpy(path, object_path, stem_length);
    memcpy(path + stem_length, ".nav", sizeof(".nav"));
    return path;
}

static void update_scene(App *app, const Input *input, float seconds, Scene *scene)
{
    float view[16], projection[16];
    mat4_view(view, app->camera_position, app->camera_rotation);
    mat4_perspective(projection, platform_aspect_ratio(app->platform));
    mat4_multiply(scene->view_projection, projection, view);

    const float azimuth = input->pointer_x * 2.35619449f;
    const float elevation = input->pointer_y * 1.25663706f;
    const float target[3] = {
        sinf(azimuth) * cosf(elevation),
        sinf(elevation),
        cosf(azimuth) * cosf(elevation)
    };
    float delta = fminf(seconds - app->light_update_time, 0.1f);
    float blend = 1.0f - expf(-8.0f * fmaxf(delta, 0.0f));
    for (int i = 0; i < 3; ++i)
        app->light_direction[i] += (target[i] - app->light_direction[i]) * blend;
    float length = sqrtf(app->light_direction[0] * app->light_direction[0] +
        app->light_direction[1] * app->light_direction[1] +
        app->light_direction[2] * app->light_direction[2]);
    for (int i = 0; i < 3; ++i) scene->light_direction[i] = app->light_direction[i] / length;
    app->light_update_time = seconds;
}

static void update_camera(App *app, const Input *input, float delta_seconds)
{
    float delta = fminf(delta_seconds, 0.1f);
    float rotation_amount = 1.5f * delta;
    app->camera_rotation[0] += (float)(input->pitch_up - input->pitch_down)
        * rotation_amount;
    app->camera_rotation[1] += (float)(input->yaw_right - input->yaw_left)
        * rotation_amount;
    app->camera_rotation[2] += (float)(input->roll_right - input->roll_left)
        * rotation_amount;

    float cos_pitch = cosf(app->camera_rotation[0]);
    float sin_pitch = sinf(app->camera_rotation[0]);
    float cos_yaw = cosf(app->camera_rotation[1]);
    float sin_yaw = sinf(app->camera_rotation[1]);
    float cos_roll = cosf(app->camera_rotation[2]);
    float sin_roll = sinf(app->camera_rotation[2]);
    float forward[3] = {
        cos_pitch * sin_yaw,
        sin_pitch,
        -cos_pitch * cos_yaw
    };
    float level_right[3] = {cos_yaw, 0.0f, sin_yaw};
    float level_up[3] = {
        -sin_pitch * sin_yaw,
        cos_pitch,
        sin_pitch * cos_yaw
    };
    float right[3];
    for (uint32_t axis = 0; axis < 3; ++axis)
        right[axis] = level_right[axis] * cos_roll + level_up[axis] * sin_roll;
    float forward_amount = (float)(input->translate_forward - input->translate_backward);
    float right_amount = (float)(input->translate_right - input->translate_left);
    float movement[3];
    float length_squared = 0.0f;
    for (uint32_t axis = 0; axis < 3; ++axis) {
        movement[axis] = forward[axis] * forward_amount + right[axis] * right_amount;
        length_squared += movement[axis] * movement[axis];
    }
    if (length_squared <= 0.0f) return;
    float distance = 4.5f * delta / sqrtf(length_squared);
    for (uint32_t axis = 0; axis < 3; ++axis)
        app->camera_position[axis] += movement[axis] * distance;
}

static int update_transforms(const App *app, float seconds)
{
    for (size_t i = 0; i < 3; ++i) {
        Transform transform = {0};
        float half_angle = (seconds * 0.45f + TEAPOT_ANGLE_OFFSETS[i]) * 0.5f;
        transform.rotation[1] = sinf(half_angle);
        transform.rotation[3] = cosf(half_angle);
        memcpy(transform.position, TEAPOT_POSITIONS[i], sizeof(transform.position));
        if (render_update_transform(app->platform, app->teapot_transforms[i], &transform)) return -1;
    }
    return 0;
}

int app_run(const char *asset_path)
{
    char *custom_navmesh_path = asset_path ? navmesh_path_for_object(asset_path) : NULL;
    if (asset_path && !custom_navmesh_path) return EXIT_FAILURE;
    App app = {.asset_path = asset_path ? asset_path : DEFAULT_OBJECT_PATH,
               .navmesh_path = custom_navmesh_path ? custom_navmesh_path : DEFAULT_NAVMESH_PATH,
               .camera_position = {0.0f, 1.3f, 5.0f},
               .camera_rotation = {-0.2165503f, 0.0f, 0.0f},
               .light_direction = {0.0f, 0.0f, 1.0f}};
    app.platform = platform_create("Vulkan Utah Teapot", 1000, 720);
    if (!app.platform) {
        free(custom_navmesh_path);
        return EXIT_FAILURE;
    }
    if (load_mesh(&app) || add_floor(&app)) {
        platform_destroy(app.platform);
        free(custom_navmesh_path);
        return EXIT_FAILURE;
    }
    for (size_t i = 0; i < 3; ++i) {
        Transform transform = {0};
        float half_angle = TEAPOT_ANGLE_OFFSETS[i] * 0.5f;
        transform.rotation[1] = sinf(half_angle);
        transform.rotation[3] = cosf(half_angle);
        memcpy(transform.position, TEAPOT_POSITIONS[i], sizeof(transform.position));
        if (render_add_transform(app.platform, &transform, &app.teapot_transforms[i]) ||
            render_add_drawable(app.platform, app.teapot_model, app.teapot_transforms[i])) {
            platform_destroy(app.platform);
            free(custom_navmesh_path);
            return EXIT_FAILURE;
        }
    }
    if (add_ant_swarms(&app)) {
        fprintf(stderr, "Could not create ant swarms\n");
        platform_destroy(app.platform);
        free(custom_navmesh_path);
        return EXIT_FAILURE;
    }
    if (start_showcase(&app)) {
        fprintf(stderr, "Could not create ant animation showcase\n");
        platform_destroy(app.platform);
        free(custom_navmesh_path);
        return EXIT_FAILURE;
    }
    struct timespec start;
    timespec_get(&start, TIME_UTC);
    int failed = 0;
    float previous_seconds = 0.0f;
    for (;;) {
        Input input;
        platform_poll_input(app.platform, &input);
        if (input.quit_requested) break;
        float seconds = elapsed_seconds(&start);
        float delta_seconds = seconds - previous_seconds;
        previous_seconds = seconds;
        Scene scene = {0};
        update_camera(&app, &input, delta_seconds);
        update_scene(&app, &input, seconds, &scene);
        render_step_ants(app.platform, delta_seconds);
        if (update_transforms(&app, seconds) || update_showcase(&app, seconds) ||
            render_draw(app.platform, &scene)) {
            failed = 1;
            break;
        }
    }
    platform_destroy(app.platform);
    free(custom_navmesh_path);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
