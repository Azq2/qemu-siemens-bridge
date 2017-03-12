
const char *pmb8876_get_reg_name(unsigned int addr) {
	struct PMB8876_REG_NAME *name = NULL;
	unsigned int i;
	for (i = 0; i < (sizeof(PMB8876_REGS_NAMES) / sizeof(PMB8876_REGS_NAMES[0])); ++i) {
		struct PMB8876_REG_NAME *n = &PMB8876_REGS_NAMES[i];
		if (n->addr == addr && !n->addr_end)
			return n->name;
		if (n->addr_end && addr >= n->addr && addr <= n->addr_end)
			name = n;
	}
	return name ? name->name : "UNKNOWN";
}

const char *pmb8876_get_cpu_mode(CPUARMState *env) {
	unsigned int mode = env->uncached_cpsr & 0x1f;
	switch (mode) {
		case ARM_CPU_MODE_USR:	return "USR";
		case ARM_CPU_MODE_FIQ:	return "FIQ";
		case ARM_CPU_MODE_IRQ:	return "IRQ";
		case ARM_CPU_MODE_SVC:	return "SVC";
		case ARM_CPU_MODE_MON:	return "MON";
		case ARM_CPU_MODE_ABT:	return "ABT";
		case ARM_CPU_MODE_HYP:	return "HYP";
		case ARM_CPU_MODE_UND:	return "UND";
		case ARM_CPU_MODE_SYS:	return "SYS";
		default:				return "UNK";
	}
}
