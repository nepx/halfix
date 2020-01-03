#ifndef IO_H
#define IO_H

#include <stdint.h>
typedef uint32_t (*io_read)(uint32_t port);
typedef void (*io_write)(uint32_t port, uint32_t data);
typedef void (*io_reset)(void);

void io_register_read(int port, int length, io_read b, io_read w, io_read d);
void io_register_write(int port, int length, io_write b, io_write w, io_write d);
void io_unregister_read(int port, int length);
void io_unregister_write(int port, int length);
void io_register_mmio_read(uint32_t start, uint32_t length, io_read b, io_read w, io_read d);
void io_register_mmio_write(uint32_t start, uint32_t length, io_write b, io_write w, io_write d);
void io_remap_mmio_read(uint32_t oldstart, uint32_t newstart);

void io_register_reset(io_reset cb);
void io_trigger_reset(void);

uint8_t io_readb(uint32_t port);
uint16_t io_readw(uint32_t port);
uint32_t io_readd(uint32_t port);
void io_writeb(uint32_t port, uint8_t data);
void io_writew(uint32_t port, uint16_t data);
void io_writed(uint32_t port, uint32_t data);

void io_handle_mmio_write(uint32_t addr, uint32_t data, int size);
uint32_t io_handle_mmio_read(uint32_t addr, int size);
int io_addr_mmio_read(uint32_t addr);

void io_init(void);

#endif