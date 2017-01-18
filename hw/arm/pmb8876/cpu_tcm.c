
static unsigned int pmb8876_tcm[2] = {0x10, 0x10};
static uint64_t pmb8876_atcm_read(CPUARMState *env, const ARMCPRegInfo *ri) {
	printf("[CP15 ATCM READ]\n");
	return pmb8876_tcm[0];
}

static uint64_t pmb8876_btcm_read(CPUARMState *env, const ARMCPRegInfo *ri) {
	printf("[CP15 BTCM READ]\n");
	return pmb8876_tcm[1];
}

static void pmb8876_atcm_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value) {
	printf("[CP15 ATCM WRITE] %08lX\n", value);
	pmb8876_tcm[0] = value;
}

static void pmb8876_btcm_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value) {
	printf("[CP15 BTCM WRITE] %08lX\n", value);
	pmb8876_tcm[1] = value;
}

static const ARMCPRegInfo pmb8876_cp_reginfo[] = {
    { .name = "ATCM", .cp = 15, .opc1 = 0, .crn = 9, .crm = 1, .opc2 = 0,
      .access = PL1_RW, 
		
		.readfn = pmb8876_atcm_read,
		.writefn = pmb8876_atcm_write,
       },
    { .name = "BTCM", .cp = 15, .opc1 = 0, .crn = 9, .crm = 1, .opc2 = 1,
      .access = PL1_RW, 
      
		.readfn = pmb8876_btcm_read,
		.writefn = pmb8876_btcm_write,
	 },
    REGINFO_SENTINEL
};
