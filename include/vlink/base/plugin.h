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
 * @file plugin.h
 * @brief Type-safe dynamic plugin loader with version checking and lifecycle management.
 *
 * @details
 * @c Plugin wraps @c dlopen / @c LoadLibrary to load shared libraries that implement a
 * given abstract C++ interface.  Each plugin library must use the @c VLINK_PLUGIN_DECLARE
 * macro to export @c vlink_plugin_create and @c vlink_plugin_destroy entry points.
 *
 * Plugin ID:
 * Every interface type @c T has a unique ID derived from its demangled type name (via
 * @c NameDetector::get<T>()) or from a user-supplied literal (via @c VLINK_PLUGIN_REGISTER_BY_ID).
 * The @c Plugin loader verifies that the ID embedded in the shared library matches the
 * caller's expected interface type before returning a @c shared_ptr<T>.
 *
 * Version checking:
 * The caller specifies a required major/minor version.  @c process_plugin_internal() performs
 * the check inside the library's entry point and returns @c nullptr if the versions are
 * incompatible, preventing ABI mismatches.
 *
 * Lifecycle:
 * - @c load<T>() opens the library, calls @c vlink_plugin_create, and wraps the result in
 *   a @c shared_ptr<T> with a custom deleter that calls @c vlink_plugin_destroy.
 * - @c unload<T>() decrements the reference count; the library is closed when the count
 *   reaches zero.
 * - @c clear() unloads all libraries.
 *
 * Macros (defined in this header):
 *
 * | Macro                             | Purpose                                         |
 * | --------------------------------- | ----------------------------------------------- |
 * | @c VLINK_PLUGIN_REGISTER          | In concrete class: derive plugin ID from type   |
 * | @c VLINK_PLUGIN_REGISTER_BY_ID    | In concrete class: use a literal string as ID   |
 * | @c VLINK_PLUGIN_DECLARE           | In .cpp: export create/destroy entry points     |
 *
 * @par Example
 * @code
 * // Interface header (my_plugin.h):
 * class MyPlugin {
 *   VLINK_PLUGIN_REGISTER(MyPlugin)
 *  public:
 *   virtual ~MyPlugin() = default;
 *   virtual void do_work() = 0;
 * };
 *
 * // Implementation .cpp (my_plugin_impl.cpp):
 * class MyPluginImpl : public MyPlugin {
 *   VLINK_PLUGIN_REGISTER(MyPlugin)
 *  public:
 *   void do_work() override { ... }
 * };
 * VLINK_PLUGIN_DECLARE(MyPluginImpl, 1, 0)
 *
 * // Loader:
 * vlink::Plugin plugin;
 * auto impl = plugin.load<MyPlugin>("my_plugin_impl", 1, 0);
 * if (impl) {
 *   impl->do_work();
 * }
 * @endcode
 */

#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>

#include "./logger.h"
#include "./macros.h"
#include "./name_detector.h"

/**
 * @def VLINK_PLUGIN_CREATE_FUNC_NAME
 * @brief Name of the plugin creation entry point exported by @c VLINK_PLUGIN_DECLARE.
 */
#define VLINK_PLUGIN_CREATE_FUNC_NAME vlink_plugin_create

/**
 * @def VLINK_PLUGIN_DESTROY_FUNC_NAME
 * @brief Name of the plugin destruction entry point exported by @c VLINK_PLUGIN_DECLARE.
 */
#define VLINK_PLUGIN_DESTROY_FUNC_NAME vlink_plugin_destroy

namespace vlink {

struct PluginEntry;

/**
 * @class Plugin
 * @brief Type-safe dynamic plugin loader with version verification and lifecycle management.
 *
 * @details
 * Loads one or more shared library plugins by interface type @c T.
 * Multiple distinct interface types may be loaded by a single @c Plugin instance.
 */
class VLINK_EXPORT Plugin final {
 public:
  /**
   * @brief Opaque handle to a loaded shared library object (used internally).
   */
  using Handle = void*;

  /**
   * @brief Constructs a @c Plugin manager with an empty library registry.
   */
  Plugin();

  /**
   * @brief Destructor.  Calls @c clear() to unload all still-loaded libraries.
   */
  ~Plugin();

  /**
   * @brief Sets the log level used for plugin load/unload diagnostics.
   *
   * @param level  Logger level.
   */
  void set_log_level(Logger::Level level);

  /**
   * @brief Returns the current log level for plugin diagnostics.
   *
   * @return Current log level.
   */
  [[nodiscard]] Logger::Level get_log_level() const;

  /**
   * @brief Returns the default search path list for finding plugin shared libraries.
   *
   * @details
   * Includes the executable directory, the system library directories, and
   * the current working directory.
   *
   * @return Deque of directory path strings to search in order.
   */
  [[nodiscard]] static std::deque<std::string> default_search_path();

  /**
   * @brief Loads a plugin implementing interface @c T from a shared library.
   *
   * @details
   * -# Resolves the library file by searching @p search_paths for a file whose name
   *    matches @p lib_name (with platform-appropriate prefix/suffix).
   * -# Opens the library with @c dlopen (or @c LoadLibrary on Windows).
   * -# Calls the @c vlink_plugin_create entry point with version and ID information.
   * -# Returns a @c shared_ptr<T> whose custom deleter calls @c vlink_plugin_destroy.
   *
   * @tparam T             Interface type.  Must have a @c get_plugin_id() static method
   *                       (inserted by @c VLINK_PLUGIN_REGISTER or @c VLINK_PLUGIN_REGISTER_BY_ID).
   * @param lib_name       File name of the shared library (without prefix/suffix).
   * @param version_major  Required major version.
   * @param version_minor  Required minor version.
   * @param dir_name       Optional explicit directory to search first.  Default: empty.
   * @param search_paths   Ordered list of directories to search.  Default: @c default_search_path().
   * @param function_name  Name of the creation entry point.  Default: @c vlink_plugin_create.
   * @return @c shared_ptr<T> on success, or @c nullptr on failure.
   */
  template <class T>
  [[nodiscard]] std::shared_ptr<T> load(
      const std::string& lib_name, uint16_t version_major, uint16_t version_minor, const std::string& dir_name = "",
      const std::deque<std::string>& search_paths = default_search_path(),
      const std::string& function_name = VLINK_MACRO_STRING_GET(VLINK_PLUGIN_CREATE_FUNC_NAME));

  /**
   * @brief Unloads the plugin library for interface @c T.
   *
   * @details
   * Removes the library from the internal registry and decrements the reference count.
   * The library is actually closed when all @c shared_ptr instances to the plugin object
   * are destroyed.
   *
   * @tparam T       Interface type.
   * @param lib_name Library file name used during @c load().
   * @return @c true if the library was found and unloaded.
   */
  template <class T>
  bool unload(const std::string& lib_name);

  /**
   * @brief Returns @c true if the plugin for interface @c T is currently loaded.
   *
   * @tparam T       Interface type.
   * @param lib_name Library file name used during @c load().
   * @return @c true if loaded.
   */
  template <class T>
  [[nodiscard]] bool has_loaded(const std::string& lib_name);

  /**
   * @brief Returns the composite key used internally to identify a (library, interface) pair.
   *
   * @details
   * The key is @c lib_name + "@" + T::get_plugin_id().
   *
   * @tparam T       Interface type.
   * @param lib_name Library file name.
   * @return Composite ID string.
   */
  template <class T>
  [[nodiscard]] std::string get_plugin_complex_id(const std::string& lib_name);

  /**
   * @brief Unloads all loaded plugin libraries.
   *
   * @details
   * Equivalent to calling @c unload<T>(lib_name) for every previously loaded plugin.
   */
  void clear();

  /**
   * @brief Internal entry-point handler called from @c VLINK_PLUGIN_DECLARE.
   *
   * @details
   * Validates the plugin ID and version, then configures the logger level inside the plugin.
   * Should not be called directly by user code.
   *
   * @param lib_name             Library name (for logging).
   * @param local_plugin_id      ID exported by the plugin implementation.
   * @param local_version_major  Major version exported by the plugin.
   * @param local_version_minor  Minor version exported by the plugin.
   * @param target_plugin_id     ID expected by the caller (from @c T::get_plugin_id()).
   * @param target_version_major Major version required by the caller.
   * @param target_version_minor Minor version required by the caller.
   * @param log_level            Logger level to propagate into the plugin.
   * @return @c true if the ID and version match.
   */
  static bool process_plugin_internal(const std::string& lib_name, const std::string& local_plugin_id,
                                      uint16_t local_version_major, uint16_t local_version_minor,
                                      const std::string& target_plugin_id, uint16_t target_version_major,
                                      uint16_t target_version_minor, uint8_t log_level);

 private:
  Handle load_and_create(const std::string& plugin_id, const std::string& lib_name, uint16_t version_major,
                         uint16_t version_minor, const std::string& dir_name,
                         const std::deque<std::string>& search_paths, const std::string& function_name,
                         std::shared_ptr<PluginEntry>* plugin_entry);

  bool unload(const std::string& plugin_complex_id);

  bool has_loaded(const std::string& plugin_complex_id);

  static bool destroy(std::shared_ptr<PluginEntry> plugin_entry, Handle handle,
                      const std::string& function_name = VLINK_MACRO_STRING_GET(VLINK_PLUGIN_DESTROY_FUNC_NAME));

  std::unique_ptr<struct PluginImpl> impl_;

  VLINK_DISALLOW_COPY_AND_ASSIGN(Plugin)
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

template <class T>
inline std::shared_ptr<T> Plugin::load(const std::string& lib_name, uint16_t version_major, uint16_t version_minor,
                                       const std::string& dir_name, const std::deque<std::string>& search_paths,
                                       const std::string& function_name) {
  static_assert(!T::get_plugin_id().empty(), "Plugin id can not be empty.");

  std::shared_ptr<PluginEntry> plugin_entry;
  auto* handle = load_and_create(T::get_plugin_id().data(), lib_name, version_major, version_minor, dir_name,
                                 search_paths, function_name, &plugin_entry);

  if VUNLIKELY (!handle) {
    return nullptr;
  }

  return std::shared_ptr<T>(static_cast<T*>(handle), [plugin_entry = std::move(plugin_entry)](T* interface_ptr) {
    destroy(std::move(plugin_entry), interface_ptr);
  });
}

template <class T>
inline bool Plugin::unload(const std::string& lib_name) {
  static_assert(!T::get_plugin_id().empty(), "Plugin id can not be empty.");

  return unload(get_plugin_complex_id<T>(lib_name));
}

template <class T>
inline bool Plugin::has_loaded(const std::string& lib_name) {
  static_assert(!T::get_plugin_id().empty(), "Plugin id can not be empty.");

  return has_loaded(get_plugin_complex_id<T>(lib_name));
}

template <class T>
inline std::string Plugin::get_plugin_complex_id(const std::string& lib_name) {
  static_assert(!T::get_plugin_id().empty(), "Plugin id can not be empty.");

  return lib_name + "@" + T::get_plugin_id().data();
}

}  // namespace vlink

////////////////////////////////////////////////////////////////
/// Macro Definitions
////////////////////////////////////////////////////////////////

#if defined(_WIN32) || defined(__CYGWIN__)
#define VLINK_PLUGIN_EXPORT __declspec(dllexport)
#else
#define VLINK_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/**
 * @brief Macro to register a plugin, automatically deriving its ID from the interface type name.
 *
 * This macro should be used within the definition of a concrete plugin class.
 * It defines a static constexpr member function `get_plugin_id()` that returns
 * the name of the `InterfaceType` as the plugin's ID.
 * It also includes a static assertion to ensure that the `InterfaceType` is an abstract class.
 *
 * @param InterfaceType The abstract interface class that the plugin implements.
 * The plugin ID will be derived from the name of this type.
 */
#define VLINK_PLUGIN_REGISTER(InterfaceType)                                                                         \
 public:                                                                                                             \
  static constexpr std::string_view get_plugin_id() {                                                                \
    static_assert(std::is_abstract_v<InterfaceType>, "Plugin interface must be abstract class.");                    \
    static_assert(std::has_virtual_destructor_v<InterfaceType>, "Plugin interface must have a virtual destructor."); \
    return vlink::NameDetector::get<InterfaceType>();                                                                \
  }

/**
 * @brief Macro to register a plugin with a specific, user-provided ID.
 *
 * This macro should be used within the definition of a concrete plugin class when you
 * want to explicitly specify the plugin's ID, rather than deriving it from the interface type name.
 * It defines a static constexpr member function `get_plugin_id()` that returns the provided `PluginID`.
 * It also includes a static assertion to ensure that the `InterfaceType` is an abstract class.
 *
 * @param InterfaceType The abstract interface class that the plugin implements.
 * @param PluginID      The string literal to be used as the plugin's unique identifier.
 */
#define VLINK_PLUGIN_REGISTER_BY_ID(InterfaceType, PluginID)                                                         \
 public:                                                                                                             \
  static constexpr std::string_view get_plugin_id() {                                                                \
    static_assert(std::is_abstract_v<InterfaceType>, "Plugin interface must be abstract class.");                    \
    static_assert(std::has_virtual_destructor_v<InterfaceType>, "Plugin interface must have a virtual destructor."); \
    return PluginID;                                                                                                 \
  }

/**
 * @brief Declares a plugin creation and destruction interface.
 *
 * This macro declares the plugin entry points for creating and destroying the plugin interface.
 * These functions are used by the `Plugin` class to load and unload the plugin.
 *
 * @param ImplementType The concrete class implementing the plugin interface.
 * @param VersionMajor The major version number of the plugin.
 * @param VersionMinor The minor version number of the plugin.
 */
#define VLINK_PLUGIN_DECLARE(ImplementType, VersionMajor, VersionMinor)                                         \
  extern "C" {                                                                                                  \
  VLINK_PLUGIN_EXPORT void* VLINK_PLUGIN_CREATE_FUNC_NAME(const char* lib_name, const char* plugin_id,          \
                                                          uint16_t version_major, uint16_t version_minor,       \
                                                          uint8_t log_level) {                                  \
    static_assert(std::is_default_constructible_v<ImplementType>,                                               \
                  "Plugin implementation must have default constructible");                                     \
    static_assert(!ImplementType::get_plugin_id().empty(), "Plugin id can not be empty.");                      \
    static_assert(!std::is_abstract_v<ImplementType>, "Plugin implementation cannot be an abstract class.");    \
                                                                                                                \
    /*NOLINTBEGIN*/                                                                                             \
    if VUNLIKELY (!vlink::Plugin::process_plugin_internal(lib_name, ImplementType::get_plugin_id().data(),      \
                                                          VersionMajor, VersionMinor, plugin_id, version_major, \
                                                          version_minor, log_level)) {                          \
      return nullptr;                                                                                           \
    }                                                                                                           \
                                                                                                                \
    return new ImplementType;                                                                                   \
    /*NOLINTEND*/                                                                                               \
  }                                                                                                             \
                                                                                                                \
  VLINK_PLUGIN_EXPORT bool VLINK_PLUGIN_DESTROY_FUNC_NAME(void* handle) {                                       \
    if VUNLIKELY (!handle) {                                                                                    \
      return false;                                                                                             \
    }                                                                                                           \
                                                                                                                \
    /*NOLINTBEGIN*/                                                                                             \
    delete static_cast<ImplementType*>(handle);                                                                 \
                                                                                                                \
    return true;                                                                                                \
    /*NOLINTEND*/                                                                                               \
  }                                                                                                             \
  }
