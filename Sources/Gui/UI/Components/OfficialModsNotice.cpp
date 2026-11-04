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

#include "OfficialModsNotice.h"

#include <utility>

#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Core/Debug.h>
#include <Core/ShellApi.h>
#include <Core/Strings.h>
#include <Gui/OfficialMods.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/UpdateChecker.h>

namespace spades {
	namespace gui {
		namespace {
			// Cool accent, so it reads apart from the warm game update notice.
			const Vector3 kAccent = MakeVector3(0.35F, 0.65F, 1.0F);

			// Where a player goes to update the game for newer official mods.
			constexpr const char* kGameReleasesPageUrl =
			  "https://github.com/zerospades/zerospades/releases/latest";
		} // namespace

		OfficialModsNotice::OfficialModsNotice(ui::UIManager* manager,
		                                       std::function<void()> downloadRequested)
		    : ButtonBase(manager), downloadRequested(std::move(downloadRequested)) {
			isMouseInteractive = false;
			Refresh();
		}

		void OfficialModsNotice::Refresh() {
			isMouseInteractive = false;

			// A game update comes first: it is announced in this spot. Until the game's
			// own check has answered, show nothing, so this banner never appears only
			// to be replaced by the game's under the cursor.
			const UpdateChecker::State game = UpdateChecker::Get().GetResult().state;
			if (game != UpdateChecker::State::UpToDate && game != UpdateChecker::State::Failed)
				return;

			OfficialModsChecker::Result result = OfficialModsChecker::Get().GetResult();
			switch (result.state) {
				case OfficialModsChecker::State::UpdateAvailable:
					needsGameUpdate = false;
					caption = _Tr("MainScreen",
					              "Official mods {0} are available (you have {1}). Click to "
					              "download them.",
					              result.latestVersion.ToString(),
					              result.installedVersion.ToString());
					break;
				case OfficialModsChecker::State::GameUpdateNeeded:
					// Only worth a click once there is a game update to get; until then
					// the installed mods are the latest this game can use.
					if (game == UpdateChecker::State::UpToDate)
						return;
					needsGameUpdate = true;
					caption = _Tr("MainScreen",
					              "Official mods {0} need a newer ZeroSpades. Click to update "
					              "the game.",
					              result.latestVersion.ToString());
					break;
				default: return;
			}
			isMouseInteractive = true;
		}

		bool OfficialModsNotice::IsShown() {
			Refresh();
			return isMouseInteractive;
		}

		void OfficialModsNotice::OnActivated() {
			if (needsGameUpdate) {
				if (!OpenURLInBrowser(kGameReleasesPageUrl))
					SPLog("[!] Could not open the release page: %s", kGameReleasesPageUrl);
			} else if (downloadRequested) {
				downloadRequested();
			}
			ButtonBase::OnActivated();
		}

		void OfficialModsNotice::Render() {
			Refresh();
			if (!isMouseInteractive)
				return;

			client::IRenderer& r = GetManager().GetRenderer();
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			float fill = 0.25F;
			if (pressed && hover)
				fill = 0.55F;
			else if (hover)
				fill = 0.4F;

			ui::SetColorNP(r, MakeVector4(kAccent.x, kAccent.y, kAccent.z, fill));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));
			ui::SetColorNP(r, MakeVector4(kAccent.x, kAccent.y, kAccent.z, 0.7F));
			r.DrawOutlinedRect(pos.x, pos.y, pos.x + sz.x, pos.y + sz.y);

			client::IFont* font = GetFont();
			if (!font)
				return;

			Vector2 txtSize = font->Measure(caption);
			Vector2 txtPos = pos + (sz - txtSize) * 0.5F;
			font->DrawShadow(caption, txtPos, 1.0F, MakeVector4(1.0F, 1.0F, 1.0F, 1.0F),
			                 MakeVector4(0.0F, 0.0F, 0.0F, 0.5F));
		}
	} // namespace gui
} // namespace spades
