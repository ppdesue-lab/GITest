#pragma once

#include "Event.h"

class MouseEvent : public Event
{
public:
    inline int GetMouseButton() const { return m_Button; }
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
protected:
    MouseButtonEvent(int button) : MouseEvent(button) {}

};

//mouse button pressed event
class MouseButtonPressedEvent : public MouseEvent
{
public:
    inline int GetMouseButton() const { return m_Button; }
    EVENT_CLASS_TYPE(MouseButtonPressed)
    MouseButtonPressedEvent(int button) : MouseEvent(button) {}

};

//mouse button released event
class MouseButtonReleasedEvent : public MouseEvent
{
public:
    inline int GetMouseButton() const { return m_Button; }
    EVENT_CLASS_TYPE(MouseButtonReleased)
    MouseButtonReleasedEvent(int button) : MouseEvent(button) {}

};


//mouse scroll event
class MouseScrolledEvent : public Event
{
public:
	MouseScrolledEvent(const float xOffset, const float yOffset)
		: m_XOffset(xOffset), m_YOffset(yOffset) {
	}

	float GetXOffset() const { return m_XOffset; }
	float GetYOffset() const { return m_YOffset; }

	std::string ToString() const override
	{
		std::stringstream ss;
		ss << "MouseScrolledEvent: " << GetXOffset() << ", " << GetYOffset();
		return ss.str();
	}

	EVENT_CLASS_TYPE(MouseScrolled)
		EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
private:
	float m_XOffset, m_YOffset;
};

//mouse moved event
class MouseMovedEvent : public Event
{
public:
	MouseMovedEvent(const float x, const float y)
		: m_MouseX(x), m_MouseY(y) {
	}

	float GetX() const { return m_MouseX; }
	float GetY() const { return m_MouseY; }

	std::string ToString() const override
	{
		std::stringstream ss;
		ss << "MouseMovedEvent: " << m_MouseX << ", " << m_MouseY;
		return ss.str();
	}

	EVENT_CLASS_TYPE(MouseMoved)
		EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
private:
	float m_MouseX, m_MouseY;
};
