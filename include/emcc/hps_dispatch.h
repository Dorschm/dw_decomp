/*
 * Forced into src/main/partner.c only (see mk/toolchain/emcc.mk).
 *
 * partner.c calls handlePraiseScold() both with no argument
 * (partner.c:403) and with a state argument (partner.c:401) under a
 * single K&R declaration.  A wasm callee can only have one signature,
 * so the two call sites are dispatched to arity-specific shims:
 *   handlePraiseScold()    -> handlePraiseScold_0()
 *   handlePraiseScold(s)   -> handlePraiseScold_1(s)
 * The shims live in src/web/psx/call_fixups.c and forward to the real
 * implementation (renamed handlePraiseScold_impl in partner_impl.c).
 * The file-scope declaration `void handlePraiseScold();` expands to
 * `void handlePraiseScold_0();`, which is harmless.
 */

#define PSX_CAT_(a, b) a##b
#define PSX_CAT(a, b) PSX_CAT_(a, b)
#define PSX_NARG_(_1, _2, _3, _4, N, ...) N
#define PSX_NARG(...) PSX_NARG_(_, ##__VA_ARGS__, 3, 2, 1, 0)
#define handlePraiseScold(...) \
	PSX_CAT(handlePraiseScold_, PSX_NARG(__VA_ARGS__))(__VA_ARGS__)
