/*
 * S5PC110 Test & Debug
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Kirill Batuzov <batuzovk@ispras.ru>
 */

/* Print all system information to stderr */
static __attribute__((unused)) void debug_sysinfo(const struct s5pc1xx_state_s *s)
{
    fprintf(stderr, "CPU: %s\n", s->env->cpu_model_str);
    fprintf(stderr, "SDRAM: %lu MB\n", s->sdram_size / (1024 * 1024));
    fprintf(stderr, "SRAM: %lu KB\n", s->isram_size / 1024);
    fprintf(stderr, "SROM: %lu KB\n", s->isrom_size / 1024);
}


/* Interrupt Controller */

#define ADDR    0xF2000F00


target_phys_addr_t vic_base[] = {
    S5PC1XX_VIC0_BASE, S5PC1XX_VIC1_BASE, S5PC1XX_VIC2_BASE, S5PC1XX_VIC3_BASE
};

typedef enum {
    enable, disable, swint, swclear, select_irq, selclear, priomask, daisyprio
} irq_op;


static __attribute__((unused)) void test_irq_handler(void *opaque, int irq,
                                                     int level)
{
    const char *name;

    if (irq)
        name = "FIQ";
    else
        name = "IRQ";

    if (level)
        fprintf(stderr, "%s was raised\n", name);
}

static __attribute__((unused)) qemu_irq *test_irq_init(void)
{
    return qemu_allocate_irqs(test_irq_handler, NULL, 2);
}

static __attribute__((unused)) uint32_t test_irq_op(irq_op op, int irq,
                                                    int is_wr, uint32_t val)
{
    int vic_id = irq / S5PC1XX_VIC_SIZE;
    uint32_t res;
    target_phys_addr_t base = vic_base[vic_id];
    target_phys_addr_t off;

    switch (op) {
        case enable:
            off = 0x10;
            break;
        case disable:
            off = 0x14;
            break;
        case swint:
            off = 0x18;
            break;
        case swclear:
            off = 0x1C;
            break;
        case select_irq:
        case selclear:
            off = 0xC;
            break;
        case priomask:
            off = 0x24;
            break;
        case daisyprio:
            off = 0x28;
            break;
        default:
            off = 0x0;
            break;
    }
    if (op == priomask || op == daisyprio || !is_wr) {
        if (is_wr) {
            cpu_physical_memory_write(base + off, (uint8_t *)&val, 4);
        } else {
            cpu_physical_memory_read(base + off, (uint8_t *)&res, 4);
        }
        return res;
    }
    if (op == select_irq || op == selclear)
        cpu_physical_memory_read(base + off, (uint8_t *)&res, 4);
    if (op == select_irq)
        res |= 1 << (irq % S5PC1XX_VIC_SIZE);
    else if (op == selclear)
        res &= ~(1 << (irq % S5PC1XX_VIC_SIZE));
    else
        res = 1 << (irq % S5PC1XX_VIC_SIZE);
    cpu_physical_memory_write(base + off, (uint8_t *)&res, 4);
    return 0;
}

static __attribute__((unused)) void test_irq_script(struct s5pc1xx_state_s *s)
{
    uint32_t res;

    fprintf(stderr,"Step 1: Interrupts disabled. Raising and lowering them.\n");
    qemu_irq_raise(s5pc1xx_get_irq(s, 14));
    qemu_irq_raise(s5pc1xx_get_irq(s, 33));
    qemu_irq_lower(s5pc1xx_get_irq(s, 14));
    qemu_irq_lower(s5pc1xx_get_irq(s, 33));
    qemu_irq_raise(s5pc1xx_get_irq(s, 69));
    qemu_irq_lower(s5pc1xx_get_irq(s, 69));
    qemu_irq_raise(s5pc1xx_get_irq(s, 101));

    fprintf(stderr, "Step 2: Interrupt 101 is raised. Enable some other.\n");
    test_irq_op(enable, 4, 1, 0);
    test_irq_op(enable, 34, 1, 0);
    test_irq_op(enable, 5, 1, 0);

    fprintf(stderr, "Step 3: Interrupt 101 is raised. Enable it.\n");
    res = 0xDDEEAABB;
    cpu_physical_memory_write(0xF2300114, (const uint8_t *)&res, 4);
    test_irq_op(enable, 101, 1, 0);
    cpu_physical_memory_read(ADDR, (uint8_t *)&res, 4);
    fprintf(stderr, "Interrupt 101 vector is %x\n", res);
    qemu_irq_raise(s5pc1xx_get_irq(s, 5));
    fprintf(stderr, "Step 4: Interrupt 101 has been acknoledged. "
                    "Interrupt 5 has been raised.\n");

    fprintf(stderr, "Step 5: Generate IRQ 4 with higher priority.\n");
    res = 0xa;
    cpu_physical_memory_write(0xF2000210, (const uint8_t *)&res, 4);
    res = 0xDDEEBBAA;
    cpu_physical_memory_write(0xF2000110, (const uint8_t *)&res, 4);
    qemu_irq_raise(s5pc1xx_get_irq(s, 4));

    fprintf(stderr, "Step 6: Acknoledge IRQ 4. Then lower it.\n");
    cpu_physical_memory_read(ADDR, (uint8_t *)&res, 4);
    fprintf(stderr, "Interrupt 4 vector is %x\n", res);
    qemu_irq_lower(s5pc1xx_get_irq(s, 4));

    fprintf(stderr, "Step 7: Finalize IRQ 4 processing. No new interrupts "
                    "should appear as IRQ 101 is in progress.\n");
    cpu_physical_memory_write(ADDR, (const uint8_t *)&res, 4);

    fprintf(stderr, "Step 8: Mask IRQ 4's priority, then raise it again.\n");
    test_irq_op(priomask, 0, 1, 0xffff & ~(1 << 0xa));
    qemu_irq_raise(s5pc1xx_get_irq(s, 4));

    fprintf(stderr, "Step 9: Finalize IRQ 101 processing. "
                    "We should recive IRQ 5.\n");
    res = 0xDDEEBBCC;
    cpu_physical_memory_write(0xF2000114, (const uint8_t *)&res, 4);
    qemu_irq_lower(s5pc1xx_get_irq(s, 101));
    cpu_physical_memory_write(ADDR, (const uint8_t *)&res, 4);
    cpu_physical_memory_read(ADDR, (uint8_t *)&res, 4);
    fprintf(stderr, "Interrupt 5 vector is %x\n", res);

    fprintf(stderr, "Step 10: Finalize IRQs. Clear them all.\n");
    qemu_irq_lower(s5pc1xx_get_irq(s, 5));
    qemu_irq_lower(s5pc1xx_get_irq(s, 4));
    cpu_physical_memory_write(ADDR, (const uint8_t *)&res, 4);
}


/* DMA */

#define DATA_ADDR       0x20010000
#define RESULT_ADDR     0x20020000
#define PROG_ADDR       0x20030000

#define DBGGO_ADDR      0xFA200D04
#define DBG0_ADDR       0xFA200D08
#define DBG1_ADDR       0xFA200D0C

#define FSC_ADDR        0xFA200034
#define FTC1_ADDR       0xFA200044
#define CPC1_ADDR       0xFA20010C


uint32_t dbg0 = 0x01A00000;
uint32_t dbg1 = PROG_ADDR;
uint32_t dbggo = 0x0;
uint32_t dbgkill = 0x00010101;

static const uint8_t dma_data[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
   11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
   21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
   31, 32
};

static const uint8_t dma_stz[] = {
    0xBC, 0x00, 0x00, 0x00, 0x01, 0x20, /* DMAMOV   SAR 0x20010000 */
    0xBC, 0x02, 0x00, 0x00, 0x02, 0x20, /* DMAMOV   DAR 0x20020000 */
    0xBC, 0x01, 0x35, 0x40, 0x0D, 0x00, /* DMAMOV   CCR SAI SS4 SB4 DAI DS4 DB4 */
    0x0C,                               /* DMASTZ   */
    0x00                                /* DMAEND   */
};

static const uint8_t dma_stzlp[] = {
    0xBC, 0x00, 0x00, 0x00, 0x01, 0x20, /* DMAMOV   SAR 0x20010000 */
    0xBC, 0x02, 0x00, 0x00, 0x02, 0x20, /* DMAMOV   DAR 0x20020000 */
    0xBC, 0x01, 0x35, 0x40, 0x0D, 0x00, /* DMAMOV   CCR SAI SS4 SB4 DAI DS4 DB4 */
    0x20, 0x01,                         /* DMALP    2 */
    0x0C,                               /* DMASTZ   */
    0x38, 0x01,                         /* DMALPEND */
    0x00                                /* DMAEND   */
};

static const uint8_t dma_copy[] = {
    0xBC, 0x00, 0x00, 0x00, 0x01, 0x20, /* DMAMOV   SAR 0x20010000 */
    0xBC, 0x02, 0x00, 0x00, 0x02, 0x20, /* DMAMOV   DAR 0x20020000 */
    0xBC, 0x01, 0x35, 0x40, 0x0D, 0x00, /* DMAMOV   CCR SAI SS4 SB4 DAI DS4 DB4 */
    0x20, 0x01,                         /* DMALP    2 */
    0x04,                               /* DMALD    */
    0x08,                               /* DMAST    */
    0x38, 0x02,                         /* DMALPEND */
    0x00                                /* DMAEND   */
};

/* Paradoxically but this should work correctly too. */
static const uint8_t dma_copy_2[] = {
    0xBC, 0x00, 0x00, 0x00, 0x01, 0x20, /* DMAMOV   SAR 0x20010000 */
    0xBC, 0x02, 0x00, 0x00, 0x02, 0x20, /* DMAMOV   DAR 0x20020000 */
    0xBC, 0x01, 0x35, 0x40, 0x0D, 0x00, /* DMAMOV   CCR SAI SS4 SB4 DAI DS4 DB4 */
    0x20, 0x01,                         /* DMALP    2 */
    0x08,                               /* DMAST    */
    0x04,                               /* DMALD    */
    0x38, 0x02,                         /* DMALPEND */
    0x00                                /* DMAEND   */
};

static const uint8_t dma_scatter[] = {
    0xBC, 0x00, 0x00, 0x00, 0x01, 0x20, /* DMAMOV   SAR 0x20010000 */
    0xBC, 0x02, 0x00, 0x00, 0x02, 0x20, /* DMAMOV   DAR 0x20020000 */
    0xBC, 0x01, 0x34, 0x40, 0x0D, 0x00, /* DMAMOV   CCR SAF SS4 SB4 DAI DS4 DB4 */
    0x20, 0x01,                         /* DMALP    2 */
    0x04,                               /* DMALD    */
    0x08,                               /* DMAST    */
    0x38, 0x02,                         /* DMALPEND */
    0x00                                /* DMAEND   */
};

static const uint8_t dma_gather[] = {
    0xBC, 0x00, 0x00, 0x00, 0x01, 0x20, /* DMAMOV   SAR 0x20010000 */
    0xBC, 0x02, 0x00, 0x00, 0x02, 0x20, /* DMAMOV   DAR 0x20020000 */
    0xBC, 0x01, 0x35, 0x00, 0x0D, 0x00, /* DMAMOV   CCR SAI SS4 SB4 DAF DS4 DB4 */
    0x20, 0x01,                         /* DMALP    2 */
    0x04,                               /* DMALD    */
    0x08,                               /* DMAST    */
    0x38, 0x02,                         /* DMALPEND */
    0x00                                /* DMAEND   */
};

/* Watchdog abort at DMAEND */
static const uint8_t dma_nold[] = {
    0xBC, 0x00, 0x00, 0x00, 0x01, 0x20, /* DMAMOV   SAR 0x20010000 */
    0xBC, 0x02, 0x00, 0x00, 0x02, 0x20, /* DMAMOV   DAR 0x20020000 */
    0xBC, 0x01, 0x34, 0x00, 0x0D, 0x00, /* DMAMOV   CCR SAF SS4 SB4 DAF DS4 DB4 */
    0x08,                               /* DMAST    */
    0x18,                               /* DMANOP   */
    0x00                                /* DMAEND   */
};

/* Watchdog abort at DMAEND */
static const uint8_t dma_nost[] = {
    0xBC, 0x00, 0x00, 0x00, 0x01, 0x20, /* DMAMOV   SAR 0x20010000 */
    0xBC, 0x02, 0x00, 0x00, 0x02, 0x20, /* DMAMOV   DAR 0x20020000 */
    0xBC, 0x01, 0x34, 0x00, 0x0D, 0x00, /* DMAMOV   CCR SAF SS4 SB4 DAF DS4 DB4 */
    0x04,                               /* DMALD    */
    0x18,                               /* DMANOP   */
    0x00                                /* DMAEND   */
};

/* Watchdog abort at DMALD */
static const uint8_t dma_ldfe[] = {
    0xBC, 0x00, 0x00, 0x00, 0x01, 0x20, /* DMAMOV   SAR 0x20010000 */
    0xBC, 0x02, 0x00, 0x00, 0x02, 0x20, /* DMAMOV   DAR 0x20020000 */
    0xBC, 0x01, 0x34, 0x00, 0x0D, 0x00, /* DMAMOV   CCR SAF SS4 SB4 DAF DS4 DB4 */
                                        /* DMALPFE  */
    0x04,                               /* DMALD    */
    0x28, 0x01,                         /* DMALPEND */
    0x00                                /* DMAEND   */
};

/* Watchdog abort at DMAST */
static const uint8_t dma_stfe[] = {
    0xBC, 0x00, 0x00, 0x00, 0x01, 0x20, /* DMAMOV   SAR 0x20010000 */
    0xBC, 0x02, 0x00, 0x00, 0x02, 0x20, /* DMAMOV   DAR 0x20020000 */
    0xBC, 0x01, 0x34, 0x00, 0x0D, 0x00, /* DMAMOV   CCR SAF SS4 SB4 DAF DS4 DB4 */
                                        /* DMALPFE  */
    0x08,                               /* DMAST    */
    0x28, 0x01,                         /* DMALPEND */
    0x00                                /* DMAEND   */
};


static inline void dma_exec_dbg(const uint8_t *prog, int size)
{
    cpu_physical_memory_write(PROG_ADDR, prog, size);
    cpu_physical_memory_write(DBG0_ADDR, (uint8_t *)&dbg0, 4);
    cpu_physical_memory_write(DBG1_ADDR, (uint8_t *)&dbg1, 4);
    cpu_physical_memory_write(DBGGO_ADDR, (uint8_t *)&dbggo, 4);
}

static inline void dma_kill_dbg(void)
{
    uint32_t zeroval = 0x0;

    cpu_physical_memory_write(DBG0_ADDR, (uint8_t *)&dbgkill, 4);
    cpu_physical_memory_write(DBG1_ADDR, (uint8_t *)&zeroval, 4);
    cpu_physical_memory_write(DBGGO_ADDR, (uint8_t *)&dbggo, 4);
}

static __attribute__((unused)) void test_dma_script(struct s5pc1xx_state_s *s)
{
    uint8_t res[32];
    uint32_t reg;
    int outcome;
    int i;

    cpu_physical_memory_write(DATA_ADDR, dma_data, 32);

    /* TEST 1 */
    cpu_physical_memory_write(RESULT_ADDR, dma_data, 32);
    dma_exec_dbg(dma_stz, 20);
    outcome = 0;
    cpu_physical_memory_read(RESULT_ADDR, res, 16);
    for (i = 0; i < 16; i++) {
        if (res[i] != 0) {
            outcome = 1;
        }
    }
    if (outcome) {
        fprintf(stderr, "DMA test 1: DMASTZ. FAILED\n");
    } else {
        fprintf(stderr, "DMA test 1: DMASTZ. OK\n");
    }

    /* TEST 2 */
    cpu_physical_memory_write(RESULT_ADDR, dma_data, 32);
    dma_exec_dbg(dma_stzlp, 24);
    outcome = 0;
    cpu_physical_memory_read(RESULT_ADDR, res, 32);
    for (i = 0; i < 32; i++) {
        if (res[i] != 0) {
            outcome = 1;
        }
    }
    if (outcome) {
        fprintf(stderr, "DMA test 2: DMASTZ in loop. FAILED\n");
    } else {
        fprintf(stderr, "DMA test 2: DMASTZ in loop. OK\n");
    }

    /* TEST 3 */
    dma_exec_dbg(dma_stzlp, 24);
    dma_exec_dbg(dma_copy, 25);
    outcome = 0;
    cpu_physical_memory_read(RESULT_ADDR, res, 32);
    for (i = 0; i < 32; i++) {
        if (res[i] != dma_data[i]) {
            outcome = 1;
        }
    }
    if (outcome) {
        fprintf(stderr, "DMA test 3: DMA copy of 32 bytes of data. FAILED\n");
    } else {
        fprintf(stderr, "DMA test 3: DMA copy of 32 bytes of data. OK\n");
    }

    /* TEST 4 */
    dma_exec_dbg(dma_stzlp, 24);
    dma_exec_dbg(dma_copy_2, 25);
    outcome = 0;
    cpu_physical_memory_read(RESULT_ADDR, res, 32);
    for (i = 0; i < 32; i++) {
        if (res[i] != dma_data[i]) {
            outcome = 1;
        }
    }
    if (outcome) {
        fprintf(stderr, "DMA test 4: DMA copy of 32 bytes of data (store before load). FAILED\n");
    } else {
        fprintf(stderr, "DMA test 4: DMA copy of 32 bytes of data (store before load). OK\n");
    }

    /* TEST 5 */
    dma_exec_dbg(dma_stzlp, 24);
    dma_exec_dbg(dma_scatter, 25);
    outcome = 0;
    cpu_physical_memory_read(RESULT_ADDR, res, 32);
    for (i = 0; i < 32; i++) {
        if (res[i] != dma_data[i % 4]) {
            outcome = 1;
        }
    }
    if (outcome) {
        fprintf(stderr, "DMA test 5: DMA scatter. FAILED\n");
    } else {
        fprintf(stderr, "DMA test 5: DMA scatter. OK\n");
    }

    /* TEST 6 */
    dma_exec_dbg(dma_stzlp, 24);
    dma_exec_dbg(dma_gather, 25);
    outcome = 0;
    cpu_physical_memory_read(RESULT_ADDR, res, 32);
    for (i = 0; i < 32; i++) {
        if (res[i] != ((i > 3) ? 0 : dma_data[28 + i])) {
            outcome = 1;
        }
    }
    if (outcome) {
        fprintf(stderr, "DMA test 6: DMA gather. FAILED\n");
    } else {
        fprintf(stderr, "DMA test 6: DMA gather. OK\n");
    }

    /* TEST 7 */
    dma_exec_dbg(dma_stzlp, 24);
    dma_exec_dbg(dma_nost, 21);
    outcome = 0;
    cpu_physical_memory_read(FSC_ADDR, (uint8_t *)&reg, 4);
    if (! (reg & 2)) {
        outcome = 1;
    }
    cpu_physical_memory_read(FTC1_ADDR, (uint8_t *)&reg, 4);
    if (reg != ((unsigned)1 << 31)) {
        outcome = 1;
    }
    cpu_physical_memory_read(CPC1_ADDR, (uint8_t *)&reg, 4);
    if (reg != PROG_ADDR + 20) {
        outcome = 1;
    }
    dma_kill_dbg();
    cpu_physical_memory_read(FSC_ADDR, (uint8_t *)&reg, 4);
    if (reg & 2) {
        outcome = 1;
    }
    if (outcome) {
        fprintf(stderr, "DMA test 7: DMALD without DMAST. FAILED\n");
    } else {
        fprintf(stderr, "DMA test 7: DMALD without DMAST. OK\n");
    }

    /* TEST 8 */
    dma_exec_dbg(dma_stzlp, 24);
    dma_exec_dbg(dma_nold, 21);
    outcome = 0;
    cpu_physical_memory_read(FSC_ADDR, (uint8_t *)&reg, 4);
    if (! (reg & 2)) {
        outcome = 1;
    }
    cpu_physical_memory_read(FTC1_ADDR, (uint8_t *)&reg, 4);
    if (reg != ((unsigned)1 << 31)) {
        outcome = 1;
    }
    cpu_physical_memory_read(CPC1_ADDR, (uint8_t *)&reg, 4);
    if (reg != PROG_ADDR + 20) {
        outcome = 1;
    }
    dma_kill_dbg();
    cpu_physical_memory_read(FSC_ADDR, (uint8_t *)&reg, 4);
    if (reg & 2) {
        outcome = 1;
    }
    if (outcome) {
        fprintf(stderr, "DMA test 8: DMAST without DMALD. FAILED\n");
    } else {
        fprintf(stderr, "DMA test 8: DMAST without DMALD. OK\n");
    }

    /* TEST 9 */
    dma_exec_dbg(dma_stzlp, 24);
    dma_exec_dbg(dma_ldfe, 22);
    outcome = 0;
    cpu_physical_memory_read(FSC_ADDR, (uint8_t *)&reg, 4);
    if (! (reg & 2)) {
        outcome = 1;
    }
    cpu_physical_memory_read(FTC1_ADDR, (uint8_t *)&reg, 4);
    if (reg != ((unsigned)1 << 31)) {
        outcome = 1;
    }
    cpu_physical_memory_read(CPC1_ADDR, (uint8_t *)&reg, 4);
    if (reg != PROG_ADDR + 18) {
        outcome = 1;
    }
    dma_kill_dbg();
    cpu_physical_memory_read(FSC_ADDR, (uint8_t *)&reg, 4);
    if (reg & 2) {
        outcome = 1;
    }
    if (outcome) {
        fprintf(stderr, "DMA test 9: DMALD in infinite loop. FAILED\n");
    } else {
        fprintf(stderr, "DMA test 9: DMALD in infinite loop. OK\n");
    }

    /* TEST 10 */
    dma_exec_dbg(dma_stzlp, 24);
    dma_exec_dbg(dma_stfe, 22);
    outcome = 0;
    cpu_physical_memory_read(FSC_ADDR, (uint8_t *)&reg, 4);
    if (! (reg & 2)) {
        outcome = 1;
    }
    cpu_physical_memory_read(FTC1_ADDR, (uint8_t *)&reg, 4);
    if (reg != ((unsigned)1 << 31)) {
        outcome = 1;
    }
    cpu_physical_memory_read(CPC1_ADDR, (uint8_t *)&reg, 4);
    if (reg != PROG_ADDR + 18) {
        outcome = 1;
    }
    dma_kill_dbg();
    cpu_physical_memory_read(FSC_ADDR, (uint8_t *)&reg, 4);
    if (reg & 2) {
        outcome = 1;
    }
    if (outcome) {
        fprintf(stderr, "DMA test 10: DMAST in infinite loop. FAILED\n");
    } else {
        fprintf(stderr, "DMA test 10: DMAST in infinite loop. OK\n");
    }
}


/* UART */

#define TRSTATUS_ADDR   0xE2900810
#define FIFOCTL_ADDR    0xE2900808
#define TRANSMIT_ADDR   0xE2900820
#define RECIVE_ADDR     0xE2900824


static const char *hello = "Hello world!\n";
static char buf[256];


static __attribute__((unused)) void test_uart_script(void)
{
    uint32_t res;
    char *s;
    int i;

    res = 1;
    cpu_physical_memory_write(FIFOCTL_ADDR, (uint8_t *)&res, 4);
    cpu_physical_memory_read(TRSTATUS_ADDR, (uint8_t *)&res, 4);
    if (! (res & 4)) {
        fprintf(stderr, "Error: UART2 transmitter is not ready!\n");
    }
    s = (char *)hello;
    while (*s) {
        cpu_physical_memory_write(TRANSMIT_ADDR, (uint8_t *)s, 1);
        s++;
    }
    sleep(10);
    s = buf;
    i = 0;
    while (1) {
        cpu_physical_memory_read(TRSTATUS_ADDR, (uint8_t *)&res, 4);
        if (! (res & 1)) {
            break;
        }
        if (i >= 255) {
            fprintf(stderr, "Error: UART2 too many input data!\n");
            break;
        }
        cpu_physical_memory_read(RECIVE_ADDR, (uint8_t *)s, 1);
        s++;
    }
    *s = '\0';
    fprintf (stderr, "Read data: %s\n", s);
}

