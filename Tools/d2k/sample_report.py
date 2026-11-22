"""Summarises a D2K_SAMPLE file (src/d2k/sampler.cpp): samples per function (and per source file), via llvm-symbolizer
and build/d2k.pdb.  python -I Tools/d2k/sample_report.py <sample file> [exe] [top N]"""
import collections
import subprocess
import sys

path = sys.argv[1]
exe = sys.argv[2] if len(sys.argv) > 2 else 'build/d2k.exe'
top = int(sys.argv[3]) if len(sys.argv) > 3 else 40
other = 0
hist = []
for line in open(path):
    n, a = line.split()
    if a == 'other':
        other = int(n)
    else:
        hist.append((int(n), int(a, 16)))
total = other + sum(n for n, _ in hist)
inp = ''.join('0x%x\n' % (0x140000000 + a) for _, a in hist)
out = subprocess.run(['llvm-symbolizer', '--obj=' + exe, '--no-inlines', '--functions=linkage', '--demangle'],
                     input=inp, capture_output=True, text=True).stdout.split('\n\n')
funcs, files = collections.Counter(), collections.Counter()
for (n, _), block in zip(hist, out):
    lines = block.strip().split('\n')
    fn = lines[0] if lines and lines[0] else '?'
    src = lines[1].rsplit(':', 2)[0] if len(lines) > 1 else '?'
    funcs[fn] += n
    files[src.replace('\\', '/').split('/')[-1]] += n
print('%d samples, %.1f%% outside the exe (system DLLs, drivers)' % (total, 100.0 * other / max(total, 1)))
print('\nby source file:')
for f, n in files.most_common(20):
    print('  %5.1f%%  %s' % (100.0 * n / total, f))
print('\nby function:')
for f, n in funcs.most_common(top):
    print('  %5.1f%%  %s' % (100.0 * n / total, f[:110]))
