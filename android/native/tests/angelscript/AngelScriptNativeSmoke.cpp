#include <angelscript.h>
#include <scriptstdstring.h>
#include <cmath>
#include <cstdio>

// Representative native signatures used by game bindings: scalar arguments,
// floating-point value types passed and returned by value, and a C++ method.
struct Pair { float x, y; };
static Pair MakePair(float x, float y) { return {x, y}; }
static float Weighted(Pair p) { return p.x * 2.0f + p.y * 3.0f; }
struct Calculator {
    float Add(float value) const { return value + 4.0f; }
};
static Calculator calculator;

static void Message(const asSMessageInfo* info, void*) {
    std::fprintf(stderr, "%s:%d:%d: %s\n", info->section, info->row, info->col, info->message);
}

int main() {
    asIScriptEngine* engine = asCreateScriptEngine();
    if (!engine) return 1;
    int result = 1;
    asIScriptContext* context = nullptr;
    do {
        RegisterStdString(engine);
        if (engine->SetMessageCallback(asFUNCTION(Message), nullptr, asCALL_CDECL) < 0 ||
            engine->RegisterObjectType("Pair", sizeof(Pair),
                asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS | asOBJ_APP_CLASS_ALLFLOATS) < 0 ||
            engine->RegisterGlobalFunction("Pair makePair(float, float)", asFUNCTION(MakePair), asCALL_CDECL) < 0 ||
            engine->RegisterGlobalFunction("float weighted(Pair)", asFUNCTION(Weighted), asCALL_CDECL) < 0 ||
            engine->RegisterObjectType("Calculator", 0, asOBJ_REF | asOBJ_NOCOUNT) < 0 ||
            engine->RegisterObjectMethod("Calculator", "float add(float) const", asMETHOD(Calculator, Add), asCALL_THISCALL) < 0 ||
            engine->RegisterGlobalProperty("Calculator calc", &calculator) < 0) {
            std::fprintf(stderr, "Native binding registration failed\n");
            break;
        }
        asIScriptModule* module = engine->GetModule("smoke", asGM_ALWAYS_CREATE);
        const char* script = "float run() { string s = 'ok'; Pair p = makePair(1.25f, 2.5f); "
                             "uint count = 0; double parsed = parseFloat('1.25tail', count); "
                             "if (count != 4) return -1; "
                             "return weighted(p) + calc.add(3.0f) + float(s.length) + float(parsed); }";
        if (!module || module->AddScriptSection("smoke", script) < 0 || module->Build() < 0) {
            std::fprintf(stderr, "Script build failed\n");
            break;
        }
        context = engine->CreateContext();
        asIScriptFunction* function = module->GetFunctionByDecl("float run()");
        if (!context || !function || context->Prepare(function) < 0 || context->Execute() != asEXECUTION_FINISHED) {
            std::fprintf(stderr, "Script execution failed\n");
            break;
        }
        float value = context->GetReturnFloat();
        if (!std::isfinite(value) || std::fabs(value - 20.25f) > 0.0001f) {
            std::fprintf(stderr, "Wrong native-call result: %f\n", value);
            break;
        }
        std::puts("AngelScript native-call smoke test passed");
        result = 0;
    } while (false);
    if (context) context->Release();
    engine->ShutDownAndRelease();
    return result;
}
