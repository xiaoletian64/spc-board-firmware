# phone-hand 固件（M5Stack AtomS3U）

M5Stack AtomS3U（ESP32-S3FN8，8MB Flash，无 PSRAM）上的语音遥控器固件：
USB 串口控制 + LED + PDM 麦克风最小原型，外加 **WiFi STA + HTTPS OTA +
USB 串口透传升级** 三条升级通道与双 OTA 分区 + 启动回滚。

## 硬件

| 项目 | 参数 |
| --- | --- |
| 板子 | M5Stack AtomS3U（ESP32-S3FN8，8MB Flash，无 PSRAM） |
| 控制台 | USB-Serial-JTAG（USB-A 口即板载串口） |
| 按键 | BOOT 按键 GPIO41 |
| PDM 麦克风 | CLK GPIO38 / DATA GPIO39 |
| RGB LED | GPIO35 |

## 烧录（首次）

用 [merged bin](../../releases) 一步烧录（0x0 起含 bootloader+分区表+应用）：

```bash
pip install esptool
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset \
  write_flash 0x0 phone-hand-merged.bin
```

从源码构建：

```bash
# 需要 ESP-IDF v5.4.x
idf.py -DIDF_TARGET=esp32s3 -B build build
idf.py -p /dev/ttyACM0 -b 460800 flash monitor
```

也可以直接用 GitHub Actions 的产物（每次构建都会上传 `bootloader.bin`、
`partition-table.bin`、`ota_data_initial.bin`、`phone-hand.bin` 和
`phone-hand-merged.bin`）。

## OTA 升级

### 1. HTTPS OTA（需 WiFi）

分区表为双 OTA 槽（`ota_0` / `ota_1` 各 0x3F0000），带 bootloader 回滚：
新固件启动后 90 秒内健康运行才标记有效，否则 bootloader 自动回退旧槽。

OTA 服务器需提供（URL 在 `main/config.h.example` 里配置，或改用下面的
串口命令在运行时下发 WiFi）：

```
GET  {OTA_URL}/manifest      -> "PCOTA1\n<size>\n<sha256-hex>\n<version>\n"
GET  {OTA_URL}/<version>.bin -> 原始应用镜像，支持 Range 断点续传
POST {OTA_URL}/report        -> {"version":..,"slot":..,"phase":..,"imageState":..}（可选）
```

下载过程做 SHA256 校验；写入由 `ota_guard` 互斥保护（WiFi OTA 与 USB
透传 OTA 不会同时写 flash）。板子串口命令：

```
WIFI\t<ssid>\t<pass>    保存到 NVS 并连接 STA
WIFIOFF                 断开
OTA                     连接 WiFi 后启动 HTTPS OTA
OTA-STATUS              查询阶段/进度/当前版本
OTA-CONFIRM             手动确认当前槽
OTA-CANCEL              查询状态（取消见 TODO）
```

### 2. USB 串口透传升级（无 WiFi 兜底）

任何串口工具即可升级，无需网络：

```
OTA\t<size>\t<sha256hex>\n   板子回 OTA-READY
<随后发 size 字节原始固件>     板子边收边写 ota 槽并计算 SHA256
                             成功回 "OTA-OK <ver>"，失败回 "OTA-FAIL <原因>"
```

30 秒无数据自动中止。参考脚本 `tools/usb_ota.py`（本仓未含，TODO）。

## 项目结构

```
main/config.h.example  可选编译期配置（WiFi/OTA 服务器），真实 config.h 不入库
main/wifi.c            WiFi STA（NVS 凭据 + 事件处理）
main/ota.cpp           HTTPS OTA：manifest + SHA256 + 断点续传 + 回报
main/ota_guard.c       OTA 互斥保护（移植自水墨屏版固件）
main/usb_ota.c         USB 串口透传升级
main/link.c            串口命令路由
partitions.csv         双 OTA 槽分区表
```

## CI

GitHub Actions 用官方 `espressif/idf:v5.4.2` 镜像构建，校验版本号写入
`esp_app_desc_t`、镜像不超 OTA 槽容量，打 tag 时自动创建 Release 并附
merged bin 与 OTA bin。

## License

MIT
