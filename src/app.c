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
    int mesh_loaded;
    const char *asset_path;
    const char *navmesh_path;
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

static void mat4_perspective(float m[16], float aspect)
{
    const float near_plane = 0.1f, far_plane = 20.0f;
    const float f = 1.0f / tanf(45.0f * 3.14159265f / 360.0f);
    memset(m, 0, sizeof(float) * 16);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = far_plane / (near_plane - far_plane);
    m[11] = -1.0f;
    m[14] = (far_plane * near_plane) / (near_plane - far_plane);
}

static void mat4_view(float m[16])
{
    const float eye[3] = {0.0f, 1.3f, 5.0f};
    const float target[3] = {0.0f, 0.2f, 0.0f};
    float f[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    float length = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    f[0] /= length; f[1] /= length; f[2] /= length;
    float s[3] = {-f[2], 0.0f, f[0]};
    length = sqrtf(s[0] * s[0] + s[2] * s[2]);
    s[0] /= length; s[2] /= length;
    float u[3] = {s[2] * f[1], s[0] * f[2] - s[2] * f[0], -s[0] * f[1]};

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
    for (uint32_t surface = 0; surface < 3; ++surface) {
        Ant ants[ANTS_PER_TEAPOT] = {0};
        for (uint32_t i = 0; i < ANTS_PER_TEAPOT; ++i) {
            uint32_t triangle = (i * 97u + surface * 31u) % triangle_count;
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
    mat4_view(view);
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
               .light_direction = {0.0f, 0.0f, 1.0f}};
    app.platform = platform_create("Vulkan Utah Teapot", 1000, 720);
    if (!app.platform) {
        free(custom_navmesh_path);
        return EXIT_FAILURE;
    }
    if (load_mesh(&app)) {
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
        update_scene(&app, &input, seconds, &scene);
        render_step_ants(app.platform, delta_seconds);
        if (update_transforms(&app, seconds) || render_draw(app.platform, &scene)) {
            failed = 1;
            break;
        }
    }
    platform_destroy(app.platform);
    free(custom_navmesh_path);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
