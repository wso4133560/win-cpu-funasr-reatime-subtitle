# Upstream source provenance

`sensevoice_original.cpp` and `funasr_vad_original.h` are unmodified snapshots from
[modelscope/FunASR](https://github.com/modelscope/FunASR), commit
`a57c05bfe2a91b5e0cb0983479634eba3e28ede5` (`runtime-llamacpp-v0.2.6`).
The original MIT license is preserved in `LICENSE-FunASR`.

`src/sensevoice.cpp` and `src/vad.cpp` are derived from those files by
`scripts/extract_engine.py`. Changes separate loading and inference, preserve weights
and CPU backends across calls, make ASR thread count configurable, guard short inputs,
and replace initialization process exits with exceptions. Rolling VAD segmentation
and GUI code are separate from the upstream inference implementation.

`miniaudio.h` is the bundled miniaudio snapshot from the same upstream commit.
Its public-domain/MIT dual license is embedded in that header.

ggml comes from ggml-org/llama.cpp commit
`803b7fcae893e9caaee3921779628fef83ac0965`; bootstrap preserves its source and license
under the ignored `dependencies/` directory. The application statically links CPU ggml.

Models are distributed separately by FunAudioLLM and are not committed to this repository.
