# esp8266stuff

This drivers intended to be used in ESP8266 RTOS SDK, made with efforts to follow
specs strictly as possible, to avoid unexplainable problems as on other drivers.
Unfortunately, as onewire/singlewire protocol is available only by bitbang,
time critical section might introduce scheduling delays to RTOS (it's unavoidable).
You can reduce MAXWAIT to lower values, to decrease timeout if onewire/singlewire
device is not reachable.

# dht.c
Tested on AM2320, but should work on others as well.
Please update values:
OW_PIN_NUM, PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4 to pin you will use for data line.
You can disable #define LOWMEM and do debugging of cycles numbers, in case you
experience any kind of problems.
I strongly recommend to not add any code in time critical section, as it may make
recognition of bits unreliable.
WiP as CRC check need to be implemented.

# ds18b20.c
Tested on DS1820 (old model), but should work on others as well.
WiP, as search, addressable read, crc need to be implemented.
