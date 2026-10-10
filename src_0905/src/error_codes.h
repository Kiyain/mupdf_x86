/**
 * @file error_codes.h
 * @brief 错误码定义
 */
#ifndef MUPDF_WRAPPER_ERROR_CODES_H
#define MUPDF_WRAPPER_ERROR_CODES_H

/**
 * @brief 错误码定义
 */
typedef enum {
    MUPDF_OK = 0,                 /**< 成功 */
    
    // 通用错误 (1-99)
    MUPDF_ERR_INVALID_PARAM = 1,  /**< 无效参数 */
    MUPDF_ERR_OUT_OF_MEMORY = 2,   /**< 内存不足 */
    MUPDF_ERR_FILE_NOT_FOUND = 3,  /**< 文件不存在 */
    MUPDF_ERR_FILE_READ = 4,      /**< 文件读取失败 */
    MUPDF_ERR_FILE_WRITE = 5,     /**< 文件写入失败 */
    MUPDF_ERR_NOT_INITIALIZED = 6,/**< 未初始化 */
    MUPDF_ERR_ALREADY_INIT = 7,   /**< 已初始化 */
    MUPDF_ERR_UNSUPPORTED = 8,   /**< 不支持的操作 */
    
    // 文档错误 (100-199)
    MUPDF_ERR_DOC_NOT_OPEN = 100,  /**< 文档未打开 */
    MUPDF_ERR_DOC_CORRUPT = 101,  /**< 文档损坏 */
    MUPDF_ERR_DOC_ENCRYPTED = 102,/**< 文档加密 */
    MUPDF_ERR_PAGE_NOT_FOUND = 103,/**< 页面不存在 */
    MUPDF_ERR_PAGE_INVALID = 104, /**< 无效页面 */
    
    // 图像错误 (200-299)
    MUPDF_ERR_IMAGE_LOAD = 200,    /**< 图像加载失败 */
    MUPDF_ERR_IMAGE_FORMAT = 201, /**< 不支持的图像格式 */
    MUPDF_ERR_IMAGE_TOO_LARGE = 202,/**< 图像过大 */
    
    // 字体错误 (300-399)
    MUPDF_ERR_FONT_LOAD = 300,     /**< 字体加载失败 */
    MUPDF_ERR_FONT_NOT_FOUND = 301,/**< 字体不存在 */
    MUPDF_ERR_FONT_GLYPH = 302,   /**< 字形获取失败 */
    
    // 打印错误 (400-499)
    MUPDF_ERR_PRINTER_NOT_FOUND = 400,/**< 打印机未找到 */
    MUPDF_ERR_PRINTER_OFFLINE = 401,  /**< 打印机离线 */
    MUPDF_ERR_PRINT_FAILED = 402,      /**< 打印失败 */
    MUPDF_ERR_PRINT_CANCEL = 403,      /**< 打印取消 */
    
    // 位置计算错误 (500-599)
    MUPDF_ERR_POSITION_INVALID = 500, /**< 位置无效 */
    MUPDF_ERR_RECT_INVALID = 501,     /**< 矩形区域无效 */
    
    // 内部错误 (900-999)
    MUPDF_ERR_INTERNAL = 900,         /**< 内部错误 */
    MUPDF_ERR_EXCEPTION = 901,        /**< MuPDF 异常 */
} MupdfErrorCode;

/**
 * @brief 获取错误码对应的描述
 * @param code 错误码
 * @return 错误描述字符串
 */
const char* mupdf_error_get_description(MupdfErrorCode code);

#endif // MUPDF_WRAPPER_ERROR_CODES_H
