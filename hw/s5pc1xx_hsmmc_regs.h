/*
 * S5C HS-MMC Controller Constants
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 *
 * Based on SMDK6400 MMC (hw/regs-hsmmc.h)
 */

#ifndef __ASM_ARCH_REGS_HSMMC_H
#define __ASM_ARCH_REGS_HSMMC_H __FILE__

/*
 * HS MMC Interface
 */

#define S5C_HSMMC_REG(x)                    (x)

/* R/W SDMA System Address register 0x0 */
#define S5C_HSMMC_SYSAD                     0x00

/* R/W Host DMA Buffer Boundary and Transfer Block Size Register 0x0 */
#define S5C_HSMMC_BLKSIZE                   0x04
#define S5C_HSMMC_MAKE_BLKSZ(dma, bblksz)   (((dma & 0x7) << 12) | (blksz & 0xFFF))

/* R/W Blocks count for current transfer 0x0 */
#define S5C_HSMMC_BLKCNT                    0x06
/* R/W Command Argument Register 0x0 */
#define S5C_HSMMC_ARGUMENT                  0x08

/* R/W Transfer Mode Setting Register 0x0 */
#define S5C_HSMMC_TRNMOD                    0x0C
#define S5C_HSMMC_TRNS_DMA                  0x01
#define S5C_HSMMC_TRNS_BLK_CNT_EN           0x02
#define S5C_HSMMC_TRNS_ACMD12               0x04
#define S5C_HSMMC_TRNS_READ                 0x10
#define S5C_HSMMC_TRNS_MULTI                0x20

#define S5C_HSMMC_TRNS_BOOTCMD              0x1000
#define S5C_HSMMC_TRNS_BOOTACK              0x2000

/* R/W Command Register 0x0 */
#define S5C_HSMMC_CMDREG                    0x0E
/* ROC Response Register 0 0x0 */
#define S5C_HSMMC_RSPREG0                   0x10
/* ROC Response Register 1 0x0 */
#define S5C_HSMMC_RSPREG1                   0x14
/* ROC Response Register 2 0x0 */
#define S5C_HSMMC_RSPREG2                   0x18
/* ROC Response Register 3 0x0 */
#define S5C_HSMMC_RSPREG3                   0x1C
#define S5C_HSMMC_CMD_RESP_MASK             0x03
#define S5C_HSMMC_CMD_CRC                   0x08
#define S5C_HSMMC_CMD_INDEX                 0x10
#define S5C_HSMMC_CMD_DATA                  0x20
#define S5C_HSMMC_CMD_RESP_NONE             0x00
#define S5C_HSMMC_CMD_RESP_LONG             0x01
#define S5C_HSMMC_CMD_RESP_SHORT            0x02
#define S5C_HSMMC_CMD_RESP_SHORT_BUSY       0x03
#define S5C_HSMMC_MAKE_CMD(c, f)            (((c & 0xFF) << 8) | (f & 0xFF))

/* R/W Buffer Data Register 0x0 */
#define S5C_HSMMC_BDATA                     0x20
/* R/ROC Present State Register 0x000A0000 */
#define S5C_HSMMC_PRNSTS                    0x24
#define S5C_HSMMC_CMD_INHIBIT               0x00000001
#define S5C_HSMMC_DATA_INHIBIT              0x00000002
#define S5C_HSMMC_DOING_WRITE               0x00000100
#define S5C_HSMMC_DOING_READ                0x00000200
#define S5C_HSMMC_SPACE_AVAILABLE           0x00000400
#define S5C_HSMMC_DATA_AVAILABLE            0x00000800
#define S5C_HSMMC_CARD_PRESENT              0x00010000
#define S5C_HSMMC_WRITE_PROTECT             0x00080000

/* R/W Present State Register 0x0 */
#define S5C_HSMMC_HOSTCTL                   0x28
#define S5C_HSMMC_CTRL_LED                  0x01
#define S5C_HSMMC_CTRL_4BITBUS              0x02
#define S5C_HSMMC_CTRL_HIGHSPEED            0x04
#define S5C_HSMMC_CTRL_1BIT                 0x00
#define S5C_HSMMC_CTRL_4BIT                 0x02
#define S5C_HSMMC_CTRL_8BIT                 0x20
#define S5C_HSMMC_CTRL_SDMA                 0x00
#define S5C_HSMMC_CTRL_ADMA2_32             0x10

/* R/W Present State Register 0x0 */
#define S5C_HSMMC_PWRCON                    0x29
#define S5C_HSMMC_POWER_OFF                 0x00
#define S5C_HSMMC_POWER_ON                  0x01
#define S5C_HSMMC_POWER_180                 0x0A
#define S5C_HSMMC_POWER_300                 0x0C
#define S5C_HSMMC_POWER_330                 0x0E
#define S5C_HSMMC_POWER_ON_ALL              0xFF

/* R/W Block Gap Control Register 0x0 */
#define S5C_HSMMC_BLKGAP                    0x2A
/* R/W Wakeup Control Register 0x0 */
#define S5C_HSMMC_WAKCON                    0x2B

#define S5C_HSMMC_STAWAKEUP                 0x8

/* R/W Command Register 0x0 */
#define S5C_HSMMC_CLKCON                    0x2C
#define S5C_HSMMC_DIVIDER_SHIFT             0x8
#define S5C_HSMMC_CLOCK_EXT_STABLE          0x8
#define S5C_HSMMC_CLOCK_CARD_EN             0x4
#define S5C_HSMMC_CLOCK_INT_STABLE          0x2
#define S5C_HSMMC_CLOCK_INT_EN              0x1

/* R/W Timeout Control Register 0x0 */
#define S5C_HSMMC_TIMEOUTCON                0x2E
#define S5C_HSMMC_TIMEOUT_MAX               0x0E

/* R/W Software Reset Register 0x0 */
#define S5C_HSMMC_SWRST                     0x2F
#define S5C_HSMMC_RESET_ALL                 0x01
#define S5C_HSMMC_RESET_CMD                 0x02
#define S5C_HSMMC_RESET_DATA                0x04

/* ROC/RW1C Normal Interrupt Status Register 0x0 */
#define S5C_HSMMC_NORINTSTS                 0x30
#define S5C_HSMMC_NIS_ERR                   0x00008000
#define S5C_HSMMC_NIS_CMDCMP                0x00000001
#define S5C_HSMMC_NIS_TRSCMP                0x00000002
#define S5C_HSMMC_NIS_DMA                   0x00000008
#define S5C_HSMMC_NIS_INSERT                0x00000040
#define S5C_HSMMC_NIS_REMOVE                0x00000080

/* ROC/RW1C Error Interrupt Status Register 0x0 */
#define S5C_HSMMC_ERRINTSTS                 0x32
#define S5C_HSMMC_EIS_CMDTIMEOUT            0x00000001
#define S5C_HSMMC_EIS_CMDERR                0x0000000E
#define S5C_HSMMC_EIS_DATATIMEOUT           0x00000010
#define S5C_HSMMC_EIS_DATAERR               0x00000060
#define S5C_HSMMC_EIS_CMD12ERR              0x00000100
#define S5C_HSMMC_EIS_ADMAERR               0x00000200
#define S5C_HSMMC_EIS_STABOOTACKERR         0x00000400

/* R/W Normal Interrupt Status Enable Register 0x0 */
#define S5C_HSMMC_NORINTSTSEN               0x34
/* R/W Error Interrupt Status Enable Register 0x0 */
#define S5C_HSMMC_ERRINTSTSEN               0x36
#define S5C_HSMMC_ENSTABOOTACKERR           0x400

/* R/W Normal Interrupt Signal Enable Register 0x0 */
#define S5C_HSMMC_NORINTSIGEN               0x38
#define S5C_HSMMC_INT_MASK_ALL              0x00
#define S5C_HSMMC_INT_RESPONSE              0x00000001
#define S5C_HSMMC_INT_DATA_END              0x00000002
#define S5C_HSMMC_INT_DMA_END               0x00000008
#define S5C_HSMMC_INT_SPACE_AVAIL           0x00000010
#define S5C_HSMMC_INT_DATA_AVAIL            0x00000020
#define S5C_HSMMC_INT_CARD_INSERT           0x00000040
#define S5C_HSMMC_INT_CARD_REMOVE           0x00000080
#define S5C_HSMMC_INT_CARD_CHANGE           0x000000C0
#define S5C_HSMMC_INT_CARD_INT              0x00000100
#define S5C_HSMMC_INT_TIMEOUT               0x00010000
#define S5C_HSMMC_INT_CRC                   0x00020000
#define S5C_HSMMC_INT_END_BIT               0x00040000
#define S5C_HSMMC_INT_INDEX                 0x00080000
#define S5C_HSMMC_INT_DATA_TIMEOUT          0x00100000
#define S5C_HSMMC_INT_DATA_CRC              0x00200000
#define S5C_HSMMC_INT_DATA_END_BIT          0x00400000
#define S5C_HSMMC_INT_BUS_POWER             0x00800000
#define S5C_HSMMC_INT_ACMD12ERR             0x01000000
#define S5C_HSMMC_INT_ADMAERR               0x02000000

#define S5C_HSMMC_INT_NORMAL_MASK           0x00007FFF
#define S5C_HSMMC_INT_ERROR_MASK            0xFFFF8000

#define S5C_HSMMC_INT_CMD_MASK              (S5C_HSMMC_INT_RESPONSE | \
                                             S5C_HSMMC_INT_TIMEOUT | \
                                             S5C_HSMMC_INT_CRC | \
                                             S5C_HSMMC_INT_END_BIT | \
                                             S5C_HSMMC_INT_INDEX | \
                                             S5C_HSMMC_NIS_ERR)
#define S5C_HSMMC_INT_DATA_MASK             (S5C_HSMMC_INT_DATA_END | \
                                             S5C_HSMMC_INT_DMA_END | \
                                             S5C_HSMMC_INT_DATA_AVAIL | \
                                             S5C_HSMMC_INT_SPACE_AVAIL | \
                                             S5C_HSMMC_INT_DATA_TIMEOUT | \
                                             S5C_HSMMC_INT_DATA_CRC | \
                                             S5C_HSMMC_INT_DATA_END_BIT)

/* R/W Error Interrupt Signal Enable Register 0x0 */
#define S5C_HSMMC_ERRINTSIGEN               0x3A
#define S5C_HSMMC_ENSIGBOOTACKERR           0x400

/* ROC Auto CMD12 error status register 0x0 */
#define S5C_HSMMC_ACMD12ERRSTS              0x3C

/* HWInit Capabilities Register 0x05E80080 */
#define S5C_HSMMC_CAPAREG                   0x40
#define S5C_HSMMC_TIMEOUT_CLK_MASK          0x0000003F
#define S5C_HSMMC_TIMEOUT_CLK_SHIFT         0x0
#define S5C_HSMMC_TIMEOUT_CLK_UNIT          0x00000080
#define S5C_HSMMC_CLOCK_BASE_MASK           0x00003F00
#define S5C_HSMMC_CLOCK_BASE_SHIFT          0x8
#define S5C_HSMMC_MAX_BLOCK_MASK            0x00030000
#define S5C_HSMMC_MAX_BLOCK_SHIFT           0x10
#define S5C_HSMMC_CAN_DO_DMA                0x00400000
#define S5C_HSMMC_CAN_DO_ADMA2              0x00080000
#define S5C_HSMMC_CAN_VDD_330               0x01000000
#define S5C_HSMMC_CAN_VDD_300               0x02000000
#define S5C_HSMMC_CAN_VDD_180               0x04000000

/* HWInit Maximum Current Capabilities Register 0x0 */
#define S5C_HSMMC_MAXCURR                   0x48

/* For ADMA2 */

/* W Force Event Auto CMD12 Error Interrupt Register 0x0000 */
#define S5C_HSMMC_FEAER                     0x50
/* W Force Event Error Interrupt Register Error Interrupt 0x0000 */
#define S5C_HSMMC_FEERR                     0x52

/* R/W ADMA Error Status Register 0x00 */
#define S5C_HSMMC_ADMAERR                   0x54
#define S5C_HSMMC_ADMAERR_CONTINUE_REQUEST  (1 << 9)
#define S5C_HSMMC_ADMAERR_INTRRUPT_STATUS   (1 << 8)
#define S5C_HSMMC_ADMAERR_LENGTH_MISMATCH   (1 << 2)
#define S5C_HSMMC_ADMAERR_STATE_ST_STOP     (0 << 0)
#define S5C_HSMMC_ADMAERR_STATE_ST_FDS      (1 << 0)
#define S5C_HSMMC_ADMAERR_STATE_ST_TFR      (3 << 0)

/* R/W ADMA System Address Register 0x00 */
#define S5C_HSMMC_ADMASYSADDR               0x58
#define S5C_HSMMC_ADMA_ATTR_MSK             0x3F
#define S5C_HSMMC_ADMA_ATTR_ACT_NOP         (0 << 4)
#define S5C_HSMMC_ADMA_ATTR_ACT_RSV         (1 << 4)
#define S5C_HSMMC_ADMA_ATTR_ACT_TRAN        (2 << 4)
#define S5C_HSMMC_ADMA_ATTR_ACT_LINK        (3 << 4)
#define S5C_HSMMC_ADMA_ATTR_INT             (1 << 2)
#define S5C_HSMMC_ADMA_ATTR_END             (1 << 1)
#define S5C_HSMMC_ADMA_ATTR_VALID           (1 << 0)

/* R/W Control register 2 0x0 */
#define S5C_HSMMC_CONTROL2                  0x80
/* R/W FIFO Interrupt Control (Control Register 3) 0x7F5F3F1F */
#define S5C_HSMMC_CONTROL3                  0x84
/* R/W Control register 4 0x0 */
#define S5C_HSMMC_CONTROL4                  0x8C


/* Magic register which is used from kernel! */
#define S5C_HSMMC_SLOT_INT_STATUS           0xFC


/* HWInit Host Controller Version Register 0x0401 */
#define S5C_HSMMC_HCVER                     0xFE
#define S5C_HSMMC_VENDOR_VER_MASK           0xFF00
#define S5C_HSMMC_VENDOR_VER_SHIFT          0x8
#define S5C_HSMMC_SPEC_VER_MASK             0x00FF
#define S5C_HSMMC_SPEC_VER_SHIFT            0x0

#define S5C_HSMMC_REG_SIZE                  0x100

#endif /* __ASM_ARCH_REGS_HSMMC_H */
