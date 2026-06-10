/*
 * QEMU RISC-V G233 Board
 *
 * Copyright (c) 2025 Chao Liu <chao.liu@yeah.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_G233_H
#define HW_G233_H

#include "hw/core/boards.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/core/sysbus.h"

#define VIRT_CPUS_MAX_BITS             9
#define VIRT_CPUS_MAX                  (1 << VIRT_CPUS_MAX_BITS)
#define VIRT_SOCKETS_MAX_BITS          2
#define VIRT_SOCKETS_MAX               (1 << VIRT_SOCKETS_MAX_BITS)

#define TYPE_RISCV_G233_MACHINE MACHINE_TYPE_NAME("g233")
typedef struct RISCVG233State RISCVG233State;
DECLARE_INSTANCE_CHECKER(RISCVG233State, RISCV_G233_MACHINE,
                         TYPE_RISCV_G233_MACHINE)

struct RISCVG233State {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    Notifier machine_done;
    RISCVHartArrayState soc[VIRT_SOCKETS_MAX];
    DeviceState *irqchip[VIRT_SOCKETS_MAX];

    int fdt_size;
    bool have_aclint;
    const MemMapEntry *memmap;
};

enum {
    G233_MROM,
    G233_CLINT,
    G233_PLIC,
    G233_UART,
    G233_WDT,
    G233_GPIO,
    G233_PWM,
    G233_SPI,
    G233_DRAM,
};

enum {
    UART_IRQ = 1,
    GPIO_IRQ,
    PWM_IRQ,
    WDT_IRQ,
    SPI_IRQ,
};

#define VIRT_IRQCHIP_NUM_SOURCES 96
#define VIRT_IRQCHIP_NUM_PRIO_BITS 3

#define VIRT_PLIC_PRIORITY_BASE 0x00
#define VIRT_PLIC_PENDING_BASE 0x1000
#define VIRT_PLIC_ENABLE_BASE 0x2000
#define VIRT_PLIC_ENABLE_STRIDE 0x80
#define VIRT_PLIC_CONTEXT_BASE 0x200000
#define VIRT_PLIC_CONTEXT_STRIDE 0x1000

#define FDT_PLIC_ADDR_CELLS   0
#define FDT_PLIC_INT_CELLS    1

#endif
