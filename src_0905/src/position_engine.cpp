/**
 * @file position_engine.cpp
 * @brief 位置计算引擎实现
 */

#include "position_engine.h"
#include <math.h>
#include <string.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <strings.h>
#endif

int position_calc(
    const LayoutRule* rule,
    float page_width,
    float page_height,
    float* out_x,
    float* out_y,
    float* out_width,
    float* out_height)
{
    if (!rule || !out_x || !out_y || !out_width || !out_height) {
        return 1; // MUPDF_ERR_INVALID_PARAM
    }
    
    if (page_width <= 0 || page_height <= 0) {
        return 501; // MUPDF_ERR_RECT_INVALID
    }
    
    float width, height;
    
    switch (rule->type) {
        case POS_LEFT_TOP:
        case POS_TOP_CENTER:
        case POS_RIGHT_TOP:
        case POS_LEFT_CENTER:
        case POS_CENTER:
        case POS_RIGHT_CENTER:
        case POS_LEFT_BOTTOM:
        case POS_BOTTOM_CENTER:
        case POS_RIGHT_BOTTOM:
            // 使用比例计算尺寸
            width = page_width * rule->scale_width;
            height = page_height * rule->scale_height;
            break;
            
        case POS_CUSTOM:
            width = rule->custom_width;
            height = rule->custom_height;
            break;
            
        default:
            return 500; // MUPDF_ERR_POSITION_INVALID
    }
    
    // 计算位置
    position_from_margin(
        rule->margin_x,
        rule->margin_y,
        page_width,
        page_height,
        width,
        height,
        rule->type,
        out_x,
        out_y
    );
    
    *out_width = width;
    *out_height = height;
    
    return 0;
}

void position_from_margin(
    float margin_x,
    float margin_y,
    float page_width,
    float page_height,
    float content_width,
    float content_height,
    PositionType type,
    float* out_x,
    float* out_y)
{
    if (!out_x || !out_y) return;
    
    // PDF 坐标系：原点左上，Y向下增加
    // 需要注意：旋转角度和页面方向会影响计算
    
    switch (type) {
        case POS_LEFT_TOP:
            *out_x = margin_x;
            *out_y = margin_y;
            break;
            
        case POS_TOP_CENTER:
            *out_x = (page_width - content_width) / 2.0f;
            *out_y = margin_y;
            break;
            
        case POS_RIGHT_TOP:
            *out_x = page_width - margin_x - content_width;
            *out_y = margin_y;
            break;
            
        case POS_LEFT_CENTER:
            *out_x = margin_x;
            *out_y = (page_height - content_height) / 2.0f;
            break;
            
        case POS_CENTER:
            *out_x = (page_width - content_width) / 2.0f;
            *out_y = (page_height - content_height) / 2.0f;
            break;
            
        case POS_RIGHT_CENTER:
            *out_x = page_width - margin_x - content_width;
            *out_y = (page_height - content_height) / 2.0f;
            break;
            
        case POS_LEFT_BOTTOM:
            *out_x = margin_x;
            *out_y = page_height - margin_y - content_height;
            break;
            
        case POS_BOTTOM_CENTER:
            *out_x = (page_width - content_width) / 2.0f;
            *out_y = page_height - margin_y - content_height;
            break;
            
        case POS_RIGHT_BOTTOM:
            *out_x = page_width - margin_x - content_width;
            *out_y = page_height - margin_y - content_height;
            break;
            
        case POS_CUSTOM:
            // 自定义位置由调用者直接指定
            *out_x = margin_x;
            *out_y = margin_y;
            break;
            
        default:
            *out_x = margin_x;
            *out_y = margin_y;
            break;
    }
}

int position_is_in_page(
    float x,
    float y,
    float width,
    float height,
    float page_width,
    float page_height)
{
    if (x < 0 || y < 0) return 0;
    if (x + width > page_width) return 0;
    if (y + height > page_height) return 0;
    return 1;
}

LayoutRule layout_default_left_top(void)
{
    LayoutRule rule;
    memset(&rule, 0, sizeof(rule));
    rule.type = POS_LEFT_TOP;
    rule.margin_x = 10.0f;
    rule.margin_y = 10.0f;
    rule.scale_width = 0.3f;
    rule.scale_height = 0.3f;
    rule.rotate = 0;
    return rule;
}

LayoutRule layout_default_right_top(void)
{
    LayoutRule rule;
    memset(&rule, 0, sizeof(rule));
    rule.type = POS_RIGHT_TOP;
    rule.margin_x = 10.0f;
    rule.margin_y = 10.0f;
    rule.scale_width = 0.3f;
    rule.scale_height = 0.3f;
    rule.rotate = 0;
    return rule;
}

LayoutRule layout_default_center(float scale)
{
    LayoutRule rule;
    memset(&rule, 0, sizeof(rule));
    rule.type = POS_CENTER;
    rule.margin_x = 0;
    rule.margin_y = 0;
    rule.scale_width = scale > 0 && scale <= 1.0f ? scale : 0.5f;
    rule.scale_height = scale > 0 && scale <= 1.0f ? scale : 0.5f;
    rule.rotate = 0;
    return rule;
}

LayoutRule layout_custom(float x, float y, float width, float height, int rotate)
{
    LayoutRule rule;
    memset(&rule, 0, sizeof(rule));
    rule.type = POS_CUSTOM;
    rule.custom_x = x;
    rule.custom_y = y;
    rule.custom_width = width;
    rule.custom_height = height;
    rule.rotate = rotate;
    return rule;
}
