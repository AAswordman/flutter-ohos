// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/accessibility_plugin.h"

#include <memory>
#include <variant>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/basic_message_channel.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/encodable_value.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_message_codec.h"
#include "flutter/shell/platform/windows/flutter_windows_view.h"
#include "flutter/shell/platform/windows/testing/engine_modifier.h"
#include "flutter/shell/platform/windows/testing/flutter_windows_engine_builder.h"
#include "flutter/shell/platform/windows/testing/mock_window_binding_handler.h"
#include "flutter/shell/platform/windows/testing/test_binary_messenger.h"
#include "flutter/shell/platform/windows/testing/windows_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

using ::testing::_;
using ::testing::NiceMock;

static constexpr char kAccessibilityChannelName[] = "flutter/accessibility";

class MockFlutterWindowsView : public FlutterWindowsView {
 public:
  MockFlutterWindowsView(FlutterWindowsEngine* engine,
                         std::unique_ptr<WindowBindingHandler> window)
      : FlutterWindowsView(kImplicitViewId, engine, std::move(window)) {}

  MOCK_METHOD(void, AnnounceAlert, (const std::wstring& text), ());
};

}  // namespace

class AccessibilityPluginTest : public WindowsTest {
 public:
  AccessibilityPluginTest() = default;
  virtual ~AccessibilityPluginTest() = default;

 protected:
  FlutterWindowsEngine* engine() { return engine_.get(); }
  TestBinaryMessenger* messenger() { return &messenger_; }

  void SetUp() override {
    WindowsTest::SetUp();

    FlutterWindowsEngineBuilder builder{GetContext()};
    engine_ = builder.Build();

    auto window = std::make_unique<NiceMock<MockWindowBindingHandler>>();
    view_ = std::make_unique<NiceMock<MockFlutterWindowsView>>(
        engine_.get(), std::move(window));

    EngineModifier modifier{engine_.get()};
    modifier.SetSemanticsEnabled(true);
    modifier.SetImplicitView(view_.get());

    plugin_ = std::make_unique<AccessibilityPlugin>(engine_.get());
    AccessibilityPlugin::SetUp(&messenger_, plugin_.get());
  }

  // Sends a message and verifies the channel acknowledges it with a null reply.
  void SendAccessibilityMessage(const std::string& type,
                                const EncodableValue& data) {
    EncodableMap msg;
    msg[EncodableValue{"type"}] = EncodableValue{type};
    msg[EncodableValue{"data"}] = data;

    auto encoded =
        StandardMessageCodec::GetInstance().EncodeMessage(EncodableValue{msg});

    bool replied = false;
    bool handled = messenger_.SimulateEngineMessage(
        kAccessibilityChannelName, encoded->data(), encoded->size(),
        [&replied](const uint8_t* reply, size_t reply_size) {
          replied = true;
          auto decoded = StandardMessageCodec::GetInstance().DecodeMessage(
              reply, reply_size);
          ASSERT_NE(decoded, nullptr);
          EXPECT_TRUE(std::holds_alternative<std::monostate>(*decoded));
        });

    EXPECT_TRUE(handled)
        << "Message was not handled by the accessibility channel";
    EXPECT_TRUE(replied);
  }

  // Sends an announcement with the supplied message and view identifier.
  void SendAnnounceMessage(const std::string& message, EncodableValue view_id) {
    EncodableMap data;
    data[EncodableValue{"message"}] = EncodableValue{message};
    data[EncodableValue{"viewId"}] = view_id;

    SendAccessibilityMessage("announce", EncodableValue{data});
  }

  MockFlutterWindowsView* view() { return view_.get(); }

 private:
  std::unique_ptr<FlutterWindowsEngine> engine_;
  std::unique_ptr<MockFlutterWindowsView> view_;
  std::unique_ptr<AccessibilityPlugin> plugin_;
  TestBinaryMessenger messenger_;
};

TEST_F(AccessibilityPluginTest, DirectAnnounceCall) {
  // Test calling Announce directly, bypassing the message channel
  EXPECT_CALL(*view(), AnnounceAlert(::testing::Eq(L"Direct"))).Times(1);

  AccessibilityPlugin plugin(engine());
  plugin.Announce(0, "Direct");

  ::testing::Mock::VerifyAndClearExpectations(view());
}

TEST_F(AccessibilityPluginTest, AnnounceWithInt32ViewId) {
  EXPECT_CALL(*view(), AnnounceAlert(::testing::Eq(L"Hello"))).Times(1);

  SendAnnounceMessage("Hello", EncodableValue{static_cast<int32_t>(0)});

  // Verify expectations are met
  ::testing::Mock::VerifyAndClearExpectations(view());
}

TEST_F(AccessibilityPluginTest, AnnounceWithInt64ViewId) {
  EXPECT_CALL(*view(), AnnounceAlert(::testing::Eq(L"Hello World"))).Times(1);

  SendAnnounceMessage("Hello World", EncodableValue{static_cast<int64_t>(0)});

  // Verify expectations are met
  ::testing::Mock::VerifyAndClearExpectations(view());
}

TEST_F(AccessibilityPluginTest, AnnounceWithInvalidViewIdType) {
  // Should not be called with invalid viewId type
  EXPECT_CALL(*view(), AnnounceAlert(_)).Times(0);

  SendAnnounceMessage("Hello", EncodableValue{"invalid"});
}

// Tap feedback is Android-specific and must not generate Windows warnings.
TEST_F(AccessibilityPluginTest, TapDoesNotLog) {
  EXPECT_CALL(*view(), AnnounceAlert(_)).Times(0);
  fml::testing::LogCapture log_capture;

  SendAccessibilityMessage("tap", EncodableValue{EncodableMap{}});

  EXPECT_TRUE(log_capture.str().empty());
}

// Tooltip events are accepted without announcing them or logging warnings.
TEST_F(AccessibilityPluginTest, TooltipDoesNotLog) {
  EXPECT_CALL(*view(), AnnounceAlert(_)).Times(0);
  fml::testing::LogCapture log_capture;
  EncodableMap data;
  data[EncodableValue{"message"}] = EncodableValue{"Tooltip"};

  SendAccessibilityMessage("tooltip", EncodableValue{data});

  EXPECT_TRUE(log_capture.str().empty());
}

// Unknown message types must retain their diagnostic warning.
TEST_F(AccessibilityPluginTest, UnsupportedMessageStillLogs) {
  EXPECT_CALL(*view(), AnnounceAlert(_)).Times(0);
  fml::testing::LogCapture log_capture;

  SendAccessibilityMessage("unknown", EncodableValue{EncodableMap{}});

  EXPECT_NE(log_capture.str().find(
                "Accessibility message type 'unknown' is not supported."),
            std::string::npos);
}

// Ignoring tap feedback must not bypass message format validation.
TEST_F(AccessibilityPluginTest, MalformedTapDataStillLogs) {
  EXPECT_CALL(*view(), AnnounceAlert(_)).Times(0);
  fml::testing::LogCapture log_capture;

  SendAccessibilityMessage("tap", EncodableValue{"invalid"});

  EXPECT_NE(log_capture.str().find(
                "Accessibility message 'data' property must be a map."),
            std::string::npos);
}

// Ignoring tooltip feedback must not bypass message format validation.
TEST_F(AccessibilityPluginTest, MalformedTooltipDataStillLogs) {
  EXPECT_CALL(*view(), AnnounceAlert(_)).Times(0);
  fml::testing::LogCapture log_capture;

  SendAccessibilityMessage("tooltip", EncodableValue{"invalid"});

  EXPECT_NE(log_capture.str().find(
                "Accessibility message 'data' property must be a map."),
            std::string::npos);
}

TEST_F(AccessibilityPluginTest, AnnounceWithMissingMessage) {
  // Should not be called when message is missing
  EXPECT_CALL(*view(), AnnounceAlert(_)).Times(0);

  EncodableMap data;
  data[EncodableValue{"viewId"}] = EncodableValue{static_cast<int32_t>(0)};

  EncodableMap msg;
  msg[EncodableValue{"type"}] = EncodableValue{"announce"};
  msg[EncodableValue{"data"}] = EncodableValue{data};

  auto encoded =
      StandardMessageCodec::GetInstance().EncodeMessage(EncodableValue{msg});

  messenger()->SimulateEngineMessage(
      kAccessibilityChannelName, encoded->data(), encoded->size(),
      [](const uint8_t* reply, size_t reply_size) {});
}

}  // namespace testing
}  // namespace flutter
