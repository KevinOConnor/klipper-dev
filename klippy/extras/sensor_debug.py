# Read and write registers of an SPI or I2C device
#
# Copyright (C) 2023  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.
from . import bus

class SensorDebug:
    def __init__(self, config):
        self.printer = config.get_printer()
        self.spi = self.i2c = None
        self.last_response = self.last_request = ""
        if config.get('i2c_address', None) is not None:
            self.i2c = bus.MCU_I2C_from_config(config)
        else:
            spi_mode = config.getint('spi_mode', minval=0, maxval=3)
            self.spi = bus.MCU_SPI_from_config(config, spi_mode)
        name = config.get_name().split()[-1]
        gcode = self.printer.lookup_object("gcode")
        gcode.register_mux_command("SENSOR_DEBUG", "CHIP", name,
                                   self.cmd_SENSOR_DEBUG,
                                   desc=self.cmd_SENSOR_DEBUG_help)
    def parse_buffer(self, value):
        if not value:
            return []
        tval = int(value, 16)
        out = []
        for i in range(len(value) // 2):
            out.append(tval & 0xff)
            tval >>= 8
        out.reverse()
        return out
    cmd_SENSOR_DEBUG_help = "Query low-level sensor register"
    def cmd_SENSOR_DEBUG(self, gcmd):
        write = gcmd.get("WRITE", "", parser=self.parse_buffer)
        if self.i2c is None:
            params = self.spi.spi_transfer_cmd.send(write)
            resp = bytearray(params['response'])
        else:
            read_count = gcmd.get_int("READ", 0, minval=0)
            if read_count:
                params = self.i2c.i2c_read(write, read_count)
                resp = bytearray(params['response'])
            else:
                params = self.i2c.i2c_write(write)
                resp = []
        self.last_request = "".join(["%02x" % (w,) for w in write])
        self.last_response = "".join(["%02x" % (r,) for r in resp])
        gcmd.respond_info("Request '%s' returns '%s'"
                          % (self.last_request, self.last_response))
    def get_status(self):
        return {'last_request': self.last_request,
                'last_response': self.last_response}

def load_config_prefix(config):
    return SensorDebug(config)
