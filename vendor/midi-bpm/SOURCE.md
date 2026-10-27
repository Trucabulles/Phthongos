# MIDI BPM Detection â€” C++ port

Original: https://github.com/valsteen/midi_bpm_detection
Revision: e14a86c1c15a3523c458c19549ce7be3f9576734
Copyright (c) 2023 Vincent Alsteen. MIT license retained in LICENSE.txt.

Ported from bpm_detection_core/src/{bpm_detection.rs,normal_distribution.rs,note_events.rs}
and bpm_detection_config/src/lib.rs. Uses only the C++17 standard library.

Preserved: note-pair interval folding, logarithmic metadata weights, Gaussian
uncertainty kernel, duration-sampled histogram, and last-bin tie breaking.
Adaptations: batch API instead of mutable realtime buffer; no retained-note pruning;
configuration/input validation; standard-library Gaussian PDF; factored histogram
amplitude (small floating-point roundoff differences). No Rust is compiled or linked
by the application or shipped in releases.

The Phthongos adapter in core/midi/TempoEstimate.cpp owns analysis windows,
peak extraction, approximate-result labels and project persistence. These are local
integration policies, separate from the upstream algorithm.

The application searches 50–200 BPM with the original default weights and kernel.
It retains up to eight separated histogram peaks and their relative scores, plus
the normalized histogram and its duration axis in the project. Local windows are
at most 8 seconds/256 note-ons, with up to 12 windows spread over the take.
No new confidence cutoff suppresses the best available approximate proposal.
