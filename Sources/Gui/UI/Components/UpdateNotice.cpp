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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "UpdateNotice.h"

#include <utility>

#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Core/Debug.h>
#include <Core/Settings.h>
#include <Core/ShellApi.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/UpdateChecker.h>

// Only the startup prompt can be turned off; the banner always announces a new
// release. Prefixed `zs` because the client shares SPConfig.cfg with OpenSpades.
DEFINE_SPADES_SETTING(cl_zsUpdatePrompt, "1");

// Nightly builds only: the nightly the startup prompt last announced. Nightlies
// come with every push, so each one is prompted for once; the banner stays.
DEFINE_SPADES_SETTING(cl_zsNightlyPromptedCommit, "");

namespace spades {
	namespace gui {
		namespace {
			// Warm accent so the notice reads apart from the grey menu chrome.
			const Vector3 kAccent = MakeVector3(1.0F, 0.72F, 0.2F);

			// Quiet confirmation colour; it should not compete with the menu.
			const Vector4 kUpToDateColor = MakeVector4(0.45F, 0.9F, 0.45F, 0.85F);
			constexpr float kCheckMarkSize = 10.0F;
			constexpr float kCheckMarkThickness = 2.0F;
			constexpr float kCheckMarkGap = 6.0F;

			constexpr float kPromptHeight = 170.0F;

			// Distance from the top and right edges of the screen to the up-to-date mark.
			constexpr float kScreenMargin = 8.0F;

			constexpr float kUpToDateFadeDuration = 0.5F;

			/**
			 * Opens the release page and, once the browser has it, runs `opened`
			 * (which closes the application). If the browser could not be launched
			 * the game stays open, so the user is not left with neither.
			 */
			void OpenReleasePage(const std::string& url, const std::function<void()>& opened) {
				if (!OpenURLInBrowser(url)) {
					SPLog("[!] Could not open the release page: %s", url.c_str());
					return;
				}
				if (opened)
					opened();
			}
		} // namespace

		// -- UpdateNotice --

		UpdateNotice::UpdateNotice(ui::UIManager* manager, std::function<void()> releasePageOpened,
		                           bool showUpToDate)
		    : ButtonBase(manager),
		      showUpToDate(showUpToDate),
		      releasePageOpened(std::move(releasePageOpened)) {
			isMouseInteractive = false;
			Refresh();
			fadeInUpToDate = !upToDate;
		}

		void UpdateNotice::Refresh() {
			// A found update is final for this process; resolve it only once.
			if (isMouseInteractive)
				return;

			UpdateChecker::Result result = UpdateChecker::Get().GetResult();
			upToDate = result.state == UpdateChecker::State::UpToDate;
			if (result.state != UpdateChecker::State::UpdateAvailable)
				return;

			isMouseInteractive = true;
			releaseUrl = result.releaseUrl;
			if (!UpdateChecker::IsNightlyBuild())
				caption =
				  _Tr("MainScreen",
				      "ZeroSpades {0} is available (you have {1}). Click to open the release page.",
				      result.latestVersion.ToString(), ReleaseVersion::Current().ToString());
			else if (result.commitsAhead >= 0)
				caption = _TrN("MainScreen",
				               "A newer nightly is out ({1}, {0} new commit; you have {2}). Click "
				               "to open the release page.",
				               "A newer nightly is out ({1}, {0} new commits; you have {2}). Click "
				               "to open the release page.",
				               result.commitsAhead, result.latestCommit,
				               UpdateChecker::CurrentNightlyCommit());
			else
				caption = _Tr("MainScreen",
				              "A newer nightly is out ({0}; you have {1}). Click to open the "
				              "release page.",
				              result.latestCommit, UpdateChecker::CurrentNightlyCommit());
		}

		void UpdateNotice::OnActivated() {
			if (!releaseUrl.empty())
				OpenReleasePage(releaseUrl, releasePageOpened);
			ButtonBase::OnActivated();
		}

		void UpdateNotice::RenderUpToDate() {
			client::IFont* font = GetFont();
			if (!font)
				return;

			client::IRenderer& r = GetManager().GetRenderer();

			// fade in from the first frame the mark is drawn
			float alpha = 1.0F;
			if (fadeInUpToDate) {
				if (upToDateSince < 0.0F)
					upToDateSince = GetManager().time;
				alpha = Clamp((GetManager().time - upToDateSince) / kUpToDateFadeDuration, 0.0F, 1.0F);
			}
			Vector4 color = kUpToDateColor;
			color.w *= alpha;

			std::string text =
			  UpdateChecker::IsNightlyBuild()
			    ? _Tr("MainScreen", "Up to date (nightly {0})",
			          UpdateChecker::CurrentNightlyCommit())
			    : _Tr("MainScreen", "Up to date ({0})", ReleaseVersion::Current().ToString());
			Vector2 txtSize = font->Measure(text);

			// Anchored to the top right corner of the screen instead of the strip, so the
			// margin stays the same whatever the width of the window.
			Vector2 txtPos =
			  MakeVector2(GetManager().screenWidth - kScreenMargin - txtSize.x, kScreenMargin);
			font->DrawShadow(text, txtPos, 1.0F, color,
							 MakeVector4(0.0F, 0.0F, 0.0F, 0.4F * alpha));

			// A check mark drawn as two strokes, so it does not depend on the font
			// carrying a check glyph.
			const float s = kCheckMarkSize;
			const float x = txtPos.x - kCheckMarkGap - s;
			const float y = txtPos.y + (txtSize.y - s) * 0.5F;
			const Vector2 start = MakeVector2(x, y + s * 0.55F);
			const Vector2 corner = MakeVector2(x + s * 0.38F, y + s * 0.9F);
			const Vector2 end = MakeVector2(x + s, y + s * 0.1F);
			ui::SetColorNP(r, color);
			r.DrawLine(start, corner, kCheckMarkThickness);
			r.DrawLine(corner, end, kCheckMarkThickness);
		}

		void UpdateNotice::Render() {
			Refresh();
			if (!isMouseInteractive) {
				if (showUpToDate && upToDate && !(upToDateCovered && upToDateCovered()))
					RenderUpToDate();
				return;
			}

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

		// -- UpdatePromptScreen --

		void UpdatePromptScreen::ShowIfPending(ui::UIElement* owner,
		                                       std::function<void()> releasePageOpened) {
			// Wait for the owner to be free, so the prompt never stacks on top of
			// another dialog (e.g. the first-launch profile prompt).
			if (!owner->IsEnabled())
				return;
			if (!static_cast<bool>(cl_zsUpdatePrompt))
				return;
			const bool nightly = UpdateChecker::IsNightlyBuild();
			if (nightly) {
				// Peek before claiming: a nightly already prompted for only gets the banner.
				UpdateChecker::Result peek = UpdateChecker::Get().GetResult();
				if (peek.state == UpdateChecker::State::UpdateAvailable &&
				    peek.latestCommit == static_cast<std::string>(cl_zsNightlyPromptedCommit))
					return;
			}
			if (!UpdateChecker::Get().ClaimStartupPrompt())
				return;

			UpdateChecker::Result result = UpdateChecker::Get().GetResult();
			std::string text =
			  nightly ? _Tr("MainScreen",
			                "A newer ZeroSpades nightly is available!\n\n"
			                "Installed nightly: {0}\nLatest nightly: {1}\n\n"
			                "Open the release page to download it? ZeroSpades will close so the "
			                "update can be installed.",
			                UpdateChecker::CurrentNightlyCommit(), result.latestCommit)
			          : _Tr("MainScreen",
			                "A new version of ZeroSpades is available!\n\n"
			                "Installed version: {0}\nLatest version: {1}\n\n"
			                "Open the release page to download it? ZeroSpades will close so the "
			                "update can be installed.",
			                ReleaseVersion::Current().ToString(), result.latestVersion.ToString());

			Handle<UpdatePromptScreen> prompt =
			  Handle<UpdatePromptScreen>::New(owner, text, result.releaseUrl,
			                                  std::move(releasePageOpened));
			prompt->Run();
		}

		UpdatePromptScreen::UpdatePromptScreen(ui::UIElement* owner, const std::string& text,
		                                       const std::string& releaseUrl,
		                                       std::function<void()> releasePageOpened)
		    : MessageBoxScreen(owner, text,
		                       {_Tr("MainScreen", "Open Release Page"), _Tr("MainScreen", "Later")},
		                       kPromptHeight, true) {
			closed = [releaseUrl, releasePageOpened](ui::UIElement& sender) {
				// Answered either way: do not prompt for this nightly again. A prompt
				// closed unanswered (the game quit under it) is shown again next time.
				const int answer = static_cast<UpdatePromptScreen&>(sender).resultIndex;
				if (answer >= 0 && UpdateChecker::IsNightlyBuild())
					cl_zsNightlyPromptedCommit = UpdateChecker::Get().GetResult().latestCommit;
				if (answer == 0)
					OpenReleasePage(releaseUrl, releasePageOpened);
			};
		}

		UpdatePromptScreen::~UpdatePromptScreen() {
			// Dropped without an answer: a resize rebuilds the main screen and
			// detaches every dialog. Let the rebuilt menu show the prompt again.
			if (resultIndex < 0)
				UpdateChecker::Get().ReleaseStartupPrompt();
		}

		void UpdatePromptScreen::HotKey(const std::string& key) {
			// Enter is swallowed rather than bound to Open Release Page: the prompt
			// appears on its own, so an Enter meant for the menu (joining a server)
			// would otherwise open the browser and quit the game.
			if (key == "Enter")
				return;
			if (IsEnabled() && key == "Escape") {
				EndDialog(1);
			} else {
				MessageBoxScreen::HotKey(key);
			}
		}
	} // namespace gui
} // namespace spades
