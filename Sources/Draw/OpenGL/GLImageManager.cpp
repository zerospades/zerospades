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

#include "GLImageManager.h"
#include "GLImage.h"
#include "GLRenderer.h"
#include "IGLDevice.h"
#include <Core/Bitmap.h>
#include <Core/Debug.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>

namespace spades {
	namespace draw {
		GLImageManager::GLImageManager(IGLDevice& dev) : device(dev), whiteImage(nullptr) {
			SPADES_MARK_FUNCTION();

			// Create the default white image right away
			GetWhiteImage();
		}

		GLImageManager::~GLImageManager() {
			SPADES_MARK_FUNCTION();
			if (whiteImage) {
				whiteImage->Invalidate();
				whiteImage->Release();
				whiteImage = nullptr;
			}
			for (auto it = images.begin(); it != images.end(); it++) {
				it->second->Invalidate();
				it->second->Release();
			}
		}

		GLImage* GLImageManager::RegisterImage(const std::string& name) {
			SPADES_MARK_FUNCTION();

			// The default white image is built in memory. The legacy file name is
			// kept as an alias so existing callers and mods keep working.
			if (IsBuiltIn(name)) {
				GLImage* white = GetWhiteImage();
				white->AddRef();
				return white;
			}

			auto it = images.find(name);
			if (it == images.end()) {
				GLImage* img = CreateImage(name);
				images[name] = img;
				img->AddRef();
				return img;
			}
			it->second->AddRef();
			return it->second;
		}

		GLImage* GLImageManager::GetWhiteImage() {
			if (!whiteImage) {
				// Build the 1x1 white default image in memory.
				Handle<Bitmap> bmp = Handle<Bitmap>::New(1, 1);
				bmp->SetPixel(0, 0, 0xFFFFFFFF);
				whiteImage = GLImage::FromBitmap(*bmp, &device).Unmanage();
			}
			return whiteImage;
		}

		GLImage* GLImageManager::CreateImage(const std::string& name) {
			SPADES_MARK_FUNCTION();

			Handle<Bitmap> bmp = Bitmap::Load(name);
			return GLImage::FromBitmap(*bmp, &device).Unmanage();
		}

		bool GLImageManager::IsBuiltIn(const std::string& name) {
			return name == "*white" || name == "Gfx/White.tga";
		}

		void GLImageManager::ClearCache() { images.clear(); }

		std::vector<std::string> GLImageManager::GetFiles() const {
			std::vector<std::string> files;
			for (const auto& entry : images)
				files.push_back(entry.first);
			return files;
		}
	} // namespace draw
} // namespace spades