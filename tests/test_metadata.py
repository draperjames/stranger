import ctypes
import json
from pathlib import Path
root = Path(__file__).resolve().parents[1]
m = json.loads((root / 'src/module.json').read_text())
assert m['id'] == 'stranger' and m['dsp'] == 'stranger.so'
assert m['api_version'] == 2
caps = m['capabilities']
assert caps['component_type'] == 'audio_fx' and caps['chainable']
assert caps['requires_continuous_processing']
params = caps['chain_params']
assert len(params) == 8 and len({p['key'] for p in params}) == 8
assert caps['ui_hierarchy']['levels']['root']['knobs'] == [p['key'] for p in params]
lib = ctypes.CDLL(str(root / 'build/host/stranger.so'))
# Stable host prefix; module deliberately consumes no newer host fields.
class Host(ctypes.Structure):
    _fields_ = [('api_version', ctypes.c_uint32), ('sample_rate', ctypes.c_int), ('frames_per_block', ctypes.c_int)]
Create = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p)
Destroy = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
Process = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int)
Set = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p)
Get = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int)
class API(ctypes.Structure):
    _fields_ = [('version', ctypes.c_uint32), ('create', Create), ('destroy', Destroy),
                ('process', Process), ('set', Set), ('get', Get), ('midi', ctypes.c_void_p)]
lib.move_audio_fx_init_v2.argtypes = [ctypes.POINTER(Host)]
lib.move_audio_fx_init_v2.restype = ctypes.POINTER(API)
api = lib.move_audio_fx_init_v2(ctypes.byref(Host(1,44100,128))).contents
s = api.create(b'.', None)
assert s
try:
    def get(key):
        buf = ctypes.create_string_buffer(16384)
        n = api.get(s, key.encode(), buf, len(buf))
        assert n >= 0 and n == len(buf.value)
        return buf.value.decode()
    assert json.loads(get('chain_params')) == params
    assert json.loads(get('ui_hierarchy')) == caps['ui_hierarchy']
    state = json.loads(get('state'))
    for p in params:
        expected = p['default']
        if p['type'] == 'enum': expected = p['options'][expected]
        if isinstance(expected, float): assert abs(state[p['key']] - expected) < 1e-6
        else: assert state[p['key']] == expected
    assert get('module_id') == 'stranger'
finally:
    api.destroy(s)
print('PASS manifest, runtime hierarchy, eight knob bindings, defaults and namespace agree')
