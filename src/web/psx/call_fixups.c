/*
 * Call-signature fixups for the web build.
 *
 * WebAssembly requires every call site's pushed operand count/types to
 * match the callee's signature exactly.  The K&R-era game sources
 * declare many cross-module functions inconsistently (a TU may declare
 * a function void where the definition returns a value, or with a
 * different arity), and the game sources must not be edited.
 *
 * wasm-ld redirects any call site whose signature differs from the
 * definition to a `signature_mismatch:*` thunk that just executes
 * `unreachable` -- a runtime trap.  To eliminate the mismatches, each
 * mismatched TU is compiled with a `-D<sym>=<shim>` rename (see
 * mk/toolchain/emcc.mk) and this file supplies the shim carrying the
 * signature that TU's calls actually use.  Shims forward to the real
 * definition, padding missing args with 0, dropping extra args, and
 * discarding or synthesizing the return value as needed.
 *
 * Semantic note: where callers pass arguments the callee does not take
 * (e.g. the second argument of fadeoutCLUT1/2 callers) the extra value
 * is dropped -- on PS1 the callee simply ignored those registers, so
 * this preserves hardware behaviour.  Where a caller uses a return
 * value the callee does not produce, the shim returns 0 (PS1 callers
 * would have read garbage from $v0).
 */

#include <stdint.h>

#include <web/host/log.h>

/* ---- helpers: declarations of the real definitions ---- */

int32_t readFile(int32_t path, int32_t buffer);
int32_t createMenuBox(int32_t a, int32_t b, int32_t c, int32_t d,
                      int32_t e, int32_t f, int32_t g, int32_t h);
void fadeoutCLUT2(int32_t fade, int32_t clut);
void fadeoutCLUT1(int32_t level, int32_t clut);
int32_t waitForDeathMapLoading(int32_t flag);
int32_t loadMapSounds2(int32_t mapSoundId);
void checkFishingMap(int32_t mapId);
int32_t loadMapSounds(int32_t mapSoundId);
int32_t removeBuffModelObject(void);
int32_t BTL_addBuffDiskEffect(int32_t e);
int32_t worldPosToScreenPos(int32_t a, int32_t b);
int32_t initializeBuffModelObject(void);
int32_t DOOA_storeDigimonY(void);
void BTL_tickBattleEndText(void);
void activateMedalTexture(int32_t medalId);
int32_t BTL_addBuffRingsEffect(int32_t idx, int32_t e);
int32_t addObject(int32_t objectId, int32_t instanceId, int32_t tick,
                  int32_t render);
void triggerSeadramonCutscene(void);
void getEntityScreenPos(int32_t e, int32_t flag, int32_t out);
int32_t scriptSetDigimon(int32_t type, int32_t slot, int32_t autotalk);
int32_t showTextboxReady(int32_t boxId, int32_t speakerId);
int32_t removeObject(int32_t objectId, int32_t instanceId);
int32_t setEFEFlashOffset(int32_t id, int32_t x, int32_t y);
void setupMap(void);
void BTL_initializeFinisherChargeup(void);
int32_t BTL_addAuraProjectile(int32_t e);
int32_t handleSpecialEvolutions(int32_t mode, int32_t entity);
int32_t loadVLALL(int32_t idx, int32_t buffer);
int32_t isSoundLoaded(int32_t mode, int32_t vabId);
uint32_t playSound(int32_t vabId, int32_t val);
void STD_addWinScene(void);
int32_t initializeMusic(void);

/* ---- void shims (caller declared void, def returns a value) ---- */

void readFile_v(int32_t path, int32_t buffer)
{
	(void)readFile(path, buffer);
}

void createMenuBox_v(int32_t a, int32_t b, int32_t c, int32_t d,
                     int32_t e, int32_t f, int32_t g, int32_t h)
{
	(void)createMenuBox(a, b, c, d, e, f, g, h);
}

void waitForDeathMapLoading_v(int32_t flag)
{
	(void)waitForDeathMapLoading(flag);
}

void loadMapSounds2_v(int32_t mapSoundId)
{
	(void)loadMapSounds2(mapSoundId);
}

int32_t loadMapSounds2_r(int32_t mapSoundId)
{
	return loadMapSounds2(mapSoundId);
}

void loadMapSounds_v(int32_t mapSoundId)
{
	(void)loadMapSounds(mapSoundId);
}

void removeBuffModelObject_v(void)
{
	(void)removeBuffModelObject();
}

void BTL_addBuffDiskEffect_v(int32_t e)
{
	(void)BTL_addBuffDiskEffect(e);
}

void worldPosToScreenPos_v(int32_t a, int32_t b)
{
	(void)worldPosToScreenPos(a, b);
}

void initializeBuffModelObject_v(void)
{
	(void)initializeBuffModelObject();
}

void DOOA_storeDigimonY_v(void)
{
	(void)DOOA_storeDigimonY();
}

void BTL_addBuffRingsEffect_v(int32_t idx, int32_t e)
{
	(void)BTL_addBuffRingsEffect(idx, e);
}

void addObject_v(int32_t objectId, int32_t instanceId, int32_t tick,
                 int32_t render)
{
	(void)addObject(objectId, instanceId, tick, render);
}

void scriptSetDigimon_v(int32_t type, int32_t slot, int32_t autotalk)
{
	(void)scriptSetDigimon(type, slot, autotalk);
}

void showTextboxReady_v(int32_t boxId, int32_t speakerId)
{
	(void)showTextboxReady(boxId, speakerId);
}

void removeObject_v(int32_t objectId, int32_t instanceId)
{
	(void)removeObject(objectId, instanceId);
}

void setEFEFlashOffset_v(int32_t id, int32_t x, int32_t y)
{
	(void)setEFEFlashOffset(id, x, y);
}

void BTL_addAuraProjectile_v(int32_t e)
{
	(void)BTL_addAuraProjectile(e);
}

void handleSpecialEvolutions_v(int32_t mode, int32_t entity)
{
	(void)handleSpecialEvolutions(mode, entity);
}

void loadVLALL_v(int32_t idx, int32_t buffer)
{
	(void)loadVLALL(idx, buffer);
}

int32_t loadVLALL_r(int32_t idx, int32_t buffer)
{
	return loadVLALL(idx, buffer);
}

void isSoundLoaded_v(int32_t mode, int32_t vabId)
{
	(void)isSoundLoaded(mode, vabId);
}

int32_t isSoundLoaded_r(int32_t mode, int32_t vabId)
{
	return isSoundLoaded(mode, vabId);
}

void playSound_v(int32_t vabId, int32_t val)
{
	(void)playSound(vabId, val);
}

void initializeMusic_v(void)
{
	(void)initializeMusic();
}

void memcpy_v(int32_t dst, int32_t src, int32_t size);

/* ---- arity shims (caller passes more args than the def takes) ---- */

void fadeoutCLUT2_3(int32_t fade, int32_t clut, int32_t unused)
{
	(void)unused;
	fadeoutCLUT2(fade, clut);
}

void fadeoutCLUT1_3(int32_t level, int32_t clut, int32_t unused)
{
	(void)unused;
	fadeoutCLUT1(level, clut);
}

void checkFishingMap_2(int32_t mapId, int32_t arg1)
{
	(void)arg1;
	checkFishingMap(mapId);
}

void BTL_tickBattleEndText_1(int32_t a)
{
	(void)a;
	BTL_tickBattleEndText();
}

void activateMedalTexture_2(int32_t medalId, int32_t unused)
{
	(void)unused;
	activateMedalTexture(medalId);
}

void triggerSeadramonCutscene_1(int32_t v)
{
	(void)v;
	triggerSeadramonCutscene();
}

void setupMap_2(int32_t a, int32_t b)
{
	(void)a;
	(void)b;
	setupMap();
}

void BTL_initializeFinisherChargeup_1(int32_t tech)
{
	(void)tech;
	BTL_initializeFinisherChargeup();
}

void STD_addWinScene_1(int32_t a)
{
	(void)a;
	STD_addWinScene();
}

/* ---- return-value shims (caller expects int, def is void) ---- */

int32_t getEntityScreenPos_r(int32_t e, int32_t flag, int32_t out)
{
	getEntityScreenPos(e, flag, out);
	return 0;
}

int32_t tickCameraMoveTo(int32_t x, int32_t z, int32_t speed)
{
	extern void tickCameraMoveTo_v(int32_t x, int32_t z, int32_t speed);
	tickCameraMoveTo_v(x, z, speed);
	return 0;
}

int32_t removeObject_4(int32_t objectId, int32_t instanceId, int32_t a,
                       int32_t b)
{
	(void)a;
	(void)b;
	return removeObject(objectId, instanceId);
}

/* ---- multi-signature call site (partner.c) ---- */

void handlePraiseScold_impl(int32_t state);

void handlePraiseScold_0(void)
{
	handlePraiseScold_impl(0);
}

void handlePraiseScold_1(int32_t state)
{
	handlePraiseScold_impl(state);
}

/* ---- caller-arity shim for script_interp.c ---- */

void setActiveAnim(int32_t scriptId, int32_t animId);

void setActiveAnim_1(int32_t scriptId)
{
	setActiveAnim(scriptId, 0);
}

/* ---- libc-facing fixes ---- */

void *memcpy(int32_t dst, int32_t src, int32_t n);

void memcpy_v(int32_t dst, int32_t src, int32_t size)
{
	(void)memcpy(dst, src, size);
}

/* ---- libmcrd: main.c declares these returning long ---- */

long MemCardInit(long flg)
{
	(void)flg;
	return web_unimplemented("MemCardInit");
}

long MemCardStart(void)
{
	return web_unimplemented("MemCardStart");
}
