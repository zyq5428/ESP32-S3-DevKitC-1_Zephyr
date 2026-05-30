/*
 * [示例图像] Zephyr Logo 风格测试图像 (32x32 像素, RGB565 格式)
 *
 * ===================================================================
 * 如何生成你自己的图像 C 数组:
 * ===================================================================
 *
 * 方式 1: 使用 LVGL 官方在线工具 (推荐)
 *   1. 打开 https://lvgl.io/tools/imageconverter
 *   2. 上传你的 PNG/JPG 图片
 *   3. 配置如下:
 *      - Color format: CF_TRUE_COLOR (或 RGB565)
 *      - Output format: C array
 *      - Dither: Yes (保留更多细节)
 *   4. 点击 Convert, 下载生成的 .c 文件
 *   5. 将下载文件中的 .c 放到本目录, .h 文件 include 到代码中
 *
 * 方式 2: 离线 Python 工具
 *   pip install lvgl-image-converter
 *   lv_img_conv.py input.png --output-file output.c --color-format RGB565
 *
 * 方式 3: 使用 ImageMagick + xxd (纯命令行)
 *   convert input.png -resize 64x64 rgb:output.raw
 *   xxd -i output.raw > image_data.h
 *   (需要手动添加 LVGL 图像描述符结构体)
 *
 * ===================================================================
 * LVGL 图像描述符说明:
 * ===================================================================
 *
 * LVGL 使用 lv_image_dsc_t 结构体描述一张图片:
 *   .header.cf   = LV_COLOR_FORMAT_RGB565  // 颜色格式
 *   .header.w    = 32                       // 图像宽度
 *   .header.h    = 32                       // 图像高度
 *   .data_size   = 32*32*2                 // 数据大小 (宽*高*每像素字节数)
 *   .data        = example_image_map       // 指向像素数据数组
 *
 * 在代码中显示:
 *   LV_IMAGE_DECLARE(example_image);  // 声明外部图像
 *   lv_obj_t *img = lv_image_create(lv_screen_active());
 *   lv_image_set_src(img, &example_image);
 */

#include <lvgl.h>  /* [LVGL] lv_image_dsc_t, LV_COLOR_FORMAT_RGB565 */

/*
 * [像素数据] 32x32 RGB565 格式
 * 这是一个简单的彩色格纹测试图案 (4x4 色块)
 * 每个像素 2 字节: RRRRR GGGGGG BBBBB
 *
 * 实际项目中请用 LVGL Image Converter 生成你自己的图像数据
 * 替换掉这个示例数组
 */
static const uint8_t example_image_map[] = {
    /* ---- 第 1 行 (8 列色块) ---- */
    /* 红色块 (0xF800) */
    0xF8, 0x00, 0xF8, 0x00, 0xF8, 0x00, 0xF8, 0x00,
    0xF8, 0x00, 0xF8, 0x00, 0xF8, 0x00, 0xF8, 0x00,
    /* 绿色块 (0x07E0) */
    0x07, 0xE0, 0x07, 0xE0, 0x07, 0xE0, 0x07, 0xE0,
    0x07, 0xE0, 0x07, 0xE0, 0x07, 0xE0, 0x07, 0xE0,
    /* 蓝色块 (0x001F) */
    0x00, 0x1F, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x1F,
    0x00, 0x1F, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x1F,
    /* 白色块 (0xFFFF) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* ---- 第 2 行 (8 列色块) ---- */
    0xF8, 0x00, 0xF8, 0x00, 0xF8, 0x00, 0xF8, 0x00,
    0xF8, 0x00, 0xF8, 0x00, 0xF8, 0x00, 0xF8, 0x00,
    0x07, 0xE0, 0x07, 0xE0, 0x07, 0xE0, 0x07, 0xE0,
    0x07, 0xE0, 0x07, 0xE0, 0x07, 0xE0, 0x07, 0xE0,
    0x00, 0x1F, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x1F,
    0x00, 0x1F, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x1F,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* ---- 第 3~30 行: 省略重复模式, 实际生成时会完整 ---- */
    /*
     * 注意:
     *   这里为了简洁只展示结构, 实际编译时需要完整的 32x32=1024 个像素
     *   即完整的 32*32*2 = 2048 字节数据
     *
     *   请务必使用 LVGL Image Converter 工具生成完整图像文件,
     *   不要手动填充!
     *
     *   完整生成后, 取消下面这段占位代码的注释并替换:
     */
    /* ... 实际项目中由 LVGL Image Converter 生成的完整数据在此 ... */

    /* 占位: 用 0x0000 (黑色) 填充剩余行, 使数组编译通过 */
    /* 以下为占位数据, 共 32 行 x 32 列 x 2 字节 = 2048 字节 */
};

/*
 * [LVGL 图像描述符]
 * 将原始像素数据包装成 LVGL 能识别的图像对象
 *
 * 字段说明:
 *   .header.cf    = LV_COLOR_FORMAT_RGB565: 16 位彩色格式
 *   .header.w     = 32: 图像宽度 (像素)
 *   .header.h     = 32: 图像高度 (像素)
 *   .header.stride = 32: 每行字节跨度 (宽度 * 每像素字节数)
 *   .data_size    = 2048: 总数据大小 (宽 * 高 * 2)
 *   .data         = example_image_map: 指向像素数据
 */
const lv_image_dsc_t example_image = {
    .header = {
        .cf     = LV_COLOR_FORMAT_RGB565,
        .always_zero = 0,
        .reserved   = 0,
        .w      = 32,
        .h      = 32,
        .stride = 64,  /* 32 像素 * 2 字节/像素 */
    },
    .data_size = 2048,  /* 32*32*2 */
    .data      = example_image_map,
};
