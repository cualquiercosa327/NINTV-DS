// =====================================================================================
// Copyright (c) 2021 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, it's source code and associated 
// readme files, with or without modification, are permitted in any medium without 
// royalty provided the this copyright notice is used and wavemotion-dave (NINTV-DS)
// and Kyle Davis (BLISS) are thanked profusely. 
//
// The NINTV-DS emulator is offered as-is, without any warranty.
// =====================================================================================

#ifndef RIP_H
#define RIP_H

#include <string.h>
#include <vector>
#include "ripxbf.h"
#include "types.h"
#include "Peripheral.h"
#include "Memory.h"
#include "JLP.h"

using namespace std;

#define MAX_BIOSES      4
#define MAX_PERIPHERALS 4

typedef struct _CartridgeConfiguration CartridgeConfiguration;

class Rip : public Peripheral
{

public:
    virtual ~Rip();

    void SetTargetSystemID(UINT32 t) { this->targetSystemID = t; }
    UINT32 GetTargetSystemID() { return targetSystemID; }

    void SetName(const CHAR* p);

    void SetProducer(const CHAR* p);
    const CHAR* GetProducer() { return producer; }

    void SetYear(const CHAR* y);
    const CHAR* GetYear() { return year; }

    PeripheralCompatibility GetPeripheralUsage(const CHAR* periphName);

    //load a raw binary Intellivision image of a game
    static Rip* LoadBin(const CHAR* filename);

    //load an Intellivision .rom file
    static Rip* LoadRom(const CHAR* filename);

    const CHAR* GetFileName() {
        return this->filename;
    }

    UINT32 GetCRC() {
        return this->crc;
    }
    JLP *JLP16Bit;

private:
    Rip(UINT32 systemID);

    void AddPeripheralUsage(const CHAR* periphName, PeripheralCompatibility usage);
    static Rip* LoadBinCfg(const CHAR* cfgFile, UINT32 crc);

    void SetFileName(const CHAR* fname) {
        strncpy(this->filename, fname, sizeof(this->filename));
    }

    UINT32 targetSystemID;
    CHAR producer[64];
    CHAR year[12];

    //peripheral compatibility indicators
    CHAR peripheralNames[MAX_PERIPHERALS][32];
    PeripheralCompatibility peripheralUsages[MAX_PERIPHERALS];
    UINT32 peripheralCount;

	CHAR filename[MAX_PATH];
	UINT32 crc;
};

#endif
