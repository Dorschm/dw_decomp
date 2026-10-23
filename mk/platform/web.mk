PSYQ_INCLUDE := external/psyq_headers/mw_lib41/include

# -Isrc so include/emcc/inline_n.h can reach <web/psx/gte.h>
INC := -Iinclude/$(TOOLCHAIN) -I$(PSYQ_INCLUDE) -Iinclude -Isrc -I$(GEN_DIR)

CPPFLAGS := -DLANGUAGE_C -D$(VERSION_MACRO) -DPLATFORM_WEB $(INC)

EXE := $(BUILD_DIR)/dw.js
WASM := $(BUILD_DIR)/dw.wasm

LDFLAGS := -O2 -sUSE_SDL=2 -sMAX_WEBGL_VERSION=2 -sASYNCIFY -sASYNCIFY_STACK_SIZE=1048576 -sSTACK_SIZE=2MB -lidbfs.js \
	-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB \
	-sEXPORTED_RUNTIME_METHODS=FS,ccall,cwrap \
	-sEXPORTED_FUNCTIONS=_main,_web_dump_display,_web_dump_vram,_web_gpu_stats,_web_pad_inject,_web_pad_set_touch,_web_debug_state,_web_audio_capture,_web_audio_capture_on_seq,_web_audio_resume,_web_mc_reload,_web_perf_report,_web_gs_trace,_web_overlay_checksum,_web_entity_dump,_web_debug_warp,_web_gl_probe,_web_set_zoom,_web_get_zoom,_web_debug_tp,_web_psx_ram,_web_debug_partner_stats,_web_debug_battle,_web_debug_battle_weaken,_web_net_local,_web_net_project,_web_get_margins,_web_map_info,_web_map_tile_rgba,_web_map_collision,_web_map_camera,_web_map_warps,_web_map_obj_defs,_web_map_obj_count,_web_map_obj_insts,_web_vram,_web_net_depth,_web_ent_info,_web_debug_tamer_pos,_web_tamer_loc,_web_map_sound_id,_web_map_npcs,_web_bake_request,_web_bake_done,_web_bake_result,_web_bake_pose,_web_npc_probe,_web_set_speed,_web_set_skip_battles,_web_battles_count,_web_text_count,_web_text_nrows,_web_text_ptr,_web_call_script,_web_script_idle \
	-sEMULATE_FUNCTION_POINTER_CASTS=1 \
	-sNO_EXIT_RUNTIME=1

# make PLATFORM=web TOOLCHAIN=emcc WEB_NAMES=1 web : keep wasm function
# names so browser stack traces are readable (bigger dw.wasm)
ifdef WEB_NAMES
LDFLAGS += --profiling-funcs
endif
ifdef WEB_UBSAN
LDFLAGS += -fsanitize=$(UBSAN_CHECKS)
endif

# The emsdk image's python3 lacks PyYAML; tools/web_python.sh installs it
# on first use (for tools/gen_text.py).
PYTHON := tools/web_python.sh

all: $(EXE)

$(EXE): $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ)
	@# MIPS divide semantics: no trap on x/0 (see tools/wasm_safe_div.py)
	python3 tools/wasm_safe_div.py $(WASM)

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
