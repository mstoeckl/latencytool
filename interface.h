#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define SMALL_WINDOW_SIZE 400

enum WhatToDo { DisplayDark, DisplayLight };
void *setup_backend(int camera);
enum WhatToDo update_backend(void *state);
void cleanup_backend(void *state);

#ifdef __cplusplus
}
#endif
