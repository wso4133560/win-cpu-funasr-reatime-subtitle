"""English WER or Unicode CER; ignore punctuation without semantic rewriting."""
import argparse
import json
import re
import unicodedata
from pathlib import Path

def words(text):
    return re.findall(r"[a-z0-9]+(?:'[a-z0-9]+)?", text.lower().replace('’', "'"))

def measure(reference, hypothesis):
    ref, hyp = words(reference), words(hypothesis)
    return _measure(ref, hyp, 'words', 'wer')


def measure_cer(reference, hypothesis):
    def chars(text):
        return [c for c in unicodedata.normalize('NFKC', text).casefold() if c.isalnum()]
    return _measure(chars(reference), chars(hypothesis), 'characters', 'cer')


def _measure(ref, hyp, unit, metric):
    if not ref:
        raise ValueError(f'Reference must contain at least one {unit}')
    table = [[(0, 0, 0, 0)] * (len(hyp) + 1) for _ in range(len(ref) + 1)]
    for i in range(1, len(ref) + 1): table[i][0] = (i, 0, i, 0)
    for j in range(1, len(hyp) + 1): table[0][j] = (j, 0, 0, j)
    for i, a in enumerate(ref, 1):
        for j, b in enumerate(hyp, 1):
            if a == b:
                table[i][j] = table[i-1][j-1]
            else:
                e,s,d,k = table[i-1][j-1]; sub = (e+1,s+1,d,k)
                e,s,d,k = table[i-1][j]; deletion = (e+1,s,d+1,k)
                e,s,d,k = table[i][j-1]; insertion = (e+1,s,d,k+1)
                table[i][j] = min((sub, deletion, insertion), key=lambda v: v[0])
    error, sub, delete, insert = table[-1][-1]
    return {f'reference_{unit}': len(ref), f'hypothesis_{unit}': len(hyp),
            'substitutions': sub, 'deletions': delete, 'insertions': insert,
            'edit_distance': error, metric: error/len(ref)}

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('reference', type=Path)
    p.add_argument('hypothesis', type=Path)
    p.add_argument('--output', type=Path)
    p.add_argument('--cer', action='store_true', help='Measure Unicode character error rate')
    a = p.parse_args()
    metric = measure_cer if a.cer else measure
    result = metric(a.reference.read_text(encoding='utf-8-sig'), a.hypothesis.read_text(encoding='utf-8-sig'))
    text = json.dumps(result, indent=2)
    print(text)
    if a.output: a.output.write_text(text+'\n', encoding='utf-8')
