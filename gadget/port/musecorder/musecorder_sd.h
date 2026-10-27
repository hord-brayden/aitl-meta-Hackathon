// microSD on the CYD boards' VSPI slot (SCK 18, MISO 19, MOSI 23, CS 5), mounted at /sd only while in use:
// a mounted FAT volume costs ~8 KB of RAM, which this board does not have to spare.
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool mc_sd_mount(void);     // counted: every mount needs an unmount
void mc_sd_unmount(void);
bool mc_sd_mounted(void);
#ifdef __cplusplus
}
#endif
