/* SPDX-License-Identifier: EUPL-1.2 */
/* What speexdsp's configure would have put in its config.h, for the files
   in this folder (and only them: no global -D flag). */
#ifndef CREWRADIO_SPEEX_CONFIG_H
#define CREWRADIO_SPEEX_CONFIG_H
#define FLOATING_POINT
#define USE_SMALLFT
#define EXPORT

/* Single precision throughout. speexdsp's float build calls the double
   functions (sqrt, exp, ...) and writes its constants as doubles, and the
   ESP32-P4's FPU is single precision only: each of those was a call into
   the soft-float library, and a frame took 10 ms of its 20. math.h first,
   so its own declarations stay as they are; then the names speexdsp uses
   are its float functions, and its constants floats. */
#include <math.h>
#define sqrt(x) sqrtf(x)
#define exp(x) expf(x)
#define log(x) logf(x)
#define log10(x) log10f(x)
#define pow(x, y) powf(x, y)
#define floor(x) floorf(x)
#define atan(x) atanf(x)
#define cos(x) cosf(x)
#define sin(x) sinf(x)
#define fabs(x) fabsf(x)
/* And for speed rather than size (the build's -Os), these files only: they
   run every 20 ms. */
#pragma GCC optimize("O2", "single-precision-constant", "no-math-errno")
#endif
