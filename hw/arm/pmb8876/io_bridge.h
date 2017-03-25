#ifndef __PMB8876_SIE_IO_BRIDGE__
#define __PMB8876_SIE_IO_BRIDGE__

static int g_ignore_irq = 0;
static int g_ignored_irq = 0;
static FILE *sie_bridge_fp(void);
void sie_bridge_write(unsigned int addr, unsigned int size, unsigned int value, unsigned int from);
unsigned int sie_bridge_read(unsigned int addr, unsigned int size, unsigned int from);

#endif // __PMB8876_SIE_IO_BRIDGE__
