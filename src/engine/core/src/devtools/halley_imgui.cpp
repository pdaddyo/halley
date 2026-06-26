#include "halley/devtools/halley_imgui.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <imgui/imgui.h>

#include <halley/api/video_api.h>
#include <halley/api/system_api.h>
#include <halley/api/input_api.h>
#include <halley/graphics/painter.h>
#include <halley/graphics/window.h>
#include <halley/graphics/camera.h>
#include <halley/graphics/render_context.h>
#include <halley/graphics/render_target/render_target_screen.h>
#include <halley/graphics/material/material.h>
#include <halley/graphics/material/material_definition.h>
#include <halley/graphics/texture.h>
#include <halley/graphics/texture_descriptor.h>
#include <halley/resources/resources.h>
#include <halley/input/input_keyboard.h>
#include <halley/input/input_device.h>
#include <halley/input/input_keys.h>
#include <halley/maths/rect.h>
#include <halley/maths/colour.h>
#include <halley/text/halleystring.h>

using namespace Halley;

// A handful of SDL2 calls have no Halley::Window equivalent (raise/title/focus query, the global
// mouse, display enumeration). engine-core has no SDL headers in its include path, but the final
// non-embed executable links SDL2, so we declare the few C symbols we need. SDL_Window* /
// SDL_GLContext are opaque pointers, so void* matches at the ABI level under C linkage.
extern "C" {
	int SDL_GL_MakeCurrent(void* window, void* context);
	int SDL_GL_SetSwapInterval(int interval);
	int SDL_GL_GetSwapInterval(void);
	void SDL_SetWindowTitle(void* window, const char* title);
	void SDL_SetWindowPosition(void* window, int x, int y);
	void SDL_SetWindowSize(void* window, int w, int h);
	void SDL_RaiseWindow(void* window);
	unsigned int SDL_GetWindowFlags(void* window);
	unsigned int SDL_GetGlobalMouseState(int* x, int* y);
	int SDL_GetNumVideoDisplays(void);
	int SDL_GetDisplayBounds(int displayIndex, void* rect);      // rect = SDL_Rect{int x,y,w,h}
	int SDL_GetDisplayUsableBounds(int displayIndex, void* rect);
}

namespace {
	// Mirror of the SDL constants we test (SDL_video.h / SDL_mouse.h). Stable ABI values.
	constexpr unsigned int kSDL_WINDOW_INPUT_FOCUS = 0x00000200u;
	constexpr unsigned int kSDL_WINDOW_MINIMIZED   = 0x00000040u;
	constexpr unsigned int kSDL_BUTTON_LMASK = 1u;       // SDL_BUTTON(1)
	constexpr unsigned int kSDL_BUTTON_MMASK = 2u;       // SDL_BUTTON(2)
	constexpr unsigned int kSDL_BUTTON_RMASK = 4u;       // SDL_BUTTON(3)

	struct SDLRectABI { int x, y, w, h; };               // matches SDL_Rect layout exactly

	// Stashed in ImGuiViewport::PlatformUserData. Owns the secondary OS window (the app-owned main
	// viewport has window == nullptr and ownedByApp == true, and is never created/destroyed here).
	struct ImGuiViewportData {
		std::shared_ptr<Window> window;
		bool ownedByApp = false;
	};

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
	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors; // enables window edge/corner resize cursor feedback
	io.BackendRendererName = "halley_painter";
	io.BackendPlatformName = "halley_input";
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigWindowsResizeFromEdges = true; // grab any window edge/corner to resize (not just the grip)

	applyTheme();

	// The material definition is cloned once per bound texture (the font atlas + any game sprite
	// textures), so different ImGui images never get batched onto the wrong texture.
	materialDef = resources.get<MaterialDefinition>(materialName);

	buildFont(video, fontData, fontDataSize, fontPixelSize);
}

HalleyImGui::~HalleyImGui()
{
	if (context) {
		ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
		if (viewportsEnabled) {
			// Close all secondary OS windows. DestroyPlatformWindows() runs Platform_DestroyWindow for
			// every viewport INCLUDING the app-owned main one (which deletes + nulls its data but skips
			// the SDL destroy, since its window is null). The delete below is a null-safe fallback for
			// the rare case the main viewport's platform window was never created.
			ImGui::DestroyPlatformWindows();
			if (ImGuiViewport* mv = ImGui::GetMainViewport()) {
				delete static_cast<ImGuiViewportData*>(mv->PlatformUserData);
				mv->PlatformUserData = nullptr;
			}
			// Shut the platform backend down cleanly: clearing BackendPlatformUserData + the viewport
			// flags is what DestroyContext's sanity check looks for ("Forgot to shutdown Platform backend?").
			ImGuiIO& io = ImGui::GetIO();
			io.BackendPlatformUserData = nullptr;
			io.BackendFlags &= ~(ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports);
			viewportsEnabled = false;
		}
		ImGui::DestroyContext(static_cast<ImGuiContext*>(context));
		context = nullptr;
	}
}

void HalleyImGui::applyTheme()
{
	// Spacious style metrics inspired by ole.kristensen's "ledSynthmaster" Dear ImGui style
	// (generous padding/spacing, rounded frames). Set once; setColorScheme() only re-tints.
	ImGuiStyle& s = ImGui::GetStyle();
	s.WindowRounding = 9.0f;
	s.ChildRounding = 8.0f;
	s.FrameRounding = 6.0f;
	s.PopupRounding = 8.0f;
	s.GrabRounding = 4.0f;
	s.TabRounding = 7.0f;
	s.ScrollbarRounding = 10.0f;
	s.WindowBorderSize = 1.0f;
	s.FrameBorderSize = 0.0f;
	s.WindowPadding = ImVec2(15.0f, 15.0f);
	s.FramePadding = ImVec2(8.0f, 5.0f);
	s.ItemSpacing = ImVec2(12.0f, 8.0f);
	s.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
	s.IndentSpacing = 22.0f;
	s.GrabMinSize = 10.0f;
	s.ScrollbarSize = 15.0f;
	s.WindowTitleAlign = ImVec2(0.0f, 0.5f);

	applyColors(darkTheme);
}

void HalleyImGui::applyColors(bool dark)
{
	// Start from a stock palette so every colour slot (including any the vendored ImGui adds, e.g.
	// docking) is filled coherently, then overlay our own. Dark mode is a muted palette pulled from
	// the PB logo (deep navy/teal field + coral accent); light mode keeps the ledSynthmaster cream+lime.
	if (dark) {
		ImGui::StyleColorsDark();
	} else {
		ImGui::StyleColorsLight();
	}

	ImVec4* c = ImGui::GetStyle().Colors;
	// Accent: muted coral from the PB logo for dark mode; ledSynthmaster lime for light mode.
	const ImVec4 accent = dark ? ImVec4(0.76f, 0.46f, 0.44f, 1.0f) : ImVec4(0.40f, 0.82f, 0.12f, 1.0f);
	const ImVec4 accentBright = dark ? ImVec4(0.86f, 0.56f, 0.53f, 1.0f) : ImVec4(0.52f, 0.95f, 0.22f, 1.0f);

	if (dark) {
		// Muted PB-logo palette: deep navy/teal field, teal interactive tints, coral accent (set below).
		const ImVec4 tealHover(0.18f, 0.27f, 0.34f, 1.0f);
		c[ImGuiCol_Text] = ImVec4(0.85f, 0.87f, 0.90f, 1.0f);
		c[ImGuiCol_TextDisabled] = ImVec4(0.49f, 0.53f, 0.58f, 1.0f);
		c[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.10f, 0.145f, 0.97f);
		c[ImGuiCol_ChildBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);
		c[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.095f, 0.135f, 0.98f);
		c[ImGuiCol_Border] = ImVec4(0.30f, 0.40f, 0.50f, 0.22f);
		c[ImGuiCol_FrameBg] = ImVec4(0.135f, 0.165f, 0.215f, 1.0f);
		c[ImGuiCol_FrameBgHovered] = tealHover;
		c[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.31f, 0.39f, 1.0f);
		c[ImGuiCol_TitleBg] = ImVec4(0.075f, 0.095f, 0.13f, 1.0f);
		c[ImGuiCol_TitleBgActive] = ImVec4(0.135f, 0.225f, 0.30f, 1.0f);
		c[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.13f, 0.17f, 1.0f);
		c[ImGuiCol_Button] = ImVec4(0.155f, 0.195f, 0.255f, 1.0f);
		c[ImGuiCol_ButtonHovered] = tealHover;
		c[ImGuiCol_Tab] = ImVec4(0.105f, 0.135f, 0.18f, 1.0f);
		c[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.31f, 0.40f, 0.90f);
		c[ImGuiCol_TabActive] = ImVec4(0.17f, 0.26f, 0.34f, 1.0f);
		c[ImGuiCol_TabUnfocused] = ImVec4(0.09f, 0.11f, 0.15f, 1.0f);
		c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.135f, 0.19f, 0.25f, 1.0f);
		c[ImGuiCol_Separator] = ImVec4(0.30f, 0.40f, 0.50f, 0.25f);
		c[ImGuiCol_TableHeaderBg] = ImVec4(0.125f, 0.16f, 0.21f, 1.0f);
		c[ImGuiCol_TableBorderStrong] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
		c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.025f);
	} else {
		const ImVec4 cream(1.0f, 0.99f, 0.96f, 1.0f);
		const ImVec4 greenHover(0.80f, 0.90f, 0.62f, 1.0f);
		c[ImGuiCol_Text] = ImVec4(0.22f, 0.21f, 0.19f, 1.0f);
		c[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.51f, 0.48f, 1.0f);
		c[ImGuiCol_WindowBg] = ImVec4(0.93f, 0.92f, 0.89f, 1.0f);
		c[ImGuiCol_ChildBg] = ImVec4(1.0f, 0.99f, 0.96f, 0.50f);
		c[ImGuiCol_PopupBg] = ImVec4(0.96f, 0.95f, 0.92f, 0.98f);
		c[ImGuiCol_Border] = ImVec4(0.74f, 0.73f, 0.69f, 0.80f);
		c[ImGuiCol_FrameBg] = cream;
		c[ImGuiCol_FrameBgHovered] = greenHover;
		c[ImGuiCol_FrameBgActive] = ImVec4(0.72f, 0.86f, 0.50f, 1.0f);
		c[ImGuiCol_TitleBg] = ImVec4(0.86f, 0.85f, 0.81f, 1.0f);
		c[ImGuiCol_TitleBgActive] = ImVec4(0.62f, 0.82f, 0.40f, 1.0f);
		c[ImGuiCol_MenuBarBg] = ImVec4(0.89f, 0.88f, 0.85f, 1.0f);
		c[ImGuiCol_Button] = ImVec4(0.84f, 0.83f, 0.79f, 1.0f);
		c[ImGuiCol_ButtonHovered] = greenHover;
		c[ImGuiCol_Tab] = ImVec4(0.84f, 0.83f, 0.79f, 1.0f);
		c[ImGuiCol_TabHovered] = ImVec4(0.74f, 0.88f, 0.52f, 0.90f);
		c[ImGuiCol_TabActive] = ImVec4(0.74f, 0.86f, 0.55f, 1.0f);
		c[ImGuiCol_TabUnfocused] = ImVec4(0.88f, 0.87f, 0.83f, 1.0f);
		c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.80f, 0.86f, 0.66f, 1.0f);
		c[ImGuiCol_Separator] = ImVec4(0.0f, 0.0f, 0.0f, 0.20f);
		c[ImGuiCol_TableHeaderBg] = ImVec4(0.86f, 0.87f, 0.80f, 1.0f);
		c[ImGuiCol_TableBorderStrong] = ImVec4(0.0f, 0.0f, 0.0f, 0.30f);
		c[ImGuiCol_TableRowBgAlt] = ImVec4(0.0f, 0.0f, 0.0f, 0.03f);
	}

	// Accent-driven interactive elements (shared by both modes).
	c[ImGuiCol_ButtonActive] = accent;
	c[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.40f);
	c[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.65f);
	c[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.85f);
	c[ImGuiCol_SliderGrab] = accent;
	c[ImGuiCol_SliderGrabActive] = accentBright;
	c[ImGuiCol_CheckMark] = accentBright;
	c[ImGuiCol_SeparatorHovered] = ImVec4(accent.x, accent.y, accent.z, 0.70f);
	c[ImGuiCol_SeparatorActive] = accent;
	c[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
	c[ImGuiCol_ResizeGripActive] = accent;
	c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.40f);
	c[ImGuiCol_DragDropTarget] = accentBright;
	c[ImGuiCol_NavHighlight] = accent;
	c[ImGuiCol_PlotLinesHovered] = accentBright;
	c[ImGuiCol_PlotHistogramHovered] = accentBright;

	if (viewportsEnabled) {
		// Torn-off windows are real OS windows: keep their background opaque so the desktop doesn't
		// show through. Rounding stays on (renderViewports clears each secondary window to the window
		// background colour, so the rounded corners blend seamlessly with the rectangular OS window).
		c[ImGuiCol_WindowBg].w = 1.0f;
	}
}

void HalleyImGui::setColorScheme(bool dark)
{
	if (!context) {
		return;
	}
	ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
	darkTheme = dark;
	applyColors(dark);
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

	// Mouse position + buttons. With multi-viewports on, ImGui needs GLOBAL desktop coordinates so it
	// can hit-test the cursor across separate OS windows (and so clicks on a detached window register,
	// which the main-window InputAPI never sees) — read SDL's global mouse state there. Otherwise use
	// the engine mouse (main-window-relative, the same space as displaySize).
	auto mouse = input.getMouse(0);
	if (viewportsEnabled) {
		int gx = 0, gy = 0;
		const unsigned int mask = SDL_GetGlobalMouseState(&gx, &gy);
		io.AddMousePosEvent(static_cast<float>(gx), static_cast<float>(gy));
		io.AddMouseButtonEvent(0, (mask & kSDL_BUTTON_LMASK) != 0);
		io.AddMouseButtonEvent(1, (mask & kSDL_BUTTON_RMASK) != 0);
		io.AddMouseButtonEvent(2, (mask & kSDL_BUTTON_MMASK) != 0);
		// Keep the app-owned main viewport's rect aligned with the OS window (in the same global space
		// as the cursor) so ImGui maps the cursor to the right viewport and places torn-off windows right.
		if (mainWindow) {
			const Rect4i r = mainWindow->getWindowRect();
			if (ImGuiViewport* mv = ImGui::GetMainViewport()) {
				mv->Pos = ImVec2(static_cast<float>(r.getLeft()), static_cast<float>(r.getTop()));
				mv->Size = ImVec2(static_cast<float>(r.getWidth()), static_cast<float>(r.getHeight()));
			}
		}
	} else if (mouse) {
		const Vector2f pos = mouse->getPosition();
		io.AddMousePosEvent(pos.x, pos.y);
		io.AddMouseButtonEvent(0, mouse->isButtonDown(static_cast<int>(MouseButton::Left)));
		io.AddMouseButtonEvent(1, mouse->isButtonDown(static_cast<int>(MouseButton::Right)));
		io.AddMouseButtonEvent(2, mouse->isButtonDown(static_cast<int>(MouseButton::Middle)));
	}

	if (mouse) {
		const Vector2f wheel = mouse->getWheelMove();
		if (wheel.x != 0.0f || wheel.y != 0.0f) {
			io.AddMouseWheelEvent(wheel.x, wheel.y);
		}
		// Map ImGui's desired cursor (set last frame from hover — e.g. a window edge/corner) onto the
		// OS cursor so all four window edges read as resizable, not just the bottom-right grip.
		if (io.BackendFlags & ImGuiBackendFlags_HasMouseCursors) {
			std::optional<MouseCursorMode> mode = MouseCursorMode::Arrow;
			switch (ImGui::GetMouseCursor()) {
			case ImGuiMouseCursor_TextInput:  mode = MouseCursorMode::IBeam; break;
			case ImGuiMouseCursor_ResizeAll:  mode = MouseCursorMode::SizeAll; break;
			case ImGuiMouseCursor_ResizeNS:   mode = MouseCursorMode::SizeNS; break;
			case ImGuiMouseCursor_ResizeEW:   mode = MouseCursorMode::SizeWE; break;
			case ImGuiMouseCursor_ResizeNESW: mode = MouseCursorMode::SizeNESW; break;
			case ImGuiMouseCursor_ResizeNWSE: mode = MouseCursorMode::SizeNWSE; break;
			case ImGuiMouseCursor_Hand:       mode = MouseCursorMode::Hand; break;
			case ImGuiMouseCursor_NotAllowed: mode = MouseCursorMode::No; break;
			default:                          mode = MouseCursorMode::Arrow; break;
			}
			input.setMouseCursorMode(mode);
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

	drawCurrentDrawData(painter);
}

void HalleyImGui::renderDrawData(Painter& painter)
{
	if (!context) {
		return;
	}
	ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
	// Re-draw the draw data finished by the most recent render() this frame, without re-running
	// ImGui::Render() (that data stays valid until the next newFrame). Used to mirror the overlay
	// into a second OS window. It scales to the bound painter's viewport.
	drawCurrentDrawData(painter);
}

void HalleyImGui::drawCurrentDrawData(Painter& painter)
{
	drawDrawData(painter, ImGui::GetDrawData());
}

void HalleyImGui::drawDrawData(Painter& painter, void* imDrawData)
{
	ImDrawData* dd = static_cast<ImDrawData*>(imDrawData);
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
						// Window-local position (display origin subtracted): the per-window camera then
						// maps [0, size] onto that window's drawable. For the main viewport DisplayPos is
						// (0,0), so this is identical to the original single-window behaviour.
						Vector2f(v.pos.x - dispPos.x, v.pos.y - dispPos.y),
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

// ---- Multi-viewport implementation ------------------------------------------------------------

void HalleyImGui::enableViewports(SystemAPI& sys, VideoAPI& vid)
{
	if (!context || viewportsEnabled) {
		return;
	}
	ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
	ImGuiIO& io = ImGui::GetIO();

	// Multi-viewport rendering makes the engine's shared GL context current on each secondary window.
	// If the video backend doesn't expose one (e.g. a future Metal path), leave viewports off — the
	// docked overlay still works fine inside the main window.
	void* ctx = vid.getImplementationPointer("SDL_GLContext");
	if (!ctx) {
		return;
	}

	system = &sys;
	video = &vid;
	mainWindow = &vid.getWindow();
	glContext = ctx;

	io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable | ImGuiConfigFlags_DockingEnable;
	io.BackendPlatformUserData = this; // recovered inside the static platform callbacks

	// Torn-off panels become real OS windows: force an opaque window background so detached windows
	// don't show the desktop through translucency. (Rounding is kept — renderViewports clears each
	// secondary window to the background colour, so the rounded corners stay seamless.)
	ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 1.0f;

	setupPlatformCallbacks();
	updateMonitors();

	// Seed the app-owned main viewport: it wraps the existing engine window, so Platform_CreateWindow
	// is never called for it. Its PlatformUserData is freed at teardown (see the destructor).
	ImGuiViewport* mv = ImGui::GetMainViewport();
	mv->PlatformHandle = mainWindow;
	mv->PlatformHandleRaw = mainWindow->getImplementationPointer("SDL_Window");
	delete static_cast<ImGuiViewportData*>(mv->PlatformUserData);
	mv->PlatformUserData = new ImGuiViewportData{ nullptr, true };

	viewportsEnabled = true;
}

void HalleyImGui::setupPlatformCallbacks()
{
	// These are non-capturing lambdas (so they convert to plain function pointers), but being defined
	// inside a HalleyImGui member they may touch its private members through the recovered `self`.
	ImGuiPlatformIO& pio = ImGui::GetPlatformIO();

	pio.Platform_CreateWindow = [](ImGuiViewport* vp) {
		auto* self = static_cast<HalleyImGui*>(ImGui::GetIO().BackendPlatformUserData);
		if (!self || !self->system) {
			return;
		}
		const Vector2i pos(static_cast<int>(vp->Pos.x), static_cast<int>(vp->Pos.y));
		const Vector2i size(static_cast<int>(std::max(1.0f, vp->Size.x)), static_cast<int>(std::max(1.0f, vp->Size.y)));
		// Create hidden + borderless; ImGui sets the position then calls Platform_ShowWindow.
		WindowDefinition def(WindowType::BorderlessWindow, std::optional<Vector2i>(pos), size, String(), false);
		auto win = self->system->createWindow(def);
		vp->PlatformUserData = new ImGuiViewportData{ win, false };
		vp->PlatformHandle = win.get();
		vp->PlatformHandleRaw = win->getImplementationPointer("SDL_Window");
	};

	pio.Platform_DestroyWindow = [](ImGuiViewport* vp) {
		auto* self = static_cast<HalleyImGui*>(ImGui::GetIO().BackendPlatformUserData);
		auto* data = static_cast<ImGuiViewportData*>(vp->PlatformUserData);
		if (data) {
			if (self && self->system && data->window && !data->ownedByApp) {
				self->system->destroyWindow(data->window);
			}
			delete data;
		}
		vp->PlatformUserData = nullptr;
		vp->PlatformHandle = nullptr;
		vp->PlatformHandleRaw = nullptr;
	};

	pio.Platform_ShowWindow = [](ImGuiViewport* vp) {
		auto* data = static_cast<ImGuiViewportData*>(vp->PlatformUserData);
		if (data && data->window) {
			data->window->show();
		}
	};

	// Set position/size straight on the SDL window rather than via Window::update(), which re-issues
	// SDL_SetWindowFullscreen/Bordered/RestoreWindow on every callback (a per-drag-frame flicker on
	// Windows). Platform_GetWindowPos/Size read getWindowRect() which queries SDL live, so they stay
	// consistent with these direct sets even though the Window's cached definition isn't updated.
	pio.Platform_SetWindowPos = [](ImGuiViewport* vp, ImVec2 p) {
		if (void* raw = vp->PlatformHandleRaw) {
			SDL_SetWindowPosition(raw, static_cast<int>(p.x), static_cast<int>(p.y));
		}
	};
	pio.Platform_GetWindowPos = [](ImGuiViewport* vp) -> ImVec2 {
		auto* data = static_cast<ImGuiViewportData*>(vp->PlatformUserData);
		if (data && data->window) {
			const Rect4i r = data->window->getWindowRect();
			return ImVec2(static_cast<float>(r.getLeft()), static_cast<float>(r.getTop()));
		}
		return vp->Pos;
	};

	pio.Platform_SetWindowSize = [](ImGuiViewport* vp, ImVec2 s) {
		if (void* raw = vp->PlatformHandleRaw) {
			SDL_SetWindowSize(raw, static_cast<int>(std::max(1.0f, s.x)), static_cast<int>(std::max(1.0f, s.y)));
		}
	};
	pio.Platform_GetWindowSize = [](ImGuiViewport* vp) -> ImVec2 {
		auto* data = static_cast<ImGuiViewportData*>(vp->PlatformUserData);
		if (data && data->window) {
			const Rect4i r = data->window->getWindowRect();
			return ImVec2(static_cast<float>(r.getWidth()), static_cast<float>(r.getHeight()));
		}
		return vp->Size;
	};

	pio.Platform_SetWindowFocus = [](ImGuiViewport* vp) {
		if (void* raw = vp->PlatformHandleRaw) {
			SDL_RaiseWindow(raw);
		}
	};
	pio.Platform_GetWindowFocus = [](ImGuiViewport* vp) -> bool {
		if (void* raw = vp->PlatformHandleRaw) {
			return (SDL_GetWindowFlags(raw) & kSDL_WINDOW_INPUT_FOCUS) != 0;
		}
		return false;
	};
	pio.Platform_GetWindowMinimized = [](ImGuiViewport* vp) -> bool {
		if (void* raw = vp->PlatformHandleRaw) {
			return (SDL_GetWindowFlags(raw) & kSDL_WINDOW_MINIMIZED) != 0;
		}
		return false;
	};
	pio.Platform_SetWindowTitle = [](ImGuiViewport* vp, const char* title) {
		if (void* raw = vp->PlatformHandleRaw) {
			SDL_SetWindowTitle(raw, title ? title : "");
		}
	};
}

void HalleyImGui::updateMonitors()
{
	ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
	pio.Monitors.resize(0);
	const int n = SDL_GetNumVideoDisplays();
	const int count = n > 0 ? n : 1;
	for (int i = 0; i < count; ++i) {
		SDLRectABI bounds{ 0, 0, 1920, 1080 };
		SDLRectABI work = bounds;
		if (n > 0) {
			SDL_GetDisplayBounds(i, &bounds);
			work = bounds;
			SDL_GetDisplayUsableBounds(i, &work);
		}
		ImGuiPlatformMonitor mon;
		mon.MainPos = ImVec2(static_cast<float>(bounds.x), static_cast<float>(bounds.y));
		mon.MainSize = ImVec2(static_cast<float>(bounds.w), static_cast<float>(bounds.h));
		mon.WorkPos = ImVec2(static_cast<float>(work.x), static_cast<float>(work.y));
		mon.WorkSize = ImVec2(static_cast<float>(work.w), static_cast<float>(work.h));
		mon.DpiScale = 1.0f;
		pio.Monitors.push_back(mon);
	}
}

void HalleyImGui::updateViewports()
{
	if (!context || !viewportsEnabled) {
		return;
	}
	ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
	updateMonitors();
	ImGui::UpdatePlatformWindows();
}

void HalleyImGui::renderViewports(RenderContext& rc)
{
	if (!context || !viewportsEnabled || !video || !glContext) {
		return;
	}
	ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));

	ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
	void* mainRaw = mainWindow ? mainWindow->getImplementationPointer("SDL_Window") : nullptr;
	const Colour4f clearCol = darkTheme ? Colour4f(0.075f, 0.10f, 0.145f, 1.0f) : Colour4f(0.93f, 0.92f, 0.89f, 1.0f);

	// Detached windows share the one GL context, so its swap interval applies to every secondary
	// swap too: at vsync=1 each window->swap() blocks a whole vblank and the frame rate divides by the
	// number of torn-off windows. Present the secondaries with no wait; the deferred MAIN swap (after
	// onRender) still paces the frame at the original interval.
	const int savedSwap = SDL_GL_GetSwapInterval();
	if (savedSwap != 0) {
		SDL_GL_SetSwapInterval(0);
	}

	for (int i = 1; i < pio.Viewports.Size; ++i) {       // [0] is the app-owned main viewport
		ImGuiViewport* vp = pio.Viewports[i];
		if (!vp || (vp->Flags & ImGuiViewportFlags_IsMinimized)) {
			continue;
		}
		auto* data = static_cast<ImGuiViewportData*>(vp->PlatformUserData);
		if (!data || !data->window) {
			continue;
		}
		void* raw = data->window->getImplementationPointer("SDL_Window");
		if (!raw || SDL_GL_MakeCurrent(raw, glContext) != 0) {
			continue; // couldn't bind this window's drawable — skip it rather than draw to the wrong one
		}
		const Vector2i ds = data->window->getDrawableSize();
		if (ds.x <= 0 || ds.y <= 0) {
			continue;
		}

		auto target = video->createScreenRenderTarget(ds);
		if (!target) {
			continue;
		}
		Camera cam;
		cam.setPosition(Vector2f(ds.x * 0.5f, ds.y * 0.5f));
		cam.setZoom(1.0f);
		cam.setViewPort(Rect4i(0, 0, ds.x, ds.y));

		ImDrawData* dd = vp->DrawData;
		rc.with(cam).with(*target).bind([&](Painter& p) {
			p.clear(clearCol);
			drawDrawData(p, dd);
		});
		data->window->swap();
	}

	// Restore the main window as the current GL drawable + its swap interval: the engine swaps it in a
	// deferred step after onRender returns, so it must be current (and vsync-paced) again or the wrong
	// or un-paced surface gets presented.
	if (mainRaw) {
		SDL_GL_MakeCurrent(mainRaw, glContext);
	}
	if (savedSwap != 0) {
		SDL_GL_SetSwapInterval(savedSwap);
	}
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
