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

#include "stdafx.h"
#include <windows.h>

#include <vd2/system/file.h>
#include <vd2/system/error.h>
#include <vd2/system/filesys.h>
#include <vd2/system/vdstl.h>

#include "PositionControl.h"

#include "InputFile.h"
#include "InputFileImages.h"
#include "AudioSource.h"
#include "VideoSource.h"
#include "AVIOutput.h"
#include "AVIOutputWAV.h"
#include "AVIOutputImages.h"
#include "AVIOutputStriped.h"
#include "Dub.h"
#include "DubOutput.h"
#include "AudioFilterSystem.h"
#include "FrameSubset.h"
#include "ProgressDialog.h"
#include "oshelper.h"

#include "mpeg.h"
#include "gui.h"
#include "prefs.h"
#include "command.h"
#include "project.h"
#include "resource.h"

///////////////////////////////////////////////////////////////////////////

extern HWND       g_hWnd;
extern DubOptions g_dubOpts;

extern wchar_t g_szInputWAVFile[MAX_PATH];

extern DubSource::ErrorMode g_videoErrorMode;
extern DubSource::ErrorMode g_audioErrorMode;

vdrefptr<InputFile> inputAVI;
VDStringW           g_inputDriver;
InputFileOptions *  g_pInputOpts = NULL;

vdrefptr<IVDVideoSource>     inputVideo;
extern vdrefptr<AudioSource> inputAudio;

IDubber *g_dubber = NULL;

COMPVARS2     g_Vcompression;
VDWaveFormat *g_ACompressionFormat     = NULL;
uint32        g_ACompressionFormatSize = 0;
VDStringA     g_ACompressionFormatHint;
vdblock<char> g_ACompressionConfig;

VDAudioFilterGraph g_audioFilterGraph;

VDStringW g_FileOutDriver;
VDStringA g_FileOutFormat;
VDStringW g_AudioOutDriver;
VDStringA g_AudioOutFormat;

extern VDProject *g_project;


bool g_drawDecompressedFrame = FALSE;
bool g_showStatusWindow      = TRUE;

extern uint32 &VDPreferencesGetRenderOutputBufferSize();
extern bool    VDPreferencesGetRenderBackgroundPriority();

///////////////////////////////////////////////////////////////////////////

void AppendAVI(const wchar_t *pszFile, uint32 flags)
{
  if (inputAVI)
  {
    IVDStreamSource *pVSS  = inputVideo->asStream();
    VDPosition       lTail = pVSS->getEnd();
    VDStringW        filename(g_project->ExpandProjectPath(pszFile));

    if (inputAVI->Append(filename.c_str(), flags))
    {
      inputVideo->streamAppendReinit();
      if (inputAudio)
        inputAudio->streamAppendReinit();
      g_project->BeginTimelineUpdate();
      FrameSubset &s = g_project->GetTimeline().GetSubset();

      s.insert(s.end(), FrameSubsetNode(lTail, pVSS->getEnd() - lTail, false, 0));
      g_project->SetAudioSource();
      g_project->EndTimelineUpdate();
    }
  }
}

int AppendAVIAutoscanEnum(const wchar_t *pszFile)
{
  wchar_t  buf[MAX_PATH];
  wchar_t *s     = buf, *t;
  int      count = 0;

  wcscpy(buf, pszFile);

  t = VDFileSplitExt(VDFileSplitPath(s));

  if (t > buf)
    --t;

  for (;;)
  {
    if (!VDDoesPathExist(buf))
      break;

    ++count;

    s = t;

    for (;;)
    {
      if (s < buf || !isdigit(*s))
      {
        memmove(s + 2, s + 1, sizeof(wchar_t) * wcslen(s));
        s[1] = L'1';
        ++t;
      }
      else
      {
        if (*s == L'9')
        {
          *s-- = L'0';
          continue;
        }
        ++*s;
      }
      break;
    }
  }

  return count;
}

void AppendAVIAutoscan(const wchar_t *pszFile, bool skip_first)
{
  wchar_t   buf[MAX_PATH];
  wchar_t * s     = buf, *t;
  int       count = 0;
  VDStringW last;

  if (!inputAVI)
    return;

  IVDStreamSource *pVSS          = inputVideo->asStream();
  VDPosition       originalCount = pVSS->getEnd();

  wcscpy(buf, pszFile);

  t = VDFileSplitExt(VDFileSplitPath(s));

  if (t > buf)
    --t;

  try
  {
    for (;;)
    {
      if (!VDDoesPathExist(buf))
        break;

      if (!skip_first)
      {
        if (!inputAVI->Append(buf, 0))
          break;

        last = buf;
        inputVideo->streamAppendReinit();
        if (inputAudio)
          inputAudio->streamAppendReinit();
        ++count;
      }

      skip_first = false;
      s          = t;

      for (;;)
      {
        if (s < buf || !isdigit(*s))
        {
          memmove(s + 2, s + 1, sizeof(wchar_t) * wcslen(s));
          s[1] = L'1';
          ++t;
        }
        else
        {
          if (*s == L'9')
          {
            *s-- = L'0';
            continue;
          }
          ++*s;
        }
        break;
      }
    }
  }
  catch (const MyError &e)
  {
    // if the first segment failed, turn the warning into an error
    if (!count)
      throw;

    // log append errors, but otherwise eat them
    VDLog(kVDLogWarning, VDTextAToW(e.gets()));
  }

  guiSetStatus("Appended %d segments (last was \"%s\")", 255, count, VDTextWToA(last).c_str());

  if (count)
  {
    FrameSubset &s = g_project->GetTimeline().GetSubset();
    g_project->BeginTimelineUpdate();
    s.insert(s.end(), FrameSubsetNode(originalCount, pVSS->getEnd() - originalCount, false, 0));
    g_project->SetAudioSource();
    g_project->EndTimelineUpdate();
  }
}

///////////////////////////////////////////////////////////////////////////

void VideoOperation::setPrefs()
{
  iDubPriority       = g_prefs.main.iDubPriority;
  backgroundPriority = VDPreferencesGetRenderBackgroundPriority();
}

///////////////////////////////////////////////////////////////////////////

void SaveWAV(RequestWAV &req)
{
  if (!inputVideo)
    throw MyError("No input file to process.");

  if (!inputAudio)
    throw MyError("No audio stream to process.");

  VDAVIOutputWAVSystem wavout(req.fileOutput.c_str(), req.auto_w64);

  VideoOperation op(&req);
  op.removeVideo = true;
  g_project->RunOperation(&wavout, op);
  // g_project->RunOperation(&wavout, TRUE, req.opt, 0, req.propagateErrors);
}

///////////////////////////////////////////////////////////////////////////

void SavePlugin(RequestVideo &req)
{
  VDAVIOutputPluginSystem fileout(req.fileOutput.c_str());

  fileout.SetDriver(req.driver, req.format);
  fileout.SetTextInfo(g_project->GetTextInfo());
  if (req.removeVideo)
    fileout.fAudioOnly = true;

  VideoOperation op(&req);
  op.removeAudio = req.removeAudio;
  op.removeVideo = req.removeVideo;
  op.setPrefs();
  g_project->RunOperation(&fileout, op);
  /*
  int type = 0;
  if (req.removeVideo){ type = 1; fileout.fAudioOnly = true; }
  if (req.removeAudio) type = 3;

  g_project->RunOperation(&fileout, type, req.opt, g_prefs.main.iDubPriority, req.propagateErrors, 0, 0,
  VDPreferencesGetRenderBackgroundPriority());
  */
}

void SaveAVI(RequestVideo &req)
{
  VDAVIOutputFileSystem fileout;

  fileout.Set1GBLimit(g_prefs.fAVIRestrict1Gb != 0);
  fileout.SetCaching(false);
  fileout.SetIndexing(!req.compat);
  fileout.SetFilename(req.fileOutput.c_str());
  fileout.SetBuffer(VDPreferencesGetRenderOutputBufferSize());
  fileout.SetTextInfo(g_project->GetTextInfo());

  VideoOperation op(&req);
  op.removeAudio = req.removeAudio;
  op.setPrefs();
  g_project->RunOperation(&fileout, op);

  // g_project->RunOperation(&fileout, req.removeAudio ? 3:FALSE, req.opt, g_prefs.main.iDubPriority,
  // req.propagateErrors, 0, 0, VDPreferencesGetRenderBackgroundPriority());
}

void SaveStripedAVI(const wchar_t *szFile)
{
  if (!inputVideo)
    throw MyError("No input video stream to process.");

  VDAVIOutputStripedSystem outstriped(szFile);

  outstriped.Set1GBLimit(g_prefs.fAVIRestrict1Gb != 0);

  VideoOperation op;
  op.setPrefs();
  g_project->RunOperation(&outstriped, op);

  // g_project->RunOperation(&outstriped, FALSE, NULL, g_prefs.main.iDubPriority, false, 0, 0,
  // VDPreferencesGetRenderBackgroundPriority());
}

void SaveStripeMaster(const wchar_t *szFile)
{
  if (!inputVideo)
    throw MyError("No input video stream to process.");

  VDAVIOutputStripedSystem outstriped(szFile);

  outstriped.Set1GBLimit(g_prefs.fAVIRestrict1Gb != 0);

  VideoOperation op;
  op.setPrefs();
  g_project->RunOperation(&outstriped, op);
  // it used setPhantomVideoMode in the past

  // g_project->RunOperation(&outstriped, 2, NULL, g_prefs.main.iDubPriority, false, 0, 0,
  // VDPreferencesGetRenderBackgroundPriority());
}

void SaveSegmentedAVI(RequestSegmentVideo &req)
{
  if (!inputVideo)
    throw MyError("No input file to process.");

  if (req.spillDigits < 1 || req.spillDigits > 10)
    throw MyError("Invalid digit count: %d", req.spillDigits);

  VDAVIOutputFileSystem outfile;

  outfile.SetIndexing(false);
  outfile.SetCaching(false);
  if (req.lSpillThreshold && req.lSpillThreshold > 2048)
    outfile.SetIndexing(true); // required to produce valid file
  outfile.SetBuffer(VDPreferencesGetRenderOutputBufferSize());

  const VDStringW filename(req.fileOutput);
  outfile.SetFilenamePattern(
    VDFileSplitExtLeft(req.fileOutput).c_str(), VDFileSplitExtRight(req.fileOutput).c_str(), req.spillDigits);

  VideoOperation op;
  op.setPrefs();
  op.opt                  = req.opt;
  op.propagateErrors      = req.propagateErrors;
  op.lSegmentCount        = req.lSegmentCount;
  op.lSpillThreshold      = req.lSpillThreshold;
  op.lSpillFrameThreshold = req.lSpillFrameThreshold;
  g_project->RunOperation(&outfile, op);

  // g_project->RunOperation(&outfile, FALSE, quick_opts, g_prefs.main.iDubPriority, fProp, lSpillThreshold,
  // lSpillFrameThreshold, VDPreferencesGetRenderBackgroundPriority());
}

void SaveImageSequence(RequestImages &req)
{
  VDAVIOutputImagesSystem outimages;

  outimages.SetFilenamePattern(req.filePrefix.c_str(), req.fileSuffix.c_str(), req.minDigits, req.startDigit);
  outimages.SetFormat(req.imageFormat, req.quality);

  VideoOperation op(&req);
  op.setPrefs();
  g_project->RunOperation(&outimages, op);

  // g_project->RunOperation(&outimages, FALSE, req.opt, g_prefs.main.iDubPriority, req.propagateErrors, 0, 0,
  // VDPreferencesGetRenderBackgroundPriority());
}

///////////////////////////////////////////////////////////////////////////


void SetSelectionStart(long ms) {}

void SetSelectionEnd(long ms) {}

void ScanForUnreadableFrames(FrameSubset *pSubset, IVDVideoSource *pVideoSource)
{
  IVDStreamSource *pVSS   = pVideoSource->asStream();
  const VDPosition lFirst = pVSS->getStart();
  const VDPosition lLast  = pVSS->getEnd();
  VDPosition       lFrame = lFirst;
  vdblock<char>    buffer;

  IVDStreamSource::ErrorMode oldErrorMode(pVSS->getDecodeErrorMode());
  pVSS->setDecodeErrorMode(IVDStreamSource::kErrorModeReportAll);

  try
  {
    ProgressDialog pd(g_hWnd, "Frame scan", "Scanning for unreadable frames", VDClampToSint32(lLast - lFrame), true);
    bool           bLastValid = true;
    VDPosition     lRangeFirst;
    long           lDeadFrames   = 0;
    long           lMaskedFrames = 0;

    pd.setValueFormat("Frame %d of %d");

    pVideoSource->streamBegin(false, true);

    const uint32 padSize = pVideoSource->streamGetDecodePadding();

    while (lFrame <= lLast)
    {
      uint32 lActualBytes, lActualSamples;
      int    err;
      bool   bValid;

      pd.advance(VDClampToSint32(lFrame - lFirst));
      pd.check();

      do
      {
        bValid = false;

        if (!bLastValid && !pVideoSource->isKey(lFrame))
          break;

        if (lFrame < lLast)
        {
          err = pVSS->read(lFrame, 1, NULL, 0, &lActualBytes, &lActualSamples);

          if (err)
            break;

          if (buffer.empty() || buffer.size() < lActualBytes + padSize)
            buffer.resize(((lActualBytes + !lActualBytes + padSize + 65535) & ~65535));

          err = pVSS->read(lFrame, 1, buffer.data(), buffer.size() - padSize, &lActualBytes, &lActualSamples);

          if (err)
            break;

          pVideoSource->streamFillDecodePadding(buffer.data(), lActualBytes);

          try
          {
            pVideoSource->streamGetFrame(buffer.data(), lActualBytes, FALSE, lFrame, lFrame);
          }
          catch (...)
          {
            ++lDeadFrames;
            break;
          }
        }

        bValid = true;
      } while (false);

      if (!bValid)
        ++lMaskedFrames;

      if (bValid ^ bLastValid)
      {
        if (!bValid)
          lRangeFirst = lFrame;
        else
          pSubset->setRange(lRangeFirst, lFrame - lRangeFirst, true, 0);

        bLastValid = bValid;
      }

      ++lFrame;
    }

    pVSS->streamEnd();

    guiSetStatus(
      "%ld frames masked (%ld frames bad, %ld frames good but undecodable)",
      255,
      lMaskedFrames,
      lDeadFrames,
      lMaskedFrames - lDeadFrames);
  }
  catch (...)
  {
    pVSS->setDecodeErrorMode(oldErrorMode);
    pVideoSource->invalidateFrameBuffer();
    throw;
  }
  pVSS->setDecodeErrorMode(oldErrorMode);
  pVideoSource->invalidateFrameBuffer();

  g_project->DisplayFrame();
}
