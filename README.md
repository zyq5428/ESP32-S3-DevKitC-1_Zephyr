# Zephyr命令

## 代理设置方法
```
$env:http_proxy="http://127.0.0.1:10808"
$env:https_proxy="http://127.0.0.1:10808"
```
## 工程文件查找技巧

### 命令输出筛选功能
```
west boards | Select-String -Pattern "pandora"
```

### 查找当前目录及其所有子目录中，文件名包含 'ap3216c' 的所有文件
```
Get-ChildItem -Recurse -Filter '*ap3216c*' -ErrorAction SilentlyContinue
```

### 查找 dts/bindings/sensor 目录下所有 *.yaml 文件内容中包含 'ap3216c' 的文件
```
Get-ChildItem -Path .\dts\bindings\sensor -Recurse -Include *.yaml | Select-String -Pattern "ap3216c" -AllMatches
```
- 命令详解：Get-ChildItem -Path .\dts\bindings\sensor -Recurse -Include *.yaml:
    - 首先，找到 dts\bindings\sensor 目录下所有的 YAML 文件。
    - '|': 将上一个命令的输出（文件对象）作为输入，传递给下一个命令。
    - Select-String -Pattern "ap3216c" -AllMatches:
        - 在传入的文件对象的内容中搜索 "ap3216c" 字符串。
        - 输出结果会显示文件名和匹配的行号。

### 查搜索子目录内的所有文件内容，查找含有'DMA'文件
```
Get-ChildItem -Path . -Recurse -Filter *.* | Select-String -Pattern "DMA"
Get-ChildItem -Path . -Recurse -Include *.overlay,*.dts,*.dtsi | Select-String -Pattern "rmt"
```
- 命令解释：
    - Get-ChildItem -Path . -Recurse: 递归地获取当前目录 (.) 及其所有子目录下的所有文件和文件夹。
    - -Filter *.*: 确保只处理文件（如果只搜索特定类型文件，例如 .c 和 .dts，可以改为 -Include *.c,*.dts,*.conf）。
    - |: 管道操作符，将前一个命令的输出传递给后一个命令。
    - Select-String -Pattern "DMA": 在接收到的所有文件的内容中搜索包含字符串 "DMA" 的行。

## west常用命令

### 编译指定开发板

```
# 彻底删除编译目录
Remove-Item -Recurse -Force build
# 编译官方例程
west build -p always -b esp32s3_devkitc/esp32s3/procpu .\zephyr\samples\basic\button\
# 编译自定义应用
west build -p auto -b esp32s3_devkitc/esp32s3/procpu .\esp_app
west build -p always -b esp32s3_devkitc/esp32s3/procpu .\esp_app
# 同时编译mcuboot和app
west build -p always -b esp32s3_devkitc/esp32s3/procpu --sysbuild .\esp_app
# 使用snippets修改flash和ram大小
est build -p always -b esp32s3_devkitc/esp32s3/procpu -S espressif-flash-32M --sysbuild .\esp_app
# 查看menuconfig和guiconfig
west build -b esp32s3_devkitc/esp32s3/procpu -t menuconfig .\esp_app
west build -b esp32s3_devkitc/esp32s3/procpu -t guiconfig .\esp_app
west build -t menuconfig
# 查看mcuboot的menuconfig
west build -t mcuboot_menuconfig
# 烧录esp开发板，指定烧录串口
west flash --esp-device COM15
# 显式指定 --domain esp_app，这是多固件工程的主域
# 在 sysbuild 联动机制下，它会以主应用为主导，同时将依赖的 MCUboot 一并安全地刷入芯片，且不会触发多域分发的实验性警告。
west flash --domain esp_app --esp-device COM15
# 监听esp开发板，指定对应串口
west espressif monitor -p COM15
# 指定flash大小烧录
west flash --esp-device COM15 -- --esp-flash-size=32MB
# 烧录MCUboot 到 0x0
python -m esptool --chip esp32s3 --port COM15 --baud 921600 write-flash --flash-size 32MB 0x0 build/mcuboot/zephyr/zephyr.bin
# 烧录主应用到 0x20000
python -m esptool --chip esp32s3 --port COM15 --baud 921600 write-flash --flash-size 32MB 0x20000 build/esp_app/zephyr/zephyr.bin
# 烧录完成后立即启动监视器
west flash --esp-device COM15 ; west espressif monitor -p COM15
west flash --esp-device COM21 ; west espressif monitor -p COM21

# 采用openocd烧录
openocd -s "C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20251215\openocd-esp32\share\openocd\scripts" `
        -f "interface/esp_usb_jtag.cfg" `
        -f "target/esp32s3.cfg" `
        -c "init" `
        -c "reset init" `
        -c "flash write_image erase E:/zephyrproject/build/zephyr/zephyr.hex" `
        -c "reset run" `
        -c "shutdown"

openocd `
        -f "interface/esp_usb_jtag.cfg" `
        -f "target/esp32s3.cfg" `
        -c "init" `
        -c "reset init" `
        -c "flash write_image erase E:/zephyrproject/build/zephyr/zephyr.hex" `
        -c "reset run" `
        -c "shutdown"
```

### 清理构建（Build）目录

```
Remove-Item -Recurse -Force build
west build -t clean
west build -t traceconfig
```

## overlay设置

### PWM功能
```
#include <dt-bindings/pinctrl/stm32-pinctrl.h>
#include <dt-bindings/pwm/pwm.h>
```

## Zephyr Shell 提供的内置命令

### 开启线程统计功能 (Kconfig)

- 在 prj.conf 中，你需要确保开启了以下宏，否则 Shell 里看不到线程相关的详细统计：
```
# 开启内核对象查询（必须）
CONFIG_THREAD_MONITOR=y
# 开启线程名显示（方便识别是哪个任务）
CONFIG_THREAD_NAME=y
# 开启栈分析功能（查看具体使用了百分之几）
CONFIG_THREAD_STACK_INFO=y
CONFIG_THREAD_ANALYZER=y
```

- 使用 Shell 命令查看
```
kernel threads
kernel thread analyzer
```

## ST7789V SPI屏引脚配置

- SPI2_CLK  PB13
- SPI2_MOSI PB15
- RES       PB10
- DC        PB11
- CS        PB12
- BLK       PB14

## AHT10引脚配置(软件I2C)

- IIC_CLK   PD6
- IIC_SDA   PC1

## AP3216C&ICM20608引脚配置(I2C3)

- IIC_CLK   PC0
- IIC_SDA   PC1

## 按键引脚配置

- KEY_UP    WK_UP   PC13    下拉10K
- KEY_DOWN  KEY1    PD9     上拉10K
- KEY_LEFT  KEY2    PD8     上拉10K
- KEY_RIGHT KEY0    PD10    上拉10K