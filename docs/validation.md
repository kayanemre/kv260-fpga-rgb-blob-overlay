# Genel blob sürümü — doğrulama

Güncelleme: 2026-09-17T07:37:42+03:00

| Aşama | Sonuç | Kanıt |
|---|---|---|
| Genel RGB-komşuluk RTL XSim | PASS | `build/blob_sim.log` |
| PS derleme / UDP / dönüşüm / blob testleri | PASS | `build/blob_software.log` |
| Vivado sentez, route, timing, bitstream, XSA | COMPLETE | `build/fpga_console.log` |
| Linux paketi | HAZIR | `build/package/kv260-blob-overlay/` |
| Kart ve canlı alım | Slot 0, operating; sentetik RTL fark/kutu testi PASS; canli PC RX 26.82 FPS | `build/blob_deployment.json`, `build/blob_live_receiver.log` |

RTL: 21 açık beklenen değer, 2707 aktarım, 57 kare, 820 stalled çevrim;
satır/kare sınırları, reset, TLAST/TUSER ve çevrim başına bir piksel kontrol edildi.
PS blob testi: keyfi cyan/mor/gri/siyah bölgeler, ayrı aynı-renk nesneler,
temas eden farklı bölgeler, arka plan reddi ve orijinal görüntünün korunması.

FPS hedefi 30'dur. Canlı ölçüm yokken hedef elde edilmiş sayılmaz. FPGA bölümü
komşu RGB farkıdır; blob birleştirme ve kutular PS'dedir. Önceki kırmızı maske
sürümü ve kanıtları `build/red_v1/` altında korunur.
