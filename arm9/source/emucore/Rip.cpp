
#include <stdlib.h>
#include <stdio.h>

#include "Rip.h"
#include "RAM.h"
#include "ROM.h"
#include "JLP.h"
#include "ROMBanker.h"
#include "CRC32.h"

#define MAX_MEMORY_UNITS  16
#define MAX_STRING_LENGTH 256

#define ROM_TAG_TITLE          0x01
#define ROM_TAG_PUBLISHER      0x02
#define ROM_TAG_RELEASE_DATE   0x05
#define ROM_TAG_COMPATIBILITY  0x06

Rip::Rip(UINT32 systemID)
: Peripheral("", ""),
  peripheralCount(0),
  targetSystemID(systemID),
  crc(0)
{
    producer = new CHAR[1];
    strcpy(producer, "");
    year = new CHAR[1];
    strcpy(year, "");
    memset(filename, 0, sizeof(filename));
    JLP16Bit = NULL;
}

Rip::~Rip()
{
    for (UINT16 i = 0; i < peripheralCount; i++)
        delete[] peripheralNames[i];
    UINT16 count = GetROMCount();
    for (UINT16 i = 0; i < count; i++)
        delete GetROM(i);
    delete[] producer;
    delete[] year;
    if (JLP16Bit) delete JLP16Bit;
}

void Rip::SetName(const CHAR* n)
{
    if (this->peripheralName)
        delete[] peripheralName;

    peripheralName = new CHAR[strlen(n)+1];
    strcpy(peripheralName, n);
}

void Rip::SetProducer(const CHAR* p)
{
    if (this->producer)
        delete[] producer;

    producer = new CHAR[strlen(p)+1];
    strcpy(producer, p);
}

void Rip::SetYear(const CHAR* y)
{
    if (this->year)
        delete[] year;

    year = new CHAR[strlen(y)+1];
    strcpy(year, y);
}

void Rip::AddPeripheralUsage(const CHAR* periphName, PeripheralCompatibility usage)
{
    peripheralNames[peripheralCount] = new CHAR[strlen(periphName)+1];
    strcpy(peripheralNames[peripheralCount], periphName);
    peripheralUsages[peripheralCount] = usage;
    peripheralCount++;
}

PeripheralCompatibility Rip::GetPeripheralUsage(const CHAR* periphName)
{
    for (UINT16 i = 0; i < peripheralCount; i++) {
        if (strcmpi(peripheralNames[i], periphName) == 0)
            return peripheralUsages[i];
    }
    return PERIPH_INCOMPATIBLE;
}


Rip* Rip::LoadBin(const CHAR* filename, const CHAR* configFile)
{
    //determine the crc of the designated file
    UINT32 crc = CRC32::getCrc(filename);
    Rip* rip = LoadCartridgeConfiguration(configFile, crc);
    if (rip == NULL)
        return NULL;

    //load the data into the rip
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        delete rip;
        return NULL;
    }

    //obtain the file size
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    //load in the file image
    UINT8* image = new UINT8[size];
    UINT32 count = 0;
    while (count < size)
        image[count++] = (UINT8)fgetc(file);
    fclose(file);

    //parse the file image into the rip
    UINT32 offset = 0;
    UINT16 romCount = rip->GetROMCount();
    for (UINT16 i = 0; i < romCount; i++) 
    {
        if (offset >= size) 
        {
            //something is wrong, the file we're trying to load isn't large enough
            //getting here indicates a likely incorrect memory map in knowncarts.cfg
            delete[] image;
            delete rip;
            return NULL;
        }
        ROM* nextRom = rip->GetROM(i);
        nextRom->load(image+offset);
        offset += nextRom->getByteWidth() * nextRom->getReadSize();
    }

    delete[] image;

    // Add the JLP RAM module if required...
    extern bool bUseJLP;
    if (bUseJLP)
    {
        rip->JLP16Bit = new JLP();
        rip->AddRAM(rip->JLP16Bit);
    }
    else
    {
        rip->JLP16Bit = NULL;
    }
    
    extern bool bForceIvoice;
    if (bForceIvoice) rip->AddPeripheralUsage("Intellivoice", PERIPH_REQUIRED);

    rip->SetFileName(filename);
    rip->crc = CRC32::getCrc(filename);

    return rip;
}

Rip* Rip::LoadCartridgeConfiguration(const CHAR* configFile, UINT32 crc)
{
    CHAR scrc[9];
    sprintf(scrc, "%08X", crc);

    //find the config that matches this crc
    FILE* cfgFile = fopen(configFile, "r");
    if (cfgFile == NULL)
        return NULL;

    Rip* rip = NULL;
    BOOL parseSuccess = FALSE;
    CHAR nextLine[256];
    while (!feof(cfgFile)) {
        fgets(nextLine, 256, cfgFile);
        //size_t length = strlen(nextLine);
        if (strstr(nextLine, scrc) != nextLine) {
            if (parseSuccess)
                break;
            else
                continue;
        }

        CHAR* nextToken;
        if ((nextToken = strstr(nextLine, "Info")) != NULL) {
            if ((nextToken = strrchr(nextLine, ':')) == NULL)
                continue;

            if (rip == NULL)
                rip = new Rip(ID_SYSTEM_INTELLIVISION);
            rip->SetYear(nextToken+1);

            *nextToken = NULL;
            if ((nextToken = strrchr(nextLine, ':')) == NULL)
                continue;
            rip->SetProducer(nextToken+1);

            *nextToken = NULL;
            if ((nextToken = strrchr(nextLine, ':')) == NULL)
                continue;
            rip->SetName(nextToken+1);

            parseSuccess = TRUE;
        }
        else if ((nextToken = strstr(nextLine, "ROM")) != NULL) {
            //TODO: check to make sure we don't exceed the maximum number of roms for a Rip

            //parse rom bit width
            if ((nextToken = strrchr(nextLine, ':')) == NULL)
                continue;

            //first check to see if this is a banked rom; if there is an equals (=) sign in
            //the last field, then we assume we should parse banking
            UINT16 triggerAddress, triggerMask, triggerValue, onMask, onValue;
            triggerAddress = triggerMask = triggerValue = onMask = onValue = 0;
            CHAR* nextEqual = strrchr(nextLine, '=');
            if (nextEqual != NULL && nextEqual > nextToken) {
                onValue = (UINT16)strtol(nextEqual+1, NULL, 16);
                *nextEqual = NULL;
                onMask = (UINT16)strtol(nextToken+1, NULL, 16);
                *nextToken = NULL;
                if ((nextToken = strrchr(nextLine, ':')) == NULL)
                    continue;

                nextEqual = strrchr(nextLine, '=');
                if (nextEqual == NULL || nextEqual < nextToken)
                    continue;

                CHAR* nextComma = strrchr(nextLine, ',');
                if (nextComma == NULL || nextComma < nextToken || nextComma > nextEqual)
                    continue;

                triggerValue = (UINT16)strtol(nextEqual+1, NULL, 16);
                *nextEqual = NULL;
                triggerMask = (UINT16)strtol(nextComma+1, NULL, 16);
                *nextComma = NULL;
                triggerAddress = (UINT16)strtol(nextToken+1, NULL, 16);
                *nextToken = NULL;

                if ((nextToken = strrchr(nextLine, ':')) == NULL)
                    continue;
            }

            UINT8 romBitWidth = (UINT8)atoi(nextToken+1);

            //parse rom size
            *nextToken = NULL;
            if ((nextToken = strrchr(nextLine, ':')) == NULL)
                continue;
            UINT16 romSize = (UINT16)strtol(nextToken+1, NULL, 16);

            *nextToken = NULL;
            if ((nextToken = strrchr(nextLine, ':')) == NULL)
                continue;
            UINT16 romAddress = (UINT16)strtol(nextToken+1, NULL, 16);

            if (rip == NULL)
                rip = new Rip(ID_SYSTEM_INTELLIVISION);
            ROM* nextRom = new ROM("Cartridge ROM", "", 0, (romBitWidth+7)/8, romSize, romAddress);
            rip->AddROM(nextRom);
            if (triggerAddress != 0)
                rip->AddRAM(new ROMBanker(nextRom, triggerAddress, triggerMask, triggerValue, onMask, onValue));

            parseSuccess = TRUE;
        }
        else if ((nextToken = strstr(nextLine, "RAM")) != NULL) {
            //TODO: check to make sure we don't exceed the maximum number of rams for a Rip

            //parse ram width
            if ((nextToken = strrchr(nextLine, ':')) == NULL)
                continue;
            UINT8 ramBitWidth = (UINT8)atoi(nextToken+1);

            //parse ram size
            *nextToken = NULL;
            if ((nextToken = strrchr(nextLine, ':')) == NULL)
                continue;
            UINT16 ramSize = (UINT16)strtol(nextToken+1, NULL, 16);

            //parse ram address
            *nextToken = NULL;
            if ((nextToken = strrchr(nextLine, ':')) == NULL)
                continue;
            UINT16 ramAddress = (UINT16)strtol(nextToken+1, NULL, 16);

            if (rip == NULL)
                rip = new Rip(ID_SYSTEM_INTELLIVISION);
            rip->AddRAM(new RAM(ramSize, ramAddress, 0xFFFF, 0xFFFF, ramBitWidth));
            parseSuccess = TRUE;
        }
        else if ((nextToken = strstr(nextLine, "Peripheral")) != NULL) {
            //TODO: check to make sure we don't exceed the maximum number of peripherals for a Rip

            if ((nextToken = strrchr(nextLine, ':')) == NULL)
                continue;

            PeripheralCompatibility pc;
            if (strcmpi(nextToken+1, "required") != 0)
                pc = PERIPH_REQUIRED;
            else if (strcmpi(nextToken+1, "optional") != 0)
                pc = PERIPH_OPTIONAL;
            else
                continue;

            *nextToken = NULL;
            if ((nextToken = strrchr(nextLine, ':')) == NULL)
                continue;

            if (rip == NULL)
                rip = new Rip(ID_SYSTEM_INTELLIVISION);
            rip->AddPeripheralUsage(nextToken+1, pc);
            parseSuccess = TRUE;
        }
    }
    fclose(cfgFile);
 
    if (rip != NULL && !parseSuccess) {
        delete rip;
        rip = NULL;
    }

    return rip;
}

Rip* Rip::LoadRom(const CHAR* filename)
{
    FILE* infile = fopen(filename, "rb");
    if (infile == NULL) {
        return NULL;
    }

    //read the magic byte (should always be $A8 or $41)
    int read = fgetc(infile);
    if ((read != 0xA8) && (read != 0x41))
    {
        fclose(infile);
        return NULL;
    }

    //read the number of ROM segments
    int romSegmentCount = fgetc(infile);
    read = fgetc(infile);
    if ((read ^ 0xFF) != romSegmentCount) {
        fclose(infile);
        return NULL;
    }

    Rip* rip = new Rip(ID_SYSTEM_INTELLIVISION);
    
    int i;
    for (i = 0; i < romSegmentCount; i++) {
        UINT16 start = (UINT16)(fgetc(infile) << 8);
        UINT16 end = (UINT16)((fgetc(infile) << 8) | 0xFF);
        UINT16 size = (UINT16)((end-start)+1);

        //finally, transfer the ROM image
        UINT16* romImage = new UINT16[size];
        int j;
        for (j = 0; j < size; j++) {
            int nextbyte = fgetc(infile) << 8;
            nextbyte |= fgetc(infile);
            romImage[j] = (UINT16)nextbyte;
        }
        rip->AddROM(new ROM("Cartridge ROM", romImage, 2, size, start, 0xFFFF));
        delete[] romImage;

        //read the CRC
        fgetc(infile); fgetc(infile);
        //TODO: validate the CRC16 instead of just skipping it
        //int crc16 = (fgetc(infile) << 8) | fgetc(infile);
        //...
    }

    //no support currently for the access tables, so skip them
    for (i = 0; i < 16; i++)
        fgetc(infile);

    //no support currently for the fine address restriction
    //tables, so skip them, too
    for (i = 0; i < 32; i++)
        fgetc(infile);

    // Read through the rest of the .ROM and look for tags...
    while ((read = fgetc(infile)) != -1) 
    {
        int length = (read & 0x3F);
        read = (read & 0xC) >> 6;
        for (i = 0; i < read; i++)
            length = (length | (fgetc(infile) << ((8*i)+6)));

        int type = fgetc(infile);
        int crc16;
        switch (type) 
        {
            case ROM_TAG_TITLE:
            {
                CHAR* title = new char[length*sizeof(char)];
                for (i = 0; i < length; i++) {
                    read = fgetc(infile);
                    title[i] = (char)read;
                }
                crc16 = (fgetc(infile) << 8) | fgetc(infile);
                rip->SetName(title);
                delete[] title;
            }
            break;
            case ROM_TAG_PUBLISHER:
            {
                CHAR* publisher = new char[length*sizeof(char)];
                for (i = 0; i < length; i++) {
                    read = fgetc(infile);
                    publisher[i] = (char)read;
                }
                crc16 = (fgetc(infile) << 8) | fgetc(infile);
                rip->SetProducer(publisher);

                delete[] publisher;
            }
            break;
            case ROM_TAG_COMPATIBILITY:
            {
                read = fgetc(infile);
                BOOL requiresECS = ((read & 0xC0) != 0x80);
                if (requiresECS)
                    rip->AddPeripheralUsage("ECS", PERIPH_REQUIRED);
                BOOL intellivoiceSupport = ((read & 0x0C) != 0x0C);
                if (intellivoiceSupport)
                    rip->AddPeripheralUsage("Intellivoice", PERIPH_OPTIONAL);
                for (i = 0; i < length-1; i++) 
                {
                    fgetc(infile);
                    fgetc(infile);
                }
                fgetc(infile);
                fgetc(infile);
            }
            break;
            case ROM_TAG_RELEASE_DATE:
            default:
            {
                for (i = 0; i < length; i++) {
                    fgetc(infile);
                    fgetc(infile);
                }
                fgetc(infile);
                fgetc(infile);
            }
            break;
        }
    }
    fclose(infile);
    
    // Add the JLP RAM module if required...
    extern bool bUseJLP;
    if (bUseJLP)
    {
        rip->JLP16Bit = new JLP();
        rip->AddRAM(rip->JLP16Bit);
    }
    else
    {
        rip->JLP16Bit = NULL;
    }
    
    extern bool bForceIvoice;
    if (bForceIvoice) rip->AddPeripheralUsage("Intellivoice", PERIPH_REQUIRED);

    rip->SetFileName(filename);
    rip->crc = CRC32::getCrc(filename);

    return rip;
}



