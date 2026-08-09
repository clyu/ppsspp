// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.



// Simulation of the hardware video/audio decoders.
// The idea is high level emulation where we simply use FFMPEG.

#pragma once

// An approximation of what the interface will look like. Similar to JPCSP's.

#include <map>
#include "Common/CommonTypes.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HW/MpegDemux.h"
#include "Core/HW/SimpleAudioDec.h"

class PointerWrap;
class AudioDecoder;

#ifdef USE_FFMPEG
struct SwsContext;
struct AVFrame;
struct AVIOContext;
struct AVFormatContext;
struct AVCodecContext;
#endif

inline s64 getMpegTimeStamp(const u8 *buf) {
	return (s64)buf[5] | ((s64)buf[4] << 8) | ((s64)buf[3] << 16) | ((s64)buf[2] << 24) 
		| ((s64)buf[1] << 32) | ((s64)buf[0] << 36);
}

#ifdef USE_FFMPEG
bool InitFFmpeg();
#endif

class MediaEngine {
public:
	MediaEngine();
	~MediaEngine();

	void closeMedia();
	bool loadStream(const u8 *buffer, int readSize, int RingbufferSize);
	bool reloadStream();
	bool addVideoStream(int streamNum, int streamId = -1);
	// open the mpeg context
	bool openContext(bool keepReadPos = false);
	void closeContext();
	// Releases m_pFrameRGB / m_buffer. Deliberately *not* done by closeContext() - see the comment
	// there. Anything that reaches in and replaces those two has to call this first.
	void freeVideoFrame();

	// Returns number of packets actually added. I guess the buffer might be full.
	int addStreamData(const u8 *buffer, int addSize);
	bool seekTo(s64 timestamp, int videoPixelMode);

	bool setVideoStream(int streamNum, bool force = false);
	// TODO: Return false if the stream doesn't exist.
	bool setAudioStream(int streamNum) { m_audioStream = streamNum; return true; }

	// The newest converted picture, or nullptr when no decode has produced one yet.
	u8 *getFrameImage();
	int getRemainSize();
	int getAudioRemainSize();

	// Which of the game's YCbCr buffers the next stepVideo() is decoding into. sceMpegAvcDecodeYCbCr
	// names one, and sceMpegAvcCsc later names the one it wants converted - they are not always the
	// same buffer, so we have to keep them apart.
	void SetYCbCrTarget(u32 addr) { m_ycbcrTarget = addr; }

	bool stepVideo(int videoPixelMode, bool skipFrame = false);
	int writeVideoImage(u32 bufferPtr, int frameWidth = 512, int videoPixelMode = 3);
	int writeVideoImageWithRange(u32 bufferPtr, int frameWidth, int videoPixelMode,
	                             int xpos, int ypos, int width, int height, u32 sourceAddr);
	int getAudioSamples(u32 bufferPtr);

	s64 getVideoTimeStamp();
	s64 getAudioTimeStamp();
	s64 getLastTimeStamp();

	bool IsVideoEnd() { return m_isVideoEnd; }
	bool IsNoAudioData();
	bool IsActuallyPlayingAudio();
	int VideoWidth() { return m_desWidth; }
	int VideoHeight() { return m_desHeight; }

	void DoState(PointerWrap &p);

private:
	// One converted picture, filed under the game's YCbCr buffer it was decoded into. We can't
	// emulate the real YCbCr memory layout, but a game that rotates between several buffers still
	// expects sceMpegAvcCsc to convert the one it names - handing back the newest picture instead
	// shows a movie frame where the game asked for a still image it decoded seconds ago.
	struct YCbCrSlot {
		u32 addr = 0;  // the game's buffer address, 0 while the slot is unused
		u8 *rgb = nullptr;
		size_t size = 0;  // bytes allocated, may exceed what the current picture needs
		int width = 0;
		int height = 0;
		int pixelMode = -1;
		u64 lastUsed = 0;
	};
	static constexpr int MAX_YCBCR_SLOTS = 6;

	bool SetupStreams();
	bool setVideoDim(int width = 0, int height = 0);
	void updateSwsFormat(int videoPixelMode);
	int getNextAudioFrame(u8 **buf, int *headerCode1, int *headerCode2);

	// Files the freshly converted m_pFrameRGB under the buffer SetYCbCrTarget() named. A frame the
	// decoder flagged as corrupt never displaces a picture we already hold for that buffer.
	void storeYCbCrFrame(int videoPixelMode, bool corrupt);
	const YCbCrSlot *findYCbCrSlot(u32 addr) const;
	void freeYCbCrSlots();
	void DoStateYCbCrSlots(PointerWrap &p);

	static int MpegReadbuffer(void *opaque, uint8_t *buf, int buf_size);

public:  // TODO: Very little of this below should be public.

#ifdef USE_FFMPEG
	std::map<int, AVCodecContext *> m_pCodecCtxs;
	AVFrame *m_pFrame = nullptr;
	AVFrame *m_pFrameRGB = nullptr;
	// m_pFrameRGB's backing store comes from av_malloc, so a freshly allocated one holds whatever
	// was on the heap. Nothing may be read out of it before a decode has actually filled it in -
	// after a savestate load in particular, the game keeps asking for pictures long before the
	// rebuilt decoder has produced one.
	bool m_frameRGBValid = false;
#endif

	u8 *m_buffer = nullptr;

	int m_desWidth = 0;
	int m_desHeight = 0;
	int m_bufSize;  // initialized in constructor
	s64 m_videopts = 0;

	s64 m_firstTimeStamp = 0;
	s64 m_lastTimeStamp = 0;

	bool m_isVideoEnd = false;

private:
	YCbCrSlot m_ycbcrSlots[MAX_YCBCR_SLOTS];
	u64 m_ycbcrTick = 0;  // bumped on every store, so the oldest slot is the one to recycle
	u32 m_ycbcrTarget = 0;

#ifdef USE_FFMPEG
	// Video ffmpeg context - not used for audio
	AVFormatContext *m_pFormatCtx = nullptr;
	std::vector<AVCodecContext *> m_codecsToClose;
	AVIOContext *m_pIOContext = nullptr;
	SwsContext *m_sws_ctx = nullptr;
#endif

	int m_sws_fmt = 0;
	int m_videoStream = -1;
	int m_expectedVideoStreams = 0;

	// Used by the demuxer.
	int m_audioStream = -1;

	int m_decodingsize = 0;
	BufferQueue *m_pdata = nullptr;

	s64 m_lastPts = -1;

	MpegDemux *m_demux = nullptr;
	AudioDecoder *m_audioContext = nullptr;
	s64 m_audiopts = 0;

	// used for audio type 
	int m_audioType;

	int m_ringbuffersize;

	u8 m_mpegheader[0x10000];  // TODO: Allocate separately
	int m_mpegheaderReadPos = 0;
	int m_mpegheaderSize = 0;
};

std::string GetFFMPEGVersion();
