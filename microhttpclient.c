/*
 * Copyright (C) 2017, Denys Fedoryshchenko
 * Contact: <nuclearcat@nuclearcat.com>
 * Licensed under the GPLv2
 * <http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt>
 *
 * Extremely minimalistic handling of tcp inbound data to parse basic HTTP/1.0
 * Just after request sent, do something like this:
 * do {
 *  ret = recv(s, recv_buf, UPG_BUF_SZ, MSG_DONTWAIT);
 *  if (ret > 0) {
 *       state = parse_http(state, recv_buf, &ret, &fwupcb);
 *  }
 * }
 * Where fwupcb will be callback that will receive "body" data
 * WARNING! I dont do supplied params check, so callback must be not NULL
 * Still experimental.
 */
#include "esp_common.h"

/* Upgrade process state */
#define STATUSLINE 0
#define NEWLINE 1
#define READING 2
#define BODY 3
#define ERROR 255

uint8_t parse_http(uint8_t state, char *buf, int *size, void *callback) {
        int i = 0;
        void (*ptrFunc)(char*,int);
        ptrFunc=callback;

        /* We are on state of reading body, directly do callback */
        if (state == BODY) {
                ptrFunc(buf, *size);
                return(state);
        }

        for (i=0; i<*size; i++) {
                switch(buf[i]) {
                case '\n':
                        state = NEWLINE;
                        break;
                case '\r':
                        if (state == NEWLINE) {
                                // Still we might have some body left in chunk
                                int offset = i+2;
                                state = BODY;
                                if (offset < *size) {
                                        ptrFunc(&buf[offset], *size-offset);
                                        *size -= (offset);
                                }
                                i=(*size); //to terminate loop
                        }
                        break;
                default:
                        if (state == NEWLINE)
                                state = READING;
                        break;
                }
        }
        return(state);
}
