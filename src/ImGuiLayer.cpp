#include "ImGuiLayer.h"
#include "Log.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <glm/gtx/euler_angles.hpp>

#include "Application.h"
#include <Primitive/Gizmo.h>
#include <Renderer/RenderCommand.h>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

ImGuiLayer::ImGuiLayer()
    : Layer("ImGuiLayer")
{
}

void ImGuiLayer::OnAttach()
{
    INFO("ImGuiLayer attached");
    // Initialize ImGui here
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    //io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
    //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
    //io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoTaskBarIcons;
    //io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoMerge;

    float fontSize = 18.0f;// *2.0f;
    io.Fonts->AddFontFromFileTTF("../data/fonts/CangErYuYangTiW03-2.ttf", fontSize,nullptr,io.Fonts->GetGlyphRangesChineseFull());
    //io.FontDefault = io.Fonts->AddFontFromFileTTF("assets/fonts/opensans/OpenSans-Regular.ttf", fontSize);
    io.Fonts->Build();
    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle& style = ImGui::GetStyle();
    //if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    //{
    //    style.WindowRounding = 0.0f;
    //    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    //}

    SetDarkThemeColors();

    Application& app = Application::Get();
    GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());

    m_Camera = app.GetCamera();

    // Setup Platform/Renderer bindings
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 410");
}

void ImGuiLayer::OnDetach()
{
    INFO("ImGuiLayer detached");
    // Cleanup ImGui here
    // ImGui::DestroyContext();
    
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiLayer::OnUpdate()
{
    // Update ImGui state

}

glm::vec3 quatToEulerSafe(glm::quat q, glm::vec3 order = glm::vec3(1, 0, 2)) {
    // order: 0=Y轴(XYZ), 1=X轴(XZY), 2=Z轴(ZXY) 等

    glm::mat4 rotMatrix = glm::mat4_cast(q);

    float pitch = asinf(glm::clamp(rotMatrix[1][2], -1.0f, 1.0f));

    if (abs(cos(pitch)) > 1e-6) {
        float roll = atan2(-rotMatrix[1][0], rotMatrix[1][1]);
        float yaw = atan2(-rotMatrix[0][2], rotMatrix[2][2]);
        return glm::vec3(pitch, yaw, roll);
    }
    else {
        // 万向锁情况：使用替代计算
        float roll = atan2(rotMatrix[2][0], rotMatrix[0][0]);
        float yaw = 0.0f;
        return glm::vec3(pitch, yaw, roll);
    }
}

void ImGuiLayer::OnImGuiRender()
{
    // Build flags from persisted UI state

    static int s_GizmoMode = 0; // 0=Translate,1=Rotate,2=Scale,3=All
    static bool s_Local = false;
    static bool s_View = false;
    static float s_GizmoSize = 1.5f;
    static float s_GizmoLineWidth = 2.5f;
    static bool s_ShowGizmoWindow = true;

    int gizmoFlags = 0;
    switch (s_GizmoMode)
    {
    case 0: gizmoFlags = GIZMO_TRANSLATE; break;
    case 1: gizmoFlags = GIZMO_ROTATE;    break;
    case 2: gizmoFlags = GIZMO_SCALE;     break;
    case 3: gizmoFlags = GIZMO_ALL;       break;
    default: gizmoFlags = GIZMO_TRANSLATE; break;
    }

    if (s_Local) gizmoFlags |= GIZMO_LOCAL;
    if (s_View)  gizmoFlags |= GIZMO_VIEW;

    // Apply gizmo settings to backend
    SetGizmoSize(s_GizmoSize);
    SetGizmoLineWidth(s_GizmoLineWidth);


    Transform* targetTransform = Application::Get().GetGizmoTargetTransform();
    if(!targetTransform)
        targetTransform = &g_DefaultTransform;

    if (DrawGizmo3D(m_Camera->GetViewMatrix(), m_Camera->GetProjectionMatrix(),
        m_LeftDownGizmo, m_MousePos, gizmoFlags, targetTransform))
    {
        if (m_Camera->isInputEnabled()) m_Camera->setInputEnabled(false);

        m_UpdateGizmo = true;
    }
    else
        m_UpdateGizmo = false;

    //add imgui window for gizmo control & info display

    //add imgui window for gizmo control & info display
    if (s_ShowGizmoWindow)
    {
        ImGui::Begin("Gizmo Controls", &s_ShowGizmoWindow, ImGuiWindowFlags_AlwaysAutoResize);

        // Mode radio buttons
        ImGui::Text("Mode:");
        ImGui::SameLine();
        ImGui::PushID("GizmoMode");
        ImGui::RadioButton("Translate", &s_GizmoMode, 0); ImGui::SameLine();
        ImGui::RadioButton("Rotate", &s_GizmoMode, 1); ImGui::SameLine();
        ImGui::RadioButton("Scale", &s_GizmoMode, 2); ImGui::SameLine();
        ImGui::RadioButton("All", &s_GizmoMode, 3);
        ImGui::PopID();

        // Orientation toggles
        ImGui::Checkbox("Local", &s_Local); ImGui::SameLine();
        ImGui::Checkbox("View", &s_View);

        // Size and line width
        ImGui::SliderFloat("Size", &s_GizmoSize, 0.1f, 5.0f);
        ImGui::SliderFloat("Line Width", &s_GizmoLineWidth, 0.5f, 10.0f);

        // Active indicator
        bool active = IsGizmoActivate();
        ImGui::Text("Active: %s", active ? "Yes" : "No");

        // Target transform info
        ImGui::Separator();
        ImGui::Text("Target Transform:");
        // Editable translation
        float translation[3] = { targetTransform->translation.x, targetTransform->translation.y, targetTransform->translation.z };
        if (ImGui::DragFloat3("Translation", translation, 0.1f))
        {
            targetTransform->translation = glm::vec3(translation[0], translation[1], translation[2]);
        }
        
        // Editable rotation (Euler degrees)
        glm::vec3 euler = glm::degrees(quatToEulerSafe(targetTransform->rotation));//not unique, need constraint


        //static glm::vec3 preEuler = euler;
        //euler.x = closestAngle(preEuler.x, euler.x);
        //euler.y = closestAngle(preEuler.y, euler.y);
        //euler.z = closestAngle(preEuler.z, euler.z);
        //preEuler = euler;

        static float rotationDeg[3] = { euler.x, euler.y, euler.z };
        if (ImGui::InputFloat3("Rotation (deg)", rotationDeg,"%.2f"))
        {
            glm::vec3 rad = glm::radians(glm::vec3(rotationDeg[0], rotationDeg[1], rotationDeg[2]));
            // Create quaternion from Euler radians
            targetTransform->rotation = glm::yawPitchRoll(rad.y,rad.x,rad.z);// glm::quat(rad);
        }
        if (m_UpdateGizmo )
        {
            rotationDeg[0] = euler[0];
            rotationDeg[1] = euler[1];
            rotationDeg[2] = euler[2];
        }

        // Editable scale
        float scale[3] = { targetTransform->scale.x, targetTransform->scale.y, targetTransform->scale.z };
        if (ImGui::DragFloat3("Scale", scale, 0.1f))
        {
            targetTransform->scale = glm::vec3(scale[0], scale[1], scale[2]);
        }

        // Simple actions
        if (ImGui::Button("Reset Transform"))
        {
            targetTransform->translation = glm::vec3(0.0f);
            targetTransform->rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            targetTransform->scale = glm::vec3(0.1f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Focus Camera"))
        {
            // If application exposes camera focus API, call it.
            // Fallback: set camera input enabled to true momentarily.
            if (m_Camera) m_Camera->setInputEnabled(true);
        }

        ImGui::End();
    }
}

void ImGuiLayer::OnEvent(Event& event)
{
    // Handle events for ImGui

    EventDispatcher dispatcher(event);
    dispatcher.Dispatch<MouseButtonPressedEvent>(BIND_EVENT_FN(OnMouseButtonDown));
    dispatcher.Dispatch<MouseButtonReleasedEvent>(BIND_EVENT_FN(OnMouseButtonUp));
    dispatcher.Dispatch<MouseMovedEvent>(BIND_EVENT_FN(OnMouseMove));

}

void ImGuiLayer::Begin()
{
    // Begin ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::End()
{
    // End ImGui frame and render
    ImGuiIO& io = ImGui::GetIO();
    Application& app = Application::Get();
    io.DisplaySize = ImVec2((float)app.GetWindow().GetWidth(), (float)app.GetWindow().GetHeight());

    // Rendering
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    //if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    //{
    //    GLFWwindow* backup_current_context = glfwGetCurrentContext();
    //    ImGui::UpdatePlatformWindows();
    //    ImGui::RenderPlatformWindowsDefault();
    //    glfwMakeContextCurrent(backup_current_context);
    //}
}


void ImGuiLayer::SetDarkThemeColors()
{
    auto& colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg] = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };

    // Headers
    colors[ImGuiCol_Header] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
    colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
    colors[ImGuiCol_HeaderActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

    // Buttons
    colors[ImGuiCol_Button] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
    colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
    colors[ImGuiCol_ButtonActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

    // Frame BG
    colors[ImGuiCol_FrameBg] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
    colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
    colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

    // Tabs
    colors[ImGuiCol_Tab] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_TabHovered] = ImVec4{ 0.38f, 0.3805f, 0.381f, 1.0f };
    colors[ImGuiCol_TabActive] = ImVec4{ 0.28f, 0.2805f, 0.281f, 1.0f };
    colors[ImGuiCol_TabUnfocused] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };

    // Title
    colors[ImGuiCol_TitleBg] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
}


bool ImGuiLayer::OnMouseButtonDown(MouseButtonPressedEvent& e)
{
    // if mouse hover on imgui window then return false;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return false;

    if (e.GetMouseButton() == 0)
    {
        m_Camera->setInputEnabled(true);
        m_LeftDownCamera = true;
        m_LeftDownGizmo = true;
    }

    return false;
};
bool ImGuiLayer::OnMouseButtonUp(MouseButtonReleasedEvent& e)
{
    if (e.GetMouseButton() == 0)
    {
        m_Camera->setInputEnabled(false);
        m_LeftDownCamera = false;
        m_LeftDownGizmo = false;
    }
    return false;
};
bool ImGuiLayer::OnMouseMove(MouseMovedEvent& e)
{
    if (m_Camera->isInputEnabled())
        m_LeftDownGizmo = false;
    m_MousePos = { e.GetX(),e.GetY() };
    m_Camera->processMouseMovement(e.GetX(), e.GetY());
    return false;
};