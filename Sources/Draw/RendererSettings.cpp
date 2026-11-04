/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include <Core/Settings.h>

// The settings both the OpenGL and the Vulkan renderer read, with their
// defaults. They live apart from either renderer so that a build with only
// one of them still has them all; each renderer's own file keeps only what
// it alone reads.
DEFINE_SPADES_SETTING(r_bloom, "1");
DEFINE_SPADES_SETTING(r_cameraBlur, "1");
DEFINE_SPADES_SETTING(r_colorCorrection, "1");
DEFINE_SPADES_SETTING(r_depthOfField, "0");
DEFINE_SPADES_SETTING(r_depthOfFieldMaxCoc, "0.01");
DEFINE_SPADES_SETTING(r_depthPrepass, "0");
DEFINE_SPADES_SETTING(r_dlights, "1");
DEFINE_SPADES_SETTING(r_exposureValue, "0");
DEFINE_SPADES_SETTING(r_fogShadow, "0");
DEFINE_SPADES_SETTING(r_fxaa, "1");
DEFINE_SPADES_SETTING(r_hdr, "0");
DEFINE_SPADES_SETTING(r_hdrAutoExposureMax, "0.5");
DEFINE_SPADES_SETTING(r_hdrAutoExposureMin, "-1.5");
DEFINE_SPADES_SETTING(r_hdrAutoExposureSpeed, "1");
DEFINE_SPADES_SETTING(r_hdrGamma, "2.2");
DEFINE_SPADES_SETTING(r_highPrec, "1");
DEFINE_SPADES_SETTING(r_lensFlare, "1");
DEFINE_SPADES_SETTING(r_lensFlareDynamic, "1");
DEFINE_SPADES_SETTING(r_maxAnisotropy, "8");
DEFINE_SPADES_SETTING(r_modelShadows, "1");
DEFINE_SPADES_SETTING(r_multisamples, "0");
DEFINE_SPADES_SETTING(r_occlusionQuery, "0");
DEFINE_SPADES_SETTING(r_outlines, "0");
DEFINE_SPADES_SETTING(r_outlinesDepthThreshold, "0.05");
DEFINE_SPADES_SETTING(r_outlinesStrength, "1");
DEFINE_SPADES_SETTING(r_physicalLighting, "0");
DEFINE_SPADES_SETTING(r_radiosity, "0");
DEFINE_SPADES_SETTING(r_saturation, "1");
DEFINE_SPADES_SETTING(r_scale, "1");
DEFINE_SPADES_SETTING(r_scaleFilter, "1");
DEFINE_SPADES_SETTING(r_shadowMapSize, "2048");
DEFINE_SPADES_SETTING(r_sharpen, "0");
DEFINE_SPADES_SETTING(r_softParticles, "1");
DEFINE_SPADES_SETTING(r_srgb, "0");
DEFINE_SPADES_SETTING(r_ssao, "0");
DEFINE_SPADES_SETTING(r_temporalAA, "0");
DEFINE_SPADES_SETTING(r_water, "2");
