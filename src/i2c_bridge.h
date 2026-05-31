/*
 * SPDX-License-Identifier: MIT
 *
 * This file is part of the debugprobe project.
 * See the repository LICENSE file for full terms.
 */

/*
    The current CDC-to-I2C bridge is a basic test implementation.
    It reads data from the USB CDC 'I2C'-Test' channel, applies a simple placeholder transformation (letter case toggle), 
    and sends the data back so communication can be verified end-to-end.
    A planned placeholder function is reserved for the future real I2C transaction logic (for example: parse command, execute I2C read/write, return response).
*/

#ifndef I2C_BRIDGE_H
#define I2C_BRIDGE_H

#include <stdbool.h>

void i2c_bridge_thread(void *ptr);
void i2c_bridge_init(void);
bool i2c_bridge_task(void);

#endif
