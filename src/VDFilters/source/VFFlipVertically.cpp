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

static int flipv_run(const VDXFilterActivation *fa, const VDXFilterFunctions *ff)
{
  return 0;
}

static long flipv_param(VDXFilterActivation *fa, const VDXFilterFunctions *ff)
{
  using namespace vd2;

  const VDXPixmapLayout &pxsrc = *fa->src.mpPixmapLayout;
  VDXPixmapLayout &      pxdst = *fa->dst.mpPixmapLayout;

  // flip the primary plane
  fa->dst.depth = 0;
  pxdst         = pxsrc;

  pxdst.data += pxdst.pitch * (pxdst.h - 1);
  pxdst.pitch = -pxdst.pitch;

  if (VDPixmapFormatHasAlphaPlane(pxsrc.format))
  {
    VDXPixmapLayoutAlpha &pxdsta = (VDXPixmapLayoutAlpha &)pxdst;
    pxdsta.data4 += pxdsta.pitch4 * (pxdst.h - 1);
    pxdsta.pitch4 = -pxdsta.pitch4;
  }

  int subh;
  switch (pxsrc.format)
  {
    case kPixFormat_XRGB1555:
    case kPixFormat_RGB565:
    case kPixFormat_RGB888:
    case kPixFormat_XRGB8888:
    case kPixFormat_Y8:
    case kPixFormat_Y8_FR:
    case kPixFormat_Y16:
    case kPixFormat_YUV422_UYVY:
    case kPixFormat_YUV422_UYVY_FR:
    case kPixFormat_YUV422_UYVY_709:
    case kPixFormat_YUV422_UYVY_709_FR:
    case kPixFormat_YUV422_YUYV:
    case kPixFormat_YUV422_YUYV_FR:
    case kPixFormat_YUV422_YUYV_709:
    case kPixFormat_YUV422_YUYV_709_FR:
    case kPixFormat_XRGB64:
      break;
    case kPixFormat_RGB_Planar:
    case kPixFormat_RGB_Planar16:
    case kPixFormat_RGB_Planar32F:
    case kPixFormat_RGBA_Planar:
    case kPixFormat_RGBA_Planar16:
    case kPixFormat_RGBA_Planar32F:
      pxdst.data2 += pxdst.pitch2 * (pxdst.h - 1);
      pxdst.pitch2 = -pxdst.pitch2;
      pxdst.data3 += pxdst.pitch3 * (pxdst.h - 1);
      pxdst.pitch3 = -pxdst.pitch3;
      break;
    case kPixFormat_YUV444_Planar:
    case kPixFormat_YUV444_Planar_FR:
    case kPixFormat_YUV444_Planar_709:
    case kPixFormat_YUV444_Planar_709_FR:
    case kPixFormat_YUV422_Planar:
    case kPixFormat_YUV422_Planar_FR:
    case kPixFormat_YUV422_Planar_709:
    case kPixFormat_YUV422_Planar_709_FR:
    case kPixFormat_YUV411_Planar:
    case kPixFormat_YUV411_Planar_FR:
    case kPixFormat_YUV411_Planar_709:
    case kPixFormat_YUV411_Planar_709_FR:
    case kPixFormat_YUV444_Planar16:
    case kPixFormat_YUV422_Planar16:
    case kPixFormat_YUV444_Alpha_Planar16:
    case kPixFormat_YUV422_Alpha_Planar16:
    case kPixFormat_YUV444_Alpha_Planar:
    case kPixFormat_YUV422_Alpha_Planar:
      subh = pxdst.h;
      pxdst.data2 += pxdst.pitch2 * (subh - 1);
      pxdst.pitch2 = -pxdst.pitch2;
      pxdst.data3 += pxdst.pitch3 * (subh - 1);
      pxdst.pitch3 = -pxdst.pitch3;
      break;
    case kPixFormat_YUV420_Planar:
    case kPixFormat_YUV420_Planar_FR:
    case kPixFormat_YUV420_Planar_709:
    case kPixFormat_YUV420_Planar_709_FR:
    case kPixFormat_YUV420_Planar16:
    case kPixFormat_YUV420_Alpha_Planar16:
    case kPixFormat_YUV420_Alpha_Planar:
      subh = (pxdst.h + 1) >> 1;
      pxdst.data2 += pxdst.pitch2 * (subh - 1);
      pxdst.pitch2 = -pxdst.pitch2;
      pxdst.data3 += pxdst.pitch3 * (subh - 1);
      pxdst.pitch3 = -pxdst.pitch3;
      break;
    case kPixFormat_YUV410_Planar:
    case kPixFormat_YUV410_Planar_FR:
    case kPixFormat_YUV410_Planar_709:
    case kPixFormat_YUV410_Planar_709_FR:
      subh = (pxdst.h + 3) >> 2;
      pxdst.data2 += pxdst.pitch2 * (subh - 1);
      pxdst.pitch2 = -pxdst.pitch2;
      pxdst.data3 += pxdst.pitch3 * (subh - 1);
      pxdst.pitch3 = -pxdst.pitch3;
      break;

    default:
      return FILTERPARAM_NOT_SUPPORTED;
  }

  return FILTERPARAM_SUPPORTS_ALTFORMATS | FILTERPARAM_PURE_TRANSFORM;
}

extern const VDXFilterDefinition g_VDVFFlipVertically = {
  0,
  0,
  NULL,
  "flip vertically",
  "Vertically flips an image.\n\n[YCbCr processing]",
  NULL,
  NULL,
  0,
  NULL,
  NULL,
  flipv_run,
  flipv_param};
