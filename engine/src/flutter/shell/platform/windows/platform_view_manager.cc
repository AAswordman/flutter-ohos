// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/platform_view_manager.h"

#include <optional>

#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_method_codec.h"

namespace flutter {
namespace {
constexpr char kChannelName[] = "flutter/platform_views";

// Reads both integer representations emitted by StandardMessageCodec.
std::optional<int64_t> ReadInteger(const EncodableMap& args, const char* key) {
  const auto it = args.find(EncodableValue(key));
  if (it == args.end()) {
    return std::nullopt;
  }
  if (const auto value = std::get_if<int32_t>(&it->second)) {
    return *value;
  }
  if (const auto value = std::get_if<int64_t>(&it->second)) {
    return *value;
  }
  return std::nullopt;
}
}  // namespace

// Validates channel messages before dispatching native view operations.
PlatformViewManager::PlatformViewManager(BinaryMessenger* binary_messenger)
    : channel_(std::make_unique<MethodChannel<EncodableValue>>(
          binary_messenger,
          kChannelName,
          &StandardMethodCodec::GetInstance())) {
  channel_->SetMethodCallHandler(
      [this](const MethodCall<EncodableValue>& call,
             std::unique_ptr<MethodResult<EncodableValue>> result) {
        const auto& method = call.method_name();
        if (method != "create" && method != "focus" && method != "dispose") {
          result->NotImplemented();
          return;
        }
        const auto args = call.arguments()
                              ? std::get_if<EncodableMap>(call.arguments())
                              : nullptr;
        if (!args) {
          result->Error("invalid_arguments", "Expected a parameter map");
          return;
        }
        const auto id = ReadInteger(*args, "id");
        if (!id || *id < 0) {
          result->Error("invalid_arguments", "Expected a nonnegative view id");
          return;
        }
        if (method == "create") {
          const auto it = args->find(EncodableValue("viewType"));
          const auto type = it == args->end()
                                ? nullptr
                                : std::get_if<std::string>(&it->second);
          if (!type || type->empty()) {
            result->Error("invalid_arguments", "Expected a nonempty viewType");
            return;
          }
          if (!AddPlatformView(*id, *type)) {
            result->Error("AddPlatformView",
                          "Unknown type or duplicate view id");
            return;
          }
        } else if (method == "dispose") {
          if (!DisposePlatformView(*id)) {
            result->Error("DisposePlatformView",
                          "Failed to dispose platform view");
            return;
          }
        } else {
          const auto direction = ReadInteger(*args, "direction");
          const auto it = args->find(EncodableValue("focus"));
          const auto focus =
              it == args->end() ? nullptr : std::get_if<bool>(&it->second);
          if (!direction || *direction < 0 || *direction > 2 || !focus) {
            result->Error("invalid_arguments", "Invalid focus or direction");
            return;
          }
          if (!FocusPlatformView(
                  *id, static_cast<FocusChangeDirection>(*direction), *focus)) {
            result->Error("FocusPlatformView", "Failed to focus platform view");
            return;
          }
        }
        result->Success();
      });
}

// Removes the channel callback before the manager storage is released.
PlatformViewManager::~PlatformViewManager() {
  channel_->SetMethodCallHandler(nullptr);
}

}  // namespace flutter
