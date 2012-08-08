/*
 * ARM PrimeCell PL330 DMA Controller
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Kirill Batuzov <batuzovk@ispras.ru>
 */

#include "sysbus.h"
#include "primecell.h"
#include "qemu-timer.h"


#define PL330_CHAN_NUM              8
#define PL330_PERIPH_NUM            32
#define PL330_MAX_BURST_LEN         128
#define PL330_INSN_MAXSIZE          6

#define PL330_FIFO_OK               0
#define PL330_FIFO_STALL            1
#define PL330_FIFO_ERR              (-1)

#define PL330_FAULT_UNDEF_INSTR     (1 <<  0)
#define PL330_FAULT_OPERAND_INVALID (1 <<  1)
#define PL330_FAULT_DMAGO_ER        (1 <<  4)
#define PL330_FAULT_EVENT_ER        (1 <<  5)
#define PL330_FAULT_CH_PERIPH_ER    (1 <<  6)
#define PL330_FAULT_CH_RDWR_ER      (1 <<  7)
#define PL330_FAULT_MFIFO_ER        (1 << 12)
#define PL330_FAULT_INSTR_FETCH_ER  (1 << 16)
#define PL330_FAULT_DATA_WRITE_ER   (1 << 17)
#define PL330_FAULT_DATA_READ_ER    (1 << 18)
#define PL330_FAULT_DBG_INSTR       (1 << 30)
#define PL330_FAULT_LOCKUP_ER       (1 << 31)

#define PL330_UNTAGGED              0xff

#define PL330_SINGLE                0x0
#define PL330_BURST                 0x1

#define PL330_WATCHDOG_LIMIT        1024

/* IOMEM mapped registers */
#define PL330_REG_DS        0x000
#define PL330_REG_DPC       0x004
#define PL330_REG_INTEN     0x020
#define PL330_REG_ES        0x024
#define PL330_REG_INTSTATUS 0x028
#define PL330_REG_INTCLR    0x02C
#define PL330_REG_FSM       0x030
#define PL330_REG_FSC       0x034
#define PL330_REG_FTM       0x038
#define PL330_REG_FTC_BASE  0x040
#define PL330_REG_CS_BASE   0x100
#define PL330_REG_CPC_BASE  0x104
#define PL330_REG_CHANCTRL  0x400
#define PL330_REG_DBGSTATUS 0xD00
#define PL330_REG_DBGCMD    0xD04
#define PL330_REG_DBGINST0  0xD08
#define PL330_REG_DBGINST1  0xD0C
#define PL330_REG_CONFIG    0xE00
#define PL330_REG_ID        0xFE0

#define PL330_IOMEM_SIZE    0x1000


static const uint32_t pl330_id[] =
{ 0x30, 0x13, 0x04, 0x00, 0xB1, 0x05, 0xF0, 0x0D };


/* DMA chanel states as they are described in PL330 Technical Reference Manual
   Most of them will not be used in emulation. */
enum pl330_chan_enum {
    pl330_chan_stopped = 0,
    pl330_chan_executing = 1,
    pl330_chan_completing,
    pl330_chan_waiting_periph,
    pl330_chan_at_barrier,
    pl330_chan_waiting_event = 4,
    pl330_chan_updating_pc = 3,
    pl330_chan_cache_miss,
    pl330_chan_fault_completing,
    pl330_chan_fault = 15,
    pl330_chan_killing
};

struct pl330_chan_state;
struct pl330_fifo;
struct pl330_queue_entry;
struct pl330_queue;
struct pl330_state;
struct pl330_insn_desc;

typedef struct pl330_chan_state pl330_chan_state;
typedef struct pl330_fifo pl330_fifo;
typedef struct pl330_queue_entry pl330_queue_entry;
typedef struct pl330_queue pl330_queue;
typedef struct pl330_state pl330_state;
typedef struct pl330_insn_desc pl330_insn_desc;


struct pl330_chan_state {
    target_phys_addr_t src;
    target_phys_addr_t dst;
    target_phys_addr_t pc;
    uint32_t control;
    uint32_t status;
    uint32_t lc[2];
    uint32_t fault_type;

    uint8_t ns;
    uint8_t is_manager;
    uint8_t request_flag;
    uint8_t wakeup;
    uint8_t wfp_sbp;

    enum pl330_chan_enum state;
    uint8_t stall;

    pl330_state *parent;
    uint8_t tag;
    uint32_t watchdog_timer;
};

struct pl330_fifo {
    uint8_t *buf;
    uint8_t *tag;
    int s, t;
    int buf_size;
};

struct pl330_queue_entry {
    target_phys_addr_t addr;
    int len;
    int n;
    int inc;
    int z;
    uint8_t tag;
    uint8_t seqn;
};

struct pl330_queue {
    pl330_queue_entry *queue;
    int queue_size;
    uint8_t *lo_seqn;
    uint8_t *hi_seqn;
};

struct pl330_state {
    SysBusDevice busdev;

    pl330_chan_state manager;
    pl330_chan_state *chan;
    pl330_fifo fifo;
    pl330_queue read_queue;
    pl330_queue write_queue;

    /* Config registers. cfg[5] = CfgDn. */
    uint32_t inten;
    uint32_t int_status;
    uint32_t ev_status;
    uint32_t cfg[6];
    uint32_t dbg[2];
    uint8_t debug_status;

    qemu_irq irq_abort;
    qemu_irq *irq;

    uint8_t num_faulting;

    int chan_num;
    int periph_num;
    unsigned int event_num;

    QEMUTimer *timer; /* is used for restore dma. */
    int8_t periph_busy[PL330_PERIPH_NUM];
};

struct pl330_insn_desc {
    /* OPCODE of the instruction */
    uint8_t opcode;
    /* Mask so we can select several sibling instructions, such as
       DMALD, DMALDS and DMALDB */
    uint8_t opmask;
    /* Size of instruction in bytes */
    uint8_t size;
    /* Interpreter */
    void (*exec)(pl330_chan_state *, uint8_t opcode, uint8_t *args, int len);
};


/* MFIFO Implementation */

/*
 * MFIFO is implemented as a cyclic buffer of BUF_SIZE size. Tagged bytes are
 * stored in this buffer. Data is stored in BUF field, tags - in the
 * corresponding array elemnets of TAG field.
 */

/* Initialize queue. */
static void pl330_fifo_init(pl330_fifo *s, uint32_t size)
{
    s->buf = qemu_mallocz(size * sizeof(uint8_t));
    s->tag = qemu_mallocz(size * sizeof(uint8_t));
    s->buf_size = size;
}

/* Cyclic increment */
static inline int pl330_fifo_inc(int x, int size)
{
    return (x + 1) % size;
}

/* Number of empty bytes in MFIFO */
static inline int pl330_fifo_num_free(pl330_fifo *s)
{
    if (s->t < s->s) {
        return s->s - s->t;
    } else {
        return s->buf_size - s->t + s->s;
    }
}

/* Number of bytes in MFIFO */
static inline int pl330_fifo_num_used(pl330_fifo *s)
{
    if (s->t >= s->s) {
        return s->t - s->s;
    } else {
        return s->buf_size - s->s + s->t;
    }
}

/* Push LEN bytes of data stored in BUF to MFIFO and tag it with TAG.
   Zero returned on success, PL330_FIFO_STALL if there is no enough free
   space in MFIFO to store requested amount of data. If push was unsaccessful
   no data is stored to MFIFO. */
static int pl330_fifo_push(pl330_fifo *s, uint8_t *buf, int len, uint8_t tag)
{
    int i;
    int old_s, old_t;

    old_s = s->s;
    old_t = s->t;
    for (i = 0; i < len; i++) {
        if (pl330_fifo_inc(s->t, s->buf_size) != s->s) {
            s->buf[s->t] = buf[i];
            s->tag[s->t] = tag;
            s->t = pl330_fifo_inc(s->t, s->buf_size);
        } else {
            /* Rollback transaction */
            s->s = old_s;
            s->t = old_t;
            return PL330_FIFO_STALL;
        }
    }
    return PL330_FIFO_OK;
}

/* Get LEN bytes of data from MFIFO and store it to BUF. Tag value of each
   byte is veryfied. Zero returned on success, PL330_FIFO_ERR on tag missmatch
   and PL330_FIFO_STALL if there is no enough data in MFIFO. If get was
   unsaccessful no data is removed from MFIFO. */
static int pl330_fifo_get(pl330_fifo *s, uint8_t *buf, int len, uint8_t tag)
{
    int i, ret;
    int old_s, old_t;

    old_s = s->s;
    old_t = s->t;
    for (i = 0; i < len; i++) {
        if (s->t != s->s && s->tag[s->s] == tag) {
            buf[i] = s->buf[s->s];
            s->s = pl330_fifo_inc(s->s, s->buf_size);
        } else {
            /* Rollback transaction */
            if (s->t == s->s)
                ret = PL330_FIFO_STALL;
            else
                ret = PL330_FIFO_ERR;
            s->s = old_s;
            s->t = old_t;
            return ret;
        }
    }
    return PL330_FIFO_OK;
}

/* Reset MFIFO. This completely erases all data in it. */
static inline void pl330_fifo_reset(pl330_fifo *s)
{
    s->s = 0;
    s->t = 0;
}

/* Return tag of the first byte stored in MFIFO. If MFIFO is empty
   PL330_UNTAGGED is returned. */
static inline uint8_t pl330_fifo_tag(pl330_fifo *s)
{
    if (s->t == s->s)
        return PL330_UNTAGGED;
    return s->tag[s->s];
}

/* Returns non-zero if tag TAG is present in fifo or zero otherwise */
static int pl330_fifo_has_tag(pl330_fifo *s, uint8_t tag)
{
    int i;

    for (i = s->s; i != s->t; i = pl330_fifo_inc(i, s->buf_size)) {
        if (s->tag[i] == tag)
            return 1;
    }
    return 0;
}

/* Remove all entry tagged with TAG from MFIFO */
static void pl330_fifo_tagged_remove(pl330_fifo *s, uint8_t tag)
{
    int i, t;

    t = s->s;
    for (i = s->s; i != s->t; i = pl330_fifo_inc(i, s->buf_size)) {
        if (s->tag[i] != tag) {
            s->buf[t] = s->buf[i];
            s->tag[t] = s->tag[i];
            t++;
        }
    }
    s->t = t;
}


/* Read-Write Queue implementation */

/*
 * Read-Write Queue stores up to QUEUE_SIZE instructions (loads or stores).
 * Each instructions is described by source (for loads) or destination (for
 * stores) address ADDR, width of data to be loaded/stored LEN, number of
 * stores/loads to be performed N, INC bit, Z bit and TAG to identify channel
 * this instruction belongs to. Queue does not store any information about
 * nature of the instruction: is it load or store. PL330 has different queues
 * for loads and stores so this is already known at the top level where it matters.
 *
 * Queue works as FIFO for instructions with equivalent tags, but can issue
 * instructions with different tags in arbitrary order. SEQN field attached to
 * each instruction helps to achieve this. For each TAG queue contains
 * instructions with consecutive SEQN values ranged from LO_SEQN[TAG] to
 * HI_SEQN[TAG]-1 inclusive. SEQN is 8-bit unsigned integer, so SEQN=255 is
 * followed by SEQN=0.
 *
 * Z bit indicates that zeroes should be stored. Thus no MFIFO fetches
 * are performed in this case.
 */

static void pl330_queue_reset(pl330_queue *s)
{
    int i;

    for (i = 0; i < s->queue_size; i++)
        s->queue[i].tag = PL330_UNTAGGED;
}

/* Initialize queue */
static void pl330_queue_init(pl330_queue *s, int size, int channum)
{
    s->queue = (pl330_queue_entry *)
        qemu_mallocz(size * sizeof(pl330_queue_entry));
    s->lo_seqn = (uint8_t *)qemu_mallocz(channum * sizeof(uint8_t));
    s->hi_seqn = (uint8_t *)qemu_mallocz(channum * sizeof(uint8_t));
    s->queue_size = size;
}

/* Returns pointer to an empty slot or NULL if queue is full */
static pl330_queue_entry *pl330_queue_find_empty(pl330_queue *s)
{
    int i;

    for (i = 0; i < s->queue_size; i++)
        if (s->queue[i].tag == PL330_UNTAGGED)
            return &s->queue[i];
    return NULL;
}

/* Puts instruction to queue.
   Return value:
     - zero - OK
     - non-zero - queue is full */
static int pl330_insn_to_queue(pl330_queue *s, target_phys_addr_t addr,
                               int len, int n, int inc, int z, uint8_t tag)
{
    pl330_queue_entry *entry = pl330_queue_find_empty(s);

    if (! entry)
        return 1;
    entry->tag = tag;
    entry->addr = addr;
    entry->len = len;
    entry->n = n;
    entry->z = z;
    entry->inc = inc;
    entry->seqn = s->hi_seqn[tag];
    s->hi_seqn[tag]++;
    return 0;
}

/* Returns a pointer to queue slot containing instruction which satisfies
   following conditions:
    - it has valid tag value (not PL330_UNTAGGED)
    - it can be issued without violating queue logic (see above)
    - if TAG argument is not PL330_UNTAGGED this instruction has tag value
      equivalent to the argument TAG value.
   If such instruction cannot be found NULL is returned. */
static pl330_queue_entry *pl330_queue_find_insn(pl330_queue *s,
                                                         uint8_t tag)
{
    int i;

    for (i = 0; i < s->queue_size; i++) {
        if (s->queue[i].tag != PL330_UNTAGGED) {
            if (s->queue[i].seqn == s->lo_seqn[s->queue[i].tag] &&
                (s->queue[i].tag == tag ||
                 tag == PL330_UNTAGGED ||
                 s->queue[i].z))
                return &s->queue[i];
        }
    }
    return NULL;
}

/* Removes instruction from queue. */
static inline void pl330_insn_from_queue(pl330_queue *s,
                                         pl330_queue_entry *e)
{
    s->lo_seqn[e->tag]++;
    e->tag = PL330_UNTAGGED;
}

/* Removes all instructions tagged with TAG from queue. */
static inline void pl330_tag_from_queue(pl330_queue *s, uint8_t tag)
{
    int i;

    for (i = 0; i < s->queue_size; i++) {
        if (s->queue[i].tag == tag)
            s->queue[i].tag = PL330_UNTAGGED;
    }
}


/* DMA instruction execution engine */

/* Moves DMA channel to the FAULT state and updates it's status. */
static inline void pl330_fault(pl330_chan_state *ch, uint32_t flags)
{
    ch->fault_type |= flags;
    ch->state = pl330_chan_fault;
    ch->parent->num_faulting++;
    if (ch->parent->num_faulting == 1) {
        qemu_irq_raise(ch->parent->irq_abort);
    }
}

/*
 * For information about instructions see PL330 Technical Reference Manual.
 *
 * Arguments:
 *   CH - chanel executing the instruction
 *   OPCODE - opcode
 *   ARGS - array of 8-bit arguments
 *   LEN - number of elements in ARGS array
 */
static void pl330_dmaaddh(pl330_chan_state *ch, uint8_t opcode,
                          uint8_t *args, int len)
{
    uint16_t im = (((uint16_t)args[0]) << 8) | ((uint16_t)args[1]);
    uint8_t ra = (opcode >> 1) & 1;

    if (ra) {
        ch->dst += im;
    } else {
        ch->src += im;
    }
}

static void pl330_dmaend(pl330_chan_state *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    if (ch->state == pl330_chan_executing && !ch->is_manager) {
        /* Wait for all transfers to complete */
        if (pl330_fifo_has_tag(&ch->parent->fifo, ch->tag) ||
            pl330_queue_find_insn(&ch->parent->read_queue, ch->tag) != NULL ||
            pl330_queue_find_insn(&ch->parent->write_queue, ch->tag) != NULL) {

            ch->stall = 1;
            return;
        }
    }
    pl330_fifo_tagged_remove(&ch->parent->fifo, ch->tag);
    pl330_tag_from_queue(&ch->parent->read_queue, ch->tag);
    pl330_tag_from_queue(&ch->parent->write_queue, ch->tag);
    ch->state = pl330_chan_stopped;
}

static void pl330_dmaflushp(pl330_chan_state *ch, uint8_t opcode,
                            uint8_t *args, int len)
{
    uint8_t periph_id;

    if (args[0] & 7) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    periph_id = (args[0] >> 3) & 0x1f;
    if (periph_id >= ch->parent->periph_num) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if (ch->ns > ((ch->parent->cfg[4] & (1 << periph_id)) >> periph_id)) {
        pl330_fault(ch, PL330_FAULT_CH_PERIPH_ER);
        return;
    }
    /* Do nothing */
}

static void pl330_dmago(pl330_chan_state *ch, uint8_t opcode,
                        uint8_t *args, int len)
{
    uint8_t chan_id;
    uint8_t ns;
    uint32_t pc;
    pl330_chan_state *s;

    if (! ch->is_manager) {
        /* TODO: what error actually is it? */
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    ns = (opcode >> 1) & 1;
    chan_id = args[0] & 7;
    if ((args[0] >> 3)) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if (chan_id >= ch->parent->chan_num) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    pc = (((uint32_t)args[4]) << 24) | (((uint32_t)args[3]) << 16) |
         (((uint32_t)args[2]) << 8)  | (((uint32_t)args[1]));
    if (ch->parent->chan[chan_id].state != pl330_chan_stopped) {
        /* TODO: what error actually is it? */
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if (ch->ns > ns) {
        pl330_fault(ch, PL330_FAULT_DMAGO_ER);
        return;
    }
    s = &ch->parent->chan[chan_id];
    s->ns = ns;
    s->pc = pc;
    s->state = pl330_chan_executing;
}

static void pl330_dmald(pl330_chan_state *ch, uint8_t opcode,
                        uint8_t *args, int len)
{
    uint8_t bs = opcode & 3;
    uint32_t size, num, inc;

    if (bs == 2) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if ((bs == 1 && ch->request_flag == PL330_BURST) ||
        (bs == 3 && ch->request_flag == PL330_SINGLE)) {
        /* Perform NOP */
        return;
    }
    num = ((ch->control >> 4) & 0xf) + 1;
    size = (uint32_t)1 << ((ch->control >> 1) & 0x7);
    inc = ch->control & 1;
    ch->stall = pl330_insn_to_queue(&ch->parent->read_queue, ch->src,
                                    size, num, inc, 0, ch->tag);
    if (inc) {
        ch->src += size * num;
    }
}

static void pl330_dmaldp(pl330_chan_state *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    uint8_t periph_id;

    if (args[0] & 7) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    periph_id = (args[0] >> 3) & 0x1f;
    if (periph_id >= ch->parent->periph_num) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if (ch->ns > ((ch->parent->cfg[4] & (1 << periph_id)) >> periph_id)) {
        pl330_fault(ch, PL330_FAULT_CH_PERIPH_ER);
        return;
    }
    pl330_dmald(ch, opcode, args, len);
}

static void pl330_dmalp(pl330_chan_state *ch, uint8_t opcode,
                        uint8_t *args, int len)
{
    uint8_t lc = (opcode & 2) >> 1;

    ch->lc[lc] = args[0];
}

static void pl330_dmalpend(pl330_chan_state *ch, uint8_t opcode,
                           uint8_t *args, int len)
{
    uint8_t nf = (opcode & 0x10) >> 4;
    uint8_t bs = opcode & 3;
    uint8_t lc = (opcode & 4) >> 2;

    if (bs == 2) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if ((bs == 1 && ch->request_flag == PL330_BURST) ||
        (bs == 3 && ch->request_flag == PL330_SINGLE)) {
        /* Perform NOP */
        return;
    }
    if (!nf || ch->lc[lc]) {
        if (nf) {
            ch->lc[lc]--;
        }
        ch->pc -= args[0];
        ch->pc -= len + 1;
        /* "ch->pc -= args[0] + len + 1" is incorrect when args[0] == 256 */
    }
}

static void pl330_dmakill(pl330_chan_state *ch, uint8_t opcode,
                          uint8_t *args, int len)
{
    if (ch->state == pl330_chan_fault ||
        ch->state == pl330_chan_fault_completing) {
        /* This is the only way for chanel from faulting state */
        ch->fault_type = 0;
        ch->parent->num_faulting--;
        if (ch->parent->num_faulting == 0) {
            qemu_irq_lower(ch->parent->irq_abort);
        }
    }
    ch->state = pl330_chan_killing;
    pl330_fifo_tagged_remove(&ch->parent->fifo, ch->tag);
    pl330_tag_from_queue(&ch->parent->read_queue, ch->tag);
    pl330_tag_from_queue(&ch->parent->write_queue, ch->tag);
    ch->state = pl330_chan_stopped;
}

static void pl330_dmamov(pl330_chan_state *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    uint8_t rd = args[0] & 7;
    uint32_t im;

    if ((args[0] >> 3)) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    im = (((uint32_t)args[4]) << 24) | (((uint32_t)args[3]) << 16) |
         (((uint32_t)args[2]) << 8)  | (((uint32_t)args[1]));
    switch (rd) {
    case 0:
        ch->src = im;
        break;
    case 1:
        ch->control = im;
        break;
    case 2:
        ch->dst = im;
        break;
    default:
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
}

static void pl330_dmanop(pl330_chan_state *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    /* NOP is NOP. */
}

static void pl330_dmarmb(pl330_chan_state *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    /* Do nothing. Since we do not emulate AXI Bus transactions there is no
       stalls. So we are allways on barrier. */
}

static void pl330_dmasev(pl330_chan_state *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    uint8_t ev_id;

    if (args[0] & 7) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    ev_id = (args[0] >> 3) & 0x1f;
    if (ev_id >= ch->parent->event_num) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if (ch->ns > ((ch->parent->cfg[3] & (1 << ev_id)) >> ev_id)) {
        pl330_fault(ch, PL330_FAULT_EVENT_ER);
        return;
    }
    if (ch->parent->inten & (1 << ev_id)) {
        ch->parent->int_status |= (1 << ev_id);
        qemu_irq_raise(ch->parent->irq[ev_id]);
    } else {
        ch->parent->ev_status |= (1 << ev_id);
    }
}

static void pl330_dmast(pl330_chan_state *ch, uint8_t opcode,
                        uint8_t *args, int len)
{
    uint8_t bs = opcode & 3;
    uint32_t size, num, inc;

    if (bs == 2) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if ((bs == 1 && ch->request_flag == PL330_BURST) ||
        (bs == 3 && ch->request_flag == PL330_SINGLE)) {
        /* Perform NOP */
        return;
    }
    num = ((ch->control >> 18) & 0xf) + 1;
    size = (uint32_t)1 << ((ch->control >> 15) & 0x7);
    inc = (ch->control >> 14) & 1;
    ch->stall = pl330_insn_to_queue(&ch->parent->write_queue, ch->dst,
                                    size, num, inc, 0, ch->tag);
    if (inc) {
        ch->dst += size * num;
    }
}

static void pl330_dmastp(pl330_chan_state *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    uint8_t periph_id;

    if (args[0] & 7) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    periph_id = (args[0] >> 3) & 0x1f;
    if (periph_id >= ch->parent->periph_num) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if (ch->ns > ((ch->parent->cfg[4] & (1 << periph_id)) >> periph_id)) {
        pl330_fault(ch, PL330_FAULT_CH_PERIPH_ER);
        return;
    }
    pl330_dmast(ch, opcode, args, len);
}

static void pl330_dmastz(pl330_chan_state *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    uint32_t size, num, inc;

    num = ((ch->control >> 18) & 0xf) + 1;
    size = (uint32_t)1 << ((ch->control >> 15) & 0x7);
    inc = (ch->control >> 14) & 1;
    ch->stall = pl330_insn_to_queue(&ch->parent->write_queue, ch->dst,
                                    size, num, inc, 1, ch->tag);
    if (inc) {
        ch->dst += size * num;
    }
}

static void pl330_dmawfe(pl330_chan_state *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    uint8_t ev_id;

    if (args[0] & 5) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    ev_id = (args[0] >> 3) & 0x1f;
    if (ev_id >= ch->parent->event_num) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if (ch->ns > ((ch->parent->cfg[3] & (1 << ev_id)) >> ev_id)) {
        pl330_fault(ch, PL330_FAULT_EVENT_ER);
        return;
    }
    ch->wakeup = ev_id;
}

static void pl330_dmawfp(pl330_chan_state *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    uint8_t bs = opcode & 3;
    uint8_t periph_id;

    if (args[0] & 7) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    periph_id = (args[0] >> 3) & 0x1f;
    if (periph_id >= ch->parent->periph_num) {
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }
    if (ch->ns > ((ch->parent->cfg[4] & (1 << periph_id)) >> periph_id)) {
        pl330_fault(ch, PL330_FAULT_CH_PERIPH_ER);
        return;
    }
    switch (bs) {
    case 0: /* S */
        ch->request_flag = PL330_SINGLE;
        ch->wfp_sbp = 0;
        break;
    case 1: /* P */
        ch->request_flag = PL330_BURST;
        ch->wfp_sbp = 2;
        break;
    case 2: /* B */
        ch->request_flag = PL330_BURST;
        ch->wfp_sbp = 1;
        break;
    default:
        pl330_fault(ch, PL330_FAULT_OPERAND_INVALID);
        return;
    }

    if (ch->parent->periph_busy[periph_id]) {
        ch->state = pl330_chan_waiting_periph;
    } else if (ch->state == pl330_chan_waiting_periph) {
        ch->state = pl330_chan_executing;
    }
}

static void pl330_dmawmb(pl330_chan_state *ch, uint8_t opcode,
                         uint8_t *args, int len)
{
    /* Do nothing. Since we do not emulate AXI Bus transactions there is no
       stalls. So we are allways on barrier. */
}

/* "NULL" terminated array of the instruction descriptions. */
static const pl330_insn_desc insn_desc[] = {
    { .opcode = 0x54, .opmask = 0xFD, .size = 3, .exec = pl330_dmaaddh },
    { .opcode = 0x00, .opmask = 0xFF, .size = 1, .exec = pl330_dmaend },
    { .opcode = 0x35, .opmask = 0xFF, .size = 2, .exec = pl330_dmaflushp },
    { .opcode = 0xA0, .opmask = 0xFD, .size = 6, .exec = pl330_dmago },
    { .opcode = 0x04, .opmask = 0xFC, .size = 1, .exec = pl330_dmald },
    { .opcode = 0x25, .opmask = 0xFD, .size = 2, .exec = pl330_dmaldp },
    { .opcode = 0x20, .opmask = 0xFD, .size = 2, .exec = pl330_dmalp },
    /* dmastp  must be before dmalpend in tis map, because thay maps
     * are overrided */
    { .opcode = 0x29, .opmask = 0xFD, .size = 2, .exec = pl330_dmastp },
    { .opcode = 0x28, .opmask = 0xE8, .size = 2, .exec = pl330_dmalpend },
    { .opcode = 0x01, .opmask = 0xFF, .size = 1, .exec = pl330_dmakill },
    { .opcode = 0xBC, .opmask = 0xFF, .size = 6, .exec = pl330_dmamov },
    { .opcode = 0x18, .opmask = 0xFF, .size = 1, .exec = pl330_dmanop },
    { .opcode = 0x12, .opmask = 0xFF, .size = 1, .exec = pl330_dmarmb },
    { .opcode = 0x34, .opmask = 0xFF, .size = 2, .exec = pl330_dmasev },
    { .opcode = 0x08, .opmask = 0xFC, .size = 1, .exec = pl330_dmast },
    { .opcode = 0x0C, .opmask = 0xFF, .size = 1, .exec = pl330_dmastz },
    { .opcode = 0x36, .opmask = 0xFF, .size = 2, .exec = pl330_dmawfe },
    { .opcode = 0x30, .opmask = 0xFC, .size = 2, .exec = pl330_dmawfp },
    { .opcode = 0x13, .opmask = 0xFF, .size = 1, .exec = pl330_dmawmb },
    { .opcode = 0x00, .opmask = 0x00, .size = 0, .exec = NULL }
};

/* Instructions which can be issued via debug registers. */
static const pl330_insn_desc debug_insn_desc[] = {
    { .opcode = 0xA0, .opmask = 0xFD, .size = 6, .exec = pl330_dmago },
    { .opcode = 0x01, .opmask = 0xFF, .size = 1, .exec = pl330_dmakill },
    { .opcode = 0x34, .opmask = 0xFF, .size = 2, .exec = pl330_dmasev },
    { .opcode = 0x00, .opmask = 0x00, .size = 0, .exec = NULL }
};

static inline const pl330_insn_desc *
    pl330_fetch_insn(pl330_chan_state *ch)
{
    uint8_t opcode;
    int i;

    cpu_physical_memory_read(ch->pc, &opcode, 1);
    for (i = 0; insn_desc[i].size; i++)
        if ((opcode & insn_desc[i].opmask) == insn_desc[i].opcode)
            return &insn_desc[i];
    return NULL;
}

static inline void pl330_exec_insn(pl330_chan_state *ch,
                                   const pl330_insn_desc *insn)
{
    uint8_t buf[PL330_INSN_MAXSIZE];

    cpu_physical_memory_read(ch->pc, buf, insn->size);
    insn->exec(ch, buf[0], &buf[1], insn->size - 1);
}

static inline void pl330_update_pc(pl330_chan_state *ch,
                                   const pl330_insn_desc *insn)
{
    ch->pc += insn->size;
}

/* Try to execute current instruction in channel CH. Number of executed
   instructions is returned (0 or 1). */
static int pl330_chan_exec(pl330_chan_state *ch)
{
    const pl330_insn_desc *insn;

    if (ch->state != pl330_chan_executing && ch->state != pl330_chan_waiting_periph) {
        return 0;
    }
    ch->stall = 0;
    insn = pl330_fetch_insn(ch);
    if (! insn) {
        pl330_fault(ch, PL330_FAULT_UNDEF_INSTR);
        return 0;
    }
    pl330_exec_insn(ch, insn);
    if (ch->state == pl330_chan_executing && !ch->stall) {
        pl330_update_pc(ch, insn);
        ch->watchdog_timer = 0;
        return 1;
    } else {
        if (ch->stall) {
            ch->watchdog_timer++;
            if (ch->watchdog_timer >= PL330_WATCHDOG_LIMIT) {
                pl330_fault(ch, PL330_FAULT_LOCKUP_ER);
            }
        }
    }
    return 0;
}

/* Try to execute 1 instruction in each channel, one instruction from read
   queue and one instruction from write queue. Number of successfully executed
   instructions is returned. */
static int pl330_exec_cycle(pl330_chan_state *channel)
{
    pl330_state *s = channel->parent;
    pl330_queue_entry *q;
    int i;
    int num_exec = 0;
    int fifo_res = 0;
    uint8_t buf[PL330_MAX_BURST_LEN];

    /* Execute one instruction in each channel */
    num_exec += pl330_chan_exec(channel);

    /* Execute one instruction from read queue */
    q = pl330_queue_find_insn(&s->read_queue, PL330_UNTAGGED);
    if (q != NULL && q->len <= pl330_fifo_num_free(&s->fifo)) {
        cpu_physical_memory_read(q->addr, buf, q->len);
        fifo_res = pl330_fifo_push(&s->fifo, buf, q->len, q->tag);
        if (fifo_res == PL330_FIFO_OK) {
            if (q->inc) {
                q->addr += q->len;
            }
            q->n--;
            if (! q->n) {
                pl330_insn_from_queue(&s->read_queue, q);
            }
            num_exec++;
        }
    }

    /* Execute one instruction from write queue. */
    q = pl330_queue_find_insn(&s->write_queue, pl330_fifo_tag(&s->fifo));
    if (q != NULL && (q->z || q->len <= pl330_fifo_num_used(&s->fifo))) {
        if (q->z) {
            for (i = 0; i < q->len; i++) {
                buf[i] = 0;
            }
        } else {
            fifo_res = pl330_fifo_get(&s->fifo, buf, q->len, q->tag);
        }
        if (fifo_res == PL330_FIFO_OK || q->z) {
            cpu_physical_memory_write(q->addr, buf, q->len);
            if (q->inc) {
                q->addr += q->len;
            }
            q->n--;
            if (! q->n) {
                pl330_insn_from_queue(&s->write_queue, q);
            }
            num_exec++;
        }
    }

    return num_exec;
}

static int pl330_exec_channel(pl330_chan_state *channel)
{
    int insr_exec = 0;

    /* TODO: Is it all right to execute everything or should we do per-cycle
       simulation? */
    while (pl330_exec_cycle(channel))
        insr_exec++;

    /* Detect deadlock */
    if (channel->state == pl330_chan_executing) {
        pl330_fault(channel, PL330_FAULT_LOCKUP_ER);
    }
    /* Situation when one of the queues has deadlocked but all channels
       has finished their programs should be impossible. */

    return insr_exec;
}


static inline void pl330_exec(pl330_state *s)
{
    int i, insr_exec;
    do {
        insr_exec = pl330_exec_channel(&s->manager);

        for (i = 0; i < s->chan_num; i++) {
            insr_exec += pl330_exec_channel(&s->chan[i]);
        }
    } while (insr_exec);
}

static void pl330_exec_cycle_timer(void *opaque)
{
    struct pl330_state *s = (struct pl330_state *)opaque;
    pl330_exec(s);
}

/* Stop or restore dma operations */
static void pl330_dma_stop_irq(void *opaque, int irq, int level)
{
    struct pl330_state *s = (struct pl330_state *)opaque;

    if (s->periph_busy[irq] != level) {
        s->periph_busy[irq] = level;
        qemu_mod_timer(s->timer, qemu_get_clock(vm_clock));
    }
}

static void pl330_debug_exec(pl330_state *s)
{
    uint8_t args[5];
    uint8_t opcode;
    uint8_t chan_id;
    int i;
    pl330_chan_state *ch;
    const pl330_insn_desc *insn;

    s->debug_status = 1;
    chan_id = (s->dbg[0] >>  8) & 0x07;
    opcode  = (s->dbg[0] >> 16) & 0xff;
    args[0] = (s->dbg[0] >> 24) & 0xff;
    args[1] = (s->dbg[1] >>  0) & 0xff;
    args[2] = (s->dbg[1] >>  8) & 0xff;
    args[3] = (s->dbg[1] >> 16) & 0xff;
    args[4] = (s->dbg[1] >> 24) & 0xff;
    if (s->dbg[0] & 1) {
        ch = &s->chan[chan_id];
    } else {
        ch = &s->manager;
    }
    insn = NULL;
    for (i = 0; debug_insn_desc[i].size; i++)
        if ((opcode & debug_insn_desc[i].opmask) == debug_insn_desc[i].opcode)
            insn = &debug_insn_desc[i];
    if (!insn) {
        pl330_fault(ch, PL330_FAULT_UNDEF_INSTR);
        return ;
    }
    ch->stall = 0;
    insn->exec(ch, opcode, args, insn->size - 1);
    if (ch->stall) {
        hw_error("pl330: stall of debug instruction not implemented\n");
    }
    s->debug_status = 0;
}


/* IOMEM mapped registers */

static void pl330_iomem_write(void *opaque, target_phys_addr_t offset,
                              uint32_t value)
{
    pl330_state *s = (pl330_state *) opaque;
    uint32_t i;

    if (offset & 3) {
        hw_error("pl330: bad write offset " TARGET_FMT_plx "\n", offset);
    }
    switch (offset) {
    case PL330_REG_INTEN:
        s->inten = value;
        break;
    case PL330_REG_INTCLR:
        for (i = 0; i < s->event_num; i++) {
            if (s->int_status & s->inten & value & (1 << i)) {
                qemu_irq_lower(s->irq[i]);
            }
        }
        s->int_status &= ~value;
        break;
    case PL330_REG_DBGCMD:
        if ((value & 3) == 0) {
            pl330_debug_exec(s);
            pl330_exec(s);
        } else {
            hw_error("pl330: write of illegal value %u for offset "
                     TARGET_FMT_plx "\n", value, offset);
        }
        break;
    case PL330_REG_DBGINST0:
        s->dbg[0] = value;
        break;
    case PL330_REG_DBGINST1:
        s->dbg[1] = value;
        break;
    default:
        hw_error("pl330: bad write offset " TARGET_FMT_plx "\n", offset);
        break;
    }
}

static uint32_t pl330_iomem_read(void *opaque, target_phys_addr_t offset)
{
    pl330_state *s = (pl330_state *) opaque;
    int chan_id;
    int i;
    uint32_t res;

    if (offset & 3) {
        hw_error("pl330: bad read offset " TARGET_FMT_plx "\n", offset);
    }

    if (offset >= PL330_REG_ID && offset < PL330_REG_ID + 32) {
        return pl330_id[(offset - PL330_REG_ID) >> 2];
    }
    if (offset >= PL330_REG_CONFIG && offset < PL330_REG_CONFIG + 24) {
        return s->cfg[(offset - PL330_REG_CONFIG) >> 2];
    }
    if (offset >= PL330_REG_CHANCTRL && offset < 0xD00) {
        offset -= PL330_REG_CHANCTRL;
        chan_id = offset >> 5;
        if (chan_id >= s->chan_num) {
            hw_error("pl330: bad read offset " TARGET_FMT_plx "\n", offset);
        }
        switch (offset & 0x1f) {
        case 0x00:
            return s->chan[chan_id].src;
        case 0x04:
            return s->chan[chan_id].dst;
        case 0x08:
            return s->chan[chan_id].control;
        case 0x0C:
            return s->chan[chan_id].lc[0];
        case 0x10:
            return s->chan[chan_id].lc[1];
        default:
            hw_error("pl330: bad read offset " TARGET_FMT_plx "\n", offset);
        }
    }
    if (offset >= PL330_REG_CS_BASE && offset < 0x400) {
        offset -= PL330_REG_CS_BASE;
        chan_id = offset >> 3;
        if (chan_id >= s->chan_num) {
            hw_error("pl330: bad read offset " TARGET_FMT_plx "\n", offset);
        }
        switch ((offset >> 2) & 1) {
        case 0x0:
            return (s->chan[chan_id].ns << 20) | (s->chan[chan_id].wakeup << 4) |
                   (s->chan[chan_id].state & 0xf) |
                   (s->chan[chan_id].wfp_sbp << 13);
        case 0x1:
            return s->chan[chan_id].pc;
        default:
            hw_error("pl330: read error\n");
        }
    }
    if (offset >= PL330_REG_FTC_BASE && offset < 0x100) {
        offset -= PL330_REG_FTC_BASE;
        chan_id = offset >> 2;
        if (chan_id >= s->chan_num) {
            hw_error("pl330: bad read offset " TARGET_FMT_plx "\n", offset);
        }
        return s->chan[chan_id].fault_type;
    }
    switch (offset) {
    case PL330_REG_DS:
        return (s->manager.ns << 8) | (s->manager.wakeup << 4) |
            (s->manager.state & 0xf);
    case PL330_REG_DPC:
        return s->manager.pc;
    case PL330_REG_INTEN:
        return s->inten;
    case PL330_REG_ES:
        return s->ev_status;
    case PL330_REG_INTSTATUS:
        return s->int_status;
    case PL330_REG_INTCLR:
        /* Documentation says that we can't read this register
         * but linux kernel does it */
        return 0;
    case PL330_REG_FSM:
        return s->manager.fault_type ? 1 : 0;
    case PL330_REG_FSC:
        res = 0;
        for (i = 0; i < s->chan_num; i++) {
            if (s->chan[i].state == pl330_chan_fault ||
                s->chan[i].state == pl330_chan_fault_completing) {
                res |= 1 << i;
            }
        }
        return res;
    case PL330_REG_FTM:
        return s->manager.fault_type;
    case PL330_REG_DBGSTATUS:
        return s->debug_status;
    default:
        hw_error("pl330: bad read offset " TARGET_FMT_plx "\n", offset);
    }
}

static CPUReadMemoryFunc * const pl330_readfn[] = {
    pl330_iomem_read,
    pl330_iomem_read,
    pl330_iomem_read
};

static CPUWriteMemoryFunc * const pl330_writefn[] = {
    pl330_iomem_write,
    pl330_iomem_write,
    pl330_iomem_write
};


/* Controller logic and initialization */

static void pl330_chan_reset(pl330_chan_state *ch)
{
    ch->src = 0;
    ch->dst = 0;
    ch->pc = 0;
    ch->state = pl330_chan_stopped;
    ch->watchdog_timer = 0;
    ch->stall = 0;
    ch->control = 0;
    ch->status = 0;
    ch->fault_type = 0;
}

static void pl330_reset(DeviceState *d)
{
    int i;
    pl330_state *s = FROM_SYSBUS(pl330_state, sysbus_from_qdev(d));

    s->inten = 0;
    s->int_status = 0;
    s->ev_status = 0;
    s->debug_status = 0;
    s->num_faulting = 0;
    pl330_fifo_reset(&s->fifo);
    pl330_queue_reset(&s->read_queue);
    pl330_queue_reset(&s->write_queue);

    for (i = 0; i < s->chan_num; i++) {
        pl330_chan_reset(&s->chan[i]);
    }
    for (i = 0; i < s->periph_num; i++) {
        s->periph_busy[i] = 0;
    }
}

/* PrimeCell PL330 Initialization. CFG is a 6-element array of 32-bit integers.
   CFG[0] is Config Register 0, CFG[1] - Config Register 1, ..., CFG[4] -
   Config Register 4 and CFG[5] - Config Register Dn. IRQS is an array of
   interrupts. Required number of elements in this array is discribed by
   bits [21:17] of CFG[0]. */
DeviceState *pl330_init(target_phys_addr_t base, const uint32_t *cfg,
                        qemu_irq *irqs, qemu_irq irq_abort)
{
    DeviceState *dev;
    SysBusDevice *s;
    int i;

    dev = qdev_create(NULL, "pl330");
    qdev_prop_set_uint32(dev, "cfg0", cfg[0]);
    qdev_prop_set_uint32(dev, "cfg1", cfg[1]);
    qdev_prop_set_uint32(dev, "cfg2", cfg[2]);
    qdev_prop_set_uint32(dev, "cfg3", cfg[3]);
    qdev_prop_set_uint32(dev, "cfg4", cfg[4]);
    qdev_prop_set_uint32(dev, "cfg5", cfg[5]);

    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    sysbus_connect_irq(s, 0, irq_abort);
    for (i = 0; i < ((cfg[0] >> 17) & 0x1f) + 1; i++)
        sysbus_connect_irq(s, i + 1, irqs[i]);
    sysbus_mmio_map(s, 0, base);

    return dev;
}

static int pl330_init1(SysBusDevice *dev)
{
    int i;
    int iomem;
    pl330_state *s = FROM_SYSBUS(pl330_state, dev);

    sysbus_init_irq(dev, &s->irq_abort);
    iomem = cpu_register_io_memory(pl330_readfn, pl330_writefn, s);
    sysbus_init_mmio(dev, PL330_IOMEM_SIZE, iomem);
    s->timer = qemu_new_timer(vm_clock, pl330_exec_cycle_timer, s);

    s->chan_num = ((s->cfg[0] >> 4) & 7) + 1;
    s->chan = qemu_mallocz(sizeof(pl330_chan_state) * s->chan_num);
    for (i = 0; i < s->chan_num; i++) {
        s->chan[i].parent = s;
        s->chan[i].tag = (uint8_t)i;
    }
    s->manager.parent = s;
    s->manager.tag = s->chan_num;
    s->manager.ns = (s->cfg[0] >> 2) & 1;
    s->manager.is_manager = 1;
    if (s->cfg[0] & 1) {
        s->periph_num = ((s->cfg[0] >> 12) & 0x1f) + 1;
    } else {
        s->periph_num = 0;
    }
    s->event_num = ((s->cfg[0] >> 17) & 0x1f) + 1;

    s->irq = qemu_mallocz(sizeof(qemu_irq) * s->event_num);
    for (i = 0; i < s->event_num; i++)
        sysbus_init_irq(dev, &s->irq[i]);

    qdev_init_gpio_in(&dev->qdev, pl330_dma_stop_irq, PL330_PERIPH_NUM);

    pl330_queue_init(&s->read_queue, ((s->cfg[5] >> 16) & 0xf) + 1, s->chan_num);
    pl330_queue_init(&s->write_queue, ((s->cfg[5] >> 8) & 0xf) + 1, s->chan_num);
    pl330_fifo_init(&s->fifo, ((s->cfg[5] >> 20) & 0x1ff) + 1);
    pl330_reset(&s->busdev.qdev);

    return 0;
}

static SysBusDeviceInfo pl330_info = {
    .init = pl330_init1,
    .qdev.name  = "pl330",
    .qdev.size  = sizeof(pl330_state),
    .qdev.reset = pl330_reset,
    .qdev.props = (Property[]) {
        DEFINE_PROP_HEX32("cfg0", pl330_state, cfg[0], 0),
        DEFINE_PROP_HEX32("cfg1", pl330_state, cfg[1], 0),
        DEFINE_PROP_HEX32("cfg2", pl330_state, cfg[2], 0),
        DEFINE_PROP_HEX32("cfg3", pl330_state, cfg[3], 0),
        DEFINE_PROP_HEX32("cfg4", pl330_state, cfg[4], 0),
        DEFINE_PROP_HEX32("cfg5", pl330_state, cfg[5], 0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void pl330_register_devices(void)
{
    sysbus_register_withprop(&pl330_info);
}

device_init(pl330_register_devices)
