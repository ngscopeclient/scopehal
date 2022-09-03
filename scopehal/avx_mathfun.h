/*
   AVX implementation of sin, cos, sincos, exp and log

   Based on "sse_mathfun.h", by Julien Pommier
   http://gruntthepeon.free.fr/ssemath/

   Copyright (C) 2012 Giovanni Garberoglio
   Interdisciplinary Laboratory for Computational Science (LISC)
   Fondazione Bruno Kessler and University of Trento
   via Sommarive, 18
   I-38123 Trento (Italy)

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  (this is the zlib license)

  Modified by A. Zonenberg:
  * Added convenient nicknames to make these functions fit the Intel intrisic naming schema used by IPP
  * Added __attribute__((target("avx2"))) to each function for use in mixed environments
  * Added function prototypes
  * Removed no-AVX2 functionality since we only use these functions if AVX2 is present
  * Fixed _PS256_CONST
  * Moved constants inside functions so we can handle compiling without -mavx2
  * Moved a bunch of implementation stuff to a source file
*/

#ifdef __x86_64__

#include <immintrin.h>

/* yes I know, the top of this file is quite ugly */
# define ALIGN32_BEG
# define ALIGN32_END __attribute__((aligned(32)))

/* __m128 is ugly to write */
typedef __m256  v8sf; // vector of 8 float (avx)
typedef __m256i v8si; // vector of 8 int   (avx)

//Added function prototypes
v8sf _mm256_log_ps(v8sf);
v8sf exp256_ps(v8sf);
v8sf _mm256_sin_ps(v8sf);
v8sf _mm256_cos_ps(v8sf);
void _mm256_sincos_ps(v8sf xx, v8sf*, v8sf*);

/* declare some AVX constants -- why can't I figure a better way to do that? */

#define _PS256_CONST(Name, Val)                                            \
    v8sf _ps256_##Name = { Val, Val, Val, Val, Val, Val, Val, Val }
#define _PI32_CONST256(Name, Val)                                            \
    int _pi32_256_tmp_##Name[8] ALIGN32_END = { Val, Val, Val, Val, Val, Val, Val, Val }; \
    v8si _pi32_256_##Name = *reinterpret_cast<v8si*>(&_pi32_256_tmp_##Name)
#define _PS256_CONST_TYPE(Name, Type, Val)                                 \
    int _pi32_256_tmp_##Name[8] ALIGN32_END = { Val, Val, Val, Val, Val, Val, Val, Val }; \
    v8sf _ps256_##Name = _mm256_load_ps((float*)&_pi32_256_tmp_##Name)

#endif /* __x86_64__ */