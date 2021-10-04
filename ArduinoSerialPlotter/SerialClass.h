#pragma once
#ifndef SERIALCLASS_H_INCLUDED
#define SERIALCLASS_H_INCLUDED

#define ARDUINO_WAIT_TIME 2000

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <format>
#include <array>

class Serial
{
private:
	//Serial comm handler
	HANDLE hSerial = nullptr;
	//Connection status
	bool connected = false;
	//Get various information about the connection
	COMSTAT status;
	//Keep track of last error
	DWORD errors;

public:
	//Create a Serial Object in an unconnected state
	Serial() noexcept {
		
	}
	//Initialize Serial communication with the given COM port
	Serial(const char* portName) {
		Connect(portName, true, CBR_9600);
	};
	Serial(const char* portName, bool reset, uint32_t baud_rate) {
		Connect(portName, reset, baud_rate);
	};
	//Close the connection
	~Serial() {
		//Check if we are connected before trying to disconnect
		if (this->connected)
		{
			//We're no longer connected
			this->connected = false;
			//Close the serial handler
			CloseHandle(this->hSerial);
		}
	}
	//Read data in a buffer, if nbChar is greater than the
	//maximum number of bytes available, it will return only the
	//bytes available. The function return -1 when nothing could
	//be read, the number of bytes actually read.
	int ReadData(char* buffer, unsigned int nbChar) {
		//Number of bytes we'll have read
		DWORD bytesRead;
		//Number of bytes we'll really ask to read
		unsigned int toRead;

		//Use the ClearCommError function to get status info on the Serial port
		ClearCommError(this->hSerial, &this->errors, &this->status);

		//Check if there is something to read
		if (this->status.cbInQue > 0)
		{
			//If there is we check if there is enough data to read the required number
			//of characters, if not we'll read only the available characters to prevent
			//locking of the application.
			if (this->status.cbInQue > nbChar)
			{
				toRead = nbChar;
			}
			else
			{
				toRead = this->status.cbInQue;
			}

			//Try to read the require number of chars, and return the number of read bytes on success
			if (ReadFile(this->hSerial, buffer, toRead, &bytesRead, NULL))
			{
				return bytesRead;
			}

		}

		//If nothing has been read, or that an error was detected return 0
		return 0;

	};
	//Writes data from a buffer through the Serial connection
	//return true on success.
	bool WriteData(const char* buffer, unsigned int nbChar) {
		DWORD bytesSend;

		//Try to write the buffer on the Serial port
		if (!WriteFile(this->hSerial, (void*)buffer, nbChar, &bytesSend, 0))
		{
			//In case it don't work get comm error and return false
			ClearCommError(this->hSerial, &this->errors, &this->status);

			return false;
		}
		else
			return true;
	};

	//Check if we are actually connected
	bool IsConnected() {
		//Simply return the connection status
		return this->connected;
	};

	int Connect(const char* portName, bool reset, uint32_t baud_rate) {
		//We're not yet connected
		this->connected = false;

		//Try to connect to the given port throuh CreateFile
		this->hSerial = CreateFile(portName,
			GENERIC_READ | GENERIC_WRITE,
			0,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL);

		//Check if the connection was successfull
		if (this->hSerial == INVALID_HANDLE_VALUE)
		{
			//If not success full display an Error
			if (GetLastError() == ERROR_FILE_NOT_FOUND) {

				//Print Error if neccessary
				printf("ERROR: Handle was not attached. Reason: %s not available.\n", portName);

			}
			else
			{
				printf("ERROR!!!");
			}
		}
		else
		{
			//If connected we try to set the comm parameters
			DCB dcbSerialParams = { 0 };

			//Try to get the current
			if (!GetCommState(this->hSerial, &dcbSerialParams))
			{
				//If impossible, show an error
				printf("failed to get current serial parameters!");
			}
			else
			{
				//Define serial connection parameters for the arduino board
				dcbSerialParams.BaudRate = baud_rate;//CBR_9600;
				dcbSerialParams.ByteSize = 8;
				dcbSerialParams.StopBits = ONESTOPBIT;
				dcbSerialParams.Parity = NOPARITY;
				//Setting the DTR to Control_Enable ensures that the Arduino is properly
				//reset upon establishing a connection
				dcbSerialParams.fDtrControl = reset ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
				dcbSerialParams.ErrorChar = '~';
				//Set the parameters and check for their proper application
				if (!SetCommState(hSerial, &dcbSerialParams))
				{
					printf("ALERT: Could not set Serial Port parameters");
				}
				else
				{
					//If everything went fine we're connected
					this->connected = true;
					//Flush any remaining characters in the buffers 
					PurgeComm(this->hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);
					if (reset) {
						//We wait 2s as the arduino board will be reseting
						Sleep(ARDUINO_WAIT_TIME);
					}
				}
			}
		}

		return this->connected;
	}

	int Connect(int portIndex, bool reset, uint32_t baud_rate) {
		char lpTargetPath[5000]; // buffer to store the path of the COMPORTS
		std::string str;
		str.reserve(512);

		std::format_to(std::back_inserter(str), std::string_view{ "COM{}" }, portIndex);
		DWORD test = QueryDosDevice(str.c_str(), lpTargetPath, 5000);

		if (test != 0) {
			//OK to continue
		}
		else {
			return false;
		}

		if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER)
		{
			return false;
		}

		return Connect(lpTargetPath, reset, baud_rate);
	}

	int Disconnect() {
		if (this->connected)
		{
			//We're no longer connected
			this->connected = false;
			//Close the serial handler
			CloseHandle(this->hSerial);
			this->hSerial = nullptr;
			return true;
		}
		else {
			return false;
		}
	}

	std::array<uint32_t, 8> ListAvailable() {
		std::array<uint32_t, 8> ret;
		for (size_t i = 0; i < ret.size(); i++)
			ret[i] = 0;

		char lpTargetPath[5000]; // buffer to store the path of the COMPORTS
		std::string str;
		str.reserve(512);

		for (int i = 0; i < 255; i++) // checking ports from COM0 to COM255
		{
			//std::string str = "COM" + std::to_string(i); // converting to COM0, COM1, COM2
			std::format_to(std::back_inserter(str), std::string_view{ "COM{}" }, i);
			DWORD test = QueryDosDevice(str.c_str(), lpTargetPath, 5000);

			// Test the return value and error if any
			ret[i / (sizeof(uint32_t)*8)] |= (test != 0 ? 1 : 0) << (i % (sizeof(uint32_t)*8));
		}

		return ret;
	}
	std::array<std::string, 256> List() {
		std::array<std::string, 256> ret;

		char lpTargetPath[5000]; // buffer to store the path of the COMPORTS
		std::string str;
		str.reserve(512);

		for (int i = 0; i < 255; i++) // checking ports from COM0 to COM255
		{
			//std::string str = "COM" + std::to_string(i); // converting to COM0, COM1, COM2
			std::format_to(std::back_inserter(str), std::string_view{ "COM{}" }, i);
			DWORD test = QueryDosDevice(str.c_str(), lpTargetPath, 5000);

			// Test the return value and error if any
			if (test != 0) //QueryDosDevice returns zero if it didn't find an object
			{
				ret[i] = std::string{ lpTargetPath };//std::format("{}", lpTargetPath);
				//std::cout << str << ": " << lpTargetPath << std::endl;
			} else {
				ret[i] = "";
			}

			if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
			}
		}

		return ret;
	}
};

#endif