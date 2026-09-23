#pragma once
namespace hf {
struct HeldInput {
    bool seen = false, down = false;
    void event(bool pressed) { seen = true; down = pressed; }
    bool held(bool uiFallback) const { return seen ? down : uiFallback; }
};
}
