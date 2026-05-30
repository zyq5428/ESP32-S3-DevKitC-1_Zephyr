/*
 * [Zephyr Logo 图像] 200x116 像素, RGB565 格式
 *
 * 由 LVGL Image Converter 从 Zephyr 官方 logo PNG 生成
 * 原始图像: Zephyr Project logo
 * 尺寸: 200 × 116 像素
 * 格式: RGB565 (16 位色, 每像素 2 字节)
 * 数据大小: 200 × 116 × 2 = 46,400 字节 (约 45 KB)
 */

#ifndef ZEPHYR_200X116_H
#define ZEPHYR_200X116_H

#include <lvgl.h>  /* [LVGL] lv_image_dsc_t 类型定义 */

/*
 * [外部声明] LV_IMAGE_DECLARE 宏声明一个外部 LVGL 图像描述符
 *
 * 展开后等价于:
 *   extern const lv_image_dsc_t zephyr_200x116;
 *
 * 图像描述符包含:
 *   .header.cf  = LV_COLOR_FORMAT_RGB565
 *   .header.w   = 200
 *   .header.h   = 116
 *   .data       = zephyr_200x116_map (像素数据数组)
 *
 * 像素数据存储在 Flash 的 .rodata 段 (const 只读数据区),
 * 不会占用宝贵的内部 SRAM。
 */
LV_IMAGE_DECLARE(zephyr_200x116);

#endif /* ZEPHYR_200X116_H */
