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

//ma ei tea kuidas seda parandada veel
#include "ili9341.h"


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
// #define GHOST_TEST

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
	enum direction {
		UP,
		DOWN,
		LEFT,
		RIGHT
	};
	struct SnakeSegment
	{
		int x;
		int y;
	};
	struct Snake{
		struct SnakeSegment segments[MAX_SNAKE_LENGTH];
		int length;
		enum Direction direction;
	};

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
Private void drawSnake(void);
Private void snakeEat(void);

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

#define MAX_SNAKE_LENGTH 100
#define SNAKE_WIDTH 8
#define SNAKE_HEIGHT 8
Private struct Snake snake = {
	.segments = {{0, 0}},
	.length = 1,
	.direction = RIGHT
};
uint16_t * priv_snake_buffer;

//main menu

#define TFT_WIDTH 320 
#define TFT_HEIGHT 240

#define BUTTON_WIDTH 100
#define BUTTON_HEIGHT 20
#define BUTTON_X 20
#define BUTTON_Y_START 100
#define BUTTON_Y_GAP 20

#define SELECT_COLOR	ILI9341_RED
#define UNSELECT_COLOR	ILI9341_WHITE

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
	
	// load snake image
	priv_snake_buffer = heap_caps_malloc(SNAKE_WIDTH * SNAKE_HEIGHT * sizeof(uint16_t), MALLOC_CAP_DMA);
	assert(priv_snake_buffer);
	sdCard_Read_bmp_file("/ghost.bmp", priv_snake_buffer); //USES GHOST.BMP FOR NOW

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

	//MAIN MENU
	//init display
	ili9341_init(TFT_WIDTH, TFT_HEIGHT);

	//fill screen with black
	ili9341_fill_screen(ILI9341_BLACK);

	//set text color
	ili9341_set_text_color(ILI9341_WHITE, ILI9341_BLACK);

	//display menu
	ili9341_draw_string(40, 50, "Main Menu", 2);


	draw_button("Start Game", BUTTON_X, BUTTON_Y_START, BUTTON_WIDTH, BUTTON_HEIGHT, SELECT_COLOR);
	draw_button("Settings", BUTTON_X, BUTTON_Y_START + BUTTON_Y_GAP, BUTTON_WIDTH, BUTTON_HEIGHT, UNSELECT_COLOR);
	draw_button("High Scores", BUTTON_X, BUTTON_Y_START + 2*BUTTON_Y_GAP, BUTTON_WIDTH, BUTTON_HEIGHT, UNSELECT_COLOR);
	draw_button("Exit", BUTTON_X, BUTTON_Y_START + 3*BUTTON_Y_GAP, BUTTON_WIDTH, BUTTON_HEIGHT, UNSELECT_COLOR);

	/* Main CPU cycle */
	while(1)
	{
		vTaskDelayUntil( &xLastWakeTime, xFrequency );
#ifdef GHOST_TEST
		/* Simple test for drawing a moving bitmap on the screen. */
		drawGhost();
#endif
		drawSnake();
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

Private void drawSnake(void) {
    // Clear previous snake position
	drawRectangleInFrameBuf(snake.body[snake.length - 1].x, snake.body[snake.length - 1].y, SNAKE_WIDTH, SNAKE_HEIGHT, COLOR_WHITE);

    // Update snake position
	for (int i = snake.length - 1; i > 0; i--) {
		snake.body[i].x = snake.body[i - 1].x;
		snake.body[i].y = snake.body[i - 1].y;
	}

    if (snake_direction == RIGHT) {
        snake.body[0].x += 1;
    } else if (snake_direction == DOWN) {
        snake.body[0].y += 1;
    } else if (snake_direction == LEFT) {
        snake.body[0].x -= 1;
    } else if (snake_direction == UP) {
        snake.body[0].y -= 1;
    }

    // Draw the snake at its new position
    for (int i = 0; i < snake.length; i++) {
        drawBmpInFrameBuf(snake.body[i].x, snake.body[i].y, SNAKE_WIDTH, SNAKE_HEIGHT, priv_snake_buffer);
    }

    // Flush the frame buffer
    display_drawScreenBuffer(priv_frame_buffer);
}
Private void snakeEat(void) {
	// Increase the length of the snake
	snake.length += 1;

	// Add a new segment to the snake
	snake.body[snake.length - 1].x = snake.body[snake.length - 2].x;
	snake.body[snake.length - 1].y = snake.body[snake.length - 2].y;
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

