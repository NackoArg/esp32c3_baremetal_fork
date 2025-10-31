## Makefile - Construcción mínima de proyecto bare-metal ESP32-C3
## -------------------------------------------------------------
## PROPÓSITO DIDÁCTICO:
##  - Mostrar reglas explícitas de compilación y enlace sin usar ESP-IDF.
##  - Separar claramente etapas: compilación, enlazado, conversión binaria y flasheo.
##  - Servir como base para que alumnos agreguen más fuentes (.c / .S).
## USO BÁSICO:
##   make           -> compila todo y muestra tamaño
##   make flash     -> genera imagen y flashea en 0x10000 (requiere bootloader existente)
##   make clean     -> limpia artefactos
## NOTAS:
##  - CFLAGS incluye -ffreestanding y -nostdlib para evitar dependencias a runtime estándar.
##  - LDFLAGS aplica el script de enlace personalizado (linker.ld).
##  - OBJETIVO: Mantener binario ultrapequeño y transparente.

TARGET      := app
BUILD_DIR   := build
LINKER      := linker.ld
SRC_DIR     := src

SRCS = $(SRC_DIR)/startup.S \
       $(SRC_DIR)/main.c

OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
OBJS := $(OBJS:$(SRC_DIR)/%.S=$(BUILD_DIR)/%.o)

CC      := riscv32-esp-elf-gcc      # Compilador RISC-V de Espressif
OBJCOPY := riscv32-esp-elf-objcopy  # Conversión de formatos (ELF -> binario plano)
OBJDUMP := riscv32-esp-elf-objdump  # Desensamblado para análisis didáctico
SIZE    := riscv32-esp-elf-size     # Resumen de tamaños de secciones

CFLAGS  := -Os -march=rv32imc -mabi=ilp32 -ffreestanding -nostdlib -Wall -Wextra -Iinclude
## -Os: optimización para tamaño. -ffreestanding: entorno sin librería estándar.
## -nostdlib/-nostartfiles (en LDFLAGS) impide que el enlazador agregue crt0 y stdlib.
LDFLAGS := -T $(LINKER) -nostdlib -nostartfiles -Wl,-Map=$(BUILD_DIR)/$(TARGET).map
LIBS    := -lgcc

all: dirs $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin $(BUILD_DIR)/$(TARGET).dis
	@if command -v $(SIZE) >/dev/null 2>&1; then \
		$(SIZE) $(BUILD_DIR)/$(TARGET).elf; \
	elif command -v size >/dev/null 2>&1; then \
		size $(BUILD_DIR)/$(TARGET).elf; \
	else \
		echo "[info] 'size' no encontrado: se omite el resumen de secciones"; \
	fi   # Mostrar resumen de tamaño tras construir

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c      # Regla genérica para fuentes C -> objeto
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S      # Regla genérica para fuentes Assembly -> objeto
	$(CC) $(CFLAGS) -c $< -o $@

dirs:                              # Crear directorio de build si no existe
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/$(TARGET).elf: $(OBJS) $(LINKER)  # Enlazar objetos con script personalizado
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS) $(LIBS)

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf  # Extraer binario plano (no siempre necesario)
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/$(TARGET).dis: $(BUILD_DIR)/$(TARGET).elf  # Desensamblado pedagógico
	@if command -v $(OBJDUMP) >/dev/null 2>&1; then \
		$(OBJDUMP) -d $< > $@; \
	elif command -v objdump >/dev/null 2>&1; then \
		objdump -d $< > $@; \
	else \
		echo "[info] 'objdump' no encontrado: se omite el desensamblado"; \
		: > $@; \
	fi

flash: all                          # Generar imagen y flashear aplicación en offset típico 0x10000
	# Generar imagen ejecutable para ROM de arranque (formato esperado por bootloader estándar)
	esptool.py --chip esp32c3 elf2image $(BUILD_DIR)/$(TARGET).elf --output $(BUILD_DIR)/image
	# Escribir aplicación en la flash en 0x10000 (requiere bootloader+partition table en 0x0)
	esptool.py --chip esp32c3 write_flash 0x10000 $(BUILD_DIR)/image
	@echo "Flasheado. Si no arranca, verifica bootloader en 0x0 (puedes flashear uno via ESP-IDF)."

clean:                               # Eliminar artefactos de build
	rm -rf $(BUILD_DIR)

.PHONY: all clean flash dirs


#Prueba para el Timer
#idf_component_register(SRCS "main.c"
#						INCLUDE_DIRS "."
#						REQUIRES esp_driver_gptimer
#)*/
