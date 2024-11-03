# Embed the server root certificate into the final binary
idf_build_get_property(project_dir PROJECT_DIR)
idf_component_register(SRCS "native_ota_example.c"
							"audio_manager.c"
							"game_manager.c"
					INCLUDE_DIRS ".")
