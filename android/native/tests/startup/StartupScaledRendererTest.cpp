#include <Gui/StartupScaledRenderer.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>

// Isolated reference-counting implementation; no Android or GPU required.
namespace spades {
    RefCountedObject::RefCountedObject() : refCount(1) {}
    RefCountedObject::~RefCountedObject() = default;
    void RefCountedObject::AddRef() { ++refCount; }
    void RefCountedObject::Release() { if (--refCount == 0) delete this; }
    namespace client {
        RendererInfo IRenderer::GetRendererInfo() { return {}; }
    }
}
using namespace spades;
using namespace spades::client;

class FakeImage : public IImage {
public:
    void Update(Bitmap&, int, int) override {}
    float GetWidth() override { return 20; }
    float GetHeight() override { return 10; }
};
class FakeRenderer : public IRenderer {
public:
    float width = 1600, height = 960;
    AABB2 lastOut{0,0,0,0}, lastIn{0,0,0,0}, clip{0,0,0,0};
    Vector2 a{0,0}, b{0,0}, c{0,0};
    void Init() override {} void Shutdown() override {}
    Handle<IImage> RegisterImage(const char*) override { return {}; }
    Handle<IModel> RegisterModel(const char*) override { return {}; }
    Handle<IImage> CreateImage(Bitmap&) override { return {}; }
    Handle<IModel> CreateModel(VoxelModel&) override { return {}; }
    void SetGameMap(stmp::optional<GameMap&>) override {}
    void SetFogDistance(float) override {} void SetFogColor(Vector3) override {}
    void StartScene(const SceneDefinition&) override {} void AddLight(const DynamicLightParam&) override {}
    void RenderModel(IModel&, const ModelRenderParam&) override {}
    void AddDebugLine(Vector3, Vector3, Vector4) override {}
    void AddSprite(IImage&, Vector3, float, float) override {}
    void AddLongSprite(IImage&, Vector3, Vector3, float) override {}
    void EndScene() override {} void MultiplyScreenColor(Vector3) override {}
    void SetColor(Vector4) override {} void SetColorAlphaPremultiplied(Vector4) override {}
    void DrawImage(stmp::optional<IImage&>, const Vector2&) override { assert(false); }
    void DrawImage(stmp::optional<IImage&>, const AABB2& out) override { lastOut = out; }
    void DrawImage(stmp::optional<IImage&>, const Vector2&, const AABB2&) override { assert(false); }
    void DrawImage(stmp::optional<IImage&>, const AABB2& out, const AABB2& in) override { lastOut = out; lastIn = in; }
    void DrawImage(stmp::optional<IImage&>, const Vector2& x, const Vector2& y,
                   const Vector2& z, const AABB2& in) override { a=x; b=y; c=z; lastIn=in; }
    void DrawFlatGameMap(const AABB2&, const AABB2&) override {}
    void DrawFlatGameMap(const Vector2&, const Vector2&, const Vector2&, const AABB2&) override {}
    void BeginClippingRect(const AABB2& r) override { clip=r; }
    void FrameDone() override {} void Flip() override {} Handle<Bitmap> ReadBitmap() override { return {}; }
    float ScreenWidth() override { return width; } float ScreenHeight() override { return height; }
    void DrawFilledRectFade(float, float, float, float, Vector4, Vector4, bool) override {}
    void DrawFilledTriangle(const Vector2& x, const Vector2& y, const Vector2& z) override { a=x; b=y; c=z; }
};
static bool near(float a, float b) { return std::fabs(a-b) < .001f; }
int main() {
    auto base = Handle<FakeRenderer>::New();
    auto scaled = Handle<gui::StartupScaledRenderer>::New(base.GetPointerOrNull());
    auto image = Handle<FakeImage>::New();
    assert(near(scaled->GetScale(), 2));
    assert(near(scaled->ScreenWidth(), 800) && near(scaled->ScreenHeight(), 480));
    scaled->DrawImage(*image, MakeVector2(10, 15));
    assert(near(base->lastOut.min.x, 20) && near(base->lastOut.min.y, 30));
    assert(near(base->lastOut.GetWidth(), 40) && near(base->lastOut.GetHeight(), 20));
    scaled->DrawImage(*image, MakeVector2(5, 6), AABB2(3, 4, 7, 8));
    assert(near(base->lastOut.min.x, 10) && near(base->lastOut.GetWidth(), 14));
    assert(near(base->lastIn.min.x, 3) && near(base->lastIn.GetWidth(), 7));
    scaled->BeginClippingRect(AABB2(10, 20, 30, 40));
    assert(near(base->clip.min.x, 20) && near(base->clip.GetHeight(), 80));
    scaled->DrawFilledTriangle(MakeVector2(1,2), MakeVector2(3,4), MakeVector2(5,6));
    assert(near(base->a.x, 2) && near(base->c.y, 12));
    base->width = 2340; base->height = 1080;
    assert(near(scaled->GetScale(), 2.25f));
    assert(near(scaled->ScreenWidth(), 1040) && near(scaled->ScreenHeight(), 480));
    base->width = 640; base->height = 360;
    assert(near(scaled->GetScale(), 1));
    std::cout << "Startup screen render scale checks passed\n";
}
