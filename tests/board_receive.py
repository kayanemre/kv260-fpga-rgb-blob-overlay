#!/usr/bin/env python3
"""Receive real board frames; exact synthetic comparison or red/black validity."""
import argparse, importlib.util, json, socket, time
from pathlib import Path
import numpy as np
import cv2
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('rx',root/'pc/udp_receiver.py')
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
p=argparse.ArgumentParser()
p.add_argument('--synthetic',action='store_true')
p.add_argument('--frames',type=int,default=10)
p.add_argument('--port',type=int,default=5000)
a=p.parse_args()
expected=b''.join(bytes((255,0,0)) if x*6//640<2 else bytes(3) for x in range(640))*480
assembler=r.Reassembler()
first=last=None
packets_seen=0
red_counts=[]
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
 sock.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,4*1024*1024)
 sock.bind(('10.42.0.1',a.port));sock.settimeout(1)
 print('BOARD_RECEIVER_READY',flush=True)
 end=time.monotonic()+60
 while assembler.completed<a.frames and time.monotonic()<end:
  try:data,peer=sock.recvfrom(65535)
  except socket.timeout:continue
  packets_seen+=1
  if packets_seen==1:print('FIRST_PACKET',peer,len(data),flush=True)
  if peer[0]!='10.42.0.12':continue
  result=assembler.feed(data)
  if result:
   now=time.monotonic();first=now if first is None else first;last=now
   raw=result[1]
   if a.synthetic:assert raw==expected,'PL synthetic pixel comparison failed'
   rgb=np.frombuffer(raw,dtype=np.uint8).reshape(480,640,3)
   assert np.all(rgb[:,:,1:]==0) and np.all((rgb[:,:,0]==0)|(rgb[:,:,0]==255)),'unexpected non-red/black output'
   red_counts.append(int(np.count_nonzero(rgb[:,:,0])))
   if assembler.completed==1:cv2.imwrite(str(root/'build'/('board_pattern.png' if a.synthetic else 'board_camera.png')),cv2.cvtColor(rgb,cv2.COLOR_RGB2BGR))
 print('RX_STATS',packets_seen,assembler.completed,assembler.dropped,assembler.invalid,flush=True)
 assert assembler.completed==a.frames,f'only {assembler.completed}/{a.frames} complete frames'
 result={'mode':'synthetic' if a.synthetic else 'camera','frames':assembler.completed,'received_fps':(assembler.completed-1)/(last-first) if last>first else 0,'incomplete':assembler.dropped,'invalid':assembler.invalid,'red_pixels_min':min(red_counts),'red_pixels_max':max(red_counts)}
 print(json.dumps(result),flush=True)
 (root/'build'/('board_pattern.json' if a.synthetic else 'board_camera.json')).write_text(json.dumps(result,indent=2)+'\n')
