/*
* PROJECT:         Aspia Remote Desktop
* FILE:            codec/video_encoder_vp8.h
* LICENSE:         See top-level directory
* PROGRAMMERS:     Dmitry Chapyshev (dmitry@aspia.ru)
*/

#ifndef _ASPIA_CODEC__VIDEO_ENCODER_VP8_H
#define _ASPIA_CODEC__VIDEO_ENCODER_VP8_H

#include "aspia_config.h"

#include <memory>

#define VPX_CODEC_DISABLE_COMPAT 1
#include "vpx/vpx_encoder.h"
#include "vpx/vp8cx.h"

#include "codec/scoped_vpx_codec.h"
#include "codec/video_encoder.h"
#include "base/macros.h"

class VideoEncoderVP8 : public VideoEncoder
{
public:
    VideoEncoderVP8();
    virtual ~VideoEncoderVP8() override;

    virtual void Resize(const DesktopSize &screen_size,
                        const PixelFormat &host_pixel_format,
                        const PixelFormat &client_pixel_format) override;

    virtual Status Encode(proto::VideoPacket *packet,
                          const uint8_t *screen_buffer,
                          const DesktopRegion &changed_region) override;

private:
    void CreateImage();
    void CreateActiveMap();
    void SetCommonCodecParameters(vpx_codec_enc_cfg_t *config);
    void CreateCodec();
    void PrepareImageAndActiveMap(const DesktopRegion &changed_region,
                                  const uint8_t *buffer,
                                  proto::VideoPacket *packet);

private:
    bool size_changed_;

    // The current frame size.
    DesktopSize screen_size_;

    ScopedVpxCodec codec_;
    vpx_image_t image_;

    int active_map_size_;

    vpx_active_map_t active_map_;
    std::unique_ptr<uint8_t[]> active_map_buffer_;

    int bytes_per_row_;
    int bytes_per_pixel_;

    // Buffer for storing the yuv image.
    std::unique_ptr<uint8_t[]> yuv_image_;

    DISALLOW_COPY_AND_ASSIGN(VideoEncoderVP8);
};

#endif // _ASPIA_CODEC___VIDEO_ENCODER_VP8_H
