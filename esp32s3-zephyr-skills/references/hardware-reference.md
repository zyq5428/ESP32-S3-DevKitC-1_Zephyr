# ESP32-S3-DevKitC-1 v1.1 硬件参考手册

> 本文件是 esp32s3-zephyr-skills 技能的硬件知识库。
> 包含开发板 ESP32-S3-DevKitC-1 v1.1 (N32R16V) 的全部硬件信息。

---

## 1. 模组规格 (ESP32-S3-WROOM-2-N32R16V)

| 参数 | 值 |
|------|-----|
| 芯片 | ESP32-S3 (Xtensa LX7 双核 @ 240MHz) |
| Flash | 32MB Octal SPI @ 1.8V |
| PSRAM | 16MB Octal SPI @ 1.8V |
| SPI 电压 | **1.8V** (与 WROOM-1 系列 3.3V 不兼容) |
| Wi-Fi | 2.4GHz 802.11 b/g/n |
| BLE | Bluetooth 5.0 LE |
| GPIO 总数 | 45 个 (但 GPIO35/36/37 被 Flash/PSRAM 占用) |
| 天线 | 板载 PCB 天线 |
| 尺寸 | 67.6mm × 28.0mm |

---

## 2. 完整引脚表 (J1 左排, 22 脚)

| Pin | 名称 | 类型 | 复用功能 (AF) | Zephyr 设备树标识 | 备注 |
|-----|------|------|---------------|-------------------|------|
| 1 | **3V3** | P | - | - | 3.3V 输出 |
| 2 | **3V3** | P | - | - | 3.3V 输出 |
| 3 | **RST** | I | EN | - | 复位 (低电平复位) |
| 4 | **GPIO4** | I/O/T | RTC_GPIO4, TOUCH4, ADC1_CH3 | GPIO4 | I2C1 SDA 默认 |
| 5 | **GPIO5** | I/O/T | RTC_GPIO5, TOUCH5, ADC1_CH4 | GPIO5 | I2C1 SCL 默认 |
| 6 | **GPIO6** | I/O/T | RTC_GPIO6, TOUCH6, ADC1_CH5 | GPIO6 | I2S1 O_SD 默认 |
| 7 | **GPIO7** | I/O/T | RTC_GPIO7, TOUCH7, ADC1_CH6 | GPIO7 | |
| 8 | **GPIO15** | I/O/T | RTC_GPIO15, U0RTS, ADC2_CH4, XTAL_32K_P | GPIO15 | 32KHz 晶振 |
| 9 | **GPIO16** | I/O/T | RTC_GPIO16, U0CTS, ADC2_CH5, XTAL_32K_N | GPIO16 | 32KHz 晶振 |
| 10 | **GPIO17** | I/O/T | RTC_GPIO17, U1TXD, ADC2_CH6 | GPIO17 | UART1 TX 默认 |
| 11 | **GPIO18** | I/O/T | RTC_GPIO18, U1RXD, ADC2_CH7, CLK_OUT3 | GPIO18 | UART1 RX 默认 |
| 12 | **GPIO8** | I/O/T | RTC_GPIO8, TOUCH8, ADC1_CH7, SUBSPICS1 | GPIO8 | |
| 13 | **GPIO3** | I/O/T | RTC_GPIO3, TOUCH3, ADC1_CH2 | GPIO3 | |
| 14 | **GPIO46** | I/O/T | 仅 GPIO | GPIO46 | 无触摸/ADC |
| 15 | **GPIO9** | I/O/T | RTC_GPIO9, TOUCH9, ADC1_CH9, FSPIHD | GPIO9 | |
| 16 | **GPIO10** | I/O/T | RTC_GPIO10, TOUCH10, ADC1_CH9, FSPICS0 | GPIO10 | SPI2 CS 默认 |
| 17 | **GPIO11** | I/O/T | RTC_GPIO11, TOUCH11, ADC2_CH0, FSPID | GPIO11 | SPI2 MOSI 默认 |
| 18 | **GPIO12** | I/O/T | RTC_GPIO12, TOUCH12, ADC2_CH1, FSPICLK | GPIO12 | SPI2 SCLK 默认 |
| 19 | **GPIO13** | I/O/T | RTC_GPIO13, TOUCH13, ADC2_CH2, FSPIQ | GPIO13 | SPI2 MISO 默认 |
| 20 | **GPIO14** | I/O/T | RTC_GPIO14, TOUCH14, ADC2_CH3, FSPIWP | GPIO14 | |
| 21 | **5V** | P | - | - | 5V 输入 |
| 22 | **G** | G | - | - | 接地 |

---

## 3. 完整引脚表 (J3 右排, 22 脚)

| Pin | 名称 | 类型 | 复用功能 (AF) | Zephyr 设备树标识 | 备注 |
|-----|------|------|---------------|-------------------|------|
| 1 | **G** | G | - | - | 接地 |
| 2 | **TX (GPIO43)** | I/O/T | U0TXD, CLK_OUT1 | GPIO43 | 串口0 默认 TX |
| 3 | **RX (GPIO44)** | I/O/T | U0RXD, CLK_OUT2 | GPIO44 | 串口0 默认 RX |
| 4 | **GPIO1** | I/O/T | RTC_GPIO1, TOUCH1, ADC1_CH0 | GPIO1 | I2C0 SDA 默认 |
| 5 | **GPIO2** | I/O/T | RTC_GPIO2, TOUCH2, ADC1_CH1 | GPIO2 | I2C0 SCL 默认 |
| 6 | **GPIO42** | I/O/T | MTMS (JTAG TMS) | GPIO42 | JTAG 接口 |
| 7 | **GPIO41** | I/O/T | MTDI (JTAG TDI), CLK_OUT1 | GPIO41 | JTAG 接口 |
| 8 | **GPIO40** | I/O/T | MTDO (JTAG TDO), CLK_OUT2 | GPIO40 | JTAG 接口 |
| 9 | **GPIO39** | I/O/T | MTCK (JTAG TCK), CLK_OUT3 | GPIO39 | SPIM3 MOSI 默认 |
| 10 | **GPIO38** | I/O/T | FSPIWP, SUBSPIWP | GPIO38 | **RGB LED (v1.1)** |
| 11 | **GPIO37** ⛔ | I/O/T | SPIDQS, FSPIQ | - | **Octal PSRAM 占用，禁止外部使用** |
| 12 | **GPIO36** ⛔ | I/O/T | SPIIO7, FSPICLK | - | **Octal PSRAM 占用，禁止外部使用** |
| 13 | **GPIO35** ⛔ | I/O/T | SPIIO6, FSPID | - | **Octal Flash 占用，禁止外部使用** |
| 14 | **GPIO0** | I/O/T | RTC_GPIO0, Strapping Pin | GPIO0 | **Boot 按钮**(低电平=下载模式) |
| 15 | **GPIO45** | I/O/T | 仅 GPIO | GPIO45 | 无触摸/ADC |
| 16 | **GPIO48** | I/O/T | SPICLK_N, SUBSPICLK_N_DIFF | GPIO48 | 可用(仅 v1.0 的 RGB LED) |
| 17 | **GPIO47** | I/O/T | SPICLK_P, SUBSPICLK_P_DIFF | GPIO47 | |
| 18 | **GPIO21** | I/O/T | RTC_GPIO21 | GPIO21 | |
| 19 | **GPIO20** | I/O/T | U1CTS, ADC2_CH9, CLK_OUT1, **USB_D+** | GPIO20 | USB OTG 占用 |
| 20 | **GPIO19** | I/O/T | U1RTS, ADC2_CH8, CLK_OUT2, **USB_D-** | GPIO19 | USB OTG 占用 |
| 21 | **G** | G | - | - | 接地 |
| 22 | **G** | G | - | - | 接地 |

类型缩写: P=电源, I=输入, O=输出, T=高阻态
⛔ = 绝对不可用

---

## 4. 板上组件映射

| 组件 | 连接引脚 | Zephyr 设备树标识 | 备注 |
|------|----------|-------------------|------|
| **RGB LED (WS2812)** | GPIO38 | `&led_strip` (自定义) | v1.1 版本 |
| **Boot 按钮** | GPIO0 | `&button0` | 低电平触发，内部上拉 |
| **USB 转 UART** | TX=GPIO43, RX=GPIO44 | `&uart0` | 通过 CP2102 等桥接芯片 |
| **USB OTG (Type-C)** | D-=GPIO19, D+=GPIO20 | `&usb_otg` | ESP32-S3 原生 USB |
| **JTAG 调试口** | TMS=42, TDI=41, TDO=40, TCK=39 | `&gpio0` (bitbang) | 通过 USB OTG 也可 JTAG |

---

## 5. 禁用引脚详细说明

### GPIO35/36/37 — 绝对不可外部使用

在 ESP32-S3-WROOM-2 (Octal SPI Flash + PSRAM) 模组上，这三个引脚用于芯片与 Octal SPI Flash/PSRAM 的内部高速通信：

- **GPIO35 (SPIIO6)** → Octal SPI 数据线 6
- **GPIO36 (SPIIO7)** → Octal SPI 数据线 7
- **GPIO37 (SPIDQS)** → Octal SPI DQS 信号

**后果：**
- 外部使用会导致 Flash/PSRAM 读写失败
- 可能导致系统崩溃、数据损坏
- 即使在设备树中定义这些引脚，Zephyr 的 pinctrl 驱动也会忽略或报错

**排查方法：** 如果程序在读写 Flash/PSRAM 时崩溃，首先检查是否误用了 GPIO35/36/37。

---

## 6. GPIO38 — 与 RGB LED 共享

v1.1 版本的 RGB LED 连接在 GPIO38：

- **使用 RGB LED 时**：GPIO38 被 WS2812 驱动占用，不可做他用
- **不使用 RGB LED 时**：GPIO38 可作普通 GPIO，但板上的 LED 可能会微亮或闪烁
- **配置方法**：通过设备树 overlay 禁用 RGB LED 节点即可释放该引脚

---

## 7. 供电说明

| 供电方式 | 电压 | 接口 | 最大电流 |
|----------|------|------|----------|
| Micro-USB (UART) | 5V → LDO → 3.3V | J1 左侧 | 500mA |
| USB Type-C (OTG) | 5V → LDO → 3.3V | J3 右侧 | 500mA |
| 5V + GND 排针 | 5V | J1 Pin21/22 | 500mA |
| 3V3 + GND 排针 | 3.3V | J1 Pin1/22 | 500mA |

> ⚠️ 四种供电方式**不可同时使用**！

---

## 8. 模组订购代码解码

以 **ESP32-S3-WROOM-2-N32R16V** 为例：

| 字段 | 含义 | 值 |
|------|------|-----|
| ESP32-S3 | 芯片系列 | ESP32-S3 |
| WROOM | 模组封装类型 | WROOM (PCB 天线) |
| 2 | 模组版本 | WROOM-2 (1.8V SPI) |
| N | Flash 类型 | Octal SPI |
| 32 | Flash 容量 | 32MB |
| R | PSRAM 类型 | Octal SPI |
| 16 | PSRAM 容量 | 16MB |
| V | SPI 电压 | 1.8V |

**对应的 Zephyr 设备树文件：** `espressif/esp32s3/esp32s3_wroom_n32r16.dtsi`

---

## 9. v1.0 与 v1.1 差异

| 项目 | v1.0 (旧版) | v1.1 (当前) |
|------|------------|------------|
| RGB LED GPIO | GPIO48 | **GPIO38** |
| USB OTG LDO | 无（与主 LDO 共享） | 独立 LDO |
| 默认板载模组 | WROOM-1-N8 | 多种可选 |

**Zephyr 默认板级文件均基于 WROOM-1-N8，使用 N32R16V 时必须 overlay。**
