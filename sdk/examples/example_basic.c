/**
 * @file example_basic.c
 * @brief 基础示例：打开 PDF、添加图片水印、添加文字水印、保存
 *
 * 编译（假设 SDK 放在项目根目录的 sdk 文件夹下）：
 *   cl /EHsc /MD example_basic.c sdk\lib\mupdf_wrapper.lib user32.lib
 *
 * 运行前将 sdk\mupdf_wrapper.dll 复制到 exe 同目录。
 *
 * 运行前将 mupdf_wrapper.dll 复制到 exe 同目录。
 */

#include "mupdf_wrapper.h"
#include <stdio.h>
#include <string.h>

static void print_error(MupdfContext* ctx, const char* msg) {
    printf("[错误] %s: %s\n", msg, mupdf_get_error(ctx));
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;  /* 防止未使用警告 */

    const char* input_pdf  = "input.pdf";
    const char* output_pdf = "output_watermarked.pdf";
    const char* img_path   = "watermark.png";  /* 水印图片路径 */

    MupdfContext* ctx = NULL;
    MupdfDocument* doc = NULL;

    /* 1. 初始化 */
    if (mupdf_init(&ctx) != 0) {
        printf("[错误] 初始化失败: %s\n", mupdf_get_error(ctx));
        return 1;
    }
    printf("[OK] MuPDF Wrapper 已初始化，版本: %s\n", mupdf_get_version());

    /* 2. 打开 PDF */
    printf("[INFO] 打开 PDF: %s\n", input_pdf);
    if (mupdf_open_document(ctx, input_pdf, &doc) != 0) {
        print_error(ctx, "打开 PDF 失败");
        mupdf_fini(ctx);
        return 1;
    }

    /* 3. 获取页面信息 */
    {
        int count = 0;
        mupdf_get_page_count(ctx, doc, &count);
        printf("[INFO] PDF 共 %d 页\n", count);

        for (int i = 0; i < count && i < 3; i++) {
            float w = 0, h = 0;
            mupdf_get_page_size(ctx, doc, i, &w, &h);
            printf("       第 %d 页: %.1f x %.1f pt  %s\n",
                   i + 1, w, h,
                   mupdf_is_page_landscape(ctx, doc, i) ? "(横向)" : "(纵向)");
        }
    }

    /* 4. 批量添加图片水印（居中） */
    {
        LayoutRule rule = {0};
        rule.type = 5;          /* 居中 */
        rule.margin_x = 0;
        rule.margin_y = 0;

        int added = mupdf_batch_add_image(ctx, doc, NULL, -1,
                                           img_path, &rule, 0, 0);
        if (added < 0) {
            print_error(ctx, "添加图片失败");
        } else {
            printf("[OK] 图片水印已添加到 %d 页\n", added);
        }
    }

    /* 5. 批量添加文字水印（每页右上角） */
    {
        LayoutRule rule = {0};
        rule.type = 2;          /* 右上 */
        rule.margin_x = 20.0f;
        rule.margin_y = 20.0f;

        int added = mupdf_batch_add_text(ctx, doc, NULL, -1,
                                          "CONFIDENTIAL", 18.0f, &rule);
        if (added < 0) {
            print_error(ctx, "添加文字失败");
        } else {
            printf("[OK] 文字水印已添加到 %d 页\n", added);
        }
    }

    /* 6. 保存 */
    printf("[INFO] 保存 PDF: %s\n", output_pdf);
    if (mupdf_save_document(ctx, doc, output_pdf) != 0) {
        print_error(ctx, "保存失败");
        mupdf_close_document(ctx, doc);
        mupdf_fini(ctx);
        return 1;
    }
    printf("[OK] 保存成功!\n");

    /* 7. 清理 */
    mupdf_close_document(ctx, doc);
    mupdf_fini(ctx);
    printf("[INFO] 资源已释放，程序正常退出。\n");
    return 0;
}
