#include <stdbool.h>

typedef struct {
    void *data;
    int size;
    bool reset_flag;
} audio_data_t;

typedef void (*audio_task_callback_t) ();

void audio_init();
void play_audio(void *mp3, int size, audio_task_callback_t cb);
