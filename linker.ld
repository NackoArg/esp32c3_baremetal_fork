/*
* linker.ld - Script de enlace mínimo para ESP32-C3 (versión didáctica)
* ---------------------------------------------------------------
* OBJETIVO PEDAGÓGICO:
*  - Mostrar explícitamente cómo el enlazador coloca secciones en memoria.
*  - Diferenciar entre FLASH mapeada (XIP) y RAM para datos.
*  - Exponer símbolos que el startup usa para copiar/limpiar secciones.
*
* NOTAS:
*  - El ESP32-C3 ejecuta código en flash mapeada (XIP) a través de la región 0x4200_0000.
*  - La memoria DRAM principal accesible a la CPU comienza en 0x3FC8_0000.
*  - LENGTH aquí es una simplificación (no todo el espacio está siempre disponible, pero para un ejemplo pequeño basta).
*  - No incluimos particiones, bootloader ni secciones avanzadas (vector interrupciones separado, etc.).
*
* SÍMBOLOS EXPUESTOS:
*  _stext/_etext : Delimitan el bloque de código (.text + .rodata) que queda en FLASH.
*  _sdata/_edata : Datos inicializados que se COPIAN desde FLASH a RAM al arranque.
*  _sbss/_ebss   : Zona BSS que se pone a cero en startup.
*  _stack_top    : Dirección usada para inicializar el stack pointer (SP).
*  _sheap/_eheap : Marcadores pedagógicos de un posible heap (no usado aún).
*/
 
ENTRY(_start)
 
MEMORY
{
  /* Ventana XIP para instrucciones. */
  IROM (rx)  : ORIGIN = 0x42000000, LENGTH = 2M
  /* Ventana XIP para rodata accesible por bus de datos. */
  DROM (rx)  : ORIGIN = 0x3C000000, LENGTH = 512K
  /* DRAM interna para .data/.bss/stack. */
  DRAM (rwx) : ORIGIN = 0x3FC80000, LENGTH = 400K
}
 
/* Externally visible symbols */
PROVIDE(_stack_top = ORIGIN(DRAM) + LENGTH(DRAM));
 
SECTIONS
{
  .text : {
    _stext = .;
    *(.init)
    *(.text*)
    . = ALIGN(4);
    _etext = .;
  } > IROM
 
  .rodata : AT (LOADADDR(.text) + SIZEOF(.text)) {
    . = ALIGN(4);
    _srodata = .;
    *(.rodata*)
    . = ALIGN(4);
    _erodata = .;
  } > DROM
 
  .data : AT (LOADADDR(.rodata) + SIZEOF(.rodata)) {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > DRAM
  PROVIDE(_sidata = LOADADDR(.data));
 
  .bss (NOLOAD) : {
    . = ALIGN(8);
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(8);
    _ebss = .;
  } > DRAM
 
  _sheap = _ebss;
  _eheap = _stack_top - 0x2000;
}