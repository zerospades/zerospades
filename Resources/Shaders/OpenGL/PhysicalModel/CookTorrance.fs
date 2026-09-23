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
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

float GGXDistribution(float m, float dotNH) {
	float m2 = m * m;
	float t = dotNH * dotNH * (m2 - 1.0) + 1.0;
	return m2 / (3.141592653 * t * t);
}

// http://en.wikipedia.org/wiki/Specular_highlight#Cook.E2.80.93Torrance_model
float CookTorrance(vec3 eyeVec, vec3 lightVec, vec3 normal) {
	vec3 halfVec = lightVec + eyeVec;
	halfVec = (dot(halfVec, halfVec) < 0.00000000001)
		? vec3(1.0, 0.0, 0.0) : normalize(halfVec);

	float dotNL = max(dot(normal, lightVec), 0.0);
	float dotNV = max(dot(normal, eyeVec), 0.0);
	float dotNH = max(dot(normal, halfVec), 0.00001);
	float dotVH = max(dot(eyeVec, halfVec), 0.0);

	// distribution term
	float m = 0.3; // roughness
	float distribution = GGXDistribution(m, dotNH);

	// fresnel term
	// FIXME: use split-sum approximation from UE4
	float fresnel2 = 1.0 - dotVH;
	float fresnel = 0.03 + 0.1 * fresnel2 * fresnel2;

	// visibility term (Schlick-Beckmann)
	float a = m * 0.7978, ia = 1.0 - a;
	float visibility = (dotNL * ia + a) * (dotNV * ia + a);
	visibility = 0.25 / max(visibility, 0.00001);

	float specular = distribution * fresnel * visibility;

	return specular;
}