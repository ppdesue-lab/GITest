#include "stdsfx.h"
#include "kengine.h"

#include <imgui.h>
class ExampleLayer : public Layer
{
public:
    Scope<Axis> axis;
    ExampleLayer()
    {
        axis = CreateScope<Axis>();
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
        auto shader = Application::Get().GetShaderLibrary()->Get("DefaultColor");
		auto camera = Application::Get().GetCamera();
        shader->Bind();
		shader->SetMat4("u_View", camera->GetViewMatrix());
		shader->SetMat4("u_Projection", camera->GetProjectionMatrix());
        shader->SetMat4("u_Model", glm::mat4(1.0f));

        RenderCommand::DrawLines(axis->GetVertexArray(), axis->GetCount());
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