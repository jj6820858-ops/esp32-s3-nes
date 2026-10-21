"""通过已连接的 ESP32-S3 读回 Flash；分块重试，不写入设备。"""
from pathlib import Path
import hashlib
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
target=root/'backups'/'before-nes-20261021.bin'
if target.exists():
    # 这是本次移植前的原固件快照；后续运行不能用新固件覆盖它。
    digest=hashlib.sha256(target.read_bytes()).hexdigest()
    assert target.stat().st_size==16*1024*1024 and digest=='14650427d1024c527258c02c85aaf5afe1e83cb4ad41bf4b6490445f3e38d6ff'
    print('Original firmware backup already exists and hash is valid; kept unchanged.')
    raise SystemExit(0)
parts=root/'backups'/'flash-parts'; parts.mkdir(parents=True,exist_ok=True)
size=16*1024*1024; block=1024*1024
for index,start in enumerate(range(0,size,block)):
    out=parts/f'{start:08x}.bin'
    if out.exists() and out.stat().st_size==block:
        print(f'backup {index+1}/16 already read',flush=True); continue
    for retry in range(3):
        args=[sys.executable,'-m','esptool','--chip','esp32s3','-p','COM4','-b','460800',
              'read_flash',hex(start),hex(block),str(out)]
        result=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (root/'logs'/f'backup-{index:02d}-{retry}.log').write_bytes(result.stdout)
        if result.returncode==0 and out.exists() and out.stat().st_size==block: break
        print(f'backup {index+1}/16 retry {retry+1}',flush=True)
    else: raise RuntimeError(f'Flash read failed at {start:#x}')
    print(f'backup {index+1}/16 complete',flush=True)
with target.open('wb') as stream:
    for start in range(0,size,block): stream.write((parts/f'{start:08x}.bin').read_bytes())
print(f'Flash backup ready: {target.stat().st_size} bytes',flush=True)
print('SHA256',hashlib.sha256(target.read_bytes()).hexdigest(),flush=True)
