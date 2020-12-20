//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2007 Avery Lee
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
#include <windowsx.h>
#include <vd2/system/w32assist.h>
#include <vd2/VDDisplay/display.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/pixel.h>
#include <vd2/VDLib/Dialog.h>
#include "FilterPreview.h"
#include "FilterInstance.h"
#include "FilterFrameRequest.h"
#include "FilterFrameVideoSource.h"
#include "filters.h"
#include "PositionControl.h"
#include "ProgressDialog.h"
#include "project.h"
#include "VBitmap.h"
#include "command.h"
#include "VideoSource.h"
#include "VideoWindow.h"
#include "resource.h"
#include "oshelper.h"
#include "dub.h"
#include "projectui.h"
#include "ClippingControl.h"

extern HINSTANCE                   g_hInst;
extern VDProject *                 g_project;
extern vdrefptr<VDProjectUI>       g_projectui;
extern PanCenteringMode            g_panCentering;
extern IVDPositionControlCallback *VDGetPositionControlCallbackTEMP();
extern void                        SaveImage(HWND, VDPosition frame, VDPixmap *px, bool skip_dialog);

int VDRenderSetVideoSourceInputFormat(IVDVideoSource *vsrc, VDPixmapFormatEx format);

float g_previewZoom = 1.0;

namespace
{
static const UINT IDC_FILTDLG_POSITION = 500;

static const UINT MYWM_REDRAW     = WM_USER + 100;
static const UINT MYWM_RESTART    = WM_USER + 101;
static const UINT MYWM_INVALIDATE = WM_USER + 102;
static const UINT TIMER_SHUTTLE   = 2;
} // namespace

///////////////////////////////////////////////////////////////////////////////

class VDVideoFilterPreviewZoomPopup : public VDDialogFrameW32
{
public:
  VDVideoFilterPreviewZoomPopup();
  ~VDVideoFilterPreviewZoomPopup();

  void Update(int x, int y, const uint32 pixels[7][7], const VDSample &ps);
  void UpdateText();

  VDZINT_PTR DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam);

  IVDVideoDisplayDrawMode *drawMode;

protected:
  bool OnLoaded();
  bool OnEraseBkgnd(HDC hdc);
  void OnPaint();

  RECT mBitmapRect;

  int        x, y;
  VDSample   ps;
  uint32     mBitmap[7][7];
  BITMAPINFO mBitmapInfo;
  HPEN       black_pen;
  HPEN       white_pen;
  bool       draw_delayed;
};

VDVideoFilterPreviewZoomPopup::VDVideoFilterPreviewZoomPopup() : VDDialogFrameW32(IDD_FILTER_PREVIEW_ZOOM)
{
  mBitmapInfo.bmiHeader.biSize          = sizeof(BITMAPINFOHEADER);
  mBitmapInfo.bmiHeader.biWidth         = 7;
  mBitmapInfo.bmiHeader.biHeight        = 7;
  mBitmapInfo.bmiHeader.biPlanes        = 1;
  mBitmapInfo.bmiHeader.biCompression   = BI_RGB;
  mBitmapInfo.bmiHeader.biSizeImage     = sizeof mBitmap;
  mBitmapInfo.bmiHeader.biBitCount      = 32;
  mBitmapInfo.bmiHeader.biXPelsPerMeter = 0;
  mBitmapInfo.bmiHeader.biYPelsPerMeter = 0;
  mBitmapInfo.bmiHeader.biClrUsed       = 0;
  mBitmapInfo.bmiHeader.biClrImportant  = 0;
  black_pen                             = CreatePen(PS_SOLID, 0, RGB(0, 0, 0));
  white_pen                             = CreatePen(PS_SOLID, 0, RGB(255, 255, 255));
  drawMode                              = 0;
}

VDVideoFilterPreviewZoomPopup::~VDVideoFilterPreviewZoomPopup()
{
  DeleteObject(black_pen);
  DeleteObject(white_pen);
}

void VDVideoFilterPreviewZoomPopup::Update(int x, int y, const uint32 pixels[7][7], const VDSample &ps)
{
  memcpy(mBitmap, pixels, sizeof mBitmap);
  this->x  = x;
  this->y  = y;
  this->ps = ps;
  if (!draw_delayed)
  {
    SetTimer(mhdlg, 1, 20, 0);
    draw_delayed = true;
  }
}

int flt255_digits(float v)
{
  int n = 3;
  if (v < 100)
    n = 2;
  if (v < 10)
    n = 1;
  if (v < 1)
    n = 0;
  if (v < 0.1)
    n = -1;
  return n;
}

void VDVideoFilterPreviewZoomPopup::UpdateText()
{
  uint32 rgb = mBitmap[3][3] & 0xffffff;
  SetControlTextF(IDC_POSITION, L"%d,%d", x, y);
  SetControlTextF(IDC_COLOR, L"#%06X", rgb);
  if (ps.r < 0.001)
    ps.r = 0;
  if (ps.g < 0.001)
    ps.g = 0;
  if (ps.b < 0.001)
    ps.b = 0;
  if (ps.a < 0.001)
    ps.a = 0;
  SetControlTextF(IDC_RED, L"R: %1.*g", flt255_digits(ps.r) + 2, ps.r);
  SetControlTextF(IDC_GREEN, L"G: %1.*g", flt255_digits(ps.g) + 2, ps.g);
  SetControlTextF(IDC_BLUE, L"B: %1.*g", flt255_digits(ps.b) + 2, ps.b);
  if (ps.sa != -1)
  {
    ShowControl(IDC_ALPHA, true);
    SetControlTextF(IDC_ALPHA, L"A: %1.*g", flt255_digits(ps.a) + 2, ps.a);
    ShowControl(IDC_ALPHA2, true);
    SetControlTextF(IDC_ALPHA2, L"A: %X", ps.sa);
  }
  else
  {
    ShowControl(IDC_ALPHA, false);
    ShowControl(IDC_ALPHA2, false);
  }
  if (ps.sr != -1)
  {
    ShowControl(IDC_RED2, true);
    SetControlTextF(IDC_RED2, L"R: %X", ps.sr);
  }
  else if (ps.sy != -1)
  {
    ShowControl(IDC_RED2, true);
    SetControlTextF(IDC_RED2, L"Y: %X", ps.sy);
  }
  else
  {
    ShowControl(IDC_RED2, false);
  }
  if (ps.sg != -1)
  {
    ShowControl(IDC_GREEN2, true);
    SetControlTextF(IDC_GREEN2, L"G: %X", ps.sg);
  }
  else if (ps.scb != -1)
  {
    ShowControl(IDC_GREEN2, true);
    SetControlTextF(IDC_GREEN2, L"Cb: %X", ps.scb);
  }
  else
  {
    ShowControl(IDC_GREEN2, false);
  }
  if (ps.sb != -1)
  {
    ShowControl(IDC_BLUE2, true);
    SetControlTextF(IDC_BLUE2, L"B: %X", ps.sb);
  }
  else if (ps.scr != -1)
  {
    ShowControl(IDC_BLUE2, true);
    SetControlTextF(IDC_BLUE2, L"Cr: %X", ps.scr);
  }
  else
  {
    ShowControl(IDC_BLUE2, false);
  }
}

VDZINT_PTR VDVideoFilterPreviewZoomPopup::DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam)
{
  switch (msg)
  {
    case WM_NCHITTEST:
      SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, HTTRANSPARENT);
      return TRUE;

    case WM_PAINT:
      OnPaint();
      return TRUE;

    case WM_TIMER:
      if (wParam == 1)
      {
        KillTimer(mhdlg, wParam);
        draw_delayed = false;
        InvalidateRect(mhdlg, 0, false);
        UpdateText();
      }
      break;

    case WM_ERASEBKGND:
      SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, OnEraseBkgnd((HDC)wParam));
      return TRUE;
  }

  return VDDialogFrameW32::DlgProc(msg, wParam, lParam);
}

bool VDVideoFilterPreviewZoomPopup::OnLoaded()
{
  HWND hwndImage = GetDlgItem(mhdlg, IDC_IMAGE);

  memset(&mBitmapRect, 0, sizeof mBitmapRect);
  draw_delayed = false;

  if (hwndImage)
  {
    GetWindowRect(hwndImage, &mBitmapRect);
    MapWindowPoints(NULL, mhdlg, (LPPOINT)&mBitmapRect, 2);
  }

  VDDialogFrameW32::OnLoaded();
  return true;
}

bool VDVideoFilterPreviewZoomPopup::OnEraseBkgnd(HDC hdc)
{
  RECT r;

  int saveHandle = SaveDC(hdc);
  if (saveHandle)
  {
    ExcludeClipRect(hdc, mBitmapRect.left, mBitmapRect.top, mBitmapRect.right, mBitmapRect.bottom);

    GetClientRect(mhdlg, &r);
    // FillRect(hdc, &r, (HBRUSH)GetClassLongPtr(mhdlg, GCLP_HBRBACKGROUND));
    FillRect(hdc, &r, (HBRUSH)(COLOR_BTNFACE + 1));
    RestoreDC(hdc, saveHandle);
    return true;
  }

  return false;
}

void VDVideoFilterPreviewZoomPopup::OnPaint()
{
  PAINTSTRUCT ps;

  HDC hdc = BeginPaint(mhdlg, &ps);
  if (hdc)
  {
    StretchDIBits(
      hdc,
      mBitmapRect.left,
      mBitmapRect.top,
      mBitmapRect.right - mBitmapRect.left,
      mBitmapRect.bottom - mBitmapRect.top,
      0,
      0,
      7,
      7,
      mBitmap,
      &mBitmapInfo,
      DIB_RGB_COLORS,
      SRCCOPY);

    if (drawMode)
    {
      int saveHandle = SaveDC(hdc);
      IntersectClipRect(hdc, mBitmapRect.left, mBitmapRect.top, mBitmapRect.right, mBitmapRect.bottom);
      drawMode->PaintZoom(
        hdc,
        mBitmapRect.left,
        mBitmapRect.top,
        mBitmapRect.right - mBitmapRect.left,
        mBitmapRect.bottom - mBitmapRect.top,
        x - 3,
        y - 3,
        7,
        7);
      RestoreDC(hdc, saveHandle);
    }

    HPEN   pen = black_pen;
    uint32 px  = mBitmap[3][3] & 0xffffff;
    int    pr  = px >> 16;
    int    pg  = (px >> 8) & 0xff;
    int    pb  = px & 0xff;
    if (pr + pg + pb < 300)
      pen = white_pen;

    HPEN pen0 = (HPEN)SelectObject(hdc, pen);
    int  cx   = (mBitmapRect.right + mBitmapRect.left) / 2;
    int  cy   = (mBitmapRect.bottom + mBitmapRect.top) / 2;

    MoveToEx(hdc, cx - 5, cy, 0);
    LineTo(hdc, cx + 6, cy);
    MoveToEx(hdc, cx, cy - 5, 0);
    LineTo(hdc, cx, cy + 6);

    SelectObject(hdc, pen0);
    EndPaint(mhdlg, &ps);
  }
}

///////////////////////////////////////////////////////////////////////////////

class FilterPreview : public IVDXFilterPreview2,
                      public IFilterModPreview,
                      public vdrefcounted<IVDVideoFilterPreviewDialog>
{
  FilterPreview(const FilterPreview &);
  FilterPreview &operator=(const FilterPreview &);

public:
  FilterPreview(FilterSystem *sys, VDFilterChainDesc *desc, FilterInstance *);
  ~FilterPreview();

  IVDXFilterPreview2 *AsIVDXFilterPreview2()
  {
    return this;
  }
  IFilterModPreview *AsIFilterModPreview()
  {
    return this;
  }
  void SetInitialTime(VDTime t);
  void SetFilterList(HWND w)
  {
    mhwndFilterList = w;
  }
  void RedoFrame2()
  {
    OnVideoRedraw();
  }
  void SetThickBorder();
  void SetClipEdit(ClipEditInfo &info);

  void SetButtonCallback(VDXFilterPreviewButtonCallback, void *);
  void SetSampleCallback(VDXFilterPreviewSampleCallback, void *);

  bool                  isPreviewEnabled();
  bool                  IsPreviewDisplayed();
  void                  InitButton(VDXHWND);
  void                  ClearWindow();
  void                  Toggle(VDXHWND);
  void                  Display(VDXHWND, bool);
  void                  DisplayEx(VDXHWND, PreviewExInfo &info);
  void                  RedoFrame();
  void                  UndoSystem();
  void                  RedoSystem();
  void                  SampleRedoSystem();
  void                  Close();
  const VDPixmapLayout &GetFrameBufferLayout();
  void                  CopyOutputFrameToClipboard();
  void                  SaveImageAsk(bool skip_dialog);
  bool                  SampleCurrentFrame();
  long                  SampleFrames();
  long                  SampleFrames(IFilterModPreviewSample *);
  int64                 FMSetPosition(int64 pos);
  void                  FMSetPositionCallback(FilterModPreviewPositionCallback, void *);
  void                  FMSetZoomCallback(FilterModPreviewZoomCallback, void *);
  void                  SetClipEditCallback(FilterModPreviewClipEditCallback, void *);
  int                   FMTranslateAccelerator(MSG *msg)
  {
    return TranslateAccelerator(mhdlg, mDlgNode.mhAccel, msg);
  }
  HWND GetHwnd()
  {
    return mhdlg;
  }
  int TranslateAcceleratorMessage(MSG *msg)
  {
    return TranslateAccelerator(mhdlg, mDlgNode.mhAccel, msg);
  }
  void StartShuttleReverse(bool sticky);
  void StartShuttleForward(bool sticky);
  void SceneShuttleStop();
  void SceneShuttleStep();
  void MoveToPreviousRange();
  void MoveToNextRange();
  void SetRangeFrames();
  void SetRangeFrames(VDPosition p0, VDPosition p1, bool set_p0, bool set_p1);

private:
  static INT_PTR CALLBACK StaticDlgProc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam);
  BOOL                    DlgProc(UINT message, WPARAM wParam, LPARAM lParam);

  void       OnInit();
  void       OnResize();
  void       OnPaint();
  void       InitFilterSystem();
  VDPosition InitPosition();
  void       OnVideoResize(bool bInitial);
  void       OnVideoRedraw();
  bool       OnCommand(UINT);

  VDPosition FetchFrame();
  VDPosition FetchFrame(VDPosition);

  void UpdateButton();
  void RedrawFrame();
  void ShowZoomMode(int px, int py);
  void ExitZoomMode();

  HWND mhdlg;
  HWND mhwndButton;
  HWND mhwndParent;
  HWND mhwndPosHost;
  HWND mhwndPosition;
  HWND mhwndVideoWindow;
  HWND mhwndDisplay;
  HWND mhwndFilterList;
  HWND mhwndFilter;

  wchar_t mButtonAccelerator;

  int  mWidth;
  int  mHeight;
  int  mDisplayX;
  int  mDisplayY;
  int  mDisplayW;
  int  mDisplayH;
  RECT mWorkArea;
  int  mBorder;
  bool mbDisplaySource;
  bool mbShowOverlay;
  bool mbNoExit;

  VDTime mInitialTimeUS;
  sint64 mInitialFrame;
  sint64 mLastOutputFrame;
  sint64 mLastTimelineFrame;
  sint64 mLastTimelineTimeMS;
  int    mShuttleMode;
  bool   mStickyShuttle;

  IVDPositionControl *      mpPosition;
  IVDVideoDisplay *         mpDisplay;
  IVDVideoWindow *          mpVideoWindow;
  VDClippingControlOverlay *pOverlay;
  FilterSystem *            mpFiltSys;
  VDFilterChainDesc *       mpFilterChainDesc;
  FilterInstance *          mpThisFilter;
  VDTimeline *              mpTimeline;
  VDFraction                mTimelineRate;

  VDXFilterPreviewButtonCallback   mpButtonCallback;
  void *                           mpvButtonCBData;
  VDXFilterPreviewSampleCallback   mpSampleCallback;
  void *                           mpvSampleCBData;
  FilterModPreviewPositionCallback mpPositionCallback;
  void *                           mpvPositionCBData;
  FilterModPreviewZoomCallback     mpZoomCallback;
  void *                           mpvZoomCBData;
  FilterModPreviewClipEditCallback mpClipEditCallback;
  void *                           mpClipEditCBData;
  PreviewZoomInfo                  zoom_info;

  MyError mFailureReason;

  VDVideoFilterPreviewZoomPopup mZoomPopup;
  HCURSOR                       mode_cursor;
  HCURSOR                       cross_cursor;

  vdrefptr<VDFilterFrameVideoSource> mpVideoFrameSource;
  vdrefptr<VDFilterFrameBuffer>      mpVideoFrameBuffer;

  ModelessDlgNode mDlgNode;
};

bool VDCreateVideoFilterPreviewDialog(
  FilterSystem *                sys,
  VDFilterChainDesc *           desc,
  FilterInstance *              finst,
  IVDVideoFilterPreviewDialog **pp)
{
  IVDVideoFilterPreviewDialog *p = new_nothrow FilterPreview(sys, desc, finst);
  if (!p)
    return false;
  p->AddRef();
  *pp = p;
  return true;
}

FilterPreview::FilterPreview(FilterSystem *pFiltSys, VDFilterChainDesc *pFilterChainDesc, FilterInstance *pfiThisFilter)
  : mhdlg(NULL), mhwndButton(NULL), mhwndParent(NULL), mhwndPosHost(NULL), mhwndPosition(NULL), mhwndVideoWindow(NULL),
    mhwndDisplay(NULL), mhwndFilterList(NULL), mhwndFilter(NULL), mButtonAccelerator(0), mWidth(0), mHeight(0),
    mDisplayX(0), mDisplayY(0), mDisplayW(0), mDisplayH(0), mInitialTimeUS(-1), mInitialFrame(-1), mLastOutputFrame(0),
    mLastTimelineFrame(0), mLastTimelineTimeMS(0), mShuttleMode(0), mStickyShuttle(false), mpPosition(NULL),
    mpDisplay(NULL), mpVideoWindow(NULL), pOverlay(NULL), mpFiltSys(pFiltSys), mpFilterChainDesc(pFilterChainDesc),
    mpThisFilter(pfiThisFilter), mpTimeline(0), mpButtonCallback(NULL), mpSampleCallback(NULL),
    mpPositionCallback(NULL), mpZoomCallback(NULL), mpClipEditCallback(NULL)
{
  mode_cursor      = 0;
  cross_cursor     = LoadCursor(0, IDC_CROSS);
  mWorkArea.left   = 0;
  mWorkArea.top    = 0;
  mWorkArea.right  = 0;
  mWorkArea.bottom = 0;
  mBorder          = 0;
  mbDisplaySource  = false;
  mbShowOverlay    = false;
  mbNoExit         = false;
}

FilterPreview::~FilterPreview()
{
  if (mhdlg)
    DestroyWindow(mhdlg);
}

void FilterPreview::SetInitialTime(VDTime t)
{
  mInitialTimeUS = t;
  mInitialFrame  = -1;
}

void FilterPreview::ExitZoomMode()
{
  if (pOverlay && mbShowOverlay)
    ShowWindow(pOverlay->GetHwnd(), true);
  if (mZoomPopup.IsCreated())
  {
    mZoomPopup.Destroy();
    mode_cursor = 0;
    if (mpZoomCallback)
    {
      zoom_info.flags = zoom_info.popup_cancel;
      mpZoomCallback(zoom_info, mpvZoomCBData);
    }
  }
}

void FilterPreview::ShowZoomMode(int px, int py)
{
  int xoffset = px - mDisplayX - mBorder;
  int yoffset = py - mDisplayY - mBorder;

  int DisplayX, DisplayY;
  int DisplayW, DisplayH;
  mpVideoWindow->GetDisplayRect(DisplayX, DisplayY, DisplayW, DisplayH);
  xoffset -= DisplayX;
  yoffset -= DisplayY;

  if (
    mpFiltSys->isRunning() && mpVideoFrameBuffer && xoffset >= -10 && yoffset >= -10 && xoffset < DisplayW + 10 &&
    yoffset < DisplayH + 10)
  {
    VDPixmap output = VDPixmapFromLayout(GetFrameBufferLayout(), (void *)mpVideoFrameBuffer->LockRead());
    output.info     = mpVideoFrameBuffer->info;
    int x           = VDFloorToInt((xoffset + 0.5) * (double)output.w / (double)DisplayW);
    int y           = VDFloorToInt((yoffset + 0.5) * (double)output.h / (double)DisplayH);
    if (x < 0)
      x = 0;
    if (y < 0)
      y = 0;
    if (x >= output.w)
      x = output.w - 1;
    if (y >= output.h)
      y = output.h - 1;

    int bg = VDSwizzleU32(GetSysColor(COLOR_BTNFACE)) >> 8;

    uint32 pixels[7][7];
    for (int i = 0; i < 7; ++i)
    {
      for (int j = 0; j < 7; ++j)
      {
        int x1 = x + j - 3;
        int y1 = y + 3 - i;
        if (x1 < 0 || y1 < 0 || x1 >= output.w || y1 >= output.h)
          pixels[i][j] = bg;
        else
          pixels[i][j] = 0xFFFFFF & VDPixmapSample(output, x1, y1);
      }
    }

    VDSample ps;
    VDPixmapSample(output, x, y, ps);

    mpVideoFrameBuffer->Unlock();

    zoom_info.x = x;
    zoom_info.y = y;
    zoom_info.r = ps.r / 255;
    zoom_info.g = ps.g / 255;
    zoom_info.b = ps.b / 255;
    zoom_info.a = ps.a / 255;

    POINT pts = {px, py};
    ClientToScreen(mhdlg, &pts);
    mZoomPopup.Create((VDGUIHandle)mhdlg);
    HMONITOR    monitor = MonitorFromPoint(pts, MONITOR_DEFAULTTONEAREST);
    MONITORINFO minfo   = {sizeof(MONITORINFO)};
    GetMonitorInfo(monitor, &minfo);
    RECT r0;
    GetWindowRect(mZoomPopup.GetWindowHandle(), &r0);
    int zw = r0.right - r0.left;
    int zh = r0.bottom - r0.top;
    if (pts.x + 32 + zw > minfo.rcWork.right)
      pts.x -= 32 + zw;
    else
      pts.x += 32;
    if (pts.y + 32 + zh > minfo.rcWork.bottom)
      pts.y -= 32 + zh;
    else
      pts.y += 32;

    if (pOverlay)
      ShowWindow(pOverlay->GetHwnd(), false);
    mZoomPopup.drawMode = mbShowOverlay ? pOverlay : 0;
    SetWindowPos(mZoomPopup.GetWindowHandle(), NULL, pts.x, pts.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    mZoomPopup.Update(x, y, pixels, ps);
    ShowWindow(mZoomPopup.GetWindowHandle(), SW_SHOWNOACTIVATE);

    TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, mhdlg, 0};
    TrackMouseEvent(&tme);
    mode_cursor = cross_cursor;

    if (mpZoomCallback)
    {
      zoom_info.flags = zoom_info.popup_update;
      mpZoomCallback(zoom_info, mpvZoomCBData);
    }
  }
  else
  {
    ExitZoomMode();
  }
}

INT_PTR CALLBACK FilterPreview::StaticDlgProc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
  FilterPreview *fpd = (FilterPreview *)GetWindowLongPtr(hdlg, DWLP_USER);

  if (message == WM_INITDIALOG)
  {
    SetWindowLongPtr(hdlg, DWLP_USER, lParam);
    fpd        = (FilterPreview *)lParam;
    fpd->mhdlg = hdlg;
  }

  return fpd && fpd->DlgProc(message, wParam, lParam);
}

BOOL FilterPreview::DlgProc(UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message)
  {
    case WM_INITDIALOG:
      OnInit();
      OnVideoResize(true);
      VDSetDialogDefaultIcons(mhdlg);
      return TRUE;

    case WM_DESTROY:
      if (mpVideoWindow)
      {
        if (mpVideoWindow->GetAutoSize())
          g_previewZoom = 0;
        else
          g_previewZoom = mpVideoWindow->GetZoom();
      }

      if (mpDisplay)
      {
        mpDisplay->Destroy();
        mpDisplay    = NULL;
        mhwndDisplay = NULL;
      }

      mhwndVideoWindow = NULL;
      mpVideoWindow    = 0;
      pOverlay         = 0;

      mDlgNode.Remove();

      DestroyWindow(mhwndPosHost);
      mhwndPosHost  = NULL;
      mhwndPosition = NULL;
      mpPosition    = NULL;

      g_projectui->DisplayPreview(false);
      return TRUE;

    case WM_GETMINMAXINFO: {
      MINMAXINFO &mmi = *(MINMAXINFO *)lParam;
      DefWindowProc(mhdlg, message, wParam, lParam);

      if (mWorkArea.right)
      {
        RECT rParent;
        GetWindowRect(mhwndParent, &rParent);
        int init_x          = rParent.right + 16;
        mmi.ptMaxPosition.x = init_x;
        mmi.ptMaxPosition.y = mWorkArea.top;
        mmi.ptMaxSize.x     = mWorkArea.right - init_x;
        mmi.ptMaxSize.y     = mWorkArea.bottom - mWorkArea.top;
      }
    }
      return 0;

    case WM_WINDOWPOSCHANGING: {
      if (mWorkArea.right && mpVideoWindow)
      {
        WINDOWPOS *pwp = ((WINDOWPOS *)lParam);
        if ((pwp->flags & SWP_NOMOVE) && (pwp->flags & SWP_NOSIZE))
          break;
        POINT p0 = {0, 0};
        if (pwp->flags & SWP_NOMOVE)
        {
          RECT r;
          GetWindowRect(mhdlg, &r);
          p0.x = r.left;
          p0.y = r.top;
        }
        else
        {
          p0.x = pwp->x;
          p0.y = pwp->y;
        }
        if (pwp->flags & SWP_NOSIZE)
        {
          RECT r;
          GetWindowRect(mhdlg, &r);
          pwp->cx = r.right - r.left;
          pwp->cy = r.bottom - r.top;
        }
        int maxw = mWorkArea.right - p0.x;
        int maxh = mWorkArea.bottom - p0.y;
        if (pwp->cx > maxw)
        {
          pwp->cx = maxw;
          pwp->flags &= ~SWP_NOSIZE;
        }
        if (pwp->cy > maxh)
        {
          pwp->cy = maxh;
          pwp->flags &= ~SWP_NOSIZE;
        }

        RECT r1 = {0, 0, maxw, maxh};
        AdjustWindowRectEx(&r1, GetWindowLong(mhdlg, GWL_STYLE), FALSE, GetWindowLong(mhdlg, GWL_EXSTYLE));
        RECT r2 = {0, 0, maxw * 2 - r1.right + r1.left, maxh * 2 - r1.bottom + r1.top};
        mpVideoWindow->SetWorkArea(r2);
        return TRUE;
      }
    }
    break;

    case WM_EXITSIZEMOVE:
      mpVideoWindow->SetAutoSize(false);
      return TRUE;

    case WM_SIZE:
      OnResize();
      return TRUE;

    case WM_PAINT:
      OnPaint();
      return TRUE;

    case WM_LBUTTONDOWN:
      if (mZoomPopup.IsCreated())
      {
        if (mpZoomCallback)
        {
          zoom_info.flags = zoom_info.popup_click;
          mpZoomCallback(zoom_info, mpvZoomCBData);
        }
        return TRUE;
      }
      else
      {
        return SendMessage(mhwndVideoWindow, message, wParam, lParam);
      }
      break;

    case WM_MOUSEMOVE: {
      int px = GET_X_LPARAM(lParam);
      int py = GET_Y_LPARAM(lParam);
      if (wParam & MK_SHIFT)
        ShowZoomMode(px, py);
      else
        ExitZoomMode();
    }
      return 0;

    case WM_MOUSEWHEEL:
      if (mhwndPosition)
        return SendMessage(mhwndPosition, WM_MOUSEWHEEL, wParam, lParam);
      break;

    case WM_MOUSELEAVE:
      ExitZoomMode();
      break;

    case WM_KEYDOWN:
      if (wParam == VK_SHIFT)
      {
        POINT pt;
        GetCursorPos(&pt);
        MapWindowPoints(0, mhdlg, &pt, 1);
        ShowZoomMode(pt.x, pt.y);
      }
      break;

    case WM_KEYUP:
      if (wParam == VK_SHIFT)
        ExitZoomMode();
      if (mShuttleMode && !mStickyShuttle)
        SceneShuttleStop();
      break;

    case WM_SETCURSOR:
      if (mode_cursor)
      {
        SetCursor(mode_cursor);
        return true;
      }
      break;

    case WM_NOTIFY: {
      const NMHDR &hdr = *(const NMHDR *)lParam;
      if (hdr.idFrom == IDC_FILTDLG_POSITION)
      {
        OnVideoRedraw();
      }
      else if (hdr.hwndFrom == mhwndVideoWindow)
      {
        switch (hdr.code)
        {
          case VWN_REPOSITION: {
            RECT r;
            GetWindowRect(mhdlg, &r);
            RECT r1 = {0, 0, 0, 0};
            AdjustWindowRectEx(&r1, GetWindowLong(mhdlg, GWL_STYLE), FALSE, GetWindowLong(mhdlg, GWL_EXSTYLE));
            int w = mWorkArea.right - r.left - (r1.right - r1.left);
            int h = mWorkArea.bottom - r.top - (r1.bottom - r1.top);
            mpVideoWindow->SetZoom(mpVideoWindow->GetMaxZoomForArea(w, h), false);
          }
          break;

          case VWN_RESIZED: {
            RECT r;
            GetWindowRect(mhwndVideoWindow, &r);

            AdjustWindowRectEx(&r, GetWindowLong(mhdlg, GWL_STYLE), FALSE, GetWindowLong(mhdlg, GWL_EXSTYLE));
            SetWindowPos(
              mhdlg, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
          }
          break;
        }
      }
    }
      return TRUE;

    case WM_COMMAND:
      if (LOWORD(wParam) == IDC_FILTDLG_POSITION)
      {
        VDTranslatePositionCommand(mhdlg, wParam, lParam);
        return TRUE;
      }
      return OnCommand(LOWORD(wParam));

    case WM_CONTEXTMENU: {
      POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
      RECT  r;

      if (::GetWindowRect(mhwndVideoWindow, &r) && ::PtInRect(&r, pt))
      {
        SendMessage(mhwndVideoWindow, WM_CONTEXTMENU, wParam, lParam);
      }
    }
    break;

    case CCM_SETCLIPBOUNDS: {
      ClippingControlBounds *ccb = (ClippingControlBounds *)lParam;
      if (mpClipEditCallback)
      {
        ClipEditInfo info;
        info.flags = ccb->state == 1 ? info.edit_finish : info.edit_update;
        info.x1    = ccb->x1;
        info.y1    = ccb->y1;
        info.x2    = ccb->x2;
        info.y2    = ccb->y2;
        mpClipEditCallback(info, mpClipEditCBData);
      }
    }
      return 0;

    case MYWM_REDRAW:
      OnVideoRedraw();
      return TRUE;

    case MYWM_RESTART:
      OnVideoResize(false);
      return TRUE;

    case MYWM_INVALIDATE:
      mpFiltSys->InvalidateCachedFrames(mpThisFilter);
      OnVideoRedraw();
      return TRUE;

    case WM_TIMER:
      if (wParam == TIMER_SHUTTLE && mShuttleMode)
        SceneShuttleStep();
      return TRUE;
  }

  return FALSE;
}

LRESULT WINAPI preview_pos_host_proc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
  if (msg == WM_NCCREATE || msg == WM_CREATE)
  {
    CREATESTRUCT *create = (CREATESTRUCT *)lparam;
    SetWindowLongPtr(wnd, GWLP_USERDATA, (LPARAM)create->lpCreateParams);
  }

  FilterPreview *owner = (FilterPreview *)GetWindowLongPtr(wnd, GWLP_USERDATA);

  switch (msg)
  {
    case WM_NOTIFY:
    case WM_COMMAND:
      return SendMessage(owner->GetHwnd(), msg, wparam, lparam);

    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR: {
      // currently this is not used because active window is forced to preview anyway
      MSG m     = {0};
      m.hwnd    = owner->GetHwnd();
      m.message = msg;
      m.wParam  = wparam;
      m.lParam  = lparam;
      m.time    = GetMessageTime();
      return owner->TranslateAcceleratorMessage(&m) != 0;
    }

    case WM_MOUSEACTIVATE:
      SetActiveWindow(owner->GetHwnd());
      return MA_NOACTIVATE;

    case WM_ACTIVATE:
      if (LOWORD(wparam) == WA_ACTIVE || LOWORD(wparam) == WA_CLICKACTIVE && lparam)
        SetActiveWindow((HWND)lparam);
      return 0;
  }

  return DefWindowProcW(wnd, msg, wparam, lparam);
}

void FilterPreview::OnInit()
{
  mpTimeline    = &g_project->GetTimeline();
  mTimelineRate = g_project->GetTimelineFrameRate();

  mWidth  = 0;
  mHeight = 0;

  int       host_style_ex = WS_EX_NOACTIVATE;
  int       host_style    = WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE;
  WNDCLASSW cls           = {CS_OWNDC, NULL, 0, 0, GetModuleHandleW(0), 0, 0, 0, 0, L"preview_pos_host"};
  cls.lpfnWndProc         = preview_pos_host_proc;
  RegisterClassW(&cls);
  mhwndPosHost =
    CreateWindowExW(host_style_ex, cls.lpszClassName, 0, host_style, 0, 0, 0, 0, mhwndParent, 0, cls.hInstance, this);

  mhwndPosition = CreateWindow(
    POSITIONCONTROLCLASS,
    NULL,
    WS_CHILD | WS_VISIBLE | PCS_FILTER,
    0,
    0,
    0,
    64,
    mhwndPosHost,
    (HMENU)IDC_FILTDLG_POSITION,
    g_hInst,
    NULL);
  mpPosition = VDGetIPositionControl((VDGUIHandle)mhwndPosition);
  int  pos_h = mpPosition->GetNiceHeight();
  RECT r1;
  GetClientRect(g_projectui->GetHwnd(), &r1);
  SetWindowPos(mhwndPosition, NULL, 0, 0, r1.right, pos_h, SWP_NOZORDER | SWP_NOACTIVATE);
  POINT p1 = {0, r1.bottom - pos_h};
  MapWindowPoints(g_projectui->GetHwnd(), 0, &p1, 1);
  SetWindowPos(mhwndPosHost, 0, p1.x, p1.y, r1.right, pos_h, SWP_NOACTIVATE);
  EnableWindow(mhwndPosHost, true);

  if (mbDisplaySource)
  {
    const VDFilterStreamDesc srcDesc(mpThisFilter->GetSourceDesc());
    mpPosition->SetRange(0, srcDesc.mFrameCount < 0 ? 1000 : srcDesc.mFrameCount);
  }
  else
  {
    mpPosition->SetRange(0, mpTimeline->GetLength());
  }
  mpPosition->SetFrameTypeCallback(VDGetPositionControlCallbackTEMP());

  VDRenderSetVideoSourceInputFormat(inputVideo, g_dubOpts.video.mInputFormat);
  inputVideo->streamRestart();

  mhwndVideoWindow = CreateWindow(
    VIDEOWINDOWCLASS, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, mhdlg, (HMENU)100, g_hInst, NULL);
  mhwndDisplay = (HWND)VDCreateDisplayWindowW32(
    0, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, (VDGUIHandle)mhwndVideoWindow);
  if (mhwndDisplay)
    mpDisplay = VDGetIVideoDisplay((VDGUIHandle)mhwndDisplay);
  EnableWindow(mhwndDisplay, FALSE);

  mpVideoWindow = VDGetIVideoWindow(mhwndVideoWindow);
  mpVideoWindow->SetPanCentering(g_panCentering);
  mpVideoWindow->SetChild(mhwndDisplay);
  mpVideoWindow->SetDisplay(mpDisplay);
  mpVideoWindow->SetMouseTransparent(true);
  mpVideoWindow->SetBorder(mBorder);
  mpVideoWindow->InitSourcePAR();
  if (g_previewZoom == 0)
  {
    mpVideoWindow->SetAutoSize(true);
  }
  else
  {
    mpVideoWindow->SetZoom(g_previewZoom, false);
  }

  if (mBorder)
  {
    mbShowOverlay = true;
    pOverlay      = VDClippingControlOverlay::Create(mhdlg, 0, 0, 0, 0, 0);
    mpDisplay->SetDrawMode(pOverlay);
    pOverlay->fillBorder  = false;
    pOverlay->drawFrame   = false;
    pOverlay->hwndDisplay = mhwndDisplay;
    pOverlay->pVD         = mpDisplay;
    mpVideoWindow->SetDrawMode(pOverlay);
  }

  mDlgNode.hdlg    = mhdlg;
  mDlgNode.mhAccel = g_projectui->GetAccelPreview();
  mDlgNode.hook    = true;
  guiAddModelessDialog(&mDlgNode);
}

void FilterPreview::DisplayEx(VDXHWND parent, PreviewExInfo &info)
{
  mhwndFilter = (HWND)parent;
  mBorder     = 0;
  if (info.flags & info.thick_border)
    mBorder = 8;
  mbDisplaySource = false;
  if (info.flags & info.display_source)
    mbDisplaySource = true;

  if (mpVideoWindow)
    mpVideoWindow->SetBorder(mBorder);

  Display(parent, true);

  mbNoExit = (info.flags & info.no_exit) != 0;
  if (mbNoExit)
    EnableMenuItem(GetSystemMenu(mhdlg, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
  else
    EnableMenuItem(GetSystemMenu(mhdlg, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_ENABLED);
}

void FilterPreview::SetClipEdit(ClipEditInfo &info)
{
  if (pOverlay)
  {
    pOverlay->SetBounds(info.x1, info.y1, info.x2, info.y2);
    pOverlay->fillBorder = (info.flags & info.fill_border) != 0;
  }
}

void FilterPreview::OnResize()
{
  RECT r;

  GetClientRect(mhdlg, &r);

  mDisplayX = 0;
  mDisplayY = 0;
  mDisplayW = r.right;
  mDisplayH = r.bottom;

  if (mDisplayW < 0)
    mDisplayW = 0;

  if (mDisplayH < 0)
    mDisplayH = 0;

  // SetWindowPos(mhwndPosition, NULL, 0, r.bottom - 64, r.right, 64, SWP_NOZORDER|SWP_NOACTIVATE);
  SetWindowPos(mhwndVideoWindow, NULL, mDisplayX, mDisplayY, mDisplayW, mDisplayH, SWP_NOZORDER | SWP_NOACTIVATE);

  InvalidateRect(mhdlg, NULL, TRUE);
}

void FilterPreview::OnPaint()
{
  PAINTSTRUCT ps;

  HDC hdc = BeginPaint(mhdlg, &ps);

  if (!hdc)
    return;

  if (mpFiltSys->isRunning())
  {
    RECT r;

    GetClientRect(mhdlg, &r);
  }
  else
  {
    RECT r;

    GetWindowRect(mhwndDisplay, &r);
    MapWindowPoints(NULL, mhdlg, (LPPOINT)&r, 2);

    FillRect(hdc, &r, (HBRUSH)(COLOR_3DFACE + 1));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, 0);

    HGDIOBJ     hgoFont = SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
    char        buf[1024];
    const char *s = mFailureReason.gets();
    _snprintf(buf, sizeof buf, "Unable to start filters:\n%s", s ? s : "(unknown)");
    buf[1023] = 0;

    RECT r2 = r;
    DrawText(hdc, buf, -1, &r2, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);

    int text_h  = r2.bottom - r2.top;
    int space_h = r.bottom - r.top;
    if (text_h < space_h)
      r.top += (space_h - text_h) >> 1;

    DrawText(hdc, buf, -1, &r, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdc, hgoFont);
  }

  EndPaint(mhdlg, &ps);
}

void FilterPreview::SetRangeFrames()
{
  VDPosition r0, r1;
  mpThisFilter->GetRangeFrames(r0, r1);
  if (mpPosition)
    mpPosition->SetSelection2(r0, r1);
}

void FilterPreview::SetRangeFrames(VDPosition p0, VDPosition p1, bool set_p0, bool set_p1)
{
  VDPosition r0, r1;
  mpThisFilter->GetRangeFrames(r0, r1);
  if ((r1 == -1) || (!set_p0 && !set_p1))
  {
    r0                   = 0;
    r1                   = g_project->GetTimeline().GetLength();
    VDPosition sel_start = g_project->GetSelectionStartFrame();
    VDPosition sel_end   = g_project->GetSelectionEndFrame();
    if (sel_end > sel_start)
    {
      r0 = sel_start;
      r1 = sel_end;
    }
  }
  if (set_p0)
  {
    r0 = p0;
    if (r0 > r1)
      r1 = r0;
  }
  if (set_p1)
  {
    r1 = p1;
    if (r1 < r0)
      r0 = r1;
  }
  mpThisFilter->SetRangeFrames(r0, r1);
  mpPosition->SetSelection2(r0, r1);
  if (mpClipEditCallback)
  {
    ClipEditInfo info;
    info.flags = info.edit_time_range;
    mpClipEditCallback(info, mpClipEditCBData);
  }

  RedoFrame();
}

void FilterPreview::InitFilterSystem()
{
  mpTimeline    = &g_project->GetTimeline();
  mTimelineRate = g_project->GetTimelineFrameRate();

  IVDStreamSource *pVSS    = inputVideo->asStream();
  const VDPixmap & px      = inputVideo->getTargetFormat();
  VDFraction       srcRate = pVSS->getRate();

  if (g_dubOpts.video.mFrameRateAdjustLo > 0)
    srcRate.Assign(g_dubOpts.video.mFrameRateAdjustHi, g_dubOpts.video.mFrameRateAdjustLo);

  sint64            len    = pVSS->getLength();
  const VDFraction &srcPAR = inputVideo->getPixelAspectRatio();

  /*mpFiltSys->prepareLinearChain(
          mpFilterChainDesc,
          px.w,
          px.h,
          pxsrc,
          srcRate,
          pVSS->getLength(),
          srcPAR);*/

  mpVideoFrameSource = new VDFilterFrameVideoSource;
  mpVideoFrameSource->Init(inputVideo, mpFiltSys->GetInputLayout());

  mpFiltSys->initLinearChain(
    NULL,
    VDXFilterStateInfo::kStatePreview,
    mpFilterChainDesc,
    mpVideoFrameSource,
    px.w,
    px.h,
    px,
    px.palette,
    srcRate,
    len,
    srcPAR);

  mpFiltSys->ReadyFilters();
}

VDPosition FilterPreview::InitPosition()
{
  if (mInitialTimeUS >= 0)
  {
    if (mbDisplaySource)
    {
      if (mpFiltSys->isRunning())
      {
        const VDFraction &dstfr         = mpFiltSys->GetOutputFrameRate();
        VDPosition        timelineFrame = VDRoundToInt64(dstfr.asDouble() * (double)mInitialTimeUS * (1.0 / 1000000.0));

        IVDFilterFrameSource *src = mpThisFilter->GetSource(0);
        if (src)
        {
          VDPosition localFrame = mpFiltSys->GetSymbolicFrame(timelineFrame, src);

          if (localFrame >= 0)
            return localFrame;
        }
      }
    }
    else
    {
      const VDFraction outputRate(mpFiltSys->GetOutputFrameRate());
      VDPosition       frame = VDRoundToInt64(outputRate.asDouble() * (double)mInitialTimeUS * (1.0 / 1000000.0));
      return frame;
    }
  }
  else if (mInitialFrame >= 0)
  {
    return mInitialFrame;
  }

  return -1;
}

void FilterPreview::OnVideoResize(bool bInitial)
{
  if (bInitial)
  {
    RECT rMain;
    GetWindowRect(g_projectui->GetHwnd(), &rMain);
    POINT pp = {0, 0};
    MapWindowPoints(mhwndPosHost, 0, &pp, 1);

    int x0 = rMain.left;
    int y0 = rMain.top;
    int x1 = rMain.right;
    int y1 = pp.y;

    MONITORINFO info = {sizeof(info)};
    GetMonitorInfo(MonitorFromWindow(mhwndParent, MONITOR_DEFAULTTONEAREST), &info);
    if (x0 < info.rcMonitor.left)
      x0 = info.rcMonitor.left;
    if (y0 < info.rcMonitor.top)
      y0 = info.rcMonitor.top;
    if (x1 > info.rcMonitor.right)
      x1 = info.rcMonitor.right;
    if (y1 > info.rcMonitor.bottom)
      y1 = info.rcMonitor.bottom;
    mWorkArea.left   = x0;
    mWorkArea.top    = y0;
    mWorkArea.right  = x1;
    mWorkArea.bottom = y1;

    RECT rParent;
    GetWindowRect(mhwndParent, &rParent);
    int init_x = rParent.right + 16;
    int init_y = mWorkArea.top;
    SetWindowPos(mhdlg, 0, init_x, init_y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(mhwndVideoWindow, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    WINDOWPLACEMENT wpos = {sizeof(wpos)};
    GetWindowPlacement(mhdlg, &wpos);
    RECT r1 = {0, 0, 0, 0};
    AdjustWindowRectEx(&r1, GetWindowLong(mhdlg, GWL_STYLE), FALSE, GetWindowLong(mhdlg, GWL_EXSTYLE));

    wpos.ptMinPosition.x = mWorkArea.left + 16;
    wpos.ptMinPosition.y = mWorkArea.bottom - (r1.bottom - r1.top);
    wpos.flags |= WPF_SETMINPOSITION;
    SetWindowPlacement(mhdlg, &wpos);
  }

  int w = 320;
  int h = 240;

  int oldw = mWidth;
  int oldh = mHeight;

  mWidth  = 320;
  mHeight = 240;

  try
  {
    InitFilterSystem();

    const VDPixmapLayout &output = GetFrameBufferLayout();
    w                            = output.w;
    h                            = output.h;
    mWidth                       = w;
    mHeight                      = h;

    mpVideoWindow->SetSourceSize(w, h);
    mpVideoWindow->SetSourcePAR(mpFiltSys->GetOutputPixelAspect());
    if (pOverlay)
      pOverlay->SetSourceSize(w, h);
    if (mpClipEditCallback)
    {
      ClipEditInfo info;
      info.flags = info.init_size;
      info.w     = w;
      info.h     = h;
      mpClipEditCallback(info, mpClipEditCBData);
    }

    if (mbDisplaySource)
    {
      const VDFilterStreamDesc srcDesc(mpThisFilter->GetSourceDesc());
      mpPosition->SetRange(0, srcDesc.mFrameCount < 0 ? 1000 : srcDesc.mFrameCount);
    }
    else
    {
      if (mpFiltSys->GetOutputFrameRate() == mpFiltSys->GetInputFrameRate())
      {
        VDPosition start, end;
        if (g_project->GetZoomRange(start, end))
          mpPosition->SetRange(start, end);
        else
          mpPosition->SetRange(0, mpTimeline->GetLength());
      }
      else
        mpPosition->SetRange(0, mpFiltSys->GetOutputFrameCount());

      VDPosition sel_start = g_project->GetSelectionStartFrame();
      VDPosition sel_end   = g_project->GetSelectionEndFrame();
      mpPosition->SetSelection(sel_start, sel_end);
      mpPosition->SetTimeline(*mpTimeline);
      SetRangeFrames();
    }

    VDPosition frame = InitPosition();
    if (frame != -1)
      mpPosition->SetPosition(frame);
  }
  catch (const MyError &e)
  {
    mpDisplay->Reset();
    ShowWindow(mhwndVideoWindow, SW_HIDE);
    mFailureReason.assign(e);
    InvalidateRect(mhdlg, NULL, TRUE);
  }

  bool fResize = oldw != w || oldh != h;
  if (fResize)
    mpVideoWindow->Resize();
  if (mpVideoWindow->GetAutoSize())
    SendMessage(mhwndVideoWindow, WM_COMMAND, ID_DISPLAY_ZOOM_AUTOSIZE, 0);

  OnVideoRedraw();
}

void FilterPreview::OnVideoRedraw()
{
  if (!mpFiltSys->isRunning())
    return;

  try
  {
    bool success = false;

    FetchFrame();

    if (mpVideoFrameBuffer)
    {
      mpVideoFrameBuffer->Unlock();
      mpVideoFrameBuffer = NULL;
    }

    for (VDFilterChainDesc::Entries::const_iterator it(mpFilterChainDesc->mEntries.begin()),
         itEnd(mpFilterChainDesc->mEntries.end());
         it != itEnd;
         ++it)
    {
      VDFilterChainEntry *ent = *it;
      FilterInstance *    fa  = ent->mpInstance;
      if (!fa->IsEnabled())
        continue;

      fa->view = ent->mpView;
    }

    vdrefptr<IVDFilterFrameClientRequest> req;
    bool                                  have_request;
    if (mbDisplaySource)
    {
      IVDFilterFrameSource *src = mpThisFilter->GetSource(0);
      have_request              = src->CreateRequest(mLastOutputFrame, false, 0, ~req);
    }
    else
    {
      have_request = mpFiltSys->RequestFrame(mLastOutputFrame, 0, ~req);
    }

    if (have_request)
    {
      while (!req->IsCompleted())
      {
        if (mpFiltSys->Run(NULL, false) == FilterSystem::kRunResult_Running)
          continue;

        if (req->IsCompleted())
          break;

        switch (mpVideoFrameSource->RunRequests(NULL, 0))
        {
          case IVDFilterFrameSource::kRunResult_Running:
          case IVDFilterFrameSource::kRunResult_IdleWasActive:
          case IVDFilterFrameSource::kRunResult_BlockedWasActive:
            continue;
        }

        mpFiltSys->Block();
      }

      success = req->IsSuccessful();
    }

    for (VDFilterChainDesc::Entries::const_iterator it(mpFilterChainDesc->mEntries.begin()),
         itEnd(mpFilterChainDesc->mEntries.end());
         it != itEnd;
         ++it)
    {
      VDFilterChainEntry *ent = *it;
      FilterInstance *    fa  = ent->mpInstance;

      if (!fa->view)
        continue;

      vdrefptr<IVDFilterFrameClientRequest> req2;
      if (fa->CreateRequest(mLastOutputFrame, false, 0, ~req2))
      {
        while (!req2->IsCompleted())
        {
          if (mpFiltSys->Run(NULL, false) == FilterSystem::kRunResult_Running)
            continue;

          if (req->IsCompleted())
            break;

          switch (mpVideoFrameSource->RunRequests(NULL, 0))
          {
            case IVDFilterFrameSource::kRunResult_Running:
            case IVDFilterFrameSource::kRunResult_IdleWasActive:
            case IVDFilterFrameSource::kRunResult_BlockedWasActive:
              continue;
          }

          mpFiltSys->Block();
        }
      }
    }

    if (mpDisplay)
    {
      ShowWindow(mhwndVideoWindow, SW_SHOW);

      if (success)
      {
        mpVideoFrameBuffer = req->GetResultBuffer();
        const void *p      = mpVideoFrameBuffer->LockRead();

        VDPixmap px = VDPixmapFromLayout(GetFrameBufferLayout(), (void *)p);
        px.info     = mpVideoFrameBuffer->info;

        mpDisplay->SetSourcePersistent(false, px);
      }
      else
      {
        VDFilterFrameRequestError *err = req->GetError();

        if (err)
          mpDisplay->SetSourceMessage(VDTextAToW(err->mError.c_str()).c_str());
        else
          mpDisplay->SetSourceSolidColor(VDSwizzleU32(GetSysColor(COLOR_3DFACE)) >> 8);
      }

      mpDisplay->Update(IVDVideoDisplay::kAllFields);
      mpPosition->SetPosition(mpPosition->GetPosition());
      RedrawWindow(mhwndPosition, 0, 0, RDW_UPDATENOW);
    }
  }
  catch (const MyError &e)
  {
    mpDisplay->Reset();
    ShowWindow(mhwndVideoWindow, SW_HIDE);
    mFailureReason.assign(e);
    InvalidateRect(mhdlg, NULL, TRUE);
    UndoSystem();
  }
}

bool FilterPreview::OnCommand(UINT cmd)
{
  switch (cmd)
  {
    case IDCANCEL:
      if (mbNoExit)
      {
        if (mhwndFilter)
        {
          SetActiveWindow(mhwndFilter);
          SendMessage(mhwndFilter, WM_COMMAND, IDCANCEL, 0);
        }
        return true;
      }
      if (mpButtonCallback)
        mpButtonCallback(false, mpvButtonCBData);

      ClearWindow();
      UndoSystem();

      UpdateButton();
      return true;

    case ID_EDIT_JUMPTO: {
      SceneShuttleStop();
      extern VDPosition VDDisplayJumpToPositionDialog(
        VDGUIHandle hParent, VDPosition currentFrame, IVDVideoSource * pVS, const VDFraction &realRate);

      VDPosition pos = VDDisplayJumpToPositionDialog(
        (VDGUIHandle)mhdlg, mpPosition->GetPosition(), inputVideo, g_project->GetInputFrameRate());
      if (pos != -1)
      {
        mpPosition->SetPosition(pos);
        OnVideoRedraw();
      }
    }
      return true;

    case ID_VIDEO_SEEK_FNEXT:
      StartShuttleForward(false);
      return true;

    case ID_VIDEO_SEEK_FPREV:
      StartShuttleReverse(false);
      return true;

    case ID_VIDEO_SEEK_FSNEXT:
      StartShuttleForward(true);
      return true;

    case ID_VIDEO_SEEK_FSPREV:
      StartShuttleReverse(true);
      return true;

    case ID_VIDEO_SEEK_STOP:
      SceneShuttleStop();
      return true;

    case ID_VIDEO_SEEK_SELSTART: {
      VDPosition sel_start, sel_end;
      if (mpPosition->GetSelection(sel_start, sel_end))
      {
        SceneShuttleStop();
        mpPosition->SetPosition(sel_start);
        OnVideoRedraw();
      }
    }
      return true;

    case ID_VIDEO_SEEK_SELEND: {
      VDPosition sel_start, sel_end;
      if (mpPosition->GetSelection(sel_start, sel_end))
      {
        SceneShuttleStop();
        mpPosition->SetPosition(sel_end);
        OnVideoRedraw();
      }
    }
      return true;

    case ID_EDIT_SETMARKER:
      if (!mbDisplaySource)
      {
        mpTimeline->ToggleMarker(mpPosition->GetPosition());
        mpPosition->SetTimeline(*mpTimeline);
      }
      return true;

    case ID_EDIT_SETSELSTART: {
      VDPosition p = mpPosition->GetPosition();
      SetRangeFrames(p, p, true, false);
    }
      return true;

    case ID_EDIT_SETSELEND: {
      VDPosition p = mpPosition->GetPosition();
      SetRangeFrames(p, p, false, true);
    }
      return true;

    case ID_EDIT_CLEAR:
      SetRangeFrames(0, -1, true, true);
      return true;

    case ID_EDIT_SELECTALL:
      SetRangeFrames(0, 0, false, false);
      return true;

    case ID_EDIT_PREVRANGE:
      SceneShuttleStop();
      MoveToPreviousRange();
      OnVideoRedraw();
      return true;

    case ID_EDIT_NEXTRANGE:
      SceneShuttleStop();
      MoveToNextRange();
      OnVideoRedraw();
      return true;

    case ID_VIDEO_COPYOUTPUTFRAME:
      CopyOutputFrameToClipboard();
      return true;

    case ID_FILE_SAVEIMAGE:
      SceneShuttleStop();
      SaveImageAsk(false);
      return true;

    case ID_FILE_SAVEIMAGE2:
      SceneShuttleStop();
      SaveImageAsk(true);
      return true;

    case ID_FILE_SAVEPROJECT:
      SendMessage(mhwndFilterList, WM_COMMAND, IDC_FILTERS_SAVE, 0);
      return true;

    case ID_VIDEO_FILTERS:
      SceneShuttleStop();
      EnableWindow(mhwndParent, false);
      SendMessage(mhwndFilterList, WM_COMMAND, ID_VIDEO_FILTERS, 0);
      EnableWindow(mhwndParent, true);
      return true;

    case ID_PANELAYOUT_AUTOSIZE:
      SendMessage(mhwndVideoWindow, WM_COMMAND, ID_DISPLAY_ZOOM_AUTOSIZE, 0);
      return true;

    case ID_OPTIONS_SHOWPROFILER:
      extern void VDOpenProfileWindow(int);
      VDOpenProfileWindow(1);
      return true;

    default:
      if (VDHandleTimelineCommand(mpPosition, mpTimeline, cmd))
      {
        SceneShuttleStop();
        OnVideoRedraw();
        return true;
      }
  }

  return false;
}

void FilterPreview::MoveToPreviousRange()
{
  VDPosition p0   = mpPosition->GetPosition();
  VDPosition pos  = mpTimeline->GetPrevEdit(p0);
  VDPosition mpos = mpTimeline->GetPrevMarker(p0);
  VDPosition r0, r1;
  mpThisFilter->GetRangeFrames(r0, r1);

  if (mpos >= 0 && (mpos > pos || pos == -1))
    pos = mpos;
  if (r1 < p0 && (r1 > pos || pos == -1))
    pos = r1;
  if (r0 < p0 && (r0 > pos || pos == -1))
    pos = r0;

  if (pos >= 0)
  {
    mpPosition->SetPosition(pos);
    return;
  }

  mpPosition->SetPosition(0);
}

void FilterPreview::MoveToNextRange()
{
  VDPosition p0   = mpPosition->GetPosition();
  VDPosition pos  = mpTimeline->GetNextEdit(p0);
  VDPosition mpos = mpTimeline->GetNextMarker(p0);
  VDPosition r0, r1;
  mpThisFilter->GetRangeFrames(r0, r1);

  if (mpos >= 0 && (mpos < pos || pos == -1))
    pos = mpos;
  if (r1 > p0 && (r1 < pos || pos == -1))
    pos = r1;
  if (r0 > p0 && (r0 < pos || pos == -1))
    pos = r0;

  if (pos >= 0)
  {
    mpPosition->SetPosition(pos);
    return;
  }

  mpPosition->SetPosition(mpTimeline->GetLength());
}

void FilterPreview::StartShuttleForward(bool sticky)
{
  mShuttleMode   = 1;
  mStickyShuttle = sticky;
  SetTimer(mhdlg, TIMER_SHUTTLE, 0, 0);
}

void FilterPreview::StartShuttleReverse(bool sticky)
{
  mShuttleMode   = -1;
  mStickyShuttle = sticky;
  SetTimer(mhdlg, TIMER_SHUTTLE, 0, 0);
}

void FilterPreview::SceneShuttleStop()
{
  mShuttleMode = 0;
  KillTimer(mhdlg, TIMER_SHUTTLE);
}

void FilterPreview::SceneShuttleStep()
{
  VDPosition pos = mpPosition->GetPosition() + mShuttleMode;
  if (pos < 0 || pos >= mpTimeline->GetLength())
  {
    SceneShuttleStop();
    return;
  }
  mpPosition->SetPosition(pos);
  OnVideoRedraw();
}

VDPosition FilterPreview::FetchFrame()
{
  return FetchFrame(mpPosition->GetPosition());
}

VDPosition FilterPreview::FetchFrame(VDPosition pos)
{
  try
  {
    IVDStreamSource *pVSS = inputVideo->asStream();
    const VDFraction frameRate(pVSS->getRate());

    // This is a pretty awful hack, but gets around the problem that the
    // timeline isn't updated for the new frame rate.
    if (mpFiltSys->GetOutputFrameRate() != frameRate)
      mLastOutputFrame = pos;
    else
    {
      mLastOutputFrame = mpTimeline->TimelineToSourceFrame(pos);
      if (mLastOutputFrame < 0)
        mLastOutputFrame = mpFiltSys->GetOutputFrameCount();
    }

    mLastTimelineFrame  = pos;
    mLastTimelineTimeMS = VDRoundToInt64(mpFiltSys->GetOutputFrameRate().AsInverseDouble() * 1000.0 * (double)pos);

    mInitialTimeUS = VDRoundToInt64(mpFiltSys->GetOutputFrameRate().AsInverseDouble() * 1000000.0 * (double)pos);
    mInitialFrame  = -1;

    if (mpPositionCallback)
      mpPositionCallback(pos, mpvPositionCBData);
  }
  catch (const MyError &)
  {
    return -1;
  }

  return pos;
}

int64 FilterPreview::FMSetPosition(int64 pos)
{
  if (mhdlg)
  {
    SceneShuttleStop();
    mpPosition->SetPosition(pos);
    OnVideoRedraw();
  }
  else
  {
    mInitialTimeUS = -1;
    mInitialFrame  = pos;
  }
  return pos;
}

bool FilterPreview::isPreviewEnabled()
{
  return !!mpFilterChainDesc;
}

bool FilterPreview::IsPreviewDisplayed()
{
  return mhdlg != NULL;
}

void FilterPreview::SetButtonCallback(VDXFilterPreviewButtonCallback pfpbc, void *pvData)
{
  mpButtonCallback = pfpbc;
  mpvButtonCBData  = pvData;
}

void FilterPreview::SetSampleCallback(VDXFilterPreviewSampleCallback pfpsc, void *pvData)
{
  mpSampleCallback = pfpsc;
  mpvSampleCBData  = pvData;
}

void FilterPreview::FMSetPositionCallback(FilterModPreviewPositionCallback pfppc, void *pvData)
{
  mpPositionCallback = pfppc;
  mpvPositionCBData  = pvData;
}

void FilterPreview::FMSetZoomCallback(FilterModPreviewZoomCallback pfppc, void *pvData)
{
  mpZoomCallback = pfppc;
  mpvZoomCBData  = pvData;
}

void FilterPreview::SetClipEditCallback(FilterModPreviewClipEditCallback pfppc, void *pvData)
{
  mpClipEditCallback = pfppc;
  mpClipEditCBData   = pvData;
}

void FilterPreview::InitButton(VDXHWND hwnd)
{
  mhwndButton = (HWND)hwnd;

  if (hwnd)
  {
    const VDStringW wintext(VDGetWindowTextW32((HWND)hwnd));

    // look for an accelerator
    mButtonAccelerator = 0;

    if (mpFilterChainDesc)
    {
      int pos = wintext.find(L'&');
      if (pos != wintext.npos)
      {
        ++pos;
        if (pos < wintext.size())
        {
          wchar_t c = wintext[pos];

          if (iswalpha(c))
            mButtonAccelerator = towlower(c);
        }
      }
    }

    EnableWindow((HWND)hwnd, mpFilterChainDesc ? TRUE : FALSE);
  }
}

void FilterPreview::ClearWindow()
{
  SetActiveWindow(mhwndParent);
  DestroyWindow(mhdlg);
  mhdlg = NULL;
}

void FilterPreview::Toggle(VDXHWND hwndParent)
{
  Display(hwndParent, !mhdlg);
}

void FilterPreview::Display(VDXHWND hwndParent, bool fDisplay)
{
  if (fDisplay == !!mhdlg)
    return;

  if (mhdlg)
  {
    ClearWindow();
    UndoSystem();
  }
  else if (mpFilterChainDesc)
  {
    mhwndParent = (HWND)hwndParent;
    mhdlg =
      CreateDialogParam(g_hInst, MAKEINTRESOURCE(IDD_FILTER_PREVIEW), (HWND)hwndParent, StaticDlgProc, (LPARAM)this);
    g_projectui->DisplayPreview(true);
  }

  UpdateButton();

  if (mpButtonCallback)
    mpButtonCallback(!!mhdlg, mpvButtonCBData);
}

void FilterPreview::RedoFrame()
{
  if (mhdlg)
    SendMessage(mhdlg, MYWM_INVALIDATE, 0, 0);
  SetRangeFrames();
}

void FilterPreview::RedoSystem()
{
  if (mhdlg)
    SendMessage(mhdlg, MYWM_RESTART, 0, 0);
  SetRangeFrames();
}

void FilterPreview::SampleRedoSystem()
{
  if (mhdlg)
    SendMessage(mhdlg, MYWM_RESTART, 0, 0);
  else
  {
    try
    {
      InitFilterSystem();
    }
    catch (const MyError &)
    {
    }
  }
  SetRangeFrames();
}

void FilterPreview::UndoSystem()
{
  if (mpDisplay)
    mpDisplay->Reset();

  if (mpVideoFrameBuffer)
  {
    mpVideoFrameBuffer->Unlock();
    mpVideoFrameBuffer = NULL;
  }
  mpFiltSys->DeinitFilters();
  mpFiltSys->DeallocateBuffers();
  mpVideoFrameSource = NULL;
}

void FilterPreview::Close()
{
  InitButton(NULL);
  if (mhdlg)
    Toggle(NULL);
  UndoSystem();
}

const VDPixmapLayout &FilterPreview::GetFrameBufferLayout()
{
  if (mbDisplaySource)
  {
    IVDFilterFrameSource *src = mpThisFilter->GetSource(0);
    if (src)
      return src->GetOutputLayout();
    mbDisplaySource    = false;
    mpClipEditCallback = 0;
    if (pOverlay)
    {
      mpDisplay->SetDrawMode(0);
      mpVideoWindow->SetDrawMode(0);
      ShowWindow(pOverlay->GetHwnd(), SW_HIDE);
      mbShowOverlay = false;
    }
  }

  return mpFiltSys->GetOutputLayout();
}

void FilterPreview::CopyOutputFrameToClipboard()
{
  if (!mpFiltSys->isRunning() || !mpVideoFrameBuffer)
    return;

  VDPixmap px = VDPixmapFromLayout(GetFrameBufferLayout(), (void *)mpVideoFrameBuffer->LockRead());
  px.info     = mpVideoFrameBuffer->info;
  g_project->CopyFrameToClipboard(px);
  mpVideoFrameBuffer->Unlock();
}

void FilterPreview::SaveImageAsk(bool skip_dialog)
{
  if (!mpFiltSys->isRunning() || !mpVideoFrameBuffer)
    return;

  VDPosition pos = mpPosition->GetPosition();
  VDPixmap   px  = VDPixmapFromLayout(GetFrameBufferLayout(), (void *)mpVideoFrameBuffer->LockRead());
  px.info        = mpVideoFrameBuffer->info;
  mpVideoFrameBuffer->Unlock();
  SaveImage(mhdlg, pos, &px, skip_dialog);
}

bool FilterPreview::SampleCurrentFrame()
{
  if (!mpFilterChainDesc || !mpSampleCallback)
    return false;

  HWND parent = mhdlg;
  if (!parent)
    parent = mhwndFilter;
  if (!parent && mhwndButton)
    parent = GetParent(mhwndButton);
  if (!parent)
    return false;

  if (!mpFiltSys->isRunning())
  {
    SampleRedoSystem();

    if (!mpFiltSys->isRunning())
      return false;
  }

  VDPosition pos;
  if (mpPosition)
    pos = mpPosition->GetPosition();
  else
    pos = InitPosition();

  if (pos >= 0)
  {
    try
    {
      IVDStreamSource *pVSS = inputVideo->asStream();
      const VDFraction frameRate(pVSS->getRate());

      // This hack is for consistency with FetchFrame().
      sint64 frame = pos;

      if (mpFiltSys->GetOutputFrameRate() == frameRate)
      {
        frame = mpTimeline->TimelineToSourceFrame(frame);

        if (frame < 0)
          frame = mpFiltSys->GetOutputFrameCount();
      }

      frame = mpFiltSys->GetSymbolicFrame(frame, mpThisFilter);

      if (frame >= 0)
      {
        vdrefptr<IVDFilterFrameClientRequest> req;
        if (mpThisFilter->CreateSamplingRequest(frame, mpSampleCallback, mpvSampleCBData, 0, 0, ~req))
        {
          while (!req->IsCompleted())
          {
            if (mpFiltSys->Run(NULL, false) == FilterSystem::kRunResult_Running)
              continue;

            if (req->IsCompleted())
              break;

            switch (mpVideoFrameSource->RunRequests(NULL, 0))
            {
              case IVDFilterFrameSource::kRunResult_Running:
              case IVDFilterFrameSource::kRunResult_IdleWasActive:
              case IVDFilterFrameSource::kRunResult_BlockedWasActive:
                continue;
            }

            mpFiltSys->Block();
          }
        }
      }
    }
    catch (const MyError &e)
    {
      e.post(parent, "Video sampling error");
    }
  }

  RedrawFrame();

  return true;
}

void FilterPreview::UpdateButton()
{
  if (mhwndButton)
  {
    VDStringW text(mhdlg ? L"Hide preview" : L"Show preview");

    if (mButtonAccelerator)
    {
      VDStringW::size_type pos = text.find(mButtonAccelerator);

      if (pos == VDStringW::npos)
        pos = text.find(towupper(mButtonAccelerator));

      if (pos != VDStringW::npos)
        text.insert(text.begin() + pos, L'&');
    }

    VDSetWindowTextW32(mhwndButton, text.c_str());
  }
}

///////////////////////

#define FPSAMP_KEYONESEC (1)
#define FPSAMP_KEYALL (2)
#define FPSAMP_ALL (3)

static INT_PTR CALLBACK SampleFramesDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
    case WM_INITDIALOG:
      CheckDlgButton(hdlg, IDC_ONEKEYPERSEC, BST_CHECKED);
      return TRUE;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDOK:
          if (IsDlgButtonChecked(hdlg, IDC_ONEKEYPERSEC))
            EndDialog(hdlg, FPSAMP_KEYONESEC);
          else if (IsDlgButtonChecked(hdlg, IDC_ALLKEYS))
            EndDialog(hdlg, FPSAMP_KEYALL);
          else
            EndDialog(hdlg, FPSAMP_ALL);
          return TRUE;
        case IDCANCEL:
          EndDialog(hdlg, 0);
          return TRUE;
      }
      break;
  }
  return FALSE;
}

long FilterPreview::SampleFrames()
{
  static const char *const szCaptions[] = {
    "Sampling one keyframe per second",
    "Sampling keyframes only",
    "Sampling all frames",
  };

  long lCount = 0;

  if (!mpFilterChainDesc || !mpSampleCallback)
    return -1;

  HWND parent = mhdlg;
  if (!parent)
    parent = mhwndFilter;
  if (!parent && mhwndButton)
    parent = GetParent(mhwndButton);
  if (!parent)
    return -1;

  if (!mpFiltSys->isRunning())
  {
    SampleRedoSystem();

    if (!mpFiltSys->isRunning())
      return -1;
  }

  int mode = DialogBox(g_hInst, MAKEINTRESOURCE(IDD_FILTER_SAMPLE), parent, SampleFramesDlgProc);
  if (!mode)
    return -1;

  // Time to do the actual sampling.
  try
  {
    VDPosition       count = mpThisFilter->GetOutputFrameCount();
    ProgressDialog   pd(parent, "Sampling input video", szCaptions[mode - 1], VDClampToSint32(count), true);
    IVDStreamSource *pVSS            = inputVideo->asStream();
    VDPosition       secondIncrement = pVSS->msToSamples(1000) - 1;

    pd.setValueFormat("Sampling frame %ld of %ld");

    if (secondIncrement < 0)
      secondIncrement = 0;

    vdrefptr<IVDFilterFrameClientRequest> req;

    VDPosition lastFrame = 0;
    for (VDPosition frame = 0; frame < count; ++frame)
    {
      pd.advance(VDClampToSint32(frame));
      pd.check();

      VDPosition srcFrame = mpThisFilter->GetSourceFrame(frame);

      if (mode != FPSAMP_ALL)
      {
        if (!inputVideo->isKey(srcFrame))
          continue;

        if (mode == FPSAMP_KEYONESEC)
        {
          if (frame - lastFrame < secondIncrement)
            continue;
        }

        lastFrame = frame;
      }

      if (mpThisFilter->CreateSamplingRequest(frame, mpSampleCallback, mpvSampleCBData, 0, 0, ~req))
      {
        while (!req->IsCompleted())
        {
          if (mpFiltSys->Run(NULL, false) == FilterSystem::kRunResult_Running)
            continue;

          if (req->IsCompleted())
            break;

          switch (mpVideoFrameSource->RunRequests(NULL, 0))
          {
            case IVDFilterFrameSource::kRunResult_Running:
            case IVDFilterFrameSource::kRunResult_IdleWasActive:
            case IVDFilterFrameSource::kRunResult_BlockedWasActive:
              continue;
          }

          mpFiltSys->Block();
        }

        ++lCount;
      }
    }
  }
  catch (MyUserAbortError e)
  {
    /* so what? */
  }
  catch (const MyError &e)
  {
    e.post(parent, "Video sampling error");
  }

  RedrawFrame();

  return lCount;
}

long FilterPreview::SampleFrames(IFilterModPreviewSample *handler)
{
  long lCount = 0;

  if (!mpFilterChainDesc || !handler)
    return -1;

  HWND parent = mhdlg;
  if (!parent)
    parent = mhwndFilter;
  if (!parent && mhwndButton)
    parent = GetParent(mhwndButton);
  if (!parent)
    return -1;

  if (!mpFiltSys->isRunning())
  {
    SampleRedoSystem();

    if (!mpFiltSys->isRunning())
      return -1;
  }

  bool image_changed = false;

  // Time to do the actual sampling.
  try
  {
    ProgressDialog pd(parent, "Sampling input video", "Sampling all frames", 1, true);

    pd.setValueFormat("Sampling frame %ld of %ld");

    vdrefptr<IVDFilterFrameClientRequest> req;

    VDPosition sel_start = g_project->GetSelectionStartFrame();
    VDPosition sel_end   = g_project->GetSelectionEndFrame();
    if (sel_end == -1)
      sel_end = g_project->GetFrameCount();
    sint64 total_count = sel_end - sel_start - 1;

    VDPosition frame = -1;
    while (1)
    {
      VDPosition nextFrame = frame + 1;
      if (frame == -1)
        nextFrame = sel_start;
      if (frame >= sel_end - 1)
        nextFrame = -1;
      handler->GetNextFrame(frame, &nextFrame, &total_count);
      if (nextFrame == -1)
        break;
      frame = nextFrame;

      pd.setLimit(VDClampToSint32(total_count));
      pd.advance(lCount);
      pd.check();

      if (mpThisFilter->CreateSamplingRequest(frame, 0, 0, handler, 0, ~req))
      {
        while (!req->IsCompleted())
        {
          if (mpFiltSys->Run(NULL, false) == FilterSystem::kRunResult_Running)
            continue;

          if (req->IsCompleted())
            break;

          switch (mpVideoFrameSource->RunRequests(NULL, 0))
          {
            case IVDFilterFrameSource::kRunResult_Running:
            case IVDFilterFrameSource::kRunResult_IdleWasActive:
            case IVDFilterFrameSource::kRunResult_BlockedWasActive:
              continue;
          }

          mpFiltSys->Block();
        }

        int result = FilterInstance::GetSamplingRequestResult(req);
        if ((result & IFilterModPreviewSample::result_image) && req->GetResultBuffer())
        {
          if (mpVideoFrameBuffer)
          {
            mpVideoFrameBuffer->Unlock();
            mpVideoFrameBuffer = NULL;
          }

          mpVideoFrameBuffer = req->GetResultBuffer();
          const void *p      = mpVideoFrameBuffer->LockRead();

          const VDPixmapLayout &layout = mpThisFilter->GetOutputLayout();
          VDPixmap              px     = VDPixmapFromLayout(layout, (void *)p);
          px.info                      = mpVideoFrameBuffer->info;

          mpDisplay->SetSourcePersistent(false, px);
          mpDisplay->Update(IVDVideoDisplay::kAllFields);
          image_changed = true;
        }

        ++lCount;
      }
    }
  }
  catch (MyUserAbortError e)
  {
    handler->Cancel();
  }
  catch (const MyError &e)
  {
    e.post(parent, "Video sampling error");
  }

  if (image_changed)
    mpDisplay->Update(IVDVideoDisplay::kAllFields);
  else
    RedrawFrame();

  return lCount;
}

void FilterPreview::RedrawFrame()
{
  if (mhdlg)
    SendMessage(mhdlg, MYWM_REDRAW, 0, 0);
}

///////////////////////////////////////////////////////////////////////////////

class PixmapView : public vdrefcounted<IVDPixmapViewDialog>
{
public:
  PixmapView();
  ~PixmapView();
  void Display(VDXHWND hwndParent, const wchar_t *title);
  void Destroy();
  void SetImage(VDPixmap &px);
  void SetDestroyCallback(PixmapViewDestroyCallback cb, void *cbData)
  {
    destroyCB     = cb;
    destroyCBData = cbData;
  }

private:
  static INT_PTR CALLBACK StaticDlgProc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam);
  BOOL                    DlgProc(UINT message, WPARAM wParam, LPARAM lParam);

  void OnInit();
  void OnResize();
  void OnPaint();
  void OnVideoRedraw();
  bool OnCommand(UINT);
  void CopyOutputFrameToClipboard();
  void SaveImageAsk(bool skip_dialog);

  HWND mhdlg;
  HWND mhwndParent;
  HWND mhwndVideoWindow;
  HWND mhwndDisplay;

  IVDVideoDisplay *mpDisplay;
  IVDVideoWindow * mpVideoWindow;

  VDPixmapBuffer image;

  ModelessDlgNode           mDlgNode;
  PixmapViewDestroyCallback destroyCB;
  void *                    destroyCBData;
};

bool VDCreatePixmapViewDialog(IVDPixmapViewDialog **pp)
{
  IVDPixmapViewDialog *p = new_nothrow PixmapView();
  if (!p)
    return false;
  p->AddRef();
  *pp = p;
  return true;
}

PixmapView::PixmapView()
  : mhdlg(NULL), mhwndParent(NULL), mhwndVideoWindow(NULL), mhwndDisplay(NULL), mpDisplay(NULL), mpVideoWindow(NULL)
{
  destroyCB     = 0;
  destroyCBData = 0;
}

PixmapView::~PixmapView()
{
  if (mhdlg)
    DestroyWindow(mhdlg);
}

void PixmapView::Display(VDXHWND hwndParent, const wchar_t *title)
{
  mhwndParent = (HWND)hwndParent;
  mhdlg =
    CreateDialogParam(g_hInst, MAKEINTRESOURCE(IDD_FILTER_PREVIEW), (HWND)hwndParent, StaticDlgProc, (LPARAM)this);
  SetWindowTextW(mhdlg, title);
  ShowWindow(mhdlg, SW_SHOW);
}

void PixmapView::Destroy()
{
  if (mhdlg)
  {
    // SetActiveWindow(mhwndParent);
    DestroyWindow(mhdlg);
    mhdlg = NULL;
  }
}

INT_PTR CALLBACK PixmapView::StaticDlgProc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
  PixmapView *fpd = (PixmapView *)GetWindowLongPtr(hdlg, DWLP_USER);

  if (message == WM_INITDIALOG)
  {
    SetWindowLongPtr(hdlg, DWLP_USER, lParam);
    fpd        = (PixmapView *)lParam;
    fpd->mhdlg = hdlg;
  }

  return fpd && fpd->DlgProc(message, wParam, lParam);
}

BOOL PixmapView::DlgProc(UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message)
  {
    case WM_INITDIALOG:
      OnInit();
      VDSetDialogDefaultIcons(mhdlg);
      OnVideoRedraw();
      return TRUE;

    case WM_DESTROY:
      if (mpDisplay)
      {
        mpDisplay->Destroy();
        mpDisplay    = NULL;
        mhwndDisplay = NULL;
      }

      mpVideoWindow    = NULL;
      mhwndVideoWindow = NULL;
      mhdlg            = NULL;

      mDlgNode.Remove();
      if (destroyCB)
        destroyCB(this, destroyCBData);
      return TRUE;

    case WM_EXITSIZEMOVE:
      mpVideoWindow->SetAutoSize(false);
      return TRUE;

    case WM_SIZE:
      OnResize();
      OnVideoRedraw();
      return TRUE;

    case WM_PAINT:
      OnPaint();
      return TRUE;

    case WM_ERASEBKGND:
      return TRUE;

    case WM_NOTIFY: {
      const NMHDR &hdr = *(const NMHDR *)lParam;
      if (hdr.hwndFrom == mhwndVideoWindow)
      {
        switch (hdr.code)
        {
          case VWN_REPOSITION: {
            RECT r;
            GetWindowRect(mhdlg, &r);
            RECT r1 = {0, 0, 0, 0};
            AdjustWindowRectEx(&r1, GetWindowLong(mhdlg, GWL_STYLE), FALSE, GetWindowLong(mhdlg, GWL_EXSTYLE));
            MONITORINFO info = {sizeof(info)};
            GetMonitorInfo(MonitorFromWindow(mhdlg, MONITOR_DEFAULTTONEAREST), &info);
            int w = info.rcWork.right - r.left - (r1.right - r1.left);
            int h = info.rcWork.bottom - r.top - (r1.bottom - r1.top);
            mpVideoWindow->SetZoom(mpVideoWindow->GetMaxZoomForArea(w, h), false);
          }
          break;

          case VWN_RESIZED: {
            RECT r;
            GetWindowRect(mhwndVideoWindow, &r);

            AdjustWindowRectEx(&r, GetWindowLong(mhdlg, GWL_STYLE), FALSE, GetWindowLong(mhdlg, GWL_EXSTYLE));
            SetWindowPos(
              mhdlg, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
          }
          break;
        }
      }
    }
      return TRUE;

    case WM_COMMAND:
      return OnCommand(LOWORD(wParam));

    case WM_CONTEXTMENU: {
      POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
      RECT  r;

      if (::GetWindowRect(mhwndVideoWindow, &r) && ::PtInRect(&r, pt))
      {
        SendMessage(mhwndVideoWindow, WM_CONTEXTMENU, wParam, lParam);
      }
    }
    break;
  }

  return FALSE;
}

void PixmapView::OnInit()
{
  mhwndVideoWindow = CreateWindow(
    VIDEOWINDOWCLASS, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, mhdlg, (HMENU)100, g_hInst, NULL);
  mhwndDisplay = (HWND)VDCreateDisplayWindowW32(
    0, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, (VDGUIHandle)mhwndVideoWindow);
  if (mhwndDisplay)
    mpDisplay = VDGetIVideoDisplay((VDGUIHandle)mhwndDisplay);
  EnableWindow(mhwndDisplay, FALSE);

  mpVideoWindow = VDGetIVideoWindow(mhwndVideoWindow);
  mpVideoWindow->SetPanCentering(g_panCentering);
  mpVideoWindow->SetChild(mhwndDisplay);
  mpVideoWindow->SetDisplay(mpDisplay);
  mpVideoWindow->SetMouseTransparent(true);
  mpVideoWindow->SetBorder(0);
  mpVideoWindow->InitSourcePAR();

  mDlgNode.hdlg    = mhdlg;
  mDlgNode.mhAccel = g_projectui->GetAccelPreview();
  guiAddModelessDialog(&mDlgNode);

  if (image.format)
    mpVideoWindow->SetSourceSize(image.w, image.h);
  else
    mpVideoWindow->SetSourceSize(256, 256);
}

void PixmapView::OnResize()
{
  RECT r;

  GetClientRect(mhdlg, &r);

  int mDisplayX = 0;
  int mDisplayY = 0;
  int mDisplayW = r.right;
  int mDisplayH = r.bottom;

  if (mDisplayW < 0)
    mDisplayW = 0;

  if (mDisplayH < 0)
    mDisplayH = 0;

  SetWindowPos(mhwndVideoWindow, NULL, mDisplayX, mDisplayY, mDisplayW, mDisplayH, SWP_NOZORDER | SWP_NOACTIVATE);

  InvalidateRect(mhdlg, NULL, TRUE);
}

void PixmapView::OnPaint()
{
  PAINTSTRUCT ps;
  BeginPaint(mhdlg, &ps);
  EndPaint(mhdlg, &ps);
}

void PixmapView::OnVideoRedraw()
{
  ShowWindow(mhwndVideoWindow, SW_SHOW);
  if (image.format)
    mpDisplay->SetSourcePersistent(false, image);
  else
    mpDisplay->SetSourceSolidColor(VDSwizzleU32(GetSysColor(COLOR_3DFACE)) >> 8);
  mpDisplay->Update(IVDVideoDisplay::kAllFields);
}

bool PixmapView::OnCommand(UINT cmd)
{
  switch (cmd)
  {
    case IDCANCEL:
      SetActiveWindow(mhwndParent);
      DestroyWindow(mhdlg);
      return true;

    case ID_VIDEO_COPYOUTPUTFRAME:
      CopyOutputFrameToClipboard();
      return true;

    case ID_FILE_SAVEIMAGE:
      SaveImageAsk(false);
      return true;

    case ID_FILE_SAVEIMAGE2:
      SaveImageAsk(true);
      return true;

    case ID_PANELAYOUT_AUTOSIZE:
      SendMessage(mhwndVideoWindow, WM_COMMAND, ID_DISPLAY_ZOOM_AUTOSIZE, 0);
      return true;

    case ID_OPTIONS_SHOWPROFILER:
      extern void VDOpenProfileWindow(int);
      VDOpenProfileWindow(1);
      return true;
  }

  return false;
}

void PixmapView::SetImage(VDPixmap &px)
{
  image.assign(px);

  if (mpVideoWindow)
  {
    if (image.format)
      mpVideoWindow->SetSourceSize(image.w, image.h);
    else
      mpVideoWindow->SetSourceSize(256, 256);
    OnVideoRedraw();
  }
}

void PixmapView::CopyOutputFrameToClipboard()
{
  if (!image.format)
    return;
  g_project->CopyFrameToClipboard(image);
}

void PixmapView::SaveImageAsk(bool skip_dialog)
{
  if (!image.format)
    return;
  SaveImage(mhdlg, -1, &image, skip_dialog);
}
