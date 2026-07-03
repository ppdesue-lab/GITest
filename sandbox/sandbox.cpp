#include "stdsfx.h"
#include "kengine.h"
#include <imgui.h>
#include <Primitive/AxisHelper.h>

class ExampleLayer : public Layer
{
public:
    AxisHelper axis;
    //Ref<FrameBuffer> fbo;

    ExampleLayer()
        : axis(glm::vec3(100, 100, 100))
    {
        //auto rustedIron = Application::Get().GetScene().CreateSphere("Rusted Iron PBR",10.f);
        //if (rustedIron && !rustedIron->Meshes.empty())
        //{
        //    rustedIron->Meshes[0]->Mat = CreateRef<MaterialPBR>(
        //        "E:/githubs/MapleEngine-main/Assets/textures/rusted_iron");
        //    rustedIron->Meshes[0]->Transfm.translation = glm::vec3(0.0f, 0.0f, 2.0f);
        //    
        //}

        //auto waterBottle = Application::Get().LoadObject3D(
        //    "E:/projects/ktcore/UnrealEngine/Engine/Source/ThirdParty/Windows/glTF-Toolkit/"
        //    "glTF-Toolkit.UWP.Test/Assets/3DModels/WaterBottle.glb");
        //if (waterBottle)
        //{
        //    for (auto& mesh : waterBottle->Meshes)
        //        mesh->Transfm.translation = glm::vec3(-20.0f, 0.0f, 0.0f);
        //}

        //auto obj = Application::Get().LoadObject3D("D:/Untitled.obj");

        // Load second object at origin
        //auto monkey = Application::Get().LoadObject3D("D:/Untitled.obj");
        //if (monkey && !monkey->Meshes.empty())
        //{
        //    auto& t = monkey->Meshes[0]->Transfm;
        //    t.translation = glm::vec3(0.0f, 0.0f, 0.0f);
        //}

        //Application& app = Application::Get();
        //fbo = FrameBuffer::Create(FrameBufferSpecification{ app.GetWindow().GetWidth(),app.GetWindow().GetHeight(),
        //    { FrameBufferTextureSpecification(FrameBufferTextureFormat::RGBA8), FrameBufferTextureSpecification(FrameBufferTextureFormat::Depth) } });
    }

    void OnAttach() override {}
    void OnDetach() override {}

    void OnUpdate() override
    {
        if (Application::Get().IsViewport2D())
            return;

        auto shader = Application::Get().GetShaderLibrary()->Get("DefaultColor");
		auto camera = Application::Get().GetCamera();
        shader->Bind();
		shader->SetMat4("u_View", camera->GetViewMatrix());
		shader->SetMat4("u_Projection", camera->GetProjectionMatrix());
        shader->SetMat4("u_Model", glm::mat4(1.0f));

        // Axis helper
        RenderCommand::DrawLines(axis.GetVertexArray(), axis.GetCount());
    }

    void OnImGuiRender() override
    {
        //ImGui::Begin(u8"中文");
        //ImGui::Text(u8"你好,imgui!");
        //ImGui::End();
	}

    void OnEvent(Event& event) override
    {
		if (!(event.GetCategoryFlags() & EventCategoryInput))
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
