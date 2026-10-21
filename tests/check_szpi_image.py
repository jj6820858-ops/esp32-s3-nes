"""Validate the actual Retro-Go image, its partitions, and embedded app files."""
import hashlib
from pathlib import Path
import struct
import zlib

root = Path(__file__).resolve().parents[1]
path = root / 'szpi-nes_0.1.0_szpi-esp32s3.img'
data = path.read_bytes()
assert len(data) < 16 * 1024 * 1024, 'Image exceeds Flash capacity'
assert data[0] == 0xE9, 'Missing ESP bootloader'
footer = struct.unpack('<8s28s28s28sII156s', data[-256:])
assert footer[0] == b'RG_IMG_0', 'Wrong Retro-Go footer'
assert footer[3].rstrip(b'\0') == b'szpi-esp32s3', 'Wrong board target'
assert footer[5] == zlib.crc32(data[:-256]), 'Image CRC mismatch'
table = data[0x8000:0x8C00]
parts = {}
pos = 0
while table[pos:pos + 2] == b'\xAA\x50':
    _, kind, subtype, offset, size, label, flags = struct.unpack('<2sBBLL16sL', table[pos:pos+32])
    label = label.rstrip(b'\0').decode()
    assert offset % (0x10000 if kind == 0 else 0x1000) == 0
    assert offset + size <= len(data) - 256
    parts[label] = (kind, subtype, offset, size)
    pos += 32
assert table[pos:pos + 2] == b'\xEB\xEB', 'Missing partition table MD5'
assert table[pos+16:pos+32] == hashlib.md5(table[:pos]).digest(), 'Partition MD5 mismatch'
assert set(parts) == {'nvs', 'otadata', 'phy_init', 'launcher', 'retro-core'}
previous_end = 0
for label, (kind, subtype, offset, size) in parts.items():
    assert offset >= previous_end, f'Overlapping partition: {label}'
    previous_end = offset + size
    if kind != 0:
        continue
    app = (root / label / 'build' / (label + '.bin')).read_bytes()
    assert len(app) <= size
    assert data[offset:offset+len(app)] == app, f'App payload mismatch: {label}'
    assert app[0] == 0xE9 and struct.unpack_from('<H', app, 12)[0] == 9, 'App is not ESP32-S3'
    print(f'{label}: offset=0x{offset:X}, partition={size}, payload={len(app)}')
    sdkconfig = (root / label / 'sdkconfig').read_text(encoding='utf-8')
    for setting in ('CONFIG_SPIRAM=y', 'CONFIG_SPIRAM_MODE_OCT=y',
                    'CONFIG_ESPTOOLPY_FLASHSIZE="16MB"', 'CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240'):
        assert setting in sdkconfig, f'{label}: missing {setting}'
print(f'{path.name}: {len(data)} bytes; CRC, partition MD5, app payloads and board configuration passed')
print('SHA256:', hashlib.sha256(data).hexdigest())
