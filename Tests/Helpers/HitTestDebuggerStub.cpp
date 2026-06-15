/*
 * HitTestDebuggerStub.cpp — test-build-only stub for HitTestDebugger.
 *
 * The production Sources/Client/HitTestDebugger.cpp constructs an SWRenderer,
 * which transitively requires ConcurrentDispatch (SDL mutexes/conds). That chain
 * breaks the "no SDL symbols in test binary" requirement. This stub satisfies the
 * linker with no-op implementations so zerospades_testable can link cleanly.
 *
 * Tests never enable cg_debugHitTest, so SaveImage / GetBitmap are never called;
 * the constructor and destructor must exist so World's unique_ptr<HitTestDebugger>
 * can be constructed and destroyed.
 *
 * Complete types for Handle<T> members are required by ~Handle() in RefCountedObject.h.
 */

// Include complete types needed for Handle<T> destructor instantiation
#include <Core/Bitmap.h>
#include <Core/RefCountedObject.h>
#include <Client/IRenderer.h>

// Include after complete types so nested Port forward-decl doesn't cause issues
#include <Client/HitTestDebugger.h>
#include <Core/Debug.h>

namespace spades {
	namespace client {

		// Define the nested Port class as a complete type so Handle<Port> ~Handle() works.
		// The stub Port has no actual backing SWPort or framebuffer.
		class HitTestDebugger::Port : public RefCountedObject {
		public:
			Port() {}
		protected:
			~Port() {}
		};

		HitTestDebugger::HitTestDebugger(World*) {
			SPADES_MARK_FUNCTION();
			// No-op stub: SWRenderer not initialized in test builds.
			// port and renderer remain null Handles — never dereferenced since
			// SaveImage/GetBitmap raise immediately if called.
		}

		HitTestDebugger::~HitTestDebugger() {
			SPADES_MARK_FUNCTION();
		}

		void HitTestDebugger::SaveImage(
		  const std::unordered_map<int, PlayerHit>& /*hits*/,
		  const std::vector<Vector3>& /*bullets*/) {
			// No-op stub: cg_debugHitTest must be 0 in all tests.
			SPRaise("HitTestDebugger::SaveImage called in test build — "
			        "ensure cg_debugHitTest is 0 (SettingsGuard resets it)");
		}

		Handle<Bitmap> HitTestDebugger::GetBitmap() {
			SPRaise("HitTestDebugger::GetBitmap called in test build");
		}

	} // namespace client
} // namespace spades
