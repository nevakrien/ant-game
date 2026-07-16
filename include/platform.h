#ifndef ANT_GAME_PLATFORM_H
#define ANT_GAME_PLATFORM_H

typedef struct Platform Platform;

typedef struct Input {
    int quit_requested;
    float pointer_x;
    float pointer_y;
    int pitch_up;
    int pitch_down;
    int yaw_left;
    int yaw_right;
    int roll_left;
    int roll_right;
    int translate_forward;
    int translate_backward;
    int translate_left;
    int translate_right;
} Input;

Platform *platform_create(const char *title, int width, int height);
void platform_destroy(Platform *platform);
void platform_poll_input(Platform *platform, Input *input);
float platform_aspect_ratio(const Platform *platform);

#endif
