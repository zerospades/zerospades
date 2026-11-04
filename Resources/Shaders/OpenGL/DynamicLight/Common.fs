/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

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

// Common code for dynamic light rendering


// -- shadowing

float EvaluateLighting();

float VisibilityOfLight() {
	return EvaluateLighting();
}

float EvaluateDynamicLightShadow() {
	return VisibilityOfLight();
}

// -- lighting (without bumpmapping)

vec3 EvaluateDynamicLights(vec3 position, vec3 normal);

varying vec3 dynamicLightWorldPosition;
varying vec3 dynamicLightNormal;

vec3 EvaluateDynamicLightNoBump() {
	vec3 normal = normalize(dynamicLightNormal);
	return EvaluateDynamicLights(dynamicLightWorldPosition, normal) *
		EvaluateDynamicLightShadow();
}

// TODO: bumpmapping variant (requires tangent vector)
