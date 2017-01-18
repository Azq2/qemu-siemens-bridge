
static FILE *__sie_bridge_fp = NULL;

static FILE *sie_bridge_fp(void) {
	if (!__sie_bridge_fp)
		__sie_bridge_fp = fopen("/dev/shm/siemens_io_sniff", "w+");
	return __sie_bridge_fp;
}

static void sie_bridge_write(unsigned int addr, unsigned int value) {
	FILE *fp = sie_bridge_fp();
	while (ftruncate(fileno(fp), 0) == 0) break;
	fseek(fp, 0, SEEK_SET);
	fwrite("W", 1, 1, fp);
	fwrite(&addr, 4, 1, fp);
	fwrite(&value, 4, 1, fp);
	
	char buf;
	while (1) {
		fseek(fp, 0, SEEK_SET);
		fflush(fp);
		if (fread(&buf, 1, 1, fp) == 1 && buf == '!')
			break;
	}
}

static unsigned int sie_bridge_read(unsigned int addr) {
	FILE *fp = sie_bridge_fp();
	while (ftruncate(fileno(fp), 0) == 0) break;
	fseek(fp, 0, SEEK_SET);
	fwrite("R", 1, 1, fp);
	fwrite(&addr, 4, 1, fp);
	
	char buf[5];
	while (1) {
		fseek(fp, 0, SEEK_SET);
		fflush(fp);
		if (fread(buf, 5, 1, fp) == 1 && buf[0] == '!') {
			return buf[4] << 24 | buf[3] << 16 | buf[2] << 8 | buf[1];
			break;
		}
	}
}
