# Vivado 2022.2

`create_bd.tcl`, `COLOR_BUILD_DIR` altında yeni `color_detector.xpr` projesini oluşturur. Bu makinede blob sürümü `/mnt/data/kria_build/kv260_color_overlay_20260917` altında derlenir.
Parça `xck26-sfvc784-2LV-c`. PS DDR/USB/Ethernet MIO ayarları yerel KV260
projesinden alınmış `ps_preset.tcl` içindedir; eski projeyi açıp değiştirmez.
Bu preset dosyası donanım parametrelerinin snapshot'ıdır; detector HLS içermez.

## Bağlantılar

| Kaynak | Hedef | İşlev |
|---|---|---|
| PS M_AXI_HPM0_FPD | control SmartConnect → DMA S_AXI_LITE | DMA yazmaç erişimi |
| DMA M_AXI_MM2S ve M_AXI_S2MM | memory SmartConnect → PS S_AXI_HP0_FPD | DDR okuma/yazma |
| DMA M_AXIS_MM2S | detector S_AXIS | 32 bit R,G,B,0 |
| detector M_AXIS | DMA S_AXIS_S2MM | 32 bit: left_delta, up_delta, 0, 0 |
| PS pl_clk0 | DMA, detector, SmartConnect, ilgili PS AXI saat girişleri | Tek 100 MHz alan |
| PS pl_resetn0 | proc_sys_reset ext_reset_in | Aktif düşük dış reset |
| proc_sys_reset peripheral_aresetn | DMA ve SmartConnect resetleri | Senkron bırakılan reset |
| DMA mm2s_prmry_reset_out_n | RGB komşuluk çekirdeği aresetn | Yazılım DMA resetinde bekleyen pikseli temizleme |
| DMA mm2s/s2mm_introut | xlconcat → PS pl_ps_irq0 | İsteğe bağlı interrupt altyapısı |

İlk C++ sürümü polling kullanır, DMA interrupt enable bitleri kapalıdır.
DMA simple mode: scatter-gather kapalı; iki yön açık; stream 32 bit,
DDR AXI 64 bit; length 23 bit; adres 32 bit. DDR düşük 2 GiB eşlemesi kullanılır.
DMA kontrol adresi `0xA0000000`, aralık 64 KiB. Bir kare = bir DMA transferi.
Carrier fanının mevcut TTC bağlantısı ve A12 pin kısıtı korunmuştur.

## Arka plan derlemesi

Proje kökünde:

```bash
python3 scripts/background.py fpga
tail -f build/fpga_console.log
cat build/fpga.status
```

Script sırasıyla BD validation, sentez, route, timing/DRC kontrolü, bitstream ve
XSA export yapar. Çıktılar `build/color_system.bit` ve `build/color_system.xsa`.
Başarılı bitiş: `build/fpga.status` içinde `COMPLETE`.
Mevcut derleme dizininin üzerine otomatik yazmaz. Vivado'da proje açılıp tekrar
çalıştırılabilir; sıfırdan yeniden oluşturmak için eski `build/vivado` dizinini
başka bir ada taşıyın.

## Linux paketi

```bash
bash scripts/package.sh
```

`build/package/kv260-blob-overlay/` içinde `.bit.bin`, `.dtbo`, `shell.json`,
PS kaynağı, raporlar ve SHA256 listesi hazırlanır. Script karta yükleme yapmaz.
Overlay Xilinx Ubuntu `fpga_full`, `amba`, `zynqmp_clk`, `zynqmp_reset`, `gic`
sembollerini ve `zocl` desteğini bekler. HP0 AFI genişliği 64 bit, HPM0 32 bit,
PL0 clock 100 MHz olarak ayarlanır. Kart imajıyla bu gereksinimler doğrulanmalıdır.
DMA node'u özellikle eklenmemiştir: uygulama register'ları doğrudan yönetir.

Mevcut FPGA uygulaması durdurulup unload edilmeden bu overlay yüklenmemelidir;
iki bağımsız full-PL tasarım aynı anda çalışamaz. Mevcut boot dosyaları değiştirilmez.
Kria erişimi yokken paket üretmek, kartta yüklendiği/çalıştığı anlamına gelmez.

## Kart üzerinde doğrulanan XRT düğüm adı

Karttaki XRT 2.13.479 sürücüsü `zocl_get_zdev()` içinde sabit
`zyxclmm_drm` adını arar. Overlay bu adı korumalıdır; yalnız `compatible`
alânının doğru olması yetmez. İlk yüklemede farklı ad kernel Oops oluşturdu;
overlay adı düzeltilip yeniden yüklendi.
Kaynak: [XRT 2022.1 zocl_drv.h](https://github.com/Xilinx/XRT/blob/2022.1/src/runtime_src/core/edge/drm/zocl/include/zocl_drv.h).

XSA export managed implementation run içindeki bitstream'i bekler. Bu nedenle
`create_bd.tcl`, timing/DRC geçince `launch_runs impl_1 -to_step write_bitstream`
çalıştırır. Mevcut routed projenin yalnız bu son aşamasını tekrar yapmak için
`vivado/finish_export.tcl` kullanılabilir.
