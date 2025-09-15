#ifndef FATFS_H
#define FATFS_H

#include "pico/stdlib.h"

void setup_ledg();
void setup_spi();
bool microsd_mount();
bool microsd_open();
void microsd_write(const char *payload);

#endif