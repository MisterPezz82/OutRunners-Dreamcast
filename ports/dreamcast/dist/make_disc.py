#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""OutRunners for Dreamcast: builds the disc images from YOUR OWN OutRunners ROM set (MAME "orunners",
the World set). Nothing of the game's ROM comes with this package: the graphics, sound and program
tables are taken from your set here, on your machine.

Windows: drop orunners.zip onto make_disc.bat (it uses the Python and tools in tools/).
Linux/WSL: ./make_disc.sh /path/to/orunners.zip  (needs python3, a C compiler and mkdcdisc).
Usage: make_disc.py ROM_ZIP [OUTPUT_FOLDER]
Makes OUTPUT/OutRunners.cdi."""
import os, shutil, subprocess, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE / 'source'
WINDOWS = os.name == 'nt'
DISC_FILES = ('core.assets', 'tiles4.z', 'sprites.z', 'pcm.bin', 'aica_fixed.bin', 'aica_index.bin')
VERSION = '0.1'
# the loading screen's two pictures, as the game itself draws them from your set: the title screen, and
# one of its cars (the frames, and where on the screen the car is, for MAME's World set)
SPLASH = {'SPLASH_BG': '3840', 'SPLASH_CAR': '3920', 'SPLASH_RECT': '150,140,275,182'}


def step(text):
    print(f'\n== {text}', flush=True)


def run(args, **kw):
    print('   ' + ' '.join(str(a) for a in args), flush=True)
    result = subprocess.run([str(a) for a in args], **kw)
    if result.returncode != 0:
        raise SystemExit(f'\nFAILED (exit {result.returncode}): {Path(str(args[0])).name}')


def python_script(script, *args, env=None, cwd=None):
    run([sys.executable, script, *args], env=env, cwd=cwd)


def tools(work):
    """asset_probe, the ADPCM encoder and mkdcdisc: bundled on Windows, built (or found) elsewhere"""
    if WINDOWS:
        win = HERE / 'tools' / 'windows'
        return win / 'asset_probe.exe', win / 'aica_adpcm_encode.exe', win / 'mkdcdisc.exe', win / 'splash_frames.exe'
    for tool in ('cc', 'mkdcdisc') if not os.environ.get('MKDCDISC') else ('cc',):
        if not shutil.which(tool):
            raise SystemExit(f'missing: {tool}' + (' (or set MKDCDISC=/path/to/mkdcdisc)' if tool == 'mkdcdisc' else ''))
    out = subprocess.run(['bash', str(SRC / 'tools/dreamcast/build_probe.sh'), 'asset-probe'], capture_output=True, text=True)
    if out.returncode != 0:
        print(out.stdout, out.stderr)
        raise SystemExit('FAILED: building asset_probe')
    probe = SRC / out.stdout.strip().splitlines()[-1]
    encoder = work / 'aica_adpcm_encode'
    run(['cc', '-O2', '-o', encoder, SRC / 'tools/dreamcast/aica_adpcm_encode.c', '-lm'])
    out = subprocess.run(['bash', str(SRC / 'tools/dreamcast/build_probe.sh'), 'splash-frames'], capture_output=True, text=True)
    splash = SRC / out.stdout.strip().splitlines()[-1] if out.returncode == 0 and out.stdout.strip() else None
    return probe, encoder, Path(os.environ.get('MKDCDISC') or shutil.which('mkdcdisc')), splash


def loading_screen(splash_tool, pack, work):
    """assets/splash.bin: the loading screen, drawn by the game from your set. Without it (the tool
    missing, or a set whose title screen is not where the World set's is) the disc shows plain credits."""
    try:
        folder = work / 'splash'
        folder.mkdir()
        env = dict(os.environ, SPLASH_DIR=str(folder), **SPLASH)
        frames = int(SPLASH['SPLASH_CAR']) + 5
        print(f'   {Path(str(splash_tool)).name}: the game runs to its title screen', flush=True)
        subprocess.run([str(splash_tool), '--frames', str(frames), '--screens', '1', '--video-hashes', str(folder / 'hashes.txt'),
                        '--assets', str(pack)], env=env, cwd=folder, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
        result = subprocess.run([sys.executable, str(SRC / 'tools/dreamcast/make_splash.py'), str(folder / 'bg.bmp'),
                                 str(folder / 'car.bmp'), str(work / 'split' / 'splash.bin'), VERSION], capture_output=True, text=True)
        if result.returncode == 0 and (work / 'split' / 'splash.bin').is_file():
            return True
        print('   ' + (result.stdout + result.stderr).strip()[-300:])
    except Exception as error:    # (the disc works without it)
        print(f'   {error}')
    print('   no loading screen picture: the disc will show plain credits')
    return False


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    rom = Path(sys.argv[1].strip().strip('"')).resolve()
    out = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else HERE / 'output'
    if not rom.is_file():
        raise SystemExit(f'not found: {rom}')
    start = time.time()
    work = out / 'work'
    for p in (work, out / 'assets', out / 'gdi'):
        shutil.rmtree(p, ignore_errors=True)
    (out / 'OutRunners.cdi').unlink(missing_ok=True)
    work.mkdir(parents=True)
    (out / 'assets').mkdir()
    probe, encoder, mkdcdisc, splash_tool = tools(work)

    step('1/6 checking and extracting your ROM set')
    (SRC / 'rom').mkdir(exist_ok=True)
    shutil.copy2(rom, SRC / 'rom' / 'orunners.zip')
    python_script(SRC / 'extract_assets.py', '--force', 'rom/orunners.zip', cwd=SRC)

    step('2/6 packing the game\'s data')
    run([probe, SRC / 'assets', work / 'outrunners.assets'], stdout=subprocess.DEVNULL)

    step('3/6 splitting the pack into the disc\'s files')
    python_script(SRC / 'tools/dreamcast/split_pack.py', work / 'outrunners.assets', work / 'split')
    python_script(SRC / 'tools/dreamcast/pack_tiles4.py', work / 'split/tiles.bin', work / 'split/tiles4.bin')
    # (the graphics stay whole in the console's RAM, compressed: no disc reads while playing)
    python_script(SRC / 'tools/dreamcast/pack_compressed.py', work / 'split/tiles4.bin', work / 'split/tiles4.z', '4096')
    python_script(SRC / 'tools/dreamcast/pack_compressed.py', work / 'split/sprites.bin', work / 'split/sprites.z', '8192')

    step('4/6 converting the samples for the Dreamcast\'s sound chip')
    env = dict(os.environ, AICA_ENCODER=str(encoder), AICA_DEPTH='4')
    python_script(SRC / 'tools/dreamcast/make_aica_pcm.py', work / 'split/pcm.bin', work / 'split', env=env)

    step('5/6 the loading screen')
    have_splash = bool(splash_tool) and loading_screen(splash_tool, work / 'outrunners.assets', work)

    disc = work / 'disc' / 'assets'
    disc.mkdir(parents=True)
    for f in DISC_FILES + (('splash.bin',) if have_splash else ()):
        shutil.copy2(work / 'split' / f, out / 'assets' / f)
        shutil.copy2(work / 'split' / f, disc / f)
    common = ['-e', HERE / 'bin' / 'outrunners.elf', '-D', work / 'disc', '-n', 'OutRunners', '-a', 'Homebrew',
              '-s', 'ORDC00001', '-P', 'V0.100', '-H', 'E000F10', '-m', '--temp-dir', work]

    step('6/6 CDI image')
    run([mkdcdisc, *common, '-N', '-o', out / 'OutRunners.cdi'])

    for p in (work, SRC / 'rom', SRC / 'assets', SRC / 'build'):
        shutil.rmtree(p, ignore_errors=True)
    print(f'\nDone in {time.time() - start:.0f} s:\n   {out / "OutRunners.cdi"}')
    print('These images hold data from your ROM set: they are for your own use, not for sharing.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
