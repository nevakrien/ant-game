#include "platform_internal.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

static int near(float a, float b)
{
    return fabsf(a - b) < 1e-5f;
}

int main(void)
{
    Platform platform = {0};
    platform.transform_count = 2;
    platform.transform_capacity = 2;
    platform.transforms = calloc(platform.transform_count, sizeof(*platform.transforms));
    assert(platform.transforms);
    platform.transforms[0].ant_owned = 1;
    platform.transforms[1].transform.rotation[3] = 1.0f;

    AntPlane source = {
        .transform_handle = ANT_WORLD_PLANE_TRANSFORM,
        .normal = {0.0f, 1.0f, 0.0f},
        .forward = {0.0f, 0.0f, 1.0f}
    };
    AntPlane destination = {
        .transform_handle = 1,
        .position = {2.0f, 0.0f, 0.0f},
        .normal = {0.0f, 1.0f, 0.0f},
        .forward = {0.0f, 0.0f, 1.0f}
    };
    AntAnimationHandle animation;
    assert(render_animate_ant_between_planes(&platform, 0, &source, &destination,
                                              1.0f, 1.0f, &animation) == 0);

    AntAnimationStatus status;
    assert(render_get_ant_animation_status(&platform, animation, &status) == 0);
    assert(status == ANT_ANIMATION_PENDING);
    assert(render_update_ant_animations(&platform, 0.0f) == 0);
    assert(render_update_ant_animations(&platform, 0.5f) == 0);
    assert(near(platform.transforms[0].transform.position[0], 1.0f));
    assert(near(platform.transforms[0].transform.position[1], 1.0f));

    assert(render_update_ant_animations(&platform, 0.5f) == 0);
    assert(render_get_ant_animation_status(&platform, animation, &status) == 0);
    assert(status == ANT_ANIMATION_FINISHED);
    assert(near(platform.transforms[0].transform.position[0], 2.0f));
    assert(near(platform.transforms[0].transform.position[1], 0.0f));

    platform.transforms[1].transform.position[0] = 3.0f;
    assert(render_update_ant_animations(&platform, 0.0f) == 0);
    assert(near(platform.transforms[0].transform.position[0], 5.0f));

    free(platform.ant_animations);
    free(platform.transforms);
    return 0;
}
