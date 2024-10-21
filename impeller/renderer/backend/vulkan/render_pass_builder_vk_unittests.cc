// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/testing/testing.h"  // IWYU pragma: keep
#include "gtest/gtest.h"
#include "impeller/core/formats.h"
#include "impeller/renderer/backend/vulkan/render_pass_builder_vk.h"
#include "impeller/renderer/backend/vulkan/test/mock_vulkan.h"
#include "vulkan/vulkan_enums.hpp"

namespace impeller {
namespace testing {

TEST(RenderPassBuilder, CreatesRenderPassWithNoDepthStencil) {
  RenderPassBuilderVK builder = RenderPassBuilderVK();
  auto const context = MockVulkanContextBuilder().Build();

  // Create a single color attachment with a transient depth stencil.
  builder.SetColorAttachment(0, PixelFormat::kR8G8B8A8UNormInt,
                             SampleCount::kCount1, LoadAction::kClear,
                             StoreAction::kStore);

  auto render_pass = builder.Build(context->GetDevice());

  EXPECT_TRUE(!!render_pass);
  EXPECT_FALSE(builder.GetDepthStencil().has_value());
}

TEST(RenderPassBuilder, CreatesRenderPassWithCombinedDepthStencil) {
  RenderPassBuilderVK builder = RenderPassBuilderVK();
  auto const context = MockVulkanContextBuilder().Build();

  // Create a single color attachment with a transient depth stencil.
  builder.SetColorAttachment(0, PixelFormat::kR8G8B8A8UNormInt,
                             SampleCount::kCount1, LoadAction::kClear,
                             StoreAction::kStore);
  builder.SetDepthStencilAttachment(PixelFormat::kD24UnormS8Uint,
                                    SampleCount::kCount1, LoadAction::kDontCare,
                                    StoreAction::kDontCare);

  auto render_pass = builder.Build(context->GetDevice());

  EXPECT_TRUE(!!render_pass);

  auto maybe_color = builder.GetColorAttachments().find(0u);
  ASSERT_NE(maybe_color, builder.GetColorAttachments().end());
  auto color = maybe_color->second;

  EXPECT_EQ(color.initialLayout, vk::ImageLayout::eUndefined);
  EXPECT_EQ(color.finalLayout, vk::ImageLayout::eGeneral);
  EXPECT_EQ(color.loadOp, vk::AttachmentLoadOp::eClear);
  EXPECT_EQ(color.storeOp, vk::AttachmentStoreOp::eStore);

  auto maybe_depth_stencil = builder.GetDepthStencil();
  ASSERT_TRUE(maybe_depth_stencil.has_value());
  if (!maybe_depth_stencil.has_value()) {
    return;
  }
  auto depth_stencil = maybe_depth_stencil.value();

  EXPECT_EQ(depth_stencil.initialLayout, vk::ImageLayout::eUndefined);
  EXPECT_EQ(depth_stencil.finalLayout,
            vk::ImageLayout::eDepthStencilAttachmentOptimal);
  EXPECT_EQ(depth_stencil.loadOp, vk::AttachmentLoadOp::eDontCare);
  EXPECT_EQ(depth_stencil.storeOp, vk::AttachmentStoreOp::eDontCare);
  EXPECT_EQ(depth_stencil.stencilLoadOp, vk::AttachmentLoadOp::eDontCare);
  EXPECT_EQ(depth_stencil.stencilStoreOp, vk::AttachmentStoreOp::eDontCare);
}

TEST(RenderPassBuilder, CreatesRenderPassWithOnlyStencil) {
  RenderPassBuilderVK builder = RenderPassBuilderVK();
  auto const context = MockVulkanContextBuilder().Build();

  // Create a single color attachment with a transient depth stencil.
  builder.SetColorAttachment(0, PixelFormat::kR8G8B8A8UNormInt,
                             SampleCount::kCount1, LoadAction::kClear,
                             StoreAction::kStore);
  builder.SetStencilAttachment(PixelFormat::kS8UInt, SampleCount::kCount1,
                               LoadAction::kDontCare, StoreAction::kDontCare);

  auto render_pass = builder.Build(context->GetDevice());

  EXPECT_TRUE(!!render_pass);

  auto maybe_depth_stencil = builder.GetDepthStencil();
  ASSERT_TRUE(maybe_depth_stencil.has_value());
  if (!maybe_depth_stencil.has_value()) {
    return;
  }
  auto depth_stencil = maybe_depth_stencil.value();

  EXPECT_EQ(depth_stencil.initialLayout, vk::ImageLayout::eUndefined);
  EXPECT_EQ(depth_stencil.finalLayout,
            vk::ImageLayout::eDepthStencilAttachmentOptimal);
  EXPECT_EQ(depth_stencil.loadOp, vk::AttachmentLoadOp::eDontCare);
  EXPECT_EQ(depth_stencil.storeOp, vk::AttachmentStoreOp::eDontCare);
  EXPECT_EQ(depth_stencil.stencilLoadOp, vk::AttachmentLoadOp::eDontCare);
  EXPECT_EQ(depth_stencil.stencilStoreOp, vk::AttachmentStoreOp::eDontCare);
}

TEST(RenderPassBuilder, CreatesMSAAResolveWithCorrectStore) {
  RenderPassBuilderVK builder = RenderPassBuilderVK();
  auto const context = MockVulkanContextBuilder().Build();

  // Create an MSAA color attachment.
  builder.SetColorAttachment(0, PixelFormat::kR8G8B8A8UNormInt,
                             SampleCount::kCount4, LoadAction::kClear,
                             StoreAction::kMultisampleResolve);

  auto render_pass = builder.Build(context->GetDevice());

  EXPECT_TRUE(!!render_pass);

  auto maybe_color = builder.GetColorAttachments().find(0u);
  ASSERT_NE(maybe_color, builder.GetColorAttachments().end());
  auto color = maybe_color->second;

  // MSAA Texture.
  EXPECT_EQ(color.initialLayout, vk::ImageLayout::eUndefined);
  EXPECT_EQ(color.finalLayout, vk::ImageLayout::eGeneral);
  EXPECT_EQ(color.loadOp, vk::AttachmentLoadOp::eClear);
  EXPECT_EQ(color.storeOp, vk::AttachmentStoreOp::eDontCare);

  auto maybe_resolve = builder.GetResolves().find(0u);
  ASSERT_NE(maybe_resolve, builder.GetResolves().end());
  auto resolve = maybe_resolve->second;

  // MSAA Resolve Texture.
  EXPECT_EQ(resolve.initialLayout, vk::ImageLayout::eUndefined);
  EXPECT_EQ(resolve.finalLayout, vk::ImageLayout::eGeneral);
  EXPECT_EQ(resolve.loadOp, vk::AttachmentLoadOp::eClear);
  EXPECT_EQ(resolve.storeOp, vk::AttachmentStoreOp::eStore);
}

}  // namespace testing
}  // namespace impeller
