#pragma once

#include <sys/wait.h>

#define EE_NPINS 74
#define	EE_PINS_OFFSET_BASE		88
typedef int gpio_t;

typedef int pin_t;

extern pin_t eeprom_pins[EE_NPINS];

// RPI 2 & 3, RPi1 is 0x20000000
#define PMUX_OUT_PU 2 

#define PMUX_IO_PDIS 0
#define PMUX_IO_PU 2 
#define PMUX_IO_PD 1

#define GPIO_HIZ -1

enum gpio_dir_e {
GPIO_DIR_IN = 0,
GPIO_DIR_OUT = 1,
GPIO_DIR_BIDIR = 2
};

#define GPIO_INPUT(io)
#define GPIO_OUTPUT(io)
#define GPIO_READ_BIT(io) (sample_12k())
#define GPIO_SET_BIT(io)
#define GPIO_CLR_BIT(io)
#define GPIO_WRITE_BIT(io, v)

extern volatile unsigned *gpio;

//extern gpio_t GPIO_NONE;
extern gpio_t FPGA_INIT, FPGA_PGM;
//extern gpio_t SPIn_SCLK, SPIn_MISO, SPIn_MOSI, 
extern gpio_t SPIn_CS0, SPIn_CS1;
extern gpio_t CMD_READY, SND_INTR;

#define devio_check(gpio, dir, pmux_val) \
	_devio_check(#gpio, gpio, dir, pmux_val);
void _devio_check(const char *name, gpio_t gpio, gpio_dir_e dir, u4_t pmux_val);

#define gpio_setup(gpio, dir, initial, pmux_val1, pmux_val2) \
	_gpio_setup(#gpio, gpio, dir, initial, pmux_val1, pmux_val2);
void _gpio_setup(const char *name, gpio_t gpio, gpio_dir_e dir, u4_t initial, u4_t pmux_val1, u4_t pmux_val2);

void peri_init();
void gpio_test(int gpio_test);
void peri_free();

extern bool sample_12k();

#define FPGA_INIT_PIN 6
#define FPGA_PGM_PIN 5

#define SPIn_CS0_PIN 8
#define SPIn_CS1_PIN 7
#define CMD_READY_PIN 13
#define SND_INTR_PIN 26
