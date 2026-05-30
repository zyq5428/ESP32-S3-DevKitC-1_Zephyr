# 构建、烧录与调试指南

> 适用于 ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS

---

## 1. 环境准备

### 前置条件

确保 Zephyr SDK 和 ESP32 工具链已正确安装：

```bash
# 验证 Zephyr SDK 是否安装
west --version

# 验证 ESP32 工具链 (espressif 工具链包含在 Zephyr SDK 中)
# 检查是否有 xtensa-esp32s3-elf-gcc
which xtensa-esp32s3-elf-gcc

# 验证 Python 依赖
pip show pyocd
```

### 激活 Zephyr 环境

```bash
# 进入项目根目录
cd E:/zephyrproject

# 激活 Python 虚拟环境 (如果使用了 .venv)
.\.venv\scripts\Activate.ps1
```

---

## 2. 构建命令

### 基本构建

```bash
# 首次构建 (指定板子和构建目录)
# -b: 板子目标 (esp32s3_devkitc/esp32s3_procpu)
# -d: 构建输出目录 (通常命名为 build)
west build -b esp32s3_devkitc/esp32s3_procpu -d build

# 如果代码有改动，只需重新构建 (不需要再指定 -b)
west build -d build

# 完全清理后重新构建
rm -rf build
# 或者
Remove-Item -Recurse -Force build

west build -b esp32s3_devkitc/esp32s3_procpu -d build
```

### 查看详细构建日志

```bash
# 显示详细的编译命令和警告
west build -b esp32s3_devkitc/esp32s3_procpu -d build -- -DCMAKE_VERBOSE_MAKEFILE=ON

# 或使用更简洁的方式
west build -d build -t verbose
```

### 构建并指定额外 Kconfig 选项

```bash
# 通过命令行临时覆盖 Kconfig 选项
west build -b esp32s3_devkitc/esp32s3_procpu -d build -- -DCONFIG_WIFI=y -DCONFIG_WIFI_ESP32=y
```

### 仅构建特定目标

```bash
# 只生成设备树预处理后的文件 (调试设备树问题很有用)
west build -d build -t devicetree

# 只生成 Kconfig 合并后的 .config 文件
west build -d build -t menuconfig

# 只编译不链接 (检查编译错误)
west build -d build -t compiler
```

---

## 3. 烧录命令

### 通过 USB 转 UART 烧录 (推荐)

```bash
# 使用 west flash 自动烧录
# 需要开发板连接到 USB 端口
west flash -d build

# 指定串口号 (如果自动检测失败)
west flash -d build --esp-device COM15
# 或者
west flash -d build --esp-device /dev/ttyUSB0
```

### 手动进入下载模式

如果自动烧录失败，手动进入下载模式：

1. 按住 **Boot** 按钮 (GPIO0 旁边的按钮)
2. 按一下 **Reset** 按钮 (EN)
3. 松开 **Reset** 按钮
4. 等待 1 秒
5. 松开 **Boot** 按钮
6. 现在开发板处于下载模式，执行 `west flash`

### 通过 USB OTG 烧录

```bash
# ESP32-S3 支持通过原生 USB OTG (Type-C) 直接烧录
# 使用 esptool.py 直接烧录
esptool.py --chip esp32s3 --port <端口> write_flash 0x0 build/zephyr/zephyr.bin
```

### 烧录后查看串口输出

```bash
# 使用 west espressif monitor 打开串口监视器
west espressif --monitor -d build

# 或使用其他串口工具
# miniterm.py (Python 自带)
python -m serial.tools.miniterm <端口> 115200

# screen (Linux/Mac)
screen /dev/ttyUSB0 115200

# PuTTY (Windows)
# 选择 Serial, 波特率 115200
```

---

## 4. 构建产物说明

构建完成后，在 `build/zephyr/` 目录下有以下关键文件：

| 文件 | 说明 |
|------|------|
| `zephyr.elf` | ELF 可执行文件 (含调试信息) |
| `zephyr.bin` | 二进制固件 |
| `zephyr.hex` | Intel HEX 格式固件 |
| `zephyr.map` | 内存映射文件 (分析 RAM/Flash 使用) |
| `.config` | 最终的 Kconfig 配置 |
| `zephyr.dts` | 预处理后的设备树文件 (调试设备树时查看) |
| `include/generated/devicetree_generated.h` | 设备树自动生成的头文件 |

---

## 5. 调试

### JTAG 调试 (通过 USB OTG)

ESP32-S3 支持通过内置 USB JTAG 调试：

```bash
# 使用 openocd 连接到开发板
openocd -f board/esp32s3-builtin.cfg

# 然后使用 GDB 连接
xtensa-esp32s3-elf-gdb build/zephyr/zephyr.elf
# (gdb) target remote :3333
# (gdb) monitor reset halt
# (gdb) continue
```

### 打印调试

```c
/* 基础打印 */
printk("调试: 变量 x = %d\n", x);

/* 使用日志系统 (推荐) */
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(my_app, LOG_LEVEL_DBG);  /* 注册日志模块 */
LOG_DBG("调试信息: %d", value);              /* 仅 LOG_LEVEL_DBG 时输出 */
LOG_INF("运行信息");
LOG_ERR("错误信息 (err=%d)", err);
LOG_WRN("警告信息");
```

### 检查 RAM/Flash 使用

```bash
# 查看内存使用摘要
west build -d build -t rom_report
# 或
west build -d build -t ram_report

# 详细内存映射
cat build/zephyr/zephyr.map | grep -E "^\s+0x"
```

---

## 6. 常见构建错误及解决

### 错误 1: "Board esp32s3_devkitc not found"

```
错误信息:
  Board esp32s3_devkitc not found

原因:
  Zephyr SDK 未正确安装或环境变量未设置

解决:
  export ZEPHYR_BASE=E:/zephyrproject/zephyr
  west update
```

### 错误 2: "error: 'GPIO35' undeclared"

```
错误信息:
  error: 'GPIO35' undeclared

原因:
  GPIO35/36/37 被 Octal SPI Flash/PSRAM 占用
  Zephyr pinctrl 驱动中没有这些引脚的 GPIO 定义

解决:
  使用其他可用引脚 (避开 GPIO35/36/37)
  参考 hardware-reference.md 中的可用引脚列表
```

### 错误 3: "section `.dram0.bss' will not fit"

```
错误信息:
  section `.dram0.bss' will not fit in region `dram0_0_seg'

原因:
  DRAM (内部 SRAM) 空间不足
  可能分配了太大的静态缓冲区

解决:
  1. 减小静态缓冲区大小
  2. 将大缓冲区移到 PSRAM (使用 shared_multi_heap)
  3. 检查是否有不必要的全局变量
```

### 错误 4: "esp32_spiram: SPIRAM not initialized"

```
错误信息:
  <err> esp32_spiram: SPIRAM not initialized

原因:
  PSRAM 配置不正确或硬件不支持

解决:
  1. 确认 prj.conf 中有 CONFIG_ESP32_SPIRAM=y
  2. 确认 overlay 包含了 esp32s3_wroom_n32r16.dtsi
  3. 确认模组确实是 N32R16V (带 PSRAM)
```

### 错误 5: "undefined reference to `__device_dts_ord_...`"

```
错误信息:
  undefined reference to `__device_dts_ord_NN`

原因:
  设备树配置问题: 在 overlay 中启用了外设但 status 未设为 "okay"，
  或引用了不存在的设备树节点

解决:
  1. 检查 overlay 中对应外设的 status = "okay";
  2. 使用 west build -d build -t devicetree 查看合并后的设备树
  3. 确认 DTS 节点标签名称拼写正确
```

---

## 7. 串口监视器使用技巧

### 过滤输出

```bash
# 只看包含特定关键字的行
west espressif --monitor -d build 2>&1 | grep "ERROR\|WARN\|!!! "

# 保存串口日志到文件
west espressif --monitor -d build 2>&1 | tee serial_log.txt
```

### 串口输出不显示的排查

1. 检查 prj.conf 中是否包含:
   ```
   CONFIG_CONSOLE=y
   CONFIG_SERIAL=y
   CONFIG_UART_CONSOLE=y
   ```
2. 检查 overlay 中 `&uart0` 的 `status = "okay";`
3. 检查波特率是否匹配 (默认 115200)
4. 确认连接的是 **Micro-USB** 端口 (不是 Type-C)

---

## 8. 双核 (PROCPU + APPCPU) 构建

ESP32-S3 有两个 Xtensa LX7 核心。如果需要使用双核：

```bash
# 主核 (PROCPU) 构建
west build -b esp32s3_devkitc/esp32s3_procpu -d build_procpu

# 从核 (APPCPU) 构建
west build -b esp32s3_devkitc/esp32s3_appcpu -d build_appcpu
```

> 双核开发较复杂，大多数应用只需要 PROCPU 单核运行。

---

## 9. 快速问题排查清单

遇到问题时，按以下顺序检查：

1. **USB 线缆**: 确保使用数据线 (非仅充电线)，连接在 Micro-USB 端口
2. **驱动**: Windows 需要安装 CP210x USB 转 UART 驱动
3. **设备管理器**: 检查 COM 端口号
4. **构建设置**:
   - `prj.conf` 中启用了所需的功能 (CONFIG_*=y)
   - `overlay` 文件中对应外设 `status = "okay"`
   - `overlay` 包含了 `esp32s3_wroom_n32r16.dtsi`
5. **烧录模式**: 如果 `west flash` 失败，手动进入下载模式
6. **供电**: 板上的红色电源指示灯是否亮起
7. **引脚冲突**: 是否使用了 GPIO35/36/37 或与板载 LED (GPIO38) 冲突
