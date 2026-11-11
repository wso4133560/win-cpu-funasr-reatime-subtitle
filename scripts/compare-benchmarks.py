"""Alternate original/candidate file replay; retain transcripts, timings and references."""
import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import time
import wave
from wer import measure, measure_cer

ROOT = Path(__file__).resolve().parents[1]
ZH_REFERENCE = '欢迎大家来体验达摩院推出的语音识别模型'


def fixtures(directory):
    en = (ROOT / 'tests/english-reference.txt').read_text(encoding='utf-8-sig').strip()
    cases = [('english', directory / 'jfk.wav', en, 'wer'),
             ('chinese', directory / 'zh.wav', ZH_REFERENCE, 'cer')]
    # Concatenate complete Mandarin utterances after trimming only outer quiet.
    # This is a repeated-utterance throughput case; natural pauses remain.
    with wave.open(str(directory / 'zh.wav')) as source:
        params = source.getparams()
        if (params.nchannels, params.sampwidth, params.framerate) != (1, 2, 16000):
            raise ValueError('Chinese fixture must be PCM16, mono, 16 kHz')
        samples = array('h', source.readframes(source.getnframes()))
    voiced = [i for i, value in enumerate(samples) if abs(value) > 100]
    start, end = max(0, voiced[0]-800), min(len(samples), voiced[-1]+801)
    stress = directory / 'zh-repeat.wav'
    with wave.open(str(stress), 'wb') as target:
        target.setparams(params)
        target.writeframes(samples[start:end].tobytes() * 5)
    cases.append(('chinese_repeat', stress, ZH_REFERENCE * 5, 'cer'))
    # Remove outer quiet more tightly for a separate forced-boundary fixture.
    # RMS threshold 200 PCM16 units in 20 ms frames, 100 ms head / 40 ms tail.
    active = [i for i in range(0, len(samples)-319, 320)
              if math.sqrt(sum(x*x for x in samples[i:i+320])/320) > 200]
    continuous = directory / 'zh-continuous.wav'
    begin, finish = max(0, active[0]-1600), min(len(samples), active[-1]+320+640)
    with wave.open(str(continuous), 'wb') as target:
        target.setparams(params)
        target.writeframes(samples[begin:finish].tobytes() * 6)
    cases.append(('chinese_continuous', continuous, ZH_REFERENCE * 6, 'cer'))
    manifest = []
    for name, path, reference, metric in cases:
        manifest.append({'name': name, 'path': str(path.resolve()), 'metric': metric,
                         'reference': reference, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    (directory / 'fixtures.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    return cases


def run(executable, audio, output, stream):
    command = [str(executable.resolve()), '--audio', str(audio.resolve()),
               '--threads', '8', '--out', str(output.resolve())]
    if stream:
        command.append('--stream')
    started = time.perf_counter()
    result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=180)
    elapsed = time.perf_counter() - started
    output.mkdir(parents=True, exist_ok=True)
    stdout = result.stdout.decode('utf-8', errors='replace')
    (output / 'stdout.txt').write_text(stdout, encoding='utf-8')
    (output / 'stderr.txt').write_bytes(result.stderr)
    if result.returncode:
        raise RuntimeError(f'{executable} failed ({result.returncode}): {result.stderr[-2000:]!r}')
    calls = re.findall(r'compute_ms=([\d.]+)', stdout)
    transcript = (output / 'hypothesis.txt').read_text(encoding='utf-8-sig')
    if not stream:
        texts = re.findall(r'^run=\d+ compute_ms=[\d.]+ text=(.*)$', stdout, re.MULTILINE)
        if len(texts) != 3 or len(set(texts)) != 1 or texts[0].strip() != transcript.strip():
            raise RuntimeError(f'Repeated whole-file transcripts disagree: {output}')
    return {'wall_s': elapsed, 'inference_ms': [float(v) for v in calls],
            'calls': len(calls), 'finals': len(re.findall(r'final=1', stdout)),
            'transcript': transcript}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--fixtures', type=Path, default=ROOT / 'test-output/optimization')
    parser.add_argument('--output', type=Path, default=ROOT / 'test-output/optimization/comparison.json')
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--case', action='append', help='Only run the named fixture(s)')
    args = parser.parse_args()
    if args.runs < 1:
        parser.error('--runs must be positive')
    cases = fixtures(args.fixtures)
    if args.case:
        cases = [case for case in cases if case[0] in args.case]
        if not cases:
            parser.error('No fixtures match --case')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    info = {'runs': args.runs, 'threads': 8,
            'baseline_sha256': hashlib.sha256(args.baseline.read_bytes()).hexdigest(),
            'candidate_sha256': hashlib.sha256(args.candidate.read_bytes()).hexdigest(),
            'sample_sources': {
                'jfk.wav': 'https://raw.githubusercontent.com/ggml-org/whisper.cpp/master/samples/jfk.wav',
                'zh.wav': 'https://isv-data.oss-cn-hangzhou.aliyuncs.com/ics/MaaS/ASR/test_audio/asr_example_zh.wav'},
            'reference_source': 'https://github.com/modelscope/FunASR/blob/main/README_zh.md',
            'model_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in (ROOT / 'models').glob('*.gguf')}}
    args.output.with_suffix('.info.json').write_text(json.dumps(info, indent=2), encoding='utf-8')
    results = []
    for case, audio, reference, metric_name in cases:
        for stream in (False, True):
            metric = measure if metric_name == 'wer' else measure_cer
            # Warm both model/runtime paths before alternating measured runs.
            for label, exe in [('baseline', args.baseline), ('candidate', args.candidate)]:
                run(exe, audio, args.output.parent / f'warm-{label}-{case}-{stream}', stream)
            for repetition in range(args.runs):
                order = [('baseline', args.baseline), ('candidate', args.candidate)]
                if repetition % 2:
                    order.reverse()
                for label, exe in order:
                    output = args.output.parent / f'{label}-{case}-{stream}-{repetition}'
                    data = run(exe, audio, output, stream)
                    data.update(label=label, case=case, stream=stream, repetition=repetition,
                                errors=metric(reference, data['transcript']))
                    results.append(data)
                    args.output.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
                    print(f'{label} {case} stream={stream} run={repetition} wall={data["wall_s"]:.3f}s '
                          f'calls={data["calls"]} {metric_name}={data["errors"][metric_name]:.4f}', flush=True)
    for case, _, _, _ in cases:
        for stream in (False, True):
            for label in ('baseline', 'candidate'):
                group = [r for r in results if (r['case'], r['stream'], r['label']) == (case, stream, label)]
                print(case, stream, label, 'median_wall_s=', round(statistics.median(r['wall_s'] for r in group), 3))


if __name__ == '__main__':
    main()
