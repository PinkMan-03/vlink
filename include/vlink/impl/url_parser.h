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
 * @file url_parser.h
 * @brief RFC-compliant URL/URI parser used internally by the VLink transport layer.
 *
 * @details
 * @c UrlParser decomposes a URL string into its constituent components following a
 * strict subset of RFC 3986.  It is used by @c Url to extract the URI scheme
 * (which VLink treats as the transport prefix), host, path, and query parameters
 * from VLink topic addresses such as:
 *
 * @code
 *   dds://my_domain/vehicle/speed?domain_id=1&qos=best_effort
 *   intra://my_topic
 *   someip://127.0.0.1:30490/my_service?instance_id=1
 * @endcode
 *
 * @par Supported URL Components
 * | Component  | Example                     | Description                            |
 * | ---------- | --------------------------- | -------------------------------------- |
 * | transport  | @c dds                      | URI scheme / VLink transport prefix before @c :// |
 * | content    | @c //host/path              | Full content portion after the scheme     |
 * | username   | @c user                     | Optional credential before host        |
 * | password   | @c pass                     | Optional credential after @c :         |
 * | host       | @c 127.0.0.1                | Hostname or IP address                 |
 * | port       | @c 30490                    | TCP/UDP port number                    |
 * | path       | @c /vehicle/speed           | Topic path                             |
 * | query      | @c domain_id=1&qos=...      | Raw query string after @c ?            |
 * | fragment   | @c section1                 | Fragment identifier after @c #         |
 *
 * @par Query Dictionary
 * The query string is automatically split into a @c std::map<string,string> using
 * either @c & (default) or @c ; as the key-value pair separator.  Values are
 * split on the first @c = character.
 *
 * @par Category
 * - @c kHierarchical -- standard @c scheme://authority/path?query#fragment syntax.
 * - @c kNonHierarchical -- opaque @c scheme:content syntax (e.g. @c mailto:user(at)host).
 *
 * @note @c UrlParser is a value type; it parses the URL at construction time and
 *       provides read-only accessors for each extracted component.
 */

#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "../base/macros.h"

namespace vlink {

/**
 * @class UrlParser
 * @brief Immutable RFC-3986 URL parser.
 *
 * @details
 * Parses the input URL string once at construction time.  All accessor methods
 * are @c const and return references to internally stored strings; the lifetime
 * of the returned references is tied to the lifetime of the @c UrlParser object.
 */
class VLINK_EXPORT UrlParser final {
 public:
  /**
   * @enum Category
   * @brief Distinguishes hierarchical and non-hierarchical URL forms.
   */
  enum class Category : uint8_t {
    kHierarchical = 0,     ///< Standard @c scheme://authority/path form (most VLink transports).
    kNonHierarchical = 1,  ///< Opaque @c scheme:content form (e.g. @c mailto:).
  };

  /**
   * @enum Component
   * @brief Identifies individual URL components for the components-map constructor.
   */
  enum class Component : uint8_t {
    kTransport = 0,  ///< URI scheme / VLink transport prefix (e.g. @c dds, @c intra).
    kContent = 1,    ///< Full content string after the scheme separator.
    kUsername = 2,   ///< Optional authentication username.
    kPassword = 3,   ///< Optional authentication password.
    kHost = 4,       ///< Hostname or IP address.
    kPort = 5,       ///< Port number (stored as string in the components map).
    kPath = 6,       ///< Resource path (e.g. @c /vehicle/speed).
    kQuery = 7,      ///< Raw query string (without the leading @c ?).
    kFragment = 8,   ///< Fragment identifier (without the leading @c #).
  };

  /**
   * @enum Separator
   * @brief Query-string key-value pair delimiter.
   */
  enum class Separator : uint8_t {
    kAmpersand = 0,  ///< @c & separator (default; @c key=val&key2=val2).
    kSemicolon = 1,  ///< @c ; separator (alternative; @c key=val;key2=val2).
  };

  /**
   * @brief Constructs a parser by parsing the given C-string URL.
   *
   * @param str        Null-terminated URL string to parse.
   * @param category   Hierarchical or non-hierarchical form; default hierarchical.
   * @param separator  Query key-value delimiter; default ampersand (@c &).
   */
  explicit UrlParser(const char* str, Category category = Category::kHierarchical,
                     Separator separator = Separator::kAmpersand);

  /**
   * @brief Constructs a parser by parsing the given @c std::string URL.
   *
   * @param str        URL string to parse.
   * @param category   Hierarchical or non-hierarchical form; default hierarchical.
   * @param separator  Query key-value delimiter; default ampersand (@c &).
   */
  explicit UrlParser(const std::string& str, Category category = Category::kHierarchical,
                     Separator separator = Separator::kAmpersand);

  /**
   * @brief Constructs a URL from an explicit component map.
   *
   * @details
   * Builds the internal state from a pre-decomposed set of components rather than
   * parsing a raw URL string.  Useful when constructing a modified URL from an
   * existing @c UrlParser instance.
   *
   * @param components  Map of @c Component to string value for each component present.
   * @param category    Hierarchical or non-hierarchical form.
   * @param rooted      @c true if the path begins with @c / (hierarchical URLs only).
   * @param separator   Query key-value delimiter; default ampersand.
   */
  explicit UrlParser(const std::map<Component, std::string>& components, Category category, bool rooted,
                     Separator separator = Separator::kAmpersand);

  /**
   * @brief Constructs a parser by copying @p other and overriding specific components.
   *
   * @details
   * Copies all components from @p other, then replaces those present in
   * @p replacements.  Equivalent to creating a modified copy of an existing URL.
   *
   * @param other         Source @c UrlParser to copy from.
   * @param replacements  Components to override in the copy.
   */
  explicit UrlParser(const UrlParser& other, const std::map<Component, std::string>& replacements);

  /**
   * @brief Returns the transport string parsed from the URL (e.g. @c "dds", @c "intra").
   *
   * @return Reference to the parsed transport component; empty if not present.
   */
  [[nodiscard]] const std::string& get_transport() const;

  /**
   * @brief Returns the URL category (hierarchical or non-hierarchical).
   *
   * @return The @c Category value supplied at construction.
   */
  [[nodiscard]] Category get_category() const;

  /**
   * @brief Returns the full content portion of the URL (after the scheme separator).
   *
   * @return Reference to the parsed content string; empty if not present.
   */
  [[nodiscard]] const std::string& get_content() const;

  /**
   * @brief Returns the authentication username component.
   *
   * @return Reference to the parsed username; empty if not present.
   */
  [[nodiscard]] const std::string& get_username() const;

  /**
   * @brief Returns the authentication password component.
   *
   * @return Reference to the parsed password; empty if not present.
   */
  [[nodiscard]] const std::string& get_password() const;

  /**
   * @brief Returns the host component (hostname or IP address).
   *
   * @return Reference to the parsed host string; empty if not present.
   */
  [[nodiscard]] const std::string& get_host() const;

  /**
   * @brief Returns the port number, or @c 0 if no port was specified.
   *
   * @return Parsed port as @c int64_t; @c 0 if absent.
   */
  [[nodiscard]] int64_t get_port() const;

  /**
   * @brief Returns the path component of the URL.
   *
   * @return Reference to the parsed path string; empty if not present.
   */
  [[nodiscard]] const std::string& get_path() const;

  /**
   * @brief Returns the raw query string (without the leading @c ? character).
   *
   * @return Reference to the raw query; empty if no query was present.
   */
  [[nodiscard]] const std::string& get_query() const;

  /**
   * @brief Returns the parsed query string as a key-value dictionary.
   *
   * @details
   * Built by splitting the raw query on the configured @c Separator and then
   * splitting each token on the first @c = character.  Keys without a @c =
   * are stored with an empty-string value.
   *
   * @return Reference to the @c std::map<string,string> query dictionary.
   */
  [[nodiscard]] const std::map<std::string, std::string>& get_query_dictionary() const;

  /**
   * @brief Returns the fragment identifier component (without the leading @c #).
   *
   * @return Reference to the parsed fragment; empty if not present.
   */
  [[nodiscard]] const std::string& get_fragment() const;

  /**
   * @brief Reconstructs the URL as a canonical string from parsed components.
   *
   * @details
   * Re-assembles the parsed URI into a canonical string.  Hierarchical URLs are
   * emitted as @c scheme://authority/path?query#fragment, while non-hierarchical
   * URLs are emitted as @c scheme:content?query#fragment.  The output may differ
   * slightly from the original input if the input had unusual whitespace or
   * encoding.
   *
   * @return Reconstructed URL string.
   */
  [[nodiscard]] std::string to_string() const;

 private:
  void setup(const std::string& str, Category category);

  std::string::const_iterator parse_transport(const std::string& str, std::string::const_iterator transport_start);

  std::string::const_iterator parse_content(const std::string& str, std::string::const_iterator content_start);

  std::string::const_iterator parse_username(const std::string& str, const std::string& content,
                                             std::string::const_iterator username_start);

  std::string::const_iterator parse_password(const std::string& str, const std::string& content,
                                             std::string::const_iterator password_start);

  std::string::const_iterator parse_host(const std::string& str, const std::string& content,
                                         std::string::const_iterator host_start);

  std::string::const_iterator parse_port(const std::string& str, const std::string& content,
                                         std::string::const_iterator port_start);

  std::string::const_iterator parse_query(const std::string& str, std::string::const_iterator query_start);

  std::string::const_iterator parse_fragment(const std::string& str, std::string::const_iterator fragment_start);

  void init_query_dictionary();

  std::string transport_;
  std::string content_;
  std::string username_;
  std::string password_;
  std::string host_;
  std::string path_;
  std::string query_;
  std::string fragment_;
  std::map<std::string, std::string> query_dict_;
  Category category_{Category::kHierarchical};
  int64_t port_{0};
  bool is_rooted_{false};
  Separator separator_{Separator::kAmpersand};
};

}  // namespace vlink
