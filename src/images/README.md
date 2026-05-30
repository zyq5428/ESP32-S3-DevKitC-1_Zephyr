# 图像资源目录

将 LVGL 需要的图像文件放在此目录下。

## 如何生成图像 C 数组

### 方法 1: LVGL 在线工具 (推荐)

1. 打开 https://lvgl.io/tools/imageconverter
2. 上传你的 PNG/JPG/BMP 图像
3. 选择 **Color format**: `RGB565` (16-bit)
4. 选择 **Output format**: `C array`
5. 勾选 **Dither**: 保留更多色彩细节
6. 点击 **Convert**, 下载生成的 `.c` 文件
7. 将 `.c` 文件放入此目录, 在代码中 `#include "images/xxx.h"`

### 方法 2: 命令行工具

```bash
# 安装 Python 工具
pip install lvgl-image-converter

# 转换图像
lv_img_conv.py logo.png --output-file logo.c --color-format RGB565
```

## 在代码中使用

```c
#include "images/my_logo.h"  // 引入图像描述符

// 声明外部图像 (LV_IMAGE_DECLARE 宏)
LV_IMAGE_DECLARE(my_logo);

// 创建 LVGL 图像对象
lv_obj_t *img = lv_image_create(lv_screen_active());
lv_image_set_src(img, &my_logo);
lv_obj_center(img);
```

## 图像大小参考

| 分辨率 | RGB565 大小 | 说明 |
|--------|------------|------|
| 32x32 | 2 KB | 小图标 |
| 64x64 | 8 KB | 中图标 |
| 128x128 | 32 KB | 大图标 |
| 240x280 | 131 KB | 全屏壁纸 (建议放 PSRAM) |

> 注意: 全屏图像 (240x280, 131KB) 无法放在内部 SRAM,
> 需要使用 `shared_multi_heap_alloc(SMH_REG_ATTR_EXTERNAL, ...)` 分配到 PSRAM。

## 查看效果

修改 `lcd_lvgl_thread.c` 中的 `lvgl_create_ui()` 函数,
在合适位置添加图像对象即可在 LCD 上看到你的图片。
