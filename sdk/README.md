# MuPDF Wrapper SDK 使用指南

## 1. 概述

`mupdf_wrapper.dll` 是基于 MuPDF 的 PDF 处理动态库，提供以下功能：

- 📄 **文档操作**：打开、保存 PDF 文档（支持文件路径和内存）
- 🖼️ **添加图片**：为指定页面添加 JPG/PNG/BMP 图片（支持自动 DPI 换算）
- ✍️ **添加文字**：为指定页面添加文字水印，支持位置布局
- 🖨️ **打印支持**：Windows GDI 打印，支持单面/双面、彩色/黑白
- 📐 **渲染**：将 PDF 页面渲染为 RGBA 图像数据

**版本要求**：Windows x64 或 x86（32位），MSVC 编译环境（VS2010+）

---

## 2. 文件清单

```
sdk/
├── mupdf_wrapper.dll           # 动态链接库 x64（运行时需要）
├── mupdf_wrapper.lib           # 导入库 x64（VS2015+）
├── lib/
│   ├── mupdf_wrapper.lib       # 导入库 x64（VS2015+ / VS2022 使用）
│   ├── mupdf_wrapper_vc10.lib  # 导入库 x64（VS2010 / VS2012 / VS2013 使用）
│   └── mupdf_wrapper_vs2010.def  # 符号定义文件
├── x86/                        # ← x86（32位）产物目录
│   ├── mupdf_wrapper.dll      # 动态链接库 x86
│   ├── mupdf_wrapper.lib      # 导入库 x86（VS2015+）
│   └── mupdf_wrapper_vc10_x86.lib  # 导入库 x86（VS2010+）
├── include/
│   ├── mupdf_wrapper.h    # 主头文件
│   ├── print_engine.h     # 打印引擎头文件
│   └── mupdf/             # MuPDF 底层头文件（83个头文件）
├── examples/
│   ├── example_basic.c    # 基础示例（添加图片+文字）
│   └── example_print.c    # 打印示例
└── README.md              # 本文档
```

---

## 3. 快速开始

### 3.1 项目配置（VS2010）

1. **头文件路径**：在项目属性 → C/C++ → 附加包含目录，添加：
   ```
   $(ProjectDir)sdk\include
   ```

2. **库路径**：在项目属性 → 链接器 → 附加库目录，添加：
   ```
   $(ProjectDir)sdk\lib
   ```

3. **依赖库**：在链接器 → 输入 → 附加依赖项，添加：
   ```
   mupdf_wrapper_vc10.lib
   ```
   > ⚠️ **VS2010/2012/2013 必须使用 `mupdf_wrapper_vc10.lib`**，不要用 `mupdf_wrapper.lib`（后者是 VS2022 格式，旧版链接器不兼容）。
   >
   > VS2015 及以上版本两个 .lib 均可使用，推荐用 `mupdf_wrapper.lib`。
   >
   > **x86 项目**：使用 `x86\mupdf_wrapper_vc10_x86.lib`。

4. **运行时目录**：将 `sdk\mupdf_wrapper.dll` 复制到编译输出目录，与生成的 .exe 同目录。

### 3.3 运行时依赖

DLL 仅依赖 Windows 系统 DLL，无第三方依赖：

| DLL | 说明 | 备注 |
|-----|------|------|
| KERNEL32.dll / USER32.dll / GDI32.dll | Windows 核心 API | 所有 Windows 版本内置 |
| WINSPOOL.DRV | 打印支持 | 所有 Windows 版本内置 |
| MSVCP140.dll / VCRUNTIME140.dll / VCRUNTIME140_1.dll | VS2015+ CRT | **Windows 10 1903+ 内置** |
| api-ms-win-crt-*.dll | UCRT（通用 C 运行时）| **Windows 10 1511+ 内置** |

> ⚠️ **目标系统要求**：**Windows 10 x64** 或更高版本（x64 DLL）。x86 DLL 兼容 Windows 7+（32位系统）。Windows 7/8.1 x64 用户需安装 [VC++ Redistributable (VS2015-2022)](https://aka.ms/vs/17/release/vc_redist.x64.exe) 或 [KB2999226](https://support.microsoft.com/kb/2999226)。

> ✅ **VS2010 兼容性**：VS2010 项目**可以直接链接和使用此 DLL**。因为 DLL 导出的是标准 C 函数（`extern "C"`），ABI 与编译器版本无关。VS2010 项目只需要 `#include "mupdf_wrapper.h"` 并链接 `mupdf_wrapper.lib` 即可。不同编译器使用各自的 CRT 是正常行为，不会产生冲突。

### 3.2 最简示例

```c
#include "mupdf_wrapper.h"
#include <stdio.h>

int main() {
    MupdfContext* ctx = NULL;
    MupdfDocument* doc = NULL;

    // 1. 初始化
    if (mupdf_init(&ctx) != 0) {
        printf("初始化失败: %s\n", mupdf_get_error(ctx));
        return 1;
    }

    // 2. 打开 PDF
    if (mupdf_open_document(ctx, "input.pdf", &doc) != 0) {
        printf("打开失败: %s\n", mupdf_get_error(ctx));
        mupdf_fini(ctx);
        return 1;
    }

    // 3. 保存（只读打开，不会修改原文件）
    if (mupdf_save_document(ctx, doc, "output.pdf") != 0) {
        printf("保存失败: %s\n", mupdf_get_error(ctx));
    } else {
        printf("保存成功！\n");
    }

    // 4. 清理
    mupdf_close_document(ctx, doc);
    mupdf_fini(ctx);
    return 0;
}
```

---

## 4. API 参考

### 4.1 初始化与销毁

```c
int mupdf_init(MupdfContext** ctx_out);          // 初始化，返回上下文句柄
int mupdf_fini(MupdfContext* ctx);              // 销毁上下文
const char* mupdf_get_error(MupdfContext* ctx); // 获取最近一次错误信息
```

### 4.2 文档操作

```c
int mupdf_open_document(MupdfContext* ctx, const char* path, MupdfDocument** doc_out);
    // 打开 PDF 文件，path 为 UTF-8 编码路径

int mupdf_open_document_from_mem(MupdfContext* ctx, const unsigned char* data,
                                  size_t size, MupdfDocument** doc_out);
    // 从内存打开 PDF

int mupdf_save_document(MupdfContext* ctx, MupdfDocument* doc, const char* path);
    // 保存 PDF 到文件

int mupdf_save_document_to_mem(MupdfContext* ctx, MupdfDocument* doc,
                                unsigned char** data_out, size_t* size_out);
    // 保存到内存，需调用 mupdf_free_buffer 释放

void mupdf_free_buffer(MupdfContext* ctx, void* data);

int mupdf_close_document(MupdfContext* ctx, MupdfDocument* doc);
```

### 4.3 页面信息

```c
int mupdf_get_page_count(MupdfContext* ctx, MupdfDocument* doc, int* count_out);
    // 获取总页数

int mupdf_get_page_size(MupdfContext* ctx, MupdfDocument* doc,
                         int page_num, float* width_out, float* height_out);
    // 获取页面尺寸（单位：点 pt），page_num 为 0-based
    // A4 纵向：595 x 842 pt；A4 横向：842 x 595 pt

int mupdf_is_page_landscape(MupdfContext* ctx, MupdfDocument* doc, int page_num);
    // 判断页面是否横向
```

### 4.4 位置规则（LayoutRule）

`LayoutRule` 用于批量添加图片/文字时的自动布局：

```c
typedef struct LayoutRule_s {
    int type;        // 位置类型：1=左上, 2=右上, 3=左下, 4=右下, 5=居中, 0=绝对坐标
    float margin_x;  // X 方向边距（点 pt）
    float margin_y;  // Y 方向边距（点 pt）
} LayoutRule;
```

> 📐 **坐标系说明**：MuPDF 使用 **y-向下（屏幕坐标系）**。
> - `type=1/2`（上）→ 内容距页面顶部 `margin_y` pt
> - `type=3/4`（下）→ 内容距页面底部 `margin_y` pt
> - PDF 横向/纵向由 `mupdf_get_page_size` 返回的实际尺寸决定，无需额外处理

### 4.5 添加图片

```c
int mupdf_add_image(MupdfContext* ctx, MupdfDocument* doc,
                    int page_num, const char* image_path,
                    float x, float y, float width, float height);
    // 添加图片到指定页面
    // x, y：图片左下角坐标（pt，MuPDF y-向下坐标系）
    // width, height：图片尺寸（pt），传 0 表示自动：
    //   - 无指定时按图片实际 DPI 自动换算（点数 = 像素数 × 72 / dpi）
    //   - 默认 DPI=96
    // 支持格式：JPG, PNG, BMP
    // 图片会自动裁剪到页面范围内

int mupdf_batch_add_image(MupdfContext* ctx, MupdfDocument* doc,
                           const int* page_nums, int page_count,
                           const char* image_path,
                           const LayoutRule* rule,
                           float width, float height);
    // 批量添加图片到多页
    // page_nums = NULL：所有页面
    // page_count = -1：所有页面
    // rule = NULL：使用默认（左上）布局
    // 返回成功添加的页面数
```

**示例：所有页面添加水印图（左上角）**
```c
LayoutRule rule = {0};
rule.type = 1;          // 左上
rule.margin_x = 10.0f;  // 距左边缘 10pt
rule.margin_y = 10.0f;  // 距顶部 10pt

int added = mupdf_batch_add_image(ctx, doc, NULL, -1,
                                   "watermark.png", &rule, 0, 0);
// added = 成功添加的页面数
```

### 4.6 添加文字

```c
int mupdf_add_text(MupdfContext* ctx, MupdfDocument* doc,
                   int page_num, const char* text,
                   float font_size, float x, float y,
                   float width, float height);
    // 添加文字注释到指定页面
    // font_size：字号（pt），0=默认 12pt
    // x, y：文本框左下角坐标
    // width, height：文本框尺寸，0=自动（约 font_size×1.5+4）
    // 使用 Helvetica 字体，黑色

int mupdf_batch_add_text(MupdfContext* ctx, MupdfDocument* doc,
                          const int* page_nums, int page_count,
                          const char* text, float font_size,
                          const LayoutRule* rule);
    // 批量添加文字（自动适应每页尺寸）
    // rule = NULL：默认左上角布局
```

**示例：每页添加"机密"水印（右上角）**
```c
LayoutRule rule = {0};
rule.type = 2;         // 右上
rule.margin_x = 10.0f;
rule.margin_y = 10.0f;

int added = mupdf_batch_add_text(ctx, doc, NULL, -1,
                                  "机密", 24.0f, &rule);
```

### 4.7 打印

```c
// 打印选项
typedef struct PrintOptions_s {
    int copies;           // 打印份数
    int duplex;           // 0=单面, 1=长边翻转(双面), 2=短边翻转(双面)
    int collate;          // 0=不分拣, 1=分拣
    int color;            // 0=黑白, 1=彩色
    float scale;           // 缩放：0=自动适应, 1=原始大小
    int orientation;      // 0=自动, 1=纵向, 2=横向
    int from_page;        // 起始页（1-based，0=全部）
    int to_page;          // 结束页（1-based，0=全部）
    const char* job_name; // 打印作业名称
} PrintOptions;

int mupdf_print(MupdfContext* ctx, MupdfDocument* doc,
                const char* printer_name, const PrintOptions* options);
    // 打印整个文档

int mupdf_print_pages(MupdfContext* ctx, MupdfDocument* doc,
                       const int* page_nums, int page_count,
                       const char* printer_name, const PrintOptions* options);
    // 打印指定页面
```

**示例：双面打印第 1-5 页**
```c
PrintOptions opts = {0};
opts.copies = 1;
opts.duplex = 1;         // 长边翻转（双面）
opts.color = 0;           // 黑白
opts.scale = 0;           // 自动适应纸张
opts.from_page = 1;
opts.to_page = 5;
opts.job_name = "My PDF Print";

int pages[] = {0, 1, 2, 3, 4}; // 0-based 页码
int ret = mupdf_print_pages(ctx, doc, pages, 5, NULL, &opts);
// printer_name = NULL 表示使用默认打印机
```

### 4.8 渲染

```c
int mupdf_render_page(MupdfContext* ctx, MupdfDocument* doc,
                       int page_num, int dpi, int alpha,
                       int* width_out, int* height_out,
                       unsigned char** data_out, size_t* data_size_out);
    // 渲染页面为 RGBA 图像
    // dpi: 建议 72(屏幕) / 150(预览) / 300(打印)
    // alpha: 0=不透明(RGB24), 1=透明(RGBA32)
    // data_out: 输出缓冲区，需调用 mupdf_free_render_data 释放

void mupdf_free_render_data(MupdfContext* ctx, void* data);
```

---

## 5. 完整示例：批量添加水印

```c
#include "mupdf_wrapper.h"
#include <stdio.h>

int main() {
    MupdfContext* ctx = NULL;
    MupdfDocument* doc = NULL;

    // 初始化
    mupdf_init(&ctx);

    // 打开 PDF
    if (mupdf_open_document(ctx, "report.pdf", &doc) != 0) {
        printf("错误: %s\n", mupdf_get_error(ctx));
        mupdf_fini(ctx);
        return 1;
    }

    // 获取页数
    int total = 0;
    mupdf_get_page_count(ctx, doc, &total);
    printf("PDF 共 %d 页\n", total);

    // ---- 添加文字水印 ----
    LayoutRule text_rule = {0};
    text_rule.type = 2;         // 右上
    text_rule.margin_x = 20.0f;
    text_rule.margin_y = 20.0f;

    int added_text = mupdf_batch_add_text(ctx, doc, NULL, -1,
                                            "CONFIDENTIAL", 18.0f, &text_rule);
    printf("已添加文字水印: %d 页\n", added_text);

    // ---- 添加图片水印 ----
    LayoutRule img_rule = {0};
    img_rule.type = 5;  // 居中
    img_rule.margin_x = 0;
    img_rule.margin_y = 0;

    int added_img = mupdf_batch_add_image(ctx, doc, NULL, -1,
                                           "logo.png", &img_rule, 0, 0);
    printf("已添加图片水印: %d 页\n", added_img);

    // ---- 保存 ----
    if (mupdf_save_document(ctx, doc, "report_watermarked.pdf") == 0) {
        printf("保存成功: report_watermarked.pdf\n");
    }

    mupdf_close_document(ctx, doc);
    mupdf_fini(ctx);
    return 0;
}
```

---

## 6. 错误码

| 返回值 | 含义 |
|--------|------|
| 0 | 成功 |
| -1 | 参数错误（空指针等）|
| -2 | 文件不存在或无法打开 |
| -3 | MuPDF 处理错误（图片损坏、页面不存在等）|
| -4 | 内存分配失败 |
| 负数 | 其他错误，调用 `mupdf_get_error()` 获取详情 |

---

## 7. 注意事项

1. **路径编码**：所有文件路径必须是 **UTF-8** 编码，不支持 ANSI。
2. **坐标系**：MuPDF 使用 y-向下（屏幕坐标系），y=0 为页面顶部。
3. **图片 DPI**：自动根据图片嵌入的 DPI 信息换算；未嵌入则默认 96dpi。
4. **页面旋转**：PDF 的 `/Rotate` 属性会自动处理，横向页面直接用 `mupdf_get_page_size` 返回的实际尺寸。
5. **线程安全**：同一 `MupdfContext` 不可并发使用，每次操作后无需显式同步。
6. **中文支持**：DLL 本身支持中文字符串路径；PDF 内文字水印使用 Helvetica（不支持中文）。
7. **依赖**：DLL 依赖 Windows GDI32、Winspool 等系统库，无其他第三方依赖。
