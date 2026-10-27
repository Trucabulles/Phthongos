"""Create a fresh allowlisted source snapshot, without importing Git history."""
import argparse
import json
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]
ROOT_FILES = {
    'CMakeLists.txt', 'README.md', 'BUILDING.md', 'CONTRIBUTING.md', 'LICENSE',
    'LICENSE_PHTHONGOS.md', 'LICENSE_PHTHONGOS_EN.md', 'runtime-lock.json',
    'package_juce.py', 'MANUEL-PHTHONGOS.md', 'MANUAL-PHTHONGOS-EN.md', 'MANUAL-EN.md', 'MANUEL.txt',
}
TOOLS = {'check_i18n.py', 'legal_resources.py', 'audit_licenses.py',
         'prepare_audio_sources.py', 'fetch_runtime.py', 'export_public_sources.py'}
EXCLUDED_PARTS = {'.git', '.github', '__pycache__', 'node_modules', 'target', 'build', '.vs'}
EXCLUDED_EXT = {'.exe', '.dll', '.lib', '.pdb', '.obj', '.o', '.onnx', '.gguf',
                '.zip', '.gz', '.xz', '.bz2', '.sf2', '.wav', '.mp3', '.pht', '.log'}


def include(relative):
    if any(part in EXCLUDED_PARTS for part in relative.parts):
        return False
    if relative.name in ('AGENTS.md', 'FUNDING.yml') or relative.suffix.lower() in EXCLUDED_EXT:
        return False
    name = relative.as_posix()
    if name in ROOT_FILES:
        return True
    if relative.parts[0] in ('native', 'core', 'cmake', 'translations', 'assets'):
        return True
    if name.startswith('vendor/JUCE/'):
        rest = relative.parts[2:]
        return len(rest) == 1 or rest[0] == 'modules' or tuple(rest[:2]) == ('extras', 'Build')
    if relative.parts[0] == 'vendor':
        return relative.parts[1] in ('lzma-native', 'midi-bpm') or name.startswith('vendor/onnxruntime/include/') or relative.parent.as_posix() == 'vendor/onnxruntime'
    if name.startswith('models/basic-pitch/'):
        return True
    if name.startswith('documentation/credits/'):
        return relative.name in ('credits-fr.txt', 'credits-en.txt',
                                 'MuScriptor-CC-BY-NC-4.0.txt', 'MuScriptor-model-card.md')
    if name.startswith('livraison/third-party/'):
        return relative.parts[2] not in ('sources', 'rust', 'rustysynth', 'javascript', 'installer')
    if relative.parts[0] == 'tools':
        return relative.name in TOOLS
    return name in ('installer/Phthongos.iss', 'migration/data/TempoReference.h',
                    'migration/TempoPortChecks.cpp')


def export(destination, report):
    destination = destination.resolve()
    if destination == ROOT or ROOT.is_relative_to(destination):
        raise ValueError('Use a separate destination, not a project ancestor.')
    if destination.exists() and any(destination.iterdir()):
        raise ValueError('Destination must be new or empty; no existing checkout is overwritten.')
    destination.mkdir(parents=True, exist_ok=True)
    names = []
    for start in sorted(ROOT_FILES):
        if not (ROOT / start).is_file():
            raise ValueError('Missing publication input: ' + start)
    candidates = [ROOT / name for name in ROOT_FILES]
    for folder in ('native', 'core', 'cmake', 'translations', 'assets', 'vendor', 'models/basic-pitch', 'livraison/third-party', 'tools', 'installer', 'migration/data', 'documentation/credits'):
        candidates.extend(p for p in (ROOT / folder).rglob('*') if p.is_file())
    candidates.append(ROOT / 'migration/TempoPortChecks.cpp')
    for source in sorted(set(candidates)):
        relative = source.relative_to(ROOT)
        if not include(relative):
            continue
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        names.append(relative.as_posix())
    helper = ROOT / 'tools/build_public.ps1'
    shutil.copy2(helper if helper.is_file() else ROOT / 'tools/build.ps1', destination / 'tools/build.ps1')
    names.append('tools/build.ps1')
    # Private migration-oracle tooling is not part of the maintained source build.
    cmake = destination / 'CMakeLists.txt'
    text = cmake.read_text(encoding='utf8')
    text = re.sub(r'option\(PHT_BUILD_MIGRATION_TESTS.*?endif\(\)\s*', '', text, flags=re.S)
    cmake.write_text(text, encoding='utf8')
    (destination / '.gitignore').write_text(
        '/native-build/\n/build/\n/releases/\n/installers/\n/.run/\n/data/\n'
        '**/__pycache__/\n*.pyc\n*.pht\n*.mid\n*.gguf\n*.onnx\n*.dll\n*.exe\n'
        '*.pdb\n*.lib\n*.sf2\n*.zip\n/livraison/player-assets/\n/livraison/runtime/\n'
        '/documentation/manuel/\n/.github/FUNDING.yml\n', encoding='utf8')
    (destination / '.gitattributes').write_text('* -text\n', encoding='utf8')
    names.extend(['.gitignore', '.gitattributes'])
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(sorted(names), indent=2) + '\n', encoding='utf8')
    print('Exported', len(names), 'source, legal and documentation files to', destination)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--manifest', required=True, type=Path)
    args = parser.parse_args()
    export(args.destination, args.manifest)
