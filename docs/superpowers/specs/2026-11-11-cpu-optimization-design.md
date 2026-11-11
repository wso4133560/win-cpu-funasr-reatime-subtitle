# CPU subtitle optimization

The user authorized implementing the preceding source/performance analysis and comparing results. Keep Windows x64, C++, CPU-only, the existing Q8 SenseVoiceSmall and FSMN models, and current recognition thresholds.

## Approach

Optimize the current engine rather than changing models/backends or introducing uncalibrated VAD/decoder changes. Reuse CPU worker pools and graph allocation buffers. Precompute immutable fbank tables without changing arithmetic. Keep early partial results at 1, 2, 3, 4 seconds; thereafter refresh every 2 seconds. Finals keep their existing priority.

Fix forced-boundary loss by continuing directly with retained voiced carry instead of requiring another onset. Preserve the tail's trailing-silence count so speech stopping exactly at a cut still finalizes. If the carry is wholly nonvoice, retain ordinary preroll behavior. This first iteration preserves contiguous partitioning; it does not introduce acoustic overlap and heuristic text deduplication, which could delete intentional repeated words. Reset must clear continuation state.

## Validation

Preserve original binaries. Add regression tests for contiguous forced boundaries, reset, reduced long-utterance work, and feature equivalence. Compare original/optimized binaries on identical official English and Chinese samples, and a documented repeated-speech stress fixture. Record aggregate wall time, inference time/calls, exact transcripts, English WER and Chinese CER. Use multiple alternating runs. Accuracy improvements are reported only when measured; small fixtures do not establish general accuracy.

## Limits

Do not alter VAD thresholds, add language-model correction, change quantization, enable GPUs, or claim general accuracy gains. Native FSMN state caching, full encoder streaming, graceful capture shutdown, and silence-clock changes are separate follow-ups. Do not stop or replace a user's running process.
