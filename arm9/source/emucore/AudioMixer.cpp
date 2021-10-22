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
#include <nds.h>
#include <stdio.h>
#include <string.h>
#include "AudioMixer.h"
#include "AudioOutputLine.h"
#include "Emulator.h"
#include "../ds_tools.h"
#include "../config.h"

UINT16 audio_mixer_buffer[256] __attribute__((section(".dtcm")));
UINT32 sampleSize __attribute__((section(".dtcm")));
UINT16 currentSampleIdx16 __attribute__((section(".dtcm"))) = 0;
UINT8  currentSampleIdx8 __attribute__((section(".dtcm"))) = 0;

extern UINT64 lcm(UINT64, UINT64);

AudioMixer::AudioMixer()
  : Processor("Audio Mixer"),
    audioProducerCount(0),
    commonClocksPerTick(0)
{
    memset(&audioProducers, 0, sizeof(audioProducers));
    sampleSize = 0;        
}

AudioMixer::~AudioMixer()
{
}

void AudioMixer::addAudioProducer(AudioProducer* p)
{
    audioProducers[audioProducerCount] = p;
    audioProducerCount++;
}

void AudioMixer::removeAudioProducer(AudioProducer* p)
{
    for (UINT32 i = 0; i < audioProducerCount; i++) {
        if (audioProducers[i] == p) {
            for (UINT32 j = i; j < (audioProducerCount-1); j++)
                audioProducers[j] = audioProducers[j+1];
            audioProducerCount--;
            return;
        }
    }
}

void AudioMixer::removeAll()
{
    while (audioProducerCount)
        removeAudioProducer(audioProducers[0]);
}

void AudioMixer::resetProcessor()
{
    //reset instance data
    commonClocksPerTick = 0;

    // Clear out the sample buffer...
    memset(audio_mixer_buffer,0x00, 256*sizeof(UINT16));

    //iterate through my audio output lines to determine the common output clock
    UINT64 totalClockSpeed = getClockSpeed();
    audio_output_line_reset();
    for (UINT32 i = 0; i < audioProducerCount; i++) 
    {
        totalClockSpeed = lcm(totalClockSpeed, ((UINT64)audioProducers[i]->getClockSpeed()));
    }

    //iterate again to determine the clock factor of each
    commonClocksPerTick = totalClockSpeed / getClockSpeed();
    for (UINT32 i = 0; i < audioProducerCount; i++) {
        commonClocksPerSample[i] = (totalClockSpeed / audioProducers[i]->getClockSpeed())
                * audioProducers[i]->getClocksPerSample();
    }
}

void AudioMixer::init(UINT32 sampleRate)
{
    AudioMixer::release();

    clockSpeed = sampleRate;
    sampleSize = ( clockSpeed / 60.0 );
    memset(audio_mixer_buffer,0x00, 256*sizeof(UINT16));
}

void AudioMixer::release()
{
}

INT32 AudioMixer::getClockSpeed()
{
    return clockSpeed;
}

extern  INT32 clockDivisor;
ITCM_CODE INT32 AudioMixer::tick(INT32 minimum)
{
    INT32 totalSample = 0;
    extern UINT8 sp_idle;
    if (clockDivisor == SOUND_DIV_DISABLE) return minimum;

    if (sp_idle) // If the Intellivoice is idle, we only have one sound producer. 
    {
        for (INT32 totalTicks = 0; totalTicks < minimum; totalTicks++) 
        {
            //mix and flush the sample buffers

            INT32 missingClocks = (this->commonClocksPerTick - commonClockCounter[0]);
            INT64 sampleToUse = (missingClocks < 0 ? previousSample[0] : currentSample[0]);

            //account for when audio producers idle by adding enough samples to each producer's buffer
            //to fill the time since last sample calculation
            INT64 missingSampleCount = (missingClocks / commonClocksPerSample[0]);
            if (missingSampleCount != 0) 
            {
                sampleBuffer[0] += missingSampleCount * sampleToUse * commonClocksPerSample[0];
                commonClockCounter[0] += missingSampleCount * commonClocksPerSample[0];
                missingClocks -= missingSampleCount * commonClocksPerSample[0];
            }
            INT64 partialSample = sampleToUse * missingClocks;

            //calculate the sample for this line
            totalSample += (INT16)((sampleBuffer[0] + partialSample) / commonClocksPerTick);

            //clear the sample buffer for this line
            sampleBuffer[0] = -partialSample;
            commonClockCounter[0] = -missingClocks;
        }
    }
    else
    {
        for (INT32 totalTicks = 0; totalTicks < minimum; totalTicks++) 
        {
            //mix and flush the sample buffers
            for (UINT32 i = 0; i < audioProducerCount; i++) 
            {
                INT32 missingClocks = (this->commonClocksPerTick - commonClockCounter[i]);
                INT64 sampleToUse = (missingClocks < 0 ? previousSample[i] : currentSample[i]);

                //account for when audio producers idle by adding enough samples to each producer's buffer
                //to fill the time since last sample calculation
                INT64 missingSampleCount = (missingClocks / commonClocksPerSample[i]);
                if (missingSampleCount != 0) 
                {
                    sampleBuffer[i] += missingSampleCount * sampleToUse * commonClocksPerSample[i];
                    commonClockCounter[i] += missingSampleCount * commonClocksPerSample[i];
                    missingClocks -= missingSampleCount * commonClocksPerSample[i];
                }
                INT64 partialSample = sampleToUse * missingClocks;

                //calculate the sample for this line
                totalSample += (INT16)((sampleBuffer[i] + partialSample) / commonClocksPerTick);

                //clear the sample buffer for this line
                sampleBuffer[i] = -partialSample;
                commonClockCounter[i] = -missingClocks;
            }

            totalSample = totalSample >> 1; // With Intellivoice there are 2 audio producers... so divide by 2
        }
    }
    
    if (b_dsi_mode)
    {
        audio_mixer_buffer[currentSampleIdx8++] = totalSample;
    }
    else
    {
        audio_mixer_buffer[currentSampleIdx16++] = totalSample;
        if (currentSampleIdx16 == SOUND_SIZE) currentSampleIdx16=0;
    }
   
    return minimum;
}

void AudioMixer::flushAudio()
{
    //the platform subclass must copy the sampleBuffer to the device
    //before calling here (which discards the contents of sampleBuffer)
}
