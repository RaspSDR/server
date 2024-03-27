/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Name        : I2C.h
 * Author      : Georgi Todorov
 * Version     :
 * Created on  : Dec 30, 2012
 *
 * Copyright © 2012 Georgi Todorov  <terahz@geodar.com>
 */

#ifndef I2C_H_
#define I2C_H_
#include <inttypes.h>

class I2C {
public:
  I2C(uint8_t, uint8_t);
  virtual ~I2C();
  int32_t read_byte(uint8_t);
  int32_t write_byte(uint8_t, uint8_t);
  int32_t write_bulk(uint8_t, uint8_t*, uint8_t);
private:
  int _bus_addr;
  int _dev_addr;
  char _filename[64];
  int _fd;
  int32_t set_device_address(uint8_t);
};

#endif /* I2C_H_ */
