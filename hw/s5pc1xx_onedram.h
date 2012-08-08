#ifndef __S5PC1XX_ONEDRAM_H__
#define __S5PC1XX_ONEDRAM_H__

#include "sysbus.h"
#include "console.h"
#include "s5pc1xx.h"
#include "qemu-timer.h"

#define TRUE     1
#define FALSE    0

#define SUCCESS  1
#define FAIL    -1

#define SZ_4K                           0x00001000
#define SZ_1K                           0x00000400
#define SZ_16M                          0x01000000

/* SDRAM */
#define S5PC11X_PA_SDRAM                (0x30000000)

/*
 * OneDRAM memory map specific definitions
 */
#define ONEDRAM_BASE                    0x30000000
#define ONEDRAM_AP_BASE                 (ONEDRAM_BASE)
#define ONEDRAM_AP_SIZE                 0x05000000      /* 80MB */
#define ONEDRAM_SHARED_BASE             (ONEDRAM_BASE + ONEDRAM_AP_SIZE)
#define ONEDRAM_SHARED_SIZE             0x00500000      /* 0x400000*/
                                                        /* 0xFFF800*/
                                                        /* 16382KB */
#define ONEDRAM_REGISTER_SIZE           0x800

/*
 * OneDRAM Interrupt related definitions
 */
#define ONEDRAM_SFR                     0xFFF800
#define ONEDRAM_IO_BASE                 (ONEDRAM_SHARED_BASE + ONEDRAM_SFR)

/* A is Modem, B is Application Processor */
#define ONEDRAM_SEM                     0x00            /* semaphore */
#define ONEDRAM_MBX_AB                  0x20            /* mailbox AtoB */
#define ONEDRAM_MBX_BA                  0x40            /* mailbox BtoA */
#define ONEDRAM_CHECK_AB                0xa0            /* check AtoB */
#define ONEDRAM_CHECK_BA                0xc0            /* check BtoA */

#define IPC_MAGIC_PTR                   0x0
#define IPC_ACCESS_PTR                  0x4

#define ONEDRAM_START_ADDRESS           0
#define ONEDRAM_MAGIC_CODE_ADDRESS      (ONEDRAM_START_ADDRESS + IPC_MAGIC_PTR)
#define ONEDRAM_ACCESS_ENABLE_ADDRESS   (ONEDRAM_START_ADDRESS + IPC_ACCESS_PTR)

#define IPC_PART_PTR_SIZE               0x4

/* formatted region */
#define FMT_OUT_HEAD_PTR                0x10
#define FMT_OUT_TAIL_PTR                (FMT_OUT_HEAD_PTR + IPC_PART_PTR_SIZE)
#define FMT_IN_HEAD_PTR                 0x18
#define FMT_IN_TAIL_PTR                 (FMT_IN_HEAD_PTR + IPC_PART_PTR_SIZE)

/*raw region */
#define RAW_OUT_HEAD_PTR                0x20
#define RAW_OUT_TAIL_PTR                (RAW_OUT_HEAD_PTR + IPC_PART_PTR_SIZE)
#define RAW_IN_HEAD_PTR                 0x28
#define RAW_IN_TAIL_PTR                 (RAW_IN_HEAD_PTR + IPC_PART_PTR_SIZE)

/* remote file system region */
#define RFS_OUT_HEAD_PTR                0x30
#define RFS_OUT_TAIL_PTR                (RFS_OUT_HEAD_PTR + IPC_PART_PTR_SIZE)
#define RFS_IN_HEAD_PTR                 0x38
#define RFS_IN_TAIL_PTR                 (RFS_IN_HEAD_PTR + IPC_PART_PTR_SIZE)

#define CP_FATAL_DISP_SIZE              0xA0
#define CP_FATAL_DISP_PTR               0x1000

#define FMT_BUF_SIZE                    0x1000
#define FMT_OUT_BUF_PTR                 0xFE000
#define FMT_IN_BUF_PTR                  0xFF000

#define RAW_BUF_SIZE                    0x100000
#define RAW_OUT_BUF_PTR                 0x100000
#define RAW_IN_BUF_PTR                  0x200000

#define RFS_BUF_SIZE                    0x100000
#define RFS_OUT_BUF_PTR                 0x300000
#define RFS_IN_BUF_PTR                  0x400000

#define IPC_PART_SIZE                   0x500000

#define ONEDRAM_OUT_FMT_BASE            FMT_OUT_BUF_PTR
#define ONEDRAM_OUT_FMT_SIZE            FMT_BUF_SIZE

#define ONEDRAM_OUT_RAW_BASE            RAW_OUT_BUF_PTR
#define ONEDRAM_OUT_RAW_SIZE            RAW_BUF_SIZE

#define ONEDRAM_OUT_RFS_BASE            RFS_OUT_BUF_PTR
#define ONEDRAM_OUT_RFS_SIZE            RFS_BUF_SIZE

#define ONEDRAM_IN_FMT_BASE             FMT_IN_BUF_PTR
#define ONEDRAM_IN_FMT_SIZE             FMT_BUF_SIZE

#define ONEDRAM_IN_RAW_BASE             RAW_IN_BUF_PTR
#define ONEDRAM_IN_RAW_SIZE             RAW_BUF_SIZE

#define ONEDRAM_IN_RFS_BASE             RFS_IN_BUF_PTR
#define ONEDRAM_IN_RFS_SIZE             RFS_BUF_SIZE


#define MAGIC_CODE                      0x00aa
#define ACCESS_ENABLE                   0x0001

#define IRQ_ONEDRAM_INT_AP              11
#define IRQ_PHONE_ACTIVE                15

#define MAX_BUFFER                      1024

#define CONNECT_LENGTH                  4

#define TCP_CMD_LENGTH                  2


/* AP IPC define */
#define IPC_AP_CONNECT_ACK              0xABCD1234
#define IPC_AP_SEND_FMT_ACK             0xCDAB1234


/* CP IPC define */
#define IPC_CP_CONNECT_APP              0x1234ABCD
#define IPC_CP_READY_FOR_LOADING        0x12341234
#define IPC_CP_IMG_LOADED               0x45674567
#define IPC_CP_READY                    0xABCDABCD

/*
 * Samsung IPC 4.0 specific definitions
 */
#define INT_MASK_VALID                  0x0080
#define INT_MASK_COMMAND                0x0040
    /* If not command */
    #define INT_MASK_REQ_ACK_RFS        0x0400
    #define INT_MASK_RES_ACK_RFS        0x0200
    #define INT_MASK_SEND_RFS           0x0100
    #define INT_MASK_REQ_ACK_FMT        0x0020
    #define INT_MASK_REQ_ACK_RAW        0x0010
    #define INT_MASK_RES_ACK_FMT        0x0008
    #define INT_MASK_RES_ACK_RAW        0x0004
    #define INT_MASK_SEND_FMT           0x0002
    #define INT_MASK_SEND_RAW           0x0001

#define INT_MASK_CMD_NONE               0x0000
#define INT_MASK_CMD_INIT_START         0x0001
#define INT_MASK_CMD_INIT_END           0x0002
    /* CMD_INIT_END extended bit */
    /* CP boot state */
    #define REQ_ONLINE_BOOT             0x0000
    #define REQ_AIRPLANE_BOOT           0x1000
    /* AP OS type */
    #define AP_OS_ANDROID               0x0100
    #define AP_OS_WINMOBILE             0x0200
    #define AP_OS_LINUX                 0x0300
    #define AP_OS_SYMBIAN               0x0400
#define INT_MASK_CMD_REQ_ACTIVE         0x0003
#define INT_MASK_CMD_RES_ACTIVE         0x0004
#define INT_MASK_CMD_REQ_TIME_SYNC      0x0005
#define INT_MASK_CMD_PHONE_START        0x0008
    /* CMD_PHONE_START extended bit */
    /* CP chip type */
    #define CP_CHIP_QUALCOMM            0x0100
    #define CP_CHIP_INFINEON            0x0200
    #define CP_CHIP_BROADCOM            0x0300
#define INT_MASK_CMD_ERR_DISPLAY        0x0009
#define INT_MASK_CMD_PHONE_DEEP_SLEEP   0x000A
#define INT_MASK_CMD_NV_REBUILDING      0x000B
#define INT_MASK_CMD_EMER_DOWN          0x000C
#define INT_MASK_CMD_SMP_REQ            0x000D
#define INT_MASK_CMD_SMP_REP            0x000E
#define INT_MASK_CMD_MAX                0x000F

#define INT_COMMAND(x)                  (INT_MASK_VALID | INT_MASK_COMMAND | x)
#define INT_NON_COMMAND(x)              (INT_MASK_VALID | x)

#define BIT_INT_MASK_CMD_REQ_ACTIVE       0x001
#define BIT_INT_MASK_CMD_ERR_DISPLAY      0x002
#define BIT_INT_MASK_CMD_PHONE_START      0x004
#define BIT_INT_MASK_CMD_REQ_TIME_SYNC    0x008
#define BIT_INT_MASK_CMD_PHONE_DEEP_SLEEP 0x010
#define BIT_INT_MASK_CMD_NV_REBUILDING    0x020
#define BIT_INT_MASK_CMD_EMER_DOWN        0x040
#define BIT_INT_MASK_CMD_SMP_REQ          0x080
#define BIT_INT_MASK_CMD_SMP_REP          0x100
#define BIT_MAX                           0x200

#define FMT_SERVICE     0
#define RAW_SERVICE     1
#define RFS_SERVICE     2

/* IRQ definition */
#define IRQ_NONE                        (0)
#define IRQ_HANDLED                     (1)
#define IRQ_RETVAL(x)                   ((x) != 0)

/*
 * Modem deivce partitions.
 */
#define FMT_INDEX                       0
#define RAW_INDEX                       1
#define RFS_INDEX                       2
#define MAX_INDEX                       3

#define MESG_PHONE_OFF                  1
#define MESG_PHONE_RESET                2

#define RETRY                           50
#define TIME_RESOLUTION                 10

#define COMMAND_SUCCESS                 0x00
#define COMMAND_GET_AUTHORITY_FAIL      0x01
#define COMMAND_FAIL                    0x02

#define CONFIG_MODEM_CORE_FMT_SERVICE

typedef struct ModemServiceOps {
    int (*send_cmd_handler)(uint16_t);
    void (*resp_cmd_handler)(void);
} ModemServiceOps;

typedef struct ModemInfo {
    /* DPRAM memory addresses */
    uint32_t in_head_addr;
    uint32_t in_tail_addr;
    uint32_t in_buff_addr;
    uint32_t in_buff_size;

    uint32_t out_head_addr;
    uint32_t out_tail_addr;
    uint32_t out_buff_addr;
    uint32_t out_buff_size;

    int ptr_size;

    uint16_t mask_req_ack;
    uint16_t mask_res_ack;
    uint16_t mask_send;
} ModemInfo;

typedef struct ModemPlatformData {
    const char *name;
    uint32_t booting_type;
    uint32_t irq_onedram_int_ap;
    uint32_t out_fmt_base;
    uint32_t out_fmt_size;

    uint32_t in_fmt_base;
    uint32_t in_fmt_size;

    int ptr_fmt_size;
} ModemPlatformData;

/* Booting type definitions */
#define XMM                             1
#define MSM                             2
#define QSC                             3

#define QEMU_MODEM                      XMM

#define SOCKET_BUFFER_MAX_SIZE          SZ_1K

typedef struct OneDRAMState {
    uint16_t waiting_authority;
    uint16_t waiting_sem_rep;
    uint16_t waiting_check;
    uint16_t non_cmd;
    uint16_t send_cmd;
    uint16_t interruptable;
    uint16_t writable;

    uint8_t  *send_buf;
    uint8_t  send_size;
} OneDRAMState;

typedef struct S5pc1xxOneDRAMState {
    SysBusDevice busdev;

    uint32_t magic_code;
    uint32_t sem;
    uint32_t mbx_ab;
    uint32_t mbx_ba;
    uint32_t check_ab;
    uint32_t check_ba;

    qemu_irq irq_onedram_int_ap;
    uint32_t irq_onedram_int_ap_pending;
    uint32_t irq_onedram_int_cp_pending;

    CharDriverState *socket;
    uint32_t vmodem_connected;
    uint32_t vmodem_bootup;
    QEMUTimer *bootup_timer;
    QEMUTimer *sem_timer;

    /* OneDRAM memory addresses */
    ModemInfo* fmt_info;

    uint8_t *socket_buffer;
    uint32_t socket_len;

    OneDRAMState onedram_state;
} S5pc1xxOneDRAMState;

/* OneDRAM */
static struct ModemPlatformData aquila_xmm_modem_data = {
    .name               = "aquila-XMM6160",
    .booting_type       = XMM,
    .irq_onedram_int_ap = 11,

     /* Memory map */
    .out_fmt_base       = FMT_OUT_HEAD_PTR,
    .out_fmt_size       = FMT_BUF_SIZE,
    .in_fmt_base        = FMT_IN_HEAD_PTR,
    .in_fmt_size        = FMT_BUF_SIZE,
    .ptr_fmt_size       = IPC_PART_PTR_SIZE,

};

/* Command handler */
static int onedram_req_active_handler(S5pc1xxOneDRAMState *s);
static int onedram_smp_req_handler(S5pc1xxOneDRAMState *s);
static int onedram_fmt_try_send_cmd(S5pc1xxOneDRAMState *s);
static int onedram_fmt_send_cmd(S5pc1xxOneDRAMState *s);
static void onedram_data_handler_fmt_autonomous(S5pc1xxOneDRAMState *s);
/*static*/ void onedram_command_handler(S5pc1xxOneDRAMState *s,
                                        uint32_t data);
/*static*/ void onedram_data_handler(S5pc1xxOneDRAMState *s,
                                     uint16_t non_cmd);
static uint32_t onedram_can_access_shm(S5pc1xxOneDRAMState *s);
static int onedram_read_shm(S5pc1xxOneDRAMState *s, uint8_t *buf,
                            uint32_t offset, uint32_t size);
static int onedram_write_shm(S5pc1xxOneDRAMState *s,
                             const uint8_t *buf, uint32_t offset,
                             uint32_t size);
/*static*/ uint32_t onedram_read_outhead(S5pc1xxOneDRAMState *s);
/*static*/ uint32_t onedram_read_inhead(S5pc1xxOneDRAMState *s);
/*static*/ uint32_t onedram_read_outtail(S5pc1xxOneDRAMState *s);
/*static*/ uint32_t onedram_read_intail(S5pc1xxOneDRAMState *s);
/*static*/ uint32_t onedram_write_outhead(S5pc1xxOneDRAMState *s,
                                          uint32_t head);
/*static*/ uint32_t onedram_write_inhead(S5pc1xxOneDRAMState *s,
                                         uint32_t head);
/*static*/ uint32_t onedram_write_outtail(S5pc1xxOneDRAMState *s,
                                          uint32_t tail);
/*static*/ uint32_t onedram_write_intail(S5pc1xxOneDRAMState *s,
                                         uint32_t tail);
/*static*/ int onedram_read_fmt(S5pc1xxOneDRAMState *s,
                                uint32_t *len);
/*static*/ void onedram_read_fmt_wrapup(S5pc1xxOneDRAMState *s,
                                        const uint16_t non_cmd);
static int onedram_insert_socket(S5pc1xxOneDRAMState *s,
                                 uint32_t psrc, uint16_t size);
void onedram_socket_push(S5pc1xxOneDRAMState *s);
int onedram_write_fmt(S5pc1xxOneDRAMState *s, const uint8_t *buf,
                      uint32_t len);
static uint32_t onedram_irq_cp_raise_32(S5pc1xxOneDRAMState *s);
static uint32_t onedram_irq_cp_raise_16(S5pc1xxOneDRAMState *s);
void onedram_disable_interrupt(S5pc1xxOneDRAMState *s);
void onedram_enable_interrupt(S5pc1xxOneDRAMState *s);
uint16_t onedram_interruptable(S5pc1xxOneDRAMState *s);
static void onedram_irq_cp_lower(S5pc1xxOneDRAMState *s);
static uint32_t onedram_irq_cp_pending(S5pc1xxOneDRAMState *s);
static void onedram_irq_ap_raise(S5pc1xxOneDRAMState *s);
static void onedram_irq_ap_lower(S5pc1xxOneDRAMState *s);
static uint32_t onedram_irq_ap_pending(S5pc1xxOneDRAMState *s);
unsigned int onedram_read_sem(S5pc1xxOneDRAMState *s);
static void onedram_put_authority(S5pc1xxOneDRAMState *s);
int onedram_try_get_authority(S5pc1xxOneDRAMState *s);
void onedram_disable_write(S5pc1xxOneDRAMState *s);
void onedram_enable_write(S5pc1xxOneDRAMState *s);
uint16_t onedram_writable(S5pc1xxOneDRAMState *s);
void onedram_send_cmd_to_pda(S5pc1xxOneDRAMState *s, uint16_t val);
static void onedram_bootup(void *opaque);
static void onedram_register_modem(S5pc1xxOneDRAMState *s,
                                   ModemPlatformData *mp);
static uint32_t onedram_io_readb(void *opaque,
                                 target_phys_addr_t offset);
static uint32_t onedram_io_readl(void *opaque,
                                 target_phys_addr_t offset);
static void onedram_io_writeb(void *opaque, target_phys_addr_t offset,
                              uint32_t val);
static void onedram_io_writel(void *opaque, target_phys_addr_t offset,
                              uint32_t val);
static int onedram_tcp_can_read(void *opaque);
static void onedram_tcp_read(void *opaque, const uint8_t *buf,
                             int size);
void onedram_tcp_write(void *opaque, const uint8_t *buf, uint32_t size);
static void onedram_tcp_event(void *opaque, int event);
static void onedram_tcp_init(void *opaque);

#endif
