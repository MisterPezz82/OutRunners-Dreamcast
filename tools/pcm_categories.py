"""
Name:        pcm_categories.py
Description: writes console/pcm_categories.c, the MultiPCM's samples by what they are, for the console's SOUND
             settings (issue 57: MUSIC, EFFECTS and VOICE apart), from pcm_samples.txt
Version:     0.1.0
Created:     25-11-2026
Modified:    25-11-2026
Author:      Sebastian Toyos

Usage: python tools/pcm_categories.py [PCM_SAMPLES [OUTPUT]]
       (PCM_SAMPLES: pcm_samples.txt at the repository's root if none; OUTPUT: console/pcm_categories.c if none)
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SAMPLES = ROOT / "pcm_samples.txt"
DEFAULT_OUTPUT = ROOT / "console" / "pcm_categories.c"

CATEGORIES = {
    "drums": "PCM_MUSIC",
    "instruments": "PCM_MUSIC",
    "speech": "PCM_VOICE",
    "effects": "PCM_EFFECTS",
    "scenery": "PCM_EFFECTS",
    "tyres": "PCM_EFFECTS",
    "engine": "PCM_EFFECTS",
}


def read_samples(path):
    samples = []
    with open(path, encoding="utf-8") as file:
        for line in file:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            if line.startswith("page ") or line.startswith("uses "):
                continue
            parts = line.split()
            if len(parts) < 3:
                continue
            offset = int(parts[0], 16)
            length = int(parts[1], 16)
            name = parts[2]
            samples.append((offset, length, name))
    return samples


def category_for(name):
    folder = name.split("/")[0]
    if folder not in CATEGORIES:
        sys.exit(f"unknown sample folder: {folder}")
    return CATEGORIES[folder]


def write_output(path, samples):
    samples.sort(key=lambda s: s[0])
    counts = {}
    entries = []
    for offset, length, name in samples:
        cat = category_for(name)
        counts[cat] = counts.get(cat, 0) + 1
        entries.append(f"\t{{ 0x{offset:06X}u, 0x{length:05X}u, {cat} }},\t/* {name} */")

    lines = [
        "/**",
        " *    Name:        pcm_categories.c",
        " *    Description: the MultiPCM's samples by what they are, for the console's SOUND settings",
        " *                 (issue 57: MUSIC, EFFECTS and VOICE apart)",
        " *    Version:     0.1.0",
        " *    Created:     25-11-2026",
        " *    Modified:    25-11-2026",
        " *    Author:      Sebastian Toyos",
        " *",
        " *    made by tools/pcm_categories.py from pcm_samples.txt; do not edit it by hand",
        " */",
        "",
        '#include "console_internal.h"',
        "",
        "const console_pcm_sample console_pcm_samples[] = {",
    ]
    lines.extend(entries)
    lines.append("};")
    lines.append("const size_t console_pcm_sample_count = sizeof(console_pcm_samples) / sizeof(console_pcm_samples[0]);")
    lines.append("")

    with open(path, "w", encoding="utf-8", newline="\n") as file:
        file.write("\n".join(lines))

    return counts


def main():
    samples_path = DEFAULT_SAMPLES
    output_path = DEFAULT_OUTPUT
    args = sys.argv[1:]
    if len(args) >= 1:
        samples_path = Path(args[0])
    if len(args) >= 2:
        output_path = Path(args[1])

    samples = read_samples(samples_path)
    counts = write_output(output_path, samples)

    print(f"pcm_categories.c: {len(samples)} samples")
    for cat in ("PCM_MUSIC", "PCM_EFFECTS", "PCM_VOICE"):
        print(f"\t{cat}: {counts.get(cat, 0)}")


if __name__ == "__main__":
    main()
