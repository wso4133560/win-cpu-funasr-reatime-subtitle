# CPU Subtitle Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce repeated CPU work and remove forced-boundary sample loss, with reproducible before/after evidence.

**Architecture:** Keep model weights/backends resident; also retain CPU thread pools and ggml graph allocators. Cache immutable feature tables. Adjust partial scheduling and continuation preroll without changing models or VAD thresholds.

**Tech Stack:** C++20, MSVC, ggml CPU, Python standard library for comparison.

**Spec:** `docs/superpowers/specs/2026-11-11-cpu-optimization-design.md`

## Global Constraints

- Windows x64, CPU-only, existing GGUF model files and thresholds.
- Never stop a user's running process. Keep baseline binaries in ignored `build-baseline/`.
- Work in the current checkout on a dedicated local branch; no publishing or merging.
- Leave changes reviewable; commit only when explicitly requested. The user subsequently requested a local commit of the optimization.

## Review Focus

- Forced continuation must not lose 30 ms or leak into reset/new unrelated speech.
- Early updates must remain timely while long-utterance partial work decreases.
- Reused allocators must support changing input lengths without stale tensor data.
- CPU thread pools must outlive backend work and be freed in safe order.
- Chinese error measurement must retain Chinese characters and count deletions/insertions.

## Task 1: Fixtures and comparison

- [x] Preserve binaries and run existing tests. Baseline CTest 2/2 passed.
- [x] Download official samples, retain source URLs/hashes and fixed references. Build deterministic repeated-speech fixtures.
- [x] Test Chinese/English error measurement with known substitution, deletion and insertion examples, then implement a repeatable comparison runner with alternating executables. CER tests failed before implementation; all three metrics tests now pass.

## Task 2: Segmentation

- [x] Add tests that fail on the original implementation: forced-final end equals next segment start; all boundary samples survive; reset clears continuation; 12-second speech generates fewer repeated partial windows while preserving early feedback/finals.
- [x] Implement continuation preroll and adaptive partial scheduling in `include/segmenter.h`.
- [x] Run pipeline tests. Boundary equality failed first; after boundary fix, repeated-work bound failed; after adaptive cadence, all pass.

## Task 3: Engine reuse

- [x] Capture baseline transcripts/timings before engine edits, including repeated/different input lengths.
- [x] Cache immutable fbank tables per frontend; retain existing fbank/LFR arithmetic. Validate actual output against original model replay rather than tests mirroring table construction.
- [x] Retain CPU thread pools and graph allocators in each engine implementation with safe destruction.
- [x] Rebuild and run full CTest plus real-model comparisons. CTest 3/3; 48 measured runs across four fixtures/two modes; no fixture error-rate regression. Detailed data in `docs/OPTIMIZATION-2026-11-11.md`.

## Task 4: Review and delivery

- [x] Fresh independent code review, fix substantive findings and rerun relevant checks. Review exposed VAD-dip and stop-at-cut tail loss; archived original fails, current tests pass. Continuation design updated accordingly.
- [x] Write benchmark methodology/results and remaining limits under docs; update usage description for partial cadence.
- [x] Copy verified Release EXE to project root, preserving baseline EXE separately. SHA256 matches tested Release output.
