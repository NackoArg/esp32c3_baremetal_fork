/*
* startup.S - Rutina de arranque mínima RISC-V para ESP32-C3 (versión didáctica)
* RESPONSABILIDADES DEL STARTUP:
*  1. Inicializar el stack pointer (SP) usando el símbolo _stack_top definido en linker.ld.
*  2. Limpiar (poner a cero) la sección .bss (variables globales no inicializadas).
*  3. Copiar la sección .data desde su dirección de carga en FLASH (LMA) a su dirección en RAM (VMA).
*  4. Llamar a main.
*  5. Si main retorna, permanecer en un bucle infinito para no ejecutar memoria basura.
*
* NOTAS:
*  - No configuramos interrupciones (mtvec) aquí todavía.
*  - No habilitamos features especiales ni cambiamos privilegios.
*  - .option norvc desactiva instrucciones comprimidas para facilitar lectura del desensamblado.
*/
 
    .section .init
    .globl _start
    .option norvc               /* Disable compressed for clarity (optional) */
 
_start:
    /* 1) Configurar el puntero de pila (SP) con la dirección simbólica _stack_top */
    la   sp, _stack_top
 
    /* 2) Limpiar la sección .bss: escribir ceros desde _sbss hasta _ebss */
    la   t0, _sbss        /* t0 = start of bss */
    la   t1, _ebss        /* t1 = end of bss */
1:  bge  t0, t1, 2f
    sw   zero, 0(t0)
    addi t0, t0, 4
    j    1b
2:
    /* 3) Copiar .data: origen = _sidata (FLASH), destino = _sdata (RAM) */
    la   t0, _sdata       /* destination */
    la   t1, _edata       /* end */
    la   t2, _sidata      /* load address en flash (establecido en linker) */
3:  bge  t0, t1, 4f
    lw   t3, 0(t2)
    sw   t3, 0(t0)
    addi t0, t0, 4
    addi t2, t2, 4
    j    3b
4:
    /* 4) Llamar a main (punto de entrada de la lógica de la aplicación) */
    call main
 
5:  j 5b   /* 5) Si main retorna, permanecer aquí (bucle infinito) */
 
    .size _start, .-_start