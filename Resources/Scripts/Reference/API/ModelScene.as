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
namespace spades {

    /**
     * A .2kv6 scene, registered once with `Renderer::RegisterScene`: the
     * whole of it, its parts by their paths of names, and where each of its
     * objects sits. Everything is in the scene's frame, so the parts drawn
     * with one matrix assemble as the scene shows them, and a part drawn
     * with that matrix times its own motion moves from there:
     *
     *     ModelScene@ sten = renderer.RegisterScene("Models/Weapons/SMG/Sten.2kv6");
     *     Model@ bolt = sten.Part("Bolt");
     *     param.matrix = weapon * CreateTranslateMatrix(boltTravel);
     *     renderer.AddModel(bolt, param);
     *
     * An object holding no voxels (an empty) marks a point to attach
     * something to, at `Transform(path)`. A path several objects share
     * names none of them.
     */
    class ModelScene {
        /** The whole scene, composed as RegisterModel composes it. */
        Model@ Whole { get {} }

        /**
         * The object at `path` ("Gun/Bolt") with everything under it, drawn
         * even when the scene hides it. Throws when there is no such object.
         */
        Model@ Part(const string& in path) {}

        /** Whether the scene has an object at `path`, and only one. */
        bool Has(const string& in path) {}

        /**
         * Where the object at `path` sits: its own coordinates to the scene's.
         * `Transform(path) * Vector3(0, 0, 0)` is its origin, where an empty
         * marks a point. Throws when there is no such object.
         */
        Matrix4 Transform(const string& in path) {}

        /** The scene's file, as given to RegisterScene. */
        string Path { get {} }
    }

}
