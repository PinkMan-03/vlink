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
 * @file vlink.h
 * @brief Umbrella header that exposes the complete VLink public communication API.
 *
 * @details
 * Including this single header is sufficient to access all six communication
 * primitives together with the message-loop dispatcher and common utility helpers.
 * No other VLink headers need to be included directly.
 *
 * @par Included APIs
 * | Header                  | Primitive(s)                    | Communication Model       |
 * | ----------------------- | ------------------------------- | ------------------------- |
 * | @c client.h             | @c Client\<ReqT,RespT\>        | Method -- caller side      |
 * | @c server.h             | @c Server\<ReqT,RespT\>        | Method -- handler side     |
 * | @c publisher.h          | @c Publisher\<MsgT\>            | Event -- message emitter   |
 * | @c subscriber.h         | @c Subscriber\<MsgT\>           | Event -- message receiver  |
 * | @c getter.h             | @c Getter\<ValueT\>             | Field -- value reader      |
 * | @c setter.h             | @c Setter\<ValueT\>             | Field -- value writer      |
 * | @c base/message_loop.h  | @c MessageLoop                  | Callback dispatcher       |
 * | @c base/utils.h         | utility functions               | General helpers           |
 *
 * @par Transport Back-end Selection
 * Switch the underlying transport by changing only the transport prefix in the URL.
 * All API calls remain identical:
 *
 * @code
 * Publisher<MyMsg> pub("dds://vehicle/speed");   // DDS transport
 * Publisher<MyMsg> pub("shm://vehicle/speed");   // shared-memory transport
 * Publisher<MyMsg> pub("zenoh://vehicle/speed"); // Zenoh transport
 * @endcode
 *
 * @verbatim
 *                 .-~~~~~~~~~-._       _.-~~~~~~~~~-.
 *             __.'              ~.   .~              `.__
 *           .'//                  \./                  \\`.
 *         .'//                     |                     \\`.
 *       .'// .-~"""""""~~~~-._     |     _,-~~~~"""""""~-. \\`.
 *     .'//.-"                 `-.  |  .-'                 "-.\\`.
 *   .'//______.============-..   \ | /   ..-============.______\\`.
 * .'______________________________\|/______________________________`.
 * @endverbatim
 *
 * @par Quick-start Example
 * @code
 * #include <vlink/vlink.h>
 * using namespace vlink;
 *
 * // --- Event model ---
 * Subscriber<MyMsg> sub("dds://my_topic");
 * sub.listen([](const MyMsg& msg) { ... });
 *
 * Publisher<MyMsg> pub("dds://my_topic");
 * pub.publish(MyMsg{});
 *
 * // --- Method model (synchronous) ---
 * Server<Req, Resp> server("dds://my_service");
 * server.listen([](const Req& req, Resp& resp) { resp = ...; });
 *
 * Client<Req, Resp> client("dds://my_service");
 * Resp resp;
 * client.invoke(Req{}, resp);
 *
 * // --- Field model ---
 * Setter<int> setter("shm://my_field");
 * setter.set(42);
 *
 * Getter<int> getter("shm://my_field");
 * auto val = getter.get();  // returns std::optional<int>
 * @endcode
 *
 * @note To enable message security, replace the type with the security variant:
 *       @c SecurityPublisher\<T\>, @c SecuritySubscriber\<T\>, @c SecurityClient\<Req,Resp\>,
 *       @c SecurityServer\<Req,Resp\>, @c SecurityGetter\<T\>, @c SecuritySetter\<T\>.
 */

#pragma once

// NOLINTBEGIN

// method
#include "./client.h"
#include "./server.h"

// event
#include "./publisher.h"
#include "./subscriber.h"

// field
#include "./getter.h"
#include "./setter.h"

// message_loop
#include "./base/message_loop.h"

// utils
#include "./base/utils.h"

// NOLINTEND
