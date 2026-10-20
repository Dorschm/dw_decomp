# Web (Emscripten) build: all game overlays are statically linked into one
# module. The canonical source lists come from mk/version/us.mk; web
# redefines unit/overlay so the lists produce wasm objects only - no asm,
# no generated objects, and no PSX startup file.

# Point ASM_DIR at a nonexistent directory in case anything expands it.
ASM_DIR := $(BUILD_DIR)/no_such_asm_dir

# src/main/_psstart.c is psx startup asm (raw MIPS __asm__ on non-MWERKS)
# and has no place on web; the host main() in src/web/host/main.c calls
# the game's dw_main() directly.
define unit
$(1)_C_SRC := $$(filter-out src/main/_psstart.c,$$($(1)_C_SRC))
$(1)_OBJ := $$($(1)_C_SRC:%=$$(BUILD_DIR)/%.o)
$(1)_DEP := $$($(1)_OBJ:%.o=%.d)
OBJ += $$($(1)_OBJ)
DEP += $$($(1)_DEP)
endef

define overlay
$(call unit,$(1),$(2))
endef

OBJ :=
DEP :=

include mk/version/us.mk

# Web host layer + PsyQ stub surface.
WEB_C_SRC := \
	$(wildcard src/web/host/*.c) \
	$(wildcard src/web/psx/*.c)

$(eval $(call unit,WEB,web))

# Authentic libgte lookup tables (rsin_tbl, SQRT, ratan_tbl, rcossin_tbl)
# extracted from the splat data dump by tools/gen_web_data.py.
$(GEN_DIR)/libgte_tables.c: tools/gen_web_data.py \
		asm/us/main/data/psyq/libgte.data.s
	@mkdir -p $(dir $@)
	$(PYTHON) tools/gen_web_data.py $@

OBJ += $(BUILD_DIR)/$(GEN_DIR)/libgte_tables.c.o
