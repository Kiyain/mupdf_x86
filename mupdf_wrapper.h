/**
 * @file mupdf_wrapper.h
 * @brief MuPDF 封装库主头文件
 * @details 提供 PDF 文档操作、图片/文字添加、打印等功能的 C 接口
 * @note 支持 VS2010+，跨平台设计
 */
#ifndef MUPDF_WRAPPER_H
#define MUPDF_WRAPPER_H

#include <stddef.h>

#ifdef _WIN32
    #ifdef MUPDF_WRAPPER_EXPORTS
        #define MUPDF_API __declspec(dllexport)
    #else
        #define MUPDF_API __declspec(dllimport)
    #endif
#else
    #define MUPDF_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 前向声明
typedef struct _MupdfContext MupdfContext;
typedef struct _MupdfDocument MupdfDocument;

// 位置规则（公开完整定义，外部调用者可直接使用）
typedef struct LayoutRule_s {
    int type;           /**< 位置类型: 0=右上, 1=左上, 2=右下, 3=左下, 4=上居中, 5=下居中 */
    float margin_x;     /**< X方向边距（点） */
    float margin_y;     /**< Y方向边距（点）：正值向下移动，负值向上移动 */
    float scale_w;      /**< 宽度缩放 (0-1, 暂未使用) */
    float scale_h;      /**< 高度缩放 (0-1, 暂未使用) */
    int relative;       /**< 是否相对于页面尺寸（暂未使用） */
    float text_width;   /**< 文字宽度（pt）：0=使用默认200，不换行需要设置足够大的值 */
    int add_page_number; /**< 是否添加页码后缀: 0=不添加, 1=添加 " - 页/总页" */
    float image_width;   /**< 一维码图像宽度（pt），用于文字左对齐基准 */
    float image_height;  /**< 一维码图像高度（pt），用于文字垂直偏移计算 */
    float text_gap;      /**< 文字与图像之间的间距（pt） */
	float text_width_scale; /**< 文字宽度缩放: 1.0=标准, >1=加宽, 默认1.0 */
    int b5_offset_x;      /**< B5纸张X方向额外偏移(pt), 默认0, 仅B5生效 */
	int b5_offset_y;      /**< B5纸张y方向额外偏移(pt), 默认0, 仅B5生效 */
} LayoutRule;

// PrintOptions 由 print_engine.h 定义
typedef struct PrintOptions_s PrintOptions;

// ============================================
// 初始化与销毁
// ============================================

/**
 * @brief 初始化 MuPDF 封装库
 * @param ctx_out 输出：上下文句柄
 * @return 成功返回 0，失败返回错误码
 */
MUPDF_API int mupdf_init(MupdfContext** ctx_out);

/**
 * @brief 销毁 MuPDF 封装库上下文
 * @param ctx 上下文句柄
 * @return 成功返回 0
 */
MUPDF_API int mupdf_fini(MupdfContext* ctx);

/**
 * @brief 获取错误信息
 * @param ctx 上下文句柄
 * @return 错误信息字符串
 */
MUPDF_API const char* mupdf_get_error(MupdfContext* ctx);

// ============================================
// 文档操作
// ============================================

/**
 * @brief 打开 PDF 文档
 * @param ctx 上下文句柄
 * @param path 文件路径（UTF-8）
 * @param doc_out 输出：文档句柄
 * @return 成功返回 0，失败返回错误码
 */
MUPDF_API int mupdf_open_document(MupdfContext* ctx, const char* path, MupdfDocument** doc_out);

/**
 * @brief 从内存打开 PDF 文档
 * @param ctx 上下文句柄
 * @param data PDF 数据
 * @param size 数据大小
 * @param doc_out 输出：文档句柄
 * @return 成功返回 0，失败返回错误码
 */
MUPDF_API int mupdf_open_document_from_mem(MupdfContext* ctx, const unsigned char* data, size_t size, MupdfDocument** doc_out);

/**
 * @brief 保存 PDF 文档
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param path 输出路径（UTF-8）
 * @return 成功返回 0，失败返回错误码
 */
MUPDF_API int mupdf_save_document(MupdfContext* ctx, MupdfDocument* doc, const char* path);

/**
 * @brief 保存到内存
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param data_out 输出：数据缓冲区（需调用 mupdf_free_buffer 释放）
 * @param size_out 输出：数据大小
 * @return 成功返回 0，失败返回错误码
 */
MUPDF_API int mupdf_save_document_to_mem(MupdfContext* ctx, MupdfDocument* doc, unsigned char** data_out, size_t* size_out);

/**
 * @brief 释放内存缓冲区
 * @param ctx 上下文句柄
 * @param data 数据缓冲区
 */
MUPDF_API void mupdf_free_buffer(MupdfContext* ctx, void* data);

/**
 * @brief 关闭 PDF 文档
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @return 成功返回 0
 */
MUPDF_API int mupdf_close_document(MupdfContext* ctx, MupdfDocument* doc);

// ============================================
// 页面信息
// ============================================

/**
 * @brief 获取页面数量
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param count_out 输出：页面数量
 * @return 成功返回 0
 */
MUPDF_API int mupdf_get_page_count(MupdfContext* ctx, MupdfDocument* doc, int* count_out);

/**
 * @brief 获取页面尺寸
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param page_num 页码（0-based）
 * @param width_out 输出：页面宽度（pt）
 * @param height_out 输出：页面高度（pt）
 * @return 成功返回 0，失败返回错误码
 */
MUPDF_API int mupdf_get_page_size(MupdfContext* ctx, MupdfDocument* doc, int page_num, float* width_out, float* height_out);

/**
 * @brief 获取页面是否横向
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param page_num 页码（0-based）
 * @return 横向返回 1，纵向返回 0
 */
MUPDF_API int mupdf_is_page_landscape(MupdfContext* ctx, MupdfDocument* doc, int page_num);

/**
 * @brief  通过分析页面文字方向判定横纵版（不依赖 /Rotate 或 MediaBox 宽高比）
 *
 * 使用 MuPDF structured text (stext) API 提取页面所有文字行，根据每行的 wmode
 * （0=水平, 1=垂直）和 dir（基线方向向量）判断文字的"自然阅读方向"，
 * 再综合页面尺寸给出最终判定。
 *
 * @param ctx      上下文句柄
 * @param doc      文档句柄
 * @param page_num 页码（0-based）
 * @param out_wmode_stats 输出：wmode 统计 [水平行数, 垂直行数]
 *                        （可为 NULL）
 * @param out_dir_angle  输出：主要文字行的基线角度（度），0=水平左→右，
 *                        90=文字旋转了90度（此时 dir.y ≈ ±1）
 *                        （可为 NULL）
 * @return 1=建议横向, 0=建议纵向, -1=无法判定
 */
MUPDF_API int mupdf_detect_orientation_by_text(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    int* out_wmode_stats,   /* 输出: [horiz_count, vert_count] */
    float* out_dir_angle    /* 输出: 主要文字行方向角（度） */
);

// ============================================
// 位置计算
// ============================================

/**
 * @brief 计算指定页面的内容位置
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param page_num 页码（0-based）
 * @param rule 位置规则
 * @param content_width 内容宽度（pt）
 * @param content_height 内容高度（pt）
 * @param out_x 输出：X坐标
 * @param out_y 输出：Y坐标
 * @return 成功返回 0
 */
MUPDF_API int mupdf_calc_position(
    MupdfContext* ctx, 
    MupdfDocument* doc, 
    int page_num,
    const LayoutRule* rule,
    float content_width,
    float content_height,
    float* out_x,
    float* out_y
);

// ============================================
// 添加图片
// ============================================

/**
 * @brief 添加图片到指定页面
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param page_num 页码（0-based）
 * @param image_path 图片路径（UTF-8）
 * @param x X坐标（pt）
 * @param y Y坐标（pt）
 * @param width 宽度（pt，0=自动）
 * @param height 高度（pt，0=自动）
 * @return 成功返回 0，失败返回错误码
 */
MUPDF_API int mupdf_add_image(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    const char* image_path,
    float x, float y,
    float width, float height
);

/**
 * @brief 从内存添加图片
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param page_num 页码（0-based）
 * @param image_data 图片数据
 * @param image_size 数据大小
 * @param image_format 图片格式（"jpeg", "png", "bmp"等）
 * @param x X坐标（pt）
 * @param y Y坐标（pt）
 * @param width 宽度（pt，0=自动）
 * @param height 高度（pt，0=自动）
 * @return 成功返回 0
 */
MUPDF_API int mupdf_add_image_from_mem(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    const unsigned char* image_data,
    size_t image_size,
    const char* image_format,
    float x, float y,
    float width, float height
);

/**
 * @brief 批量添加图片到多个页面
 * @details LayoutRule位置按未旋转页面计算，图片随后完整继承页面/Rotate。
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param page_nums 页码数组（NULL=所有页面）
 * @param page_count 页码数量（-1=所有页面）
 * @param image_path 图片路径
 * @param rule 位置规则（NULL=使用默认规则，左上角）
 *        - reverse: 0=正常, 1=X反转(左↔右), 2=Y反转(上↔下), 3=全部反转
 * @param width 固定宽度（0=使用规则）
 * @param height 固定高度（0=使用规则）
 * @return 成功返回添加的页面数，失败返回负数
 */
MUPDF_API int mupdf_batch_add_image(
    MupdfContext* ctx,
    MupdfDocument* doc,
    const int* page_nums,
    int page_count,
    const char* image_path,
    LayoutRule* rule,
    float width,
    float height
);

// ============================================
// 添加文字
// ============================================

/**
 * @brief 添加文字到指定页面
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param page_num 页码（0-based）
 * @param text 文字内容
 * @param font_size 字体大小（pt）
 * @param x X坐标（pt）
 * @param y Y坐标（pt）
 * @param width 文本框宽度（pt，0=自动）
 * @param height 文本框高度（pt，0=自动）
 * @return 成功返回 0
 */
MUPDF_API int mupdf_add_text(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    const char* text,
    float font_size,
    float x, float y,
    float width, float height
);

/**
 * @brief 设置文字样式
 */
typedef struct {
    float font_size;       /**< 字体大小 */
    float color_r;         /**< 红色 (0-1) */
    float color_g;         /**< 绿色 (0-1) */
    float color_b;         /**< 蓝色 (0-1) */
    float color_a;         /**< 透明度 (0-1) */
    int bold;              /**< 是否粗体 */
    int italic;            /**< 是否斜体 */
    int align;             /**< 对齐: 0=左, 1=中, 2=右 */
    const char* font_name; /**< 字体名称（NULL=使用默认） */
} TextStyle;

/**
 * @brief 添加带样式的文字
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param page_num 页码
 * @param text 文字内容
 * @param style 文字样式（NULL=默认样式）
 * @param x X坐标
 * @param y Y坐标
 * @param width 宽度
 * @param height 高度
 * @return 成功返回 0
 */
MUPDF_API int mupdf_add_text_with_style(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    const char* text,
    const TextStyle* style,
    float x, float y,
    float width, float height
);


/**
 * @brief 批量添加文字
 * @details LayoutRule位置按未旋转页面计算，文字随后完整继承页面/Rotate。
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param page_nums 页码数组
 * @param page_count 页码数量（-1=所有页面）
 * @param text 文字内容
 * @param font_size 字体大小
 * @param rule 位置规则
 * @return 成功返回添加的页面数
 */
MUPDF_API int mupdf_batch_add_text(
    MupdfContext* ctx,
    MupdfDocument* doc,
    const int* page_nums,
    int page_count,
    char* text,
    float font_size,
    LayoutRule* rule
);

/**
 * @brief 批量添加条码图像及其下方文字
 * @details 图像和文字作为一个整体，按未旋转页面的LayoutRule位置计算，
 *          随后完整继承页面/Rotate。文字在未旋转坐标中位于图像下方。
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param page_nums 页码数组（NULL=所有页面）
 * @param page_count 页码数量（-1=所有页面）
 * @param image_path 条码图片路径
 * @param text 条码文字（本机代码页，与 mupdf_batch_add_text 保持兼容）
 * @param font_size 字体大小（pt）
 * @param rule 位置规则
 * @param image_width 图像宽度（pt，0=按图片DPI计算）
 * @param image_height 图像高度（pt，0=按图片DPI计算）
 * @return 成功返回添加的页面数，失败返回负数
 */
MUPDF_API int mupdf_batch_add_barcode_label(
    MupdfContext* ctx,
    MupdfDocument* doc,
    const int* page_nums,
    int page_count,
    const char* image_path,
    const char* text,
    float font_size,
    LayoutRule* rule,
    float image_width,
    float image_height
);

/**
 * @brief 直接绘制单行文字（不换行）
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param page_num 页码（0-based）
 * @param text 文字内容
 * @param font_size 字体大小（pt）
 * @param x X坐标（pt）
 * @param y Y坐标（pt，PDF坐标系）
 * @param color_r 红色 (0-1)
 * @param color_g 绿色 (0-1)
 * @param color_b 蓝色 (0-1)
 * @return 成功返回 0
 */
MUPDF_API int mupdf_add_text_direct(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    const char* text,
    float font_size,
    float x, float y,
    float color_r, float color_g, float color_b
);

/**
 * @brief 批量添加单行文字（不换行）
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param page_nums 页码数组
 * @param page_count 页码数量（-1=所有页面）
 * @param text 文字内容
 * @param font_size 字体大小
 * @param rule 位置规则
 * @param color_r 红色 (0-1)
 * @param color_g 绿色 (0-1)
 * @param color_b 蓝色 (0-1)
 * @return 成功返回添加的页面数
 */
MUPDF_API int mupdf_batch_add_text_direct(
    MupdfContext* ctx,
    MupdfDocument* doc,
    const int* page_nums,
    int page_count,
    const char* text,
    float font_size,
    LayoutRule* rule,
    float color_r, float color_g, float color_b
);

// ============================================
// 渲染
// ============================================

/**
 * @brief 渲染页面到图像
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param page_num 页码
 * @param dpi DPI（通常 72 即可满足屏幕显示，300+ 用于打印）
 * @param alpha 是否有透明通道
 * @param width_out 输出：图像宽度
 * @param height_out 输出：图像高度
 * @param data_out 输出：图像数据（RGBA格式，需释放）
 * @param data_size_out 输出：数据大小
 * @return 成功返回 0
 */
MUPDF_API int mupdf_render_page(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    int dpi,
    int alpha,
    int* width_out,
    int* height_out,
    unsigned char** data_out,
    size_t* data_size_out
);

/**
 * @brief 释放渲染数据
 * @param ctx 上下文句柄
 * @param data 渲染数据
 */
MUPDF_API void mupdf_free_render_data(MupdfContext* ctx, void* data);

// ============================================
// 打印
// ============================================

/**
 * @brief 打印文档
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param printer_name 打印机名称（NULL=默认）
 * @param options 打印选项（NULL=默认）
 * @return 成功返回 0
 */
MUPDF_API int mupdf_print(
    MupdfContext* ctx,
    MupdfDocument* doc,
    const char* printer_name,
    const PrintOptions* options
);

/**
 * @brief 打印指定页面
 * @param ctx 上下文句柄
 * @param doc 文档句柄
 * @param page_nums 页码数组
 * @param page_count 页码数量
 * @param printer_name 打印机名称
 * @param options 打印选项
 * @return 成功返回 0
 */
MUPDF_API int mupdf_print_pages(
    MupdfContext* ctx,
    MupdfDocument* doc,
    const int* page_nums,
    int page_count,
    const char* printer_name,
    const PrintOptions* options
);

// ============================================
// 工具函数
// ============================================

/**
 * @brief 获取支持的图像格式列表
 * @return 逗号分隔的格式列表
 */
MUPDF_API const char* mupdf_get_supported_image_formats(void);

/**
 * @brief 检查是否为 PDF 文件
 * @param path 文件路径
 * @return 是 PDF 返回 1
 */
MUPDF_API int mupdf_is_pdf_file(const char* path);

/**
 * @brief 获取版本信息
 * @return 版本字符串
 */
MUPDF_API const char* mupdf_get_version(void);

#ifdef __cplusplus
}
#endif

#endif // MUPDF_WRAPPER_H
