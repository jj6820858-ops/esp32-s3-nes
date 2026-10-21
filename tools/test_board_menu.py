"""实机回归：仅在 SZPI-Controls.nes 正常运行时使用，会写该测试 ROM 的存档 0。

通过串口注入原菜单按键验证保存、加载和应用切换，不修改商业游戏存档。
运行期间不要同时触摸屏幕或占用串口。
"""
from pathlib import Path
import argparse
import serial
import sys
import time

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
parser=argparse.ArgumentParser()
parser.add_argument('--port',default='COM5')
args=parser.parse_args()
link=serial.Serial(port=None,baudrate=115200,timeout=0.1)
link.dtr=False; link.rts=False; link.port=args.port; link.open()
data=bytearray()

def command(key,seconds=0.8):
    link.write(key.encode('ascii')); link.flush()
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        data.extend(link.read(max(1,link.in_waiting)))

try:
    # M → Save & Continue → Slot 0；再 M → 下两次 → Load game → Slot 0。
    for key,delay in [('m',1),('a',1),('a',2),('m',1),('d',0.6),('d',0.6),
                      ('a',1),('a',2),('h',5),('g',8),('h',5),('g',8)]:
        command(key,delay)
finally:
    link.close()
path=Path('logs/board-save-load-roundtrip.log')
path.write_bytes(data)
decoded=data.decode('utf-8',errors='replace')
for line in decoded.splitlines():
    if any(s in line.lower() for s in ('state_save','state_load','rg_emu_save_state',
                                      'rg_emu_load_state','switching to app','deinit',
                                      'panic','assert','failed','nes running')):
        print(line)
assert 'RG_PANIC' not in decoded and 'Guru Meditation' not in decoded
assert 'state_save: Game saved!' in decoded, 'Saving the test ROM state failed'
assert 'state_load: Game restored' in decoded, 'Loading the test ROM state failed'
assert decoded.count('NES running: frames=300')>=2, 'NES did not run after both round trips'
print(f'Save, load and application round trips passed; evidence: {path}')
