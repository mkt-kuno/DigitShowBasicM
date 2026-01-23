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

#include "stdafx.h"
#include "ModbusRTU.h"
#include <cstring>
#include <algorithm>

// CRC-16 lookup table for Modbus RTU
static const uint16_t CRC16_TABLE[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
};

// Singleton instance
static ModbusRTU g_ModbusInstance;

ModbusRTU* GetModbusInstance()
{
    return &g_ModbusInstance;
}

ModbusRTU::ModbusRTU()
    : m_hSerial(INVALID_HANDLE_VALUE)
    , m_slaveAddress(1)
    , m_aoDataChanged(false)
{
    memset(m_lastError, 0, sizeof(m_lastError));
    memset(m_prevAoData, 0, sizeof(m_prevAoData));
}

ModbusRTU::~ModbusRTU()
{
    Close();
}

bool ModbusRTU::Open(const char* portName, uint8_t slaveAddress)
{
    if (m_hSerial != INVALID_HANDLE_VALUE) {
        Close();
    }

    if (portName == nullptr || strlen(portName) == 0) {
        strcpy_s(m_lastError, "Invalid port name");
        return false;
    }

    if (slaveAddress < 1 || slaveAddress > 247) {
        strcpy_s(m_lastError, "Invalid slave address (must be 1-247)");
        return false;
    }

    m_slaveAddress = slaveAddress;

    // Open COM port
    // For COM ports > 9 (e.g., COM10, COM11), Windows requires "\\.\COMxx" format
    // COM1-COM9 can use either format, but we use the extended format for all ports
    // to ensure compatibility
    char fullPortName[32];
    sprintf_s(fullPortName, "\\\\.\\%s", portName);

    m_hSerial = CreateFileA(
        fullPortName,
        GENERIC_READ | GENERIC_WRITE,
        0,              // No sharing
        NULL,           // Default security
        OPEN_EXISTING,
        0,              // Non-overlapped
        NULL
    );

    if (m_hSerial == INVALID_HANDLE_VALUE) {
        sprintf_s(m_lastError, "Failed to open %s", portName);
        return false;
    }

    if (!ConfigureSerialPort()) {
        Close();
        return false;
    }

    // Clear buffers
    PurgeComm(m_hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

    // Reset previous AO data
    memset(m_prevAoData, 0, sizeof(m_prevAoData));
    m_aoDataChanged = true; // Force initial write

    return true;
}

bool ModbusRTU::ConfigureSerialPort()
{
    // Configure serial port parameters
    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(dcb);

    if (!GetCommState(m_hSerial, &dcb)) {
        sprintf_s(m_lastError, "Failed to get COM state (error %s)", GetLastError());
        return false;
    }

    dcb.BaudRate = BAUDRATE;    // 38400 bps
    dcb.ByteSize = 8;           // 8 data bits
    dcb.Parity = NOPARITY;      // No parity
    dcb.StopBits = ONESTOPBIT;  // 1 stop bit
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = TRUE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fAbortOnError = FALSE;

    if (!SetCommState(m_hSerial, &dcb)) {
        sprintf_s(m_lastError, "Failed to set COM state (error %s)", GetLastError());
        return false;
    }

    // Configure timeouts
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;          // Max time between bytes
    timeouts.ReadTotalTimeoutConstant = 100;    // Base timeout
    timeouts.ReadTotalTimeoutMultiplier = 10;   // Per-byte timeout
    timeouts.WriteTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 10;

    if (!SetCommTimeouts(m_hSerial, &timeouts)) {
        sprintf_s(m_lastError, "Failed to set COM timeouts (error %s)", GetLastError());
        return false;
    }

    return true;
}

void ModbusRTU::Close()
{
    if (m_hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hSerial);
        m_hSerial = INVALID_HANDLE_VALUE;
    }
}

bool ModbusRTU::IsOpen() const
{
    return m_hSerial != INVALID_HANDLE_VALUE;
}

uint16_t ModbusRTU::CalculateCRC16(const uint8_t* data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc = (crc >> 8) ^ CRC16_TABLE[(crc ^ data[i]) & 0xFF];
    }
    return crc;
}

bool ModbusRTU::SendReceive(const uint8_t* request, size_t requestLen,
                            uint8_t* response, size_t expectedResponseLen)
{
    if (!IsOpen()) {
        strcpy_s(m_lastError, "Serial port not open");
        return false;
    }

    // Clear any pending data
    PurgeComm(m_hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

    // Send request
    DWORD bytesWritten = 0;
    if (!WriteFile(m_hSerial, request, (DWORD)requestLen, &bytesWritten, NULL)) {
        sprintf_s(m_lastError, "Write failed (error %s)", GetLastError());
        return false;
    }

    if (bytesWritten != requestLen) {
        sprintf_s(m_lastError, "Incomplete write (%lu/%zu bytes)", bytesWritten, requestLen);
        return false;
    }

    // Wait for transmission to complete
    FlushFileBuffers(m_hSerial);

    // Read response
    DWORD bytesRead = 0;
    DWORD totalRead = 0;
    
    while (totalRead < expectedResponseLen) {
        if (!ReadFile(m_hSerial, response + totalRead, 
                      (DWORD)(expectedResponseLen - totalRead), &bytesRead, NULL)) {
            sprintf_s(m_lastError, "Read failed (error %s)", GetLastError());
            return false;
        }
        
        if (bytesRead == 0) {
            // Timeout
            sprintf_s(m_lastError, "Read timeout (%lu/%zu bytes)", totalRead, expectedResponseLen);
            return false;
        }
        
        totalRead += bytesRead;
    }

    // Verify slave address
    if (response[0] != m_slaveAddress) {
        sprintf_s(m_lastError, "Invalid slave address in response");
        return false;
    }

    // Check for exception response
    if (response[1] & 0x80) {
        sprintf_s(m_lastError, "Modbus exception code: %d", response[2]);
        return false;
    }

    // Verify CRC
    // Modbus RTU transmits CRC in little-endian order (low byte first, high byte second)
    uint16_t receivedCRC = response[expectedResponseLen - 2] | 
                           (response[expectedResponseLen - 1] << 8);
    uint16_t calculatedCRC = CalculateCRC16(response, expectedResponseLen - 2);
    
    if (receivedCRC != calculatedCRC) {
        sprintf_s(m_lastError, "CRC mismatch (recv: 0x%04X, calc: 0x%04X)", 
                  receivedCRC, calculatedCRC);
        return false;
    }

    return true;
}

bool ModbusRTU::ReadInputRegisters(int16_t* data)
{
    if (data == nullptr) {
        strcpy_s(m_lastError, "Null data pointer");
        return false;
    }

    // Build request: Read Input Registers (0x04)
    // Request: [SlaveAddr][FuncCode][StartAddr_Hi][StartAddr_Lo][NumRegs_Hi][NumRegs_Lo][CRC_Lo][CRC_Hi]
    uint8_t request[8];
    request[0] = m_slaveAddress;
    request[1] = FC_READ_INPUT_REGISTERS;
    request[2] = 0x00;  // Starting address high byte
    request[3] = 0x00;  // Starting address low byte (register 0)
    request[4] = 0x00;  // Number of registers high byte
    request[5] = AI_CHANNELS;  // Number of registers low byte (16)

    uint16_t crc = CalculateCRC16(request, 6);
    request[6] = crc & 0xFF;        // CRC low byte
    request[7] = (crc >> 8) & 0xFF; // CRC high byte

    // Expected response: [SlaveAddr][FuncCode][ByteCount][Data...][CRC_Lo][CRC_Hi]
    // Byte count = 2 * 16 = 32 bytes of data
    const size_t responseLen = 3 + (AI_CHANNELS * 2) + 2; // 37 bytes
    uint8_t response[64];

    if (!SendReceive(request, sizeof(request), response, responseLen)) {
        return false;
    }

    // Verify function code
    if (response[1] != FC_READ_INPUT_REGISTERS) {
        sprintf_s(m_lastError, "Unexpected function code in response");
        return false;
    }

    // Verify byte count
    if (response[2] != AI_CHANNELS * 2) {
        sprintf_s(m_lastError, "Unexpected byte count in response");
        return false;
    }

    // Parse data (big-endian in Modbus, convert to host order)
    for (int i = 0; i < AI_CHANNELS; i++) {
        int16_t value = (int16_t)((response[3 + i * 2] << 8) | response[3 + i * 2 + 1]);
        data[i] = value;
    }

    return true;
}

bool ModbusRTU::WriteHoldingRegisters(const uint16_t* data)
{
    if (data == nullptr) {
        strcpy_s(m_lastError, "Null data pointer");
        return false;
    }

    // Check if data has changed
    bool hasChanged = false;
    for (int i = 0; i < AO_CHANNELS; i++) {
        uint16_t clampedValue = ClampOutputValue(data[i]);
        if (clampedValue != m_prevAoData[i]) {
            hasChanged = true;
            break;
        }
    }

    if (!hasChanged) {
        // No change, skip write
        return true;
    }

    // Build request: Write Multiple Registers (0x10)
    // Request: [SlaveAddr][FuncCode][StartAddr_Hi][StartAddr_Lo][NumRegs_Hi][NumRegs_Lo][ByteCount][Data...][CRC_Lo][CRC_Hi]
    // Buffer size: 7 header bytes + (8 channels * 2 bytes) + 2 CRC bytes = 25 bytes
    uint8_t request[7 + AO_CHANNELS * 2 + 2];
    request[0] = m_slaveAddress;
    request[1] = FC_WRITE_MULTIPLE_REGISTERS;
    request[2] = 0x00;  // Starting address high byte
    request[3] = 0x00;  // Starting address low byte (register 0)
    request[4] = 0x00;  // Number of registers high byte
    request[5] = AO_CHANNELS;  // Number of registers low byte (8)
    request[6] = AO_CHANNELS * 2;  // Byte count (16 bytes for 8 registers)

    // Add data (big-endian)
    for (int i = 0; i < AO_CHANNELS; i++) {
        uint16_t clampedValue = ClampOutputValue(data[i]);
        request[7 + i * 2] = (clampedValue >> 8) & 0xFF;     // High byte
        request[7 + i * 2 + 1] = clampedValue & 0xFF;        // Low byte
        m_prevAoData[i] = clampedValue;  // Store for change detection
    }

    size_t dataLen = 7 + AO_CHANNELS * 2;
    uint16_t crc = CalculateCRC16(request, dataLen);
    request[dataLen] = crc & 0xFF;        // CRC low byte
    request[dataLen + 1] = (crc >> 8) & 0xFF; // CRC high byte

    // Expected response: [SlaveAddr][FuncCode][StartAddr_Hi][StartAddr_Lo][NumRegs_Hi][NumRegs_Lo][CRC_Lo][CRC_Hi]
    const size_t responseLen = 8;
    uint8_t response[16];

    if (!SendReceive(request, dataLen + 2, response, responseLen)) {
        return false;
    }

    // Verify function code
    if (response[1] != FC_WRITE_MULTIPLE_REGISTERS) {
        sprintf_s(m_lastError, "Unexpected function code in response");
        return false;
    }

    return true;
}

uint16_t ModbusRTU::ClampOutputValue(uint16_t value)
{
    if (value > AO_MAX_MV) {
        return AO_MAX_MV;
    }
    return value;
}

const char* ModbusRTU::GetLastError() const
{
    return m_lastError;
}
