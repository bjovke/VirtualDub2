#ifndef tiff_image_h
#define tiff_image_h

#include <vd2/system/vdtypes.h>

bool VDIsTiffHeader(const void *pv, uint32 len);

struct VDPixmap;
struct FilterModPixmapInfo;

class VDINTERFACE IVDImageDecoderTIFF {
public:
	virtual ~IVDImageDecoderTIFF() {}

	virtual void Decode(const void *src, uint32 srclen) = 0;
	virtual void GetSize(int& w, int& h) = 0;
	virtual int GetFormat() = 0;
	virtual void GetImage(void *p, int pitch, int format) = 0;
	virtual void GetPixmapInfo(FilterModPixmapInfo& info) = 0;
};

enum {
  tiffenc_default,
  tiffenc_lzw,
  tiffenc_zip
};

class VDINTERFACE IVDImageEncoderTIFF {
public:
	virtual ~IVDImageEncoderTIFF() {}
	virtual void Encode(const VDPixmap& px, void *&p, uint32& len, int compress, bool alpha) = 0;
};

IVDImageDecoderTIFF *VDCreateImageDecoderTIFF();
IVDImageEncoderTIFF *VDCreateImageEncoderTIFF();

#endif
