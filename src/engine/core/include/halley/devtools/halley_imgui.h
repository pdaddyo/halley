#pragma once
#include <halley/maths/vector2.h>
#include <halley/maths/vector4.h>
#include <halley/time/halleytime.h>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Halley
{
	class Resources;
	class VideoAPI;
	class InputAPI;
	class Painter;
	class Texture;
	class Material;
	class MaterialDefinition;
	class String;

	// Vertex layout the ImGui material must declare (in this order):
	//   pos    : vec2 (POSITION)   offset 0
	//   uv     : vec2 (TEXCOORD0)  offset 8
	//   colour : vec4 (COLOUR)     offset 16
	// 32-byte stride. ImGui's packed RGBA8 vertex colour is expanded to a float4 here
	// because Halley has no normalised-ubyte vertex attribute type.
	struct HalleyImGuiVertex
	{
		Vector2f pos;
		Vector2f uv;
		Vector4f colour;
	};

	// Minimal, engine-agnostic Dear ImGui backend for Halley.
	//
	// Rendering rides the standard Painter (so it works on every video backend, GL or Metal):
	// ImGui draw data is translated into Painter::draw() triangle batches with per-command
	// Painter::setClip() scissoring. Input is fed from the InputAPI mouse + keyboard into
	// ImGui's IO. The owner drives ImGui:: UI calls between newFrame() and render().
	//
	// Typical per-frame use (one global ImGui context, owned here):
	//   imgui->newFrame(input, displaySize, deltaTime);   // in the update step
	//   ImGui::Begin(...); ... ImGui::End();               // build UI (anywhere after newFrame)
	//   imgui->render(painter);                            // inside an active Painter pass
	class HalleyImGui
	{
	public:
		// materialName must resolve to a MaterialDefinition matching HalleyImGuiVertex and
		// exposing a sampler2D parameter named "tex" (e.g. authored as "Stranded/Imgui").
		// fontData (optional): a TTF blob (e.g. embedded) loaded at fontPixelSize; if null, the
		// built-in ImGui font is used. The blob is baked into the atlas; it is NOT freed (the
		// caller keeps ownership, e.g. a static array).
		HalleyImGui(Resources& resources, VideoAPI& video, const String& materialName,
			const void* fontData = nullptr, int fontDataSize = 0, float fontPixelSize = 13.0f);
		~HalleyImGui();

		HalleyImGui(const HalleyImGui&) = delete;
		HalleyImGui& operator=(const HalleyImGui&) = delete;

		// Pump input and begin a new ImGui frame. displaySize is in render-target pixels
		// (the same coordinate space as InputAPI mouse positions).
		void newFrame(InputAPI& input, Vector2f displaySize, Time deltaTime);

		// Finish the frame and draw it. Must be called inside an active Painter pass whose
		// camera maps render-target pixel coordinates (top-left origin, y down) to the screen.
		// Safe to call even if newFrame() was not called this frame (it is a no-op then).
		void render(Painter& painter);

		// Register a texture for use as an ImGui image (ImGui::Image). Returns a stable id to
		// pass as ImTextureID. Idempotent per texture pointer; the backend keeps the texture (and
		// a matching material) alive. Use for drawing game sprites/atlases in the overlay.
		uint64_t registerTexture(std::shared_ptr<const Texture> texture);

		bool wantCaptureMouse() const;
		bool wantCaptureKeyboard() const;

		// The opaque ImGuiContext* (so the owner can SetCurrentContext if it ever needs to).
		void* getContext() const { return context; }

	private:
		void* context = nullptr; // ImGuiContext* (kept opaque to keep imgui.h out of this header)
		std::shared_ptr<const MaterialDefinition> materialDef; // to clone a material per texture
		std::shared_ptr<Texture> fontTexture;
		uint64_t fontTexId = 0;
		// One material per bound texture (ImGui mixes the font atlas with game sprites). Keyed by
		// the ImTextureID. A separate material per texture avoids Painter batching the wrong texture.
		std::unordered_map<uint64_t, std::shared_ptr<Material>> materials;
		bool frameStarted = false;

		// Per-draw-command scratch (reused every frame). Painter::draw requires the index
		// buffer to be 0-based into exactly the vertices passed (numIndices >= numVertices),
		// so each ImGui command is compacted to just the vertices it references.
		std::vector<HalleyImGuiVertex> cmdVerts;
		std::vector<unsigned short> cmdIdx; // 0-based; matches Halley::IndexType (unsigned short)
		std::vector<int> vtxRemap;          // original vertex index -> compact index
		std::vector<unsigned long long> vtxRemapGen; // generation stamp per original vertex
		unsigned long long remapGen = 0;    // monotonic; never resets, so stamps never collide

		void buildFont(VideoAPI& video, const void* fontData, int fontDataSize, float fontPixelSize);
		void applyTheme();
	};
}
