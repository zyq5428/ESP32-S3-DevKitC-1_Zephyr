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
west build -p always -b esp32s3_devkitc/esp32s3/procpu .\zephyr\samples\basic\button\
west build -p auto -b esp32s3_devkitc/esp32s3/procpu .\esp_app
west build -p always -b esp32s3_devkitc/esp32s3/procpu .\esp_app
west build -p always -b esp32s3_devkitc/esp32s3/procpu --sysbuild .\esp_app
est build -p always -b esp32s3_devkitc/esp32s3/procpu -S espressif-flash-32M --sysbuild .\esp_app
west build -b esp32s3_devkitc/esp32s3/procpu -t menuconfig .\esp_app
west build -b esp32s3_devkitc/esp32s3/procpu -t guiconfig .\esp_app
west build -t menuconfig
west flash --esp-device COM15
west espressif monitor -p COM15
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

```
&flash0 {
	/delete-node/ partitions;
};

&flash0 {
	reg = <0x0 DT_SIZE_M(32)>;
};

#include <espressif/partitions_0x1000_amp_32M.dtsi>
```

### 清理构建（Build）目录

```
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