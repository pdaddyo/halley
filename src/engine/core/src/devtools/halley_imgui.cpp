#include "halley/devtools/halley_imgui.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <imgui/imgui.h>

#include <halley/api/video_api.h>
#include <halley/api/input_api.h>
#include <halley/graphics/painter.h>
#include <halley/graphics/material/material.h>
#include <halley/graphics/material/material_definition.h>
#include <halley/graphics/texture.h>
#include <halley/graphics/texture_descriptor.h>
#include <halley/resources/resources.h>
#include <halley/input/input_keyboard.h>
#include <halley/input/input_device.h>
#include <halley/input/input_keys.h>
#include <halley/maths/rect.h>
#include <halley/text/halleystring.h>

using namespace Halley;

namespace {
	// Halley KeyCode == USB HID / SDL scancode. Map the keys ImGui cares about for
	// navigation and editing onto ImGuiKey. Letters/digits are handled by range below.
	ImGuiKey toImGuiKey(KeyCode kc)
	{
		const int c = static_cast<int>(kc);
		// Letters A..Z are scancodes 4..29, contiguous, matching ImGuiKey_A..Z.
		if (c >= static_cast<int>(KeyCode::A) && c <= static_cast<int>(KeyCode::A) + 25) {
			return static_cast<ImGuiKey>(ImGuiKey_A + (c - static_cast<int>(KeyCode::A)));
		}
		switch (kc) {
		case KeyCode::Num1: return ImGuiKey_1;
		case KeyCode::Num2: return ImGuiKey_2;
		case KeyCode::Num3: return ImGuiKey_3;
		case KeyCode::Num4: return ImGuiKey_4;
		case KeyCode::Num5: return ImGuiKey_5;
		case KeyCode::Num6: return ImGuiKey_6;
		case KeyCode::Num7: return ImGuiKey_7;
		case KeyCode::Num8: return ImGuiKey_8;
		case KeyCode::Num9: return ImGuiKey_9;
		case KeyCode::Num0: return ImGuiKey_0;
		case KeyCode::Enter: return ImGuiKey_Enter;
		case KeyCode::KeypadEnter: return ImGuiKey_KeypadEnter;
		case KeyCode::Esc: return ImGuiKey_Escape;
		case KeyCode::Backspace: return ImGuiKey_Backspace;
		case KeyCode::Tab: return ImGuiKey_Tab;
		case KeyCode::Space: return ImGuiKey_Space;
		case KeyCode::Delete: return ImGuiKey_Delete;
		case KeyCode::Home: return ImGuiKey_Home;
		case KeyCode::End: return ImGuiKey_End;
		case KeyCode::PageUp: return ImGuiKey_PageUp;
		case KeyCode::PageDown: return ImGuiKey_PageDown;
		case KeyCode::Right: return ImGuiKey_RightArrow;
		case KeyCode::Left: return ImGuiKey_LeftArrow;
		case KeyCode::Down: return ImGuiKey_DownArrow;
		case KeyCode::Up: return ImGuiKey_UpArrow;
		case KeyCode::Minus: return ImGuiKey_Minus;
		case KeyCode::Equals: return ImGuiKey_Equal;
		case KeyCode::Comma: return ImGuiKey_Comma;
		case KeyCode::Period: return ImGuiKey_Period;
		default: return ImGuiKey_None;
		}
	}

	// Translate a printable key press into an ASCII character for ImGui text input.
	// Good enough for a debug overlay (US layout, common symbols); returns 0 to skip.
	unsigned int keyToChar(KeyCode kc, bool shift)
	{
		const int c = static_cast<int>(kc);
		if (c >= static_cast<int>(KeyCode::A) && c <= static_cast<int>(KeyCode::A) + 25) {
			const char base = static_cast<char>('a' + (c - static_cast<int>(KeyCode::A)));
			return static_cast<unsigned int>(shift ? (base - 'a' + 'A') : base);
		}
		switch (kc) {
		case KeyCode::Num1: return shift ? '!' : '1';
		case KeyCode::Num2: return shift ? '@' : '2';
		case KeyCode::Num3: return shift ? '#' : '3';
		case KeyCode::Num4: return shift ? '$' : '4';
		case KeyCode::Num5: return shift ? '%' : '5';
		case KeyCode::Num6: return shift ? '^' : '6';
		case KeyCode::Num7: return shift ? '&' : '7';
		case KeyCode::Num8: return shift ? '*' : '8';
		case KeyCode::Num9: return shift ? '(' : '9';
		case KeyCode::Num0: return shift ? ')' : '0';
		case KeyCode::Space: return ' ';
		case KeyCode::Minus: return shift ? '_' : '-';
		case KeyCode::Equals: return shift ? '+' : '=';
		case KeyCode::Comma: return shift ? '<' : ',';
		case KeyCode::Period: return shift ? '>' : '.';
		case KeyCode::Slash: return shift ? '?' : '/';
		default: return 0;
		}
	}
}

HalleyImGui::HalleyImGui(Resources& resources, VideoAPI& video, const String& materialName,
	const void* fontData, int fontDataSize, float fontPixelSize)
{
	IMGUI_CHECKVERSION();
	auto* ctx = ImGui::CreateContext();
	context = ctx;
	ImGui::SetCurrentContext(ctx);

	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr; // don't write imgui.ini into the working directory
	io.LogFilename = nullptr;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
	io.BackendRendererName = "halley_painter";
	io.BackendPlatformName = "halley_input";
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	applyTheme();

	// The material definition is cloned once per bound texture (the font atlas + any game sprite
	// textures), so different ImGui images never get batched onto the wrong texture.
	materialDef = resources.get<MaterialDefinition>(materialName);

	buildFont(video, fontData, fontDataSize, fontPixelSize);
}

HalleyImGui::~HalleyImGui()
{
	if (context) {
		ImGui::DestroyContext(static_cast<ImGuiContext*>(context));
		context = nullptr;
	}
}

void HalleyImGui::applyTheme()
{
	ImGui::StyleColorsDark();
	ImGuiStyle& s = ImGui::GetStyle();
	s.WindowRounding = 5.0f;
	s.ChildRounding = 5.0f;
	s.FrameRounding = 4.0f;
	s.PopupRounding = 4.0f;
	s.GrabRounding = 4.0f;
	s.TabRounding = 4.0f;
	s.ScrollbarRounding = 4.0f;
	s.WindowBorderSize = 1.0f;
	s.FrameBorderSize = 0.0f;
	s.WindowPadding = ImVec2(10.0f, 8.0f);
	s.FramePadding = ImVec2(8.0f, 4.0f);
	s.ItemSpacing = ImVec2(8.0f, 6.0f);
	s.GrabMinSize = 9.0f;
	s.ScrollbarSize = 12.0f;
	s.WindowTitleAlign = ImVec2(0.0f, 0.5f);

	ImVec4* c = s.Colors;
	const ImVec4 accent(0.26f, 0.59f, 0.98f, 1.0f);
	const ImVec4 accentDim(0.26f, 0.59f, 0.98f, 0.55f);
	c[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.105f, 0.12f, 0.96f);
	c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.16f);
	c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.105f, 0.12f, 0.98f);
	c[ImGuiCol_Border] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
	c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.17f, 0.19f, 1.0f);
	c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.24f, 0.27f, 1.0f);
	c[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.28f, 0.32f, 1.0f);
	c[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.085f, 0.10f, 1.0f);
	c[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.135f, 0.17f, 1.0f);
	c[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.125f, 0.14f, 1.0f);
	c[ImGuiCol_Header] = ImVec4(0.20f, 0.30f, 0.45f, 0.65f);
	c[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.40f, 0.62f, 0.80f);
	c[ImGuiCol_HeaderActive] = accentDim;
	c[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.26f, 1.0f);
	c[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.40f, 0.60f, 1.0f);
	c[ImGuiCol_ButtonActive] = accent;
	c[ImGuiCol_SliderGrab] = accent;
	c[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.70f, 1.0f, 1.0f);
	c[ImGuiCol_CheckMark] = ImVec4(0.45f, 0.72f, 1.0f, 1.0f);
	c[ImGuiCol_Tab] = ImVec4(0.13f, 0.15f, 0.18f, 1.0f);
	c[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.40f, 0.60f, 0.9f);
	c[ImGuiCol_TabActive] = ImVec4(0.20f, 0.32f, 0.48f, 1.0f);
	c[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.12f, 0.14f, 1.0f);
	c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16f, 0.20f, 0.26f, 1.0f);
	c[ImGuiCol_Separator] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
	c[ImGuiCol_TableHeaderBg] = ImVec4(0.16f, 0.17f, 0.20f, 1.0f);
	c[ImGuiCol_TableBorderStrong] = ImVec4(0.0f, 0.0f, 0.0f, 0.6f);
	c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);
}

uint64_t HalleyImGui::registerTexture(std::shared_ptr<const Texture> texture)
{
	if (!texture || !materialDef) {
		return 0;
	}
	const uint64_t id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(texture.get()));
	if (materials.find(id) == materials.end()) {
		auto mat = materialDef->createMaterial();
		mat->set("tex", texture);
		materials.emplace(id, std::move(mat));
	}
	return id;
}

void HalleyImGui::buildFont(VideoAPI& video, const void* fontData, int fontDataSize, float fontPixelSize)
{
	ImGuiIO& io = ImGui::GetIO();
	if (fontData && fontDataSize > 0) {
		ImFontConfig cfg;
		cfg.FontDataOwnedByAtlas = false; // caller owns the blob (e.g. a static array)
		io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(fontData), fontDataSize,
			fontPixelSize > 0.0f ? fontPixelSize : 13.0f, &cfg);
	}

	unsigned char* pixels = nullptr;
	int w = 0, h = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
	if (!pixels || w <= 0 || h <= 0) {
		return;
	}

	const size_t numBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
	TextureDescriptor desc(Vector2i(w, h), TextureFormat::RGBA);
	desc.pixelFormat = PixelDataFormat::Image;
	desc.useFiltering = true;
	desc.useMipMap = false;
	desc.addressMode = TextureAddressMode::Clamp;
	desc.pixelData = TextureDescriptorImageData(
		gsl::span<const std::byte>(reinterpret_cast<const std::byte*>(pixels), numBytes));

	auto tex = video.createTexture(Vector2i(w, h));
	tex->load(std::move(desc));
	fontTexture = std::move(tex);

	fontTexId = registerTexture(fontTexture);
	io.Fonts->SetTexID(static_cast<ImTextureID>(fontTexId));
}

void HalleyImGui::newFrame(InputAPI& input, Vector2f displaySize, Time deltaTime)
{
	if (!context) {
		return;
	}
	ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
	ImGuiIO& io = ImGui::GetIO();

	// If the previous frame was never rendered, discard it so NewFrame stays balanced.
	if (frameStarted) {
		ImGui::EndFrame();
		frameStarted = false;
	}

	io.DisplaySize = ImVec2(std::max(1.0f, displaySize.x), std::max(1.0f, displaySize.y));
	io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
	io.DeltaTime = std::max(1.0f / 1000.0f, static_cast<float>(deltaTime));

	// Mouse (positions are in the same render-target pixel space as displaySize).
	if (auto mouse = input.getMouse(0)) {
		const Vector2f pos = mouse->getPosition();
		io.AddMousePosEvent(pos.x, pos.y);
		io.AddMouseButtonEvent(0, mouse->isButtonDown(static_cast<int>(MouseButton::Left)));
		io.AddMouseButtonEvent(1, mouse->isButtonDown(static_cast<int>(MouseButton::Right)));
		io.AddMouseButtonEvent(2, mouse->isButtonDown(static_cast<int>(MouseButton::Middle)));
		const Vector2f wheel = mouse->getWheelMove();
		if (wheel.x != 0.0f || wheel.y != 0.0f) {
			io.AddMouseWheelEvent(wheel.x, wheel.y);
		}
	}

	// Keyboard.
	if (auto kb = input.getKeyboard(0)) {
		const KeyMods mods = kb->getKeyMods();
		const bool shift = (mods & KeyMods::Shift) != KeyMods::None;
		io.AddKeyEvent(ImGuiMod_Shift, shift);
		io.AddKeyEvent(ImGuiMod_Ctrl, (mods & KeyMods::Ctrl) != KeyMods::None);
		io.AddKeyEvent(ImGuiMod_Alt, (mods & KeyMods::Alt) != KeyMods::None);
		io.AddKeyEvent(ImGuiMod_Super, (mods & KeyMods::Mod) != KeyMods::None);

		// Feed held-state for the navigation/edit/shortcut keys ImGui understands.
		static const KeyCode tracked[] = {
			KeyCode::Enter, KeyCode::KeypadEnter, KeyCode::Esc, KeyCode::Backspace, KeyCode::Tab,
			KeyCode::Space, KeyCode::Delete, KeyCode::Home, KeyCode::End, KeyCode::PageUp,
			KeyCode::PageDown, KeyCode::Left, KeyCode::Right, KeyCode::Up, KeyCode::Down,
			KeyCode::Minus, KeyCode::Equals, KeyCode::Comma, KeyCode::Period,
		};
		for (KeyCode kc : tracked) {
			const ImGuiKey k = toImGuiKey(kc);
			if (k != ImGuiKey_None) {
				io.AddKeyEvent(k, kb->isButtonDown(static_cast<int>(kc)));
			}
		}
		// Letters + digits held state (for ctrl+C / ctrl+V / ctrl+A / ctrl+Z etc.).
		for (int c = static_cast<int>(KeyCode::A); c <= static_cast<int>(KeyCode::A) + 25; ++c) {
			io.AddKeyEvent(static_cast<ImGuiKey>(ImGuiKey_A + (c - static_cast<int>(KeyCode::A))),
				kb->isButtonDown(c));
		}

		// Text input: one character per fresh key press this frame.
		for (const auto& kp : kb->getPendingKeys()) {
			const bool kpShift = (kp.mod & KeyMods::Shift) != KeyMods::None;
			const bool ctrlOrAlt = ((kp.mod & KeyMods::Ctrl) != KeyMods::None)
				|| ((kp.mod & KeyMods::Alt) != KeyMods::None);
			if (ctrlOrAlt) {
				continue; // don't type characters for shortcuts
			}
			if (const unsigned int ch = keyToChar(kp.key, kpShift)) {
				io.AddInputCharacter(ch);
			}
		}
	}

	ImGui::NewFrame();
	frameStarted = true;
}

void HalleyImGui::render(Painter& painter)
{
	if (!context) {
		return;
	}
	ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
	if (!frameStarted) {
		return; // nothing was built this frame
	}

	ImGui::Render();
	frameStarted = false;

	ImDrawData* dd = ImGui::GetDrawData();
	if (!dd || dd->CmdListsCount == 0 || materials.empty() || !fontTexture) {
		return;
	}

	static_assert(sizeof(IndexType) == sizeof(unsigned short), "ImGui backend assumes 16-bit indices");

	const Rect4i vp = painter.getViewPort();
	const ImVec2 dispPos = dd->DisplayPos;
	const ImVec2 dispSize = dd->DisplaySize;
	const float scaleX = dispSize.x > 0.0f ? static_cast<float>(vp.getWidth()) / dispSize.x : 1.0f;
	const float scaleY = dispSize.y > 0.0f ? static_cast<float>(vp.getHeight()) / dispSize.y : 1.0f;

	for (int n = 0; n < dd->CmdListsCount; ++n) {
		const ImDrawList* drawList = dd->CmdLists[n];
		const size_t vtxCount = static_cast<size_t>(drawList->VtxBuffer.Size);
		if (vtxRemap.size() < vtxCount) {
			vtxRemap.resize(vtxCount);
			vtxRemapGen.resize(vtxCount, 0);
		}

		for (int c = 0; c < drawList->CmdBuffer.Size; ++c) {
			const ImDrawCmd& cmd = drawList->CmdBuffer[c];
			if (cmd.UserCallback || cmd.ElemCount == 0) {
				continue; // we don't issue user callbacks
			}

			// Clip rect: ImGui display space -> render-target pixels.
			const float cx1 = (cmd.ClipRect.x - dispPos.x) * scaleX + static_cast<float>(vp.getLeft());
			const float cy1 = (cmd.ClipRect.y - dispPos.y) * scaleY + static_cast<float>(vp.getTop());
			const float cx2 = (cmd.ClipRect.z - dispPos.x) * scaleX + static_cast<float>(vp.getLeft());
			const float cy2 = (cmd.ClipRect.w - dispPos.y) * scaleY + static_cast<float>(vp.getTop());
			if (cx2 <= cx1 || cy2 <= cy1) {
				continue;
			}

			// Compact this command to exactly the vertices it references, with 0-based indices,
			// expanding the packed RGBA8 colour to float4 (Halley has no ubyte4 attribute).
			// Indices are relative to VtxOffset (RendererHasVtxOffset is enabled).
			++remapGen;
			cmdVerts.clear();
			cmdIdx.clear();
			cmdIdx.reserve(cmd.ElemCount);
			const ImDrawIdx* idxBuf = drawList->IdxBuffer.Data + cmd.IdxOffset;
			for (unsigned int i = 0; i < cmd.ElemCount; ++i) {
				const size_t orig = static_cast<size_t>(cmd.VtxOffset) + idxBuf[i];
				if (vtxRemapGen[orig] != remapGen) {
					vtxRemapGen[orig] = remapGen;
					vtxRemap[orig] = static_cast<int>(cmdVerts.size());
					const ImDrawVert& v = drawList->VtxBuffer[static_cast<int>(orig)];
					const unsigned int col = v.col;
					cmdVerts.push_back(HalleyImGuiVertex{
						Vector2f(v.pos.x, v.pos.y),
						Vector2f(v.uv.x, v.uv.y),
						Vector4f(
							static_cast<float>(col & 0xFF) / 255.0f,
							static_cast<float>((col >> 8) & 0xFF) / 255.0f,
							static_cast<float>((col >> 16) & 0xFF) / 255.0f,
							static_cast<float>((col >> 24) & 0xFF) / 255.0f) });
				}
				cmdIdx.push_back(static_cast<unsigned short>(vtxRemap[orig]));
			}
			if (cmdVerts.empty()) {
				continue;
			}

			// Pick the material for this command's texture (font atlas or a registered sprite).
			const uint64_t texId = static_cast<uint64_t>(cmd.GetTexID());
			auto matIt = materials.find(texId);
			if (matIt == materials.end()) {
				matIt = materials.find(fontTexId);
			}
			if (matIt == materials.end()) {
				continue;
			}

			painter.setClip(Rect4i(static_cast<int>(cx1), static_cast<int>(cy1),
				static_cast<int>(cx2 - cx1), static_cast<int>(cy2 - cy1)));
			painter.draw(matIt->second, cmdVerts.size(), cmdVerts.data(),
				gsl::span<const IndexType>(reinterpret_cast<const IndexType*>(cmdIdx.data()), cmdIdx.size()),
				PrimitiveType::Triangle);
		}
	}

	painter.setClip(std::nullopt);
}

bool HalleyImGui::wantCaptureMouse() const
{
	if (!context) {
		return false;
	}
	ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
	return ImGui::GetIO().WantCaptureMouse;
}

bool HalleyImGui::wantCaptureKeyboard() const
{
	if (!context) {
		return false;
	}
	ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
	return ImGui::GetIO().WantCaptureKeyboard;
}
