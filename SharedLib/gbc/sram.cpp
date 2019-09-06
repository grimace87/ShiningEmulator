#include "sram.h"

#include "../appplatform.h"

#include <ctime>

Sram::Sram() {
    data = new unsigned char[8 * 4096];
}

Sram::~Sram() {
    delete[] data;
    if (sramFile) {
        fclose(sramFile);
    }
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

    sramFile = appPlatform.openFileInAppDir(batteryFileWithoutPath, "rb+"); // In/out, must exist
    if (sramFile == nullptr) {
        sramFile = appPlatform.openFileInAppDir(batteryFileWithoutPath, "wb"); // Create empty file for writing
        if (sramFile == nullptr) {
            return;
        }

        // Zero data buffer, write to new file
        std::fill(data, data + sizeBytes, 0);
        fwrite(data, 1, sizeBytes, sramFile);
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
            fwrite(newTimerData, 1, 16, sramFile);
        }

        // Close file, re-open for in/out
        fclose(sramFile);
        sramFile = appPlatform.openFileInAppDir(batteryFileWithoutPath, "rb+");
    } else {
        // Get file size, read in saved data, verify timer data if needed
        fseek(sramFile, 0L, SEEK_END);
        size_t fileSize = ftell(sramFile);
        rewind(sramFile);
        fread(data, 1, sizeBytes, sramFile);
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
                fwrite(newTimerData, 1, 16, sramFile);
            }
        }
        rewind(sramFile);
    }
}

void Sram::writeTimerData(unsigned int timerMode, unsigned char byte) {
    data[sizeBytes + timerMode] = byte;
    if (hasBattery && sramFile) {
        fseek(sramFile, sizeBytes + timerMode, SEEK_SET);
        fwrite(&byte, 1, 1, sramFile);
    }
}

void Sram::write(unsigned int address, unsigned char byte) {
    unsigned int normalisedAddress = (address & 0x1fff) % sizeBytes;
    data[bankOffset + normalisedAddress] = byte;
    if (hasBattery && sramFile) {
        fseek(sramFile, normalisedAddress, SEEK_SET);
        fwrite(&byte, 1, 1, sramFile);
    }
}

unsigned char Sram::read(unsigned int address) {
    return data[bankOffset + (address & 0x1fff)];
}
