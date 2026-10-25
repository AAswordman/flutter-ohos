// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_map>

#include "flutter/display_list/dl_builder.h"
#include "flutter/display_list/dl_paint.h"
#include "flutter/display_list/dl_tile_mode.h"
#include "flutter/display_list/effects/dl_image_filter.h"
#include "flutter/display_list/geometry/dl_geometry_types.h"
#include "flutter/testing/testing.h"
#include "gtest/gtest.h"
#include "impeller/base/validation.h"
#include "impeller/core/formats.h"
#include "impeller/core/texture_descriptor.h"
#include "impeller/display_list/aiks_unittests.h"
#include "impeller/display_list/canvas.h"
#include "impeller/display_list/dl_dispatcher.h"
#include "impeller/display_list/dl_runtime_effect_impeller.h"
#include "impeller/display_list/dl_vertices_geometry.h"
#include "impeller/geometry/geometry_asserts.h"
#include "impeller/playground/playground.h"
#include "impeller/playground/widgets.h"
#include "impeller/renderer/render_target.h"
#include "impeller/renderer/testing/mocks.h"
#include "third_party/abseil-cpp/absl/status/status_matchers.h"

namespace impeller {
namespace testing {

std::unique_ptr<Canvas> CreateTestCanvas(
    ContentContext& context,
    std::optional<Rect> cull_rect = std::nullopt,
    bool requires_readback = false) {
  TextureDescriptor onscreen_desc;
  onscreen_desc.size = {100, 100};
  onscreen_desc.format =
      context.GetDeviceCapabilities().GetDefaultColorFormat();
  onscreen_desc.usage = TextureUsage::kRenderTarget;
  onscreen_desc.storage_mode = StorageMode::kDevicePrivate;
  onscreen_desc.sample_count = SampleCount::kCount1;
  std::shared_ptr<Texture> onscreen =
      context.GetContext()->GetResourceAllocator()->CreateTexture(
          onscreen_desc);

  ColorAttachment color0;
  color0.load_action = LoadAction::kClear;
  if (context.GetContext()->GetCapabilities()->SupportsOffscreenMSAA()) {
    TextureDescriptor onscreen_msaa_desc = onscreen_desc;
    onscreen_msaa_desc.sample_count = SampleCount::kCount4;
    onscreen_msaa_desc.storage_mode = StorageMode::kDeviceTransient;
    onscreen_msaa_desc.type = TextureType::kTexture2DMultisample;

    std::shared_ptr<Texture> onscreen_msaa =
        context.GetContext()->GetResourceAllocator()->CreateTexture(
            onscreen_msaa_desc);
    color0.resolve_texture = onscreen;
    color0.texture = onscreen_msaa;
    color0.store_action = StoreAction::kMultisampleResolve;
  } else {
    color0.texture = onscreen;
  }

  RenderTarget render_target;
  render_target.SetColorAttachment(color0, 0);

  if (cull_rect.has_value()) {
    return std::make_unique<Canvas>(
        context, render_target, /*is_onscreen=*/false,
        /*requires_readback=*/requires_readback, cull_rect.value());
  }
  return std::make_unique<Canvas>(context, render_target, /*is_onscreen=*/false,
                                  /*requires_readback=*/requires_readback);
}

TEST_P(AiksTest, TransformMultipliesCorrectly) {
  ContentContext context(GetContext(), nullptr);
  auto canvas = CreateTestCanvas(context);

  ASSERT_MATRIX_NEAR(canvas->GetCurrentTransform(), Matrix());

  // clang-format off
  canvas->Translate(Vector3(100, 200));
  ASSERT_MATRIX_NEAR(
    canvas->GetCurrentTransform(),
    Matrix(  1,   0,   0,   0,
             0,   1,   0,   0,
             0,   0,   1,   0,
           100, 200,   0,   1));

  canvas->Rotate(Radians(kPiOver2));
  ASSERT_MATRIX_NEAR(
    canvas->GetCurrentTransform(),
    Matrix(  0,   1,   0,   0,
            -1,   0,   0,   0,
             0,   0,   1,   0,
           100, 200,   0,   1));

  canvas->Scale(Vector3(2, 3));
  ASSERT_MATRIX_NEAR(
    canvas->GetCurrentTransform(),
    Matrix(  0,   2,   0,   0,
            -3,   0,   0,   0,
             0,   0,   0,   0,
           100, 200,   0,   1));

  canvas->Translate(Vector3(100, 200));
  ASSERT_MATRIX_NEAR(
    canvas->GetCurrentTransform(),
    Matrix(   0,   2,   0,   0,
             -3,   0,   0,   0,
              0,   0,   0,   0,
           -500, 400,   0,   1));
  // clang-format on
}

// Keeps the real GPU allocator and fails only complete offscreen target
// creation. Onscreen textures, shaders and command submission still use the
// real backend.
class FailingRenderTargetAllocator final : public RenderTargetAllocator {
 public:
  explicit FailingRenderTargetAllocator(std::shared_ptr<Allocator> allocator)
      : RenderTargetAllocator(std::move(allocator)) {}

  bool fail = false;
  size_t starts = 0;
  size_t ends = 0;

  void Start() override { starts++; }
  void End() override { ends++; }

  RenderTarget CreateOffscreen(
      const Context& context,
      ISize size,
      int mip_count,
      std::string_view label,
      RenderTarget::AttachmentConfig color_config,
      std::optional<RenderTarget::AttachmentConfig> stencil_config,
      const std::shared_ptr<Texture>& existing_color,
      const std::shared_ptr<Texture>& existing_depth,
      std::optional<PixelFormat> format) override {
    if (fail) {
      return {};
    }
    return RenderTargetAllocator::CreateOffscreen(
        context, size, mip_count, label, color_config, stencil_config,
        existing_color, existing_depth, format);
  }

  RenderTarget CreateOffscreenMSAA(
      const Context& context,
      ISize size,
      int mip_count,
      std::string_view label,
      RenderTarget::AttachmentConfigMSAA color_config,
      std::optional<RenderTarget::AttachmentConfig> stencil_config,
      const std::shared_ptr<Texture>& existing_msaa,
      const std::shared_ptr<Texture>& existing_resolve,
      const std::shared_ptr<Texture>& existing_depth,
      std::optional<PixelFormat> format) override {
    if (fail) {
      return {};
    }
    return RenderTargetAllocator::CreateOffscreenMSAA(
        context, size, mip_count, label, color_config, stencil_config,
        existing_msaa, existing_resolve, existing_depth, format);
  }
};

TEST_P(AiksTest, FailedRootReadbackTargetDoesNotAbortAndNextFrameRecovers) {
  ScopedValidationDisable disable_validation;
  auto allocator = std::make_shared<FailingRenderTargetAllocator>(
      GetContext()->GetResourceAllocator());
  ContentContext context(GetContext(), nullptr, allocator);
  allocator->fail = true;
  auto failed = CreateTestCanvas(context, std::nullopt, true);
  EXPECT_FALSE(failed->IsValid());
  EXPECT_FALSE(failed->GetLocalCoverageLimit().has_value());
  EXPECT_FALSE(failed->EndReplay());
  EXPECT_EQ(allocator->starts, allocator->ends);

  allocator->fail = false;
  auto recovered = CreateTestCanvas(context, std::nullopt, true);
  EXPECT_TRUE(recovered->IsValid());
  EXPECT_TRUE(recovered->GetLocalCoverageLimit().has_value());
  recovered->DrawRect(Rect::MakeLTRB(0, 0, 20, 20), {.color = Color::Red()});
  EXPECT_TRUE(recovered->EndReplay());
  EXPECT_EQ(allocator->starts, allocator->ends);
}

TEST_P(AiksTest,
       FailedSaveLayerPreservesNestedSaveRestoreAndReportsFrameFailure) {
  ScopedValidationDisable disable_validation;
  auto allocator = std::make_shared<FailingRenderTargetAllocator>(
      GetContext()->GetResourceAllocator());
  ContentContext context(GetContext(), nullptr, allocator);
  auto canvas = CreateTestCanvas(context);
  ASSERT_TRUE(canvas->IsValid());
  canvas->SaveLayer({}, Rect::MakeLTRB(0, 0, 75, 75), nullptr,
                    ContentBoundsPromise::kContainsContents, 4);
  ASSERT_TRUE(canvas->IsValid());
  allocator->fail = true;
  canvas->SaveLayer({}, Rect::MakeLTRB(0, 0, 50, 50), nullptr,
                    ContentBoundsPromise::kContainsContents, 2);
  EXPECT_EQ(canvas->GetSaveCount(), 3u);
  canvas->Save(1);
  canvas->DrawRect(Rect::MakeLTRB(0, 0, 10, 10), {.color = Color::Red()});
  EXPECT_TRUE(canvas->Restore());
  EXPECT_TRUE(canvas->Restore());
  EXPECT_EQ(canvas->GetSaveCount(), 2u);
  canvas->DrawRect(Rect::MakeLTRB(0, 0, 10, 10), {.color = Color::Blue()});
  EXPECT_TRUE(canvas->Restore());
  EXPECT_EQ(canvas->GetSaveCount(), 1u);
  EXPECT_FALSE(canvas->IsValid());
  EXPECT_FALSE(canvas->EndReplay());
  EXPECT_EQ(allocator->starts, allocator->ends);

  allocator->fail = false;
  auto recovered = CreateTestCanvas(context);
  recovered->SaveLayer({}, Rect::MakeLTRB(0, 0, 50, 50), nullptr,
                       ContentBoundsPromise::kContainsContents, 1);
  recovered->DrawRect(Rect::MakeLTRB(0, 0, 10, 10), {.color = Color::Red()});
  EXPECT_TRUE(recovered->Restore());
  EXPECT_TRUE(recovered->EndReplay());
  EXPECT_EQ(allocator->starts, allocator->ends);
}

TEST_P(AiksTest, InvalidRenderTargetReturnsFrameFailureRatherThanSuccess) {
  ScopedValidationDisable disable_validation;
  ContentContext context(GetContext(), nullptr);
  flutter::DisplayListBuilder builder;
  builder.DrawRect(flutter::DlRect::MakeLTRB(0, 0, 10, 10), flutter::DlPaint());
  EXPECT_FALSE(RenderToTarget(context, RenderTarget(), builder.Build(),
                              Rect::MakeLTRB(0, 0, 100, 100), true, false));
}

TEST_P(AiksTest, LazyRenderPassRejectsMissingAttachments) {
  ScopedValidationDisable disable_validation;
  ContentContext context(GetContext(), nullptr);
  auto empty = std::make_unique<EntityPassTarget>(RenderTarget(), false, false);
  EXPECT_FALSE(
      LazyRenderingConfig::Make(context, std::move(empty)).has_value());
  EXPECT_FALSE(LazyRenderingConfig::Make(context, nullptr).has_value());

  auto color_only = context.GetRenderTargetCache()->CreateOffscreen(
      *GetContext(), {32, 32}, 1, "Missing depth and stencil",
      RenderTarget::kDefaultColorAttachmentConfig, std::nullopt);
  ASSERT_TRUE(color_only.IsValid());
  auto incomplete =
      std::make_unique<EntityPassTarget>(color_only, false, false);
  EXPECT_FALSE(
      LazyRenderingConfig::Make(context, std::move(incomplete)).has_value());
}

TEST_P(AiksTest, FullCoverageDrawRestartsValidatedPass) {
  ContentContext context(GetContext(), nullptr);
  auto canvas = CreateTestCanvas(context);
  ASSERT_TRUE(canvas->IsValid());
  // Start recording commands before the full-coverage draw discards them.
  canvas->DrawCircle({20, 20}, 8, {.color = Color::Red()});
  canvas->DrawRect(Rect::MakeLTRB(0, 0, 100, 100),
                   {.color = Color::Blue(), .blend_mode = BlendMode::kSrc});
  EXPECT_TRUE(canvas->IsValid());
  EXPECT_TRUE(canvas->GetLocalCoverageLimit().has_value());
  canvas->DrawCircle({50, 50}, 8, {.color = Color::Green()});
  EXPECT_TRUE(canvas->EndReplay());
}

TEST_P(AiksTest, FailedSubpassCommandsPreserveStackAndAreNotRetried) {
  ScopedValidationDisable disable_validation;
  auto real_context = GetContext();
  for (bool fail_encoding : {false, true}) {
    auto context = std::make_shared<::testing::NiceMock<MockImpellerContext>>();
    ON_CALL(*context, GetCapabilities())
        .WillByDefault(::testing::ReturnRef(real_context->GetCapabilities()));
    ON_CALL(*context, GetResourceAllocator())
        .WillByDefault(::testing::Return(real_context->GetResourceAllocator()));
    ON_CALL(*context, GetShaderLibrary())
        .WillByDefault(::testing::Return(real_context->GetShaderLibrary()));
    ON_CALL(*context, GetSamplerLibrary())
        .WillByDefault(::testing::Return(real_context->GetSamplerLibrary()));
    ON_CALL(*context, GetPipelineLibrary())
        .WillByDefault(::testing::Return(real_context->GetPipelineLibrary()));
    ON_CALL(*context, GetCommandQueue())
        .WillByDefault(::testing::Return(real_context->GetCommandQueue()));
    ON_CALL(*context, GetBackendType())
        .WillByDefault(::testing::Return(real_context->GetBackendType()));
    ON_CALL(*context, GetRuntimeStageBackend())
        .WillByDefault(
            ::testing::Return(real_context->GetRuntimeStageBackend()));
    ON_CALL(*context, IsValid()).WillByDefault(::testing::Return(true));
    bool fail_commands = false;
    ON_CALL(*context, CreateCommandBuffer()).WillByDefault([&]() {
      if (!fail_commands) {
        return real_context->CreateCommandBuffer();
      }
      if (!fail_encoding) {
        return std::shared_ptr<CommandBuffer>();
      }
      auto buffer =
          std::make_shared<::testing::NiceMock<MockCommandBuffer>>(context);
      ON_CALL(*buffer, IsValid()).WillByDefault(::testing::Return(true));
      ON_CALL(*buffer, OnCreateRenderPass(::testing::_))
          .WillByDefault([&](RenderTarget target) {
            auto pass = std::make_shared<::testing::NiceMock<MockRenderPass>>(
                context, target);
            ON_CALL(*pass, IsValid()).WillByDefault(::testing::Return(true));
            EXPECT_CALL(*pass, OnEncodeCommands(::testing::_))
                .Times(1)
                .WillOnce(::testing::Return(false));
            return pass;
          });
      return std::static_pointer_cast<CommandBuffer>(buffer);
    });
    ContentContext renderer(context, nullptr);
    auto canvas = CreateTestCanvas(renderer);
    ASSERT_TRUE(canvas->IsValid());
    canvas->SaveLayer({}, Rect::MakeLTRB(0, 0, 50, 50), nullptr,
                      ContentBoundsPromise::kContainsContents, 1);
    fail_commands = true;
    EXPECT_FALSE(canvas->Restore());
    EXPECT_EQ(canvas->GetSaveCount(), 1u);
    EXPECT_FALSE(canvas->IsValid());
    fail_commands = false;
    EXPECT_FALSE(canvas->EndReplay());

    auto backdrop = CreateTestCanvas(renderer, std::nullopt, true);
    backdrop->SetBackdropData({}, 1);
    auto blur =
        flutter::DlImageFilter::MakeBlur(4, 4, flutter::DlTileMode::kClamp);
    fail_commands = true;
    backdrop->SaveLayer({}, Rect::MakeLTRB(0, 0, 50, 50), blur.get(),
                        ContentBoundsPromise::kContainsContents, 1);
    EXPECT_EQ(backdrop->GetSaveCount(), 2u);
    EXPECT_TRUE(backdrop->Restore());
    EXPECT_EQ(backdrop->GetSaveCount(), 1u);
    EXPECT_FALSE(backdrop->IsValid());
    fail_commands = false;
    EXPECT_FALSE(backdrop->EndReplay());

    auto recovered = CreateTestCanvas(renderer);
    recovered->DrawRect(Rect::MakeLTRB(0, 0, 10, 10), {.color = Color::Red()});
    EXPECT_TRUE(recovered->EndReplay());
  }
}

TEST_P(AiksTest, CanvasCanPushPopCTM) {
  ContentContext context(GetContext(), nullptr);
  auto canvas = CreateTestCanvas(context);

  ASSERT_EQ(canvas->GetSaveCount(), 1u);
  ASSERT_EQ(canvas->Restore(), false);

  canvas->Translate(Size{100, 100});
  canvas->Save(10);
  ASSERT_EQ(canvas->GetSaveCount(), 2u);
  ASSERT_MATRIX_NEAR(canvas->GetCurrentTransform(),
                     Matrix::MakeTranslation({100.0, 100.0, 0.0}));
  ASSERT_TRUE(canvas->Restore());
  ASSERT_EQ(canvas->GetSaveCount(), 1u);
  ASSERT_MATRIX_NEAR(canvas->GetCurrentTransform(),
                     Matrix::MakeTranslation({100.0, 100.0, 0.0}));
}

TEST_P(AiksTest, CanvasCTMCanBeUpdated) {
  ContentContext context(GetContext(), nullptr);
  auto canvas = CreateTestCanvas(context);

  Matrix identity;
  ASSERT_MATRIX_NEAR(canvas->GetCurrentTransform(), identity);
  canvas->Translate(Size{100, 100});
  ASSERT_MATRIX_NEAR(canvas->GetCurrentTransform(),
                     Matrix::MakeTranslation({100.0, 100.0, 0.0}));
}

TEST_P(AiksTest, BackdropCountDownNormal) {
  ContentContext context(GetContext(), nullptr);
  if (!context.GetDeviceCapabilities().SupportsFramebufferFetch()) {
    GTEST_SKIP() << "Test requires device with framebuffer fetch";
  }
  auto canvas = CreateTestCanvas(context, Rect::MakeLTRB(0, 0, 100, 100),
                                 /*requires_readback=*/true);
  // 3 backdrop filters
  canvas->SetBackdropData({}, 3);

  auto blur =
      flutter::DlImageFilter::MakeBlur(4, 4, flutter::DlTileMode::kClamp);
  flutter::DlRect rect = flutter::DlRect::MakeLTRB(0, 0, 50, 50);

  EXPECT_TRUE(canvas->RequiresReadback());
  canvas->DrawRect(rect, {.color = Color::Azure()});
  canvas->SaveLayer({}, rect, blur.get(),
                    ContentBoundsPromise::kContainsContents,
                    /*total_content_depth=*/1);
  canvas->Restore();
  EXPECT_TRUE(canvas->RequiresReadback());

  canvas->SaveLayer({}, rect, blur.get(),
                    ContentBoundsPromise::kContainsContents,
                    /*total_content_depth=*/1);
  canvas->Restore();
  EXPECT_TRUE(canvas->RequiresReadback());

  canvas->SaveLayer({}, rect, blur.get(),
                    ContentBoundsPromise::kContainsContents,
                    /*total_content_depth=*/1);
  canvas->Restore();
  EXPECT_FALSE(canvas->RequiresReadback());
}

TEST_P(AiksTest, BackdropCountDownBackdropId) {
  ContentContext context(GetContext(), nullptr);
  if (!context.GetDeviceCapabilities().SupportsFramebufferFetch()) {
    GTEST_SKIP() << "Test requires device with framebuffer fetch";
  }
  auto canvas = CreateTestCanvas(context, Rect::MakeLTRB(0, 0, 100, 100),
                                 /*requires_readback=*/true);
  // 3 backdrop filters all with same id.
  std::unordered_map<int64_t, BackdropData> data;
  data[1] = BackdropData{.backdrop_count = 3};
  canvas->SetBackdropData(data, 3);

  auto blur =
      flutter::DlImageFilter::MakeBlur(4, 4, flutter::DlTileMode::kClamp);

  EXPECT_TRUE(canvas->RequiresReadback());
  canvas->DrawRect(flutter::DlRect::MakeLTRB(0, 0, 50, 50),
                   {.color = Color::Azure()});
  canvas->SaveLayer({}, std::nullopt, blur.get(),
                    ContentBoundsPromise::kContainsContents,
                    /*total_content_depth=*/1, /*can_distribute_opacity=*/false,
                    /*backdrop_id=*/1);
  canvas->Restore();
  EXPECT_FALSE(canvas->RequiresReadback());

  canvas->SaveLayer({}, std::nullopt, blur.get(),
                    ContentBoundsPromise::kContainsContents,
                    /*total_content_depth=*/1, /*can_distribute_opacity=*/false,
                    /*backdrop_id=*/1);
  canvas->Restore();
  EXPECT_FALSE(canvas->RequiresReadback());

  canvas->SaveLayer({}, std::nullopt, blur.get(),
                    ContentBoundsPromise::kContainsContents,
                    /*total_content_depth=*/1, /*can_distribute_opacity=*/false,
                    /*backdrop_id=*/1);
  canvas->Restore();
  EXPECT_FALSE(canvas->RequiresReadback());
}

TEST_P(AiksTest, BackdropCountDownBackdropIdMixed) {
  ContentContext context(GetContext(), nullptr);
  if (!context.GetDeviceCapabilities().SupportsFramebufferFetch()) {
    GTEST_SKIP() << "Test requires device with framebuffer fetch";
  }
  auto canvas = CreateTestCanvas(context, Rect::MakeLTRB(0, 0, 100, 100),
                                 /*requires_readback=*/true);
  // 3 backdrop filters, 2 with same id.
  std::unordered_map<int64_t, BackdropData> data;
  data[1] = BackdropData{.backdrop_count = 2};
  canvas->SetBackdropData(data, 3);

  auto blur =
      flutter::DlImageFilter::MakeBlur(4, 4, flutter::DlTileMode::kClamp);

  EXPECT_TRUE(canvas->RequiresReadback());
  canvas->DrawRect(flutter::DlRect::MakeLTRB(0, 0, 50, 50),
                   {.color = Color::Azure()});
  canvas->SaveLayer({}, std::nullopt, blur.get(),
                    ContentBoundsPromise::kContainsContents, 1, false);
  canvas->Restore();
  EXPECT_TRUE(canvas->RequiresReadback());

  canvas->SaveLayer({}, std::nullopt, blur.get(),
                    ContentBoundsPromise::kContainsContents, 1, false, 1);
  canvas->Restore();
  EXPECT_FALSE(canvas->RequiresReadback());

  canvas->SaveLayer({}, std::nullopt, blur.get(),
                    ContentBoundsPromise::kContainsContents, 1, false, 1);
  canvas->Restore();
  EXPECT_FALSE(canvas->RequiresReadback());
}

// We only know the total number of backdrop filters, not the number of backdrop
// filters in the root pass. If we reach a count of 0 while in a nested
// saveLayer, we should not restore to the onscreen.
TEST_P(AiksTest, BackdropCountDownWithNestedSaveLayers) {
  ContentContext context(GetContext(), nullptr);
  if (!context.GetDeviceCapabilities().SupportsFramebufferFetch()) {
    GTEST_SKIP() << "Test requires device with framebuffer fetch";
  }
  auto canvas = CreateTestCanvas(context, Rect::MakeLTRB(0, 0, 100, 100),
                                 /*requires_readback=*/true);

  canvas->SetBackdropData({}, 2);

  auto blur =
      flutter::DlImageFilter::MakeBlur(4, 4, flutter::DlTileMode::kClamp);

  EXPECT_TRUE(canvas->RequiresReadback());
  canvas->DrawRect(flutter::DlRect::MakeLTRB(0, 0, 50, 50),
                   {.color = Color::Azure()});
  canvas->SaveLayer({}, std::nullopt, blur.get(),
                    ContentBoundsPromise::kContainsContents,
                    /*total_content_depth=*/3);

  // This filter is nested in the first saveLayer. We cannot restore to onscreen
  // here.
  canvas->SaveLayer({}, std::nullopt, blur.get(),
                    ContentBoundsPromise::kContainsContents,
                    /*total_content_depth=*/1);
  canvas->Restore();
  EXPECT_TRUE(canvas->RequiresReadback());

  canvas->Restore();
  EXPECT_TRUE(canvas->RequiresReadback());
}

TEST_P(AiksTest, DrawVerticesLinearGradientWithEmptySize) {
  RenderCallback callback = [&](RenderTarget& render_target) {
    ContentContext context(GetContext(), nullptr);
    Canvas canvas(context, render_target, true, false);

    std::vector<flutter::DlPoint> vertex_coordinates = {
        flutter::DlPoint(0, 0),
        flutter::DlPoint(600, 0),
        flutter::DlPoint(0, 600),
    };
    std::vector<flutter::DlPoint> texture_coordinates = {
        flutter::DlPoint(0, 0),
        flutter::DlPoint(500, 0),
        flutter::DlPoint(0, 500),
    };
    std::vector<uint16_t> indices = {0, 1, 2};
    flutter::DlVertices::Builder vertices_builder(
        flutter::DlVertexMode::kTriangleStrip, vertex_coordinates.size(),
        flutter::DlVertices::Builder::kHasTextureCoordinates, indices.size());
    vertices_builder.store_vertices(vertex_coordinates.data());
    vertices_builder.store_indices(indices.data());
    vertices_builder.store_texture_coordinates(texture_coordinates.data());
    auto vertices = vertices_builder.build();

    // The start and end points of the gradient form an empty rectangle.
    std::vector<flutter::DlColor> colors = {flutter::DlColor::kBlue(),
                                            flutter::DlColor::kRed()};
    std::vector<Scalar> stops = {0.0, 1.0};
    auto gradient = flutter::DlColorSource::MakeLinear(
        {0, 0}, {0, 600}, 2, colors.data(), stops.data(),
        flutter::DlTileMode::kClamp);

    Paint paint;
    paint.color_source = gradient.get();
    canvas.DrawVertices(std::make_shared<DlVerticesGeometry>(vertices, context),
                        BlendMode::kSrcOver, paint);

    canvas.EndReplay();
    return true;
  };

  ASSERT_TRUE(Playground::OpenPlaygroundHere(callback));
}

TEST_P(AiksTest, DrawVerticesWithEmptyTextureCoordinates) {
  auto runtime_stages_result =
      OpenAssetAsRuntimeStage("runtime_stage_simple.frag.iplr");
  ABSL_ASSERT_OK(runtime_stages_result);
  std::shared_ptr<RuntimeStage> runtime_stage =
      runtime_stages_result
          .value()[PlaygroundBackendToRuntimeStageBackend(GetBackend())];
  ASSERT_TRUE(runtime_stage);

  auto runtime_effect = flutter::DlRuntimeEffectImpeller::Make(runtime_stage);
  auto uniform_data = std::make_shared<std::vector<uint8_t>>();
  auto color_source = flutter::DlColorSource::MakeRuntimeEffect(
      runtime_effect, {}, uniform_data);

  RenderCallback callback = [&](RenderTarget& render_target) {
    ContentContext context(GetContext(), nullptr);
    Canvas canvas(context, render_target, true, false);

    std::vector<flutter::DlPoint> vertex_coordinates = {
        flutter::DlPoint(100, 100),
        flutter::DlPoint(300, 100),
        flutter::DlPoint(100, 300),
    };
    // The bounding box of the texture coordinates is empty.
    std::vector<flutter::DlPoint> texture_coordinates = {
        flutter::DlPoint(0, 0),
        flutter::DlPoint(0, 100),
        flutter::DlPoint(0, 0),
    };
    std::vector<uint16_t> indices = {0, 1, 2};
    flutter::DlVertices::Builder vertices_builder(
        flutter::DlVertexMode::kTriangleStrip, vertex_coordinates.size(),
        flutter::DlVertices::Builder::kHasTextureCoordinates, indices.size());
    vertices_builder.store_vertices(vertex_coordinates.data());
    vertices_builder.store_indices(indices.data());
    vertices_builder.store_texture_coordinates(texture_coordinates.data());
    auto vertices = vertices_builder.build();

    Paint paint;
    paint.color_source = color_source.get();
    canvas.DrawVertices(std::make_shared<DlVerticesGeometry>(vertices, context),
                        BlendMode::kSrcOver, paint);

    canvas.EndReplay();
    return true;
  };

  ASSERT_TRUE(Playground::OpenPlaygroundHere(callback));
}

TEST_P(AiksTest, SupportsBlitToOnscreen) {
  ContentContext context(GetContext(), nullptr);
  auto canvas = CreateTestCanvas(context, Rect::MakeLTRB(0, 0, 100, 100),
                                 /*requires_readback=*/true);

  if (GetBackend() != PlaygroundBackend::kMetal) {
    EXPECT_FALSE(canvas->SupportsBlitToOnscreen());
  } else {
    EXPECT_TRUE(canvas->SupportsBlitToOnscreen());
  }
}

TEST_P(AiksTest, RoundSuperellipseShadowComparison) {
  // Config
  Size default_size(600, 400);
  Point left_center(400, 700);
  Point right_center(1300, 700);
  Color color = Color::Red();

  // Convert `color` to a `color_source`. This forces
  // `canvas.DrawRoundSuperellipse` to use the regular shadow algorithm
  // (blurring) instead of the fast shadow algorithm.
  std::shared_ptr<flutter::DlColorSource> color_source;
  {
    flutter::DlColor dl_color = flutter::DlColor(color.ToARGB());
    std::vector<flutter::DlColor> colors = {dl_color, dl_color};
    std::vector<Scalar> stops = {0.0, 1.0};
    color_source = flutter::DlColorSource::MakeLinear(
        {0, 0}, {1000, 1000}, 2, colors.data(), stops.data(),
        flutter::DlTileMode::kClamp);
  }

  auto RectMakeCenterHalfSize = [](Point center, Point half_size) {
    Size size(half_size.x * 2, half_size.y * 2);
    return Rect::MakeOriginSize(center - half_size, size);
  };

  RenderCallback callback = [&](RenderTarget& render_target) {
    ContentContext context(GetContext(), nullptr);
    Canvas canvas(context, render_target, true, false);
    // Somehow there's a scaling factor between PlaygroundPoint and Canvas.
    Matrix ctm = Matrix::MakeScale(Vector2(1, 1) * 0.5);
    Matrix i_ctm = ctm.Invert();

    static Scalar sigma = 0.05;
    static Scalar radius = 200;

    // Define the ImGui
    ImGui::Begin("Shadow", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    {
      ImGui::SliderFloat("Sigma", &sigma, 0, 100);
      ImGui::SliderFloat("Radius", &radius, 0, 1000);
    }
    ImGui::End();

    static PlaygroundPoint right_reference_var(
        ctm * (right_center + default_size / 2), 30, Color::White());
    Point right_reference = i_ctm * DrawPlaygroundPoint(right_reference_var);
    Point half_size = (right_reference - right_center).Abs();
    Rect left_bounds = RectMakeCenterHalfSize(left_center, half_size);
    Rect right_bounds = RectMakeCenterHalfSize(right_center, half_size);

    Paint paint{
        .color = color,
        .mask_blur_descriptor =
            Paint::MaskBlurDescriptor{
                .sigma = Sigma(sigma),
            },
    };

    // Left: Draw with canvas
    canvas.DrawRoundSuperellipse(
        RoundSuperellipse::MakeRectRadius(left_bounds, radius), paint);

    // Right: Direct draw
    paint.color_source = color_source.get();
    canvas.DrawRoundSuperellipse(
        RoundSuperellipse::MakeRectRadius(right_bounds, radius), paint);

    canvas.EndReplay();
    return true;
  };

  ASSERT_TRUE(Playground::OpenPlaygroundHere(callback));
}

}  // namespace testing
}  // namespace impeller
