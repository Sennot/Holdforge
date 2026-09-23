#pragma once

namespace hf {
// Borrow P1's physical input only while GD processes a paired hold gate.
// References, including the original P2 touch ID, always return to their old values.
class SharedInputScope {
    bool& m_jump;
    int& m_touch;
    bool m_previousJump;
    int m_previousTouch;
public:
    SharedInputScope(bool& jump, int& touch, bool sourceJump, int sourceTouch)
      : m_jump(jump), m_touch(touch), m_previousJump(jump), m_previousTouch(touch) {
        m_jump = sourceJump; m_touch = sourceTouch;
    }
    ~SharedInputScope() { m_jump = m_previousJump; m_touch = m_previousTouch; }
    SharedInputScope(SharedInputScope const&) = delete;
    SharedInputScope& operator=(SharedInputScope const&) = delete;
};
}
