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
#include <AdaptUGC.h>  // we keep it, maybe at times a small 1.4 inch display can be added
#include <cmath>
#include <Serial.h>
#include "Flarm.h"
#include "driver/ledc.h"
#include "OTA.h"
#include "Version.h"
#include "Colors.h"
#include "Switch.h"
#include "driver/temp_sensor.h"

OTA *ota = 0;
AdaptUGC *egl = 0;

// global color variables for adaptable display variant

typedef enum e_flash_freq { FLASH_OFF, FLASH_LOW, FLASH_MED, FLASH_HIGH } e_flash_freq;

static e_flash_freq flash_freq = FLASH_LOW;

bool inch2dot4=false;
Switch swMode;
float zoom=1.0;

static bool moving_state = false;

bool isMoving(){
	bool moving=false;
	float gs,track;
	if( Flarm::getGPS( gs, track ) ){
		ESP_LOGI(FNAME,"GS: %f",  gs );
		if( gs > 0.5 /* 15 */ )
			moving = true;
	}
	if( moving_state != moving ){
		moving_state = moving;
		ESP_LOGI(FNAME,"New Moving state: %d",  moving_state );
	}
	return moving;
}

void led_on(){
    gpio_set_level( GPIO_NUM_4, 1 );
	gpio_set_level( GPIO_NUM_9, 1 );
	ESP_LOGI(FNAME,"LED 1");
}

void led_off(){
    gpio_set_level( GPIO_NUM_4, 0 );
	gpio_set_level( GPIO_NUM_9, 0 );
	ESP_LOGI(FNAME,"LED 0");
}

#define PERIOD 50
#define FLASHES (1000/(PERIOD))  // 10

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

    temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(temp_sensor_set_config(temp_sensor));
    ESP_ERROR_CHECK(temp_sensor_start());
    // Get converted sensor data
    float tsens_out;
    ESP_ERROR_CHECK(temp_sensor_read_celsius(&tsens_out));
    ESP_LOGI(FNAME, "Temperature in %f °C", tsens_out);

    Version V;
    std::string ver( "SW Ver.: " );
    ver += V.version();

    if( serial1_tx_enable.get() ){ // we don't need TX pin, so disable
      	serial1_tx_enable.set(0);
    }
    swMode.begin(GPIO_NUM_0, B_UP );

    // Init I/O Pads
    gpio_pad_select_gpio(GPIO_NUM_4);
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    gpio_pad_select_gpio(GPIO_NUM_9);
    gpio_set_direction(GPIO_NUM_9, GPIO_MODE_OUTPUT);

    // Initial function test
    for( int i=0; i<3; i++ ){
    	led_on();
     	delay( 100 );
     	led_off();
     	delay(100);
     }

    for(int i=0; i<30; i++){  // 40
    	if( swMode.isClosed() ){
    		ota = new OTA();
    		ota->doSoftwareUpdate();
    		while(1){
    			delay(100);
    		}
    	}
    	delay( 100 );
    }
    Switch::startTask();
    Flarm::begin();
    Serial::begin();

    if( Serial::selfTest() )
    	printf("Serial Loop Test OK");
    else
    	printf("Self Loop Test Failed");

    int i=0;
    while(1){
    	if( Flarm::gpsStatus() != true ){  // GPS bad
    		if( tsens_out > 80 )
    			flash_freq = FLASH_OFF;
    		else if(  tsens_out > 75 ){
    			flash_freq = FLASH_LOW;
    		}
    		else{
    			flash_freq = FLASH_MED;
    		}
    	}
    	else{  // GPS okay
    		ESP_LOGI(FNAME,"GPS OK");
    		if( isMoving() ){  // we are moving
    			ESP_LOGI(FNAME,"Moving");
    			if( Flarm::alarmLevel() > 0 || Flarm::objectInRange( 1.5 ) ){ // there is Flarm alarm or close target
    				if( tsens_out < 70 ){
    					flash_freq = FLASH_HIGH;
    				}else{
    					if( tsens_out < 75 ){        // is is acceptable for lower flashes ?
    						flash_freq = FLASH_MED;
    					}else{                       // no, is it critically hot ?
    						if( tsens_out < 80 )
    							flash_freq = FLASH_LOW;  // no, lower freq should be okay
    						else
    							flash_freq = FLASH_OFF;  // yessss, switch off > 80°C CPU temperature
    					}
    				}
    			}else{
    				if( tsens_out < 75 )
    					flash_freq = FLASH_MED;  // no, lower freq should be okay
    				else if( tsens_out < 80 ){
    					flash_freq = FLASH_LOW;  // yessss, flash low
    				}else{
    					flash_freq = FLASH_OFF;  // switch off beyond 80°C CPU temperature
    				}
    			}
    		}else{  // not moving
    			flash_freq = FLASH_OFF;  // GPS okay, standing on GND
    		}
    	}
    	i++;
    	if( flash_freq == FLASH_OFF ){
    		led_off();
    	}
    	// flash_freq = FLASH_HIGH;
    	if( (i%FLASHES) == 0 ){  // once per second
    		ESP_ERROR_CHECK(temp_sensor_read_celsius(&tsens_out));
    		ESP_LOGI(FNAME,"FREQ: %d CPU-T: %.2f C", flash_freq, tsens_out );
    	}
    	delay(PERIOD);
    	if( flash_freq == FLASH_LOW ){
    		if( (i%(FLASHES*5)) == 0 || (i%(FLASHES*5)) == 2 || (i%(FLASHES*5)) == 4 ){   // every 5 seconds
    			led_on();
    		}
    		if( (i%(FLASHES*5)) == 1 || (i%(FLASHES*5)) == 3 || (i%(FLASHES*5)) == 5 ){
    			led_off();
    		}
    	}else if ( flash_freq == FLASH_MED ){                  // every 2 seconds
    		if( (i%(FLASHES*2)) == 0 || (i%(FLASHES*2)) == 2 || (i%(FLASHES*2)) == 4 ){
    			led_on();
    		}
    		if( (i%(FLASHES*2)) == 1 || (i%(FLASHES*2)) == 3 || (i%(FLASHES*2)) == 5 ){
    			led_off();
    		}
    	}
    	else if ( flash_freq == FLASH_HIGH ){                 // every second
    			if( (i%(FLASHES*1)) == 0 || (i%(FLASHES*1)) == 2 || (i%(FLASHES*1)) == 4 || (i%(FLASHES*1)) == 6 || (i%(FLASHES*1)) == 8 ){
    				led_on();
    			}
    			if( (i%(FLASHES*1)) == 1 || (i%(FLASHES*1)) == 3 || (i%(FLASHES*1)) == 5 || (i%(FLASHES*1)) == 7 || (i%(FLASHES*1)) == 9 ){
    				led_off();
    			}
    	}

    }

}
