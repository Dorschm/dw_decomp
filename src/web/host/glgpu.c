/*
 * Native WebGL2 renderer — see glgpu.h.
 *
 * Framebuffer: one RGBA8 texture of (1024*S) x (512*S), addressed in PS1
 * VRAM coordinates, so double-buffered draw areas and the display rect
 * work unchanged. Alpha carries the PS1 mask/STP bit.
 *
 * Semi-transparency: a PS1 semi-transparent primitive only blends the
 * texels whose STP bit is set (all pixels when untextured). Each semi
 * batch is drawn in two passes — pass 1 writes the non-STP texels with
 * blending off, pass 2 blends the STP texels with the E1 mode:
 *   0: B/2+F/2   1: B+F   2: B-F   3: B+F/4
 * Batches break whenever semi/blend/draw-area state changes, which keeps
 * the game's ordering-table draw order.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

#include "glgpu.h"

int glgpu_on;

static const uint16_t *cpu_vram;	/* gpu_vram[512][1024] */
static int S = 4;			/* internal resolution scale */
static GLuint vram_tex, fb_tex, fbo;
static GLuint prog_draw, prog_copy, prog_present;
static GLuint vbo, vao, quad_vao, quad_vbo;
static GLint u_pass, u_scale_present, u_rect, u_rgb24;
static GLint u_copy_rect;
static int canvas_w, canvas_h;
static int dirty_y0 = 512, dirty_y1 = -1;
static unsigned n_changed, n_sync, n_present;
static int last_present[5];

typedef struct Vert {
	float x, y, r, g, b, u, v;
	uint32_t clut, info, tw;
} Vert;

#define MAX_VERTS 65536
static Vert *verts;
static int nverts;

/* batch state */
static int b_semi = -1, b_blend = -1;
static int b_cx0, b_cy0, b_cx1, b_cy1;

static const char *vs_draw =
	"#version 300 es\n"
	"layout(location=0) in vec2 a_pos;\n"
	"layout(location=1) in vec3 a_col;\n"
	"layout(location=2) in vec2 a_uv;\n"
	"layout(location=3) in uvec3 a_info;\n"
	"out vec3 v_col; out vec2 v_uv; flat out uvec3 v_info;\n"
	"void main(){\n"
	"  v_col=a_col; v_uv=a_uv; v_info=a_info;\n"
	"  gl_Position=vec4(a_pos.x/512.0-1.0, a_pos.y/256.0-1.0, 0.0, 1.0);\n"
	"}\n";

static const char *fs_draw =
	"#version 300 es\n"
	"precision highp float; precision highp int;\n"
	"precision highp usampler2D;\n"
	"uniform usampler2D u_vram; uniform int u_pass;\n"
	"in vec3 v_col; in vec2 v_uv; flat in uvec3 v_info;\n"
	"out vec4 o;\n"
	"uint at(int x,int y){return texelFetch(u_vram,ivec2(x&1023,y&511),0).r;}\n"
	"vec3 dec(uint c){return vec3(float(c&31u),float((c>>5)&31u),"
	"float((c>>10)&31u))/31.0;}\n"
	"void main(){\n"
	"  uint info=v_info.y;\n"
	"  bool tex=((info>>22)&1u)==1u, raw=((info>>23)&1u)==1u,\n"
	"       semi=((info>>24)&1u)==1u;\n"
	"  vec3 c=v_col/255.0; bool stp=true; float m=0.0;\n"
	"  if(tex){\n"
	"    int tbx=int(info&1023u), tby=int((info>>10)&511u);\n"
	"    int dep=int((info>>19)&3u);\n"
	"    int u=int(floor(v_uv.x))&255, v=int(floor(v_uv.y))&255;\n"
	"    uint tw=v_info.z;\n"
	"    int mx=int(tw&31u)*8, my=int((tw>>5)&31u)*8;\n"
	"    int ox=int((tw>>10)&31u)*8, oy=int((tw>>15)&31u)*8;\n"
	"    u=(u&~mx)|(ox&mx); v=(v&~my)|(oy&my);\n"
	"    uint clut=v_info.x; int cx=int(clut&63u)*16, cy=int(clut>>6);\n"
	"    uint t;\n"
	"    if(dep==0){uint w=at(tbx+u/4,tby+v);\n"
	"      t=at(cx+int((w>>uint((u&3)*4))&15u),cy);}\n"
	"    else if(dep==1){uint w=at(tbx+u/2,tby+v);\n"
	"      t=at(cx+int((w>>uint((u&1)*8))&255u),cy);}\n"
	"    else t=at(tbx+u,tby+v);\n"
	"    if(t==0u) discard;\n"
	"    stp=(t>>15)==1u; m=stp?1.0:0.0;\n"
	"    vec3 tc=dec(t);\n"
	"    c=raw?tc:min(tc*v_col/128.0,vec3(1.0));\n"
	"  }\n"
	"  bool blended=semi&&stp;\n"
	"  if(u_pass==1&&blended) discard;\n"
	"  if(u_pass==2&&!blended) discard;\n"
	"  o=vec4(c,m);\n"
	"}\n";

/* copy CPU VRAM (texture) rect into the framebuffer */
static const char *vs_quad =
	"#version 300 es\n"
	"layout(location=0) in vec2 a_pos;\n"
	"uniform vec4 u_rect;\n"
	"out vec2 v_p;\n"
	"void main(){\n"
	"  v_p=u_rect.xy+a_pos*u_rect.zw;\n"
	"  gl_Position=vec4(v_p.x/512.0-1.0, v_p.y/256.0-1.0, 0.0, 1.0);\n"
	"}\n";

static const char *fs_copy =
	"#version 300 es\n"
	"precision highp float; precision highp int;\n"
	"precision highp usampler2D;\n"
	"uniform usampler2D u_vram;\n"
	"in vec2 v_p; out vec4 o;\n"
	"void main(){\n"
	"  uint t=texelFetch(u_vram,ivec2(v_p)&ivec2(1023,511),0).r;\n"
	"  o=vec4(float(t&31u)/31.0,float((t>>5)&31u)/31.0,"
	"float((t>>10)&31u)/31.0,float(t>>15));\n"
	"}\n";

static const char *vs_present =
	"#version 300 es\n"
	"layout(location=0) in vec2 a_pos;\n"
	"out vec2 v_t;\n"
	"void main(){ v_t=a_pos; gl_Position=vec4(a_pos.x*2.0-1.0,"
	"1.0-a_pos.y*2.0,0.0,1.0); }\n";

static const char *fs_present =
	"#version 300 es\n"
	"precision highp float; precision highp int;\n"
	"precision highp usampler2D;\n"
	"uniform sampler2D u_fb; uniform usampler2D u_vram;\n"
	"uniform vec4 u_rect; uniform int u_rgb24;\n"
	"in vec2 v_t; out vec4 o;\n"
	"uint at(int x,int y){return texelFetch(u_vram,ivec2(x&1023,y&511),0).r;}\n"
	"uint byteat(int off,int y){uint w=at(off>>1,y);"
	"return (off&1)==1?(w>>8):(w&255u);}\n"
	"void main(){\n"
	"  vec2 p=u_rect.xy+v_t*u_rect.zw;\n"
	"  if(u_rgb24==1){\n"
	"    int x=int(p.x-u_rect.x), y=int(p.y);\n"
	"    int off=int(u_rect.x)*2+x*3;\n"
	"    o=vec4(float(byteat(off,y)),float(byteat(off+1,y)),"
	"float(byteat(off+2,y)),255.0)/255.0;\n"
	"  } else o=vec4(texture(u_fb,p/vec2(1024.0,512.0)).rgb,1.0);\n"
	"}\n";

static GLuint compile(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		printf("glgpu: shader compile failed: %s\n", log);
		return 0;
	}
	return s;
}

static GLuint link(const char *vs, const char *fs)
{
	GLuint p = glCreateProgram(), a = compile(GL_VERTEX_SHADER, vs),
	       b = compile(GL_FRAGMENT_SHADER, fs);
	GLint ok = 0;
	if (!a || !b)
		return 0;
	glAttachShader(p, a);
	glAttachShader(p, b);
	glLinkProgram(p);
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetProgramInfoLog(p, sizeof(log), NULL, log);
		printf("glgpu: link failed: %s\n", log);
		return 0;
	}
	return p;
}

static int query_scale(void)
{
	/* ?scale=N (1..8) or ?renderer=soft (returns 0) */
	return emscripten_run_script_int(
		"(function(){if(typeof location==='undefined')return 4;"
		"var q=new URLSearchParams(location.search);"
		"if(q.get('renderer')==='soft')return 0;"
		"var s=parseInt(q.get('scale')||'4',10);"
		"return(s>=1&&s<=8)?s:4;})()");
}

int glgpu_init(const uint16_t *vram)
{
	EmscriptenWebGLContextAttributes at;
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx;
	GLint maxtex = 0;
	static const float quad[] = {0, 0, 1, 0, 0, 1, 1, 1};

	cpu_vram = vram;
	S = query_scale();
	if (S == 0) {
		printf("glgpu: software renderer requested\n");
		return 0;
	}
	emscripten_webgl_init_context_attributes(&at);
	at.majorVersion = 2;
	at.minorVersion = 0;
	at.alpha = 0;
	at.depth = 0;
	at.stencil = 0;
	at.antialias = 0;
	at.premultipliedAlpha = 0;
	at.preserveDrawingBuffer = 0;
	ctx = emscripten_webgl_create_context("#canvas", &at);
	if (ctx <= 0) {
		printf("glgpu: no WebGL2 (%d), using software GPU\n", (int)ctx);
		return 0;
	}
	emscripten_webgl_make_context_current(ctx);
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxtex);
	while (S > 1 && 1024 * S > maxtex)
		S--;

	prog_draw = link(vs_draw, fs_draw);
	prog_copy = link(vs_quad, fs_copy);
	prog_present = link(vs_present, fs_present);
	if (!prog_draw || !prog_copy || !prog_present)
		return 0;

	glGenTextures(1, &vram_tex);
	glBindTexture(GL_TEXTURE_2D, vram_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, 1024, 512, 0,
		     GL_RED_INTEGER, GL_UNSIGNED_SHORT, cpu_vram);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &fb_tex);
	glBindTexture(GL_TEXTURE_2D, fb_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1024 * S, 512 * S, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, fb_tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
	    GL_FRAMEBUFFER_COMPLETE) {
		printf("glgpu: framebuffer incomplete\n");
		return 0;
	}
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	verts = malloc(sizeof(Vert) * MAX_VERTS);
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Vert) * MAX_VERTS, NULL,
		     GL_STREAM_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vert),
			      (void *)offsetof(Vert, x));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vert),
			      (void *)offsetof(Vert, r));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vert),
			      (void *)offsetof(Vert, u));
	glEnableVertexAttribArray(3);
	glVertexAttribIPointer(3, 3, GL_UNSIGNED_INT, sizeof(Vert),
			       (void *)offsetof(Vert, clut));

	glGenVertexArrays(1, &quad_vao);
	glBindVertexArray(quad_vao);
	glGenBuffers(1, &quad_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

	u_pass = glGetUniformLocation(prog_draw, "u_pass");
	u_copy_rect = glGetUniformLocation(prog_copy, "u_rect");
	u_rect = glGetUniformLocation(prog_present, "u_rect");
	u_rgb24 = glGetUniformLocation(prog_present, "u_rgb24");
	(void)u_scale_present;
	glUseProgram(prog_present);
	glUniform1i(glGetUniformLocation(prog_present, "u_fb"), 1);
	glUniform1i(glGetUniformLocation(prog_present, "u_vram"), 0);
	glUseProgram(prog_draw);
	glUniform1i(glGetUniformLocation(prog_draw, "u_vram"), 0);
	glUseProgram(prog_copy);
	glUniform1i(glGetUniformLocation(prog_copy, "u_vram"), 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 2);

	glgpu_on = 1;
	printf("glgpu: WebGL2 renderer on, %dx internal (%dx%d)\n", S,
	       1024 * S, 512 * S);
	return 1;
}

static void sync_vram_tex(void)
{
	if (dirty_y1 < dirty_y0)
		return;
	n_sync++;
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, vram_tex);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, dirty_y0, 1024,
			dirty_y1 - dirty_y0 + 1, GL_RED_INTEGER,
			GL_UNSIGNED_SHORT, cpu_vram + dirty_y0 * 1024);
	dirty_y0 = 512;
	dirty_y1 = -1;
}

static void bind_fb(void)
{
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, 1024 * S, 512 * S);
}

static void set_blend(int mode)
{
	glEnable(GL_BLEND);
	switch (mode) {
	case 0:
		glBlendColor(0.5f, 0.5f, 0.5f, 0.5f);
		glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		glBlendFuncSeparate(GL_CONSTANT_COLOR, GL_CONSTANT_COLOR,
				    GL_ONE, GL_ZERO);
		break;
	case 1:
		glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
		break;
	case 2:
		glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
		glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
		break;
	default:
		glBlendColor(0.25f, 0.25f, 0.25f, 0.25f);
		glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		glBlendFuncSeparate(GL_CONSTANT_COLOR, GL_ONE, GL_ONE, GL_ZERO);
		break;
	}
}

static void flush(void)
{
	if (nverts == 0)
		return;
	sync_vram_tex();
	bind_fb();
	glEnable(GL_SCISSOR_TEST);
	glScissor(b_cx0 * S, b_cy0 * S, (b_cx1 - b_cx0 + 1) * S,
		  (b_cy1 - b_cy0 + 1) * S);
	glUseProgram(prog_draw);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, vram_tex);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(Vert) * nverts, verts);
	if (!b_semi) {
		glDisable(GL_BLEND);
		glUniform1i(u_pass, 0);
		glDrawArrays(GL_TRIANGLES, 0, nverts);
	} else {
		glDisable(GL_BLEND);
		glUniform1i(u_pass, 1);
		glDrawArrays(GL_TRIANGLES, 0, nverts);
		set_blend(b_blend);
		glUniform1i(u_pass, 2);
		glDrawArrays(GL_TRIANGLES, 0, nverts);
		glDisable(GL_BLEND);
	}
	glDisable(GL_SCISSOR_TEST);
	nverts = 0;
}

void glgpu_tri(const GlVtx v[3], const GlPrim *p)
{
	int semi = p->semi ? 1 : 0, blend = semi ? p->blend : 0, k;
	uint32_t info;

	if (p->cx1 < p->cx0 || p->cy1 < p->cy0)
		return;
	if (nverts > 0 &&
	    (semi != b_semi || blend != b_blend || p->cx0 != b_cx0 ||
	     p->cy0 != b_cy0 || p->cx1 != b_cx1 || p->cy1 != b_cy1 ||
	     nverts + 3 > MAX_VERTS))
		flush();
	b_semi = semi;
	b_blend = blend;
	b_cx0 = p->cx0; b_cy0 = p->cy0; b_cx1 = p->cx1; b_cy1 = p->cy1;

	info = (uint32_t)(p->tbx & 1023) | ((uint32_t)(p->tby & 511) << 10) |
	       ((uint32_t)(p->depth & 3) << 19) |
	       ((uint32_t)(p->textured ? 1 : 0) << 22) |
	       ((uint32_t)(p->raw ? 1 : 0) << 23) |
	       ((uint32_t)semi << 24);
	for (k = 0; k < 3; k++) {
		Vert *o = &verts[nverts++];
		o->x = (float)v[k].x;
		o->y = (float)v[k].y;
		o->r = (float)v[k].r;
		o->g = (float)v[k].g;
		o->b = (float)v[k].b;
		o->u = (float)v[k].u;
		o->v = (float)v[k].v;
		o->clut = p->clut;
		o->info = info;
		o->tw = p->tw;
	}
}

void glgpu_fill(int x, int y, int w, int h, int r, int g, int b)
{
	if (w <= 0 || h <= 0)
		return;
	flush();
	/* the CPU copy was filled too: keep the texture mirror in sync */
	if ((y & 511) < dirty_y0) dirty_y0 = y & 511;
	if ((y & 511) + h - 1 > dirty_y1)
		dirty_y1 = (y & 511) + h - 1 > 511 ? 511 : (y & 511) + h - 1;
	bind_fb();
	glEnable(GL_SCISSOR_TEST);
	glScissor(x * S, y * S, w * S, h * S);
	glClearColor(r / 255.0f, g / 255.0f, b / 255.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_SCISSOR_TEST);
}

void glgpu_vram_changed(int x, int y, int w, int h)
{
	int y0, y1;

	n_changed++;
	if (w <= 0 || h <= 0)
		return;
	flush();
	y0 = y & 511;
	y1 = y0 + h - 1;
	if (y1 > 511) { /* wraps: dirty everything */
		y0 = 0;
		y1 = 511;
	}
	if (y0 < dirty_y0) dirty_y0 = y0;
	if (y1 > dirty_y1) dirty_y1 = y1;
	sync_vram_tex();
	bind_fb();
	glDisable(GL_BLEND);
	glUseProgram(prog_copy);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, vram_tex);
	glUniform4f(u_copy_rect, (float)(x & 1023), (float)y0, (float)w,
		    (float)h);
	glBindVertexArray(quad_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void glgpu_present(int dx, int dy, int dw, int dh, int rgb24)
{
	int want_w, want_h;

	n_present++;
	last_present[0] = dx; last_present[1] = dy; last_present[2] = dw;
	last_present[3] = dh; last_present[4] = rgb24;
	flush();
	sync_vram_tex();
	if (dw <= 0) dw = 320;
	if (dh <= 0) dh = 240;
	/* fixed 4:3 output at the internal scale */
	want_w = 320 * S;
	want_h = 240 * S;
	if (want_w != canvas_w || want_h != canvas_h) {
		emscripten_set_canvas_element_size("#canvas", want_w, want_h);
		canvas_w = want_w;
		canvas_h = want_h;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, canvas_w, canvas_h);
	glDisable(GL_BLEND);
	glUseProgram(prog_present);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, vram_tex);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, fb_tex);
	glUniform4f(u_rect, (float)dx, (float)dy, (float)dw, (float)dh);
	glUniform1i(u_rgb24, rgb24 ? 1 : 0);
	glBindVertexArray(quad_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glActiveTexture(GL_TEXTURE0);
}

/* Debug: framebuffer pixel at VRAM (x,y) vs the CPU VRAM word. */
EMSCRIPTEN_KEEPALIVE
void web_gl_probe(int x, int y)
{
	uint8_t px[4] = {0, 0, 0, 0};

	if (!glgpu_on) {
		printf("glprobe: off\n");
		return;
	}
	flush();
	bind_fb();
	glReadPixels(x * S + S / 2, y * S + S / 2, 1, 1, GL_RGBA,
		     GL_UNSIGNED_BYTE, px);
	printf("glprobe: changed=%u sync=%u present=%u last=%d,%d %dx%d "
	       "rgb24=%d\n", n_changed, n_sync, n_present, last_present[0],
	       last_present[1], last_present[2], last_present[3],
	       last_present[4]);
	printf("glprobe: fb(%d,%d)=%d,%d,%d,%d cpu=%04x err=0x%x\n", x, y,
	       px[0], px[1], px[2], px[3],
	       cpu_vram[(y & 511) * 1024 + (x & 1023)], glGetError());
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
