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

/* ==================== 赛博朋克配色 (16位 RGB565) ==================== */
#define CYBER_BG      lv_color_make(10, 10, 15)     /* 暗夜蓝黑背景 */
#define CYBER_CYAN    lv_color_make(0, 255, 240)    /* 霓虹青 — 时间/WiFi */
#define CYBER_PINK    lv_color_make(255, 0, 128)    /* 霓虹粉 — 日期/云 */
#define CYBER_YELLOW  lv_color_make(255, 230, 0)    /* 警示黄 — 温度 */
#define CYBER_GREEN   lv_color_make(0, 255, 120)    /* 荧光绿 — 状态栏 */
#define CYBER_DIM     lv_color_make(20, 40, 60)     /* 暗蓝微光 — 电路线 */
#define CYBER_WHITE   lv_color_make(200, 200, 210)  /* 灰白 — 次要文字 */
#define CYBER_RED     lv_color_make(255, 50, 50)    /* 霓虹红 — 断开 */

/* ==================== 动态控件指针 (每秒刷新) ==================== */
static lv_obj_t *cy_time_label     = NULL;
static lv_obj_t *cy_date_label     = NULL;
static lv_obj_t *cy_temp_label     = NULL;
static lv_obj_t *cy_wifi_label     = NULL;
static lv_obj_t *cy_sync_label     = NULL;
static lv_obj_t *cy_therm_liquid   = NULL;  /* 温度计液柱 */

/* ==================== RTC → 日历转换 ==================== */
static void unix_to_calendar(uint64_t unix_time,
                             int *y, int *mo, int *d,
                             int *h, int *mi, int *s, int *wd)
{
    uint64_t ts = unix_time;
    uint32_t total_days, rem;
    int year = 1970, month = 1, day = 1, dim;

    *s  = (int)(ts % 60); ts /= 60;
    *mi = (int)(ts % 60); ts /= 60;
    *h  = (int)(ts % 24);
    total_days = (uint32_t)(ts / 24);

    rem = total_days;
    while (1) {
        int lp = ((year%4==0)&&(year%100!=0))||(year%400==0);
        if (rem < (uint32_t)(lp?366:365)) break;
        rem -= (lp ? 366 : 365); year++;
    }
    *y = year;
    for (month = 1; month <= 12; month++) {
        if (month==1) dim=31; else if (month==2) {
            int lp = ((year%4==0)&&(year%100!=0))||(year%400==0);
            dim = lp?29:28;
        } else if (month==3) dim=31; else if (month==4) dim=30;
        else if (month==5) dim=31; else if (month==6) dim=30;
        else if (month==7) dim=31; else if (month==8) dim=31;
        else if (month==9) dim=30; else if (month==10) dim=31;
        else if (month==11) dim=30; else dim=31;
        if (rem < (uint32_t)dim) { day = (int)(rem+1); break; }
        rem -= (uint32_t)dim;
    }
    *mo = month; *d = day;
    int yw=year, mw=month;
    if (mw<3) { mw+=12; yw-=1; }
    *wd = (day+2*mw+(3*(mw+1))/5+yw+yw/4-yw/100+yw/400+1)%7;
}

/* ==================== 电路板背景线 ==================== */
static void draw_circuit_bg(void)
{
    static lv_point_precise_t l1[] = { {0,40}, {30,40}, {60,10}, {100,10} };
    lv_obj_t *line1 = lv_line_create(app_screen);
    lv_line_set_points(line1, l1, 4);
    lv_obj_set_style_line_color(line1, CYBER_DIM, LV_PART_MAIN);
    lv_obj_set_style_line_width(line1, 2, LV_PART_MAIN);

    static lv_point_precise_t l2[] = { {220,0}, {220,100}, {190,130}, {190,200} };
    lv_obj_t *line2 = lv_line_create(app_screen);
    lv_line_set_points(line2, l2, 4);
    lv_obj_set_style_line_color(line2, CYBER_DIM, LV_PART_MAIN);
    lv_obj_set_style_line_width(line2, 2, LV_PART_MAIN);

    static lv_point_precise_t l3[] = { {5,120}, {50,120}, {70,100} };
    lv_obj_t *line3 = lv_line_create(app_screen);
    lv_line_set_points(line3, l3, 3);
    lv_obj_set_style_line_color(line3, CYBER_DIM, LV_PART_MAIN);
    lv_obj_set_style_line_width(line3, 1, LV_PART_MAIN);
}

/* ==================== 霓虹温度计 ==================== */
static lv_obj_t *create_thermometer(void)
{
    lv_obj_t *c = lv_obj_create(app_screen);
    lv_obj_set_size(c, 30, 55);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(c, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(c, 0, LV_PART_MAIN);

    /* 玻璃管 */
    lv_obj_t *tube = lv_obj_create(c);
    lv_obj_set_size(tube, 8, 35);
    lv_obj_align(tube, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(tube, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(tube, lv_color_make(150,150,150), LV_PART_MAIN);
    lv_obj_set_style_border_width(tube, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(tube, 4, LV_PART_MAIN);

    /* 液柱 (高度随温度动态调整) */
    cy_therm_liquid = lv_obj_create(c);
    lv_obj_set_size(cy_therm_liquid, 4, 10);
    lv_obj_align(cy_therm_liquid, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_set_style_bg_color(cy_therm_liquid, CYBER_YELLOW, LV_PART_MAIN);
    lv_obj_set_style_border_width(cy_therm_liquid, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(cy_therm_liquid, 2, LV_PART_MAIN);

    /* 感温泡 */
    lv_obj_t *bulb = lv_obj_create(c);
    lv_obj_set_size(bulb, 16, 16);
    lv_obj_align(bulb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bulb, CYBER_YELLOW, LV_PART_MAIN);
    lv_obj_set_style_border_width(bulb, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bulb, LV_RADIUS_CIRCLE, LV_PART_MAIN);

    return c;
}

/* ==================== 霓虹云图标 ==================== */
static lv_obj_t *create_cloud_icon(void)
{
    lv_obj_t *c = lv_obj_create(app_screen);
    lv_obj_set_size(c, 70, 55);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(c, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(c, 0, LV_PART_MAIN);

    lv_obj_t *a1 = lv_arc_create(c);
    lv_obj_set_size(a1, 30, 30);
    lv_arc_set_angles(a1, 135, 360);
    lv_obj_align(a1, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_arc_color(a1, CYBER_PINK, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a1, 3, LV_PART_MAIN);
    lv_obj_set_style_opa(a1, LV_OPA_TRANSP, LV_PART_KNOB);

    lv_obj_t *a2 = lv_arc_create(c);
    lv_obj_set_size(a2, 35, 35);
    lv_arc_set_angles(a2, 180, 45);
    lv_obj_align(a2, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_arc_color(a2, CYBER_PINK, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a2, 3, LV_PART_MAIN);
    lv_obj_set_style_opa(a2, LV_OPA_TRANSP, LV_PART_KNOB);

    /* 雨滴 */
    static lv_point_precise_t dp[] = { {0,0}, {5,10} };
    lv_obj_t *d1 = lv_line_create(c);
    lv_line_set_points(d1, dp, 2);
    lv_obj_set_style_line_color(d1, CYBER_PINK, LV_PART_MAIN);
    lv_obj_set_style_line_width(d1, 3, LV_PART_MAIN);
    lv_obj_align(d1, LV_ALIGN_BOTTOM_MID, -12, 8);

    lv_obj_t *d2 = lv_line_create(c);
    lv_line_set_points(d2, dp, 2);
    lv_obj_set_style_line_color(d2, CYBER_PINK, LV_PART_MAIN);
    lv_obj_set_style_line_width(d2, 3, LV_PART_MAIN);
    lv_obj_align(d2, LV_ALIGN_BOTTOM_MID, 8, 8);

    return c;
}

/* ==================== 1Hz 刷新回调 ==================== */
static void cyberpunk_clock_update(lv_timer_t *timer)
{
    char buf[32];
    const char *wd_str[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    int y, mo, d, h, mi, s, wd;

    if (g_rtc_synced) {
        uint64_t now = g_rtc_base_unix
                       + (k_uptime_get() - g_rtc_base_uptime) / 1000;
        unix_to_calendar(now, &y, &mo, &d, &h, &mi, &s, &wd);
        g_current_year=y; g_current_month=mo; g_current_day=d;
        g_current_hour=h; g_current_minute=mi; g_current_second=s;
        g_current_weekday=wd;
    } else {
        y=g_current_year; mo=g_current_month; d=g_current_day;
        h=g_current_hour; mi=g_current_minute; s=g_current_second;
        wd=g_current_weekday;
    }

    /* 时间 */
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, mi, s);
    lv_label_set_text(cy_time_label, buf);

    /* 日期 */
    snprintf(buf, sizeof(buf), "%s %02d/%02d", wd_str[wd%7], mo, d);
    lv_label_set_text(cy_date_label, buf);

    /* WiFi */
    if (g_wifi_connected) {
        lv_label_set_text(cy_wifi_label, "WIFI: ON");
        lv_obj_set_style_text_color(cy_wifi_label, CYBER_CYAN, 0);
    } else {
        lv_label_set_text(cy_wifi_label, "WIFI: OFF");
        lv_obj_set_style_text_color(cy_wifi_label, CYBER_RED, 0);
    }

    /* 温度 + 温度计液柱 */
    if (g_weather_ready) {
        snprintf(buf, sizeof(buf), "%d", g_temperature);
        lv_label_set_text(cy_temp_label, buf);
        /* 液柱高度: 温度 0→40°C 映射到 4→30px */
        int liq_h = 4 + (g_temperature * 26) / 40;
        if (liq_h < 4)  liq_h = 4;
        if (liq_h > 30) liq_h = 30;
        lv_obj_set_size(cy_therm_liquid, 4, liq_h);
    }

    /* 同步状态 */
    if (g_wifi_connected && g_time_synced && g_weather_ready) {
        lv_label_set_text(cy_sync_label, "SYSTEM: SYNC OK");
        lv_obj_set_style_text_color(cy_sync_label, CYBER_GREEN, 0);
    } else if (g_wifi_connected && g_time_synced) {
        lv_label_set_text(cy_sync_label, "SYSTEM: SYNCING...");
        lv_obj_set_style_text_color(cy_sync_label, CYBER_YELLOW, 0);
    } else {
        lv_label_set_text(cy_sync_label, "SYSTEM: WAITING NET");
        lv_obj_set_style_text_color(cy_sync_label, CYBER_YELLOW, 0);
    }
}

/* ==================== 构建赛博朋克时钟 ==================== */
static void lvgl_draw_cyberpunk_clock(void)
{
    lv_obj_clean(app_screen);
    lv_obj_set_style_bg_color(app_screen, CYBER_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(app_screen, LV_OPA_COVER, LV_PART_MAIN);

    /* 电路板背景线 */
    draw_circuit_bg();

    /* WiFi (左上) */
    cy_wifi_label = lv_label_create(app_screen);
    lv_label_set_text(cy_wifi_label, "WIFI: OFF");
    lv_obj_set_style_text_color(cy_wifi_label, CYBER_CYAN, 0);
    lv_obj_set_style_text_font(cy_wifi_label, &lv_font_montserrat_12, 0);
    lv_obj_align(cy_wifi_label, LV_ALIGN_TOP_LEFT, 12, 12);

    /* 日期 (右上) */
    cy_date_label = lv_label_create(app_screen);
    lv_label_set_text(cy_date_label, "--- --/--");
    lv_obj_set_style_text_color(cy_date_label, CYBER_PINK, 0);
    lv_obj_set_style_text_font(cy_date_label, &lv_font_montserrat_12, 0);
    lv_obj_align(cy_date_label, LV_ALIGN_TOP_RIGHT, -12, 12);

    /* 时间 (48号巨字, 霓虹青, 正中偏上) */
    cy_time_label = lv_label_create(app_screen);
    lv_label_set_text(cy_time_label, "--:--:--");
    lv_obj_set_style_text_color(cy_time_label, CYBER_CYAN, 0);
    lv_obj_set_style_text_font(cy_time_label, &lv_font_montserrat_48, 0);
    lv_obj_align(cy_time_label, LV_ALIGN_CENTER, 0, -25);

    /* 天气区: 云图标 (左) + 温度数字 (右) */
    lv_obj_t *cloud = create_cloud_icon();
    lv_obj_align(cloud, LV_ALIGN_CENTER, -55, 60);

    lv_obj_t *therm = create_thermometer();
    lv_obj_align(therm, LV_ALIGN_CENTER, -5, 62);

    cy_temp_label = lv_label_create(app_screen);
    lv_label_set_text(cy_temp_label, "--");
    lv_obj_set_style_text_color(cy_temp_label, CYBER_YELLOW, 0);
    lv_obj_set_style_text_font(cy_temp_label, &lv_font_montserrat_28, 0);
    lv_obj_align(cy_temp_label, LV_ALIGN_CENTER, 40, 60);

    /* 底部状态栏 (荧光绿边框) */
    lv_obj_t *status_box = lv_obj_create(app_screen);
    lv_obj_set_size(status_box, 190, 32);
    lv_obj_align(status_box, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_opa(status_box, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_bg_color(status_box, CYBER_GREEN, LV_PART_MAIN);
    lv_obj_set_style_border_color(status_box, CYBER_GREEN, LV_PART_MAIN);
    lv_obj_set_style_border_width(status_box, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(status_box, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(status_box, 0, LV_PART_MAIN);

    cy_sync_label = lv_label_create(status_box);
    lv_label_set_text(cy_sync_label, "SYSTEM: WAITING NET");
    lv_obj_set_style_text_color(cy_sync_label, CYBER_GREEN, 0);
    lv_obj_set_style_text_font(cy_sync_label, &lv_font_montserrat_12, 0);
    lv_obj_align(cy_sync_label, LV_ALIGN_CENTER, 0, 0);

    /* 1Hz RTC 刷新 */
    lv_timer_create(cyberpunk_clock_update, 1000, NULL);
    LOG_INF("Cyberpunk clock ready (48px + circuit + thermometer).");
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