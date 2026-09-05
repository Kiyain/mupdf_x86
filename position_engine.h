/**
 * @file position_engine.h
 * @brief 位置计算引擎 - 支持跨页面尺寸的统一位置添加
 */
#ifndef MUPDF_WRAPPER_POSITION_ENGINE_H
#define MUPDF_WRAPPER_POSITION_ENGINE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 位置类型枚举
 */
typedef enum {
    POS_LEFT_TOP = 0,       /**< 左上角 */
    POS_TOP_CENTER,         /**< 顶部居中 */
    POS_RIGHT_TOP,          /**< 右上角 */
    POS_LEFT_CENTER,        /**< 左侧居中 */
    POS_CENTER,             /**< 正中央 */
    POS_RIGHT_CENTER,       /**< 右侧居中 */
    POS_LEFT_BOTTOM,        /**< 左下角 */
    POS_BOTTOM_CENTER,      /**< 底部居中 */
    POS_RIGHT_BOTTOM,       /**< 右下角 */
    POS_CUSTOM              /**< 自定义位置（使用固定坐标） */
} PositionType;

/**
 * @brief 位置规则结构体
 * @details 用于描述内容在页面上的放置规则，支持相对页面尺寸的自动适配
 */
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

/**
 * @brief 计算指定页面的实际位置和尺寸
 * @param rule      位置规则
 * @param page_width   页面宽度（pt）
 * @param page_height  页面高度（pt）
 * @param out_x        输出：计算后的X坐标
 * @param out_y        输出：计算后的Y坐标
 * @param out_width    输出：计算后的宽度
 * @param out_height   输出：计算后的高度
 * @return 成功返回0，失败返回错误码
 */
int position_calc(
    const LayoutRule* rule,
    float page_width,
    float page_height,
    float* out_x,
    float* out_y,
    float* out_width,
    float* out_height
);

/**
 * @brief 根据边距计算位置
 * @param margin_x X方向边距
 * @param margin_y Y方向边距
 * @param page_width 页面宽度
 * @param page_height 页面高度
 * @param content_width 内容宽度
 * @param content_height 内容高度
 * @param type 位置类型
 * @param out_x 输出X坐标
 * @param out_y 输出Y坐标
 */
void position_from_margin(
    float margin_x,
    float margin_y,
    float page_width,
    float page_height,
    float content_width,
    float content_height,
    PositionType type,
    float* out_x,
    float* out_y
);

/**
 * @brief 检查矩形是否在页面范围内
 * @param x 矩形X坐标
 * @param y 矩形Y坐标
 * @param width 矩形宽度
 * @param height 矩形高度
 * @param page_width 页面宽度
 * @param page_height 页面高度
 * @return 如果在范围内返回1，否则返回0
 */
int position_is_in_page(
    float x,
    float y,
    float width,
    float height,
    float page_width,
    float page_height
);

/**
 * @brief 创建默认的布局规则（左上角）
 * @return 布局规则结构体
 */
LayoutRule layout_default_left_top(void);

/**
 * @brief 创建默认的布局规则（右上角）
 * @return 布局规则结构体
 */
LayoutRule layout_default_right_top(void);

/**
 * @brief 创建默认的布局规则（居中）
 * @param scale 相对于页面的大小比例
 * @return 布局规则结构体
 */
LayoutRule layout_default_center(float scale);

/**
 * @brief 创建自定义位置的布局规则
 * @param x X坐标
 * @param y Y坐标
 * @param width 宽度
 * @param height 高度
 * @param rotate 旋转角度
 * @return 布局规则结构体
 */
LayoutRule layout_custom(float x, float y, float width, float height, int rotate);

#ifdef __cplusplus
}
#endif

#endif // MUPDF_WRAPPER_POSITION_ENGINE_H
