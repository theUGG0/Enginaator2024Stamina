/*
**====================================================================================
** Imported definitions
**====================================================================================
*/
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"

#include "esp_task_wdt.h"

/* Display driver is defined in display.c and display.h
 * Note that when you add new files to the project, then CMakeLists.txt also needs to be updated for these files to be built. */
#include "display.h"
/* The SD card functionality has been moved to its own separate file for this project. */
#include "sdCard.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
static esp_adc_cal_characteristics_t adc1_chars;
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
/* #define GHOST_TEST */


#define MAX_SNAKE_LENGTH 100
#define GRID_WIDTH 20
#define GRID_HEIGHT 20

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
	enum ScreenState{
		SCREEN_MAIN_MENU,
		SCREEN_GAME,
		SCREEN_SETTINGS
	};
	enum MenuOption{
		OPTION_LEVELS,
		OPTION_SETTINGS,
	};
	enum Direction {
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
		struct SnakeSegment body[MAX_SNAKE_LENGTH];
		int length;
		enum Direction direction;
		int status;
	};
	struct Food
	{
		int x;
		int y;
		const char* foodFilePath;
	};
	struct intTriple{
		int a;
		int b;
		int c;
	};
/*
**====================================================================================
** Private function forward declarations
**====================================================================================
*/

Private uint8_t initialize_spi(void);
Private void drawRectangleInFrameBuf(int xPos, int yPos, int width, int height, uint16_t color);
Private void drawBmpInFrameBuf(int xPos, int yPos, int width, int height, uint16_t * data_buf);
Private void drawSnake(void);
Private void snakeEat(void);
Private void snakeDie(void);
Private void snakeCollision(void);
Private void drawBackground(void);
Private void drawSnakeGame(void);
Private void foodSpawn(void);
Private void drawFood(void);
Private void gameLoop(void);
Private void menuLoop(void);
Private void initLevel(void);
Private void drawMenu(void);
Private void optionsLoop(void);
Private void drawOptions(void);
Private void changeMenuSelection(int selectedMenuBtn);
Private void updateOptionSelection(int option);
Private void updateSnakePosition(void);

Private struct intTriple handleInputs(void);

Private void drawEnginaator(void);

/*
**====================================================================================
** Private variable declarations
**====================================================================================
*/

uint16_t * priv_frame_buffer;
Private struct Snake snake = {
	.body = {{0, 0}},
	.length = 2,
	.direction = RIGHT,
	.status = 0
};
uint16_t * priv_snake_buffer;
uint16_t * priv_snake_body_buffer;
uint16_t * priv_food_buffer;
uint16_t * priv_enginaator_buffer;
uint16_t * priv_levelselect1_buffer;
uint16_t * priv_levelselect2_buffer;
uint16_t * priv_levelselect3_buffer;
uint16_t * priv_settings_buffer;
uint16_t * priv_settingsbtn_buffer;
int level = 1;
int option = 1;
int selectedMenuBtn = 4;
int gameSpeed = 1;
struct Food food;

enum ScreenState currentScreen = SCREEN_MAIN_MENU;

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
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 0, &adc1_chars);
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

		changeMenuSelection(1);

		// load snake image
		priv_snake_buffer = heap_caps_malloc(20 * 20 * sizeof(uint16_t), MALLOC_CAP_DMA);
		assert(priv_snake_buffer);
		sdCard_Read_bmp_file("/images/snake_head.bmp", priv_snake_buffer);

		priv_snake_body_buffer = heap_caps_malloc(20 * 20 * sizeof(uint16_t), MALLOC_CAP_DMA);
		assert(priv_snake_body_buffer);
		sdCard_Read_bmp_file("/images/snake_body.bmp", priv_snake_body_buffer);

		/* Load an image from the SD Card into the frame buffer */
/* 		sdCard_Read_bmp_file("/logo.bmp", priv_frame_buffer);


		display_drawBitmap(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, priv_frame_buffer); */
	}

	/* Five second delay... */
	vTaskDelay(5000u / portTICK_PERIOD_MS);
	



	/* The idea is that we will try to keep a cyclic process that is called every 40 milliseconds, we call our
	 * drawing functions and then delay for the period remaining.
	 */

	TickType_t xLastWakeTime;
	const TickType_t xFrequency = 40u / portTICK_PERIOD_MS;
	xLastWakeTime = xTaskGetTickCount ();

	/* Main CPU cycle */
	while(1)
	{

		switch (currentScreen) {
        case SCREEN_MAIN_MENU:
            menuLoop();
            break;
        case SCREEN_GAME:
        	gameLoop();
            break;
        case SCREEN_SETTINGS:
			optionsLoop();
            break;
		}

		vTaskDelayUntil( &xLastWakeTime, xFrequency );
		
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



Private void drawSnakeGame(void) {
	// Draw the background
	drawBackground();
	updateSnakePosition();
	// check for snake collision
	snakeCollision();
	if (snake.status != 1) {
		return;
	}
	// Draw the snake
	drawFood();
	drawSnake();


	// Flush the frame buffer
	display_drawScreenBuffer(priv_frame_buffer);


}

Private void drawMenu(void){

    printf("Drawing menu...\n");

	// Draw the background
	drawBackground();

    printf("Drawing menu butoons\n");
	drawBmpInFrameBuf(50, 50, 100, 40, priv_levelselect1_buffer);
	drawBmpInFrameBuf(150, 50, 100, 40, priv_levelselect2_buffer);
	drawBmpInFrameBuf(50, 100, 100, 40, priv_levelselect3_buffer);
	drawBmpInFrameBuf(150, 100, 100, 40, priv_settingsbtn_buffer);

	
	display_drawScreenBuffer(priv_frame_buffer);
}

Private void drawOptions(void){

	// Draw the background

	drawBmpInFrameBuf(100, 50, 100, 40, priv_settings_buffer);
	
	display_drawScreenBuffer(priv_frame_buffer);
}

Private void changeMenuSelection(int selectedMenuBtn) {
	const char* level1path = "/images/lvl1.bmp";
	const char* level2path = "/images/lvl2.bmp";
	const char* level3path = "/images/lvl3.bmp";
	const char* optionsbtnpath = "/images/options.bmp";


	if (selectedMenuBtn == 4){

	    printf("Drawing menu btn 1...\n");
		level1path = "/images/lvl1h.bmp";
	} else if (selectedMenuBtn == 3){
		level2path = "/images/lvl2h.bmp";
	} else if (selectedMenuBtn == 2){
		level3path = "/images/lvl3h.bmp";
	}
	else if (selectedMenuBtn == 1){
		optionsbtnpath = "/images/optionsh.bmp";
	}

	
	priv_levelselect1_buffer = heap_caps_malloc(100 * 40 * sizeof(uint16_t), MALLOC_CAP_DMA);
	assert(priv_levelselect1_buffer);
	sdCard_Read_bmp_file(level1path, priv_levelselect1_buffer);

	priv_levelselect2_buffer = heap_caps_malloc(100 * 40 * sizeof(uint16_t), MALLOC_CAP_DMA);
	assert(priv_levelselect2_buffer);
	sdCard_Read_bmp_file(level2path, priv_levelselect2_buffer);

	priv_levelselect3_buffer = heap_caps_malloc(100 * 40 * sizeof(uint16_t), MALLOC_CAP_DMA);
	assert(priv_levelselect3_buffer);
	sdCard_Read_bmp_file(level3path, priv_levelselect3_buffer);

	priv_settingsbtn_buffer = heap_caps_malloc(100 * 40 * sizeof(uint16_t), MALLOC_CAP_DMA);
	assert(priv_settingsbtn_buffer);
	sdCard_Read_bmp_file(optionsbtnpath, priv_settingsbtn_buffer);
}

Private struct intTriple handleInputs(void) {
	int joystick_x = adc1_get_raw(ADC1_CHANNEL_2);
	int joystick_y = adc1_get_raw(ADC1_CHANNEL_7);
	int joystick_btn = gpio_get_level(GPIO_NUM_18);
	struct intTriple returnValues = {joystick_x, joystick_y, joystick_btn};
	return returnValues;

}

Private void moveSnake(struct intTriple returnValues) {
	int joystick_x = returnValues.a;
	int joystick_y = returnValues.b;
	if (joystick_x > 4000 && snake.direction != RIGHT) {
		snake.direction = LEFT;
	} else if (joystick_x < 10 && snake.direction != LEFT) {
		snake.direction = RIGHT;
	} if (joystick_y > 4000 && snake.direction != UP) {
		snake.direction = DOWN;
	} else if (joystick_y < 10 && snake.direction != DOWN) {
		snake.direction = UP;
	}
}

TickType_t lastRenderTicks = 0;

Private void gameLoop(void) {

	if (xTaskGetTickCount() - lastRenderTicks > 40) {	
		drawSnakeGame();
		
		if (level == 2){
			drawEnginaator();	
		} else if (level == 3){
			// OMALOOMING
		}
		lastRenderTicks = xTaskGetTickCount();
	}
	moveSnake(handleInputs());

}

Private void menuLoop(void) {

	drawMenu();

	struct intTriple returnValues = handleInputs();
	int joystick_x = returnValues.a;
	int joystick_y = returnValues.b;
	int joystick_btn = returnValues.c;
	if (joystick_x > 4000 && selectedMenuBtn != 4) {
		selectedMenuBtn++;
		changeMenuSelection(selectedMenuBtn);
	} else if (joystick_x < 10 && selectedMenuBtn != 1) {
		selectedMenuBtn--;
		changeMenuSelection(selectedMenuBtn);
	}
	if (joystick_btn == 0) {
		if (selectedMenuBtn != 4) {
			level = option;
			currentScreen = SCREEN_GAME;
			initLevel();
		}
		else {
			currentScreen = SCREEN_SETTINGS;
		}
		
	}
}

Private void updateOptionSelection(int option) {
	const char* optionspath = "/images/speed1.bmp";

	if (option == 2){
		const char* optionspath = "/images/speed2.bmp";
	} else if (option == 3){
		const char* optionspath = "/images/speed3.bmp";
	}

	priv_settings_buffer = heap_caps_malloc(100 * 20 * sizeof(uint16_t), MALLOC_CAP_DMA);
	assert(priv_settings_buffer);
	sdCard_Read_bmp_file(optionspath, priv_settings_buffer);
}

Private void optionsLoop(void) {

	drawOptions();

	struct intTriple returnValues = handleInputs();
	int joystick_x = returnValues.a;
	int joystick_y = returnValues.b;
	int joystick_btn = returnValues.c;
	if (joystick_x > 4000 && gameSpeed != 3) {
		gameSpeed++;
		updateOptionSelection(gameSpeed);
	} else if (joystick_x < 10 && gameSpeed != 1) {
		gameSpeed--;
		updateOptionSelection(gameSpeed);
	}
	if (joystick_btn == 1) {
		currentScreen = SCREEN_MAIN_MENU;
	}
}

Private void drawBackground(void) {
	// Draw the background
	drawRectangleInFrameBuf(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, COLOR_WHITE);
}

Private void drawSnake(void) {

    // Clear previous snake position
/* 	drawRectangleInFrameBuf(snake.body[snake.length - 1].x, snake.body[snake.length - 1].y, GRID_WIDTH, GRID_HEIGHT, COLOR_WHITE); */
    // Update snake position
	for (int i = snake.length - 1; i > 0; i--) {
		snake.body[i].x = snake.body[i - 1].x;
		snake.body[i].y = snake.body[i - 1].y;
	}



}

Private void updateSnakePosition(void) {
    if (snake.direction == RIGHT) {
        snake.body[0].x += 20;
    } else if (snake.direction == DOWN) {
        snake.body[0].y += 20;
    } else if (snake.direction == LEFT) {
        snake.body[0].x -= 20;
    } else if (snake.direction == UP) {
        snake.body[0].y -= 20;
    }
    // Draw the snake at its new position
    for (int i = 0; i < snake.length; i++) {
		if (i == 0){
        	drawBmpInFrameBuf(snake.body[i].x, snake.body[i].y, GRID_WIDTH, GRID_HEIGHT, priv_snake_buffer);
		} else {
			drawBmpInFrameBuf(snake.body[i].x, snake.body[i].y, GRID_WIDTH, GRID_HEIGHT, priv_snake_body_buffer);
		}
    }
}

Private void foodSpawn(void) {

	food.x = (rand() % (DISPLAY_WIDTH / GRID_WIDTH))*GRID_WIDTH;
	food.y = (rand() % (DISPLAY_HEIGHT / GRID_HEIGHT))*GRID_HEIGHT;

    // Select a random food type
    int randomFoodType = rand() % 6;

    // Get the file path for the selected food type
    switch (randomFoodType) {
        case 1:
            food.foodFilePath = "/images/apple.bmp";
            break;
        case 2:
            food.foodFilePath = "/images/cherry.bmp";
            break;
        case 3:
            food.foodFilePath = "/images/grapes.bmp";
            break;
		case 4:
			food.foodFilePath = "/images/pineapple.bmp";
			break;
		case 5:
			food.foodFilePath = "/images/tomato.bmp";
			break;
		case 6:
			food.foodFilePath = "/images/watermelon.bmp";
			break;
		default:
			food.foodFilePath = "/images/apple.bmp";
			break;
    }

    priv_food_buffer = heap_caps_malloc(GRID_HEIGHT * GRID_WIDTH * sizeof(uint16_t), MALLOC_CAP_DMA);
	assert(priv_food_buffer);
	sdCard_Read_bmp_file(food.foodFilePath, priv_food_buffer);
}
Private void drawFood(void){
	drawBmpInFrameBuf(food.x, food.y, GRID_WIDTH, GRID_HEIGHT, priv_food_buffer);
}

Private void snakeEat(void) {
	// Increase the length of the snake
	snake.length += 1;

	// Add a new segment to the snake
	snake.body[snake.length - 1].x = snake.body[snake.length - 2].x;
	snake.body[snake.length - 1].y = snake.body[snake.length - 2].y;
}

Private void initLevel(void) {
	//set speed
	const TickType_t xFrequency = (40u / portTICK_PERIOD_MS) * gameSpeed;

	// Reset the snake
	snake.status = 1;
	snake.length = 1;
	snake.body[0].x = (rand() % (DISPLAY_WIDTH / GRID_WIDTH))*GRID_WIDTH;
	snake.body[0].y = (rand() % (DISPLAY_HEIGHT / GRID_HEIGHT))*GRID_HEIGHT;

	snake.direction = RIGHT;

    foodSpawn();
}

Private void drawEnginaator(void) {
	priv_enginaator_buffer = heap_caps_malloc(156*40*sizeof(uint16_t), MALLOC_CAP_DMA);
    assert(priv_enginaator_buffer);
    sdCard_Read_bmp_file("/enginaator.bmp", priv_enginaator_buffer);
	drawBmpInFrameBuf((DISPLAY_WIDTH/2)-156/2, (DISPLAY_HEIGHT/2)-40/2, 156, 40, priv_enginaator_buffer);
}

Private void snakeDie(void) {
	snake.status = 0;
	currentScreen = SCREEN_MAIN_MENU;
	printf("Snake ded\n");
}

Private void snakeCollision(void) {

	// Check if the snake has collided with the walls
	if (snake.body[0].x < 0 || snake.body[0].x >= DISPLAY_WIDTH || snake.body[0].y < 0 || snake.body[0].y >= DISPLAY_HEIGHT) {
		snakeDie();
	}
	else if (level == 2 && (snake.body[0].x < (DISPLAY_WIDTH/2)-156/2 || snake.body[0].x >= (DISPLAY_WIDTH/2)+156/2 || snake.body[0].y < (DISPLAY_HEIGHT/2)-40/2 || snake.body[0].y >= (DISPLAY_HEIGHT/2)+40/2)){
		snakeDie();
	}

	// Check if the snake has collided with itself
	for (int i = 1; i < snake.length; i++) {
		if (snake.body[0].x == snake.body[i].x && snake.body[0].y == snake.body[i].y) {
			snakeDie();
		}
	}
	if (snake.body[0].x == food.x && snake.body[0].y == food.y){
		foodSpawn();
		snakeEat();

		printf("Snake eat\n");
	}
}

