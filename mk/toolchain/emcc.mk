CC := emcc

# The game was written for 1990s MIPS compilers that made none of
# today's undefined-behaviour assumptions. Keep their semantics: a null
# dereference is an ordinary load of low memory (e.g. addFileReadRequest's
# `*loc` with loc == NULL, harmless on PS1 but compiled to `unreachable`
# by clang), signed overflow wraps, and objects may be type-punned.
UB_FLAGS := -fno-delete-null-pointer-checks -fwrapv -fno-strict-aliasing
CFLAGS := -O2 -Wall -std=c99 -sUSE_SDL=2 $(UB_FLAGS)

# No -Werror for game code (it must not be edited); our own code under
# src/web/ gets it via a target-specific flag below.
# src/web/ files must NOT see the PsyQ headers (they clash with the
# emscripten system headers, e.g. wchar_t in SDL.h), so they get their
# own CPPFLAGS without $(INC)/$(PSYQ_INCLUDE).
$(BUILD_DIR)/src/web/%.c.o: CFLAGS += -Werror -Wextra
# PsyQ headers redeclare libc builtins; don't let that fail -Werror.
$(BUILD_DIR)/src/web/psx/%.c.o: CFLAGS += \
	-Wno-incompatible-library-redeclaration \
	-Wno-deprecated-non-prototype -Wno-visibility
# Only the SDL host layer must not see the PsyQ headers (they clash with
# the emscripten system headers, e.g. wchar_t in SDL.h). The psx stubs DO
# include the real PsyQ headers for correct prototypes.
$(BUILD_DIR)/src/web/host/%.c.o: CPPFLAGS = -DLANGUAGE_C -D$(VERSION_MACRO) \
	-DPLATFORM_WEB -Iinclude -Isrc -I$(GEN_DIR)

# The game's entry point is literally named main() in src/main/main.c.
# Rename it to dw_main in that TU only; the web host main() calls it.
$(BUILD_DIR)/src/main/main.c.o: CPPFLAGS += -Dmain=dw_main

# Call-signature fixups: many K&R-era declarations disagree across TUs.
# wasm-ld redirects any call site whose signature differs from the
# definition to an `unreachable` thunk (and some mismatches produce
# invalid wasm outright).  The game sources can't be edited, so each
# mismatched TU gets a -D rename of the symbol and src/web/psx/
# call_fixups.c supplies a shim with the signature that TU uses.
$(BUILD_DIR)/src/std/std_setup.c.o: CPPFLAGS += -DreadFile=readFile_v
$(BUILD_DIR)/src/vs/vs_scene.c.o: CPPFLAGS += -DreadFile=readFile_v
$(BUILD_DIR)/src/trn/trn_reward.c.o: CPPFLAGS += -DcreateMenuBox=createMenuBox_v
$(BUILD_DIR)/src/evl/evl.c.o: CPPFLAGS += -DfadeoutCLUT2=fadeoutCLUT2_3 \
	-DfadeoutCLUT1=fadeoutCLUT1_3 -DloadMapSounds2=loadMapSounds2_r \
	-DloadVLALL=loadVLALL_r -DisSoundLoaded=isSoundLoaded_r
$(BUILD_DIR)/src/main/evl.c.o: CPPFLAGS += -DloadMapSounds2=loadMapSounds2_v \
	-DloadVLALL=loadVLALL_v -DisSoundLoaded=isSoundLoaded_v
$(BUILD_DIR)/src/trn/trn_hud.c.o: CPPFLAGS += \
	-DworldPosToScreenPos=worldPosToScreenPos_v
$(BUILD_DIR)/src/dooa/dooa.c.o: CPPFLAGS += \
	-DwaitForDeathMapLoading=waitForDeathMapLoading_v \
	-DDOOA_storeDigimonY=DOOA_storeDigimonY_v \
	-DsetEFEFlashOffset=setEFEFlashOffset_v -DfadeoutCLUT1=fadeoutCLUT1_3
$(BUILD_DIR)/src/murd/murd.c.o: CPPFLAGS += \
	-DwaitForDeathMapLoading=waitForDeathMapLoading_v \
	-DfadeoutCLUT1=fadeoutCLUT1_3
$(BUILD_DIR)/src/main/map.c.o: CPPFLAGS += -DtickCameraMoveTo=tickCameraMoveTo_v \
	-DcheckFishingMap=checkFishingMap_2
$(BUILD_DIR)/src/main/main.c.o: CPPFLAGS += -DcheckFishingMap=checkFishingMap_2 \
	-DBTL_addBuffDiskEffect=BTL_addBuffDiskEffect_v -Dmemcpy=memcpy_v \
	-DinitializeMusic=initializeMusic_v
$(BUILD_DIR)/src/main/partner.c.o: CPPFLAGS += -DloadMapSounds=loadMapSounds_v \
	-DhandleSpecialEvolutions=handleSpecialEvolutions_v \
	-DplaySound=playSound_v -include emcc/hps_dispatch.h
# -std=gnu99 required for the ,##__VA_ARGS__ comma-deletion in
# hps_dispatch.h's arity dispatch (it is a GNU extension).
$(BUILD_DIR)/src/main/partner.c.o: CFLAGS += -std=gnu99
$(BUILD_DIR)/src/main/partner_impl.c.o: CPPFLAGS += \
	-DhandlePraiseScold=handlePraiseScold_impl
$(BUILD_DIR)/src/btl/battle_main.c.o: CPPFLAGS += \
	-DremoveBuffModelObject=removeBuffModelObject_v \
	-DBTL_addBuffRingsEffect=BTL_addBuffRingsEffect_v -DaddObject=addObject_v \
	-DremoveObject=removeObject_v \
	-DBTL_addAuraProjectile=BTL_addAuraProjectile_v \
	-DBTL_initializeFinisherChargeup=BTL_initializeFinisherChargeup_1
$(BUILD_DIR)/src/vs/vs_hud.c.o: CPPFLAGS += -DaddObject=addObject_v \
	-DremoveObject=removeObject_v
$(BUILD_DIR)/src/trn2/trn2_hud.c.o: CPPFLAGS += \
	-DworldPosToScreenPos=worldPosToScreenPos_v
$(BUILD_DIR)/src/main/btl.c.o: CPPFLAGS += \
	-DinitializeBuffModelObject=initializeBuffModelObject_v
$(BUILD_DIR)/src/main/battle_ui.c.o: CPPFLAGS += \
	-DBTL_tickBattleEndText=BTL_tickBattleEndText_1
$(BUILD_DIR)/src/main/game_menu.c.o: CPPFLAGS += \
	-DactivateMedalTexture=activateMedalTexture_2
$(BUILD_DIR)/src/main/fish.c.o: CPPFLAGS += \
	-DtriggerSeadramonCutscene=triggerSeadramonCutscene_1
$(BUILD_DIR)/src/main/tamer.c.o: CPPFLAGS += \
	-DgetEntityScreenPos=getEntityScreenPos_r -Dmemcpy=memcpy_p \
	-DplaySound=playSound_v
$(BUILD_DIR)/src/main/script_interp.c.o: CPPFLAGS += \
	-DsetActiveAnim=setActiveAnim_1 -DscriptSetDigimon=scriptSetDigimon_v \
	-DsetupMap=setupMap_2
$(BUILD_DIR)/src/dget/dget.c.o: CPPFLAGS += -DshowTextboxReady=showTextboxReady_v \
	-DplaySound=playSound_v
$(BUILD_DIR)/src/btl/battle_setup.c.o: CPPFLAGS += -DplaySound=playSound_v
$(BUILD_DIR)/src/main/inventory.c.o: CPPFLAGS += -DplaySound=playSound_v
$(BUILD_DIR)/src/main/ui.c.o: CPPFLAGS += -DplaySound=playSound_v
$(BUILD_DIR)/src/std/std_main.c.o: CPPFLAGS += -DSTD_addWinScene=STD_addWinScene_1
# trn/trn2 files call removeObject() with 4 args (def takes 2).
$(BUILD_DIR)/src/trn2/trn2_mp.c.o: CPPFLAGS += -DremoveObject=removeObject_4
$(BUILD_DIR)/src/trn/trn_brain.c.o: CPPFLAGS += -DremoveObject=removeObject_4
$(BUILD_DIR)/src/trn/trn_def.c.o: CPPFLAGS += -DremoveObject=removeObject_4
$(BUILD_DIR)/src/trn/trn_hp.c.o: CPPFLAGS += -DremoveObject=removeObject_4
$(BUILD_DIR)/src/trn/trn_mp.c.o: CPPFLAGS += -DremoveObject=removeObject_4
$(BUILD_DIR)/src/trn/trn_off.c.o: CPPFLAGS += -DremoveObject=removeObject_4
$(BUILD_DIR)/src/trn/trn_speed.c.o: CPPFLAGS += -DremoveObject=removeObject_4

DEPFLAGS = -MMD -MP -MF $(@:.o=.d) -MT $@

$(BUILD_DIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $(DEPFLAGS) -o $@ $<
