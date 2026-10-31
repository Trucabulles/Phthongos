# Building Phthongos on Windows

## Prerequisites

- Windows 10/11, x64.
- Visual Studio 2022 Community or Build Tools, with Desktop development with
  C++, MSVC v143 and a Windows SDK.
- CMake 3.24 or later, available on PATH.
- Python 3.10 or later for staging and validation scripts. The application has
  no Python runtime dependency. These scripts use the standard library.
- PowerShell 5.1 or later.
- Optional: Inno Setup 6.7 or compatible for building the installer.

The source tree includes JUCE 9.0.4, ONNX Runtime API headers, XZ/liblzma and the
MIDI tempo detector C++ port. Preserve their licenses. The local JUCE input-queue
guard is maintained in `cmake/JuceInputQueues.cmake`; do not remove it when
updating JUCE. Use the applicable JUCE license terms for your own builds.

## Runtime preparation

Large models, binaries, sound banks and source archives are provided in the
Windows portable release rather than committed as Git source files.
`runtime-lock.json` identifies the exact matching archive and its SHA-256.
Supply that archive after downloading the matching release:

```powershell
python tools/fetch_runtime.py --archive C:/Downloads/phthongos-0.1.0-windows-x64-portable.zip
```

The script checks the archive hash and every restored file before writing it.
It does not execute downloaded binaries. It restores runtime components,
notices, corresponding-source archives, sound assets and the two user manuals.
Do not substitute an arbitrary release or copy files out of an installed user
profile. MuScriptor model weights are optional and not required for building.

## Build

Run from the source root:

```powershell
cmake -S . -B native-build -G "Visual Studio 17 2022" -A x64
cmake --build native-build --config Release --target PhthongosJUCE --parallel 4
python -X utf8 package_juce.py --dev
./.run/Release/Phthongos.exe
```

The raw executable is written under
`native-build/PhthongosJUCE_artefacts/Release/`. Run the staged executable with
its complete resources rather than the raw executable alone.

The source-snapshot helper `tools/build.ps1` provides the equivalent actions
`compile`, `run`, `test`, `package` and `installer`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 -Action compile -NoPause
```

## Verify

```powershell
python -X utf8 tools/check_i18n.py
./.run/Release/Phthongos.exe --localisation-test
./.run/Release/Phthongos.exe --language fr --basic-pitch-test
./.run/Release/Phthongos.exe --language en --basic-pitch-test
```

Wait for each GUI process to exit before starting the next. Inspect
`.run/Release/test-results/` and require `passed: true`. Use the source helper's
`-Action test` to run these checks sequentially. Tests use generated audio and
isolated editor caches; they do not require personal recordings or projects.
The Basic Pitch checks cover decoding, cached adjustments and editor workflows.
Microphone, speaker and hardware-specific behaviour still need real-device
tests. Development test outputs must never enter a user package.

## Packages

Prepare separate clean folders for the two formats:

```powershell
python -X utf8 package_juce.py --portable
python -X utf8 package_juce.py
```

Each command prints its newly created folder under `releases/`. The first has
`portable.flag`; the second is the installer input. Do not reuse a folder after
launching the application, because it may then contain preferences or caches.

ZIP the entire clean portable folder. For the installer, pass the second folder
to Inno Setup, for example:

```powershell
& 'C:/Program Files (x86)/Inno Setup 6/ISCC.exe' '/DPackageDir=C:/build/Phthongos/releases/Phthongos-JUCE-<timestamp>' '/DOutputDir=C:/build/Phthongos/installers' installer/Phthongos.iss
```

The installer supports current-user installation, English/French setup,
language-matched license text and shortcut selection. Compilation and packaging
do not upload anything or change an existing release.

Only the application, required components, licensing material and user manuals
belong in user packages. The package builder verifies binary/source hashes and
produces a licensing audit under `build/legal-audio`, outside the package.

## Component sources

### Saved-project compatibility regression

On a staged runtime, `Phthongos.exe --project-compatibility-test C:/projects/saved.pht`
checks reopening a legacy analysis with a different runtime manifest, Save As,
embedded audio/maps, edit history, provenance and reopening the saved copy.
It also verifies rejection of corrupt checksums, unsupported versions and
inconsistent map dimensions. The input project is never saved over; temporary
copies and JSON reports are written under `test-results/` beside the test binary.
Use a project saved with a different runtime manifest for this regression.

`tools/prepare_audio_sources.py` can regenerate FluidSynth/libsndfile source
archives from the pinned official inputs in
`livraison/third-party/audio-source-lock.json`. It needs network access on the
first run and verifies SHA-256; later runs can use `build/legal-audio` offline.
The existing corresponding-source archives are restored by the runtime fetcher.

The MuScriptor native engine's source-reference archive records the upstream
muscriptor.cpp and ggml revisions, the separate C ABI bridge and a build outline.
Its optional GGUF weights have a separate license. This application build reuses
the verified native engine binary. Updating or rebuilding inference components
requires numerical and musical regression checks and new provenance records.

## Source layout

- `native/`: JUCE application, views, localization and workflow checks.
- `core/`: document, project, MIDI, audio and analysis services.
- `vendor/`: required source dependencies and ONNX API headers.
- `translations/`: embedded French/English catalogs.
- `models/basic-pitch/`: model provenance and license; weights restored separately.
- `assets/`: application branding.
- `livraison/`: runtime staging inputs and offline licensing material.
- `tools/`: build, runtime restoration and validation utilities.
- `installer/`: bilingual installer definition.

Personal data, migration-oracle archives, development history and machine-specific
paths are not required to build the published source snapshot.
