
static FILE *__sie_bridge_fp = NULL;

static char cmd_w_size[] = {0, 'o', 'w', 'O', 'W'};
static char cmd_r_size[] = {0, 'i', 'r', 'I', 'R'};

static FILE *sie_bridge_fp(void) {
	if (!__sie_bridge_fp)
		__sie_bridge_fp = fopen("/dev/shm/siemens_io_sniff", "w+");
	return __sie_bridge_fp;
}

void sie_bridge_write(unsigned int addr, unsigned int size, unsigned int value, unsigned int from) {
	if (from % 4 == 0)
		from -= 4;
	else
		from -= 2;
	
	FILE *fp = sie_bridge_fp();
	while (ftruncate(fileno(fp), 0) == 0) break;
	fseek(fp, 0, SEEK_SET);
	fwrite(&cmd_w_size[size], 1, 1, fp);
	fwrite(&addr, 4, 1, fp);
	fwrite(&value, 4, 1, fp);
	fwrite(&from, 4, 1, fp);
	
	int tell;
	unsigned char buf;
	while (1) {
		fseek(fp, 0, SEEK_END);
		tell = ftell(fp);
		if (tell == 1 || tell == 2) {
			fseek(fp, 0, SEEK_SET);
			fflush(fp);
			if (fread(&buf, 1, 1, fp) == 1) {
				if (buf == 0x2B) { // IRQ
					if (fread(&buf, 1, 1, fp) == 1) {
					//	fprintf(stderr, "**** NEW IRQ: %02X\n", buf);
						pmb8876_trigger_irq(buf);
					} else {
						fprintf(stderr, "%s(%08X, %08X, %08X): Read IRQ error\n", __func__, addr, value, from);
						exit(1);
					}
				} else if (buf != 0x21) {
					fprintf(stderr, "%s(%08X, %08X, %08X): Invalid ACK: 0x%02X\n", __func__, addr, value, from, buf);
					exit(1);
				}
				break;
			}
		}
	}
}

unsigned int sie_bridge_read(unsigned int addr, unsigned int size, unsigned int from) {
	if (from % 4 == 0)
		from -= 4;
	else
		from -= 2;
	
	FILE *fp = sie_bridge_fp();
	while (ftruncate(fileno(fp), 0) == 0) break;
	fseek(fp, 0, SEEK_SET);
	fwrite(&cmd_r_size[size], 1, 1, fp);
	fwrite(&addr, 4, 1, fp);
	fwrite(&from, 4, 1, fp);
	
	int tell;
	unsigned char irq;
	unsigned char buf[5];
	while (1) {
		fseek(fp, 0, SEEK_END);
		tell = ftell(fp);
		if (tell == 5 || tell == 6) {
			fseek(fp, 0, SEEK_SET);
			fflush(fp);
			if (fread(buf, 5, 1, fp) == 1) {
				if (buf[0] == 0x2B) { // IRQ
					if (fread(&irq, 1, 1, fp) == 1) {
					//	fprintf(stderr, "**** NEW IRQ: %02X\n", irq);
						pmb8876_trigger_irq(irq);
					} else {
						fprintf(stderr, "%s(%08X, %08X): Read IRQ error\n", __func__, addr, from);
						exit(1);
					}
				} else if (buf[0] != 0x21) {
					fprintf(stderr, "%s(%08X, %08X): Invalid ACK: 0x%02X\n", __func__, addr, from, buf[0]);
					exit(1);
				}
				return buf[4] << 24 | buf[3] << 16 | buf[2] << 8 | buf[1];
			}
		}
	}
}
