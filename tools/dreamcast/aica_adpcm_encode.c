/* SPDX-License-Identifier: BSD-3-Clause */
/* AICA ADPCM encoder with look-ahead: reads 16-bit little-endian samples on stdin, writes packed
   nibbles (low nibble first) on stdout, and the decoded samples' squared error to stderr. Decoding
   follows MAME aica.cpp (see tools/make_aica_pcm.py). Usage: aica_adpcm_encode DEPTH < in > out */
#include <stdint.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include <stdio.h>
#include <stdlib.h>

static const int step_table[8] = { 230, 230, 230, 230, 307, 409, 512, 614 };

static void dec(int nib, int *hist, int *size)
{
	const int delta = nib & 7;
	int diff = ((1 + 2 * delta) * *size) >> 3;
	if (diff > 32767) diff = 32767;
	int h = (nib & 8) ? *hist - diff : *hist + diff;
	*hist = h < -32768 ? -32768 : (h > 32767 ? 32767 : h);
	int s = (step_table[delta] * *size) >> 8;
	*size = s < 127 ? 127 : (s > 24576 ? 24576 : s);
}

static const int16_t *in;
static long count;

/* the least squared error over the next 'depth' samples from state (hist, size) at sample i */
static double search(long i, int depth, int hist, int size, int *first)
{
	double best = 1e300;
	for (int nib = 0; nib < 16; nib++)
	{
		int h = hist, s = size;
		dec(nib, &h, &s);
		const double e = (double)(in[i] - h) * (in[i] - h);
		if (e > best) continue;
		double total = e;
		if (depth > 1 && i + 1 < count)
		{
			int dummy;
			total += search(i + 1, depth - 1, h, s, &dummy);
		}
		if (total < best) { best = total; *first = nib; }
	}
	return best;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
	_setmode(_fileno(stdin), _O_BINARY);    /* (samples in, nibbles out: bytes, not text) */
	_setmode(_fileno(stdout), _O_BINARY);
#endif
	const int depth = argc > 1 ? atoi(argv[1]) : 2;
	static int16_t buf[1 << 17];
	count = (long)fread(buf, 2, sizeof(buf) / 2, stdin);
	in = buf;
	int hist = 0, size = 127, pending = -1;
	double err = 0, sig = 0;
	for (long i = 0; i < count; i++)
	{
		int nib = 0;
		search(i, depth, hist, size, &nib);
		dec(nib, &hist, &size);
		err += (double)(in[i] - hist) * (in[i] - hist);
		sig += (double)in[i] * in[i];
		if (pending < 0) pending = nib;
		else { putchar(pending | nib << 4); pending = -1; }
	}
	if (pending >= 0) putchar(pending);
	fprintf(stderr, "%.0f %.0f\n", sig, err);
	return 0;
}
