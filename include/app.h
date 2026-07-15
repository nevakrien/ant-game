#ifndef ANT_GAME_APP_H
#define ANT_GAME_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Pass NULL to use the default OBJ asset configured by the build. */
int app_run(const char *asset_path);

#ifdef __cplusplus
}
#endif

#endif
