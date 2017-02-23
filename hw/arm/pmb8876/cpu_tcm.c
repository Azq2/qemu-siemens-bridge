/*
	TCM
*/
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

/*
	Jazelle
*/
// JIDR
static uint64_t pmb8876_jidr_read(CPUARMState *env, const ARMCPRegInfo *ri) {
	printf("[CP15 JIDR READ]\n");
	return 0;
}
static void pmb8876_jidr_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value) {
	printf("[CP15 JIDR WRITE] %08lX\n", value);
}

// JOSCR
static uint64_t pmb8876_joscr_read(CPUARMState *env, const ARMCPRegInfo *ri) {
	printf("[CP15 JOSCR READ]\n");
	return 0;
}
static void pmb8876_joscr_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value) {
	printf("[CP15 JOSCR WRITE] %08lX\n", value);
}

// JMCR
static uint64_t pmb8876_jmcr_read(CPUARMState *env, const ARMCPRegInfo *ri) {
	printf("[CP15 JMCR READ]\n");
	return 0;
}
static void pmb8876_jmcr_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value) {
	printf("[CP15 JMCR WRITE] %08lX\n", value);
}

// JPR
static uint64_t pmb8876_jpr_read(CPUARMState *env, const ARMCPRegInfo *ri) {
	printf("[CP15 JPR READ]\n");
	return 0;
}
static void pmb8876_jpr_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value) {
	printf("[CP15 JPR WRITE] %08lX\n", value);
}

// JCOTTR
static uint64_t pmb8876_jcottr_read(CPUARMState *env, const ARMCPRegInfo *ri) {
	printf("[CP15 JCOTTR READ]\n");
	return 0;
}
static void pmb8876_jcottr_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value) {
	printf("[CP15 JCOTTR WRITE] %08lX\n", value);
}

/*
	CP15 regs list
*/
static const ARMCPRegInfo pmb8876_cp_reginfo[] = {
	// Jazelle
    { .name = "JIDR", .cp = 14, .opc1 = 7, .crn = 0, .crm = 0, .opc2 = 0,
      .access = PL1_RW, 
		
		.readfn = pmb8876_jidr_read,
		.writefn = pmb8876_jidr_write,
       },
    { .name = "JOSCR", .cp = 14, .opc1 = 7, .crn = 1, .crm = 0, .opc2 = 0,
      .access = PL1_RW, 
      
		.readfn = pmb8876_joscr_read,
		.writefn = pmb8876_joscr_write,
	 },
    { .name = "JMCR", .cp = 14, .opc1 = 7, .crn = 2, .crm = 0, .opc2 = 0,
      .access = PL1_RW, 
      
		.readfn = pmb8876_jmcr_read,
		.writefn = pmb8876_jmcr_write,
	 },
    { .name = "JPR", .cp = 14, .opc1 = 7, .crn = 3, .crm = 0, .opc2 = 0,
      .access = PL1_RW, 
      
		.readfn = pmb8876_jpr_read,
		.writefn = pmb8876_jpr_write,
	 },
    { .name = "JCOTTR", .cp = 14, .opc1 = 7, .crn = 4, .crm = 0, .opc2 = 0,
      .access = PL1_RW, 
      
		.readfn = pmb8876_jcottr_read,
		.writefn = pmb8876_jcottr_write,
	 },
	 
	 // TCM
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
