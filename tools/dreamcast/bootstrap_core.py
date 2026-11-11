#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Fetch pinned dependency. Manager-reviewed setup utility."""
import io
import json
from pathlib import Path
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / 'tools/core'
MARKER = TARGET / '.dreamcast-bootstrap-commit'
PIN = '1a610f5a041aca68760be04872219dcb712f1eec'

def fetch(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'OutRunners-Dreamcast'})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()

def main():
    tree = json.loads(fetch(f'https://api.github.com/repos/cheviman/outrunners-port/git/trees/{PIN}?recursive=1'))
    sha = next(item['sha'] for item in tree['tree'] if item['path'] == 'tools/core' and item['type'] == 'commit')
    print('Core:', sha)
    if TARGET.exists():
        if MARKER.is_file() and MARKER.read_text().strip() == sha:
            print('Already installed')
            return
        raise RuntimeError('Existing tools/core has no matching marker; refusing overwrite')
    with zipfile.ZipFile(io.BytesIO(fetch(f'https://codeload.github.com/cheviman/arcade-decomp/zip/{sha}'))) as archive:
        prefix = archive.infolist()[0].filename.split('/')[0] + '/'
        planned = []
        for info in archive.infolist():
            if info.is_dir():
                continue
            if not info.filename.startswith(prefix) or (info.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('Unexpected archive entry')
            path = TARGET / info.filename[len(prefix):]
            if not path.resolve().is_relative_to(TARGET.resolve()) or path.exists():
                raise ValueError('Unsafe or existing extraction target')
            planned.append((info, path))
        for info, path in planned:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(archive.read(info))
    MARKER.write_text(sha + '\n', encoding='utf-8')

if __name__ == '__main__':
    main()
