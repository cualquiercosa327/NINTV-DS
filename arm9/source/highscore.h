// =====================================================================================
// Copyright (c) 2021-2024 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, its source code and associated 
// readme files, with or without modification, are permitted in any medium without 
// royalty provided the this copyright notice is used and wavemotion-dave (NINTV-DS)
// and Kyle Davis (BLISS) are thanked profusely. 
//
// The NINTV-DS emulator is offered as-is, without any warranty.
// =====================================================================================

#ifndef __HIGHSCORE_H
#define __HIGHSCORE_H

#include <nds.h>
#include "types.h"

extern void highscore_init(void);
extern void highscore_save(void);
extern void highscore_display(UINT32 crc);

#endif
