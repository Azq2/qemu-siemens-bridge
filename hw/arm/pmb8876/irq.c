#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/arm/arm.h"
#include "cpu.h"
#include "hw/arm/pmb8876/irq.h"
#include "hw/arm/pmb8876/io_bridge.h"

#define TYPE_PMB8876_INTC "pmb8876-intc"

#define PMB8876_INTC_ERR(s, ...) fprintf(stderr, "[pmb8876-intc] " s, ##__VA_ARGS__);
#define PMB8876_INTC_DBG(s, ...) printf("[pmb8876-intc] " s, ##__VA_ARGS__);

#define PMB8876_INTC(obj) OBJECT_CHECK(struct pmb8876_pic, (obj), TYPE_PMB8876_INTC)

struct pmb8876_pic {
	SysBusDevice parent_obj;
	
	MemoryRegion mmio;
	qemu_irq parent_irq;
	
	uint32_t prio[PMB8876_IRQS_NR];		// Приоритеты интерраптов
	uint8_t state[PMB8876_IRQS_NR];		// Текущие интеррапты
	int current;						// Текущий интеррапт
};

static void update_irq(struct pmb8876_pic *p) {
    qemu_set_irq(p->parent_irq, p->current != -1);
}

static int get_current_irq(struct pmb8876_pic *p ) {
	int i;
	int irq_n = -1;
	int max_prio = -1;
	
	for (i = 0; i < PMB8876_IRQS_NR; ++i) {
		if (p->prio[i] && p->state[i]) {
			if (max_prio == -1 || max_prio > p->prio[i]) {
				irq_n = i;
				max_prio = p->prio[i];
			}
		}
	}
	
	return irq_n;
}

static uint64_t pic_read(void *opaque, hwaddr haddr, unsigned size) {
	struct pmb8876_pic *p = (struct pmb8876_pic *) opaque;
	ARMCPU *cpu = ARM_CPU(qemu_get_cpu(0));
	
	uint64_t addr = (uint64_t) haddr;
	uint64_t value = 0;
	
	switch (addr) {
		case 0x00: // MOD_ID
			value = 0x0031C011;
		break;
		
		case 0x08: // FIQ_ACK
		case 0x14: // IRQ_ACK
			PMB8876_INTC_ERR("FIQ_ACK not implemented! (from %08X)\n", cpu->env.regs[15]);
			value = 0;
		break;
		
		case 0x18: // FIQ_CURRENT_NUM
			PMB8876_INTC_ERR("FIQ_CURRENT_NUM not implemented! (from %08X)\n", cpu->env.regs[15]);
			value = 0;
		break;
		
		case 0x1C: // IRQ_CURRENT_NUM
			PMB8876_INTC_DBG("IRQ_CURRENT_NUM=%d\n", p->current);
			value = p->current != -1 ? p->current : 0;
		break;
		
		default:
			// Таблица приоритетов
			if (addr >= 0x30 && addr <= 0x30 + 4 * PMB8876_IRQS_NR) {
				value = p->prio[(addr - 0x30) / 4];
			} else {
				PMB8876_INTC_ERR("Unimplemented addr read: %08lX (from %08X)\n", addr, cpu->env.regs[15]);
				value = 0xFFFFFFFF;
			}
		break;
	}
	
	unsigned int from_phone = sie_bridge_read((uint32_t) (PMB8876_IRQ_BASE + addr), cpu->env.regs[15]);
	if (from_phone != value && addr != 0x1C) {
		PMB8876_INTC_ERR("%08lX [irq=%02X]: Not sync: %08lX != %08X (from %08X)\n", addr + PMB8876_IRQ_BASE, (addr - 0x30) / 4, value, from_phone, cpu->env.regs[15]);
		value = from_phone;
	}
}

static void pic_write(void *opaque, hwaddr haddr, uint64_t val, unsigned size) {
	struct pmb8876_pic *p = (struct pmb8876_pic *) opaque;
	ARMCPU *cpu = ARM_CPU(qemu_get_cpu(0));
	uint64_t addr = (uint64_t) haddr;
	
	sie_bridge_write((uint32_t) (PMB8876_IRQ_BASE + addr), (uint32_t) val, cpu->env.regs[15]);
	
	switch (addr) {
		case 0x08: // FIQ_ACK
			PMB8876_INTC_ERR("FIQ_ACK not implemented! (from %08X)\n", cpu->env.regs[15]);
		break;
		
		case 0x14: // IRQ_ACK
			if (p->current != -1) { // Получаем текущий интеррапт и сбрасываем его
				p->state[p->current] = 0;
				PMB8876_INTC_DBG("ack irq %02X (prio: %d) (from %08X)\n", p->current, p->prio[p->current], cpu->env.regs[15]);
				p->current = get_current_irq(p);
			} else {
				PMB8876_INTC_ERR("IRQ_ACK without active irq (from %08X)\n", cpu->env.regs[15]);
			}
			PMB8876_INTC_DBG("p->current=%d\n", p->current);
		break;
		
		case 0x18: // FIQ_CURRENT_NUM
			PMB8876_INTC_ERR("Unexpected write to FIQ_CURRENT_NUM (from %08X)\n", cpu->env.regs[15]);
		break;
		
		case 0x1C: // IRQ_CURRENT_NUM
			PMB8876_INTC_ERR("Unexpected write to IRQ_CURRENT_NUM (from %08X)\n", cpu->env.regs[15]);
		break;
		
		default:
			// Таблица приоритетов
			if (addr >= 0x30 && addr <= 0x30 + 4 * PMB8876_IRQS_NR) {
				PMB8876_INTC_DBG("irq %02lX prio: %ld (from %08X)\n", (addr - 0x30) / 4, val, cpu->env.regs[15]);
				p->prio[(addr - 0x30) / 4] = val;
			} else {
				PMB8876_INTC_ERR("Unimplemented addr write: %08lX=%08lX (from %08X)\n", addr, val, cpu->env.regs[15]);
			}
		break;
	}
	update_irq(p);
}

static const MemoryRegionOps pic_ops = {
	.read = pic_read,
	.write = pic_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
	.valid = {
		.min_access_size = 4,
		.max_access_size = 4
	}
};

static void irq_handler(void *opaque, int irq, int level) {
	struct pmb8876_pic *p = (struct pmb8876_pic *) opaque;
	
	if (irq >= 0 && irq < PMB8876_IRQS_NR) {
		if (!p->state[irq]) {
			p->state[irq] = 1;
			p->current = get_current_irq(p);
		}
	} else {
		ARMCPU *cpu = ARM_CPU(qemu_get_cpu(0));
		PMB8876_INTC_ERR("Unknown interrupt: %02X (from %08X)\n", irq, cpu->env.regs[15]);
	}
	
	update_irq(p);
}

static void pmb8876_intc_init(Object *obj) {
	struct pmb8876_pic *p = PMB8876_INTC(obj);
	
	p->current = -1;
	
	qdev_init_gpio_in(DEVICE(obj), irq_handler, PMB8876_IRQS_NR);
	sysbus_init_irq(SYS_BUS_DEVICE(obj), &p->parent_irq);
	
	memory_region_init_io(&p->mmio, obj, &pic_ops, p, "pmb8876-intc", 0x30 + PMB8876_IRQS_NR * 4);
	sysbus_init_mmio(SYS_BUS_DEVICE(obj), &p->mmio);
}

static Property pmb8876_intc_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void pmb8876_intc_class_init(ObjectClass *klass, void *data) {
	DeviceClass *dc = DEVICE_CLASS(klass);
	dc->props = pmb8876_intc_properties;
}

static const TypeInfo pmb8876_intc_info = {
    .name          	= TYPE_PMB8876_INTC,
    .parent        	= TYPE_SYS_BUS_DEVICE,
    .instance_size 	= sizeof(struct pmb8876_pic),
    .instance_init 	= pmb8876_intc_init,
    .class_init    	= pmb8876_intc_class_init,
};

static void pmb8876_intc_register_types(void) {
	type_register_static(&pmb8876_intc_info);
}
type_init(pmb8876_intc_register_types)
