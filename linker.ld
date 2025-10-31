/* linker.ld - Script mínimo para ESP32-C3 (didáctico) */
ENTRY(_start)

MEMORY
{
  IROM (rx)  : ORIGIN = 0x42000000, LENGTH = 2M
  DROM (rx)  : ORIGIN = 0x3C000000, LENGTH = 512K
  DRAM (rwx) : ORIGIN = 0x3FC80000, LENGTH = 400K
}

PROVIDE(_stack_top = ORIGIN(DRAM) + LENGTH(DRAM));
PROVIDE(__stack = _stack_top);

SECTIONS
{
  .text : {
    _stext = .;
    KEEP(*(.init))
    KEEP(*(.init.*))
    *(.text.startup*)
    *(.text*)
    . = ALIGN(4);
    _etext = .;
  } > IROM

  .rodata : AT (LOADADDR(.text) + SIZEOF(.text)) {
    . = ALIGN(4);
    _srodata = .;
    *(.rodata)
    *(.rodata.*)
    *(.srodata)
    *(.srodata.*)
    . = ALIGN(4);
    _erodata = .;
  } > DROM

  .data : AT (LOADADDR(.rodata) + SIZEOF(.rodata)) {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data.*)
    *(.sdata)
    *(.sdata.*)
    *(.sdata2)
    *(.sdata2.*)
    . = ALIGN(4);
    _edata = .;
  } > DRAM
  PROVIDE(_sidata = LOADADDR(.data));
  PROVIDE(__global_pointer$ = _sdata + 0x800);

  .bss (NOLOAD) : {
    . = ALIGN(8);
    _sbss = .;
    *(.bss)
    *(.bss.*)
    *(.sbss)
    *(.sbss.*)
    *(COMMON)
    . = ALIGN(8);
    _ebss = .;
  } > DRAM

  _sheap = _ebss;
  _eheap = _stack_top - 0x2000;
}

PROVIDE(__bss_start = _sbss);
PROVIDE(__bss_end   = _ebss);
PROVIDE(__data_start = _sdata);
PROVIDE(__data_end   = _edata);
PROVIDE(__data_load  = _sidata);

ASSERT(SIZEOF(.text) <= LENGTH(IROM), "IROM overflow: ajusta .text o memoria");
ASSERT(SIZEOF(.data) + SIZEOF(.bss) <= LENGTH(DRAM), "DRAM overflow: reduce .data/.bss");
