/*
 *  Copyright (C) 2017, Denys Fedoryshchenko
 * Contact: <nuclearcat@nuclearcat.com>
 * Licensed under the GPLv2
 * <http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt>
 *
 * DHT11/DHT22/AM2302/AM2320 driver for ESP8266 FreeRTOS SDK
 * Driver made by more strictly following standards than random sources
 *
 * Driver can use much less mem (define LOWMEM), if instead of measuring number
 * of cycles&storing them, we use Tlow with Thigh comparison, but it is more prone to errors
 * due measurement jitter, as algoritm execution time varies and tiny code are
 * in "time critical" part
 */

#include "esp_common.h"
#include <fcntl.h>
#include <stdio.h>
#include <gpio.h>
/*
   Following list for PIN_FUNC_SELECT and PIN_PULLUP_DIS
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

/*
 * Define here your PIN and init for it
 * WARNING: PIN4 and PIN5 swapped :( at most of ESP12 modules
 */
#define OW_PIN_NUM    4
#define OW_PIN_INIT() PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4)
#define OW_PIN_NOPULLUP() PIN_PULLUP_DIS(PERIPHS_IO_MUX_GPIO4_U)
#define OW_GET_IN()   GPIO_INPUT_GET(GPIO_ID_PIN(OW_PIN_NUM))
#define OW_OUT_LOW()  ( GPIO_OUTPUT_SET(GPIO_ID_PIN(OW_PIN_NUM), 0) )
#define OW_OUT_HIGH() ( GPIO_OUTPUT_SET(GPIO_ID_PIN(OW_PIN_NUM), 1) )
#define OW_DIR_IN()   ( GPIO_DIS_OUTPUT(GPIO_ID_PIN(OW_PIN_NUM)) )

#define MAXWAIT 1000000 /* Max waiting cycles in waittransition, approx 1s */
#define HIGH    1
#define LOW     0

/* Keep it for lower mem usage */
#define LOWMEM

void delay_ms(uint32_t ms) {
        uint32_t i;
        for (i = 0; i < ms; i++)
                os_delay_us(1000);
}

/* Return number of cycles it waited for level, or 0 on failure */
int waittransition(uint level) {
        int waittime = MAXWAIT-1;
        while (waittime--) {
                if (OW_GET_IN() == level) {
                        break;
                }
        }
        if (waittime)
                return(MAXWAIT-waittime);
        else
                return(0);
}

/*
 * Important info from datasheet (AM2320)
 * 3.3V - max 1m wire length, 5V max 30m. 5.1K pullup resistor for data line.
 * Do not poll sensor more often than each 2S
 * return 0 on success, 1 on failure
*/
int dht_read(int *temp, int *hum) {
        uint8_t data[5];
        int i;

        memset(data, 0x0, 5);
        OW_PIN_INIT();
        OW_PIN_NOPULLUP();

        /* Not in specs, but device should see transition from low to high
           Might be reduced
        */
        OW_OUT_HIGH();
        delay_ms(25);

        /* Tbe, you may adjust delay to 80us-20ms, esp. if you have
         *  high capacitance on data line, or its long. By specs at least 80us
         */
        OW_OUT_LOW();
        delay_ms(5);
        /* Time critical part might introduce lag to your realtime functions
        * Best (ideal) case lag 2950us, worst case 5370us, with non-critical
        * section (but it is "schedulable") ~30ms
        */
#ifdef LOWMEM
        portENTER_CRITICAL();
        OW_DIR_IN();
        /* Tbe, to Tgo, might be <= 1 */
        if (!waittransition(1))
                goto bad;

        /* Tgo, to Trel */
        if (!waittransition(0))
                goto bad;

        /* Trel, to Treh */
        if (!waittransition(1))
                goto bad;

        /* Treh, to first byte Tlow */
        if (!waittransition(0))
                goto bad;
        {
                int lowcycles, highcycles;
                for (i=0; i<40; ++i) {
                        lowcycles   = waittransition(1);
                        highcycles  = waittransition(0);
                        data[i/8] <<= 1;
                        if (highcycles > lowcycles)
                                data[i/8] |= 1;
                }
        }

        portEXIT_CRITICAL();
#else
        /* Don't add anything here, time critical code */
        portENTER_CRITICAL();
        uint32_t cycles[80];
        {
                OW_DIR_IN();
                /* Tbe, to Tgo, might be <= 1 */
                if (!waittransition(1))
                        goto bad;

                /* Tgo, to Trel */
                if (!waittransition(0))
                        goto bad;

                /* Trel, to Treh */
                if (!waittransition(1))
                        goto bad;

                /* Treh, to first byte Tlow */
                if (!waittransition(0))
                        goto bad;

                /* Each bit, [i] duration of low pulse, [i+1] - high pulse */
                for (i=0; i<80; i+=2) {
                        cycles[i]   = waittransition(1);
                        cycles[i+1] = waittransition(0);
                }
        }
        portEXIT_CRITICAL();

        /*
           // In case you want to debug received timing values
           for (i=0;i<40;i++) { printf("[%d]%02X:",i,cycles[i]);}
         */

        /* Time critical finished, processing data */
        for (i=0; i<40; ++i) {
                uint32_t lowCycles  = cycles[2*i];
                uint32_t highCycles = cycles[2*i+1];
                /* On errors - quit */
                if ((lowCycles == 0) || (highCycles == 0)) {
                        return(0);
                }
                /* Add bits for each byte if high cycle is more than low */
                data[i/8] <<= 1;
                if (highCycles > lowCycles) {
                        data[i/8] |= 1;
                }
        }
#endif

        *hum = ((data[0] << 8) + data[1]);

        /* DHT11 specific */
        if ( *hum > 1000 )
                *hum = data[0];

        *temp = (((data[2] & 0x7F) << 8) + data[3]);

        /* DHT11 specific */
        if ( *temp > 1250 )
                *temp = data[2];
        /* Negative temperature */
        if ( data[2] & 0x80 )
                *temp = -*temp;

        return(0);
bad:
        portEXIT_CRITICAL();
        return(1);
}
