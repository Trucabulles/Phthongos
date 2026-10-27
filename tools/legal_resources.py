"""Prepare and verify the licensing inputs used by the native package.

This utility never changes runtime binaries or musical resources.
"""
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
NOTICES = ROOT / 'livraison/third-party'


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare():
    for name in ('LICENSE', 'LICENSE_PHTHONGOS.md', 'LICENSE_PHTHONGOS_EN.md'):
        text = (ROOT / name).read_text(encoding='utf8')
        if 'Trucabulles' not in text or 'Phthongos' not in text:
            raise ValueError('Unexpected application license: ' + name)

    # Check the binary-specific audio receipts prepared from pinned inputs.
    lock = json.loads((NOTICES / 'audio-source-lock.json').read_text())
    for component in lock['components']:
        binary = ROOT / 'livraison/player-assets/fluidsynth' / component['dll']
        if sha256(binary) != component['binarySha256']:
            raise ValueError('Audio DLL changed: ' + component['dll'])
        folder = NOTICES / component['id']
        receipt = json.loads((folder / 'provenance.json').read_text())
        if receipt['binarySha256'] != component['binarySha256']:
            raise ValueError('Audio provenance mismatch: ' + component['id'])
        import zipfile
        with zipfile.ZipFile(folder / 'corresponding-source.zip') as archive:
            if archive.testzip() is not None:
                raise ValueError('Damaged source archive: ' + component['id'])
            for item in receipt['inputs']:
                if hashlib.sha256(archive.read(item['name'])).hexdigest() != item['sha256']:
                    raise ValueError('Source mismatch: ' + item['name'])

    receipt = json.loads((NOTICES / 'muscriptor/provenance.json').read_text())
    if sha256(ROOT / 'livraison' / receipt['binary']) != receipt['sha256']:
        raise ValueError('MuScriptor binary changed: provenance must be reviewed')
    if sha256(NOTICES / 'muscriptor/source-reference.zip') != receipt['sourceArchiveSha256']:
        raise ValueError('MuScriptor source-reference archive changed')

    # Keep the original JUCE inventory and upstream license files. This inventory
    # covers vendored dependencies; it does not assert that every module is linked.
    juce = ROOT / 'vendor/JUCE'
    target = NOTICES / 'juce'
    target.mkdir(exist_ok=True)
    for name in ('LICENSE.md', 'JUCE.spdx.json'):
        shutil.copy2(juce / name, target / name)
    modules = ('juce_core', 'juce_graphics', 'juce_audio_formats', 'juce_audio_devices')
    entries = []
    for module in modules:
        for file in sorted((juce / 'modules' / module).rglob('*')):
            if not file.is_file():
                continue
            if any(token in file.name.lower() for token in ('license', 'copying', 'notice', 'copyright')):
                relative = file.relative_to(juce).as_posix()
                destination = target / 'upstream-notices' / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(file, destination)
                entries.append(relative)
    # Some bundled libraries keep their notice in a source header or README.
    for relative in (
        'modules/juce_audio_formats/codecs/flac/libFLAC/include/private/bitmath.h',
        'modules/juce_graphics/image_formats/jpglib/README',
    ):
        file = juce / relative
        if file.is_file():
            destination = target / 'upstream-notices' / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, destination)
            entries.append(relative)
    combined = ['JUCE 9.0.4 — vendored dependency notices\n',
                'Original texts below are preserved without relicensing. The SPDX file\n'
                'inventories upstream source dependencies, including platform-specific\n'
                'or disabled components; it is not a list of all linked modules.\n']
    for relative in entries:
        combined.extend(['\n' + '=' * 72 + '\n' + relative + '\n\n',
                         (juce / relative).read_text(encoding='utf8', errors='replace')])
    (target / 'THIRD-PARTY.txt').write_text(''.join(combined), encoding='utf8')
    (target / 'SOURCE.txt').write_text(
        'JUCE 9.0.4. Vendored source: vendor/JUCE in this project.\n'
        'Original license and upstream SPDX inventory are retained. Phthongos\n'
        'uses JUCE under its JUCE license terms, not by relicensing its modules\n'
        'under the Phthongos application license.\n', encoding='utf8')

    return {
        'schemaVersion': 1,
        'target': 'windows-x64',
        'application': {'name': 'Phthongos', 'rightsHolder': 'Trucabulles',
                        'license': 'LicenseRef-Phthongos',
                        'notices': ['phthongos/LICENSE_PHTHONGOS_EN.md',
                                    'phthongos/LICENSE_PHTHONGOS.md']},
        'bundled': [
            {'name': 'JUCE', 'version': '9.0.4', 'license': 'JUCE license terms',
             'notices': ['juce/LICENSE.md', 'juce/THIRD-PARTY.txt', 'juce/JUCE.spdx.json']},
            {'name': 'FluidSynth', 'version': '2.6.1', 'license': 'LGPL-2.1-or-later',
             'binary': 'player-assets/fluidsynth/libfluidsynth-3.dll',
             'sha256': lock['components'][0]['binarySha256'],
             'notices': ['fluidsynth/LICENSE.txt', 'fluidsynth/provenance.json'],
             'source': 'fluidsynth/corresponding-source.zip', 'linkage': 'replaceable shared library'},
            {'name': 'libsndfile and its bundled codecs', 'version': '1.2.2',
             'license': 'LGPL-2.1-or-later; codec licenses retained individually',
             'binary': 'player-assets/fluidsynth/sndfile.dll',
             'sha256': lock['components'][1]['binarySha256'],
             'notices': ['libsndfile/LICENSE.txt', 'native-codecs/NOTICE.txt', 'libsndfile/provenance.json'],
             'source': 'libsndfile/corresponding-source.zip', 'linkage': 'replaceable shared library'},
            {'name': 'muscriptor.cpp, ggml and PFFFT', 'license': 'MIT; PFFFT BSD-style',
             'binary': receipt['binary'], 'sha256': receipt['sha256'],
             'notices': ['muscriptor/THIRD-PARTY.txt', 'muscriptor/provenance.json'],
             'source': 'muscriptor/source-reference.zip'},
            {'name': 'SpeexDSP resampler', 'version': '1.2.1', 'license': 'BSD-3-Clause',
             'binary': 'runtime/research_speex.dll',
             'sha256': json.loads((NOTICES / 'speexdsp/provenance.json').read_text(encoding='utf-8-sig'))['dll_sha256'],
             'notices': ['speexdsp/NOTICE.txt', 'speexdsp/provenance.json']},
            {'name': 'XZ / liblzma', 'version': '5.2 source family',
             'license': 'upstream liblzma public-domain dedication and retained file-specific terms',
             'notices': ['lzma-native/xz-5.2/COPYING', 'lzma-native/SOURCE.txt']},
            {'name': 'MIDI BPM Detection — C++ port', 'license': 'MIT',
             'revision': 'e14a86c1c15a3523c458c19549ce7be3f9576734',
             'notices': ['midi-bpm/LICENSE.txt', 'midi-bpm/SOURCE.md']},
            {'name': 'Inferno, Magma and Viridis', 'license': 'CC0-1.0',
             'notices': ['colormaps/NOTICE.txt', 'colormaps/LICENSE.txt']},
            {'name': 'Upright Piano KW', 'version': 'compact SF2 2019-07-03', 'license': 'CC0-1.0',
             'binary': 'player-assets/piano.sf2',
             'sha256': 'cf2a98eb38a32c4954b4b6e2caae4112d62dd8e892eceefdd7942b0e7d01ac2f',
             'notices': ['piano/NOTICE.md', 'piano/CC0.txt']},
            {'name': 'Microsoft Visual C++ redistributable runtime', 'license': 'Microsoft redistribution terms',
             'notices': ['microsoft/NOTICE.md']},
        ],
        'optional_models': {'bundled': False, 'name': 'MuScriptor Small / Medium / Large',
                            'license': 'CC-BY-NC-4.0',
                            'source': 'https://huggingface.co/DamRsn/muscriptor-gguf',
                            'revision': 'd7045f94e8b19427f4ff9542975035e66596e51c',
                            'notices': ['optional-models/MuScriptor-CC-BY-NC-4.0.txt',
                                        'optional-models/MuScriptor-model-card.md']},
        'optional_soundbank': {'bundled': False, 'name': 'MuseScore General HQ 0.2.1',
                               'notices': ['optional-msghq/LICENSE.txt', 'optional-msghq/Sample_Sources.csv']},
        'excluded_runtime_components': ['Rust', 'CPAL', 'RustySynth', 'Tauri', 'React', 'WebView2'],
    }
