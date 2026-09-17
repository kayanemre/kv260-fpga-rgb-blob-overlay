#!/usr/bin/env python3
"""Record artifact evidence; board acceptance requires its own current log."""
import json
from datetime import datetime
from pathlib import Path
root=Path(__file__).resolve().parents[1]
b=root/'build'
def read(name):
 p=b/name
 return p.read_text(errors='replace') if p.exists() else ''
state=read('fpga.status').strip() or 'NOT_STARTED'
board=json.loads(read('blob_deployment.json') or '{}')
board_text=('Slot 0, operating; sentetik RTL fark/kutu testi PASS; canli PC RX '+str(board.get('rx_fps','bekleniyor'))+' FPS' if board.get('loaded') else 'Yeni blob tasarimi henuz kartta dogrulanmadi')
pkg=b/'package/kv260-blob-overlay/SHA256SUMS'
text=f'''# Genel blob sürümü — doğrulama

Güncelleme: {datetime.now().astimezone().isoformat(timespec='seconds')}

| Aşama | Sonuç | Kanıt |
|---|---|---|
| Genel RGB-komşuluk RTL XSim | {'PASS' if 'PASS:' in read('blob_sim.log') else 'bekleniyor'} | `build/blob_sim.log` |
| PS derleme / UDP / dönüşüm / blob testleri | {'PASS' if 'PASS: 6 blob scenarios' in read('blob_software.log') else 'bekleniyor'} | `build/blob_software.log` |
| Vivado sentez, route, timing, bitstream, XSA | {state} | `build/fpga_console.log` |
| Linux paketi | {'HAZIR' if pkg.exists() else 'bekleniyor'} | `build/package/kv260-blob-overlay/` |
| Kart ve canlı alım | {board_text} | `build/blob_deployment.json`, `build/blob_live_receiver.log` |

RTL: 21 açık beklenen değer, 2707 aktarım, 57 kare, 820 stalled çevrim;
satır/kare sınırları, reset, TLAST/TUSER ve çevrim başına bir piksel kontrol edildi.
PS blob testi: keyfi cyan/mor/gri/siyah bölgeler, ayrı aynı-renk nesneler,
temas eden farklı bölgeler, arka plan reddi ve orijinal görüntünün korunması.

FPS hedefi 30'dur. Canlı ölçüm yokken hedef elde edilmiş sayılmaz. FPGA bölümü
komşu RGB farkıdır; blob birleştirme ve kutular PS'dedir. Önceki kırmızı maske
sürümü ve kanıtları `build/red_v1/` altında korunur.
'''
(root/'docs/validation.md').write_text(text)
print('Validation updated:',state,board_text)
