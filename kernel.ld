ENTRY(kernel_entry)
SECTIONS {
  . = 0x10000;
  .text   : { *(.text.start) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data   : { *(.data .data.*) }
  .bss    : { __bss_start = .; *(.bss .bss.*) *(COMMON) __bss_end = .; }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
