/**
 * @file example_print.c
 * @brief 打印示例：打印 PDF 指定页面到指定打印机
 *
 * 编译：
 *   cl /EHsc /MD example_print.c sdk\lib\mupdf_wrapper.lib user32.lib winspool.lib
 */

#include "mupdf_wrapper.h"
#include "print_engine.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    const char* input_pdf = "input.pdf";
    const char* printer   = NULL;   /* NULL = 默认打印机 */

    MupdfContext* ctx = NULL;
    MupdfDocument* doc = NULL;

    /* 打印前先列出可用打印机 */
    {
        PrinterInfo printers[16];
        int n = print_get_printers(printers, 16);
        printf("检测到 %d 台打印机:\n", n);
        for (int i = 0; i < n; i++) {
            printf("  [%d] %s%s%s\n", i + 1, printers[i].name,
                   printers[i].is_default ? " (默认)" : "",
                   printers[i].is_ready ? " [就绪]" : " [离线]");
        }
        printf("\n");
    }

    /* 初始化 */
    if (mupdf_init(&ctx) != 0) {
        printf("[错误] 初始化失败\n");
        return 1;
    }

    /* 打开 PDF */
    if (mupdf_open_document(ctx, input_pdf, &doc) != 0) {
        printf("[错误] 打开 PDF 失败: %s\n", mupdf_get_error(ctx));
        mupdf_fini(ctx);
        return 1;
    }

    /* 打印选项 */
    PrintOptions opts = {0};
    opts.copies   = 1;        /* 打印 1 份 */
    opts.duplex   = 1;        /* 1=单面, 2=长边翻转双面, 3=短边翻转双面 */
    opts.collate  = 1;        /* 分拣 */
    opts.color    = 2;        /* 1=黑白, 2=彩色 */
    opts.scale    = 0;        /* 自动适应纸张 */
    opts.orientation = 0;    /* 自动方向 */
    opts.from_page = 1;      /* 打印第 1 页起 */
    opts.to_page   = 0;       /* 0 = 打印到最后一页 */
    opts.job_name = "MuPDF Wrapper 打印任务";

    /* 打印整个文档 */
    printf("[INFO] 开始打印到 %s...\n",
           printer ? printer : "(默认打印机)");
    int ret = mupdf_print(ctx, doc, printer, &opts);

    if (ret == 0) {
        printf("[OK] 打印完成\n");
    } else {
        printf("[错误] 打印失败: %s\n", mupdf_get_error(ctx));
    }

    mupdf_close_document(ctx, doc);
    mupdf_fini(ctx);
    return (ret == 0) ? 0 : 1;
}
