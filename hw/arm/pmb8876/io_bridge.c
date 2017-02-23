
static FILE *__sie_bridge_fp = NULL;

static FILE *sie_bridge_fp(void) {
	if (!__sie_bridge_fp)
		__sie_bridge_fp = fopen("/dev/shm/siemens_io_sniff", "w+");
	return __sie_bridge_fp;
}

void sie_bridge_write(unsigned int addr, unsigned int value, unsigned int from) {
	FILE *fp = sie_bridge_fp();
	while (ftruncate(fileno(fp), 0) == 0) break;
	fseek(fp, 0, SEEK_SET);
	fwrite("W", 1, 1, fp);
	fwrite(&addr, 4, 1, fp);
	fwrite(&value, 4, 1, fp);
	fwrite(&from, 4, 1, fp);
	
	unsigned char buf;
	while (1) {
		fseek(fp, 0, SEEK_SET);
		fflush(fp);
		if (fread(&buf, 1, 1, fp) == 1 && buf == '!')
			break;
	}
}

unsigned int sie_bridge_read(unsigned int addr, unsigned int from) {
	FILE *fp = sie_bridge_fp();
	while (ftruncate(fileno(fp), 0) == 0) break;
	fseek(fp, 0, SEEK_SET);
	fwrite("R", 1, 1, fp);
	fwrite(&addr, 4, 1, fp);
	fwrite(&from, 4, 1, fp);
	
	unsigned char buf[5];
	while (1) {
		fseek(fp, 0, SEEK_SET);
		fflush(fp);
		if (fread(buf, 5, 1, fp) == 1 && buf[0] == (unsigned char) '!') {
			return buf[4] << 24 | buf[3] << 16 | buf[2] << 8 | buf[1];
			break;
		}
	}
}
