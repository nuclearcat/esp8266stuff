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

void delay_ms(uint32_t ms)
{
        uint32_t i;
        for (i = 0; i < ms; i++)
                os_delay_us(1000);
}

#define MAXWAIT 1000000
int expectpulse(uint level) {
        int waittime = MAXWAIT-1;
        while (waittime--) {
                if (OW_GET_IN() == level) {
                        break;
                }
                //os_delay_us(1);
        }
        if (waittime)
                return(MAXWAIT-waittime);
        else
                return(0);
}

int dht_read(int *temp, int *hum) {
        uint8_t data[5];
        int i;

        memset(data, 0x0, 5);
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4);
        PIN_PULLUP_DIS(PERIPHS_IO_MUX_GPIO4_U);

        OW_OUT_HIGH();
        delay_ms(250);
        OW_OUT_LOW();
        delay_ms(5); // Tbe (Host starting signal low time)

        portENTER_CRITICAL();
        uint32_t cycles[80];
        {
                int i;
                OW_DIR_IN();

                i = expectpulse(1);
                if (!i)
                        goto bad;

                i = expectpulse(0);
                if (!i)
                        goto bad;

                i = expectpulse(1);
                if (!i)
                        goto bad;

                i = expectpulse(0);
                if (!i)
                        goto bad;

                for (i=0; i<80; i+=2) {
                        cycles[i]   = expectpulse(1);
                        cycles[i+1] = expectpulse(0);
                }
        }
        portEXIT_CRITICAL();
        for (i=0; i<40; ++i) {
                uint32_t lowCycles  = cycles[2*i];
                uint32_t highCycles = cycles[2*i+1];
                if ((lowCycles == 0) || (highCycles == 0)) {
                        return(0);
                }
                data[i/8] <<= 1;
                if (highCycles > lowCycles) {
                        data[i/8] |= 1;
                }
        }

        *hum = ((data[0] << 8) + data[1]);
        if ( *hum > 1000 )
                *hum = data[0];

        *temp = (((data[2] & 0x7F) << 8) + data[3]);
        if ( *temp > 1250 )
                *temp = data[2];
        if ( data[2] & 0x80 )
                *temp = -*temp;

        return(1);
bad:
        portEXIT_CRITICAL();
        return(0);
}
