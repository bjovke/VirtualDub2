//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2001 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_VIRTUALDUB_COMMAND_H
#define f_VIRTUALDUB_COMMAND_H

#ifdef _MSC_VER
#pragma once
#endif

#include <vd2/system/refcount.h>
#include <vd2/system/VDString.h>
#include <vd2/Riza/audiocodec.h>

class InputFile;
class IVDInputDriver;
class IVDOutputDriver;
class IVDVideoSource;
class AVIOutput;
class VideoSource;
class AudioSource;
class IDubber;
class DubOptions;
class FrameSubset;
struct VDAudioFilterGraph;

extern vdrefptr<InputFile> inputAVI;
extern VDStringW           g_inputDriver;

extern vdrefptr<IVDVideoSource> inputVideo;

extern IDubber *g_dubber;

extern VDWaveFormat *g_ACompressionFormat;
extern uint32        g_ACompressionFormatSize;
extern VDStringA     g_ACompressionFormatHint;
extern vdblock<char> g_ACompressionConfig;

extern VDAudioFilterGraph g_audioFilterGraph;

extern VDStringW g_FileOutDriver;
extern VDStringA g_FileOutFormat;
extern VDStringW g_AudioOutDriver;
extern VDStringA g_AudioOutFormat;

extern bool g_drawDecompressedFrame;
extern bool g_showStatusWindow;

///////////////////////////

struct CommandRequest
{
  VDStringW   fileOutput;
  bool        job;
  bool        propagateErrors;
  DubOptions *opt;

  CommandRequest()
  {
    job             = false;
    propagateErrors = false;
    opt             = 0;
  }
};

struct RequestVideo : public CommandRequest
{
  bool             compat;
  bool             removeAudio;
  bool             removeVideo;
  IVDOutputDriver *driver;
  const char *     format;

  RequestVideo()
  {
    compat      = false;
    removeAudio = false;
    removeVideo = false;
    driver      = 0;
    format      = 0;
  }
};

struct RequestSegmentVideo : public RequestVideo
{
  long lSegmentCount;
  long lSpillThreshold;
  long lSpillFrameThreshold;
  int  spillDigits;

  RequestSegmentVideo()
  {
    lSegmentCount        = 0;
    lSpillThreshold      = 0;
    lSpillFrameThreshold = 0;
    spillDigits          = 0;
  }
};

struct RequestWAV : public CommandRequest
{
  bool auto_w64;

  RequestWAV()
  {
    auto_w64 = true;
  }
};

struct RequestImages : public CommandRequest
{
  VDStringW filePrefix;
  VDStringW fileSuffix;
  int       minDigits;
  int       startDigit;
  int       imageFormat;
  int       quality;
};

struct VideoOperation
{
  DubOptions *    opt;
  CommandRequest *req;
  long            lSegmentCount;
  long            lSpillThreshold;
  long            lSpillFrameThreshold;
  char            iDubPriority;
  bool            propagateErrors;
  bool            backgroundPriority;
  bool            removeAudio;
  bool            removeVideo;

  VideoOperation()
  {
    req                  = 0;
    opt                  = 0;
    propagateErrors      = false;
    lSegmentCount        = 0;
    lSpillThreshold      = 0;
    lSpillFrameThreshold = 0;
    iDubPriority         = 0;
    backgroundPriority   = false;
    removeAudio          = false;
    removeVideo          = false;
  }
  VideoOperation(CommandRequest *req)
  {
    this->req            = req;
    opt                  = req->opt;
    propagateErrors      = req->propagateErrors;
    lSegmentCount        = 0;
    lSpillThreshold      = 0;
    lSpillFrameThreshold = 0;
    iDubPriority         = 0;
    backgroundPriority   = false;
    removeAudio          = false;
    removeVideo          = false;
  }
  void setPrefs();
};

void AppendAVI(const wchar_t *pszFile, uint32 flags);
int  AppendAVIAutoscanEnum(const wchar_t *pszFile);
void AppendAVIAutoscan(const wchar_t *pszFile, bool skip_first = false);
void SaveWAV(RequestWAV &req);
void SaveAVI(RequestVideo &req);
void SavePlugin(RequestVideo &req);
void SaveStripedAVI(const wchar_t *szFile);
void SaveStripeMaster(const wchar_t *szFile);
void SaveSegmentedAVI(RequestSegmentVideo &req);
void SaveImageSequence(RequestImages &req);
void EnsureSubset();
void ScanForUnreadableFrames(FrameSubset *pSubset, IVDVideoSource *pVideoSource);

#endif
