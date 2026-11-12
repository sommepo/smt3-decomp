#include "common.h"

#ifdef VERSION_MNAX
#include "ps2asm.h"

/* gcc 2.95 libgcc2.c and longlong.h, as each of _divdi3.o, _moddi3.o,
 * _udivdi3.o and _umoddi3.o compiles them: a static inline __udivmoddi4 with
 * the generic C udiv_qrnnd (so UDIV_NEEDS_NORMALIZATION), the __clz_tab
 * count_leading_zeros and the MIPS umul_ppmm. Each object has its own copy of
 * __clz_tab; the one shared inline takes it as a parameter. */
typedef unsigned int UQItype __attribute__ ((mode (QI)));
typedef int SItype __attribute__ ((mode (SI)));
typedef unsigned int USItype __attribute__ ((mode (SI)));
typedef int DItype __attribute__ ((mode (DI)));
typedef unsigned int UDItype __attribute__ ((mode (DI)));
typedef int word_type __attribute__ ((mode (__word__)));

#define SI_TYPE_SIZE (sizeof (SItype) * BITS_PER_UNIT)
#define BITS_PER_UNIT 8

struct DIstruct {SItype low, high;};

typedef union
{
  struct DIstruct s;
  DItype ll;
} DIunion;

#define __BITS4 (SI_TYPE_SIZE / 4)
#define __ll_B ((USItype) 1 << (SI_TYPE_SIZE / 2))
#define __ll_lowpart(t) ((USItype) (t) & (__ll_B - 1))
#define __ll_highpart(t) ((USItype) (t) >> (SI_TYPE_SIZE / 2))

#define umul_ppmm(w1, w0, u, v) EE_MULTU_HI_LO(w1, w0, u, v)

#define sub_ddmmss(sh, sl, ah, al, bh, bl) \
  do {									\
    USItype __x;							\
    __x = (al) - (bl);							\
    (sh) = (ah) - (bh) - (__x > (al));					\
    (sl) = __x;								\
  } while (0)

#define __udiv_qrnnd_c(q, r, n1, n0, d) \
  do {									\
    USItype __d1, __d0, __q1, __q0;					\
    USItype __r1, __r0, __m;						\
    __d1 = __ll_highpart (d);						\
    __d0 = __ll_lowpart (d);						\
									\
    __r1 = (n1) % __d1;							\
    __q1 = (n1) / __d1;							\
    __m = (USItype) __q1 * __d0;					\
    __r1 = __r1 * __ll_B | __ll_highpart (n0);				\
    if (__r1 < __m)							\
      {									\
	__q1--, __r1 += (d);						\
	if (__r1 >= (d)) /* i.e. we didn't get carry when adding to __r1 */\
	  if (__r1 < __m)						\
	    __q1--, __r1 += (d);					\
      }									\
    __r1 -= __m;							\
									\
    __r0 = __r1 % __d1;							\
    __q0 = __r1 / __d1;							\
    __m = (USItype) __q0 * __d0;					\
    __r0 = __r0 * __ll_B | __ll_lowpart (n0);				\
    if (__r0 < __m)							\
      {									\
	__q0--, __r0 += (d);						\
	if (__r0 >= (d))						\
	  if (__r0 < __m)						\
	    __q0--, __r0 += (d);					\
      }									\
    __r0 -= __m;							\
									\
    (q) = (USItype) __q1 * __ll_B | __q0;				\
    (r) = __r0;								\
  } while (0)

#define UDIV_NEEDS_NORMALIZATION 1
#define udiv_qrnnd __udiv_qrnnd_c

#define count_leading_zeros(count, x) \
  do {									\
    USItype __xr = (x);							\
    USItype __a;							\
									\
    if (SI_TYPE_SIZE <= 32)						\
      {									\
	__a = __xr < ((USItype)1<<2*__BITS4)				\
	  ? (__xr < ((USItype)1<<__BITS4) ? 0 : __BITS4)		\
	  : (__xr < ((USItype)1<<3*__BITS4) ?  2*__BITS4 : 3*__BITS4);	\
      }									\
    else								\
      {									\
	for (__a = SI_TYPE_SIZE - 8; __a > 0; __a -= 8)			\
	  if (((__xr >> __a) & 0xff) != 0)				\
	    break;							\
      }									\
									\
    (count) = SI_TYPE_SIZE - (__clz_tab[__xr >> __a] + __a);		\
  } while (0)

extern const u8 libgccDivdiBitLengths[256];
extern const u8 libgccModdiBitLengths[256];
extern const u8 libgccUdivdiBitLengths[256];
extern const u8 libgccUmoddiBitLengths[];

static inline
DItype
__negdi2 (DItype u)
{
  DIunion w;
  DIunion uu;

  uu.ll = u;

  w.s.low = -uu.s.low;
  w.s.high = -uu.s.high - ((USItype) w.s.low > 0);

  return w.ll;
}

static inline
UDItype
__udivmoddi4 (UDItype n, UDItype d, UDItype *rp, const UQItype *__clz_tab)
{
  DIunion ww;
  DIunion nn, dd;
  DIunion rr;
  USItype d0, d1, n0, n1, n2;
  USItype q0, q1;
  USItype b, bm;

  nn.ll = n;
  dd.ll = d;

  d0 = dd.s.low;
  d1 = dd.s.high;
  n0 = nn.s.low;
  n1 = nn.s.high;

#if !UDIV_NEEDS_NORMALIZATION
  if (d1 == 0)
    {
      if (d0 > n1)
	{
	  /* 0q = nn / 0D */

	  udiv_qrnnd (q0, n0, n1, n0, d0);
	  q1 = 0;

	  /* Remainder in n0.  */
	}
      else
	{
	  /* qq = NN / 0d */

	  if (d0 == 0)
	    d0 = 1 / d0;	/* Divide intentionally by zero.  */

	  udiv_qrnnd (q1, n1, 0, n1, d0);
	  udiv_qrnnd (q0, n0, n1, n0, d0);

	  /* Remainder in n0.  */
	}

      if (rp != 0)
	{
	  rr.s.low = n0;
	  rr.s.high = 0;
	  *rp = rr.ll;
	}
    }

#else /* UDIV_NEEDS_NORMALIZATION */

  if (d1 == 0)
    {
      if (d0 > n1)
	{
	  /* 0q = nn / 0D */

	  count_leading_zeros (bm, d0);

	  if (bm != 0)
	    {
	      /* Normalize, i.e. make the most significant bit of the
		 denominator set.  */

	      d0 = d0 << bm;
	      n1 = (n1 << bm) | (n0 >> (SI_TYPE_SIZE - bm));
	      n0 = n0 << bm;
	    }

	  udiv_qrnnd (q0, n0, n1, n0, d0);
	  q1 = 0;

	  /* Remainder in n0 >> bm.  */
	}
      else
	{
	  /* qq = NN / 0d */

	  if (d0 == 0)
	    d0 = 1 / d0;	/* Divide intentionally by zero.  */

	  count_leading_zeros (bm, d0);

	  if (bm == 0)
	    {
	      /* From (n1 >= d0) /\ (the most significant bit of d0 is set),
		 conclude (the most significant bit of n1 is set) /\ (the
		 leading quotient digit q1 = 1).

		 This special case is necessary, not an optimization.
		 (Shifts counts of SI_TYPE_SIZE are undefined.)  */

	      n1 -= d0;
	      q1 = 1;
	    }
	  else
	    {
	      /* Normalize.  */

	      b = SI_TYPE_SIZE - bm;

	      d0 = d0 << bm;
	      n2 = n1 >> b;
	      n1 = (n1 << bm) | (n0 >> b);
	      n0 = n0 << bm;

	      udiv_qrnnd (q1, n1, n2, n1, d0);
	    }

	  /* n1 != d0...  */

	  udiv_qrnnd (q0, n0, n1, n0, d0);

	  /* Remainder in n0 >> bm.  */
	}

      if (rp != 0)
	{
	  rr.s.low = n0 >> bm;
	  rr.s.high = 0;
	  *rp = rr.ll;
	}
    }
#endif /* UDIV_NEEDS_NORMALIZATION */

  else
    {
      if (d1 > n1)
	{
	  /* 00 = nn / DD */

	  q0 = 0;
	  q1 = 0;

	  /* Remainder in n1n0.  */
	  if (rp != 0)
	    {
	      rr.s.low = n0;
	      rr.s.high = n1;
	      *rp = rr.ll;
	    }
	}
      else
	{
	  /* 0q = NN / dd */

	  count_leading_zeros (bm, d1);
	  if (bm == 0)
	    {
	      /* From (n1 >= d1) /\ (the most significant bit of d1 is set),
		 conclude (the most significant bit of n1 is set) /\ (the
		 quotient digit q0 = 0 or 1).

		 This special case is necessary, not an optimization.  */

	      /* The condition on the next line takes advantage of that
		 n1 >= d1 (true due to program flow).  */
	      if (n1 > d1 || n0 >= d0)
		{
		  q0 = 1;
		  sub_ddmmss (n1, n0, n1, n0, d1, d0);
		}
	      else
		q0 = 0;

	      q1 = 0;

	      if (rp != 0)
		{
		  rr.s.low = n0;
		  rr.s.high = n1;
		  *rp = rr.ll;
		}
	    }
	  else
	    {
	      USItype m1, m0;
	      /* Normalize.  */

	      b = SI_TYPE_SIZE - bm;

	      d1 = (d1 << bm) | (d0 >> b);
	      d0 = d0 << bm;
	      n2 = n1 >> b;
	      n1 = (n1 << bm) | (n0 >> b);
	      n0 = n0 << bm;

	      udiv_qrnnd (q0, n1, n2, n1, d1);
	      umul_ppmm (m1, m0, q0, d0);

	      if (m1 > n1 || (m1 == n1 && m0 > n0))
		{
		  q0--;
		  sub_ddmmss (m1, m0, m1, m0, d1, d0);
		}

	      q1 = 0;

	      /* Remainder in (n1n0 - m1m0) >> bm.  */
	      if (rp != 0)
		{
		  sub_ddmmss (n1, n0, n1, n0, m1, m0);
		  rr.s.low = (n1 << b) | (n0 >> bm);
		  rr.s.high = n1 >> bm;
		  *rp = rr.ll;
		}
	    }
	}
    }

  ww.s.low = q0;
  ww.s.high = q1;
  return ww.ll;
}
#endif

INCLUDE_ASM(const s32, "sdk/libgcc_longlong", libc_2E52FC);

#ifdef VERSION_MNAX
DItype
__divdi3 (DItype u, DItype v)
{
  word_type c = 0;
  DIunion uu, vv;
  DItype w;

  uu.ll = u;
  vv.ll = v;

  if (uu.s.high < 0)
    c = ~c,
    uu.ll = __negdi2 (uu.ll);
  if (vv.s.high < 0)
    c = ~c,
    vv.ll = __negdi2 (vv.ll);

  w = __udivmoddi4 (uu.ll, vv.ll, (UDItype *) 0, libgccDivdiBitLengths);
  if (c)
    w = __negdi2 (w);

  return w;
}
#else
INCLUDE_ASM(const s32, "sdk/libgcc_longlong", __divdi3);
#endif

#ifdef NON_MATCHING
/* EE little-endian raw word view; arithmetic below converts each half to u32. */
typedef union {
    u64 value;
    struct { s32 low, high; } words;
} runtimeWide_t;

extern const u8 libgccUdivdiBitLengths[256];

static inline u32 LeadingZeroBits(u32 word)
{
    u32 shift;
    if (word < 0x10000) {
        shift = word < 0x100 ? 0 : 8;
    } else {
        shift = word < 0x1000000 ? 16 : 24;
    }
    return 32 - (libgccUdivdiBitLengths[word >> shift] + shift);
}

/* The historical longlong.h uses this multi-output arithmetic primitive.
 * A macro keeps quotient/remainder outputs scalar under this compiler.
 * Divide a two-word numerator by a normalized one-word divisor, estimating
 * and correcting each base-65536 quotient digit. */
#define DIVIDE_NORMALIZED(quotient, remainder, high, low, divisor) \
    do { \
        u32 divisorHigh = divisor >> 16; \
        u32 divisorLow = divisor & 0xFFFF; \
        u32 highDigit, lowDigit, highRemainder, lowRemainder, product; \
     \
        highDigit = high / divisorHigh; \
        highRemainder = high % divisorHigh; \
        product = highDigit * divisorLow; \
        highRemainder = (highRemainder << 16) | (low >> 16); \
        if (highRemainder < product) { \
            highDigit--; \
            highRemainder += divisor; \
            if (highRemainder >= divisor && highRemainder < product) { \
                highDigit--; \
                highRemainder += divisor; \
            } \
        } \
        highRemainder -= product; \
        lowDigit = highRemainder / divisorHigh; \
        lowRemainder = highRemainder % divisorHigh; \
        product = lowDigit * divisorLow; \
        lowRemainder = (lowRemainder << 16) | (low & 0xFFFF); \
        if (lowRemainder < product) { \
            lowDigit--; \
            lowRemainder += divisor; \
            if (lowRemainder >= divisor && lowRemainder < product) { \
                lowDigit--; \
                lowRemainder += divisor; \
            } \
        } \
        (remainder) = lowRemainder - product; \
        (quotient) = (highDigit << 16) | lowDigit; \
    } while (0)

#endif

#ifdef NON_MATCHING
/* Preserve the shared helper's optional remainder output. The wrapper
 * discards its quotient and returns the remainder written to its local. */
extern const u8 libgccModdiBitLengths[256];
extern const u8 libgccUmoddiBitLengths[];

static inline u32 RemainderLeadingZeroBits(u32 word, const u8 *bitLengths)
{
    u32 shift;
    if (word < 0x10000) {
        shift = word < 0x100 ? 0 : 8;
    } else {
        shift = word < 0x1000000 ? 16 : 24;
    }
    return 32 - (bitLengths[word >> shift] + shift);
}

static inline u64 DivideWithRemainder(u64 numerator, u64 denominator, u64 *remainder,
                                          const u8 *bitLengths)
{
    runtimeWide_t n, d, result, rest;
    u32 nHigh, nLow, dHigh, dLow, shift, inverse;
    u32 qHigh, qLow;

    n.value = numerator;
    d.value = denominator;
    dHigh = d.words.high;
    nHigh = n.words.high;
    dLow = d.words.low;
    nLow = n.words.low;
    if (dHigh == 0) {
        if (dLow > nHigh) {
            shift = RemainderLeadingZeroBits(dLow, bitLengths);
            if (shift != 0) {
                dLow <<= shift;
                nHigh = (nHigh << shift) | (nLow >> (32 - shift));
                nLow <<= shift;
            }
            DIVIDE_NORMALIZED(qLow, nHigh, nHigh, nLow, dLow);
            qHigh = 0;
        } else {
            if (dLow == 0) {
                dLow = 1 / dLow;
            }
            shift = RemainderLeadingZeroBits(dLow, bitLengths);
            if (shift == 0) {
                nHigh -= dLow;
                qHigh = 1;
            } else {
                u32 upper;
                inverse = 32 - shift;
                dLow <<= shift;
                upper = nHigh >> inverse;
                nHigh = (nHigh << shift) | (nLow >> inverse);
                nLow <<= shift;
                DIVIDE_NORMALIZED(qHigh, nHigh, upper, nHigh, dLow);
            }
            DIVIDE_NORMALIZED(qLow, nHigh, nHigh, nLow, dLow);
        }
        if (remainder != NULL) {
            rest.words.low = nHigh >> shift;
            rest.words.high = 0;
            *remainder = rest.value;
        }
    } else if (dHigh > nHigh) {
        qLow = 0;
        qHigh = 0;
        if (remainder != NULL) {
            rest.words.low = nLow;
            rest.words.high = nHigh;
            *remainder = rest.value;
        }
    } else {
        shift = RemainderLeadingZeroBits(dHigh, bitLengths);
        if (shift == 0) {
            if (nHigh > dHigh || nLow >= dLow) {
                u32 previousLow = nLow;
                qLow = 1;
                nLow -= dLow;
                nHigh = nHigh - dHigh - (previousLow < nLow);
            } else {
                qLow = 0;
            }
            if (remainder != NULL) {
                rest.words.low = nLow;
                rest.words.high = nHigh;
                *remainder = rest.value;
            }
        } else {
            u32 upper, productHigh, productLow;
            runtimeWide_t product;
            inverse = 32 - shift;
            dHigh = (dHigh << shift) | (dLow >> inverse);
            dLow <<= shift;
            upper = nHigh >> inverse;
            nHigh = (nHigh << shift) | (nLow >> inverse);
            nLow <<= shift;
            DIVIDE_NORMALIZED(qLow, nHigh, upper, nHigh, dHigh);
            product.value = (u64)qLow * dLow;
            productHigh = product.words.high;
            productLow = product.words.low;
            if (productHigh > nHigh ||
                (productHigh == nHigh && productLow > nLow)) {
                u32 previousLow = productLow;
                qLow--;
                productLow -= dLow;
                productHigh = productHigh - dHigh - (previousLow < productLow);
            }
            if (remainder != NULL) {
                u32 previousLow = nLow;
                nLow -= productLow;
                nHigh = nHigh - productHigh - (previousLow < nLow);
                rest.words.low = (nHigh << inverse) | (nLow >> shift);
                rest.words.high = nHigh >> shift;
                *remainder = rest.value;
            }
        }
        qHigh = 0;
    }
    result.words.low = qLow;
    result.words.high = qHigh;
    return result.value;
}

#endif

#if defined(VERSION_MNAX)
DItype
__moddi3 (DItype u, DItype v)
{
  word_type c = 0;
  DIunion uu, vv;
  DItype w;

  uu.ll = u;
  vv.ll = v;

  if (uu.s.high < 0)
    c = ~c,
    uu.ll = __negdi2 (uu.ll);
  if (vv.s.high < 0)
    vv.ll = __negdi2 (vv.ll);

  (void) __udivmoddi4 (uu.ll, vv.ll, &w, libgccModdiBitLengths);
  if (c)
    w = __negdi2 (w);

  return w;
}
#elif defined(NON_MATCHING)
s64 __moddi3(s64 numerator, s64 denominator)
{
    runtimeWide_t n, d, remainder;
    s32 negative = 0;

    n.value = (u64)numerator;
    d.value = (u64)denominator;
    if (n.words.high < 0) {
        u32 negatedHigh = 0U - (u32)n.words.high;
        n.words.low = 0U - (u32)n.words.low;
        n.words.high = negatedHigh - ((u32)n.words.low != 0);
        negative = -1;
    }
    if (d.words.high < 0) {
        u32 negatedHigh = 0U - (u32)d.words.high;
        d.words.low = 0U - (u32)d.words.low;
        d.words.high = negatedHigh - ((u32)d.words.low != 0);
    }
    DivideWithRemainder(n.value, d.value, &remainder.value, libgccModdiBitLengths);
    if (negative != 0) {
        u32 negatedHigh = 0U - (u32)remainder.words.high;
        remainder.words.low = 0U - (u32)remainder.words.low;
        remainder.words.high = negatedHigh -
                               ((u32)remainder.words.low != 0);
    }
    return (s64)remainder.value;
}

#else
INCLUDE_ASM(const s32, "sdk/libgcc_longlong", __moddi3);
#endif

#if defined(VERSION_MNAX)
UDItype
__udivdi3 (UDItype n, UDItype d)
{
  return __udivmoddi4 (n, d, (UDItype *) 0, libgccUdivdiBitLengths);
}
#elif defined(NON_MATCHING)
/* NON_MATCHING: 1424/1404 bytes; 74 of 351 native words match.
 * Widening-product word extraction and scalar allocation differ. */
u64 __udivdi3(u64 numerator, u64 denominator)
{
    runtimeWide_t n, d, result;
    u32 nHigh, nLow, dHigh, dLow, shift, inverse;
    u32 qHigh, qLow;
    n.value = numerator;
    d.value = denominator;
    dHigh = d.words.high;
    nHigh = n.words.high;
    dLow = d.words.low;
    nLow = n.words.low;
    if (dHigh == 0) {
        if (dLow > nHigh) {
            shift = LeadingZeroBits(dLow);
            if (shift != 0) {
                dLow <<= shift;
                nHigh = (nHigh << shift) | (nLow >> (32 - shift));
                nLow <<= shift;
            }
            DIVIDE_NORMALIZED(qLow, nHigh, nHigh, nLow, dLow);
            qHigh = 0;
        } else {
            if (dLow == 0) {
                /* Preserve the runtime's native divide-by-zero trap. */
                dLow = 1 / dLow;
            }
            shift = LeadingZeroBits(dLow);
            if (shift == 0) {
                nHigh -= dLow;
                qHigh = 1;
            } else {
                u32 upper;
                inverse = 32 - shift;
                dLow <<= shift;
                upper = nHigh >> inverse;
                nHigh = (nHigh << shift) | (nLow >> inverse);
                nLow <<= shift;
                DIVIDE_NORMALIZED(qHigh, nHigh, upper, nHigh, dLow);
            }
            DIVIDE_NORMALIZED(qLow, nHigh, nHigh, nLow, dLow);
        }
    } else if (dHigh > nHigh) {
        qLow = 0;
        qHigh = 0;
    } else {
        shift = LeadingZeroBits(dHigh);
        if (shift == 0) {
            if (nHigh > dHigh || nLow >= dLow) {
                qLow = 1;
            } else {
                qLow = 0;
            }
        } else {
            u32 upper, productHigh, productLow;
            runtimeWide_t product;
            inverse = 32 - shift;
            dHigh = (dHigh << shift) | (dLow >> inverse);
            dLow <<= shift;
            upper = nHigh >> inverse;
            nHigh = (nHigh << shift) | (nLow >> inverse);
            nLow <<= shift;
            DIVIDE_NORMALIZED(qLow, nHigh, upper, nHigh, dHigh);
            product.value = (u64)qLow * dLow;
            productHigh = product.words.high;
            productLow = product.words.low;
            if (productHigh > nHigh ||
                (productHigh == nHigh && productLow > nLow)) {
                qLow--;
            }
        }
        qHigh = 0;
    }
    result.words.low = qLow;
    result.words.high = qHigh;
    return result.value;
}

#else
INCLUDE_ASM(const s32, "sdk/libgcc_longlong", __udivdi3);
#endif

#if defined(VERSION_MNAX)
UDItype
__umoddi3 (UDItype u, UDItype v)
{
  UDItype w;

  (void) __udivmoddi4 (u, v, &w, libgccUmoddiBitLengths);

  return w;
}
#elif defined(NON_MATCHING)
u64 __umoddi3(u64 numerator, u64 denominator)
{
    u64 remainder;
    DivideWithRemainder(numerator, denominator, &remainder, libgccUmoddiBitLengths);
    return remainder;
}

#undef DIVIDE_NORMALIZED
#else
INCLUDE_ASM(const s32, "sdk/libgcc_longlong", __umoddi3);
#endif

const u8 libgccDivdiBitLengths[256] = {0x00, 0x01, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08};

const u8 libgccModdiBitLengths[256] = {0x00, 0x01, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08};

const u8 libgccUdivdiBitLengths[256] = {0x00, 0x01, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08};

const u8 libgccUmoddiBitLengths[264] = {0x00, 0x01, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

