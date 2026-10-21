"""生成自编的 NROM 测试程序，使用自己的图案，不含商业游戏 ROM。

资料：NESdev iNES、PPU registers、Controller reading；所有指令由下面的简易
汇编器写入，方便把 ESP32 外设代码与模拟器内的 6502 游戏程序区分开。
"""
from pathlib import Path
import struct

root=Path(__file__).resolve().parents[1]
code=bytearray(); labels={}; fixups=[]
def emit(*values): code.extend(values)
def label(name): labels[name]=0x8000+len(code)
def absolute(op, address): emit(op,address&255,address>>8)
def jump(op, name): emit(op,0,0); fixups.append((len(code)-2,name,False))
def branch(op, name): emit(op,0); fixups.append((len(code)-1,name,True))
def store(address,value): emit(0xa9,value); absolute(0x8d,address)
def ppu_address(address): store(0x2006,address>>8); store(0x2006,address&255)

label('reset'); emit(0x78,0xd8,0xa2,0xff,0x9a,0xe8)
for address in (0x2000,0x2001,0x4010,0x4015): absolute(0x8e,address)
for i in range(2):
    label('warm'+str(i)); absolute(0x2c,0x2002); branch(0x10,'warm'+str(i))
emit(0xa9,0,0xa2,0)
label('clear')
for page in (0,1,3,4,5,6,7): absolute(0x9d,page*256)
emit(0xe8); branch(0xd0,'clear')
emit(0xa9,0xff,0xa2,0); label('hide'); absolute(0x9d,0x0200)
emit(0xe8); branch(0xd0,'hide')
for address,value in ((0x0200,112),(0x0201,1),(0x0202,0),(0x0203,128)):
    store(address,value)
ppu_address(0x3f00); emit(0xa2,0)
label('palette'); jump(0xbd,'colors'); absolute(0x8d,0x2007)
emit(0xe8,0xe0,32); branch(0xd0,'palette')
ppu_address(0x2000); emit(0xa9,0,0xa0,4,0xa2,0)
label('nametable'); absolute(0x8d,0x2007); emit(0xe8); branch(0xd0,'nametable')
emit(0x88); branch(0xd0,'nametable')
for address,text in ((0x20c4,'SZPI NES READY'),(0x2144,'D PAD MOVE'),
                     (0x21c4,'A TONE B COLOR'),(0x2244,'M OPENS MENU')):
    ppu_address(address)
    for c in text: store(0x2007,ord(c) if c!=' ' else 0)
store(0x4015,1); store(0x4000,0x30); store(0x4001,8)
store(0x4002,0xfd); store(0x4003,8)
store(0x2005,0); store(0x2005,0); store(0x2001,0x1e)
label('frame'); absolute(0x2c,0x2002); branch(0x10,'frame')
store(0x4016,1); store(0x4016,0); emit(0x85,0,0xa2,8)
label('pad'); absolute(0xad,0x4016); emit(0x29,1,0x4a,0x26,0,0xca)
branch(0xd0,'pad')
for mask,address,opcode in ((1,0x0203,0xee),(2,0x0203,0xce),
                             (4,0x0200,0xee),(8,0x0200,0xce)):
    emit(0xa5,0,0x29,mask); branch(0xf0,'skip'+str(mask))
    absolute(opcode,address); label('skip'+str(mask))
emit(0xa5,0,0x29,0x80); branch(0xf0,'quiet')
store(0x4000,0x3a); jump(0x4c,'tone_done')
label('quiet'); store(0x4000,0x30); label('tone_done')
ppu_address(0x3f11); emit(0xa5,0,0x29,0x40); branch(0xf0,'cyan')
emit(0xa9,0x2a); jump(0x4c,'color_done')
label('cyan'); emit(0xa9,0x21); label('color_done'); absolute(0x8d,0x2007)
store(0x2003,0); store(0x4014,2); store(0x2005,0); store(0x2005,0)
jump(0x4c,'frame'); label('interrupt'); emit(0x40)
label('colors'); emit(*([0x0f,0x30,0x21,0x11]*4+[0x0f,0x21,0x30,0x11]*4))
for pos,name,relative in fixups:
    address=labels[name]
    if relative:
        delta=address-(0x8000+pos+1); assert -128<=delta<=127
        code[pos]=delta&255
    else: code[pos:pos+2]=struct.pack('<H',address)
prg=bytearray(16384); prg[:len(code)]=code
prg[-6:]=struct.pack('<HHH',labels['interrupt'],labels['reset'],labels['interrupt'])
chr_rom=bytearray(8192)
font={'A':[2,5,7,5,5],'B':[6,5,6,5,6],'C':[7,4,4,4,7],
      'D':[6,5,5,5,6],'E':[7,4,6,4,7],'I':[7,2,2,2,7],
      'L':[4,4,4,4,7],'M':[5,7,7,5,5],'N':[5,7,7,7,5],
      'O':[7,5,5,5,7],'P':[6,5,6,4,4],'R':[6,5,6,5,5],
      'S':[7,4,7,1,7],'T':[7,2,2,2,2],'U':[5,5,5,5,7],
      'V':[5,5,5,5,2],'Y':[5,5,2,2,2],'Z':[7,1,2,4,7]}
for c,rows in font.items(): chr_rom[ord(c)*16+1:ord(c)*16+6]=bytes(v<<3 for v in rows)
chr_rom[16:24]=bytes((0x3c,0x7e,0xff,0xdb,0xff,0xa5,0x7e,0x3c))
rom=b'NES\x1a'+bytes((1,1))+bytes(10)+prg+chr_rom
assert len(rom)==24592
out=root/'sdcard'/'roms'/'nes'/'SZPI-Controls.nes'; out.write_bytes(rom)
# 由 ESP32 首次挂载成功后创建文件；指定初始化器省略全零区域，便于查看内容。
target=root/'components'/'retro-go'/'targets'/'szpi-esp32s3'/'szpi_demo.c'
rows=[f'    [{i}] = 0x{v:02x},' for i,v in enumerate(rom) if v]
target.write_text('// 自编 NROM 测试程序；由 tools/make_nes_demo.py 生成，零填充项已省略。\n'
                  '#include <stddef.h>\n#include <stdint.h>\n'
                  'const uint8_t szpi_demo_rom[24592] = {\n'+'\n'.join(rows)+'\n};\n'
                  'const size_t szpi_demo_rom_size = sizeof(szpi_demo_rom);\n',encoding='utf-8')
print(f'{out}: {len(rom)} bytes, {len(code)} bytes of 6502 program/data')
