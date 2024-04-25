/*
**====================================================================================
** Imported definitions
**====================================================================================
*/
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include "driver/spi_master.h"

#include "esp_task_wdt.h"

/* Display driver is defined in display.c and display.h
 * Note that when you add new files to the project, then CMakeLists.txt also needs to be updated for these files to be built. */
#include "display.h"
/* The SD card functionality has been moved to its own separate file for this project. */
#include "sdCard.h"

/*
**====================================================================================
** Private constant definitions
**====================================================================================
*/

#define Private static

#define PIN_NUM_CLK   12	/* SPI Clock pin */
#define PIN_NUM_MOSI  11	/* SPI Master Out, Slave in  - The ESP32 transmits data over this pin */
#define PIN_NUM_MISO  13	/* SPI Master In, Slave Out  - The ESP32 receives data over this pin. */

/* Note that additional connections are required for the display to work. These are defined in display.c */
/* PIN_NUM_DC         5  - Data/Control pin  		*/
/* PIN_NUM_RST        3  - Display Reset pin 		*/
/* PIN_NUM_CS         4  - Display Chip Select pin 	*/
/* PIN_NUM_BCKL       2  - Backlight LED 			*/
/*
 * PIN_NUM_SDCARD_CS  7  - SD Card chip select pin	*/

/* Additionally GND and 3.3V need to be connected to the GND and VCC pins on the display board respectively. */

/* Uncomment this to enable the ghost bitmap test. */
#define GHOST_TEST

/*
**====================================================================================
** Private macro definitions
**====================================================================================
*/

#define SET_FRAME_BUF_PIXEL(buf,x,y,color) *((buf) + (x) + (320*(y)))=color

/*
**====================================================================================
** Private type definitions
**====================================================================================
*/

/*
**====================================================================================
** Private function forward declarations
**====================================================================================
*/

Private uint8_t initialize_spi(void);
Private void drawRectangleInFrameBuf(int xPos, int yPos, int width, int height, uint16_t color);
Private void drawBmpInFrameBuf(int xPos, int yPos, int width, int height, uint16_t * data_buf);
#ifdef GHOST_TEST
Private void drawGhost(void);
#endif

/*
**====================================================================================
** Private variable declarations
**====================================================================================
*/

uint16_t * priv_frame_buffer;
#ifdef GHOST_TEST
#define GHOST_SPEED 4
uint16_t * priv_ghost_buffer;
Private int ghost_position = 0;
Private int ghost_direction = GHOST_SPEED;
#endif

/*
**====================================================================================
** Public function definitions
**====================================================================================
*/
void app_main(void)
{
	uint8_t res;

	/* Check how much RAM we have currently available... */
	printf("Total available memory: %u bytes\n", heap_caps_get_total_size(MALLOC_CAP_8BIT));

	/*Allocate memory for the frame buffer from the heap. */
    priv_frame_buffer = heap_caps_malloc(240*320*sizeof(uint16_t), MALLOC_CAP_DMA);
    assert(priv_frame_buffer);

	/*Call the function to initialize the SPI peripheral connected to the SD card. */
	res = initialize_spi();

	if(res == 1u)
	{
		/* Initialize the Sd Card logic as well as the display driver.
		 * Note that these devices are on the same SPI bus. The Chip Select pins allow us to
		 * select and deselect them as needed. */
		display_init();
		sdCard_init();

		display_fillRectangle(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, COLOR_ORANGE);
		vTaskDelay(1000u / portTICK_PERIOD_MS);

		/* Load an image from the SD Card into the frame buffer */
		sdCard_Read_bmp_file("/logo.bmp", priv_frame_buffer);

		display_drawBitmap(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, priv_frame_buffer);
	}

	/* Five second delay... */
	vTaskDelay(5000u / portTICK_PERIOD_MS);

#ifdef GHOST_TEST
	priv_ghost_buffer = heap_caps_malloc(64*64*sizeof(uint16_t), MALLOC_CAP_DMA);
	assert(priv_ghost_buffer);
	sdCard_Read_bmp_file("/ghost.bmp", priv_ghost_buffer);
#endif

	/* The idea is that we will try to keep a cyclic process that is called every 40 milliseconds, we call our
	 * drawing functions and then delay for the period remaining.
	 */

	TickType_t xLastWakeTime;
	const TickType_t xFrequency = 40u / portTICK_PERIOD_MS;
	xLastWakeTime = xTaskGetTickCount ();

	/* Main CPU cycle */
	while(1)
	{
		vTaskDelayUntil( &xLastWakeTime, xFrequency );
#ifdef GHOST_TEST
		/* Simple test for drawing a moving bitmap on the screen. */
		drawGhost();
#endif
	}
}

/*
**====================================================================================
** Private function definitions
**====================================================================================
*/

/* This function initializest the SPI interface, but does not yet add any devies on it.  */
Private uint8_t initialize_spi(void)
{
    esp_err_t ret;

    printf("Setting up SPI peripheral\n");

    spi_bus_config_t bus_cfg =
    {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_MAX_TRANSFER_SIZE,
    };

    ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);

    if (ret != ESP_OK)
    {
        printf("Failed to initialize bus.\n");
        return 0u;
    }

    return 1u;
}


Private void drawRectangleInFrameBuf(int xPos, int yPos, int width, int height, uint16_t color)
{
	for (int x = xPos; ((x < (xPos+width)) && (x < 320)); x++)
	{
		for (int y = yPos; ((y < (yPos+height)) && (y < 240)); y++)
		{
			SET_FRAME_BUF_PIXEL(priv_frame_buffer, x, y, color);
		}
	}
}


Private void drawBmpInFrameBuf(int xPos, int yPos, int width, int height, uint16_t * data_buf)
{
	uint16_t * data_ptr = data_buf;

	for (int x = xPos; (x < (xPos + width)); x++)
	{
		for (int y = yPos; (y < (yPos + height)); y++)
		{
			if ((x >= 0) && (x < DISPLAY_WIDTH) && (y >= 0) && (y < DISPLAY_HEIGHT))
			{
				SET_FRAME_BUF_PIXEL(priv_frame_buffer, x, y, *data_ptr);
			}
			data_ptr++;
		}
	}
}

#ifdef GHOST_TEST
Private void drawGhost(void)
{
	/* Here we draw the whole screen buffer every time. Note that this is not necessarily the most optimal solution, since it takes a lot
	 * of time to redraw the whole screen */

	/* Update cube position */
	ghost_position += ghost_direction;

	if(ghost_position <= 0)
	{
		ghost_direction = GHOST_SPEED;
	}

	if(ghost_position >= (DISPLAY_WIDTH - 64))
	{
		ghost_direction = 0 - GHOST_SPEED;
	}

	/* Here we draw the data onto an internal buffer and then pass the whole buffer on to the display driver. */
	/* 1. Draw the background in the buffer */
	drawRectangleInFrameBuf(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, COLOR_WHITE);
	/* 2. Draw the ghost bitmap in the buffer */
	drawBmpInFrameBuf(ghost_position, 88, 64, 64, priv_ghost_buffer);

	/* 3. Flush the frame buffer. */
	display_drawScreenBuffer(priv_frame_buffer);
}
#endif

