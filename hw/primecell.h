#ifndef PRIMECELL_H
#define PRIMECELL_H

/* Declarations for ARM PrimeCell based periperals.  */
/* Also includes some devices that are currently only used by the
   ARM boards.  */

/* pl080.c */
void *pl080_init(uint32_t base, qemu_irq irq, int nchannels);

/* pl192.c */
void pl192_chain(void *first, void *next);

/* pl330.c */
DeviceState *pl330_init(target_phys_addr_t base, const uint32_t *cfg,
                        qemu_irq *irqs, qemu_irq irq_abort);

/* arm_sysctl.c */
void arm_sysctl_init(uint32_t base, uint32_t sys_id, uint32_t proc_id);

#endif
