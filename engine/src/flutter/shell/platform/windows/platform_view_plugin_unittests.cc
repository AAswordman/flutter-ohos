// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/platform_view_plugin.h"

#include <memory>
#include <vector>

#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_method_codec.h"
#include "flutter/shell/platform/windows/testing/test_binary_messenger.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {
namespace {

class PlatformViewPluginTest : public ::testing::Test {
 protected:
  // Creates a platform thread task runner and a real Win32 parent window.
  void SetUp() override {
    runner_ = std::make_unique<TaskRunner>([]() -> uint64_t { return 0; },
                                           [](const FlutterTask*) {});
    parent_ = ::CreateWindowW(L"STATIC", L"Platform view test",
                              WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr,
                              nullptr, ::GetModuleHandle(nullptr), nullptr);
    ASSERT_NE(parent_, nullptr);
    plugin_ = std::make_unique<PlatformViewPlugin>(&messenger_, runner_.get());
    FlutterPlatformViewTypeEntry entry = {};
    entry.struct_size = sizeof(entry);
    entry.factory = CreateChild;
    entry.user_data = this;
    plugin_->RegisterPlatformViewType("test/native", entry);
  }

  // Releases owned children before their parent and task runner.
  void TearDown() override {
    plugin_.reset();
    ::DestroyWindow(parent_);
    runner_.reset();
  }

  // Records creation parameters and returns an owned native child.
  static HWND CreateChild(const FlutterPlatformViewCreationParameters* args) {
    auto self = static_cast<PlatformViewPluginTest*>(args->user_data);
    self->created_id_ = args->platform_view_id;
    self->creation_count_++;
    EXPECT_STREQ(args->platform_view_type, "test/native");
    EXPECT_EQ(args->parent_window, self->parent_);
    if (self->cancel_creation_) {
      EXPECT_TRUE(self->plugin_->DisposePlatformView(args->platform_view_id));
    }
    self->last_child_ = ::CreateWindowW(L"STATIC", L"Native child", WS_CHILD, 0,
                                        0, 50, 50, args->parent_window, nullptr,
                                        ::GetModuleHandle(nullptr), nullptr);
    return self->last_child_;
  }

  // Sends an encoded method call through the real channel and records its
  // reply.
  std::vector<uint8_t> Send(const std::string& method, EncodableValue args) {
    const auto message = StandardMethodCodec::GetInstance().EncodeMethodCall(
        MethodCall<EncodableValue>(method,
                                   std::make_unique<EncodableValue>(args)));
    std::vector<uint8_t> reply;
    EXPECT_TRUE(messenger_.SimulateEngineMessage(
        "flutter/platform_views", message->data(), message->size(),
        [&reply](const uint8_t* data, size_t size) {
          if (size != 0) {
            reply.assign(data, data + size);
          }
        }));
    return reply;
  }

  TestBinaryMessenger messenger_;
  std::unique_ptr<TaskRunner> runner_;
  std::unique_ptr<PlatformViewPlugin> plugin_;
  HWND parent_ = nullptr;
  HWND last_child_ = nullptr;
  PlatformViewId created_id_ = -1;
  int creation_count_ = 0;
  bool cancel_creation_ = false;
};

// Unknown types and duplicate IDs must fail before invoking a native factory.
TEST_F(PlatformViewPluginTest, RejectsUnknownTypesAndDuplicateIds) {
  EXPECT_FALSE(plugin_->AddPlatformView(1, "missing"));
  EXPECT_FALSE(plugin_->AddPlatformView(-1, "test/native"));
  EXPECT_TRUE(plugin_->AddPlatformView(1, "test/native"));
  EXPECT_FALSE(plugin_->AddPlatformView(1, "test/native"));
  EXPECT_EQ(creation_count_, 0);
}

// A pending view is created once with its owner and destroyed on disposal.
TEST_F(PlatformViewPluginTest, CreatesAndDisposesOwnedChild) {
  ASSERT_TRUE(plugin_->AddPlatformView(1, "test/native"));
  EXPECT_FALSE(plugin_->GetNativeHandleForId(1).has_value());
  ASSERT_TRUE(plugin_->InstantiatePlatformView(1, parent_));
  const auto child = plugin_->GetNativeHandleForId(1);
  ASSERT_TRUE(child.has_value());
  EXPECT_EQ(::GetParent(*child), parent_);
  EXPECT_EQ(created_id_, 1);
  EXPECT_TRUE(plugin_->InstantiatePlatformView(1, parent_));
  EXPECT_EQ(creation_count_, 1);
  EXPECT_FALSE(plugin_->AddPlatformView(1, "test/native"));
  EXPECT_TRUE(plugin_->DisposePlatformView(1));
  EXPECT_FALSE(::IsWindow(*child));
  EXPECT_FALSE(plugin_->GetNativeHandleForId(1).has_value());
  EXPECT_FALSE(plugin_->DisposePlatformView(1));
}

// Cancellation before composition never invokes the native factory.
TEST_F(PlatformViewPluginTest, CancelsPendingCreation) {
  ASSERT_TRUE(plugin_->AddPlatformView(1, "test/native"));
  EXPECT_TRUE(plugin_->DisposePlatformView(1));
  EXPECT_FALSE(plugin_->InstantiatePlatformView(1, parent_));
  EXPECT_EQ(creation_count_, 0);
}

// Reentrant cancellation cannot publish or leak a newly created native window.
TEST_F(PlatformViewPluginTest, CancelsDuringFactoryCallback) {
  cancel_creation_ = true;
  ASSERT_TRUE(plugin_->AddPlatformView(1, "test/native"));
  EXPECT_FALSE(plugin_->InstantiatePlatformView(1, parent_));
  EXPECT_FALSE(plugin_->GetNativeHandleForId(1).has_value());
  EXPECT_FALSE(::IsWindow(last_child_));
}

// Plugin teardown destroys live native children even without a Dart dispose
// call.
TEST_F(PlatformViewPluginTest, TeardownReleasesLiveChildren) {
  ASSERT_TRUE(plugin_->AddPlatformView(1, "test/native"));
  ASSERT_TRUE(plugin_->InstantiatePlatformView(1, parent_));
  const HWND child = last_child_;
  plugin_.reset();
  EXPECT_FALSE(::IsWindow(child));
}

// The channel preserves view IDs larger than the signed 32-bit range.
TEST_F(PlatformViewPluginTest, ChannelPreserves64BitIds) {
  constexpr int64_t id = INT64_C(1) << 40;
  const auto reply =
      Send("create",
           EncodableValue(EncodableMap{
               {EncodableValue("id"), EncodableValue(id)},
               {EncodableValue("viewType"), EncodableValue("test/native")}}));
  ASSERT_FALSE(reply.empty());
  EXPECT_EQ(reply[0], 0);
  ASSERT_TRUE(plugin_->InstantiatePlatformView(id, parent_));
  EXPECT_EQ(created_id_, id);
  const auto disposed = Send(
      "dispose",
      EncodableValue(EncodableMap{{EncodableValue("id"), EncodableValue(id)}}));
  ASSERT_FALSE(disposed.empty());
  EXPECT_EQ(disposed[0], 0);
  EXPECT_FALSE(::IsWindow(last_child_));
}

// Malformed messages produce error envelopes instead of variant exceptions.
TEST_F(PlatformViewPluginTest, ChannelRejectsInvalidArguments) {
  for (const auto& args :
       {EncodableValue(), EncodableValue("invalid"),
        EncodableValue(
            EncodableMap{{EncodableValue("id"), EncodableValue("invalid")}}),
        EncodableValue(
            EncodableMap{{EncodableValue("id"), EncodableValue(-1)}})}) {
    const auto reply = Send("create", args);
    ASSERT_FALSE(reply.empty());
    EXPECT_EQ(reply[0], 1);
  }
  const auto focus =
      Send("focus", EncodableValue(EncodableMap{
                        {EncodableValue("id"), EncodableValue(1)},
                        {EncodableValue("direction"), EncodableValue(3)},
                        {EncodableValue("focus"), EncodableValue(true)}}));
  ASSERT_FALSE(focus.empty());
  EXPECT_EQ(focus[0], 1);
  EXPECT_TRUE(Send("unsupported", EncodableValue()).empty());
}

// Unregistered IDs cannot take keyboard focus.
TEST_F(PlatformViewPluginTest, UnknownViewCannotReceiveFocus) {
  EXPECT_FALSE(
      plugin_->FocusPlatformView(1, FocusChangeDirection::kProgrammatic, true));
}

}  // namespace
}  // namespace testing
}  // namespace flutter
