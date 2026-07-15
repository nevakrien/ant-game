#include "app.h"
#include "object.h"
#include "platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef DEFAULT_OBJECT_PATH
#define DEFAULT_OBJECT_PATH "assets/teapot.obj"
#endif

typedef struct App {
    Platform *platform;
    PlatformMeshIndex teapot_mesh;
    int mesh_loaded;
    const char *asset_path;
    time_t asset_modified;
    float asset_check_time;
    float light_direction[3];
    float light_update_time;
} App;

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

static int load_mesh(App *app)
{
    ObjectMesh mesh;
    if (object_mesh_load_obj_file(app->asset_path, &mesh)) {
        fprintf(stderr, "Could not load OBJ mesh: %s\n", app->asset_path);
        return -1;
    }
    int result;
    if (app->mesh_loaded) result = platform_update_mesh(app->platform, app->teapot_mesh, &mesh);
    else {
        result = platform_add_mesh(app->platform, &mesh, &app->teapot_mesh);
        if (!result) app->mesh_loaded = 1;
    }
    object_mesh_destroy(&mesh);
    return result;
}

static void reload_mesh_if_changed(App *app, float seconds)
{
    if (seconds - app->asset_check_time < 0.5f) return;
    app->asset_check_time = seconds;
    struct stat status;
    if (stat(app->asset_path, &status) == 0 && status.st_mtime != app->asset_modified) {
        if (load_mesh(app) == 0) {
            app->asset_modified = status.st_mtime;
            fprintf(stderr, "Reloaded %s\n", app->asset_path);
        }
    }
}

static void update_scene(App *app, const PlatformInput *input, float seconds, PlatformScene *scene)
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

static void update_objects(const App *app, float seconds, PlatformObject objects[3])
{
    const float positions[3][3] = {
        {-1.6f, -0.35f, -0.5f},
        { 0.0f,  0.15f,  0.0f},
        { 1.6f, -0.35f, -0.5f}
    };
    const float angle_offsets[3] = {-0.7f, 0.0f, 0.7f};
    for (size_t i = 0; i < 3; ++i) {
        float half_angle = (seconds * 0.45f + angle_offsets[i]) * 0.5f;
        objects[i].mesh_index = app->teapot_mesh;
        objects[i].rotation[1] = sinf(half_angle);
        objects[i].rotation[3] = cosf(half_angle);
        memcpy(objects[i].position, positions[i], sizeof(objects[i].position));
    }
}

int app_run(const char *asset_path)
{
    App app = {.asset_path = asset_path ? asset_path : DEFAULT_OBJECT_PATH,
               .light_direction = {0.0f, 0.0f, 1.0f}};
    app.platform = platform_create("Vulkan Utah Teapot", 1000, 720);
    if (!app.platform) return EXIT_FAILURE;
    if (load_mesh(&app)) {
        platform_destroy(app.platform);
        return EXIT_FAILURE;
    }
    struct stat asset_status;
    if (stat(app.asset_path, &asset_status) == 0) app.asset_modified = asset_status.st_mtime;

    struct timespec start;
    timespec_get(&start, TIME_UTC);
    int failed = 0;
    for (;;) {
        PlatformInput input;
        platform_poll_input(app.platform, &input);
        if (input.quit_requested) break;
        float seconds = elapsed_seconds(&start);
        reload_mesh_if_changed(&app, seconds);
        PlatformScene scene = {0};
        update_scene(&app, &input, seconds, &scene);
        PlatformObject objects[3] = {0};
        update_objects(&app, seconds, objects);
        if (platform_draw(app.platform, &scene, objects, 3)) {
            failed = 1;
            break;
        }
    }
    platform_destroy(app.platform);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
