/**
 * @file print_engine.cpp
 * @brief 打印引擎实现 - Windows GDI 打印
 */

#ifdef _WIN32

#include "print_engine.h"
#include <windows.h>
#include <wingdi.h>
#include <winspool.h>
#include <cstring>
#include <cstdio>
#include <cmath>

// 统一使用 mupdf_wrapper.cpp 中的单文件结构化日志器，避免两个 FILE* 并发写同一文件。
extern void mupdf_diag_log(
    const char* level,
    const char* component,
    const char* event,
    const char* format,
    ...);

#define PE_LOG_EVENT(level, event, fmt, ...) \
    mupdf_diag_log(level, "PRINT", event, fmt, ##__VA_ARGS__)

static const char* pe_paper_name(int paper_size) {
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

static void pe_escape_utf8(const char* input, char* output, size_t output_size) {
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

static void pe_acp_to_utf8(const char* input, char* output, size_t output_size) {
    if (!output || output_size == 0) return;
    output[0] = '\0';
    if (!input) input = "(null)";

    WCHAR wide[1024];
    int wide_len = MultiByteToWideChar(CP_ACP, 0, input, -1, wide, (int)(sizeof(wide) / sizeof(wide[0])));
    if (wide_len <= 0) {
        _snprintf(output, output_size, "<ACP conversion failed: %lu>", GetLastError());
        output[output_size - 1] = '\0';
        return;
    }
    char utf8[4096];
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, (int)sizeof(utf8), NULL, NULL);
    if (utf8_len <= 0) {
        _snprintf(output, output_size, "<UTF-8 conversion failed: %lu>", GetLastError());
        output[output_size - 1] = '\0';
        return;
    }
    pe_escape_utf8(utf8, output, output_size);
}

static void pe_log_devmode(const char* event, const DEVMODEA* dm, LONG source_result) {
    if (!dm) {
        PE_LOG_EVENT("WARN", event, "devmode=null | source_result=%ld", source_result);
        return;
    }
    PE_LOG_EVENT(
        "INFO", event,
        "source_result=%ld | dm_size=%u | driver_extra=%u | fields=0x%08lX | orientation=%d | "
        "paper_size=%d | paper_width_0_1mm=%d | paper_length_0_1mm=%d | copies=%d | duplex=%d | color=%d | print_quality=%d | y_resolution=%d",
        source_result, (unsigned int)dm->dmSize, (unsigned int)dm->dmDriverExtra, dm->dmFields,
        dm->dmOrientation, dm->dmPaperSize, dm->dmPaperWidth, dm->dmPaperLength,
        dm->dmCopies, dm->dmDuplex, dm->dmColor, dm->dmPrintQuality, dm->dmYResolution);
}

static void pe_log_printer_info(HANDLE hPrinter, const char* selected_name) {
    DWORD needed = 0;
    GetPrinter(hPrinter, 2, NULL, 0, &needed);
    if (needed == 0) {
        PE_LOG_EVENT("WARN", "PRINTER_INFO", "status=UNAVAILABLE | selected=\"%s\" | gle=%lu", selected_name ? selected_name : "(null)", GetLastError());
        return;
    }

    PRINTER_INFO_2* info = (PRINTER_INFO_2*)malloc(needed);
    if (!info) {
        PE_LOG_EVENT("WARN", "PRINTER_INFO", "status=UNAVAILABLE | selected=\"%s\" | reason=out_of_memory | bytes=%lu", selected_name ? selected_name : "(null)", needed);
        return;
    }

    if (GetPrinter(hPrinter, 2, (LPBYTE)info, needed, &needed)) {
        char name_utf8[2048], port_utf8[2048], driver_utf8[2048];
        pe_acp_to_utf8(info->pPrinterName, name_utf8, sizeof(name_utf8));
        pe_acp_to_utf8(info->pPortName, port_utf8, sizeof(port_utf8));
        pe_acp_to_utf8(info->pDriverName, driver_utf8, sizeof(driver_utf8));
        PE_LOG_EVENT(
            "INFO", "PRINTER_INFO",
            "status=OK | name=\"%s\" | port=\"%s\" | driver=\"%s\" | attributes=0x%08lX | printer_status=0x%08lX | jobs=%lu",
            name_utf8, port_utf8, driver_utf8, info->Attributes, info->Status, info->cJobs);
    } else {
        PE_LOG_EVENT("WARN", "PRINTER_INFO", "status=UNAVAILABLE | selected=\"%s\" | gle=%lu", selected_name ? selected_name : "(null)", GetLastError());
    }
    free(info);
}

static void pe_log_dc_caps(HDC dc, const char* event) {
    if (!dc) {
        PE_LOG_EVENT("WARN", event, "dc=null");
        return;
    }
    PE_LOG_EVENT(
        "INFO", event,
        "dpi=%dx%d | printable=%dx%d | physical=%dx%d | physical_offset=%d,%d | technology=%d | bits_pixel=%d | planes=%d",
        GetDeviceCaps(dc, LOGPIXELSX), GetDeviceCaps(dc, LOGPIXELSY),
        GetDeviceCaps(dc, HORZRES), GetDeviceCaps(dc, VERTRES),
        GetDeviceCaps(dc, PHYSICALWIDTH), GetDeviceCaps(dc, PHYSICALHEIGHT),
        GetDeviceCaps(dc, PHYSICALOFFSETX), GetDeviceCaps(dc, PHYSICALOFFSETY),
        GetDeviceCaps(dc, TECHNOLOGY), GetDeviceCaps(dc, BITSPIXEL), GetDeviceCaps(dc, PLANES));
}

// ============================================
// 内部结构
// ============================================

struct PrintEngine {
    HDC hPrinterDC;
    DOCINFO docInfo;
    PRINTDLG printDlg;
    
    int page_count;
    int current_page;
    int copies;
    int duplex;
    
    char printer_name[256];
    char error_msg[512];
    
    PrintCallback callback;
    void* user_data;
    
    bool job_started;
};

// ============================================
// 工具函数
// ============================================

static void set_error(PrintEngine* engine, const char* msg) {
    if (engine) {
        strncpy(engine->error_msg, msg, sizeof(engine->error_msg) - 1);
    }
    char message_log[2048];
    pe_escape_utf8(msg, message_log, sizeof(message_log));
    PE_LOG_EVENT("ERROR", "SET_ERROR", "engine=%p | message=\"%s\"", (void*)engine, message_log);
}

static DEVMODE* get_devmode(const char* printer_name) {
    HANDLE hPrinter;
    DEVMODE* dm = nullptr;
    
    if (OpenPrinter((LPSTR)printer_name, &hPrinter, nullptr)) {
        DWORD needed = 0;
        GetPrinter(hPrinter, 2, nullptr, 0, &needed);
        
        if (needed > 0) {
            PRINTER_INFO_2* pi2 = (PRINTER_INFO_2*)malloc(needed);
            if (GetPrinter(hPrinter, 2, (LPBYTE)pi2, needed, &needed)) {
                if (pi2->pDevMode) {
                    DWORD dmSize = pi2->pDevMode->dmSize + pi2->pDevMode->dmDriverExtra;
                    dm = (DEVMODE*)malloc(dmSize);
                    memcpy(dm, pi2->pDevMode, dmSize);
                }
            }
            free(pi2);
        }
        ClosePrinter(hPrinter);
    }
    
    return dm;
}

// ============================================
// 打印引擎实现
// ============================================

PrintOptions print_options_default(void) {
    PrintOptions opts = {0};
    opts.copies = 1;
    opts.duplex = 1;      // 1=单面
    opts.collate = 0;
    opts.color = 2;       // 2=彩色
    opts.scale = 0;
    opts.orientation = 0;
    opts.from_page = 0;
    opts.to_page = 0;
    opts.job_name = "MuPDF Document";
    return opts;
}

int print_get_printers(PrinterInfo* printers, int max_count) {
    if (!printers || max_count <= 0) return 0;
    
    DWORD needed = 0, returned = 0;
    EnumPrinters(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 2, nullptr, 0, &needed, &returned);
    
    if (needed == 0) return 0;
    
    PRINTER_INFO_2* info = (PRINTER_INFO_2*)malloc(needed);
    if (!EnumPrinters(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 2, (LPBYTE)info, needed, &needed, &returned)) {
        free(info);
        return 0;
    }
    
    int count = 0;
    for (DWORD i = 0; i < returned && count < max_count; i++) {
        PrinterInfo* pi = &printers[count];
        memset(pi, 0, sizeof(PrinterInfo));
        
        strncpy(pi->name, info[i].pPrinterName, sizeof(pi->name) - 1);
        if (info[i].pPortName) strncpy(pi->port, info[i].pPortName, sizeof(pi->port) - 1);
        if (info[i].pDriverName) strncpy(pi->driver, info[i].pDriverName, sizeof(pi->driver) - 1);
        
        pi->is_default = (info[i].Attributes & PRINTER_ATTRIBUTE_DEFAULT) ? 1 : 0;
        pi->is_ready = (info[i].Status == 0) ? 1 : 0;
        
        count++;
    }
    
    free(info);
    return count;
}

int print_get_default_printer(char* name, int name_size) {
    if (!name || name_size <= 0) return -1;
    
    DWORD size = (DWORD)name_size;
    if (GetDefaultPrinter(name, &size)) {
        return 0;
    }
    return -1;
}

int print_is_printer_ready(const char* printer_name) {
    if (!printer_name) return 0;
    
    HANDLE hPrinter;
    if (!OpenPrinter((LPSTR)printer_name, &hPrinter, nullptr)) {
        return 0;
    }
    
    PRINTER_INFO_2* info = nullptr;
    DWORD needed = 0;
    GetPrinter(hPrinter, 2, nullptr, 0, &needed);
    
    if (needed > 0) {
        info = (PRINTER_INFO_2*)malloc(needed);
        if (GetPrinter(hPrinter, 2, (LPBYTE)info, needed, &needed)) {
            // PRINTER_ATTRIBUTE_PAUSED = 0x00000040
            int ready = (info->Status == 0 && !(info->Attributes & 0x00000040)) ? 1 : 0;
            free(info);
            ClosePrinter(hPrinter);
            return ready;
        }
        free(info);
    }
    
    ClosePrinter(hPrinter);
    return 0;
}

PrintEngine* print_engine_create(const char* printer_name, const PrintOptions* options) {
    PrintEngine* engine = new PrintEngine;
    memset(engine, 0, sizeof(PrintEngine));
    
    engine->copies = options ? options->copies : 1;
    engine->duplex = options ? options->duplex : 0;
    
    if (printer_name && printer_name[0]) {
        strncpy(engine->printer_name, printer_name, sizeof(engine->printer_name) - 1);
    } else {
        print_get_default_printer(engine->printer_name, sizeof(engine->printer_name));
    }

    char printer_utf8[2048];
    pe_acp_to_utf8(engine->printer_name, printer_utf8, sizeof(printer_utf8));
    PE_LOG_EVENT(
        "INFO", "ENGINE_CREATE_BEGIN",
        "engine=%p | printer=\"%s\" | options_ptr=%p | copies=%d | duplex=%d | color=%d | orientation=%d | paper_id=%d | paper=%s",
        (void*)engine, printer_utf8, (void*)options,
        options ? options->copies : 1, options ? options->duplex : 0,
        options ? options->color : 2, options ? options->orientation : 0,
        options ? options->paper_size : 0, pe_paper_name(options ? options->paper_size : 0));
    
    // 打开打印机
    HANDLE hPrinter;
    if (!OpenPrinter(engine->printer_name, &hPrinter, nullptr)) {
        PE_LOG_EVENT("ERROR", "ENGINE_CREATE_END", "status=FAIL | engine=%p | printer=\"%s\" | stage=OpenPrinter | gle=%lu", (void*)engine, printer_utf8, GetLastError());
        set_error(engine, "Cannot open printer");
        delete engine;
        return nullptr;
    }
    pe_log_printer_info(hPrinter, printer_utf8);
    
    // 由打印机驱动创建完整的 DEVMODE，包含驱动私有数据。
    LONG devmode_size = DocumentPropertiesA(
        nullptr, hPrinter, engine->printer_name, nullptr, nullptr, 0);
    if (devmode_size <= 0) {
        DWORD le = GetLastError();
        PE_LOG_EVENT(
            "ERROR", "DEVMODE_QUERY",
            "status=FAIL | stage=QUERY_SIZE | engine=%p | printer=\"%s\" | size_result=%ld | gle=%lu",
            (void*)engine, printer_utf8, devmode_size, le);
        ClosePrinter(hPrinter);
        set_error(engine, "Cannot get printer devmode size");
        delete engine;
        return nullptr;
    }

    DEVMODEA* devmode = (DEVMODEA*)malloc((size_t)devmode_size);
    if (!devmode) {
        PE_LOG_EVENT(
            "ERROR", "DEVMODE_QUERY",
            "status=FAIL | stage=ALLOCATE | engine=%p | printer=\"%s\" | requested_bytes=%ld",
            (void*)engine, printer_utf8, devmode_size);
        ClosePrinter(hPrinter);
        set_error(engine, "Cannot allocate printer devmode");
        delete engine;
        return nullptr;
    }
    memset(devmode, 0, (size_t)devmode_size);

    LONG devmode_result = DocumentPropertiesA(
        nullptr, hPrinter, engine->printer_name, devmode, nullptr, DM_OUT_BUFFER);
    if (devmode_result != IDOK) {
        DWORD le = GetLastError();
        PE_LOG_EVENT(
            "ERROR", "DEVMODE_QUERY",
            "status=FAIL | stage=INITIALIZE | engine=%p | printer=\"%s\" | size=%ld | result=%ld | gle=%lu",
            (void*)engine, printer_utf8, devmode_size, devmode_result, le);
        free(devmode);
        ClosePrinter(hPrinter);
        set_error(engine, "Cannot initialize printer devmode");
        delete engine;
        return nullptr;
    }

    pe_log_devmode("DEVMODE_BEFORE", devmode, devmode_result);
    // 应用双面打印设置
    // duplex: 1=单面, 2=长边翻转, 3=短边翻转
    if (devmode && engine->duplex > 1) {
        // 确保 DEVMODE 可以修改
        if (!(devmode->dmFields & DM_DUPLEX)) {
            devmode->dmFields |= DM_DUPLEX;
        }
        // 设置双面打印模式
        // DMDUP_SIMPLEX = 1 (单面)
        // DMDUP_HORIZONTAL = 2 (短边翻转，适用于纵向页面)
        // DMDUP_VERTICAL = 3 (长边翻转，适用于横向页面)
        switch (engine->duplex) {
            case 2: // 长边翻转
                devmode->dmDuplex = DMDUP_VERTICAL;
                break;
            case 3: // 短边翻转
                devmode->dmDuplex = DMDUP_HORIZONTAL;
                break;
            default:
                devmode->dmDuplex = DMDUP_SIMPLEX;
                break;
        }
    }
    
    // 应用份数设置
    if (devmode) {
        devmode->dmCopies = (short)engine->copies;
        devmode->dmFields |= DM_COPIES;
    }
    
    // 应用颜色设置（黑白/彩色）
    // color: 1=黑白, 2=彩色
    if (devmode) {
        devmode->dmFields |= DM_COLOR;
        int color_mode = options ? options->color : 2;  // 默认彩色(2)
        devmode->dmColor = (color_mode == 2) ? DMCOLOR_COLOR : DMCOLOR_MONOCHROME;
    }
    
    // 应用页面方向设置
    // orientation: 0=自动(不设置), 1=纵向, 2=横向
    // 根据 PDF 页面的横纵向自动设置，防止某些打印机（如立思辰）出现 90 度旋转问题
    if (devmode && options && options->orientation > 0) {
        devmode->dmFields |= DM_ORIENTATION;
        devmode->dmOrientation = (options->orientation == 2) ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
    }

    // 应用纸张大小设置
    // paper_size: 0=自动(不修改), 1=A4, 2=A3, 3=Letter, 4=Legal, 5=A5
    if (devmode && options && options->paper_size > 0) {
        // ★★★ 调试：显示即将设置的纸张信息 ★★★
        const char* paper_name = "?";
        switch(options->paper_size) {
            case 1: paper_name = "A4"; break;
            case 2: paper_name = "A3"; break;
            case 3: paper_name = "Letter"; break;
            case 4: paper_name = "Legal"; break;
            case 5: paper_name = "A5"; break;
            case 6: paper_name = "A2"; break;
            case 7: paper_name = "A1"; break;
            case 8: paper_name = "A0"; break;
            case 9: paper_name = "A6"; break;
            case 10: paper_name = "A7"; break;
            case 11: paper_name = "A8"; break;
            case 12: paper_name = "B0"; break;
            case 13: paper_name = "B1"; break;
            case 14: paper_name = "B2"; break;
            case 15: paper_name = "B3"; break;
            case 16: paper_name = "B4"; break;
            case 17: paper_name = "B5"; break;
            case 18: paper_name = "B6"; break;
            case 19: paper_name = "B7"; break;
        }
        PE_LOG_EVENT("INFO", "PAPER_APPLY", "engine=%p | requested_id=%d | requested=%s", (void*)engine, options->paper_size, paper_name);
        devmode->dmFields |= DM_PAPERSIZE;
        switch (options->paper_size) {
            case 1: devmode->dmPaperSize = DMPAPER_A4;      break; // A4  210×297mm
            case 2: devmode->dmPaperSize = DMPAPER_A3;      break; // A3  297×420mm
            case 3: devmode->dmPaperSize = DMPAPER_LETTER;  break; // Letter 8.5×11in
            case 4: devmode->dmPaperSize = DMPAPER_LEGAL;   break; // Legal  8.5×14in
            case 5: devmode->dmPaperSize = DMPAPER_A5;      break; // A5  148×210mm
            case 6: // A2: 420×594mm
                    devmode->dmFields &= ~DM_PAPERSIZE;
                    devmode->dmPaperSize = 0;
                    devmode->dmPaperLength = 5940; devmode->dmPaperWidth = 4200;
                    devmode->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
            case 7: // A1: 594×841mm
                    devmode->dmFields &= ~DM_PAPERSIZE;
                    devmode->dmPaperSize = 0;
                    devmode->dmPaperLength = 8410; devmode->dmPaperWidth = 5940;
                    devmode->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
            case 8: // A0: 841×1189mm
                    devmode->dmFields &= ~DM_PAPERSIZE;
                    devmode->dmPaperSize = 0;
                    devmode->dmPaperLength = 11890; devmode->dmPaperWidth = 8410;
                    devmode->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
            case 9:  // A6: 105×148mm
                    devmode->dmFields &= ~DM_PAPERSIZE;
                    devmode->dmPaperSize = 0;
                    devmode->dmPaperLength = 1480; devmode->dmPaperWidth = 1050;
                    devmode->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
            case 10: // A7: 74×105mm
                    devmode->dmFields &= ~DM_PAPERSIZE;
                    devmode->dmPaperSize = 0;
                    devmode->dmPaperLength = 1050; devmode->dmPaperWidth = 740;
                    devmode->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
            case 11: // A8: 52×74mm
                    devmode->dmFields &= ~DM_PAPERSIZE;
                    devmode->dmPaperSize = 0;
                    devmode->dmPaperLength = 740; devmode->dmPaperWidth = 520;
                    devmode->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
            case 12: // B0: 1000×1414mm
                    devmode->dmFields &= ~DM_PAPERSIZE;
                    devmode->dmPaperSize = 0;
                    devmode->dmPaperLength = 14140; devmode->dmPaperWidth = 10000;
                    devmode->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
            case 13: // B1: 707×1000mm
                    devmode->dmFields &= ~DM_PAPERSIZE;
                    devmode->dmPaperSize = 0;
                    devmode->dmPaperLength = 10000; devmode->dmPaperWidth = 7070;
                    devmode->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
            case 14: // B2: 500×707mm
                    devmode->dmFields &= ~DM_PAPERSIZE;
                    devmode->dmPaperSize = 0;
                    devmode->dmPaperLength = 7070; devmode->dmPaperWidth = 5000;
                    devmode->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
            case 15: // B3: 353×500mm
                    devmode->dmFields &= ~DM_PAPERSIZE;
                    devmode->dmPaperSize = 0;
                    devmode->dmPaperLength = 5000; devmode->dmPaperWidth = 3530;
                    devmode->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
            case 16: // B4: 250×353mm
                    devmode->dmFields &= ~DM_PAPERSIZE;
                    devmode->dmPaperSize = 0;
                    devmode->dmPaperLength = 3530; devmode->dmPaperWidth = 2500;
                    devmode->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
            case 17: // B5: 176×250mm
                    devmode->dmFields &= ~DM_PAPERSIZE;
                    devmode->dmPaperSize = 0;
                    devmode->dmPaperLength = 2500; devmode->dmPaperWidth = 1760;
                    devmode->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
            case 18: // B6: 125×176mm
                    devmode->dmFields &= ~DM_PAPERSIZE;
                    devmode->dmPaperSize = 0;
                    devmode->dmPaperLength = 1760; devmode->dmPaperWidth = 1250;
                    devmode->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
            case 19: // B7: 88×125mm
                    devmode->dmFields &= ~DM_PAPERSIZE;
                    devmode->dmPaperSize = 0;
                    devmode->dmPaperLength = 1250; devmode->dmPaperWidth = 880;
                    devmode->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
            default: break;
        }
        // 清除自定义尺寸标志（避免与 dmPaperSize 冲突）——仅当非自定义尺寸时
        if (options->paper_size >= 1 && options->paper_size <= 5) {
            devmode->dmFields &= ~DM_PAPERLENGTH;
            devmode->dmFields &= ~DM_PAPERWIDTH;
        }

    }

    pe_log_devmode("DEVMODE_AFTER", (DEVMODEA*)devmode, devmode_result);
    
    // 创建设备上下文
    engine->hPrinterDC = CreateDC(nullptr, engine->printer_name, nullptr, devmode);
    
    if (devmode) free(devmode);
    
    if (!engine->hPrinterDC) {
        PE_LOG_EVENT("ERROR", "ENGINE_CREATE_END", "status=FAIL | engine=%p | printer=\"%s\" | stage=CreateDC | gle=%lu", (void*)engine, printer_utf8, GetLastError());
        ClosePrinter(hPrinter);
        set_error(engine, "Cannot create printer DC");
        delete engine;
        return nullptr;
    }

    pe_log_dc_caps(engine->hPrinterDC, "DC_CAPS");
    PE_LOG_EVENT("INFO", "ENGINE_CREATE_END", "status=OK | engine=%p | printer=\"%s\"", (void*)engine, printer_utf8);
    
    ClosePrinter(hPrinter);
    return engine;
}

void print_engine_set_callback(PrintEngine* engine, PrintCallback callback, void* user_data) {
    if (engine) {
        engine->callback = callback;
        engine->user_data = user_data;
    }
}

int print_engine_start_job(PrintEngine* engine, const char* document_title) {
    if (!engine || !engine->hPrinterDC) return -1;

    char title_utf8[2048];
    char printer_utf8[2048];
    pe_acp_to_utf8(document_title ? document_title : "MuPDF Document", title_utf8, sizeof(title_utf8));
    pe_acp_to_utf8(engine->printer_name, printer_utf8, sizeof(printer_utf8));
    PE_LOG_EVENT("INFO", "START_DOC_BEGIN", "engine=%p | printer=\"%s\" | title=\"%s\"", (void*)engine, printer_utf8, title_utf8);
    
    memset(&engine->docInfo, 0, sizeof(engine->docInfo));
    engine->docInfo.cbSize = sizeof(engine->docInfo);
    engine->docInfo.lpszDocName = document_title ? document_title : "MuPDF Document";
    engine->docInfo.lpszOutput = nullptr;
    engine->docInfo.lpszDatatype = nullptr;
    engine->docInfo.fwType = 0;
    
    // 设置双面打印模式
    if (engine->duplex) {
        DEVMODE* dm = (DEVMODE*)::GetWindowLongPtr((HWND)engine->hPrinterDC, GWLP_HINSTANCE);
        // 实际上需要使用 DocumentProperties 来设置
    }
    
    int job_id = StartDoc(engine->hPrinterDC, &engine->docInfo);
    if (job_id <= 0) {
        PE_LOG_EVENT("ERROR", "START_DOC_END", "status=FAIL | engine=%p | title=\"%s\" | job_id=%d | gle=%lu", (void*)engine, title_utf8, job_id, GetLastError());
        set_error(engine, "StartDoc failed");
        return -2;
    }
    
    engine->job_started = true;
    engine->current_page = 0;

    PE_LOG_EVENT("INFO", "START_DOC_END", "status=OK | engine=%p | title=\"%s\" | spool_job_id=%d", (void*)engine, title_utf8, job_id);
    
    return 0;
}

int print_engine_print_page(
    PrintEngine* engine,
    int page_width,
    int page_height,
    int bits_per_pixel,
    const unsigned char* image_data,
    int data_size
) {
    if (!engine || !engine->hPrinterDC || !engine->job_started) {
        PE_LOG_EVENT("ERROR", "PAGE_END", "status=FAIL | stage=VALIDATE | engine=%p | ret=-1 | reason=invalid_engine_or_job", (void*)engine);
        return -1;
    }
    if (!image_data || page_width <= 0 || page_height <= 0) {
        PE_LOG_EVENT("ERROR", "PAGE_END", "status=FAIL | stage=VALIDATE | engine=%p | ret=-1 | data=%p | pixel_size=%dx%d", (void*)engine, (const void*)image_data, page_width, page_height);
        return -1;
    }

    if (StartPage(engine->hPrinterDC) <= 0) {
        DWORD le = GetLastError();
        char err[256];
        _snprintf(err, sizeof(err), "[PrintEngine] StartPage FAILED on page %d, gle=%lu", engine->current_page + 1, le);
        set_error(engine, err);
        PE_LOG_EVENT("ERROR", "PAGE_END", "status=FAIL | stage=StartPage | engine=%p | page=%d | ret=-2 | gle=%lu", (void*)engine, engine->current_page + 1, le);
        return -2;
    }

    engine->current_page++;

    PE_LOG_EVENT("INFO", "PAGE_BEGIN", "engine=%p | page=%d | pixel_size=%dx%d | bpp=%d | data_size=%d | data_mb=%.3f | source_aspect=%.6f", (void*)engine, engine->current_page, page_width, page_height, bits_per_pixel, data_size, data_size / 1048576.0, page_height != 0 ? (float)page_width / page_height : 0.0f);

    // 获取打印机参数
    int printer_dpi_x = GetDeviceCaps(engine->hPrinterDC, LOGPIXELSX);
    int printer_dpi_y = GetDeviceCaps(engine->hPrinterDC, LOGPIXELSY);
    int printable_w = GetDeviceCaps(engine->hPrinterDC, HORZRES);
    int printable_h = GetDeviceCaps(engine->hPrinterDC, VERTRES);

    if (printer_dpi_x <= 0) printer_dpi_x = 600;
    if (printer_dpi_y <= 0) printer_dpi_y = 600;
    if (printable_w <= 0) printable_w = (int)((float)page_width * printer_dpi_x / 72.0f);
    if (printable_h <= 0) printable_h = (int)((float)page_height * printer_dpi_y / 72.0f);

    // MuPDF 渲染时使用的固定 DPI（与 mupdf_wrapper.cpp 保持一致）
    const int RENDER_DPI = 200;

    // 将图像像素尺寸转换为打印机像素尺寸（保持物理尺寸一致）
    // 例如：150 DPI 渲染的 1245px，在 600 DPI 打印机上 = 1245*600/150 = 4980 打印机像素
    int target_w = (int)((float)page_width * printer_dpi_x / RENDER_DPI);
    int target_h = (int)((float)page_height * printer_dpi_y / RENDER_DPI);

    // 如果目标尺寸超出可打印区域，按比例缩小以适应页面（不裁切内容）
    if (target_w > printable_w || target_h > printable_h) {
        float scale_x = (float)printable_w / target_w;
        float scale_y = (float)printable_h / target_h;
        float scale = (scale_x < scale_y) ? scale_x : scale_y;
        target_w = (int)(target_w * scale);
        target_h = (int)(target_h * scale);
    }

    // 计算居中偏移（在可打印区域内）
    int center_x = (printable_w - target_w) / 2;
    int center_y = (printable_h - target_h) / 2;
    if (center_x < 0) center_x = 0;
    if (center_y < 0) center_y = 0;

    float final_scale_x = page_width != 0 ? (float)target_w / page_width : 0.0f;
    float final_scale_y = page_height != 0 ? (float)target_h / page_height : 0.0f;
    float scale_diff_pct = final_scale_y != 0 ? (final_scale_x / final_scale_y - 1.0f) * 100.0f : 0.0f;
    PE_LOG_EVENT(
        fabs((double)scale_diff_pct) > 0.1 ? "WARN" : "INFO", "SCALE",
        "engine=%p | page=%d | render_dpi=%d | source=%dx%d | source_aspect=%.6f | printer_dpi=%dx%d | "
        "printable=%dx%d | target=%dx%d | target_aspect=%.6f | scale_x=%.6f | scale_y=%.6f | scale_diff_pct=%.3f | center=%d,%d",
        (void*)engine, engine->current_page, RENDER_DPI, page_width, page_height,
        page_height != 0 ? (float)page_width / page_height : 0.0f,
        printer_dpi_x, printer_dpi_y, printable_w, printable_h, target_w, target_h,
        target_h != 0 ? (float)target_w / target_h : 0.0f,
        final_scale_x, final_scale_y, scale_diff_pct, center_x, center_y);

    // 创建源 DIB BITMAPINFO
    // 注意：bmi.biHeight 必须与 image_data 方向一致（自上而下=负值）
    // SetDIBits 用 biHeight 的符号判断数据方向，无需数据预处理
    // image_data 是自上而下排列 → biHeight 必须为负
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = page_width;
    bmi.bmiHeader.biHeight = -page_height;  // 负值=自上而下 DIB，与 image_data 方向一致
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = (WORD)bits_per_pixel;
    bmi.bmiHeader.biCompression = BI_RGB;
    int src_row_bytes = page_width * (bits_per_pixel / 8);
    bmi.bmiHeader.biSizeImage = (DWORD)(src_row_bytes * page_height);

    PE_LOG_EVENT(
        data_size >= 3 ? "DEBUG" : "WARN", "DIB_SOURCE",
        "engine=%p | page=%d | bitmap_size=%dx%d | top_down=true | bpp=%d | source_row_bytes=%d | dib_size_image=%lu | first_pixel_rgb=%d,%d,%d | has_first_pixel=%s",
        (void*)engine, engine->current_page, page_width, page_height, bits_per_pixel, src_row_bytes,
        bmi.bmiHeader.biSizeImage,
        data_size >= 3 ? image_data[0] : -1,
        data_size >= 3 ? image_data[1] : -1,
        data_size >= 3 ? image_data[2] : -1,
        data_size >= 3 ? "true" : "false");

    // ================================================================
    // 使用内存 DC 中转方案：CreateCompatibleDC → SetDIBits → BitBlt
    // 这是最可靠的大图打印方式，避免 StretchDIBits 直接操作大图像导致驱动崩溃
    // ================================================================

    // 1. 创建内存 DC
    HDC hMemDC = CreateCompatibleDC(engine->hPrinterDC);
    if (!hMemDC) {
        char err[128];
        DWORD le = GetLastError();
        _snprintf(err, sizeof(err), "CreateCompatibleDC failed, gle=%lu", le);
        set_error(engine, err);
        PE_LOG_EVENT("ERROR", "PAGE_END", "status=FAIL | stage=CreateCompatibleDC | engine=%p | page=%d | ret=-3 | gle=%lu", (void*)engine, engine->current_page, le);
        EndPage(engine->hPrinterDC);
        return -3;
    }

    // 2. 创建 DIB Section（分配像素存储）
    void* bits_ptr = nullptr;
    HBITMAP hBitmap = CreateDIBSection(
        hMemDC,
        &bmi,
        DIB_RGB_COLORS,
        &bits_ptr,
        nullptr,
        0
    );
    if (!hBitmap || !bits_ptr) {
        char err[128];
        DWORD le = GetLastError();
        _snprintf(err, sizeof(err), "CreateDIBSection failed, gle=%lu", le);
        set_error(engine, err);
        PE_LOG_EVENT("ERROR", "PAGE_END", "status=FAIL | stage=CreateDIBSection | engine=%p | page=%d | ret=-4 | gle=%lu", (void*)engine, engine->current_page, le);
        DeleteDC(hMemDC);
        EndPage(engine->hPrinterDC);
        return -4;
    }

    // 3. 直接将图像数据复制到 DIB Section 的像素缓冲区
    // 注意：bits_ptr 指向 DIB 像素数据，需根据 biHeight 符号判断排列方向
    // biHeight < 0 → 自上而下（top-down），第 0 行 = 图像顶部
    // biHeight > 0 → 底朝上（bottom-up），第 0 行 = 图像底部
    if (bits_ptr && image_data) {
        int src_stride = page_width * (bits_per_pixel / 8);
        int dst_stride = ((page_width * bits_per_pixel + 31) / 32) * 4; // DIB 行对齐到 4 字节
        int bytes_per_pixel = bits_per_pixel / 8;

        // MuPDF 渲染输出是 RGB 格式，Windows GDI DIB 要求 BGR 格式
        // 必须逐像素做 R↔B 交换，否则在严格遵守 BGR 的打印机驱动（如立思辰）上红色会变蓝色
        if (bmi.bmiHeader.biHeight < 0) {
            // 自上而下：逐行拷贝并做 RGB→BGR 转换
            for (int y = 0; y < page_height; y++) {
                const unsigned char* src_row = image_data + (size_t)y * src_stride;
                unsigned char* dst_row = (unsigned char*)bits_ptr + (size_t)y * dst_stride;
                if (bytes_per_pixel == 3) {
                    for (int x = 0; x < page_width; x++) {
                        dst_row[x*3 + 0] = src_row[x*3 + 2]; // B ← R
                        dst_row[x*3 + 1] = src_row[x*3 + 1]; // G ← G
                        dst_row[x*3 + 2] = src_row[x*3 + 0]; // R ← B
                    }
                } else {
                    memcpy(dst_row, src_row, src_stride);
                }
            }
        } else {
            // 底朝上：垂直翻转并做 RGB→BGR 转换
            for (int y = 0; y < page_height; y++) {
                const unsigned char* src_row = image_data + (size_t)y * src_stride;
                unsigned char* dst_row = (unsigned char*)bits_ptr + (size_t)(page_height - 1 - y) * dst_stride;
                if (bytes_per_pixel == 3) {
                    for (int x = 0; x < page_width; x++) {
                        dst_row[x*3 + 0] = src_row[x*3 + 2]; // B ← R
                        dst_row[x*3 + 1] = src_row[x*3 + 1]; // G ← G
                        dst_row[x*3 + 2] = src_row[x*3 + 0]; // R ← B
                    }
                } else {
                    memcpy(dst_row, src_row, src_stride);
                }
            }
        }
    } else {
        char err[256];
        _snprintf(err, sizeof(err), "[PrintEngine] Invalid bits_ptr=%p or image_data=%p on page %d",
            bits_ptr, (const void*)image_data, engine->current_page);
        set_error(engine, err);
        PE_LOG_EVENT("ERROR", "PAGE_END", "status=FAIL | stage=CopyPixels | engine=%p | page=%d | ret=-5 | bits_ptr=%p | image_data=%p", (void*)engine, engine->current_page, bits_ptr, (const void*)image_data);
        DeleteObject(hBitmap);
        DeleteDC(hMemDC);
        EndPage(engine->hPrinterDC);
        return -5;
    }

    // 4. 将位图选入内存 DC

    // 4. 将位图选入内存 DC
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);

    // 5. 设置打印机 DC 的缩放模式
    int previous_stretch_mode = SetStretchBltMode(engine->hPrinterDC, COLORONCOLOR);
    PE_LOG_EVENT(previous_stretch_mode != 0 ? "DEBUG" : "WARN", "GDI_STRETCH_MODE", "engine=%p | page=%d | requested=COLORONCOLOR | previous_mode=%d | gle=%lu", (void*)engine, engine->current_page, previous_stretch_mode, previous_stretch_mode != 0 ? ERROR_SUCCESS : GetLastError());

    // 6. 将图像从内存 DC 传输到打印机 DC（自动处理缩放）
    // 注意：BitBlt 不做缩放，必须使用 StretchBlt 才能正确缩放
    BOOL ok = StretchBlt(
        engine->hPrinterDC,
        center_x, center_y,
        target_w, target_h,
        hMemDC,
        0, 0,
        page_width, page_height,
        SRCCOPY
    );

    // 7. 恢复并清理
    SelectObject(hMemDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);

    if (!ok) {
        char err[256];
        DWORD le = GetLastError();
        _snprintf(err, sizeof(err), "[PrintEngine] StretchBlt FAILED on page %d, gle=%lu", engine->current_page, le);
        set_error(engine, err);
        PE_LOG_EVENT("ERROR", "PAGE_END", "status=FAIL | stage=StretchBlt | engine=%p | page=%d | ret=-6 | gle=%lu | source=%dx%d | target=%dx%d", (void*)engine, engine->current_page, le, page_width, page_height, target_w, target_h);
        EndPage(engine->hPrinterDC);
        return -6;
    }

    PE_LOG_EVENT("INFO", "GDI_RESULT", "status=OK | engine=%p | page=%d | operation=StretchBlt | source=%dx%d | target=%dx%d | center=%d,%d", (void*)engine, engine->current_page, page_width, page_height, target_w, target_h, center_x, center_y);

    int end_page_ret = EndPage(engine->hPrinterDC);
    if (end_page_ret <= 0) {
        DWORD le = GetLastError();
        char err[256];
        _snprintf(err, sizeof(err), "[PrintEngine] EndPage FAILED on page %d, gle=%lu", engine->current_page, le);
        set_error(engine, err);
        PE_LOG_EVENT("ERROR", "PAGE_END", "status=FAIL | stage=EndPage | engine=%p | page=%d | ret=-7 | end_page_ret=%d | gle=%lu", (void*)engine, engine->current_page, end_page_ret, le);
        return -7;
    }

    // 调用回调
    if (engine->callback) {
        if (engine->callback(engine->current_page, engine->page_count, engine->user_data) != 0) {
            PE_LOG_EVENT("WARN", "PAGE_END", "status=CANCELLED | stage=CALLBACK | engine=%p | page=%d | ret=-8", (void*)engine, engine->current_page);
            return -8; // 用户取消
        }
    }

    PE_LOG_EVENT("INFO", "PAGE_END", "status=OK | engine=%p | page=%d | ret=0 | end_page_ret=%d", (void*)engine, engine->current_page, end_page_ret);

    return 0;
}

int print_engine_end_job(PrintEngine* engine) {
    if (!engine || !engine->hPrinterDC) return -1;
    
    if (engine->job_started) {
        int end_doc_ret = EndDoc(engine->hPrinterDC);
        PE_LOG_EVENT(end_doc_ret > 0 ? "INFO" : "ERROR", "END_DOC", "status=%s | engine=%p | pages=%d | ret=%d | gle=%lu", end_doc_ret > 0 ? "OK" : "FAIL", (void*)engine, engine->current_page, end_doc_ret, end_doc_ret > 0 ? ERROR_SUCCESS : GetLastError());
        engine->job_started = false;
    }
    
    return 0;
}

void print_engine_cancel_job(PrintEngine* engine) {
    if (engine && engine->hPrinterDC && engine->job_started) {
        AbortDoc(engine->hPrinterDC);
        engine->job_started = false;
    }
}

void print_engine_destroy(PrintEngine* engine) {
    if (!engine) return;

    PE_LOG_EVENT("INFO", "ENGINE_DESTROY", "engine=%p | job_started=%s | pages=%d", (void*)engine, engine->job_started ? "true" : "false", engine->current_page);
    
    if (engine->job_started) {
        EndDoc(engine->hPrinterDC);
    }
    
    if (engine->hPrinterDC) {
        DeleteDC(engine->hPrinterDC);
    }
    
    delete engine;
}

int print_engine_reset_paper(PrintEngine* engine, int paper_size) {
    if (!engine || !engine->hPrinterDC) {
        PE_LOG_EVENT("ERROR", "PAPER_RESET_END", "status=FAIL | engine=%p | requested_id=%d | requested=%s | ret=-1 | reason=invalid_engine", (void*)engine, paper_size, pe_paper_name(paper_size));
        return -1;
    }

    PE_LOG_EVENT("INFO", "PAPER_RESET_BEGIN", "engine=%p | requested_id=%d | requested=%s | current_page=%d | job_started=%s", (void*)engine, paper_size, pe_paper_name(paper_size), engine->current_page, engine->job_started ? "true" : "false");
    
    // 只能在上一次 EndPage 之后、下一次 StartPage 之前调用
    if (engine->job_started && engine->current_page > 0) {
        // 当前处于页面中间，不能调用
    }
    
    // 获取当前打印机的 DEVMODE
    HANDLE hPrinter;
    if (!OpenPrinter(engine->printer_name, &hPrinter, nullptr)) {
        set_error(engine, "Cannot open printer for reset_paper");
        return -2;
    }
    
    DWORD needed = 0;
    GetPrinter(hPrinter, 2, nullptr, 0, &needed);
    
    if (needed == 0) {
        ClosePrinter(hPrinter);
        set_error(engine, "GetPrinter failed for reset_paper");
        return -3;
    }
    
    PRINTER_INFO_2* pi2 = (PRINTER_INFO_2*)malloc(needed);
    if (!GetPrinter(hPrinter, 2, (LPBYTE)pi2, needed, &needed)) {
        free(pi2);
        ClosePrinter(hPrinter);
        set_error(engine, "GetPrinter failed for reset_paper");
        return -4;
    }
    
    // 获取当前的 DEVMODE（从 PRINTER_INFO_2）
    DEVMODE* dm = nullptr;
    if (pi2->pDevMode) {
        DWORD dmSize = pi2->pDevMode->dmSize + pi2->pDevMode->dmDriverExtra;
        dm = (DEVMODE*)malloc(dmSize);
        memcpy(dm, pi2->pDevMode, dmSize);
    }
    free(pi2);
    ClosePrinter(hPrinter);
    
    if (!dm) {
        set_error(engine, "Cannot get DEVMODE for reset_paper");
        return -5;
    }

    pe_log_devmode("PAPER_RESET_DEVMODE_BEFORE", (DEVMODEA*)dm, IDOK);
    
    // 设置纸张大小
    dm->dmFields |= DM_PAPERSIZE;
    switch (paper_size) {
        case 1: dm->dmPaperSize = DMPAPER_A4;      break;
        case 2: dm->dmPaperSize = DMPAPER_A3;      break;
        case 3: dm->dmPaperSize = DMPAPER_LETTER;  break;
        case 4: dm->dmPaperSize = DMPAPER_LEGAL;   break;
        case 5: dm->dmPaperSize = DMPAPER_A5;      break;
        case 6: // A2: 420×594mm
                dm->dmFields &= ~DM_PAPERSIZE;
                dm->dmPaperSize = 0;
                dm->dmPaperLength = 5940; dm->dmPaperWidth = 4200;
                dm->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
        case 7: // A1: 594×841mm
                dm->dmFields &= ~DM_PAPERSIZE;
                dm->dmPaperSize = 0;
                dm->dmPaperLength = 8410; dm->dmPaperWidth = 5940;
                dm->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
        case 8: // A0: 841×1189mm
                dm->dmFields &= ~DM_PAPERSIZE;
                dm->dmPaperSize = 0;
                dm->dmPaperLength = 11890; dm->dmPaperWidth = 8410;
                dm->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
        case 9:  // A6: 105×148mm
                dm->dmFields &= ~DM_PAPERSIZE;
                dm->dmPaperSize = 0;
                dm->dmPaperLength = 1480; dm->dmPaperWidth = 1050;
                dm->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
        case 10: // A7: 74×105mm
                dm->dmFields &= ~DM_PAPERSIZE;
                dm->dmPaperSize = 0;
                dm->dmPaperLength = 1050; dm->dmPaperWidth = 740;
                dm->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
        case 11: // A8: 52×74mm
                dm->dmFields &= ~DM_PAPERSIZE;
                dm->dmPaperSize = 0;
                dm->dmPaperLength = 740; dm->dmPaperWidth = 520;
                dm->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
        case 12: // B0: 1000×1414mm
                dm->dmFields &= ~DM_PAPERSIZE;
                dm->dmPaperSize = 0;
                dm->dmPaperLength = 14140; dm->dmPaperWidth = 10000;
                dm->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
        case 13: // B1: 707×1000mm
                dm->dmFields &= ~DM_PAPERSIZE;
                dm->dmPaperSize = 0;
                dm->dmPaperLength = 10000; dm->dmPaperWidth = 7070;
                dm->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
        case 14: // B2: 500×707mm
                dm->dmFields &= ~DM_PAPERSIZE;
                dm->dmPaperSize = 0;
                dm->dmPaperLength = 7070; dm->dmPaperWidth = 5000;
                dm->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
        case 15: // B3: 353×500mm
                dm->dmFields &= ~DM_PAPERSIZE;
                dm->dmPaperSize = 0;
                dm->dmPaperLength = 5000; dm->dmPaperWidth = 3530;
                dm->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
        case 16: // B4: 250×353mm
                dm->dmFields &= ~DM_PAPERSIZE;
                dm->dmPaperSize = 0;
                dm->dmPaperLength = 3530; dm->dmPaperWidth = 2500;
                dm->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
        case 17: // B5: 176×250mm
                dm->dmFields &= ~DM_PAPERSIZE;
                dm->dmPaperSize = 0;
                dm->dmPaperLength = 2500; dm->dmPaperWidth = 1760;
                dm->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
        case 18: // B6: 125×176mm
                dm->dmFields &= ~DM_PAPERSIZE;
                dm->dmPaperSize = 0;
                dm->dmPaperLength = 1760; dm->dmPaperWidth = 1250;
                dm->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
        case 19: // B7: 88×125mm
                dm->dmFields &= ~DM_PAPERSIZE;
                dm->dmPaperSize = 0;
                dm->dmPaperLength = 1250; dm->dmPaperWidth = 880;
                dm->dmFields |= DM_PAPERLENGTH | DM_PAPERWIDTH; break;
        default: 
            PE_LOG_EVENT("WARN", "PAPER_RESET_END", "status=SKIPPED | engine=%p | requested_id=%d | requested=%s | ret=0 | reason=unsupported_paper_id", (void*)engine, paper_size, pe_paper_name(paper_size));
            free(dm);
            return 0;
    }
    // 清除自定义尺寸标志（避免与 dmPaperSize 冲突）——仅当非自定义尺寸时
    if (paper_size >= 1 && paper_size <= 5) {
        dm->dmFields &= ~DM_PAPERLENGTH;
        dm->dmFields &= ~DM_PAPERWIDTH;
    }

    pe_log_devmode("PAPER_RESET_DEVMODE_AFTER", (DEVMODEA*)dm, IDOK);

    // ResetDC 动态切换纸张（必须在 StartPage 之前调用）
    HDC newDC = ResetDC(engine->hPrinterDC, dm);
    free(dm);
    
    if (!newDC) {
        char err[128];
        DWORD le = GetLastError();
        _snprintf(err, sizeof(err), "ResetDC failed, gle=%lu", le);
        set_error(engine, err);
        PE_LOG_EVENT("ERROR", "PAPER_RESET_END", "status=FAIL | engine=%p | requested_id=%d | requested=%s | ret=-6 | gle=%lu", (void*)engine, paper_size, pe_paper_name(paper_size), le);
        return -6;
    }

    pe_log_dc_caps(newDC, "PAPER_RESET_DC_CAPS");
    PE_LOG_EVENT("INFO", "PAPER_RESET_END", "status=OK | engine=%p | requested_id=%d | requested=%s | ret=0", (void*)engine, paper_size, pe_paper_name(paper_size));
    
    return 0;
}

const char* print_engine_get_error(PrintEngine* engine) {
    if (!engine) return "Null engine";
    return engine->error_msg[0] ? engine->error_msg : "No error";
}

int print_simple(
    const char* printer_name,
    int page_width,
    int page_height,
    const unsigned char* image_data,
    int page_count,
    int copies,
    int duplex
) {
    PrintOptions opts = print_options_default();
    opts.copies = copies;
    opts.duplex = duplex;
    
    PrintEngine* engine = print_engine_create(printer_name, &opts);
    if (!engine) return -1;
    
    engine->page_count = page_count;
    
    if (print_engine_start_job(engine, "MuPDF Print") != 0) {
        print_engine_destroy(engine);
        return -2;
    }
    
    for (int i = 0; i < page_count; i++) {
        int ret = print_engine_print_page(
            engine, page_width, page_height, 24,
            image_data + (size_t)i * page_width * page_height * 3,
            page_width * page_height * 3
        );
        if (ret != 0) {
            print_engine_cancel_job(engine);
            print_engine_destroy(engine);
            return -3;
        }
    }
    
    print_engine_end_job(engine);
    print_engine_destroy(engine);
    
    return 0;
}

#else // !_WIN32

// 非 Windows 平台的占位实现
struct PrintEngine { char _dummy; };

PrintOptions print_options_default(void) { PrintOptions o = {0}; return o; }
int print_get_printers(PrinterInfo*, int) { return 0; }
int print_get_default_printer(char*, int) { return -1; }
int print_is_printer_ready(const char*) { return 0; }
PrintEngine* print_engine_create(const char*, const PrintOptions*) { return nullptr; }
void print_engine_set_callback(PrintEngine*, PrintCallback, void*) {}
int print_engine_start_job(PrintEngine*, const char*) { return -1; }
int print_engine_print_page(PrintEngine*, int, int, int, const unsigned char*, int) { return -1; }
int print_engine_end_job(PrintEngine*) { return -1; }
void print_engine_cancel_job(PrintEngine*) {}
void print_engine_destroy(PrintEngine*) {}
const char* print_engine_get_error(PrintEngine*) { return "Not implemented on this platform"; }
int print_simple(const char*, int, int, const unsigned char*, int, int, int) { return -1; }

#endif // _WIN32
