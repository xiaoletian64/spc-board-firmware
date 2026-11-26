# P4 板刷机交接手册（p4 hub / idf-p4-wifi6 线）

> 2026-11-26 由宿主手机刷机站研究产出。目标芯片实测：**ESP32-P4 (eco7, ROM 2026-01-09)**，
> flash **32MB GD25Q256 (c8:4019)**，40MHz 晶振，MAC `e8:f6:0a:ee:b9:f2`。

## 1. 板子接口（本板实测）

- 双 Type-C：**UART 口 = CH343 串口桥 (VID:PID 1A86:55D3, CDC-ACM)**，刷机+供电用这个
- **OTG 口 = P4 原生 USB 2.0 HS**（USB-Serial-JTAG 与 OTG 共享；旧固件占用时口上无枚举）
- 本板 **RTS→EN 自动复位电路通，DTR→下载 strap(GPIO35) 未接** → 进下载模式必须手动按 BOOT

## 2. 进下载模式（本板必须）

1. 按住丝印 **BOOT** 键（P4 strap 脚 = GPIO35，不是 IO0）
2. 点一下 **RST**（或按住 BOOT 重新插 USB）
3. 松开 BOOT → 芯片停在 ROM 下载模式等待主机

（带完整自动下载电路的板子可免按键；本板不行，实测 DTR 拉不出 strap。）

## 3. 烧录命令（权威，来自 CI flash_args，esptool ≥5.x）

```bash
python -m esptool --chip esp32p4 -b 460800 \
  --before default-reset --after hard-reset write-flash \
  --flash-mode dio --flash-size 32MB --flash-freq 80m \
  0x2000  build/bootloader/bootloader.bin \
  0x8000  build/partition_table/partition-table.bin \
  0xd000  build/ota_data_initial.bin \
  0x20000 build/phone-hand.bin
```

或者 CI 产出的合并镜像（一步到位）：

```bash
python -m esptool --chip esp32p4 -b 460800 \
  --before default-reset --after hard-reset write-flash \
  0x2000 p4-merged.bin
```

macOS/Windows 把端口换成 /dev/ttyACM0 / COMx；按住 BOOT 后再回车。

## 4. 取产物

Actions → p4phonehub-build（main 最新绿勾 run）→ Artifacts → `p4wifi6-bin`
（含 bootloader / partition-table / ota_data_initial / phone-hand / p4-merged）。
注意 retention 7 天，过期就重跑一次 workflow。

## 5. 验证

```bash
# 读回校验（抽样）
python -m esptool --chip esp32p4 -b 460800 --before default-reset read-flash 0x20000 0x1000 app_check.bin
# 串口监视（115200）
python -m esptool --chip esp32p4 read-flash 0 0 && python -m serial.tools.miniterm /dev/ttyACM0 115200
```

上电应看到第二阶段 bootloader 横幅（`ESP-ROM:esp32p4-eco7-20260109` → load:… → entry）。

## 6. 刷完之后（免按键路线）

- 新固件保留 USB-Serial-JTAG → 后续经 OTG 口 `esptool --before usb-reset` **免按键**重刷
- 固件将加「重启进下载模式」命令（BLE/网络触发）+ WiFi/BLE OTA → 按键永久退役

## 7. 已知坑（实测记录）

- 每次重新打开串口（如桥接 App 会话重建）都可能瞬时抖动 EN 复位芯片 → BOOT 松手时机要在烧录开始之后
- CH343 串口下 esptool 标准复位序列会报 `Wrong boot mode (0x30f SPI_FAST_FLASH_BOOT)` = 芯片没在下载模式，不是链路问题
- P4 的 esptool 支持 ≥ v5（ROM 2026-01 eco7 实测 v5.7 通过）
