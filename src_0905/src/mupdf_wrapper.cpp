/**
 * @file mupdf_wrapper.cpp
 * @brief MuPDF 封装库实现
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "mupdf_wrapper.h"
#include "print_engine.h"
#include "mupdf/fitz.h"
#include "mupdf/fitz/structured-text.h"
#include "mupdf/pdf.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <cstdarg>
#include <float.h>

// ============================================
// 内部结构体定义
// ============================================

struct _MupdfContext {
    fz_context* ctx;
    char error_msg[512];
    fz_font* cached_simsun_font;
};

struct _MupdfDocument {
    fz_context* ctx;
    fz_document* fz_doc;
    pdf_document* pdf_doc; // 可能为 NULL（非 PDF 文档）
    pdf_obj* cached_simsun_ref; // 缓存的 SimSun PDF 字体对象
    int edit_operation_depth;
};

// LayoutRule_s 已在 mupdf_wrapper.h 中定义
// PrintOptions_s 在 print_engine.h 中定义

// ============================================
// 日志文件功能
// ============================================

static const char* const MUPDF_LOG_PATH = "mupdf_wrapper.log";
static const __int64 MUPDF_LOG_MAX_BYTES = 10LL * 1024LL * 1024LL;
static const int MUPDF_LOG_FILE_COUNT = 5;

static FILE* g_log_file = NULL;
static SRWLOCK g_log_lock = SRWLOCK_INIT;

static void log_escape_value(const char* input, char* output, size_t output_size) {
    if (!output || output_size == 0) return;
    if (!input) input = "(null)";

    size_t out = 0;
    for (const unsigned char* p = (const unsigned char*)input; *p && out + 1 < output_size; ++p) {
        const char* escaped = NULL;
        if (*p == '\\') escaped = "\\\\";
        else if (*p == '"') escaped = "\\\"";
        else if (*p == '\r') escaped = "\\r";
        else if (*p == '\n') escaped = "\\n";
        else if (*p == '\t') escaped = "\\t";
        else if (*p == '|') escaped = "\\|";

        if (escaped) {
            for (const char* q = escaped; *q && out + 1 < output_size; ++q)
                output[out++] = *q;
        } else {
            output[out++] = (char)*p;
        }
    }
    output[out] = '\0';
}

static void log_hex_bytes(const unsigned char* data, size_t size, char* output, size_t output_size) {
    static const char hex[] = "0123456789ABCDEF";
    if (!output || output_size == 0) return;
    output[0] = '\0';
    if (!data) return;

    size_t limit = size < 128 ? size : 128;
    size_t out = 0;
    for (size_t i = 0; i < limit && out + 2 < output_size; ++i) {
        output[out++] = hex[(data[i] >> 4) & 0x0F];
        output[out++] = hex[data[i] & 0x0F];
    }
    if (size > limit && out + 3 < output_size) {
        output[out++] = '.';
        output[out++] = '.';
        output[out++] = '.';
    }
    output[out] = '\0';
}

static void log_acp_as_utf8(const char* input, char* output, size_t output_size) {
    if (!output || output_size == 0) return;
    output[0] = '\0';
    if (!input) {
        snprintf(output, output_size, "(null)");
        return;
    }

    WCHAR wide[2048];
    char utf8[4096];
    int wide_len = MultiByteToWideChar(CP_ACP, 0, input, -1, wide, (int)(sizeof(wide) / sizeof(wide[0])));
    if (wide_len <= 0) {
        snprintf(output, output_size, "<ACP conversion failed: %lu>", GetLastError());
        return;
    }
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, (int)sizeof(utf8), NULL, NULL);
    if (utf8_len <= 0) {
        snprintf(output, output_size, "<UTF-8 conversion failed: %lu>", GetLastError());
        return;
    }
    log_escape_value(utf8, output, output_size);
}

static void log_open_locked(void) {
    if (g_log_file) return;
    g_log_file = fopen(MUPDF_LOG_PATH, "ab");
    if (!g_log_file) return;

    _fseeki64(g_log_file, 0, SEEK_END);
    if (_ftelli64(g_log_file) == 0) {
        const char* header = "# MUPDF_DIAGNOSTIC_LOG version=2 encoding=UTF-8 max_size=10MB files=5\r\n";
        fwrite(header, 1, strlen(header), g_log_file);
        fflush(g_log_file);
    }
}

static void log_rotate_locked(size_t incoming_size) {
    log_open_locked();
    if (!g_log_file) return;

    _fseeki64(g_log_file, 0, SEEK_END);
    __int64 current_size = _ftelli64(g_log_file);
    if (current_size < 0 || current_size + (__int64)incoming_size <= MUPDF_LOG_MAX_BYTES)
        return;

    fclose(g_log_file);
    g_log_file = NULL;

    char old_path[MAX_PATH];
    char new_path[MAX_PATH];
    snprintf(old_path, sizeof(old_path), "%s.%d", MUPDF_LOG_PATH, MUPDF_LOG_FILE_COUNT - 1);
    DeleteFileA(old_path);

    for (int i = MUPDF_LOG_FILE_COUNT - 2; i >= 1; --i) {
        snprintf(old_path, sizeof(old_path), "%s.%d", MUPDF_LOG_PATH, i);
        snprintf(new_path, sizeof(new_path), "%s.%d", MUPDF_LOG_PATH, i + 1);
        MoveFileExA(old_path, new_path, MOVEFILE_REPLACE_EXISTING);
    }

    snprintf(new_path, sizeof(new_path), "%s.1", MUPDF_LOG_PATH);
    if (!MoveFileExA(MUPDF_LOG_PATH, new_path, MOVEFILE_REPLACE_EXISTING)) {
        // 外部程序锁住历史文件时，优先保证当前日志不无限增长。
        g_log_file = fopen(MUPDF_LOG_PATH, "wb");
        if (g_log_file) {
            const char* header = "# MUPDF_DIAGNOSTIC_LOG version=2 encoding=UTF-8 max_size=10MB files=5 rotation=fallback-truncate\r\n";
            fwrite(header, 1, strlen(header), g_log_file);
            fflush(g_log_file);
        }
        OutputDebugStringA("MuPDF diagnostic log rotation fallback: active log was truncated.\r\n");
        return;
    }
    log_open_locked();
}

void mupdf_diag_log(
    const char* level,
    const char* component,
    const char* event,
    const char* format,
    ...
) {
    char raw_detail[4096];
    raw_detail[0] = '\0';
    if (format && format[0]) {
        va_list args;
        va_start(args, format);
        vsnprintf(raw_detail, sizeof(raw_detail), format, args);
        va_end(args);
        raw_detail[sizeof(raw_detail) - 1] = '\0';
    }

    char detail[8192];
    size_t detail_out = 0;
    for (const unsigned char* p = (const unsigned char*)raw_detail; *p && detail_out + 1 < sizeof(detail); ++p) {
        const char* escaped = NULL;
        if (*p == '\r') escaped = "\\r";
        else if (*p == '\n') escaped = "\\n";
        else if (*p == '\t') escaped = "\\t";

        if (escaped) {
            for (const char* q = escaped; *q && detail_out + 1 < sizeof(detail); ++q)
                detail[detail_out++] = *q;
        } else {
            detail[detail_out++] = (char)*p;
        }
    }
    detail[detail_out] = '\0';

    AcquireSRWLockExclusive(&g_log_lock);

    SYSTEMTIME st;
    GetLocalTime(&st);

    char line[9216];
    int line_len = snprintf(
        line,
        sizeof(line),
        "%04u-%02u-%02u %02u:%02u:%02u.%03u | level=%s | component=%s | event=%s%s%s\r\n",
        (unsigned int)st.wYear, (unsigned int)st.wMonth, (unsigned int)st.wDay,
        (unsigned int)st.wHour, (unsigned int)st.wMinute, (unsigned int)st.wSecond, (unsigned int)st.wMilliseconds,
        level ? level : "INFO",
        component ? component : "CORE",
        event ? event : "MESSAGE",
        detail[0] ? " | " : "",
        detail);

    if (line_len < 0) {
        ReleaseSRWLockExclusive(&g_log_lock);
        return;
    }
    if (line_len >= (int)sizeof(line)) {
        line_len = (int)sizeof(line) - 3;
        line[line_len++] = '\r';
        line[line_len++] = '\n';
        line[line_len] = '\0';
    }

    log_rotate_locked((size_t)line_len);
    if (g_log_file) {
        fwrite(line, 1, (size_t)line_len, g_log_file);
        fflush(g_log_file);
    }
    OutputDebugStringA(line);
    ReleaseSRWLockExclusive(&g_log_lock);
}

#define LOG_EVENT(level, component, event, fmt, ...) \
    mupdf_diag_log(level, component, event, fmt, ##__VA_ARGS__)

static void log_set_error_event(const char* function_name, const char* message) {
    char message_log[2048];
    log_escape_value(message, message_log, sizeof(message_log));
    LOG_EVENT("ERROR", "CORE", "SET_ERROR", "function=%s | message=\"%s\"", function_name ? function_name : "(unknown)", message_log);
}

// ============================================
// 内部工具宏
// ============================================

#define SET_ERROR(ctx, msg) \
    do { \
        if (ctx) { \
            const char* _set_error_msg = (msg); \
            strncpy((ctx)->error_msg, _set_error_msg, sizeof((ctx)->error_msg)-1); \
            (ctx)->error_msg[sizeof((ctx)->error_msg)-1] = '\0'; \
            log_set_error_event(__FUNCTION__, _set_error_msg); \
        } \
    } while(0)

static void begin_edit_operation(fz_context* fz, MupdfDocument* doc, const char* title) {
    pdf_enable_journal(fz, doc->pdf_doc);
    pdf_begin_operation(fz, doc->pdf_doc, title);
    ++doc->edit_operation_depth;
}

static void clear_edit_history(fz_context* fz, MupdfDocument* doc) {
    if (doc->edit_operation_depth == 0 && doc->pdf_doc->journal) {
        pdf_discard_journal(fz, doc->pdf_doc->journal);
    }
}

static void end_edit_operation(fz_context* fz, MupdfDocument* doc) {
    pdf_end_operation(fz, doc->pdf_doc);
    if (doc->edit_operation_depth > 0) --doc->edit_operation_depth;
    clear_edit_history(fz, doc);
}

static void abandon_edit_operation(fz_context* fz, MupdfDocument* doc) {
    pdf_abandon_operation(fz, doc->pdf_doc);
    if (doc->edit_operation_depth > 0) --doc->edit_operation_depth;
    clear_edit_history(fz, doc);
}

static const char* log_position_name(int type) {
    switch (type) {
        case 0: return "RIGHT_TOP";
        case 1: return "LEFT_TOP";
        case 2: return "RIGHT_BOTTOM";
        case 3: return "LEFT_BOTTOM";
        case 4: return "TOP_CENTER";
        case 5: return "BOTTOM_CENTER";
        default: return "INVALID_AS_LEFT_TOP";
    }
}

static const char* log_paper_name(int paper_size) {
    switch (paper_size) {
        case 1: return "A4";
        case 2: return "A3";
        case 3: return "LETTER";
        case 4: return "LEGAL";
        case 5: return "A5";
        case 6: return "A2";
        case 7: return "A1";
        case 8: return "A0";
        case 9: return "A6";
        case 10: return "A7";
        case 11: return "A8";
        case 12: return "B0";
        case 13: return "B1";
        case 14: return "B2";
        case 15: return "B3";
        case 16: return "B4";
        case 17: return "B5";
        case 18: return "B6";
        case 19: return "B7";
        default: return "UNRECOGNIZED";
    }
}

typedef struct PageNativeGeometry_s {
    int rotate;
    fz_rect visual_bounds;
    float native_width;
    float native_height;
    fz_matrix native_to_visual;
} PageNativeGeometry;

static void load_native_page_geometry(
    fz_context* fz,
    pdf_page* page,
    PageNativeGeometry* geometry
) {
    if (!page || !geometry) {
        fz_throw(fz, FZ_ERROR_ARGUMENT, "Invalid native page geometry arguments");
    }

    int rotate = pdf_dict_get_inheritable_int(fz, page->obj, PDF_NAME(Rotate));
    rotate %= 360;
    if (rotate < 0) rotate += 360;
    if (rotate != 0 && rotate != 90 && rotate != 180 && rotate != 270) {
        fz_throw(fz, FZ_ERROR_ARGUMENT, "Page Rotate must be 0, 90, 180, or 270");
    }

    fz_rect visual = fz_bound_page(fz, (fz_page*)page);
    float visual_width = visual.x1 - visual.x0;
    float visual_height = visual.y1 - visual.y0;
    if (!_finite(visual_width) || !_finite(visual_height) ||
        visual_width <= 0.0f || visual_height <= 0.0f) {
        fz_throw(fz, FZ_ERROR_ARGUMENT, "Invalid page bounds for native placement");
    }

    geometry->rotate = rotate;
    geometry->visual_bounds = visual;
    geometry->native_width = (rotate == 90 || rotate == 270) ? visual_height : visual_width;
    geometry->native_height = (rotate == 90 || rotate == 270) ? visual_width : visual_height;

    if (rotate == 0) {
        geometry->native_to_visual = fz_make_matrix(
            1, 0, 0, 1, visual.x0, visual.y0);
    } else if (rotate == 90) {
        geometry->native_to_visual = fz_make_matrix(
            0, 1, -1, 0, visual.x0 + geometry->native_height, visual.y0);
    } else if (rotate == 180) {
        geometry->native_to_visual = fz_make_matrix(
            -1, 0, 0, -1,
            visual.x0 + geometry->native_width,
            visual.y0 + geometry->native_height);
    } else {
        geometry->native_to_visual = fz_make_matrix(
            0, -1, 1, 0, visual.x0, visual.y0 + geometry->native_width);
    }
}

static fz_rect native_rect_to_visual(
    const PageNativeGeometry* geometry,
    fz_rect native_rect
) {
    return fz_transform_rect(native_rect, geometry->native_to_visual);
}

static void log_page_geometry(
    fz_context* fz,
    pdf_page* page,
    MupdfDocument* doc,
    int page_num,
    const char* stage
) {
    fz_rect media = fz_empty_rect;
    fz_rect crop = fz_empty_rect;
    fz_rect visual = fz_empty_rect;
    fz_matrix pdf_to_fitz = fz_identity;
    fz_matrix fitz_to_pdf = fz_identity;
    float user_unit = 1.0f;
    int rotate = 0;
    int normalized_rotate = 0;
    int inverse_singular = 0;

    fz_try(fz) {
        pdf_obj* media_obj = pdf_dict_get_inheritable(fz, page->obj, PDF_NAME(MediaBox));
        pdf_obj* crop_obj = pdf_dict_get_inheritable(fz, page->obj, PDF_NAME(CropBox));
        media = pdf_to_rect(fz, media_obj);
        crop = crop_obj ? pdf_to_rect(fz, crop_obj) : media;
        user_unit = pdf_dict_get_real_default(fz, page->obj, PDF_NAME(UserUnit), 1.0f);
        rotate = pdf_dict_get_inheritable_int(fz, page->obj, PDF_NAME(Rotate));
        normalized_rotate = rotate % 360;
        if (normalized_rotate < 0) normalized_rotate += 360;
        normalized_rotate = (90 * ((normalized_rotate + 45) / 90)) % 360;
        pdf_page_transform(fz, page, nullptr, &pdf_to_fitz);
        inverse_singular = fz_try_invert_matrix(&fitz_to_pdf, pdf_to_fitz);
        visual = fz_bound_page(fz, (fz_page*)page);
    } fz_catch(fz) {
        LOG_EVENT(
            "WARN", "PAGE", "GEOMETRY_ERROR",
            "doc=%p | page=%d | stage=%s | message=\"%s\"",
            (void*)doc, page_num + 1, stage ? stage : "UNKNOWN", fz_caught_message(fz));
        return;
    }

    LOG_EVENT(
        inverse_singular ? "WARN" : "INFO", "PAGE", "GEOMETRY",
        "doc=%p | page=%d | stage=%s | rotate_raw=%d | rotate=%d | user_unit=%.6f | "
        "media=[%.3f,%.3f,%.3f,%.3f] | crop=[%.3f,%.3f,%.3f,%.3f] | "
        "visual=[%.3f,%.3f,%.3f,%.3f] | visual_size=%.3fx%.3f | orientation=%s | "
        "pdf_to_fitz=[%.6f,%.6f,%.6f,%.6f,%.6f,%.6f] | "
        "fitz_to_pdf=[%.6f,%.6f,%.6f,%.6f,%.6f,%.6f] | inverse_singular=%s",
        (void*)doc, page_num + 1, stage ? stage : "UNKNOWN", rotate, normalized_rotate, user_unit,
        media.x0, media.y0, media.x1, media.y1,
        crop.x0, crop.y0, crop.x1, crop.y1,
        visual.x0, visual.y0, visual.x1, visual.y1,
        visual.x1 - visual.x0, visual.y1 - visual.y0,
        (visual.x1 - visual.x0) > (visual.y1 - visual.y0) ? "LANDSCAPE" : "PORTRAIT",
        pdf_to_fitz.a, pdf_to_fitz.b, pdf_to_fitz.c, pdf_to_fitz.d, pdf_to_fitz.e, pdf_to_fitz.f,
        fitz_to_pdf.a, fitz_to_pdf.b, fitz_to_pdf.c, fitz_to_pdf.d, fitz_to_pdf.e, fitz_to_pdf.f,
        inverse_singular ? "true" : "false");
}

static fz_rect no_rotate_annot_design_rect(
    fz_context* fz,
    pdf_page* page,
    fz_rect desired_visual
) {
    fz_matrix pdf_to_fitz;
    fz_matrix fitz_to_pdf;
    pdf_page_transform(fz, page, nullptr, &pdf_to_fitz);
    if (fz_try_invert_matrix(&fitz_to_pdf, pdf_to_fitz)) {
        fz_throw(fz, FZ_ERROR_ARGUMENT, "Cannot invert PDF page transform for annotation");
    }

    fz_rect display_rect = fz_transform_rect(desired_visual, fitz_to_pdf);
    fz_rect raw_rect = display_rect;
    int rotate = pdf_dict_get_inheritable_int(fz, page->obj, PDF_NAME(Rotate));
    rotate %= 360;
    if (rotate < 0) rotate += 360;
    if (rotate != 0 && rotate != 90 && rotate != 180 && rotate != 270) {
        fz_throw(fz, FZ_ERROR_ARGUMENT, "Page Rotate must be 0, 90, 180, or 270");
    }

    float display_w = display_rect.x1 - display_rect.x0;
    float display_h = display_rect.y1 - display_rect.y0;
    if (rotate == 90) {
        raw_rect = fz_make_rect(
            display_rect.x0,
            display_rect.y0 - display_w,
            display_rect.x0 + display_h,
            display_rect.y0);
    } else if (rotate == 180) {
        raw_rect = fz_make_rect(
            display_rect.x1,
            display_rect.y0 - display_h,
            display_rect.x1 + display_w,
            display_rect.y0);
    } else if (rotate == 270) {
        raw_rect = fz_make_rect(
            display_rect.x1,
            display_rect.y1 - display_w,
            display_rect.x1 + display_h,
            display_rect.y1);
    }

    return fz_transform_rect(raw_rect, pdf_to_fitz);
}

static void log_stamp_annotation(
    fz_context* fz,
    pdf_annot* stamp,
    MupdfDocument* doc,
    int page_num,
    fz_rect expected_visual,
    fz_rect ap_bbox,
    fz_matrix ap_matrix,
    const char* resource_name
) {
    fz_rect raw_rect = fz_empty_rect;
    fz_rect visual_rect = fz_empty_rect;
    fz_matrix annot_matrix = fz_identity;
    int flags = 0;

    fz_try(fz) {
        pdf_obj* annot_obj = pdf_annot_obj(fz, stamp);
        raw_rect = pdf_dict_get_rect(fz, annot_obj, PDF_NAME(Rect));
        visual_rect = pdf_bound_annot(fz, stamp);
        annot_matrix = pdf_annot_transform(fz, stamp);
        flags = pdf_annot_flags(fz, stamp);
    } fz_catch(fz) {
        LOG_EVENT(
            "WARN", "IMAGE", "ANNOT_ERROR",
            "doc=%p | page=%d | message=\"%s\"",
            (void*)doc, page_num + 1, fz_caught_message(fz));
        return;
    }

    float expected_w = expected_visual.x1 - expected_visual.x0;
    float expected_h = expected_visual.y1 - expected_visual.y0;
    float raw_w = raw_rect.x1 - raw_rect.x0;
    float raw_h = raw_rect.y1 - raw_rect.y0;
    float ap_w = ap_bbox.x1 - ap_bbox.x0;
    float ap_h = ap_bbox.y1 - ap_bbox.y0;
    float scale_x = sqrtf(annot_matrix.a * annot_matrix.a + annot_matrix.b * annot_matrix.b);
    float scale_y = sqrtf(annot_matrix.c * annot_matrix.c + annot_matrix.d * annot_matrix.d);
    float scale_diff_pct = scale_y != 0 ? (scale_x / scale_y - 1.0f) * 100.0f : 0.0f;

    LOG_EVENT(
        "INFO", "IMAGE", "ANNOT",
        "doc=%p | page=%d | resource=%s | flags=%d | print=%s | no_rotate=%s | "
        "expected_visual=[%.3f,%.3f,%.3f,%.3f] | actual_visual=[%.3f,%.3f,%.3f,%.3f] | "
        "raw_rect=[%.3f,%.3f,%.3f,%.3f] | expected_size=%.3fx%.3f | raw_size=%.3fx%.3f | "
        "ap_bbox=[%.3f,%.3f,%.3f,%.3f] | ap_size=%.3fx%.3f | "
        "ap_matrix=[%.6f,%.6f,%.6f,%.6f,%.6f,%.6f] | "
        "annot_matrix=[%.6f,%.6f,%.6f,%.6f,%.6f,%.6f] | scale_x=%.6f | scale_y=%.6f | scale_diff_pct=%.3f",
        (void*)doc, page_num + 1, resource_name ? resource_name : "(null)", flags,
        (flags & PDF_ANNOT_IS_PRINT) ? "true" : "false",
        (flags & PDF_ANNOT_IS_NO_ROTATE) ? "true" : "false",
        expected_visual.x0, expected_visual.y0, expected_visual.x1, expected_visual.y1,
        visual_rect.x0, visual_rect.y0, visual_rect.x1, visual_rect.y1,
        raw_rect.x0, raw_rect.y0, raw_rect.x1, raw_rect.y1,
        expected_w, expected_h, raw_w, raw_h,
        ap_bbox.x0, ap_bbox.y0, ap_bbox.x1, ap_bbox.y1, ap_w, ap_h,
        ap_matrix.a, ap_matrix.b, ap_matrix.c, ap_matrix.d, ap_matrix.e, ap_matrix.f,
        annot_matrix.a, annot_matrix.b, annot_matrix.c, annot_matrix.d, annot_matrix.e, annot_matrix.f,
        scale_x, scale_y, scale_diff_pct);
}

// ============================================
// 初始化与销毁
// ============================================

int mupdf_init(MupdfContext** ctx_out) {
    LOG_EVENT("INFO", "CORE", "INIT_BEGIN", "cache_limit_mb=320 | acp=%u", GetACP());
    if (!ctx_out) {
        LOG_EVENT("ERROR", "CORE", "INIT_END", "status=FAIL | ret=-1 | reason=null_ctx_out");
        return -1;
    }

    // 使用有限存储上限（256MB），防止长时间运行（如打印300页PDF）时
    // MuPDF 内部缓存（字体、图片、色彩空间等）无限增长导致内存耗尽、
    // fz_new_pixmap_with_bbox 抛异常 → 渲染失败 → 页面丢失
    fz_context* mupdf_ctx = fz_new_context(nullptr, nullptr, 320 << 20);
    if (!mupdf_ctx) {
        LOG_EVENT("ERROR", "CORE", "INIT_END", "status=FAIL | ret=-1 | reason=fz_new_context_failed");
        return -1;
    }

    fz_register_document_handlers(mupdf_ctx);

    MupdfContext* wrapper = new MupdfContext;
    wrapper->ctx = mupdf_ctx;
    wrapper->error_msg[0] = '\0';
    wrapper->cached_simsun_font = nullptr;

    *ctx_out = wrapper;
    LOG_EVENT("INFO", "CORE", "INIT_END", "status=OK | ctx=%p", (void*)wrapper);
    return 0;
}

int mupdf_fini(MupdfContext* ctx) {
    if (!ctx) {
        LOG_EVENT("ERROR", "CORE", "FINI", "status=FAIL | ret=-1 | reason=null_ctx");
        return -1;
    }
    LOG_EVENT("INFO", "CORE", "FINI_BEGIN", "ctx=%p", (void*)ctx);
    if (ctx->ctx) {
        if (ctx->cached_simsun_font) {
            fz_drop_font(ctx->ctx, ctx->cached_simsun_font);
            ctx->cached_simsun_font = nullptr;
        }
        fz_drop_context(ctx->ctx);
        ctx->ctx = nullptr;
    }
    delete ctx;
    LOG_EVENT("INFO", "CORE", "FINI_END", "status=OK");
    return 0;
}

const char* mupdf_get_error(MupdfContext* ctx) {
    if (!ctx) return "Null context";
    return ctx->error_msg;
}

// ============================================
// 文档操作
// ============================================

int mupdf_open_document(MupdfContext* ctx, const char* path, MupdfDocument** doc_out) {
    if (!ctx || !path || !doc_out) return -1;

    char path_utf8[4096];
    log_acp_as_utf8(path, path_utf8, sizeof(path_utf8));
    LOG_EVENT("INFO", "DOC", "OPEN_BEGIN", "source=file | path=\"%s\"", path_utf8);

    fz_context* fz = ctx->ctx;
    fz_document* fz_doc = nullptr;

    fz_try(fz) {
        fz_doc = fz_open_document(fz, path);
    } fz_catch(fz) {
        const char* caught_message = fz_caught_message(fz);
        LOG_EVENT("ERROR", "DOC", "OPEN_END", "status=FAIL | source=file | path=\"%s\" | ret=-2 | message=\"%s\"", path_utf8, caught_message);
        SET_ERROR(ctx, caught_message);
        return -2;
    }

    if (!fz_doc) {
        LOG_EVENT("ERROR", "DOC", "OPEN_END", "status=FAIL | source=file | path=\"%s\" | ret=-2 | reason=null_document", path_utf8);
        SET_ERROR(ctx, "Failed to open document");
        return -2;
    }

    MupdfDocument* wrapper = new MupdfDocument;
    wrapper->ctx = fz;
    wrapper->fz_doc = fz_doc;
    wrapper->pdf_doc = pdf_document_from_fz_document(fz, fz_doc);
    wrapper->cached_simsun_ref = nullptr;
    wrapper->edit_operation_depth = 0;

    *doc_out = wrapper;
    LOG_EVENT("INFO", "DOC", "OPEN_END", "status=OK | source=file | path=\"%s\" | doc=%p | is_pdf=%s", path_utf8, (void*)wrapper, wrapper->pdf_doc ? "true" : "false");
    return 0;
}

int mupdf_open_document_from_mem(MupdfContext* ctx, const unsigned char* data, size_t size, MupdfDocument** doc_out) {
    if (!ctx || !data || size == 0 || !doc_out) return -1;

    char prefix_hex[260];
    log_hex_bytes(data, size, prefix_hex, sizeof(prefix_hex));
    LOG_EVENT("INFO", "DOC", "OPEN_BEGIN", "source=memory | size=%zu | prefix_hex=%s", size, prefix_hex);

    fz_context* fz = ctx->ctx;
    fz_document* fz_doc = nullptr;

    fz_try(fz) {
        fz_buffer* buf = fz_new_buffer_from_shared_data(fz, data, size);
        fz_stream* stream = fz_open_buffer(fz, buf);
        fz_drop_buffer(fz, buf);
        fz_doc = fz_open_document_with_stream(fz, "pdf", stream);
        fz_drop_stream(fz, stream);
    } fz_catch(fz) {
        const char* caught_message = fz_caught_message(fz);
        LOG_EVENT("ERROR", "DOC", "OPEN_END", "status=FAIL | source=memory | size=%zu | ret=-2 | message=\"%s\"", size, caught_message);
        SET_ERROR(ctx, caught_message);
        return -2;
    }

    if (!fz_doc) {
        LOG_EVENT("ERROR", "DOC", "OPEN_END", "status=FAIL | source=memory | size=%zu | ret=-2 | reason=null_document", size);
        SET_ERROR(ctx, "Failed to open document from memory");
        return -2;
    }

    MupdfDocument* wrapper = new MupdfDocument;
    wrapper->ctx = fz;
    wrapper->fz_doc = fz_doc;
    wrapper->pdf_doc = pdf_document_from_fz_document(fz, fz_doc);
    wrapper->cached_simsun_ref = nullptr;
    wrapper->edit_operation_depth = 0;

    *doc_out = wrapper;
    LOG_EVENT("INFO", "DOC", "OPEN_END", "status=OK | source=memory | size=%zu | doc=%p | is_pdf=%s", size, (void*)wrapper, wrapper->pdf_doc ? "true" : "false");
    return 0;
}

int mupdf_save_document(MupdfContext* ctx, MupdfDocument* doc, const char* path) {
    if (!ctx || !doc || !path) return -1;
    char path_utf8[4096];
    log_acp_as_utf8(path, path_utf8, sizeof(path_utf8));
    LOG_EVENT("INFO", "DOC", "SAVE_BEGIN", "doc=%p | target=file | path=\"%s\"", (void*)doc, path_utf8);
    if (!doc->pdf_doc) {
        LOG_EVENT("ERROR", "DOC", "SAVE_END", "status=FAIL | doc=%p | target=file | path=\"%s\" | ret=-2 | reason=not_pdf", (void*)doc, path_utf8);
        SET_ERROR(ctx, "Not a PDF document");
        return -2;
    }

    fz_context* fz = ctx->ctx;

    fz_try(fz) {
        // 设置压缩优化选项以减小文件大小
        pdf_write_options opts = pdf_default_write_options;
        opts.do_garbage = 1;           // 垃圾回收
        opts.do_compress = 1;          // 压缩内容流
        opts.do_compress_images = 1;    // 压缩图片
        opts.do_compress_fonts = 1;    // 压缩字体
        pdf_save_document(fz, doc->pdf_doc, path, &opts);
    } fz_catch(fz) {
        const char* caught_message = fz_caught_message(fz);
        LOG_EVENT("ERROR", "DOC", "SAVE_END", "status=FAIL | doc=%p | target=file | path=\"%s\" | ret=-3 | message=\"%s\"", (void*)doc, path_utf8, caught_message);
        SET_ERROR(ctx, caught_message);
        return -3;
    }

    LOG_EVENT("INFO", "DOC", "SAVE_END", "status=OK | doc=%p | target=file | path=\"%s\" | garbage=1 | compress=1 | compress_images=1 | compress_fonts=1", (void*)doc, path_utf8);
    return 0;
}

int mupdf_save_document_to_mem(MupdfContext* ctx, MupdfDocument* doc, unsigned char** data_out, size_t* size_out) {
    if (!ctx || !doc || !data_out || !size_out) return -1;
    LOG_EVENT("INFO", "DOC", "SAVE_BEGIN", "doc=%p | target=memory", (void*)doc);
    if (!doc->pdf_doc) { SET_ERROR(ctx, "Not a PDF document"); return -2; }

    // 保存到临时文件再读取（MuPDF 的 pdf_write_document 到输出流在部分版本不可用）
    char tmp_path[MAX_PATH] = {0};
    if (!GetTempPathA(MAX_PATH, tmp_path)) {
        SET_ERROR(ctx, "Cannot get temp path");
        return -3;
    }
    char tmp_file[MAX_PATH] = {0};
    if (!GetTempFileNameA(tmp_path, "pdf", 0, tmp_file)) {
        SET_ERROR(ctx, "Cannot create temp file");
        return -3;
    }

    int ret = mupdf_save_document(ctx, doc, tmp_file);
    if (ret != 0) {
        DeleteFileA(tmp_file);
        return ret;
    }

    // 读取临时文件
    FILE* f = fopen(tmp_file, "rb");
    if (!f) {
        DeleteFileA(tmp_file);
        SET_ERROR(ctx, "Cannot read temp file");
        return -4;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char* buf = (unsigned char*)malloc((size_t)len);
    if (!buf) {
        fclose(f);
        DeleteFileA(tmp_file);
        SET_ERROR(ctx, "Out of memory");
        return -5;
    }

    fread(buf, 1, (size_t)len, f);
    fclose(f);
    DeleteFileA(tmp_file);

    *data_out = buf;
    *size_out = (size_t)len;
    LOG_EVENT("INFO", "DOC", "SAVE_END", "status=OK | doc=%p | target=memory | size=%ld", (void*)doc, len);
    return 0;
}

void mupdf_free_buffer(MupdfContext* ctx, void* data) {
    if (data) free(data);
}

int mupdf_close_document(MupdfContext* ctx, MupdfDocument* doc) {
    if (!doc) {
        LOG_EVENT("ERROR", "DOC", "CLOSE", "status=FAIL | ret=-1 | reason=null_doc");
        return -1;
    }
    LOG_EVENT("INFO", "DOC", "CLOSE_BEGIN", "doc=%p", (void*)doc);
    if (doc->cached_simsun_ref) {
        pdf_drop_obj(doc->ctx, doc->cached_simsun_ref);
        doc->cached_simsun_ref = nullptr;
    }
    if (doc->fz_doc) {
        fz_drop_document(doc->ctx, doc->fz_doc);
        doc->fz_doc = nullptr;
        doc->pdf_doc = nullptr;
    }
    delete doc;
    LOG_EVENT("INFO", "DOC", "CLOSE_END", "status=OK");
    return 0;
}

// ============================================
// 页面信息
// ============================================

int mupdf_get_page_count(MupdfContext* ctx, MupdfDocument* doc, int* count_out) {
    if (!ctx || !doc || !count_out) return -1;

    fz_context* fz = ctx->ctx;
    int count = 0;

    fz_try(fz) {
        count = fz_count_pages(fz, doc->fz_doc);
    } fz_catch(fz) {
        SET_ERROR(ctx, fz_caught_message(fz));
        return -2;
    }

    *count_out = count;
    return 0;
}

int mupdf_get_page_size(MupdfContext* ctx, MupdfDocument* doc, int page_num, float* width_out, float* height_out) {
    if (!ctx || !doc) return -1;

    fz_context* fz = ctx->ctx;

    fz_try(fz) {
        fz_page* page = fz_load_page(fz, doc->fz_doc, page_num);
        fz_rect rect = fz_bound_page(fz, page);
        fz_drop_page(fz, page);

        if (width_out) *width_out = rect.x1 - rect.x0;
        if (height_out) *height_out = rect.y1 - rect.y0;
    } fz_catch(fz) {
        SET_ERROR(ctx, fz_caught_message(fz));
        return -2;
    }

    return 0;
}

int mupdf_is_page_landscape(MupdfContext* ctx, MupdfDocument* doc, int page_num) {
    float w = 0, h = 0;
    if (mupdf_get_page_size(ctx, doc, page_num, &w, &h) != 0) return 0;
    return (w > h) ? 1 : 0;
}

// ============================================
// 基于文字方向判定页面横纵版
// ============================================

#define ORIENT_BY_TEXT_THRESHOLD 0.7f   // 比例阈值：某类文字线超过70%才视为"主方向"
#define ORIENT_BY_TEXT_MIN_LINES  5      // 最少需要5行文字才能做出可靠判定

MUPDF_API int mupdf_detect_orientation_by_text(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    int* out_wmode_stats,
    float* out_dir_angle
) {
    if (!ctx || !doc) return -1;

    fz_context* fz = ctx->ctx;
    int result = -1;  /* 默认：无法判定 */
    int h_count = 0, v_count = 0;  /* wmode 统计 */
    float angle_sum_x = 0, angle_sum_y = 0; /* dir 向量分量累加 */

    fz_try(fz) {
        /* 1. 加载页面 */
        fz_page* page = fz_load_page(fz, doc->fz_doc, page_num);
        fz_rect page_rect = fz_bound_page(fz, page);
        float page_w = page_rect.x1 - page_rect.x0;
        float page_h = page_rect.y1 - page_rect.y0;

        LOG_EVENT("DEBUG", "PAGE", "ORIENTATION_TEXT_BEGIN", "doc=%p | page=%d | page_size=%.3fx%.3f | aspect_landscape=%s", (void*)doc, page_num + 1, page_w, page_h, page_w > page_h ? "true" : "false");

        /* 2. 创建 stext 设备并提取文字 */
        fz_stext_options stext_opts = { 0 };
        stext_opts.flags = FZ_STEXT_PRESERVE_WHITESPACE | FZ_STEXT_PRESERVE_SPANS;
        fz_stext_page* stext_page = fz_new_stext_page(fz, page_rect);
        fz_device* stext_dev = fz_new_stext_device(fz, stext_page, &stext_opts);

        fz_run_page(fz, page, stext_dev, fz_identity, NULL);
        fz_close_device(fz, stext_dev);
        fz_drop_device(fz, stext_dev);

        /* 3. 遍历所有文字块和行，统计 wmode 和 dir */
        int total_lines = 0;
        fz_stext_block* block = stext_page->first_block;
        while (block) {
            if (block->type == FZ_STEXT_BLOCK_TEXT) {
                fz_stext_line* line = block->u.t.first_line;
                while (line) {
                    total_lines++;

                    if (line->wmode == 0) {
                        /* 水平书写模式 */
                        h_count++;
                        angle_sum_x += (float)fabs((double)line->dir.x);
                        angle_sum_y += (float)fabs((double)line->dir.y);

                        LOG_EVENT("DEBUG", "PAGE", "ORIENTATION_TEXT_LINE", "doc=%p | page=%d | line=%d | wmode=HORIZONTAL | dir=%.6f,%.6f | bbox=[%.3f,%.3f,%.3f,%.3f]", (void*)doc, page_num + 1, total_lines, line->dir.x, line->dir.y, line->bbox.x0, line->bbox.y0, line->bbox.x1, line->bbox.y1);

                    } else {
                        /* 垂直书写模式（CJK竖排） */
                        v_count++;
                        angle_sum_y += 1.0f; /* 垂直文字 dir.y 主导 */

                        LOG_EVENT("DEBUG", "PAGE", "ORIENTATION_TEXT_LINE", "doc=%p | page=%d | line=%d | wmode=VERTICAL | dir=%.6f,%.6f | bbox=[%.3f,%.3f,%.3f,%.3f]", (void*)doc, page_num + 1, total_lines, line->dir.x, line->dir.y, line->bbox.x0, line->bbox.y0, line->bbox.x1, line->bbox.y1);
                    }

                    line = line->next;
                }
            }
            block = block->next;
        }

        /* 4. 输出统计量 */
        if (out_wmode_stats) {
            out_wmode_stats[0] = h_count;
            out_wmode_stats[1] = v_count;
        }
        if (out_dir_angle && h_count > 0) {
            /* 用 atan2 计算主要文字行的基线角度 */
            float avg_y = angle_sum_y / (float)h_count;
            float avg_x = angle_sum_x / (float)h_count;
            *out_dir_angle = (float)(atan2((double)avg_y, (double)avg_x) * 180.0 / 3.141592653589793);
        }

        LOG_EVENT("DEBUG", "PAGE", "ORIENTATION_TEXT_STATS", "doc=%p | page=%d | total_lines=%d | horizontal_lines=%d | vertical_lines=%d", (void*)doc, page_num + 1, total_lines, h_count, v_count);

        /* 5. 判定逻辑 */
        if (total_lines < ORIENT_BY_TEXT_MIN_LINES) {
            LOG_EVENT("DEBUG", "PAGE", "ORIENTATION_TEXT_DECISION", "doc=%p | page=%d | reason=INSUFFICIENT_LINES | minimum=%d | fallback=PAGE_ASPECT", (void*)doc, page_num + 1, ORIENT_BY_TEXT_MIN_LINES);
            result = (page_w > page_h) ? 1 : 0;
        } else {
            float h_ratio = (float)h_count / (float)total_lines;

            if (h_ratio >= ORIENT_BY_TEXT_THRESHOLD) {
                /* ================================================================
                 * 水平文字为主 (≥70%)
                 *
                 * 检查 dir 向量的主导分量：
                 *   - dir.x 主导 (|dir.x| > |dir.y|)：文字基线水平 → 正常横向阅读
                 *   - dir.y 主导 (|dir.y| > |dir.x|)：文字基线垂直 → 文字被旋转了90°
                 *
                 * 判定策略：
                 *   如果文字基线是水平的 (avg_dir_x > avg_dir_y)：
                 *     - page_w > page_h：文字和页面都横向 → landscape
                 *     - page_h > page_w：文字横向但页面纵向 → portrait（符合直觉）
                 *   如果文字基线是垂直的 (avg_dir_y > avg_dir_x)：
                 *     - page_w > page_h：文字旋转90°放在横向页面上 → 实际应纵向
                 *     - page_h > page_w：文字旋转90°放在纵向页面上 → 实际应横向
                 * ================================================================ */
                float avg_x = (h_count > 0) ? angle_sum_x / (float)h_count : 0;
                float avg_y = (h_count > 0) ? angle_sum_y / (float)h_count : 0;
                int text_is_horizontal_baseline = (avg_x >= avg_y) ? 1 : 0;

                LOG_EVENT("DEBUG", "PAGE", "ORIENTATION_TEXT_DIRECTION", "doc=%p | page=%d | avg_abs_dir=%.6f,%.6f | horizontal_baseline=%s", (void*)doc, page_num + 1, avg_x, avg_y, text_is_horizontal_baseline ? "true" : "false");

                if (text_is_horizontal_baseline) {
                    /* 文字基线水平：页面宽高比直接反映阅读方向 */
                    result = (page_w > page_h) ? 1 : 0;
                    LOG_EVENT("DEBUG", "PAGE", "ORIENTATION_TEXT_DECISION", "doc=%p | page=%d | reason=HORIZONTAL_BASELINE | result=%s", (void*)doc, page_num + 1, result ? "LANDSCAPE" : "PORTRAIT");
                } else {
                    /* 文字基线垂直（旋转了90°）：宽高比与阅读方向相反 */
                    result = (page_w > page_h) ? 0 : 1;
                    LOG_EVENT("DEBUG", "PAGE", "ORIENTATION_TEXT_DECISION", "doc=%p | page=%d | reason=VERTICAL_BASELINE | result=%s", (void*)doc, page_num + 1, result ? "LANDSCAPE" : "PORTRAIT");
                }

            } else if (v_count >= total_lines - h_count && v_count > 0) {
                /* ================================================================
                 * 垂直文字为主（CJK竖排）
                 * CJK竖排通常出现在纵向页面上
                 * ================================================================ */
                LOG_EVENT("DEBUG", "PAGE", "ORIENTATION_TEXT_DECISION", "doc=%p | page=%d | reason=CJK_VERTICAL_DOMINANT | result=PORTRAIT", (void*)doc, page_num + 1);
                result = 0; /* portrait */

            } else {
                /* 混合方向，回退到宽高比 */
                LOG_EVENT("DEBUG", "PAGE", "ORIENTATION_TEXT_DECISION", "doc=%p | page=%d | reason=MIXED_DIRECTION | fallback=PAGE_ASPECT", (void*)doc, page_num + 1);
                result = (page_w > page_h) ? 1 : 0;
            }
        }

        fz_drop_stext_page(fz, stext_page);
        fz_drop_page(fz, page);
    } fz_catch(fz) {
        const char* caught_message = fz_caught_message(fz);
        LOG_EVENT("ERROR", "PAGE", "ORIENTATION_TEXT_END", "status=FAIL | doc=%p | page=%d | ret=-2 | message=\"%s\"", (void*)doc, page_num + 1, caught_message);
        SET_ERROR(ctx, caught_message);
        return -2;
    }

    LOG_EVENT("INFO", "PAGE", "ORIENTATION_TEXT_END", "status=OK | doc=%p | page=%d | result=%s | ret=%d", (void*)doc, page_num + 1, result == 1 ? "LANDSCAPE" : (result == 0 ? "PORTRAIT" : "UNKNOWN"), result);

    return result;
}

// ============================================
// 位置计算
// ============================================

static void calculate_rule_position(
    const LayoutRule* rule,
    float page_width,
    float page_height,
    float content_width,
    float content_height,
    float* out_x,
    float* out_y,
    int* out_b5_match,
    int* out_b5_applied
) {
    float x = rule->margin_x;
    float y = rule->margin_y;

    switch (rule->type) {
        case 0:
            x = page_width - content_width - rule->margin_x;
            y = rule->margin_y;
            break;
        case 1:
            x = rule->margin_x;
            y = rule->margin_y;
            break;
        case 2:
            x = page_width - content_width - rule->margin_x;
            y = page_height - content_height - rule->margin_y;
            break;
        case 3:
            x = rule->margin_x;
            y = page_height - content_height - rule->margin_y;
            break;
        case 4:
            x = (page_width - content_width) / 2.0f;
            y = rule->margin_y;
            break;
        case 5:
            x = (page_width - content_width) / 2.0f;
            y = page_height - content_height - rule->margin_y;
            break;
        default:
            x = rule->margin_x;
            y = rule->margin_y;
            break;
    }

    int b5_match = 0;
    int b5_applied = 0;
    if (rule->b5_offset_x != 0) {
        float short_side = page_width < page_height ? page_width : page_height;
        if (short_side >= 474.0f && short_side <= 541.0f) {
            b5_match = 1;
            if (rule->type == 1 || rule->type == 3) {
                x += rule->b5_offset_x;
                b5_applied = 1;
            }
            if (rule->type == 2 || rule->type == 3 || rule->type == 5) {
                y -= rule->b5_offset_y;
            } else {
                y += rule->b5_offset_y;
            }
            b5_applied = 1;
        }
    }

    *out_x = x;
    *out_y = y;
    if (out_b5_match) *out_b5_match = b5_match;
    if (out_b5_applied) *out_b5_applied = b5_applied;
}

int mupdf_calc_position(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    const LayoutRule* rule,
    float content_width,
    float content_height,
    float* out_x,
    float* out_y
) {
    if (!ctx || !doc || !rule || !out_x || !out_y) return -1;

    float page_w = 595, page_h = 842; // A4 默认值
    mupdf_get_page_size(ctx, doc, page_num, &page_w, &page_h);

    // B5纸张特殊偏移（仅该客户打印纸槽偏差用，默认0不影响其他用户）
    int b5_enabled = rule->b5_offset_x != 0;
    int b5_match = 0;
    int b5_applied = 0;
    calculate_rule_position(
        rule, page_w, page_h, content_width, content_height,
        out_x, out_y, &b5_match, &b5_applied);

    float x = *out_x;
    float y = *out_y;

    int inside_page = x >= 0 && y >= 0 && x + content_width <= page_w && y + content_height <= page_h;
    LOG_EVENT(
        inside_page ? "INFO" : "WARN", "LAYOUT", "POSITION",
        "doc=%p | page=%d | position_id=%d | position=%s | page_size=%.3fx%.3f | "
        "content_size=%.3fx%.3f | margin=%.3f,%.3f | b5_enabled=%s | b5_match=%s | "
        "b5_applied=%s | b5_offset=%d,%d | visual_xy=%.3f,%.3f | "
        "visual_rect=[%.3f,%.3f,%.3f,%.3f] | inside_page=%s",
        (void*)doc, page_num + 1, rule->type, log_position_name(rule->type),
        page_w, page_h, content_width, content_height,
        rule->margin_x, rule->margin_y,
        b5_enabled ? "true" : "false", b5_match ? "true" : "false", b5_applied ? "true" : "false",
        rule->b5_offset_x, rule->b5_offset_y, x, y,
        x, y, x + content_width, y + content_height,
        inside_page ? "true" : "false");

    return 0;
}

static int mupdf_calc_native_position(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    const LayoutRule* rule,
    float content_width,
    float content_height,
    float* out_x,
    float* out_y,
    PageNativeGeometry* geometry_out
) {
    if (!ctx || !doc || !doc->pdf_doc || !rule || !out_x || !out_y) return -1;

    fz_context* fz = ctx->ctx;
    pdf_page* page = nullptr;
    PageNativeGeometry geometry;
    int b5_match = 0;
    int b5_applied = 0;

    fz_var(page);
    fz_try(fz) {
        page = pdf_load_page(fz, doc->pdf_doc, page_num);
        load_native_page_geometry(fz, page, &geometry);
        calculate_rule_position(
            rule, geometry.native_width, geometry.native_height,
            content_width, content_height, out_x, out_y,
            &b5_match, &b5_applied);
    } fz_always(fz) {
        if (page) pdf_drop_page(fz, page);
    } fz_catch(fz) {
        SET_ERROR(ctx, fz_caught_message(fz));
        return -2;
    }

    fz_rect native_rect = fz_make_rect(
        *out_x, *out_y, *out_x + content_width, *out_y + content_height);
    fz_rect visual_rect = native_rect_to_visual(&geometry, native_rect);
    int inside = *out_x >= 0.0f && *out_y >= 0.0f &&
        *out_x + content_width <= geometry.native_width &&
        *out_y + content_height <= geometry.native_height;

    LOG_EVENT(
        inside ? "INFO" : "WARN", "LAYOUT", "NATIVE_POSITION",
        "doc=%p | page=%d | placement_space=NATIVE | rotate=%d | position_id=%d | position=%s | "
        "native_page_size=%.3fx%.3f | content_size=%.3fx%.3f | margin=%.3f,%.3f | "
        "b5_match=%s | b5_applied=%s | native_rect=[%.3f,%.3f,%.3f,%.3f] | "
        "expected_visual=[%.3f,%.3f,%.3f,%.3f] | inside_page=%s",
        (void*)doc, page_num + 1, geometry.rotate, rule->type, log_position_name(rule->type),
        geometry.native_width, geometry.native_height, content_width, content_height,
        rule->margin_x, rule->margin_y,
        b5_match ? "true" : "false", b5_applied ? "true" : "false",
        native_rect.x0, native_rect.y0, native_rect.x1, native_rect.y1,
        visual_rect.x0, visual_rect.y0, visual_rect.x1, visual_rect.y1,
        inside ? "true" : "false");

    if (geometry_out) *geometry_out = geometry;
    return 0;
}

// ============================================
// 内部辅助：通过内容流添加图片
//
// inherit_page_rotation=0 时输入为旋转后的视觉坐标；
// inherit_page_rotation=1 时输入为未旋转页面坐标，并随 /Rotate 旋转。
// ============================================

static int add_image_to_page_content(
    fz_context* fz,
    pdf_document* pdf_doc,
    pdf_page* page,
    MupdfDocument* wrapper_doc,
    int page_num,
    fz_image* img,
    float x, float y, float w, float h,
    int inherit_page_rotation
) {
    pdf_obj* img_ref = nullptr;
    pdf_annot* stamp = nullptr;
    fz_buffer* ap = nullptr;

    fz_var(img_ref);
    fz_var(stamp);
    fz_var(ap);

    fz_try(fz) {
    // 将图片添加到 PDF 资源
    img_ref = pdf_add_image(fz, pdf_doc, img);
    if (!img_ref) fz_throw(fz, FZ_ERROR_GENERIC, "Cannot add image object");

    // 获取/创建页面资源字典
    //pdf_obj* page_obj = page->obj;
   // pdf_obj* resources = pdf_dict_get(fz, page_obj, PDF_NAME(Resources));
	//pdf_pbj* inherited = pdf_dict_get_inherited(fz,page_obj,PDF_NAME(Resources));
    //if (!resources) {
    //    resources = pdf_new_dict(fz, pdf_doc, 2);
    //    pdf_dict_put(fz, page_obj, PDF_NAME(Resources), resources);
    //    pdf_drop_obj(fz, resources);
    //    resources = pdf_dict_get(fz, page_obj, PDF_NAME(Resources));
    //}
	
	   // 获取/创建页面资源字典
    pdf_obj* page_obj = page->obj;
	pdf_flatten_inheritable_page_items(fz,page_obj);
    pdf_obj* resources = pdf_dict_get(fz, page_obj, PDF_NAME(Resources));
	//pdf_obj* inherited = pdf_dict_get_inherited(fz,page_obj,PDF_NAME(Resources));
    if (!resources) {
        resources = pdf_new_dict(fz, pdf_doc, 2);
        pdf_dict_put_drop(fz, page_obj, PDF_NAME(Resources), resources);
        resources = pdf_dict_get(fz, page_obj, PDF_NAME(Resources));
    } else {
        pdf_obj* resources_copy = pdf_copy_dict(fz, resources);
        pdf_dict_put_drop(fz, page_obj, PDF_NAME(Resources), resources_copy);
        resources = pdf_dict_get(fz, page_obj, PDF_NAME(Resources));
    }
	
    // 获取/创建 XObject 子字典
    pdf_obj* xobj = pdf_dict_get(fz, resources, PDF_NAME(XObject));
    if (!xobj) {
        xobj = pdf_new_dict(fz, pdf_doc, 2);
        pdf_dict_put_drop(fz, resources, PDF_NAME(XObject), xobj);
        xobj = pdf_dict_get(fz, resources, PDF_NAME(XObject));
    } else {
        pdf_obj* xobj_copy = pdf_copy_dict(fz, xobj);
        pdf_dict_put_drop(fz, resources, PDF_NAME(XObject), xobj_copy);
        xobj = pdf_dict_get(fz, resources, PDF_NAME(XObject));
    }

    // 为当前页面选择未占用的资源名，避免覆盖原内容或先前添加的图片。
    char res_name[32];
    int res_index = 0;
    do {
        snprintf(res_name, sizeof(res_name), "HDIm%d", res_index++);
    } while (pdf_dict_gets(fz, xobj, res_name) != nullptr);

    // 将图片引用加入 XObject 字典
    pdf_dict_puts(fz, xobj, res_name, img_ref);

    log_page_geometry(fz, page, wrapper_doc, page_num, "IMAGE_ADD");

    PageNativeGeometry native_geometry;
    fz_rect input_rect = fz_make_rect(x, y, x + w, y + h);
    fz_rect expected_visual;
    if (inherit_page_rotation) {
        load_native_page_geometry(fz, page, &native_geometry);
        expected_visual = native_rect_to_visual(&native_geometry, input_rect);
    } else {
        fz_rect page_bounds = fz_bound_page(fz, (fz_page*)page);
        expected_visual = fz_make_rect(
            page_bounds.x0 + x,
            page_bounds.y0 + y,
            page_bounds.x0 + x + w,
            page_bounds.y0 + y + h);
    }

    LOG_EVENT(
        "INFO", "IMAGE", "PAGE_PLAN",
        "doc=%p | page=%d | resource=%s | placement_space=%s | rotate=%d | "
        "input_rect=[%.3f,%.3f,%.3f,%.3f] | expected_visual=[%.3f,%.3f,%.3f,%.3f] | "
        "size=%.3fx%.3f | aspect=%.6f",
        (void*)wrapper_doc, page_num + 1, res_name,
        inherit_page_rotation ? "NATIVE" : "VISUAL",
        inherit_page_rotation ? native_geometry.rotate : 0,
        input_rect.x0, input_rect.y0, input_rect.x1, input_rect.y1,
        expected_visual.x0, expected_visual.y0, expected_visual.x1, expected_visual.y1,
        w, h, h != 0.0f ? w / h : 0.0f);

    // 创建 Stamp 注释（代替内容流写入），浮动在页面顶层，不被原内容遮盖
    stamp = pdf_create_annot(fz, page, PDF_ANNOT_STAMP);
    int flags = pdf_annot_flags(fz, stamp) | PDF_ANNOT_IS_PRINT;
    if (!inherit_page_rotation) flags |= PDF_ANNOT_IS_NO_ROTATE;
    pdf_set_annot_flags(fz, stamp, flags);

    fz_rect annot_design_rect = inherit_page_rotation
        ? expected_visual
        : no_rotate_annot_design_rect(fz, page, expected_visual);
    pdf_set_annot_rect(fz, stamp, annot_design_rect);

    // 构建注释外观流：画图片
    char ap_buf[256];
    snprintf(ap_buf, sizeof(ap_buf), "q\n%.4f 0 0 %.4f 0 0 cm\n/%s Do\nQ\n", w, h, res_name);
    ap = fz_new_buffer_from_copied_data(fz, (const unsigned char*)ap_buf, strlen(ap_buf));

    // 设置外观，使用页面资源（已注册图片到 XObject）
    fz_rect bbox = fz_make_rect(0, 0, w, h);
    fz_matrix ctm = fz_identity;
    pdf_set_annot_appearance(fz, stamp, "N", nullptr, ctm, bbox, resources, ap);

    pdf_update_annot(fz, stamp);
    log_stamp_annotation(fz, stamp, wrapper_doc, page_num, expected_visual, bbox, ctm, res_name);
    } fz_always(fz) {
        fz_drop_buffer(fz, ap);
        pdf_drop_annot(fz, stamp);
        pdf_drop_obj(fz, img_ref);
    } fz_catch(fz) {
        fz_rethrow(fz);
    }

    return 0;
}

// ============================================
// 内部辅助：SimSun（宋体）字体加载与文字编码
// ============================================

#define SIMSUN_FONT_PATH    L"C:\\Windows\\Fonts\\simsun.ttc"
#define SIMSUN_FONT_NAME    "SimSun"
#define SIMSUN_PAGE_RES_NAME "FSun"

static fz_font* get_simsun_font(fz_context* fz, MupdfContext* ctx) {
    if (ctx->cached_simsun_font) {
        LOG_EVENT("DEBUG", "FONT", "CACHE_HIT", "font=SimSun | font_ptr=%p", (void*)ctx->cached_simsun_font);
        return ctx->cached_simsun_font;
    }

    LOG_EVENT("INFO", "FONT", "LOAD_BEGIN", "font=SimSun | path=simsun.ttc | ttc_index=0");

    fz_try(fz) {
        ctx->cached_simsun_font = fz_new_font_from_file(fz, SIMSUN_FONT_NAME, "C:\\Windows\\Fonts\\simsun.ttc", 0, 0);
    } fz_catch(fz) {
        LOG_EVENT("ERROR", "FONT", "LOAD_END", "status=FAIL | font=SimSun | path=simsun.ttc | ttc_index=0 | message=\"%s\"", fz_caught_message(fz));
        ctx->cached_simsun_font = nullptr;
    }

    if (ctx->cached_simsun_font) {
        LOG_EVENT("INFO", "FONT", "LOAD_END", "status=OK | font=SimSun | font_ptr=%p | path=simsun.ttc | ttc_index=0", (void*)ctx->cached_simsun_font);
    }

    return ctx->cached_simsun_font;
}

static pdf_obj* get_simsun_pdf_font(fz_context* fz, MupdfContext* mctx, MupdfDocument* doc) {
    if (doc->cached_simsun_ref) {
        LOG_EVENT("DEBUG", "FONT", "PDF_CACHE_HIT", "doc=%p | font=SimSun | pdf_font=%p", (void*)doc, (void*)doc->cached_simsun_ref);
        return doc->cached_simsun_ref;
    }

    fz_font* font = get_simsun_font(fz, mctx);
    if (!font) return nullptr;

    fz_try(fz) {
        doc->cached_simsun_ref = pdf_add_cid_font(fz, doc->pdf_doc, font);
    } fz_catch(fz) {
        LOG_EVENT("ERROR", "FONT", "PDF_ADD", "status=FAIL | doc=%p | font=SimSun | message=\"%s\"", (void*)doc, fz_caught_message(fz));
        doc->cached_simsun_ref = nullptr;
    }

    if (doc->cached_simsun_ref) {
        LOG_EVENT("INFO", "FONT", "PDF_ADD", "status=OK | doc=%p | font=SimSun | pdf_font=%p", (void*)doc, (void*)doc->cached_simsun_ref);
    }

    return doc->cached_simsun_ref;
}

static void reset_simsun_pdf_font(fz_context* fz, MupdfDocument* doc) {
    if (doc && doc->cached_simsun_ref) {
        pdf_drop_obj(fz, doc->cached_simsun_ref);
        doc->cached_simsun_ref = nullptr;
    }
}

static void add_font_to_page_resources(
    fz_context* fz,
    pdf_document* pdf_doc,
    pdf_page* page,
    const char* base_name,
    pdf_obj* font_ref,
    char* resource_name,
    size_t resource_name_size
) {
    pdf_obj* page_obj = page->obj;
    pdf_flatten_inheritable_page_items(fz, page_obj);
    pdf_obj* resources = pdf_dict_get(fz, page_obj, PDF_NAME(Resources));
    if (!resources) {
        resources = pdf_new_dict(fz, pdf_doc, 2);
        pdf_dict_put_drop(fz, page_obj, PDF_NAME(Resources), resources);
        resources = pdf_dict_get(fz, page_obj, PDF_NAME(Resources));
    } else {
        pdf_obj* resources_copy = pdf_copy_dict(fz, resources);
        pdf_dict_put_drop(fz, page_obj, PDF_NAME(Resources), resources_copy);
        resources = pdf_dict_get(fz, page_obj, PDF_NAME(Resources));
    }

    pdf_obj* fonts = pdf_dict_get(fz, resources, PDF_NAME(Font));
    if (!fonts) {
        fonts = pdf_new_dict(fz, pdf_doc, 2);
        pdf_dict_put_drop(fz, resources, PDF_NAME(Font), fonts);
        fonts = pdf_dict_get(fz, resources, PDF_NAME(Font));
    } else {
        pdf_obj* fonts_copy = pdf_copy_dict(fz, fonts);
        pdf_dict_put_drop(fz, resources, PDF_NAME(Font), fonts_copy);
        fonts = pdf_dict_get(fz, resources, PDF_NAME(Font));
    }

    int name_len = snprintf(resource_name, resource_name_size, "%s", base_name);
    if (name_len <= 0 || (size_t)name_len >= resource_name_size) {
        fz_throw(fz, FZ_ERROR_LIMIT, "Barcode font resource name is too long");
    }

    pdf_obj* existing = pdf_dict_gets(fz, fonts, resource_name);
    if (existing && pdf_objcmp_resolve(fz, existing, font_ref) != 0) {
        int index = 0;
        do {
            name_len = snprintf(resource_name, resource_name_size, "%s%d", base_name, index++);
            if (name_len <= 0 || (size_t)name_len >= resource_name_size) {
                fz_throw(fz, FZ_ERROR_LIMIT, "Cannot allocate barcode font resource name");
            }
            existing = pdf_dict_gets(fz, fonts, resource_name);
        } while (existing && pdf_objcmp_resolve(fz, existing, font_ref) != 0);
    }

    if (!existing) pdf_dict_puts(fz, fonts, resource_name, font_ref);
}

static int utf8_to_gid_hex(fz_context* fz, fz_font* font, const char* utf8, char* hex_out, int hex_out_size) {
    if (!utf8 || !hex_out || hex_out_size <= 2) return 0;

    const unsigned char* p = (const unsigned char*)utf8;
    int pos = 0;
    int codepoint_count = 0;
    int glyph_count = 0;
    int missing_count = 0;
    unsigned int first_missing = 0;
    hex_out[0] = '\0';

    while (*p && pos < hex_out_size - 5) {
        unsigned int cp = 0;
        if (*p < 0x80) {
            cp = *p;
            p += 1;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = ((*p & 0x1F) << 6) | (*(p + 1) & 0x3F);
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = ((*p & 0x0F) << 12) | ((*(p + 1) & 0x3F) << 6) | (*(p + 2) & 0x3F);
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) {
            cp = ((*p & 0x07) << 18) | ((*(p + 1) & 0x3F) << 12) | ((*(p + 2) & 0x3F) << 6) | (*(p + 3) & 0x3F);
            p += 4;
        } else {
            p++;
            continue;
        }

        if (cp >= 0x10000) {
            int hi_surr = 0xD800 | ((cp - 0x10000) >> 10);
            int lo_surr = 0xDC00 | ((cp - 0x10000) & 0x3FF);
            int gid_hi = fz_encode_character(fz, font, hi_surr);
            int gid_lo = fz_encode_character(fz, font, lo_surr);
            if (gid_hi <= 0 || gid_lo <= 0) {
                missing_count++;
                if (first_missing == 0) first_missing = cp;
            }
            pos += snprintf(hex_out + pos, hex_out_size - pos, "%04X%04X",
                (unsigned int)(gid_hi > 0 ? gid_hi : 1),
                (unsigned int)(gid_lo > 0 ? gid_lo : 1));
            glyph_count += 2;
        } else {
            int gid = fz_encode_character(fz, font, (int)cp);
            if (gid <= 0) {
                missing_count++;
                if (first_missing == 0) first_missing = cp;
                gid = 1;
            }
            pos += snprintf(hex_out + pos, hex_out_size - pos, "%04X", (unsigned int)gid);
            glyph_count++;
        }
        codepoint_count++;
    }

    LOG_EVENT(
        missing_count ? "WARN" : "DEBUG", "FONT", "ENCODE",
        "font=SimSun | codepoints=%d | glyphs=%d | gid_hex_length=%d | missing_glyphs=%d | first_missing=U+%04X",
        codepoint_count, glyph_count, pos, missing_count, first_missing);

    return pos;
}

static float measure_text_width_simsun(fz_context* fz, fz_font* font, const char* utf8, float font_size) {
    if (!utf8 || !font) return 0;

    float total_w = 0;
    const unsigned char* p = (const unsigned char*)utf8;

    while (*p) {
        unsigned int cp = 0;
        if (*p < 0x80) {
            cp = *p;
            p += 1;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = ((*p & 0x1F) << 6) | (*(p + 1) & 0x3F);
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = ((*p & 0x0F) << 12) | ((*(p + 1) & 0x3F) << 6) | (*(p + 2) & 0x3F);
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) {
            cp = ((*p & 0x07) << 18) | ((*(p + 1) & 0x3F) << 12) | ((*(p + 2) & 0x3F) << 6) | (*(p + 3) & 0x3F);
            p += 4;
        } else {
            p++;
            continue;
        }

        int gid = fz_encode_character(fz, font, (int)cp);
        if (gid > 0) {
            total_w += fz_advance_glyph(fz, font, gid, 0) * font_size;
        } else {
            total_w += font_size * 0.5f;
        }
    }

    return total_w;
}

static void append_isolated_page_content(
    fz_context* fz,
    pdf_document* pdf_doc,
    pdf_obj* page_obj,
    const char* content,
    size_t content_len
) {
    fz_buffer* label_buffer = nullptr;
    fz_buffer* prefix_buffer = nullptr;
    pdf_obj* label_stream = nullptr;
    pdf_obj* prefix_stream = nullptr;
    pdf_obj* new_contents = nullptr;
    pdf_obj* old_contents = pdf_dict_get(fz, page_obj, PDF_NAME(Contents));

    fz_var(label_buffer);
    fz_var(prefix_buffer);
    fz_var(label_stream);
    fz_var(prefix_stream);
    fz_var(new_contents);

    fz_try(fz) {
        label_buffer = fz_new_buffer(fz, content_len + 2);
        if (old_contents) {
            fz_append_data(fz, label_buffer, "Q\n", 2);
        }
        fz_append_data(fz, label_buffer, content, content_len);
        label_stream = pdf_add_stream(fz, pdf_doc, label_buffer, nullptr, 0);

        if (!old_contents) {
            pdf_dict_put(fz, page_obj, PDF_NAME(Contents), label_stream);
        } else {
            if (!pdf_is_stream(fz, old_contents) && !pdf_is_array(fz, old_contents)) {
                fz_throw(fz, FZ_ERROR_FORMAT, "Page Contents is neither a stream nor an array");
            }

            int old_count = pdf_is_array(fz, old_contents) ? pdf_array_len(fz, old_contents) : 1;
            new_contents = pdf_new_array(fz, pdf_doc, old_count + 2);
            prefix_buffer = fz_new_buffer_from_copied_data(fz, (const unsigned char*)"q\n", 2);
            prefix_stream = pdf_add_stream(fz, pdf_doc, prefix_buffer, nullptr, 0);
            pdf_array_push(fz, new_contents, prefix_stream);

            if (pdf_is_array(fz, old_contents)) {
                for (int i = 0; i < old_count; ++i) {
                    pdf_array_push(fz, new_contents, pdf_array_get(fz, old_contents, i));
                }
            } else {
                pdf_array_push(fz, new_contents, old_contents);
            }

            pdf_array_push(fz, new_contents, label_stream);
            pdf_dict_put(fz, page_obj, PDF_NAME(Contents), new_contents);
        }
    } fz_always(fz) {
        pdf_drop_obj(fz, new_contents);
        pdf_drop_obj(fz, prefix_stream);
        pdf_drop_obj(fz, label_stream);
        fz_drop_buffer(fz, prefix_buffer);
        fz_drop_buffer(fz, label_buffer);
    } fz_catch(fz) {
        fz_rethrow(fz);
    }
}

static int add_text_direct_to_page(
    fz_context* fz,
    pdf_document* pdf_doc,
    pdf_page* page,
    MupdfContext* mctx,
    MupdfDocument* doc,
    const char* text,
    float x, float y,
    float font_size,
    float color_r,
    float color_g,
    float color_b,
    const fz_matrix* text_matrix = nullptr
) {
    char text_position[192];
    int position_len;

    if (text_matrix) {
        position_len = snprintf(
            text_position,
            sizeof(text_position),
            "%.6f %.6f %.6f %.6f %.6f %.6f Tm ",
            text_matrix->a,
            text_matrix->b,
            text_matrix->c,
            text_matrix->d,
            text_matrix->e,
            text_matrix->f);
    } else {
        position_len = snprintf(
            text_position,
            sizeof(text_position),
            "%.2f %.2f Td ",
            x,
            y);
    }

    if (position_len <= 0 || position_len >= (int)sizeof(text_position)) {
        return -1;
    }

    char text_log[4096];
    log_escape_value(text, text_log, sizeof(text_log));
    LOG_EVENT(
        "DEBUG", "TEXT", "CONTENT_BEGIN",
        "doc=%p | page_obj=%p | text=\"%s\" | font_size=%.3f | color=%.3f,%.3f,%.3f | positioning=%s | operator=\"%s\"",
        (void*)doc, (void*)page->obj, text_log, font_size, color_r, color_g, color_b,
        text_matrix ? "TM" : "TD", text_position);

    pdf_obj* font_ref = get_simsun_pdf_font(fz, mctx, doc);
    if (!font_ref) {
        LOG_EVENT("ERROR", "FONT", "FALLBACK_REJECTED", "doc=%p | page_obj=%p | requested=SimSun | reason=font_embedding_failed", (void*)doc, (void*)page->obj);
        return -1;
    }

    char font_resource_name[32];
    add_font_to_page_resources(
        fz, pdf_doc, page, SIMSUN_PAGE_RES_NAME, font_ref,
        font_resource_name, sizeof(font_resource_name));

    fz_font* font = get_simsun_font(fz, mctx);

    char gid_hex[16384];
    utf8_to_gid_hex(fz, font, text, gid_hex, sizeof(gid_hex));

    char content_buf[32768];
    int len = snprintf(content_buf, sizeof(content_buf),
        "q\n"
        "%.3f %.3f %.3f rg\n"
        "BT\n"
        "/%s %.2f Tf\n"
        "%s"
        "<%s> Tj\n"
        "ET\n"
        "Q\n",
        color_r, color_g, color_b,
        font_resource_name, font_size,
        text_position,
        gid_hex);

    if (len <= 0 || len >= (int)(sizeof(content_buf) - 1)) {
        return -1;
    }

    append_isolated_page_content(fz, pdf_doc, page->obj, content_buf, (size_t)len);
    LOG_EVENT("INFO", "TEXT", "CONTENT_END", "status=OK | doc=%p | page_obj=%p | font=SimSun | resource=%s | stream_bytes=%d | gid_hex_length=%zu", (void*)doc, (void*)page->obj, font_resource_name, len, strlen(gid_hex));
    return 0;
}

/**
 * @brief 直接绘制单行文字（不换行）
 */
int mupdf_add_text_direct(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    const char* text,
    float font_size,
    float x, float y,
    float color_r,
    float color_g,
    float color_b
) {
    if (!ctx || !doc || !text) return -1;
    if (!doc->pdf_doc) { SET_ERROR(ctx, "Not a PDF document"); return -2; }

    fz_context* fz = ctx->ctx;
    int result = 0;
    pdf_page* page = nullptr;
    int operation_started = 0;
    char text_log[4096];
    char text_hex[260];

    fz_var(page);
    fz_var(operation_started);

    log_escape_value(text, text_log, sizeof(text_log));
    log_hex_bytes((const unsigned char*)text, strlen(text), text_hex, sizeof(text_hex));
    LOG_EVENT("INFO", "TEXT", "DIRECT_BEGIN", "doc=%p | page=%d | text=\"%s\" | text_hex=%s | utf8_valid=%s | pdf_xy=%.3f,%.3f | font_size=%.3f | color=%.3f,%.3f,%.3f", (void*)doc, page_num + 1, text_log, text_hex, MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0) > 0 ? "true" : "false", x, y, font_size, color_r, color_g, color_b);

    fz_try(fz) {
        begin_edit_operation(fz, doc, "Add direct text to page");
        operation_started = 1;
        page = pdf_load_page(fz, doc->pdf_doc, page_num);
        log_page_geometry(fz, page, doc, page_num, "TEXT_DIRECT");
        result = add_text_direct_to_page(fz, doc->pdf_doc, page, ctx, doc, text, x, y, font_size, color_r, color_g, color_b);
        if (result != 0) {
            fz_throw(fz, FZ_ERROR_GENERIC, "Cannot write direct text content stream");
        }
        end_edit_operation(fz, doc);
        operation_started = 0;
    } fz_always(fz) {
        if (page) pdf_drop_page(fz, page);
    } fz_catch(fz) {
        if (operation_started) abandon_edit_operation(fz, doc);
        reset_simsun_pdf_font(fz, doc);
        const char* caught_message = fz_caught_message(fz);
        LOG_EVENT("ERROR", "TEXT", "DIRECT_END", "status=FAIL | doc=%p | page=%d | ret=-3 | message=\"%s\"", (void*)doc, page_num + 1, caught_message);
        SET_ERROR(ctx, caught_message);
        return -3;
    }

    LOG_EVENT(result == 0 ? "INFO" : "ERROR", "TEXT", "DIRECT_END", "status=%s | doc=%p | page=%d | ret=%d", result == 0 ? "OK" : "FAIL", (void*)doc, page_num + 1, result);
    return result;
}

/**
 * @brief 批量添加单行文字（不换行）
 */
int mupdf_batch_add_text_direct(
    MupdfContext* ctx,
    MupdfDocument* doc,
    const int* page_nums,
    int page_count,
    const char* text,
    float font_size,
    LayoutRule* rule,
    float color_r,
    float color_g,
    float color_b
) {
    if (!ctx || !doc || !text) return -1;

    int total_pages = 0;
    mupdf_get_page_count(ctx, doc, &total_pages);

    LayoutRule default_rule;
    if (!rule) {
        memset(&default_rule, 0, sizeof(default_rule));
        default_rule.type = 1;
        default_rule.margin_x = 10;
        default_rule.margin_y = 10;
        rule = &default_rule;
    }

    int added = 0;

    if (page_nums && page_count > 0) {
        for (int i = 0; i < page_count; i++) {
            int pn = page_nums[i];
            if (pn < 0 || pn >= total_pages) {
                LOG_EVENT("WARN", "TEXT", "PAGE_SKIP", "doc=%p | page_index=%d | reason=out_of_range | total_pages=%d", (void*)doc, pn, total_pages);
                continue;
            }
            float img_x = 0, img_y = 0;
            mupdf_calc_position(ctx, doc, pn, rule, rule->image_width, rule->image_height, &img_x, &img_y);
            float x = img_x;
            float y = img_y + rule->image_height + rule->text_gap;
            if (mupdf_add_text_direct(ctx, doc, pn, text, font_size, x, y, color_r, color_g, color_b) == 0) added++;
        }
    } else {
        for (int i = 0; i < total_pages; i++) {
            float img_x = 0, img_y = 0;
            mupdf_calc_position(ctx, doc, i, rule, rule->image_width, rule->image_height, &img_x, &img_y);
            float x = img_x;
            float y = img_y + rule->image_height + rule->text_gap;
            if (mupdf_add_text_direct(ctx, doc, i, text, font_size, x, y, color_r, color_g, color_b) == 0) added++;
        }
    }

    return added;
}

// ============================================
// 添加图片
// ============================================

static int mupdf_add_image_internal(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    const char* image_path,
    float x, float y,
    float width, float height,
    int inherit_page_rotation
) {
    if (!ctx || !doc || !image_path) return -1;
    if (!doc->pdf_doc) { SET_ERROR(ctx, "Not a PDF document"); return -2; }

    char image_path_utf8[4096];
    log_acp_as_utf8(image_path, image_path_utf8, sizeof(image_path_utf8));

    fz_context* fz = ctx->ctx;
    int result = 0;
    fz_image* img = nullptr;
    fz_page* size_page = nullptr;
    pdf_page* page = nullptr;
    int operation_started = 0;

    fz_var(img);
    fz_var(size_page);
    fz_var(page);
    fz_var(operation_started);

    fz_try(fz) {
        begin_edit_operation(fz, doc, "Add image to page");
        operation_started = 1;
        img = fz_new_image_from_file(fz, image_path);

        // 像素→点换算：点数 = 像素数 × 72 / dpi
        // 若图片没有嵌入 dpi 信息则 xres/yres 默认96
        float w, h;
        if (width > 0) {
            w = width;
        } else {
            int xres = 0, yres = 0;
            fz_image_resolution(img, &xres, &yres);
            if (xres <= 0) xres = 96;
            w = (float)img->w * 72.0f / (float)xres;
        }
        if (height > 0) {
            h = height;
        } else {
            int xres = 0, yres = 0;
            fz_image_resolution(img, &xres, &yres);
            if (yres <= 0) yres = 96;
            h = (float)img->h * 72.0f / (float)yres;
        }

        // 获取页面尺寸，将图片裁剪到页面范围内
        float page_w = 595, page_h = 842;
        size_page = fz_load_page(fz, doc->fz_doc, page_num);
        fz_rect pr = fz_bound_page(fz, size_page);
        page_w = pr.x1 - pr.x0;
        page_h = pr.y1 - pr.y0;
        if (inherit_page_rotation) {
            PageNativeGeometry geometry;
            load_native_page_geometry(fz, (pdf_page*)size_page, &geometry);
            page_w = geometry.native_width;
            page_h = geometry.native_height;
        }
        fz_drop_page(fz, size_page);
        size_page = nullptr;
        // 防止图片超出页面（只限制最大值，不限制最小值，允许负坐标）
        if (w > page_w) w = page_w;
        if (h > page_h) h = page_h;
        // 允许负坐标（内容可以超出页面边界），不强制限制

        page = pdf_load_page(fz, doc->pdf_doc, page_num);
        result = add_image_to_page_content(
            fz, doc->pdf_doc, page, doc, page_num, img,
            x, y, w, h, inherit_page_rotation);
        if (result != 0) {
            fz_throw(fz, FZ_ERROR_GENERIC, "Cannot add image annotation");
        }
        end_edit_operation(fz, doc);
        operation_started = 0;
    } fz_always(fz) {
        if (page) pdf_drop_page(fz, page);
        if (size_page) fz_drop_page(fz, size_page);
        if (img) fz_drop_image(fz, img);
    } fz_catch(fz) {
        if (operation_started) abandon_edit_operation(fz, doc);
        const char* caught_message = fz_caught_message(fz);
        LOG_EVENT("ERROR", "IMAGE", "PAGE_END", "status=FAIL | doc=%p | page=%d | source=file | path=\"%s\" | ret=-3 | message=\"%s\"", (void*)doc, page_num + 1, image_path_utf8, caught_message);
        SET_ERROR(ctx, caught_message);
        return -3;
    }

    LOG_EVENT(result == 0 ? "INFO" : "ERROR", "IMAGE", "PAGE_END", "status=%s | doc=%p | page=%d | source=file | path=\"%s\" | ret=%d", result == 0 ? "OK" : "FAIL", (void*)doc, page_num + 1, image_path_utf8, result);
    return result;
}

int mupdf_add_image(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    const char* image_path,
    float x, float y,
    float width, float height
) {
    return mupdf_add_image_internal(
        ctx, doc, page_num, image_path,
        x, y, width, height, 0);
}

int mupdf_add_image_from_mem(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    const unsigned char* image_data,
    size_t image_size,
    const char* image_format,
    float x, float y,
    float width, float height
) {
    if (!ctx || !doc || !image_data || image_size == 0) return -1;
    if (!doc->pdf_doc) { SET_ERROR(ctx, "Not a PDF document"); return -2; }

    char image_prefix_hex[260];
    char image_format_log[256];
    log_hex_bytes(image_data, image_size, image_prefix_hex, sizeof(image_prefix_hex));
    log_escape_value(image_format ? image_format : "(auto)", image_format_log, sizeof(image_format_log));
    LOG_EVENT("INFO", "IMAGE", "MEMORY_BEGIN", "doc=%p | page=%d | format=\"%s\" | input_bytes=%zu | prefix_hex=%s | requested_size=%.3fx%.3f | visual_xy=%.3f,%.3f", (void*)doc, page_num + 1, image_format_log, image_size, image_prefix_hex, width, height, x, y);

    fz_context* fz = ctx->ctx;
    int result = 0;
    fz_buffer* buf = nullptr;
    fz_image* img = nullptr;
    fz_page* size_page = nullptr;
    pdf_page* page = nullptr;
    int operation_started = 0;

    fz_var(buf);
    fz_var(img);
    fz_var(size_page);
    fz_var(page);
    fz_var(operation_started);

    fz_try(fz) {
        begin_edit_operation(fz, doc, "Add memory image to page");
        operation_started = 1;
        buf = fz_new_buffer_from_shared_data(fz, image_data, image_size);
        img = fz_new_image_from_buffer(fz, buf);
        fz_drop_buffer(fz, buf);
        buf = nullptr;

        float w, h;
        if (width > 0) {
            w = width;
        } else {
            int xres = 0, yres = 0;
            fz_image_resolution(img, &xres, &yres);
            if (xres <= 0) xres = 96;
            w = (float)img->w * 72.0f / (float)xres;
        }
        if (height > 0) {
            h = height;
        } else {
            int xres = 0, yres = 0;
            fz_image_resolution(img, &xres, &yres);
            if (yres <= 0) yres = 96;
            h = (float)img->h * 72.0f / (float)yres;
        }

        // 获取页面尺寸，防止超界
        float page_w = 595, page_h = 842;
        size_page = fz_load_page(fz, doc->fz_doc, page_num);
        fz_rect pr = fz_bound_page(fz, size_page);
        page_w = pr.x1 - pr.x0;
        page_h = pr.y1 - pr.y0;
        fz_drop_page(fz, size_page);
        size_page = nullptr;
        if (w > page_w) w = page_w;
        if (h > page_h) h = page_h;
        // 允许负坐标（内容可以超出页面边界），不强制限制

        int source_xres = 0, source_yres = 0;
        fz_image_resolution(img, &source_xres, &source_yres);
        LOG_EVENT("INFO", "IMAGE", "SOURCE", "doc=%p | page=%d | source=memory | format=\"%s\" | input_bytes=%zu | pixel_size=%dx%d | dpi=%d,%d | final_size=%.3fx%.3f | source_aspect=%.6f | final_aspect=%.6f", (void*)doc, page_num + 1, image_format_log, image_size, img->w, img->h, source_xres, source_yres, w, h, img->h != 0 ? (float)img->w / img->h : 0.0f, h != 0 ? w / h : 0.0f);

        page = pdf_load_page(fz, doc->pdf_doc, page_num);
        result = add_image_to_page_content(
            fz, doc->pdf_doc, page, doc, page_num, img,
            x, y, w, h, 0);
        if (result != 0) {
            fz_throw(fz, FZ_ERROR_GENERIC, "Cannot add memory image annotation");
        }
        end_edit_operation(fz, doc);
        operation_started = 0;
    } fz_always(fz) {
        if (page) pdf_drop_page(fz, page);
        if (size_page) fz_drop_page(fz, size_page);
        if (img) fz_drop_image(fz, img);
        if (buf) fz_drop_buffer(fz, buf);
    } fz_catch(fz) {
        if (operation_started) abandon_edit_operation(fz, doc);
        const char* caught_message = fz_caught_message(fz);
        LOG_EVENT("ERROR", "IMAGE", "MEMORY_END", "status=FAIL | doc=%p | page=%d | format=\"%s\" | input_bytes=%zu | ret=-3 | message=\"%s\"", (void*)doc, page_num + 1, image_format_log, image_size, caught_message);
        SET_ERROR(ctx, caught_message);
        return -3;
    }

    LOG_EVENT(result == 0 ? "INFO" : "ERROR", "IMAGE", "MEMORY_END", "status=%s | doc=%p | page=%d | format=\"%s\" | input_bytes=%zu | ret=%d", result == 0 ? "OK" : "FAIL", (void*)doc, page_num + 1, image_format_log, image_size, result);
    return result;
}

int mupdf_batch_add_image(
    MupdfContext* ctx,
    MupdfDocument* doc,
    const int* page_nums,
    int page_count,
    const char* image_path,
    LayoutRule* rule,
    float width,
    float height
) {
    if (!ctx || !doc || !image_path) return -1;

    char image_path_utf8[4096];
    log_acp_as_utf8(image_path, image_path_utf8, sizeof(image_path_utf8));
    LOG_EVENT(
        "INFO", "IMAGE", "BATCH_BEGIN",
        "doc=%p | path=\"%s\" | page_mode=%s | requested_page_count=%d | requested_size=%.3fx%.3f",
        (void*)doc, image_path_utf8,
        (page_nums && page_count > 0) ? "SELECTED" : "ALL",
        page_count, width, height);

    fz_context* fz = ctx->ctx;
    fz_image* img = nullptr;

    fz_try(fz) {
        img = fz_new_image_from_file(fz, image_path);
    } fz_catch(fz) {
        const char* caught_message = fz_caught_message(fz);
        LOG_EVENT("ERROR", "IMAGE", "BATCH_END", "status=FAIL | doc=%p | path=\"%s\" | ret=-2 | message=\"%s\"", (void*)doc, image_path_utf8, caught_message);
        SET_ERROR(ctx, caught_message);
        return -2;
    }

    // 计算图片的 PDF 点尺寸（假设 96 DPI）
    // 1 point = 1/72 inch, 1 pixel at 96dpi = 72/96 = 0.75 points
    float img_w, img_h;
    if (width > 0 && height > 0) {
        // 用户指定了尺寸（点是）
        img_w = width;
        img_h = height;
    } else if (width > 0) {
        // 只指定了宽度
        int xres = 0, yres = 0;
        fz_image_resolution(img, &xres, &yres);
        if (xres <= 0) xres = 96;
        if (yres <= 0) yres = 96;
        img_w = width;
        img_h = (float)img->h * width / (float)img->w * (float)xres / (float)yres;
    } else if (height > 0) {
        // 只指定了高度
        int xres = 0, yres = 0;
        fz_image_resolution(img, &xres, &yres);
        if (xres <= 0) xres = 96;
        if (yres <= 0) yres = 96;
        img_h = height;
        img_w = (float)img->w * height / (float)img->h * (float)yres / (float)xres;
    } else {
        // 用户没指定，使用 DPI 计算
        int xres = 0, yres = 0;
        fz_image_resolution(img, &xres, &yres);
        if (xres <= 0) xres = 96;
        if (yres <= 0) yres = 96;
        // 转换为 PDF 点数：像素数 * 72 / DPI
        img_w = (float)img->w * 72.0f / (float)xres;
        img_h = (float)img->h * 72.0f / (float)yres;
    }

    int total_pages = 0;
    mupdf_get_page_count(ctx, doc, &total_pages);

    LayoutRule default_rule;
    if (!rule) {
        memset(&default_rule, 0, sizeof(default_rule));
        default_rule.type = 1;
        default_rule.margin_x = 10;
        default_rule.margin_y = 10;
        rule = &default_rule;
    }

    // 自动回写图像尺寸，供后续 mupdf_batch_add_text 使用
    rule->image_width = img_w;
    rule->image_height = img_h;
    int source_xres = 0, source_yres = 0;
    fz_image_resolution(img, &source_xres, &source_yres);
    LOG_EVENT(
        "INFO", "IMAGE", "SOURCE",
        "doc=%p | path=\"%s\" | pixel_size=%dx%d | dpi=%d,%d | requested_size=%.3fx%.3f | "
        "final_size=%.3fx%.3f | source_aspect=%.6f | final_aspect=%.6f | aspect_diff_pct=%.3f",
        (void*)doc, image_path_utf8, img->w, img->h, source_xres, source_yres,
        width, height, img_w, img_h,
        img->h != 0 ? (float)img->w / (float)img->h : 0.0f,
        img_h != 0 ? img_w / img_h : 0.0f,
        (img->w != 0 && img->h != 0 && img_h != 0) ? ((img_w / img_h) / ((float)img->w / (float)img->h) - 1.0f) * 100.0f : 0.0f);

    int added = 0;

    // 使用实际图片宽高计算位置，确保右对齐和居中正确
    float pos_h = img_h;  // 使用图片高度
    float pos_w = img_w;  // 使用图片宽度，确保右对齐和居中正确

    if (page_nums && page_count > 0) {
        for (int i = 0; i < page_count; i++) {
            int pn = page_nums[i];
            if (pn < 0 || pn >= total_pages) {
                LOG_EVENT("WARN", "IMAGE", "PAGE_SKIP", "doc=%p | page_index=%d | reason=out_of_range | total_pages=%d", (void*)doc, pn, total_pages);
                continue;
            }
            float x = 0, y = 0;
            if (mupdf_calc_native_position(
                    ctx, doc, pn, rule, pos_w, pos_h,
                    &x, &y, nullptr) != 0) {
                continue;
            }
            int ret = mupdf_add_image_internal(
                ctx, doc, pn, image_path, x, y, img_w, img_h, 1);
            if (ret == 0) added++;
        }
    } else {
        for (int i = 0; i < total_pages; i++) {
            float x = 0, y = 0;
            if (mupdf_calc_native_position(
                    ctx, doc, i, rule, pos_w, pos_h,
                    &x, &y, nullptr) != 0) {
                continue;
            }
            int ret = mupdf_add_image_internal(
                ctx, doc, i, image_path, x, y, img_w, img_h, 1);
            if (ret == 0) added++;
        }
    }

    fz_drop_image(fz, img);
    int requested_pages = (page_nums && page_count > 0) ? page_count : total_pages;
    LOG_EVENT(
        added == requested_pages ? "INFO" : "WARN", "IMAGE", "BATCH_END",
        "status=%s | doc=%p | requested_pages=%d | total_pages=%d | added_pages=%d | failed_or_skipped=%d",
        added == requested_pages ? "OK" : (added > 0 ? "PARTIAL" : "FAIL"),
        (void*)doc, requested_pages, total_pages, added, requested_pages - added);
    return added;
}

// ============================================
// 添加文字（使用 SimSun 字体写入内容流）
// ============================================

static int mupdf_add_text_internal(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    const char* text,
    float font_size,
    float x, float y,
    float width, float height,
    int inherit_page_rotation
) {
    if (!ctx || !doc || !text) return -1;
    if (!doc->pdf_doc) { SET_ERROR(ctx, "Not a PDF document"); return -2; }

    fz_context* fz = ctx->ctx;
    float fs = font_size > 0 ? font_size : 12.0f;
    int result = 0;
    pdf_page* page = nullptr;
    int operation_started = 0;
    char text_log[4096];
    log_escape_value(text, text_log, sizeof(text_log));

    fz_var(page);
    fz_var(operation_started);

    LOG_EVENT(
        "INFO", "TEXT", "PAGE_BEGIN",
        "doc=%p | page=%d | text=\"%s\" | placement_space=%s | requested_xy=%.3f,%.3f | box_size=%.3fx%.3f | font_size=%.3f",
        (void*)doc, page_num + 1, text_log,
        inherit_page_rotation ? "NATIVE" : "VISUAL", x, y, width, height, fs);

    fz_try(fz) {
        begin_edit_operation(fz, doc, "Add text to page");
        operation_started = 1;
        page = pdf_load_page(fz, doc->pdf_doc, page_num);
        log_page_geometry(fz, page, doc, page_num, "TEXT_ADD");
        fz_rect bounds = fz_bound_page(fz, (fz_page*)page);
        PageNativeGeometry native_geometry;
        if (inherit_page_rotation) {
            load_native_page_geometry(fz, page, &native_geometry);
        }

        float placement_width = inherit_page_rotation
            ? native_geometry.native_width
            : bounds.x1 - bounds.x0;
        if (x < 0.0f) x = 0.0f;
        if (x + width > placement_width) x = placement_width - width;
        if (x < 0.0f) x = 0.0f;

        fz_matrix pdf_to_fitz;
        fz_matrix fitz_to_pdf;
        pdf_page_transform(fz, page, nullptr, &pdf_to_fitz);
        if (fz_try_invert_matrix(&fitz_to_pdf, pdf_to_fitz)) {
            fz_throw(fz, FZ_ERROR_ARGUMENT, "Cannot invert PDF page transform");
        }

        fz_point visual_p0;
        fz_point visual_p1;
        fz_point visual_p2;
        if (inherit_page_rotation) {
            visual_p0 = fz_transform_point_xy(x, y, native_geometry.native_to_visual);
            visual_p1 = fz_transform_point_xy(x + 1.0f, y, native_geometry.native_to_visual);
            visual_p2 = fz_transform_point_xy(x, y - 1.0f, native_geometry.native_to_visual);
        } else {
            float visual_x = bounds.x0 + x;
            float visual_y = bounds.y0 + y;
            visual_p0 = fz_make_point(visual_x, visual_y);
            visual_p1 = fz_make_point(visual_x + 1.0f, visual_y);
            visual_p2 = fz_make_point(visual_x, visual_y - 1.0f);
        }

        fz_point p0 = fz_transform_point(visual_p0, fitz_to_pdf);
        fz_point p1 = fz_transform_point(visual_p1, fitz_to_pdf);
        fz_point p2 = fz_transform_point(visual_p2, fitz_to_pdf);

        fz_matrix text_matrix = fz_make_matrix(
            p1.x - p0.x,
            p1.y - p0.y,
            p2.x - p0.x,
            p2.y - p0.y,
            p0.x,
            p0.y);

        fz_point roundtrip = fz_transform_point(p0, pdf_to_fitz);
        LOG_EVENT(
            "INFO", "TEXT", "MATRIX",
            "doc=%p | page=%d | text=\"%s\" | placement_space=%s | rotate=%d | "
            "visual_baseline=%.6f,%.6f | visual_direction=%.6f,%.6f | pdf_baseline=%.6f,%.6f | "
            "pdf_to_fitz=[%.6f,%.6f,%.6f,%.6f,%.6f,%.6f] | "
            "fitz_to_pdf=[%.6f,%.6f,%.6f,%.6f,%.6f,%.6f] | "
            "tm=[%.6f,%.6f,%.6f,%.6f,%.6f,%.6f] | roundtrip_visual=%.6f,%.6f | roundtrip_error=%.6f,%.6f",
            (void*)doc, page_num + 1, text_log,
            inherit_page_rotation ? "NATIVE" : "VISUAL",
            inherit_page_rotation ? native_geometry.rotate : 0,
            visual_p0.x, visual_p0.y,
            visual_p1.x - visual_p0.x, visual_p1.y - visual_p0.y,
            p0.x, p0.y,
            pdf_to_fitz.a, pdf_to_fitz.b, pdf_to_fitz.c, pdf_to_fitz.d, pdf_to_fitz.e, pdf_to_fitz.f,
            fitz_to_pdf.a, fitz_to_pdf.b, fitz_to_pdf.c, fitz_to_pdf.d, fitz_to_pdf.e, fitz_to_pdf.f,
            text_matrix.a, text_matrix.b, text_matrix.c, text_matrix.d, text_matrix.e, text_matrix.f,
            roundtrip.x, roundtrip.y,
            roundtrip.x - visual_p0.x, roundtrip.y - visual_p0.y);

        result = add_text_direct_to_page(
            fz, doc->pdf_doc, page, ctx, doc,
            text, 0.0f, 0.0f, fs, 0.0f, 0.0f, 0.0f, &text_matrix);
        if (result != 0) {
            fz_throw(fz, FZ_ERROR_GENERIC, "Cannot write text content stream");
        }
        end_edit_operation(fz, doc);
        operation_started = 0;
    } fz_always(fz) {
        if (page) pdf_drop_page(fz, page);
    } fz_catch(fz) {
        if (operation_started) abandon_edit_operation(fz, doc);
        reset_simsun_pdf_font(fz, doc);
        const char* caught_message = fz_caught_message(fz);
        LOG_EVENT("ERROR", "TEXT", "PAGE_END", "status=FAIL | doc=%p | page=%d | text=\"%s\" | ret=-3 | message=\"%s\"", (void*)doc, page_num + 1, text_log, caught_message);
        SET_ERROR(ctx, caught_message);
        return -3;
    }

    LOG_EVENT(result == 0 ? "INFO" : "ERROR", "TEXT", "PAGE_END", "status=%s | doc=%p | page=%d | text=\"%s\" | ret=%d", result == 0 ? "OK" : "FAIL", (void*)doc, page_num + 1, text_log, result);
    return result;
}

int mupdf_add_text(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    const char* text,
    float font_size,
    float x, float y,
    float width, float height
) {
    return mupdf_add_text_internal(
        ctx, doc, page_num, text, font_size,
        x, y, width, height, 0);
}

int mupdf_add_text_with_style(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    const char* text,
    const TextStyle* style,
    float x, float y,
    float width, float height
) {
    if (!ctx || !doc || !text) return -1;
    if (!doc->pdf_doc) { SET_ERROR(ctx, "Not a PDF document"); return -2; }

    fz_context* fz = ctx->ctx;
    float fs = (style && style->font_size > 0) ? style->font_size : 12.0f;
    float cr = 0.0f, cg = 0.0f, cb = 0.0f;
    if (style) {
        cr = style->color_r;
        cg = style->color_g;
        cb = style->color_b;
    }
    int result = 0;
    pdf_page* page = nullptr;
    int operation_started = 0;
    char text_log[4096];

    fz_var(page);
    fz_var(operation_started);

    log_escape_value(text, text_log, sizeof(text_log));
    LOG_EVENT("INFO", "TEXT", "STYLE_BEGIN", "doc=%p | page=%d | text=\"%s\" | utf8_valid=%s | visual_xy=%.3f,%.3f | box_size=%.3fx%.3f | font_size=%.3f | color=%.3f,%.3f,%.3f | style_ptr=%p", (void*)doc, page_num + 1, text_log, MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0) > 0 ? "true" : "false", x, y, width, height, fs, cr, cg, cb, (void*)style);

    fz_try(fz) {
        begin_edit_operation(fz, doc, "Add styled text to page");
        operation_started = 1;
        page = pdf_load_page(fz, doc->pdf_doc, page_num);
        log_page_geometry(fz, page, doc, page_num, "TEXT_STYLE");
        fz_rect bounds = fz_bound_page(fz, (fz_page*)page);
        float page_w = bounds.x1 - bounds.x0;
        if (x < 0) x = 0;
        if (x + width > page_w) x = page_w - width;
        if (x < 0) x = 0;

        fz_matrix pdf_to_fitz;
        fz_matrix fitz_to_pdf;
        pdf_page_transform(fz, page, nullptr, &pdf_to_fitz);
        if (fz_try_invert_matrix(&fitz_to_pdf, pdf_to_fitz)) {
            fz_throw(fz, FZ_ERROR_ARGUMENT, "Cannot invert PDF page transform");
        }

        float visual_x = bounds.x0 + x;
        float visual_y = bounds.y0 + y;
        fz_point p0 = fz_transform_point_xy(visual_x, visual_y, fitz_to_pdf);
        fz_point p1 = fz_transform_point_xy(visual_x + 1.0f, visual_y, fitz_to_pdf);
        fz_point p2 = fz_transform_point_xy(visual_x, visual_y - 1.0f, fitz_to_pdf);
        fz_matrix text_matrix = fz_make_matrix(
            p1.x - p0.x,
            p1.y - p0.y,
            p2.x - p0.x,
            p2.y - p0.y,
            p0.x,
            p0.y);

        result = add_text_direct_to_page(
            fz, doc->pdf_doc, page, ctx, doc,
            text, 0.0f, 0.0f, fs, cr, cg, cb, &text_matrix);
        if (result != 0) {
            fz_throw(fz, FZ_ERROR_GENERIC, "Cannot write styled text content stream");
        }
        end_edit_operation(fz, doc);
        operation_started = 0;
    } fz_always(fz) {
        if (page) pdf_drop_page(fz, page);
    } fz_catch(fz) {
        if (operation_started) abandon_edit_operation(fz, doc);
        reset_simsun_pdf_font(fz, doc);
        const char* caught_message = fz_caught_message(fz);
        LOG_EVENT("ERROR", "TEXT", "STYLE_END", "status=FAIL | doc=%p | page=%d | ret=-3 | message=\"%s\"", (void*)doc, page_num + 1, caught_message);
        SET_ERROR(ctx, caught_message);
        return -3;
    }

    LOG_EVENT(result == 0 ? "INFO" : "ERROR", "TEXT", "STYLE_END", "status=%s | doc=%p | page=%d | ret=%d", result == 0 ? "OK" : "FAIL", (void*)doc, page_num + 1, result);
    return result;
}

// ============================================
// 内部辅助：估算 UTF-8 文字在指定字号下的显示宽度（pt）
// 用于设置文本框宽度，防止文字自动换行
// ============================================
static float estimate_text_width(fz_context* fz, fz_font* font, const char* utf8_text, float font_size) {
    if (!utf8_text) return 0;
    if (font) return measure_text_width_simsun(fz, font, utf8_text, font_size) + font_size * 0.5f;

    float width = 0;
    const unsigned char* p = (const unsigned char*)utf8_text;
    while (*p) {
        if (*p < 0x80) {
            width += font_size * 0.70f;
            p++;
        } else if ((*p & 0xE0) == 0xC0) {
            width += font_size * 0.70f;
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            width += font_size * 1.2f;
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) {
            width += font_size * 1.2f;
            p += 4;
        } else {
            p++;
        }
    }
    return width + font_size * 0.5f;
}

MUPDF_API int mupdf_batch_add_text(
    MupdfContext* ctx,
    MupdfDocument* doc,
    const int* page_nums,
    int page_count,
    char* text,
    float font_size,
    LayoutRule* rule
) {
    if (!ctx || !doc || !text) return -1;

    int total_pages = 0;
    mupdf_get_page_count(ctx, doc, &total_pages);
    if (total_pages <= 0) total_pages = 1;

    LayoutRule default_rule;
    if (!rule) {
        memset(&default_rule, 0, sizeof(default_rule));
        default_rule.type = 1;
        default_rule.margin_x = 10;
        default_rule.margin_y = 10;
        default_rule.text_width = 0;
        default_rule.add_page_number = 1;
        rule = &default_rule;
    }

    int added = 0;
    fz_var(added);
    float txt_h = font_size * 1.5f + 4;
    int add_page_num = rule->add_page_number;

    size_t acp_input_size = strlen(text);
    char acp_text_utf8[4096];
    char acp_hex[260];
    log_acp_as_utf8(text, acp_text_utf8, sizeof(acp_text_utf8));
    log_hex_bytes((const unsigned char*)text, acp_input_size, acp_hex, sizeof(acp_hex));
    LOG_EVENT(
        "INFO", "TEXT", "BATCH_BEGIN",
        "doc=%p | page_mode=%s | requested_page_count=%d | total_pages=%d | position_id=%d | position=%s | "
        "font_size=%.3f | add_page_number=%d | acp=%u | input_bytes=%zu | input_text=\"%s\" | input_hex=%s",
        (void*)doc, (page_nums && page_count > 0) ? "SELECTED" : "ALL", page_count, total_pages,
        rule->type, log_position_name(rule->type), font_size, add_page_num, GetACP(), acp_input_size,
        acp_text_utf8, acp_hex);

			int len = MultiByteToWideChar(CP_ACP,0,(LPCTSTR)text,-1,NULL,0);
			int acp_query_len = len;
			if (len <= 0) {
				SET_ERROR(ctx, "Cannot query barcode text ACP length");
				return -2;
			}
			WCHAR * wszUtf8 = (WCHAR*)malloc(((size_t)len + 1) * sizeof(WCHAR));
			if (!wszUtf8) {
				SET_ERROR(ctx, "Cannot allocate barcode text wide buffer");
				return -2;
			}
			memset(wszUtf8, 0, ((size_t)len + 1) * sizeof(WCHAR));
			int acp_convert_len = MultiByteToWideChar(CP_ACP,0,(LPCTSTR)text,-1,wszUtf8,len);
			DWORD acp_error = acp_convert_len > 0 ? ERROR_SUCCESS : GetLastError();
			if (acp_convert_len <= 0) {
				free(wszUtf8);
				SET_ERROR(ctx, "Cannot convert barcode text from ACP");
				return -2;
			}

			len = WideCharToMultiByte(CP_UTF8,0,wszUtf8,-1,NULL,0,NULL,NULL);
			int utf8_query_len = len;
			if (len <= 0) {
				free(wszUtf8);
				SET_ERROR(ctx, "Cannot query barcode text UTF-8 length");
				return -2;
			}
			char * szcaInBarCodeDesc = (char*)malloc((size_t)len + 1);
			if (!szcaInBarCodeDesc) {
				free(wszUtf8);
				SET_ERROR(ctx, "Cannot allocate barcode text UTF-8 buffer");
				return -2;
			}
			memset(szcaInBarCodeDesc,0,(size_t)len+1);
			int utf8_convert_len = WideCharToMultiByte(CP_UTF8,0,wszUtf8,-1,szcaInBarCodeDesc,len,NULL,NULL);
			DWORD utf8_error = utf8_convert_len > 0 ? ERROR_SUCCESS : GetLastError();
			free(wszUtf8);
			wszUtf8 = NULL;
			if (utf8_convert_len <= 0) {
				free(szcaInBarCodeDesc);
				SET_ERROR(ctx, "Cannot convert barcode text to UTF-8");
				return -2;
			}
			text=szcaInBarCodeDesc;

    char utf8_text_log[4096];
    char utf8_hex[260];
    log_escape_value(text, utf8_text_log, sizeof(utf8_text_log));
    log_hex_bytes((const unsigned char*)text, strlen(text), utf8_hex, sizeof(utf8_hex));
    int utf8_valid = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0) > 0;
    LOG_EVENT(
        (acp_convert_len > 0 && utf8_convert_len > 0 && utf8_valid) ? "INFO" : "WARN",
        "TEXT", "ENCODING",
        "doc=%p | acp=%u | input_bytes=%zu | acp_query_len=%d | acp_convert_len=%d | acp_error=%lu | "
        "utf8_query_len=%d | utf8_convert_len=%d | utf8_error=%lu | utf8_valid=%s | utf8_text=\"%s\" | utf8_hex=%s",
        (void*)doc, GetACP(), acp_input_size, acp_query_len, acp_convert_len, acp_error,
        utf8_query_len, utf8_convert_len, utf8_error, utf8_valid ? "true" : "false", utf8_text_log, utf8_hex);

    fz_context* fz = ctx->ctx;
    fz_try(fz) {
    if (page_nums && page_count > 0) {
        for (int i = 0; i < page_count; i++) {
            int pn = page_nums[i];
            if (pn < 0 || pn >= total_pages) {
                LOG_EVENT("WARN", "TEXT", "PAGE_SKIP", "doc=%p | page_index=%d | reason=out_of_range | total_pages=%d", (void*)doc, pn, total_pages);
                continue;
            }

            // 先构建最终文本（可能带页码）
            char full_text[512];
            if (add_page_num) {
                snprintf(full_text, sizeof(full_text), "%s-%d/%d", text, pn + 1, total_pages);
            } else {
                snprintf(full_text, sizeof(full_text), "%s", text);
            }

            PageNativeGeometry geometry;
            float img_x = 0.0f, img_y = 0.0f;
            if (mupdf_calc_native_position(
                    ctx, doc, pn, rule,
                    rule->image_width, rule->image_height,
                    &img_x, &img_y, &geometry) != 0) {
                fz_throw(fz, FZ_ERROR_GENERIC, "Cannot calculate native batch text position");
            }
            float page_w = geometry.native_width;
            float page_h = geometry.native_height;

			float txt_w = estimate_text_width(ctx->ctx, get_simsun_font(ctx->ctx, ctx), full_text, font_size);
			float max_w = page_w - 2 * rule->margin_x;
		
			if (txt_w > max_w) txt_w = max_w;
			if (txt_w < 50) txt_w = 50;	
            //float txt_w = page_w - rule->margin_x;  // 页面宽度减去左边距
            //if (txt_w < 50) txt_w = 50;
            float x = img_x;
            float y = img_y + rule->image_height + rule->text_gap;
			//float y;
			//if(rule->type ==2 || rule->type == 3||rule->type ==5){
			//	y = img_y + txt_h + rule->text_gap;
			//}else{
			//	y = img_y + rule->image_height + rule->text_gap;
			//}
            char full_text_log[4096];
            log_escape_value(full_text, full_text_log, sizeof(full_text_log));
            float expected_text_y = img_y + rule->image_height + rule->text_gap;
            int text_inside_page = x >= 0 && y >= 0 && x + txt_w <= page_w && y + txt_h <= page_h;
            LOG_EVENT(
                text_inside_page ? "INFO" : "WARN", "TEXT", "PAGE_PLAN",
                "doc=%p | page=%d | text=\"%s\" | placement_space=NATIVE | rotate=%d | "
                "page_size=%.3fx%.3f | font_size=%.3f | text_size=%.3fx%.3f | "
                "image_pos=%.3f,%.3f | image_size=%.3fx%.3f | gap=%.3f | text_pos=%.3f,%.3f | "
                "align_dx=%.6f | below_dy=%.6f | inside_page=%s",
                (void*)doc, pn + 1, full_text_log, geometry.rotate,
                page_w, page_h, font_size, txt_w, txt_h,
                img_x, img_y, rule->image_width, rule->image_height, rule->text_gap, x, y,
                x - img_x, y - expected_text_y, text_inside_page ? "true" : "false");

			int text_ret = mupdf_add_text_internal(
                ctx, doc, pn, full_text, font_size,
                x, y, txt_w, txt_h, 1);
            if(text_ret == 0) added++;
			/*
            {
                pdf_page* pdf_pg = pdf_load_page(ctx->ctx, doc->pdf_doc, pn);
                float pdf_y = page_h - y - font_size;
                add_text_direct_to_page(ctx->ctx, doc->pdf_doc, pdf_pg, full_text,
                    x, pdf_y, font_size, 0.0f, 0.0f, 0.0f);
				
                pdf_drop_page(ctx->ctx, pdf_pg);
                added++;
            }
			*/
        }
    } else {
        for (int i = 0; i < total_pages; i++) {
            // 先构建最终文本（可能带页码）
            char full_text[512];
            if (add_page_num) {
                snprintf(full_text, sizeof(full_text), "%s-%d/%d", text, i + 1, total_pages);
            } else {
                snprintf(full_text, sizeof(full_text), "%s", text);
            }

            PageNativeGeometry geometry;
            float img_x = 0.0f, img_y = 0.0f;
            if (mupdf_calc_native_position(
                    ctx, doc, i, rule,
                    rule->image_width, rule->image_height,
                    &img_x, &img_y, &geometry) != 0) {
                fz_throw(fz, FZ_ERROR_GENERIC, "Cannot calculate native batch text position");
            }
            float page_w = geometry.native_width;
            float page_h = geometry.native_height;

			float txt_w = estimate_text_width(ctx->ctx, get_simsun_font(ctx->ctx, ctx), full_text, font_size);
			float max_w = page_w - 2 * rule->margin_x;
		
			if (txt_w > max_w) txt_w = max_w;
			if (txt_w < 50) txt_w = 50;	
            //float txt_w = page_w - rule->margin_x;  // 页面宽度减去左边距
            //if (txt_w < 50) txt_w = 50;

            float x = img_x;
            float y = img_y + rule->image_height + rule->text_gap;
			//float y;
			//if(rule->type ==2 || rule->type == 3||rule->type ==5){
			//	y = img_y - txt_h - rule->text_gap;
			//}else{
			//	y = img_y + rule->image_height+rule->text_gap;
			//}
            char full_text_log[4096];
            log_escape_value(full_text, full_text_log, sizeof(full_text_log));
            float expected_text_y = img_y + rule->image_height + rule->text_gap;
            int text_inside_page = x >= 0 && y >= 0 && x + txt_w <= page_w && y + txt_h <= page_h;
            LOG_EVENT(
                text_inside_page ? "INFO" : "WARN", "TEXT", "PAGE_PLAN",
                "doc=%p | page=%d | text=\"%s\" | placement_space=NATIVE | rotate=%d | "
                "page_size=%.3fx%.3f | font_size=%.3f | text_size=%.3fx%.3f | "
                "image_pos=%.3f,%.3f | image_size=%.3fx%.3f | gap=%.3f | text_pos=%.3f,%.3f | "
                "align_dx=%.6f | below_dy=%.6f | inside_page=%s",
                (void*)doc, i + 1, full_text_log, geometry.rotate,
                page_w, page_h, font_size, txt_w, txt_h,
                img_x, img_y, rule->image_width, rule->image_height, rule->text_gap, x, y,
                x - img_x, y - expected_text_y, text_inside_page ? "true" : "false");

			int text_ret = mupdf_add_text_internal(
                ctx, doc, i, full_text, font_size,
                x, y, txt_w, txt_h, 1);
            if(text_ret == 0) added++;
			/*
            {
                pdf_page* pdf_pg = pdf_load_page(ctx->ctx, doc->pdf_doc, i);
                float pdf_y = page_h - y - font_size;
                add_text_direct_to_page(ctx->ctx, doc->pdf_doc, pdf_pg, full_text,
                    x, pdf_y, font_size, 0.0f, 0.0f, 0.0f);
                pdf_drop_page(ctx->ctx, pdf_pg);
                added++;
            }
			*/
        }
    }
    } fz_always(fz) {
        free(szcaInBarCodeDesc);
    } fz_catch(fz) {
        const char* message = fz_caught_message(fz);
        LOG_EVENT("ERROR", "TEXT", "BATCH_END", "status=FAIL | doc=%p | added_pages=%d | message=\"%s\"", (void*)doc, added, message);
        SET_ERROR(ctx, message);
        return -2;
    }
    int requested_pages = (page_nums && page_count > 0) ? page_count : total_pages;
    LOG_EVENT(
        added == requested_pages ? "INFO" : "WARN", "TEXT", "BATCH_END",
        "status=%s | doc=%p | requested_pages=%d | total_pages=%d | added_pages=%d | failed_or_skipped=%d",
        added == requested_pages ? "OK" : (added > 0 ? "PARTIAL" : "FAIL"),
        (void*)doc, requested_pages, total_pages, added, requested_pages - added);
    return added;
}

typedef struct BarcodeLabelPlan_s {
    int page_num;
    float image_x;
    float image_y;
    float text_x;
    float text_baseline_y;
    float text_width;
    float text_height;
    float group_width;
    float group_height;
    char text[1024];
} BarcodeLabelPlan;

static char* barcode_text_to_utf8(const char* text) {
    if (!text) return nullptr;

    int wide_len = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (wide_len <= 0) return nullptr;

    WCHAR* wide = (WCHAR*)malloc((size_t)wide_len * sizeof(WCHAR));
    if (!wide) return nullptr;

    if (MultiByteToWideChar(CP_ACP, 0, text, -1, wide, wide_len) <= 0) {
        free(wide);
        return nullptr;
    }

    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 0) {
        free(wide);
        return nullptr;
    }

    char* utf8 = (char*)malloc((size_t)utf8_len);
    if (!utf8) {
        free(wide);
        return nullptr;
    }

    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, utf8_len, nullptr, nullptr) <= 0) {
        free(utf8);
        free(wide);
        return nullptr;
    }

    free(wide);
    return utf8;
}

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
) {
    if (!ctx || !doc || !doc->pdf_doc || !image_path || !text || !rule ||
        !_finite(font_size) || !_finite(image_width) || !_finite(image_height) ||
        font_size <= 0.0f || image_width < 0.0f || image_height < 0.0f) {
        if (ctx) SET_ERROR(ctx, "Invalid barcode label parameters");
        return -1;
    }
    if (rule->type < 0 || rule->type > 5 ||
        !_finite(rule->margin_x) || !_finite(rule->margin_y) || !_finite(rule->text_gap) ||
        rule->text_gap < 0.0f) {
        SET_ERROR(ctx, "Invalid barcode label layout rule");
        return -1;
    }

    int selected_mode = page_nums != nullptr;
    if ((selected_mode && page_count <= 0) || (!selected_mode && page_count != -1)) {
        SET_ERROR(ctx, "Invalid barcode label page selection");
        return -1;
    }

    int total_pages = 0;
    if (mupdf_get_page_count(ctx, doc, &total_pages) != 0 || total_pages <= 0) {
        SET_ERROR(ctx, "Cannot get page count for barcode label");
        return -2;
    }

    int plan_count = selected_mode ? page_count : total_pages;
    if (plan_count <= 0) {
        SET_ERROR(ctx, "No pages selected for barcode label");
        return -1;
    }

    char image_path_log[4096];
    char text_log[4096];
    log_acp_as_utf8(image_path, image_path_log, sizeof(image_path_log));
    log_acp_as_utf8(text, text_log, sizeof(text_log));
    LOG_EVENT(
        "INFO", "BARCODE", "LABEL_BEGIN",
        "doc=%p | page_mode=%s | requested_page_count=%d | total_pages=%d | image=\"%s\" | text=\"%s\" | "
        "font_size=%.3f | position_id=%d | position=%s | requested_image_size=%.3fx%.3f",
        (void*)doc, selected_mode ? "SELECTED" : "ALL", page_count, total_pages,
        image_path_log, text_log, font_size, rule->type, log_position_name(rule->type), image_width, image_height);

    fz_context* fz = ctx->ctx;
    fz_image* image = nullptr;
    fz_try(fz) {
        image = fz_new_image_from_file(fz, image_path);
    } fz_catch(fz) {
        const char* message = fz_caught_message(fz);
        LOG_EVENT("ERROR", "BARCODE", "LABEL_END", "status=FAIL | stage=IMAGE_LOAD | doc=%p | message=\"%s\"", (void*)doc, message);
        SET_ERROR(ctx, message);
        return -2;
    }

    float resolved_image_width = image_width;
    float resolved_image_height = image_height;
    int xres = 0;
    int yres = 0;
    fz_image_resolution(image, &xres, &yres);
    if (xres <= 0) xres = 96;
    if (yres <= 0) yres = 96;

    if (resolved_image_width > 0.0f && resolved_image_height <= 0.0f) {
        resolved_image_height = (float)image->h * resolved_image_width / (float)image->w * (float)xres / (float)yres;
    } else if (resolved_image_height > 0.0f && resolved_image_width <= 0.0f) {
        resolved_image_width = (float)image->w * resolved_image_height / (float)image->h * (float)yres / (float)xres;
    } else if (resolved_image_width <= 0.0f && resolved_image_height <= 0.0f) {
        resolved_image_width = (float)image->w * 72.0f / (float)xres;
        resolved_image_height = (float)image->h * 72.0f / (float)yres;
    }
    fz_drop_image(fz, image);
    image = nullptr;

    if (!_finite(resolved_image_width) || !_finite(resolved_image_height) ||
        resolved_image_width <= 0.0f || resolved_image_height <= 0.0f) {
        SET_ERROR(ctx, "Invalid resolved barcode image size");
        return -1;
    }

    char* utf8_text = barcode_text_to_utf8(text);
    if (!utf8_text) {
        SET_ERROR(ctx, "Cannot convert barcode text to UTF-8");
        return -2;
    }

    fz_font* font = get_simsun_font(fz, ctx);
    if (!font) {
        free(utf8_text);
        SET_ERROR(ctx, "Cannot load SimSun for barcode label");
        return -2;
    }

    float ascender = 0.0f;
    float descender = 0.0f;
    float text_height = 0.0f;
    float baseline_offset = 0.0f;
    fz_var(ascender);
    fz_var(descender);
    fz_var(text_height);
    fz_var(baseline_offset);
    fz_try(fz) {
        fz_calculate_font_ascender_descender(fz, font);
        ascender = fz_font_ascender(fz, font);
        descender = fz_font_descender(fz, font);
        text_height = (ascender - descender) * font_size;
        baseline_offset = ascender * font_size;
    } fz_catch(fz) {
        const char* message = fz_caught_message(fz);
        SET_ERROR(ctx, message);
        free(utf8_text);
        return -2;
    }
    if (!_finite(ascender) || !_finite(descender) || ascender <= descender) {
        free(utf8_text);
        SET_ERROR(ctx, "Invalid SimSun font metrics");
        return -2;
    }
    if (!_finite(text_height) || !_finite(baseline_offset) || text_height <= 0.0f) {
        free(utf8_text);
        SET_ERROR(ctx, "Invalid barcode text dimensions");
        return -2;
    }

    BarcodeLabelPlan* plans = (BarcodeLabelPlan*)calloc((size_t)plan_count, sizeof(BarcodeLabelPlan));
    if (!plans) {
        free(utf8_text);
        SET_ERROR(ctx, "Cannot allocate barcode label plans");
        return -2;
    }

    for (int i = 0; i < plan_count; ++i) {
        int page_num = selected_mode ? page_nums[i] : i;
        if (page_num < 0 || page_num >= total_pages) {
            free(plans);
            free(utf8_text);
            SET_ERROR(ctx, "Barcode label page number is out of range");
            return -1;
        }

        BarcodeLabelPlan* plan = &plans[i];
        plan->page_num = page_num;
        int text_len;
        if (rule->add_page_number) {
            text_len = snprintf(plan->text, sizeof(plan->text), "%s-%d/%d", utf8_text, page_num + 1, total_pages);
        } else {
            text_len = snprintf(plan->text, sizeof(plan->text), "%s", utf8_text);
        }
        if (text_len <= 0 || text_len >= (int)sizeof(plan->text)) {
            free(plans);
            free(utf8_text);
            SET_ERROR(ctx, "Barcode label text is too long");
            return -1;
        }

        fz_try(fz) {
            plan->text_width = measure_text_width_simsun(fz, font, plan->text, font_size);
        } fz_catch(fz) {
            const char* message = fz_caught_message(fz);
            SET_ERROR(ctx, message);
            free(plans);
            free(utf8_text);
            return -2;
        }
        plan->text_height = text_height;
        plan->group_width = resolved_image_width > plan->text_width ? resolved_image_width : plan->text_width;
        plan->group_height = resolved_image_height + rule->text_gap + text_height;
        if (!_finite(plan->group_width) || !_finite(plan->group_height) || plan->group_width <= 0.0f || plan->group_height <= 0.0f) {
            free(plans);
            free(utf8_text);
            SET_ERROR(ctx, "Invalid barcode label group dimensions");
            return -1;
        }

        PageNativeGeometry geometry;
        float group_x = 0.0f;
        float group_y = 0.0f;
        if (mupdf_calc_native_position(
                ctx, doc, page_num, rule,
                plan->group_width, plan->group_height,
                &group_x, &group_y, &geometry) != 0 ||
            !_finite(group_x) || !_finite(group_y) ||
            group_x < 0.0f || group_y < 0.0f ||
            group_x + plan->group_width > geometry.native_width ||
            group_y + plan->group_height > geometry.native_height) {
            free(plans);
            free(utf8_text);
            SET_ERROR(ctx, "Barcode label group is outside the native page");
            return -3;
        }

        plan->image_x = group_x;
        plan->image_y = group_y;
        plan->text_x = group_x;
        plan->text_baseline_y = group_y + resolved_image_height + rule->text_gap + baseline_offset;
        if (!_finite(plan->text_baseline_y)) {
            free(plans);
            free(utf8_text);
            SET_ERROR(ctx, "Invalid barcode text baseline");
            return -3;
        }

        char plan_text_log[4096];
        log_escape_value(plan->text, plan_text_log, sizeof(plan_text_log));
        LOG_EVENT(
            "INFO", "BARCODE", "LABEL_PLAN",
            "doc=%p | page=%d | text=\"%s\" | placement_space=NATIVE | rotate=%d | "
            "page_size=%.3fx%.3f | group=[%.3f,%.3f,%.3f,%.3f] | "
            "image=[%.3f,%.3f,%.3f,%.3f] | text_baseline=%.3f,%.3f | text_size=%.3fx%.3f | inside_page=true",
            (void*)doc, page_num + 1, plan_text_log, geometry.rotate,
            geometry.native_width, geometry.native_height,
            group_x, group_y, plan->group_width, plan->group_height,
            plan->image_x, plan->image_y, resolved_image_width, resolved_image_height,
            plan->text_x, plan->text_baseline_y, plan->text_width, plan->text_height);
    }

    free(utf8_text);
    utf8_text = nullptr;

    int added = 0;
    int write_error = 0;
    int failed_page = -1;
    int operation_started = 0;
    fz_var(added);
    fz_var(write_error);
    fz_var(failed_page);
    fz_var(operation_started);

    fz_try(fz) {
        begin_edit_operation(fz, doc, "Add barcode label batch");
        operation_started = 1;
        if (!get_simsun_pdf_font(fz, ctx, doc)) {
            fz_throw(fz, FZ_ERROR_GENERIC, "Cannot embed SimSun for barcode label");
        }
        for (int i = 0; i < plan_count; ++i) {
            BarcodeLabelPlan* plan = &plans[i];
            int image_ret = mupdf_add_image_internal(
                ctx, doc, plan->page_num, image_path,
                plan->image_x, plan->image_y,
                resolved_image_width, resolved_image_height, 1);
            if (image_ret != 0) {
                write_error = -4;
                failed_page = plan->page_num;
                break;
            }

            int text_ret = mupdf_add_text_internal(
                ctx, doc, plan->page_num, plan->text, font_size,
                plan->text_x, plan->text_baseline_y,
                plan->text_width, plan->text_height, 1);
            if (text_ret != 0) {
                write_error = -5;
                failed_page = plan->page_num;
                break;
            }
            ++added;
        }

        if (write_error != 0) {
            abandon_edit_operation(fz, doc);
        } else {
            end_edit_operation(fz, doc);
        }
        operation_started = 0;
    } fz_catch(fz) {
        if (operation_started) abandon_edit_operation(fz, doc);
        reset_simsun_pdf_font(fz, doc);
        const char* message = fz_caught_message(fz);
        LOG_EVENT("ERROR", "BARCODE", "LABEL_END", "status=FAIL | stage=PDF_OPERATION | doc=%p | message=\"%s\"", (void*)doc, message);
        SET_ERROR(ctx, message);
        free(plans);
        return -6;
    }

    if (write_error != 0) {
        reset_simsun_pdf_font(fz, doc);
        LOG_EVENT("ERROR", "BARCODE", "LABEL_END", "status=FAIL | stage=%s | doc=%p | page=%d | ret=%d | added=%d | rolled_back=true", write_error == -4 ? "IMAGE_ADD" : "TEXT_ADD", (void*)doc, failed_page + 1, write_error, added);
        free(plans);
        return write_error;
    }

    free(plans);
    LOG_EVENT("INFO", "BARCODE", "LABEL_END", "status=OK | doc=%p | requested_pages=%d | added_pages=%d", (void*)doc, plan_count, added);
    return added;
}

// ============================================
// 渲染
// ============================================

// 内部持有 pixmap 的渲染结果容器
struct RenderResult {
    fz_pixmap* pix;
    fz_context* ctx;
    int width;
    int height;
};

int mupdf_render_page(
    MupdfContext* ctx,
    MupdfDocument* doc,
    int page_num,
    int dpi,
    int alpha,
    int* width_out,
    int* height_out,
    unsigned char** data_out,
    size_t* data_size_out
) {
    if (!ctx || !doc || !width_out || !height_out || !data_out || !data_size_out) return -1;

    fz_context* fz = ctx->ctx;
    fz_pixmap* pix = nullptr;

    fz_page* page = nullptr;
    fz_device* dev = nullptr;

    LOG_EVENT("INFO", "RENDER", "BEGIN", "doc=%p | page=%d | dpi=%d | alpha=%d", (void*)doc, page_num + 1, dpi, alpha);

    fz_try(fz) {
        page = fz_load_page(fz, doc->fz_doc, page_num);
        if (doc->pdf_doc) log_page_geometry(fz, (pdf_page*)page, doc, page_num, "RENDER");
        fz_rect rect = fz_bound_page(fz, page);

        float scale = (float)dpi / 72.0f;
        fz_matrix ctm = fz_scale(scale, scale);
        fz_irect irect = fz_round_rect(fz_transform_rect(rect, ctm));

        LOG_EVENT(
            "DEBUG", "RENDER", "TARGET",
            "doc=%p | page=%d | page_bounds=[%.3f,%.3f,%.3f,%.3f] | scale=%.6f | pixel_bounds=[%d,%d,%d,%d] | expected_pixels=%dx%d",
            (void*)doc, page_num + 1, rect.x0, rect.y0, rect.x1, rect.y1, scale,
            irect.x0, irect.y0, irect.x1, irect.y1, irect.x1 - irect.x0, irect.y1 - irect.y0);

        pix = fz_new_pixmap_with_bbox(fz, fz_device_rgb(fz), irect, nullptr, alpha ? 1 : 0);
        fz_clear_pixmap_with_value(fz, pix, alpha ? 0 : 255);

        dev = fz_new_draw_device(fz, ctm, pix);
        fz_run_page(fz, page, dev, fz_identity, nullptr);
        fz_close_device(fz, dev);
        fz_drop_device(fz, dev);
        dev = nullptr;
        fz_drop_page(fz, page);
        page = nullptr;
    } fz_always(fz) {
        // 确保异常路径下也释放 page 和 device，防止每页泄漏累积
        if (dev) fz_drop_device(fz, dev);
        if (page) fz_drop_page(fz, page);
    } fz_catch(fz) {
        if (pix) fz_drop_pixmap(fz, pix);
        pix = nullptr;
        const char* caught_message = fz_caught_message(fz);
        LOG_EVENT("ERROR", "RENDER", "END", "status=FAIL | doc=%p | page=%d | ret=-2 | message=\"%s\"", (void*)doc, page_num + 1, caught_message);
        SET_ERROR(ctx, caught_message);
        return -2;
    }

    // 获取 pixmap 实际参数
    int src_n      = pix->n;       // 实际通道数：RGB=3, RGBA=4
    int src_stride = pix->stride;  // 实际每行字节数
    int pw         = pix->w;
    int ph         = pix->h;
    const unsigned char* src_samples = fz_pixmap_samples(fz, pix);

    // 检查首像素色彩
    int chk_r = src_samples ? src_samples[0] : -1;
    int chk_g = src_samples && src_n > 1 ? src_samples[1] : -1;
    int chk_b = src_samples && src_n > 2 ? src_samples[2] : -1;
    LOG_EVENT(
        src_stride == pw * src_n ? "INFO" : "WARN", "RENDER", "PIXMAP",
        "doc=%p | page=%d | pixel_size=%dx%d | channels=%d | stride=%d | expected_interleaved_stride=%d | "
        "stride_matches=%s | estimated_source_mb=%.3f | first_pixel=%d,%d,%d",
        (void*)doc, page_num + 1, pw, ph, src_n, src_stride, pw * src_n,
        src_stride == pw * src_n ? "true" : "false",
        ((double)src_stride * ph) / 1048576.0, chk_r, chk_g, chk_b);

    *width_out  = pw;
    *height_out = ph;

    // 统一输出为紧密排列的 RGB（3字节/像素），不含 alpha
    int dst_stride = pw * 3;
    size_t data_sz = (size_t)dst_stride * ph;
    const char* conversion_mode =
        (src_n == 3 && src_stride == dst_stride) ? "RGB_COPY" :
        (src_n == 3 && src_stride == pw) ? "RGB_PLANAR_TO_INTERLEAVED" :
        (src_n == 3) ? "RGB_REMOVE_PADDING" :
        (src_n == 4 && src_stride == pw * 4) ? "RGBA_TO_RGB" :
        (src_n == 4) ? "RGBA_REMOVE_PADDING" : "FALLBACK_ROW_COPY";
    unsigned char* owned_data = (unsigned char*)malloc(data_sz);
    if (owned_data) {
        if (src_n == 3 && src_stride == dst_stride) {
            // 交织 RGB：已是目标格式，直接复制
            memcpy(owned_data, src_samples, data_sz);
        } else if (src_n == 3 && src_stride == pw) {
            // 平面格式（RRR...GGG...BBB...）→ 交织 RGB
            for (int row = 0; row < ph; row++) {
                int row_offset = row * pw;  // 每通道的行内偏移（不是 stride！）
                for (int col = 0; col < pw; col++) {
                    owned_data[(row * pw + col) * 3 + 0] = src_samples[row_offset + col];                   // R
                    owned_data[(row * pw + col) * 3 + 1] = src_samples[pw * ph + row_offset + col];         // G
                    owned_data[(row * pw + col) * 3 + 2] = src_samples[pw * ph * 2 + row_offset + col];     // B
                }
            }
        } else if (src_n == 3) {
            // RGB 有行填充（stride > w*3）→ 去填充
            for (int row = 0; row < ph; row++) {
                memcpy(owned_data + row * dst_stride,
                       src_samples + row * src_stride,
                       dst_stride);
            }
        } else if (src_n == 4 && src_stride == pw * 4) {
            // 交织 RGBA → 交织 RGB
            for (int row = 0; row < ph; row++) {
                for (int col = 0; col < pw; col++) {
                    owned_data[(row * pw + col) * 3 + 0] = src_samples[(row * pw + col) * 4 + 0];
                    owned_data[(row * pw + col) * 3 + 1] = src_samples[(row * pw + col) * 4 + 1];
                    owned_data[(row * pw + col) * 3 + 2] = src_samples[(row * pw + col) * 4 + 2];
                }
            }
        } else if (src_n == 4) {
            // RGBA 有行填充 → 去填充再丢弃 alpha
            for (int row = 0; row < ph; row++) {
                for (int col = 0; col < pw; col++) {
                    owned_data[(row * pw + col) * 3 + 0] = src_samples[row * src_stride + col * 4 + 0];
                    owned_data[(row * pw + col) * 3 + 1] = src_samples[row * src_stride + col * 4 + 1];
                    owned_data[(row * pw + col) * 3 + 2] = src_samples[row * src_stride + col * 4 + 2];
                }
            }
        } else {
            // 兜底：逐行取 min(stride, dst_stride) 字节
            int copy_bytes = (src_stride < dst_stride) ? (int)src_stride : dst_stride;
            for (int row = 0; row < ph; row++) {
                memcpy(owned_data + row * dst_stride,
                       src_samples + row * src_stride,
                       copy_bytes);
            }
        }
        *data_out = owned_data;
        *data_size_out = data_sz;
    }

    fz_drop_pixmap(fz, pix);

    if (!owned_data) {
        LOG_EVENT("ERROR", "RENDER", "END", "status=FAIL | doc=%p | page=%d | ret=-3 | reason=out_of_memory | requested_bytes=%zu", (void*)doc, page_num + 1, data_sz);
        SET_ERROR(ctx, "Out of memory for render data");
        return -3;
    }

    LOG_EVENT(
        "INFO", "RENDER", "END",
        "status=OK | doc=%p | page=%d | pixel_size=%dx%d | output_channels=3 | output_stride=%d | output_bytes=%zu | output_mb=%.3f | conversion=%s",
        (void*)doc, page_num + 1, pw, ph, dst_stride, data_sz, data_sz / 1048576.0, conversion_mode);
    return 0;
}

void mupdf_free_render_data(MupdfContext* ctx, void* data) {
    if (data) {
        free(data);
    }
}

// ============================================
// 工具函数
// ============================================

/**
 * @brief 根据 PDF 页面尺寸（单位：点，1pt=1/72inch）检测纸张类型
 * @param pw 页面宽度（pt）
 * @param ph 页面高度（pt）
 * @return 纸张类型代码：1=A4, 2=A3, 3=Letter, 4=Legal, 5=A5, 6=A2,5=A1,7=A0,0=未识别
 */
static int detect_paper_size_by_points(float pw, float ph) {
    if (pw <= 0 || ph <= 0) return 0;
    // 计算原始长边/短边（与 pw/ph 的 portrait/landscape 判断无关）
    float long_side  = (pw > ph) ? pw : ph;
    float short_side = (pw > ph) ? ph : pw;

    LOG_EVENT("DEBUG", "PRINT", "PAPER_DETECT_INPUT", "page_size_pt=%.3fx%.3f | long_side=%.3f | short_side=%.3f", pw, ph, long_side, short_side);

#define RETURN_DETECTED_PAPER(code) \
    do { \
        LOG_EVENT((code) ? "INFO" : "WARN", "PRINT", "PAPER_DETECT", \
            "page_size_pt=%.3fx%.3f | long_side=%.3f | short_side=%.3f | paper_id=%d | paper=%s", \
            pw, ph, long_side, short_side, (code), log_paper_name(code)); \
        return (code); \
    } while(0)

    // 允许 ±25pt 误差（扫描 PDF 尺寸往往不严格）
    // A3 portrait MediaBox: ~842x1191,  short=~842, long=~1191
    // A3 landscape MediaBox: ~1191x842, short=~842, long=~1191  (Rotate=90时视觉纵向)
    // A4 portrait MediaBox: ~595x842,   short=~595, long=~842
    // A4 landscape MediaBox: ~842x595,   short=~595, long=~842  (Rotate=90时视觉纵向)
    // 注意：A3/A4/A5 的短边尺寸完全不同（842 vs 595 vs 419），可以用短边精确区分
    if (short_side >= 815.f && short_side <= 865.f) {
        // 短边 ~842 → A3
        if (long_side >= 1165.f && long_side <= 1220.f) {
            RETURN_DETECTED_PAPER(2); // A3
        }
    }
    if (short_side >= 570.f && short_side <= 620.f) {
        // 短边 ~595 → A4
        if (long_side >= 815.f && long_side <= 865.f) {
            RETURN_DETECTED_PAPER(1); // A4
        }
    }
    if (short_side >= 390.f && short_side <= 445.f) {
        // 短边 ~419 → A5
        if (long_side >= 570.f && long_side <= 620.f) {
            RETURN_DETECTED_PAPER(5); // A5
        }
    }
    // Letter: ~612x792
    if (short_side >= 590.f && short_side <= 635.f) {
        if (long_side >= 770.f && long_side <= 815.f) {
            RETURN_DETECTED_PAPER(3); // Letter
        }
    }
    // Legal: ~612x1008
    if (short_side >= 590.f && short_side <= 635.f) {
        if (long_side >= 980.f && long_side <= 1030.f) {
            RETURN_DETECTED_PAPER(4); // Legal
        }
    }
    // A2: ~420x594mm → 1191x1684pt
    if (short_side >= 1165.f && short_side <= 1220.f) {
        if (long_side >= 1655.f && long_side <= 1715.f) {
            RETURN_DETECTED_PAPER(6); // A2
        }
    }
    // A1: ~594x841mm → 1684x2384pt
    if (short_side >= 1655.f && short_side <= 1715.f) {
        if (long_side >= 2355.f && long_side <= 2415.f) {
            RETURN_DETECTED_PAPER(7); // A1
        }
    }
    // A0: ~841x1189mm → 2384x3370pt
    if (short_side >= 2355.f && short_side <= 2415.f) {
        if (long_side >= 3340.f && long_side <= 3400.f) {
            RETURN_DETECTED_PAPER(8); // A0
        }
    }
    // A6: ~105x148mm → 298x419pt
    if (short_side >= 273.f && short_side <= 323.f) {
        if (long_side >= 394.f && long_side <= 444.f) {
            RETURN_DETECTED_PAPER(9); // A6
        }
    }
    // A7: ~74x105mm → 210x298pt
    if (short_side >= 185.f && short_side <= 235.f) {
        if (long_side >= 273.f && long_side <= 323.f) {
            RETURN_DETECTED_PAPER(10); // A7
        }
    }
    // A8: ~52x74mm → 147x210pt
    if (short_side >= 122.f && short_side <= 172.f) {
        if (long_side >= 185.f && long_side <= 235.f) {
            RETURN_DETECTED_PAPER(11); // A8
        }
    }
    // B0: ~1000x1414mm → 2835x4008pt
    if (short_side >= 2810.f && short_side <= 2860.f) {
        if (long_side >= 3983.f && long_side <= 4033.f) {
            RETURN_DETECTED_PAPER(12); // B0
        }
    }
    // B1: ~707x1000mm → 2004x2835pt
    if (short_side >= 1979.f && short_side <= 2029.f) {
        if (long_side >= 2810.f && long_side <= 2860.f) {
            RETURN_DETECTED_PAPER(13); // B1
        }
    }
    // B2: ~500x707mm → 1417x2004pt
    if (short_side >= 1392.f && short_side <= 1442.f) {
        if (long_side >= 1979.f && long_side <= 2029.f) {
            RETURN_DETECTED_PAPER(14); // B2
        }
    }
    // B3: ~353x500mm → 1000x1417pt
    if (short_side >= 975.f && short_side <= 1025.f) {
        if (long_side >= 1392.f && long_side <= 1442.f) {
            RETURN_DETECTED_PAPER(15); // B3
        }
    }
    // B4: ~250x353mm → 709x1000pt
    if (short_side >= 684.f && short_side <= 734.f) {
        if (long_side >= 975.f && long_side <= 1025.f) {
            RETURN_DETECTED_PAPER(16); // B4
        }
    }
    // B5: ~176x250mm → 499x709pt
    if (short_side >= 474.f && short_side <= 524.f) {
        if (long_side >= 684.f && long_side <= 734.f) {
            RETURN_DETECTED_PAPER(17); // B5
        }
    }
    // B6: ~125x176mm → 354x499pt
    if (short_side >= 329.f && short_side <= 379.f) {
        if (long_side >= 474.f && long_side <= 524.f) {
            RETURN_DETECTED_PAPER(18); // B6
        }
    }
    // B7: ~88x125mm → 249x354pt
    if (short_side >= 224.f && short_side <= 274.f) {
        if (long_side >= 329.f && long_side <= 379.f) {
            RETURN_DETECTED_PAPER(19); // B7
        }
    }
    RETURN_DETECTED_PAPER(0);
#undef RETURN_DETECTED_PAPER
}

// ============================================
// 打印
// ============================================

int mupdf_print(
    MupdfContext* ctx,
    MupdfDocument* doc,
    const char* printer_name,
    const PrintOptions* options
) {
    return mupdf_print_pages(ctx, doc, nullptr, -1, printer_name, options);
}

int mupdf_print_pages(
    MupdfContext* ctx,
    MupdfDocument* doc,
    const int* page_nums,
    int page_count,
    const char* printer_name,
    const PrintOptions* options
) {
    if (!ctx || !doc) return -1;

    int total = 0;
    mupdf_get_page_count(ctx, doc, &total);
    if (total <= 0) return -2;

    char printer_log[1024];
    log_acp_as_utf8(printer_name ? printer_name : "(default)", printer_log, sizeof(printer_log));
    LOG_EVENT(
        "INFO", "PRINT", "REQUEST",
        "doc=%p | total_pages=%d | printer=\"%s\" | page_mode=%s | requested_page_count=%d | page_nums_ptr=%p",
        (void*)doc, total, printer_log, (page_nums && page_count > 0) ? "SELECTED" : "RANGE", page_count, (void*)page_nums);

    PrintOptions default_opts = print_options_default();
    PrintOptions* opts = options ? nullptr : &default_opts;
    const PrintOptions* print_opts = options ? options : &default_opts;

    // 自动检测 PDF 页面方向，防止某些打印机（如立思辰）出现 90 度旋转问题
    // 如果用户已经明确设置了 orientation（orientation != 0），则使用用户设置
    if (!options || options->orientation == 0) {
        // 确定要打印的页码范围
        int from = print_opts->from_page > 0 ? print_opts->from_page - 1 : 0;
        int to = print_opts->to_page > 0 ? print_opts->to_page - 1 : total - 1;
        if (to >= total) to = total - 1;

        // 检测所有要打印页面的方向
        int landscape_count = 0;
        int portrait_count = 0;
        int valid_page_count = 0;

        for (int i = from; i <= to; i++) {
            int page_idx = i;
            if (page_nums && page_count > 0) {
                if (i >= page_count) break;
                page_idx = page_nums[i];
            }

            if (mupdf_is_page_landscape(ctx, doc, page_idx)) {
                landscape_count++;
            } else {
                portrait_count++;
            }
            valid_page_count++;
        }

        // 根据检测结果设置打印方向
        // 只有当所有页面方向一致时才强制设置方向，否则保持自动模式
        if (valid_page_count > 0) {
            if (!opts) {
                // 需要复制一份 options 以便修改
                opts = (PrintOptions*)malloc(sizeof(PrintOptions));
                if (opts) *opts = *options;
            }
            if (opts) {
                if (landscape_count > 0 && portrait_count == 0) {
                    // 所有页面都是横向
                    opts->orientation = 2;  // 横向
                    LOG_EVENT("INFO", "PRINT", "ORIENTATION", "mode=AUTO | landscape_pages=%d | portrait_pages=%d | selected=2 | selected_name=LANDSCAPE", landscape_count, portrait_count);
                } else if (portrait_count > 0 && landscape_count == 0) {
                    // 所有页面都是纵向
                    opts->orientation = 1;  // 纵向
                    LOG_EVENT("INFO", "PRINT", "ORIENTATION", "mode=AUTO | landscape_pages=%d | portrait_pages=%d | selected=1 | selected_name=PORTRAIT", landscape_count, portrait_count);
                } else {
                    // 混合方向，保持自动模式
                    opts->orientation = 0;
                    LOG_EVENT("WARN", "PRINT", "ORIENTATION", "mode=AUTO | landscape_pages=%d | portrait_pages=%d | selected=0 | selected_name=MIXED", landscape_count, portrait_count);
                }
                print_opts = opts;
            }
        }
    }

    // 自动检测 PDF 纸张尺寸（当 paper_size==0 或无效值 时），根据第一页的 PDF 点尺寸推算
    // PDF 单位：point（1pt = 1/72英寸）
    // A3 = 841.89 × 1190.55 pt, A4 = 595.28 × 841.89 pt, A5 = 419.53 × 595.28 pt
    // Letter = 612 × 792 pt,    Legal = 612 × 1008 pt
    // 注意：混合纸张时由打印循环中逐页处理，此处仅设置首帧 dmPaperSize 初始值
    // ★ 修复：检测无效的 paper_size（如未初始化导致的 -858993460），视为自动模式
    int user_paper = options ? options->paper_size : 0;
    LOG_EVENT("INFO", "PRINT", "PAPER_REQUEST", "options_ptr=%p | requested_paper_id=%d | requested_paper=%s | auto_detect=%s", (void*)options, user_paper, log_paper_name(user_paper), (!options || user_paper <= 0 || user_paper > 19) ? "true" : "false");

    // paper_size 有效值: 0=自动, 1=A4, 2=A3, 3=Letter, 4=Legal, 5=A5
    // 负数或 >5 视为无效，触发自动检测
    if (!options || user_paper <= 0 || user_paper > 19) 
	{
        int first_page_idx = (print_opts->from_page > 0) ? print_opts->from_page - 1 : 0;
        if (page_nums && page_count > 0) first_page_idx = page_nums[0];

        float pw = 0, ph = 0;
        mupdf_get_page_size(ctx, doc, first_page_idx, &pw, &ph);
        LOG_EVENT("INFO", "PRINT", "PAPER_PAGE_SIZE", "doc=%p | page=%d | visual_size_pt=%.3fx%.3f", (void*)doc, first_page_idx + 1, pw, ph);

        int detected_paper = detect_paper_size_by_points(pw, ph);

        const char* det_name = log_paper_name(detected_paper);
        LOG_EVENT(detected_paper > 0 ? "INFO" : "WARN", "PRINT", "PAPER_SELECTED", "source=AUTO | page=%d | visual_size_pt=%.3fx%.3f | paper_id=%d | paper=%s", first_page_idx + 1, pw, ph, detected_paper, det_name);

        if (detected_paper > 0) {
            if (!opts) {
                opts = (PrintOptions*)malloc(sizeof(PrintOptions));
                if (opts) *opts = (options ? *options : *print_opts);
            }
            if (opts) {
                opts->paper_size = detected_paper;
                print_opts = opts;
            }
        } else {
            LOG_EVENT("WARN", "PRINT", "PAPER_SELECTED", "source=AUTO | page=%d | status=UNRECOGNIZED | visual_size_pt=%.3fx%.3f", first_page_idx + 1, pw, ph);
        }
    } else {
        // 用户指定了 paper_size，打印用户设置的值
        const char* opt_name = log_paper_name(options->paper_size);
        LOG_EVENT("INFO", "PRINT", "PAPER_SELECTED", "source=USER | paper_id=%d | paper=%s", options->paper_size, opt_name);
    }

    LOG_EVENT("INFO", "PRINT", "ENGINE_CREATE_BEGIN", "printer=\"%s\" | paper_id=%d | paper=%s | orientation=%d | copies=%d | duplex=%d | color=%d", printer_log, print_opts->paper_size, log_paper_name(print_opts->paper_size), print_opts->orientation, print_opts->copies, print_opts->duplex, print_opts->color);

    // 确定要打印的页码范围
    int from = print_opts->from_page > 0 ? print_opts->from_page - 1 : 0;
    int to = print_opts->to_page > 0 ? print_opts->to_page - 1 : total - 1;
    if (to >= total) to = total - 1;

    // 创建打印引擎
    PrintEngine* engine = print_engine_create(printer_name, print_opts);
    if (!engine) {
        LOG_EVENT("ERROR", "PRINT", "ENGINE_CREATE_END", "status=FAIL | printer=\"%s\" | ret=-3", printer_log);
        SET_ERROR(ctx, "Failed to create print engine");
        return -3;
    }
    LOG_EVENT("INFO", "PRINT", "ENGINE_CREATE_END", "status=OK | engine=%p | printer=\"%s\"", (void*)engine, printer_log);

    const char* job_name = print_opts->job_name ? print_opts->job_name : "MuPDF Document";
    char job_name_log[2048];
    log_acp_as_utf8(job_name, job_name_log, sizeof(job_name_log));
    LOG_EVENT("INFO", "PRINT", "JOB_BEGIN", "doc=%p | engine=%p | printer=\"%s\" | job_name=\"%s\" | total_pages=%d | from=%d | to=%d", (void*)doc, (void*)engine, printer_log, job_name_log, total, from + 1, to + 1);
    int start_job_ret = print_engine_start_job(engine, job_name);
    if (start_job_ret != 0) {
        char pe_err[512];
        _snprintf(pe_err, sizeof(pe_err), "%s", print_engine_get_error(engine));
        pe_err[sizeof(pe_err) - 1] = '\0';
        LOG_EVENT("ERROR", "PRINT", "JOB_END", "status=FAIL | doc=%p | engine=%p | ret=%d | message=\"%s\"", (void*)doc, (void*)engine, start_job_ret, pe_err);
        print_engine_destroy(engine);
        SET_ERROR(ctx, pe_err);
        return -4;
    }

    int dpi = 200; // 打印分辨率：200 DPI 兼顾 A4~A0 清晰度和内存安全（A0约186MB，未超256MB上限）
    int last_paper_size = print_opts->paper_size; // 记住上次纸张类型，避免重复 ResetDC

    LOG_EVENT("INFO", "PRINT", "LOOP_BEGIN", "doc=%p | engine=%p | from=%d | to=%d | total_pages=%d | render_dpi=%d | paper_id=%d | paper=%s", (void*)doc, (void*)engine, from + 1, to + 1, total, dpi, last_paper_size, log_paper_name(last_paper_size));

    int printed_count = 0;
    int failed_count = 0;
    int skipped_count = 0;

    for (int i = from; i <= to; i++) {
        int page_idx = i;
        if (page_nums && page_count > 0) {
            if (i >= page_count) break;
            page_idx = page_nums[i];
        }

        int page_progress = i - from + 1;
        int page_total = to - from + 1;
        LOG_EVENT("INFO", "PRINT", "PAGE_BEGIN", "doc=%p | engine=%p | page=%d | progress=%d/%d | paper_id=%d | paper=%s", (void*)doc, (void*)engine, page_idx + 1, page_progress, page_total, last_paper_size, log_paper_name(last_paper_size));

        float pw = 0, ph = 0;

        // ★ 逐页检测纸张大小，换页前 ResetDC（仅当纸张类型变化时）
        if (print_opts->paper_size == 0 || last_paper_size == 0) {
            // 用户指定了 auto (0)，每次都检测
            mupdf_get_page_size(ctx, doc, page_idx, &pw, &ph);
            int page_paper = detect_paper_size_by_points(pw, ph);
            if (page_paper > 0 && page_paper != last_paper_size) {
                int reset_ret = print_engine_reset_paper(engine, page_paper);
                LOG_EVENT(reset_ret == 0 ? "INFO" : "ERROR", "PRINT", "PAPER_RESET", "doc=%p | engine=%p | page=%d | from_id=%d | from=%s | to_id=%d | to=%s | ret=%d", (void*)doc, (void*)engine, page_idx + 1, last_paper_size, log_paper_name(last_paper_size), page_paper, log_paper_name(page_paper), reset_ret);
                last_paper_size = page_paper;
            }
        } else if (i > from) {
            // 用户指定了固定纸张，但需要检测混合纸张
            mupdf_get_page_size(ctx, doc, page_idx, &pw, &ph);
            int page_paper = detect_paper_size_by_points(pw, ph);
            if (page_paper > 0 && page_paper != last_paper_size) {
                int reset_ret = print_engine_reset_paper(engine, page_paper);
                LOG_EVENT(reset_ret == 0 ? "INFO" : "ERROR", "PRINT", "PAPER_RESET", "doc=%p | engine=%p | page=%d | from_id=%d | from=%s | to_id=%d | to=%s | ret=%d", (void*)doc, (void*)engine, page_idx + 1, last_paper_size, log_paper_name(last_paper_size), page_paper, log_paper_name(page_paper), reset_ret);
                last_paper_size = page_paper;
            }
        }

        int w = 0, h = 0;
        unsigned char* data = nullptr;
        size_t data_sz = 0;

        int ret = mupdf_render_page(ctx, doc, page_idx, dpi, 0, &w, &h, &data, &data_sz);
        if (ret != 0 || !data) {
            // ★ 修复：渲染失败时记录详细日志（之前是静默 continue 导致页面丢失无法诊断）
            LOG_EVENT("ERROR", "PRINT", "PAGE_END", "status=FAIL | stage=RENDER | doc=%p | engine=%p | page=%d | ret=%d | data=%p | message=\"%s\"", (void*)doc, (void*)engine, page_idx + 1, ret, (void*)data, mupdf_get_error(ctx));
            failed_count++;
            skipped_count++;
            continue;
        }

        LOG_EVENT(
            data && data_sz >= 3 ? "INFO" : "ERROR", "PRINT", "PAGE_BITMAP",
            "doc=%p | engine=%p | page=%d | pixel_size=%dx%d | aspect=%.6f | bytes=%zu | mb=%.3f | render_dpi=%d | "
            "paper_id=%d | paper=%s | first_pixel=%d,%d,%d | data_valid=%s",
            (void*)doc, (void*)engine, page_idx + 1, w, h, h != 0 ? (float)w / h : 0.0f,
            data_sz, data_sz / 1048576.0, dpi, last_paper_size, log_paper_name(last_paper_size),
            data && data_sz >= 3 ? data[0] : -1, data && data_sz >= 3 ? data[1] : -1,
            data && data_sz >= 3 ? data[2] : -1, data && data_sz >= 3 ? "true" : "false");

        // mupdf_render_page 已保证输出紧密排列 RGB（3字节/像素，无行填充、无alpha）
        int bpp = 24;

        // ★★★ 核心修复：检查 print_engine_print_page 返回值 ★★★
        // 之前完全忽略返回值，导致打印引擎在第N页失败后后续所有页面静默失败
        // 打印队列只收到失败前的页面（如60页），但程序以为全部打印成功
        int pe_ret = print_engine_print_page(engine, w, h, bpp, data, (int)data_sz);
        free(data);

        if (pe_ret != 0) {
            // 打印引擎失败：先保存错误信息再 destroy（避免 use-after-free）
            char pe_err[512];
            _snprintf(pe_err, sizeof(pe_err), "%s", print_engine_get_error(engine));
            pe_err[sizeof(pe_err) - 1] = '\0';
            LOG_EVENT("ERROR", "PRINT", "PAGE_END", "status=FAIL | stage=PRINT_ENGINE | doc=%p | engine=%p | page=%d | ret=%d | message=\"%s\" | printed_before_failure=%d/%d", (void*)doc, (void*)engine, page_idx + 1, pe_ret, pe_err, printed_count, page_total);

            print_engine_end_job(engine);
            print_engine_destroy(engine);

            // 释放可能分配的 options 副本
            if (opts == &default_opts) opts = nullptr;
            if (opts && opts != options) free(opts);

            char err_msg[512];
            _snprintf(err_msg, sizeof(err_msg),
                "Print engine failed on page %d (ret=%d): %s. Printed %d/%d pages before failure.",
                page_idx, pe_ret, pe_err, printed_count, page_total);
            SET_ERROR(ctx, err_msg);
            return -5;
        }

        printed_count++;
        LOG_EVENT("INFO", "PRINT", "PAGE_END", "status=OK | doc=%p | engine=%p | page=%d | printed=%d | failed=%d | skipped=%d", (void*)doc, (void*)engine, page_idx + 1, printed_count, failed_count, skipped_count);

        // 定期清理 MuPDF 存储缓存，防止长时间打印（如300页）时缓存无限增长
        // 即使设置了 256MB 上限，定期清理可进一步降低内存峰值
        if (printed_count > 0 && printed_count % 50 == 0) {
            LOG_EVENT("INFO", "RENDER", "CACHE_CLEAR", "doc=%p | printed_pages=%d", (void*)doc, printed_count);
            fz_empty_store(ctx->ctx);
        }
    }

    LOG_EVENT(failed_count ? "WARN" : "INFO", "PRINT", "LOOP_END", "status=%s | doc=%p | engine=%p | printed=%d | failed=%d | skipped=%d", failed_count ? "PARTIAL" : "OK", (void*)doc, (void*)engine, printed_count, failed_count, skipped_count);

    print_engine_end_job(engine);
    print_engine_destroy(engine);

    // 释放可能分配的 options 副本
    if (opts == &default_opts) {
        opts = nullptr;  // 栈上的变量不需要释放
    }
    if (opts && opts != options) {
        free(opts);
    }

    LOG_EVENT(failed_count ? "WARN" : "INFO", "PRINT", "JOB_END", "status=%s | doc=%p | printer=\"%s\" | job_name=\"%s\" | printed=%d | failed=%d | skipped=%d | ret=0", failed_count ? "PARTIAL" : "OK", (void*)doc, printer_log, job_name_log, printed_count, failed_count, skipped_count);
    return 0;
}

// ============================================
// 工具函数
// ============================================

const char* mupdf_get_supported_image_formats(void) {
    return "jpeg,jpg,png,bmp,gif,tiff,webp";
}

int mupdf_is_pdf_file(const char* path) {
    if (!path) return 0;
    const char* ext = strrchr(path, '.');
    if (!ext) return 0;
    return (_stricmp(ext, ".pdf") == 0) ? 1 : 0;
}

const char* mupdf_get_version(void) {
    return "1.0.0";
}
