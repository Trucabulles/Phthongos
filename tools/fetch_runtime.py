"""Restore pinned Windows runtime resources from the official portable archive.

The archive is supplied by the developer; no downloaded executable is run.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def restore(archive, root=ROOT):
    lock = json.loads((root / 'runtime-lock.json').read_text(encoding='utf8'))
    if hashlib.sha256(archive.read_bytes()).hexdigest() != lock['sha256']:
        raise ValueError('Portable archive does not match runtime-lock.json')
    with zipfile.ZipFile(archive) as package:
        for name, item in lock['files'].items():
            relative = Path(name)
            if relative.is_absolute() or '..' in relative.parts or ':' in name or '\\' in name:
                raise ValueError('Unsafe runtime path: ' + name)
            content = package.read(lock['prefix'] + item['member'])
            if hashlib.sha256(content).hexdigest() != item['sha256']:
                raise ValueError('Runtime resource mismatch: ' + name)
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
    print('Verified and restored', len(lock['files']), 'runtime resources. No binary was executed.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    args = parser.parse_args()
    restore(args.archive.resolve())
