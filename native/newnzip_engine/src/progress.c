#include "common.h"

void progress_state_init(ProgressState *state, size_t total) {
    state->total = total;
    state->completed = 0;
    pthread_mutex_init(&state->mutex, NULL);
}

void progress_state_destroy(ProgressState *state) {
    pthread_mutex_destroy(&state->mutex);
}

void progress_step(ProgressState *state, const char *stage, const char *name) {
    pthread_mutex_lock(&state->mutex);
    state->completed += 1;
    fprintf(stderr, "NEWNZIP_PROGRESS\t%s\t%zu\t%zu\t%s\n",
            stage,
            state->completed,
            state->total,
            name ? name : "");
    fflush(stderr);
    pthread_mutex_unlock(&state->mutex);
}
