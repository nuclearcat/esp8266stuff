#include "esp_common.h"
#include <fcntl.h>
#include <stdio.h>
#include <gpio.h>
/*
   GPIO0:	PERIPHS_IO_MUX_GPIO0_U
   GPIO1:	PERIPHS_IO_MUX_U0TXD_U
   GPIO2:	PERIPHS_IO_MUX_GPIO2_U
   GPIO3:	PERIPHS_IO_MUX_U0RXD_U
   GPIO4:	PERIPHS_IO_MUX_GPIO4_U
   GPIO5:	PERIPHS_IO_MUX_GPIO5_U
   GPIO6:	PERIPHS_IO_MUX_SD_CLK_U
   GPIO7:	PERIPHS_IO_MUX_SD_DATA0_U
   GPIO8:	PERIPHS_IO_MUX_SD_DATA1_U
   GPIO9:	PERIPHS_IO_MUX_SD_DATA2_U
   GPIO10:	PERIPHS_IO_MUX_SD_DATA3_U
   GPIO11:	PERIPHS_IO_MUX_SD_CMD_U
   GPIO12:	PERIPHS_IO_MUX_MTDI_U
   GPIO13:	PERIPHS_IO_MUX_MTCK_U
   GPIO14:	PERIPHS_IO_MUX_MTMS_U
   GPIO15:	PERIPHS_IO_MUX_MTDO_U
 */

/* ESP12 PIN4 and PIN5 swapped :@ */
#define OW_PIN_NUM 4
#define OW_GET_IN()   ( GPIO_INPUT_GET(GPIO_ID_PIN(OW_PIN_NUM)) )
#define OW_OUT_LOW()  ( GPIO_OUTPUT_SET(GPIO_ID_PIN(OW_PIN_NUM), 0) )
#define OW_OUT_HIGH() ( GPIO_OUTPUT_SET(GPIO_ID_PIN(OW_PIN_NUM), 1) )
#define OW_DIR_IN()   ( GPIO_DIS_OUTPUT(GPIO_ID_PIN(OW_PIN_NUM)) )

// OK if just using a single permanently connected device
int onewire_reset() {
        int r;
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4);
        PIN_PULLUP_DIS(PERIPHS_IO_MUX_GPIO4_U);
        portENTER_CRITICAL();
        OW_OUT_LOW();
        os_delay_us(480);
        OW_DIR_IN();
        os_delay_us(70);
        r = OW_GET_IN(); // Is OW device present it will pull low
        portEXIT_CRITICAL();
        os_delay_us(410); // TODO: Why?
        // if r - 1 - bad, means device didnt pulled low
        return(r);
}

#define OW_RECOVERY_TIME 10 // Depends on wire length

/*********************** onewire_write() ********************************/
/*This function writes a byte to the sensor.*/
/* */
/*Parameters: byte - the byte to be written to the 1-wire */
/*Returns: */
/*********************************************************************/

void onewire_write(int data) {
        int count, temp;

        for (count=0; count<8; ++count) {
                portENTER_CRITICAL();
                temp = data>>count;
                temp &= 0x1;
                if (temp) {
                        OW_OUT_LOW();
                        os_delay_us(10);
                        OW_OUT_HIGH();
                        os_delay_us(55);
                } else {
                        OW_OUT_LOW();
                        os_delay_us(65);
                        OW_OUT_HIGH();
                        os_delay_us(5);
                }
                portEXIT_CRITICAL();
        }
}

int onewire_read() {
        int count, data = 0;
        for (count=0; count<8; ++count) {
                portENTER_CRITICAL();
                OW_OUT_LOW();
                os_delay_us(3);
                OW_DIR_IN();
                os_delay_us(10);
                if (OW_GET_IN())
                        data |= (1<<count);
                os_delay_us(53);
                portEXIT_CRITICAL();
        }
        return( data );
}

int ds1820_read() {
        uint8_t i=0, data[9], type=0;
        int16_t raw;

        if (onewire_reset())
                return(0); // Device not found

        onewire_reset();

        // Read ROM (and important - type)
        onewire_write(0x33);
        for (i=0; i<8; i++)
                data[i] = onewire_read();

        type = data[0];

        onewire_reset();
        onewire_write(0xCC);
        onewire_write(0x44);

        while (i == 0)
                i = onewire_read();
        // Conversion delay
        vTaskDelay(1000 / portTICK_RATE_MS);

        onewire_reset();
        onewire_write(0xCC);
        onewire_write(0xBE);
        for (i=0; i<9; i++)
                data[i] = onewire_read();

        raw = (data[1] << 8) | data[0]; // glue to 16bit value
        // old DS
        if (type == 0x10) {
                raw = raw << 3; // 9 bit resolution default
                // remaining - to archieve full resolution (12bit)
                if (data[7] == 0x10)
                        raw = (raw & 0xFFF0) + 12 - data[6];
        } else {
                uint8_t cfg = (data[4] & 0x60);
                if (cfg == 0x00)
                  raw = raw & ~7;  // 9 bit resolution, 93.75 ms
                else if (cfg == 0x20)
                  raw = raw & ~3;  // 10 bit res, 187.5 ms
                else if (cfg == 0x40)
                  raw = raw & ~1;  // 11 bit res, 375 ms
        }
        // default is 12 bit resolution, 750 ms conversion time
        // we get YXXX - where it is Y.XXX * 1000 to avoid float
        return(raw * 1000 / 16);
}
