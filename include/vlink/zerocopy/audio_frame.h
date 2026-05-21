/*
 * Copyright (C) 2026 by Thun Lu. All rights reserved.
 * Author: Thun Lu <thun.lu@zohomail.cn>
 * Repo:   https://github.com/thun-res/vlink
 *  _    __   __      _           __
 * | |  / /  / /     (_) ____    / /__
 * | | / /  / /     / / / __ \  / //_/
 * | |/ /  / /___  / / / / / / / ,<
 * |___/  /_____/ /_/ /_/ /_/ /_/|_|
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file audio_frame.h
 * @brief Zero-copy audio frame container for VLink transport.
 *
 * @details
 * @c AudioFrame carries one audio frame -- uncompressed PCM or compressed
 * codec output -- together with a @c Header for sequencing and timestamping.
 * It is the canonical conduit for microphone capture, text-to-speech output,
 * voice-command pipelines, and in-vehicle infotainment audio streams.  The
 * struct is exactly 128 bytes on 64-bit platforms.
 *
 * @par Sample formats
 * | Value         | Description                          |
 * | ------------- | ------------------------------------ |
 * | kFormatPcmS16 | Signed 16-bit PCM                    |
 * | kFormatPcmS24 | Signed 24-bit PCM (packed in 3 bytes)|
 * | kFormatPcmS32 | Signed 32-bit PCM                    |
 * | kFormatPcmF32 | IEEE-754 single-precision PCM        |
 * | kFormatPcmU8  | Unsigned 8-bit PCM                   |
 * | kFormatOpus   | Opus-encoded                         |
 * | kFormatAac    | AAC-encoded                          |
 * | kFormatMp3    | MP3-encoded                          |
 * | kFormatFlac   | FLAC-encoded                         |
 *
 * @par Channel layouts
 * | Value             | Description                                |
 * | ----------------- | ------------------------------------------ |
 * | kLayoutInterleaved| Channels interleaved per sample            |
 * | kLayoutPlanar     | Channels stored in separate plane regions  |
 *
 * @par Binary wire format
 * @code
 * [ magic_begin (4) | AudioFrame struct (128) | audio bytes (N) | magic_end (4) ]
 * @endcode
 * The struct block is a raw snapshot of the 64-bit ABI layout used by this
 * library; receivers must parse it through @c operator<< and must not treat
 * embedded pointer/ownership fields as portable wire values.
 *
 * @par Usage
 * @code
 * vlink::zerocopy::AudioFrame frame;
 * frame.set_sample_rate(48000);
 * frame.set_num_channels(2);
 * frame.set_num_samples(960);
 * frame.set_bit_depth(16);
 * frame.set_format(vlink::zerocopy::AudioFrame::kFormatPcmS16);
 * frame.set_layout(vlink::zerocopy::AudioFrame::kLayoutInterleaved);
 * frame.set_codec("PCM");
 * frame.create(960 * 2 * sizeof(int16_t));
 * @endcode
 *
 * @note
 * - 32-bit architectures emit a compile-time warning and are not supported.
 * - After @c operator<<, the data pointer references memory inside the
 *   source @c Bytes.  The @c Bytes must outlive the @c AudioFrame.
 * - @c fill_data is an alias for @c deep_copy(uint8_t*, size_t).
 */

#pragma once

#include <cstdint>
#include <string_view>

#include "../base/bytes.h"
#include "./header.h"

namespace vlink {

namespace zerocopy {

/**
 * @struct AudioFrame
 * @brief Zero-copy audio frame with sample-format metadata and Header.
 *
 * @details
 * Carries one complete audio frame (raw PCM or compressed codec payload)
 * along with sample rate, channel count, bit depth, codec hint, language
 * tag, capture timestamp, and frame duration.  The struct size is fixed at
 * 128 bytes on 64-bit targets with a 5-byte reserved tail.
 */
struct VLINK_EXPORT_AND_ALIGNED(8) AudioFrame final {
  /**
   * @brief Sample / codec encoding format of the audio payload.
   */
  enum Format : uint8_t {
    kFormatUnknown = 0,  ///< Unknown or uninitialised format.

    kFormatPcmS16 = 1,  ///< Signed 16-bit linear PCM.
    kFormatPcmS24 = 2,  ///< Signed 24-bit linear PCM (3 bytes per sample).
    kFormatPcmS32 = 3,  ///< Signed 32-bit linear PCM.
    kFormatPcmF32 = 4,  ///< IEEE-754 single-precision linear PCM.
    kFormatPcmU8 = 5,   ///< Unsigned 8-bit linear PCM.

    kFormatOpus = 100,  ///< Opus-encoded payload.
    kFormatAac = 101,   ///< AAC-encoded payload.
    kFormatMp3 = 102,   ///< MP3-encoded payload.
    kFormatFlac = 103,  ///< FLAC-encoded payload.
  };

  /**
   * @brief Channel data layout.
   */
  enum Layout : uint8_t {
    kLayoutUnknown = 0,      ///< Unknown / uninitialised layout.
    kLayoutInterleaved = 1,  ///< Samples interleaved across channels (L,R,L,R,...).
    kLayoutPlanar = 2,       ///< Channels stored in separate contiguous planes.
  };

  /**
   * @brief Default constructor.
   *
   * @details
   * Verifies via @c static_assert that the struct is exactly 128 bytes on
   * 64-bit platforms.  32-bit architectures emit a compile-time warning.
   */
  AudioFrame() noexcept;

  /**
   * @brief Destructor -- frees the owned audio buffer if @c is_owner() is @c true.
   */
  ~AudioFrame() noexcept;

  /**
   * @brief Copy constructor -- performs a deep copy of @p target.
   */
  AudioFrame(const AudioFrame& target) noexcept;

  /**
   * @brief Move constructor -- transfers ownership from @p target.
   */
  AudioFrame(AudioFrame&& target) noexcept;

  /**
   * @brief Copy-assignment operator -- deep-copies @p target.
   */
  AudioFrame& operator=(const AudioFrame& target) noexcept;

  /**
   * @brief Move-assignment operator -- transfers ownership from @p target.
   */
  AudioFrame& operator=(AudioFrame&& target) noexcept;

  /**
   * @brief Deserialises an @c AudioFrame from a @c Bytes wire buffer (zero-copy).
   */
  bool operator<<(const Bytes& bytes) noexcept;

  /**
   * @brief Serialises this @c AudioFrame into a @c Bytes wire buffer.
   */
  bool operator>>(Bytes& bytes) const noexcept;

  /**
   * @brief Checks whether @p bytes contains a valid @c AudioFrame wire buffer.
   */
  [[nodiscard]] static bool check_valid(const Bytes& bytes) noexcept;

  /**
   * @brief Returns the total serialised byte count for this frame.
   */
  [[nodiscard]] size_t get_serialized_size() const noexcept;

  /**
   * @brief Returns @c true when the audio buffer is present and non-empty.
   */
  [[nodiscard]] bool is_valid() const noexcept;

  /**
   * @brief Borrows the audio buffer from @p target without copying.
   */
  bool shallow_copy(const AudioFrame& target) noexcept;

  /**
   * @brief Deep-copies the audio buffer from @p target.
   */
  bool deep_copy(const AudioFrame& target) noexcept;

  /**
   * @brief Transfers ownership from @p target.
   */
  bool move_copy(AudioFrame& target) noexcept;

  /**
   * @brief Allocates an owned audio buffer of @p size bytes.
   */
  bool create(size_t size) noexcept;

  /**
   * @brief Releases owned resources and resets metadata and @c header.
   */
  void clear() noexcept;

  /**
   * @brief Borrows an external raw audio pointer without copying.
   */
  bool shallow_copy(uint8_t* data, size_t size) noexcept;

  /**
   * @brief Deep-copies audio data from a raw pointer.
   */
  bool deep_copy(uint8_t* data, size_t size) noexcept;

  /**
   * @brief Alias for @c deep_copy(uint8_t*, size_t).
   */
  bool fill_data(uint8_t* data, size_t size) noexcept;

  /**
   * @brief Returns the capture state timestamp in nanoseconds since epoch.
   */
  [[nodiscard]] uint64_t update_time_ns() const noexcept;

  /**
   * @brief Returns the frame duration in nanoseconds.
   */
  [[nodiscard]] uint64_t duration_ns() const noexcept;

  /**
   * @brief Returns the codec name (e.g. @c "PCM", @c "OPUS", @c "AAC").
   */
  [[nodiscard]] std::string_view codec() const noexcept;

  /**
   * @brief Returns the language tag (e.g. @c "en", @c "zh") for STT context.
   */
  [[nodiscard]] std::string_view language() const noexcept;

  /**
   * @brief Returns the microphone / sensor channel identifier.
   */
  [[nodiscard]] uint32_t channel() const noexcept;

  /**
   * @brief Returns the nominal publish frequency in Hz.
   */
  [[nodiscard]] uint32_t freq() const noexcept;

  /**
   * @brief Returns the sample rate in Hz.
   */
  [[nodiscard]] uint32_t sample_rate() const noexcept;

  /**
   * @brief Returns the number of samples per channel.
   */
  [[nodiscard]] uint32_t num_samples() const noexcept;

  /**
   * @brief Returns the compressed bitrate in bps (0 if uncompressed).
   */
  [[nodiscard]] uint32_t bitrate() const noexcept;

  /**
   * @brief Returns the channel count (1 = mono, 2 = stereo, ...).
   */
  [[nodiscard]] uint16_t num_channels() const noexcept;

  /**
   * @brief Returns the bit depth per PCM sample (16, 24, 32...).
   */
  [[nodiscard]] uint16_t bit_depth() const noexcept;

  /**
   * @brief Returns the sample / codec encoding format.
   */
  [[nodiscard]] Format format() const noexcept;

  /**
   * @brief Returns the channel data layout.
   */
  [[nodiscard]] Layout layout() const noexcept;

  /**
   * @brief Returns a read-only pointer to the audio buffer.
   */
  [[nodiscard]] const uint8_t* data() const noexcept;

  /**
   * @brief Returns the audio buffer size in bytes.
   */
  [[nodiscard]] size_t size() const noexcept;

  /**
   * @brief Returns @c true if this object owns its audio buffer.
   */
  [[nodiscard]] bool is_owner() const noexcept;

  /**
   * @brief Sets the capture state timestamp in nanoseconds since epoch.
   */
  void set_update_time_ns(uint64_t update_time_ns) noexcept;

  /**
   * @brief Sets the frame duration in nanoseconds.
   */
  void set_duration_ns(uint64_t duration_ns) noexcept;

  /**
   * @brief Sets the codec name (truncated to fit @c sizeof(codec) - 1).
   */
  void set_codec(std::string_view codec) noexcept;

  /**
   * @brief Sets the language tag (truncated to fit @c sizeof(language) - 1).
   */
  void set_language(std::string_view language) noexcept;

  /**
   * @brief Sets the microphone / sensor channel identifier.
   */
  void set_channel(uint32_t channel) noexcept;

  /**
   * @brief Sets the nominal publish frequency in Hz.
   */
  void set_freq(uint32_t freq) noexcept;

  /**
   * @brief Sets the sample rate in Hz.
   */
  void set_sample_rate(uint32_t sample_rate) noexcept;

  /**
   * @brief Sets the number of samples per channel.
   */
  void set_num_samples(uint32_t num_samples) noexcept;

  /**
   * @brief Sets the compressed bitrate in bps (0 if uncompressed).
   */
  void set_bitrate(uint32_t bitrate) noexcept;

  /**
   * @brief Sets the channel count.
   */
  void set_num_channels(uint16_t num_channels) noexcept;

  /**
   * @brief Sets the bit depth per PCM sample.
   */
  void set_bit_depth(uint16_t bit_depth) noexcept;

  /**
   * @brief Sets the sample / codec encoding format.
   */
  void set_format(Format format) noexcept;

  /**
   * @brief Sets the channel data layout.
   */
  void set_layout(Layout layout) noexcept;

  /**
   * @brief Gets the reserved field.
   */
  uint32_t& get_reserved() noexcept { return reserved32_; }

  Header header;  ///< Sequencing and timestamp metadata for this frame.

  static constexpr bool kZerocopyTypes{true};  ///< Internal marker for VLink zero-copy type traits.

 private:
  uint8_t* data_{nullptr};
  size_t size_{0};
  uint64_t update_time_ns_{0};
  uint64_t duration_ns_{0};
  char codec_[16]{0};
  char language_[8]{0};
  uint32_t channel_{0};
  uint32_t freq_{0};
  uint32_t sample_rate_{0};
  uint32_t num_samples_{0};
  uint32_t bitrate_{0};
  uint16_t num_channels_{0};
  uint16_t bit_depth_{0};
  Format format_{kFormatUnknown};
  Layout layout_{kLayoutUnknown};
  bool is_owner_{false};
  uint8_t reserved8_{0};
  uint32_t reserved32_{0};

  static constexpr uint32_t kMagicNumberBegin{0x98B7F1AA};
  static constexpr uint32_t kMagicNumberEnd{0x98B7F1AF};
};

}  // namespace zerocopy

}  // namespace vlink
