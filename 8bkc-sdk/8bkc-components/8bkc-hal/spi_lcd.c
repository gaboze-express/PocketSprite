// Copyright 2016-2017 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "esp_heap_alloc_caps.h"
#include "driver/ledc.h"

#include "sdkconfig.h"


#define PIN_NUM_MOSI CONFIG_HW_LCD_MOSI_GPIO
#define PIN_NUM_CLK  CONFIG_HW_LCD_CLK_GPIO
#define PIN_NUM_CS   CONFIG_HW_LCD_CS_GPIO
#define PIN_NUM_DC   CONFIG_HW_LCD_DC_GPIO
#define PIN_NUM_RST  CONFIG_HW_LCD_RESET_GPIO
#define PIN_NUM_BCKL CONFIG_HW_LCD_BL_GPIO


#define MADCTL_MY       0x80
#define MADCTL_MX       0x40
#define MADCTL_MV       0x20
#define MADCTL_ML       0x10
#define MADCTL_RGB      0x00
#define MADCTL_BGR      0x08
#define MADCTL_MH       0x04

#define TFT_CMD_SWRESET 0x01
#define TFT_CMD_SLEEP 0x11
#define TFT_CMD_DISPLAY_OFF 0x28


static const int DUTY_MAX = 0x1fff;
static const int LCD_BACKLIGHT_ON_VALUE = 1;
static void backlight_init();
/*
 The LCD needs a bunch of command/argument values to be initialized. They are stored in this struct.
*/
typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes; //No of data in data; bit 7 = delay after set; 0xFF = end of cmds.
} ili_init_cmd_t;

#if (CONFIG_HW_LCD_TYPE == 2)
//for ST7735R
static const ili_init_cmd_t st_sleep_cmds[] = {
    {TFT_CMD_SWRESET, {0}, 0x80},
    {TFT_CMD_DISPLAY_OFF, {0}, 0x80},
    {TFT_CMD_SLEEP, {0}, 0x80},
    {0, {0}, 0xff}
};

static const ili_init_cmd_t ili_init_cmds[]={
    {0x01, {0}, 0x80}, //SWRESET
    //   {0x00, {0}, 0x80}, //NOP delay?
    {0x11, {0}, 0x80}, //SLEEPOUT
    {0xB1, {0x01, 0x2C, 0x2D}, 3}, //FRMCTL1 porch padding
    {0xB2, {0x01, 0x2C, 0x2D}, 3}, //FRMCTL2 porch padding
    {0xB3, {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}, 6}, //FRMCTL3 porch padding
    {0xB4, {0x07}, 1}, //INVCTR
    {0xC0, {0xA2, 0x02, 0x84}, 3}, //PWCTR1
    {0xC1, {0xC5}, 1}, //PWCTR2
    {0xC2, {0x0A, 0x00}, 2}, //PWCTR3
    {0xC3, {0x8A, 0x2A}, 2}, //PWCTR4
    {0xC4, {0x8A, 0xEE}, 2}, //PWCTR5
    {0xC5, {0x0E}, 1}, //VMCTR1
    {0x20, {0}, 0}, //INVOFF
    #if CONFIG_LCD_ROTATED
    //{0x36, {(1<<3)|(1<<5)|(1<<6)|(0<<7)}, 1}, //MADCTL RGB, MV, MX, MY
    {0x36, {(MADCTL_MV)|(MADCTL_MX)}, 1},
    #else
    {0x36, {(1<<3)|(0<<5)|(1<<6)|(1<<7)}, 1}, //MADCTL
    #endif
    {0x3A, {0x05}, 1|0x80}, //COLMOD
    {0x2A, {0x00, 0x02, 0x00, 0x81}, 4}, //CASET
    {0x2B, {0x00, 0x01, 0x00, 0xA0}, 4}, //RASET
    //commented values are for blacktab, good luck!
    //   {0xE0, {0x09, 0x16, 0x09, 0x20, 0x21, 0x1B, 0x13, 0x19, 0x17, 0x15, 0x1E, 0x2B, 0x04, 0x05, 0x02, 0x0E}, 16},
    //   {0xE1, {0x0B, 0x14, 0x08, 0x1E, 0x22, 0x1D, 0x18, 0x1E, 0x1B, 0x1A, 0x24, 0x2B, 0x06, 0x06, 0x02, 0x0F}, 16},
    {0xE0, {0x02, 0x1c, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2d, 0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10}, 16},
    {0xE1, {0x03, 0x1d, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D, 0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10}, 16},
    {0x13, {0}, 0x0}, //NORON
    {0x29, {0}, 0x80}, //DISPON
    {0, {0}, 0xff}
};

#endif

#if (CONFIG_HW_LCD_TYPE == 1)
//for ST7789V
static const ili_init_cmd_t ili_init_cmds[]={
    {0x36, {(0<<5)|(0<<6)}, 1},
    {0x3A, {0x55}, 1},
    {0xB2, {0x0c, 0x0c, 0x00, 0x33, 0x33}, 5},
    {0xB7, {0x45}, 1},
    {0xBB, {0x2B}, 1},
    {0xC0, {0x2C}, 1},
    {0xC2, {0x01, 0xff}, 2},
    {0xC3, {0x11}, 1},
    {0xC4, {0x20}, 1},
    {0xC6, {0x0f}, 1},
    {0xD0, {0xA4, 0xA1}, 1},
    {0xE0, {0xD0, 0x00, 0x05, 0x0E, 0x15, 0x0D, 0x37, 0x43, 0x47, 0x09, 0x15, 0x12, 0x16, 0x19}, 14},
    {0xE1, {0xD0, 0x00, 0x05, 0x0D, 0x0C, 0x06, 0x2D, 0x44, 0x40, 0x0E, 0x1C, 0x18, 0x16, 0x19}, 14},
    {0x11, {0}, 0x80},
    {0x29, {0}, 0x80},
    {0, {0}, 0xff}
};

#endif

#if (CONFIG_HW_LCD_TYPE == 0)

//for ILI9341
static const ili_init_cmd_t ili_init_cmds[]={
    {0xCF, {0x00, 0x83, 0X30}, 3},
    {0xED, {0x64, 0x03, 0X12, 0X81}, 4},
    {0xE8, {0x85, 0x01, 0x79}, 3},
    {0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5},
    {0xF7, {0x20}, 1},
    {0xEA, {0x00, 0x00}, 2},
    {0xC0, {0x26}, 1},
    {0xC1, {0x11}, 1},
    {0xC5, {0x35, 0x3E}, 2},
    {0xC7, {0xBE}, 1},
    {0x36, {0x48}, 1}, //not sure if this mirrors the display correctly...
    {0x3A, {0x55}, 1},
    {0xB1, {0x00, 0x1B}, 2},
    {0xF2, {0x08}, 1},
    {0x26, {0x01}, 1},
    {0xE0, {0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0X87, 0x32, 0x0A, 0x07, 0x02, 0x07, 0x05, 0x00}, 15},
    {0XE1, {0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78, 0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A, 0x1F}, 15},
    {0x2A, {0x00, 0x00, 0x00, 0xEF}, 4},
    {0x2B, {0x00, 0x00, 0x01, 0x3f}, 4}, 
    {0x2C, {0}, 0},
    {0xB7, {0x07}, 1},
    {0xB6, {0x0A, 0x82, 0x27, 0x00}, 4},
    {0x11, {0}, 0x80},
    {0x29, {0}, 0x80},
    {0, {0}, 0xff},
};

#endif

static spi_device_handle_t spi;


//Send a command to the ILI9341. Uses spi_device_transmit, which waits until the transfer is complete.
void ili_cmd(spi_device_handle_t spi, const uint8_t cmd) 
{
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=8;                     //Command is 8 bits
    t.tx_buffer=&cmd;               //The data is the cmd itself
    t.user=(void*)0;                //D/C needs to be set to 0
    ret=spi_device_transmit(spi, &t);  //Transmit!
    assert(ret==ESP_OK);            //Should have had no issues.
}

//Send data to the ILI9341. Uses spi_device_transmit, which waits until the transfer is complete.
void ili_data(spi_device_handle_t spi, const uint8_t *data, int len) 
{
    esp_err_t ret;
    spi_transaction_t t;
    if (len==0) return;             //no need to send anything
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=len*8;                 //Len is in bytes, transaction length is in bits.
    t.tx_buffer=data;               //Data
    t.user=(void*)1;                //D/C needs to be set to 1
    ret=spi_device_transmit(spi, &t);  //Transmit!
    assert(ret==ESP_OK);            //Should have had no issues.
}

//This function is called (in irq context!) just before a transmission starts. It will
//set the D/C line to the value indicated in the user field.
void ili_spi_pre_transfer_callback(spi_transaction_t *t) 
{
    int dc=(int)t->user;
    gpio_set_level(PIN_NUM_DC, dc);
}

//Initialize the display
void ili_init(spi_device_handle_t spi) 
{
    int cmd=0;
    //Initialize non-SPI GPIOs
    gpio_set_direction(PIN_NUM_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_BCKL, GPIO_MODE_OUTPUT);

    //Reset the display
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(100 / portTICK_RATE_MS);
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(100 / portTICK_RATE_MS);

    //Send all the commands
    while (ili_init_cmds[cmd].databytes!=0xff) {
        uint8_t dmdata[16];
        ili_cmd(spi, ili_init_cmds[cmd].cmd);
        //Need to copy from flash to DMA'able memory
        memcpy(dmdata, ili_init_cmds[cmd].data, 16);
        ili_data(spi, dmdata, ili_init_cmds[cmd].databytes&0x1F);
        if (ili_init_cmds[cmd].databytes&0x80) {
            vTaskDelay(100 / portTICK_RATE_MS);
        }
        cmd++;
    }

    backlight_init();
}

void setBrightness(int value)
{
    int duty = DUTY_MAX * (value * 0.01f);

    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty, 500);
    ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT);
}

static void backlight_init()
{
    //configure timer0
    ledc_timer_config_t ledc_timer;
    memset(&ledc_timer, 0, sizeof(ledc_timer));

    ledc_timer.duty_resolution = LEDC_TIMER_13_BIT; //set timer counter bit number
    ledc_timer.freq_hz = 5000;              //set frequency of pwm
    ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;   //timer mode,
    ledc_timer.timer_num = LEDC_TIMER_0;    //timer index

    ledc_timer_config(&ledc_timer);

    //set the configuration
    ledc_channel_config_t ledc_channel;
    memset(&ledc_channel, 0, sizeof(ledc_channel));

    //set LEDC channel 0
    ledc_channel.channel = LEDC_CHANNEL_0;
    //set the duty for initialization.(duty range is 0 ~ ((2**duty_resolution)-1)
    ledc_channel.duty = (LCD_BACKLIGHT_ON_VALUE) ? 0 : DUTY_MAX;
    //GPIO number
    ledc_channel.gpio_num = PIN_NUM_BCKL;
    //GPIO INTR TYPE, as an example, we enable fade_end interrupt here.
    ledc_channel.intr_type = LEDC_INTR_FADE_END;
    //set LEDC mode, from ledc_mode_t
    ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
    //set LEDC timer source, if different channel use one timer,
    //the frequency and duty_resolution of these channels should be the same
    ledc_channel.timer_sel = LEDC_TIMER_0;

    ledc_channel_config(&ledc_channel);

    //initialize fade service.
    ledc_fade_func_install(0);

    // duty range is 0 ~ ((2**duty_resolution)-1)
    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (LCD_BACKLIGHT_ON_VALUE) ? DUTY_MAX : 0, 500);
    ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT);

    printf("Backlight initialization done.\n");
}


static void send_header_start(spi_device_handle_t spi, int xpos, int ypos, int w, int h)
{
    esp_err_t ret;
    int x;
    //Transaction descriptors. Declared static so they're not allocated on the stack; we need this memory even when this
    //function is finished because the SPI driver needs access to it even while we're already calculating the next line.
    static spi_transaction_t trans[5];

    //In theory, it's better to initialize trans and data only once and hang on to the initialized
    //variables. 
    for (x=0; x<5; x++) {
        memset(&trans[x], 0, sizeof(spi_transaction_t));
        if ((x&1)==0) {
            //Even transfers are commands
            trans[x].length=8;
            trans[x].user=(void*)0;
        } else {
            //Odd transfers are data
            trans[x].length=8*4;
            trans[x].user=(void*)1;
        }
        trans[x].flags=SPI_TRANS_USE_TXDATA;
    }
    trans[0].tx_data[0]=0x2A;           //Column Address Set
    trans[1].tx_data[0]=xpos>>8;              //Start Col High
    trans[1].tx_data[1]=xpos;              //Start Col Low
    trans[1].tx_data[2]=(xpos+w-1)>>8;       //End Col High
    trans[1].tx_data[3]=(xpos+w-1)&0xff;     //End Col Low
    trans[2].tx_data[0]=0x2B;           //Page address set
    trans[3].tx_data[0]=ypos>>8;        //Start page high
    trans[3].tx_data[1]=ypos&0xff;      //start page low
    trans[3].tx_data[2]=(ypos+h-1)>>8;    //end page high
    trans[3].tx_data[3]=(ypos+h-1)&0xff;  //end page low
    trans[4].tx_data[0]=0x2C;           //memory write

    //Queue all transactions.
    for (x=0; x<5; x++) {
        ret=spi_device_queue_trans(spi, &trans[x], portMAX_DELAY);
        assert(ret==ESP_OK);
    }

    //When we are here, the SPI driver is busy (in the background) getting the transactions sent. That happens
    //mostly using DMA, so the CPU doesn't have much to do here. We're not going to wait for the transaction to
    //finish because we may as well spend the time calculating the next line. When that is done, we can call
    //send_line_finish, which will wait for the transfers to be done and check their status.
}


void send_header_cleanup(spi_device_handle_t spi) 
{
    spi_transaction_t *rtrans;
    esp_err_t ret;
    //Wait for all 5 transactions to be done and get back the results.
    for (int x=0; x<5; x++) {
        ret=spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
        assert(ret==ESP_OK);
        //We could inspect rtrans now if we received any info back. The LCD is treated as write-only, though.
    }
}

void spi_lcd_send(int x, int y, int w, int h, uint16_t *scr) {
    esp_err_t ret;
    spi_transaction_t trans={0};
    spi_transaction_t *rtrans;
	send_header_start(spi, x, y, w, h);
	trans.length=w*h*16;
	trans.user=(void*)1;
	trans.tx_buffer=scr;
    ret=spi_device_queue_trans(spi, &trans, portMAX_DELAY);  //Transmit!
    assert(ret==ESP_OK);            //Should have had no issues.
	send_header_cleanup(spi);
    ret=spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
    assert(ret==ESP_OK);
}

void st7735r_poweroff()
{
    esp_err_t err = ESP_OK;
    
    // fade off backlight
    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (LCD_BACKLIGHT_ON_VALUE) ? 0 : DUTY_MAX, 100);
    ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_WAIT_DONE);

    // Disable LCD panel
    int cmd = 0;
    while (st_sleep_cmds[cmd].databytes != 0xff)
    {
        ili_cmd(spi, st_sleep_cmds[cmd].cmd);
        ili_data(spi, st_sleep_cmds[cmd].data, st_sleep_cmds[cmd].databytes & 0x7f);
        if (st_sleep_cmds[cmd].databytes & 0x80)
        {
            vTaskDelay(100 / portTICK_RATE_MS);
        }
        cmd++;
    }
}


void spi_lcd_init() {
    esp_err_t ret;
    spi_bus_config_t buscfg={
        .miso_io_num=-1,
        .mosi_io_num=PIN_NUM_MOSI,
        .sclk_io_num=PIN_NUM_CLK,
        .quadwp_io_num=-1,
        .quadhd_io_num=-1,
        #if (CONFIG_HW_LCD_TYPE == 2)
        .max_transfer_sz=(160*128*2)+16
        #else
        .max_transfer_sz=(320*240*2)+16
        #endif
    };
    spi_device_interface_config_t devcfg={
        .clock_speed_hz=22000000,               //Clock out at 26 MHz. Yes, that's heavily overclocked.
        .mode=0,                                //SPI mode 0
        .spics_io_num=PIN_NUM_CS,               //CS pin
        .queue_size=10,               //We want to be able to queue this many transfers
        .pre_cb=ili_spi_pre_transfer_callback,  //Specify pre-transfer callback to handle D/C line
    };

    //Initialize the SPI bus
    ret=spi_bus_initialize(HSPI_HOST, &buscfg, 1);
    assert(ret==ESP_OK);
    //Attach the LCD to the SPI bus
    ret=spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
    assert(ret==ESP_OK);
    //Initialize the LCD
    ili_init(spi);
    printf("LCD initialize done. \n");
}
