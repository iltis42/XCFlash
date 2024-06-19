/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_flash.h"
#include "Arduino.h"
#include "SetupNG.h"
#include <HardwareSerial.h>
#include <AdaptUGC.h>
#include <cmath>
#include <Serial.h>
#include "Flarm.h"
#include "driver/ledc.h"
#include "OTA.h"
#include "Version.h"
#include "Colors.h"
#include "flarmnetdata.h"
#include "Switch.h"
#include "SetupMenu.h"

OTA *ota = 0;
AdaptUGC *egl = 0;

// global color variables for adaptable display variant


static SetupMenu *menu=0;
bool inch2dot4=false;
Switch swUp;
Switch swDown;
Switch swMode;
float zoom=1.0;

extern "C" void app_main(void)
{
    initArduino();
    bool setupPresent;
    SetupCommon::initSetup( setupPresent );
    printf("Setup present: %d speed: %d\n", setupPresent, serial1_speed.get() );

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), WiFi%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "/EMB_FLASH" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }
    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    delay(100);
    //  serial1_speed.set( 1 );  // test for autoBaud

    Version V;
    std::string ver( "SW Ver.: " );
    ver += V.version();

    if( DISPLAY_W == 240 )
    	inch2dot4 = true;


    if( serial1_tx_enable.get() ){ // we don't need TX pin, so disable
      	serial1_tx_enable.set(0);
    }


    swUp.begin(GPIO_NUM_0, B_UP );


    for(int i=0; i<30; i++){  // 40
    	if( swUp.isClosed() ){
    		ota = new OTA();
    		ota->doSoftwareUpdate();
    		while(1){
    			delay(100);
    		}
    	}
    	delay( 100 );
    }

    // menu = new SetupMenu();
    // menu->begin();
    Switch::startTask();

    Flarm::begin();
    Serial::begin();

    if( Serial::selfTest() )
    	printf("Serial Loop Test OK");
    else
    	printf("Self Loop Test Failed");


    // gpio_set_drive_capability(GPIO_NUM_4, GPIO_DRIVE_CAP_MAX );
    int i=0;

    gpio_pad_select_gpio(GPIO_NUM_4);
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    gpio_pad_select_gpio(GPIO_NUM_9);
    gpio_set_direction(GPIO_NUM_9, GPIO_MODE_OUTPUT);

    while(1){
    	i++;
    	delay(100);
    	if( (i%20) == 0 || (i%20) == 2 || (i%20) == 5 ){
    		gpio_set_level( GPIO_NUM_4, 1 );
    		gpio_set_level( GPIO_NUM_9, 1 );
    		ESP_LOGI(FNAME,"LED 1 %d", i%20 );
    	}
    	if( (i%20) == 1 || (i%20) == 3 || (i%20) == 6 ){
    	    gpio_set_level( GPIO_NUM_4, 0 );
    	    gpio_set_level( GPIO_NUM_9, 0 );
    	    ESP_LOGI(FNAME,"LED 0 %d", i%20 );
    	}
    }


}
