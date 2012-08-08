/*
 * AT42QT602240 Touchscreen
 *
 * Copyright (c) 2010 Samsung Electronics.
 * Contributed by Alexey Merkulov <steelart@ispras.ru>
 *                Dmitry Zhurikhin <zhur@ispras.ru>
 *                Vladimir Monakhov <vmonakhov@ispras.ru>
 *
 * NB: Only features used in the kernel driver is implemented currently.
 */

#include "console.h"
#include "i2c-addressable.h"

/* Object types */
#define QT602240_DEBUG_DELTAS       2
#define QT602240_DEBUG_REFERENCES   3
#define QT602240_DEBUG_CTERANGE     26
#define QT602240_GEN_MESSAGE        5
#define QT602240_GEN_COMMAND        6
#define QT602240_GEN_POWER          7
#define QT602240_GEN_ACQUIRE        8
#define QT602240_TOUCH_MULTI        9
#define QT602240_TOUCH_KEYARRAY     15
#define QT602240_PROCI_GRIPFACE     20
#define QT602240_PROCG_NOISE        22
#define QT602240_PROCI_ONETOUCH     24
#define QT602240_PROCI_TWOTOUCH     27
#define QT602240_SPT_GPIOPWM        19
#define QT602240_SPT_SELFTEST       25
#define QT602240_SPT_CTECONFIG      28

/* Orient */
#define QT602240_NORMAL             0x0
#define QT602240_DIAGONAL           0x1
#define QT602240_HORIZONTAL_FLIP    0x2
#define QT602240_ROTATED_90_COUNTER 0x3
#define QT602240_VERTICAL_FLIP      0x4
#define QT602240_ROTATED_90         0x5
#define QT602240_ROTATED_180        0x6
#define QT602240_DIAGONAL_COUNTER   0x7

/* Touch status */
#define QT602240_SUPPRESS           (1 << 1)
#define QT602240_AMP                (1 << 2)
#define QT602240_VECTOR             (1 << 3)
#define QT602240_MOVE               (1 << 4)
#define QT602240_RELEASE            (1 << 5)
#define QT602240_PRESS              (1 << 6)
#define QT602240_DETECT             (1 << 7)

/* Message */
#define QT602240_REPORTID           0
#define QT602240_MSG_STATUS         1
#define QT602240_MSG_XPOSMSB        2
#define QT602240_MSG_YPOSMSB        3
#define QT602240_MSG_XYPOSLSB       4
#define QT602240_MSG_TCHAREA        5
#define QT602240_MSG_TCHAMPLITUDE   6
#define QT602240_MSG_TCHVECTOR      7
#define QT602240_CHECKSUM           8

/* Message format */
#define OBJECT_TABLE_MAX_SIZE       16

#define OBJ_ADDR_TYPE               0
#define OBJ_ADDR_START              1
#define OBJ_ADDR_SIZE               3
#define OBJ_ADDR_INSTANCES          4
#define OBJ_ADDR_REPORT_IDS         5
#define OBJ_SIZE                    6

/* Size of message queue */
#define QT602240_MAX_MESSAGE        10

#define QEMUMAXX                    0x7FFF
#define QEMUMAXY                    0x7FFF

#define FAMILY_ID_SIZE              1
#define VARIANT_ID_SIZE             1
#define VERSION_SIZE                1
#define BUILD_SIZE                  1
#define MATRIX_X_SIZE_SIZE          1
#define MATRIX_Y_SIZE_SIZE          1
#define OBJECTS_NUM_SIZE            1
#define OBJECT_TABLE_SIZE           (OBJECT_TABLE_MAX_SIZE * OBJ_SIZE)
#define CHECKSUM_SIZE               1
#define MESSAGE_SIZE                9
#define MULTITOUCH_SIZE             30
#define GENCOMMAND_SIZE             5
#define SPT_CTECONFIG_SIZE          5

#define FAMILY_ID                   0
#define VARIANT_ID                  (FAMILY_ID + FAMILY_ID_SIZE)
#define VERSION                     (VARIANT_ID + VARIANT_ID_SIZE)
#define BUILD                       (VERSION + VERSION_SIZE)
#define MATRIX_X_SIZE               (BUILD + BUILD_SIZE)
#define MATRIX_Y_SIZE               (MATRIX_X_SIZE + MATRIX_X_SIZE_SIZE)
#define OBJECTS_NUM                 (MATRIX_Y_SIZE + MATRIX_Y_SIZE_SIZE)
#define OBJECT_TABLE                (OBJECTS_NUM + OBJECTS_NUM_SIZE)
#define CHECKSUM                    (OBJECT_TABLE + OBJECT_TABLE_SIZE)
#define MESSAGE                     (CHECKSUM + CHECKSUM_SIZE)
#define MULTITOUCH                  (MESSAGE + MESSAGE_SIZE)
#define GENCOMMAND                  (MULTITOUCH + MULTITOUCH_SIZE)
#define SPT_CTECONFIG               (GENCOMMAND + GENCOMMAND_SIZE)
#define TOTAL_SIZE                  (SPT_CTECONFIG + SPT_CTECONFIG_SIZE)

#define MULTITOUCH_ORIENT       9
#define MULTITOUCH_XRANGE_LSB   18
#define MULTITOUCH_XRANGE_MSB   19
#define MULTITOUCH_YRANGE_LSB   20
#define MULTITOUCH_YRANGE_MSB   21

/* This structure closely correspond to the memory map of the real device.
 * We use this property in read\write functions by directly reading\writing
 * data at the offsets provided by the driver.  It is possible due to proper
 * filling of ADDR_START field of each object with this object's structure
 * offset in the next structure and byte-to-byte equivalence of each object
 * structure to that of the real device.  */
typedef struct QT602240State {

    I2CAddressableState i2c_addressable;

    uint8_t regs[TOTAL_SIZE];
#if 0
    uint8_t family_id;

    uint8_t variant_id;

    /* The current major and minor firmware version of the device.
     * The upper nibble contains the major version and the lower nibble
     * contains the minor version */
    uint8_t version;

    uint8_t build;

    uint8_t matrix_x_size;

    uint8_t matrix_y_size;

    /* Number of elements in the Object Table */
    uint8_t objects_num;

    uint8_t object_table[OBJECT_TABLE_MAX_SIZE * OBJ_SIZE];

    uint8_t checksum;

    /* Debug objects are not implemented */


    uint8_t message[9];

    /* Command Processor are not implemented */

    /* Power Configuration are not implemented */

    /* Acquisition Configuration are not implemented */

    uint8_t multitouch[30];

    uint8_t gencommand[5];

    uint8_t spt_cteconfig[5];
#endif

    /* Other objects are not supported */

    int prev_x, prev_y;
    int pressure;
    qemu_irq irq;

    /* Messages are stored in a cyclic buffer */
    int queue_start, queue_end;
    uint8_t queue[QT602240_MAX_MESSAGE][MESSAGE_SIZE];

    /* Boundary reported coordinates */
    uint32_t minx, maxx, miny, maxy, orient;
} QT602240State;

typedef struct QT602240MultitouchMessage {
    uint8_t status;
    uint8_t xposmsb;
    uint8_t yposmsb;
    uint8_t xyposlsb;
    uint8_t tcharea;
    uint8_t tchamplitude;
    uint8_t tchvector;
} QT602240MultitouchMessage;


/* Add one object to the table */
static void qt602240_add_object(QT602240State *s, uint16_t offset,
                                uint8_t size, int type, uint8_t report_ids)
{
    int i;

    for (i = 0; i < OBJECT_TABLE_MAX_SIZE; i++) {
        if (s->regs[OBJECT_TABLE + i * OBJ_SIZE + OBJ_ADDR_TYPE] == 0) {

            s->regs[OBJECT_TABLE + i * OBJ_SIZE + OBJ_ADDR_TYPE] = type;

            s->regs[OBJECT_TABLE + i * OBJ_SIZE + OBJ_ADDR_START] =
                offset & 0xFF;

            s->regs[OBJECT_TABLE + i * OBJ_SIZE + OBJ_ADDR_START + 1] =
                (offset >> 8) & 0xFF;

            s->regs[OBJECT_TABLE + i * OBJ_SIZE + OBJ_ADDR_SIZE] = size - 1;

            s->regs[OBJECT_TABLE + i * OBJ_SIZE + OBJ_ADDR_INSTANCES] = 0;

            s->regs[OBJECT_TABLE + i * OBJ_SIZE + OBJ_ADDR_REPORT_IDS] = report_ids;

            s->regs[OBJECTS_NUM]++;
            break;
        }
    }
}

/* Reset to default values */
static void qt602240_reset(QT602240State *s)
{
    s->regs[FAMILY_ID] = 0x80;
    s->regs[VARIANT_ID] = 0x00;
    s->regs[VERSION] = 0x11;
    s->regs[BUILD] = 0x00;
    s->regs[MATRIX_X_SIZE] = 16;
    s->regs[MATRIX_Y_SIZE] = 14;
    s->regs[OBJECTS_NUM] = 0;

    s->minx = 0;
    s->maxx = 1;
    s->miny = 0;
    s->maxy = 1;

    qt602240_add_object(s, MESSAGE, MESSAGE_SIZE,
                        QT602240_GEN_MESSAGE, 0);
    qt602240_add_object(s, MULTITOUCH, MULTITOUCH_SIZE,
                        QT602240_TOUCH_MULTI, 10);
    qt602240_add_object(s, SPT_CTECONFIG, SPT_CTECONFIG_SIZE,
                        QT602240_SPT_CTECONFIG, 0);
    qt602240_add_object(s, GENCOMMAND, GENCOMMAND_SIZE,
                        QT602240_GEN_COMMAND, 0);

    s->regs[MESSAGE + QT602240_REPORTID] = 0xFF;

    s->queue_start = 0;
    s->queue_end = 0;
}

#define OFFESETOF_MEM(s, mem) ((void *)(&(mem)) - (void *)(s))

static uint8_t qt602240_read(void *opaque, uint32_t address, uint8_t offset)
{
    QT602240State *s = (QT602240State *)opaque;
    uint8_t retval;
    uint32_t reg = address + offset;

    if (reg > TOTAL_SIZE) {
        hw_error("qt602240: bad read offset 0x%x\n", reg);
    }

    retval = s->regs[reg];
    if (reg >= MESSAGE + QT602240_REPORTID &&
        reg <= MESSAGE + QT602240_CHECKSUM) {
        /* Get message from the queue */
        if (s->queue_start == s->queue_end) {
            /* No messages */
            return 0xFF;
        }
        retval = s->queue[s->queue_start][reg - MESSAGE];
        /* Here is an assumption that message is read till the end */
        if (reg == MESSAGE + QT602240_CHECKSUM) {
            /* Move to the next message from the queue */
            s->queue_start = (s->queue_start + 1) % QT602240_MAX_MESSAGE;
        }
    }

    return retval;
}

static void qt602240_write(void *opaque, uint32_t address, uint8_t offset,
                           uint8_t val)
{
    QT602240State *s = (QT602240State *)opaque;
    uint32_t reg = address + offset;

    if (reg >= MULTITOUCH &&
        reg < MULTITOUCH + MULTITOUCH_SIZE) {
        s->regs[reg] = val;

        if (reg == MULTITOUCH + MULTITOUCH_ORIENT) {
            s->orient = s->regs[reg] & 1;
        }

        if (reg == MULTITOUCH + MULTITOUCH_XRANGE_LSB ||
            reg == MULTITOUCH + MULTITOUCH_XRANGE_MSB) {
            int res = s->regs[MULTITOUCH + MULTITOUCH_XRANGE_LSB] +
                     (s->regs[MULTITOUCH + MULTITOUCH_XRANGE_MSB] << 8) + 1;

            if (s->orient == 0)
                s->maxx = res;
            else
                s->maxy = res;
        }

        if (reg == MULTITOUCH + MULTITOUCH_YRANGE_LSB ||
            reg == MULTITOUCH + MULTITOUCH_YRANGE_MSB) {
            int res = s->regs[MULTITOUCH + MULTITOUCH_YRANGE_LSB] +
                     (s->regs[MULTITOUCH + MULTITOUCH_YRANGE_MSB] << 8) + 1;

            if (s->orient == 0)
                s->maxy = res;
            else
                s->maxx = res;
        }

    } else if (reg >= GENCOMMAND &&
        reg < GENCOMMAND + GENCOMMAND_SIZE) {
        s->regs[reg] = val;
    } else if (reg >= SPT_CTECONFIG &&
        reg < SPT_CTECONFIG + SPT_CTECONFIG_SIZE) {
        s->regs[reg] = val;
    } else {
        hw_error("qt602240: bad write offset 0x%x\n", reg);
    }
}

/* Modify the message read by the driver */
static void qt602240_msg(QT602240State *s, int x, int y, int status)
{
    /* Check if queue is full */
    if ((s->queue_end + 1) % QT602240_MAX_MESSAGE == s->queue_start) {
        return;
    }

    memset(s->queue[s->queue_end], 0, MESSAGE_SIZE);
    s->queue[s->queue_end][QT602240_REPORTID] = 1;
    s->queue[s->queue_end][QT602240_MSG_XPOSMSB] = x >> 2;
    s->queue[s->queue_end][QT602240_MSG_YPOSMSB] = y >> 2;
    s->queue[s->queue_end][QT602240_MSG_XYPOSLSB] =
        ((x & 3) << 6) | ((y & 3) << 2);
    s->queue[s->queue_end][QT602240_MSG_STATUS] = status;
    s->queue[s->queue_end][QT602240_MSG_TCHAREA] = 1;

    s->queue_end = (s->queue_end + 1) % QT602240_MAX_MESSAGE;
}

static void qt602240_ts_event(void *opaque,
                              int x, int y, int z, int buttons_state)
{
    QT602240State *s = (QT602240State *)opaque;

    /* Convert QEMU mouse coordinates to the touchscreen */
    /* FIXME: should we use configuration data provided by the driver? */
    y = (s->miny + y * (s->maxy - s->miny) / QEMUMAXY);
    x = (s->minx + x * (s->maxx - s->minx) / QEMUMAXX);

    if (s->pressure == !buttons_state) {
        if (buttons_state) {
            qt602240_msg(s, x, y, QT602240_PRESS | QT602240_DETECT);
        } else {
            qt602240_msg(s, x, y, QT602240_RELEASE);
        }
        qemu_irq_raise(s->irq);
    } else if (s->pressure && (x != s->prev_x || y != s->prev_y)) {
        qt602240_msg(s, x, y, QT602240_MOVE | QT602240_DETECT);
        qemu_irq_raise(s->irq);
    }

    s->pressure = !!buttons_state;
    s->prev_x = x;
    s->prev_y = y;
}

static int qt602240_init(I2CAddressableState *s)
{
    QT602240State *t = FROM_I2CADDR_SLAVE(QT602240State, s);

    qdev_init_gpio_out(&s->i2c.qdev, &t->irq, 1);

    qemu_add_mouse_event_handler(qt602240_ts_event, t, 1,
                                 "AT42QT602240 Touchscreen");
    qt602240_reset(t);

    return 0;
}

static I2CAddressableDeviceInfo qt602240_info = {
    .i2c.qdev.name  = "qt602240",
    .i2c.qdev.size  = sizeof(QT602240State),
    .init  = qt602240_init,
    .read  = qt602240_read,
    .write = qt602240_write,
    .size  = 2,
    .rev   = 0
};

static void qt602240_register_devices(void)
{
    i2c_addressable_register_device(&qt602240_info);
}

device_init(qt602240_register_devices)
