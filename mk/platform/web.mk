PSYQ_INCLUDE := external/psyq_headers/mw_lib41/include

# -Isrc so include/emcc/inline_n.h can reach <web/psx/gte.h>
INC := -Iinclude/$(TOOLCHAIN) -I$(PSYQ_INCLUDE) -Iinclude -Isrc -I$(GEN_DIR)

CPPFLAGS := -DLANGUAGE_C -D$(VERSION_MACRO) -DPLATFORM_WEB $(INC)

EXE := $(BUILD_DIR)/dw.js
WASM := $(BUILD_DIR)/dw.wasm

LDFLAGS := -O1 -g -sUSE_SDL=2 -sASYNCIFY \
	-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB \
	-sEXPORTED_RUNTIME_METHODS=FS,ccall,cwrap \
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

generate:
