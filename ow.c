#include <fcntl.h>
#include <stdio.h>
#include <driver/gpio.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include <freertos/task.h>
#include <limits.h>
#include "esp_log.h"
#include "esp8266/gpio_struct.h"


/* ESP12 PIN4 and PIN5 sometimes swapped :@ */
#define OW_PIN_DATA     5
#define OW_PIN_POWER    4
#define OW_GET_IN()     ( fast_pin_get(OW_PIN_DATA) )
#define OW_OUT_LOW()    ( fast_pin_set(OW_PIN_DATA, 0) )
#define OW_OUT_HIGH()   ( fast_pin_set(OW_PIN_DATA, 1) )
#define OW_DIR_IN()     ( fast_pin_dir(OW_PIN_DATA, 0) )
#define OW_DIR_OUT()    ( fast_pin_dir(OW_PIN_DATA, 1) )


void onewire_gpio_setup() {
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = 1ULL << OW_PIN_DATA | 1ULL << OW_PIN_POWER;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    gpio_set_direction(OW_PIN_POWER, GPIO_MODE_OUTPUT);
    gpio_set_level(OW_PIN_POWER, 1);
}

IRAM_ATTR inline void __attribute__ ((always_inline)) fast_pin_set(gpio_num_t gpio_num, uint32_t level) {
    if (level) {
        GPIO.out_w1ts |= (0x1 << gpio_num);
    } else {
        GPIO.out_w1tc |= (0x1 << gpio_num);
    }
}

IRAM_ATTR inline int __attribute__ ((always_inline)) fast_pin_get(gpio_num_t gpio_num)
{
    return (GPIO.in >> gpio_num) & 0x1;
}

IRAM_ATTR inline void __attribute__ ((always_inline)) fast_pin_dir(gpio_num_t gpio_num, gpio_mode_t mode)
{
    if (mode) {
        GPIO.enable_w1ts |= (0x1 << gpio_num);
    } else {
        GPIO.enable_w1tc |= (0x1 << gpio_num);
    }
}



// 12.5ns with 80MHz clock
IRAM_ATTR inline void __attribute__ ((always_inline)) WaitUS(uint32_t delta)
{
    uint32_t cycleCount;
    uint32_t waitUntil;
    delta *= 80;
    delta -= 5;
    __asm__ __volatile__("rsr     %0, ccount":"=a" (waitUntil));
    waitUntil += delta;
    do {
        __asm__ __volatile__("rsr     %0, ccount":"=a" (cycleCount));
    } while (waitUntil > cycleCount);
}


// OK if just using a single permanently connected device
IRAM_ATTR int onewire_reset() {
    int r;

    OW_DIR_OUT();
    vPortETSIntrLock();
    OW_OUT_LOW();
    WaitUS(480);
    OW_DIR_IN();
    WaitUS(70);
    r = OW_GET_IN(); // Is OW device present it will pull low
    vPortETSIntrUnlock();

    WaitUS(410); // TODO: Why?
    // if r - 1 - bad, means device didnt pulled low
    return (r);
}


#define OW_RECOVERY_TIME 10 // Depends on wire length

/*********************** onewire_write() ********************************/
/*This function writes a byte to the sensor.*/
/* */
/*Parameters: byte - the byte to be written to the 1-wire */
/*Returns: */
/*********************************************************************/


IRAM_ATTR void onewire_write(int data) {
    int count, temp;

    vPortETSIntrLock();

    for (count = 0; count < 8; ++count) {
        OW_DIR_OUT();
        temp = data >> count;
        temp &= 0x1;
        if (temp) {
            OW_OUT_LOW();
            WaitUS(10);
            OW_OUT_HIGH();
            WaitUS(55);
        } else {
            OW_OUT_LOW();
            WaitUS(65);
            OW_OUT_HIGH();
            WaitUS(5);
        }
    }
    vPortETSIntrUnlock();
}

IRAM_ATTR int onewire_read() {
    int count, data = 0;
    vPortETSIntrLock();
    for (count = 0; count < 8; ++count) {
        OW_DIR_OUT();
        OW_OUT_LOW();
        WaitUS(3);
        OW_DIR_IN();
        WaitUS(10);
        if (OW_GET_IN())
            data |= (1 << count);
        WaitUS(50);
    }
    vPortETSIntrUnlock();
    return ( data );
}

#define CRC8_POLYNOMIAL 0x8C
uint8_t crc8_data(uint8_t *buffer, uint8_t length)
{
    uint8_t crc8 = 0, valid = 0;
    uint8_t inByte, byteCount, bitCount, mix;

    for (byteCount = 0; byteCount < length; byteCount++)
    {
        inByte = buffer[byteCount];
        if (inByte)
        {
            valid = 1;
        }
        for (bitCount = 0; bitCount < CHAR_BIT; bitCount++)
        {
            mix = (crc8 ^ inByte) & 0x01;
            crc8 >>= 1;
            if (mix)
            {
                crc8 ^= CRC8_POLYNOMIAL;
            }
            inByte >>= 1;
        }
    }
    if (!valid)
    {
        /* If all bytes are 0, return a different CRC so that the test will fail */
        return 0xFF;
    }
    return crc8;
}

int ds1820_read(double *temp) {
    uint8_t i = 0, data[9], type = 0;
    int32_t raw;
    uint8_t crc8 = 0xFF;

    /*
    // Can be used to verify calibration of WaitUS
    uint32_t d1, d2;
    vPortETSIntrLock();
    __asm__ __volatile__("rsr     %0, ccount":"=a" (d1));
    WaitUS(10);
    __asm__ __volatile__("rsr     %0, ccount":"=a" (d2));
    vPortETSIntrUnlock();
    printf("%d\r\n", d2-d1);



    __asm__ __volatile__("rsr     %0, ccount":"=a" (d1));
    WaitUS(100);
    __asm__ __volatile__("rsr     %0, ccount":"=a" (d2));
    printf("%d\r\n", d2-d1);


    vPortETSIntrLock();
    __asm__ __volatile__("rsr     %0, ccount":"=a" (d1));
    WaitUS(200);
    __asm__ __volatile__("rsr     %0, ccount":"=a" (d2));
    vPortETSIntrUnlock();
    printf("%d\r\n", d2-d1);
    */

    if (onewire_reset())
        return -1; // Device not found

    vTaskDelay(100 / portTICK_RATE_MS);
    // Read ROM (and important - type)
    onewire_write(0x33);
    for (i = 0; i < 8; i++)
        data[i] = onewire_read();

    crc8 = crc8_data(data, 7);
    type = data[0];
    if (crc8 != data[7]) {
        for (i = 0; i < 8; i++)
            printf("rom[%d]%02x ", i, data[i]);
        printf("\r\n");

        printf("ROM CRC error\r\n");
        return -5;
    }


    if (onewire_reset())
        return -3;

    onewire_write(0xCC);
    onewire_write(0x44);

    // Conversion delay
    vTaskDelay(1000 / portTICK_RATE_MS);

    if (onewire_reset())
        return -4;

    onewire_write(0xCC);
    onewire_write(0xBE);
    for (i = 0; i < 9; i++)
        data[i] = onewire_read();

    crc8 = crc8_data(data, 8);

    if (crc8 != data[8]) {
        for (i = 0; i < 9; i++)
            printf("data[%d]%02x ", i, data[i]);
        ESP_LOGE("ow", "CRC mismatch %02x %02x", data[8], crc8);
        return -2;
    }

    

    if (type == 0x10) {
        //printf(" OLD TYPE\r\n");
        *temp = (double)(data[0] >> 1);
        double count_per_c = 0x10;
        double count_remain = data[6];
        *temp = *temp - 0.25 + ((count_per_c - count_remain) / count_per_c);
    } else {
        double minus = 1.0;
        if (data[1] & 0x80)
            minus = -1.0;

        uint8_t cfg = (data[4] & 0x60);
        uint8_t mask = 0xFF;
        
        /* Ignore some bits depends on precision */
        if (cfg == 0x00)
            mask = 0xF8;        
        else if (cfg == 0x20) 
            mask = 0xFC;
        else if (cfg == 0x40)
            mask = 0xFE;
        
        raw = (data[1] << 8) | (data[0] & mask);

        /* If minus... */
        if (minus == -1.0)
            raw = 0xFFFF - raw;
        *temp = (double)raw * 0.0625 * minus;

        /* Increase resolution */
        if (cfg != 0x60) {
            printf("Fixing resolution\r\n");
            onewire_reset();
            onewire_write(0xCC);
            onewire_write(0x4E);
            onewire_write(0x0);//alarm
            onewire_write(0x0);//alarm
            onewire_write(0x7F);
            onewire_reset();
            vTaskDelay(30 / portTICK_RATE_MS);
            onewire_write(0xCC);
            onewire_write(0x48); // save to eeprom
            vTaskDelay(30 / portTICK_RATE_MS);
        }

    }

    return 0;
}
