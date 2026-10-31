MODULE_NAME = bayer
MODULE_OBJS = $(BUILD_DIR)/bayer.o $(BUILD_DIR)/bayer_camera.o $(BUILD_DIR)/bayer_settings.o $(BUILD_DIR)/bayer_raw_trace.o $(BUILD_DIR)/bayer_capture_gate.o $(BUILD_DIR)/bayer_capture_runtime.o $(BUILD_DIR)/fx_bayer.o $(BUILD_DIR)/gl_format.o
MODULE_CFLAGS += -Ishared -Iengine -include shared/fx_namespace.h -DFX_PREFIX=bayer -DBAYER_RELEASE
TOP_DIR = ../..
include $(TOP_DIR)/modules/Makefile
.DEFAULT_GOAL := $(BUILD_DIR)/module_complete
$(BUILD_DIR)/fx_bayer.o: fx_bayer.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -O2 $(CDEPS_FLAGS) -c -o $@ $<
$(BUILD_DIR)/bayer_preview.o: bayer_preview.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -O2 $(CDEPS_FLAGS) -c -o $@ $<
$(BUILD_DIR)/bayer_driver.o: bayer_driver.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -O2 $(CDEPS_FLAGS) -c -o $@ $<
$(BUILD_DIR)/bayer_capture_runtime.o: bayer_capture_runtime.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -O2 $(CDEPS_FLAGS) -c -o $@ $<
$(BUILD_DIR)/fx_camera.o: shared/fx_camera.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CDEPS_FLAGS) -c -o $@ $<
$(BUILD_DIR)/fx_pixels.o: shared/fx_pixels.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -O2 $(CDEPS_FLAGS) -c -o $@ $<
$(BUILD_DIR)/gl_format.o: engine/gl_format.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CDEPS_FLAGS) -c -o $@ $<
$(BUILD_DIR)/gl_review.o: engine/gl_review.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -O2 $(CDEPS_FLAGS) -c -o $@ $<
