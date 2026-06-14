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

/* ==================== 外部资源声明 ==================== */
/*
 * [背景] sbpk_240x280 — 赛博朋克风 240×280 全屏背景图 (RGB565)
 * 源文件: src/images/sbpk_240x280.c
 */
extern const lv_image_dsc_t sbpk_240x280;

/*
 * [字体] weather_chinese_font — 16px 4bpp 中文天气字体
 * 源文件: src/fonts/weather_chinese_font.c
 */
extern const lv_font_t weather_chinese_font;

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
    lv_label_set_text(deco_line, "<<<<<<O>>>>>");
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
 *   │                       │  ← 状态栏 (y=8)
 *   │                            │
 *   │      周六  06/14            │  ← 日期 28号 (y≈80)
 *   │       22 : 48 : 36          │  ← 时间 48号 (y≈115)
 *   │                            │
 *   │    ╭────╮    ╭────╮       │  ← 赛博朋克环形仪表 (y≈175)
 *   │   ╱ 27°C ╲  ╱ 60% ╲     │    双弧霓虹光效
 *   │  │  温度  ││  湿度  │     │    中心数值+中文标签
 *   │   ╲      ╱  ╲      ╱     │
 *   │    ╰────╯    ╰────╯       │
 *   │                            │
 *   │   天气:多云  风速:12 km/h   │  ← 中文天气+风速 (y≈225)
 *   │                            │
 *   │   ◢■■■ SYSTEM SYNC OK ■■■◣│  ← 同步状态 (y≈268)
 *   └────────────────────────────┘
 *
 * 动态更新: LVGL 定时器每秒回调刷新所有动态文本和弧形仪表。
 *
 * 使用的字体：
 *   - montserrat 12/14/28/48  (西文/数字)
 *   - weather_chinese_font (自定义中文天气字体, 16px)
 *
 * 图标颜色规则:
 *   - WiFi:   LV_SYMBOL_WIFI          蓝色(已连) / 灰色(未连)
 *   - 蓝牙:   LV_SYMBOL_BLUETOOTH     蓝色(已连) / 灰色(未连)
 *   - 电池:   LV_SYMBOL_BATTERY_FULL  绿色(≥50%) / 黄色(20~49%) / 红色(<20%)
 *
 * 环形仪表颜色规则:
 *   - 温度弧: 青色(<10°C) / 绿色(10~30°C) / 红色(>30°C)
 *   - 湿度弧: 青色(30~70%) / 黄色(<30%) / 粉色(>70%)
 */

/* ==================== 赛博朋克配色 (16位 RGB565) ==================== */
#define CYBER_BG      lv_color_make(10, 10, 15)     /* 暗夜蓝黑背景 */
#define CYBER_CYAN    lv_color_make(0, 255, 240)    /* 霓虹青 — 时间/WiFi */
#define CYBER_PINK    lv_color_make(255, 0, 128)    /* 霓虹粉 — 日期/云 */
#define CYBER_YELLOW  lv_color_make(255, 230, 0)    /* 警示黄 — 温度 */
#define CYBER_GREEN   lv_color_make(0, 255, 120)    /* 荧光绿 — 状态栏 */
#define CYBER_DIM     lv_color_make(20, 40, 60)     /* 暗蓝微光 — 电路线 */
#define CYBER_WHITE   lv_color_make(200, 200, 210)  /* 灰白 — 次要文字 */
#define CYBER_RED     lv_color_make(255, 50, 50)    /* 霓虹红 — 断开/低电量 */
#define CYBER_GRAY    lv_color_make(128, 128, 140)  /* 暗灰 — 图标未连接 */

/* ==================== 动态控件指针 (每秒刷新) ==================== */
static lv_obj_t *cy_time_label     = NULL;
static lv_obj_t *cy_date_label     = NULL;
static lv_obj_t *cy_wifi_label     = NULL;
static lv_obj_t *cy_bt_label       = NULL;   /* 蓝牙图标标签 */
static lv_obj_t *cy_battery_label  = NULL;   /* 电池图标标签 */
static lv_obj_t *cy_sync_label     = NULL;
static lv_obj_t *cy_temp_arc       = NULL;   /* 温度环形仪表 */
static lv_obj_t *cy_temp_val_label = NULL;   /* 温度环形中心数值 */
static lv_obj_t *cy_humid_arc      = NULL;   /* 湿度环形仪表 */
static lv_obj_t *cy_humid_val_label= NULL;   /* 湿度环形中心数值 */
static lv_obj_t *cy_weather_label  = NULL;   /* 中文天气描述 */
static lv_obj_t *cy_wind_label     = NULL;   /* 中文风速信息 */

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

/* ==================== 创建环形仪表盘辅助函数 ==================== */

/*
 * [函数] 创建赛博朋克风格弧形圆环仪表
 *
 * 设计要点:
 *   - 双层弧: 底层暗色轨道(8px) + 上层霓虹指示器(10px, 略宽营造光晕感)
 *   - 缺口在正下方 (旋转135°, 背景弧0~270°)
 *   - 隐藏旋钮, 不可点击
 *
 * 参数:
 *   - parent:     父容器
 *   - size:       弧的外径 (宽=高=size)
 *   - range_min:  最小值
 *   - range_max:  最大值
 *   - arc_color:  指示器默认霓虹颜色
 *   - bg_color:   轨道暗色
 *
 * 返回: lv_arc 对象指针
 */
static lv_obj_t *create_arc_gauge(lv_obj_t *parent, int size,
                                  int range_min, int range_max,
                                  lv_color_t arc_color, lv_color_t bg_color)
{
    lv_obj_t *arc = lv_arc_create(parent);

    /* [尺寸] 正方形 */
    lv_obj_set_size(arc, size, size);

    /* [隐藏旋钮] 纯显示仪表, 不可交互 */
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    /* [旋转] 缺口在正下方: 旋转135° + 弧长270° */
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);

    /* [范围] 最小值→最大值 */
    lv_arc_set_range(arc, range_min, range_max);

    /* [轨道样式] 暗色底层, 8px宽 — 赛博朋克"关灯"效果 */
    lv_obj_set_style_arc_color(arc, bg_color, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_60, LV_PART_MAIN);

    /* [指示器样式] 霓虹上层, 10px宽 — 比轨道略宽形成光晕感 */
    lv_obj_set_style_arc_color(arc, arc_color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);

    /* [控件背景] 完全透明, 无边框 */
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);

    return arc;
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

    /* 时间 (48号大字, 居中) */
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, mi, s);
    lv_label_set_text(cy_time_label, buf);

    /* 日期 (28号大字, 位于时间上方, 品红色) */
    snprintf(buf, sizeof(buf), "%s %02d/%02d", wd_str[wd%7], mo, d);
    lv_label_set_text(cy_date_label, buf);

    /* ---- 状态栏图标 ---- */

    /* WiFi 图标: LV_SYMBOL_WIFI, 蓝色=已连接, 灰色=未连接 */
    lv_label_set_text(cy_wifi_label, LV_SYMBOL_WIFI);
    if (g_wifi_connected) {
        lv_obj_set_style_text_color(cy_wifi_label, CYBER_CYAN, 0);
    } else {
        lv_obj_set_style_text_color(cy_wifi_label, CYBER_GRAY, 0);
    }

    /* 蓝牙图标: LV_SYMBOL_BLUETOOTH, 蓝色=已连接, 灰色=未连接 */
    lv_label_set_text(cy_bt_label, LV_SYMBOL_BLUETOOTH);
    if (g_bluetooth_connected) {
        lv_obj_set_style_text_color(cy_bt_label, CYBER_CYAN, 0);
    } else {
        lv_obj_set_style_text_color(cy_bt_label, CYBER_GRAY, 0);
    }

    /* 电池图标: LV_SYMBOL_BATTERY_FULL, 绿色=满(≥50%), 黄色=中(20~49%), 红色=低(<20%) */
    lv_label_set_text(cy_battery_label, LV_SYMBOL_BATTERY_FULL);
    if (g_battery_level >= 50) {
        lv_obj_set_style_text_color(cy_battery_label, CYBER_GREEN, 0);
    } else if (g_battery_level >= 20) {
        lv_obj_set_style_text_color(cy_battery_label, CYBER_YELLOW, 0);
    } else {
        lv_obj_set_style_text_color(cy_battery_label, CYBER_RED, 0);
    }

    /* ---- 环形仪表: 温度 + 湿度 ---- */

    if (g_weather_ready) {
        /* 温度环形仪表 */
        int temp = g_temperature;
        if (temp < -20) temp = -20;
        if (temp > 50)  temp = 50;
        lv_arc_set_value(cy_temp_arc, temp);

        /* 温度中心数值 */
        snprintf(buf, sizeof(buf), "%d°C", temp);
        lv_label_set_text(cy_temp_val_label, buf);

        /* 温度弧颜色: 青(<10°C) / 绿(10~30°C) / 红(>30°C) */
        lv_color_t temp_color;
        if (temp < 10) {
            temp_color = CYBER_CYAN;
        } else if (temp <= 30) {
            temp_color = CYBER_GREEN;
        } else {
            temp_color = CYBER_RED;
        }
        lv_obj_set_style_arc_color(cy_temp_arc, temp_color, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(cy_temp_val_label, temp_color, 0);

        /* 湿度环形仪表 */
        int humid = g_humidity;
        if (humid < 0)   humid = 0;
        if (humid > 100) humid = 100;
        lv_arc_set_value(cy_humid_arc, humid);

        /* 湿度中心数值 */
        snprintf(buf, sizeof(buf), "%d%%", humid);
        lv_label_set_text(cy_humid_val_label, buf);

        /* 湿度弧颜色: 黄(<30%) / 青(30~70%) / 粉(>70%) */
        lv_color_t hum_color;
        if (humid < 30) {
            hum_color = CYBER_YELLOW;
        } else if (humid <= 70) {
            hum_color = CYBER_CYAN;
        } else {
            hum_color = CYBER_PINK;
        }
        lv_obj_set_style_arc_color(cy_humid_arc, hum_color, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(cy_humid_val_label, hum_color, 0);

        /* * 【安全修复】创建一个局部的缓冲区，把 volatile 字符串安全地拷贝出来
        * 假设天气描述最大长度不超过 32 字节
        */
        char local_weather_desc[32] = {0};
        
        // 使用 snprintf 安全地将 volatile 的全局变量复制给非 volatile 的局部数组
        snprintf(local_weather_desc, sizeof(local_weather_desc), "%s", (const char *)g_weather_desc);

        /* * 现在将没有 volatile 风险的局部变量传给 LVGL，警告完美消失！*/
        lv_label_set_text(cy_weather_label, local_weather_desc);

        /* 中文风速信息 */
        snprintf(buf, sizeof(buf), "风速：%dkm/h", g_wind_speed);
        lv_label_set_text(cy_wind_label, buf);
    }

    /* ---- 底部同步状态 ---- */
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

/* ==================== 构建赛博朋克时钟 (240*280 + R角屏完美适配版) ==================== */
static void lvgl_draw_cyberpunk_clock(void)
{
    lv_obj_clean(app_screen);
    lv_obj_set_style_bg_color(app_screen, CYBER_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(app_screen, LV_OPA_COVER, LV_PART_MAIN);

    /* 赛博朋克全屏背景图 (240×280, RGB565, 最底层) */
    lv_obj_t *bg_img = lv_image_create(app_screen);
    lv_image_set_src(bg_img, &sbpk_240x280);
    lv_obj_align(bg_img, LV_ALIGN_CENTER, 0, 0);

    /* ======== 1. 状态栏: WiFi + 蓝牙 (左上), 电池 (右上) ======== */
    /* 【R角优化】将左侧图标向右整体挪动，防止左上角R角切割 */
    cy_wifi_label = lv_label_create(app_screen);
    lv_label_set_text(cy_wifi_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(cy_wifi_label, CYBER_GRAY, 0);
    lv_obj_set_style_text_font(cy_wifi_label, &lv_font_montserrat_14, 0);
    lv_obj_align(cy_wifi_label, LV_ALIGN_TOP_LEFT, 24, 8); // X从12改到24，安全避开R角

    cy_bt_label = lv_label_create(app_screen);
    lv_label_set_text(cy_bt_label, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(cy_bt_label, CYBER_GRAY, 0);
    lv_obj_set_style_text_font(cy_bt_label, &lv_font_montserrat_14, 0);
    lv_obj_align(cy_bt_label, LV_ALIGN_TOP_LEFT, 52, 8); // 顺延挪动

    /* 【R角优化】将右侧电池图标向左挪动，防止右上角R角切割 */
    cy_battery_label = lv_label_create(app_screen);
    lv_label_set_text(cy_battery_label, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_color(cy_battery_label, CYBER_GREEN, 0);
    lv_obj_set_style_text_font(cy_battery_label, &lv_font_montserrat_14, 0);
    lv_obj_align(cy_battery_label, LV_ALIGN_TOP_RIGHT, -24, 8); // X从-12改到-24，安全避开R角


    /* ======== 2. 日期与时间 (【冻结保留】完美居中版 + 顶部R角微调) ======== */
    cy_date_label = lv_label_create(app_screen);
    lv_label_set_text(cy_date_label, "--- --/--");
    lv_obj_set_style_text_color(cy_date_label, CYBER_PINK, 0);
    lv_obj_set_style_text_font(cy_date_label, &lv_font_montserrat_28, 0);
    /* 【R角优化】日期大字稍微往下靠一点（26 -> 32），防止字头碰到上方边缘弧度 */
    lv_obj_align(cy_date_label, LV_ALIGN_TOP_MID, 0, 32); 

    cy_time_label = lv_label_create(app_screen);
    lv_label_set_text(cy_time_label, "--:--:--");
    lv_obj_set_style_text_color(cy_time_label, CYBER_CYAN, 0);
    lv_obj_set_style_text_font(cy_time_label, &lv_font_montserrat_48, 0);
    
    lv_obj_set_width(cy_time_label, 240); // 占满240屏幕宽度
    lv_obj_set_style_text_align(cy_time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(cy_time_label, cy_date_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 2); 


    /* ======== 3. 赛博朋克环形仪表: 温度 (左) + 湿度 (右) ======== */
    /* 因为是垂直280长屏，圆弧居中靠下设置在 155，天然处于屏幕最胖、最安全的腰部，不受R角影响 */
    int arc_y_center = 155; 
    int arc_x_offset = 58; 

    /* ---------------- 左侧：温度弧 ---------------- */
    cy_temp_arc = create_arc_gauge(app_screen, 76, 
                                   -20, 50,
                                   CYBER_CYAN, CYBER_DIM);
    lv_obj_align(cy_temp_arc, LV_ALIGN_TOP_MID, -arc_x_offset, arc_y_center - 38);

    cy_temp_val_label = lv_label_create(app_screen);
    lv_label_set_text(cy_temp_val_label, "--°");
    lv_obj_set_style_text_color(cy_temp_val_label, CYBER_CYAN, 0);
    lv_obj_set_style_text_font(cy_temp_val_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(cy_temp_val_label, cy_temp_arc, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *temp_tag = lv_label_create(app_screen);
    lv_label_set_text(temp_tag, "温度");
    lv_obj_set_style_text_color(temp_tag, CYBER_WHITE, 0);
    lv_obj_set_style_text_font(temp_tag, &weather_chinese_font, 0);
    lv_obj_align_to(temp_tag, cy_temp_arc, LV_ALIGN_OUT_BOTTOM_MID, 0, 1);

    /* ---------------- 右侧：湿度弧 ---------------- */
    cy_humid_arc = create_arc_gauge(app_screen, 76, 
                                    0, 100,
                                    CYBER_CYAN, CYBER_DIM);
    lv_obj_align(cy_humid_arc, LV_ALIGN_TOP_MID, arc_x_offset, arc_y_center - 38);

    cy_humid_val_label = lv_label_create(app_screen);
    lv_label_set_text(cy_humid_val_label, "--%");
    lv_obj_set_style_text_color(cy_humid_val_label, CYBER_CYAN, 0);
    lv_obj_set_style_text_font(cy_humid_val_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(cy_humid_val_label, cy_humid_arc, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *hum_tag = lv_label_create(app_screen);
    lv_label_set_text(hum_tag, "湿度");
    lv_obj_set_style_text_color(hum_tag, CYBER_WHITE, 0);
    lv_obj_set_style_text_font(hum_tag, &weather_chinese_font, 0);
    lv_obj_align_to(hum_tag, cy_humid_arc, LV_ALIGN_OUT_BOTTOM_MID, 0, 1);


    /* ======== 4. 中文天气 & 风速 (【改行居中】文字收缩在中央，天生免疫R角) ======== */

    /* [天气描述] */
    cy_weather_label = lv_label_create(app_screen);
    lv_label_set_text(cy_weather_label, "等待中...");
    lv_obj_set_style_text_color(cy_weather_label, CYBER_PINK, 0);
    lv_obj_set_style_text_font(cy_weather_label, &weather_chinese_font, 0);
    
    lv_obj_set_width(cy_weather_label, 240); 
    lv_obj_set_style_text_align(cy_weather_label, LV_TEXT_ALIGN_CENTER, 0); 
    lv_obj_align(cy_weather_label, LV_ALIGN_TOP_MID, 0, 204); 

    /* [风速信息] */
    cy_wind_label = lv_label_create(app_screen);
    lv_label_set_text(cy_wind_label, "风速：--km/h");
    lv_obj_set_style_text_color(cy_wind_label, CYBER_WHITE, 0);
    lv_obj_set_style_text_font(cy_wind_label, &weather_chinese_font, 0);
    
    lv_obj_set_width(cy_wind_label, 240); 
    lv_obj_set_style_text_align(cy_wind_label, LV_TEXT_ALIGN_CENTER, 0); 
    lv_obj_align_to(cy_wind_label, cy_weather_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);


    /* ======== 5. 底部状态栏 (【R角优化】向上收缩安全区) ======== */
    lv_obj_t *status_box = lv_obj_create(app_screen);
    /* 稍微缩短一点宽度（200 -> 180），让长方形边框的左右两个底角远离屏幕边缘的R角 */
    lv_obj_set_size(status_box, 180, 22); 
    /* 【关键】从底部向上抬高到 -12 像素（原先是-4），让框体完美避开最底部的圆弧盲区 */
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
    LOG_INF("Cyberpunk clock 240x280 R-Corner layout compiled.");
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