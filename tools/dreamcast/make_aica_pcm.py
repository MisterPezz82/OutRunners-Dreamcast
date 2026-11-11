#!/usr/bin/env python3
"""Sample bank for the Dreamcast's AICA from the MultiPCM sample ROM (pcm.bin, 8-bit signed).

The fixed first MiB of the ROM (always visible to the chip) becomes AICA 4-bit ADPCM, one stream per
distinct sample start in the headers (ADPCM decodes only from a stream's start). The banked 512 KiB
pages stay 8-bit and are loaded into sound RAM when the game switches banks (not made here).

Encoder: closed-loop Yamaha/AICA ADPCM, decoding exactly as the AICA does (MAME aica.cpp: low nibble
first, diff = (2n+1)*step/8, step multipliers 230/307/409/512/614 over 256, step 127..24576, start
step 127, no high-pass).
Outputs: aica_fixed.bin (streams), aica_index.bin (little-endian u32 triples: chip start address,
byte offset in aica_fixed.bin, length in samples), and an SNR report.
Usage: make_aica_pcm.py pcm.bin out_dir
"""
import math, os, struct, subprocess, sys

ENCODER = os.environ.get('AICA_ENCODER')        # tools/dreamcast/aica_adpcm_encode.c, if built
DEPTH = os.environ.get('AICA_DEPTH', '3')

STEP_TABLE = (230, 230, 230, 230, 307, 409, 512, 614)

def step(nib, hist, size):
    delta = nib & 7
    diff = ((1 + 2 * delta) * size) >> 3
    diff = min(diff, 32767)
    hist = hist - diff if nib & 8 else hist + diff
    hist = max(-32768, min(32767, hist))
    size = max(127, min(24576, (STEP_TABLE[delta] * size) >> 8))
    return hist, size

def encode(samples):
    """samples: 16-bit ints. Returns (bytes, decoded list)."""
    hist, size = 0, 127
    out = bytearray((len(samples) + 1) // 2)
    dec = []
    for i, s in enumerate(samples):
        diff = s - hist
        mag = abs(diff)
        # the nibble whose reconstruction comes closest
        best, best_err = 0, None
        for n in range(8):
            d = min(((1 + 2 * n) * size) >> 3, 32767)
            err = abs(mag - d)
            if best_err is None or err < best_err:
                best, best_err = n, err
        nib = best | (8 if diff < 0 else 0)
        hist, size = step(nib, hist, size)
        dec.append(hist)
        if i & 1:
            out[i >> 1] |= nib << 4
        else:
            out[i >> 1] = nib
    return bytes(out), dec

def main():
    rom = open(sys.argv[1], 'rb').read()
    out_dir = sys.argv[2]
    starts = {}
    for i in range(512):
        h = rom[i * 12:i * 12 + 12]
        start = (h[0] << 16 | h[1] << 8 | h[2]) & 0x3fffff
        end = 0x10000 - (h[5] << 8 | h[6])
        if start == 0 and end == 0x10000:
            continue
        chip = start & 0x1fffff
        if chip < 0x100000:
            starts[chip] = max(starts.get(chip, 0), min(end, 0xffff))
    bank = bytearray()
    index = bytearray()
    sig = noise = 0.0
    for chip in sorted(starts):
        n = starts[chip]
        pcm = [struct.unpack('b', rom[(chip + k) & 0x3fffff:(chip + k) % len(rom) + 1][:1] or b'\0')[0] << 8 for k in range(n)]
        if ENCODER:
            r = subprocess.run([ENCODER, DEPTH], input=struct.pack('<%dh' % n, *pcm), capture_output=True, check=True)
            data = r.stdout
            s_, e_ = map(float, r.stderr.split())
            sig += s_
            noise += e_
        else:
            data, dec = encode(pcm)
            for a, b in zip(pcm, dec):
                sig += a * a
                noise += (a - b) ** 2
        while len(bank) % 4:
            bank.append(0)
        index += struct.pack('<III', chip, len(bank), n)
        bank += data
    open(f'{out_dir}/aica_fixed.bin', 'wb').write(bank)
    open(f'{out_dir}/aica_index.bin', 'wb').write(index)
    snr = 10 * math.log10(sig / noise) if noise else float('inf')
    print(f'{len(starts)} streams, {len(bank)} bytes ADPCM, SNR {snr:.1f} dB')

main()
