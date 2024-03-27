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
 * Name        : I2C.cpp
 * Author      : Georgi Todorov
 * Version     :
 * Created on  : Dec 30, 2012
 *
 * Copyright © 2012 Georgi Todorov  <terahz@geodar.com>
 */

#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>              /* Standard I/O functions */
#include <fcntl.h>
#include <unistd.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <syslog.h>		/* Syslog functionality */
#include "i2c.h"

#include <stdexcept>
#include <string.h>

// errors are returned as a negative signed int with values found here: /usr/include/asm-generic/errno-base.h
// use strerror(errno) to get a string

I2C::I2C(uint8_t bus_number, uint8_t device_address) {
  _bus_addr = bus_number;

  snprintf(_filename, sizeof(_filename), "/dev/i2c-%d", bus_number);
  if ((_fd = open(_filename, O_RDWR)) < 0) {
    _bus_addr = -1;
    std::string err_str = "error " + std::to_string(errno) + " while opening " + _filename + ": " + strerror(errno);
    throw std::invalid_argument(err_str);
  }
  
  if (set_device_address(device_address) != 0){
    std::string err_str = "error " + std::to_string(errno) + " while addressing device " +
      std::to_string(device_address) + "on" + _filename + ": " + strerror(errno);
    throw std::invalid_argument(err_str);
  }
  
}

I2C::~I2C() {
  close(_fd);
}

int32_t I2C::set_device_address(uint8_t device_address){

  if (device_address != _dev_addr){
    if (ioctl(_fd, I2C_SLAVE, device_address) < 0) {
      return (-errno);
    }
    _dev_addr = device_address;
  }
  return(0);
}

//! Read a single byte from I2C Bus
/*!
 \param address register address to read from
 */
int32_t I2C::read_byte(uint8_t register_address) {
  if (_fd == -1) {
    return(-EBADF);
  }

  uint8_t buff[1];
  int count;
  
  buff[0] = register_address;
  count = write(_fd, buff, sizeof(buff));
  if (count < 0) {
    return(-errno);
  }
  if (count != sizeof(buff)) {
    // ENXIO? address phase error
    return(-ENXIO);
  }

  count = read(_fd, buff, sizeof(buff));
  if (count < 0) {
    return(errno);
    }
  if (count != sizeof(buff))
    return(0);
  
  return buff[0];
}

//! Write a single byte to an I2C Device
/*!
 \param address register address to write to
 \param data 8 bit data to write
 */
int32_t I2C::write_byte(uint8_t register_address, uint8_t data) {
  if (_fd == -1) {
    return -EBADF;
  }
  
  uint8_t buff[2];
  buff[0] = register_address;
  buff[1] = data;
  int result = write(_fd, buff, sizeof(buff));
  if (result < 0) {
    return (-errno);
  }
  
  return(result);
}

int32_t I2C::write_bulk(uint8_t register_address, uint8_t* data, uint8_t count) {
  if (_fd == -1) {
    return -EBADF;
  }
  
  uint8_t buff[count+1];
  buff[0] = register_address;
  memcpy(buff+1, data, count);

  int result = write(_fd, buff, sizeof(buff));
  if (result < 0) {
    return (-errno);
  }
  
  return(result);
}

