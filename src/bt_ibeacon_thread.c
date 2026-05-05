/* iBeacon 示例 */

#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>

/* 
 * 1米处的基准 RSSI 值（校准值）。
 * 0xc8 补码表示 -56 dBm。手机 App 根据这个参考值和接收到的实际信号强度来估算距离。
 */
#ifndef IBEACON_RSSI
#define IBEACON_RSSI 0xc8
#endif

/* 构造 iBeacon 广播包 */
static const struct bt_data ad[] = {
    /* 标志位：仅限蓝牙低功耗 (LE)，不支持经典蓝牙 (BR/EDR) */
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
    
    /* 核心：iBeacon 格式的厂商自定义数据 */
	BT_DATA_BYTES(BT_DATA_MANUFACTURER_DATA,
		      0x4c, 0x00, /* 1. Apple 公司 ID (0x004c)，小端序 */
		      0x02, 0x15, /* 2. 数据类型 (0x02) 和 剩余长度 (0x15 = 21 字节) */
		      /* 3. Proximity UUID (16 字节): 标识一个组织或区域 */
		      0x18, 0xee, 0x15, 0x16, /* UUID[15..12] */
		      0x01, 0x6b,             /* UUID[11..10] */
		      0x4b, 0xec,             /* UUID[9..8] */
		      0xad, 0x96,             /* UUID[7..6] */
		      0xbc, 0xb9, 0x6d, 0x16, 0x6e, 0x97, /* UUID[5..0] */
		      /* 4. Major (2 字节): 主编号，通常用于区分分店 */
		      0x00, 0x00, 
		      /* 5. Minor (2 字节): 次编号，通常用于区分具体货架或位置 */
		      0x00, 0x00, 
		      /* 6. Measured Power: 1米处的 RSSI 校准值 */
		      IBEACON_RSSI) 
};

/* 蓝牙就绪后的回调函数 */
static void bt_ready(int err)
{
	if (err) {
		printk("蓝牙初始化失败 (err %d)\n", err);
		return;
	}

	printk("蓝牙初始化成功\n");

	/* 
     * 开始广播：
     * BT_LE_ADV_NCONN: 不可连接广播 (iBeacon 只需要广播，不需要连接)
     */
	err = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad),
			      NULL, 0);
	if (err) {
		printk("广播启动失败 (err %d)\n", err);
		return;
	}

	printk("iBeacon 已开始运行\n");
}

/**
 * @brief 蓝牙线程入口函数
 * 
 * @param p1, p2, p3 线程参数（由 K_THREAD_DEFINE 传入，此处未使用）
 */
void bt_thread_entry(void *p1, void *p2, void *p3)
{
	int err;

	printk("正在启动 iBeacon 演示...\n");

	/* 初始化蓝牙系统，初始化完成后会调用 bt_ready */
	err = bt_enable(bt_ready);
	if (err) {
		printk("蓝牙初始化启动失败 (err %d)\n", err);
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