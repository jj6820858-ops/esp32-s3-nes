"""读取板端 UART 日志，可选择复位或发送一个调试命令；不格式化 TF 卡。"""
import argparse
from pathlib import Path
import serial
import time
import sys

# Windows 控制台可能默认 GBK；复位时的无效 UART 字节不应让日志工具退出。
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

parser=argparse.ArgumentParser()
parser.add_argument('--port',default='COM5')
parser.add_argument('--seconds',type=float,default=15)
parser.add_argument('--reset',action='store_true')
parser.add_argument('--send',default='')
parser.add_argument('--output',default='logs/serial-probe.log')
args=parser.parse_args()
link=serial.Serial(port=None,baudrate=115200,timeout=0.1)
link.dtr=False; link.rts=False; link.port=args.port; link.open()
if args.reset:
    link.rts=True; time.sleep(0.15); link.rts=False
if args.send: link.write(args.send.encode('ascii')); link.flush()
start=time.monotonic(); data=bytearray()
try:
    while time.monotonic()-start<args.seconds:
        data.extend(link.read(max(1,link.in_waiting)))
finally:
    link.close()
path=Path(args.output); path.parent.mkdir(parents=True,exist_ok=True)
path.write_bytes(data)
print(data.decode('utf-8',errors='replace'))
