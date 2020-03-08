#include "sram.h"

#include "../appplatform.h"

#include <ctime>

Sram::Sram() {
    data = new unsigned char[8 * 4096];
}

Sram::~Sram() {
    delete[] data;
}

void Sram::openSramFile(std::string& romFileName, AppPlatform& appPlatform) {
    unsigned char newTimerData[16];
    std::fill(newTimerData, newTimerData + 16, 0);

    if (sizeBytes == 0) {
        return;
    }

    size_t lastSlashPos = romFileName.find_last_of("/\\");
    if (lastSlashPos == std::string::npos) {
        lastSlashPos = 0;
    } else {
        lastSlashPos++;
    }

    std::string fileNameWithoutPath = romFileName.substr(lastSlashPos);
    size_t dotPosition = fileNameWithoutPath.find_last_of('.');
    std::string batteryFileWithoutPath;
    if (dotPosition == std::string::npos) {
        batteryFileWithoutPath = fileNameWithoutPath + ".gsv";
    } else {
        batteryFileWithoutPath = fileNameWithoutPath.substr(0, dotPosition) + ".gsv";
    }

    sramFile = appPlatform.openFileInAppDir(batteryFileWithoutPath, FileOpenMode::RANDOM_READ_WRITE_BINARY); // In/out, must exist
    if (!sramFile.is_open()) {
        std::fstream createdFile = appPlatform.openFileInAppDir(batteryFileWithoutPath, FileOpenMode::WRITE_NEW_FILE_BINARY); // Create empty file for writing
        if (!createdFile.is_open()) {
            return;
        }

        // Zero data buffer, write to new file
        std::fill(data, data + sizeBytes, 0);
        createdFile.write(reinterpret_cast<char*>(data), sizeof(uint8_t) * sizeBytes);
        if (hasTimer) {
            // Prepare new data to append to file
            appPlatform.withCurrentTime([&](struct tm* localTime) {
                newTimerData[0] = (unsigned char)localTime->tm_sec;
                newTimerData[1] = (unsigned char)localTime->tm_min;
                newTimerData[2] = (unsigned char)localTime->tm_hour;
                newTimerData[3] = (unsigned char)localTime->tm_mday;
                newTimerData[4] = (unsigned char)localTime->tm_mon;
                newTimerData[5] = (unsigned char)localTime->tm_year;
            });

            // Append data at the end
            createdFile.write(reinterpret_cast<char*>(newTimerData), sizeof(char) * 16);
        }

        // Close file, re-open for in/out
        createdFile.close();
        sramFile = appPlatform.openFileInAppDir(batteryFileWithoutPath, FileOpenMode::RANDOM_READ_WRITE_BINARY);
    } else {
        // Get file size, read in saved data, verify timer data if needed
        sramFile.seekg(std::ios_base::end);
        size_t fileSize = sramFile.tellg();
        sramFile.seekg(std::ios_base::beg);
        sramFile.read(reinterpret_cast<char*>(data), sizeof(uint8_t) * sizeBytes);
        if (hasTimer) {
            // Check if the file already has the extra space at the end
            if (fileSize < sizeBytes + 16) {
                // Prepare new data to append to file
                appPlatform.withCurrentTime([&](struct tm* localTime) {
                    newTimerData[0] = (unsigned char)localTime->tm_sec;
                    newTimerData[1] = (unsigned char)localTime->tm_min;
                    newTimerData[2] = (unsigned char)localTime->tm_hour;
                    newTimerData[3] = (unsigned char)localTime->tm_mday;
                    newTimerData[4] = (unsigned char)localTime->tm_mon;
                    newTimerData[5] = (unsigned char)localTime->tm_year;
                });

                // Write the new data
                auto readPos = sramFile.tellg();
                sramFile.seekp(readPos);
                sramFile.write(reinterpret_cast<char*>(newTimerData), sizeof(char) * 16);
                sramFile.seekp(std::ios_base::beg);
            }
        }
        sramFile.seekg(std::ios_base::beg);
    }
}

void Sram::writeTimerData(unsigned int timerMode, unsigned char byte) {
    data[sizeBytes + timerMode] = byte;
    if (hasBattery && sramFile) {
        sramFile.seekp(sizeBytes + timerMode);
        sramFile.write(reinterpret_cast<char*>(&byte), sizeof(char));
    }
}

void Sram::write(unsigned int address, unsigned char byte) {
    unsigned int normalisedAddress = (address & 0x1fff) % sizeBytes;
    data[bankOffset + normalisedAddress] = byte;
    if (hasBattery && sramFile) {
        sramFile.seekp(normalisedAddress);
        sramFile.write(reinterpret_cast<char*>(&byte), sizeof(char));
    }
}

unsigned char Sram::read(unsigned int address) {
    return data[bankOffset + (address & 0x1fff)];
}
