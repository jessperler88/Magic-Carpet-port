# mctools

Python 3.13 toolkit for Magic Carpet 1 data. Requires `pip install propack pillow`
(only `propack` is needed for *packing*; decoding is self-contained).

Run everything from this directory:

```
python -m mctools.extract  [game_dir] [out_dir]   # unpack whole game + levels + palettes
python -m mctools.tmaps    [game_dir] [out_dir]   # tmaps.dat chunks -> PNG
python -m mctools.sprites  [game_dir] [out_dir]   # .dat/.tab sprite sheets -> PNG
python -m mctools.lefile   <exe> --flat out.bin --json map.json --fixups fixups.csv
python -m mctools.rnc      <file...>              # decode and report sizes
```

Defaults: game_dir `C:\Magic Carpet\MagicCarpet\magic`, out_dir `C:\Magic Carpet\extracted`.

Library use:

```python
from mctools import rnc, level, dattab, sprites, tmaps, lefile
lv = level.Level.parse(open('.../levels/lev00000.dat','rb').read())
print(lv.header, len(lv.active_things))
```

See `docs/FORMATS.md` for the format notes behind each module.
