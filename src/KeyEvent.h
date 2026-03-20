#pragma once

#include "Event.h"

class KeyEvent : public Event
{
public:
    inline int GetKeyCode() const { return m_KeyCode; }

    EVENT_CLASS_TYPE(KeyEvent)
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
    EVENT_CLASS_TYPE(KeyPressedEvent)

protected:
    KeyPressedEvent(int keycode) : m_KeyCode(keycode) {}

    int m_KeyCode;
};

//key released event
class KeyReleasedEvent : public KeyEvent
{
public:
    inline int GetKeyCode() const { return m_KeyCode; }
    EVENT_CLASS_TYPE(KeyReleasedEvent)
protected:
    KeyReleasedEvent(int keycode) : m_KeyCode(keycode) {}

    int m_KeyCode;
};

//key typed event
class KeyTypedEvent : public KeyEvent
{
public:
    inline int GetKeyCode() const { return m_KeyCode; }
    EVENT_CLASS_TYPE(KeyTypedEvent)
protected:
    KeyTypedEvent(int keycode) : m_KeyCode(keycode) {}

    int m_KeyCode;
};
