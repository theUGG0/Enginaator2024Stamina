/*
 * sdCard.h
 *
 *  Created on: Mar 13, 2024
 *      Author: JRE
 */

#ifndef MAIN_SDCARD_H_
#define MAIN_SDCARD_H_

extern void sdCard_init(void);
extern void sdCard_Read_bmp_file(const char *path, uint16_t * output_buffer);

#endif /* MAIN_SDCARD_H_ */
