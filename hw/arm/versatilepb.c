#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "sysemu/sysemu.h"
#include "hw/pci/pci.h"
#include "hw/boards.h"
#include "sysemu/block-backend.h"
#include "exec/address-spaces.h"
#include "hw/block/flash.h"
#include "qemu/error-report.h"
#include "hw/char/pl011.h"
#include "hw/loader.h"
#include "exec/ram_addr.h"

#include "pmb8876/utils.h"
#include "pmb8876/io_bridge.h"
#include "pmb8876/regs.h"

#include "pmb8876/utils.c"
#include "pmb8876/io_bridge.c"
#include "pmb8876/cpu_tcm.c"

static ARMCPU *cpu;
static unsigned int PMB8876_IRQ_C[0xFF];
static unsigned int irq_num = 0;
static unsigned int irq_num_i = 0;
static unsigned int _time = 0;
static qemu_irq irq = NULL;

static void exec_irq(unsigned int irq_n) {
	if (irq_num != 0 || (cpsr_read(&cpu->env) & 0x80))
		return;
	
	unsigned int action = 0;
	cpu_physical_memory_read(0x2E38 + irq_n, &action, 1);
	
	irq_num = irq_n;
	printf("exec_irq(%02X) %s (%s) [action=%d]\n", irq_n, (!(cpsr_read(&cpu->env) & 0x80)) ? "irq on" : "irq off", pmb8876_get_cpu_mode(&(cpu->env)), action);
	qemu_irq_raise(irq);
	return;
	
	if (!(cpu->env.uncached_cpsr & 0x80)) {
		irq_num = irq_n;
		qemu_irq_pulse(qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ));
	}
}

// ================================================================= //
//                     SIEMES EL71 HARDWARE                          //
// ================================================================= //
static uint64_t cpu_io_read(void *opaque, hwaddr offset, unsigned size) {
	offset += (unsigned int) opaque;
	
	if (offset == PMB8876_USART0_FCSTAT) {
		return 2 | 4 | 1;
	} else if (offset == PMB8876_USART0_TXB) {
		return 0;
	} else if (offset == PMB8876_USART0_ICR) {
		return 0;
	} else if (offset == PMB8876_USART0_RXB) {
		return 0x55;
	} else if (offset >= PMB8876_USART0_CLC && offset <= PMB8876_USART0_ICR) {
		printf ("READ UNIMPL UART (%s) 0x%x\n", pmb8876_get_reg_name(offset), (int)offset);
		return 0;
	}
	unsigned int value = sie_bridge_read(offset);
	
	if (offset == 0xF4400010)
		return 0x80005003 |  0x10000000;
	
//	if (offset >= 0xf2800030 && offset <= 0xf28002a8)
//		value = PMB8876_IRQ_C[(offset - 0xf2800030) / 4];
	
	if (offset == 0xF280001C) {
		printf("get_irq(%02X)\n", irq_num);
		value = 0x77;
	}
	
	unsigned int action = 0;
	cpu_physical_memory_read(0xA8D95BCC, &action, 4);
	
	if (PMB8876_IRQ_C[0x77] && action) {
		exec_irq(0x77);
	}
	++irq_num_i;
	return value;
}

static void cpu_io_write(void *opaque, hwaddr offset, uint64_t value, unsigned size) {
	offset += (unsigned int) opaque;
	
//	printf ("WRITE (%s) 0x%x=0x%x [%s]\n", pmb8876_get_reg_name(offset), (int)offset, (int)value, pmb8876_get_cpu_mode(&cpu->env));
	/* UART */
	if (offset == PMB8876_USART0_ICR) {
		return;
	} else if (offset == PMB8876_USART0_TXB) {
		// printf("%c", (char) (value & 0xFF));
		printf("UART TX: %02lX\n", value & 0xFF);
		return;
	} else if (offset >= PMB8876_USART0_CLC && offset <= PMB8876_USART0_ICR) {
		printf ("WRITE UNIMPL UART (%s) 0x%x\n", pmb8876_get_reg_name(offset), (int)offset);
		return;
	}
	
	if (offset == 0xF2800014) {
		printf("ack_irq(%02X)\n", irq_num);
		qemu_irq_lower(irq);
		irq_num = 0;
	}
	
	if (offset >= 0xf2800030 && offset <= 0xf28002a8) {
		PMB8876_IRQ_C[(offset - 0xf2800030) / 4] = value;
	}
	
	return sie_bridge_write(offset, value);
}
static const MemoryRegionOps pmb8876_common_io_opts = {
	.read = cpu_io_read,
	.write = cpu_io_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};
// ================================================================= //

static void versatile_init(MachineState *machine, int board_id) {
	ObjectClass *cpu_oc;
	Object *cpuobj;
	DeviceState *dev, *sysctl;
	MemoryRegion *sysmem = get_system_memory();
	 
	if (!machine->cpu_model)
		machine->cpu_model = "arm926";
	
	cpu_oc = cpu_class_by_name(TYPE_ARM_CPU, machine->cpu_model);
	if (!cpu_oc) {
		fprintf(stderr, "Unable to find CPU definition\n");
		exit(1);
	}
	
	// cpu_state = &cpu->parent_obj;
	cpuobj = object_new(object_class_get_name(cpu_oc));
	if (object_property_find(cpuobj, "has_el3", NULL))
		object_property_set_bool(cpuobj, false, "has_el3", &error_fatal);
	object_property_set_bool(cpuobj, true, "realized", &error_fatal);
	
	cpu = ARM_CPU(cpuobj);
	define_arm_cp_regs(cpu, pmb8876_cp_reginfo); // TCM
	
    sysctl = qdev_create(NULL, "realview_sysctl");
    qdev_prop_set_uint32(sysctl, "sys_id", 0x41007004);
    qdev_prop_set_uint32(sysctl, "proc_id", 0x02000000);
    qdev_init_nofail(sysctl);
    sysbus_mmio_map(SYS_BUS_DEVICE(sysctl), 0, 0x10000000);
	 
	irq = qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ);
	//sysbus_init_irq(SYS_BUS_DEVICE(sysctl), &irq);
	
	/*
		Memory map (https://habrahabr.ru/post/149068/):
		0x00000000-0x00003FFF — Внутренняя SRAM #1 (внутри микроконтроллера)
		0x00080000-0x00097FFF — Внутренняя SRAM #2 (внутри микроконтроллера)
		0x00400000-0x0040FFFF — Внутренний BootROM (внутри микроконтроллера)
		0xA0000000-0xA7FFFFFF — Внешняя Flash (бывает 32 МБ, 64 МБ, 96 МБ, 128 МБ)
		0xA8000000-0xA9FFFFFF — Внешняя SDRAM (бывает 8 МБ, 16 МБ, 32 МБ)
		0xB0000000-0xB7FFFFFF — Зеркало Flash с доступом на запись
		0xF0000000-0xFFFFFFFF — Порты ввода/вывода, регистры встроенных устройств в мк
	*/
    MemoryRegion *sram1 = g_new(MemoryRegion, 1);
    MemoryRegion *sram2 = g_new(MemoryRegion, 1);
    MemoryRegion *brom = g_new(MemoryRegion, 1);
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    MemoryRegion *flash_io = g_new(MemoryRegion, 1);
    MemoryRegion *sdram = g_new(MemoryRegion, 1);
    MemoryRegion *io = g_new(MemoryRegion, 1);
    MemoryRegion *test = g_new(MemoryRegion, 1);
	
	// IO
	memory_region_init_io(io, NULL, &pmb8876_common_io_opts, (void *) 0, "IO", 0xFFFF0000);
	memory_region_add_subregion(sysmem, 0x0, io);
	
	// SRAM1
	memory_region_allocate_system_memory(sram1, NULL, "SRAM1", 0x40000);
	memory_region_add_subregion(sysmem, 0x0, sram1);
	
	// SRAM2
	memory_region_allocate_system_memory(sram2, NULL, "SRAM2", 0x18000);
	memory_region_add_subregion(sysmem, 0x80000, sram2);
	
	// BootROM
	memory_region_allocate_system_memory(brom, NULL, "BROM", 0x100000);
	memory_region_add_subregion(sysmem, 0x400000, brom);
	
	// FLASH
	memory_region_allocate_system_memory(flash, NULL, "FLASH", 0x8000000 - 0x1000);
	memory_region_add_subregion(sysmem, 0xA0001000, flash);
    
	// FLASH IO
	memory_region_init_io(flash_io, NULL, &pmb8876_common_io_opts, (void *) 0xA0000000, "FLASH_IO", 0x1000);
	memory_region_add_subregion(sysmem, 0xA0000000, flash_io);
    
	// SDRAM
	memory_region_allocate_system_memory(sdram, NULL, "SDRAM", 0x01000000);
	memory_region_add_subregion(sysmem, 0xA8000000, sdram);
    
	// test IO
//	memory_region_init_io(test, NULL, &pmb8876_common_io_opts, (void *) 0xA8E2A580, "FLASH_IO", 0x8);
//	memory_region_add_subregion(sysmem, 0xA0000000, test);
    
    // IRQ
    memset(&PMB8876_IRQ_C, 0, sizeof(PMB8876_IRQ_C));
    
	////////////////////////////////////////////////
	//                   BOOT                     //
	////////////////////////////////////////////////
	
	int r;
	static const unsigned char boot[] = {
		0xF1, 0x04, 0xA0, 0xE3, 0x20, 0x10, 0x90, 0xE5, 0xFF, 0x10, 0xC1, 0xE3, 0xA5, 0x10, 0x81, 0xE3, 0x20, 0x10, 0x80, 0xE5, 0x1E, 0xFF, 0x2F, 0xE1, 
		0x04, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x49, 0x45, 0x4D, 0x45, 0x4E, 0x53, 0x5F, 0x42, 0x4F, 0x4F, 0x54, 0x43, 0x4F, 0x44, 0x45, 
		0x01, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
		
		0x01, 0x04, 0x05, 0x00, 0x89, 0x00, 0x89 // normal mode
		// 0x01, 0x04, 0x05, 0x00, 0x8B, 0x00, 0x8B // service mode
		// 0x01, 0x04, 0x05, 0x80, 0x83, 0x00, 0x03 // burnin mode
	};
	
	cpu_physical_memory_write(0x82000, &boot, sizeof(boot));
	
	r = load_image_targphys("/home/azq2/dev/siemens/ff/brom.bin", 0x00400000, 0xFFFF);
	if (r < 0) {
		error_report("Failed to load firmware from brom.bin");
		exit(1);
	}
	
	// r = load_image_targphys("/home/azq2/dev/siemens/IPhone2G_BB.bin", 0xA0000000, 0x8000000);
	r = load_image_targphys("/home/azq2/dev/siemens/ff/ff.bin", 0xA0000000, 0x8000000);
	if (r < 0) {
		error_report("Failed to load firmware from EL71.bin");
		exit(1);
	}
	cpu_set_pc(CPU(cpu), 0x00400000);
}

// xuj
static void vpb_init(MachineState *machine) {
	versatile_init(machine, 0x183);
}
static void vab_init(MachineState *machine) {
	versatile_init(machine, 0x25e);
}
static void versatilepb_class_init(ObjectClass *oc, void *data) {
	MachineClass *mc = MACHINE_CLASS(oc);
	
	mc->desc = "ARM Versatile/PB (ARM926EJ-S)";
	mc->init = vpb_init;
	mc->block_default_type = IF_SCSI;
}
static const TypeInfo versatilepb_type = {
	.name = MACHINE_TYPE_NAME("versatilepb"),
	.parent = TYPE_MACHINE,
	.class_init = versatilepb_class_init,
};
static void versatileab_class_init(ObjectClass *oc, void *data) {
	MachineClass *mc = MACHINE_CLASS(oc);

	mc->desc = "ARM Versatile/AB (ARM926EJ-S)";
	mc->init = vab_init;
	mc->block_default_type = IF_SCSI;
}
static const TypeInfo versatileab_type = {
	.name = MACHINE_TYPE_NAME("versatileab"),
	.parent = TYPE_MACHINE,
	.class_init = versatileab_class_init,
};
static void versatile_machine_init(void) {
	type_register_static(&versatilepb_type);
	type_register_static(&versatileab_type);
}
type_init(versatile_machine_init)
