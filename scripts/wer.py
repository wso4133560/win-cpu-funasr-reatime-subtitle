"""English WER: lowercase, ignore punctuation, keep words/numbers; no semantic rewriting."""
import argparse
import json
import re
from pathlib import Path

def words(text):
    return re.findall(r"[a-z0-9]+(?:'[a-z0-9]+)?", text.lower().replace('’', "'"))

def measure(reference, hypothesis):
    ref, hyp = words(reference), words(hypothesis)
    if not ref:
        raise ValueError('Reference must contain at least one word')
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
    return dict(reference_words=len(ref), hypothesis_words=len(hyp), substitutions=sub,
                deletions=delete, insertions=insert, edit_distance=error, wer=error/len(ref))

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('reference', type=Path)
    p.add_argument('hypothesis', type=Path)
    p.add_argument('--output', type=Path)
    a = p.parse_args()
    result = measure(a.reference.read_text(encoding='utf-8-sig'), a.hypothesis.read_text(encoding='utf-8-sig'))
    text = json.dumps(result, indent=2)
    print(text)
    if a.output: a.output.write_text(text+'\n', encoding='utf-8')
