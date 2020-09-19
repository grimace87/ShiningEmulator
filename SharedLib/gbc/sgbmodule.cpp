#include "sgbmodule.h"

#include "colourutils.h"
#include "gbc.h"

#define SGBCOM_PAL01    0x00
#define SGBCOM_PAL23    0x01
#define SGBCOM_PAL03    0x02
#define SGBCOM_PAL12    0x03
#define SGBCOM_ATTR_BLK 0x04
#define SGBCOM_ATTR_LIN 0x05
#define SGBCOM_ATTR_DIV 0x06
#define SGBCOM_ATTR_CHR 0x07
#define SGBCOM_SOUND    0x08
#define SGBCOM_SOU_TRN  0x09
#define SGBCOM_PAL_SET  0x0a
#define SGBCOM_PAL_TRN  0x0b
#define SGBCOM_ATRC_EN  0x0c
#define SGBCOM_TEST_EN  0x0d
#define SGBCOM_ICON_EN  0x0e
#define SGBCOM_DATA_SEND 0x0f
#define SGBCOM_DATA_TRN 0x10
#define SGBCOM_MLT_REQ  0x11
#define SGBCOM_JUMP     0x12
#define SGBCOM_CHR_TRN  0x13
#define SGBCOM_PCT_TRN  0x14
#define SGBCOM_ATTR_TRN 0x15
#define SGBCOM_ATTR_SET 0x16
#define SGBCOM_MASK_EN  0x17
#define SGBCOM_OBJ_TRN  0x18
#define SGBCOM_PAL_PRI  0x19

void SgbModule::checkByte() {
    readCommandBits = 0;
    unsigned int byte = commandBits[0];
    byte |= commandBits[1] << 1;
    byte |= commandBits[2] << 2;
    byte |= commandBits[3] << 3;
    byte |= commandBits[4] << 4;
    byte |= commandBits[5] << 5;
    byte |= commandBits[6] << 6;
    byte |= commandBits[7] << 7;
    commandBytes[noPacketsSent][readCommandBytes] = byte;
    readCommandBytes++;
    if ((readCommandBytes == 1) && (noPacketsSent == 0)) {
        // This is first command byte
        noPacketsToSend = commandBytes[0][0] & 0x07;
        command = (commandBytes[0][0] >> 3) & 0x1f;
    }
}

void SgbModule::checkPackets(Gbc* gbc) {
    unsigned int byteIndex;
    unsigned int sentDataSets;
    unsigned int ctrlCode, dataGroups, groupNo, packetNo, byteNo;
    unsigned int p, c, dp;
    unsigned int xLeft, xRight, yTop, yBottom;
    unsigned int x, y;

    switch (command) {
        case SGBCOM_PAL01:
            palettes[0] = REMAP_555_8888(commandBytes[0][1], commandBytes[0][2]);
            palettes[4] = palettes[0];
            palettes[8] = palettes[0];
            palettes[12] = palettes[0];
            palettes[1] = REMAP_555_8888(commandBytes[0][3], commandBytes[0][4]);
            palettes[2] = REMAP_555_8888(commandBytes[0][5], commandBytes[0][6]);
            palettes[3] = REMAP_555_8888(commandBytes[0][7], commandBytes[0][8]);
            palettes[5] = REMAP_555_8888(commandBytes[0][9], commandBytes[0][10]);
            palettes[6] = REMAP_555_8888(commandBytes[0][11], commandBytes[0][12]);
            palettes[7] = REMAP_555_8888(commandBytes[0][13], commandBytes[0][14]);
            break;
        case SGBCOM_PAL23:
            palettes[0] = REMAP_555_8888(commandBytes[0][1], commandBytes[0][2]);
            palettes[4] = palettes[0];
            palettes[8] = palettes[0];
            palettes[12] = palettes[0];
            palettes[9] = REMAP_555_8888(commandBytes[0][3], commandBytes[0][4]);
            palettes[10] = REMAP_555_8888(commandBytes[0][5], commandBytes[0][6]);
            palettes[11] = REMAP_555_8888(commandBytes[0][7], commandBytes[0][8]);
            palettes[13] = REMAP_555_8888(commandBytes[0][9], commandBytes[0][10]);
            palettes[14] = REMAP_555_8888(commandBytes[0][11], commandBytes[0][12]);
            palettes[15] = REMAP_555_8888(commandBytes[0][13], commandBytes[0][14]);
            break;
        case SGBCOM_PAL03:
            palettes[0] = REMAP_555_8888(commandBytes[0][1], commandBytes[0][2]);
            palettes[4] = palettes[0];
            palettes[8] = palettes[0];
            palettes[12] = palettes[0];
            palettes[1] = REMAP_555_8888(commandBytes[0][3], commandBytes[0][4]);
            palettes[2] = REMAP_555_8888(commandBytes[0][5], commandBytes[0][6]);
            palettes[3] = REMAP_555_8888(commandBytes[0][7], commandBytes[0][8]);
            palettes[13] = REMAP_555_8888(commandBytes[0][9], commandBytes[0][10]);
            palettes[14] = REMAP_555_8888(commandBytes[0][11], commandBytes[0][12]);
            palettes[15] = REMAP_555_8888(commandBytes[0][13], commandBytes[0][14]);
            break;
        case SGBCOM_PAL12:
            palettes[0] = REMAP_555_8888(commandBytes[0][1], commandBytes[0][2]);
            palettes[4] = palettes[0];
            palettes[8] = palettes[0];
            palettes[12] = palettes[0];
            palettes[5] = REMAP_555_8888(commandBytes[0][3], commandBytes[0][4]);
            palettes[6] = REMAP_555_8888(commandBytes[0][5], commandBytes[0][6]);
            palettes[7] = REMAP_555_8888(commandBytes[0][7], commandBytes[0][8]);
            palettes[9] = REMAP_555_8888(commandBytes[0][9], commandBytes[0][10]);
            palettes[10] = REMAP_555_8888(commandBytes[0][11], commandBytes[0][12]);
            palettes[11] = REMAP_555_8888(commandBytes[0][13], commandBytes[0][14]);
            break;
        case SGBCOM_ATTR_BLK:
            // Get number of data groups
            dataGroups = commandBytes[0][1] & 0x1f;
            packetNo = 0;
            byteNo = 2;
            for (groupNo = 0; groupNo < dataGroups; groupNo++) {
                // Get control code and colour palette for this data group
                ctrlCode = commandBytes[packetNo][byteNo++] & 0x07;
                unsigned int paletteCodes = commandBytes[packetNo][byteNo++] & 0x3f;
                if (byteNo >= 16) {
                    byteNo = 0;
                    packetNo++;
                }
                // Get coordinates
                xLeft = commandBytes[packetNo][byteNo++] & 0x1f;
                yTop = commandBytes[packetNo][byteNo++] & 0x1f;
                if (byteNo >= 16) {
                    byteNo = 0;
                    packetNo++;
                }
                xRight = commandBytes[packetNo][byteNo++] & 0x1f;
                yBottom = commandBytes[packetNo][byteNo++] & 0x1f;
                if (byteNo >= 16) {
                    byteNo = 0;
                    packetNo++;
                }
                // Invalid conditions may cause errors in my program:
                if (xLeft > 19) {
                    break;
                }
                if (xRight > 19) {
                    xRight = 19;
                }
                if (yTop > 17) {
                    break;
                }
                if (yBottom > 17) {
                    yBottom = 17;
                }
                if (xLeft > xRight) {
                    break;
                }
                if (yTop > yBottom) {
                    break;
                }
                // Assign the palettes to screen segments
                if (ctrlCode > 3) {
                    // Fill area outside block
                    p = (paletteCodes & 0x30) >> 4;
                    for (y = 0; y < 18; y++) {
                        for (x = 0; x < xLeft; x++) {
                            chrPalettes[y * 20 + x] = p;
                        }
                        for (x = xRight + 1; x < 20; x++) {
                            chrPalettes[y * 20 + x] = p;
                        }
                    }
                    for (x = xLeft; x <= xRight; x++) {
                        for (y = 0; y < yTop; y++) {
                            chrPalettes[y * 20 + x] = p;
                        }
                        for (y = yBottom + 1; y < 18; y++) {
                            chrPalettes[y * 20 + x] = p;
                        }
                    }
                }
                if ((ctrlCode & 0x01) > 0) {
                    // Fill area inside block
                    p = paletteCodes & 0x03;
                    for (y = yTop + 1; y < yBottom; y++) {
                        for (x = xLeft + 1; x < xRight; x++) {
                            chrPalettes[y * 20 + x] = p;
                        }
                    }
                }
                if (ctrlCode > 0 && ctrlCode != 5) {
                    if (ctrlCode == 1) {
                        p = paletteCodes & 0x03;
                    } else if (ctrlCode == 4) {
                        p = (paletteCodes & 0x30) >> 4;
                    } else {
                        p = (paletteCodes & 0x0c) >> 2;
                    }
                    for (y = yTop; y <= yBottom; y++) {
                        chrPalettes[y * 20 + xLeft] = p;
                        chrPalettes[y * 20 + xRight] = p;
                    }
                    for (x = xLeft; x <= xRight; x++)
                    {
                        chrPalettes[yTop * 20 + x] = p;
                        chrPalettes[yBottom * 20 + x] = p;
                    }
                }
            }
            break;
        case SGBCOM_ATTR_LIN:
            command = 0;
            break;
        case SGBCOM_ATTR_DIV:
            if (commandBytes[0][1] & 0x40) {
                // Divide by vertical line (use H coordinate)
                unsigned int hCoord = (unsigned int)(commandBytes[0][2] & 0x1f);

                // Palette for left of the line
                unsigned int paletteNo = (commandBytes[0][1] & 0x0c) >> 2;
                if (hCoord > 18) {
                    hCoord = 18;
                }
                for (y = 0; y < hCoord; y++) {
                    for (x = 0; x < 20; x++) {
                        chrPalettes[y * 20 + x] = paletteNo;
                    }
                }

                // Palette for on the line
                paletteNo = (commandBytes[0][1] & 0x30) >> 4;
                for (x = 0; x < 20; x++) {
                    chrPalettes[hCoord * 20 + x] = paletteNo;
                }

                // Palette for right of the line
                paletteNo = commandBytes[0][1] & 0x03;
                for (y = hCoord; y < 18; y++) {
                    for (x = 0; x < 20; x++) {
                        chrPalettes[y * 20 + x] = paletteNo;
                    }
                }
            } else {
                // Divide by horizontal line (use V coordinate)
                unsigned int vCoord = (unsigned int)(commandBytes[0][2] & 0x1f);

                // Palette for above the line
                unsigned int paletteNo = (commandBytes[0][1] & 0x0c) >> 2;
                if (vCoord > 20) {
                    vCoord = 20;
                }
                for (y = 0; y < 18; y++) {
                    for (x = 0; x < vCoord; x++) {
                        chrPalettes[y * 20 + x] = paletteNo;
                    }
                }

                // Palette for on the line
                paletteNo = (commandBytes[0][1] & 0x30) >> 4;
                for (y = 0; y < 18; y++) {
                    chrPalettes[y * 20 + vCoord] = paletteNo;
                }

                // Palette for below the line
                paletteNo = commandBytes[0][1] & 0x03;
                for (y = 0; y < 18; y++) {
                    for (x = vCoord; x < 20; x++) {
                        chrPalettes[y * 20 + x] = paletteNo;
                    }
                }
            }
            break;
        case SGBCOM_ATTR_CHR:
            xLeft = commandBytes[0][1] & 0x1f;
            yTop = commandBytes[0][2] & 0x1f;
            if (xLeft > 19) {
                break;
            }
            if (yTop > 17) {
                break;
            }
            sentDataSets = commandBytes[0][4] & 0x01;
            sentDataSets *= 256;
            sentDataSets += commandBytes[0][3];
            ctrlCode = commandBytes[0][5] & 0x01; // Write hori/vert
            dp = 0; // Number of bytes used so far
            byteIndex = 0; // Position within bytes (one of 4 pal no's)
            x = xLeft;
            y = yTop;
            p = 0; // Packet no.
            c = 6; // Byte in packet
            if (ctrlCode == 0x00) { // Horizontal write
                while (dp < sentDataSets) {
                    if (byteIndex == 0) {
                        chrPalettes[y * 20 + x] = (commandBytes[p][c] & 0xc0) >> 6;
                    } else if (byteIndex == 1) {
                        chrPalettes[y * 20 + x] = (commandBytes[p][c] & 0x30) >> 4;
                    } else if (byteIndex == 2) {
                        chrPalettes[y * 20 + x] = (commandBytes[p][c] & 0x0c) >> 2;
                    } else {
                        chrPalettes[y * 20 + x] = commandBytes[p][c] & 0x03;
                    }
                    x++;
                    if (x >= 20) {
                        x = xLeft;
                        y++;
                        if (y >= 18) {
                            break;
                        }
                    }
                    byteIndex++;
                    if (byteIndex >= 4) {
                        byteIndex = 0;
                        c++;
                        if (c >= 16) {
                            c = 0;
                            p++;
                            if (p >= noPacketsSent) {
                                break;
                            }
                        }
                        dp++;
                    }
                }
            } else { // Vertical write
                while (dp < sentDataSets) {
                    if (byteIndex == 0) {
                        chrPalettes[y * 20 + x] = (commandBytes[p][c] & 0xc0) >> 6;
                    } else if (byteIndex == 1) {
                        chrPalettes[y * 20 + x] = (commandBytes[p][c] & 0x30) >> 4;
                    } else if (byteIndex == 2) {
                        chrPalettes[y * 20 + x] = (commandBytes[p][c] & 0x0c) >> 2;
                    } else {
                        chrPalettes[y * 20 + x] = commandBytes[p][c] & 0x03;
                    }
                    y++;
                    if (y >= 18) {
                        y = yTop;
                        x++;
                        if (x >= 20) {
                            break;
                        }
                    }
                    byteIndex++;
                    if (byteIndex >= 4) {
                        byteIndex = 0;
                        c++;
                        if (c >= 16) {
                            c = 0;
                            p++;
                            if (p >= noPacketsSent) {
                                break;
                            }
                        }
                        dp++;
                    }
                }
            }
            break;
        case SGBCOM_SOUND:
            command = 0;
            break;
        case SGBCOM_PAL_SET:
        {
            unsigned char attributes = commandBytes[0][9];
            for (p = 0; p < 4; p++) {
                unsigned int srcPaletteNo = (unsigned int)(commandBytes[0][p * 2 + 2] & 0x01);
                srcPaletteNo *= 256;
                srcPaletteNo += (unsigned int)(commandBytes[0][p * 2 + 1]);
                for (c = 0; c < 4; c++) {
                    unsigned int dstIndex = p * 4 + c;
                    unsigned int srcIndex = srcPaletteNo * 4 + c;
                    palettes[dstIndex] = sysPalettes[srcIndex];
                }
            }
            if (attributes & 0x40) {
                freezeMode = 0;
                freezeScreen = false;
            }
        }
            break;
        case SGBCOM_PAL_TRN:
            if (gbc->ioPorts[0x40] & 0x80) {
                mapVramForTrnOp(gbc);
                unsigned int srcIndex = 0;
                for (p = 0; p < 512; p++) {
                    for (c = 0; c < 4; c++) {
                        sysPalettes[p * 4 + c] = REMAP_555_8888((unsigned int)mappedVramForTrnOp[srcIndex], (unsigned int)mappedVramForTrnOp[srcIndex + 1]);
                        srcIndex += 2;
                    }
                }
            }
            break;
        case SGBCOM_ICON_EN:
            command = 0;
            break;
        case SGBCOM_DATA_SEND:
            command = 0;
            break;
        case SGBCOM_MLT_REQ:
            multEnabled = commandBytes[0][1] & 0x01;
            noPlayers = commandBytes[0][1] + 0x01;
            readJoypadID = 0x0f;
            break;
        case SGBCOM_CHR_TRN:
            command = 0;
            break;
        case SGBCOM_ATTR_TRN:
            command = 0;
            break;
        case SGBCOM_ATTR_SET:
            command = 0;
            break;
        case SGBCOM_PCT_TRN:
            command = 0;
            break;
        case SGBCOM_MASK_EN:
            if (commandBytes[0][1] == 0x00) {
                freezeScreen = 0;
            } else if (commandBytes[0][1] < 0x04) {
                freezeScreen = 1;
            } else {
                freezeScreen = 0;
            }
            freezeMode = commandBytes[0][1];
            break;
        case SGBCOM_PAL_PRI:
            command = 0;
            break;
        default:
            break;
    }
}

void SgbModule::colouriseFrame(uint32_t* imageData) {
    for (unsigned int chrx = 0; chrx < 20; chrx++) {
        for (unsigned int chry = 0; chry < 18; chry++) {
            unsigned int paletteNumber = chrPalettes[chry * 20 + chrx];
            unsigned int* usePalette = &palettes[paletteNumber * 4];
            unsigned int x_max = 8 * chrx + 8;
            unsigned int y_max = 8 * chry + 8;
            for (unsigned int px = 8 * chrx; px < x_max; px++) {
                for (unsigned int py = 8 * chry; py < y_max; py++) {
                    imageData[py * 160 + px] = usePalette[monoData[160 * py + px]];
                }
            }
        }
    }
}

void SgbModule::mapVramForTrnOp(Gbc* gbc) {
    // Copy data from the VRAM signal
    // Assumes a certain display configuration and does not account for variances
    // This includes display enable, background not scrolled, window and sprites not on-screen,
    // and the BGP palette register has a certain value (possibly 0xe4)
    auto vram = (unsigned char*)gbc->vram.data();
    unsigned char lcdControl = gbc->ioPorts[0x40];
    unsigned int mapStart = lcdControl & 0x08 ? 0x1c00 : 0x1800;
    unsigned int charsStart, charCodeInverter;
    if (lcdControl & 0x10) {
        charsStart = 0x0000;
        charCodeInverter = 0x0000;
    } else {
        charsStart = 0x0800;
        charCodeInverter = 0x0080;
    }

    for (unsigned int chrY = 0; chrY < 18; chrY++) {
        for (unsigned int chrX = 0; chrX < 20; chrX++) {
            // Copy 16 bytes of the character tile in this location
            unsigned int mapIndex = chrY * 32 + chrX;
            unsigned int zeroBasedTileNo = (unsigned int)vram[mapStart + mapIndex] ^ charCodeInverter;
            unsigned int charsDataStartIndex = charsStart + zeroBasedTileNo * 16;
            unsigned char* dst = &mappedVramForTrnOp[16 * (chrY * 20 + chrX)];
            for (unsigned int byteNo = 0; byteNo < 16; byteNo++) {
                *dst = vram[charsDataStartIndex];
                dst++;
                charsDataStartIndex++;
            }
        }
    }
}
