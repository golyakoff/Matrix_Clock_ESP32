#!/usr/bin/env python3
"""Generate manifest.json for the firmware update from the internet (see main/include/updater.h).

Usage:
  tools/make_manifest.py --version v2.0.0 \\
      --base-url https://github.com/golyakoff/Matrix_Clock_ESP32/releases/download/v2.0.0 \\
      release_4mb=release/tetris-clock_v2.0.0_release_4mb_fw.bin \\
      release_16mb=release/tetris-clock_v2.0.0_release_16mb_fw.bin \\
      > manifest.json

Each build is VARIANT=FILE; the URL is BASE_URL/<file name>. VARIANT is CONFIG_MC_BUILD_VARIANT
(variants/<VARIANT>.defaults): the clock looks for its own build in the manifest.
Ported from FireLamp_ESP32 (tools/make_manifest.py).
"""
import argparse
import hashlib
import json
import os
import re
import sys


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--version', required=True, help='firmware version, vMAJOR.MINOR.PATCH')
    p.add_argument('--base-url', required=True, help='https URL of the release assets')
    p.add_argument('builds', nargs='+', help='VARIANT=FILE')
    args = p.parse_args()

    if not re.fullmatch(r'v\d+\.\d+(\.\d+)?', args.version):
        p.error('version must be vMAJOR.MINOR.PATCH')
    if not args.base_url.startswith('https://'):
        p.error('base URL must be https')

    builds = {}
    for b in args.builds:
        m = re.fullmatch(r'([a-z0-9_]+)=(.+)', b)
        if not m:
            p.error(f'invalid build "{b}", expected VARIANT=FILE')
        variant, path = m.groups()
        with open(path, 'rb') as f:
            data = f.read()
        builds[variant] = {
            'url': args.base_url.rstrip('/') + '/' + os.path.basename(path),
            'size': len(data),
            'sha256': hashlib.sha256(data).hexdigest(),
        }

    json.dump({'version': args.version, 'builds': builds}, sys.stdout, indent=2)
    sys.stdout.write('\n')


if __name__ == '__main__':
    main()
