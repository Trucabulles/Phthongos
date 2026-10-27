# Phthongos

Phthongos is a native Windows application for audio-to-MIDI transcription,
listening, piano-roll editing and checking MIDI against the source audio.
Processing runs locally.

- Built-in Basic Pitch transcription with interactive decoding controls.
- Editable voices, MIDI input, a spectrogram and guided note proposals.
- Audio/MIDI comparison and dynamics estimation from audio.
- A bundled upright piano and an optional General MIDI sound bank.
- English and French interface and user manuals.
- Optional MuScriptor models, downloaded or selected separately under their own
  license conditions.

## Windows packages

Download the portable ZIP or installer from the [Windows release](https://github.com/Trucabulles/Phthongos/releases/tag/v0.1.0).

The portable ZIP and Windows installer contain the same application and runtime
components. Extract the portable archive to a writable folder before running
`Phthongos.exe`; do not run it from inside the ZIP. The portable marker keeps
preferences and optional downloads in the adjacent `data` folder. No existing
user data is included. A fresh profile defaults to English.

The installer offers English and French and presents the corresponding
application license. The interface language can be changed later in Settings.

The French and English user-manual PDFs are included in both packages and can
be opened from the application menu. Source manuals are
[English](MANUAL-PHTHONGOS-EN.md) and [French](MANUEL-PHTHONGOS.md).

## Development

See [BUILDING.md](BUILDING.md) for prerequisites, verified runtime preparation,
compilation, tests and package creation. The installed application does not
require Python, Node, Rust or a web browser runtime.

Windows x64 is the currently supported build target. No Linux or macOS package
is claimed by this source snapshot.

## License

Copyright © 2026 Trucabulles.

See [LICENSE](LICENSE) for the English translation of the Phthongos license and
[LICENSE_PHTHONGOS.md](LICENSE_PHTHONGOS.md) for the authoritative French text.
Use, study and modification are permitted under those terms; professional use
is permitted, while commercialization of the software or derivatives requires
permission. This is a custom source-available license.

Third-party libraries, models and sound banks retain their own licenses. Their
notices and source/provenance records accompany the distributions. Optional
MuScriptor weights are not included in the packages; their CC BY-NC 4.0 terms
are independent of the application license.

For contributions, see [CONTRIBUTING.md](CONTRIBUTING.md).
