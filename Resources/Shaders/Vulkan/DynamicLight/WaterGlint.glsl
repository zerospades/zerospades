/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

// The dynamic lights glinting off the water's waves: what tells the water from the
// ground under a flashlight, where no sun shines on either. The includer includes
// `DynamicLight/Lights.glsl` first.

// How tight a glint is: the Phong exponent of the water's highlight
const float waterGlintShininess = 256.0;
// Water's reflectance at normal incidence
const float waterReflectance = 0.02;
// The brightest a glint gets, so the waves' flicker cannot flash the screen
const float waterGlintLimit = 8.0;

/**
 * The light the dynamic lights glint off the water towards the eye, at `position`,
 * where `wave` is the surface's normal negated (unit length, as the water shaders
 * keep it) and `ongoing` the eye's ray (unit length).
 */
vec3 EvaluateWaterDynamicLightGlint(vec3 position, vec3 wave, vec3 ongoing) {
	vec3 normal = -wave;
	vec3 reflected = reflect(ongoing, normal);

	// Schlick's approximation of the Fresnel term
	float cosView = clamp(dot(wave, ongoing), 0.0, 1.0);
	float fresnel = waterReflectance + (1.0 - waterReflectance) * pow(1.0 - cosView, 5.0);

	vec3 glint = EvaluateDynamicLightsSpecular(position, normal, reflected, waterGlintShininess);
	return min(glint * fresnel, vec3(waterGlintLimit));
}
