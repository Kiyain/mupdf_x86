/**
 * @file print_engine.h
 * @brief 打印引擎 - 支持物理打印机和单双面打印
 */
#ifndef MUPDF_WRAPPER_PRINT_ENGINE_H
#define MUPDF_WRAPPER_PRINT_ENGINE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 打印选项
 * @note duplex 参数说明：
 *   - 0: 单面打印
 *   - 1: 双面长边翻转（Long Edge Flip）- 适用于横向打印，打印后沿长边翻页
 *   - 2: 双面短边翻转（Short Edge Flip）- 适用于纵向打印，打印后沿短边翻页
 */
typedef struct PrintOptions_s {
    int copies;              /**< 打印份数 */
    int duplex;              /**< 双面打印: 1=单面, 2=长边翻转, 3=短边翻转 */
    int collate;             /**< 分拣: 0=不分拣, 1=分拣 */
    int color;               /**< 颜色: 1=黑白, 2=彩色 */
    float scale;             /**< 缩放比例 (0=自动适应, 1=原始大小) */
    int orientation;         /**< 方向: 0=自动, 1=纵向, 2=横向 */
    int from_page;           /**< 起始页 (1-based, 0=全部) */
    int to_page;             /**< 结束页 (1-based, 0=全部) */
    const char* job_name;    /**< 打印作业名称 */
    int paper_size;          /**< 纸张大小: 0=自动检测, 1=A4, 2=A3, 3=Letter, 4=Legal, 5=A5 */
} PrintOptions;

/**
 * @brief 打印机信息
 */
typedef struct {
    char name[256];          /**< 打印机名称 */
    char port[128];          /**< 端口 */
    char driver[128];         /**< 驱动名称 */
    int is_default;          /**< 是否默认打印机 */
    int is_ready;            /**< 是否就绪 */
} PrinterInfo;

/**
 * @brief 默认打印选项
 */
PrintOptions print_options_default(void);

/**
 * @brief 获取系统打印机列表
 * @param printers 输出：打印机信息数组
 * @param max_count 最大数量
 * @return 实际打印机数量
 */
int print_get_printers(PrinterInfo* printers, int max_count);

/**
 * @brief 获取默认打印机名称
 * @param name 输出缓冲区
 * @param name_size 缓冲区大小
 * @return 成功返回0
 */
int print_get_default_printer(char* name, int name_size);

/**
 * @brief 检查打印机是否就绪
 * @param printer_name 打印机名称
 * @return 就绪返回1，否则返回0
 */
int print_is_printer_ready(const char* printer_name);

/**
 * @brief 打印回调函数类型
 * @param page_num 当前页码
 * @param total_pages 总页数
 * @param user_data 用户数据
 * @return 返回0继续打印，返回非0取消打印
 */
typedef int (*PrintCallback)(int page_num, int total_pages, void* user_data);

/**
 * @brief 打印引擎上下文（Windows 特定）
 */
typedef struct PrintEngine PrintEngine;

/**
 * @brief 创建打印引擎
 * @param printer_name 打印机名称（NULL使用默认打印机）
 * @param options 打印选项
 * @return 引擎上下文，失败返回NULL
 */
PrintEngine* print_engine_create(const char* printer_name, const PrintOptions* options);

/**
 * @brief 设置打印回调
 * @param engine 引擎上下文
 * @param callback 回调函数
 * @param user_data 用户数据
 */
void print_engine_set_callback(PrintEngine* engine, PrintCallback callback, void* user_data);

/**
 * @brief 开始打印任务
 * @param engine 引擎上下文
 * @param document_title 文档标题
 * @return 成功返回0，失败返回错误码
 */
int print_engine_start_job(PrintEngine* engine, const char* document_title);

/**
 * @brief 打印一页（渲染后的位图数据）
 * @param engine 引擎上下文
 * @param page_width 页面宽度（像素）
 * @param page_height 页面高度（像素）
 * @param bits_per_pixel 每像素位数（通常为24或32）
 * @param image_data 图像数据（RGB/BGR格式）
 * @param data_size 图像数据大小
 * @return 成功返回0，失败返回错误码
 */
int print_engine_print_page(
    PrintEngine* engine,
    int page_width,
    int page_height,
    int bits_per_pixel,
    const unsigned char* image_data,
    int data_size
);

/**
 * @brief 结束打印任务
 * @param engine 引擎上下文
 * @return 成功返回0，失败返回错误码
 */
int print_engine_end_job(PrintEngine* engine);

/**
 * @brief 取消打印任务
 * @param engine 引擎上下文
 */
void print_engine_cancel_job(PrintEngine* engine);

/**
 * @brief 销毁打印引擎
 * @param engine 引擎上下文
 */
void print_engine_destroy(PrintEngine* engine);

/**
 * @brief 重置打印机纸张大小（动态换纸，用于混合纸张PDF打印）
 * @param engine 引擎上下文
 * @param paper_size 纸张大小: 1=A4, 2=A3, 3=Letter, 4=Legal, 5=A5
 * @return 成功返回0，失败返回错误码
 * @note 此函数在两页之间调用（StartPage之前），使用ResetDC动态切换纸张
 */
int print_engine_reset_paper(PrintEngine* engine, int paper_size);

/**
 * @brief 获取最后错误的描述
 * @param engine 引擎上下文
 * @return 错误描述字符串
 */
const char* print_engine_get_error(PrintEngine* engine);

/**
 * @brief 简单的直接打印接口（适合快速使用）
 * @param printer_name 打印机名称（NULL使用默认）
 * @param page_width 页面宽度（像素）
 * @param page_height 页面高度（像素）
 * @param image_data 图像数据
 * @param page_count 页数
 * @param copies 份数
 * @param duplex 双面打印
 * @return 成功返回0，失败返回错误码
 */
int print_simple(
    const char* printer_name,
    int page_width,
    int page_height,
    const unsigned char* image_data,
    int page_count,
    int copies,
    int duplex
);

#ifdef __cplusplus
}
#endif

#endif // MUPDF_WRAPPER_PRINT_ENGINE_H
