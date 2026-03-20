#pragma once

enum class EventType
{
    None = 0,
    WindowClose, WindowResize, WindowFocus, WindowLostFocus, WindowMoved,
    AppTick, AppUpdate, AppRender,
    KeyPressed, KeyReleased, KeyTyped,
    MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
};

enum EventCategory
{
    None = 0,
    EventCategoryApplication = 1 << 0,
    EventCategoryInput = 1 << 1,
    EventCategoryKeyboard = 1 << 2,
    EventCategoryMouse = 1 << 3
};



#define EVENT_CLASS_TYPE(type) static EventType GetStaticType() { return EventType::type; } \
virtual EventType GetEventType() const override { return GetStaticType(); } \
virtual const char* GetTypeName() const override { return #type; }

class Event
{
public:
    
    virtual EventType GetEventType() const = 0;
    virtual int GetCategoryFlags() const = 0;
    virtual std::string GetTypeName() const = 0;

    inline bool IsInCategory(EventCategory category)
    {
        return GetCategoryFlags() & category;
    }


};