{
  description = "A compatible client of Ace of Spades 0.75";
  
  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
  };
  
  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem
      (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          inherit (pkgs) stdenv;

          # Non-GPL assets - please see `PakLocation.txt` for the terms of use
          # and issue #424 for the situation
          devPackage = pkgs.fetchurl {
            url = https://github.com/yvt/openspades-paks/releases/download/r33/OpenSpadesDevPackage-r33.zip;
            sha256 = "CSfcMjoLOroO6NHWjWtUSwD+ZUdA/q1tH6rTeqx3oq0=";
          };
          # Google Noto Fonts, licensed under the SIL Open Font License
          notoFontPak = pkgs.fetchurl {
            url = https://github.com/yvt/openspades/releases/download/v0.1.1b/NotoFonts.pak;
            sha256 = "VQYMZNYqNBZ9+01YCcabqqIfck/mU/BRcFZKXpBEX00=";
          };
        in rec {
          packages.default = packages.zerospades;

          packages.zerospades = stdenv.mkDerivation rec {
            pname = "zerospades";
            version = "0.0.8";

            src = self;

            nativeBuildInputs = with pkgs; [ cmake imagemagick unzip zip file glslang ];

            buildInputs = with pkgs;
              ([
                freetype SDL2 SDL2_image libGL zlib curl glew opusfile libogg openal vulkan-loader vulkan-headers
              ] ++ lib.optionals stdenv.hostPlatform.isDarwin [
                darwin.apple_sdk.frameworks.Cocoa
              ]);

            cmakeFlags = [
              "-DZEROSPADES_INSTALL_BINARY=bin"
              "-DCMAKE_CXX_STANDARD=17"
              "-DUSE_INTERNAL_OPENAL=OFF"
            ];

            inherit notoFontPak;

            # Used by `downloadpak.sh`. Instructs the script to copy the
            # development package from this path instead of downloading it.
            ZEROSPADES_DEVPAK_PATH = devPackage;

            postPatch = ''
              patchShebangs Resources
            '';

            postInstall = ''
              cp $notoFontPak $out/share/games/zerospades/Resources/
            '';
          };
        });
}