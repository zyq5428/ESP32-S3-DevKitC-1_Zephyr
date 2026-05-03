/* 
 * main.c - 完整的蓝牙多功能外设示例
 * 功能：包含 BAS（电池）、HRS（心率）、CTS（时间）、IAS（即时警报）及自定义 Vendor 服务
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>

/* 引入 Zephyr 蓝牙及相关服务头文件 */
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h> // 电池服务
#include <zephyr/bluetooth/services/cts.h> // 时间服务
#include <zephyr/bluetooth/services/hrs.h> // 心率服务
#include <zephyr/bluetooth/services/ias.h> // 立即提醒服务

/* ==================== 1. 自定义供应商服务 (Vendor Service) 定义 ==================== */

/* 定义 128位 UUID：12345678-1234-5678-1234-56789abcdef0 */
#define BT_UUID_CUSTOM_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

static const struct bt_uuid_128 vnd_uuid = BT_UUID_INIT_128(BT_UUID_CUSTOM_SERVICE_VAL);

/* 定义特征 UUID：用于加密传输 (Encrypted) */
static const struct bt_uuid_128 vnd_enc_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1));

/* 定义特征 UUID：用于身份验证 (Authenticated) */
static const struct bt_uuid_128 vnd_auth_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2));

#define VND_MAX_LEN 20
#define BT_HR_HEARTRATE_DEFAULT_MIN 90U
#define BT_HR_HEARTRATE_DEFAULT_MAX 160U

/* 存储特征值的缓冲区 */
static uint8_t vnd_value[VND_MAX_LEN + 1] = { 'V', 'e', 'n', 'd', 'o', 'r'};
static uint8_t vnd_auth_value[VND_MAX_LEN + 1] = { 'V', 'e', 'n', 'd', 'o', 'r'};
static uint8_t vnd_wwr_value[VND_MAX_LEN + 1] = { 'V', 'e', 'n', 'd', 'o', 'r' };

/* 通用读回调函数：当主机请求读取 vnd_value 时触发 */
static ssize_t read_vnd(struct bt_conn *conn, const struct bt_gatt_attr *attr,
            void *buf, uint16_t len, uint16_t offset)
{
    const char *value = attr->user_data; // 指向 vnd_value 等
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, strlen(value));
}

/* 通用写回调函数：当主机请求写入数据时触发 */
static ssize_t write_vnd(struct bt_conn *conn, const struct bt_gatt_attr *attr,
             const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    uint8_t *value = attr->user_data;

    if (offset + len > VND_MAX_LEN) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(value + offset, buf, len);
    value[offset + len] = 0; // 确保是字符串格式

    return len;
}

static uint8_t simulate_vnd; // 控制是否进行 Vendor 指示模拟
static uint8_t indicating;   // 指示状态标志（防止重叠发送）
static struct bt_gatt_indicate_params ind_params; // 指示参数结构体

/* CCC (Client Characteristic Configuration) 改变时的回调：主机开启/关闭订阅 */
static void vnd_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    simulate_vnd = (value == BT_GATT_CCC_INDICATE) ? 1 : 0;
}

/* Indication 发送后的回调：确认主机是否收到 */
static void indicate_cb(struct bt_conn *conn,
            struct bt_gatt_indicate_params *params, uint8_t err)
{
    printk("Indication %s\n", err != 0U ? "fail" : "success");
}

/* 指示流程结束后的销毁函数 */
static void indicate_destroy(struct bt_gatt_indicate_params *params)
{
    printk("Indication complete\n");
    indicating = 0U; // 重置标志位，允许下一次发送
}

/* --- 长数据写入演示 --- */
#define VND_LONG_MAX_LEN 74
static uint8_t vnd_long_value[VND_LONG_MAX_LEN + 1] = "Vendor long data...";

static ssize_t write_long_vnd(struct bt_conn *conn,
                  const struct bt_gatt_attr *attr, const void *buf,
                  uint16_t len, uint16_t offset, uint8_t flags)
{
    uint8_t *value = attr->user_data;

    if (flags & BT_GATT_WRITE_FLAG_PREPARE) { return 0; } // 准备写入阶段

    if (offset + len > VND_LONG_MAX_LEN) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(value + offset, buf, len);
    value[offset + len] = 0;
    return len;
}

static const struct bt_uuid_128 vnd_long_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef3));

static struct bt_gatt_cep vnd_long_cep = { .properties = BT_GATT_CEP_RELIABLE_WRITE };

/* --- 无回复写入演示 (Write Without Response) --- */
static const struct bt_uuid_128 vnd_write_cmd_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef4));

static ssize_t write_without_rsp_vnd(struct bt_conn *conn,
                     const struct bt_gatt_attr *attr,
                     const void *buf, uint16_t len, uint16_t offset,
                     uint8_t flags)
{
    uint8_t *value = attr->user_data;

    if (!(flags & BT_GATT_WRITE_FLAG_CMD)) {
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED); // 拒绝需要回复的写请求
    }

    if (offset + len > VND_MAX_LEN) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(value + offset, buf, len);
    value[offset + len] = 0;
    return len;
}

/* 注册 Vendor 服务到 GATT 数据库 */
BT_GATT_SERVICE_DEFINE(vnd_svc,
    BT_GATT_PRIMARY_SERVICE(&vnd_uuid),
    // 特征：加密读写 + 指示
    BT_GATT_CHARACTERISTIC(&vnd_enc_uuid.uuid,
                   BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_INDICATE,
                   BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
                   read_vnd, write_vnd, vnd_value),
    BT_GATT_CCC(vnd_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),
    // 特征：身份认证读写
    BT_GATT_CHARACTERISTIC(&vnd_auth_uuid.uuid,
                   BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                   BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN,
                   read_vnd, write_vnd, vnd_auth_value),
    // 特征：长数据
    BT_GATT_CHARACTERISTIC(&vnd_long_uuid.uuid, BT_GATT_CHRC_READ |
                   BT_GATT_CHRC_WRITE | BT_GATT_CHRC_EXT_PROP,
                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE | BT_GATT_PERM_PREPARE_WRITE,
                   read_vnd, write_long_vnd, &vnd_long_value),
    BT_GATT_CEP(&vnd_long_cep),
    // 特征：无回复写
    BT_GATT_CHARACTERISTIC(&vnd_write_cmd_uuid.uuid,
                   BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                   BT_GATT_PERM_WRITE, NULL,
                   write_without_rsp_vnd, &vnd_wwr_value),
);

/* ==================== 2. 广播包配置 ==================== */

/* 主广播包：包含服务 UUID 列表[cite: 1] */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL,
              BT_UUID_16_ENCODE(BT_UUID_HRS_VAL),
              BT_UUID_16_ENCODE(BT_UUID_BAS_VAL),
              BT_UUID_16_ENCODE(BT_UUID_CTS_VAL)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_CUSTOM_SERVICE_VAL),
};

/* 扫描响应包：包含设备全名[cite: 1] */
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* ==================== 3. 蓝牙事件回调 ==================== */

/* MTU 更新回调 */
void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
    printk("Updated MTU: TX: %d RX: %d bytes\n", tx, rx);
}

static struct bt_gatt_cb gatt_callbacks = { .att_mtu_updated = mtu_updated };

/* 连接成功回调 */
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("Connection failed, err 0x%02x %s\n", err, bt_hci_err_to_str(err));
    } else {
        printk("Connected\n");
    }
}

/* 断开连接回调 */
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected, reason 0x%02x %s\n", reason, bt_hci_err_to_str(reason));
}

/* 注册连接回调 */
BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

/* 立即警报服务 (IAS) 回调：处理手机端的 Alert 指令 */
static void alert_stop(void) { printk("Alert stopped\n"); }
static void alert_start(void) { printk("Mild alert started\n"); }
static void alert_high_start(void) { printk("High alert started\n"); }

BT_IAS_CB_DEFINE(ias_callbacks) = {
    .no_alert = alert_stop,
    .mild_alert = alert_start,
    .high_alert = alert_high_start,
};

/* ==================== 4. 核心功能函数 ==================== */

/* 蓝牙初始化就绪后调用 */
static void bt_ready(void)
{
    int err;
    printk("Bluetooth initialized\n");

    if (IS_ENABLED(CONFIG_SETTINGS)) { settings_load(); }

    /* 开始 BLE 广播[cite: 1] */
    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        printk("Advertising failed to start (err %d)\n", err);
        return;
    }
    printk("Advertising successfully started\n");
}

/* 安全配对相关回调：显示配对码 */
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    printk("Passkey for %s: %06u\n", bt_conn_dst_str(conn), passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
    printk("Pairing cancelled: %s\n", bt_conn_dst_str(conn));
}

static struct bt_conn_auth_cb auth_cb_display = {
    .passkey_display = auth_passkey_display,
    .cancel = auth_cancel,
};

/* --- 传感器数据模拟更新函数 --- */

/* 电池电量模拟：每次减少 1% */
static void bas_notify(void)
{
    uint8_t battery_level = bt_bas_get_battery_level();
    battery_level--;
    if (!battery_level) { battery_level = 100U; }
    bt_bas_set_battery_level(battery_level); // 会自动向订阅的手机发送通知
}

static uint8_t bt_heartrate = BT_HR_HEARTRATE_DEFAULT_MIN;

/* 心率模拟：在 90~160 之间波动 */
static void hrs_notify(void)
{
    bt_heartrate++;
    if (bt_heartrate == BT_HR_HEARTRATE_DEFAULT_MAX) {
        bt_heartrate = BT_HR_HEARTRATE_DEFAULT_MIN;
    }
    bt_hrs_notify(bt_heartrate);
}

/* --- 时间服务 (CTS) 相关逻辑 --- */
static struct bt_cts_local_time local_time = {
    .timezone_offset = BT_CTS_TIMEZONE_DEFAULT_VALUE,
    .dst_offset = BT_CTS_DST_OFFSET_UNKNOWN,
};
static bool cts_notification_enabled;
static int64_t unix_ms_ref;

static void cts_notification_changed_cb(bool enabled) { cts_notification_enabled = enabled; }

/* 接收手机发来的时间并保存 */
static int cts_time_write_cb(struct bt_cts_time_format *cts_time)
{
    int64_t unix_ms;
    if (IS_ENABLED(CONFIG_BT_CTS_HELPER_API)) {
        bt_cts_time_to_unix_ms(cts_time, &unix_ms);
        unix_ms_ref = unix_ms - k_uptime_get(); // 计算时间偏差
        return 0;
    }
    return -ENOTSUP;
}

/* 返回当前设备时间给手机 */
static int cts_fill_current_cts_time_cb(struct bt_cts_time_format *cts_time)
{
    int64_t unix_ms = unix_ms_ref + k_uptime_get();
    return bt_cts_time_from_unix_ms(cts_time, unix_ms);
}

const struct bt_cts_cb cts_cb = {
    .notification_changed = cts_notification_changed_cb,
    .cts_time_write = cts_time_write_cb,
    .fill_current_cts_time = cts_fill_current_cts_time_cb,
};

/* 心率控制点回调：主机可以通过写请求重置心率测量 */
static int bt_hrs_ctrl_point_write(uint8_t request)
{
    if (request != BT_HRS_CONTROL_POINT_RESET_ENERGY_EXPANDED_REQ) { return -ENOTSUP; }
    bt_heartrate = BT_HR_HEARTRATE_DEFAULT_MIN;
    return 0;
}

static struct bt_hrs_cb hrs_cb = { .ctrl_point_write = bt_hrs_ctrl_point_write };

/* ==================== 5. 主程序入口 ==================== */

int main(void)
{
    struct bt_gatt_attr *vnd_ind_attr;
    char str[BT_UUID_STR_LEN];
    int err;

    /* 第一步：初始化蓝牙控制器 */
    err = bt_enable(NULL);
    if (err != 0) {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    /* 第二步：执行自定义就绪逻辑（开始广播） */
    bt_ready();

    /* 第三步：注册各服务的专门回调 */
    bt_cts_init(&cts_cb);
    bt_hrs_cb_register(&hrs_cb);
    bt_gatt_cb_register(&gatt_callbacks);
    bt_conn_auth_cb_register(&auth_cb_display);

    /* 查找自定义特征的属性指针，用于后续发送 Indication[cite: 1] */
    vnd_ind_attr = bt_gatt_find_by_uuid(vnd_svc.attrs, vnd_svc.attr_count, &vnd_enc_uuid.uuid);
    bt_uuid_to_str(&vnd_enc_uuid.uuid, str, sizeof(str));
    printk("Indicate VND attr %p (UUID %s)\n", vnd_ind_attr, str);

    /* 第四步：进入传感器模拟主循环 */
    while (1) {
        k_sleep(K_SECONDS(1)); // 每秒执行一次

        /* 1. 如果手机开启了时间通知，则发送更新 */
        if (cts_notification_enabled) {
            bt_cts_send_notification(BT_CTS_UPDATE_REASON_MANUAL);
        }

        /* 2. 模拟并发送心率、电池数据 */
        hrs_notify();
        bas_notify();

        /* 3. 模拟自定义供应商服务的 Indication (指示) */
        if (simulate_vnd && vnd_ind_attr) {
            if (indicating) { continue; } // 如果上一笔指示还未收到回复，跳过

            ind_params.attr = vnd_ind_attr;
            ind_params.func = indicate_cb;
            ind_params.destroy = indicate_destroy;
            ind_params.data = &indicating;
            ind_params.len = sizeof(indicating);

            if (bt_gatt_indicate(NULL, &ind_params) == 0) {
                indicating = 1U; // 设置正在发送标志
            }
        }
    }
    return 0;
}