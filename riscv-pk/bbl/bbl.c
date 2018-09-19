#include "bbl.h"
#include "mtrap.h"
#include "atomic.h"
#include "vm.h"
#include "bits.h"
#include "config.h"
#include "mfdt.h"
#include "libfdt.h"
#include "fdt.h"
#include <string.h>

static const void* entry_point;
long disabled_hart_mask;

static uintptr_t dtb_output()
{
  extern char _payload_end;
  uintptr_t end = (uintptr_t) &_payload_end;
  return (end + MEGAPAGE_SIZE - 1) / MEGAPAGE_SIZE * MEGAPAGE_SIZE;
}

static void filter_dtb(uintptr_t source)
{
  int err;
  uintptr_t dest = dtb_output();
  uint32_t size = fdt_size(source);
  memcpy((void*)dest, (void*)source, size);

  // Allocate space for additional nodes i.e. timer, cpu-map
  err = fdt_open_into((void *)dest, (void *)dest, size + 2048);

  if (err < 0)
    die("%s: fdt buffer couldn't be expanded err = [%d]!!\n", __func__, err);

  add_msemi_pcie_node((void *)dest);
  // Remove information from the chained FDT
  filter_harts(dest, &disabled_hart_mask);
  filter_plic(dest);
  filter_compat(dest, "riscv,clint0");
  filter_compat(dest, "riscv,debug-013");
}

void boot_other_hart(uintptr_t unused __attribute__((unused)))
{
  const void* entry;
  do {
    entry = entry_point;
    mb();
  } while (!entry);

  long hartid = read_csr(mhartid);
  if ((1 << hartid) & disabled_hart_mask) {
    while (1) {
      __asm__ volatile("wfi");
#ifdef __riscv_div
      __asm__ volatile("div x0, x0, x0");
#endif
    }
  }

  enter_supervisor_mode(entry, hartid, dtb_output());
}

void boot_loader(uintptr_t dtb)
{
  extern char _payload_start;
  filter_dtb(dtb);
#ifdef PK_ENABLE_LOGO
  print_logo();
#endif
#ifdef PK_PRINT_DEVICE_TREE
  fdt_print(dtb_output());
#endif
  mb();
  entry_point = &_payload_start;
  boot_other_hart(0);
}
