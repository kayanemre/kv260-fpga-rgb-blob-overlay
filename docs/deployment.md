# Genel blob sürümü — kart yükleme / 2026-09-17

Yeni firmware: `/lib/firmware/xilinx/kv260-blob-overlay/`.
KV260 (`ubuntu@10.42.0.12`) slot 0; FPGA manager `operating`.
ARM64 program: `/home/ubuntu/kv260_blob_overlay/camera_udp`.

## Doğrulanan davranış

Elle yazılmış RTL her pikselin sol/üst komşusuyla RGB farkını hesaplar. PS bu
farklarla benzer görünümlü bağlı bölgeleri birleştirir. Canlı `--primary-only`
ayarında yalnız belirgin kırmızı, yeşil ve mavi bölgeler tutulur; orijinal kamera
görüntüsüne sınıf renginde kutular çizilip UDP ile PC'ye gönderilir.
Ana kanal en az 65 olmalı; kırmızı için yeşilin en az 2 katı, diğer kanal
karşılaştırmalarında yaklaşık 1,5 kat baskınlık aranır. Böylece karışık ve soluk
tonlar daha güçlü biçimde elenir.

- XSim: 2707 aktarım, 57 kare; reset, TLAST/TUSER, backpressure ve tam hız PASS.
- PS/PC: 8 Python testi, piksel dönüşümü, gerçek UDP loopback ve 5 blob senaryosu PASS.
- Kartta 10 sentetik kare: tüm piksel farkları ve beş blob koordinatı PASS.
- PC sentetik alımında 9 tam / 1 eksik kare; kaydedilen tam kare bağımsız beklenen
  görüntüyle birebir aynı (0 farklı kanal değeri). Eksik kare gösterilmez.
- Gerçek UVC kamera: 640×480 YUYV BT.601 limited. İstenen ve sürücünün bildirdiği
  hız 30 FPS; uçtan uca ölçülen canlı alım/gösterim yaklaşık **26–27 FPS**.
  30 FPS hedefi henüz elde edilmiş sayılmaz. Son 30 ölçüm aralığında ortalama 26.82 FPS; 810 tam, 0 yeni eksik kare; gönderici kuyruk kaybı 0.
- Canlı pencere: `KV260 blob overlay`. Ekran kanıtı: `build/blob_live_window.png`.
  İlk alınan orijinal görüntü + kutular: `build/blob_live_frame.png`.

## Çalışan süreçler ve ağ

Canlı ışık/gölge toleransı `52/36`: renk toleransı 36→52, komşu sınırı eşiği
24→36. `run_live.sh` bu ayarı uygular; minimum alan 200 piksel olarak kalır.
RTL ve bitstream değişmez. Uygulama kaydı: `build/blob_tolerance_update.log`.

Kart PID/log: `/tmp/kv260_blob_live.pid`, `/tmp/kv260_blob_live.log`.
PC PID/log: `build/live_receiver.pid`, `build/blob_live_receiver.log`.
Son kart ölçümü: `build/blob_live_board.log`; özet: `build/blob_deployment.json`.

PC UFW kuralı yalnız `eno1`, kaynak `10.42.0.12`, hedef `10.42.0.1:5000/udp`.
Alıcı 8 MiB tampon istese de bu bilgisayarda SO_RCVBUF 425984 bayt ile sınırlı.
Canlı ölçüm sırasında aralıklı eksik kare görüldü; UDP kayıpsız taşıma sağlamaz.
Son gönderici 16 paketlik gruplar arasında 300 µs bekler. Ağ işçisi ile kamera/PL
ayrı çalışır; bir bekleyen kare sınırı gecikmenin büyümesini önler. PC alımı GUI'den ayrıdır.

## Yeniden başlatma

PC:

```bash
python3 pc/udp_receiver.py --bind 10.42.0.1 --source 10.42.0.12 --port 5000
```

Kartta aynı DMA'yı kullanan mevcut göndericiyi önce SIGTERM ile durdurun:

```bash
sudo kill -TERM "$(cat /tmp/kv260_blob_live.pid)"
cd /home/ubuntu/kv260_blob_overlay
sudo ./run_live.sh --ip 10.42.0.1 --dma-base 0xa0000000 --device /dev/video0 --fps 30
```

## Korunan sürüm ve platform notları

Önceki kırmızı maske firmware'i `kv260-color-detector` adıyla korunur;
eski kaynak/paket/kanıtlar `build/red_v1/` içindedir. İki sürümün RTL/PS veri
sözleşmeleri farklıdır. Boot ayarları değiştirilmedi.

XRT 2.13.479 için zocl aygıt düğümü **`zyxclmm_drm`** olmalıdır.
İlk eski sürüm yüklemesinde farklı düğüm adı Xorg/systemd-logind kernel Oops
oluşturmuştu; ad düzeltildi, yeniden yükleme ve gerçek DMA testleri geçti.
Eski hata kaydı `build/board_load.log` içinde; kart bu süreçte yeniden başlatılmadı.
