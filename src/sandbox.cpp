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
        obj->Meshes[0]->Transfm.translation = glm::vec3(0.3,0,0);
        obj->Meshes[0]->Transfm.scale = glm::vec3(0.01, 0.01, 0.01);
        obj->Meshes[0]->Transfm.rotation = glm::quat(glm::vec3(0, glm::radians(90.0f), 0));



        Transform b = {
            glm::vec3(0.6f, 0.0f, 0.0f),    // Translation
			//quat for rotate y 90 degree
			glm::quat(glm::vec3(0, glm::radians(-90.0f), 0)),    // Rotation
			//glm::quat(1,0,0,0),    // Rotation
            glm::vec3(2,2,2) // Scale
        };
        obj->Meshes[0]->Transfm = obj->Meshes[0]->Transfm * b;
        obj->Meshes[0]->Transfm = obj->Meshes[0]->Transfm / b;
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