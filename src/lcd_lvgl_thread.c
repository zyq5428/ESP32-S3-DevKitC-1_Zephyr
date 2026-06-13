/*
 * [LCD + LVGL 线程] ESP32-S3-DevKitC-1 v1.1 N32R16V + Zephyr RTOS
 *
 * 功能：
 * - 初始化 ST7789V (240x280) SPI LCD 显示屏
 * - 使用 Zephyr 内置 LVGL 9 图形库渲染自定义精美智能手表界面
 * - 采用“单屏幕复用重绘”机制，彻底杜绝多屏切换时的内存释放/野指针崩溃（0x4 / 乱码地址）。
 *
 * 硬件连接:
 * - GPIO12 (SCLK) → LCD SCL
 * - GPIO11 (MOSI) → LCD SDA
 * - GPIO10 (CS)   → LCD CS
 * - GPIO8  (DC)   → LCD DC
 * - GPIO9  (RST)  → LCD RST
 * - GPIO4  (BLK)  → LCD BLK (PWM 背光)
 */

/* ==================== Zephyr 头文件 ==================== */
#include <zephyr/kernel.h>          /* [内核] K_THREAD_DEFINE, k_sleep, k_msleep */
#include <zephyr/device.h>          /* [设备] device_is_ready, DEVICE_DT_GET */
#include <zephyr/devicetree.h>      /* [设备树] DT_CHOSEN, DT_NODELABEL */
#include <zephyr/drivers/display.h> /* [显示] display_blanking_off */
#include <zephyr/drivers/gpio.h>    /* [GPIO] 背光控制 */
#include <zephyr/drivers/pwm.h>     /* [PWM] 背光 PWM 调光 */
#include <zephyr/sys/printk.h>      /* [打印] printk */
#include <zephyr/logging/log.h>     /* [日志] LOG_MODULE_REGISTER, LOG_INF */

/* ==================== LVGL 头文件 ==================== */
#include <lvgl.h>                   /* [LVGL] lv_* 图形库 API */

/* ==================== 项目头文件 ==================== */
#include "lcd_lvgl_thread.h"        /* [头文件] 全局变量声明 */
#include "wifi_weather_thread.h"    /* [头文件] WiFi/天气/时间 全局变量 */
#include "images/zephyr_200x116.h"  /* [头文件] Zephyr Logo 图像描述符 */

/* ==================== 标准库头文件 ==================== */
#include <stdio.h>                  /* [格式化] snprintf */

/* ==================== 日志模块注册 ==================== */
LOG_MODULE_REGISTER(LCD_LVGL, LOG_LEVEL_INF);

/* ==================== 全局变量定义 ==================== */
volatile int g_lcd_brightness = 30; /* [初始化] 默认背光亮度 30% */
volatile bool g_lvgl_ready = false; /* [初始化] LVGL 尚未就绪 */

/* ==================== LVGL 独享主显示屏幕对象 ==================== */
static lv_obj_t *app_screen = NULL;  /* [全局容器] 整个生命周期内唯一使用的屏幕画布 */

/* ==================== PWM 背光设备获取 ==================== */
static const struct pwm_dt_spec pwm_backlight =
    PWM_DT_SPEC_GET(DT_ALIAS(pwm_backlight));

/* ==================== 背光控制函数 ==================== */
static void lcd_backlight_set(uint8_t brightness)
{
    int ret;
    uint32_t pulse_ns;

    if (!pwm_is_ready_dt(&pwm_backlight)) {
        return;
    }

    if (brightness > 100) {
        brightness = 100;
    }

    pulse_ns = (uint32_t)(((uint64_t)brightness *
                           (uint64_t)pwm_backlight.period) / 100U);

    ret = pwm_set_pulse_dt(&pwm_backlight, pulse_ns);
    if (ret < 0) {
        LOG_ERR("Failed to set backlight PWM (err %d)", ret);
    }
}

/* ==================== UI 界面构建函数 ==================== */

/*
 * [函数] 阶段一：在 app_screen 上绘制赛博朋克风格开机欢迎画面
 *
 * 赛博朋克配色方案:
 *   主色调: 霓虹青 #00F0FF (Cyan)
 *   强调色: 霓虹品红 #FF00FF (Magenta)
 *   高亮色: 霓虹黄 #FFFF00 (Yellow)
 *   背景:   纯黑 #000000
 */
static void lvgl_draw_boot_ui(void)
{
    lv_obj_t *logo_img;
    lv_obj_t *title_label;
    lv_obj_t *info_label;
    lv_obj_t *status_label;

    /* [色彩常量] 赛博朋克霓虹色调 */
    lv_color_t neon_cyan    = lv_color_hex(0x00f0ff);
    lv_color_t neon_magenta = lv_color_hex(0xff00ff);
    lv_color_t neon_yellow  = lv_color_hex(0xffff00);
    lv_color_t pure_black   = lv_color_hex(0x000000);
    lv_color_t dark_gray    = lv_color_hex(0x333344);

    /* 【安全策略】直接使用活动主屏幕 */
    app_screen = lv_screen_active();

    /* 纯黑背景 — 赛博朋克标志性底色 */
    lv_obj_set_style_bg_color(app_screen, pure_black,
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(app_screen, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Zephyr Logo */
    logo_img = lv_image_create(app_screen);
    lv_image_set_src(logo_img, &zephyr_200x116);
    lv_obj_align(logo_img, LV_ALIGN_TOP_MID, 0, 15);

    /* 标题 — 霓虹青色 */
    title_label = lv_label_create(app_screen);
    lv_label_set_text(title_label, "ZEPHYR RTOS + LVGL");
    lv_obj_set_style_text_color(title_label, neon_cyan, 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 145);

    /* 硬件规格信息 — 暗灰色 */
    info_label = lv_label_create(app_screen);
    lv_label_set_text(info_label,
        "ESP32-S3 DevKitC v1.1\n"
        "ST7789  240x280\n"
        "N32R16V  32MB Flash");
    lv_obj_set_style_text_color(info_label, dark_gray, 0);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(info_label, LV_ALIGN_BOTTOM_MID, 0, -70);

    /* 装饰线 — 霓虹品红色 */
    lv_obj_t *deco_line = lv_label_create(app_screen);
    lv_label_set_text(deco_line, "◢ ▬▬▬▬▬ ◆ ▬▬▬▬▬ ◣");
    lv_obj_set_style_text_color(deco_line, neon_magenta, 0);
    lv_obj_set_style_text_font(deco_line, &lv_font_montserrat_12, 0);
    lv_obj_align(deco_line, LV_ALIGN_BOTTOM_MID, 0, -45);

    /* 启动状态 — 霓虹黄色 */
    status_label = lv_label_create(app_screen);
    lv_label_set_text(status_label, "[ SYSTEM BOOTING... ]");
    lv_obj_set_style_text_color(status_label, neon_yellow, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    LOG_INF("Cyberpunk boot interface drawn successfully.");
}

/*
 * [函数] 阶段二：绘制赛博朋克风格实时时钟界面
 *
 * 界面布局 (240x280, 圆角屏四边留 12px 安全边距):
 *   ┌────────────────────────────┐
 *   │  ◆ WIFI:ON      [====] 100%│  ← 状态栏 (y:12)
 *   │  ═══════ ◆ ════════      │  ← 上装饰线
 *   │                            │
 *   │       22 : 48 : 36          │  ← 时间 (青/灰, 14号字)
 *   │      周六  06/13            │  ← 日期 (品红, 14号字)
 *   │      ── ◆ ── ◆ ──        │  ← 分隔装饰
 *   │                            │
 *   │         27°C               │  ← 温度 (黄, 14号字)
 *   │         阴天                │  ← 天气 (青, 14号字)
 *   │      风: 11 km/h           │  ← 风速 (灰, 12号字)
 *   │                            │
 *   │     ◢■■■ SYNC OK ■■■◣     │  ← 同步状态 (绿)
 *   └────────────────────────────┘
 *
 * 动态更新: LVGL 定时器每秒回调刷新所有动态文本。
 * 只使用 montserrat 12/14 (已验证可正常渲染)。
 */

/* ==================== 赛博朋克配色常量 ==================== */
#define CYBER_CYAN    lv_color_hex(0x00f0ff)
#define CYBER_MAGENTA lv_color_hex(0xff00ff)
#define CYBER_YELLOW  lv_color_hex(0xffff00)
#define CYBER_GREEN   lv_color_hex(0x00ff41)
#define CYBER_ORANGE  lv_color_hex(0xff6600)
#define CYBER_RED     lv_color_hex(0xff0040)
#define CYBER_DIM     lv_color_hex(0x3a3a5e)

/*
 * [函数] WMO 天气代码 → 简短英文描述
 * Montserrat 字体不含中文字形，中文天气描述在 LCD 上会显示为方块。
 * 因此 LCD 用英文显示天气，串口日志保留中文。
 */
static const char *wmo_to_en(int code)
{
    switch (code) {
    case 0:     return "Clear";
    case 1:     return "P.Cloudy";
    case 2:     return "Cloudy";
    case 3:     return "Overcast";
    case 45:
    case 48:    return "Fog";
    case 51:
    case 53:
    case 55:    return "Drizzle";
    case 61:
    case 63:
    case 65:    return "Rain";
    case 71:
    case 73:
    case 75:    return "Snow";
    case 80:
    case 81:
    case 82:    return "Showers";
    case 95:
    case 96:
    case 99:    return "Thunder";
    default:    return "?";
    }
}

/* ==================== 动态控件指针 (每秒刷新) ==================== */
static lv_obj_t *cy_time_label     = NULL;
static lv_obj_t *cy_date_label     = NULL;
static lv_obj_t *cy_temp_label     = NULL;
static lv_obj_t *cy_weather_label  = NULL;
static lv_obj_t *cy_wind_label     = NULL;
static lv_obj_t *cy_wifi_label     = NULL;
static lv_obj_t *cy_status_label   = NULL;
static lv_obj_t *cy_deco_a         = NULL;
static lv_obj_t *cy_deco_b         = NULL;

/*
 * [函数] Unix 时间戳 → 年/月/日/时/分/秒/星期 (北京时间 UTC+8)
 *
 * 纯手写算法，不依赖 libc gmtime()（嵌入式 picolibc 可能不支持）。
 * 与 wifi_weather_thread.c 中 SNTP 同步的转换算法一致。
 */
static void unix_to_calendar(uint64_t unix_time,
                             int *y, int *mo, int *d,
                             int *h, int *mi, int *s, int *wd)
{
    uint64_t ts = unix_time;
    uint32_t total_days, remaining_days;
    int year = 1970, month = 1, day = 1;
    int days_in_month;

    /* 时/分/秒 */
    *s  = (int)(ts % 60); ts /= 60;
    *mi = (int)(ts % 60); ts /= 60;
    *h  = (int)(ts % 24);
    total_days = (uint32_t)(ts / 24);

    /* 年 */
    remaining_days = total_days;
    while (1) {
        int leap = ((year % 4 == 0) && (year % 100 != 0))
                   || (year % 400 == 0);
        uint32_t dy = leap ? 366 : 365;
        if (remaining_days < dy) break;
        remaining_days -= dy;
        year++;
    }
    *y = year;

    /* 月 */
    for (month = 1; month <= 12; month++) {
        switch (month) {
        case 1:  days_in_month = 31; break;
        case 2: {
            int leap = ((year % 4 == 0) && (year % 100 != 0))
                       || (year % 400 == 0);
            days_in_month = leap ? 29 : 28;
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
            day = (int)(remaining_days + 1);
            break;
        }
        remaining_days -= (uint32_t)days_in_month;
    }
    *mo = month;
    *d  = day;

    /* 星期 (Sakamoto) */
    int yw = year, mw = month;
    if (mw < 3) { mw += 12; yw -= 1; }
    *wd = (day + 2*mw + (3*(mw+1))/5 + yw + yw/4 - yw/100 + yw/400 + 1) % 7;
}

/*
 * [回调] LVGL 定时器 — 每秒刷新赛博朋克时钟 (RTC 硬件定时器驱动)
 */
static void cyberpunk_clock_update(lv_timer_t *timer)
{
    char buf[48];
    const char *wd_str[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    int y, mo, d, h, mi, s, wd;

    /*
     * [RTC 时间计算] 从 SNTP 同步基准 + 硬件定时器差值推算当前时间
     *
     * g_rtc_base_unix:   SNTP 同步时的北京时间 Unix 戳
     * g_rtc_base_uptime: 同步时的 k_uptime_get() 值 (毫秒)
     * k_uptime_get():    当前系统 uptime (硬件定时器驱动, 毫秒级精度)
     *
     * 公式: now = base_unix + (current_uptime - base_uptime) / 1000
     * 硬件定时器保证无累积漂移，精度远高于软件手动累加。
     */
    if (g_rtc_synced) {
        uint64_t elapsed_ms = k_uptime_get() - g_rtc_base_uptime;
        uint64_t now_unix   = g_rtc_base_unix + elapsed_ms / 1000;
        unix_to_calendar(now_unix, &y, &mo, &d, &h, &mi, &s, &wd);

        /* [同步全局变量] 保持兼容性 */
        g_current_year   = y;
        g_current_month  = mo;
        g_current_day    = d;
        g_current_hour   = h;
        g_current_minute = mi;
        g_current_second = s;
        g_current_weekday = wd;
    } else {
        /* RTC 尚未同步，使用默认值 */
        y = g_current_year; mo = g_current_month; d = g_current_day;
        h = g_current_hour; mi = g_current_minute; s = g_current_second;
        wd = g_current_weekday;
    }

    /* 时间: "22:48:36" */
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, mi, s);
    lv_label_set_text(cy_time_label, buf);

    /* 日期: "Sat  06/14" */
    snprintf(buf, sizeof(buf), "%s  %02d/%02d",
             wd_str[wd % 7], mo, d);
    lv_label_set_text(cy_date_label, buf);

    /* WiFi */
    if (g_wifi_connected) {
        lv_label_set_text(cy_wifi_label, "WIFI:ON");
        lv_obj_set_style_text_color(cy_wifi_label, CYBER_CYAN, 0);
    } else {
        lv_label_set_text(cy_wifi_label, "WIFI:OFF");
        lv_obj_set_style_text_color(cy_wifi_label, CYBER_RED, 0);
    }

    /* 天气 (用英文 WMO 映射，避免中文字体乱码) */
    if (g_weather_ready) {
        snprintf(buf, sizeof(buf), "%d C", g_temperature);
        lv_label_set_text(cy_temp_label, buf);
        lv_label_set_text(cy_weather_label, wmo_to_en(g_weather_code));
        snprintf(buf, sizeof(buf), "Wind %d km/h", g_wind_speed);
        lv_label_set_text(cy_wind_label, buf);
    }

    /* 同步状态 */
    if (g_wifi_connected && g_time_synced && g_weather_ready) {
        lv_label_set_text(cy_status_label, "SYNC OK");
        lv_obj_set_style_text_color(cy_status_label, CYBER_GREEN, 0);
    } else if (g_wifi_connected && g_time_synced) {
        lv_label_set_text(cy_status_label, "SYNCING...");
        lv_obj_set_style_text_color(cy_status_label, CYBER_YELLOW, 0);
    } else {
        lv_label_set_text(cy_status_label, "WAITING NET");
        lv_obj_set_style_text_color(cy_status_label, CYBER_ORANGE, 0);
    }

    /* 装饰线闪烁 */
    static int tick = 0;
    tick++;
    lv_color_t dc = (tick % 2 == 0) ? CYBER_MAGENTA : CYBER_CYAN;
    lv_obj_set_style_text_color(cy_deco_a, dc, 0);
    lv_obj_set_style_text_color(cy_deco_b, dc, 0);
}

static void lvgl_draw_cyberpunk_clock(void)
{
    /* [圆角屏边距] 四边留 10px */
    #define PAD 10

    LOG_INF("Building cyberpunk clock...");

    /* 清理 + 纯黑背景 */
    lv_obj_clean(app_screen);
    lv_obj_set_style_bg_color(app_screen, lv_color_hex(0x000000),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(app_screen, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ===== 顶部栏: WiFi(左) + 日期(右上) ===== */
    cy_wifi_label = lv_label_create(app_screen);
    lv_label_set_text(cy_wifi_label, "WIFI:OFF");
    lv_obj_set_style_text_color(cy_wifi_label, CYBER_RED, 0);
    lv_obj_set_style_text_font(cy_wifi_label, &lv_font_montserrat_12, 0);
    lv_obj_align(cy_wifi_label, LV_ALIGN_TOP_LEFT, PAD, PAD);

    cy_date_label = lv_label_create(app_screen);
    lv_label_set_text(cy_date_label, "---  --/--");
    lv_obj_set_style_text_color(cy_date_label, CYBER_MAGENTA, 0);
    lv_obj_set_style_text_font(cy_date_label, &lv_font_montserrat_12, 0);
    lv_obj_align(cy_date_label, LV_ALIGN_TOP_RIGHT, -PAD, PAD);

    /* ===== 时间 HH:MM (24号最大字, 霓虹青, 屏幕正中偏上) ===== */
    cy_time_label = lv_label_create(app_screen);
    lv_label_set_text(cy_time_label, "--:--:--");
    lv_obj_set_style_text_color(cy_time_label, CYBER_CYAN, 0);
    lv_obj_set_style_text_font(cy_time_label, &lv_font_montserrat_24, 0);
    lv_obj_align(cy_time_label, LV_ALIGN_CENTER, 0, -15);

    /* ===== 温度 (16号, 黄) ===== */
    cy_temp_label = lv_label_create(app_screen);
    lv_label_set_text(cy_temp_label, "-- C");
    lv_obj_set_style_text_color(cy_temp_label, CYBER_YELLOW, 0);
    lv_obj_set_style_text_font(cy_temp_label, &lv_font_montserrat_16, 0);
    lv_obj_align(cy_temp_label, LV_ALIGN_CENTER, 0, 45);

    /* ===== 天气 + 风速 (14号, 青+灰, 同行) ===== */
    cy_weather_label = lv_label_create(app_screen);
    lv_label_set_text(cy_weather_label, "waiting...");
    lv_obj_set_style_text_color(cy_weather_label, CYBER_CYAN, 0);
    lv_obj_set_style_text_font(cy_weather_label, &lv_font_montserrat_14, 0);
    lv_obj_align(cy_weather_label, LV_ALIGN_CENTER, 0, 70);

    cy_wind_label = lv_label_create(app_screen);
    lv_label_set_text(cy_wind_label, "Wind -- km/h");
    lv_obj_set_style_text_color(cy_wind_label, CYBER_DIM, 0);
    lv_obj_set_style_text_font(cy_wind_label, &lv_font_montserrat_12, 0);
    lv_obj_align(cy_wind_label, LV_ALIGN_CENTER, 0, 90);

    /* ===== 同步状态 (底部) ===== */
    cy_status_label = lv_label_create(app_screen);
    lv_label_set_text(cy_status_label, "WAITING NET");
    lv_obj_set_style_text_color(cy_status_label, CYBER_ORANGE, 0);
    lv_obj_set_style_text_font(cy_status_label, &lv_font_montserrat_12, 0);
    lv_obj_align(cy_status_label, LV_ALIGN_BOTTOM_MID, 0, -PAD);

    /* 装饰闪烁 — 保留两道线增加赛博感 */
    cy_deco_a = lv_label_create(app_screen);
    lv_label_set_text(cy_deco_a, "-----------------");
    lv_obj_set_style_text_color(cy_deco_a, CYBER_DIM, 0);
    lv_obj_set_style_text_font(cy_deco_a, &lv_font_montserrat_12, 0);
    lv_obj_align(cy_deco_a, LV_ALIGN_CENTER, 0, 28);

    cy_deco_b = lv_label_create(app_screen);
    lv_label_set_text(cy_deco_b, "-----------------");
    lv_obj_set_style_text_color(cy_deco_b, CYBER_DIM, 0);
    lv_obj_set_style_text_font(cy_deco_b, &lv_font_montserrat_12, 0);
    lv_obj_align(cy_deco_b, LV_ALIGN_BOTTOM_MID, 0, -30);

    /* 每秒刷新 */
    lv_timer_create(cyberpunk_clock_update, 1000, NULL);

    LOG_INF("Cyberpunk clock ready.");
}

/* ==================== 显示设备初始化函数 ==================== */
static const struct device *display_init(void)
{
    const struct device *display_dev;
    int ret;

    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display device '%s' is not ready!",
                display_dev ? display_dev->name : "NULL");
        return NULL;
    }
    LOG_INF("Display device '%s' is ready", display_dev->name);

    ret = display_blanking_off(display_dev);
    if (ret < 0 && ret != -ENOSYS) {
        LOG_ERR("Failed to turn off blanking (error %d)", ret);
        return NULL;
    }
    LOG_INF("Display blanking turned off, screen should be lit");

    return display_dev;
}

/* ==================== LVGL 线程入口函数 ==================== */
void lcd_lvgl_thread_entry(void *p1, void *p2, void *p3)
{
    const struct device *display_dev;
    uint32_t loop_count = 0;          

    LOG_INF("LCD/LVGL Thread starting...");

    /* 基础稳压延时 */
    k_msleep(500);

    /* ---------- 步骤 1: 驱使 PWM 控制器点亮屏幕背光 ---------- */
    lcd_backlight_set(g_lcd_brightness);
    LOG_INF("Backlight set to %d%%", g_lcd_brightness);

    /* ---------- 步骤 2: 初始化硬件屏显设备驱动 ---------- */
    display_dev = display_init();
    if (display_dev == NULL) {
        LOG_ERR("Display init failed! LCD thread will exit.");
        return;
    }

    /* ---------- 步骤 3: 确保校验 LVGL 引擎内核状态 ---------- */
    if (!lv_is_initialized()) {
        LOG_ERR("LVGL Core not ready! Check prj.conf");
        return;
    }
    g_lvgl_ready = true; 
    LOG_INF("LVGL initialized and ready");

    /* ---------- 步骤 4: 在当前画布上渲染首屏开机信息 ---------- */
    lvgl_draw_boot_ui();

    /* ---------- 步骤 5: 保持上电状态展示 5 秒时间 ---------- */
    LOG_INF("Showing dev board info for 5 seconds...");
    k_sleep(K_SECONDS(5));

    /* ---------- 步骤 6: 绘制赛博朋克时钟界面 ---------- */
    LOG_INF("Switching to cyberpunk clock interface...");
    lvgl_draw_cyberpunk_clock();
    /*
     * 时钟界面由 LVGL 定时器 cyberpunk_clock_update 每秒自动刷新，
     * 本线程无需再做任何 UI 操作，直接进入永久休眠。
     */

    /* ---------- 步骤 7: 永久休眠 (LVGL 定时器接管刷新) ---------- */
    while (1) {
        k_sleep(K_SECONDS(300));
        loop_count++;
        LOG_DBG("Display thread alive (%lu hours), Cyberpunk Clock running",
                (unsigned long)(loop_count * 5 / 60));
    }
}

/* ==================== 线程静态调度配置 ==================== */
#define LCD_LVGL_STACK_SIZE 4096
#define LCD_LVGL_PRIORITY 14

K_THREAD_DEFINE(lcd_lvgl_tid, LCD_LVGL_STACK_SIZE,
                lcd_lvgl_thread_entry, NULL, NULL, NULL,
                LCD_LVGL_PRIORITY, 0, 0);