/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2019 Olive Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "oiiodecoder.h"

#include <OpenImageIO/imagebufalgo.h>
#include <QDebug>
#include <QFileInfo>

#include "common/define.h"

QStringList OIIODecoder::supported_formats_;

OIIODecoder::OIIODecoder() :
  image_(nullptr),
  buffer_(nullptr)
{
}

QString OIIODecoder::id()
{
  return QStringLiteral("oiio");
}

bool OIIODecoder::Probe(Footage *f, const QAtomicInt *cancelled)
{
  // We prioritize OIIO over FFmpeg to pick up still images more effectively, but some OIIO decoders (notably OpenJPEG)
  // will segfault entirely if given unexpected data (an MPEG-4 for instance). To workaround this issue, we use OIIO's
  // "extension_list" attribute and match it with the extension of the file.

  // Check if we've created the supported formats list, create it if not
  if (supported_formats_.isEmpty()) {
    QStringList extension_list = QString::fromStdString(OIIO::get_string_attribute("extension_list")).split(';');

    // The format of "extension_list" is "format:ext", we want to separate it into a simple list of extensions
    foreach (const QString& ext, extension_list) {
      QStringList format_and_ext = ext.split(':');

      supported_formats_.append(format_and_ext.at(1).split(','));
    }
  }

  //
  QFileInfo file_info(f->filename());

  if (!supported_formats_.contains(file_info.completeSuffix(), Qt::CaseInsensitive)) {
    return false;
  }

  std::string std_filename = f->filename().toStdString();

  auto in = OIIO::ImageInput::open(std_filename);

  if (!in) {
    return false;
  }

  if (!strcmp(in->format_name(), "FFmpeg movie")) {
    // If this is FFmpeg via OIIO, fall-through to our native FFmpeg decoder
    return false;
  }

  // Get stats for this image and dump them into the Footage file
  const OIIO::ImageSpec& spec = in->spec();

  ImageStreamPtr image_stream = std::make_shared<ImageStream>();
  image_stream->set_width(spec.width);
  image_stream->set_height(spec.height);

  // Images will always have just one stream
  image_stream->set_index(0);

  // OIIO automatically premultiplies alpha
  // FIXME: We usually disassociate the alpha for the color management later, for 8-bit images this likely reduces the
  //        fidelity?
  image_stream->set_premultiplied_alpha(true);

  f->add_stream(image_stream);

  // If we're here, we have a successful image open
  in->close();

  return true;
}

bool OIIODecoder::Open()
{
  QMutexLocker locker(&mutex_);

  image_ = OIIO::ImageInput::open(stream()->footage()->filename().toStdString());

  if (!image_) {
    return false;
  }

  // Check if we can work with this pixel format
  const OIIO::ImageSpec& spec = image_->spec();

  width_ = spec.width;
  height_ = spec.height;

  is_rgba_ = (spec.nchannels == kRGBAChannels);

  // Weirdly, switch statement doesn't work correctly here
  if (spec.format == OIIO::TypeDesc::UINT8) {
    pix_fmt_ = is_rgba_ ? PixelFormat::PIX_FMT_RGBA8 : PixelFormat::PIX_FMT_RGB8;
  } else if (spec.format == OIIO::TypeDesc::UINT16) {
    pix_fmt_ = is_rgba_ ? PixelFormat::PIX_FMT_RGBA16U : PixelFormat::PIX_FMT_RGB16U;
  } else if (spec.format == OIIO::TypeDesc::HALF) {
    pix_fmt_ = is_rgba_ ? PixelFormat::PIX_FMT_RGBA16F : PixelFormat::PIX_FMT_RGB16F;
  } else if (spec.format == OIIO::TypeDesc::FLOAT) {
    pix_fmt_ = is_rgba_ ? PixelFormat::PIX_FMT_RGBA32F : PixelFormat::PIX_FMT_RGB32F;
  } else {
    qWarning() << "Failed to convert OIIO::ImageDesc to native pixel format";
    return false;
  }

  // FIXME: Many OIIO pixel formats are not handled here
  type_ = PixelFormat::GetOIIOTypeDesc(pix_fmt_);

#if OIIO_VERSION < 20100
  buffer_ = new OIIO::ImageBuf(OIIO::ImageSpec(spec.width, spec.height, spec.nchannels, type_));
#else
  buffer_ = new OIIO::ImageBuf(OIIO::ImageSpec(spec.width, spec.height, spec.nchannels, type_), OIIO::InitializePixels::No);
#endif
  image_->read_image(type_, buffer_->localpixels());

  open_ = true;

  return true;
}

Decoder::RetrieveState OIIODecoder::GetRetrieveState(const rational &time)
{
  QMutexLocker locker(&mutex_);

  if (!open_) {
    return kFailedToOpen;
  }

  return kReady;
}

FramePtr OIIODecoder::RetrieveVideo(const rational &timecode, const int& divider)
{
  QMutexLocker locker(&mutex_);

  if (!open_) {
    return nullptr;
  }

  FramePtr frame = Frame::Create();

  frame->set_width(width_ / divider);
  frame->set_height(height_ / divider);
  frame->set_format(pix_fmt_);
  frame->allocate();

  OIIO::ImageBuf dst(OIIO::ImageSpec(frame->width(), frame->height(), buffer_->spec().nchannels, buffer_->spec().format), frame->data());

  if (!OIIO::ImageBufAlgo::resize(dst, *buffer_)) {
    qWarning() << "OIIO resize failed";
  }

  return frame;
}

void OIIODecoder::Close()
{
  QMutexLocker locker(&mutex_);

  if (image_) {
    image_->close();
#if OIIO_VERSION < 10903
    OIIO::ImageInput::destroy(image_);
#endif
    image_ = nullptr;
  }

  delete buffer_;
  buffer_ = nullptr;
}

bool OIIODecoder::SupportsVideo()
{
  return true;
}

QString OIIODecoder::GetIndexFilename()
{
  return QString();
}
