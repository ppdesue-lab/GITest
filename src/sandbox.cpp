#include "stdsfx.h"
#include "kengine.h"

#include <imgui.h>
class ExampleLayer : public Layer
{
public:
    Scope<Axis> axis;
	Scope<Object3D> obj;
	Scope<Cube> cube;
    ExampleLayer()
    {
        axis = CreateScope<Axis>();

        obj = CreateScope<Object3D>();
		obj->Load<VertexNormal>("D:/Untitled.obj");
        obj->Meshes[0]->Transfm.translation = glm::vec3(0.3,0,0);
        obj->Meshes[0]->Transfm.scale = glm::vec3(0.01, 0.01, 0.01);
        obj->Meshes[0]->Transfm.rotation = glm::quat(glm::vec3(0, glm::radians(90.0f), 0));


		cube = CreateScope<Cube>(1.0f);

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

        {
			auto backshader = Application::Get().GetShaderLibrary()->Get("DefaultBackgroundSH");
			backshader->Bind();
			auto viewrotate = glm::mat4(glm::mat3(camera->GetViewMatrix())); // 去除平移部分
			auto invViewProj = glm::inverse(camera->GetProjectionMatrix() * viewrotate);
			backshader->SetMat4("u_invViewProj", invViewProj);
            glm::vec3 shCoeffs[9] = {
                glm::vec3(0.79,  0.44,  0.54),   // L00
                glm::vec3(0.39,  0.35,  0.60),   // L1-1
                glm::vec3(-0.34, -0.18, -0.27),   // L10
                glm::vec3(-0.29, -0.06,  0.01),   // L11
                glm::vec3(-0.11, -0.05, -0.12),   // L2-2
                glm::vec3(-0.26, -0.22, -0.47),   // L2-1
                glm::vec3(-0.16, -0.09, -0.15),   // L20
                glm::vec3(0.56,  0.21,  0.14),   // L21
                glm::vec3(0.21, -0.05, -0.30)    // L22
                        };
			backshader->SetVec3Array("shCoeffs", &shCoeffs[0].x, 9);
            RenderCommand::DrawIndexed(cube->GetVertexArray(), cube->GetCount());
        }
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