# 设备树 (DeviceTree) 配置指南

> 适用于 ESP32-S3-DevKitC-1 v1.1 N32R16V 在 Zephyr RTOS 下的设备树配置。
> 本指南涵盖所有常用外设的 pinctrl 和节点配置方法。

---

## 目录

1. [设备树基础概念](#1-设备树基础概念)
2. [GPIO 配置](#2-gpio-配置)
3. [UART 配置](#3-uart-配置)
4. [I2C 配置](#4-i2c-配置)
5. [SPI 配置](#5-spi-配置)
6. [PWM (LEDC) 配置](#6-pwm-ledc-配置)
7. [ADC 配置](#7-adc-配置)
8. [RGB LED (WS2812) 配置](#8-rgb-led-ws2812-配置)
9. [Touch Sensor 配置](#9-touch-sensor-配置)
10. [Wi-Fi 配置](#10-wi-fi-配置)
11. [BLE 配置](#11-ble-配置)
12. [引脚复用速查表](#12-引脚复用速查表)

---

## 1. 设备树基础概念

设备树 (DeviceTree / DTS) 是一种描述硬件配置的数据结构。Zephyr 使用设备树来：

- **描述硬件**：有哪些外设、连接在哪些引脚上
- **配置驱动**：串口波特率、I2C 时钟频率等参数
- **解耦硬件与代码**：修改引脚不需要改动 C 代码

### 关键文件类型

| 文件类型 | 扩展名 | 位置 | 作用 |
|----------|--------|------|------|
| 芯片定义 | `.dtsi` | `zephyr/dts/xtensa/espressif/esp32s3/` | 定义 ESP32-S3 芯片的所有外设 |
| 模组定义 | `esp32s3_wroom_*.dtsi` | 同上 | 定义模组的 Flash/PSRAM 容量 |
| 板级定义 | `*.dts` | `boards/espressif/esp32s3_devkitc/` | 定义板级外设启用的默认配置 |
| 应用覆盖 | `.overlay` | 你的项目 `boards/` 目录 | ★ 你的自定义配置 |

### overlay 文件命名规则

```
boards/esp32s3_devkitc_esp32s3_procpu.overlay
       |_________board_target_______|.overlay
```

`board_target` 可通过 `west build -b <target>` 中的目标名确定。

---

## 2. GPIO 配置

### 基础 GPIO (输入/输出引脚)

```dts
/ {
	/*
	 * [自定义 GPIO 设备] 定义使用 GPIO 的外部设备
	 * 这些节点会在代码中通过 DT 宏访问
	 */

	/*
	 * [输出引脚示例] 定义一个由 GPIO 控制的 LED 指示灯
	 * compatible = "gpio-leds": 告知 Zephyr 这是 LED 设备
	 */
	my_leds {
		compatible = "gpio-leds";

		/*
		 * [LED 节点] 一个具体的 LED
		 * gpios = <&gpio0 4 GPIO_ACTIVE_HIGH>;
		 *   &gpio0: GPIO 控制器 0 (管理 GPIO0~GPIO31)
		 *   4:      GPIO 编号 (GPIO4)
		 *   GPIO_ACTIVE_HIGH: 高电平点亮 LED
		 */
		user_led: led_0 {
			gpios = <&gpio0 4 GPIO_ACTIVE_HIGH>;
			label = "外部 LED 指示灯";
		};
	};

	/*
	 * [输入引脚示例] 定义一个按键/开关输入
	 * compatible = "gpio-keys": 告知 Zephyr 这是按键设备
	 */
	my_buttons {
		compatible = "gpio-keys";

		/*
		 * [按键节点] 定义一个外部按键
		 * GPIO_ACTIVE_LOW: 按下时引脚拉低 (常见于独立按键)
		 * GPIO_PULL_UP:    启用内部上拉电阻，防止悬空时误触发
		 *
		 * 注意：
		 * - GPIO0 已被 Boot 按钮占用，不要重复定义
		 * - 避开 GPIO35/36/37
		 */
		user_button: button_0 {
			gpios = <&gpio0 6 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
			label = "外部按键";
		};
	};
};
```

### GPIO 中断配置

```dts
/ {
	/*
	 * [中断按键] 支持中断触发的按键
	 * zephyr,code = <INPUT_KEY_0>: 映射为内核按键事件码
	 */
	buttons {
		compatible = "gpio-keys";

		int_button: int_button_0 {
			gpios = <&gpio0 7 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
			label = "中断按键";
			/*
			 * [中断触发边沿] 下降沿触发(按下瞬间)
			 * 也可以使用 GPIO_INT_TRIG_BOTH (双边沿)
			 */
			zephyr,code = <INPUT_KEY_0>;
		};
	};
};
```

---

## 3. UART 配置

```dts
/*
 * [UART0 - 默认串口] 通过 USB 转 UART 输出
 * TX = GPIO43, RX = GPIO44
 * 这是系统的控制台串口 (zephyr,console)
 */
&uart0 {
	status = "okay";
	current-speed = <115200>;
	/* pinctrl-0: 指定使用的引脚配置
	 * pinctrl-names = "default": 使用默认(活跃)状态的配置
	 */
	pinctrl-0 = <&uart0_default>;
	pinctrl-names = "default";
};

/*
 * [UART1 - 第二路串口] 可连接外部设备
 * 默认引脚 TX = GPIO17, RX = GPIO18
 */
&uart1 {
	status = "okay";
	current-speed = <115200>;
	pinctrl-0 = <&uart1_default>;
	pinctrl-names = "default";
};

/*
 * [自定义 UART 引脚] 如需修改 UART1 的默认引脚
 * 在 &pinctrl 中定义新的引脚配置
 */
&pinctrl {
	/*
	 * [uart1_custom] 自定义 UART1 引脚配置
	 * 将 TX 映射到 GPIO9, RX 映射到 GPIO10
	 */
	uart1_custom: uart1_custom {
		group_tx {
			/*
			 * [TX 引脚复用] UART1_TX_GPIO9 宏定义在
			 * zephyr/dt-bindings/pinctrl/esp32s3-pinctrl.h 中
			 */
			pinmux = <UART1_TX_GPIO9>;
			/* output-high: TX 空闲状态保持高电平(UART 协议要求) */
			output-high;
		};
		group_rx {
			pinmux = <UART1_RX_GPIO10>;
			/* bias-pull-up: 启用上拉，防止悬空时产生噪声 */
			bias-pull-up;
		};
	};
};

/* 然后将 &uart1 的 pinctrl-0 指向自定义配置 */
/*
 * &uart1 {
 *     pinctrl-0 = <&uart1_custom>;
 *     pinctrl-names = "default";
 * };
 */
```

### UART 引脚宏速查

| 信号 | 可用宏 (部分) |
|------|-------------|
| UART0 TX | `UART0_TX_GPIO43` |
| UART0 RX | `UART0_RX_GPIO44` |
| UART1 TX | `UART1_TX_GPIO17`, `UART1_TX_GPIO9`, `UART1_TX_GPIO7` 等 |
| UART1 RX | `UART1_RX_GPIO18`, `UART0_RX_GPIO10`, `UART1_RX_GPIO8` 等 |

> 完整列表见 `zephyr/include/zephyr/dt-bindings/pinctrl/esp32s3-pinctrl.h`

---

## 4. I2C 配置

### I2C0 (默认引脚: SDA=GPIO1, SCL=GPIO2)

```dts
&i2c0 {
	status = "okay";
	/*
	 * [时钟频率] I2C_BITRATE_STANDARD = 100kHz
	 * 可选: I2C_BITRATE_FAST (400kHz), I2C_BITRATE_FAST_PLUS (1MHz)
	 */
	clock-frequency = <I2C_BITRATE_STANDARD>;
	pinctrl-0 = <&i2c0_default>;
	pinctrl-names = "default";

	/*
	 * [I2C 设备子节点] 挂在 I2C0 总线上的设备
	 * 这里以 BME280 温湿度传感器为例
	 * 每个 I2C 设备需要 reg 属性指定 7 位 I2C 地址
	 */
	bme280: bme280@76 {
		compatible = "bosch,bme280";
		/* reg: I2C 从设备地址，0x76 是 BME280 的常见地址 */
		reg = <0x76>;
		label = "BME280 温湿度传感器";
	};
};
```

### I2C1 (默认引脚: SDA=GPIO4, SCL=GPIO5)

```dts
&i2c1 {
	status = "okay";
	clock-frequency = <I2C_BITRATE_STANDARD>;
	pinctrl-0 = <&i2c1_default>;
	pinctrl-names = "default";
};
```

### 自定义 I2C 引脚

```dts
&pinctrl {
	i2c0_custom: i2c0_custom {
		group1 {
			/*
			 * [I2C 引脚复用] 必须同时配置 SDA 和 SCL
			 * drive-open-drain: I2C 协议要求开漏输出
			 * bias-pull-up: I2C 需要上拉电阻
			 */
			pinmux = <I2C0_SDA_GPIO7>,
				 <I2C0_SCL_GPIO8>;
			bias-pull-up;
			drive-open-drain;
			output-high;
		};
	};
};
```

### I2C 引脚宏速查

| 信号 | GPIO | 宏 |
|------|------|-----|
| I2C0 SDA | 1 | `I2C0_SDA_GPIO1` |
| I2C0 SCL | 2 | `I2C0_SCL_GPIO2` |
| I2C1 SDA | 4 | `I2C1_SDA_GPIO4` |
| I2C1 SCL | 5 | `I2C1_SCL_GPIO5` |

> I2C 引脚可通过 pinctrl 重映射到其他 GPIO，但推荐使用默认映射。

---

## 5. SPI 配置

### SPI2 (FSPI 控制器, 默认引脚可用)

```dts
/*
 * [SPI2 配置] SPIM2 是 ESP32-S3 的 FSPI 控制器(主模式)
 *
 * 默认引脚 (Zephyr 板级定义):
 *   MOSI = GPIO11, MISO = GPIO13, SCLK = GPIO12, CS = GPIO10
 *
 * ⚠️ 关键约束:
 *   GPIO35/36/37 被 Octal Flash/PSRAM 占用，不可使用
 *   GPIO38 被板载 RGB LED 占用，冲突时优先释放 LED
 *   SPI3 默认使用 GPIO39(MOSI), GPIO37(MISO), GPIO36(SCLK), GPIO38(CS)
 *        —— 其中 GPIO36/37 不可用，所以 SPI3 不可使用默认配置!
 */
&spi2 {
	status = "okay";
	#address-cells = <1>;
	#size-cells = <0>;
	pinctrl-0 = <&spim2_default>;
	pinctrl-names = "default";

	/*
	 * [SPI 设备子节点] 挂在 SPI2 总线上的设备
	 * reg: 片选线索引 (0 = CS0 = GPIO10)
	 * spi-max-frequency: 最大 SPI 时钟频率
	 */
	my_spi_device: device@0 {
		compatible = "my-spi-device";
		reg = <0>;
		spi-max-frequency = <8000000>;
		label = "SPI 外部设备";
	};
};
```

### 自定义 SPI 引脚

```dts
&pinctrl {
	/*
	 * [自定义 SPI2 引脚] 将所有信号映射到安全可用的 GPIO
	 * GPIO35/36/37/38 被占用，因此手动指定四个安全引脚
	 */
	spim2_custom: spim2_custom {
		group1 {
			/*
			 * [信号分配]
			 * MISO = GPIO13 (接收数据)
			 * SCLK = GPIO12 (时钟)
			 * CS   = GPIO9  (片选，替代默认 GPIO10)
			 */
			pinmux = <SPIM2_MISO_GPIO13>,
			         <SPIM2_SCLK_GPIO12>,
			         <SPIM2_CSEL_GPIO9>;
		};
		group2 {
			/* MOSI = GPIO11 (发送数据) */
			pinmux = <SPIM2_MOSI_GPIO11>;
			output-low; /* MOSI 空闲时为低电平 */
		};
	};
};
```

### ⚠️ SPI3 警告

```dts
/*
 * SPI3 (SPIM3) 默认引脚: MOSI=39, MISO=37, SCLK=36, CS=38
 * 问题：
 *   - GPIO36 和 GPIO37 是 Octal PSRAM 数据线，绝对不可使用
 *   - GPIO38 被板载 RGB LED 占用
 *
 * 解决方案：
 *   优先使用 SPI2 (FSPI)
 *   若必须使用 SPI3，需要通过 pinctrl 重映射所有引脚到安全位置
 */
```

---

## 6. PWM (LEDC) 配置

ESP32-S3 使用 **LEDC** (LED PWM Controller) 提供 PWM 输出，支持高达 8 个通道。

```dts
/ {
	/*
	 * [PWM 输出设备] 使用 LEDC 控制器生成 PWM 信号
	 *
	 * compatible = "pwm-leds": 将 PWM 设备映射为 LED 驱动
	 * 也可直接使用 pwm 驱动 (compatible = "pwm-leds" 更简单)
	 */
	pwm_outputs {
		compatible = "pwm-leds";

		/*
		 * [PWM 通道 - 红色] 使用 LEDC 通道 0
		 * pwms = <&ledc0 0 0 PWM_POLARITY_NORMAL>;
		 *   &ledc0:  LEDC PWM 控制器
		 *   0:       LEDC 通道号 (0~7)
		 *   0:       初始周期 (0 = 占空比 0%)
		 *   PWM_POLARITY_NORMAL: 正常极性(高电平有效)
		 *
		 * 连接到 GPIO4 作为 PWM 输出
		 */
		pwm_red: pwm_led_0 {
			pwms = <&ledc0 0 0 PWM_POLARITY_NORMAL>;
			gpios = <&gpio0 4 GPIO_ACTIVE_HIGH>;
			label = "PWM 红色通道";
		};

		/*
		 * [PWM 通道 - 绿色] 使用 LEDC 通道 1
		 * 连接到 GPIO5
		 */
		pwm_green: pwm_led_1 {
			pwms = <&ledc0 1 0 PWM_POLARITY_NORMAL>;
			gpios = <&gpio0 5 GPIO_ACTIVE_HIGH>;
			label = "PWM 绿色通道";
		};

		/*
		 * [PWM 通道 - 蓝色] 使用 LEDC 通道 2
		 * 连接到 GPIO6
		 * 注意避开 GPIO35/36/37/38
		 */
		pwm_blue: pwm_led_2 {
			pwms = <&ledc0 2 0 PWM_POLARITY_NORMAL>;
			gpios = <&gpio0 6 GPIO_ACTIVE_HIGH>;
			label = "PWM 蓝色通道";
		};
	};
};
```

### 在 prj.conf 中启用 PWM/LEDC

```
CONFIG_PWM=y
CONFIG_LEDC=y
```

---

## 7. ADC 配置

```dts
/*
 * [ADC 通道配置] ESP32-S3 有 2 个 ADC 控制器
 * ADC1: 通道 0~9 (GPIO1~10)
 * ADC2: 通道 0~9 (GPIO11~20)
 *
 * 注意：ADC2 与 Wi-Fi 共享，使用 Wi-Fi 时 ADC2 可能不可用
 */

/ {
	zephyr,user {
		/*
		 * [ADC 输入通道] 定义一个 ADC 读取节点
		 * io-channels = <&adc0 3>;
		 *   &adc0: ADC 控制器 0 (ADC1)
		 *   3:      ADC1 通道 3 (对应 GPIO4)
		 */
		adc_input: adc_input {
			io-channels = <&adc0 3>;
		};
	};
};
```

### ADC 通道与 GPIO 对应表 (ADC1)

| ADC1 通道 | GPIO | 备注 |
|-----------|------|------|
| CH0 | GPIO1 | I2C0 SDA 默认 |
| CH1 | GPIO2 | I2C0 SCL 默认 |
| CH2 | GPIO3 | |
| CH3 | GPIO4 | I2C1 SDA 默认 |
| CH4 | GPIO5 | I2C1 SCL 默认 |
| CH5 | GPIO6 | |
| CH6 | GPIO7 | |
| CH7 | GPIO8 | |
| CH8 | GPIO9 | |
| CH9 | GPIO10 | SPI2 CS 默认 |

### 在 prj.conf 中启用 ADC

```
CONFIG_ADC=y
CONFIG_ADC_ASYNC=y
```

---

## 8. RGB LED (WS2812) 配置

板载 RGB LED (v1.1) 连接在 GPIO38，使用 WS2812 驱动：

```dts
/ {
	/*
	 * [RGB LED] 板载可寻址 WS2812 LED
	 * 使用 LED Strip 驱动控制颜色
	 */
	leds {
		compatible = "gpio-leds";

		rgb_led: led_0 {
			gpios = <&gpio0 38 GPIO_ACTIVE_HIGH>;
			label = "板载 RGB LED (WS2812)";
		};
	};
};
```

在 prj.conf 中启用 WS2812 驱动：

```
CONFIG_LED_STRIP=y
CONFIG_LED_STRIP_WS2812=y
```

### 禁用 RGB LED 释放 GPIO38

如果想将 GPIO38 用作其他用途：

```dts
/* 在 overlay 中不定义 rgb_led 节点，或显式禁用 */
/delete-node/ &rgb_led;
```

---

## 9. Touch Sensor 配置

```dts
&touch {
	status = "okay";
	/*
	 * [触摸传感器配置参数]
	 * debounce-interval-ms: 消抖时间(毫秒)
	 * href-microvolt: 高参考电压
	 * lref-microvolt: 低参考电压
	 * href-atten-microvolt: 高参考电压衰减
	 */
	debounce-interval-ms = <30>;
	href-microvolt = <2700000>;
	lref-microvolt = <500000>;
	href-atten-microvolt = <1000000>;
	/* 滤波器配置 */
	filter-mode = <ESP32_TOUCH_FILTER_MODE_IIR_16>;
	filter-debounce-cnt = <1>;
	filter-noise-thr = <ESP32_TOUCH_FILTER_NOISE_THR_4_8TH>;
	filter-jitter-step = <4>;
	filter-smooth-level = <ESP32_TOUCH_FILTER_SMOOTH_MODE_IIR_2>;
};
```

### 触摸通道与 GPIO 对应表

| 触摸通道 | GPIO | 触摸通道 | GPIO |
|----------|------|----------|------|
| TOUCH1 | GPIO1 | TOUCH8 | GPIO8 |
| TOUCH2 | GPIO2 | TOUCH9 | GPIO9 |
| TOUCH3 | GPIO3 | TOUCH10 | GPIO10 |
| TOUCH4 | GPIO4 | TOUCH11 | GPIO11 |
| TOUCH5 | GPIO5 | TOUCH12 | GPIO12 |
| TOUCH6 | GPIO6 | TOUCH13 | GPIO13 |
| TOUCH7 | GPIO7 | TOUCH14 | GPIO14 |

---

## 10. Wi-Fi 配置

```dts
/*
 * [Wi-Fi] 启用 ESP32-S3 的 Wi-Fi 功能
 * 状态设为 "okay" 后，Zephyr Wi-Fi 子系统即可使用
 */
&wifi {
	status = "okay";
};
```

prj.conf 中需要：

```
CONFIG_WIFI=y
CONFIG_WIFI_ESP32=y
CONFIG_NETWORKING=y
CONFIG_NET_L2_ETHERNET=y
CONFIG_NET_DHCPV4=y
```

---

## 11. BLE 配置

```dts
/*
 * [蓝牙 HCI] 启用 ESP32-S3 的蓝牙 HCI 接口
 * 启用后 Zephyr 蓝牙子系统可用
 */
&esp32_bt_hci {
	status = "okay";
};
```

prj.conf 中需要：

```
CONFIG_BT=y
CONFIG_BT_ESP32=y
CONFIG_BT_HCI=y
```

---

## 12. 引脚复用速查表

### 常用外设可用引脚 (避开禁用引脚)

| 外设 | 推荐引脚 (安全可用) | 需避开的引脚 |
|------|-------------------|-------------|
| UART0 TX | 43 (固定) | - |
| UART0 RX | 44 (固定) | - |
| UART1 TX/RX | 17/18, 9/10, 7/8, 15/16 | 35,36,37,38 |
| I2C0 SDA/SCL | 1/2 (推荐), 7/8, 9/10 | 35,36,37,38 |
| I2C1 SDA/SCL | 4/5 (推荐), 6/7, 8/9 | 35,36,37,38 |
| SPI2 (MISO/MOSI/SCLK/CS) | 13/11/12/10 (推荐) | 35,36,37,38 |
| SPI3 (需重映射) | 全部需自定义 | 35,36,37,38 |
| PWM (LEDC) | 任意 GPIO (0~34, 38~48) | 35,36,37 |
| ADC1 | GPIO1~10 | 35,36,37 |
| ADC2 | GPIO11~20 (Wi-Fi 禁用时) | 35,36,37 |
| Touch | GPIO1~14 | 35,36,37 |
| GPIO 通用 | 0,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,21,38,39,40,41,42,45,46,47,48 | 35,36,37 |
