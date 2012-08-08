/*
 * S5PC1XX OneDRAM controller.
 *
 * Contributed by Kefeng Li <li.kefeng@samsung.com>
 */

#include "s5pc1xx_onedram.h"

#define TICK_COUNTDOWN  50

uint32_t sem_retry = 50;


/* Command handler */
static int onedram_req_active_handler(S5pc1xxOneDRAMState *s)
{
    uint16_t cmd = INT_COMMAND(INT_MASK_CMD_RES_ACTIVE);

    onedram_send_cmd_to_pda(s, cmd);
    return COMMAND_SUCCESS;
}

static int onedram_smp_req_handler(S5pc1xxOneDRAMState *s)
{
    uint16_t cmd;

    onedram_put_authority(s);

    cmd = INT_COMMAND(INT_MASK_CMD_SMP_REP);
    onedram_send_cmd_to_pda(s, cmd);

    return COMMAND_SUCCESS;
}


/* timer for waiting the semaphore for sem_retry times try */
static void onedram_wait_semaphore(void *opaque)
{
    S5pc1xxOneDRAMState *s = (S5pc1xxOneDRAMState *)opaque;
    int64_t timeout;

    if(sem_retry <= 0) {
        fprintf(stderr, "time out to wait semaphore from AP\n");
        qemu_del_timer(s->sem_timer);
        sem_retry = 100;
    } else if(onedram_read_sem(s)) {
        sem_retry--;
        timeout = get_ticks_per_sec();
        qemu_mod_timer(s->sem_timer, qemu_get_clock(vm_clock) + TICK_COUNTDOWN);
    } else {
        sem_retry = 100;
        qemu_del_timer(s->sem_timer);
        onedram_fmt_send_cmd(s);
    }
}

/* try to see if we have semaphore for sending, if not, request it from AP */
static int onedram_fmt_try_send_cmd(S5pc1xxOneDRAMState *s)
{
    if(onedram_read_sem(s)) {
        fprintf(stderr, "onedram_fmt_try_send_cmd - can't get anthority\n");
        qemu_mod_timer(s->sem_timer, qemu_get_clock(vm_clock) + TICK_COUNTDOWN);
    } else {
        onedram_fmt_send_cmd(s);
    }
    return 1;
}

/* the real sending function of fmt */
static int onedram_fmt_send_cmd(S5pc1xxOneDRAMState *s)
{
    int psrc = -1;
    uint32_t len;
    int ret = 0;

    onedram_disable_interrupt(s);
    onedram_disable_write(s);

    do {
        psrc = onedram_read_fmt(s, &len);

        if (psrc < 0) {
            ret = -1;
            break;
        }
        if (len == 0) { /* read done */
            ret = 0;
            break;
        }
        if (!onedram_insert_socket(s, psrc, len))
            break;
    } while (1);

    onedram_socket_push(s);

    onedram_enable_interrupt(s);
    onedram_enable_write(s);
    return ret;
}


static void onedram_data_handler_fmt_autonomous(S5pc1xxOneDRAMState *s)
{
    uint16_t non_cmd = 0;
    uint32_t in_head = 0, in_tail = 0;

    if (onedram_can_access_shm(s)) {
        in_head = onedram_read_inhead(s);
        in_tail = onedram_read_intail(s);

        if (in_head != in_tail) {
            non_cmd |= INT_MASK_SEND_FMT;
            fprintf(stderr, "formated partition has head-tail mis-match\n");
        }
    } else {
        fprintf(stderr,
                "onedram_data_handler_fmt_autonomous - can't access shm\n");
    }

    if (non_cmd & INT_MASK_SEND_FMT)
        onedram_fmt_try_send_cmd(s);
}

/*static*/ void onedram_command_handler(S5pc1xxOneDRAMState *s,
                                        uint32_t data)
{
    uint8_t cmd = (uint8_t) (data & 0xff);

    onedram_data_handler_fmt_autonomous(s);

    switch (cmd) {
    case INT_MASK_CMD_NONE:
        return;
    case INT_MASK_CMD_REQ_ACTIVE:
        onedram_req_active_handler(s);
        return;
    case INT_MASK_CMD_RES_ACTIVE:
        return;
    case INT_MASK_CMD_INIT_START:
        return;
    case INT_MASK_CMD_INIT_END:
        return;
    case INT_MASK_CMD_ERR_DISPLAY:
        return;
    case INT_MASK_CMD_PHONE_START:
        return;
    case INT_MASK_CMD_REQ_TIME_SYNC:
        return;
    case INT_MASK_CMD_PHONE_DEEP_SLEEP:
        return;
    case INT_MASK_CMD_NV_REBUILDING:
        return;
    case INT_MASK_CMD_EMER_DOWN:
        return;
    case INT_MASK_CMD_SMP_REQ:
        onedram_smp_req_handler(s);
        return;
    case INT_MASK_CMD_SMP_REP:
        return;
    default:
        fprintf(stderr, "command_handler: Unknown command.. %x\n", cmd);
        return;
    }
}

/*static*/ void onedram_data_handler(S5pc1xxOneDRAMState *s,
                                     uint16_t non_cmd)
{
    if (non_cmd & INT_MASK_SEND_FMT)
        onedram_fmt_try_send_cmd(s);
}

/* Shared Memory R/W */
static uint32_t onedram_can_access_shm(S5pc1xxOneDRAMState *s)
{
    return !(onedram_io_readl(s, ONEDRAM_SEM));
}

static int onedram_read_shm(S5pc1xxOneDRAMState *s, uint8_t *buf,
                            uint32_t offset, uint32_t size)
{
    uint8_t *src_base;
    target_phys_addr_t phy_base, src_len;
    phy_base = ONEDRAM_SHARED_BASE + offset;
    src_len = size;

    if (!onedram_can_access_shm(s)) {
        fprintf(stderr,
                "onedram_read_shm : can't access to shm\n");
        return 0;
    }

    if (size > (ONEDRAM_SHARED_SIZE - offset)){
        fprintf(stderr,
                "onedram_read_shm : size exceed the maximum\n");
        return 0;
    }

    src_base = cpu_physical_memory_map(phy_base, &src_len, 0);

    if (!src_base) {
        fprintf(stderr,
                "onedram_read_shm : src_base is NULL\n");
        return 0;
    }

    memcpy(buf, src_base, src_len);

    cpu_physical_memory_unmap(src_base, src_len, 0, src_len);

    return 1;
}

static int onedram_write_shm(S5pc1xxOneDRAMState *s,
                             const uint8_t *buf, uint32_t offset,
                             uint32_t size)
{
    uint8_t *src_base;
    target_phys_addr_t phy_base, src_len;
    phy_base = ONEDRAM_SHARED_BASE + offset;
    src_len = size;

    if (!onedram_can_access_shm(s)) {
        fprintf(stderr,
                "onedram_write_shm : can't access to fmt\n");
        return 0;
    }

    if (size > ONEDRAM_IN_FMT_SIZE){
        fprintf(stderr,
                "onedram_write_shm : size exceeds the maximum\n");
        return 0;
    }

    src_base = cpu_physical_memory_map(phy_base, &src_len, 1);

    if (!src_base) {
        fprintf(stderr,
                "onedram_write_shm : src_base is NULL\n");
        return 0;
    }

    memcpy(src_base, buf, size);

    cpu_physical_memory_unmap(src_base, src_len, 1, src_len);

    return 1;
}

/* Formatted Shared Memory Operation */
/*static*/ uint32_t onedram_read_outhead(S5pc1xxOneDRAMState *s)
{
    uint32_t head = 0;

    if (!s->fmt_info) {
        fprintf(stderr, "err\n");
    } else {
        onedram_read_shm(s, (uint8_t *)&head,
                         s->fmt_info->out_head_addr,
                         s->fmt_info->ptr_size);
    }
    return head;
}

/*static*/ uint32_t onedram_read_inhead(S5pc1xxOneDRAMState *s)
{
    uint32_t head = 0;

    if (!s->fmt_info) {
        fprintf(stderr, "err\n");
    } else {
        onedram_read_shm(s, (uint8_t *)&head,
                         s->fmt_info->in_head_addr,
                         s->fmt_info->ptr_size);
    }
    return head;
}

/*static*/ uint32_t onedram_read_outtail(S5pc1xxOneDRAMState *s)
{
    uint32_t tail = 0;

    if (!s->fmt_info) {
        fprintf(stderr, "err\n");
    } else {
        onedram_read_shm(s, (uint8_t *)&tail,
                         s->fmt_info->out_tail_addr,
                         s->fmt_info->ptr_size);
    }
    return tail;
}

/*static*/ uint32_t onedram_read_intail(S5pc1xxOneDRAMState *s)
{
    uint32_t tail = 0;

    if (!s->fmt_info) {
        fprintf(stderr, "err\n");
    } else {
        onedram_read_shm(s, (uint8_t *)&tail,
                         s->fmt_info->in_tail_addr,
                         s->fmt_info->ptr_size);
    }
    return tail;
}

/*static*/ uint32_t onedram_write_outhead(S5pc1xxOneDRAMState *s,
                                          uint32_t head)
{
    if (!s->fmt_info) {
        fprintf(stderr, "err\n");
    } else {
        onedram_write_shm(s, (uint8_t *)&head,
                          s->fmt_info->out_head_addr,
                          s->fmt_info->ptr_size);
    }
    return head;
}

/*static*/ uint32_t onedram_write_inhead(S5pc1xxOneDRAMState *s,
                                         uint32_t head)
{
    if (!s->fmt_info) {
        fprintf(stderr, "err\n");
    } else {
        onedram_write_shm(s, (uint8_t *)&head,
                          s->fmt_info->in_head_addr,
                          s->fmt_info->ptr_size);
    }
    return head;
}

/*static*/ uint32_t onedram_write_outtail(S5pc1xxOneDRAMState *s,
                                          uint32_t tail)
{
    if (!s->fmt_info) {
        fprintf(stderr, "err\n");
    } else {
        onedram_write_shm(s, (uint8_t *)&tail,
                          s->fmt_info->out_tail_addr,
                          s->fmt_info->ptr_size);
    }
    return tail;
}

/*static*/ uint32_t onedram_write_intail(S5pc1xxOneDRAMState *s,
                                         uint32_t tail)
{
    if (!s->fmt_info) {
        fprintf(stderr, "err\n");
    } else {
        onedram_write_shm(s, (uint8_t *)&tail,
                          s->fmt_info->in_tail_addr,
                          s->fmt_info->ptr_size);
    }
    return tail;
}

/*static*/ int onedram_read_fmt(S5pc1xxOneDRAMState *s,
                                uint32_t *len)
{
    int psrc = -1;
    uint32_t head = 0, tail = 0;
    uint32_t new_tail = 0;

    head = onedram_read_outhead(s);
    tail = onedram_read_outtail(s);

    if (head >= s->fmt_info->out_buff_size ||
        tail >= s->fmt_info->out_buff_size) {
        fprintf(stderr, "head(%d) or tail(%d) is out of bound\n", head, tail);
        goto done_onedram_read_fmt;
    }

    if (head == tail) {
        psrc = /*(uint8_t *)*/(s->fmt_info->out_buff_addr + tail);
        *len = 0;
        goto done_onedram_read_fmt;
    }

    if (head > tail) {
        /* ------- tail ++++++++++++ head -------- */
        psrc = /*(uint8_t *)*/(s->fmt_info->out_buff_addr + tail);
        *len = (head - tail);
    } else {
        /* +++++++ head ------------ tail ++++++++ */
        psrc = /*(uint8_t *)*/(s->fmt_info->out_buff_addr + tail);
        *len = (s->fmt_info->out_buff_size - tail);
    }

    /* new tail */
    new_tail = (uint32_t)((tail + *len) % s->fmt_info->out_buff_size);
    onedram_write_outtail(s, new_tail);

done_onedram_read_fmt:
    return psrc;
}

static int onedram_insert_socket(S5pc1xxOneDRAMState *s,
                                 uint32_t psrc, uint16_t size)
{
    uint8_t *buf;

    if ((s->socket_len + size) >= SOCKET_BUFFER_MAX_SIZE) {
        fprintf(stderr, "the socket buffer is overflow!\n");

        onedram_read_shm(s, (s->socket_buffer + s->socket_len), psrc,
                         SOCKET_BUFFER_MAX_SIZE - s->socket_len);
        s->socket_len = SOCKET_BUFFER_MAX_SIZE;
        return 0;
    } else {
        buf = s->socket_buffer + s->socket_len;
        onedram_read_shm(s, buf, psrc, size);
        s->socket_len += size;
        return 1;
    }
}

void onedram_socket_push(S5pc1xxOneDRAMState *s)
{
    onedram_tcp_write(s, s->socket_buffer, s->socket_len);
    s->socket_len = 0;
}

int onedram_write_fmt(S5pc1xxOneDRAMState *s, const uint8_t *buf,
                      uint32_t len)
{
    int ret = FAIL;
    uint32_t size = 0;
    uint32_t head = 0, tail = 0;
    uint16_t irq_mask = 0;

    if (!s->fmt_info || !buf)
        return FAIL;

    onedram_disable_interrupt(s);
    onedram_disable_write(s);

    head = onedram_read_inhead(s);
    tail = onedram_read_intail(s);

    if (head < tail) {
        /* +++++++++ head ---------- tail ++++++++++ */
        size = tail - head - 1;
        size = (len > size) ? size : len;

        onedram_write_shm(s, (uint8_t *)buf,
                              s->fmt_info->in_buff_addr + head, size);
        ret = size;
    } else if (tail == 0) {
        /* tail +++++++++++++++ head --------------- */
        size = s->fmt_info->in_buff_size - head - 1;
        size = (len > size) ? size : len;

        onedram_write_shm(s, (uint8_t *)buf,
                          s->fmt_info->in_buff_addr + head, size);
        ret = size;
    } else {
        /* ------ tail +++++++++++ head ------------ */
        size = s->fmt_info->in_buff_size - head;
        size = (len > size) ? size : len;

        onedram_write_shm(s, (uint8_t *)buf,
                          s->fmt_info->in_buff_addr + head, size);
        ret = (int)size;

        if ((int)len > ret) {
            size = (len - ret > tail - 1) ? tail - 1 : len - ret;
            buf += ret;
            onedram_write_shm(s, (uint8_t *)buf,
                              s->fmt_info->in_buff_addr, size);
            ret += (int)size;
        }
    }

    /* calculate new head */
    head = (uint32_t)((head + ret) % s->fmt_info->in_buff_size);
    onedram_write_inhead(s, head);

    if (head >= s->fmt_info->in_buff_size ||
        tail >= s->fmt_info->in_buff_size) {
        fprintf(stderr, "head(%d) or tail(%d) is out of bound\n", head, tail);
        goto err_onedram_write_fmt;
    }

    /* send interrupt to the phone, if.. */
    irq_mask = INT_MASK_VALID;

    if (ret > 0)
        irq_mask |= s->fmt_info->mask_send;

    if ((int)len > ret)
        irq_mask |= s->fmt_info->mask_req_ack;

    onedram_put_authority(s);
    onedram_enable_interrupt(s);
    onedram_send_cmd_to_pda(s, irq_mask);
    onedram_enable_write(s);

    return ret;

err_onedram_write_fmt:
    onedram_put_authority(s);
    onedram_enable_interrupt(s);
    onedram_enable_write(s);

    return ret;
}

/* Interrupt Operation */
static uint32_t onedram_irq_cp_raise_32(S5pc1xxOneDRAMState *s)
{
    uint32_t irq_mask;
    int64_t timeout;

    s->irq_onedram_int_cp_pending = 1;
    irq_mask = onedram_io_readl(s, ONEDRAM_MBX_BA);

    switch (irq_mask) {
    case IPC_CP_IMG_LOADED:
        onedram_io_writel(s, ONEDRAM_MBX_AB, IPC_CP_READY);
        timeout = get_ticks_per_sec();
        qemu_mod_timer(s->bootup_timer, qemu_get_clock(vm_clock) + timeout);
        return IRQ_HANDLED;
    case IPC_CP_READY_FOR_LOADING:
        return IRQ_HANDLED;
    default:
        fprintf(stderr, "onedram_irq_cp_raise_32: unknown command\n");
        break;
    }

    return IRQ_HANDLED;
}

static uint32_t onedram_irq_cp_raise_16(S5pc1xxOneDRAMState *s)
{
    uint16_t irq_mask;
    s->irq_onedram_int_cp_pending = 1;

    irq_mask = (uint16_t)onedram_io_readl(s, ONEDRAM_MBX_BA);

    if (!(irq_mask & INT_MASK_VALID)) {
        fprintf(stderr, "Invalid interrupt mask: 0x%04x\n", irq_mask);
        return IRQ_NONE;
    }

    if (irq_mask & INT_MASK_COMMAND) {
        irq_mask &= ~(INT_MASK_VALID | INT_MASK_COMMAND);
        onedram_command_handler(s, irq_mask);
    } else {
        irq_mask &= ~INT_MASK_VALID;
        onedram_data_handler(s, irq_mask);
    }

    return IRQ_HANDLED;
}

void onedram_disable_interrupt(S5pc1xxOneDRAMState *s)
{
    s->onedram_state.interruptable = 0;
}

void onedram_enable_interrupt(S5pc1xxOneDRAMState *s)
{
    s->onedram_state.interruptable = 1;
}

uint16_t onedram_interruptable(S5pc1xxOneDRAMState *s)
{
    return s->onedram_state.interruptable;
}

static void onedram_irq_cp_lower(S5pc1xxOneDRAMState *s)
{
    s->irq_onedram_int_cp_pending = 0;
}

static uint32_t onedram_irq_cp_pending(S5pc1xxOneDRAMState *s)
{
    return s->irq_onedram_int_cp_pending;
}

static void onedram_irq_ap_raise(S5pc1xxOneDRAMState *s)
{
    s->irq_onedram_int_ap_pending = 1;
    qemu_irq_raise(s->irq_onedram_int_ap);
}

static void onedram_irq_ap_lower(S5pc1xxOneDRAMState *s)
{
    s->irq_onedram_int_ap_pending = 0;
    qemu_irq_lower(s->irq_onedram_int_ap);
}

static uint32_t onedram_irq_ap_pending(S5pc1xxOneDRAMState *s)
{
    return s->irq_onedram_int_ap_pending;
}

/* Authority Operation */
unsigned int onedram_read_sem(S5pc1xxOneDRAMState *s)
{
    unsigned int sem;

    sem = onedram_io_readl(s, ONEDRAM_SEM);

    return sem;
}

static void onedram_put_authority(S5pc1xxOneDRAMState *s)
{
    uint32_t sem;
    sem = 0x1;
    onedram_io_writel(s, ONEDRAM_SEM, sem);
}

int onedram_try_get_authority(S5pc1xxOneDRAMState *s)
{
    uint16_t cmd = 0;

    if(!onedram_read_sem(s))
        return TRUE;

    cmd = INT_COMMAND(INT_MASK_CMD_SMP_REQ);
    onedram_send_cmd_to_pda(s, cmd);

    return FALSE;
}

void onedram_disable_write(S5pc1xxOneDRAMState *s)
{
    s->onedram_state.writable = 0;
}

void onedram_enable_write(S5pc1xxOneDRAMState *s)
{
    s->onedram_state.writable = 1;
}

uint16_t onedram_writable(S5pc1xxOneDRAMState *s)
{
    return s->onedram_state.writable;
}

void onedram_send_cmd_to_pda(S5pc1xxOneDRAMState *s, uint16_t val)
{
    uint16_t check_ab;

    check_ab = (uint16_t)onedram_io_readl(s, ONEDRAM_CHECK_AB);
    check_ab &= 0x1;
    if (!check_ab) {
        onedram_io_writel(s, ONEDRAM_MBX_AB, (uint32_t)val);
    } else {
        fprintf(stderr, "mailbox_ab has not been read by AP yet!\n");
    }
}

/* Boot up timer */
static void onedram_bootup(void *opaque)
{
    uint16_t cmd;

    S5pc1xxOneDRAMState *s = (S5pc1xxOneDRAMState *)opaque;
    if(!s->vmodem_bootup) {
        onedram_io_writel(s, ONEDRAM_MBX_AB, IPC_CP_READY_FOR_LOADING);
        s->vmodem_bootup = 1;

    } else {
        /* Init the in/out head/tail */
        onedram_write_inhead(s, 0);
        onedram_write_intail(s, 0);
        onedram_write_outhead(s, 0);
        onedram_write_outtail(s, 0);
        /* put the authority to AP to let it access to shared memory */
        onedram_put_authority(s);
        cmd = INT_COMMAND(INT_MASK_CMD_PHONE_START|CP_CHIP_INFINEON);
        onedram_send_cmd_to_pda(s, cmd);
        qemu_del_timer(s->bootup_timer);
    }
}

/* Register Modem */
static void onedram_register_modem(S5pc1xxOneDRAMState *s,
                                   ModemPlatformData *mp)
{
    /* fmt info */
    s->fmt_info->in_head_addr = mp->in_fmt_base;
    s->fmt_info->in_tail_addr = mp->in_fmt_base + mp->ptr_fmt_size;
    s->fmt_info->in_buff_addr =
                FMT_IN_BUF_PTR/*mp->in_fmt_base + (mp->ptr_fmt_size << 1)*/;
    s->fmt_info->in_buff_size = mp->in_fmt_size;
    s->fmt_info->out_head_addr = mp->out_fmt_base;
    s->fmt_info->out_tail_addr = mp->out_fmt_base + mp->ptr_fmt_size;
    s->fmt_info->out_buff_addr =
                FMT_OUT_BUF_PTR/*mp->out_fmt_base + (mp->ptr_fmt_size << 1)*/;
    s->fmt_info->out_buff_size = mp->out_fmt_size;
    s->fmt_info->mask_req_ack = INT_MASK_REQ_ACK_FMT;
    s->fmt_info->mask_res_ack = INT_MASK_RES_ACK_FMT;
    s->fmt_info->mask_send = INT_MASK_SEND_FMT;
    s->fmt_info->ptr_size = mp->ptr_fmt_size;
}

/* onedram IO R/W */
static uint32_t onedram_io_readb(void *opaque,
                                 target_phys_addr_t offset)
{
    S5pc1xxOneDRAMState *s = (S5pc1xxOneDRAMState *)opaque;

    switch (offset) {
    case 0x00:
        return (s->sem) & 0x000000FF;
    case 0x01:
        return ((s->sem >> 8 ) & 0x000000FF);
    case 0x20:
        return (s->mbx_ab) & 0x000000FF;
    case 0x21:
        /* If interrupt is pending, once after AP reads the mailbox_ab,
         * we should clear the pending */
        if (onedram_irq_ap_pending(s))
            onedram_irq_ap_lower(s);
        /* set check_ab to 0 means the mailbox_ab has been read by AP */
        s->check_ab = 0;
        return ((s->mbx_ab >> 8) & 0x000000FF);
    /* when modem is booting, the AP will read the high two bytes of mail_box
     * (only for new onedram driver) */
    case 0x22:
        return ((s->mbx_ab >> 16) & 0x000000FF);
    case 0x23:
        /* If interrupt is pending, once after AP reads the mailbox_ab,
         * we should clear the pending */
        if (onedram_irq_ap_pending(s))
            onedram_irq_ap_lower(s);
        /* set check_ab to 0 means the mailbox_ab has been read by AP */
        s->check_ab = 0;
        return ((s->mbx_ab >> 24) & 0x000000FF);
    case 0x40:
        return (s->mbx_ba) & 0x000000FF;
    case 0x41:
        if (onedram_irq_cp_pending(s))
            onedram_irq_cp_lower(s);
        /* set check_ba to 0 means the mailbox_ab has been read by CP */
        s->check_ba = 0;
        return ((s->mbx_ba >> 8 ) & 0x000000FF);
    case 0xA0:
        return (s->check_ab) & 0x000000FF;
    case 0xA1:
        return ((s->check_ab >> 8 ) & 0x000000FF);
    case 0xC0:
        return ((s->check_ba) & 0x000000FF);
    case 0xC1:
        return ((s->check_ba >> 8 ) & 0x000000FF);
    default:
        hw_error("onedram: bad read offset " TARGET_FMT_plx "\n",
                 offset);
    }
}

static uint32_t onedram_io_readl(void *opaque,
                                 target_phys_addr_t offset)
{
    S5pc1xxOneDRAMState *s = (S5pc1xxOneDRAMState *)opaque;

    switch (offset) {
    case 0x00:
        return s->sem;
    case 0x20:
        /* If interrupt is pending, once AP reads the mailbox_ab,
         * we should clear the pending */
        if (onedram_irq_ap_pending(s))
            onedram_irq_ap_lower(s);
        /* set check_ab to 0 means the mailbox_ab has been read by AP */
        s->check_ab = 0;
        return s->mbx_ab;
    case 0x40:
        if (onedram_irq_cp_pending(s))
            onedram_irq_cp_lower(s);
        /* set check_ba to 0 means the mailbox_ab has been read by CP */
        s->check_ba = 0;
        return s->mbx_ba;
    case 0xA0:
        return s->check_ab;
    case 0xC0:
        return s->check_ba;
    default:
        hw_error("onedram: bad read offset " TARGET_FMT_plx "\n",
                 offset);
    }
}

static void onedram_io_writeb(void *opaque, target_phys_addr_t offset,
                              uint32_t val)
{
    S5pc1xxOneDRAMState *s = (S5pc1xxOneDRAMState *)opaque;

    switch (offset) {
    case 0x00:
        s->sem = val;
        break;
    case 0x20:
        (s->mbx_ab) = (val & 0x000000FF);
        break;
    case 0x21:
        (s->mbx_ab) |= ((val << 8) & 0x0000FF00) ;
        /* set check_ab to 1 means the mailbox_ab is waiting for AP to read */
        s->check_ab = 1;
        /* If interrupt is not pending, raise the interrupt to AP */
        if (!onedram_irq_ap_pending(s))
            onedram_irq_ap_raise(s);
        break;
    case 0x40:
        (s->mbx_ba) = (val & 0x000000FF);
        break;
    case 0x41:
        (s->mbx_ba) |= ((val << 8) & 0x0000FF00);
        /* set check_ab to 1 means the mailbox_ba is waiting for CP to read */
        s->check_ba = 1;
        /* raise an interrupt to inform CP that there is a message has come */
        if (onedram_interruptable(s))
            onedram_irq_cp_raise_16(s);
        break;
    case 0xA0:
        (s->check_ab) = (val & 0x000000FF);
        break;
    case 0xA1:
        (s->check_ab) |= ((val << 8) & 0x0000FF00);
        break;
    case 0xC0:
        (s->check_ba) = (val & 0x000000FF);
        break;
    case 0xC1:
        (s->check_ba) |= ((val << 8) & 0x0000FF00) ;
        break;
    default:
        hw_error("onedram: bad write offset " TARGET_FMT_plx "\n",
                 offset);
    }
}

static void onedram_io_writel(void *opaque, target_phys_addr_t offset,
                              uint32_t val)
{
    S5pc1xxOneDRAMState *s = (S5pc1xxOneDRAMState *)opaque;

    switch (offset) {
    case 0x00:
        s->sem = val;
        break;
    case 0x20:
        s->mbx_ab = val;
        s->check_ab = 1;
        if (!onedram_irq_ap_pending(s))
            onedram_irq_ap_raise(s);
        break;
    case 0x40:
        s->mbx_ba = val;
        s->check_ba = 1;
        if (onedram_interruptable(s))
            onedram_irq_cp_raise_32(s);
        break;
    case 0xA0:
        s->check_ab = val;
        break;
    case 0xC0:
        s->check_ba = val;
        break;
    default:
        hw_error("onedram: bad write offset " TARGET_FMT_plx "\n",
                 offset);
    }
}

/*
 *  In mailbox there are 2 bytes CMD and 4 bytes message,
 *  but 4 bytes only for booting the phone.
 *  in other cases, only use 2 bytes read
 */
static CPUReadMemoryFunc * const onedram_mm_read[] = {
    onedram_io_readb,
    onedram_io_readl,
    onedram_io_readl
};

static CPUWriteMemoryFunc * const onedram_mm_write[] = {
    onedram_io_writeb,
    onedram_io_writel,
    onedram_io_writel
};

/* Socket for Vmodem operation*/
static int onedram_tcp_can_read(void *opaque)
{
    return MAX_BUFFER;
}

static void onedram_tcp_read(void *opaque, const uint8_t *buf,
                             int size)
{
    S5pc1xxOneDRAMState *s = (S5pc1xxOneDRAMState *)opaque;
    uint32_t send_cmd;
    int64_t timeout;
    /* In booting stage, we need to set up the connection to
     * Vmodem thru Socket */
    if (!s->vmodem_connected) {
        if (((uint32_t *)buf)[0] == IPC_CP_CONNECT_APP) {
            send_cmd = IPC_AP_CONNECT_ACK;
            onedram_tcp_write(s, (uint8_t *)&send_cmd, CONNECT_LENGTH);
            s->vmodem_connected = 1;
            /* put the anthority to AP,
             * because AP will try to load the modem image for CP */
            onedram_put_authority(s);
            /* before here, the PSI has been loaded by CP already,
             * in the new onedram driver,
             * we have to send IPC_CP_READY_FOR_LOADING to AP
             * rather than waiting for to be read from AP */
            timeout = get_ticks_per_sec();
            qemu_mod_timer(s->bootup_timer,
                           qemu_get_clock(vm_clock) + timeout/10);
        }
    } else {
        /* The connection to Vmodem has been set up,
         * so now we only exchange IPC */
        if (onedram_writable(s)) {
            //onedram_prepare_write_fmt(s, buf, size);
            onedram_write_fmt(s, buf, size);
        } else {
            return;
        }
    }
}

void onedram_tcp_write(void *opaque, const uint8_t *buf, uint32_t size)
{
    S5pc1xxOneDRAMState *s = (S5pc1xxOneDRAMState *)opaque;

    s->socket->chr_write(s->socket, buf, size);
}

static void onedram_tcp_event(void *opaque, int event)
{
    /* not implemented yet */
}

static void onedram_tcp_init(void *opaque)
{
    S5pc1xxOneDRAMState *s = (S5pc1xxOneDRAMState *)opaque;

    /* open a socket to communicate with vmodem */
    const char *p = "tcp:localhost:7777,server,nowait";
    s->socket= qemu_chr_open("onedram_socket", p, NULL);

    if (!s->socket)
        hw_error("onedram: could not open onedram socket\n");

    qemu_chr_add_handlers(s->socket, onedram_tcp_can_read,
                          onedram_tcp_read, onedram_tcp_event,
                          s);
}

DeviceState *s5pc1xx_onedram_init(const char *name, target_phys_addr_t base,
                                  qemu_irq irq_ap)
{
    DeviceState *dev = qdev_create(NULL, name);
    ram_addr_t onedram_shared, onedram_ap;

    qdev_init_nofail(dev);
    onedram_ap = qemu_ram_alloc(ONEDRAM_AP_SIZE);
    cpu_register_physical_memory(base, ONEDRAM_AP_SIZE,
                                 onedram_ap | IO_MEM_RAM);
    onedram_shared = qemu_ram_alloc(ONEDRAM_SHARED_SIZE);
    cpu_register_physical_memory(base + ONEDRAM_AP_SIZE, ONEDRAM_SHARED_SIZE,
                                 onedram_shared | IO_MEM_RAM);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0,
                    base + ONEDRAM_AP_SIZE + ONEDRAM_SFR);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq_ap);
    return dev;
}

static int s5pc1xx_onedram_init1(SysBusDevice *dev, ModemPlatformData *mp)
{
    S5pc1xxOneDRAMState *s = FROM_SYSBUS(S5pc1xxOneDRAMState, dev);
    int onedram_io;

    sysbus_init_irq(dev, &s->irq_onedram_int_ap);
    onedram_io = cpu_register_io_memory(onedram_mm_read,
                                        onedram_mm_write, s);
    sysbus_init_mmio(dev, ONEDRAM_REGISTER_SIZE, onedram_io);

    s->sem = 0;
    s->mbx_ab = 0;
    s->mbx_ba = 0;
    s->check_ab = 0;
    s->check_ba = 0;

    s->irq_onedram_int_ap_pending = 0;
    s->irq_onedram_int_cp_pending = 0;

    s->vmodem_connected = 0;
    s->vmodem_bootup = 0;
    s->fmt_info = (ModemInfo *)qemu_mallocz(sizeof(ModemInfo));

    s->socket_buffer = (uint8_t *)qemu_mallocz(SOCKET_BUFFER_MAX_SIZE);
    s->socket_len = 0;

    s->onedram_state.waiting_authority = FALSE;
    s->onedram_state.non_cmd = INT_MASK_CMD_NONE;
    s->onedram_state.send_size = 0;
    s->onedram_state.waiting_sem_rep = 0;
    s->onedram_state.send_buf = NULL;
    s->onedram_state.interruptable = 1;
    s->onedram_state.writable = 1;

    onedram_register_modem(s, mp);
    onedram_tcp_init(s);

    s->bootup_timer = qemu_new_timer(vm_clock, onedram_bootup, s);
    s->sem_timer = qemu_new_timer(vm_clock, onedram_wait_semaphore, s);

    return 0;
}

static int s5pc1xx_onedram_aquila_xmm_init(SysBusDevice *dev)
{
    return s5pc1xx_onedram_init1(dev, &aquila_xmm_modem_data);
}
static void s5pc1xx_onedram_register_devices(void)
{
    sysbus_register_dev("s5pc1xx,onedram,aquila,xmm",
                        sizeof(S5pc1xxOneDRAMState),
                        s5pc1xx_onedram_aquila_xmm_init);
}

device_init(s5pc1xx_onedram_register_devices)
