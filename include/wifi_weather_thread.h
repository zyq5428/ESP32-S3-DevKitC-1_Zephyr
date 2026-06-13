/*
 * [WiFi + 天气 + 时间线程] 全局变量声明
 *
 * 本头文件声明 wifi_weather_thread.c 中定义的全局变量，
 * 供其他线程（如 LCD 显示线程）读取当前的网络状态、时间和天气信息。
 *
 * 使用方式:
 *   #include "wifi_weather_thread.h"
 *   // if (g_wifi_connected) { 显示 WiFi 图标 }
 *   // LOG_INF("当前温度: %d°C", g_temperature);
 */

#ifndef WIFI_WEATHER_THREAD_H
#define WIFI_WEATHER_THREAD_H

#include <zephyr/kernel.h>   /* [内核] volatile bool, atomic_t 等类型 */

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 网络状态变量 ==================== */

/* [WiFi 连接标志] true=已连接路由器并获取到 IP 地址 */
extern volatile bool g_wifi_connected;

/* [WiFi IP 地址字符串] 点分十进制格式，如 "192.168.1.100" */
extern volatile char g_wifi_ip_addr[16];

/* ==================== 时间同步变量 ==================== */

/* [时间同步标志] true=已通过 SNTP 成功获取网络时间 */
extern volatile bool g_time_synced;

/* [当前时间] 时:分:秒 (北京时间 UTC+8) */
extern volatile int g_current_hour;
extern volatile int g_current_minute;
extern volatile int g_current_second;

/* [当前日期] 年/月/日 (北京时间 UTC+8) */
extern volatile int g_current_year;
extern volatile int g_current_month;
extern volatile int g_current_day;

/* [当前星期] 0=周日, 1=周一 ... 6=周六 */
extern volatile int g_current_weekday;

/* ==================== 天气信息变量 ==================== */

/* [天气同步标志] true=已成功获取天气数据 */
extern volatile bool g_weather_ready;

/* [当前温度] 单位：摄氏度(°C)，范围通常 -50 ~ 50 */
extern volatile int g_temperature;

/* [WMO 天气代码] 用于确定天气图标和描述
 *  0=晴天, 1/2/3=多云, 45/48=雾, 51-57=小雨, 61-67=雨
 *  71-77=雪, 80-82=阵雨, 95-99=雷暴
 *  完整对照表: https://www.nodc.noaa.gov/archive/arc0021/0002199/1.1/data/0-data/HTML/WMO-CODE/WMO4677.HTM
 */
extern volatile int g_weather_code;

/* [天气描述字符串] 中文天气描述，如 "晴天"、"多云"、"小雨" 等 */
extern volatile char g_weather_desc[32];

/* [风速] 单位：km/h */
extern volatile int g_wind_speed;

/* ==================== 线程 ID 声明 ==================== */

/* [线程 ID] wifi_weather 线程的 ID，供其他代码引用
 * 注意: K_THREAD_DEFINE 宏将其定义为 const k_tid_t */
extern const k_tid_t wifi_weather_tid;

#ifdef __cplusplus
}
#endif

#endif /* WIFI_WEATHER_THREAD_H */
