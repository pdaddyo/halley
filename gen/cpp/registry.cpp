// Halley codegen version 140
#include <halley.hpp>
using namespace Halley;


// System factory functions


class GameCodegenFunctions : public CodegenFunctions {
public:
	Vector<SystemReflector> makeSystemReflectors() override {
		Vector<SystemReflector> result;
		result.reserve(0);
		return result;
	}
	Vector<std::unique_ptr<ComponentReflector>> makeComponentReflectors() override {
		Vector<std::unique_ptr<ComponentReflector>> result;
		result.reserve(0);
		return result;
	}
	Vector<std::unique_ptr<MessageReflector>> makeMessageReflectors() override {
		Vector<std::unique_ptr<MessageReflector>> result;
		result.reserve(0);
		return result;
	}
	Vector<std::unique_ptr<SystemMessageReflector>> makeSystemMessageReflectors() override {
		Vector<std::unique_ptr<SystemMessageReflector>> result;
		result.reserve(0);
		return result;
	}
};

namespace Halley {
	std::unique_ptr<CodegenFunctions> createCodegenFunctions() {
		return std::make_unique<GameCodegenFunctions>();
	}
}
