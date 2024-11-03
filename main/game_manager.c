#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "game_manager.h"
#include "audio_manager.h"

static unsigned int questions_count = 0;
static question_header_t* start_question = 0;
static question_header_t* current_question = 0;
static int current_question_index = 0;
static int points[7];

void game_init(void *data) {
	questions_count = *((unsigned int*)data);
	start_question = (question_header_t*)(((char*)data) + sizeof(unsigned int));
	current_question = start_question;

	for (int i = 0; i < 7; i++) points[i] = 0;
	current_question_index = 0;

	vTaskDelay(1000 / portTICK_PERIOD_MS);
	play_current_question();
}

void game_next_question() {
	if (!start_question) {
		return;
	}

	current_question_index++;

	question_header_t *header_ptr = start_question;
	for (int i = 0; i < current_question_index; i++) {
		char *data_ptr = ((char*)header_ptr) + sizeof(question_header_t);
		header_ptr = (question_header_t*)(data_ptr + header_ptr->size);
	}

	current_question = header_ptr;
}

void play_current_question() {
	question_header_t *header_ptr = current_question;
	char *data_ptr = ((char*)header_ptr) + sizeof(question_header_t);
	play_audio(data_ptr, header_ptr->size, 0);
}

void play_current_question_with_callback(void (*audio_callback) ()) {
	question_header_t *header_ptr = current_question;
	char *data_ptr = ((char*)header_ptr) + sizeof(question_header_t);
	play_audio(data_ptr, header_ptr->size, audio_callback);
}

void game_process_key(char key, void (*game_end_callback) ()) {
	if (key == 1 || key == 2) {
		int padding = key == 1 ? 0 : 7;

		for (int i = 0; i < 7; i++) {
			points[i] += current_question->points[i + padding];
		}

		if ((current_question_index + 1) == questions_count) {
			int highest_points_value = points[0], highest_points_index = 0;
			for (int i = 0; i < 7; i++) {
				if (points[i] > highest_points_value) {
					highest_points_value = points[i];
					highest_points_index = i;
				}
			}

			game_next_question();
			for (int i = 0; i < highest_points_index; i++) {
				game_next_question();
			}

			play_current_question_with_callback(game_end_callback);
			return;
		}

		game_next_question();
		play_current_question();
	} else if (key == 3) {
		if ((current_question_index + 1) == questions_count) {
			return;
		}
		play_current_question();
	}
}

