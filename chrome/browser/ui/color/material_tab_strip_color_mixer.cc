// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/color/material_tab_strip_color_mixer.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/color/chrome_color_provider_utils.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_recipe.h"

void AddMaterialTabStripColorMixer(ui::ColorProvider* provider,
                                   const ui::ColorProviderKey& key) {
  if (!ShouldApplyChromeMaterialOverrides(key)) {
    return;
  }

  const bool dark_mode =
      key.color_mode == ui::ColorProviderKey::ColorMode::kDark;

  // TODO(crbug.com/40883407): Validate final mappings for ChromeRefresh23
  // color.
  ui::ColorMixer& mixer = provider->AddMixer();
  mixer[kColorDetachedTabBackgroundActiveFrameActive] = {
      dark_mode ? SkColorSetRGB(0x2F, 0x33, 0x41) : ui::kColorSysBase};
  mixer[kColorTabBackgroundActiveFrameActive] = {
      dark_mode ? SkColorSetRGB(0x2F, 0x33, 0x41) : ui::kColorSysBase};
  mixer[kColorTabBackgroundActiveFrameInactive] = {
      kColorTabBackgroundActiveFrameActive};

  mixer[kColorTabBackgroundInactiveFrameActive] = {
      dark_mode ? SkColorSetRGB(0x23, 0x26, 0x31) : ui::kColorSysHeader};
  mixer[kColorTabBackgroundInactiveFrameInactive] = {
      dark_mode ? SkColorSetRGB(0x1F, 0x22, 0x2C) : ui::kColorSysHeaderInactive};
  mixer[kColorTabBackgroundInactiveHoverFrameActive] = {
      dark_mode ? SkColorSetRGB(0x29, 0x2D, 0x3A) : ui::kColorSysStateHeaderHover};
  mixer[kColorTabStripComboButtonSeparator] = {ui::kColorSysDivider};
  mixer[kColorTabStripControlButtonInkDrop] = {ui::kColorSysStateHeaderHover};
  mixer[kColorTabStripControlButtonInkDropFrameInactive] = {
      ui::kColorSysStateHeaderHoverInactive};
  mixer[kColorTabStripControlButtonInkDropRipple] = {
      ui::kColorSysStateRippleNeutralOnSubtle};
  mixer[kColorTabStripControlButtonInkDropRippleFrameInactive] = {
      ui::kColorSysStateRippleNeutralOnSubtle};

  // TODO(tbergquist): Use kColorSysStateHeaderHoverInactive, once it exists.
  mixer[kColorTabBackgroundInactiveHoverFrameInactive] = {
      dark_mode ? SkColorSetRGB(0x26, 0x2A, 0x35) : ui::kColorSysStateHeaderHoverInactive};

  if (key.frame_style == ui::ColorProviderKey::FrameStyle::kGlass) {
    constexpr SkAlpha kSelectedTabOpacity = 0.80 * SK_AlphaOPAQUE;
    mixer[kColorTabBackgroundSelectedFrameActive] = {
        ui::SetAlpha(ui::GetResultingPaintColor(ui::kColorSysStateHeaderSelect,
                                                ui::kColorSysHeader),
                     kSelectedTabOpacity)};
    mixer[kColorTabBackgroundSelectedFrameInactive] = {
        ui::SetAlpha(ui::GetResultingPaintColor(ui::kColorSysStateHeaderSelect,
                                                ui::kColorSysHeaderInactive),
                     kSelectedTabOpacity)};
    mixer[kColorTabStripScrollButtonBackground] = {SK_ColorTRANSPARENT};
  } else {
    mixer[kColorTabBackgroundSelectedFrameActive] = {
        ui::GetResultingPaintColor(ui::kColorSysStateHeaderSelect,
                                   kColorTabBackgroundInactiveFrameActive)};
    mixer[kColorTabBackgroundSelectedFrameInactive] = {
        ui::GetResultingPaintColor(ui::kColorSysStateHeaderSelect,
                                   kColorTabBackgroundInactiveFrameInactive)};
    mixer[kColorTabStripScrollButtonBackground] = {
        kColorTabBackgroundInactiveFrameActive};
  }
  mixer[kColorTabBackgroundSelectedHoverFrameActive] = {
      ui::GetResultingPaintColor(ui::kColorSysStateHoverDimBlendProtection,
                                 kColorTabBackgroundSelectedFrameActive)};
  mixer[kColorTabBackgroundSelectedHoverFrameInactive] = {
      ui::GetResultingPaintColor(ui::kColorSysStateHoverDimBlendProtection,
                                 kColorTabBackgroundSelectedFrameInactive)};
#if !BUILDFLAG(IS_ANDROID)
  mixer[kColorTabDiscardRingFrameActive] = {ui::kColorSysStateInactiveRing};
  mixer[kColorTabDiscardRingFrameInactive] = {kColorTabDiscardRingFrameActive};
#endif
  mixer[kColorTabForegroundActiveFrameActive] = {ui::kColorSysOnSurface};
  mixer[kColorTabForegroundActiveFrameInactive] = {
      kColorTabForegroundActiveFrameActive};
  mixer[kColorTabForegroundInactiveFrameActive] =
      ui::BlendForMinContrast({ui::kColorSysOnSurfaceSecondary},
                              {kColorTabBackgroundInactiveFrameActive});
  mixer[kColorTabForegroundInactiveFrameInactive] =
      ui::BlendForMinContrast({kColorTabForegroundInactiveFrameActive},
                              {kColorTabBackgroundInactiveFrameInactive});

  // TabDivider colors.
  mixer[kColorTabDividerFrameActive] = {ui::kColorSysOnHeaderDivider};
  mixer[kColorTabDividerFrameInactive] = {ui::kColorSysOnHeaderDividerInactive};

  // Tabstrip Control Button colors.
  mixer[kColorNewTabButtonCRForegroundFrameActive] = {
      ui::kColorSysOnSurfaceSubtle};
  mixer[kColorNewTabButtonCRForegroundFrameInactive] = {
      ui::kColorSysOnSurfaceSubtle};
  mixer[kColorNewTabButtonCRBackgroundFrameActive] = {
      ui::kColorSysHeaderContainer};
  mixer[kColorNewTabButtonCRBackgroundFrameInactive] = {
      ui::kColorSysHeaderContainerInactive};

  mixer[kColorTabSearchButtonCRForegroundFrameActive] = {
      ui::kColorSysOnSurfacePrimary};
  mixer[kColorTabSearchButtonCRForegroundFrameInactive] = {
      ui::kColorSysOnSurfacePrimaryInactive};

  mixer[kColorTabStripScrollButtonIcon] = {ui::kColorSysOnSurfaceSecondary};
}
