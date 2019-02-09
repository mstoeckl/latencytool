#include "interface.h"

#include <stdbool.h>
#include <stdlib.h>

struct state {
    bool show_dark;
};

void *setup_backend(int camera) { return calloc(1, sizeof(struct state)); }

enum WhatToDo update_backend(void *state) {
    struct state *s = (struct state *)state;
    s->show_dark = !s->show_dark;
    return s->show_dark ? DisplayDark : DisplayLight;
}

void cleanup_backend(void *state) { free(state); }
