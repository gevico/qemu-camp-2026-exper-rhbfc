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

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qapi/error.h"
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/char/pl011.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/g233.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/numa.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/sifive_plic.h"
#include "chardev/char.h"
#include "system/device_tree.h"
#include "system/system.h"

static const MemMapEntry g233_memmap[] = {
    [G233_MROM] =   {     0x1000,        0x2000 },
    [G233_CLINT] =  { 0x02000000,        0xC000 },
    [G233_PLIC] =   { 0x0C000000,     0x4000000 },
    [G233_UART] =   { 0x10000000,       0x1000 },
    [G233_WDT] =    { 0x10010000,       0x1000 },
    [G233_GPIO] =   { 0x10012000,        0x100 },
    [G233_PWM] =    { 0x10015000,       0x1000 },
    [G233_SPI] =    { 0x10018000,       0x1000 },
    [G233_DRAM] =   { 0x80000000,    0x40000000 },
};

static void create_fdt_socket_cpus(RISCVG233State *s, int socket,
                                   char *clust_name, uint32_t *phandle,
                                   uint32_t *intc_phandles)
{
    int cpu;
    uint32_t cpu_phandle;
    MachineState *ms = MACHINE(s);
    bool is_32_bit = riscv_is_32bit(&s->soc[0]);

    for (cpu = s->soc[socket].num_harts - 1; cpu >= 0; cpu--) {
        RISCVCPU *cpu_ptr = &s->soc[socket].harts[cpu];
        int8_t satp_mode_max = cpu_ptr->cfg.max_satp_mode;
        g_autofree char *cpu_name = NULL;
        g_autofree char *core_name = NULL;
        g_autofree char *intc_name = NULL;
        g_autofree char *sv_name = NULL;

        cpu_phandle = (*phandle)++;

        cpu_name = g_strdup_printf("/cpus/cpu@%d",
            s->soc[socket].hartid_base + cpu);
        qemu_fdt_add_subnode(ms->fdt, cpu_name);

        if (satp_mode_max != -1) {
            sv_name = g_strdup_printf("riscv,%s",
                                      satp_mode_str(satp_mode_max, is_32_bit));
            qemu_fdt_setprop_string(ms->fdt, cpu_name, "mmu-type", sv_name);
        }

        riscv_isa_write_fdt(cpu_ptr, ms->fdt, cpu_name);

        if (cpu_ptr->cfg.ext_zicbom) {
            qemu_fdt_setprop_cell(ms->fdt, cpu_name, "riscv,cbom-block-size",
                                  cpu_ptr->cfg.cbom_blocksize);
        }

        if (cpu_ptr->cfg.ext_zicboz) {
            qemu_fdt_setprop_cell(ms->fdt, cpu_name, "riscv,cboz-block-size",
                                  cpu_ptr->cfg.cboz_blocksize);
        }

        if (cpu_ptr->cfg.ext_zicbop) {
            qemu_fdt_setprop_cell(ms->fdt, cpu_name, "riscv,cbop-block-size",
                                  cpu_ptr->cfg.cbop_blocksize);
        }

        qemu_fdt_setprop_string(ms->fdt, cpu_name, "compatible", "riscv");
        qemu_fdt_setprop_string(ms->fdt, cpu_name, "status", "okay");
        qemu_fdt_setprop_cell(ms->fdt, cpu_name, "reg",
            s->soc[socket].hartid_base + cpu);
        qemu_fdt_setprop_string(ms->fdt, cpu_name, "device_type", "cpu");
        riscv_socket_fdt_write_id(ms, cpu_name, socket);
        qemu_fdt_setprop_cell(ms->fdt, cpu_name, "phandle", cpu_phandle);

        intc_phandles[cpu] = (*phandle)++;

        intc_name = g_strdup_printf("%s/interrupt-controller", cpu_name);
        qemu_fdt_add_subnode(ms->fdt, intc_name);
        qemu_fdt_setprop_cell(ms->fdt, intc_name, "phandle",
            intc_phandles[cpu]);
        qemu_fdt_setprop_string(ms->fdt, intc_name, "compatible",
            "riscv,cpu-intc");
        qemu_fdt_setprop(ms->fdt, intc_name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(ms->fdt, intc_name, "#interrupt-cells", 1);

        core_name = g_strdup_printf("%s/core%d", clust_name, cpu);
        qemu_fdt_add_subnode(ms->fdt, core_name);
        qemu_fdt_setprop_cell(ms->fdt, core_name, "cpu", cpu_phandle);
    }
}

static void create_fdt_socket_memory(RISCVG233State *s, int socket)
{
    g_autofree char *mem_name = NULL;
    hwaddr addr;
    uint64_t size;
    MachineState *ms = MACHINE(s);

    addr = s->memmap[G233_DRAM].base + riscv_socket_mem_offset(ms, socket);
    size = riscv_socket_mem_size(ms, socket);
    mem_name = g_strdup_printf("/memory@%"HWADDR_PRIx, addr);
    qemu_fdt_add_subnode(ms->fdt, mem_name);
    qemu_fdt_setprop_sized_cells(ms->fdt, mem_name, "reg", 2, addr, 2, size);
    qemu_fdt_setprop_string(ms->fdt, mem_name, "device_type", "memory");
    riscv_socket_fdt_write_id(ms, mem_name, socket);
}

static void create_fdt_socket_clint(RISCVG233State *s,
                                    int socket,
                                    uint32_t *intc_phandles)
{
    int cpu;
    g_autofree char *clint_name = NULL;
    g_autofree uint32_t *clint_cells = NULL;
    hwaddr clint_addr;
    MachineState *ms = MACHINE(s);
    static const char * const clint_compat[2] = {
        "sifive,clint0", "riscv,clint0"
    };

    clint_cells = g_new0(uint32_t, s->soc[socket].num_harts * 4);

    for (cpu = 0; cpu < s->soc[socket].num_harts; cpu++) {
        clint_cells[cpu * 4 + 0] = cpu_to_be32(intc_phandles[cpu]);
        clint_cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_SOFT);
        clint_cells[cpu * 4 + 2] = cpu_to_be32(intc_phandles[cpu]);
        clint_cells[cpu * 4 + 3] = cpu_to_be32(IRQ_M_TIMER);
    }

    clint_addr = s->memmap[G233_CLINT].base +
                 s->memmap[G233_CLINT].size * socket;
    clint_name = g_strdup_printf("/soc/clint@%"HWADDR_PRIx, clint_addr);
    qemu_fdt_add_subnode(ms->fdt, clint_name);
    qemu_fdt_setprop_string_array(ms->fdt, clint_name, "compatible",
                                  (char **)&clint_compat,
                                  ARRAY_SIZE(clint_compat));
    qemu_fdt_setprop_sized_cells(ms->fdt, clint_name, "reg",
        2, clint_addr, 2, s->memmap[G233_CLINT].size);
    qemu_fdt_setprop(ms->fdt, clint_name, "interrupts-extended",
        clint_cells, s->soc[socket].num_harts * sizeof(uint32_t) * 4);
    riscv_socket_fdt_write_id(ms, clint_name, socket);
}

static void create_fdt_socket_aclint(RISCVG233State *s,
                                     int socket,
                                     uint32_t *intc_phandles)
{
    int cpu;
    char *name;
    unsigned long addr, size;
    uint32_t aclint_cells_size;
    g_autofree uint32_t *aclint_mswi_cells = NULL;
    g_autofree uint32_t *aclint_mtimer_cells = NULL;
    MachineState *ms = MACHINE(s);

    aclint_mswi_cells = g_new0(uint32_t, s->soc[socket].num_harts * 2);
    aclint_mtimer_cells = g_new0(uint32_t, s->soc[socket].num_harts * 2);

    for (cpu = 0; cpu < s->soc[socket].num_harts; cpu++) {
        aclint_mswi_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        aclint_mswi_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_M_SOFT);
        aclint_mtimer_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        aclint_mtimer_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_M_TIMER);
    }
    aclint_cells_size = s->soc[socket].num_harts * sizeof(uint32_t) * 2;

    /* Per-socket ACLINT MSWI */
    addr = s->memmap[G233_CLINT].base +
           (s->memmap[G233_CLINT].size * socket);
    name = g_strdup_printf("/soc/mswi@%lx", addr);
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible",
        "riscv,aclint-mswi");
    qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
        2, addr, 2, RISCV_ACLINT_SWI_SIZE);
    qemu_fdt_setprop(ms->fdt, name, "interrupts-extended",
        aclint_mswi_cells, aclint_cells_size);
    qemu_fdt_setprop(ms->fdt, name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(ms->fdt, name, "#interrupt-cells", 0);
    riscv_socket_fdt_write_id(ms, name, socket);
    g_free(name);

    /* Per-socket ACLINT MTIMER */
    addr = s->memmap[G233_CLINT].base + RISCV_ACLINT_SWI_SIZE +
           (s->memmap[G233_CLINT].size * socket);
    size = s->memmap[G233_CLINT].size - RISCV_ACLINT_SWI_SIZE;
    name = g_strdup_printf("/soc/mtimer@%lx", addr);
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible",
        "riscv,aclint-mtimer");
    qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
        2, addr + RISCV_ACLINT_DEFAULT_MTIME,
        2, size - RISCV_ACLINT_DEFAULT_MTIME,
        2, addr + RISCV_ACLINT_DEFAULT_MTIMECMP,
        2, RISCV_ACLINT_DEFAULT_MTIME);
    qemu_fdt_setprop(ms->fdt, name, "interrupts-extended",
        aclint_mtimer_cells, aclint_cells_size);
    riscv_socket_fdt_write_id(ms, name, socket);
    g_free(name);
}

static void create_fdt_socket_plic(RISCVG233State *s,
                                   int socket,
                                   uint32_t *phandle, uint32_t *intc_phandles,
                                   uint32_t *plic_phandles)
{
    int cpu;
    g_autofree char *plic_name = NULL;
    g_autofree uint32_t *plic_cells;
    unsigned long plic_addr;
    MachineState *ms = MACHINE(s);
    static const char * const plic_compat[2] = {
        "sifive,plic-1.0.0", "riscv,plic0"
    };

    plic_phandles[socket] = (*phandle)++;
    plic_addr = s->memmap[G233_PLIC].base +
                (s->memmap[G233_PLIC].size * socket);
    plic_name = g_strdup_printf("/soc/plic@%lx", plic_addr);
    qemu_fdt_add_subnode(ms->fdt, plic_name);
    qemu_fdt_setprop_cell(ms->fdt, plic_name,
        "#interrupt-cells", FDT_PLIC_INT_CELLS);
    qemu_fdt_setprop_cell(ms->fdt, plic_name,
        "#address-cells", FDT_PLIC_ADDR_CELLS);
    qemu_fdt_setprop_string_array(ms->fdt, plic_name, "compatible",
                                  (char **)&plic_compat,
                                  ARRAY_SIZE(plic_compat));
    qemu_fdt_setprop(ms->fdt, plic_name, "interrupt-controller", NULL, 0);

    plic_cells = g_new0(uint32_t, s->soc[socket].num_harts * 4);

    for (cpu = 0; cpu < s->soc[socket].num_harts; cpu++) {
        plic_cells[cpu * 4 + 0] = cpu_to_be32(intc_phandles[cpu]);
        plic_cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_EXT);
        plic_cells[cpu * 4 + 2] = cpu_to_be32(intc_phandles[cpu]);
        plic_cells[cpu * 4 + 3] = cpu_to_be32(IRQ_S_EXT);
    }

    qemu_fdt_setprop(ms->fdt, plic_name, "interrupts-extended",
                     plic_cells,
                     s->soc[socket].num_harts * sizeof(uint32_t) * 4);

    qemu_fdt_setprop_sized_cells(ms->fdt, plic_name, "reg",
                                 2, plic_addr, 2, s->memmap[G233_PLIC].size);
    qemu_fdt_setprop_cell(ms->fdt, plic_name, "riscv,ndev",
                          VIRT_IRQCHIP_NUM_SOURCES - 1);
    riscv_socket_fdt_write_id(ms, plic_name, socket);
    qemu_fdt_setprop_cell(ms->fdt, plic_name, "phandle",
        plic_phandles[socket]);
}

static void create_fdt_sockets(RISCVG233State *s, uint32_t *phandle,
                               uint32_t *intc_phandles, uint32_t *plic_phandle)
{
    int socket;
    MachineState *ms = MACHINE(s);
    int socket_count = riscv_socket_count(ms);
    int phandle_pos = ms->smp.cpus;

    qemu_fdt_add_subnode(ms->fdt, "/cpus");
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "timebase-frequency",
                          RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#address-cells", 0x1);
    qemu_fdt_add_subnode(ms->fdt, "/cpus/cpu-map");

    for (socket = (socket_count - 1); socket >= 0; socket--) {
        g_autofree char *clust_name = NULL;
        phandle_pos -= s->soc[socket].num_harts;

        clust_name = g_strdup_printf("/cpus/cpu-map/cluster%d", socket);
        qemu_fdt_add_subnode(ms->fdt, clust_name);

        create_fdt_socket_cpus(s, socket, clust_name, phandle,
                               &intc_phandles[phandle_pos]);
        create_fdt_socket_memory(s, socket);

        if (s->have_aclint) {
            create_fdt_socket_aclint(s, socket,
                                     &intc_phandles[phandle_pos]);
        } else {
            create_fdt_socket_clint(s, socket,
                                    &intc_phandles[phandle_pos]);
        }
    }

    create_fdt_socket_plic(s, 0, phandle, intc_phandles, plic_phandle);
}

static void create_fdt_uart(RISCVG233State *s,
                            uint32_t irq_mmio_phandle)
{
    g_autofree char *name = NULL;
    MachineState *ms = MACHINE(s);

    name = g_strdup_printf("/soc/serial@%"HWADDR_PRIx,
                           s->memmap[G233_UART].base);
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "pl011");
    qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
                                 2, s->memmap[G233_UART].base,
                                 2, s->memmap[G233_UART].size);
    qemu_fdt_setprop_cell(ms->fdt, name, "clock-frequency", 3686400);
    qemu_fdt_setprop_cell(ms->fdt, name, "interrupt-parent", irq_mmio_phandle);
    qemu_fdt_setprop_cell(ms->fdt, name, "interrupts", UART_IRQ);

    qemu_fdt_setprop_string(ms->fdt, "/chosen", "stdout-path", name);
    qemu_fdt_setprop_string(ms->fdt, "/aliases", "serial0", name);
}

static void finalize_fdt(RISCVG233State *s)
{
    uint32_t phandle = 1, plic_phandle = 1;
    MachineState *ms = MACHINE(s);
    uint32_t *intc_phandles = g_new0(uint32_t, ms->smp.cpus);

    create_fdt_sockets(s, &phandle, intc_phandles, &plic_phandle);
    create_fdt_uart(s, plic_phandle);
    g_free(intc_phandles);
}

static void create_fdt(RISCVG233State *s)
{
    MachineState *ms = MACHINE(s);
    uint8_t rng_seed[32];

    ms->fdt = create_device_tree(&s->fdt_size);
    if (!ms->fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(ms->fdt, "/", "model", "gevico-g233,qemu");
    qemu_fdt_setprop_string(ms->fdt, "/", "compatible", "riscv-virtio");
    qemu_fdt_setprop_cell(ms->fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(ms->fdt, "/", "#address-cells", 0x2);

    qemu_fdt_add_subnode(ms->fdt, "/soc");
    qemu_fdt_setprop(ms->fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(ms->fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(ms->fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(ms->fdt, "/soc", "#address-cells", 0x2);

    qemu_fdt_add_subnode(ms->fdt, "/chosen");
    qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
    qemu_fdt_setprop(ms->fdt, "/chosen", "rng-seed",
                     rng_seed, sizeof(rng_seed));
    qemu_fdt_add_subnode(ms->fdt, "/aliases");
}

static DeviceState *virt_create_plic(const MemMapEntry *memmap, int socket,
                                     int base_hartid, int hart_count)
{
    g_autofree char *plic_hart_config = NULL;

    plic_hart_config = riscv_plic_hart_config_string(hart_count);

    return sifive_plic_create(
             memmap[G233_PLIC].base + socket * memmap[G233_PLIC].size,
             plic_hart_config, hart_count, base_hartid,
             VIRT_IRQCHIP_NUM_SOURCES,
             ((1U << VIRT_IRQCHIP_NUM_PRIO_BITS) - 1),
             VIRT_PLIC_PRIORITY_BASE, VIRT_PLIC_PENDING_BASE,
             VIRT_PLIC_ENABLE_BASE, VIRT_PLIC_ENABLE_STRIDE,
             VIRT_PLIC_CONTEXT_BASE,
             VIRT_PLIC_CONTEXT_STRIDE,
             memmap[G233_PLIC].size);
}

static void virt_machine_done(Notifier *notifier, void *data)
{
    RISCVG233State *s = container_of(notifier, RISCVG233State,
                                     machine_done);
    MachineState *machine = MACHINE(s);
    hwaddr start_addr = s->memmap[G233_DRAM].base;
    target_ulong firmware_end_addr, kernel_start_addr;
    const char *firmware_name = riscv_default_firmware_name(&s->soc[0]);
    uint64_t fdt_load_addr;
    uint64_t kernel_entry = 0;
    RISCVBootInfo boot_info;

    if (machine->dtb == NULL) {
        finalize_fdt(s);
    }

    firmware_end_addr = riscv_find_and_load_firmware(machine, firmware_name,
                                                     &start_addr, NULL);

    riscv_boot_info_init(&boot_info, &s->soc[0]);

    if (machine->kernel_filename && !kernel_entry) {
        kernel_start_addr = riscv_calc_kernel_start_addr(&boot_info,
                                                         firmware_end_addr);
        riscv_load_kernel(machine, &boot_info, kernel_start_addr,
                          true, NULL);
        kernel_entry = boot_info.image_low_addr;
    }

    fdt_load_addr = riscv_compute_fdt_addr(s->memmap[G233_DRAM].base,
                                           s->memmap[G233_DRAM].size,
                                           machine, &boot_info);
    riscv_load_fdt(fdt_load_addr, machine->fdt);

    riscv_setup_rom_reset_vec(machine, &s->soc[0], start_addr,
                              s->memmap[G233_MROM].base,
                              s->memmap[G233_MROM].size, kernel_entry,
                              fdt_load_addr);
}

static void virt_machine_init(MachineState *machine)
{
    RISCVG233State *s = RISCV_G233_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    DeviceState *mmio_irqchip;
    int i, base_hartid, hart_count;
    int socket_count = riscv_socket_count(machine);

    s->memmap = g233_memmap;

    if (VIRT_SOCKETS_MAX < socket_count) {
        error_report("number of sockets/nodes should be less than %d",
            VIRT_SOCKETS_MAX);
        exit(1);
    }

    /* Initialize sockets */
    mmio_irqchip = NULL;
    for (i = 0; i < socket_count; i++) {
        g_autofree char *soc_name = g_strdup_printf("soc%d", i);

        if (!riscv_socket_check_hartids(machine, i)) {
            error_report("discontinuous hartids in socket%d", i);
            exit(1);
        }

        base_hartid = riscv_socket_first_hartid(machine, i);
        hart_count = riscv_socket_hart_count(machine, i);

        object_initialize_child(OBJECT(machine), soc_name, &s->soc[i],
                                TYPE_RISCV_HART_ARRAY);
        object_property_set_str(OBJECT(&s->soc[i]), "cpu-type",
                                machine->cpu_type, &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]), "hartid-base",
                                base_hartid, &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]), "num-harts",
                                hart_count, &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&s->soc[i]), &error_fatal);

        if (s->have_aclint) {
            /* ACLINT MSWI + MTIMER */
            riscv_aclint_swi_create(s->memmap[G233_CLINT].base +
                        i * s->memmap[G233_CLINT].size,
                    base_hartid, hart_count, false);
            riscv_aclint_mtimer_create(s->memmap[G233_CLINT].base +
                        i * s->memmap[G233_CLINT].size +
                        RISCV_ACLINT_SWI_SIZE,
                    RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                    base_hartid, hart_count,
                    RISCV_ACLINT_DEFAULT_MTIMECMP,
                    RISCV_ACLINT_DEFAULT_MTIME,
                    RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
        } else {
            /* Legacy SiFive CLINT */
            riscv_aclint_swi_create(
                    s->memmap[G233_CLINT].base + i * s->memmap[G233_CLINT].size,
                    base_hartid, hart_count, false);
            riscv_aclint_mtimer_create(s->memmap[G233_CLINT].base +
                    i * s->memmap[G233_CLINT].size + RISCV_ACLINT_SWI_SIZE,
                    RISCV_ACLINT_DEFAULT_MTIMER_SIZE, base_hartid, hart_count,
                    RISCV_ACLINT_DEFAULT_MTIMECMP, RISCV_ACLINT_DEFAULT_MTIME,
                    RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
        }

        /* PLIC */
        s->irqchip[i] = virt_create_plic(s->memmap, i,
                                         base_hartid, hart_count);

        if (i == 0) {
            mmio_irqchip = s->irqchip[i];
        }
    }

    /* DRAM */
    memory_region_add_subregion(system_memory, s->memmap[G233_DRAM].base,
                                machine->ram);

    /* MROM */
    memory_region_init_rom(mask_rom, NULL, "riscv_virt_board.mrom",
                           s->memmap[G233_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, s->memmap[G233_MROM].base,
                                mask_rom);

    DeviceState *gpio_dev = qdev_new("g233-gpio");
    sysbus_realize_and_unref(SYS_BUS_DEVICE(gpio_dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(gpio_dev), 0, s->memmap[G233_GPIO].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(gpio_dev), 0,
                       qdev_get_gpio_in(mmio_irqchip, GPIO_IRQ));

    DeviceState *pwm_dev = qdev_new("g233-pwm");
    sysbus_realize_and_unref(SYS_BUS_DEVICE(pwm_dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(pwm_dev), 0, s->memmap[G233_PWM].base);

    DeviceState *wdt_dev = qdev_new("g233-wdt");
    sysbus_realize_and_unref(SYS_BUS_DEVICE(wdt_dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(wdt_dev), 0, s->memmap[G233_WDT].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(wdt_dev), 0,
                       qdev_get_gpio_in(mmio_irqchip, WDT_IRQ));

    /* SPI controller */
    DeviceState *spi_dev = qdev_new("g233-spi");
    DeviceState *flash_dev;

    sysbus_realize_and_unref(SYS_BUS_DEVICE(spi_dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(spi_dev), 0, s->memmap[G233_SPI].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(spi_dev), 0,
                       qdev_get_gpio_in(mmio_irqchip, SPI_IRQ));

    /* CS0: W25X16 (2MB) */
    flash_dev = qdev_new("w25x16");
    qdev_prop_set_uint8(flash_dev, "cs", 0);
    qdev_realize_and_unref(flash_dev,
                           qdev_get_child_bus(spi_dev, "spi"),
                           &error_fatal);

    /* CS1: W25X32 (4MB) */
    flash_dev = qdev_new("w25x32");
    qdev_prop_set_uint8(flash_dev, "cs", 1);
    qdev_realize_and_unref(flash_dev,
                           qdev_get_child_bus(spi_dev, "spi"),
                           &error_fatal);

    /* UART */
    pl011_create(s->memmap[G233_UART].base,
                 qdev_get_gpio_in(mmio_irqchip, UART_IRQ),
                 serial_hd(0));

    /* Create FDT */
    if (machine->dtb) {
        machine->fdt = load_device_tree(machine->dtb, &s->fdt_size);
        if (!machine->fdt) {
            error_report("load_device_tree() failed");
            exit(1);
        }
    } else {
        create_fdt(s);
    }

    s->machine_done.notify = virt_machine_done;
    qemu_add_machine_init_done_notifier(&s->machine_done);
}

static void virt_machine_instance_init(Object *obj)
{
}

static void virt_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V G233 SoC Board";
    mc->init = virt_machine_init;
    mc->max_cpus = VIRT_CPUS_MAX;
    mc->default_cpu_type = TYPE_RISCV_CPU_GEVICO_CV1;
    mc->block_default_type = IF_VIRTIO;
    mc->no_cdrom = 1;
    mc->pci_allow_0_address = true;
    mc->possible_cpu_arch_ids = riscv_numa_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = riscv_numa_cpu_index_to_props;
    mc->get_default_cpu_node_id = riscv_numa_get_default_cpu_node_id;
    mc->numa_mem_supported = true;
    mc->cpu_cluster_has_numa_boundary = true;
    mc->default_ram_id = "riscv_virt_board.ram";
}

static const TypeInfo virt_machine_typeinfo = {
    .name       = TYPE_RISCV_G233_MACHINE,
    .parent     = TYPE_MACHINE,
    .class_init = virt_machine_class_init,
    .instance_init = virt_machine_instance_init,
    .instance_size = sizeof(RISCVG233State),
};

static void virt_machine_init_register_types(void)
{
    type_register_static(&virt_machine_typeinfo);
}

type_init(virt_machine_init_register_types)
