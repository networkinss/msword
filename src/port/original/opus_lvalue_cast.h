#ifndef OPUS_LVALUE_CAST_H
#define OPUS_LVALUE_CAST_H

/*
 * Cast-as-lvalue compatibility for the original Opus sources.
 *
 * The original code uses the K&R / Microsoft C "cast as lvalue" extension to
 * serialise a value through a pointer and advance that pointer in one step:
 *
 *      *((int *) pb)++ = 0;            (store, then advance)
 *      rw = *((int *) pce)++;          (load,  then advance)
 *
 * GCC removed the extension in 4.0 and clang has never accepted it, in either
 * the GNU or the clang-cl driver; -fms-extensions does not relax it. MSVC still
 * accepts it, so the MSVC branch below expands to the original spelling
 * verbatim and that build is unchanged, bit for bit.
 *
 * Semantics that must be preserved exactly: the pointer advances by
 * sizeof(TYPE) *BYTES*, regardless of the pointer's own declared type. That is
 * not the same as `p += sizeof(TYPE)` -- at Opus/interp/exp.c the pointer is an
 * `int *`, where `p += sizeof(long)` would advance four times too far. The
 * arithmetic below is therefore always done through `char *`.
 */

#if defined(__GNUC__) || defined(__clang__)

/* Store `v` through a TYPE * view of `p`, then advance `p` by sizeof(TYPE)
   bytes. The void * round-trip keeps this valid for any pointer type `p`. */
#define OpusPutPp(TYPE, p, v)                                                 \
    (*(TYPE *)(void *)(p) = (v),                                              \
     (p) = (void *)((char *)(void *)(p) + sizeof(TYPE)))

/* Load a TYPE through `p`, then advance `p` by sizeof(TYPE) bytes. `p` is
   advanced first and the value read back from the pre-advance address, which
   avoids needing a temporary while keeping evaluation order well defined. */
#define OpusGetPp(TYPE, p)                                                    \
    (*(TYPE *)(void *)(((p) = (void *)((char *)(void *)(p) + sizeof(TYPE))),  \
                       (char *)(void *)(p) - sizeof(TYPE)))

/* Advance `p` by `n` elements of TYPE, i.e. n * sizeof(TYPE) bytes, for the
   original `(char *) pchr += cb` / `(WORD *) pelpSrc += 1` spelling. */
#define OpusAdvPp(TYPE, p, n)                                                 \
    ((p) = (void *)((char *)(void *)(p) + (n) * (long)sizeof(TYPE)))

/* Logical (zero-filling) shift of a signed integer lvalue -- the original
   `(uns) w >>= 1` casts to unsigned precisely to avoid sign extension. */
#define OpusShrU(w, n) ((w) = (unsigned)(w) >> (n))
#define OpusShlU(w, n) ((w) = (unsigned)(w) << (n))

/* Assign through a TYPE * view of `p`, for the original
   `(char *) pchr = (char *) *vhgrpchr + bchrCur;` spelling. The value is a
   byte address; the cast only existed to retype the destination. */
#define OpusSetPp(TYPE, p, v) ((p) = (void *)(v))

#else /* MSVC: keep the original cast-as-lvalue spelling. */

#define OpusPutPp(TYPE, p, v) (*((TYPE *)(p))++ = (v))
#define OpusGetPp(TYPE, p)    (*((TYPE *)(p))++)
#define OpusAdvPp(TYPE, p, n) ((TYPE *)(p) += (n))
#define OpusShrU(w, n)        ((unsigned)(w) >>= (n))
#define OpusShlU(w, n)        ((unsigned)(w) <<= (n))
#define OpusSetPp(TYPE, p, v) ((TYPE *)(p) = (v))

#endif

#endif /* OPUS_LVALUE_CAST_H */
