/*
* PROJECT:         Aspia Remote Desktop
* FILE:            codec/video_encoder_zlib.cpp
* LICENSE:         See top-level directory
* PROGRAMMERS:     Dmitry Chapyshev (dmitry@aspia.ru)
*/

#include "codec/video_encoder_zlib.h"

#include "base/logging.h"

VideoEncoderZLIB::VideoEncoderZLIB() :
    packet_flags_(proto::VideoPacket::LAST_PACKET),
    host_bytes_per_pixel_(0),
    client_bytes_per_pixel_(0),
    host_stride_(0),
    client_stride_(0),
    size_changed_(true)
{
    compressor_.reset(new CompressorZLIB());
}

VideoEncoderZLIB::~VideoEncoderZLIB()
{
    // Nothing
}

uint8_t* VideoEncoderZLIB::GetOutputBuffer(proto::VideoPacket *packet, size_t size)
{
    packet->mutable_data()->resize(size);

    return const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(packet->mutable_data()->data()));
}

void VideoEncoderZLIB::CompressRect(proto::VideoPacket *packet, const DesktopRect &rect)
{
    proto::VideoRect *video_rect = packet->add_changed_rect();

    video_rect->set_x(rect.x());
    video_rect->set_y(rect.y());
    video_rect->set_width(rect.width());
    video_rect->set_height(rect.height());

    // Делаем сброс компрессора при сжатии каждого прямоугольника
    compressor_->Reset();

    // Размер строки прямоугольника в байтах
    const int row_size = rect.width() * client_bytes_per_pixel_;

    int packet_size = row_size * rect.height();
    packet_size += packet_size / 100 + 16;

    // Получаем указатели на буферы
    const uint8_t *in = *translated_buffer_ + rect.y() * client_stride_ + rect.x() * client_bytes_per_pixel_;
    uint8_t *out = GetOutputBuffer(packet, packet_size);

    int filled = 0;   // Количество байт в буфере назначения
    int row_pos = 0;  // Position in the current row in bytes.
    int row_y = 0;    // Current row.
    bool compress_again = true;

    while (compress_again)
    {
        Compressor::CompressorFlush flush = Compressor::CompressorNoFlush;

        // Если мы достигли последней строки в прямоугольнике
        if (row_y == rect.height() - 1)
        {
            // Ставим соответствующий флаг
            flush = Compressor::CompressorFinish;
        }

        int consumed = 0; // Количество байт, которое было взято из исходного буфера
        int written = 0;  // Количество байт, которое было записано в буфер назначения

        // Сжимаем очередную порцию данных
        compress_again = compressor_->Process(in + row_pos, row_size - row_pos,
                                              out + filled, packet_size - filled,
                                              flush, &consumed, &written);

        row_pos += consumed; // Сдвигаем положение с текущей строке прямоугольника
        filled += written;   // Увеличиваем счетчик итогового размера буфера назначения

        // If we have filled the message or we have reached the end of stream.
        if (filled == packet_size || !compress_again)
        {
            packet->mutable_data()->resize(filled);
            return;
        }

        // Если мы достигли конца текущей строки в прямоугольнике и это не последняя строка
        if (row_pos == row_size && row_y < rect.height() - 1)
        {
            // Обнуляаем положение в текущей строке
            row_pos = 0;

            // Переходим к следующей строке в буфере
            in += client_stride_;

            // Увеличиваем номер текущей строки
            ++row_y;
        }
    }
}

void VideoEncoderZLIB::TranslateRect(const DesktopRect &rect, const uint8_t *src_buffer)
{
    const uint8_t *in = src_buffer + host_stride_ * rect.y() + rect.x() * host_bytes_per_pixel_;
    uint8_t *out = *translated_buffer_ + client_stride_ * rect.y() + rect.x() * client_bytes_per_pixel_;

    translator_->Translate(in, host_stride_,
                           out, client_stride_,
                           rect.width(), rect.height());
}

void VideoEncoderZLIB::Resize(const DesktopSize &screen_size,
                              const PixelFormat &host_pixel_format,
                              const PixelFormat &client_pixel_format)
{
    screen_size_ = screen_size;

    client_pixel_format_ = client_pixel_format;

    host_bytes_per_pixel_ = host_pixel_format.bytes_per_pixel();
    client_bytes_per_pixel_ = client_pixel_format.bytes_per_pixel();

    host_stride_ = screen_size_.width() * host_bytes_per_pixel_;
    client_stride_ = screen_size_.width() * client_bytes_per_pixel_;

    translator_ = SelectTranslator(host_pixel_format, client_pixel_format);

    CHECK(translator_);

    size_t buffer_size = screen_size.width() *
                         screen_size.height() *
                         client_pixel_format.bytes_per_pixel();

    translated_buffer_.reset(new ScopedAlignedBuffer(buffer_size, 32));

    memset(*translated_buffer_, 0, buffer_size);

    size_changed_ = true;
}

VideoEncoder::Status VideoEncoderZLIB::Encode(proto::VideoPacket *packet,
                                              const uint8_t *screen_buffer,
                                              const DesktopRegion &changed_region)
{
    if (packet_flags_ & proto::VideoPacket::LAST_PACKET)
    {
        packet_flags_ = proto::VideoPacket::FIRST_PACKET;

        rect_iterator.reset(new DesktopRegion::Iterator(changed_region));

        proto::VideoPacketFormat *format = packet->mutable_format();

        // Заполняем кодировку
        format->set_encoding(proto::VIDEO_ENCODING_ZLIB);

        if (size_changed_)
        {
            // Размеры экрана
            proto::VideoSize *size = format->mutable_screen_size();

            size->set_width(screen_size_.width());
            size->set_height(screen_size_.height());

            // Формат пикселей
            proto::VideoPixelFormat *pixel_format = format->mutable_pixel_format();

            pixel_format->set_bits_per_pixel(client_pixel_format_.bits_per_pixel());

            pixel_format->set_red_max(client_pixel_format_.red_max());
            pixel_format->set_green_max(client_pixel_format_.green_max());
            pixel_format->set_blue_max(client_pixel_format_.blue_max());

            pixel_format->set_red_shift(client_pixel_format_.red_shift());
            pixel_format->set_green_shift(client_pixel_format_.green_shift());
            pixel_format->set_blue_shift(client_pixel_format_.blue_shift());

            size_changed_ = false;
        }
    }
    else
    {
        packet_flags_ = proto::VideoPacket::PARTITION_PACKET;
    }

    const DesktopRect &rect = rect_iterator->rect();

    // Translate rect to client pixel format
    TranslateRect(rect, screen_buffer);

    // Compress rect with using ZLIB compressor
    CompressRect(packet, rect);

    // Перемещаем итератор в следующее положение
    rect_iterator->Advance();

    // Если после перемещения мы находимся в конце списка
    if (rect_iterator->IsAtEnd())
    {
        // Добавляем флаг последнего пакета
        packet_flags_ |= proto::VideoPacket::LAST_PACKET;
    }

    packet->set_flags(packet_flags_);

    return (packet_flags_ & proto::VideoPacket::LAST_PACKET) ? Status::End : Status::Next;
}
