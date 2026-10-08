import json
import tarfile
from pathlib import Path
root = Path(__file__).resolve().parents[1]
with tarfile.open(root/'dist/stranger-module.tar.gz') as t:
    names = t.getnames()
    assert all(n == 'stranger' or n.startswith('stranger/') for n in names)
    assert all(not n.startswith('/') and '..' not in Path(n).parts for n in names)
    assert all(not m.issym() and not m.islnk() for m in t.getmembers())
    for name in ['stranger.so','module.json','help.json','LICENSE','THIRD_PARTY.md','AI_DISCLOSURE.md','LICENSES/SCHWUNG-MIT.txt','README.md','docs/PROVENANCE.md','docs/VALIDATION.md']:
        assert f'stranger/{name}' in names
    mod = json.load(t.extractfile('stranger/module.json'))
    assert mod['id'] == 'stranger' and mod['dsp'] == 'stranger.so'
    elf = t.extractfile('stranger/stranger.so').read()
    assert elf[:4] == b'\x7fELF' and elf[4] == 2 and int.from_bytes(elf[18:20], 'little') == 183
print('PASS release layout, required files, Stranger namespace and ARM64 ELF')
