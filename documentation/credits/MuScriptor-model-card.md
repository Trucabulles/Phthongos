---
license: cc-by-nc-4.0
license_link: https://creativecommons.org/licenses/by-nc/4.0/legalcode.en
base_model:
  - MuScriptor/muscriptor-small
  - MuScriptor/muscriptor-medium
  - MuScriptor/muscriptor-large
base_model_relation: quantized
library_name: gguf
tags:
  - gguf
  - ggml
  - music-transcription
  - audio-to-midi
  - automatic-music-transcription
  - muscriptor
  - neuralnote
---

# MuScriptor — GGUF

GGUF conversions of the [MuScriptor](https://huggingface.co/MuScriptor)
multi-instrument music transcription weights by **Mirelo** and **Kyutai**,
converted for use with
[`muscriptor.cpp`](https://github.com/DamRsn/muscriptor.cpp), a C++/ggml
port of the reference implementation, and with
[NeuralNote](https://github.com/DamRsn/NeuralNote).

These are format conversions: safetensors → GGUF, with the bulk weights cast to
float16. Nothing was retrained or fine-tuned. All credit for the model belongs
to Mirelo and Kyutai — see [`NOTICE.md`](NOTICE.md).

## Files

| File | Source checkpoint | Params | Size |
|---|---|---|---|
| `v1/muscriptor-small-f16.gguf` | [`muscriptor-small`](https://huggingface.co/MuScriptor/muscriptor-small) | ~103M | 200 MiB |
| `v1/muscriptor-medium-f16.gguf` | [`muscriptor-medium`](https://huggingface.co/MuScriptor/muscriptor-medium) | ~307M | 590 MiB |
| `v1/muscriptor-large-f16.gguf` | [`muscriptor-large`](https://huggingface.co/MuScriptor/muscriptor-large) | ~1.4B | 2.6 GiB |

`medium` is the default. `small` is the one to reach for on a machine without a
usable GPU; `large` is worth it only with one.

The `v1/` directory names the checkpoint format generation, which each file
also carries as `muscriptor.format_version`. A loader reads the generation it
was built for and refuses the others, so a later generation lands beside this
one rather than replacing it, and an application pinned to `v1` keeps
resolving.

## Usage

See [`muscriptor.cpp`](https://github.com/DamRsn/muscriptor.cpp). These
files are for that library — they are not loadable by llama.cpp or whisper.cpp.

`v1/SHA256SUMS` lists the hash of every weight file beside it, and each file
also carries its own provenance and licence notice as GGUF metadata.
Applications embedding these weights should pin a revision hash rather than
`main`, and verify against a checksum compiled into the application rather than
one fetched from this repo.

## Licence

**CC BY-NC 4.0 — non-commercial use only.** Full text in
[`LICENSE`](LICENSE); deed at
<https://creativecommons.org/licenses/by-nc/4.0/>.

Mirelo and Kyutai supplement the licence with specific conditions of use,
reproduced verbatim in [`NOTICE.md`](NOTICE.md). They are quoted for
information; this repository imposes no terms of its own.

## Citation

```bibtex
@article{muscriptor2026,
  title   = {MuScriptor: An Open Model for Multi-Instrument Music Transcription},
  author  = {Rouard, Simon and Krause, Michael and Roebel, Axel and
             Simon-Gabriel, Carl-Johann and D{\'e}fossez, Alexandre},
  journal = {arXiv preprint arXiv:2607.08168},
  year    = {2026}
}
```
