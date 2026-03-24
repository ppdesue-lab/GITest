#pragma once

#include "Event.h"
#include <sstream>

class KeyEvent : public Event
{
public:
    inline int GetKeyCode() const { return m_KeyCode; }

    EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

protected:
    KeyEvent(int keycode) : m_KeyCode(keycode) {}

    int m_KeyCode;
};

//key pressed event
class KeyPressedEvent : public KeyEvent
{
public:
    inline int GetKeyCode() const { return m_KeyCode; }
    EVENT_CLASS_TYPE(KeyPressed)
	bool IsRepeat() const { return m_IsRepeat; }

    std::string ToString() const override
    {
        std::stringstream ss;
        ss << "KeyPressedEvent: " << m_KeyCode << " (repeat = " << m_IsRepeat << ")";
        return ss.str();
    }
    KeyPressedEvent(int keycode,bool repeat=false) : KeyEvent(keycode), m_IsRepeat(repeat){}

private:
    bool m_IsRepeat;
};

//key released event
class KeyReleasedEvent : public KeyEvent
{
public:
    EVENT_CLASS_TYPE(KeyReleased)
    
    std::string ToString() const override
    {
        std::stringstream ss;
        ss << "KeyReleaseEvent: " << m_KeyCode ;
        return ss.str();
    }
    KeyReleasedEvent(int keycode) : KeyEvent(keycode) {}

};

//key typed event
class KeyTypedEvent : public KeyEvent
{
public:
    std::string ToString() const override
    {
        std::stringstream ss;
        ss << "KeyTypedEvent: " << m_KeyCode;
        return ss.str();
    }

    EVENT_CLASS_TYPE(KeyTyped)
    KeyTypedEvent(int keycode) : KeyEvent(keycode) {}

};
