"""Prepare source and attribution material for the unmodified audio DLLs.

Inputs are pinned by SHA-256. No downloaded executable is run. The original
binary archives are used only to verify the distributed DLLs byte for byte.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / 'livraison/third-party/audio-source-lock.json'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def prepare(cache):
    lock = json.loads(LOCK.read_text(encoding='utf8'))
    inputs = {}
    for item in lock['inputs']:
        path = cache / item['name']
        if not path.is_file():
            path.parent.mkdir(parents=True, exist_ok=True)
            temporary = path.with_suffix(path.suffix + '.part')
            with urllib.request.urlopen(item['url'], timeout=120) as response:
                temporary.write_bytes(response.read())
            if digest(temporary.read_bytes()) != item['sha256']:
                temporary.unlink()
                raise ValueError('Download hash mismatch: ' + item['name'])
            temporary.replace(path)
        data = path.read_bytes()
        if digest(data) != item['sha256']:
            raise ValueError('Cached input hash mismatch: ' + item['name'])
        inputs[item['name']] = data

    for component in lock['components']:
        dll = ROOT / 'livraison/player-assets/fluidsynth' / component['dll']
        binary = dll.read_bytes()
        if digest(binary) != component['binarySha256']:
            raise ValueError('Unrecognized audio DLL: ' + component['dll'])
        with zipfile.ZipFile(io.BytesIO(inputs[component['binaryArchive']])) as upstream:
            matches = [n for n in upstream.namelist() if n.endswith('/' + component['dll'])]
            if len(matches) != 1 or upstream.read(matches[0]) != binary:
                raise ValueError('Official binary mismatch: ' + component['dll'])
        target = ROOT / 'livraison/third-party' / component['id']
        target.mkdir(parents=True, exist_ok=True)
        provenance = dict(component)
        provenance['inputs'] = [i for i in lock['inputs'] if i['name'] in component['sources']]
        provenance['applicationChanges'] = 'None. The official DLL is redistributed without modification.'
        provenance['buildEvidence'] = (
            'Original upstream workflow and dependency port recipes are supplied in the source archive. '
            'The historical vcpkg snapshot matches the codec versions and source hashes identified '
            'in the official DLL. It is reconstruction evidence, not an upstream-attested build lock '
            'or a claim of bit-for-bit reproducibility.')
        provenance_text = json.dumps(provenance, indent=2) + '\n'
        (target / 'provenance.json').write_text(provenance_text, encoding='utf8')
        with zipfile.ZipFile(target / 'corresponding-source.zip', 'w', zipfile.ZIP_DEFLATED) as z:
            for name in component['sources']:
                z.writestr(name, inputs[name])
            z.writestr('provenance.json', provenance_text)
            z.writestr('SOURCE.txt', (target / 'SOURCE.txt').read_bytes())
            z.writestr('prepare_audio_sources.py', Path(__file__).read_bytes())
            z.writestr('audio-source-lock.json', LOCK.read_bytes())
        if component['id'] == 'libsndfile':
            # Keep codec copyright texts directly accessible in the license UI.
            for name in component['sources']:
                if name.startswith('vcpkg-') or '.tar.' not in name:
                    continue
                with tarfile.open(fileobj=io.BytesIO(inputs[name])) as archive:
                    members = [m for m in archive.getmembers() if m.isfile()]
                    top = members[0].name.split('/')[0]
                    for member in members:
                        rel = member.name.removeprefix(top + '/')
                        if '/' not in rel and rel.upper().startswith(('COPYING', 'LICENSE', 'AUTHORS')):
                            content = archive.extractfile(member).read()
                            label = name.split('.tar')[0] + '-' + rel.replace('.', '-') + '.txt'
                            (target / label).write_bytes(content)
        print('Verified official DLL and prepared sources: ' + component['id'], flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cache', type=Path, default=ROOT / 'build/legal-audio')
    args = parser.parse_args()
    prepare(args.cache)


if __name__ == '__main__':
    main()
