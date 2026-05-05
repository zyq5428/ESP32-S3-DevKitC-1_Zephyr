/* 
 * BTHome 传感器模板示例 
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>

/* 定义 BTHome 服务数据长度和 UUID */
#define SERVICE_DATA_LEN        9
#define SERVICE_UUID            0xfcd2      /* BTHome 官方分配的 16位 UUID */
#define IDX_TEMPL               4           /* 存储温度低字节在数组中的索引 */
#define IDX_TEMPH               5           /* 存储温度高字节在数组中的索引 */

/* 
 * 自定义广播参数：
 * 1. BT_LE_ADV_OPT_USE_IDENTITY: 使用设备真实的身份地址。
 * 2. BT_GAP_ADV_SLOW_INT_MIN/MAX: 使用较慢的广播频率（省电模式）。
 */
#define ADV_PARAM BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_IDENTITY, \
				  BT_GAP_ADV_SLOW_INT_MIN, \
				  BT_GAP_ADV_SLOW_INT_MAX, NULL)

/* 
 * BTHome 数据载荷格式：
 * 字节 0-1: UUID (0xfcd2)
 * 字节 2: 0x40 (BTHome V2 标志)
 * 字节 3: 0x02 (数据类型：温度)
 * 字节 4-5: 温度数值 (Little Endian, 单位 0.01°C)
 * 字节 6: 0x03 (数据类型：湿度)
 * 字节 7-8: 湿度数值 (Little Endian, 单位 0.01%)
 */
static uint8_t service_data[SERVICE_DATA_LEN] = {
	BT_UUID_16_ENCODE(SERVICE_UUID),
	0x40,
	0x02,	/* Temperature ID */
	0xc4,	/* 19.88°C 的低字节 (示例初值) */
	0x00,   /* 高字节 */
	0x03,	/* Humidity ID */
	0xbf,	/* 50.55% 的低字节 */
	0x13,   /* 高字节 */
};

/* 构造广播包 */
static struct bt_data ad[] = {
    /* 标志位：通用可发现 */
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    /* 设备名称：从 prj.conf 获取 */
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
    /* 核心：16位服务数据 (Service Data)，存放 BTHome 载荷 */
	BT_DATA(BT_DATA_SVC_DATA16, service_data, ARRAY_SIZE(service_data))
};

/**
 * @brief 蓝牙线程入口函数
 * 
 * @param p1, p2, p3 线程参数（由 K_THREAD_DEFINE 传入，此处未使用）
 */
void bt_thread_entry(void *p1, void *p2, void *p3)
{
	int err;
	int temp = 0; // 模拟温度变量

	printk("正在启动 BTHome 传感器模板...\n");

	/* 初始化蓝牙（同步模式） */
	err = bt_enable(NULL);
	if (err) {
		printk("初始化失败 (err %d)\n", err);
		return;
	}

	/* 开启广播 */
	err = bt_le_adv_start(ADV_PARAM, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("广播失败 (err %d)\n", err);
		return;
	}

	for (;;) {
		/* 
         * 模拟数据逻辑：
         * BTHome 要求温度单位是 0.01°C，所以 25°C 要发 2500。
         * 2500 的十六进制是 0x09C4。
         */
		service_data[IDX_TEMPH] = (temp * 100) >> 8;   /* 取高 8 位 */
		service_data[IDX_TEMPL] = (temp * 100) & 0xff; /* 取低 8 位 */

		if (temp++ == 25) {
			temp = 0;
		}

		/* 
         * 关键 API：动态更新广播内容。
         * 此函数不需要停止广播再重新开启，能更平滑地切换数据。
         */
		err = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
		if (err) {
			printk("更新失败 (err %d)\n", err);
		}

		/* 按照广播间隔进入睡眠，保持同步 */
		k_sleep(K_MSEC(BT_GAP_ADV_SLOW_INT_MIN));
	}
	return;
}

/* 线程配置参数 */
#define BT_STACK_SIZE 4096  // 线程栈大小（单位：字节）
#define BT_PRIORITY 11      // 线程优先级（数字越大优先级越低）

/**
 * @brief 定义并自动启动线程
 * 
 * 参数依次为：线程 ID, 栈大小, 入口函数, 参数1, 参数2, 参数3, 优先级, 选项, 启动延迟
 */
K_THREAD_DEFINE(bluetooth_tid, BT_STACK_SIZE, 
                bt_thread_entry, NULL, NULL, NULL,
                BT_PRIORITY, 0, 0);