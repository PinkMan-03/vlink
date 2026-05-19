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
 * @file status_detail.h
 * @brief Concrete DDS-compatible status event structs with counter and handle fields.
 *
 * @details
 * This file defines the ten concrete status structs derived from @c Status::Base.
 * Each struct carries the specific counter and handle fields reported by the DDS middleware
 * for that event type.
 *
 * Writer-side (Publisher / Server / Setter):
 * - @c PublicationMatched       -- matched subscriber count changed
 * - @c OfferedDeadlineMissed    -- writer missed its offered publication deadline
 * - @c OfferedIncompatibleQos   -- incompatible QoS subscriber detected
 * - @c LivelinessLost           -- writer failed to assert liveliness
 *
 * Reader-side (Subscriber / Client / Getter):
 * - @c SubscriptionMatched      -- matched publisher count changed
 * - @c RequestedDeadlineMissed  -- reader did not receive within its requested deadline
 * - @c LivelinessChanged        -- publisher liveliness state changed
 * - @c SampleRejected           -- sample dropped due to resource limit
 * - @c RequestedIncompatibleQos -- incompatible QoS publisher detected
 * - @c SampleLost               -- sample lost before delivery
 *
 * @par Accessing status fields
 * @code
 * sub->register_status_callback([](vlink::Status::BasePtr status) {
 *     if (status->get_type() == vlink::Status::kSampleRejected) {
 *         auto rej = status->as<vlink::Status::SampleRejected>();
 *         if (rej->last_reason == vlink::Status::SampleRejected::kRejectedBySamplesLimit) {
 *             VLOG_W("Sample rejected: limit exceeded");
 *         }
 *     }
 * });
 * @endcode
 */

#pragma once

#include <cstdint>
#include <string>

#include "./status.h"

namespace vlink {

namespace Status {

/* ================== For writer ================== */

/**
 * @struct PublicationMatched
 * @brief Status event fired when a DataWriter gains or loses a matching DataReader.
 *
 * @details
 * Delivered to the writer's status callback whenever a subscriber that matches
 * the topic name and QoS policy appears or disappears.
 */
struct VLINK_EXPORT PublicationMatched final : public Base {
 public:
  /**
   * @brief Returns @c kPublicationMatched.
   *
   * @return Status type identifier.
   */
  [[nodiscard]] Type get_type() const override;

  /**
   * @brief Returns the status name string.
   *
   * @return @c "PublicationMatched".
   */
  [[nodiscard]] std::string get_string() const override;

  int32_t total_count{0};                            ///< Cumulative number of readers ever matched.
  int32_t total_count_change{0};                     ///< Change in total_count since last callback.
  int32_t current_count{0};                          ///< Number of readers currently matched.
  int32_t current_count_change{0};                   ///< Change in current_count since last callback.
  InstanceHandle last_subscription_handle{nullptr};  ///< Handle of the most recently matched reader.

  /**
   * @brief Writes the status fields to @p ostream.
   *
   * @param ostream  Output stream.
   * @param status   This @c PublicationMatched status.
   * @return Reference to @p ostream.
   */
  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& ostream, const PublicationMatched& status) noexcept;
};

/**
 * @struct OfferedDeadlineMissed
 * @brief Status event fired when a DataWriter fails to publish within its offered deadline period.
 *
 * @details
 * Delivered once per instance that missed the deadline.  @c last_instance_handle
 * identifies the instance that most recently missed its deadline.
 */
struct VLINK_EXPORT OfferedDeadlineMissed final : public Base {
 public:
  /**
   * @brief Returns @c kOfferedDeadlineMissed.
   *
   * @return Status type identifier.
   */
  [[nodiscard]] Type get_type() const override;

  /**
   * @brief Returns the status name string.
   *
   * @return @c "OfferedDeadlineMissed".
   */
  [[nodiscard]] std::string get_string() const override;

  int32_t total_count{0};                        ///< Total deadline misses across all instances.
  int32_t total_count_change{0};                 ///< Change in total_count since last callback.
  InstanceHandle last_instance_handle{nullptr};  ///< Handle of the most recently missed instance.

  /**
   * @brief Writes the status fields to @p ostream.
   *
   * @param ostream  Output stream.
   * @param status   This @c OfferedDeadlineMissed status.
   * @return Reference to @p ostream.
   */
  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& ostream, const OfferedDeadlineMissed& status) noexcept;
};

/**
 * @struct OfferedIncompatibleQos
 * @brief Status event fired when a DataWriter discovers a subscriber with incompatible QoS.
 *
 * @details
 * @c last_policy_id identifies the QoS policy ID that caused the incompatibility.
 */
struct VLINK_EXPORT OfferedIncompatibleQos final : public Base {
 public:
  /**
   * @brief Returns @c kOfferedIncompatibleQos.
   *
   * @return Status type identifier.
   */
  [[nodiscard]] Type get_type() const override;

  /**
   * @brief Returns the status name string.
   *
   * @return @c "OfferedIncompatibleQos".
   */
  [[nodiscard]] std::string get_string() const override;

  int32_t total_count{0};         ///< Total incompatible subscribers ever detected.
  int32_t total_count_change{0};  ///< Change in total_count since last callback.
  int32_t last_policy_id{0};      ///< ID of the QoS policy that caused the last incompatibility.

  /**
   * @brief Writes the status fields to @p ostream.
   *
   * @param ostream  Output stream.
   * @param status   This @c OfferedIncompatibleQos status.
   * @return Reference to @p ostream.
   */
  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& ostream, const OfferedIncompatibleQos& status) noexcept;
};

/**
 * @struct LivelinessLost
 * @brief Status event fired when a DataWriter loses liveliness (failed to assert within duration).
 *
 * @details
 * Delivered to the writer when the liveliness lease expires without a successful assertion.
 */
struct VLINK_EXPORT LivelinessLost final : public Base {
 public:
  /**
   * @brief Returns @c kLivelinessLost.
   *
   * @return Status type identifier.
   */
  [[nodiscard]] Type get_type() const override;

  /**
   * @brief Returns the status name string.
   *
   * @return @c "LivelinessLost".
   */
  [[nodiscard]] std::string get_string() const override;

  int32_t total_count{0};         ///< Total times liveliness was lost.
  int32_t total_count_change{0};  ///< Change in total_count since last callback.

  /**
   * @brief Writes the status fields to @p ostream.
   *
   * @param ostream  Output stream.
   * @param status   This @c LivelinessLost status.
   * @return Reference to @p ostream.
   */
  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& ostream, const LivelinessLost& status) noexcept;
};

/* ================== For reader ================== */

/**
 * @struct SubscriptionMatched
 * @brief Status event fired when a DataReader gains or loses a matching DataWriter.
 *
 * @details
 * Delivered to the reader's status callback whenever a publisher that matches
 * the topic name and QoS policy appears or disappears.
 */
struct VLINK_EXPORT SubscriptionMatched final : public Base {
 public:
  /**
   * @brief Returns @c kSubscriptionMatched.
   *
   * @return Status type identifier.
   */
  [[nodiscard]] Type get_type() const override;

  /**
   * @brief Returns the status name string.
   *
   * @return @c "SubscriptionMatched".
   */
  [[nodiscard]] std::string get_string() const override;

  int32_t total_count{0};                           ///< Cumulative number of writers ever matched.
  int32_t total_count_change{0};                    ///< Change in total_count since last callback.
  int32_t current_count{0};                         ///< Number of writers currently matched.
  int32_t current_count_change{0};                  ///< Change in current_count since last callback.
  InstanceHandle last_publication_handle{nullptr};  ///< Handle of the most recently matched writer.

  /**
   * @brief Writes the status fields to @p ostream.
   *
   * @param ostream  Output stream.
   * @param status   This @c SubscriptionMatched status.
   * @return Reference to @p ostream.
   */
  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& ostream, const SubscriptionMatched& status) noexcept;
};

/**
 * @struct RequestedDeadlineMissed
 * @brief Status event fired when a DataReader does not receive data within its requested deadline.
 *
 * @details
 * @c last_instance_handle identifies the data instance whose deadline was most recently missed.
 */
struct VLINK_EXPORT RequestedDeadlineMissed final : public Base {
 public:
  /**
   * @brief Returns @c kRequestedDeadlineMissed.
   *
   * @return Status type identifier.
   */
  [[nodiscard]] Type get_type() const override;

  /**
   * @brief Returns the status name string.
   *
   * @return @c "RequestedDeadlineMissed".
   */
  [[nodiscard]] std::string get_string() const override;

  int32_t total_count{0};                        ///< Total deadline misses across all instances.
  int32_t total_count_change{0};                 ///< Change in total_count since last callback.
  InstanceHandle last_instance_handle{nullptr};  ///< Handle of the most recently missed instance.

  /**
   * @brief Writes the status fields to @p ostream.
   *
   * @param ostream  Output stream.
   * @param status   This @c RequestedDeadlineMissed status.
   * @return Reference to @p ostream.
   */
  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& ostream, const RequestedDeadlineMissed& status) noexcept;
};

/**
 * @struct LivelinessChanged
 * @brief Status event fired when the liveliness state of a matched DataWriter changes.
 *
 * @details
 * Tracks how many matched publishers are alive versus not alive.  The @c last_publication_handle
 * identifies the writer whose liveliness state most recently changed.
 */
struct VLINK_EXPORT LivelinessChanged final : public Base {
 public:
  /**
   * @brief Returns @c kLivelinessChanged.
   *
   * @return Status type identifier.
   */
  [[nodiscard]] Type get_type() const override;

  /**
   * @brief Returns the status name string.
   *
   * @return @c "LivelinessChanged".
   */
  [[nodiscard]] std::string get_string() const override;

  int32_t alive_count{0};                           ///< Number of matched writers that are currently alive.
  int32_t not_alive_count{0};                       ///< Number of matched writers that are not alive.
  int32_t alive_count_change{0};                    ///< Change in alive_count since last callback.
  int32_t not_alive_count_change{0};                ///< Change in not_alive_count since last callback.
  InstanceHandle last_publication_handle{nullptr};  ///< Handle of the writer that most recently changed.

  /**
   * @brief Writes the status fields to @p ostream.
   *
   * @param ostream  Output stream.
   * @param status   This @c LivelinessChanged status.
   * @return Reference to @p ostream.
   */
  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& ostream, const LivelinessChanged& status) noexcept;
};

/**
 * @struct SampleRejected
 * @brief Status event fired when an incoming sample is rejected due to a resource limit.
 *
 * @details
 * The @c Kind enum identifies which resource limit caused the rejection.
 * @c last_reason and @c last_instance_handle describe the most recent rejection.
 */
struct VLINK_EXPORT SampleRejected final : public Base {
 public:
  /**
   * @brief Reason codes for sample rejection.
   *
   * | Kind                             | Limit exceeded                      |
   * | -------------------------------- | ----------------------------------- |
   * | kNotRejected                     | Sample was not rejected             |
   * | kRejectedByInstancesLimit        | Max instances limit reached         |
   * | kRejectedBySamplesLimit          | Max total samples limit reached     |
   * | kRejectedBySamplesPerInstanceLimit | Max samples per instance reached  |
   */
  enum Kind : uint8_t {
    kNotRejected = 0,                       ///< No rejection.
    kRejectedByInstancesLimit = 1,          ///< Max instances limit exceeded.
    kRejectedBySamplesLimit = 2,            ///< Max total samples limit exceeded.
    kRejectedBySamplesPerInstanceLimit = 3  ///< Max samples per instance limit exceeded.
  };

  /**
   * @brief Returns @c kSampleRejected.
   *
   * @return Status type identifier.
   */
  [[nodiscard]] Type get_type() const override;

  /**
   * @brief Returns the status name string.
   *
   * @return @c "SampleRejected".
   */
  [[nodiscard]] std::string get_string() const override;

  int32_t total_count{0};                        ///< Total number of samples rejected.
  int32_t total_count_change{0};                 ///< Change in total_count since last callback.
  Kind last_reason{kNotRejected};                ///< Reason for the most recent rejection.
  InstanceHandle last_instance_handle{nullptr};  ///< Handle of the instance that was most recently rejected.

  /**
   * @brief Writes the status fields to @p ostream.
   *
   * @param ostream  Output stream.
   * @param status   This @c SampleRejected status.
   * @return Reference to @p ostream.
   */
  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& ostream, const SampleRejected& status) noexcept;
};

/**
 * @struct RequestedIncompatibleQos
 * @brief Status event fired when a DataReader discovers a publisher with incompatible QoS.
 *
 * @details
 * @c last_policy_id identifies the QoS policy ID that caused the incompatibility.
 */
struct VLINK_EXPORT RequestedIncompatibleQos final : public Base {
 public:
  /**
   * @brief Returns @c kRequestedIncompatibleQos.
   *
   * @return Status type identifier.
   */
  [[nodiscard]] Type get_type() const override;

  /**
   * @brief Returns the status name string.
   *
   * @return @c "RequestedIncompatibleQos".
   */
  [[nodiscard]] std::string get_string() const override;

  int32_t total_count{0};         ///< Total incompatible publishers ever detected.
  int32_t total_count_change{0};  ///< Change in total_count since last callback.
  int32_t last_policy_id{0};      ///< ID of the QoS policy that caused the last incompatibility.

  /**
   * @brief Writes the status fields to @p ostream.
   *
   * @param ostream  Output stream.
   * @param status   This @c RequestedIncompatibleQos status.
   * @return Reference to @p ostream.
   */
  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& ostream, const RequestedIncompatibleQos& status) noexcept;
};

/**
 * @struct SampleLost
 * @brief Status event fired when a sample is lost before it can be delivered to the DataReader.
 *
 * @details
 * Sample loss typically occurs when a publisher produces data faster than the subscriber
 * consumes it and the history depth is exceeded.
 */
struct VLINK_EXPORT SampleLost final : public Base {
 public:
  /**
   * @brief Returns @c kSampleLost.
   *
   * @return Status type identifier.
   */
  [[nodiscard]] Type get_type() const override;

  /**
   * @brief Returns the status name string.
   *
   * @return @c "SampleLost".
   */
  [[nodiscard]] std::string get_string() const override;

  int32_t total_count{0};         ///< Total samples ever lost.
  int32_t total_count_change{0};  ///< Change in total_count since last callback.

  /**
   * @brief Writes the status fields to @p ostream.
   *
   * @param ostream  Output stream.
   * @param status   This @c SampleLost status.
   * @return Reference to @p ostream.
   */
  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& ostream, const SampleLost& status) noexcept;
};

}  // namespace Status

}  // namespace vlink
