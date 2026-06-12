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

namespace spades {

    /**
     * ConfigItem provides read/write access to a single configuration variable
     * (cvar), such as "cg_fov". Construct one with the variable's name; an
     * optional second argument supplies a default value that is registered if
     * the variable does not exist yet.
     */
    class ConfigItem {
        /** Creates an accessor for the named configuration variable. */
        ConfigItem(const string &in name) {}

        /**
         * Creates an accessor for the named configuration variable, registering
         * the given default value.
         */
        ConfigItem(const string &in name, const string &in defaultValue) {}

        /** Sets the value from a floating-point number. */
        ConfigItem @opAssign(float value) {}

        /** Sets the value from an integer. */
        ConfigItem @opAssign(int value) {}

        /** Sets the value from a string. */
        ConfigItem @opAssign(const string &in value) {}

        /** The value interpreted as an integer. */
        int IntValue {
            get {}
            set {}
        }

        /** The value interpreted as a floating-point number. */
        float FloatValue {
            get {}
            set {}
        }

        /** The value as a string. */
        string StringValue {
            get {}
            set {}
        }

        /** The value interpreted as a boolean. */
        bool BoolValue {
            get {}
        }

        /** The default value registered for this variable. */
        string DefaultValue {
            get {}
        }

        /** Whether the variable is unknown to the engine. */
        bool IsUnknown {
            get {}
        }
    }

    /** Returns the names of all registered configuration variables. */
    array<string> @GetAllConfigNames() {}

}
