#ifndef __PMB8876_SIE_IO_BRIDGE__
#define __PMB8876_SIE_IO_BRIDGE__

static FILE *sie_bridge_fp(void);
void sie_bridge_write(unsigned int addr, unsigned int value, unsigned int from);
unsigned int sie_bridge_read(unsigned int addr, unsigned int from);

#endif // __PMB8876_SIE_IO_BRIDGE__
