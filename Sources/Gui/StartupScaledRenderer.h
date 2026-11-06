/* Android adapter for the original desktop startup UI. */
#pragma once

#include <algorithm>
#include <Client/IRenderer.h>
#include <Core/Bitmap.h>

namespace spades { namespace gui {

// The original UI is laid out in approximately 800x480 pixels. SDL uses the
// physical surface size on Android, so scale 2D drawing and input together to
// keep its controls readable and touchable without changing the desktop UI.
class StartupScaledRenderer final : public client::IRenderer {
    Handle<client::IRenderer> base;
    Vector2 Scale(Vector2 p) const { return p * GetScale(); }
    AABB2 Scale(const AABB2& r) const { return AABB2(Scale(r.min), Scale(r.max)); }
protected:
    ~StartupScaledRenderer() override = default;
public:
    explicit StartupScaledRenderer(client::IRenderer* renderer) : base(renderer) {}
    float GetScale() const {
        return std::max(1.0f, std::min(base->ScreenWidth() / 800.0f, base->ScreenHeight() / 480.0f));
    }
    void Init() override { base->Init(); }
    void Shutdown() override { base->Shutdown(); }
    Handle<client::IImage> RegisterImage(const char* f) override { return base->RegisterImage(f); }
    Handle<client::IModel> RegisterModel(const char* f) override { return base->RegisterModel(f); }
    void ClearCache() override { base->ClearCache(); }
    Handle<client::IImage> CreateImage(Bitmap& bmp) override { return base->CreateImage(bmp); }
    Handle<client::IModel> CreateModel(VoxelModel& model) override { return base->CreateModel(model); }
    void SetGameMap(stmp::optional<client::GameMap&> m) override { base->SetGameMap(m); }
    void SetFogDistance(float d) override { base->SetFogDistance(d); }
    void SetFogColor(Vector3 c) override { base->SetFogColor(c); }
    void StartScene(const client::SceneDefinition& s) override { base->StartScene(s); }
    void AddLight(const client::DynamicLightParam& l) override { base->AddLight(l); }
    void RenderModel(client::IModel& m, const client::ModelRenderParam& p) override { base->RenderModel(m, p); }
    void AddDebugLine(Vector3 a, Vector3 b, Vector4 c) override { base->AddDebugLine(a, b, c); }
    void AddSprite(client::IImage& i, Vector3 c, float r, float t) override { base->AddSprite(i, c, r, t); }
    void AddLongSprite(client::IImage& i, Vector3 a, Vector3 b, float r) override { base->AddLongSprite(i, a, b, r); }
    void EndScene() override { base->EndScene(); }
    void MultiplyScreenColor(Vector3 c) override { base->MultiplyScreenColor(c); }
    void SetColor(Vector4 c) override { base->SetColor(c); }
    void SetColorAlphaPremultiplied(Vector4 c) override { base->SetColorAlphaPremultiplied(c); }
    void DrawImage(stmp::optional<client::IImage&> i, const Vector2& p) override {
        if (!i) { base->DrawImage(i, Scale(p)); return; } // preserve base's error for a null image
        DrawImage(i, AABB2(p.x, p.y, i->GetWidth(), i->GetHeight()));
    }
    void DrawImage(stmp::optional<client::IImage&> i, const AABB2& r) override {
        base->DrawImage(i, Scale(r));
    }
    void DrawImage(stmp::optional<client::IImage&> i, const Vector2& p, const AABB2& src) override {
        DrawImage(i, AABB2(p.x, p.y, src.GetWidth(), src.GetHeight()), src);
    }
    void DrawImage(stmp::optional<client::IImage&> i, const AABB2& r, const AABB2& src) override {
        base->DrawImage(i, Scale(r), src);
    }
    void DrawImage(stmp::optional<client::IImage&> i, const Vector2& a, const Vector2& b,
                   const Vector2& c, const AABB2& src) override {
        base->DrawImage(i, Scale(a), Scale(b), Scale(c), src);
    }
    void DrawFlatGameMap(const AABB2& r, const AABB2& src) override { base->DrawFlatGameMap(Scale(r), src); }
    void DrawFlatGameMap(const Vector2& a, const Vector2& b, const Vector2& c, const AABB2& src) override {
        base->DrawFlatGameMap(Scale(a), Scale(b), Scale(c), src);
    }
    void BeginClippingCircle(const Vector2& p, float r) override { base->BeginClippingCircle(Scale(p), r * GetScale()); }
    void EndClippingCircle() override { base->EndClippingCircle(); }
    void BeginClippingRect(const AABB2& r) override { base->BeginClippingRect(Scale(r)); }
    void EndClippingRect() override { base->EndClippingRect(); }
    bool IsRendererSW() const override { return base->IsRendererSW(); }
    client::RendererInfo GetRendererInfo() override { return base->GetRendererInfo(); }
    void FrameDone() override { base->FrameDone(); }
    void Flip() override { base->Flip(); }
    Handle<Bitmap> ReadBitmap() override { return base->ReadBitmap(); }
    float ScreenWidth() override { return base->ScreenWidth() / GetScale(); }
    float ScreenHeight() override { return base->ScreenHeight() / GetScale(); }
    void DrawFilledRectFade(float x0, float y0, float x1, float y1, Vector4 a, Vector4 b, bool h) override {
        const float s = GetScale(); base->DrawFilledRectFade(x0*s, y0*s, x1*s, y1*s, a, b, h);
    }
    void DrawFilledTriangle(const Vector2& a, const Vector2& b, const Vector2& c) override {
        base->DrawFilledTriangle(Scale(a), Scale(b), Scale(c));
    }
};
} }
