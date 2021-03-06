#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "qemu/timer.h"
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

#include "hw/arm/pmb8876/irq.h"

static ARMCPU *cpu;
static qemu_irq pmb8876_irq[PMB8876_IRQS_NR];
static QEMUTimer *gsm_tpu_timer;
static struct {
	unsigned int clc;
	unsigned int con;
} gsm_tpu;
int xuj;
int xuj2 = 0;

static void gsm_tpu_timer_handler(void *opaque) {
	qemu_set_irq(pmb8876_irq[0x77], 1);
	timer_mod(gsm_tpu_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000 * 1000);
}

const char *pmb8876_trigger_irq(int irq) {
	qemu_set_irq(pmb8876_irq[irq], 1);
}

// ================================================================= //
//                     SIEMES EL71 HARDWARE                          //
// ================================================================= //
static uint64_t cpu_io_read(void *opaque, hwaddr offset, unsigned size) {
	offset += (unsigned int) opaque;
	
	unsigned int value = sie_bridge_read(offset, size, cpu->env.regs[15]);
	
	if (offset == PMB8876_USART0_FCSTAT) {
		return 2 | 4 | 1;
	} else if (offset == PMB8876_USART0_TXB) {
		return 0;
	} else if (offset == PMB8876_USART0_ICR) {
		return 0;
	} else if (offset == PMB8876_USART0_RXB) {
		return 0x55;
	} else if (offset >= PMB8876_USART0_CLC && offset <= PMB8876_USART0_ICR) {
		//printf ("READ UNIMPL UART (%s) 0x%x (from 0x%08X)\n", pmb8876_get_reg_name(offset), (int)offset, cpu->env.regs[15]);
	}
	
	if (size !=  4) {
	//	printf("Invalid size of read: %d (ADDR: %08lX, VALUE: %08X, PC: %08X)\n", size, (unsigned long) offset, value, cpu->env.regs[15]);
		// exit(0);
	}
	
//	printf("READ: %d (ADDR: %08X, VALUE: %08X, PC: %08X)\n", size, offset, value, cpu->env.regs[15]);
	
//	if (offset == 0xF4400010)
//		return (value & ~0xF0000000) | 0x10000000;
	
	return value;
}

static void cpu_io_write(void *opaque, hwaddr offset, uint64_t value, unsigned size) {
	offset += (unsigned int) opaque;
	if (size !=  4) {
	//	printf("Invalid size of write: %d (ADDR: %08lX, VALUE: %08lX, PC: %08X)\n", size, (unsigned long) offset, (unsigned long) value, cpu->env.regs[15]);
		// exit(0);
	}

#if 0
	if (offset == 0xF43000D0)
		qemu_set_irq(pmb8876_irq[0x9B], 1);
	
	/* ====== GSM TPU ====== */
	if (offset >= 0xF6400000 && offset <= 0xF64000FF) {
		if (offset == 0xF6400000) {
			gsm_tpu.clc = (uint32_t) value;
		} else if (offset == 0xF64000F8) {
			gsm_tpu.con = (uint32_t) value;
			
			if ((gsm_tpu.con & 0x1000) && gsm_tpu.con & 0x4000  && !xuj) {
				xuj = 1;
				timer_mod(gsm_tpu_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000 * 1000 * 1000);
			}
			/*
			printf("gsm_tpu.con update!\n");
			if (gsm_tpu.con & 0x4000) {
				printf("reset timer!\n");
				qemu_set_irq(pmb8876_irq[0x77], 0);
			}
			
			if ((gsm_tpu.con & 0x1000) && gsm_tpu.con & 0x4000) {
				printf("tick timer!\n");
				timer_mod(gsm_tpu_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
			}
			*/
		}
	}
	
	if (offset == 0xF7600034) {
	//	qemu_set_irq(pmb8876_irq[0x9B], 1);
	}
	/* ==================== */
#endif

//	printf ("WRITE (%s) 0x%x=0x%x [%s]\n", pmb8876_get_reg_name(offset), (int)offset, (int)value, pmb8876_get_cpu_mode(&cpu->env));
	/* UART */
	if (offset == PMB8876_USART0_ICR) {
		return;
	} else if (offset == PMB8876_USART0_TXB) {
		// printf("%c", (char) (value & 0xFF));
		char c = (value & 0xFF);
		if (c == '\xFF' || c == '\xFE' || c == '\x10' || c == '\x11')
			c = '\n';
		fprintf(stderr, "%c", c);
		
	//	printf("TX: %02X (%c)\n", (value & 0xFF), (char) (value & 0xFF));
	//	printf("UART TX: %02lX\n", value & 0xFF);
		return;
	} else if (offset >= PMB8876_USART0_CLC && offset <= PMB8876_USART0_ICR) {
		// printf ("WRITE UNIMPL UART (%s) 0x%x\n", pmb8876_get_reg_name(offset), (int)offset);
	}
	
	sie_bridge_write(offset, size, value, cpu->env.regs[15]);
}
static const MemoryRegionOps pmb8876_common_io_opts = {
	.read = cpu_io_read,
	.write = cpu_io_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};
// ================================================================= //
//							FLASH BRIDGE							 //
// ================================================================= //
static int flash_wcyle = 0;
static int flash_cmd = 0;
static int flash_cmd_addr = 0;
static int flash_count = 0;
static uint64_t flash_io_read(void *opaque, hwaddr offset, unsigned size) {
	MemoryRegion *flash = (MemoryRegion *) opaque;
	offset += 0xA0000000;
	
	unsigned long value = sie_bridge_read(offset, size, cpu->env.regs[15]);
	
//	printf("FLASH IO READ[%ld]: %08lX from %08lX\n", size, value, offset);
	
	if (flash_wcyle == 1 && flash_cmd == 0x70) {
		flash_wcyle = 0;
		if (g_ignore_irq > 1) {
			fprintf(stderr, "**** RESTORE IRQ: %02X\n", 0x77);
			pmb8876_trigger_irq(0x77);
		}
		g_ignore_irq = 0;
		memory_region_rom_device_set_romd(flash, true);
	}
	
	return value;
}
static void flash_io_write(void *opaque, hwaddr offset, uint64_t value, unsigned size) {
	MemoryRegion *flash = (MemoryRegion *) opaque;
	offset += 0xA0000000;
	
//	printf("FLASH IO WRITE[%d]: %08lX to %08lX\n", size, value, offset);
	sie_bridge_write(offset, size, value, cpu->env.regs[15]);
	
	if (!flash_wcyle) {
		// Переводим память в режим IO
		memory_region_rom_device_set_romd(flash, false);
	}
	
	/* 1 write cycle */
	if (flash_wcyle == 0) {
		switch (value) {
			case 0xF0:
			case 0xFF:
				fprintf(stderr, "[FLASH] Read Array\n");
				goto xx_flash_reset;
			case 0x70:
				fprintf(stderr, "[FLASH] Read Status Register\n");
			break;
			case 0x90:
				fprintf(stderr, "[FLASH] Read Electronic Signature\n");
			break;
			case 0x98:
				fprintf(stderr, "[FLASH] Read CFI Query\n");
			break;
			case 0x50:
				fprintf(stderr, "[FLASH] Clear Status Register\n");
				goto xx_flash_reset;
			case 0x20:
				fprintf(stderr, "[FLASH] Block Erase\n");
			break;
			case 0x41:
				fprintf(stderr, "[FLASH] Program\n");
			break;
			case 0xE9:
				fprintf(stderr, "[FLASH] Buffer Program\n");
			break;
			case 0xB0:
				fprintf(stderr, "[FLASH] Program/Erase Suspend\n");
				goto xx_flash_reset;
			case 0xD0:
				fprintf(stderr, "[FLASH] Program/Erase Resume\n");
				goto xx_flash_reset;
			case 0xC0:
				fprintf(stderr, "[FLASH] Protection Register Program\n");
			break;
			case 0x60:
				fprintf(stderr, "[FLASH] Block lock/unlock\n");
			break;
			case 0xBC:
				fprintf(stderr, "[FLASH] Blank Check\n");
			break;
			case 0x94:
				fprintf(stderr, "[FLASH] Read EFA Block\n");
			break;
			case 0x44:
				fprintf(stderr, "[FLASH] Program EFA Block\n");
			break;
			case 0x24:
				fprintf(stderr, "[FLASH] Erase EFA Block\n");
			break;
			case 0x64:
				fprintf(stderr, "[FLASH] EFA lock/unlock\n");
			break;
			default:
				goto xx_flash_error;
		}
		++flash_wcyle;
		flash_cmd = value;
		flash_cmd_addr = offset;
		g_ignore_irq = 1;
	} else if (flash_wcyle == 1) {
		switch (flash_cmd) {
			case 0x98:
				fprintf(stderr, "[FLASH] Exit Read CFI Query\n");
				goto xx_flash_reset;
			case 0x70:
				fprintf(stderr, "[FLASH] Exit Read Status Register\n");
				goto xx_flash_reset;
			case 0x90:
				fprintf(stderr, "[FLASH] Exit Read Electronic Signature\n");
				goto xx_flash_reset;
			case 0x20: /* Block erase */
				if (value == 0xD0) { /* Confirm */
					fprintf(stderr, "[FLASH] Block Erase Confirm\n");
					goto xx_flash_reset;
				} else if (value == 0xFF) { /* Read Array */
					goto xx_flash_reset;
				}
				goto xx_flash_error;
			break;
			
			case 0x41: /* Program */
				fprintf(stderr, "[FLASH] Program Data (%04X)\n", value);
				if (size != 2)
					goto xx_flash_error;
				
				unsigned char *data = (unsigned char *) memory_region_get_ram_ptr(flash);
				data[offset - 0xA0000000] = value & 0xFF;
				data[offset - 0xA0000000 + 1] = (value >> 8) & 0xFF;
				
				goto xx_flash_reset;
			break;
			
			case 0xE9: /* Buffer Program */
				flash_count = (value + 1);
				fprintf(stderr, "[FLASH] Buffer Program (size=%d)\n", flash_count);
				++flash_wcyle;
			break;
			
			case 0xC0: /* Protection Register Program */
				fprintf(stderr, "[FLASH] Protection Register Program Data (%04X)\n", value);
				goto xx_flash_error;
			break;
			
			case 0x60: /* Block lock/unlock */
				if (value == 0x01) {
					fprintf(stderr, "[FLASH] Block Lock\n");
				} else if (value == 0x03) {
					fprintf(stderr, "[FLASH] Set Configuration Register (%08X=%08X)\n", flash_cmd_addr, offset);
				} else if (value == 0x04) {
					fprintf(stderr, "[FLASH] Set Enhanced Configuration Register\n");
				} else if (value == 0xD0) {
					fprintf(stderr, "[FLASH] Block Unlock\n");
				} else if (value == 0x2F) {
					fprintf(stderr, "[FLASH] Block Lock-Down\n");
				} else {
					fprintf(stderr, "[FLASH] Unknown Lock Cmd: %02X\n", value);
				}
				goto xx_flash_reset;
			break;
			
			case 0x64: /* EFA lock/unlock */
				if (value == 0x01) {
					fprintf(stderr, "[FLASH] Lock EFA Block\n");
				} else if (value == 0xD0) {
					fprintf(stderr, "[FLASH] Unlock EFA Block\n");
				} else if (value == 0x2F) {
					fprintf(stderr, "[FLASH] Lock-Down EFA Block\n");
				} else {
					fprintf(stderr, "[FLASH] Unknown EFA Lock Cmd: %02X\n", value);
				}
				goto xx_flash_reset;
			break;
			
			case 0xBC: /* Blank Check */
				if (value == 0xD0) { /* Confirm */
					fprintf(stderr, "[FLASH] Blank Check Confirm\n");
					goto xx_flash_reset;
				} else if (value == 0xFF) { /* Read Array */
					goto xx_flash_reset;
				}
				goto xx_flash_error;
			break;
			
			case 0x44: /* Program EFA Block */
				fprintf(stderr, "[FLASH] Program EFA Block Data (%04X)\n", value);
				goto xx_flash_reset;
			break;
			
			case 0x24: /* Erase EFA Block */
				if (value == 0xD0) { /* Confirm */
					fprintf(stderr, "[FLASH] Erase EFA Block Confirm\n");
					goto xx_flash_reset;
				} else if (value == 0xFF) { /* Read Array */
					goto xx_flash_reset;
				}
				goto xx_flash_error;
			break;
			
			default:
				goto xx_flash_error;
		}
	} else if (flash_wcyle == 2) {
		switch (flash_cmd) {
			case 0xE9: /* Buffer Program */
			{
				if (size != 2 && size != 4)
					goto xx_flash_error;
				
				flash_count -= (size == 4 ? 2 : 1);
				
				if (!flash_count)
					++flash_wcyle;
				fprintf(stderr, "[FLASH] Buffer Program Write %08lX: %08lX\n", offset - 0xA0000000, value);
				
				unsigned char *data = (unsigned char *) memory_region_get_ram_ptr(flash);
				if (size == 1) {
					data[offset - 0xA0000000] = value;
				} else if (size == 2) {
					data[offset - 0xA0000000] = value & 0xFF;
					data[offset - 0xA0000000 + 1] = (value >> 8) & 0xFF;
				} else if (size == 3) {
					data[offset - 0xA0000000] = value & 0xFF;
					data[offset - 0xA0000000 + 1] = (value >> 8) & 0xFF;
					data[offset - 0xA0000000 + 2] = (value >> 16) & 0xFF;
				} else if (size == 4) {
					data[offset - 0xA0000000] = value & 0xFF;
					data[offset - 0xA0000000 + 1] = (value >> 8) & 0xFF;
					data[offset - 0xA0000000 + 2] = (value >> 16) & 0xFF;
					data[offset - 0xA0000000 + 3] = (value >> 24) & 0xFF;
					
					fprintf(
						stderr, 
						"=> %08X: %02X, %02X, %02X, %02X\n", 
						offset - 0xA0000000, 
						data[offset - 0xA0000000], 
						data[offset - 0xA0000000 + 1], 
						data[offset - 0xA0000000 + 2], 
						data[offset - 0xA0000000 + 3]
					);
				}
			}
			break;
			
			default:
				goto xx_flash_error;
		}
	} else if (flash_wcyle == 3) {
		switch (flash_cmd) {
			case 0xE9: /* Buffer Program */
				if (value == 0xD0) { /* Confirm */
					fprintf(stderr, "[FLASH] Buffer Program Confirm\n");
					goto xx_flash_reset;
				}
				goto xx_flash_error;
			break;
			
			default:
				goto xx_flash_error;
		}
	} else {
		// WTF?
		goto xx_flash_error;
	}
	return;
	
xx_flash_error:
	fprintf(stderr, "[FLASH] Invalid write %08X to %08X (wcycle=%d, cmd=%02X)\n", value, offset, flash_wcyle, flash_cmd);
	exit(1);
xx_flash_reset:
	flash_wcyle = 0;
	flash_cmd = 0;
	
	if (g_ignore_irq > 1) {
		fprintf(stderr, "**** RESTORE IRQ: %02X\n", 0x77);
		pmb8876_trigger_irq(0x77);
	}
	g_ignore_irq = 0;
	
	sie_bridge_write(offset, size, 0xFF, cpu->env.regs[15]);
	
	memory_region_rom_device_set_romd(flash, true);
}
static const MemoryRegionOps pmb8876_flash_io_opts = {
	.read = flash_io_read,
	.write = flash_io_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};
// ================================================================= //
static void versatile_init(MachineState *machine, int board_id) {
	int i;
	ObjectClass *cpu_oc;
	Object *cpuobj;
	DeviceState *dev, *sysctl;
	MemoryRegion *sysmem = get_system_memory();
	Error *err = NULL;
	
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
    MemoryRegion *sram3 = g_new(MemoryRegion, 1);
    MemoryRegion *brom = g_new(MemoryRegion, 1);
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    MemoryRegion *flash_io = g_new(MemoryRegion, 1);
    MemoryRegion *sdram = g_new(MemoryRegion, 1);
    MemoryRegion *io = g_new(MemoryRegion, 1);
    MemoryRegion *test = g_new(MemoryRegion, 1);
    MemoryRegion *test2 = g_new(MemoryRegion, 1);
    MemoryRegion *test3 = g_new(MemoryRegion, 1);
	
	int iphone_bb = 0;
	
	// IO
	memory_region_init_io(io, NULL, &pmb8876_common_io_opts, (void *) 0, "IO", 0xFFFF0000);
	memory_region_add_subregion(sysmem, 0x0, io);
	
	// SRAM1
	memory_region_allocate_system_memory(sram1, NULL, "SRAM1", 0x40000);
	memory_region_add_subregion(sysmem, 0x0, sram1);
	
	// SRAM2
	memory_region_allocate_system_memory(sram2, NULL, "SRAM2", 0x18000);
	memory_region_add_subregion(sysmem, 0x80000, sram2);
	
	// SRAM2
	memory_region_allocate_system_memory(sram3, NULL, "SRAM3", 0xFFFF);
	memory_region_add_subregion(sysmem, 0xFFFF0000, sram3);
	
	// BootROM
	memory_region_allocate_system_memory(brom, NULL, "BROM", 0x100000);
	memory_region_add_subregion(sysmem, 0x400000, brom);
	
	if (iphone_bb) {
		// FLASH
		memory_region_allocate_system_memory(flash, NULL, "FLASH", 0x8000000 );
		memory_region_add_subregion(sysmem, 0xA0000000, flash);
		
		// SDRAM
		memory_region_allocate_system_memory(sdram, NULL, "SDRAM", 0x01000000);
		memory_region_add_subregion(sysmem, 0xB0000000, sdram);
	} else {
		// FLASH
	//	memory_region_allocate_system_memory(flash, NULL, "FLASH", 0x8000000 - 0x1000);
	//	memory_region_add_subregion(sysmem, 0xA0001000, flash);
		
		// FLASH IO
	//	memory_region_init_io(flash_io, NULL, &pmb8876_common_io_opts, (void *) 0xA0000000, "FLASH_IO", 0x1000);
	//	memory_region_add_subregion(sysmem, 0xA0000000, flash_io);
		
		/*
			SRAM1:0008AE1C		FLASH_TYPE <6, 0x89, 0x880F>; 1
			SRAM1:0008AE1C		FLASH_TYPE <7, 0x89, 0x880D>; 2				// работает, потом падает по EEPROM
			SRAM1:0008AE1C		FLASH_TYPE <8, 0x89, 0x8810>; 3
			SRAM1:0008AE1C		FLASH_TYPE <0xB, 0x89, 0x887E>; 4			// работает сначала, потом падает qemu
			SRAM1:0008AE1C		FLASH_TYPE <9, 0x89, 0x881C>; 5
			SRAM1:0008AE1C		FLASH_TYPE <0xA, 0x89, 0x8819>; 6
			SRAM1:0008AE1C		FLASH_TYPE <0xC, 0x20, 0x880D>; 7
			SRAM1:0008AE1C		FLASH_TYPE <0xD, 0x20, 0x8810>; 8
			SRAM1:0008AE1C		FLASH_TYPE <0xE, 0x20, 0x8819>; 9			// работает сначала, потом падает qemu
			SRAM1:0008AE1C		FLASH_TYPE <0x10, 1, 0x22 7E 22 18>; 0xA
			SRAM1:0008AE1C		FLASH_TYPE <0x12, 1, 0x22 7E 22 30>; 0xB
			SRAM1:0008AE1C		FLASH_TYPE <0x13, 1, 0x227E2223>; 0xC
			SRAM1:0008AE1C		FLASH_TYPE <0x14, 0xEC, 0x22FC0401>; 0xD
			SRAM1:0008AE1C		FLASH_TYPE <0x15, 0xEC, 0x22FD0401>; 0xE
			SRAM1:0008AE1C		FLASH_TYPE <0x16, 0x22, 0x8820>; 0xF
		*/
		DeviceState *dev = qdev_create(NULL, "cfi.pflash.0020:8819");
		qdev_prop_set_uint32(dev, "num-blocks", 0x04000000 / 0x00040000);
		qdev_prop_set_uint64(dev, "sector-length", 0x00040000);
		qdev_prop_set_uint8(dev, "width", 2);
		qdev_prop_set_bit(dev, "big-endian", false);
		qdev_prop_set_uint16(dev, "id0", 0x00);
		qdev_prop_set_uint16(dev, "id1", 0x20);
		qdev_prop_set_uint16(dev, "id2", 0x88);
		qdev_prop_set_uint16(dev, "id3", 0x19);
		qdev_prop_set_string(dev, "name", "pflash");
		
		/* OTP0 */
		uint8_t otp0[] = {
			// ESN
			0x99, 0x09, 0x29, 0x3D, 
			0x02, 0x99, 0x02, 0x28
		};
		qdev_prop_set_uint16(dev, "otp0-lock", 0x0000); /* Locked */
		qdev_prop_set_uint16(dev, "otp0-data-len", sizeof(otp0));
		qdev_prop_set_ptr(dev, "otp0-data", otp0);
		
		/* OTP1 */
		uint8_t otp1[] = {
			// IMEI
			0x53, 0x98, 0x14, 0x00, 
			0x15, 0x02, 0x44, 0xFF
		};
		qdev_prop_set_uint16(dev, "otp1-lock", 0x0000); /* All Locked */
		qdev_prop_set_uint16(dev, "otp1-data-len", sizeof(otp1));
		qdev_prop_set_ptr(dev, "otp1-data", otp1);
		
		qdev_init_nofail(dev);
		
		memory_region_add_subregion(sysmem, 0xA0000000,
			sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0));

//		pflash_cfi01_register(0xA0000000, NULL, "pflash", 0x04000000, NULL,
//			0x00040000, 0x04000000 / 0x00040000, 2, 0x00, 0x20, 0x88, 0x19, 0);
		
	//	pflash_cfi02_register(0xA0000000, NULL, "pflash", 0x04000000, NULL, 
	//		0x00040000, 0x04000000 / 0x00040000, 1, 2, 0x22, 0x7E, 0x22, 0x18, 0x555, 0xAAA, 0);
		
	//	memory_region_init_rom_device(flash, NULL, &pmb8876_flash_io_opts, (void *) flash,
	//		"flash", 64 * 1024 * 1024, &err);
	//	memory_region_add_subregion(sysmem, 0xA0000000, flash);
		
		// SDRAM
		memory_region_allocate_system_memory(sdram, NULL, "SDRAM", 0x01000000);
		memory_region_add_subregion(sysmem, 0xA8000000, sdram);
	}
    
	// test IO
	memory_region_init_io(test, NULL, &pmb8876_common_io_opts, (void *) 0xA8D95BA4, "FLASH_IO2", 4);
	memory_region_add_subregion(sysmem, 0xA8D95BA4, test);
    
	// test IO2
	memory_region_init_io(test2, NULL, &pmb8876_common_io_opts, (void *) 0xA8D95BC8, "FLASH_IO3", 4);
	memory_region_add_subregion(sysmem, 0xA8D95BC8, test2);
    
	// test IO2
	memory_region_init_io(test3, NULL, &pmb8876_common_io_opts, (void *) 0xA8FB0060, "FLASH_IO3", 4);
	memory_region_add_subregion(sysmem, 0xA8FB0060, test3);
    
    // Инициализация IRQ
	dev = qdev_create(NULL, "pmb8876-intc");
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, PMB8876_IRQ_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ));
	
	for (i = 0; i < PMB8876_IRQS_NR; i++)
		pmb8876_irq[i] = qdev_get_gpio_in(dev, i);
	
	// GSM TPU
	gsm_tpu_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, gsm_tpu_timer_handler, NULL);
	
	////////////////////////////////////////////////
	//                   BOOT                     //
	////////////////////////////////////////////////
	
	int r;
	static const unsigned char boot[] = {
		0xF1, 0x04, 0xA0, 0xE3, 0x20, 0x10, 0x90, 0xE5, 0xFF, 0x10, 0xC1, 0xE3, 0xA5, 0x10, 0x81, 0xE3, 0x20, 0x10, 0x80, 0xE5, 0x1E, 0xFF, 0x2F, 0xE1, 
		0x04, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x49, 0x45, 0x4D, 0x45, 0x4E, 0x53, 0x5F, 0x42, 0x4F, 0x4F, 0x54, 0x43, 0x4F, 0x44, 0x45, 
		0x01, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
		
		// 0x01, 0x04, 0x05, 0x00, 0x89, 0x00, 0x89 // normal mode
		0x01, 0x04, 0x05, 0x00, 0x8B, 0x00, 0x8B // service mode
		// 0x01, 0x04, 0x05, 0x80, 0x83, 0x00, 0x03 // burnin mode
	};
	
	cpu_physical_memory_write(0x82000, &boot, sizeof(boot));
	
	r = load_image_targphys("/home/azq2/dev/siemens/ff/brom.bin", 0x00400000, 0xFFFF);
	if (r < 0) {
		error_report("Failed to load firmware from brom.bin");
		exit(1);
	}
	
	if (iphone_bb) {
		r = load_image_targphys("/home/azq2/dev/siemens/IPhone2G_BB.bin", 0xA0000000, 0x4000000);
	} else {
		r = load_image_targphys("/home/azq2/mnt/ff_recalc.bin", 0xA0000000, 0x4000000);
	}
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
