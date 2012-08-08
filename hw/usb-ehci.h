#ifndef QEMU_USB_EHCI_H
#define QEMU_USB_EHCI_H

#include "qemu-common.h"

void usb_ehci_init_pci(PCIBus *bus, int devfn);

#endif
