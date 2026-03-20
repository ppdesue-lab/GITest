#pragma once

#include "Event.h"

class MouseEvent : public Event
{
public:
    inline int GetMouseButton() const { return m_Button; }
    EVENT_CLASS_TYPE(MouseEvent)
    EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
protected:
    MouseEvent(int button) : m_Button(button) {}

    int m_Button;
};


//mouse button events
class MouseButtonEvent : public MouseEvent
{
public:
    inline int GetMouseButton() const { return m_Button; }
    EVENT_CLASS_TYPE(MouseButtonEvent)
    EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
protected:
    MouseButtonEvent(int button) : m_Button(button) {}

    int m_Button;
};

//mouse button pressed event
class MouseButtonPressedEvent : public MouseEvent
{
public:
    inline int GetMouseButton() const { return m_Button; }
    EVENT_CLASS_TYPE(MouseButtonPressedEvent)
    EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
protected:
    MouseButtonPressedEvent(int button) : m_Button(button) {}

    int m_Button;
};

//mouse button released event
class MouseButtonReleasedEvent : public MouseEvent
{
public:
    inline int GetMouseButton() const { return m_Button; }
    EVENT_CLASS_TYPE(MouseButtonReleasedEvent)
    EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
protected:
    MouseButtonReleasedEvent(int button) : m_Button(button) {}

    int m_Button;
};


//mouse scroll event
class MouseScrolledEvent : public MouseEvent
{
public:
    inline int GetMouseButton() const { return m_Button; }
    EVENT_CLASS_TYPE(MouseScrolledEvent)
    EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
protected:
    MouseScrolledEvent(int button) : m_Button(button) {}

    int m_Button;
};

//mouse moved event
class MouseMovedEvent : public MouseEvent
{
public:
    inline int GetMouseButton() const { return m_Button; }
    EVENT_CLASS_TYPE(MouseMovedEvent)
    EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
protected:
    MouseMovedEvent(int button) : m_Button(button) {}

    int m_Button;
};
