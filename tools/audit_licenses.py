"""Validate native release licensing material and report binary imports offline."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile


def digest(file):
    return hashlib.sha256(file.read_bytes()).hexdigest()


def imports(file):
    """Read the normal and delay-import directories of a PE image without loading it."""
    data = file.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Not a PE image: ' + file.name)
    sections, size = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    opt = pe + 24
    plus = struct.unpack_from('<H', data, opt)[0] == 0x20b
    directories = opt + (112 if plus else 96)
    imagebase = struct.unpack_from('<Q' if plus else '<I', data, opt + (24 if plus else 28))[0]
    table = opt + size

    def offset(rva):
        for i in range(sections):
            at = table + 40 * i
            virtualsize, virtual, rawsize, raw = struct.unpack_from('<IIII', data, at + 8)
            if virtual <= rva < virtual + max(virtualsize, rawsize):
                return raw + rva - virtual
        raise ValueError('Invalid PE RVA: ' + file.name)

    result = []
    for index, stride, nameoff in ((1, 20, 12), (13, 32, 4)):
        rva, length = struct.unpack_from('<II', data, directories + 8 * index)
        if not rva:
            continue
        at = offset(rva)
        for i in range(min(length // stride + 1, 4096)):
            entry = at + i * stride
            if not any(data[entry:entry + stride]):
                break
            name = struct.unpack_from('<I', data, entry + nameoff)[0]
            if index == 13 and not (struct.unpack_from('<I', data, entry)[0] & 1):
                name -= imagebase
            start = offset(name)
            result.append(data[start:data.index(b'\0', start)].decode('ascii'))
    return sorted(set(result), key=str.lower)


def audit(root):
    checks = []

    def check(condition, description):
        checks.append({'check': description, 'passed': bool(condition)})

    def exists(relative):
        check((root / relative).is_file(), 'Present: ' + relative)

    for name in ('LICENSE', 'LICENSE_PHTHONGOS_EN.md', 'LICENSE_PHTHONGOS.md'):
        exists(name)
        if (root / name).is_file():
            text = (root / name).read_text(encoding='utf8')
            check('Trucabulles' in text and 'KITHÁRA NÉA' not in text, 'Application holder and name: ' + name)
    check((root / 'LICENSE').read_bytes() == (root / 'LICENSE_PHTHONGOS_EN.md').read_bytes(),
          'English license is the default LICENSE')
    third = root / 'third-party'
    manifest = json.loads((third / 'components.json').read_text(encoding='utf8'))
    for item in manifest['bundled'] + [manifest['application'], manifest['optional_models'], manifest['optional_soundbank']]:
        for notice in item.get('notices', []):
            exists('third-party/' + notice)
        if 'binary' in item:
            file = root / item['binary']
            check(file.is_file() and digest(file) == item['sha256'], 'Binary matches provenance: ' + item['name'])
        if item.get('source', '').endswith('.zip'):
            exists('third-party/' + item['source'])
    for component in ('fluidsynth', 'libsndfile'):
        folder = third / component
        receipt = json.loads((folder / 'provenance.json').read_text())
        with zipfile.ZipFile(folder / 'corresponding-source.zip') as z:
            check(z.testzip() is None, 'Source archive CRC: ' + component)
            for item in receipt['inputs']:
                check(hashlib.sha256(z.read(item['name'])).hexdigest() == item['sha256'],
                      'Pinned corresponding source: ' + item['name'])
    receipt = json.loads((third / 'muscriptor/provenance.json').read_text())
    check(digest(third / 'muscriptor/source-reference.zip') == receipt['sourceArchiveSha256'],
          'MuScriptor source archive matches provenance')
    for model in ('basic_pitch', 'onnxruntime'):
        check(model in manifest, 'Pinned provenance: ' + model)
    ort = manifest['onnxruntime']
    for name, expected in ort['runtime_files'].items():
        check(digest(root / 'runtime' / name) == expected, 'ONNX Runtime binary hash: ' + name)
    check(not manifest['optional_models']['bundled'], 'MuScriptor weights declared optional')
    check(not any(root.rglob('*.gguf')), 'No optional GGUF weights shipped')
    check(not any(root.rglob('MuseScore_General_HQ.sf2')), 'No optional HQ bank shipped')
    for name in ('rust', 'rustysynth', 'javascript', 'installer'):
        check(not (third / name).exists(), 'No obsolete attribution directory: ' + name)
    binaries = {}
    localnames = {p.name.lower() for p in root.rglob('*') if p.suffix.lower() in ('.dll', '.exe')}
    for file in sorted(root.rglob('*')):
        if file.suffix.lower() in ('.dll', '.exe'):
            dependencies = imports(file)
            binaries[file.relative_to(root).as_posix()] = {'sha256': digest(file), 'imports': dependencies}
            for name in dependencies:
                if name.lower().startswith(('msvcp', 'vcruntime')) or name.lower() in ('sndfile.dll', 'libfluidsynth-3.dll'):
                    check(name.lower() in localnames, 'Bundled non-system import: ' + name)
    return {'passed': all(c['passed'] for c in checks), 'checks': checks, 'binaries': binaries,
            'scope': 'Offline package evidence and attribution checks; no runtime binary was loaded. '
                     'Platform drivers/system DLLs are supplied by Windows. '
                     'Historical build reproducibility limits remain documented in provenance.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    report = audit(args.package)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n', encoding='utf8')
    print(f"Licensing checks: {sum(c['passed'] for c in report['checks'])}/{len(report['checks'])}; passed={report['passed']}")
    for item in report['checks']:
        if not item['passed']:
            print('FAILED: ' + item['check'])
    raise SystemExit(0 if report['passed'] else 1)
