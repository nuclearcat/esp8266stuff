#include <stdint.h>

void parse_http(uint8_t *state, char *buf, int *size, void *callback);
int ds1820_read(void);
int dht_read(int *temp, int *hum);
void dht_init(void);
