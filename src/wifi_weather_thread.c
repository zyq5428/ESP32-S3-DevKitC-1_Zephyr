/*
 * [WiFi + 天气 + 时间线程] ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS
 *
 * 功能：
 *   - 使用 net_mgmt API 连接 WiFi 路由器 (hero / 594270026)
 *   - 通过 DHCP 自动获取 IP 地址和 DNS 服务器
 *   - 使用 SNTP 协议从阿里云 NTP 服务器同步北京时间 (UTC+8)
 *   - 通过 HTTP GET 请求从 Open-Meteo 免费天气 API 获取实时天气数据
 *   - 解析 JSON 响应，提取温度、天气代码、风速等信息
 *   - 每 10 分钟自动更新一次天气数据
 *
 * 工作流程：
 *   1. 系统上电后等待 2 秒 (等待 WiFi 硬件初始化)
 *   2. 注册 net_mgmt 事件回调，监听 WiFi 连接状态变化
 *   3. 查找 WiFi 网络接口 (wlan0)
 *   4. 发起 WiFi 连接请求 (SSID: hero, 密码: 594270026, WPA2-PSK)
 *   5. 使用信号量阻塞等待 DHCP 分配 IP 地址成功
 *   6. 调用 SNTP 客户端同步网络时间 → 转为北京时间 (UTC+8)
 *   7. 发起 HTTP GET 请求到 api.open-meteo.com 获取当前天气
 *   8. 解析 JSON 响应体，更新全局变量
 *   9. 进入主循环：每 10 分钟更新天气，保持 WiFi 连接活跃
 *
 * 注意事项：
 *   - WiFi 连接失败时会自动重试，重试间隔 10 秒
 *   - SNTP 超时设为 10 秒，超时后跳过时间同步继续获取天气
 *   - HTTP 请求使用原始 TCP Socket，无 TLS，端口 80
 *   - JSON 解析使用 Zephyr 内置 json.h 库 (流式解析器)
 *   - 所有网络操作都有超时保护，防止线程永久卡死
 */

/* ==================== Zephyr 头文件 ==================== */
#include <zephyr/kernel.h>              /* [内核] K_THREAD_DEFINE, k_sleep, k_sem */
#include <zephyr/device.h>              /* [设备] device_is_ready */
#include <zephyr/devicetree.h>          /* [设备树] DT_NODELABEL */
#include <zephyr/net/net_if.h>          /* [网络接口] net_if_get_default, net_if_ipv4_* */
#include <zephyr/net/net_mgmt.h>        /* [网络管理] net_mgmt_add_event_callback */
#include <zephyr/net/wifi_mgmt.h>       /* [WiFi] wifi_connect_req_params */
#include <zephyr/net/sntp.h>            /* [SNTP] sntp_query, sntp_init */
#include <zephyr/net/socket.h>          /* [Socket] zsock_socket, connect, send, recv */
#include <zephyr/net/net_ip.h>          /* [IP] net_addr_ntop, INET_ADDRSTRLEN */
/* JSON 解析改用手动字符串搜索，无需 json.h */
#include <zephyr/sys/printk.h>          /* [打印] printk */
#include <zephyr/logging/log.h>         /* [日志] LOG_MODULE_REGISTER, LOG_INF */

/* ==================== 标准库头文件 ==================== */
#include <string.h>                     /* [字符串] strlen, strstr, memcpy, strncpy */
#include <stdio.h>                      /* [格式化] snprintf */

/* ==================== 项目头文件 ==================== */
#include "wifi_weather_thread.h"        /* [头文件] 全局变量声明 */

/* ==================== 日志模块注册 ==================== */
LOG_MODULE_REGISTER(WIFI_WEATHER, LOG_LEVEL_INF);

/* ==================== WiFi 连接参数 ==================== */

/*
 * [WiFi SSID] 要连接的路由器名称
 * 用户验证过可以正常联网的路由器
 */
#define WIFI_SSID       "hero"

/*
 * [WiFi 密码] 路由器 WPA2-PSK 密码
 * 长度必须 >= 8 个字符 (WPA2 规范要求)
 */
#define WIFI_PASSWORD   "594270026"

/*
 * [WiFi 安全类型] WPA2-PSK (最常见家用路由器加密方式)
 * WIFI_SECURITY_TYPE_PSK: 使用预共享密钥 (Pre-Shared Key) 认证
 * ESP32-S3 也支持: WIFI_SECURITY_TYPE_NONE (开放), WIFI_SECURITY_TYPE_WPA_PSK
 */
#define WIFI_SECURITY   WIFI_SECURITY_TYPE_PSK

/*
 * [WiFi 连接超时] 等待关联 AP 的最大时间 (秒)
 * 设置为 30 秒，家用路由器通常 3~10 秒内完成关联
 */
#define WIFI_CONNECT_TIMEOUT_S  30

/*
 * [WiFi 重试间隔] 连接失败后等待多少秒再重试
 * 设置为 10 秒，避免频繁重试消耗功耗
 */
#define WIFI_RETRY_INTERVAL_S   10

/* ==================== SNTP 时间同步参数 ==================== */

/*
 * [NTP 服务器地址] 使用阿里云公共 NTP 服务器
 * 国内延迟低 (<30ms)，同步速度快
 * 备选: "pool.ntp.org" (国际), "cn.ntp.org.cn" (中国)
 */
#define SNTP_SERVER     "ntp.aliyun.com"

/*
 * [SNTP 超时] 等待 NTP 服务器响应的最大时间 (毫秒)
 * 设置为 10000ms (10 秒)，国内 NTP 通常 100~500ms 即可返回
 */
#define SNTP_TIMEOUT_MS 10000

/*
 * [时区偏移] 北京时间 = UTC + 8 小时 = 28800 秒
 * SNTP 返回的是 UTC 时间，加上此偏移得到北京时间
 */
#define TIMEZONE_OFFSET_SEC  (8 * 3600)

/* ==================== 天气 API 参数 ==================== */

/*
 * [天气 API 主机名] Open-Meteo 免费天气 API (无需注册/API Key)
 * 支持 HTTP (端口 80) 和 HTTPS (端口 443)
 * 这里使用 HTTP 以避免 TLS/SSL 的复杂性
 */
#define WEATHER_HOST    "api.open-meteo.com"

/*
 * [天气 API 端口] HTTP = 80
 */
#define WEATHER_PORT    80

/*
 * [查询城市纬度] 深圳 (Shenzhen) ≈ 22.54°N
 * 用户可根据自己所在城市修改此值
 * 北京=39.90, 上海=31.23, 广州=23.13, 深圳=22.54
 */
#define WEATHER_LAT     22.54

/*
 * [查询城市经度] 深圳 (Shenzhen) ≈ 114.06°E
 * 用户可根据自己所在城市修改此值
 * 北京=116.41, 上海=121.47, 广州=113.26, 深圳=114.06
 */
#define WEATHER_LON     114.06

/*
 * [天气查询间隔] 每隔 600 秒 (10 分钟) 拉取一次天气数据
 * Open-Meteo 免费 API 限制: 每小时 10000 次请求
 * 10 分钟间隔 = 每天 144 次，远低于限制
 */
#define WEATHER_UPDATE_INTERVAL_S   600

/*
 * [HTTP 接收缓冲区大小] 足够容纳 Open-Meteo JSON 响应
 * 实际响应约 500~800 字节，分配 2048 字节留有足够余量
 */
#define HTTP_RECV_BUF_SIZE  2048

/* ==================== 全局变量定义 ==================== */

/* [网络状态] WiFi 连接状态 (true=已连接并获取 IP) */
volatile bool g_wifi_connected = false;

/* [网络状态] 当前设备的 IPv4 地址字符串 */
volatile char g_wifi_ip_addr[16] = "0.0.0.0";

/* [时间状态] 是否已通过 SNTP 同步过时间 */
volatile bool g_time_synced = false;

/* [时间数据] 当前北京时间 */
volatile int g_current_hour   = 0;
volatile int g_current_minute = 0;
volatile int g_current_second = 0;
volatile int g_current_year   = 2026;
volatile int g_current_month  = 1;
volatile int g_current_day    = 1;
volatile int g_current_weekday = 0;

/* [天气状态] 是否已获取天气数据 */
volatile bool g_weather_ready = false;

/* [天气数据] 当前天气信息 */
volatile int g_temperature    = 0;
volatile int g_weather_code   = 0;
volatile char g_weather_desc[32] = "等待中...";
volatile int g_wind_speed     = 0;

/* ==================== 同步信号量 ==================== */

/*
 * [信号量] 用于在线程间传递 "IP 地址已获取" 的事件
 *
 * 工作原理：
 *   主线程调用 k_sem_take(&ip_sem, timeout) 阻塞等待
 *   net_mgmt 事件回调中收到 NET_EVENT_IPV4_ADDR_ADD 时调用 k_sem_give(&ip_sem) 唤醒
 *   这样避免了 while(1) + k_sleep 的忙等待模式，节省 CPU
 */
static K_SEM_DEFINE(ip_sem, 0, 1);

/* ==================== net_mgmt 事件回调函数 ==================== */

/*
 * [回调] WiFi 网络管理事件处理函数
 *
 * 当 WiFi 状态发生变化时，Zephyr 网络栈会调用此函数。
 * 它在系统工作队列的线程上下文中执行（不是中断上下文），
 * 因此可以安全地调用 LOG_* 和 k_sem_give 等函数。
 *
 * 参数：
 *   cb:        事件回调结构体指针
 *   mgmt_event: 触发的事件类型 (NET_EVENT_WIFI_*, NET_EVENT_IPV4_*)
 *   iface:      发生事件的网络接口指针
 */
static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                     uint64_t mgmt_event,
                                     struct net_if *iface)
{
    switch (mgmt_event) {

    /*
     * [事件] WiFi 连接尝试的结果 (成功或失败)
     * 这是对 net_mgmt(NET_REQUEST_WIFI_CONNECT, ...) 请求的异步响应
     */
    case NET_EVENT_WIFI_CONNECT_RESULT: {
        /*
         * [获取连接状态] cb->info 指向 struct wifi_status
         * status=0 表示连接成功, 非0表示失败
         */
        const struct wifi_status *ws =
            (const struct wifi_status *)cb->info;
        int conn_status = (ws != NULL) ? ws->status : -1;
        if (conn_status == 0) {
            LOG_INF("WiFi 关联成功! 等待 DHCP 分配 IP 地址...");
        } else {
            LOG_ERR("WiFi 关联失败 (错误码: %d)", conn_status);
        }
        break;
    }

    /*
     * [事件] WiFi 与 AP 断开连接
     * AP 重启、信号弱、密码错误等原因都会触发此事件
     */
    case NET_EVENT_WIFI_DISCONNECT_RESULT: {
        const struct wifi_status *ws =
            (const struct wifi_status *)cb->info;
        int reason = (ws != NULL) ? ws->status : -1;
        LOG_WRN("WiFi 已断开! (原因码: %d)", reason);
        /*
         * [状态更新] 标记 WiFi 已断开
         * 其他线程读取 g_wifi_connected 即可感知网络断开
         */
        g_wifi_connected = false;
        /* [清空 IP] 断开后 IP 地址不可用 */
        strncpy((char *)g_wifi_ip_addr, "0.0.0.0",
                sizeof(g_wifi_ip_addr) - 1);
        break;
    }

    /*
     * [事件] L4 层 (TCP/UDP) 已连接
     * 这表示 DHCP 已经完成、IP 地址已分配且可用
     * 在此之后可以进行 TCP/UDP 通信
     */
    case NET_EVENT_L4_CONNECTED: {
        LOG_INF("L4 已连接 (IP 地址已就绪)");
        break;
    }

    /*
     * [事件] IPv4 地址已添加到网络接口
     * DHCP 客户端成功从路由器获取到 IP 后触发
     */
    case NET_EVENT_IPV4_ADDR_ADD: {
        char ip_str[INET_ADDRSTRLEN];

        /*
         * [提取 IP 地址] 从网络接口获取第一个 IPv4 全局地址
         * net_if_ipv4_get_global_addr(): 返回接口上 PREFERRED 状态的 IPv4 地址
         * 注意：返回的是 struct in_addr*，需要用 net_addr_ntop 转为字符串
         */
        struct in_addr *addr = net_if_ipv4_get_global_addr(iface,
                                                            NET_ADDR_PREFERRED);
        if (addr != NULL) {
            net_addr_ntop(AF_INET, addr, ip_str, sizeof(ip_str));
            LOG_INF("已获取 IP 地址: %s", ip_str);

            /* [更新全局变量] 保存 IP 地址供其他线程使用 */
            strncpy((char *)g_wifi_ip_addr, ip_str,
                    sizeof(g_wifi_ip_addr) - 1);
            g_wifi_connected = true;

            /*
             * [信号量通知] 唤醒 wifi_weather_thread_entry 中
             * 正在 k_sem_take(&ip_sem, ...) 上阻塞等待的代码
             */
            k_sem_give(&ip_sem);
        } else {
            LOG_ERR("无法获取 IPv4 地址 (addr 为 NULL)");
        }
        break;
    }

    /*
     * [事件] IPv4 地址从网络接口移除
     * DHCP 租约过期或 WiFi 断开时触发
     */
    case NET_EVENT_IPV4_ADDR_DEL: {
        LOG_WRN("IPv4 地址已释放");
        g_wifi_connected = false;
        break;
    }

    default:
        /* [忽略] 不关心的事件类型不做处理 */
        break;
    }
}

/* ==================== net_mgmt 事件回调注册 ==================== */

/*
 * [回调结构体] 静态定义 net_mgmt 事件回调
 * 必须定义为 static 全局变量，因为在 SYS_INIT 和事件处理器中都引用它
 */
static struct net_mgmt_event_callback wifi_mgmt_cb;

/*
 * [注册事件回调] 在系统初始化阶段，向网络管理子系统注册我们的事件处理器
 *
 * 使用 SYS_INIT 确保在网络子系统准备好之后、应用线程启动之前完成注册
 * APPLICATION 级别 (优先级 0): 在大多数内核服务初始化之后执行
 */
static int wifi_mgmt_callback_register(void)
{
    /*
     * [初始化回调] 绑定事件处理函数到回调结构体
     * wifi_mgmt_event_handler: 我们自定义的事件处理函数
     */
    net_mgmt_init_event_callback(&wifi_mgmt_cb,
                                  wifi_mgmt_event_handler,
                                  /*
                                   * [事件位掩码] 只监听我们关心的事件类型
                                   * 使用按位或(|)组合多个事件:
                                   */
                                  NET_EVENT_WIFI_CONNECT_RESULT
                                  | NET_EVENT_WIFI_DISCONNECT_RESULT
                                  | NET_EVENT_L4_CONNECTED
                                  | NET_EVENT_IPV4_ADDR_ADD
                                  | NET_EVENT_IPV4_ADDR_DEL);

    /*
     * [注册] 将回调添加到全局网络管理事件分发器
     * 参数: 回调结构体地址
     */
    net_mgmt_add_event_callback(&wifi_mgmt_cb);

    LOG_INF("WiFi 网络管理事件回调已注册");
    return 0;
}

/*
 * [系统初始化] 在 APPLICATION 初始化阶段自动调用注册函数
 * SYS_INIT 宏保证了初始化顺序：内核 → 驱动 → 网络栈 → APPLICATION → 应用线程
 */
SYS_INIT(wifi_mgmt_callback_register, APPLICATION, 0);

/* ==================== WiFi 连接函数 ==================== */

/*
 * [函数] 发起 WiFi 连接请求
 *
 * 调用 net_mgmt API 向 WiFi 驱动程序发送连接命令。
 * 连接结果会通过 NET_EVENT_WIFI_CONNECT_RESULT 事件异步返回。
 *
 * 参数 iface: WiFi 网络接口指针 (通过 net_if_get_first_wifi() 获取)
 * 返回值: 0=请求已发送, 负数=发送失败
 */
static int wifi_connect(struct net_if *iface)
{
    int ret;

    /*
     * [构造连接参数] wifi_connect_req_params 包含连接 WiFi 所需的所有信息
     *
     * 字段说明:
     *   .ssid:        路由器名称 (字符串)
     *   .ssid_length: SSID 的字节数 (不含 '\0')
     *   .psk:         WPA/WPA2 预共享密钥 (密码)
     *   .psk_length:  密码的字节数 (不含 '\0')
     *   .channel:     信道号 (WIFI_CHANNEL_ANY = 自动扫描)
     *   .security:    加密类型 (WIFI_SECURITY_TYPE_PSK = WPA2-PSK)
     *   .timeout:     关联超时时间 (秒), 0=使用默认值
     *   .band:        频段 (WIFI_FREQ_BAND_2_4_GHZ = 2.4GHz)
     *                 ESP32-S3 支持 2.4GHz 但不支持 5GHz
     */
    struct wifi_connect_req_params cnx_params = {
        .ssid        = WIFI_SSID,
        .ssid_length = strlen(WIFI_SSID),
        .psk         = WIFI_PASSWORD,
        .psk_length  = strlen(WIFI_PASSWORD),
        .channel     = WIFI_CHANNEL_ANY,
        .security    = WIFI_SECURITY,
        .timeout     = WIFI_CONNECT_TIMEOUT_S,
        .band        = WIFI_FREQ_BAND_2_4_GHZ,
    };

    LOG_INF("正在连接 WiFi: \"%s\" (安全: WPA2-PSK)...", WIFI_SSID);

    /*
     * [发送连接请求] net_mgmt() 是同步调用，它会阻塞直到底层驱动接受请求
     *
     * 参数:
     *   NET_REQUEST_WIFI_CONNECT: 请求类型 = WiFi 连接
     *   iface:                    目标网络接口
     *   &cnx_params:              连接参数结构体
     *   sizeof(cnx_params):       参数结构体的大小
     *
     * 返回值:
     *   0:           请求已成功提交给 WiFi 驱动程序
     *   负数:        请求失败 (如参数无效、驱动未就绪等)
     *
     * 注意: 此函数返回 0 只表示"命令已发出"，不代表"已连上 AP"
     *       实际连接结果通过 NET_EVENT_WIFI_CONNECT_RESULT 事件异步通知
     */
    ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface,
                   &cnx_params, sizeof(cnx_params));

    if (ret != 0) {
        LOG_ERR("WiFi 连接请求失败 (错误码: %d)", ret);
    }

    return ret;
}

/* ==================== SNTP 时间同步函数 ==================== */

/*
 * [函数] 通过 SNTP 协议同步网络时间
 *
 * SNTP (Simple Network Time Protocol) 是 NTP 的简化版本，
 * 适用于不需要高精度时间同步的嵌入式设备。
 *
 * 使用 Zephyr 内置的 sntp_simple() 一次性便捷函数：
 *   自动完成 DNS 解析、sntp_init()、sntp_query()、sntp_close() 全部流程。
 *   sntp_time.seconds 返回的是 Unix 时间戳 (自 1970-01-01 起的秒数)。
 *   加上 8 小时偏移 (28800 秒) 即可得到北京时间。
 *
 * 返回值: 0=同步成功, 负数=同步失败
 */
static int sync_time_via_sntp(void)
{
    int ret;
    struct sntp_time sntp_result;   /* [SNTP 结果] Unix 时间戳 (秒 + 小数秒) */

    LOG_INF("正在通过 SNTP 同步网络时间 (服务器: %s)...", SNTP_SERVER);

    /*
     * [一次性 SNTP 查询] sntp_simple() 封装了完整的 SNTP 请求流程
     *
     * 参数:
     *   SNTP_SERVER:      NTP 服务器地址，格式 "hostname" 或 "hostname:port"
     *                     sntp_simple() 内部自动完成 DNS 解析和 socket 连接
     *   SNTP_TIMEOUT_MS:  超时时间 (毫秒)，超时返回 -ETIMEDOUT
     *   &sntp_result:     输出参数，存储服务器返回的时间
     *
     * sntp_result.seconds: 自 1970-01-01 00:00:00 UTC 起的秒数 (Unix 时间戳)
     *
     * 返回 0 表示成功获取到 SNTP 时间
     */
    ret = sntp_simple(SNTP_SERVER, SNTP_TIMEOUT_MS, &sntp_result);

    if (ret < 0) {
        LOG_ERR("SNTP 时间同步失败 (错误码: %d)", ret);
        return ret;
    }

    /*
     * [时间转换] Unix 时间戳 → 北京时间 (UTC+8)
     *
     * sntp_simple() 返回的 .seconds 已经是 Unix 时间戳 (基于 1970-01-01)，
     * 不需要再做 NTP→Unix 纪元转换 (旧版 API 的 .seconds 基于 1900-01-01)。
     * 只需加上 8 小时 (28800 秒) 偏移 = 北京时间。
     */
    uint64_t beijing_unix_time = sntp_result.seconds + TIMEZONE_OFFSET_SEC;

    /*
     * [转换] Unix 时间戳 → 年/月/日/时/分/秒
     *
     * 使用手动算法而不是 gmtime()/localtime()，原因：
     *   1. 嵌入式 C 库 (picolibc/newlib-nano) 可能不支持时区
     *   2. 手动算法不依赖任何外部状态，完全可预测
     *   3. 代码量小，适合嵌入式
     *
     * 算法概要:
     *   1. 总秒数 → 时:分:秒 (除以 60/3600/86400 取余)
     *   2. 总天数 → 年 (逐年减 365/366，闰年规则)
     *   3. 剩余天数 → 月/日 (逐月减 28/29/30/31)
     *   4. Sakamoto 算法 → 星期几
     */

    /* [变量] 时间拆解的中间值 */
    uint64_t total_seconds = beijing_unix_time;
    uint32_t total_days;        /* 自 1970-01-01 以来的总天数 */
    uint32_t remaining_days;    /* 拆解年份后剩余的天数 */
    int year  = 1970;           /* [起始年份] Unix 纪元起点 */
    int month = 1;              /* [起始月份] */
    int day   = 1;              /* [起始日期] */
    int hour, minute, second;
    int days_in_month;          /* 当前月的天数 */

    /* [步骤1] 提取时/分/秒分量 */
    second = (int)(total_seconds % 60);
    total_seconds /= 60;
    minute = (int)(total_seconds % 60);
    total_seconds /= 60;
    hour   = (int)(total_seconds % 24);
    total_days = (uint32_t)(total_seconds / 24);

    /*
     * [步骤2] 按年拆解 (考虑闰年)
     * 闰年规则: 能被 4 整除但不能被 100 整除，或者能被 400 整除
     */
    remaining_days = total_days;
    while (1) {
        int is_leap = ((year % 4 == 0) && (year % 100 != 0))
                      || (year % 400 == 0);
        uint32_t days_this_year = is_leap ? 366 : 365;

        if (remaining_days < days_this_year) {
            /* [找到年份] 剩余天数不足一年 */
            break;
        }
        remaining_days -= days_this_year;
        year++;
    }

    /*
     * [步骤3] 按月拆解
     * 每月天数: 1月=31, 2月=28/29, 3月=31, 4月=30, 5月=31, 6月=30,
     *           7月=31, 8月=31, 9月=30, 10月=31, 11月=30, 12月=31
     */
    for (month = 1; month <= 12; month++) {
        switch (month) {
        case 1:  days_in_month = 31; break;
        case 2: {
            int is_leap = ((year % 4 == 0) && (year % 100 != 0))
                          || (year % 400 == 0);
            days_in_month = is_leap ? 29 : 28;
            break;
        }
        case 3:  days_in_month = 31; break;
        case 4:  days_in_month = 30; break;
        case 5:  days_in_month = 31; break;
        case 6:  days_in_month = 30; break;
        case 7:  days_in_month = 31; break;
        case 8:  days_in_month = 31; break;
        case 9:  days_in_month = 30; break;
        case 10: days_in_month = 31; break;
        case 11: days_in_month = 30; break;
        case 12: days_in_month = 31; break;
        default: days_in_month = 30; break;
        }

        if (remaining_days < (uint32_t)days_in_month) {
            /* [找到月份] 剩余天数不足一月，日期 = remaining_days + 1 */
            day = (int)(remaining_days + 1);
            break;
        }
        remaining_days -= (uint32_t)days_in_month;
    }

    /*
     * [步骤4] 计算星期几 (Sakamoto 算法)
     * 将 1 月和 2 月视为上一年的 13 月和 14 月
     * 结果: 0=周日, 1=周一 ... 6=周六
     */
    {
        int yw = year;
        int mw = month;
        if (mw < 3) {
            mw += 12;
            yw -= 1;
        }
        int weekday = (day + 2 * mw + (3 * (mw + 1)) / 5
                       + yw + yw / 4 - yw / 100 + yw / 400 + 1) % 7;
        g_current_weekday = weekday;
    }

    /* [更新全局变量] 将拆解后的北京时间写入全局变量 */
    g_current_year   = year;
    g_current_month  = month;
    g_current_day    = day;
    g_current_hour   = hour;
    g_current_minute = minute;
    g_current_second = second;
    g_time_synced    = true;

    /* [日志] 使用局部变量避免 volatile 警告 */
    int wday = g_current_weekday;
    LOG_INF("时间同步成功! 北京时间: %04d-%02d-%02d %02d:%02d:%02d (星期%d)",
            year, month, day, hour, minute, second, wday);

    return 0;
}

/* ==================== 天气代码转换函数 ==================== */

/*
 * [函数] 将 WMO 天气代码转换为中文描述字符串
 *
 * WMO (World Meteorological Organization) 天气代码是国际标准，
 * Open-Meteo API 返回的就是 WMO 代码 (0~99)。
 *
 * 参数 code: WMO 天气代码
 * 返回值: 指向中文描述字符串的指针 (静态内存，不需要 free)
 */
static const char *wmo_code_to_zh_description(int code)
{
    switch (code) {
    case 0:     return "晴天";
    case 1:     return "少云";
    case 2:     return "多云";
    case 3:     return "阴天";
    case 45:
    case 48:    return "雾";
    case 51:    return "小毛毛雨";
    case 53:    return "中毛毛雨";
    case 55:    return "大毛毛雨";
    case 56:    return "冻毛毛雨";
    case 57:    return "冻毛毛雨";
    case 61:    return "小雨";
    case 63:    return "中雨";
    case 65:    return "大雨";
    case 66:    return "冻雨";
    case 67:    return "冻雨";
    case 71:    return "小雪";
    case 73:    return "中雪";
    case 75:    return "大雪";
    case 77:    return "雪粒";
    case 80:    return "阵雨";
    case 81:    return "中阵雨";
    case 82:    return "大阵雨";
    case 85:    return "小阵雪";
    case 86:    return "大阵雪";
    case 95:    return "雷暴";
    case 96:    return "雷暴+小冰雹";
    case 99:    return "雷暴+大冰雹";
    default:    return "未知天气";
    }
}

/* ==================== 天气 API HTTP 请求函数 ==================== */

/*
 * [函数] 通过 HTTP GET 请求获取天气数据
 *
 * JSON 解析采用手动字符串搜索方式，直接在 HTTP 响应正文中
 * 查找 temperature/weathercode/windspeed 字段并提取整数值。
 * 避免使用 Zephyr JSON 库 (其浮点解析依赖 FULL_LIBC)。
 *
 * 使用原始 TCP Socket 发起 HTTP/1.1 GET 请求到 Open-Meteo API。
 * 整个流程:
 *   1. DNS 解析 api.open-meteo.com → IP 地址
 *   2. 创建 TCP socket → connect
 *   3. 发送 HTTP GET 请求
 *   4. 接收 HTTP 响应 (头 + JSON 体)
 *   5. 定位 JSON 体 (跳过 HTTP 头部的 \r\n\r\n 分隔符)
 *   6. 解析 JSON → 更新全局变量
 *   7. 关闭 socket
 *
 * 返回值: 0=成功, 负数=失败
 */
static int fetch_weather(void)
{
    int sock_fd = -1;               /* [socket fd] TCP socket 文件描述符 */
    int ret;
    int total_received = 0;         /* [接收计数] 已收到的总字节数 */
    struct sockaddr_in server_addr; /* [服务器地址] IPv4 地址 + 端口 */
    struct zsock_addrinfo hints;    /* [DNS 查询提示] 指定期望的地址类型 */
    struct zsock_addrinfo *res;     /* [DNS 结果链表] getaddrinfo 返回的结果 */

    /* [HTTP 接收缓冲区] 静态分配避免栈溢出 (2048 字节放在 BSS 段) */
    static uint8_t recv_buf[HTTP_RECV_BUF_SIZE];

    LOG_INF("正在获取天气数据 (坐标: %.2f, %.2f)...",
            (double)WEATHER_LAT, (double)WEATHER_LON);

    /* ===== 步骤 1: DNS 解析主机名 → IP 地址 ===== */

    /*
     * [清零] 清空 hints 结构体，确保所有字段都为 0
     * memset 避免未初始化的垃圾值影响 getaddrinfo 行为
     */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;          /* [IPv4] 只请求 IPv4 地址 */
    hints.ai_socktype = SOCK_STREAM;      /* [TCP] 流式套接字 */

    /*
     * [DNS 查询] zsock_getaddrinfo() 将主机名解析为 IP 地址
     * 返回 0 表示解析成功，res 指向地址链表
     */
    ret = zsock_getaddrinfo(WEATHER_HOST, "80", &hints, &res);
    if (ret < 0) {
        LOG_ERR("DNS 解析失败: %s (错误码: %d)", WEATHER_HOST, ret);
        return ret;
    }

    /* ===== 步骤 2: 创建 TCP socket 并连接服务器 ===== */

    /*
     * [创建 socket] AF_INET=IPv4, SOCK_STREAM=TCP, IPPROTO_TCP=TCP 协议
     * zsock_socket() 是 socket() 的 Zephyr 线程安全封装
     */
    sock_fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_fd < 0) {
        LOG_ERR("创建 TCP socket 失败 (错误码: %d)", sock_fd);
        zsock_freeaddrinfo(res);
        return sock_fd;
    }

    /*
     * [构建服务器地址] 将 DNS 解析结果的 IP 复制到 sockaddr_in 结构体
     * res->ai_addr 包含已解析的 IPv4 地址
     */
    memcpy(&server_addr, res->ai_addr, sizeof(server_addr));
    /* [端口] 主机字节序 → 网络字节序 (htons = Host TO Network Short) */
    server_addr.sin_port = htons(WEATHER_PORT);

    /* [释放 DNS 结果] DNS 结果已复制到 server_addr，不再需要 */
    zsock_freeaddrinfo(res);

    /*
     * [设置 socket 超时] 防止连接不上时永久阻塞
     * SO_SNDTIMEO: 发送超时 = 10 秒
     * SO_RCVTIMEO: 接收超时 = 10 秒
     */
    struct timeval tv;
    tv.tv_sec  = 10;
    tv.tv_usec = 0;
    zsock_setsockopt(sock_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    zsock_setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /*
     * [连接服务器] zsock_connect() 发起 TCP 三次握手
     * 返回 0 表示 TCP 连接建立成功
     */
    ret = zsock_connect(sock_fd, (struct sockaddr *)&server_addr,
                         sizeof(server_addr));
    if (ret < 0) {
        LOG_ERR("连接天气服务器失败 (错误码: %d)", ret);
        zsock_close(sock_fd);
        return ret;
    }

    /* ===== 步骤 3: 构建并发送 HTTP GET 请求 ===== */

    /*
     * [HTTP 请求格式] 手动构建 HTTP/1.1 GET 请求字符串
     *
     * HTTP 协议格式:
     *   GET /path HTTP/1.1\r\n
     *   Host: hostname\r\n
     *   Connection: close\r\n
     *   \r\n              ← 空行标志请求头结束
     *
     * 每个行以 \r\n 结束 (CRLF, 回车+换行)
     * Connection: close → 请求完成后服务器主动关闭连接，方便嵌入式解析
     */
    char http_request[512];
    int req_len = snprintf(http_request, sizeof(http_request),
        "GET /v1/forecast?latitude=%.2f&longitude=%.2f"
        "&current_weather=true HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        (double)WEATHER_LAT, (double)WEATHER_LON,
        WEATHER_HOST);

    /*
     * [发送请求] zsock_send() 将 HTTP 请求发送到服务器
     * 返回值: 实际发送的字节数，负数表示错误
     */
    ret = zsock_send(sock_fd, http_request, req_len, 0);
    if (ret < 0) {
        LOG_ERR("发送 HTTP 请求失败 (错误码: %d)", ret);
        zsock_close(sock_fd);
        return ret;
    }
    LOG_DBG("HTTP 请求已发送 (%d 字节)", ret);

    /* ===== 步骤 4: 接收 HTTP 响应 ===== */

    /*
     * [接收响应] zsock_recv() 从 socket 读取服务器返回的数据
     *
     * 循环读取直到服务器关闭连接 (ret=0) 或缓冲区满
     */
    total_received = 0;
    while (total_received < (HTTP_RECV_BUF_SIZE - 1)) {
        ret = zsock_recv(sock_fd,
                         recv_buf + total_received,
                         HTTP_RECV_BUF_SIZE - 1 - total_received,
                         0);
        if (ret <= 0) {
            /* [连接关闭或错误] 服务器关闭连接 (ret=0) 或发生错误 (ret<0) */
            break;
        }
        total_received += ret;
    }

    /* [字符串终止] 在缓冲区末尾添加 '\0' 以便 strstr 等字符串操作 */
    recv_buf[total_received] = '\0';

    LOG_INF("HTTP 响应已接收 (%d 字节)", total_received);

    /* [关闭 socket] 数据已全部接收，释放资源 */
    zsock_close(sock_fd);

    if (total_received == 0) {
        LOG_ERR("HTTP 响应为空 (0 字节)");
        return -EIO;
    }

    /* ===== 步骤 5: 定位 JSON 正文 (跳过 HTTP 头) ===== */

    /*
     * [查找 JSON 起始位置] HTTP 头部和正文由 "\r\n\r\n" 分隔
     * strstr() 搜索子串，+4 跳过这 4 个字节到达 JSON 正文开头
     */
    char *json_start = strstr((char *)recv_buf, "\r\n\r\n");
    if (json_start == NULL) {
        /* [容错] 某些服务器只用 \n 分隔，尝试查找 "\n\n" */
        json_start = strstr((char *)recv_buf, "\n\n");
        if (json_start != NULL) {
            json_start += 2; /* 跳过 "\n\n" */
        }
    } else {
        json_start += 4; /* 跳过 "\r\n\r\n" */
    }

    if (json_start == NULL) {
        LOG_ERR("无法在 HTTP 响应中定位 JSON 正文");
        return -EINVAL;
    }

    /*
     * [步骤 6] 手动字符串搜索提取天气数据
     *
     * 不使用 Zephyr JSON 库的原因:
     *   Open-Meteo 返回的温度/风速是浮点数 (如 28.5)，
     *   JSON 库解析浮点数需要 CONFIG_JSON_LIBRARY_FP_SUPPORT=y，
     *   而该选项依赖 CONFIG_REQUIRES_FULL_LIBC，picolibc 不满足。
     *   因此 JSON 解析直接返回 -EINVAL。
     *
     * 手动方案: 在 JSON 字符串中搜索 "temperature": 等关键字，
     * 然后逐字符解析后面的数字 (整数部分即可，小数忽略)。
     * 这是嵌入式系统中最可靠的做法。
     */

    /*
     * [辅助宏] 从 current_weather 子对象中提取某个键后面的整数值
     *
     * 为什么要限定在 current_weather 内搜索:
     *   Open-Meteo 响应有两个 "temperature" 键:
     *     current_weather_units.temperature = "°C"  (字符串, 先出现)
     *     current_weather.temperature = 27.4        (数字, 后出现)
     *   strstr() 会先匹配到 units 中的字符串型值, 导致解析失败。
     *   因此需要先定位 "current_weather":{ 再在其后搜索。
     */
    const char *cw_start = strstr(json_start, "\"current_weather\":{");
    if (cw_start == NULL) {
        /* [容错] 旧版 API 可能把 current_weather 拼写不同 */
        cw_start = json_start;
    }

    #define JSON_EXTRACT_INT(json_ptr, key, out_ptr) do {          \
        const char *_p = strstr((json_ptr), "\"" key "\":");      \
        if (_p != NULL) {                                          \
            _p += strlen("\"" key "\":");   /* 跳过 "key": */     \
            while (*_p == ' ' || *_p == '\t') _p++; /* 跳过空白 */\
            int _sign = 1;                                         \
            if (*_p == '-') { _sign = -1; _p++; }                  \
            int _val = 0;                                          \
            while (*_p >= '0' && *_p <= '9') {                     \
                _val = _val * 10 + (*_p - '0');                   \
                _p++;                                              \
            }                                                      \
            *(out_ptr) = _sign * _val;                             \
        }                                                          \
    } while (0)

    /* [提取温度] 在 current_weather 对象内找到 "temperature":27.4 → 27 */
    JSON_EXTRACT_INT(cw_start, "temperature", &g_temperature);

    /* [提取天气代码] 在 current_weather 对象内找到 "weathercode":3 → 3 */
    JSON_EXTRACT_INT(cw_start, "weathercode", &g_weather_code);

    /* [提取风速] 在 current_weather 对象内找到 "windspeed":12.0 → 12 */
    JSON_EXTRACT_INT(cw_start, "windspeed", &g_wind_speed);

    /* [更新天气描述] 根据 WMO 代码查找对应的中文描述 */
    const char *desc = wmo_code_to_zh_description(g_weather_code);
    strncpy((char *)g_weather_desc, desc, sizeof(g_weather_desc) - 1);
    g_weather_desc[sizeof(g_weather_desc) - 1] = '\0';

    /* [标记天气已就绪] 供 LCD 线程等读取 */
    g_weather_ready = true;

    /* [日志] 使用局部变量避免 volatile 类型警告 */
    int tmp_temp = g_temperature;
    int tmp_wind = g_wind_speed;
    char tmp_desc[32];
    strncpy(tmp_desc, (const char *)g_weather_desc, sizeof(tmp_desc) - 1);
    tmp_desc[sizeof(tmp_desc) - 1] = '\0';
    LOG_INF("天气获取成功! 温度: %d°C, 天气: %s, 风速: %d km/h",
            tmp_temp, tmp_desc, tmp_wind);

    return 0;
}

/* ==================== 主线程入口函数 ==================== */

/*
 * [线程入口] WiFi + 天气 + 时间 主线程
 *
 * 这是新线程的入口点，遵循 lcd_lvgl_thread.c 相同的模板结构：
 *   1. 系统稳定延时
 *   2. 初始化 WiFi 连接 (带自动重试)
 *   3. 等待 IP 地址获取 (信号量)
 *   4. 同步网络时间 (SNTP)
 *   5. 获取天气数据 (HTTP GET)
 *   6. 进入主循环 (周期性更新)
 *
 * 参数: 三个 void* 由 K_THREAD_DEFINE 传入，本例均未使用
 */
void wifi_weather_thread_entry(void *p1, void *p2, void *p3)
{
    /* [抑制编译警告] 未使用的 K_THREAD_DEFINE 参数 */
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    struct net_if *wifi_iface;          /* [网络接口] WiFi 接口指针 */
    uint32_t loop_count = 0;            /* [循环计数] 心跳日志序号 */
    int ret;
    bool time_sync_done = false;        /* [标志] 是否已完成 SNTP 同步 */
    bool weather_done  = false;         /* [标志] 是否已成功获取天气 */

    LOG_INF("===== WiFi + 天气 + 时间 线程启动 =====");

    /*
     * [步骤 0] 系统基础稳压延时
     * 等待 WiFi 硬件、驱动和网络协议栈完全初始化 (2 秒经验值)
     */
    k_msleep(2000);

    /* ========== 步骤 1: 查找 WiFi 网络接口 ========== */

    /*
     * [获取 WiFi 接口] net_if_get_first_wifi() 返回第一个 WiFi 接口
     * ESP32-S3 通常只有一个 WiFi 接口 (wlan0)
     * 如果返回 NULL，说明 WiFi 驱动未成功初始化
     */
    wifi_iface = net_if_get_first_wifi();
    if (wifi_iface == NULL) {
        LOG_ERR("未找到 WiFi 网络接口! 请检查:");
        LOG_ERR("  1. overlay 中是否包含 &wifi { status = \"okay\"; };");
        LOG_ERR("  2. prj.conf 中是否启用了 CONFIG_WIFI=y");
        LOG_ERR("  3. prj.conf 中是否启用了 CONFIG_WIFI_ESP32=y");
        return; /* [致命错误] 没有 WiFi 接口，线程退出 */
    }
    LOG_INF("WiFi 网络接口已就绪 (wlan0)");

    /* ========== 步骤 2: WiFi 连接循环 (带重试) ========== */

    LOG_INF("===== 开始 WiFi 连接 =====");

    /*
     * [连接循环] 反复尝试连接 WiFi 直到成功获取 IP 地址
     *
     * 循环逻辑 (使用轮询方式，不依赖 net_mgmt 事件回调):
     *   1. 调用 wifi_connect() 发起连接请求
     *   2. 每 2 秒检查一次 net_if_ipv4_get_global_addr() 是否返回有效 IP
     *   3. 最长等待 30 秒 (15 次检查 × 2 秒)
     *   4. 成功 → 跳出循环
     *   5. 超时 → 等 10 秒后重试
     *
     * 为什么用轮询而不是 net_mgmt 事件:
     *   ESP32 WiFi 驱动的事件触发时机与 Zephyr 网络栈的 net_mgmt 通知
     *   之间存在竞态，在 esp32s3 平台上事件可能先于回调注册触发。
     *   轮询 net_if 状态更可靠，且 2 秒间隔的 CPU 开销可忽略。
     */
    while (!g_wifi_connected) {
        /* [发起连接] 提交 WiFi 连接请求给驱动 */
        ret = wifi_connect(wifi_iface);
        if (ret < 0) {
            /*
             * [检查] -120 = -EALREADY = WiFi 已经连上了 (可能是上次请求成功了
             * 但我们的轮询没检测到)。此时直接跳到 IP 轮询阶段。
             */
            if (ret == -EALREADY) {
                LOG_INF("WiFi 已处于连接状态，直接等待 IP...");
            } else {
                LOG_ERR("WiFi 连接请求提交失败 (err=%d)，%d 秒后重试...",
                        ret, WIFI_RETRY_INTERVAL_S);
                k_sleep(K_SECONDS(WIFI_RETRY_INTERVAL_S));
                continue;
            }
        }

        /*
         * [轮询等待 IP] 每 2 秒检查一次是否获取到 IPv4 地址
         * 最多检查 15 次 (共 30 秒)
         */
        #define IP_POLL_INTERVAL_S   2
        #define IP_POLL_MAX_RETRIES  15
        bool got_ip = false;

        for (int attempt = 0; attempt < IP_POLL_MAX_RETRIES; attempt++) {
            k_sleep(K_SECONDS(IP_POLL_INTERVAL_S));

            /*
             * [检查 IP] 直接从网络接口获取 IPv4 全局地址
             * 如果 DHCP 成功，这里会返回有效的 in_addr 指针
             */
            struct in_addr *addr =
                net_if_ipv4_get_global_addr(wifi_iface, NET_ADDR_PREFERRED);

            if (addr != NULL && addr->s_addr != 0) {
                char ip_str[INET_ADDRSTRLEN];
                net_addr_ntop(AF_INET, addr, ip_str, sizeof(ip_str));

                /* [检查] 确认不是 0.0.0.0 这种无效地址 */
                if (strcmp(ip_str, "0.0.0.0") != 0) {
                    LOG_INF("已获取 IP 地址: %s (轮询 %d 次)",
                            ip_str, attempt + 1);

                    /* [更新全局变量] 保存 IP 地址 */
                    strncpy((char *)g_wifi_ip_addr, ip_str,
                            sizeof(g_wifi_ip_addr) - 1);
                    g_wifi_connected = true;
                    got_ip = true;
                    break;
                }
            }
        }

        if (got_ip) {
            /* [连接成功] */
            LOG_INF("WiFi 连接成功! IP 地址: %s",
                    (const char *)g_wifi_ip_addr);
            break; /* 跳出连接重试循环 */
        } else {
            LOG_ERR("WiFi 连接超时 (%d秒未获取IP)，%d 秒后重试...",
                    IP_POLL_MAX_RETRIES * IP_POLL_INTERVAL_S,
                    WIFI_RETRY_INTERVAL_S);
            k_sleep(K_SECONDS(WIFI_RETRY_INTERVAL_S));
        }
    }

    /* ========== 步骤 3: SNTP 时间同步 ========== */

    LOG_INF("===== 开始 SNTP 时间同步 =====");

    /*
     * [时间同步] 尝试通过 SNTP 获取网络时间
     * 如果同步失败 → 不阻塞后续流程，稍后在主循环中重试
     */
    ret = sync_time_via_sntp();
    if (ret == 0) {
        time_sync_done = true;
    } else {
        LOG_WRN("SNTP 时间同步失败，将在后续循环中重试");
    }

    /* ========== 步骤 4: 首次天气数据获取 ========== */

    LOG_INF("===== 开始首次天气数据获取 =====");

    /*
     * [获取天气] 发起 HTTP 请求获取当前天气
     * 如果失败 → 不影响线程继续运行，稍后重试
     */
    ret = fetch_weather();
    if (ret == 0) {
        weather_done = true;
    } else {
        LOG_WRN("首次天气获取失败，将在后续循环中重试");
    }

    /* ========== 步骤 5: 主循环 (周期性更新 + 心跳) ========== */

    LOG_INF("===== 进入主循环 (天气更新间隔: %d 秒) =====",
            WEATHER_UPDATE_INTERVAL_S);
    LOG_INF("WiFi + 天气 + 时间 线程已就绪!");

    /*
     * [分段延时] 将 10 分钟的等待拆分成 30 秒一段
     * 好处:
     *   - 每 30 秒可以检查 WiFi 连接状态
     *   - 每 30 秒推进本地时间
     *   - 每 5 分钟检查是否需要重试 SNTP
     * 10 分钟 = 20 段 × 30 秒
     */
    #define HEARTBEAT_INTERVAL_S  30

    while (1) {
        for (int tick = 0;
             tick < (WEATHER_UPDATE_INTERVAL_S / HEARTBEAT_INTERVAL_S);
             tick++) {

            k_sleep(K_SECONDS(HEARTBEAT_INTERVAL_S));
            loop_count++;

            /*
             * [WiFi 断线检测] 如果 WiFi 意外断开 (g_wifi_connected=false)，
             * 自动发起重连。同时重新初始化信号量以防旧信号量干扰。
             */
            if (!g_wifi_connected) {
                LOG_WRN("检测到 WiFi 断开，尝试重新连接...");
                ret = wifi_connect(wifi_iface);
                if (ret == 0 || ret == -EALREADY) {
                    /* [轮询等待 IP] 和初始连接同样的逻辑 */
                    for (int a = 0; a < 15; a++) {
                        k_sleep(K_SECONDS(2));
                        struct in_addr *addr =
                            net_if_ipv4_get_global_addr(wifi_iface,
                                                        NET_ADDR_PREFERRED);
                        if (addr != NULL && addr->s_addr != 0) {
                            char ip[INET_ADDRSTRLEN];
                            net_addr_ntop(AF_INET, addr, ip, sizeof(ip));
                            if (strcmp(ip, "0.0.0.0") != 0) {
                                strncpy((char *)g_wifi_ip_addr, ip,
                                        sizeof(g_wifi_ip_addr) - 1);
                                g_wifi_connected = true;
                                LOG_INF("WiFi 重连成功! IP: %s", ip);
                                break;
                            }
                        }
                    }
                }
            }

            /*
             * [SNTP 重试] 如果之前时间同步失败，每 5 分钟重试一次
             * (10 ticks × 30 秒 = 300 秒 = 5 分钟)
             */
            if (!time_sync_done && (loop_count % 10 == 0)) {
                LOG_INF("重试 SNTP 时间同步...");
                ret = sync_time_via_sntp();
                if (ret == 0) {
                    time_sync_done = true;
                }
            }

            /*
             * [时间推进] SNTP 只同步一次基准时间，之后每 30 秒手动推进
             * 注意: 嵌入式 RTC 精度有限，长时间运行可能有漂移
             *       建议每天至少 SNTP 同步一次来校准
             */
            if (time_sync_done) {
                g_current_second += HEARTBEAT_INTERVAL_S;
                /* [进位] 秒→分→时→日 */
                while (g_current_second >= 60) {
                    g_current_second -= 60;
                    g_current_minute++;
                }
                while (g_current_minute >= 60) {
                    g_current_minute -= 60;
                    g_current_hour++;
                }
                while (g_current_hour >= 24) {
                    g_current_hour -= 24;
                    g_current_day++;
                    g_current_weekday = (g_current_weekday + 1) % 7;
                }
            }
        }

        /*
         * [心跳日志] 每 10 分钟打印一次状态摘要
         * 使用局部非 volatile 副本避免编译器警告
         */
        {
            int y = g_current_year, mo = g_current_month, d = g_current_day;
            int h = g_current_hour, mi = g_current_minute, s = g_current_second;
            int wd = g_current_weekday, t = g_temperature, ws = g_wind_speed;
            bool wc = g_wifi_connected;
            char desc[32];
            strncpy(desc, (const char *)g_weather_desc, sizeof(desc) - 1);
            desc[sizeof(desc) - 1] = '\0';
            LOG_INF("[心跳 #%u] WiFi=%s | %04d-%02d-%02d %02d:%02d:%02d 周%d | %d°C %s 风%dkm/h",
                    loop_count,
                    wc ? "已连接" : "断开",
                    y, mo, d, h, mi, s, wd, t, desc, ws);
        }

        /*
         * [天气更新] 每 10 分钟刷新一次天气数据
         * 如果之前失败 (weather_done=false)，也会在此重试
         */
        ret = fetch_weather();
        if (ret == 0) {
            weather_done = true;
        } else {
            LOG_WRN("天气更新失败 (err=%d)，%d 秒后重试",
                    ret, WEATHER_UPDATE_INTERVAL_S);
        }
    }
}

/* ==================== 线程静态调度配置 ==================== */

/*
 * [线程栈大小] 8192 字节 (8KB)
 *
 * 为什么需要 8KB:
 *   - HTTP 接收缓冲区是 static 的 (不占栈)，但 send/recv 有局部变量
 *   - SNTP 客户端库内部有栈分配
 *   - JSON 解析器需要栈空间
 *   - net_mgmt 回调可能嵌套调用
 *   - 实测约 4~6KB，8KB 留有安全余量
 *
 * 检查栈使用量: Shell 中执行 `kernel stacks` 查看
 */
#define WIFI_WEATHER_STACK_SIZE   8192

/*
 * [线程优先级] 10 (中等偏低)
 *
 * 优先级排布:
 *   - LVGL 渲染线程 = 5 (最高，保证 UI 流畅)
 *   - BLE 线程 ≈ 7~8 (蓝牙实时性)
 *   - WiFi+天气线程 = 10 (网络 I/O 为主，不抢 CPU)
 *   - 空闲线程 = 15 (最低)
 */
#define WIFI_WEATHER_PRIORITY     10

/*
 * K_THREAD_DEFINE: 静态定义并自动启动线程
 *
 * 参数:
 *   1. wifi_weather_tid:           线程 ID (k_tid_t)
 *   2. WIFI_WEATHER_STACK_SIZE:    栈大小 (字节)
 *   3. wifi_weather_thread_entry:  线程入口函数
 *   4~6. NULL, NULL, NULL:         传递给入口函数的参数 (本例不需要)
 *   7. WIFI_WEATHER_PRIORITY:      线程优先级
 *   8. 0:                          线程选项 (0 = 默认)
 *   9. 0:                          启动前延时 (0 = 立即启动)
 *
 * 使用 K_THREAD_DEFINE 而不是 k_thread_create() 的好处:
 *   - 编译时静态分配栈空间 (编译器可检查)
 *   - 系统启动时自动创建和运行 (不需要手动调用)
 *   - 与 lcd_lvgl_thread.c 保持一致的线程模式
 */
K_THREAD_DEFINE(wifi_weather_tid, WIFI_WEATHER_STACK_SIZE,
                wifi_weather_thread_entry, NULL, NULL, NULL,
                WIFI_WEATHER_PRIORITY, 0, 0);
