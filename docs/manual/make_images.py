#!/usr/bin/env python3
"""Pictures for the manual.

Run the snapshot tool first (it renders the real editors and writes one picture per
Track control into docs/screenshots/controls with an index), then this script:
it names the Track pictures and cuts the Hub parts out of hub-map.png.

    build/tests/freqfreaks_snapshot_artefacts/Release/freqfreaks_snapshot docs/screenshots
    python3 docs/manual/make_images.py
"""
import os
import shutil
from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
shots = os.path.join(here, '..', 'screenshots')
out = os.path.join(here, 'img')
os.makedirs(out, exist_ok=True)

names = {
    'A / B': 'ab', 'Amount': 'amount', 'Auto': 'auto', 'Bass Mono': 'bassmono', 'EQ Depth': 'depth',
    'Link': 'link', 'Mono Safety': 'monosafety', 'Output': 'output', 'Range': 'range', 'Sharp': 'sharp',
    'Simple / Detail / Expert': 'levels', 'Space Amount': 'spaceamount', 'Space comp': 'spacecomp',
    'Атака / Релиз': 'attackrelease', 'Бюджет освобождения': 'budget', 'Имя трека': 'name',
    'Прослушка (Delta, референс)': 'listen', 'Режим Space': 'spacemode', 'Скорость': 'speed',
    'Спектр': 'spectrum', 'Сторона для Pan Away': 'panside', 'Уступает (референс)': 'reference',
    'Цвет трека': 'colour', 'Чувствительность': 'sensitivity',
}
index = os.path.join(shots, 'controls', 'index.tsv')
for line in open(index, encoding='utf-8').read().strip().split('\n'):
    title, file = line.split('\t')
    if title in names:
        shutil.copy(os.path.join(shots, 'controls', file), os.path.join(out, 'track-' + names[title] + '.png'))
    else:
        print('no name for', title)

# Hub parts, in pixels of hub-map.png (the 1280 x 800 window rendered at 2x).
hub = Image.open(os.path.join(shots, 'hub-map.png'))
for name, box in {
    'total': (530, 30, 1180, 104), 'space': (1225, 30, 1885, 104), 'bypass': (1930, 30, 2240, 104),
    'mono': (2240, 30, 2540, 104), 'tabs': (505, 160, 1130, 240), 'compact': (18, 160, 450, 220),
    'row': (18, 222, 452, 372), 'map': (512, 282, 1860, 532), 'issue': (1928, 262, 2536, 476),
}.items():
    hub.crop(box).save(os.path.join(out, 'hub-' + name + '.png'))
print('pictures in', out)
