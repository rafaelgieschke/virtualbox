/* $Id$ */
/** @file
 * Video recording audio backend for Main.
 *
 * This driver is part of Main and is responsible for providing audio
 * data to Main's video capturing feature.
 *
 * The driver itself implements a PDM host audio backend, which in turn
 * provides the driver with the required audio data and audio events.
 *
 * For now there is support for the following destinations (called "sinks"):
 *
 * - Direct writing of .webm files to the host.
 * - Communicating with Main via the Console object to send the encoded audio data to.
 *   The Console object in turn then will route the data to the Display / video capturing interface then.
 */

/*
 * Copyright (C) 2016-2022 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* This code makes use of the Opus codec (libopus):
 *
 * Copyright 2001-2011 Xiph.Org, Skype Limited, Octasic,
 *                     Jean-Marc Valin, Timothy B. Terriberry,
 *                     CSIRO, Gregory Maxwell, Mark Borgerding,
 *                     Erik de Castro Lopo
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of Internet Society, IETF or IETF Trust, nor the
 * names of specific contributors, may be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Opus is subject to the royalty-free patent licenses which are
 * specified at:
 *
 * Xiph.Org Foundation:
 * https://datatracker.ietf.org/ipr/1524/
 *
 * Microsoft Corporation:
 * https://datatracker.ietf.org/ipr/1914/
 *
 * Broadcom Corporation:
 * https://datatracker.ietf.org/ipr/1526/
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_HOST_AUDIO
#include "LoggingNew.h"

#include "DrvAudioRec.h"
#include "ConsoleImpl.h"

#include "WebMWriter.h"

#include <iprt/mem.h>
#include <iprt/cdefs.h>

#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>
#include <VBox/err.h>

#ifdef VBOX_WITH_LIBOPUS
# include <opus.h>
#endif


/*********************************************************************************************************************************
*   Defines                                                                                                                      *
*********************************************************************************************************************************/
#define AVREC_OPUS_HZ_MAX               48000   /**< Maximum sample rate (in Hz) Opus can handle. */
#define AVREC_OPUS_FRAME_MS_DEFAULT     20      /**< Default Opus frame size (in ms). */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Enumeration for specifying the recording container type.
 */
typedef enum AVRECCONTAINERTYPE
{
    /** Unknown / invalid container type. */
    AVRECCONTAINERTYPE_UNKNOWN      = 0,
    /** Recorded data goes to Main / Console. */
    AVRECCONTAINERTYPE_MAIN_CONSOLE = 1,
    /** Recorded data will be written to a .webm file. */
    AVRECCONTAINERTYPE_WEBM         = 2
} AVRECCONTAINERTYPE;

/**
 * Structure for keeping generic container parameters.
 */
typedef struct AVRECCONTAINERPARMS
{
    /** The container's type. */
    AVRECCONTAINERTYPE      enmType;
    union
    {
        /** WebM file specifics. */
        struct
        {
            /** Allocated file name to write .webm file to. Must be free'd. */
            char *pszFile;
        } WebM;
    };

} AVRECCONTAINERPARMS, *PAVRECCONTAINERPARMS;

/**
 * Structure for keeping container-specific data.
 */
typedef struct AVRECCONTAINER
{
    /** Generic container parameters. */
    AVRECCONTAINERPARMS     Parms;

    union
    {
        struct
        {
            /** Pointer to Console. */
            Console        *pConsole;
        } Main;

        struct
        {
            /** Pointer to WebM container to write recorded audio data to.
             *  See the AVRECMODE enumeration for more information. */
            WebMWriter     *pWebM;
            /** Assigned track number from WebM container. */
            uint8_t         uTrack;
        } WebM;
    };
} AVRECCONTAINER, *PAVRECCONTAINER;

/**
 * Structure for keeping generic codec parameters.
 */
typedef struct AVRECCODECPARMS
{
    /** The codec's used PCM properties. */
    PDMAUDIOPCMPROPS        PCMProps;
    /** The codec's bitrate. 0 if not used / cannot be specified. */
    uint32_t                uBitrate;

} AVRECCODECPARMS, *PAVRECCODECPARMS;

/**
 * Structure for keeping codec-specific data.
 */
typedef struct AVRECCODEC
{
    /** Generic codec parameters. */
    AVRECCODECPARMS         Parms;
    union
    {
#ifdef VBOX_WITH_LIBOPUS
        struct
        {
            /** Encoder we're going to use. */
            OpusEncoder    *pEnc;
            /** Time (in ms) an (encoded) frame takes.
             *
             *  For Opus, valid frame sizes are:
             *  ms           Frame size
             *  2.5          120
             *  5            240
             *  10           480
             *  20 (Default) 960
             *  40           1920
             *  60           2880
             */
            uint32_t        msFrame;
            /** The frame size in bytes (based on msFrame). */
            uint32_t        cbFrame;
            /** The frame size in samples per frame (based on msFrame). */
            uint32_t        csFrame;
        } Opus;
#endif /* VBOX_WITH_LIBOPUS */
    };

#ifdef VBOX_WITH_STATISTICS /** @todo Register these values. */
    struct
    {
        /** Number of frames encoded. */
        uint64_t        cEncFrames;
        /** Total time (in ms) of already encoded audio data. */
        uint64_t        msEncTotal;
    } Stats;
#endif
} AVRECCODEC, *PAVRECCODEC;

typedef struct AVRECSINK
{
    /** @todo Add types for container / codec as soon as we implement more stuff. */

    /** Container data to use for data processing. */
    AVRECCONTAINER       Con;
    /** Codec data this sink uses for encoding. */
    AVRECCODEC           Codec;
    /** Timestamp (in ms) of when the sink was created. */
    uint64_t             tsStartMs;
} AVRECSINK, *PAVRECSINK;

/**
 * Audio video recording (output) stream.
 */
typedef struct AVRECSTREAM
{
    /** Common part. */
    PDMAUDIOBACKENDSTREAM   Core;
    /** The stream's acquired configuration. */
    PDMAUDIOSTREAMCFG       Cfg;
    /** (Audio) frame buffer. */
    PRTCIRCBUF              pCircBuf;
    /** Pointer to sink to use for writing. */
    PAVRECSINK              pSink;
    /** Last encoded PTS (in ms). */
    uint64_t                uLastPTSMs;
    /** Temporary buffer for the input (source) data to encode. */
    void                   *pvSrcBuf;
    /** Size (in bytes) of the temporary buffer holding the input (source) data to encode. */
    size_t                  cbSrcBuf;
    /** Temporary buffer for the encoded output (destination) data. */
    void                   *pvDstBuf;
    /** Size (in bytes) of the temporary buffer holding the encoded output (destination) data. */
    size_t                  cbDstBuf;
} AVRECSTREAM, *PAVRECSTREAM;

/**
 * Video recording audio driver instance data.
 */
typedef struct DRVAUDIORECORDING
{
    /** Pointer to audio video recording object. */
    AudioVideoRec       *pAudioVideoRec;
    /** Pointer to the driver instance structure. */
    PPDMDRVINS           pDrvIns;
    /** Pointer to host audio interface. */
    PDMIHOSTAUDIO        IHostAudio;
    /** Pointer to the console object. */
    ComPtr<Console>      pConsole;
    /** Pointer to the DrvAudio port interface that is above us. */
    PPDMIAUDIOCONNECTOR  pDrvAudio;
    /** The driver's configured container parameters. */
    AVRECCONTAINERPARMS  ContainerParms;
    /** The driver's configured codec parameters. */
    AVRECCODECPARMS      CodecParms;
    /** The driver's sink for writing output to. */
    AVRECSINK            Sink;
} DRVAUDIORECORDING, *PDRVAUDIORECORDING;


AudioVideoRec::AudioVideoRec(Console *pConsole)
    : AudioDriver(pConsole)
    , mpDrv(NULL)
{
}


AudioVideoRec::~AudioVideoRec(void)
{
    if (mpDrv)
    {
        mpDrv->pAudioVideoRec = NULL;
        mpDrv = NULL;
    }
}


/**
 * Applies a video recording configuration to this driver instance.
 *
 * @returns VBox status code.
 * @param   Settings        Capturing configuration to apply.
 */
int AudioVideoRec::applyConfiguration(const settings::RecordingSettings &Settings)
{
    /** @todo Do some validation here. */
    mVideoRecCfg = Settings; /* Note: Does have an own copy operator. */
    return VINF_SUCCESS;
}


/**
 * @copydoc AudioDriver::configureDriver
 */
int AudioVideoRec::configureDriver(PCFGMNODE pLunCfg)
{
    int rc = CFGMR3InsertInteger(pLunCfg, "Object",    (uintptr_t)mpConsole->i_recordingGetAudioDrv());
    AssertRCReturn(rc, rc);
    rc = CFGMR3InsertInteger(pLunCfg, "ObjectConsole", (uintptr_t)mpConsole);
    AssertRCReturn(rc, rc);

    /** @todo For now we're using the configuration of the first screen here audio-wise. */
    Assert(mVideoRecCfg.mapScreens.size() >= 1);
    const settings::RecordingScreenSettings &Screen0Settings = mVideoRecCfg.mapScreens[0];

    rc = CFGMR3InsertInteger(pLunCfg, "ContainerType", (uint64_t)Screen0Settings.enmDest);
    AssertRCReturn(rc, rc);
    if (Screen0Settings.enmDest == RecordingDestination_File)
    {
        rc = CFGMR3InsertString(pLunCfg, "ContainerFileName", Utf8Str(Screen0Settings.File.strName).c_str());
        AssertRCReturn(rc, rc);
    }
    rc = CFGMR3InsertInteger(pLunCfg, "CodecHz", Screen0Settings.Audio.uHz);
    AssertRCReturn(rc, rc);
    rc = CFGMR3InsertInteger(pLunCfg, "CodecBits", Screen0Settings.Audio.cBits);
    AssertRCReturn(rc, rc);
    rc = CFGMR3InsertInteger(pLunCfg, "CodecChannels", Screen0Settings.Audio.cChannels);
    AssertRCReturn(rc, rc);
    rc = CFGMR3InsertInteger(pLunCfg, "CodecBitrate", 0); /* Let Opus decide for now. */
    AssertRCReturn(rc, rc);

    return AudioDriver::configureDriver(pLunCfg);
}


/*********************************************************************************************************************************
*   PDMIHOSTAUDIO                                                                                                                *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetConfig}
 */
static DECLCALLBACK(int) drvAudioVideoRecHA_GetConfig(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDCFG pBackendCfg)
{
    RT_NOREF(pInterface);
    AssertPtrReturn(pBackendCfg, VERR_INVALID_POINTER);

    /*
     * Fill in the config structure.
     */
    RTStrCopy(pBackendCfg->szName, sizeof(pBackendCfg->szName), "VideoRec");
    pBackendCfg->cbStream       = sizeof(AVRECSTREAM);
    pBackendCfg->fFlags         = 0;
    pBackendCfg->cMaxStreamsIn  = 0;
    pBackendCfg->cMaxStreamsOut = UINT32_MAX;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetStatus}
 */
static DECLCALLBACK(PDMAUDIOBACKENDSTS) drvAudioVideoRecHA_GetStatus(PPDMIHOSTAUDIO pInterface, PDMAUDIODIR enmDir)
{
    RT_NOREF(pInterface, enmDir);
    return PDMAUDIOBACKENDSTS_RUNNING;
}


/**
 * Creates an audio output stream and associates it with the specified recording sink.
 *
 * @returns VBox status code.
 * @param   pThis               Driver instance.
 * @param   pStreamAV           Audio output stream to create.
 * @param   pSink               Recording sink to associate audio output stream to.
 * @param   pCfgReq             Requested configuration by the audio backend.
 * @param   pCfgAcq             Acquired configuration by the audio output stream.
 */
static int avRecCreateStreamOut(PDRVAUDIORECORDING pThis, PAVRECSTREAM pStreamAV,
                                PAVRECSINK pSink, PCPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    AssertPtrReturn(pThis,     VERR_INVALID_POINTER);
    AssertPtrReturn(pStreamAV, VERR_INVALID_POINTER);
    AssertPtrReturn(pSink,     VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgReq,   VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgAcq,   VERR_INVALID_POINTER);

    if (pCfgReq->enmPath != PDMAUDIOPATH_OUT_FRONT)
    {
        LogRel2(("Recording: Support for surround audio not implemented yet\n"));
        AssertFailed();
        return VERR_NOT_SUPPORTED;
    }

#ifdef VBOX_WITH_LIBOPUS
    int rc = RTCircBufCreate(&pStreamAV->pCircBuf, pSink->Codec.Opus.cbFrame * 2 /* Use "double buffering" */);
    if (RT_SUCCESS(rc))
    {
        size_t cbScratchBuf = pSink->Codec.Opus.cbFrame;
        pStreamAV->pvSrcBuf = RTMemAlloc(cbScratchBuf);
        if (pStreamAV->pvSrcBuf)
        {
            pStreamAV->cbSrcBuf = cbScratchBuf;
            pStreamAV->pvDstBuf = RTMemAlloc(cbScratchBuf);
            if (pStreamAV->pvDstBuf)
            {
                pStreamAV->cbDstBuf = cbScratchBuf;

                pStreamAV->pSink      = pSink; /* Assign sink to stream. */
                pStreamAV->uLastPTSMs = 0;

                /* Make sure to let the driver backend know that we need the audio data in
                 * a specific sampling rate Opus is optimized for. */
/** @todo r=bird: pCfgAcq->Props isn't initialized at all, except for uHz... */
                pCfgAcq->Props.uHz         = pSink->Codec.Parms.PCMProps.uHz;
//                pCfgAcq->Props.cShift      = PDMAUDIOPCMPROPS_MAKE_SHIFT_PARMS(pCfgAcq->Props.cbSample, pCfgAcq->Props.cChannels);

                /* Every Opus frame marks a period for now. Optimize this later. */
                pCfgAcq->Backend.cFramesPeriod       = PDMAudioPropsMilliToFrames(&pCfgAcq->Props, pSink->Codec.Opus.msFrame);
                pCfgAcq->Backend.cFramesBufferSize   = PDMAudioPropsMilliToFrames(&pCfgAcq->Props, 100 /*ms*/); /** @todo Make this configurable. */
                pCfgAcq->Backend.cFramesPreBuffering = pCfgAcq->Backend.cFramesPeriod * 2;
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_NO_MEMORY;
    }
#else
    RT_NOREF(pThis, pSink, pStreamAV, pCfgReq, pCfgAcq);
    int rc = VERR_NOT_SUPPORTED;
#endif /* VBOX_WITH_LIBOPUS */

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCreate}
 */
static DECLCALLBACK(int) drvAudioVideoRecHA_StreamCreate(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                         PCPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    PDRVAUDIORECORDING pThis     = RT_FROM_CPP_MEMBER(pInterface, DRVAUDIORECORDING, IHostAudio);
    PAVRECSTREAM       pStreamAV = (PAVRECSTREAM)pStream;
    AssertPtrReturn(pStreamAV, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgReq, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgAcq, VERR_INVALID_POINTER);

    if (pCfgReq->enmDir == PDMAUDIODIR_IN)
        return VERR_NOT_SUPPORTED;

    /* For now we only have one sink, namely the driver's one.
     * Later each stream could have its own one, to e.g. router different stream to different sinks .*/
    PAVRECSINK pSink = &pThis->Sink;

    int rc = avRecCreateStreamOut(pThis, pStreamAV, pSink, pCfgReq, pCfgAcq);
    PDMAudioStrmCfgCopy(&pStreamAV->Cfg, pCfgAcq);

    return rc;
}


/**
 * Destroys (closes) an audio output stream.
 *
 * @returns VBox status code.
 * @param   pThis               Driver instance.
 * @param   pStreamAV           Audio output stream to destroy.
 */
static int avRecDestroyStreamOut(PDRVAUDIORECORDING pThis, PAVRECSTREAM pStreamAV)
{
    RT_NOREF(pThis);

    if (pStreamAV->pCircBuf)
    {
        RTCircBufDestroy(pStreamAV->pCircBuf);
        pStreamAV->pCircBuf = NULL;
    }

    if (pStreamAV->pvSrcBuf)
    {
        Assert(pStreamAV->cbSrcBuf);
        RTMemFree(pStreamAV->pvSrcBuf);
        pStreamAV->pvSrcBuf = NULL;
        pStreamAV->cbSrcBuf = 0;
    }

    if (pStreamAV->pvDstBuf)
    {
        Assert(pStreamAV->cbDstBuf);
        RTMemFree(pStreamAV->pvDstBuf);
        pStreamAV->pvDstBuf = NULL;
        pStreamAV->cbDstBuf = 0;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDestroy}
 */
static DECLCALLBACK(int) drvAudioVideoRecHA_StreamDestroy(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                          bool fImmediate)
{
    PDRVAUDIORECORDING pThis     = RT_FROM_CPP_MEMBER(pInterface, DRVAUDIORECORDING, IHostAudio);
    PAVRECSTREAM       pStreamAV = (PAVRECSTREAM)pStream;
    AssertPtrReturn(pStream, VERR_INVALID_POINTER);
    RT_NOREF(fImmediate);

    int rc = VINF_SUCCESS;
    if (pStreamAV->Cfg.enmDir == PDMAUDIODIR_OUT)
        rc = avRecDestroyStreamOut(pThis, pStreamAV);

    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamEnable}
 */
static DECLCALLBACK(int) drvAudioVideoRecHA_StreamEnable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDisable}
 */
static DECLCALLBACK(int) drvAudioVideoRecHA_StreamDisable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamPause}
 */
static DECLCALLBACK(int) drvAudioVideoRecHA_StreamPause(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamResume}
 */
static DECLCALLBACK(int) drvAudioVideoRecHA_StreamResume(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDrain}
 */
static DECLCALLBACK(int) drvAudioVideoRecHA_StreamDrain(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetState}
 */
static DECLCALLBACK(PDMHOSTAUDIOSTREAMSTATE) drvAudioVideoRecHA_StreamGetState(PPDMIHOSTAUDIO pInterface,
                                                                               PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    AssertPtrReturn(pStream, PDMHOSTAUDIOSTREAMSTATE_INVALID);
    return PDMHOSTAUDIOSTREAMSTATE_OKAY;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetWritable}
 */
static DECLCALLBACK(uint32_t) drvAudioVideoRecHA_StreamGetWritable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    return UINT32_MAX;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamPlay}
 */
static DECLCALLBACK(int) drvAudioVideoRecHA_StreamPlay(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                       const void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten)
{
    RT_NOREF(pInterface);
    PAVRECSTREAM pStreamAV = (PAVRECSTREAM)pStream;
    AssertPtrReturn(pStreamAV, VERR_INVALID_POINTER);
    if (cbBuf)
        AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(pcbWritten, VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;

    uint32_t cbWrittenTotal = 0;

    /*
     * Call the encoder with the data.
     */
#ifdef VBOX_WITH_LIBOPUS
    PAVRECSINK pSink    = pStreamAV->pSink;
    AssertPtr(pSink);
    PAVRECCODEC pCodec  = &pSink->Codec;
    AssertPtr(pCodec);
    PRTCIRCBUF pCircBuf = pStreamAV->pCircBuf;
    AssertPtr(pCircBuf);

    uint32_t cbToWrite = cbBuf;

    /*
     * Write as much as we can into our internal ring buffer.
     */
    while (   cbToWrite > 0
           && RTCircBufFree(pCircBuf))
    {
        void  *pvCircBuf = NULL;
        size_t cbCircBuf = 0;
        RTCircBufAcquireWriteBlock(pCircBuf, cbToWrite, &pvCircBuf, &cbCircBuf);

        if (cbCircBuf)
        {
            memcpy(pvCircBuf, (uint8_t *)pvBuf + cbWrittenTotal, cbCircBuf),
            cbWrittenTotal += (uint32_t)cbCircBuf;
            Assert(cbToWrite >= cbCircBuf);
            cbToWrite      -= (uint32_t)cbCircBuf;
        }

        RTCircBufReleaseWriteBlock(pCircBuf, cbCircBuf);
        AssertBreak(cbCircBuf);
    }

    /*
     * Process our internal ring buffer and encode the data.
     */

    /* Only encode data if we have data for the given time period (or more). */
    while (RTCircBufUsed(pCircBuf) >= pCodec->Opus.cbFrame)
    {
        LogFunc(("cbAvail=%zu, csFrame=%RU32, cbFrame=%RU32\n",
                 RTCircBufUsed(pCircBuf), pCodec->Opus.csFrame, pCodec->Opus.cbFrame));

        uint32_t cbSrc = 0;
        while (cbSrc < pCodec->Opus.cbFrame)
        {
            void  *pvCircBuf = NULL;
            size_t cbCircBuf = 0;
            RTCircBufAcquireReadBlock(pCircBuf, pCodec->Opus.cbFrame - cbSrc, &pvCircBuf, &cbCircBuf);

            if (cbCircBuf)
            {
                memcpy((uint8_t *)pStreamAV->pvSrcBuf + cbSrc, pvCircBuf, cbCircBuf);

                cbSrc += (uint32_t)cbCircBuf;
                Assert(cbSrc <= pStreamAV->cbSrcBuf);
            }

            RTCircBufReleaseReadBlock(pCircBuf, cbCircBuf);
            AssertBreak(cbCircBuf);
        }

        Assert(cbSrc == pCodec->Opus.cbFrame);

# ifdef VBOX_AUDIO_DEBUG_DUMP_PCM_DATA_PATH
        RTFILE fh;
        RTFileOpen(&fh, VBOX_AUDIO_DEBUG_DUMP_PCM_DATA_PATH "DrvAudioVideoRec.pcm",
                   RTFILE_O_OPEN_CREATE | RTFILE_O_APPEND | RTFILE_O_WRITE | RTFILE_O_DENY_NONE);
        RTFileWrite(fh, pStreamAV->pvSrcBuf, cbSrc, NULL);
        RTFileClose(fh);
# endif

        /*
         * Opus always encodes PER "OPUS FRAME", that is, exactly 2.5, 5, 10, 20, 40 or 60 ms of audio data.
         *
         * A packet can have up to 120ms worth of audio data.
         * Anything > 120ms of data will result in a "corrupted package" error message by
         * by decoding application.
         */

        /* Call the encoder to encode one "Opus frame" per iteration. */
        opus_int32 cbWritten = opus_encode(pSink->Codec.Opus.pEnc,
                                           (opus_int16 *)pStreamAV->pvSrcBuf, pCodec->Opus.csFrame,
                                           (uint8_t *)pStreamAV->pvDstBuf, (opus_int32)pStreamAV->cbDstBuf);
        if (cbWritten > 0)
        {
            /* Get overall frames encoded. */
            const uint32_t cEncFrames     = opus_packet_get_nb_frames((uint8_t *)pStreamAV->pvDstBuf, cbWritten);

# ifdef VBOX_WITH_STATISTICS
            pSink->Codec.Stats.cEncFrames += cEncFrames;
            pSink->Codec.Stats.msEncTotal += pSink->Codec.Opus.msFrame * cEncFrames;
# endif
            Assert((uint32_t)cbWritten <= (uint32_t)pStreamAV->cbDstBuf);
            const uint32_t cbDst = RT_MIN((uint32_t)cbWritten, (uint32_t)pStreamAV->cbDstBuf);

            Assert(cEncFrames == 1);

            if (pStreamAV->uLastPTSMs == 0)
                pStreamAV->uLastPTSMs = RTTimeProgramMilliTS(); /* We want the absolute time (in ms) since program start. */

            const uint64_t uDurationMs = pSink->Codec.Opus.msFrame * cEncFrames;
            const uint64_t uPTSMs      = pStreamAV->uLastPTSMs;

            pStreamAV->uLastPTSMs += uDurationMs;

            switch (pSink->Con.Parms.enmType)
            {
                case AVRECCONTAINERTYPE_MAIN_CONSOLE:
                {
                    HRESULT hr = pSink->Con.Main.pConsole->i_recordingSendAudio(pStreamAV->pvDstBuf, cbDst, uPTSMs);
                    Assert(hr == S_OK);
                    RT_NOREF(hr);
                    break;
                }

                case AVRECCONTAINERTYPE_WEBM:
                {
                    WebMWriter::BlockData_Opus blockData = { pStreamAV->pvDstBuf, cbDst, uPTSMs };
                    rc = pSink->Con.WebM.pWebM->WriteBlock(pSink->Con.WebM.uTrack, &blockData, sizeof(blockData));
                    AssertRC(rc);
                    break;
                }

                default:
                    AssertFailedStmt(rc = VERR_NOT_IMPLEMENTED);
                    break;
            }
        }
        else if (cbWritten < 0)
        {
            AssertMsgFailed(("Encoding failed: %s\n", opus_strerror(cbWritten)));
            rc = VERR_INVALID_PARAMETER;
        }

        if (RT_FAILURE(rc))
            break;
    }

    *pcbWritten = cbWrittenTotal;
#else
    /* Report back all data as being processed. */
    *pcbWritten = cbBuf;

    rc = VERR_NOT_SUPPORTED;
#endif /* VBOX_WITH_LIBOPUS */

    LogFlowFunc(("csReadTotal=%RU32, rc=%Rrc\n", cbWrittenTotal, rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetReadable}
 */
static DECLCALLBACK(uint32_t) drvAudioVideoRecHA_StreamGetReadable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface, pStream);
    return 0; /* Video capturing does not provide any input. */
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCapture}
 */
static DECLCALLBACK(int) drvAudioVideoRecHA_StreamCapture(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                          void *pvBuf, uint32_t cbBuf, uint32_t *pcbRead)
{
    RT_NOREF(pInterface, pStream, pvBuf, cbBuf);
    *pcbRead = 0;
    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   PDMIBASE                                                                                                                     *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvAudioVideoRecQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVAUDIORECORDING pThis = PDMINS_2_DATA(pDrvIns, PDRVAUDIORECORDING);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHOSTAUDIO, &pThis->IHostAudio);
    return NULL;
}


/*********************************************************************************************************************************
*   PDMDRVREG                                                                                                                    *
*********************************************************************************************************************************/

/**
 * Shuts down (closes) a recording sink,
 *
 * @returns VBox status code.
 * @param   pSink               Recording sink to shut down.
 */
static void avRecSinkShutdown(PAVRECSINK pSink)
{
    AssertPtrReturnVoid(pSink);

#ifdef VBOX_WITH_LIBOPUS
    if (pSink->Codec.Opus.pEnc)
    {
        opus_encoder_destroy(pSink->Codec.Opus.pEnc);
        pSink->Codec.Opus.pEnc = NULL;
    }
#endif
    switch (pSink->Con.Parms.enmType)
    {
        case AVRECCONTAINERTYPE_WEBM:
        {
            if (pSink->Con.WebM.pWebM)
            {
                LogRel2(("Recording: Finished recording audio to file '%s' (%zu bytes)\n",
                         pSink->Con.WebM.pWebM->GetFileName().c_str(), pSink->Con.WebM.pWebM->GetFileSize()));

                int rc2 = pSink->Con.WebM.pWebM->Close();
                AssertRC(rc2);

                delete pSink->Con.WebM.pWebM;
                pSink->Con.WebM.pWebM = NULL;
            }
            break;
        }

        case AVRECCONTAINERTYPE_MAIN_CONSOLE:
        default:
            break;
    }
}


/**
 * @interface_method_impl{PDMDRVREG,pfnPowerOff}
 */
/*static*/ DECLCALLBACK(void) AudioVideoRec::drvPowerOff(PPDMDRVINS pDrvIns)
{
    PDRVAUDIORECORDING pThis = PDMINS_2_DATA(pDrvIns, PDRVAUDIORECORDING);
    LogFlowFuncEnter();
    avRecSinkShutdown(&pThis->Sink);
}


/**
 * @interface_method_impl{PDMDRVREG,pfnDestruct}
 */
/*static*/ DECLCALLBACK(void) AudioVideoRec::drvDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVAUDIORECORDING pThis = PDMINS_2_DATA(pDrvIns, PDRVAUDIORECORDING);

    LogFlowFuncEnter();

    switch (pThis->ContainerParms.enmType)
    {
        case AVRECCONTAINERTYPE_WEBM:
        {
            avRecSinkShutdown(&pThis->Sink);
            RTStrFree(pThis->ContainerParms.WebM.pszFile);
            break;
        }

        default:
            break;
    }

    /*
     * If the AudioVideoRec object is still alive, we must clear it's reference to
     * us since we'll be invalid when we return from this method.
     */
    if (pThis->pAudioVideoRec)
    {
        pThis->pAudioVideoRec->mpDrv = NULL;
        pThis->pAudioVideoRec = NULL;
    }

    LogFlowFuncLeave();
}


/**
 * Initializes a recording sink.
 *
 * @returns VBox status code.
 * @param   pThis               Driver instance.
 * @param   pSink               Sink to initialize.
 * @param   pConParms           Container parameters to set.
 * @param   pCodecParms         Codec parameters to set.
 */
static int avRecSinkInit(PDRVAUDIORECORDING pThis, PAVRECSINK pSink, PAVRECCONTAINERPARMS pConParms, PAVRECCODECPARMS pCodecParms)
{
    uint32_t       uHz       = PDMAudioPropsHz(&pCodecParms->PCMProps);
    uint8_t const  cbSample  = PDMAudioPropsSampleSize(&pCodecParms->PCMProps);
    uint8_t        cChannels = PDMAudioPropsChannels(&pCodecParms->PCMProps);
    uint32_t       uBitrate  = pCodecParms->uBitrate;

    /* Opus only supports certain input sample rates in an efficient manner.
     * So make sure that we use those by resampling the data to the requested rate. */
    if      (uHz > 24000) uHz = AVREC_OPUS_HZ_MAX;
    else if (uHz > 16000) uHz = 24000;
    else if (uHz > 12000) uHz = 16000;
    else if (uHz > 8000 ) uHz = 12000;
    else     uHz = 8000;

    if (cChannels > 2)
    {
        LogRel(("Recording: Warning: More than 2 (stereo) channels are not supported at the moment\n"));
        cChannels = 2;
    }

    int orc;
    OpusEncoder *pEnc = opus_encoder_create(uHz, cChannels, OPUS_APPLICATION_AUDIO, &orc);
    if (orc != OPUS_OK)
    {
        LogRel(("Recording: Audio codec failed to initialize: %s\n", opus_strerror(orc)));
        return VERR_AUDIO_BACKEND_INIT_FAILED;
    }

    AssertPtr(pEnc);

    if (uBitrate) /* Only explicitly set the bitrate if we specified one. Otherwise let Opus decide. */
    {
        opus_encoder_ctl(pEnc, OPUS_SET_BITRATE(uBitrate));
        if (orc != OPUS_OK)
        {
            opus_encoder_destroy(pEnc);
            pEnc = NULL;

            LogRel(("Recording: Audio codec failed to set bitrate (%RU32): %s\n", uBitrate, opus_strerror(orc)));
            return VERR_AUDIO_BACKEND_INIT_FAILED;
        }
    }

    const bool fUseVBR = true; /** Use Variable Bit Rate (VBR) by default. @todo Make this configurable? */

    orc = opus_encoder_ctl(pEnc, OPUS_SET_VBR(fUseVBR ? 1 : 0));
    if (orc != OPUS_OK)
    {
        opus_encoder_destroy(pEnc);
        pEnc = NULL;

        LogRel(("Recording: Audio codec failed to %s VBR mode: %s\n", fUseVBR ? "enable" : "disable", opus_strerror(orc)));
        return VERR_AUDIO_BACKEND_INIT_FAILED;
    }

    int rc = VINF_SUCCESS;

    try
    {
        switch (pConParms->enmType)
        {
            case AVRECCONTAINERTYPE_MAIN_CONSOLE:
            {
                if (pThis->pConsole)
                {
                    pSink->Con.Main.pConsole = pThis->pConsole;
                }
                else
                    rc = VERR_NOT_SUPPORTED;
                break;
            }

            case AVRECCONTAINERTYPE_WEBM:
            {
                /* If we only record audio, create our own WebM writer instance here. */
                if (!pSink->Con.WebM.pWebM) /* Do we already have our WebM writer instance? */
                {
                    /** @todo Add sink name / number to file name. */
                    const char *pszFile = pSink->Con.Parms.WebM.pszFile;
                    AssertPtr(pszFile);

                    pSink->Con.WebM.pWebM = new WebMWriter();
                    rc = pSink->Con.WebM.pWebM->Open(pszFile,
                                                     /** @todo Add option to add some suffix if file exists instead of overwriting? */
                                                     RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_DENY_NONE,
                                                     WebMWriter::AudioCodec_Opus, WebMWriter::VideoCodec_None);
                    if (RT_SUCCESS(rc))
                    {
                        rc = pSink->Con.WebM.pWebM->AddAudioTrack(uHz, cChannels, cbSample * 8 /* Bits */,
                                                                  &pSink->Con.WebM.uTrack);
                        if (RT_SUCCESS(rc))
                        {
                            LogRel(("Recording: Recording audio to audio file '%s'\n", pszFile));
                        }
                        else
                            LogRel(("Recording: Error creating audio track for audio file '%s' (%Rrc)\n", pszFile, rc));
                    }
                    else
                        LogRel(("Recording: Error creating audio file '%s' (%Rrc)\n", pszFile, rc));
                }
                break;
            }

            default:
                rc = VERR_NOT_SUPPORTED;
                break;
        }
    }
    catch (std::bad_alloc &)
    {
        rc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(rc))
    {
        pSink->Con.Parms.enmType     = pConParms->enmType;

        PAVRECCODEC pCodec = &pSink->Codec;

        PDMAudioPropsInit(&pCodec->Parms.PCMProps, cbSample, pCodecParms->PCMProps.fSigned, cChannels, uHz);
        pCodec->Parms.uBitrate  = uBitrate;

        pCodec->Opus.pEnc       = pEnc;
        pCodec->Opus.msFrame    = AVREC_OPUS_FRAME_MS_DEFAULT;

        if (!pCodec->Opus.msFrame)
            pCodec->Opus.msFrame = AVREC_OPUS_FRAME_MS_DEFAULT; /* 20ms by default; to prevent division by zero. */
        pCodec->Opus.csFrame = pSink->Codec.Parms.PCMProps.uHz / (1000 /* s in ms */ / pSink->Codec.Opus.msFrame);
        pCodec->Opus.cbFrame = PDMAudioPropsFramesToBytes(&pSink->Codec.Parms.PCMProps, pCodec->Opus.csFrame);

#ifdef VBOX_WITH_STATISTICS
        pSink->Codec.Stats.cEncFrames = 0;
        pSink->Codec.Stats.msEncTotal = 0;
#endif
        pSink->tsStartMs              = RTTimeMilliTS();
    }
    else
    {
        if (pEnc)
        {
            opus_encoder_destroy(pEnc);
            pEnc = NULL;
        }

        LogRel(("Recording: Error creating sink (%Rrc)\n", rc));
    }

    return rc;
}


/**
 * Construct a audio video recording driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
/*static*/ DECLCALLBACK(int) AudioVideoRec::drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVAUDIORECORDING pThis = PDMINS_2_DATA(pDrvIns, PDRVAUDIORECORDING);
    RT_NOREF(fFlags);

    LogRel(("Audio: Initializing video recording audio driver\n"));
    LogFlowFunc(("fFlags=0x%x\n", fFlags));

    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                   = pDrvIns;
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface = drvAudioVideoRecQueryInterface;
    /* IHostAudio */
    pThis->IHostAudio.pfnGetConfig                  = drvAudioVideoRecHA_GetConfig;
    pThis->IHostAudio.pfnGetDevices                 = NULL;
    pThis->IHostAudio.pfnSetDevice                  = NULL;
    pThis->IHostAudio.pfnGetStatus                  = drvAudioVideoRecHA_GetStatus;
    pThis->IHostAudio.pfnDoOnWorkerThread           = NULL;
    pThis->IHostAudio.pfnStreamConfigHint           = NULL;
    pThis->IHostAudio.pfnStreamCreate               = drvAudioVideoRecHA_StreamCreate;
    pThis->IHostAudio.pfnStreamInitAsync            = NULL;
    pThis->IHostAudio.pfnStreamDestroy              = drvAudioVideoRecHA_StreamDestroy;
    pThis->IHostAudio.pfnStreamNotifyDeviceChanged  = NULL;
    pThis->IHostAudio.pfnStreamEnable               = drvAudioVideoRecHA_StreamEnable;
    pThis->IHostAudio.pfnStreamDisable              = drvAudioVideoRecHA_StreamDisable;
    pThis->IHostAudio.pfnStreamPause                = drvAudioVideoRecHA_StreamPause;
    pThis->IHostAudio.pfnStreamResume               = drvAudioVideoRecHA_StreamResume;
    pThis->IHostAudio.pfnStreamDrain                = drvAudioVideoRecHA_StreamDrain;
    pThis->IHostAudio.pfnStreamGetState             = drvAudioVideoRecHA_StreamGetState;
    pThis->IHostAudio.pfnStreamGetPending           = NULL;
    pThis->IHostAudio.pfnStreamGetWritable          = drvAudioVideoRecHA_StreamGetWritable;
    pThis->IHostAudio.pfnStreamPlay                 = drvAudioVideoRecHA_StreamPlay;
    pThis->IHostAudio.pfnStreamGetReadable          = drvAudioVideoRecHA_StreamGetReadable;
    pThis->IHostAudio.pfnStreamCapture              = drvAudioVideoRecHA_StreamCapture;

    /*
     * Get the Console object pointer.
     */
    void *pvUser;
    int rc = CFGMR3QueryPtr(pCfg, "ObjectConsole", &pvUser); /** @todo r=andy Get rid of this hack and use IHostAudio::SetCallback. */
    AssertRCReturn(rc, rc);

    /* CFGM tree saves the pointer to Console in the Object node of AudioVideoRec. */
    pThis->pConsole = (Console *)pvUser;
    AssertReturn(!pThis->pConsole.isNull(), VERR_INVALID_POINTER);

    /*
     * Get the pointer to the audio driver instance.
     */
    rc = CFGMR3QueryPtr(pCfg, "Object", &pvUser); /** @todo r=andy Get rid of this hack and use IHostAudio::SetCallback. */
    AssertRCReturn(rc, rc);

    pThis->pAudioVideoRec = (AudioVideoRec *)pvUser;
    AssertPtrReturn(pThis->pAudioVideoRec, VERR_INVALID_POINTER);

    /*
     * Get the recording container and codec parameters from the audio driver instance.
     */
    PAVRECCONTAINERPARMS pConParams  = &pThis->ContainerParms;
    PAVRECCODECPARMS     pCodecParms = &pThis->CodecParms;

    RT_ZERO(pThis->ContainerParms);
    RT_ZERO(pThis->CodecParms);

    rc = CFGMR3QueryU32(pCfg, "ContainerType", (uint32_t *)&pConParams->enmType);
    AssertRCReturn(rc, rc);

    switch (pConParams->enmType)
    {
        case AVRECCONTAINERTYPE_WEBM:
            rc = CFGMR3QueryStringAlloc(pCfg, "ContainerFileName", &pConParams->WebM.pszFile);
            AssertRCReturn(rc, rc);
            break;

        default:
            break;
    }

    uint32_t uHz = 0;
    rc = CFGMR3QueryU32(pCfg, "CodecHz", &uHz);
    AssertRCReturn(rc, rc);

    uint8_t cSampleBits = 0;
    rc = CFGMR3QueryU8(pCfg,  "CodecBits", &cSampleBits); /** @todo CodecBits != CodecBytes */
    AssertRCReturn(rc, rc);

    uint8_t cChannels = 0;
    rc = CFGMR3QueryU8(pCfg,  "CodecChannels", &cChannels);
    AssertRCReturn(rc, rc);

    PDMAudioPropsInit(&pCodecParms->PCMProps, cSampleBits / 8, true /*fSigned*/, cChannels, uHz);
    AssertMsgReturn(PDMAudioPropsAreValid(&pCodecParms->PCMProps),
                    ("Configuration error: Audio configuration is invalid!\n"), VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES); /** @todo wrong status code. */

    rc = CFGMR3QueryU32(pCfg, "CodecBitrate", &pCodecParms->uBitrate);
    AssertRCReturn(rc, rc);

    pThis->pAudioVideoRec = (AudioVideoRec *)pvUser;
    AssertPtrReturn(pThis->pAudioVideoRec, VERR_INVALID_POINTER);

    pThis->pAudioVideoRec->mpDrv = pThis;

    /*
     * Get the interface for the above driver (DrvAudio) to make mixer/conversion calls.
     * Described in CFGM tree.
     */
/** @todo r=bird: What on earth do you think you need this for?!? It's not an
 * interface lower drivers are supposed to be messing with! */
    pThis->pDrvAudio = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIAUDIOCONNECTOR);
    AssertMsgReturn(pThis->pDrvAudio, ("Configuration error: No upper interface specified!\n"), VERR_PDM_MISSING_INTERFACE_ABOVE);

#ifdef VBOX_AUDIO_DEBUG_DUMP_PCM_DATA_PATH
    RTFileDelete(VBOX_AUDIO_DEBUG_DUMP_PCM_DATA_PATH "DrvAudioVideoRec.webm");
    RTFileDelete(VBOX_AUDIO_DEBUG_DUMP_PCM_DATA_PATH "DrvAudioVideoRec.pcm");
#endif

    /*
     * Init the recording sink.
     */
    LogRel(("Recording: Audio driver is using %RU32Hz, %RU16bit, %RU8 channel%s\n",
            PDMAudioPropsHz(&pThis->CodecParms.PCMProps), PDMAudioPropsSampleBits(&pThis->CodecParms.PCMProps),
            PDMAudioPropsChannels(&pThis->CodecParms.PCMProps), PDMAudioPropsChannels(&pThis->CodecParms.PCMProps) == 1 ? "" : "s"));

    rc = avRecSinkInit(pThis, &pThis->Sink, &pThis->ContainerParms, &pThis->CodecParms);
    if (RT_SUCCESS(rc))
        LogRel2(("Recording: Audio recording driver initialized\n"));
    else
        LogRel(("Recording: Audio recording driver initialization failed: %Rrc\n", rc));

    return rc;
}


/**
 * Video recording audio driver registration record.
 */
const PDMDRVREG AudioVideoRec::DrvReg =
{
    PDM_DRVREG_VERSION,
    /* szName */
    "AudioVideoRec",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Audio driver for video recording",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_AUDIO,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVAUDIORECORDING),
    /* pfnConstruct */
    AudioVideoRec::drvConstruct,
    /* pfnDestruct */
    AudioVideoRec::drvDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    AudioVideoRec::drvPowerOff,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

