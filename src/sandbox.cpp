#include "stdsfx.h"
#include "kengine.h"

#include <imgui.h>
class ExampleLayer : public Layer
{
public:
    Scope<Axis> axis;
	Scope<Object3D> obj;
    ExampleLayer()
    {
        axis = CreateScope<Axis>();

        obj = CreateScope<Object3D>();
		obj->Load<VertexNormal>("D:/Untitled.obj");
        obj->Meshes[0]->Transfm.scale = glm::vec3(0.01, 0.01, 0.01);

		Application::Get().BindGizmoTargetTransform(&obj->Meshes[0]->Transfm);
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
        //
        RenderCommand::DrawLines(axis->GetVertexArray(), axis->GetCount());
        //
		obj->Draw(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    }

    void OnImGuiRender() override
    {
        ImGui::Begin(u8"中文");
        ImGui::Text(u8"你好,imgui!");
        ImGui::End();
	}

    void OnEvent(Event& event) override
    {
        INFO("{}", event.ToString());
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