# mupdf_wrapper SDK 函数说明文档

## 概述

`mupdf_wrapper` 是一个基于 MuPDF 的 PDF 处理封装库，提供 C 接口，支持 VS2010+ 调用。

**输出目录**: `F:\projects\mupdf-wrapper\sdk\x86\`

## 文件清单

| 文件 | 说明 |
|------|------|
| `mupdf_wrapper.dll` | 动态链接库 (40.5 MB) |
| `mupdf_wrapper_vc10_x86.lib` | VS2010 兼容的导入库 |
| `mupdf_wrapper.h` | 头文件 |

---

## 核心数据结构

### LayoutRule - 布局规则

```c
typedef struct LayoutRule_s {
    int type;           // 位置类型: 0=绝对坐标, 1=左上, 2=右上, 3=左下, 4=右下, 5=居中
    float margin_x;     // X方向边距（点）
    float margin_y;     // Y方向边距（点）：正值向下移动，负值向上移动
    float scale_w;      // 宽度缩放 (暂未使用)
    float scale_h;      // 高度缩放 (暂未使用)
    int relative;       // 是否相对于页面尺寸 (暂未使用)
    float text_width;   // 文字宽度（pt）：0=使用默认值200，不换行需要设置足够大的值
} LayoutRule;
```

**type 类型说明**:
- `0` - 绝对坐标：直接使用 margin_x, margin_y 作为坐标
- `1` - 左上角：x=margin_x, y=margin_y
- `2` - 右上角：x=页面宽度-内容宽度-margin_x, y=margin_y
- `3` - 左下角：x=margin_x, y=页面高度-内容高度-margin_y
- `4` - 右下角：x=页面宽度-内容宽度-margin_x, y=页面高度-内容高度-margin_y
- `5` - 居中：x=(页面宽度-内容宽度)/2, y=(页面高度-内容高度)/2

**margin_y 说明**:
- 正值：向下移动
- 负值：向上移动（可超出页面边界）

**text_width 说明**:
- 0：使用默认值 200pt
- 正值：设置文字框宽度，足够大时可防止自动换行

---

### PrintOptions - 打印选项

```c
typedef struct PrintOptions_s {
    int copies;              // 打印份数
    int duplex;              // 双面打印模式: 0=单面, 1=长边翻转, 2=短边翻转
    int collate;             // 分拣: 0=不分拣, 1=分拣
    int color;               // 颜色: 0=黑白, 1=彩色
    float scale;             // 缩放比例 (0=自动适应纸张, 1=原始大小)
    int orientation;         // 方向: 0=自动, 1=纵向, 2=横向
    int from_page;           // 起始页 (1-based, 0=全部)
    int to_page;             // 结束页 (1-based, 0=全部)
    const char* job_name;    // 打印作业名称
} PrintOptions;
```

**duplex 双面打印模式详解**:

| 值 | 模式 | 说明 | 适用场景 |
|----|------|------|----------|
| 0 | 单面打印 | 每张纸只打印一面 | 普通打印机、单面纸张 |
| 1 | 长边翻转 (Long Edge Flip) | 沿纸张长边翻页 | 横向打印的文档 |
| 2 | 短边翻转 (Short Edge Flip) | 沿纸张短边翻页 | 纵向打印的文档 |

**示意图**:

```
长边翻转 (duplex=1)          短边翻转 (duplex=2)
┌─────┬─────┐              ┌─────┬─────┐
│     │     │翻页           │     │ ↓   │
│ 正面 │ 反面│              │ 正面 │     │
│     │     │              │     │     │
├─────┼─────┤              ├─────┼─────┤
│     │     │              │     │ ↑   │
│ 反面 │ 正面│              │ 反面 │     │
│     │     │              │     │     │
└─────┴─────┘              └─────┴─────┘
```

---

## 函数详细说明

### 初始化与销毁

#### mupdf_init

```c
int mupdf_init(MupdfContext** ctx_out);
```

**功能**: 初始化 MuPDF 封装库

**参数**:
- `ctx_out` - 输出参数，返回上下文句柄

**返回值**: 成功返回 0，失败返回错误码

**示例**:
```c
MupdfContext* ctx;
mupdf_init(&ctx);
```

---

#### mupdf_fini

```c
int mupdf_fini(MupdfContext* ctx);
```

**功能**: 销毁 MuPDF 封装库上下文

**参数**:
- `ctx` - 上下文句柄

**返回值**: 成功返回 0

---

### 文档操作

#### mupdf_open_document

```c
int mupdf_open_document(MupdfContext* ctx, const char* path, MupdfDocument** doc_out);
```

**功能**: 打开 PDF 文档

**参数**:
- `ctx` - 上下文句柄
- `path` - 文件路径（UTF-8 编码）
- `doc_out` - 输出参数，返回文档句柄

**返回值**: 成功返回 0，失败返回错误码

---

#### mupdf_save_document

```c
int mupdf_save_document(MupdfContext* ctx, MupdfDocument* doc, const char* path);
```

**功能**: 保存 PDF 文档到文件

**参数**:
- `ctx` - 上下文句柄
- `doc` - 文档句柄
- `path` - 输出路径（UTF-8 编码）

**返回值**: 成功返回 0，失败返回错误码

**说明**: 将修改后的 PDF 文档保存到指定路径。MuPDF 会自动处理 PDF 的完整性，包括交叉引用表更新等。

---

#### mupdf_close_document

```c
int mupdf_close_document(MupdfContext* ctx, MupdfDocument* doc);
```

**功能**: 关闭 PDF 文档

**参数**:
- `ctx` - 上下文句柄
- `doc` - 文档句柄

**返回值**: 成功返回 0

---

### 批量添加图片

#### mupdf_batch_add_image

```c
int mupdf_batch_add_image(
    MupdfContext* ctx,
    MupdfDocument* doc,
    const int* page_nums,    // 页码数组，NULL=所有页面
    int page_count,           // 页码数量，-1=所有页面
    const char* image_path,   // 图片路径
    const LayoutRule* rule,   // 位置规则，NULL=默认规则(左上角，margin_y=10)
    float width,              // 固定宽度，0=自动
    float height              // 固定高度，0=自动
);
```

**功能**: 批量添加图片到多个页面

**参数详解**:
| 参数 | 说明 |
|------|------|
| `ctx` | 上下文句柄，由 mupdf_init 创建 |
| `doc` | 文档句柄，由 mupdf_open_document 打开 |
| `page_nums` | 页码数组，如 `{0, 2, 5}` 表示第1、3、6页；NULL 表示所有页面 |
| `page_count` | 页码数量；-1 表示处理所有页面 |
| `image_path` | 图片文件路径，支持 jpeg/png/bmp 等格式 |
| `rule` | 布局规则，控制图片位置；NULL 使用默认规则（type=1 左上，margin_x=10, margin_y=10） |
| `width` | 图片宽度（点），0=自动根据图片原始尺寸计算 |
| `height` | 图片高度（点），0=自动根据图片原始尺寸计算 |

**返回值**: 成功返回添加到的页面数，失败返回负数

**示例**:
```c
// 在所有页面的左上角（向下偏移 20 点）添加图片
LayoutRule rule;
memset(&rule, 0, sizeof(rule));
rule.type = 1;  // 左上
rule.margin_x = 10;
rule.margin_y = 20;  // 正值向下

mupdf_batch_add_image(ctx, doc, NULL, -1, "barcode.png", &rule, 0, 50);

// 在所有页面的左上角（向上偏移 -20 点）添加图片（超出边界）
rule.margin_y = -20;  // 负值向上移动
mupdf_batch_add_image(ctx, doc, NULL, -1, "barcode.png", &rule, 0, 50);
```

---

### 批量添加文字

#### mupdf_batch_add_text

```c
int mupdf_batch_add_text(
    MupdfContext* ctx,
    MupdfDocument* doc,
    const int* page_nums,
    int page_count,
    const char* text,         // 文字内容
    float font_size,          // 字体大小（点）
    const LayoutRule* rule    // 位置规则，NULL=默认规则
);
```

**功能**: 批量添加文字到多个页面

**参数详解**:
| 参数 | 说明 |
|------|------|
| `ctx` | 上下文句柄 |
| `doc` | 文档句柄 |
| `page_nums` | 页码数组，NULL=所有页面 |
| `page_count` | 页码数量，-1=所有页面 |
| `text` | 要添加的文字内容（UTF-8） |
| `font_size` | 字体大小，单位：点（pt） |
| `rule` | 布局规则，控制文字位置 |

**返回值**: 成功返回添加到的页面数

**示例**:
```c
// 添加中文文字
LayoutRule rule = {0};
rule.type = 1;
rule.margin_x = 50;
rule.margin_y = -30;  // 向上移动

mupdf_batch_add_text(ctx, doc, NULL, -1, "条码测试 123456", 14, &rule);
```

---

## 完整使用示例

```c
#include "mupdf_wrapper.h"

int main() {
    MupdfContext* ctx = NULL;
    MupdfDocument* doc = NULL;

    // 初始化
    mupdf_init(&ctx);

    // 打开 PDF
    if (mupdf_open_document(ctx, "input.pdf", &doc) != 0) {
        printf("Error: %s\n", mupdf_get_error(ctx));
        return 1;
    }

    // 设置布局规则：左上角，向上偏移 -10 点
    LayoutRule rule = {0};
    rule.type = 1;           // 左上
    rule.margin_x = 10;
    rule.margin_y = -10;    // 负值向上移动

    // 添加条码图片
    int count = mupdf_batch_add_image(ctx, doc, NULL, -1, "barcode.png", &rule, 0, 40);
    printf("Added to %d pages\n", count);

    // 添加文字
    LayoutRule text_rule = {0};
    text_rule.type = 1;
    text_rule.margin_x = 10;
    text_rule.margin_y = -25;  // 向上移动

    mupdf_batch_add_text(ctx, doc, NULL, -1, "测试文字", 12, &text_rule);

    // 保存
    mupdf_save_document(ctx, doc, "output.pdf");

    // 清理
    mupdf_close_document(ctx, doc);
    mupdf_fini(ctx);

    return 0;
}
```

---

## 编译与链接

### VS2010 项目配置

1. **头文件搜索路径**: 添加 `F:\projects\mupdf-wrapper\sdk\include`
2. **库文件搜索路径**: 添加 `F:\projects\mupdf-wrapper\sdk\x86`
3. **附加依赖项**: 添加 `mupdf_wrapper_vc10_x86.lib`
4. **复制 DLL**: 将 `mupdf_wrapper.dll` 复制到可执行文件目录

### 代码中声明

```c
#pragma comment(lib, "mupdf_wrapper_vc10_x86.lib")
#include "mupdf_wrapper.h"
```

---

## 错误码

| 错误码 | 说明 |
|--------|------|
| -1 | 参数错误 |
| -2 | 文档操作失败 |
| -3 | 写入失败 |

获取错误信息: `mupdf_get_error(ctx)`

---

## 版本信息

- 库版本: 1.0.0
- MuPDF 版本: 1.x
- 编译环境: VS2022 x86
- SDK 路径: `F:\projects\mupdf-wrapper\sdk\x86`