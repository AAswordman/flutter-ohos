// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/platform_view_plugin.h"

#include "flutter/fml/logging.h"

namespace flutter {

// Binds platform view ownership to the engine platform thread.
PlatformViewPlugin::PlatformViewPlugin(BinaryMessenger* messenger,
                                       TaskRunner* task_runner)
    : PlatformViewManager(messenger), task_runner_(task_runner) {}

// Releases native windows before the platform task runner is destroyed.
PlatformViewPlugin::~PlatformViewPlugin() {
  FML_DCHECK(task_runner_->RunsTasksOnCurrentThread());
  std::unordered_map<PlatformViewId, HWND> windows;
  {
    std::scoped_lock lock(views_mutex_);
    windows.swap(platform_views_);
  }
  for (const auto& [id, window] : windows) {
    if (::IsWindow(window)) {
      ::DestroyWindow(window);
    }
  }
}

// Returns a snapshot of a live instance without inserting a missing ID.
std::optional<HWND> PlatformViewPlugin::GetNativeHandleForId(
    PlatformViewId id) const {
  std::scoped_lock lock(views_mutex_);
  const auto it = platform_views_.find(id);
  if (it == platform_views_.end()) {
    return std::nullopt;
  }
  return it->second;
}

// Registers a factory once and rejects invalid or duplicate definitions.
void PlatformViewPlugin::RegisterPlatformViewType(
    std::string_view type_name,
    const FlutterPlatformViewTypeEntry& type) {
  if (!task_runner_->RunsTasksOnCurrentThread() || type_name.empty() ||
      type.struct_size < sizeof(FlutterPlatformViewTypeEntry) ||
      !type.factory) {
    FML_LOG(ERROR) << "Invalid platform view type registration.";
    return;
  }
  if (!platform_view_types_.emplace(std::string(type_name), type).second) {
    FML_LOG(ERROR) << "Platform view type already registered: " << type_name;
  }
}

// Invokes the factory with the owning Flutter HWND and publishes its child.
bool PlatformViewPlugin::InstantiatePlatformView(PlatformViewId id,
                                                 HWND parent_window) {
  if (!task_runner_->RunsTasksOnCurrentThread() || !::IsWindow(parent_window) ||
      ::GetWindowThreadProcessId(parent_window, nullptr) !=
          ::GetCurrentThreadId()) {
    return false;
  }
  const auto pending = pending_platform_views_.find(id);
  if (pending == pending_platform_views_.end()) {
    const auto existing = GetNativeHandleForId(id);
    return existing.has_value() && ::GetParent(*existing) == parent_window;
  }
  if (!creating_platform_views_.insert(id).second) {
    return false;
  }
  const auto creation = pending->second;
  const std::string type_name = *creation;
  const auto type = platform_view_types_.at(type_name);
  // Keep the reservation while plugin code may re-enter the message loop.
  FlutterPlatformViewCreationParameters parameters = {};
  parameters.struct_size = sizeof(parameters);
  parameters.parent_window = parent_window;
  parameters.platform_view_type = type_name.c_str();
  parameters.user_data = type.user_data;
  parameters.platform_view_id = id;
  const HWND window = type.factory(&parameters);
  creating_platform_views_.erase(id);
  const auto current = pending_platform_views_.find(id);
  if (current == pending_platform_views_.end() || current->second != creation) {
    if (::IsWindow(window) && window != parent_window) {
      ::DestroyWindow(window);
    }
    return false;
  }
  pending_platform_views_.erase(current);
  if (!::IsWindow(window) || ::GetParent(window) != parent_window ||
      !(::GetWindowLongPtr(window, GWL_STYLE) & WS_CHILD)) {
    FML_LOG(ERROR)
        << "Platform view factory must return a child of its parent.";
    if (::IsWindow(window) && window != parent_window) {
      ::DestroyWindow(window);
    }
    return false;
  }
  std::scoped_lock lock(views_mutex_);
  platform_views_.emplace(id, window);
  return true;
}

// Reserves a nonnegative ID for a known type until its owning view is
// available.
bool PlatformViewPlugin::AddPlatformView(PlatformViewId id,
                                         std::string_view type_name) {
  if (!task_runner_->RunsTasksOnCurrentThread() || id < 0 ||
      platform_view_types_.find(std::string(type_name)) ==
          platform_view_types_.end() ||
      GetNativeHandleForId(id).has_value()) {
    return false;
  }
  return pending_platform_views_
      .emplace(id, std::make_shared<std::string>(type_name))
      .second;
}

// Removes a pending or live view and destroys its owned HWND outside the lock.
bool PlatformViewPlugin::DisposePlatformView(PlatformViewId id) {
  if (!task_runner_->RunsTasksOnCurrentThread()) {
    return false;
  }
  if (pending_platform_views_.erase(id) != 0) {
    return true;
  }
  HWND window;
  {
    std::scoped_lock lock(views_mutex_);
    const auto it = platform_views_.find(id);
    if (it == platform_views_.end()) {
      return false;
    }
    window = it->second;
    platform_views_.erase(it);
  }
  return !::IsWindow(window) || ::DestroyWindow(window) != FALSE;
}

// Transfers focus to the native view or returns it to its Flutter parent.
bool PlatformViewPlugin::FocusPlatformView(PlatformViewId id,
                                           FocusChangeDirection direction,
                                           bool focus) {
  if (!task_runner_->RunsTasksOnCurrentThread()) {
    return false;
  }
  const auto window = GetNativeHandleForId(id);
  if (!window.has_value() || !::IsWindow(*window)) {
    return false;
  }
  HWND target = *window;
  if (!focus) {
    const HWND current = ::GetFocus();
    if (current != *window && !::IsChild(*window, current)) {
      return true;
    }
    target = ::GetParent(*window);
  } else if (direction == FocusChangeDirection::kForward ||
             direction == FocusChangeDirection::kBackward) {
    // Dialog navigation discovers the first or last tabbable native descendant.
    const HWND child = ::GetNextDlgTabItem(
        *window, nullptr, direction == FocusChangeDirection::kBackward);
    if (child) {
      target = child;
    }
  } else if (direction != FocusChangeDirection::kProgrammatic) {
    return false;
  }
  ::SetFocus(target);
  return ::GetFocus() == target;
}

}  // namespace flutter
