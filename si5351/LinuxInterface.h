/*
 * Copyright (C) 2021 Coburn Wightman
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _LinuxInterface_h
#define _LinuxInterface_h

#include "I2CInterface.h"
#include "i2c.h"

/**
 * An interface for talking to the Si5351
 * via linux userland i2c library.
 */
class LinuxInterface : public I2CInterface {
public:
    LinuxInterface(uint8_t i2c_bus_addr, uint8_t chip_address) {
        i2c = new I2C(i2c_bus_addr, chip_address);
    }

    virtual ~LinuxInterface() {
        delete i2c;
    }

    // check f
    uint8_t check_address(uint8_t device_addr) {

        int32_t result = i2c->read_byte(0);
        if (result < 0)
            return 4; // Wire 'other error'

        return 0; // success
    }

    uint8_t read(uint8_t i2c_dev_addr, uint8_t reg_addr) {

        int32_t result = i2c->read_byte(reg_addr);
        if (result < 0)
            return 0;

        return result;
    }

    // Wire.endTransmission() result codes:
    //
    // 0:success
    // 1:data too long to fit in transmit buffer
    // 2:received NACK on transmit of address
    // 3:received NACK on transmit of data
    // 4:other error
    uint8_t write(uint8_t i2c_dev_addr, uint8_t reg_addr, uint8_t data) {

        int result = i2c->write_byte(reg_addr, data);
        if (result < 0)
            return 4;

        return 0;

        // Wire.beginTransmission(i2c_dev_addr);
        // Wire.write(addr);
        // Wire.write(data);
        // return Wire.endTransmission();
    }

    uint8_t write_bulk(uint8_t i2c_dev_addr, uint8_t reg_addr, uint8_t count, uint8_t* data) {
        int result = i2c->write_bulk(reg_addr, data, count);
        if (result < 0)
            return 4;

        return 0;
        // Wire.beginTransmission(i2c_dev_addr);
        // Wire.write(addr);
        // for(int i = 0; i < bytes; i++)
        // {
        //     Wire.write(data[i]);
        // }
        // return Wire.endTransmission();
    }

private:
    I2C* i2c;
};

#endif
