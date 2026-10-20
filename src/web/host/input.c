/*
 * SDL input for the web build. Kept in its own translation unit because
 * <SDL.h> and the PsyQ headers disagree on system typedefs.
 * Returns the pad bitmask in PsyQ libetc layout (PAD* bits, 1=pressed).
 */

#include <stdint.h>
#include <stdio.h>

#include <SDL.h>

/* PsyQ libetc bit layout (mirrored here so no psyq headers are needed) */
#define WB_PADLup	(1u << 12)
#define WB_PADLdown	(1u << 14)
#define WB_PADLleft	(1u << 15)
#define WB_PADLright	(1u << 13)
#define WB_PADRup	(1u << 4)
#define WB_PADRdown	(1u << 6)
#define WB_PADRleft	(1u << 7)
#define WB_PADRright	(1u << 5)
#define WB_PADL1	(1u << 2)
#define WB_PADL2	(1u << 0)
#define WB_PADR1	(1u << 3)
#define WB_PADR2	(1u << 1)
#define WB_PADstart	(1u << 11)
#define WB_PADselect	(1u << 8)

static SDL_GameController *controller;
static uint32_t key_bits;
static uint32_t injected_bits;
static uint32_t touch_bits;
static int inject_log;

/* Test-harness hook: ORs pad1 bits into the next web_pad_state() result.
 * Exported so the node/browser harness can drive the game. */
void web_pad_inject(uint32_t pad1bits)
{
	if (pad1bits && inject_log < 8) {
		inject_log++;
		fprintf(stderr, "pad: inject %04x\n", pad1bits);
	}
	injected_bits |= pad1bits;
}

/* Touch overlay hook: unlike web_pad_inject (one-shot), these bits are
 * held until the caller clears them - each PadRead ORs them in. */
void web_pad_set_touch(uint32_t pad1bits)
{
	touch_bits = pad1bits;
}

static uint32_t key_bit(SDL_Keycode k)
{
	switch (k) {
	case SDLK_UP:		return WB_PADLup;
	case SDLK_DOWN:		return WB_PADLdown;
	case SDLK_LEFT:		return WB_PADLleft;
	case SDLK_RIGHT:	return WB_PADLright;
	case SDLK_x:		return WB_PADRdown;  /* Cross    */
	case SDLK_z:		return WB_PADRleft;  /* Square   */
	case SDLK_s:		return WB_PADRup;    /* Triangle */
	case SDLK_a:		return WB_PADRright; /* Circle   */
	case SDLK_q:		return WB_PADL1;
	case SDLK_w:		return WB_PADR1;
	case SDLK_e:		return WB_PADL2;
	case SDLK_r:		return WB_PADR2;
	case SDLK_RETURN:	return WB_PADstart;
	case SDLK_RSHIFT:	return WB_PADselect;
	}
	return 0;
}

static void poll_events(void)
{
	SDL_Event e;

	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_QUIT:
			break;
		case SDL_KEYDOWN:
			if (!e.key.repeat)
				key_bits |= key_bit(e.key.keysym.sym);
			break;
		case SDL_KEYUP:
			key_bits &= ~key_bit(e.key.keysym.sym);
			break;
		case SDL_CONTROLLERDEVICEADDED:
			if (controller == NULL)
				controller =
					SDL_GameControllerOpen(e.cdevice.which);
			break;
		case SDL_CONTROLLERDEVICEREMOVED:
			if (controller != NULL &&
			    e.cdevice.which ==
				    SDL_JoystickInstanceID(
					    SDL_GameControllerGetJoystick(
						    controller))) {
				SDL_GameControllerClose(controller);
				controller = NULL;
			}
			break;
		}
	}
}

uint32_t web_pad_state(void)
{
	uint32_t b;

	poll_events();
	b = key_bits | touch_bits | injected_bits;
	injected_bits = 0;
	if (injected_bits && inject_log < 20) {
		inject_log++;
		fprintf(stderr, "pad: poll sees %04x\n", b);
	}

	if (controller != NULL) {
		if (SDL_GameControllerGetButton(controller,
						SDL_CONTROLLER_BUTTON_DPAD_UP))
			b |= WB_PADLup;
		if (SDL_GameControllerGetButton(controller,
						SDL_CONTROLLER_BUTTON_DPAD_DOWN))
			b |= WB_PADLdown;
		if (SDL_GameControllerGetButton(controller,
						SDL_CONTROLLER_BUTTON_DPAD_LEFT))
			b |= WB_PADLleft;
		if (SDL_GameControllerGetButton(controller,
						SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
			b |= WB_PADLright;
		if (SDL_GameControllerGetButton(controller,
						SDL_CONTROLLER_BUTTON_A))
			b |= WB_PADRdown;
		if (SDL_GameControllerGetButton(controller,
						SDL_CONTROLLER_BUTTON_X))
			b |= WB_PADRleft;
		if (SDL_GameControllerGetButton(controller,
						SDL_CONTROLLER_BUTTON_Y))
			b |= WB_PADRup;
		if (SDL_GameControllerGetButton(controller,
						SDL_CONTROLLER_BUTTON_B))
			b |= WB_PADRright;
		if (SDL_GameControllerGetButton(controller,
						SDL_CONTROLLER_BUTTON_LEFTSHOULDER))
			b |= WB_PADL1;
		if (SDL_GameControllerGetButton(controller,
						SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))
			b |= WB_PADR1;
		if (SDL_GameControllerGetAxis(controller,
					      SDL_CONTROLLER_AXIS_TRIGGERLEFT) >
		    8000)
			b |= WB_PADL2;
		if (SDL_GameControllerGetAxis(controller,
					      SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >
		    8000)
			b |= WB_PADR2;
		if (SDL_GameControllerGetButton(controller,
						SDL_CONTROLLER_BUTTON_START))
			b |= WB_PADstart;
		if (SDL_GameControllerGetButton(controller,
						SDL_CONTROLLER_BUTTON_BACK))
			b |= WB_PADselect;
	}
	return b;
}
