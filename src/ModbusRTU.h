/*
 * DigitShowBasic - Triaxial Test Machine Control Software
 * Copyright (C) 2025 Makoto KUNO
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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __MODBUSRTU_H_INCLUDE__
#define __MODBUSRTU_H_INCLUDE__

#pragma once

#include <windows.h>
#include <cstdint>

/**
 * Modbus RTU communication class for AD/DA board.
 * 
 * This replaces the CONTEC CAIO driver with Modbus RTU protocol.
 * 
 * Input Channels (ReadInputRegisters - Function Code 0x04):
 *   - 16ch int16_t values
 *   - HX711 (ch 0-7): int16_t
 *   - ADS1115 (ch 8-15): int16_t
 * 
 * Output Channels (WriteHoldingRegisters - Function Code 0x10):
 *   - 8ch uint16_t values
 *   - GP8403 (ch 0-7): 0-10000 mV output, clamped to range
 * 
 * Fixed settings:
 *   - Baudrate: 38400 bps
 *   - Data bits: 8
 *   - Parity: None
 *   - Stop bits: 1
 */
class ModbusRTU
{
public:
    // Constants for channel configuration
    static const int AI_CHANNELS = 16;      // Analog Input channels (int16_t)
    static const int AO_CHANNELS = 8;       // Analog Output channels (uint16_t)
    static const uint16_t AO_MIN_MV = 0;    // Minimum output voltage (mV)
    static const uint16_t AO_MAX_MV = 10000; // Maximum output voltage (mV)
    static const DWORD BAUDRATE = 38400;    // Fixed baudrate

    // Modbus function codes
    static const uint8_t FC_READ_INPUT_REGISTERS = 0x04;
    static const uint8_t FC_WRITE_MULTIPLE_REGISTERS = 0x10;

    ModbusRTU();
    ~ModbusRTU();

    /**
     * Open the serial port for Modbus communication.
     * @param portName COM port name (e.g., "COM3")
     * @param slaveAddress Modbus slave address (1-247)
     * @return true if successful, false otherwise
     */
    bool Open(const char* portName, uint8_t slaveAddress = 1);

    /**
     * Close the serial port.
     */
    void Close();

    /**
     * Check if the port is open.
     * @return true if open, false otherwise
     */
    bool IsOpen() const;

    /**
     * Read input registers (Analog Input values).
     * Called by timer every 100ms.
     * @param data Array to store 16 int16_t values
     * @return true if successful, false otherwise
     */
    bool ReadInputRegisters(int16_t* data);

    /**
     * Write holding registers (Analog Output values).
     * Only called after ReadInputRegisters when values have changed.
     * Values are clamped to 0-10000 mV range.
     * @param data Array of 8 uint16_t values (mV)
     * @return true if successful, false otherwise
     */
    bool WriteHoldingRegisters(const uint16_t* data);

    /**
     * Get the last error message.
     * @return Error message string
     */
    const char* GetLastError() const;

    /**
     * Clamp output value to valid range (0-10000 mV).
     * @param value Value to clamp
     * @return Clamped value
     */
    static uint16_t ClampOutputValue(uint16_t value);

private:
    HANDLE m_hSerial;           // Serial port handle
    uint8_t m_slaveAddress;     // Modbus slave address
    char m_lastError[256];      // Last error message

    // Previous output values for change detection
    uint16_t m_prevAoData[AO_CHANNELS];
    bool m_aoDataChanged;

    /**
     * Configure serial port settings.
     * @return true if successful, false otherwise
     */
    bool ConfigureSerialPort();

    /**
     * Calculate CRC-16 for Modbus RTU.
     * @param data Data buffer
     * @param length Data length
     * @return CRC-16 value
     */
    static uint16_t CalculateCRC16(const uint8_t* data, size_t length);

    /**
     * Send Modbus request and receive response.
     * @param request Request buffer
     * @param requestLen Request length
     * @param response Response buffer
     * @param expectedResponseLen Expected response length
     * @return true if successful, false otherwise
     */
    bool SendReceive(const uint8_t* request, size_t requestLen,
                     uint8_t* response, size_t expectedResponseLen);
};

// Global Modbus instance (singleton-like for simplicity)
ModbusRTU* GetModbusInstance();

#endif // __MODBUSRTU_H_INCLUDE__
