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

#include <emscripten/console.h>
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

/* tamer.c declares memcpy with pointer args; give that TU its own
 * pointer-typed rename so it doesn't collide with memcpy_v. */
void *memcpy(int32_t dst, int32_t src, int32_t n);

void memcpy_p(uint8_t *dst, uint8_t *src, uint32_t size)
{
	(void)memcpy((int32_t)dst, (int32_t)src, (int32_t)size);
}

/* ---- debug state probe for the input harness ---- */

extern int32_t CURRENT_MENU, TARGET_MENU, FIRST_SCREEN_PRESSED_START;
extern int32_t POLLED_INPUT, CHANGED_INPUT;

extern long gs_drawot_calls, gs_swapbuf_calls, cd_read_calls, gsort_obj_calls;
extern long tmd_prim_counts[32];
extern unsigned short SCRIPT_STATE_2;
extern unsigned char NAMING_BOX_FLAG, NAMING_CURRENT_LETTER;
extern unsigned short NAMING_SELECTOR;
extern int8_t MAIN_STATE, CURRENT_SCREEN;

void web_debug_state(void)
{
	emscripten_console_logf(
		"state: menu cur=%ld tgt=%ld first_start=%ld pad=0x%lx chg=0x%lx "
		"drawot=%ld swap=%ld cd=%ld sortobj=%ld ss2=%x name=%x,%x,%x "
		"main=%d scr=%d",
		(long)CURRENT_MENU, (long)TARGET_MENU,
		(long)FIRST_SCREEN_PRESSED_START, (unsigned long)POLLED_INPUT,
		(unsigned long)CHANGED_INPUT,
		gs_drawot_calls, gs_swapbuf_calls, cd_read_calls, gsort_obj_calls,
		(unsigned)SCRIPT_STATE_2, (unsigned)NAMING_BOX_FLAG,
		(unsigned)NAMING_CURRENT_LETTER, (unsigned)NAMING_SELECTOR,
		(int)MAIN_STATE, (int)CURRENT_SCREEN);
	{
		extern int16_t FADE_OUT_CURRENT, FADE_IN_CURRENT;
		extern uint8_t FADE_OUT_IN_PROGRESS, FADE_PROGRESS;
		emscripten_console_logf("fade: out=%ld in=%ld prog=%d fip=%d",
			(long)FADE_OUT_CURRENT, (long)FADE_IN_CURRENT,
			(int)FADE_PROGRESS, (int)FADE_OUT_IN_PROGRESS);
	}
	{
		/* Tamer/partner world position for walkthrough probes.
		 * Entity: type(4) posData(4); PositionData: obj(16)
		 * posMatrix(80) scale(12) rotation(8) location(12). */
		extern char TAMER_ENTITY[], PARTNER_ENTITY[];
		uint8_t *tp = *(uint8_t **)(TAMER_ENTITY + 4);
		uint8_t *pp = *(uint8_t **)(PARTNER_ENTITY + 4);
		if (tp)
			emscripten_console_logf("tamer: %ld %ld %ld",
				(long)*(int32_t *)(tp + 116),
				(long)*(int32_t *)(tp + 120),
				(long)*(int32_t *)(tp + 124));
		if (pp)
			emscripten_console_logf("partner: %ld %ld %ld",
				(long)*(int32_t *)(pp + 116),
				(long)*(int32_t *)(pp + 120),
				(long)*(int32_t *)(pp + 124));
	}
	{
		/* camera + view-matrix state: GS_VIEWPOINT params and the
		 * composed GsWSMATRIX (R[0][0],R[1][1],R[2][2] + t) */
		extern int32_t GS_VIEWPOINT;
		extern int32_t GsWSMATRIX;
		int32_t *v = &GS_VIEWPOINT;
		int32_t *wm = &GsWSMATRIX;
		int16_t *mm = (int16_t *)wm;
		emscripten_console_logf(
			"view: vp=%ld,%ld,%ld vr=%ld,%ld,%ld rz=%ld ws.t=%ld,%ld,%ld "
			"ws.m=%d %d %d / %d %d %d / %d %d %d",
			(long)v[0], (long)v[1], (long)v[2],
			(long)v[3], (long)v[4], (long)v[5], (long)v[6],
			(long)wm[5], (long)wm[6], (long)wm[7],
			(int)mm[0], (int)mm[1], (int)mm[2],
			(int)mm[3], (int)mm[4], (int)mm[5],
			(int)mm[6], (int)mm[7], (int)mm[8]);
	}
	{
		static const char *nm[8] = {
			"F3", "NF3", 0, 0, "TF3", "TNF3", 0, 0 };
		static const char *nm2[8] = {
			"F4", "NF4", 0, 0, "TF4", "TNF4", 0, 0 };
		int i;
		for (i = 0; i < 32; i++) {
			const char *n = i < 8 ? nm[i] :
					(i - 8 < 8 ? nm2[i - 8] : 0);
			if (!n) {
				if (tmd_prim_counts[i])
					emscripten_console_logf(
						"tmd: code%x=%ld", i,
						tmd_prim_counts[i]);
				continue;
			}
			if (tmd_prim_counts[i])
				emscripten_console_logf("tmd: %s=%ld", n,
							tmd_prim_counts[i]);
		}
		for (i = 16; i < 32; i++)
			if (tmd_prim_counts[i])
				emscripten_console_logf("tmd: grp%x=%ld", i,
							tmd_prim_counts[i]);
	}
}

/* entity dump for the battle/walkthrough harness: index, type, world
 * position, on-map/on-screen flags for every live ENTITY_TABLE entry */
void web_entity_dump(void)
{
	extern char *ENTITY_TABLE[];
	extern int8_t GAME_STATE;
	extern int8_t CURRENT_SCREEN;
	extern int8_t TAMER_STATE;
	extern int32_t IS_SCRIPT_PAUSED;
	extern uint8_t SCRIPT_TEXTBOX_MODE, ACTIVE_INSTRUCTION;
	int i;

	emscripten_console_logf(
		"ent: state=%d scr=%d tstate=%d paused=%ld tbmode=%d "
		"instr=%d", (int)GAME_STATE, (int)CURRENT_SCREEN,
		(int)TAMER_STATE, (long)IS_SCRIPT_PAUSED,
		(int)SCRIPT_TEXTBOX_MODE, (int)ACTIVE_INSTRUCTION);
	for (i = 0; i < 10; i++) {
		char *e = ENTITY_TABLE[i];
		uint8_t *pd;
		int32_t type, x, z;
		int8_t onmap, onscr;
		if (e == NULL)
			continue;
		type = *(int32_t *)(e + 0);
		pd = *(uint8_t **)(e + 4);
		if (pd == NULL)
			continue;
		{
			extern unsigned char PSX_RAM[0x200000];
			uintptr_t pv = (uintptr_t)pd;
			uintptr_t ram = (uintptr_t)PSX_RAM;
			if (pv >= ram && pv < ram + 0x200000)
				pd = PSX_RAM + (pv & 0x1fffff);
			else if ((pv >> 28) == 0x8)
				pd = PSX_RAM + (pv & 0x1fffff);
		}
		x = *(int32_t *)(pd + 116);
		z = *(int32_t *)(pd + 124);
		onmap = *(int8_t *)(e + 52);
		onscr = *(int8_t *)(e + 53);
		/* for i>=2 ENTITY_TABLE points into NPC_ENTITIES
		 * (NPCEntity layout: scriptId @102, autotalk @103) */
		emscripten_console_logf(
			"ent[%d]: type=%lx pos=%ld,%ld map=%d scr=%d "
			"sid=%d auto=%d", i,
			(unsigned long)type, (long)x, (long)z, (int)onmap,
			(int)onscr, i >= 2 ? (int)(uint8_t)e[101] : -1,
			i >= 2 ? (int)(uint8_t)e[102] : -1);
	}
	{
		/* which scripted movement the interpreter is waiting on */
		extern uint8_t WAIT_FOR_ENTITY_ID;
		extern struct {
			uint8_t type, entityId, target, speed;
			int16_t posX, posY, targetX, targetY;
		} SCRIPT_MOVEMENT[22];
		int s;
		emscripten_console_logf("wait: id=%d", (int)WAIT_FOR_ENTITY_ID);
		{
			extern int8_t CAMERA_REACHED_TARGET, CAMERA_HAS_TARGET;
			extern int16_t CAMERA_MOVE_DIFF_X, CAMERA_MOVE_DIFF_Y;
			extern int32_t DRAWING_OFFSET_X, DRAWING_OFFSET_Y;
			emscripten_console_logf(
				"cammv: reached=%d hastgt=%d diff=%ld,%ld ofs=%ld,%ld",
				(int)CAMERA_REACHED_TARGET, (int)CAMERA_HAS_TARGET,
				(long)(int32_t)CAMERA_MOVE_DIFF_X, (long)(int32_t)CAMERA_MOVE_DIFF_Y,
				(long)DRAWING_OFFSET_X, (long)DRAWING_OFFSET_Y);
		}
		{
			/* which UI boxes are open (0=main textbox,1/2=naming).
			 * UIBoxData: 2xRECT(16) + frame/state/rowOffset/
			 * visibleRows/totalRows s16 + features/color + 2 ptrs
			 * => state (s16) @ +18, sizeof 36 */
			extern char UI_BOX_DATA[6 * 36];
			emscripten_console_logf(
				"uibox: %d %d %d %d",
				(int)*(int16_t *)(UI_BOX_DATA + 18),
				(int)*(int16_t *)(UI_BOX_DATA + 36 + 18),
				(int)*(int16_t *)(UI_BOX_DATA + 72 + 18),
				(int)*(int16_t *)(UI_BOX_DATA + 108 + 18));
		}
		for (s = 0; s < 12; s++) {
			if (SCRIPT_MOVEMENT[s].type != 0xff)
				emscripten_console_logf(
					"mv[%d]: type=%d ent=%d tgt=%d "
					"pos=%d,%d spd=%d", s,
					(int)SCRIPT_MOVEMENT[s].type,
					(int)SCRIPT_MOVEMENT[s].entityId,
					(int)SCRIPT_MOVEMENT[s].target,
					(int)SCRIPT_MOVEMENT[s].posX,
					(int)SCRIPT_MOVEMENT[s].posY,
					(int)SCRIPT_MOVEMENT[s].speed);
		}
	}
}

/* debug warp for the harness: same path as stepping on a warp tile
 * (tamer.c tickTamerTile -> state 5 -> tamerTickChangeMap). Only safe
 * while the tamer is in free-roam (TAMER_STATE 0). */
void web_debug_warp(int map, int exit, int force)
{
	extern uint8_t TARGET_MAP, CURRENT_EXIT;
	extern int8_t TAMER_STATE;
	extern void tamerSetState(int32_t state);
	extern void unsetCameraFollowPlayer(void);
	extern void stopGameTime(void);

	if (TAMER_STATE != 0 && !force) {
		emscripten_console_logf("warp: refused, tamer state %d",
					(int)TAMER_STATE);
		return;
	}
	TARGET_MAP = (uint8_t)map;
	CURRENT_EXIT = (uint8_t)exit;
	tamerSetState(5);
	unsetCameraFollowPlayer();
	stopGameTime();
	emscripten_console_logf("warp: -> map %d exit %d", map, exit);
}

/* ---- netplay hooks (web/netplay.js) ---------------------------------
 * Read-only views of the tamer for the multiplayer relay, and a
 * world->screen projection so the page can place other players' markers
 * with the game's own camera. Layout (see web_debug_state): Entity:
 * type(4) posData(4) animPtr(4) anim(...); PositionData: location at
 * +116, rotation SVECTOR at +108; EntityAnim.animId at entity+46. */
extern char TAMER_ENTITY[];
extern int8_t GAME_STATE, CURRENT_SCREEN, TAMER_STATE;

/* out[0]=in field with a live tamer, [1]=map, [2..4]=x,y,z, [5]=rot y,
 * [6]=tamer state, [7]=anim id */
int32_t *web_net_local(void)
{
	static int32_t out[12];
	uint8_t *pd = *(uint8_t **)(TAMER_ENTITY + 4);

	for (int i = 0; i < 12; i++)
		out[i] = 0;
	if (pd == NULL || GAME_STATE != 0)
		return out;
	out[0] = 1;
	out[1] = (int32_t)CURRENT_SCREEN;
	/* world-matrix translation of posData[1]: exactly what
	 * getEntityScreenPos(tamer, 1) projects, so remote positions sent in
	 * this space land in the same place on every client */
	out[2] = *(int32_t *)(pd + 136 + 72);
	out[3] = *(int32_t *)(pd + 136 + 76);
	out[4] = *(int32_t *)(pd + 136 + 80);
	out[5] = *(int16_t *)(pd + 110);
	out[6] = (int32_t)TAMER_STATE;
	out[7] = *(uint8_t *)(TAMER_ENTITY + 46);
	return out;
}

/* project a world position through the current camera; returns
 * {x, y} in the game's 320x240 screen space (valid only in the field) */
int16_t *web_net_project(int32_t x, int32_t y, int32_t z)
{
	static int16_t out[2];
	uint8_t pd[2 * 136];
	uint8_t ent[64];
	uint8_t *src = *(uint8_t **)(TAMER_ENTITY + 4);
	int k;

	out[0] = out[1] = 0;
	if (src == NULL)
		return out;
	for (k = 0; k < (int)sizeof(pd); k++)
		pd[k] = src[k];
	for (k = 0; k < 2; k++) {
		*(int32_t *)(pd + k * 136 + 72) = x;
		*(int32_t *)(pd + k * 136 + 76) = y;
		*(int32_t *)(pd + k * 136 + 80) = z;
	}
	for (k = 0; k < (int)sizeof(ent); k++)
		ent[k] = (uint8_t)TAMER_ENTITY[k];
	*(uint8_t **)(ent + 4) = pd;
	getEntityScreenPos((int32_t)(uintptr_t)ent, 1, (int32_t)(uintptr_t)out);
	return out;
}

/* ---- asset extraction hooks (tools/extract_maps.js) -----------------
 * MapTiles (include/dw/map.h): tiles[35] of 20 bytes {imagePtr(4)
 * tileId posX posY texU texV tpage clut(+16)}, objects[188] of 34 bytes,
 * then cameraX/cameraY (int16) and width/height (int8) at +7096/+7097. */
extern char MAP_TILE_DATA[];
extern uint16_t gpu_vram[][1024];

/* out[0]=width, out[1]=height (tiles of 128x128), out[2]=tile count */
int32_t *web_map_info(void)
{
	static int32_t out[3];

	out[0] = (int8_t)MAP_TILE_DATA[7096];
	out[1] = (int8_t)MAP_TILE_DATA[7097];
	out[2] = out[0] * out[1];
	return out;
}

/* 128x128 RGBA of tile idx (8bpp indices through its CLUT in VRAM);
 * NULL if the tile has no image */
uint8_t *web_map_tile_rgba(int32_t idx)
{
	static uint8_t buf[128 * 128 * 4];
	uint8_t *t = (uint8_t *)MAP_TILE_DATA + idx * 20;
	uint8_t *img = *(uint8_t **)t;
	int16_t clut = *(int16_t *)(t + 16);
	int cx = (clut & 0x3f) * 16, cy = (clut >> 6) & 0x1ff;
	int i;

	if (img == NULL || idx < 0 || idx >= 35)
		return NULL;
	for (i = 0; i < 128 * 128; i++) {
		uint16_t p = gpu_vram[cy][cx + img[i]];

		buf[i * 4 + 0] = (uint8_t)((p & 31) << 3);
		buf[i * 4 + 1] = (uint8_t)(((p >> 5) & 31) << 3);
		buf[i * 4 + 2] = (uint8_t)(((p >> 10) & 31) << 3);
		buf[i * 4 + 3] = 255;
	}
	return buf;
}

/* ---- collision / camera dump for the remake (tools/extract_maps.js) ---- */
extern uint8_t MAP_COLLISION_DATA[];
extern int32_t DRAWING_OFFSET_X, DRAWING_OFFSET_Y;

/* 100x100 bytes, index tx + tz * 100; bit 0x80 = blocked, other non-zero
 * values are tile triggers (warps etc.); world x = (tx-50)*100.., z = (50-tz)*100.. */
uint8_t *web_map_collision(void)
{
	return MAP_COLLISION_DATA;
}

/* out[0]=cameraX out[1]=cameraY out[2]=DRAWING_OFFSET_X out[3]=DRAWING_OFFSET_Y */
int32_t *web_map_camera(void)
{
	static int32_t out[4];

	out[0] = *(int16_t *)(MAP_TILE_DATA + 7092);
	out[1] = *(int16_t *)(MAP_TILE_DATA + 7094);
	out[2] = DRAWING_OFFSET_X;
	out[3] = DRAWING_OFFSET_Y;
	return out;
}

/* per-map exit table (MapWarps in battle_tick.c): int16 spawnX[10],
 * spawnY[10], spawnZ[10], rotation[10], targetMap[10], targetExit[10].
 * Trigger cells 110..119 in the collision grid warp through entry (n-110). */
extern int16_t MAP_WARPS[];

int16_t *web_map_warps(void)
{
	return MAP_WARPS;
}

/* ---- map overlay objects + depth for the remake ----------------------
 * LocalMapObject (14 bytes): texX texY someX someY someZ (int16) width
 * height clut transparency (int8). Instances live in MAP_TILE_DATA+700
 * (34 bytes each, see web_map_tile_rgba). VRAM is exposed so the extractor
 * can decode sprite pixels with the engine's own tpage/clut rules. */
extern char LOCAL_MAP_OBJECTS[];
extern int16_t MAP_OBJECT_INSTANCE_COUNT;
extern uint32_t gte_regs[];

char *web_map_obj_defs(void)
{
	return LOCAL_MAP_OBJECTS;
}

int32_t web_map_obj_count(void)
{
	return MAP_OBJECT_INSTANCE_COUNT;
}

uint8_t *web_map_obj_insts(void)
{
	return (uint8_t *)MAP_TILE_DATA + 700;
}

uint16_t *web_vram(void)
{
	return &gpu_vram[0][0];
}

/* SZ3 of the last projection (web_net_project), in the game's order units
 * (map objects use orderValue = SZ3 >> 4 clamped to 0x23..0xff6) */
int32_t web_net_depth(void)
{
	return (int32_t)(gte_regs[19] >> 4);
}
