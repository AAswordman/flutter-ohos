// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_PLATFORM_VIEW_PLUGIN_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_PLATFORM_VIEW_PLUGIN_H_

#include "flutter/shell/platform/windows/platform_view_manager.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace flutter {

// The concrete implementation of PlatformViewManager that keeps track of
// existing platform view types and instances, and handles their instantiation.
class PlatformViewPlugin : public PlatformViewManager {
 public:
  PlatformViewPlugin(BinaryMessenger* messenger, TaskRunner* task_runner);

  ~PlatformViewPlugin();

  // Find the HWND corresponding to a platform view id. Returns null if the id
  // has no associated platform view.
  std::optional<HWND> GetNativeHandleForId(PlatformViewId id) const;

  // The runner-facing API calls this method to register a window type
  // corresponding to a platform view identifier supplied to the widget tree.
  void RegisterPlatformViewType(std::string_view type_name,
                                const FlutterPlatformViewTypeEntry& type);

  // | PlatformViewManager |
  // type_name must correspond to a string that has already been registered
  // with RegisterPlatformViewType.
  bool AddPlatformView(PlatformViewId id, std::string_view type_name) override;

  // Creates a registered child window on the platform thread. The parent is
  // the Flutter view that owns this instance. The plugin owns the child HWND.
  bool InstantiatePlatformView(PlatformViewId id, HWND parent_window);

  // Destroys an instance or cancels its pending creation on the platform
  // thread.
  bool DisposePlatformView(PlatformViewId id) override;

  // | PlatformViewManager |
  // id must correspond to an identifier that has already been added with
  // AddPlatformView.
  bool FocusPlatformView(PlatformViewId id,
                         FocusChangeDirection direction,
                         bool focus) override;

 private:
  std::unordered_map<std::string, FlutterPlatformViewTypeEntry>
      platform_view_types_;

  std::unordered_map<PlatformViewId, HWND> platform_views_;

  std::unordered_map<PlatformViewId, std::shared_ptr<std::string>>
      pending_platform_views_;

  // Prevents recursive factory invocation while a creation callback is active.
  std::unordered_set<PlatformViewId> creating_platform_views_;

  // Protects native handle snapshots read by the raster thread.
  mutable std::mutex views_mutex_;

  // Pointer to the task runner of the associated engine.
  TaskRunner* task_runner_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_PLATFORM_VIEW_PLUGIN_H_
