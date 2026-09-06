/* Pseudorandom source, Win32.                                    SESSION 7

   This file reimplements glibc's random() rather than calling a Windows
   generator, which needs justifying because reimplementing a libc routine is
   normally the wrong instinct.

   Two things constrain the choice. lrzip never seeds the generator, so on
   Linux hash_index[] is a fixed table and compressed output is reproducible
   across runs and machines; and every one of the 32 bits must vary, because
   the table seeds the rolling hash that drives match-finding.

   The obvious substitutions each break one of those:

     rand()    UCRT caps RAND_MAX at 32767, so upstream's two-call expression
               leaves bits 15 and 31 permanently zero. Measured, not assumed:
               100000 draws of ((unsigned)rand() << 16) ^ (unsigned)rand()
               cover 0x7FFF7FFF, with 0x80008000 never set.
     rand_s()  Full width, but seeded from the OS entropy pool, so the table
               would differ on every run and Windows output would stop being
               reproducible.

   Matching glibc costs about thirty lines and buys more than either: Windows
   and Linux produce byte-identical archives from identical input, which makes
   cross-platform output comparison a usable test in Session 11.

   The generator is glibc's TYPE_3 additive-feedback design: a degree-31
   trinomial, r[i] = r[i-31] + r[i-3] with 32-bit wraparound, returning the
   top 31 bits. Verified against the documented first ten outputs of the
   unseeded (equivalently srandom(1)) sequence.
*/

#include "lr_platform.h"
#include "lr_win32.h"
#include <bcrypt.h>
#include <limits.h>

/* Ring of the last 31 values. Index arithmetic works modulo 31 because the
   recurrence reaches back exactly 31 places: (i - 31) mod 31 == i mod 31. */
static uint32_t lr_rand_state[31];
static unsigned lr_rand_idx;
static bool lr_rand_ready;

static void lr_random_seed(uint32_t seed)
{
	uint32_t r[344];
	int i;

	/* glibc maps a zero seed to 1; the sequence is degenerate otherwise. */
	if (seed == 0)
		seed = 1;

	r[0] = seed;

	/* Fill the initial table with the Lehmer generator glibc uses,
	   r[i] = (16807 * r[i-1]) mod 2147483647, evaluated by Schrage's method
	   so that no intermediate leaves int32_t range. */
	for (i = 1; i < 31; i++) {
		int32_t prev = (int32_t)r[i - 1];
		int32_t hi = prev / 127773;
		int32_t lo = prev % 127773;
		int32_t word = 16807 * lo - 2836 * hi;

		if (word < 0)
			word += 2147483647;
		r[i] = (uint32_t)word;
	}

	for (i = 31; i < 34; i++)
		r[i] = r[i - 31];

	/* glibc discards the first 310 outputs to let the state mix. */
	for (i = 34; i < 344; i++)
		r[i] = r[i - 31] + r[i - 3];

	/* r[313..343] is 31 consecutive indices, so the residues mod 31 cover
	   0..30 exactly once and this populates the whole ring. */
	for (i = 313; i < 344; i++)
		lr_rand_state[i % 31] = r[i];

	lr_rand_idx = 344;
	lr_rand_ready = true;
}

static uint32_t lr_random_next(void)
{
	unsigned cur, back3;

	if (!lr_rand_ready)
		lr_random_seed(1);	/* glibc's unseeded default */

	cur = lr_rand_idx % 31;
	back3 = (lr_rand_idx + 28) % 31;	/* (idx - 3) mod 31 */

	/* Deliberate unsigned wraparound: this is the generator's arithmetic. */
	lr_rand_state[cur] += lr_rand_state[back3];
	lr_rand_idx++;

	return lr_rand_state[cur] >> 1;
}

uint32_t lr_random32(void)
{
	/* Same construction as the POSIX side, over the same sequence, so both
	   platforms produce the identical hash_index table. */
	uint32_t high = lr_random_next();
	uint32_t low = lr_random_next();

	return (high << 16) ^ low;
}

/* BCryptGenRandom with BCRYPT_USE_SYSTEM_PREFERRED_RNG is the documented CNG
   entropy source and needs no algorithm handle. Chosen over rand_s(), which
   would avoid the bcrypt import but is specified in terms of a C library
   function rather than as a CSPRNG, and over RtlGenRandom, which is not part
   of the documented API surface. bcrypt.dll ships with every supported
   Windows, so this stays a native build with no redistributable added. */
bool lr_secure_random(void *buf, size_t len)
{
	if (len > ULONG_MAX)
		return false;

	return BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)len,
			       BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}
