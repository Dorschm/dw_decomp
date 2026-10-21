PSYQ_INCLUDE := external/psyq_headers/mw_lib41/include

# -Isrc so include/emcc/inline_n.h can reach <web/psx/gte.h>
INC := -Iinclude/$(TOOLCHAIN) -I$(PSYQ_INCLUDE) -Iinclude -Isrc -I$(GEN_DIR)

CPPFLAGS := -DLANGUAGE_C -D$(VERSION_MACRO) -DPLATFORM_WEB $(INC)

EXE := $(BUILD_DIR)/dw.js
WASM := $(BUILD_DIR)/dw.wasm

LDFLAGS := -O2 -sUSE_SDL=2 -sMAX_WEBGL_VERSION=2 -sASYNCIFY -lidbfs.js \
	-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB \
	-sEXPORTED_RUNTIME_METHODS=FS,ccall,cwrap \
	-sEXPORTED_FUNCTIONS=_main,_web_dump_display,_web_dump_vram,_web_gpu_stats,_web_pad_inject,_web_pad_set_touch,_web_debug_state,_web_audio_capture,_web_audio_capture_on_seq,_web_audio_resume,_web_mc_reload,_web_perf_report,_web_gs_trace,_web_overlay_checksum,_web_entity_dump,_web_debug_warp,_web_gl_probe \
	-sEMULATE_FUNCTION_POINTER_CASTS=1 \
	-sNO_EXIT_RUNTIME=1

# The emsdk image's python3 lacks PyYAML; tools/web_python.sh installs it
# on first use (for tools/gen_text.py).
PYTHON := tools/web_python.sh

all: $(EXE)

$(EXE): $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ)

# Copy the playable artifacts next to web/index.html.
web: $(EXE)
	cp -f $(EXE) $(WASM) web/

# Standalone GTE unit test - gte.c only, no PsyQ/game code.
# Runs under node: node build/us/web/emcc/gte_test.js
GTE_TEST := $(BUILD_DIR)/gte_test.js
gte_test: $(GTE_TEST)

$(GTE_TEST): src/web/test/gte_test.c src/web/psx/gte.c src/web/psx/gte.h
	@mkdir -p $(dir $@)
	$(CC) -O1 -Isrc -o $@ src/web/test/gte_test.c src/web/psx/gte.c

# Standalone mixer decode test - mixer.c only. Needs
# .tmp_test/vag_ss3.h from tools/vag_decode.py.
MIXER_TEST := $(BUILD_DIR)/mixer_test.js
mixer_test: $(MIXER_TEST)

$(MIXER_TEST): src/web/test/mixer_test.c src/web/psx/mixer.c src/web/psx/mixer.h
	@mkdir -p $(dir $@)
	$(CC) -O1 -Isrc -o $@ src/web/test/mixer_test.c src/web/psx/mixer.c

generate:
