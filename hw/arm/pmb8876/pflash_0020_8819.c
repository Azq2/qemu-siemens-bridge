/*
 *  CFI parallel flash with ST command set emulation (0020:8819)
 *
 *  Copyright (c) 2006 Thorsten Zitterell
 *  Copyright (c) 2005 Jocelyn Mayer
 *  Copyright (c) 2017 Kiril Zhumarin
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * For now, this code can emulate flashes of 1, 2 or 4 bytes width.
 * Supported commands/modes are:
 * - flash read
 * - flash write
 * - flash OTP read
 * - flash ID read
 * - sector erase
 * - CFI queries
 *
 * It does not support timings
 * It does not support flash interleaving
 * It does not implement software data protection as found in many real chips
 * It does not implement erase suspend/resume commands
 * It does not implement multiple sectors erase
 *
 * It does not implement much more ...
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/block/flash.h"
#include "sysemu/block-backend.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "qemu/bitops.h"
#include "exec/address-spaces.h"
#include "qemu/host-utils.h"
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"

#define PFLASH_BUG(fmt, ...) \
do { \
	fprintf(stderr, "PFLASH: Possible BUG - " fmt, ## __VA_ARGS__); \
	exit(1); \
} while(0)

#define PFLASH_DEBUG
#ifdef PFLASH_DEBUG
#define DPRINTF(fmt, ...)									\
do {														\
	fprintf(stderr, "PFLASH: " fmt , ## __VA_ARGS__);		\
} while (0)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

#define CFI_PFLASH01(obj) OBJECT_CHECK(pflash_t, (obj), "cfi.pflash.0020:8819")

#define PFLASH_BE		  0
#define PFLASH_SECURE	  1

struct pflash_t {
	/*< private >*/
	SysBusDevice parent_obj;
	/*< public >*/

	BlockBackend *blk;
	uint32_t nb_blocs;
	uint64_t sector_len;
	uint8_t bank_width;
	uint32_t features;
	uint8_t wcycle; /* if 0, the flash is read normally */
	int ro;
	uint8_t cmd;
	uint8_t status;
	uint16_t ident0;
	uint16_t ident1;
	uint16_t ident2;
	uint16_t ident3;
	uint16_t cfi_adr;
	uint16_t cfi_len;
	uint8_t cfi_table[0x32];
	uint16_t pri_adr;
	uint16_t pri_len;
	uint8_t pri_table[0x50];
	struct {
		uint16_t lock;
		uint16_t adr;
		uint16_t len;
		uint8_t data[16];
		uint16_t init_data_len;
		void *init_data;
	} otp0;
	struct {
		uint16_t lock;
		uint16_t adr;
		uint16_t len;
		uint8_t data[0xFF];
		uint16_t init_data_len;
		void *init_data;
	} otp1;
	uint16_t otp_adr;
	uint16_t otp_len;
	uint64_t counter;
	unsigned int writeblock_size;
	QEMUTimer *timer;
	MemoryRegion mem;
	char *name;
	void *storage;
	VMChangeStateEntry *vmstate;
};

static int pflash_post_load(void *opaque, int version_id);

static const VMStateDescription vmstate_pflash = {
	.name = "pflash_0020_8819",
	.version_id = 1,
	.minimum_version_id = 1,
	.post_load = pflash_post_load,
	.fields = (VMStateField[]) {
		VMSTATE_UINT8(wcycle, pflash_t),
		VMSTATE_UINT8(cmd, pflash_t),
		VMSTATE_UINT8(status, pflash_t),
		VMSTATE_UINT64(counter, pflash_t),
		VMSTATE_END_OF_LIST()
	}
};

static void pflash_timer (void *opaque)
{
	pflash_t *pfl = opaque;

	DPRINTF("%s: command %02x done\n", __func__, pfl->cmd);
	/* Reset flash */
	pfl->status ^= 0x80;
	memory_region_rom_device_set_romd(&pfl->mem, true);
	pfl->wcycle = 0;
	pfl->cmd = 0;
}

/* Perform a CFI query */
static uint32_t pflash_cfi_query(pflash_t *pfl, uint32_t boff)
{
	uint32_t resp;
	
	if (boff >= pfl->cfi_adr && boff < pfl->cfi_adr + pfl->cfi_len) {
		/* CFI Table */
		resp = pfl->cfi_table[boff];
		DPRINTF("%s: CFI reg %04x: %04x\n", __func__, boff, resp);
	} else if (boff >= pfl->pri_adr && boff < pfl->pri_adr + pfl->pri_len) {
		/* PRI Table */
		resp = pfl->pri_table[boff - pfl->pri_adr];
		DPRINTF("%s: PRI reg %04x: %04x\n", __func__, boff, resp);
	} else if (boff >= pfl->otp0.adr && boff < pfl->otp0.adr + pfl->otp0.len) {
		/* User Programmable OTP 0 */
		uint32_t offset = (boff - pfl->otp0.adr) * 2;
		resp = pfl->otp0.data[offset + 1] << 8 | pfl->otp0.data[offset];
		DPRINTF("%s: OTP0 reg %04x: %04x\n", __func__, boff, resp);
	} else if (boff >= pfl->otp1.adr && boff < pfl->otp1.adr + pfl->otp1.len) {
		/* User Programmable OTP 1 */
		uint32_t offset = (boff - pfl->otp1.adr) * 2;
		resp = pfl->otp1.data[offset + 1] << 8 | pfl->otp1.data[offset];
		DPRINTF("%s: OTP1 reg %04x: %04x\n", __func__, boff, resp);
	} else {
		switch (boff) {
			case 0x00:
				resp = pfl->ident0 << 8 | pfl->ident1;
				DPRINTF("%s: Manufacturer Code %04x\n", __func__, resp);
			break;
			
			case 0x01:
				resp = pfl->ident2 << 8 | pfl->ident3;
				DPRINTF("%s: Device ID Code %04x\n", __func__, resp);
			break;
			
			case 0x02:
				resp = 0x0011;
				DPRINTF("%s: Lock Status %04x\n", __func__, resp);
			break;
			
			case 0x05:
				resp = 0xF907;
				DPRINTF("%s: Configuration Register %04x\n", __func__, resp);
			break;
			
			case 0x06:
				resp = 0x0004;
				DPRINTF("%s: Enhanced Configuration Register %04x\n", __func__, resp);
			break;
			
			case 0x80:
				resp = pfl->otp0.lock;
				DPRINTF("%s: Protection Register PR0 Lock %04x\n", __func__, resp);
			break;
			
			case 0x89:
				resp = pfl->otp1.lock;
				DPRINTF("%s: Protection Register PR1-PR16 Lock %04x\n", __func__, resp);
			break;
			
			default:
				DPRINTF("%s: Unsupported %s offset: 0x%02X\n", __func__, 
					(pfl->cmd == 0x90 ? "Device Information" : "CFI"), boff);
				resp = 0xFF;
			break;
		}
	}
	
	return resp;
}

static uint32_t pflash_data_read(pflash_t *pfl, hwaddr offset,
								 int width, int be)
{
	uint8_t *p;
	uint32_t ret;

	p = pfl->storage;
	switch (width) {
	case 1:
		ret = p[offset];
		DPRINTF("%s: data offset " TARGET_FMT_plx " %02x\n",
				__func__, offset, ret);
		break;
	case 2:
		if (be) {
			ret = p[offset] << 8;
			ret |= p[offset + 1];
		} else {
			ret = p[offset];
			ret |= p[offset + 1] << 8;
		}
		DPRINTF("%s: data offset " TARGET_FMT_plx " %04x\n",
				__func__, offset, ret);
		break;
	case 4:
		if (be) {
			ret = p[offset] << 24;
			ret |= p[offset + 1] << 16;
			ret |= p[offset + 2] << 8;
			ret |= p[offset + 3];
		} else {
			ret = p[offset];
			ret |= p[offset + 1] << 8;
			ret |= p[offset + 2] << 16;
			ret |= p[offset + 3] << 24;
		}
		DPRINTF("%s: data offset " TARGET_FMT_plx " %08x\n",
				__func__, offset, ret);
		break;
	default:
		DPRINTF("BUG in %s\n", __func__);
		abort();
	}
	return ret;
}

static uint32_t pflash_read (pflash_t *pfl, hwaddr offset,
							 int width, int be)
{
	uint32_t ret, bank_width_shift = 0;

	ret = -1;

	switch (pfl->cmd) {
	default:
		/* This should never happen : reset state & treat it as a read */
		DPRINTF("%s: unknown command state: %x\n", __func__, pfl->cmd);
		pfl->wcycle = 0;
		pfl->cmd = 0;
		/* fall through to read code */
	case 0x00:
		/* Flash area read */
		ret = pflash_data_read(pfl, offset, width, be);
		break;
	case 0x10: /* Single byte program */
	case 0x20: /* Block erase */
	case 0x28: /* Block erase */
	case 0x40: /* single byte program */
	case 0x41: /* single word program */
	case 0x50: /* Clear status register */
	case 0x60: /* Block /un)lock */
	case 0x70: /* Status Register */
	case 0xe8: /* Write block */
	case 0xe9: /* Write block */
		/* Status register read.  Return status from each device in
		 * bank.
		 */
		ret = pfl->status;
		
		if (pfl->cmd == 0x70) {
			pfl->wcycle = 0;
			pfl->cmd = 0;
			memory_region_rom_device_set_romd(&pfl->mem, true);
		}
		
		DPRINTF("%s: status %x\n", __func__, ret);
		break;
	case 0x90: /* Device Information */
	case 0x98: /* Query mode */
		if (pfl->bank_width == 2)
			bank_width_shift = 1;
		else if (pfl->bank_width == 4)
			bank_width_shift = 2;
		
		// get cfi values
		ret = 0;
		for (uint32_t i = 0; i < width; i += pfl->bank_width) {
			uint32_t cfi_id = (offset + i) >> bank_width_shift;
			ret |= pflash_cfi_query(pfl, cfi_id) << (i * 8);
		}
		
		// shift byte offset
		if (bank_width_shift)
			ret >>= ((offset - ((offset >> bank_width_shift) << bank_width_shift)) * 8);
		
		// mask
		ret &= (1 << (width * 8)) - 1;
		
		DPRINTF("\nOTP[%08lX, %d] = %08X\n\n", offset, width, ret);
		break;
	}
#if 0
	DPRINTF("%s: reading offset " TARGET_FMT_plx " under cmd %02x width %d (%04X)\n",
			__func__, offset, pfl->cmd, width, ret);
#endif
	return ret;
}

/* update flash content on disk */
static void pflash_update(pflash_t *pfl, int offset, int size)
{
	int offset_end;
	if (pfl->blk) {
		offset_end = offset + size;
		/* widen to sector boundaries */
		offset = QEMU_ALIGN_DOWN(offset, BDRV_SECTOR_SIZE);
		offset_end = QEMU_ALIGN_UP(offset_end, BDRV_SECTOR_SIZE);
		blk_pwrite(pfl->blk, offset, pfl->storage + offset,
					offset_end - offset, 0);
	}
}

static inline void pflash_data_write(pflash_t *pfl, hwaddr offset,
									 uint32_t value, int width, int be)
{
	uint8_t *p = pfl->storage;

	DPRINTF("%s: block write offset " TARGET_FMT_plx
			" value %x counter %016" PRIx64 "\n",
			__func__, offset, value, pfl->counter);
	switch (width) {
	case 1:
		p[offset] = value;
		break;
	case 2:
		if (be) {
			p[offset] = value >> 8;
			p[offset + 1] = value;
		} else {
			p[offset] = value;
			p[offset + 1] = value >> 8;
		}
		break;
	case 4:
		if (be) {
			p[offset] = value >> 24;
			p[offset + 1] = value >> 16;
			p[offset + 2] = value >> 8;
			p[offset + 3] = value;
		} else {
			p[offset] = value;
			p[offset + 1] = value >> 8;
			p[offset + 2] = value >> 16;
			p[offset + 3] = value >> 24;
		}
		break;
	}

}

static void pflash_write(pflash_t *pfl, hwaddr offset,
						 uint32_t value, int width, int be)
{
	uint8_t *p;
	uint8_t cmd;

	cmd = value;
#if 0
	DPRINTF("%s: writing offset " TARGET_FMT_plx " value %08x width %d wcycle 0x%x\n",
			__func__, offset, value, width, pfl->wcycle);
#endif
	if (!pfl->wcycle) {
		/* Set the device in I/O access mode */
		memory_region_rom_device_set_romd(&pfl->mem, false);
	}

	switch (pfl->wcycle) {
	case 0:
		/* read mode */
		switch (cmd) {
		case 0x00: /* ??? */
			goto reset_flash;
		case 0x10: /* Single Byte Program */
		case 0x40: /* Single Byte Program */
		case 0x41: /* Single Word Program */
			DPRINTF("%s: Single Word Program\n", __func__);
			break;
		case 0x20: /* Block erase */
			p = pfl->storage;
			offset &= ~(pfl->sector_len - 1);

			DPRINTF("%s: block erase at " TARGET_FMT_plx " bytes %x\n",
					__func__, offset, (unsigned)pfl->sector_len);

			if (!pfl->ro) {
				memset(p + offset, 0xff, pfl->sector_len);
				pflash_update(pfl, offset, pfl->sector_len);
			} else {
				pfl->status |= 0x20; /* Block erase error */
			}
			pfl->status |= 0x80; /* Ready! */
			break;
		case 0x50: /* Clear status bits */
			DPRINTF("%s: Clear status bits\n", __func__);
			pfl->status = 0x0;
			goto reset_flash;
		case 0x60: /* Block (un)lock */
			DPRINTF("%s: Block unlock\n", __func__);
			break;
		case 0x70: /* Status Register */
			DPRINTF("%s: Read status register\n", __func__);
			pfl->cmd = cmd;
			return;
		case 0x90: /* Read Device ID */
			DPRINTF("%s: Read Device information\n", __func__);
			pfl->cmd = cmd;
			return;
		case 0x98: /* CFI query */
			DPRINTF("%s: CFI query\n", __func__);
			break;
		case 0xe8: /* Write to buffer */
		case 0xe9: /* Write to buffer */
			DPRINTF("%s: Write to buffer\n", __func__);
			pfl->status |= 0x80; /* Ready! */
			break;
		case 0xf0: /* Probe for AMD flash */
			DPRINTF("%s: Probe for AMD flash\n", __func__);
			goto reset_flash;
		case 0xff: /* Read array mode */
			DPRINTF("%s: Read array mode\n", __func__);
			goto reset_flash;
		default:
			goto error_flash;
		}
		pfl->wcycle++;
		pfl->cmd = cmd;
		break;
	case 1:
		switch (pfl->cmd) {
		case 0x10: /* Single Byte Program */
		case 0x40: /* Single Byte Program */
		case 0x41: /* Single Word Program */
			DPRINTF("%s: Single Byte Program\n", __func__);
			if (!pfl->ro) {
				pflash_data_write(pfl, offset, value, width, be);
				pflash_update(pfl, offset, width);
			} else {
				pfl->status |= 0x10; /* Programming error */
			}
			pfl->status |= 0x80; /* Ready! */
			pfl->wcycle = 0;
		break;
		case 0x20: /* Block erase */
		case 0x28:
			if (cmd == 0xd0) { /* confirm */
				pfl->wcycle = 0;
				pfl->status |= 0x80;
			} else if (cmd == 0xff) { /* read array mode */
				goto reset_flash;
			} else
				goto error_flash;

			break;
		case 0xe8:
		case 0xe9:
			/* Mask writeblock size based on device width, or bank width if
			 * device width not specified.
			 */
			value = extract32(value, 0, pfl->bank_width * 8);
			DPRINTF("%s: block write of %x bytes\n", __func__, value);
			pfl->counter = value + 1;
			pfl->wcycle++;
			break;
		case 0x60:
			if (cmd == 0xd0) {
				pfl->wcycle = 0;
				pfl->status |= 0x80;
			} else if (cmd == 0x01) {
				pfl->wcycle = 0;
				pfl->status |= 0x80;
			} else if (cmd == 0xff) {
				DPRINTF("%s: Set Configuration Register\n", __func__);
				goto reset_flash;
			} else if (cmd == 0x03) {
				goto reset_flash;
			} else {
				DPRINTF("%s: Unknown (un)locking command %02X\n", __func__, cmd);
				goto reset_flash;
			}
			break;
		case 0x98:
			if (cmd == 0xff) {
				goto reset_flash;
			} else {
				DPRINTF("%s: leaving query mode\n", __func__);
			}
			break;
		default:
			goto error_flash;
		}
		break;
	case 2:
		switch (pfl->cmd) {
		case 0xe8: /* Block write */
			if (!pfl->ro) {
				pflash_data_write(pfl, offset, value, width, be);
			} else {
				pfl->status |= 0x10; /* Programming error */
			}

			pfl->status |= 0x80;

			if (!pfl->counter) {
				hwaddr mask = pfl->writeblock_size - 1;
				mask = ~mask;

				DPRINTF("%s: block write finished\n", __func__);
				pfl->wcycle++;
				if (!pfl->ro) {
					/* Flush the entire write buffer onto backing storage.  */
					pflash_update(pfl, offset & mask, pfl->writeblock_size);
				} else {
					pfl->status |= 0x10; /* Programming error */
				}
			}

			pfl->counter--;
			break;
		case 0xe9: /* Block write */
			if (!pfl->ro) {
				pflash_data_write(pfl, offset, value, width, be);
			} else {
				pfl->status |= 0x10; /* Programming error */
			}

			pfl->status |= 0x80;
			pfl->counter -= width / pfl->bank_width;
			
			if (!pfl->counter) {
				hwaddr mask = pfl->writeblock_size - 1;
				mask = ~mask;

				DPRINTF("%s: block write finished\n", __func__);
				pfl->wcycle++;
				if (!pfl->ro) {
					/* Flush the entire write buffer onto backing storage.  */
					pflash_update(pfl, offset & mask, pfl->writeblock_size);
				} else {
					pfl->status |= 0x10; /* Programming error */
				}
			}

			break;
		default:
			goto error_flash;
		}
		break;
	case 3: /* Confirm mode */
		switch (pfl->cmd) {
		case 0xe8: /* Block write */
			if (cmd == 0xd0) {
				pfl->wcycle = 0;
				pfl->status |= 0x80;
			} else {
				DPRINTF("%s: unknown command for \"write block\"\n", __func__);
				PFLASH_BUG("Write block confirm");
				goto reset_flash;
			}
			break;
		case 0xe9: /* Block write */
			if (cmd == 0xd0) {
				pfl->wcycle = 0;
				pfl->status |= 0x80;
			} else {
				DPRINTF("%s: unknown command for \"write block\"\n", __func__);
				PFLASH_BUG("Write block confirm");
				goto reset_flash;
			}
			goto reset_flash;
		default:
			goto error_flash;
		}
		break;
	default:
		/* Should never happen */
		DPRINTF("%s: invalid write state\n",  __func__);
		goto reset_flash;
	}
	return;

 error_flash:
	fprintf(stderr, "%s: Unimplemented flash cmd sequence "
				  "(offset " TARGET_FMT_plx ", wcycle 0x%x cmd 0x%x value 0x%x)"
				  "\n", __func__, offset, pfl->wcycle, pfl->cmd, value);
	exit(1);
//	qemu_log_mask(LOG_UNIMP, "%s: Unimplemented flash cmd sequence "
//				  "(offset " TARGET_FMT_plx ", wcycle 0x%x cmd 0x%x value 0x%x)"
//				  "\n", __func__, offset, pfl->wcycle, pfl->cmd, value);

 reset_flash:
	memory_region_rom_device_set_romd(&pfl->mem, true);

	pfl->wcycle = 0;
	pfl->cmd = 0;
}


static MemTxResult pflash_mem_read_with_attrs(void *opaque, hwaddr addr, uint64_t *value,
											  unsigned len, MemTxAttrs attrs)
{
	pflash_t *pfl = opaque;
	bool be = !!(pfl->features & (1 << PFLASH_BE));

	if ((pfl->features & (1 << PFLASH_SECURE)) && !attrs.secure) {
		*value = pflash_data_read(opaque, addr, len, be);
	} else {
		*value = pflash_read(opaque, addr, len, be);
	}
	return MEMTX_OK;
}

static MemTxResult pflash_mem_write_with_attrs(void *opaque, hwaddr addr, uint64_t value,
												unsigned len, MemTxAttrs attrs)
{
	pflash_t *pfl = opaque;
	bool be = !!(pfl->features & (1 << PFLASH_BE));

	if ((pfl->features & (1 << PFLASH_SECURE)) && !attrs.secure) {
		return MEMTX_ERROR;
	} else {
		pflash_write(opaque, addr, value, len, be);
		return MEMTX_OK;
	}
}

static const MemoryRegionOps pflash_0020_8819_ops = {
	.read_with_attrs = pflash_mem_read_with_attrs,
	.write_with_attrs = pflash_mem_write_with_attrs,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static void pflash_0020_8819_realize(DeviceState *dev, Error **errp)
{
	pflash_t *pfl = CFI_PFLASH01(dev);
	uint64_t total_len;
	int ret;
	uint64_t blocks_per_device, device_len;
	Error *local_err = NULL;

	total_len = pfl->sector_len * pfl->nb_blocs;

	/* These are only used to expose the parameters of each device
	 * in the cfi_table[].
	 */
	blocks_per_device = pfl->nb_blocs;
	device_len = pfl->sector_len * blocks_per_device;
	
	memory_region_init_rom_device(
		&pfl->mem, OBJECT(dev),
		&pflash_0020_8819_ops,
		pfl,
		pfl->name, total_len, &local_err);
	if (local_err) {
		error_propagate(errp, local_err);
		return;
	}

	vmstate_register_ram(&pfl->mem, DEVICE(pfl));
	pfl->storage = memory_region_get_ram_ptr(&pfl->mem);
	sysbus_init_mmio(SYS_BUS_DEVICE(dev), &pfl->mem);

	if (pfl->blk) {
		/* read the initial flash content */
		ret = blk_pread(pfl->blk, 0, pfl->storage, total_len);

		if (ret < 0) {
			vmstate_unregister_ram(&pfl->mem, DEVICE(pfl));
			error_setg(errp, "failed to read the initial flash content");
			return;
		}
	}

	if (pfl->blk) {
		pfl->ro = blk_is_read_only(pfl->blk);
	} else {
		pfl->ro = 0;
	}
	
	pfl->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, pflash_timer, pfl);
	pfl->wcycle = 0;
	pfl->cmd = 0;
	pfl->status = 0;
	/* Hardcoded CFI */
	pfl->cfi_adr = 0x10;
	pfl->cfi_len = sizeof(pfl->cfi_table);
	memset(&pfl->cfi_table, 0xFF, pfl->cfi_len);
	/* Standard "QRY" string */
	pfl->cfi_table[0x10] = 'Q';
	pfl->cfi_table[0x11] = 'R';
	pfl->cfi_table[0x12] = 'Y';
	/* Command set */
	pfl->cfi_table[0x13] = 0x00;
	pfl->cfi_table[0x14] = 0x02;
	/* Primary extended table address (none) */
	pfl->cfi_table[0x15] = 0x0A;
	pfl->cfi_table[0x16] = 0x01;
	/* Alternate command set (none) */
	pfl->cfi_table[0x17] = 0x00;
	pfl->cfi_table[0x18] = 0x00;
	/* Alternate extended table (none) */
	pfl->cfi_table[0x19] = 0x00;
	pfl->cfi_table[0x1A] = 0x00;
	/* Vcc min */
	pfl->cfi_table[0x1B] = 0x17; /* 1.7V */
	/* Vcc max */
	pfl->cfi_table[0x1C] = 0x20; /* 2.0V */
	/* Vpp min */
	pfl->cfi_table[0x1D] = 0x85; /* 8.5V */
	/* Vpp max */
	pfl->cfi_table[0x1E] = 0x95; /* 9.5V */
	/* Timeout for single word/byte write */
	pfl->cfi_table[0x1F] = 0x06;
	/* Timeout for min size buffer write */
	pfl->cfi_table[0x20] = 0x0B;
	/* Typical timeout for block erase */
	pfl->cfi_table[0x21] = 0x0A;
	/* Typical timeout for full chip erase */
	pfl->cfi_table[0x22] = 0x00;
	/* Max timeout for single word/byte write */
	pfl->cfi_table[0x23] = 0x02;
	/* Max timeout for buffer write */
	pfl->cfi_table[0x24] = 0x02;
	/* Max timeout for block erase */
	pfl->cfi_table[0x25] = 0x02;
	/* Max timeout for chip erase */
	pfl->cfi_table[0x26] = 0x00;
	/* Device size */
	pfl->cfi_table[0x27] = ctz32(device_len); /* + 1; */
	/* Flash device interface (8 & 16 bits) */
	pfl->cfi_table[0x28] = 0x01;
	pfl->cfi_table[0x29] = 0x00;
	/* Max number of bytes in multi-bytes write */
	if (pfl->bank_width == 1) {
		pfl->cfi_table[0x2A] = 0x08;
		pfl->cfi_table[0x2B] = 0x00;
	} else {
		pfl->cfi_table[0x2A] = 0x0A;
		pfl->cfi_table[0x2B] = 0x00;
	}
	pfl->writeblock_size = 1 << pfl->cfi_table[0x2A];

	/* Number of erase block regions (uniform) */
	pfl->cfi_table[0x2C] = 0x01;
	/* Erase block region 1 */
	pfl->cfi_table[0x2D] = blocks_per_device - 1;
	pfl->cfi_table[0x2E] = (blocks_per_device - 1) >> 8;
	pfl->cfi_table[0x2F] = pfl->sector_len >> 8;
	pfl->cfi_table[0x30] = pfl->sector_len >> 16;
	
	/* Hardcoded PRI table */
	pfl->pri_adr = (pfl->cfi_table[0x16] << 8) | pfl->cfi_table[0x15];
	pfl->pri_len = sizeof(pfl->pri_table);
	memset(&pfl->pri_table, 0xFF, pfl->pri_len);
	/* PRI */
	pfl->pri_table[0x00] = 'P';
	pfl->pri_table[0x01] = 'R';
	pfl->pri_table[0x02] = 'I';
	/* Major version */
	pfl->pri_table[0x03] = '1';
	/* Minor version */
	pfl->pri_table[0x04] = '4';
	/* Feature Support */
	pfl->pri_table[0x05] = 0xE6;
	pfl->pri_table[0x06] = 0x07;
	pfl->pri_table[0x07] = 0x00;
	pfl->pri_table[0x08] = 0x00;
	/* Suspend Cmd Support */
	pfl->pri_table[0x09] = 0x01;
	/* Block Protect Status */
	pfl->pri_table[0x0A] = 0x33;
	pfl->pri_table[0x0B] = 0x00;
	/* VDD Logic Supply Optimum Program/Erase voltage */
	pfl->pri_table[0x0C] = 0x18; /* 1.8V */
	/* VPP Supply Optimum Program/Erase voltage */
	pfl->pri_table[0x0D] = 0x90; /* 9V */
	/* Number of protection register fields */
	pfl->pri_table[0x0E] = 0x02;
	/* Protection Field 1: Protection Description */
	pfl->pri_table[0x0F] = 0x80;
	pfl->pri_table[0x10] = 0x00;
	pfl->pri_table[0x11] = 0x03;
	pfl->pri_table[0x12] = 0x03;
	/* Protection Register 2: Protection Description */
	pfl->pri_table[0x13] = 0x89;
	pfl->pri_table[0x14] = 0x00;
	pfl->pri_table[0x15] = 0x00;
	pfl->pri_table[0x16] = 0x00;
	pfl->pri_table[0x17] = 0x00;
	pfl->pri_table[0x18] = 0x00;
	pfl->pri_table[0x19] = 0x00;
	pfl->pri_table[0x1A] = 0x10;
	pfl->pri_table[0x1B] = 0x00;
	pfl->pri_table[0x1C] = 0x04;
	/* Page-mode read capability */
	pfl->pri_table[0x1D] = 0x05;
	/* Number of synchronous mode read configuration fields that follow. */
	pfl->pri_table[0x1E] = 0x03;
	/* Synchronous mode read capability configuration 1 */
	pfl->pri_table[0x1F] = 0x02;
	/* Synchronous mode read capability configuration 2 */
	pfl->pri_table[0x20] = 0x03;
	/* Synchronous mode read capability configuration 3 */
	pfl->pri_table[0x21] = 0x07;
	/* Number of Bank Regions within the device */
	pfl->pri_table[0x22] = 0x01;
	/* Data size of this Bank Region Information Section */
	pfl->pri_table[0x23] = 0x16;
	pfl->pri_table[0x24] = 0x00;
	/* Number of identical banks within Bank Region 1 */
	pfl->pri_table[0x25] = 0x08;
	pfl->pri_table[0x26] = 0x00;
	/* Number of program or erase operations allowed in Bank Region 1 */
	pfl->pri_table[0x27] = 0x11;
	/* Number of program or erase operations allowed in other banks while a bank in this region is being erased */
	pfl->pri_table[0x28] = 0x00;
	/* Number of program or erase operations allowed in other banks while a bank in this region is being erased */
	pfl->pri_table[0x29] = 0x00;
	/* Types of erase block regions in Bank Region 1 */
	pfl->pri_table[0x2A] = 0x01;
	/* Bank Region 1 Erase Block Type 1 Information */
	pfl->pri_table[0x2B] = 0x1F;
	pfl->pri_table[0x2C] = 0x00;
	pfl->pri_table[0x2D] = 0x00;
	pfl->pri_table[0x2E] = 0x04;
	/* Bank Region 1 (Erase Block Type 1) */
	pfl->pri_table[0x2F] = 0x64;
	pfl->pri_table[0x30] = 0x00;
	/* Bank Region 1 (Erase Block Type 1): BIts per cell, internal ECC */
	pfl->pri_table[0x31] = 0x12;
	/* Bank Region 1 (Erase Block Type 1): Page mode and Synchronous mode capabilities */
	pfl->pri_table[0x32] = 0x03;
	/* Bank Region 1 (Erase Block Type 1) Programming Region Information */
	pfl->pri_table[0x33] = 0xA0;
	pfl->pri_table[0x34] = 0x00;
	pfl->pri_table[0x35] = 0x10;
	pfl->pri_table[0x36] = 0x00;
	pfl->pri_table[0x37] = 0x10;
	pfl->pri_table[0x38] = 0x00;
	/* Number of Bank Regions within the device */
	pfl->pri_table[0x39] = 0x01;
	/* Data size of this Bank Region Information Section */
	pfl->pri_table[0x3A] = 0x16;
	pfl->pri_table[0x3B] = 0x00;
	/* Number of identical banks within Bank Region 1 */
	pfl->pri_table[0x3C] = 0x01;
	pfl->pri_table[0x3D] = 0x00;
	/* Number of program or erase operations allowed in Bank Region 1 */
	pfl->pri_table[0x3E] = 0x11;
	/* Number of program or erase operations allowed in other banks while a bank in this region is being erased */
	pfl->pri_table[0x3F] = 0x00;
	/* Number of program or erase operations allowed in other banks while a bank in this region is being erased */
	pfl->pri_table[0x40] = 0x00;
	/* Types of erase block regions in Bank Region 1 */
	pfl->pri_table[0x41] = 0x01;
	/* "Bank Region 1 Erase Block Type 1 Information */
	pfl->pri_table[0x42] = 0x03;
	pfl->pri_table[0x43] = 0x00;
	pfl->pri_table[0x44] = 0x20;
	pfl->pri_table[0x45] = 0x00;
	/* Bank Region 1 (Erase Block Type 1) */
	pfl->pri_table[0x46] = 0x64;
	pfl->pri_table[0x47] = 0x00;
	/* Bank Region 1 (Erase Block Type 1): BIts per cell, internal ECC */
	pfl->pri_table[0x48] = 0x01;
	/* Bank Region 1 (Erase Block Type 1): Page mode and Synchronous mode capabilities */
	pfl->pri_table[0x49] = 0x03;
	/* Bank Region 1 (Erase Block Type 1) Programming Region Information */
	pfl->pri_table[0x4A] = 0x00;
	pfl->pri_table[0x4B] = 0x80;
	pfl->pri_table[0x4C] = 0x00;
	pfl->pri_table[0x4D] = 0x00;
	pfl->pri_table[0x4E] = 0x00;
	pfl->pri_table[0x4F] = 0x80;
	
	memset(&pfl->otp0.data, 0xFF, sizeof(pfl->otp0.data));
	memset(&pfl->otp1.data, 0xFF, sizeof(pfl->otp1.data));
	
	pfl->otp0.adr	= 0x81;
	pfl->otp0.len	= sizeof(pfl->otp0.data) / 2;
	
	pfl->otp1.adr	= 0x8A;
	pfl->otp1.len	= sizeof(pfl->otp1.data) / 2;
	
	if (pfl->otp0.init_data) {
		if (pfl->otp0.init_data_len > pfl->otp0.len)
			pfl->otp0.init_data_len = pfl->otp0.len;
		memcpy(pfl->otp0.data, pfl->otp0.init_data, pfl->otp0.init_data_len);
	}
	
	if (pfl->otp1.init_data) {
		if (pfl->otp1.init_data_len > pfl->otp1.len)
			pfl->otp1.init_data_len = pfl->otp1.len;
		memcpy(pfl->otp1.data, pfl->otp1.init_data, pfl->otp1.init_data_len);
	}
}

static Property pflash_0020_8819_properties[] = {
	DEFINE_PROP_DRIVE("drive", struct pflash_t, blk),
	/* num-blocks is the number of blocks actually visible to the guest,
	 * ie the total size of the device divided by the sector length.
	 * If we're emulating flash devices wired in parallel the actual
	 * number of blocks per indvidual device will differ.
	 */
	DEFINE_PROP_UINT32("num-blocks", struct pflash_t, nb_blocs, 0),
	DEFINE_PROP_UINT64("sector-length", struct pflash_t, sector_len, 0),
	/* width here is the overall width of this QEMU device in bytes.
	 * The QEMU device may be emulating a number of flash devices
	 * wired up in parallel; the width of each individual flash
	 * device should be specified via device-width. If the individual
	 * devices have a maximum width which is greater than the width
	 * they are being used for, this maximum width should be set via
	 * max-device-width (which otherwise defaults to device-width).
	 * So for instance a 32-bit wide QEMU flash device made from four
	 * 16-bit flash devices used in 8-bit wide mode would be configured
	 * with width = 4, device-width = 1, max-device-width = 2.
	 *
	 * If device-width is not specified we default to backwards
	 * compatible behaviour which is a bad emulation of two
	 * 16 bit devices making up a 32 bit wide QEMU device. This
	 * is deprecated for new uses of this device.
	 */
	DEFINE_PROP_UINT8("width", struct pflash_t, bank_width, 0),
	DEFINE_PROP_BIT("big-endian", struct pflash_t, features, PFLASH_BE, 0),
	DEFINE_PROP_BIT("secure", struct pflash_t, features, PFLASH_SECURE, 0),
	DEFINE_PROP_UINT16("id0", struct pflash_t, ident0, 0),
	DEFINE_PROP_UINT16("id1", struct pflash_t, ident1, 0),
	DEFINE_PROP_UINT16("id2", struct pflash_t, ident2, 0),
	DEFINE_PROP_UINT16("id3", struct pflash_t, ident3, 0),
	DEFINE_PROP_STRING("name", struct pflash_t, name),
	
	/* OTP0 Initial Data */
	DEFINE_PROP_UINT16("otp0-lock", struct pflash_t, otp0.lock, 0x0002),
	DEFINE_PROP_PTR("otp0-data", struct pflash_t, otp0.init_data),
	DEFINE_PROP_UINT16("otp0-data-len", struct pflash_t, otp0.init_data_len, 0),
	
	/* OTP1 Initial Data */
	DEFINE_PROP_UINT16("otp1-lock", struct pflash_t, otp1.lock, 0xFFFF),
	DEFINE_PROP_PTR("otp1-data", struct pflash_t, otp1.init_data),
	DEFINE_PROP_UINT16("otp1-data-len", struct pflash_t, otp1.init_data_len, 0),
	
	DEFINE_PROP_END_OF_LIST(),
};

static void pflash_0020_8819_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);

	dc->realize = pflash_0020_8819_realize;
	dc->props = pflash_0020_8819_properties;
	dc->vmsd = &vmstate_pflash;
	set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}


static const TypeInfo pflash_0020_8819_info = {
	.name			= "cfi.pflash.0020:8819",
	.parent			= TYPE_SYS_BUS_DEVICE,
	.instance_size	= sizeof(struct pflash_t),
	.class_init		= pflash_0020_8819_class_init,
};

static void pflash_0020_8819_register_types(void)
{
	type_register_static(&pflash_0020_8819_info);
}

type_init(pflash_0020_8819_register_types)

MemoryRegion *pflash_0020_8819_get_memory(pflash_t *fl)
{
	return &fl->mem;
}

static void postload_update_cb(void *opaque, int running, RunState state)
{
	pflash_t *pfl = opaque;

	/* This is called after bdrv_invalidate_cache_all.  */
	qemu_del_vm_change_state_handler(pfl->vmstate);
	pfl->vmstate = NULL;

	DPRINTF("%s: updating bdrv for %s\n", __func__, pfl->name);
	pflash_update(pfl, 0, pfl->sector_len * pfl->nb_blocs);
}

static int pflash_post_load(void *opaque, int version_id)
{
	pflash_t *pfl = opaque;

	if (!pfl->ro) {
		pfl->vmstate = qemu_add_vm_change_state_handler(postload_update_cb,
														pfl);
	}
	return 0;
}
