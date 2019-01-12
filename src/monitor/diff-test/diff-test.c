#include <dlfcn.h>

#include "nemu.h"
#include "monitor/monitor.h"
#include "monitor/diff-test.h"

void (*ref_difftest_memcpy_from_dut)(paddr_t dest, void *src, size_t n) = NULL;
void (*ref_difftest_getregs)(void *c) = NULL;
void (*ref_difftest_setregs)(const void *c) = NULL;
void (*ref_difftest_exec)(uint64_t n) = NULL;

static bool is_skip_ref;
static bool is_skip_dut;
//static uint32_t eflags_skip_mask;
static bool is_detach;

void difftest_skip_ref() { is_skip_ref = true; }
void difftest_skip_dut() { is_skip_dut = true; }
//void difftest_skip_eflags(uint32_t mask) { eflags_skip_mask = mask; }

bool arch_difftest_check_reg(CPU_state *ref_r, vaddr_t pc);
void arch_difftest_attach(void);

void init_difftest(char *ref_so_file, long img_size) {
#ifndef DIFF_TEST
  return;
#endif

  assert(ref_so_file != NULL);

  void *handle;
  handle = dlopen(ref_so_file, RTLD_LAZY | RTLD_DEEPBIND);
  assert(handle);

  ref_difftest_memcpy_from_dut = dlsym(handle, "difftest_memcpy_from_dut");
  assert(ref_difftest_memcpy_from_dut);

  ref_difftest_getregs = dlsym(handle, "difftest_getregs");
  assert(ref_difftest_getregs);

  ref_difftest_setregs = dlsym(handle, "difftest_setregs");
  assert(ref_difftest_setregs);

  ref_difftest_exec = dlsym(handle, "difftest_exec");
  assert(ref_difftest_exec);

  void (*ref_difftest_init)(void) = dlsym(handle, "difftest_init");
  assert(ref_difftest_init);

  Log("Differential testing: \33[1;32m%s\33[0m", "ON");
  Log("The result of every instruction will be compared with %s. "
      "This will help you a lot for debugging, but also significantly reduce the performance. "
      "If it is not necessary, you can turn it off in include/common.h.", ref_so_file);

  ref_difftest_init();
  ref_difftest_memcpy_from_dut(PC_START, guest_to_host(PC_START), img_size);
  ref_difftest_setregs(&cpu);
}

static inline void difftest_check_reg(const char *name, vaddr_t pc, uint32_t ref, uint32_t dut) {
  if (ref != dut) {
    Log("%s is different after executing instruction at pc = 0x%08x, right = 0x%08x, wrong = 0x%08x",
        name, pc, ref, dut);
  }
}

void difftest_step(vaddr_t pc) {
  CPU_state ref_r;

  if (is_detach) return;

  if (is_skip_dut) {
    is_skip_dut = false;
    return;
  }

  if (is_skip_ref) {
    // to skip the checking of an instruction, just copy the reg state to reference design
    ref_difftest_getregs(&ref_r);
    ref_difftest_setregs(&cpu);
    is_skip_ref = false;
    return;
  }

  ref_difftest_exec(1);
  ref_difftest_getregs(&ref_r);

  // TODO: Check the registers state with QEMU.
  if (memcmp(&cpu, &ref_r, DIFFTEST_REG_SIZE)) {
    int i;
    for (i = 0; i < DIFFTEST_REG_SIZE / sizeof(cpu.pc) - 1; i ++) {
      difftest_check_reg(reg_name(i, 4), pc, ref_r.gpr[i]._32, cpu.gpr[i]._32);
    }
    difftest_check_reg("pc", pc, ref_r.pc, cpu.pc);

    nemu_state.state = NEMU_ABORT;
    nemu_state.halt_pc = pc;
  }
}

void difftest_detach() {
  is_detach = true;
}

void difftest_attach() {
#ifndef DIFF_TEST
  return;
#endif

  is_detach = false;
  is_skip_ref = false;
  is_skip_dut = false;

  arch_difftest_attach();
}
