typedef struct {
    unsigned int size;
    signed char points[14]; // 7 yes and 7 no
} __attribute__((packed)) question_header_t;

void game_init(void *data);
void game_next_question();
void play_current_question();
void play_current_question_with_callback(void (*audio_callback) ());
void game_process_key(char key, void (*game_end_callback) ());
