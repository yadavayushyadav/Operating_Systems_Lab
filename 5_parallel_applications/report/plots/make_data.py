#!/usr/bin/env python3
# Turns the raw measurements (timings.csv and usage.csv, written by src/run_experiments.sh) into what
# report.tex uses: .dat files for the graphs and .tex files with the rows of the tables.
# Every number is the median over the repetitions of a run; ranges are minimum - maximum.
# Usage: python3 make_data.py        (from report/plots)

import csv
import os
import statistics
from collections import defaultdict

MEASUREMENTS = '../../measurements'

rows = list(csv.DictReader(open(MEASUREMENTS + '/timings.csv')))
print(len(rows), 'runs,', sum(r['correct'] == '1' for r in rows), 'with a correct output and passed checks')
for r in rows:
    if r['correct'] != '1':
        print('  NOT CORRECT:', r)

# Image 1 was measured again on its own (experiment "small"), because its short runs were disturbed in the
# experiments "images" and "single". If those runs exist, they replace the image 1 runs of the two experiments.
has_small = any(r['experiment'] == 'small' for r in rows)

# (program, variant, batch, image, runs) -> list of values of a column
def collect(column, experiments):
    values = defaultdict(list)
    for r in rows:
        experiment = r['experiment']
        if has_small and r['image'] == '1' and experiment in ('images', 'single'):
            continue
        if experiment == 'small':
            experiment = 'images' if r['runs'] == '100' else 'single'
        if experiment in experiments and r[column] != '':
            values[(r['program'], int(r['variant']), int(r['batch']), int(r['image']), int(r['runs']))].append(float(r[column]))
    return values

def median(values, key):
    return statistics.median(values[key]) if key in values else float('nan')

def write(name, lines):
    with open(name, 'w') as out:
        out.write('\n'.join(lines) + '\n')
    print('---', name)
    print('\n'.join(lines))

def number(value, digits=2):
    return ('%.' + str(digits) + 'f') % value

programs = ['part2_1', 'part2_2', 'part2_3', 'part3_1', 'part3_2']
names = {'part2_1': 'threads, atomic operations', 'part2_2': 'processes, pipes', 'part2_3': 'processes, shared memory',
         'part3_1': r'sockets, S1+S2 $\vert$ S3', 'part3_2': r'sockets, S1 $\vert$ S2+S3'}
labels = {'part2_1': r'\texttt{part2\_1}', 'part2_2': r'\texttt{part2\_2}', 'part2_3': r'\texttt{part2\_3}',
          'part3_1': r'\texttt{part3\_1}', 'part3_2': r'\texttt{part3\_2}'}
size = {1: (450, 180), 2: (786, 393), 3: (1024, 575), 4: (1280, 910), 5: (1500, 1000), 6: (2271, 1500), 7: (3750, 2470)}
pixels = {image: size[image][0] * size[image][1] for image in size}

# ================= image 1, 1000 runs (experiments main and batch) =================
processing = collect('processing', ['main', 'batch'])
s1 = collect('s1', ['main', 'batch'])
s2 = collect('s2', ['main', 'batch'])
s3 = collect('s3', ['main', 'batch'])
base_key = ('part1', 0, 0, 1, 1000)
baseline = median(processing, base_key)
b1, b2, b3 = median(s1, base_key), median(s2, base_key), median(s3, base_key)
print('\nBaseline, image 1, 1000 runs: processing %.3f s (n=%d, %.3f - %.3f)   S1 %.3f  S2 %.3f  S3 %.3f' % (
    baseline, len(processing[base_key]), min(processing[base_key]), max(processing[base_key]), b1, b2, b3))
print('all baseline runs:', ' '.join('%.3f' % v for v in processing[base_key]))

million_pixels = 81.0   # pixels processed in 1000 runs of image 1, in millions
write('table_baseline.tex', [
    r'S1 smoothen     & %s & %s--%s & %.1f & %.0f\,\%% \\' % (number(b1), number(min(s1[base_key])), number(max(s1[base_key])), 1000 * b1 / million_pixels, 100 * b1 / (b1 + b2 + b3)),
    r'S2 find details & %s & %s--%s & %.1f & %.0f\,\%% \\' % (number(b2), number(min(s2[base_key])), number(max(s2[base_key])), 1000 * b2 / million_pixels, 100 * b2 / (b1 + b2 + b3)),
    r'S3 sharpen      & %s & %s--%s & %.1f & %.0f\,\%% \\' % (number(b3), number(min(s3[base_key])), number(max(s3[base_key])), 1000 * b3 / million_pixels, 100 * b3 / (b1 + b2 + b3)),
    r'\midrule',
    r'Processing time (S1 + S2 + S3) & %s & %s--%s & %.1f & 100\,\%% \\' % (number(baseline), number(min(processing[base_key])), number(max(processing[base_key])), 1000 * baseline / million_pixels),
])
print('bounds: three cores %.2f   A: S1+S2 | B: S3 %.2f   A: S1 | B: S2+S3 %.2f' % (
    baseline / b1, baseline / (b1 + b2), baseline / max(b1, b2 + b3)))

# pixel by pixel (batch 1), both variants
lines = []
for program in programs:
    if program == 'part3_1':
        lines.append(r'\midrule')
    k1, k2 = (program, 1, 1, 1, 1000), (program, 2, 1, 1, 1000)
    t1, t2 = median(processing, k1), median(processing, k2)
    lines.append(r'%s & %s & %.1f & %.1f--%.1f & %.3f & %.0f & %s & %s--%s & %s \\' % (
        labels[program], names[program], t1, min(processing[k1]), max(processing[k1]), baseline / t1, 1000 * t1 / million_pixels,
        number(t2), number(min(processing[k2])), number(max(processing[k2])), number(baseline / t2)))
write('table_main.tex', lines)

# time for one pixel to travel through the pipeline, variant 1, batch 1
print('\nnanoseconds per pixel, variant 1, batch 1: baseline %.0f' % (1000 * baseline / million_pixels),
      ' '.join('%s %.0f' % (p, 1000 * median(processing, (p, 1, 1, 1, 1000)) / million_pixels) for p in programs))

# when the stages finished, variant 2
lines = []
for batch in [1, 450]:
    for program in programs[:3]:
        key = (program, 2, batch, 1, 1000)
        lines.append(r'%s & %d & %s & %s & %s & %s \\' % (labels[program], batch, number(median(s1, key)), number(median(s2, key)),
                     number(median(s3, key)), number(baseline / median(processing, key))))
    if batch == 1:
        lines.append(r'\midrule')
write('table_variant2.tex', lines)

# processing time against the number of pixels per message, variant 1
batches = [1, 2, 4, 8, 16, 32, 64, 128, 256, 450, 1024, 4096, 16384, 81000]
with open('batch.dat', 'w') as out:
    out.write('batch threads pipes shm speedup_threads speedup_pipes speedup_shm\n')
    for batch in batches:
        times = [median(processing, (program, 1, batch, 1, 1000)) for program in programs[:3]]
        out.write('%d %s %s\n' % (batch, ' '.join('%.4f' % t for t in times), ' '.join('%.4f' % (baseline / t) for t in times)))
lines = []
for batch in batches:
    cells = []
    for program in programs[:3]:
        t = median(processing, (program, 1, batch, 1, 1000))
        cells.append('%s & %s' % (number(t, 1) if t >= 10 else number(t), number(baseline / t, 3) if t >= 10 else number(baseline / t)))
    note = {1: ' (one pixel)', 450: ' (one row)', 81000: ' (whole image)'}.get(batch, '')
    lines.append(r'%s%s & %s & %s \\' % ('{:,}'.format(batch).replace(',', r'\,'), note, '{:,}'.format(81000000 // batch + (81000000 % batch > 0)).replace(',', r'\,'), ' & '.join(cells)))
write('table_batch.tex', lines)

# Part III
lines = []
for variant in [1, 2]:
    for batch in ([1, 16, 450, 81000] if variant == 1 else [1, 450]):
        t31, t32 = median(processing, ('part3_1', variant, batch, 1, 1000)), median(processing, ('part3_2', variant, batch, 1, 1000))
        megabytes = (81000000 if variant == 1 else 810000) * 3 / 1e6
        fmt = (lambda t: number(t, 1) if t >= 10 else number(t))
        lines.append(r'%d & %s & %s & %s & %s & %s & %s \\' % (variant, '{:,}'.format(batch).replace(',', r'\,'), number(megabytes, 1) if megabytes < 100 else number(megabytes, 0),
                     fmt(t31), number(baseline / t31, 3) if t31 >= 10 else number(baseline / t31), fmt(t32), number(baseline / t32, 3) if t32 >= 10 else number(baseline / t32)))
    if variant == 1:
        lines.append(r'\midrule')
write('table_part3.tex', lines)
print('A finished after (s1 column), part3:', {k[:3]: round(median(s1, k), 2) for k in s1 if k[0].startswith('part3')})

# speed-ups of all programs in four configurations, for the bar chart
with open('speedups.dat', 'w') as out:
    out.write('configuration threads pipes shm sockets31 sockets32\n')
    for index, (variant, batch) in enumerate([(1, 1), (1, 450), (2, 1), (2, 450)]):
        out.write('%d %s\n' % (index + 1, ' '.join('%.4f' % (baseline / median(processing, (program, variant, batch, 1, 1000))) for program in programs)))
print(open('speedups.dat').read())

# ================= images of different sizes, 100 runs, one row per message =================
processing = collect('processing', ['images'])
s1 = collect('s1', ['images'])
if processing:
    lines = []
    for variant in [1, 2]:
        with open('images_v%d.dat' % variant, 'w') as out:
            out.write('image pixels baseline bound threads pipes shm speedup_threads speedup_pipes speedup_shm\n')
            for image in range(1, 8):
                key = ('part1', 0, 0, image, 100)
                if key not in processing:
                    continue
                base = median(processing, key)
                times = [median(processing, (program, variant, size[image][0], image, 100)) for program in programs[:3]]
                out.write('%d %d %.4f %.4f %s %s\n' % (image, pixels[image], base, base / median(s1, key), ' '.join('%.4f' % t for t in times), ' '.join('%.4f' % (base / t) for t in times)))
    for image in range(1, 8):
        key = ('part1', 0, 0, image, 100)
        if key not in processing:
            continue
        base = median(processing, key)
        cells = []
        for variant in [1, 2]:
            for program in programs[:3]:
                t = median(processing, (program, variant, size[image][0], image, 100))
                cells.append(number(base / t))
        lines.append(r'%d & $%d\times%d$ & %s & %s & %.1f & %s & %s \\' % (image, size[image][0], size[image][1], number(pixels[image] / 1e6, 2), number(base),
                     1e9 * base / (100 * pixels[image]), number(base / median(s1, key)), ' & '.join(cells)))
    write('table_images.tex', lines)

# ================= a single run =================
# The whole run is compared here (the "Total Time" of the programs): reading the file, allocating the images,
# processing and writing the file.
columns = {name: collect(name, ['single']) for name in ['read', 'processing', 'write', 'total']}
if columns['total']:
    lines = []
    for image in [1, 4, 7]:
        key = ('part1', 0, 0, image, 1)
        if key not in columns['total']:
            continue
        r, p, w, total = [median(columns[name], key) for name in ['read', 'processing', 'write', 'total']]
        other = total - r - p - w          # allocating the three result images, one block per pixel
        cells = [number(1000 * r, 1 if total < 0.1 else 0), number(1000 * other, 1 if total < 0.1 else 0), number(1000 * p, 1 if total < 0.1 else 0),
                 number(1000 * w, 1 if total < 0.1 else 0), number(1000 * total, 1 if total < 0.1 else 0), '%.0f\\,\\%%' % (100 * p / total)]
        for batch in [size[image][0], 1]:
            for program in programs[:3]:
                cells.append(number(total / median(columns['total'], (program, 1, batch, image, 1))))
        lines.append(r'%d & $%d\times%d$ & %s \\' % (image, size[image][0], size[image][1], ' & '.join(cells)))
    write('table_single.tex', lines)
    print('single run, seconds: image program batch   read  processing  write   total   (n)')
    for key in sorted(columns['total'], key=lambda k: (k[3], k[0], k[2])):
        print('%d %-8s %6d  %s   %d' % (key[3], key[0], key[2], '  '.join('%8.4f' % median(columns[name], key) for name in ['read', 'processing', 'write', 'total']), len(columns['total'][key])))

# ================= processor time and context switches =================
if os.path.exists(MEASUREMENTS + '/usage.csv'):
    usage = list(csv.DictReader(open(MEASUREMENTS + '/usage.csv')))
    lines = []
    for batch in ['1', '450']:
        for u in usage:
            if u['batch'] != batch or (u['program'] == 'part1' and batch == '450'):
                continue
            messages = 2 * 8100000 // int(batch)
            label = r'\texttt{part1}' if u['program'] == 'part1' else labels[u['program']]
            lines.append(r'%s & %s & %s & %s & %s & %s & %s \\' % (label, '--' if u['program'] == 'part1' else batch, u['elapsed'], u['user'], u['system'],
                         '{:,}'.format(int(u['voluntary_switches'])).replace(',', r'\,'),
                         '--' if u['program'] == 'part1' else number(int(u['voluntary_switches']) / messages, 3)))
        if batch == '1':
            lines.append(r'\midrule')
    write('table_usage.tex', lines)
