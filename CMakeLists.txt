if(CONFIG_ZMK_INPUT_PROCESSOR_MOTION_THRESHOLD)
  target_sources(app PRIVATE src/input_processor_motion_threshold.c)
endif()
