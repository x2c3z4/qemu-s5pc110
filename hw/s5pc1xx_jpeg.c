/*
 * JPEG codec for Samsung S5PC110-based board emulation
 *
 * Copyright (c) 2010 Samsung Electronics.
 * Contributed by Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 */

#include "qemu-common.h"
#include "sysbus.h"

#ifdef CONFIG_JPEG
#include <stdio.h>
#include <jpeglib.h>
#endif

/* Control registers */
#define JPGMOD      0x000       /* R/W Specifies the Sub-sampling Mode Register 0x0000_0000 */
    #define PROC_MODE           (1 << 3)

#define JPGOPR      0x004       /* R Specifies the Operation Status Register 0x0000_0000 */
#define QTBL        0x008       /* R/W Specifies the Quantization Table Number Register 0x0000_0000 */
#define HTBL        0x00C       /* R/W Specifies the Huffman Table Number Register 0x0000_0000 */
#define JPGDRI_U    0x010       /* R/W Specifies the MCU, which inserts RST marker(upper 8-bit) 0x0000_0000 */
#define JPGDRI_L    0x014       /* R/W Specifies the MCU, which inserts RST marker (lower 8-bit) 0x0000_0000 */
#define JPGY_U      0x018       /* R/W Specifies the Vertical Resolution (upper 8-bit) 0x0000_0000 */
#define JPGY_L      0x01C       /* R/W Specifies the Vertical Resolution (lower 8-bit) 0x0000_0000 */
#define JPGX_U      0x020       /* R/W Specifies the Horizontal Resolution (upper 8-bit) 0x0000_0000 */
#define JPGX_L      0x024       /* R/W Specifies the Horizontal Resolution (lower 8-bit) 0x0000_0000 */
#define JPGCNT_U    0x028       /* R Specifies the amount of the compressed data in bytes (upper 8- bit) 0x0000_0000 */
#define JPGCNT_M    0x02C       /* R Specifies the amount of the compressed data in bytes (middle 8-bit) 0x0000_0000 */
#define JPGCNT_L    0x030       /* R Specifies the amount of the compressed data in bytes (lower 8-bit) 0x0000_0000 */
#define JPGINTSE    0x034       /* R/W Specifies the Interrupt Setting Register 0x0000_0000 */
#define JPGINTST    0x038       /* R Specifies the Interrupt Status Register 0x0000_0000 */
    #define RESULT_STAT         (1 << 6)
    #define STREAM_STAT         (1 << 5)

#define JPGCOM      0x04C       /* W Specifies the Command register 0x0000_0000 */
    #define INT_RELEASE         (1 << 2)

#define IMGADR      0x050       /* R/W Specifies the Source or Destination Image Address 0x0000_0000 */
#define JPGADR      0x058       /* R/W Specifies the Source or Destination JPEG File Address 0x0000_0000 */
#define COEF1       0x05C       /* R/W Specifies the Coefficient Values for RGB <-> YCbCr Converter 0x0000_0000 */
#define COEF2       0x060       /* R/W Specifies the Coefficient Values for RGB <-> YCbCr Converter 0x0000_0000 */
#define COEF3       0x064       /* R/W Specifies the Coefficient Values for RGB <-> YCbCr Converter 0x0000_0000 */
#define JPGCMOD     0x068       /* R/W Specifies the Mode Selection and Core Clock Setting 0x0000_0020 */
#define JPGCLKCON   0x06C       /* R/W Specifies the Power On/ Off and Clock Down Control 0x0000_0002 */
#define JSTART      0x070       /* W Specifies the Start Compression or Decompression 0x0000_0000 */
#define SW_RESET    0x078       /* R/W Specifies the S/W Reset 0x0000_0000 */
#define TIMER_SE    0x07C       /* R/W Specifies the Internal Timer Setting Register 0x7FFF_FFFF */
    #define TIMER_INT_EN        (1 << 31)

#define TIMER_ST    0x080       /* R/W Specifies the Internal Timer Status Register 0x7FFF_FFFF */
    #define TIMER_INT_STAT      (1 << 31)

#define COMSTAT     0x084       /* R Specifies the Command Status Register 0x0000_0000 */
#define OUTFORM     0x088       /* R/W Specifies the Output Color Format of Decompression 0x0000_0000 */
#define VERSION     0x08C       /* R Specifies the Version Register 0x0000_0003 */
#define ENC_STREAM_INTSE 0x098  /* R/W Specifies the Compressed Stream Size Interrupt Setting Register 0x00FF_FFE0 */
    #define ENC_STREAM_INT_EN   (1 << 24)

#define ENC_STREAM_INTST 0x09C  /* R Specifies the Compressed Stream Size Interrupt Status Register 0x0000_0000 */
    #define ENC_STREAM_INT_STAT (1 << 0)

#define QTBL0(n)    (0x400 + (n << 2))   /* R/W Specifies the Quantization table 0 0x0000_0000 */
#define QTBL1(n)    (0x500 + (n << 2))   /* R/W Specifies the Quantization table 1 0x0000_0000 */
#define QTBL2(n)    (0x600 + (n << 2))   /* R/W Specifies the Quantization table 2 0x0000_0000 */
#define QTBL3(n)    (0x700 + (n << 2))   /* R/W Specifies the Quantization table 3 0x0000_0000 */
#define HDCTBL0(n)  (0x800 + (n << 2))   /* W Specifies the Huffman DC Table 0 - the number of code per code length 0x0000_0000 */
#define HDCTBLG0(n) (0x840 + (n << 2))   /* W Specifies the Huffman DC Table 0 - Group number of the order for occurrence 0x0000_0000 */
#define HACTBL0(n)  (0x880 + (n << 2))   /* W Specifies the Huffman AC Table 0 - the number of code per code length 0x0000_0000 */
#define HACTBLG0(n) (0x8C0 + (n << 2))   /* W Specifies the Huffman AC Table 0 - Group number of the order for occurrence 0x0000_0000 */
#define HDCTBL1(n)  (0xC00 + (n << 2))   /* W Specifies the Huffman DC Table 1 - the number of code per code length 0x0000_0000 */
#define HDCTBLG1(n) (0xC40 + (n << 2))   /* W Specifies the Huffman DC Table 1 - Group number of the order for occurrence 0x0000_0000 */
#define HACTBL1(n)  (0xC80 + (n << 2))   /* W Specifies the Huffman AC Table 1 - the number of code per code length 0x0000_0000 */
#define HACTBLG1(n) (0xCC0 + (n << 2))   /* W Specifies the Huffman AC Table 1 - Group number of the order for occurrence 0x0000_0000 */

#define S5PC1XX_JPEG_REG_MEM_SIZE   0xF48

/* FIXME: jpeg header size (8192 bytes) is an experimental value */
#define JPEG_HDR_SIZE   8192
#define MAX_IMG_SIZE   (8192 * 8192 * 3)

/* Control values */
#define COMPR           0
#define DECOMPR         1

#define YCbCr_422    (((4 + 2 + 2) * 2) | (16 << 5) | (8  << 10))  /* YCbCr4:2:2, x2 bits a sample, MCU block size = 16x8 */
#define RGB_565        (5 + 6 + 5)                                 /* RGB565, x1 bits a sample */

#define YCbCr_444    (((4 + 4 + 4) * 2) | (8  << 5) | (8  << 10))
#define YCbCr_420    (((4 + 2 + 0) * 2) | (16 << 5) | (16 << 10))
#define GRAY         (((4 + 0 + 0) * 2) | (8  << 5) | (8  << 10))

#define JUST_RAISE_IRQ   0
#define RESULT_INT      (1 << 0)
#define STREAM_INT      (1 << 1)
#define TIMER_INT       (1 << 2)
#define ENC_STREAM_INT  (1 << 3)

#define LOW             1
#define HIGH            0
#define NONE            0

/* Arithmetical macroses */
#define SIZE_OUT(value, min)    ((((value) + (min) - 1) / (min)) * (min))   /* evaluate a length in 'min' units */
#define ALL_BITS(b,a)           (((1 << ((b) - (a) + 1)) - 1) << (a))       /* all bits from 'b' to 'a' are high */


#ifdef CONFIG_JPEG
typedef struct jpeg_compress_struct CInfo;
typedef struct jpeg_decompress_struct DInfo;

typedef struct Dummy_CInfo {
    JDIMENSION      image_width;
    JDIMENSION      image_height;
    J_COLOR_SPACE   in_color_space;
    J_COLOR_SPACE   jpeg_color_space;
    struct comp_info {
        int h_samp_factor;
        int v_samp_factor;
    } comp_info[3];
} Dummy_CInfo;

typedef struct Dummy_DInfo {
    JDIMENSION      image_width;
    JDIMENSION      image_height;
    J_COLOR_SPACE   out_color_space;
} Dummy_DInfo;
#endif

typedef struct S5pc1xxJpegState {
    SysBusDevice busdev;
    qemu_irq     irq;
    uint8_t      proc_mode, c1;
    uint32_t     byte_cnt, src_len, dst_len;
    uint32_t     mode_in, mode_out;
    target_phys_addr_t src_addr, dst_addr;
    char         *src_base, *dst_base;

    /* Control registers */
    uint32_t     jpgmod;
    uint32_t     jpgopr;
    uint32_t     qtbl;
    uint32_t     htbl;
    uint32_t     jpgdri_u;
    uint32_t     jpgdri_l;
    uint32_t     jpgy_u;
    uint32_t     jpgy_l;
    uint32_t     jpgx_u;
    uint32_t     jpgx_l;
    uint32_t     jpgintse;
    uint32_t     jpgintst;
    uint32_t     imgadr;
    uint32_t     jpgadr;
    uint32_t     coef1;
    uint32_t     coef2;
    uint32_t     coef3;
    uint32_t     jpgcmod;
    uint32_t     jpgclkcon;
    uint32_t     sw_reset;
    uint32_t     timer_se;
    uint32_t     timer_st;
    uint32_t     comstat;
    uint32_t     outform;
    uint32_t     version;
    uint32_t     enc_stream_intse;
    uint32_t     enc_stream_intst;
    uint8_t      qtbl0[64];
    uint8_t      qtbl1[64];
    uint8_t      qtbl2[64];
    uint8_t      qtbl3[64];
    uint8_t      hdctbl0[28];
    uint8_t      hactbl0[178];
    uint8_t      hdctbl1[28];
    uint8_t      hactbl1[178];

#ifdef CONFIG_JPEG
    /* Two structures below are used to protect corresponding values
     * in CInfo and DInfo from overwriting */
    Dummy_CInfo *dummy_cinfo;
    Dummy_DInfo *dummy_dinfo;

    CInfo *cinfo;
    DInfo *dinfo;
#endif
} S5pc1xxJpegState;

static void s5pc1xx_jpeg_irq(S5pc1xxJpegState *s,
                             uint8_t irq_stat, short to_clear);

////////// Special code (only when configured with libjpeg support) //////////

#ifdef CONFIG_JPEG
/* Combine image properties before compression */
static void s5pc1xx_jpeg_pre_coding(S5pc1xxJpegState *s)
{
    uint8_t  src_color_mode, dst_color_mode;
    uint16_t cols_out, rows_out;

    Dummy_CInfo *dummy_cinfo = s->dummy_cinfo;

    /* Input and output images addresses */
    s->src_addr = s->imgadr;
    s->dst_addr = s->jpgadr;

    /* Input image area */
    dummy_cinfo->image_width  = (s->jpgx_u << 8) | s->jpgx_l;
    dummy_cinfo->image_height = (s->jpgy_u << 8) | s->jpgy_l;

    /* Coeff for color mode converting */
    s->c1 = (s->jpgcmod & 0x2) << 3;       /* 0 or 16 */

    /* Input image color mode */
    src_color_mode = (s->jpgcmod & ALL_BITS(7, 5)) >> 5;
    switch (src_color_mode) {
    case 0x1:
        s->mode_in = YCbCr_422;
        dummy_cinfo->in_color_space = JCS_YCbCr;
        break;
    case 0x2:
        s->mode_in = RGB_565;
        dummy_cinfo->in_color_space = JCS_RGB;
        break;
    default:
        hw_error("s5pc1xx_jpeg: bad input color space (num = %u)\n",
                 src_color_mode);
    }

    /* Input image size */
    s->src_len = (dummy_cinfo->image_width * (s->mode_in & ALL_BITS(4, 0)) *
                  dummy_cinfo->image_height) >> 3;

    /* Output image color mode */
    dst_color_mode = (s->jpgmod  & ALL_BITS(2, 0));
    dummy_cinfo->comp_info[0].h_samp_factor = 2;
    dummy_cinfo->comp_info[0].v_samp_factor = 2;
    switch (dst_color_mode) {
    case 0x1:
        s->mode_out = YCbCr_422;
        dummy_cinfo->jpeg_color_space = JCS_YCbCr;
        dummy_cinfo->comp_info[1].h_samp_factor = 1;
        dummy_cinfo->comp_info[1].v_samp_factor = 2;
        dummy_cinfo->comp_info[2].h_samp_factor = 1;
        dummy_cinfo->comp_info[2].v_samp_factor = 2;
        break;
    case 0x2:
        s->mode_out = YCbCr_420;
        dummy_cinfo->jpeg_color_space = JCS_YCbCr;
        dummy_cinfo->comp_info[1].h_samp_factor = 1;
        dummy_cinfo->comp_info[1].v_samp_factor = 1;
        dummy_cinfo->comp_info[2].h_samp_factor = 1;
        dummy_cinfo->comp_info[2].v_samp_factor = 1;
        break;
    default:
        hw_error("s5pc1xx_jpeg: bad output color space (num = %u)\n",
                 dst_color_mode);
    }

    /* Output image area */
    cols_out = SIZE_OUT(dummy_cinfo->image_width,
                        (s->mode_out >> 5) & ALL_BITS(4, 0));
    rows_out = SIZE_OUT(dummy_cinfo->image_height,
                        (s->mode_out >> 10) & ALL_BITS(4, 0));

    /* Output image size */
    s->dst_len = JPEG_HDR_SIZE +
                 ((cols_out * (s->mode_out & ALL_BITS(4, 0)) * rows_out) >> 3);
}

/* Calculate JPEG data size */
static uint32_t s5pc1xx_jpeg_data_size(S5pc1xxJpegState *s)
{
    target_phys_addr_t page_size = TARGET_PAGE_SIZE;
    uint8_t *jpg_Buf = NULL;
    int i, j;

    s->dummy_dinfo->image_height =
    s->dummy_dinfo->image_width  = 0;

    /* Travel over all memory pages occupied by the image */
    for (i = 0, j = 1; i < (MAX_IMG_SIZE + JPEG_HDR_SIZE); i++) {
        /* Map the first memory page or map the next memory page if less
         * than 9 bytes till the end of the current one remains */
        if (!i || ((i % TARGET_PAGE_SIZE) > (TARGET_PAGE_SIZE - 9))) {
            jpg_Buf = cpu_physical_memory_map(s->jpgadr, &page_size, 0);
            if (!jpg_Buf || (page_size != TARGET_PAGE_SIZE * j)) {
                fprintf(stderr,
                        "s5pc1xx_jpeg: input memory can't be accessed\n");
                /* Raise result interrupt as NOT OK */
                s5pc1xx_jpeg_irq(s, STREAM_INT, HIGH);
                return 0;
            }
            j++;
            page_size = TARGET_PAGE_SIZE * j;
        }

        if (jpg_Buf[i] == 0xFF) {
            if (jpg_Buf[i + 1] == 0xC0) {
                s->dummy_dinfo->image_height =
                    (jpg_Buf[i + 5] << 8) | jpg_Buf[i + 6];
                s->dummy_dinfo->image_width =
                    (jpg_Buf[i + 7] << 8) | jpg_Buf[i + 8];
            }
            if (jpg_Buf[i + 1] == 0xD9)
                return (i + 2); /* Resulting size of the image */
        }
    }

    fprintf(stderr, "s5pc1xx_jpeg: input image end can't be reached\n");
    /* Raise result interrupt as NOT OK */
    s5pc1xx_jpeg_irq(s, STREAM_INT, HIGH);
    return 0;
}

/* Combine image properties before decompression */
static void s5pc1xx_jpeg_pre_decoding(S5pc1xxJpegState *s)
{
    uint8_t  dst_color_mode;
    uint16_t cols_out, rows_out;

    Dummy_DInfo *dummy_dinfo = s->dummy_dinfo;

    /* Input and output image addresses */
    s->src_addr = s->jpgadr;
    s->dst_addr = s->imgadr;

    s->src_len = s5pc1xx_jpeg_data_size(s);

    /* Output image color mode */
    dst_color_mode = (s->outform & 0x1);
    switch (dst_color_mode) {
    case 0x0:
        s->mode_out = YCbCr_422;
        dummy_dinfo->out_color_space = JCS_YCbCr;
        break;
    case 0x1:
        s->mode_out = YCbCr_420;
        dummy_dinfo->out_color_space = JCS_YCbCr;
        break;
    default:
        hw_error("s5pc1xx_jpeg: bad output color space (num = %u)\n",
                 dst_color_mode);
    }

    /* Output image area */
    cols_out = SIZE_OUT(dummy_dinfo->image_width,
                        s->mode_out >> 5 & ALL_BITS(4, 0));
    rows_out = SIZE_OUT(dummy_dinfo->image_height,
                        s->mode_out >> 10 & ALL_BITS(4, 0));

    /* Output image size */
    s->dst_len = (cols_out * (s->mode_out & ALL_BITS(4, 0)) * rows_out) >> 3;
}

/* Get references to input and output data */
static int s5pc1xx_jpeg_mem_map(S5pc1xxJpegState *s)
{
    target_phys_addr_t src_len, dst_len;

    src_len = s->src_len;
    dst_len = s->dst_len;

    s->src_base = cpu_physical_memory_map(s->src_addr, &src_len, 0);
    s->dst_base = cpu_physical_memory_map(s->dst_addr, &dst_len, 0);

    if (!s->src_base || !s->dst_base) {
        fprintf(stderr, "s5pc1xx_jpeg: bad image address\n");
        /* Raise result interrupt as NOT OK */
        s5pc1xx_jpeg_irq(s, JUST_RAISE_IRQ, NONE);
        return 1;
    }

    if ((src_len == s->src_len) && (dst_len == s->dst_len))
        return 0;

    cpu_physical_memory_unmap(s->src_base, src_len, 0, 0);
    cpu_physical_memory_unmap(s->dst_base, dst_len, 0, 0);
    fprintf(stderr, "s5pc1xx_jpeg: not enough memory for image\n");
    /* Raise result interrupt as NOT OK */
    s5pc1xx_jpeg_irq(s, JUST_RAISE_IRQ, NONE);
    return 1;
}

/* JPEG compression */
static void s5pc1xx_jpeg_coding(S5pc1xxJpegState *s)
{
    int row_stride, i;
    struct jpeg_error_mgr jerr;
    JSAMPROW row_pointer[1];

    CInfo *cinfo = s->cinfo;
    Dummy_CInfo *dummy_cinfo = s->dummy_cinfo;

    /* Allocate and initialize JPEG compression object */
    cinfo->err = jpeg_std_error(&jerr);
    jpeg_create_compress(cinfo);

    cinfo->dest->next_output_byte = (JSAMPLE *) s->dst_base;
    cinfo->dest->free_in_buffer   = s->dst_len;

    /* RGB or YCbCr (3 components) can be used as compressor input color space
     * according to s5pc1xx specification */
    cinfo->input_components = 3;
    cinfo->image_width      = dummy_cinfo->image_width;
    cinfo->image_height     = dummy_cinfo->image_height;
    cinfo->in_color_space   = dummy_cinfo->in_color_space;

    jpeg_set_defaults(cinfo);

    /* Note: using dummy_cinfo->jpeg_color_space instead of
     * cinfo->jpeg_color_space because the second one is set by
     * jpeg_set_defaults() and may be incorrect */
    jpeg_set_colorspace(cinfo, dummy_cinfo->jpeg_color_space);

#if JPEG_LIB_VERSION >= 61

    for (i = 0; i < 4; i++) {
        cinfo->comp_info[i].quant_tbl_no = (s->qtbl >> (i * 2)) & 0x3;
        cinfo->comp_info[i].dc_tbl_no    = (s->htbl >> (i * 2)) & 0x1;
        cinfo->comp_info[i].ac_tbl_no    = (s->htbl >> (i * 2 + 1)) & 0x1;
    }

    /* FIXME: force_baseline may be 'false' in the next four instructions */
    jpeg_add_quant_table (cinfo, 0,
                          (const unsigned int *) s->qtbl0,
                          100, true);
    jpeg_add_quant_table (cinfo, 1,
                          (const unsigned int *) s->qtbl1,
                          100, true);
    jpeg_add_quant_table (cinfo, 2,
                          (const unsigned int *) s->qtbl2,
                          100, true);
    jpeg_add_quant_table (cinfo, 3,
                          (const unsigned int *) s->qtbl3,
                          100, true);

    cinfo->dc_huff_tbl_ptrs[0] = (JHUFF_TBL *) s->hdctbl0;
    cinfo->ac_huff_tbl_ptrs[0] = (JHUFF_TBL *) s->hactbl0;

    cinfo->dc_huff_tbl_ptrs[1] = (JHUFF_TBL *) s->hdctbl1;
    cinfo->ac_huff_tbl_ptrs[1] = (JHUFF_TBL *) s->hactbl1;

    jpeg_alloc_huff_table((j_common_ptr) cinfo);

#endif

    for (i = 0; i < 3; i++) {
        cinfo->comp_info[i].h_samp_factor =
            dummy_cinfo->comp_info[i].h_samp_factor;
        cinfo->comp_info[i].v_samp_factor =
            dummy_cinfo->comp_info[i].v_samp_factor;
    }

    jpeg_start_compress(cinfo, FALSE);

    /* JSAMPLEs per row in input_buffer */
    row_stride = cinfo->image_width * cinfo->input_components;

    while (cinfo->next_scanline < cinfo->image_height) {
        row_pointer[0] =
            (JSAMPROW) &s->src_base[cinfo->next_scanline * row_stride];
        (void) jpeg_write_scanlines(cinfo, row_pointer, 1);
    }

    /* Finish compression */
    jpeg_finish_compress(cinfo);
    jpeg_destroy_compress(cinfo);
}

/* JPEG decompression */
static void s5pc1xx_jpeg_decoding(S5pc1xxJpegState *s)
{
    struct jpeg_error_mgr jerr;
    JSAMPARRAY buffer;  /* Output row buffer */
    int row_stride;     /* Physical row width in output buffer */
    int i, count;

    DInfo *dinfo = s->dinfo;
    Dummy_DInfo *dummy_dinfo = s->dummy_dinfo;

    /* Allocate and initialize JPEG decompression object */
    dinfo->err = jpeg_std_error(&jerr);
    jpeg_create_decompress(dinfo);

    dinfo->src->next_input_byte = (JSAMPLE *) s->src_base;
    dinfo->src->bytes_in_buffer = s->src_len;

    (void) jpeg_read_header(dinfo, TRUE);

    dinfo->out_color_space = dummy_dinfo->out_color_space;

    (void) jpeg_start_decompress(dinfo);

    /* JSAMPLEs per row in output buffer */
    row_stride = dinfo->output_width * dinfo->output_components;

    /* Make a one-row-high sample array
     * that will go away when done with image */
    buffer = (*(dinfo->mem->alloc_sarray))
        ((j_common_ptr) dinfo, JPOOL_IMAGE, row_stride, 1);

    count = 0;
    while (dinfo->output_scanline < dinfo->output_height) {
        (void) jpeg_read_scanlines(dinfo, buffer, 1);

        for (i = 0; i < row_stride; i++, count++)
            s->dst_base[count] = (char) buffer[0][i];
    }

    /* Finish decompression */
    (void) jpeg_finish_decompress(dinfo);
    jpeg_destroy_decompress(dinfo);
}
#endif

//////////////////////////// Common code //////////////////////////////////////

/* Reset JPEG controller */
static void s5pc1xx_jpeg_reset(S5pc1xxJpegState *s)
{
    int i;

    s->jpgmod           = 0x1;
    s->jpgopr           = 0x0;
    s->qtbl             = 0x0;
    s->htbl             = 0x0;
    s->jpgdri_u         = 0x0;
    s->jpgdri_l         = 0x0;
    s->jpgy_u           = 0x0;
    s->jpgy_l           = 0x0;
    s->jpgx_u           = 0x0;
    s->jpgx_l           = 0x0;
    s->jpgintse         = 0x0;
    s->jpgintst         = 0x0;
    s->imgadr           = 0x0;
    s->jpgadr           = 0x0;
    s->coef1            = 0x0;
    s->coef2            = 0x0;
    s->coef3            = 0x0;
    s->jpgcmod          = 0x00000020;
    s->jpgclkcon        = 0x00000002;
    s->sw_reset         = 0x0;
    s->timer_se         = 0x7FFFFFFF;
    s->timer_st         = 0x7FFFFFFF;
    s->comstat          = 0x0;
    s->outform          = 0x0;
    s->version          = 0x00000003;
    s->enc_stream_intse = 0x00FFFFE0;
    s->enc_stream_intst = 0x0;

    for(i = 0; i < 178; i++) {
        s->hactbl0[i] = 0x0;
        s->hactbl1[i] = 0x0;

        if (i > 63)
            continue;

        s->qtbl0[i]    = 0x0;
        s->qtbl1[i]    = 0x0;
        s->qtbl2[i]    = 0x0;
        s->qtbl3[i]    = 0x0;

        if (i > 28)
            continue;

        s->hdctbl0[i]  = 0x0;
        s->hdctbl1[i]  = 0x0;
    }
}

/* Interrupts handler */
static void s5pc1xx_jpeg_irq(S5pc1xxJpegState *s,
                             uint8_t irq_stat, short to_clear)
{
    switch(irq_stat) {
    case JUST_RAISE_IRQ:
        qemu_irq_raise(s->irq);
        return;
    case RESULT_INT:
        if (to_clear) {
            s->jpgintst &= ~RESULT_STAT;
            break;
        }
        s->jpgintst |= RESULT_STAT;
        qemu_irq_raise(s->irq);
        return;
    case STREAM_INT:
        if (to_clear) {
            s->jpgintst &= ~STREAM_STAT;
            break;
        }
        s->jpgintst |= STREAM_STAT;
        qemu_irq_raise(s->irq);
        return;
    case TIMER_INT:
        if (to_clear) {
            s->timer_st &= ~TIMER_INT_STAT;
            break;
        }
        s->timer_st |= TIMER_INT_STAT;
        if (s->timer_se & TIMER_INT_EN)
            qemu_irq_raise(s->irq);
        return;
    case ENC_STREAM_INT:
        if (to_clear) {
            s->enc_stream_intst &= ~ENC_STREAM_INT_STAT;
            break;
        }
        s->enc_stream_intst |= ENC_STREAM_INT_STAT;
        if (s->enc_stream_intse & ENC_STREAM_INT_EN)
            qemu_irq_raise(s->irq);
        return;
    default:
        return;
    }

    /* Lower irq if all states are clear */
    if (!((s->jpgintst & RESULT_STAT) |
          (s->jpgintst & STREAM_STAT) |
          (s->timer_st & TIMER_INT_STAT) |
          (s->enc_stream_intst & ENC_STREAM_INT_STAT)))
        qemu_irq_lower(s->irq);
}

/* JPEG controller registers read */
static uint32_t s5pc1xx_jpeg_read(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxJpegState *s = (S5pc1xxJpegState *)opaque;

    switch(offset) {
    case JPGMOD:
#ifdef CONFIG_JPEG
        if ((s->proc_mode == DECOMPR) && !s->jpgopr) {
            if (s->dinfo->jpeg_color_space == JCS_GRAYSCALE) {
                s->jpgmod = (s->jpgmod & ALL_BITS(31, 3)) | 0x3;
            } else {
                /* FIXME: the value below must be different
                 * depending on downsampling coefs */
                s->jpgmod = (s->jpgmod & ALL_BITS(31, 3)) | 0x0;
            }
        }
#endif
        return s->jpgmod;
    case JPGOPR:
        return s->jpgopr;
    case QTBL:
        return s->qtbl;
    case HTBL:
        return s->htbl;
    case JPGDRI_U:
        return s->jpgdri_u;
    case JPGDRI_L:
        return s->jpgdri_l;
    case JPGY_U:
        return s->jpgy_u;
    case JPGY_L:
        return s->jpgy_l;
    case JPGX_U:
        return s->jpgx_u;
    case JPGX_L:
        return s->jpgx_l;
    case JPGCNT_U:
        if ((s->proc_mode & COMPR) && !s->jpgopr)
            return (s->byte_cnt >> 16) & ALL_BITS(7, 0);
        return 0;
    case JPGCNT_M:
        if ((s->proc_mode & COMPR) && !s->jpgopr)
            return (s->byte_cnt >>  8) & ALL_BITS(7, 0);
        return 0;
    case JPGCNT_L:
        if ((s->proc_mode & COMPR)  && !s->jpgopr)
            return (s->byte_cnt >>  0) & ALL_BITS(7, 0);
        return 0;
    case JPGINTSE:
        return s->jpgintse;
    case JPGINTST:
        /* Set successful status if no operation is going on */
        if (!s->jpgopr)
            s->jpgintst = 0x40;
        return s->jpgintst;
    case IMGADR:
        return s->imgadr;
    case JPGADR:
        return s->jpgadr;
    case COEF1:
        return s->coef1;
    case COEF2:
        return s->coef2;
    case COEF3:
        return s->coef3;
    case JPGCMOD:
        return s->jpgcmod;
    case JPGCLKCON:
        return s->jpgclkcon;
    case SW_RESET:
        return s->sw_reset;
    case TIMER_SE:
        return s->timer_se;
    case TIMER_ST:
        return s->timer_st;
    case COMSTAT:
        return s->comstat;
    case OUTFORM:
        return s->outform;
    case VERSION:
        return s->version;
    case ENC_STREAM_INTSE:
        return s->enc_stream_intse;
    case ENC_STREAM_INTST:
        return s->enc_stream_intst;
    case QTBL0(0) ... QTBL0(63):
        return s->qtbl0[(offset - QTBL0(0)) >> 2];
    case QTBL1(0) ... QTBL1(63):
        return s->qtbl1[(offset - QTBL1(0)) >> 2];
    case QTBL2(0) ... QTBL2(63):
        return s->qtbl2[(offset - QTBL2(0)) >> 2];
    case QTBL3(0) ... QTBL3(63):
        return s->qtbl3[(offset - QTBL3(0)) >> 2];
    default:
        hw_error("s5pc1xx_jpeg: bad read offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
}

/* JPEG controller register write */
static void s5pc1xx_jpeg_write(void *opaque, target_phys_addr_t offset,
                               uint32_t value)
{
    uint32_t old_val;
    S5pc1xxJpegState *s = (S5pc1xxJpegState *)opaque;

    switch(offset) {
    case JPGMOD:
        old_val      = s->jpgmod;
        s->proc_mode = (value & PROC_MODE) ? DECOMPR : COMPR;
        s->jpgmod    = (s->proc_mode == COMPR) ?
                       value :
                       ((value & ALL_BITS(31, 3)) | (old_val & ALL_BITS(2, 0)));
        break;
    case QTBL:
        s->qtbl = value;
        break;
    case HTBL:
        s->htbl = value;
        break;
    case JPGDRI_U:
        s->jpgdri_u = value;
        break;
    case JPGDRI_L:
        s->jpgdri_l = value;
        break;
    case JPGY_U:
        s->jpgy_u = value;
        break;
    case JPGY_L:
        s->jpgy_l = value;
        break;
    case JPGX_U:
        s->jpgx_u = value;
        break;
    case JPGX_L:
        s->jpgx_l = value;
        break;
    case JPGINTSE:
        s->jpgintse = value;
        break;
    case JPGCOM:
        if (value & INT_RELEASE) {
            s5pc1xx_jpeg_irq(s, RESULT_INT, LOW);
            s5pc1xx_jpeg_irq(s, STREAM_INT, LOW);
        }
        break;
    case IMGADR:
        s->imgadr = value;
        break;
    case JPGADR:
        s->jpgadr = value;
        break;
    case COEF1:
        s->coef1 = value;
        break;
    case COEF2:
        s->coef2 = value;
        break;
    case COEF3:
        s->coef3 = value;
        break;
    case JPGCMOD:
        s->jpgcmod = value;
        break;
    case JPGCLKCON:
        s->jpgclkcon = value;
        break;
    case JSTART:
        if (!value)
            break;
        s->jpgopr = 0x1;
#ifdef CONFIG_JPEG
        switch(s->proc_mode) {
        case COMPR:
            s5pc1xx_jpeg_pre_coding(s);
            if (s5pc1xx_jpeg_mem_map(s))
                break;
            s5pc1xx_jpeg_coding(s);
            s->byte_cnt = s5pc1xx_jpeg_data_size(s);
            /* Raise result interrupt as OK */
            s5pc1xx_jpeg_irq(s, RESULT_INT, HIGH);
            break;
        case DECOMPR:
            s5pc1xx_jpeg_pre_decoding(s);
            if (!s->src_len)
                break;
            if (s5pc1xx_jpeg_mem_map(s))
                break;
            s5pc1xx_jpeg_decoding(s);
            /* Raise result interrupt as OK */
            s5pc1xx_jpeg_irq(s, RESULT_INT, HIGH);
            break;
        }
#endif
        s->jpgopr = 0x0;
        break;
    case SW_RESET:
        if (value) {
            s->sw_reset = 0x1;
            s5pc1xx_jpeg_reset(s);
        }
        s->sw_reset = 0x0;
        break;
    case TIMER_SE:
        s->timer_se = value;
        break;
    case TIMER_ST:
        s->timer_st = value;
        if (value & TIMER_INT_STAT)
            s5pc1xx_jpeg_irq(s, TIMER_INT, LOW);
        break;
    case OUTFORM:
        s->outform = value;
        break;
    case ENC_STREAM_INTSE:
        s->enc_stream_intse = value;
        break;
    case ENC_STREAM_INTST:
        s->enc_stream_intst = value;
        if (value & ENC_STREAM_INT_STAT)
            s5pc1xx_jpeg_irq(s, ENC_STREAM_INT, LOW);
        break;
    case QTBL0(0) ... QTBL0(63):
        s->qtbl0[(offset - QTBL0(0)) >> 2] = value & 0xFF;
        break;
    case QTBL1(0) ... QTBL1(63):
        s->qtbl1[(offset - QTBL1(0)) >> 2] = value & 0xFF;
        break;
    case QTBL2(0) ... QTBL2(63):
        s->qtbl2[(offset - QTBL2(0)) >> 2] = value & 0xFF;
        break;
    case QTBL3(0) ... QTBL3(63):
        s->qtbl3[(offset - QTBL3(0)) >> 2] = value & 0xFF;
        break;
    case HDCTBL0(0) ... HDCTBL0(15):
        s->hdctbl0[(offset - HDCTBL0(0)) >> 2] = value & 0xFF;
        break;
    case HDCTBLG0(0) ... HDCTBLG0(11):
        s->hdctbl0[((offset - HDCTBLG0(0)) >> 2) + 16] = value & 0xFF;
        break;
    case HACTBL0(0) ... HACTBL0(15):
        s->hactbl0[(offset - HACTBL0(0)) >> 2] = value & 0xFF;
        break;
    case HACTBLG0(0) ... HACTBLG0(161):
        s->hactbl0[((offset - HACTBLG0(0)) >> 2) + 16] = value & 0xFF;
        break;
    case HDCTBL1(0) ... HDCTBL1(15):
        s->hdctbl1[(offset - HDCTBL1(0)) >> 2] = value & 0xFF;
        break;
    case HDCTBLG1(0) ... HDCTBLG1(11):
        s->hdctbl1[((offset - HDCTBLG1(0)) >> 2) + 16] = value & 0xFF;
        break;
    case HACTBL1(0) ... HACTBL1(15):
        s->hactbl1[(offset - HACTBL1(0)) >> 2] = value & 0xFF;
        break;
    case HACTBLG1(0) ... HACTBLG1(161):
        s->hactbl1[((offset - HACTBLG1(0)) >> 2) + 16] = value & 0xFF;
        break;
    default:
        hw_error("s5pc1xx_jpeg: bad write offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
}

static CPUReadMemoryFunc * const s5pc1xx_jpeg_readfn[] = {
   s5pc1xx_jpeg_read,
   s5pc1xx_jpeg_read,
   s5pc1xx_jpeg_read
};

static CPUWriteMemoryFunc * const s5pc1xx_jpeg_writefn[] = {
   s5pc1xx_jpeg_write,
   s5pc1xx_jpeg_write,
   s5pc1xx_jpeg_write
};

/* JPEG initialization */
static int s5pc1xx_jpeg_init(SysBusDevice *dev)
{
    S5pc1xxJpegState *s = FROM_SYSBUS(S5pc1xxJpegState, dev);
    int iomemtype;

    sysbus_init_irq(dev, &s->irq);

    iomemtype =
        cpu_register_io_memory(s5pc1xx_jpeg_readfn, s5pc1xx_jpeg_writefn, s);
    sysbus_init_mmio(dev, S5PC1XX_JPEG_REG_MEM_SIZE, iomemtype);

    s5pc1xx_jpeg_reset(s);

#ifdef CONFIG_JPEG
    s->cinfo = (CInfo *) qemu_mallocz(sizeof(CInfo));
    s->dinfo = (DInfo *) qemu_mallocz(sizeof(DInfo));
    s->dummy_cinfo = (Dummy_CInfo *) qemu_mallocz(sizeof(Dummy_CInfo));
    s->dummy_dinfo = (Dummy_DInfo *) qemu_mallocz(sizeof(Dummy_DInfo));
#endif

    return 0;
}

static void s5pc1xx_jpeg_register(void)
{
    sysbus_register_dev("s5pc1xx,jpeg", sizeof(S5pc1xxJpegState),
                        s5pc1xx_jpeg_init);
}

device_init(s5pc1xx_jpeg_register)
