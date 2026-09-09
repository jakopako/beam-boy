/********************************************************************
** Beam Boy — trimmed Berry build configuration.
**
** Starting point: upstream default/berry_conf.h. Diffs from that default
** are commented inline; everything else is left at the upstream default
** so future Berry upgrades stay easy to diff.
**
** Rationale for each change is in docs/phase-5-vm-bakeoff.md ("Berry build
** recipe"): cartridges ship as precompiled bytecode (the compiler lives in
** the build pipeline, not on the device), single-precision float matches
** the ESP32-S3's FPU width, and every module a cartridge must not touch
** (fs / os / debug / solidify / time) is compiled out entirely rather than
** merely left unused, since BE_USE_XXX_MODULE controls whether the code is
** linked in at all.
********************************************************************/
#ifndef BERRY_CONF_H
#define BERRY_CONF_H

#include <assert.h>

#ifndef BE_DEBUG
#define BE_DEBUG                        0
#endif

#define BE_INTGER_TYPE                  1   /* CHANGED: 32-bit long, not 64-bit long long -- cartridges don't need 64-bit ints and this halves the size of every boxed integer. */

#define BE_USE_SINGLE_FLOAT             1   /* CHANGED: matches the ESP32-S3 FPU's native width; no accuracy the cartridges need is lost. */

#define BE_BYTES_MAX_SIZE               (32*1024)

#define BE_USE_PRECOMPILED_OBJECT       1

#define BE_DEBUG_SOURCE_FILE            1

#define BE_DEBUG_RUNTIME_INFO           1

#define BE_DEBUG_VAR_INFO               1

#define BE_USE_PERF_COUNTERS            1

#define BE_VM_OBSERVABILITY_SAMPLING    12  /* CHANGED from upstream default 20: the Phase 6 sandbox (src/vm/beam_api.cpp) checks its per-call time budget from this heartbeat. At the default (every ~1M instructions), the Phase 5 bake-off's measured per-instruction cost means the heartbeat could fire tens of milliseconds apart -- looser than the 8 ms budget it is meant to enforce. Every 4096 instructions catches an overrun promptly without measurably slowing normal cartridges (the check itself is one branch). */

#define BE_STACK_TOTAL_MAX              20000

#define BE_STACK_FREE_MIN               10

#define BE_STACK_START                  50

#define BE_CONST_SEARCH_SIZE            50

#define BE_USE_STR_HASH_CACHE           0

#define BE_USE_FILE_SYSTEM              0   /* CHANGED: cartridges must not touch the filesystem directly -- all persistence goes through the bound Beam API, not Berry's os/io. Note this flag alone does not stop be_filelib.c from compiling (it has no #if guard at all, unlike the other libs) -- it is excluded outright via library.json's srcFilter since it needs be_fopen/be_fread/etc. from default/be_port.c, which we do not vendor. */

#define BE_USE_SCRIPT_COMPILER          1   /* Bake-off keeps the compiler on-device for now: scripts load as source via be_loadstring(), which is far faster to iterate on than a host-side bytecode-compile step. Once Berry is confirmed as the shipping VM, switch to precompiled .bec per the plan (see docs/phase-5-vm-bakeoff.md) to drop the ~96 KB parser/lexer from the shipped image. */

#define BE_USE_BYTECODE_SAVER           0   /* CHANGED: saving is a build-pipeline concern (native berry_compile tool), not a device one. */

#define BE_USE_BYTECODE_LOADER          0   /* Not used while scripts load as source (see BE_USE_SCRIPT_COMPILER above). */

#define BE_USE_SHARED_LIB               0   /* CHANGED: no dynamic loading of .so/.dll on a microcontroller. */

#define BE_USE_OVERLOAD_HASH            1

#define BE_MAX_PARSER_DEPTH             25

#define BE_USE_DEBUG_HOOK               0

#define BE_USE_DEBUG_GC                  0

#define BE_USE_DEBUG_STACK               0

#define BE_USE_MEM_ALIGNED               0

/* CHANGED: every module below is compiled out unless a cartridge specifically
 * needs it. Cartridges talk to the console exclusively through the bound
 * `beam` API (see src/vm/beam_api.*), so none of Berry's own os/time/debug
 * surface should be reachable from a script. json/math/string stay enabled
 * since games plausibly need them and they cost little. */
#define BE_USE_STRING_MODULE            1
#define BE_USE_JSON_MODULE              0   /* CHANGED: cartridges receive data through the Beam API, not by parsing JSON on-device. */
#define BE_USE_MATH_MODULE              1
#define BE_USE_TIME_MODULE              0   /* CHANGED: use beam.now()/beam.dt(), not Berry's own OS-clock access. */
#define BE_USE_OS_MODULE                0   /* CHANGED: no OS surface for a sandboxed cartridge. */
#define BE_USE_GLOBAL_MODULE            1
#define BE_USE_SYS_MODULE                0   /* CHANGED: sys exposes the interpreter/host details a cartridge has no business touching. */
#define BE_USE_DEBUG_MODULE             0   /* CHANGED: debug library is a development aid, not a cartridge dependency; drop it from the shipped image. */
#define BE_USE_GC_MODULE                1   /* Kept enabled: gc.collect() is a reasonable escape hatch for a long-running cartridge. */
#define BE_USE_SOLIDIFY_MODULE          0   /* CHANGED: solidify (Berry object -> bytecode) is a build-pipeline tool, not a device feature. */
#define BE_USE_INTROSPECT_MODULE        0   /* CHANGED: introspection is a debugging aid with no cartridge use case. */
#define BE_USE_STRICT_MODULE            1

#define BE_EXPLICIT_ABORT               abort
#define BE_EXPLICIT_EXIT                exit
#define BE_EXPLICIT_MALLOC              malloc
#define BE_EXPLICIT_FREE                free
#define BE_EXPLICIT_REALLOC             realloc

#ifndef be_assert
#if BE_DEBUG
#define be_assert(expr)     assert(expr)
#else
#define be_assert(expr)     ((void)0)
#endif
#endif

#endif
