#include "stdsfx.h"
#include "kengine.h"

#include <imgui.h>
class ExampleLayer : public Layer
{
public:
    ExampleLayer()
    {

    }

    void OnAttach() override
    {

    }


    void OnDetach() override
    {

    }

    void OnUpdate() override
    {
        RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });

        RenderCommand::Clear();
    }

    void OnImGuiRender() override
    {
        ImGui::Begin(u8"中文");
        ImGui::Text(u8"你好,imgui!");
        ImGui::End();
	}

    void OnEvent(Event& event) override
    {
        INFO("{}", event.GetName());
    }


};

class SandboxApp : public Application
{
public:
    SandboxApp()
    {
        PushLayer(CreateRef<ExampleLayer>());
    };
    ~SandboxApp() override = default;


};

extern "C" Application* CreateApplication()
{
    return new SandboxApp();
}