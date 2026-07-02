#include "ImGuiLayer.h"
#include "Log.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <ImNodesEz.h>

#include <glm/gtx/euler_angles.hpp>

#include "Application.h"
#include <Camera/FPSCamera.h>
#include <Editor/ObjectInspector.h>
#include <Primitive/Gizmo.h>
#include <Renderer/RenderCommand.h>
#include <Renderer/Texture.h>

#ifdef ERROR
#undef ERROR
#endif
#include <thirdparty/ofd/portable-file-dialogs.h>
#ifdef ERROR
#undef ERROR
#endif

#ifdef PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif
#endif

#include <cctype>
#include <cwctype>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#include <spdlog/sinks/base_sink.h>

#include <backends/imgui_impl_glfw.h>
#ifdef G_DX11
#include <backends/imgui_impl_dx11.h>
#include <Platform/DX11/DX11Context.h>
#else
#include <backends/imgui_impl_opengl3.h>
#endif

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// Console ring buffer
std::deque<ConsoleMessage> ImGuiLayer::s_ConsoleMessages;

// Custom spdlog sink for ImGui console
class ImGuiConsoleSink : public spdlog::sinks::base_sink<std::mutex>
{
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t formatted;
        base_sink<std::mutex>::formatter_->format(msg, formatted);
        std::string str = fmt::to_string(formatted);

        ImVec4 color;
        switch (msg.level)
        {
        case spdlog::level::trace:    color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); break;
        case spdlog::level::info:     color = ImVec4(0.6f, 0.8f, 1.0f, 1.0f); break;
        case spdlog::level::warn:     color = ImVec4(1.0f, 0.9f, 0.3f, 1.0f); break;
        case spdlog::level::err:      color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); break;
        case spdlog::level::critical: color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); break;
        default:                      color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); break;
        }

        ImGuiLayer::AddConsoleMessage(color, str);
    }

    void flush_() override {}
};

namespace
{
    struct FocusBounds
    {
        glm::vec3 Min = glm::vec3((std::numeric_limits<float>::max)());
        glm::vec3 Max = glm::vec3(std::numeric_limits<float>::lowest());
        bool Valid = false;
    };

    void AddFocusSphere(FocusBounds& focus, const BoundingSphere& sphere)
    {
        if (!sphere.Valid || sphere.Radius <= 0.0f)
            return;

        glm::vec3 extents(sphere.Radius);
        glm::vec3 minPoint = sphere.Center - extents;
        glm::vec3 maxPoint = sphere.Center + extents;

        if (!focus.Valid)
        {
            focus.Min = minPoint;
            focus.Max = maxPoint;
            focus.Valid = true;
            return;
        }

        focus.Min = glm::min(focus.Min, minPoint);
        focus.Max = glm::max(focus.Max, maxPoint);
    }

    BoundingSphere ComputeViewportFocusSphere(Application& app)
    {
        Scene& scene = app.GetScene();
        FocusBounds focus;

        for (int selectedIndex : app.GetSelectedObjectIndices())
        {
            Scene::Entry* entry = scene.GetEntry(selectedIndex);
            if (entry && entry->Object)
                AddFocusSphere(focus, entry->Object->GetWorldBoundingSphere());
        }

        if (!focus.Valid)
        {
            const auto& objects = scene.GetObjects();
            for (const auto& entry : objects)
            {
                if (entry.Visible && entry.Object)
                    AddFocusSphere(focus, entry.Object->GetWorldBoundingSphere());
            }
        }

        if (!focus.Valid)
            return { glm::vec3(0.0f), 100.0f, true };

        BoundingSphere sphere;
        sphere.Center = (focus.Min + focus.Max) * 0.5f;
        sphere.Radius = glm::length(focus.Max - sphere.Center);
        sphere.Radius = std::max(sphere.Radius, 1.0f);
        sphere.Valid = true;
        return sphere;
    }

    void SetAxisCameraView(Application& app, const glm::vec3& viewDirection)
    {
        Ref<FPSCamera> camera = std::dynamic_pointer_cast<FPSCamera>(app.GetCamera());
        if (!camera)
            return;

        glm::vec3 direction = glm::normalize(viewDirection);
        if (std::abs(direction.y) > 0.99f)
            direction = glm::normalize(direction + glm::vec3(0.0f, 0.0f, 0.02f));

        const glm::vec3 target = glm::vec3(0.0f);
        const float distance = 100.0f;
        camera->setInputEnabled(false);
        camera->setPosition(target + direction * distance);
        camera->lookAt(target);
    }

    ImVec2 ToImVec2(const glm::vec2& value)
    {
        return ImVec2(value.x, value.y);
    }

    ImU32 ApplyAlpha(ImU32 color, float alpha)
    {
        const int a = (color >> IM_COL32_A_SHIFT) & 0xff;
        const int r = (color >> IM_COL32_R_SHIFT) & 0xff;
        const int g = (color >> IM_COL32_G_SHIFT) & 0xff;
        const int b = (color >> IM_COL32_B_SHIFT) & 0xff;
        return IM_COL32(r, g, b, (int)glm::clamp(a * alpha, 0.0f, 255.0f));
    }

    bool DrawViewportAxisIndicator(Application& app, const ImVec2& viewportMin, const ImVec2& viewportMax)
    {
        Ref<FPSCamera> camera = std::dynamic_pointer_cast<FPSCamera>(app.GetCamera());
        if (!camera)
            return false;

        const ImVec2 savedCursor = ImGui::GetCursorScreenPos();
        const ImVec2 panelSize(132.0f, 132.0f);
        const ImVec2 panelMin(
            std::max(viewportMin.x + 8.0f, viewportMax.x - panelSize.x - 12.0f),
            viewportMin.y + 12.0f);
        const ImVec2 panelMax(panelMin.x + panelSize.x, panelMin.y + panelSize.y);
        const ImVec2 center(panelMin.x + panelSize.x * 0.5f, panelMin.y + panelSize.y * 0.5f);

        ImGui::SetCursorScreenPos(panelMin);
        ImGui::InvisibleButton("##ViewportGizmo", panelSize);
        const bool overlayHovered = ImGui::IsItemHovered();
        const bool overlayClicked = overlayHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

        struct AxisNode
        {
            const char* Label;
            glm::vec3 Direction;
            ImU32 Color;
            glm::vec2 ScreenPos = glm::vec2(0.0f);
            float Depth = 0.0f;
            float Radius = 10.0f;
            bool Hovered = false;
        };

        std::array<AxisNode, 6> axes = { {
            { "X",  glm::vec3( 1.0f,  0.0f,  0.0f), IM_COL32(224, 72, 78, 255) },
            { "-X", glm::vec3(-1.0f,  0.0f,  0.0f), IM_COL32(142, 50, 54, 255) },
            { "Y",  glm::vec3( 0.0f,  1.0f,  0.0f), IM_COL32(73, 184, 88, 255) },
            { "-Y", glm::vec3( 0.0f, -1.0f,  0.0f), IM_COL32(50, 126, 64, 255) },
            { "Z",  glm::vec3( 0.0f,  0.0f,  1.0f), IM_COL32(79, 124, 244, 255) },
            { "-Z", glm::vec3( 0.0f,  0.0f, -1.0f), IM_COL32(52, 78, 165, 255) },
        } };

        const glm::vec3 right = camera->getRight();
        const glm::vec3 up = camera->getUp();
        const glm::vec3 view = -camera->getForward();
        const glm::vec2 center2(center.x, center.y);
        const float axisLength = 42.0f;
        const float hitRadius = 14.0f;
        const glm::vec2 mouse(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
        int hoveredAxis = -1;
        float bestHoverDepth = -2.0f;

        for (int i = 0; i < (int)axes.size(); ++i)
        {
            AxisNode& axis = axes[i];
            const float x = glm::dot(axis.Direction, right);
            const float y = glm::dot(axis.Direction, up);
            axis.Depth = glm::dot(axis.Direction, view);
            axis.ScreenPos = center2 + glm::vec2(x, -y) * axisLength;
            axis.Radius = axis.Depth > 0.0f ? 10.5f : 8.0f;

            const float dist = glm::length(mouse - axis.ScreenPos);
            if (overlayHovered && dist <= hitRadius && axis.Depth > bestHoverDepth)
            {
                hoveredAxis = i;
                bestHoverDepth = axis.Depth;
            }
        }

        if (hoveredAxis >= 0)
        {
            axes[hoveredAxis].Hovered = true;
            axes[hoveredAxis].Radius += 3.0f;
            ImGui::SetTooltip("View from %s", axes[hoveredAxis].Label);
            if (overlayClicked)
                SetAxisCameraView(app, axes[hoveredAxis].Direction);
        }

        std::array<int, 6> order = { 0, 1, 2, 3, 4, 5 };
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return axes[a].Depth < axes[b].Depth;
        });

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddCircleFilled(center, 52.0f, overlayHovered ? IM_COL32(24, 27, 32, 186) : IM_COL32(18, 20, 24, 150), 48);
        drawList->AddCircle(center, 52.0f, overlayHovered ? IM_COL32(255, 255, 255, 72) : IM_COL32(255, 255, 255, 42), 48, 1.0f);

        for (int index : order)
        {
            const AxisNode& axis = axes[index];
            const float alpha = axis.Depth > 0.0f ? 0.95f : 0.36f;
            drawList->AddLine(center, ToImVec2(axis.ScreenPos), ApplyAlpha(axis.Color, alpha * 0.72f), axis.Depth > 0.0f ? 2.2f : 1.4f);
        }

        for (int index : order)
        {
            const AxisNode& axis = axes[index];
            const float alpha = axis.Hovered ? 1.0f : (axis.Depth > 0.0f ? 0.98f : 0.48f);
            const ImVec2 pos = ToImVec2(axis.ScreenPos);
            drawList->AddCircleFilled(pos, axis.Radius, ApplyAlpha(axis.Color, alpha), 24);
            drawList->AddCircle(pos, axis.Radius, axis.Hovered ? IM_COL32(255, 255, 255, 230) : ApplyAlpha(IM_COL32(255, 255, 255, 150), alpha), 24, 1.4f);

            const ImVec2 textSize = ImGui::CalcTextSize(axis.Label);
            drawList->AddText(ImVec2(pos.x - textSize.x * 0.5f, pos.y - textSize.y * 0.5f),
                ApplyAlpha(IM_COL32(255, 255, 255, 255), alpha), axis.Label);
        }

        ImGui::SetCursorScreenPos(savedCursor);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        return overlayHovered;
    }

    bool IsDriveRootPath(const std::filesystem::path& path)
    {
        return path.has_root_name() && path.has_root_directory() && path.relative_path().empty();
    }

    bool IsDriveRootAvailable(const std::filesystem::path& path)
    {
        if (!IsDriveRootPath(path))
            return false;

#ifdef PLATFORM_WINDOWS
        const UINT driveType = GetDriveTypeW(path.wstring().c_str());
        return driveType != DRIVE_NO_ROOT_DIR && driveType != DRIVE_UNKNOWN;
#else
        std::error_code ec;
        return std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec);
#endif
    }

    bool ContentPathLess(const std::filesystem::path& a, const std::filesystem::path& b)
    {
        std::wstring an = a.filename().wstring();
        std::wstring bn = b.filename().wstring();
        std::transform(an.begin(), an.end(), an.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        std::transform(bn.begin(), bn.end(), bn.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return an < bn;
    }

    bool EnumerateContentDirectory(const std::filesystem::path& root,
        std::vector<std::filesystem::path>& directories,
        std::vector<std::filesystem::path>& files)
    {
        directories.clear();
        files.clear();

        std::error_code iterEc;
        for (const auto& entry : std::filesystem::directory_iterator(root,
            std::filesystem::directory_options::skip_permission_denied, iterEc))
        {
            std::error_code typeEc;
            if (entry.is_directory(typeEc))
                directories.push_back(entry.path());
            else if (!typeEc && entry.is_regular_file(typeEc))
                files.push_back(entry.path());
        }
        if (iterEc)
        {
            WARN("Unable to fully browse directory {}: {}", root.u8string(), iterEc.message());
            return false;
        }

        std::sort(directories.begin(), directories.end(), ContentPathLess);
        std::sort(files.begin(), files.end(), ContentPathLess);
        return true;
    }
}

ImGuiLayer::ImGuiLayer()
    : Layer("ImGuiLayer")
{
}

void ImGuiLayer::OnAttach()
{
    INFO("ImGuiLayer attached");
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    float fontSize = 18.0f;
    io.Fonts->AddFontFromFileTTF(GetFilePath("../data/fonts/CangErYuYangTiW03-2.ttf").c_str(), fontSize, nullptr, io.Fonts->GetGlyphRangesChineseFull());

    ImGui::StyleColorsDark();
    SetDarkThemeColors();

    Application& app = Application::Get();
    GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());

    m_Camera = app.GetCamera();

#ifdef G_DX11
    ImGui_ImplGlfw_InitForOther(window, true);
    ImGui_ImplDX11_Init(DX11Context::GetDevice(), DX11Context::GetDeviceContext());
#else
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 410");
#endif

    // Set up console sink
    auto consoleSink = std::make_shared<ImGuiConsoleSink>();
    consoleSink->set_pattern("%^[%T] %n: %v%$");
    Log::GetCoreLogger()->sinks().push_back(consoleSink);

    // Initialize content browser path
    m_CurrentDir = std::filesystem::current_path().u8string();
    Application::Get().GetTimelineAnimation().Initialize();
    Application::Get().GetCameraAnimation().Initialize();
}

void ImGuiLayer::OnDetach()
{
    INFO("ImGuiLayer detached");

#ifdef G_DX11
    ImGui_ImplDX11_Shutdown();
#else
    ImGui_ImplOpenGL3_Shutdown();
#endif
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiLayer::OnUpdate()
{
}

void ImGuiLayer::AddConsoleMessage(const ImVec4& color, const std::string& message)
{
    s_ConsoleMessages.push_back({ color, message });
    if (s_ConsoleMessages.size() > MAX_CONSOLE_MESSAGES)
        s_ConsoleMessages.pop_front();
}

glm::vec3 quatToEulerSafe(glm::quat q, glm::vec3 order = glm::vec3(1, 0, 2)) {
    glm::mat4 rotMatrix = glm::mat4_cast(q);
    float pitch = asinf(glm::clamp(rotMatrix[1][2], -1.0f, 1.0f));
    if (abs(cos(pitch)) > 1e-6) {
        float roll = atan2(-rotMatrix[1][0], rotMatrix[1][1]);
        float yaw = atan2(-rotMatrix[0][2], rotMatrix[2][2]);
        return glm::vec3(pitch, yaw, roll);
    }
    else {
        float roll = atan2(rotMatrix[2][0], rotMatrix[0][0]);
        float yaw = 0.0f;
        return glm::vec3(pitch, yaw, roll);
    }
}

void ImGuiLayer::OnImGuiRender()
{
    DrawMenuBar();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 pos = viewport->Pos;
    ImVec2 size = viewport->Size;

    float menuBarHeight = ImGui::GetFrameHeight();
    if (menuBarHeight < 1.0f) menuBarHeight = 20.0f;

    DrawEditorLayout(pos, size, menuBarHeight);
    Application& app = Application::Get();
    app.GetTimelineAnimation().OnImGuiRender(app.GetScene(), app.GetDeltaTime());
    app.GetCameraAnimation().OnImGuiRender(app.GetCamera(), app.GetDeltaTime());

}

void ImGuiLayer::DrawEditorLayout(ImVec2 pos, ImVec2 size, float menuBarHeight)
{
    ImGuiWindowFlags dockspaceFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y + menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(size.x, size.y - menuBarHeight));
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("EditorDockSpace", nullptr, dockspaceFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceID = ImGui::GetID("EditorDockSpaceID");
    ImGuiDockNodeFlags nodeFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    const bool needsDefaultDockingLayout = ImGui::DockBuilderGetNode(dockspaceID) == nullptr;
    ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), nodeFlags);

    static bool dockspaceBuilt = false;
    if (!dockspaceBuilt && needsDefaultDockingLayout)
    {
        dockspaceBuilt = true;
        ImGui::DockBuilderRemoveNode(dockspaceID);
        ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceID, ImGui::GetWindowSize());

        ImGuiID mainDock = dockspaceID;
        ImGuiID leftDock = ImGui::DockBuilderSplitNode(mainDock, ImGuiDir_Left, 0.18f, nullptr, &mainDock);
        ImGuiID rightDock = ImGui::DockBuilderSplitNode(mainDock, ImGuiDir_Right, 0.22f, nullptr, &mainDock);
        ImGuiID bottomDock = ImGui::DockBuilderSplitNode(mainDock, ImGuiDir_Down, 0.25f, nullptr, &mainDock);
        ImGuiID consoleDock = ImGui::DockBuilderSplitNode(bottomDock, ImGuiDir_Right, 0.50f, nullptr, &bottomDock);

        ImGui::DockBuilderDockWindow("Project", leftDock);
        ImGui::DockBuilderDockWindow("Properties", rightDock);
        ImGui::DockBuilderDockWindow("Viewport", mainDock);
        ImGui::DockBuilderDockWindow("Content Browser", bottomDock);
        ImGui::DockBuilderDockWindow("Console", consoleDock);
        ImGui::DockBuilderFinish(dockspaceID);
    }

    ImGui::End();

    ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("Project", nullptr, panelFlags);
    DrawProjectPanel();
    ImGui::End();

    ImGui::Begin("Properties", nullptr, panelFlags);
    DrawPropertiesPanel();
    ImGui::End();

    ImGui::Begin("Viewport", nullptr, panelFlags | ImGuiWindowFlags_NoScrollbar);
    DrawViewportPanel();
    ImGui::End();

    ImGui::Begin("Content Browser", nullptr, panelFlags);
    DrawContentBrowser();
    ImGui::End();

    ImGui::Begin("Console", nullptr, panelFlags);
    DrawConsolePanel();
    ImGui::End();

    if (m_ShowNodeEditor)
    {
        ImGui::Begin("NodeEditor", &m_ShowNodeEditor,
            panelFlags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        DrawNodeEditorWindow();
        ImGui::End();
    }
}

void ImGuiLayer::DrawNodeEditorWindow()
{
    enum NodeSlotType
    {
        NodeSlotValue = 1,
        NodeSlotColor,
        NodeSlotSurface,
    };

    struct NodeEditorNode;
    struct NodeEditorConnection
    {
        NodeEditorNode* InputNode = nullptr;
        const char* InputSlot = nullptr;
        NodeEditorNode* OutputNode = nullptr;
        const char* OutputSlot = nullptr;
    };

    struct NodeEditorNode
    {
        const char* Title = nullptr;
        ImVec2 Pos = {};
        bool Selected = false;
        std::vector<ImNodes::Ez::SlotInfo> Inputs;
        std::vector<ImNodes::Ez::SlotInfo> Outputs;
    };

    auto makeNode = [](const char* title, ImVec2 pos,
        std::vector<ImNodes::Ez::SlotInfo> inputs,
        std::vector<ImNodes::Ez::SlotInfo> outputs) -> std::unique_ptr<NodeEditorNode> {
        auto node = std::make_unique<NodeEditorNode>();
        node->Title = title;
        node->Pos = pos;
        node->Inputs = std::move(inputs);
        node->Outputs = std::move(outputs);
        return node;
    };

    static ImNodes::Ez::Context* context = ImNodes::Ez::CreateContext();
    IM_UNUSED(context);

    static std::vector<std::unique_ptr<NodeEditorNode>> nodes;
    static std::vector<NodeEditorConnection> connections;
    static bool initialized = false;
    if (!initialized)
    {
        initialized = true;
        nodes.push_back(makeNode("Texture", ImVec2(60.0f, 80.0f), {},
            { { "Color", NodeSlotColor } }));
        nodes.push_back(makeNode("Multiply", ImVec2(280.0f, 70.0f),
            { { "A", NodeSlotColor }, { "B", NodeSlotColor } },
            { { "Result", NodeSlotColor } }));
        nodes.push_back(makeNode("Material", ImVec2(520.0f, 100.0f),
            { { "BaseColor", NodeSlotColor }, { "Roughness", NodeSlotValue } },
            { { "Surface", NodeSlotSurface } }));
        connections.push_back({ nodes[1].get(), "A", nodes[0].get(), "Color" });
        connections.push_back({ nodes[2].get(), "BaseColor", nodes[1].get(), "Result" });
    }

    auto createNode = [&](const char* title) {
        if (strcmp(title, "Float") == 0)
            nodes.push_back(makeNode("Float", ImVec2(0.0f, 0.0f), {}, { { "Value", NodeSlotValue } }));
        else if (strcmp(title, "Texture") == 0)
            nodes.push_back(makeNode("Texture", ImVec2(0.0f, 0.0f), {}, { { "Color", NodeSlotColor } }));
        else if (strcmp(title, "Multiply") == 0)
            nodes.push_back(makeNode("Multiply", ImVec2(0.0f, 0.0f),
                { { "A", NodeSlotColor }, { "B", NodeSlotColor } }, { { "Result", NodeSlotColor } }));
        else if (strcmp(title, "Material") == 0)
            nodes.push_back(makeNode("Material", ImVec2(0.0f, 0.0f),
                { { "BaseColor", NodeSlotColor }, { "Roughness", NodeSlotValue } }, { { "Surface", NodeSlotSurface } }));
        ImNodes::AutoPositionNode(nodes.back().get());
    };

    auto removeConnection = [&](const NodeEditorConnection& target) {
        connections.erase(std::remove_if(connections.begin(), connections.end(),
            [&](const NodeEditorConnection& connection) {
                return connection.InputNode == target.InputNode &&
                    connection.InputSlot == target.InputSlot &&
                    connection.OutputNode == target.OutputNode &&
                    connection.OutputSlot == target.OutputSlot;
            }), connections.end());
    };

    auto removeNode = [&](NodeEditorNode* target) {
        connections.erase(std::remove_if(connections.begin(), connections.end(),
            [&](const NodeEditorConnection& connection) {
                return connection.InputNode == target || connection.OutputNode == target;
            }), connections.end());
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
            [&](const std::unique_ptr<NodeEditorNode>& node) { return node.get() == target; }), nodes.end());
    };

    ImNodes::Ez::BeginCanvas();

    NodeEditorNode* nodeToDelete = nullptr;
    for (const auto& nodePtr : nodes)
    {
        NodeEditorNode* node = nodePtr.get();
        if (ImNodes::Ez::BeginNode(node, node->Title, &node->Pos, &node->Selected))
        {
            ImNodes::Ez::InputSlots(node->Inputs.empty() ? nullptr : node->Inputs.data(), (int)node->Inputs.size());

            if (strcmp(node->Title, "Float") == 0)
            {
                static float value = 0.5f;
                ImGui::SetNextItemWidth(90.0f);
                ImGui::SliderFloat("##Value", &value, 0.0f, 1.0f, "%.2f");
            }
            else if (strcmp(node->Title, "Texture") == 0)
            {
                ImGui::TextUnformatted("Albedo");
            }
            else if (strcmp(node->Title, "Multiply") == 0)
            {
                ImGui::TextUnformatted("Blend");
            }
            else if (strcmp(node->Title, "Material") == 0)
            {
                ImGui::TextUnformatted("Preview");
            }

            ImNodes::Ez::OutputSlots(node->Outputs.empty() ? nullptr : node->Outputs.data(), (int)node->Outputs.size());

            NodeEditorConnection newConnection;
            void* inputNode = nullptr;
            void* outputNode = nullptr;
            if (ImNodes::GetNewConnection(&inputNode, &newConnection.InputSlot, &outputNode, &newConnection.OutputSlot))
            {
                newConnection.InputNode = static_cast<NodeEditorNode*>(inputNode);
                newConnection.OutputNode = static_cast<NodeEditorNode*>(outputNode);
                const bool duplicate = std::any_of(connections.begin(), connections.end(),
                    [&](const NodeEditorConnection& connection) {
                        return connection.InputNode == newConnection.InputNode &&
                            connection.InputSlot == newConnection.InputSlot &&
                            connection.OutputNode == newConnection.OutputNode &&
                            connection.OutputSlot == newConnection.OutputSlot;
                    });
                if (!duplicate)
                    connections.push_back(newConnection);
            }
        }
        ImNodes::Ez::EndNode();

        if (node->Selected && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete))
            nodeToDelete = node;
    }

    for (const NodeEditorConnection& connection : std::vector<NodeEditorConnection>(connections))
    {
        if (!ImNodes::Connection(connection.InputNode, connection.InputSlot, connection.OutputNode, connection.OutputSlot))
            removeConnection(connection);
    }

    if (nodeToDelete)
        removeNode(nodeToDelete);

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ImGui::IsWindowHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Right))
        ImGui::OpenPopup("NodeEditorContextMenu");

    if (ImGui::BeginPopup("NodeEditorContextMenu"))
    {
        if (ImGui::MenuItem("Float"))
            createNode("Float");
        if (ImGui::MenuItem("Texture"))
            createNode("Texture");
        if (ImGui::MenuItem("Multiply"))
            createNode("Multiply");
        if (ImGui::MenuItem("Material"))
            createNode("Material");
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Zoom"))
            ImNodes::GetCurrentCanvas()->Zoom = 1.0f;
        ImGui::EndPopup();
    }

    ImNodes::Ez::EndCanvas();
}

void ImGuiLayer::DrawShadowDebugWindow()
{
    Application& app = Application::Get();
    CSM& csm = app.GetCSM();
    static int debugCascade = 0;

    ImGui::PushID("CSMDebug");
    ImGui::Checkbox("Enable", &csm.Enabled());
    if (!csm.Enabled())
    {
        ImGui::PopID();
        return;
    }

    ImGui::SliderInt("Cascade", &debugCascade, 0, (int)csm.GetCascadeCount() - 1);
    float maxShadowDistance = csm.GetMaxShadowDistance();
    if (ImGui::SliderFloat("Max Shadow Distance", &maxShadowDistance, 50.0f, 2000.0f, "%.0f"))
        csm.SetMaxShadowDistance(maxShadowDistance);
    float splitLambda = csm.GetSplitLambda();
    if (ImGui::SliderFloat("Split Lambda", &splitLambda, 0.0f, 1.0f, "%.2f"))
        csm.SetSplitLambda(splitLambda);
    ImGui::SliderFloat("Constant Bias", &csm.ConstantBias(), 0.0f, 0.02f, "%.5f");
    ImGui::SliderFloat("Slope Bias", &csm.SlopeBias(), 0.0f, 0.05f, "%.5f");
    ImGui::SliderFloat("Offset Factor", &csm.PolygonOffsetFactor(), 0.0f, 8.0f, "%.2f");
    ImGui::SliderFloat("Offset Units", &csm.PolygonOffsetUnits(), 0.0f, 16.0f, "%.2f");
    DirectionalLight& light = csm.GetLight();
    glm::vec3 lightDirection = light.Direction;
    if (ImGui::DragFloat3("Light Direction", &lightDirection.x, 0.01f))
        if (glm::length(lightDirection) > 0.0001f)
            light.Direction = glm::normalize(lightDirection);
    ImGui::ColorEdit3("Light Color", &light.Color.x);
    ImGui::SliderFloat("Light Intensity", &light.Intensity, 0.0f, 10.0f, "%.2f");
    const auto& cascadeDistances = csm.GetCascadeDistances();
    for (uint32_t i = 0; i < csm.GetCascadeCount(); i++)
        ImGui::Text("Cascade %u: %.2f - %.2f", i, cascadeDistances[i], cascadeDistances[i + 1]);
    bool debugCascadeView = app.GetDebugCascadeView();
    if (ImGui::Checkbox("Show Cascade View", &debugCascadeView))
        app.SetDebugCascadeView(debugCascadeView);
    if (ImGui::Button("Refresh"))
    {
        csm.UpdateDebugTexture(debugCascade);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save"))
    {
        csm.SaveShadowMap("D:/shadow_cascade_" + std::to_string(debugCascade) + ".png", debugCascade);
    }

    uint32_t texID = csm.GetDebugTextureID(debugCascade);
    if (texID)
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float aspect = 1.0f;
        float w = avail.x;
        float h = w / aspect;
        if (h > avail.y) h = avail.y;
        ImGui::Image((ImTextureID)(uint64_t)texID, ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
    }

    ImGui::PopID();
}

void ImGuiLayer::DrawProbeGIDebugWindow()
{
    Application& app = Application::Get();
    ProbeGI& gi = app.GetProbeGI();

    ImGui::PushID("ProbeGIDebug");
    ImGui::Checkbox("Enable", &gi.Enabled());
    if (!gi.Enabled())
    {
        ImGui::PopID();
        return;
    }

    ImGui::Checkbox("Show Probes", &gi.ShowProbes());
    ImGui::SliderFloat("GI Intensity", &gi.Intensity(), 0.0f, 4.0f, "%.2f");
    const char* modes[] = { "Combined", "Indirect Only" };
    ImGui::Combo("View Mode", &gi.DebugMode(), modes, IM_ARRAYSIZE(modes));

    glm::vec3 origin = gi.Origin();
    if (ImGui::DragFloat3("Origin", &origin.x, 0.5f))
        gi.Origin() = origin;
    float spacing = gi.Spacing();
    if (ImGui::SliderFloat("Spacing", &spacing, 1.0f, 100.0f, "%.1f"))
        gi.Spacing() = spacing;
    glm::ivec3 counts = gi.GetCounts();
    int countValues[3] = { counts.x, counts.y, counts.z };
    if (ImGui::SliderInt3("Counts", countValues, 1, 4))
        gi.SetCounts(glm::ivec3(countValues[0], countValues[1], countValues[2]));
    ImGui::Text("Active probes: %d / %d", gi.GetProbeCount(), ProbeGI::MaxProbeCount);
    ImGui::TextDisabled("CPU irradiance seed; capture pass pending");

    ImGui::PopID();
}

void ImGuiLayer::DrawSSAODebugWindow()
{
    SSAO& ssao = Application::Get().GetSSAO();

    ImGui::PushID("SSAODebug");
    ImGui::Checkbox("Enable", &ssao.Enabled());
    if (!ssao.Enabled())
    {
        ImGui::PopID();
        return;
    }

    int algorithm = (int)ssao.CurrentAlgorithm();
    const char* algorithms[] = { "Current SSAO", "SSAO11 HBAO" };
    if (ImGui::Combo("Algorithm", &algorithm, algorithms, IM_ARRAYSIZE(algorithms)))
        ssao.CurrentAlgorithm() = (SSAO::Algorithm)algorithm;

    ImGui::SliderFloat("Radius (view units)", &ssao.Radius(), 0.05f, 50.0f, "%.2f");
    ImGui::SliderFloat("Strength", &ssao.Strength(), 0.0f, 3.0f, "%.2f");
    if (ssao.CurrentAlgorithm() == SSAO::Algorithm::KernelSSAO)
    {
        ImGui::SliderFloat("Bias", &ssao.Bias(), 0.0f, 0.3f, "%.3f");
    }
    else
    {
        ImGui::SliderInt("Step Size", &ssao.StepSize(), 1, 16);
        ImGui::SliderFloat("Angle Bias", &ssao.AngleBiasDegrees(), 0.0f, 45.0f, "%.0f");
        ImGui::SliderFloat("Power Exponent", &ssao.PowerExponent(), 0.1f, 4.0f, "%.2f");
        ImGui::SliderInt("Blur Radius", &ssao.BlurRadius(), 0, 16);
        ImGui::SliderFloat("Blur Sharpness", &ssao.BlurSharpness(), 0.0f, 32.0f, "%.1f");
        ImGui::SliderFloat("Max Radius Pixels", &ssao.MaxRadiusPixels(), 16.0f, 512.0f, "%.0f");
    }
    const char* modes[] = { "Combined", "AO Only" };
    ImGui::Combo("View Mode", &ssao.DebugMode(), modes, IM_ARRAYSIZE(modes));
    ImGui::TextDisabled(ssao.CurrentAlgorithm() == SSAO::Algorithm::SSAO11HBAO
        ? "SSAO11 HBAO + cross-bilateral blur"
        : "OpenGL editor viewport pass");

    uint64_t aoTexture = ssao.GetAOTexture();
    if (aoTexture)
    {
        ImGui::SeparatorText("AO Preview");
        ImVec2 avail = ImGui::GetContentRegionAvail();
        const glm::vec2& viewportSize = Application::Get().GetViewportSize();
        float aspect = viewportSize.y > 0.0f ? viewportSize.x / viewportSize.y : 1.0f;
        float width = avail.x;
        float height = width / aspect;
        if (height > avail.y && avail.y > 0.0f)
        {
            height = avail.y;
            width = height * aspect;
        }
        ImGui::Image((ImTextureID)aoTexture, ImVec2(width, height), ImVec2(0, 1), ImVec2(1, 0));
    }

    ImGui::PopID();
}

void ImGuiLayer::DrawFXAADebugWindow()
{
    Application& app = Application::Get();
    FXAA& fxaa = app.GetFXAA();

    ImGui::PushID("FXAADebug");
    const char* msaaModes[] = { "Off", "2x", "4x", "8x" };
    int msaaIndex = 0;
    switch (app.GetMSAASamples())
    {
    case 2: msaaIndex = 1; break;
    case 4: msaaIndex = 2; break;
    case 8: msaaIndex = 3; break;
    default: msaaIndex = 0; break;
    }
    if (ImGui::Combo("MSAA", &msaaIndex, msaaModes, IM_ARRAYSIZE(msaaModes)))
    {
        const int samples[] = { 1, 2, 4, 8 };
        app.SetMSAASamples(samples[msaaIndex]);
    }

    bool fxaaEnabled = fxaa.Enabled();
    if (ImGui::Checkbox("Enable FXAA", &fxaaEnabled))
    {
        fxaa.Enabled() = fxaaEnabled;
        if (fxaa.Enabled())
            app.SetMSAASamples(1);
    }
    if (app.IsMSAAEnabled())
        ImGui::TextDisabled("FXAA is disabled while MSAA is enabled.");
    if (fxaa.Enabled())
    {
        const char* modes[] = { "Result", "Edges", "Difference" };
        ImGui::Combo("View Mode", &fxaa.DebugMode(), modes, IM_ARRAYSIZE(modes));
        ImGui::SliderFloat("Edge Threshold", &fxaa.EdgeThreshold(), 0.0312f, 0.333f, "%.4f");
        ImGui::SliderFloat("Minimum Threshold", &fxaa.EdgeThresholdMin(), 0.0f, 0.0833f, "%.4f");
        ImGui::SliderFloat("Subpixel Quality", &fxaa.SubpixelQuality(), 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Span Max", &fxaa.SpanMax(), 2.0f, 16.0f, "%.1f");
        ImGui::TextDisabled("Edges/Difference confirms which silhouette pixels FXAA processes.");
    }
    ImGui::PopID();
}

void ImGuiLayer::DrawSVGFDenoiserWindow()
{
    Application& app = Application::Get();
    SVGF& svgf = app.GetSVGF();

    ImGui::PushID("SVGFDenoiser");
    bool enabled = svgf.Enabled();
    if (ImGui::Checkbox("Enable SVGF", &enabled))
    {
        svgf.Enabled() = enabled;
        svgf.ResetHistory();
        app.GetPathTracer().ResetAccumulation();
    }
    if (ImGui::Button("Reset History"))
    {
        svgf.ResetHistory();
        app.GetPathTracer().ResetAccumulation();
    }
    if (svgf.Enabled())
    {
        ImGui::SliderInt("Iterations", &svgf.Iterations(), 0, 6);
        ImGui::SliderFloat("History Alpha", &svgf.HistoryAlpha(), 0.0f, 0.98f, "%.2f");
        ImGui::SliderFloat("Phi Color", &svgf.PhiColor(), 0.5f, 16.0f, "%.2f");
        ImGui::SliderFloat("Phi Normal", &svgf.PhiNormal(), 1.0f, 256.0f, "%.0f");
        ImGui::SliderFloat("Phi Depth", &svgf.PhiDepth(), 0.05f, 10.0f, "%.2f");
        ImGui::TextDisabled("Rendering mode path-tracer denoiser");
    }
    ImGui::PopID();
}

void ImGuiLayer::DrawPBRIBLDebugWindow()
{
    PBRIBL& ibl = Application::Get().GetPBRIBL();
    const char* views[] = { "Lit", "Albedo", "World Normal", "IBL Diffuse", "IBL Specular" };

    ImGui::PushID("PBRIBLDebug");
    ImGui::Checkbox("Enable IBL", &ibl.Enabled());
    ImGui::Checkbox("Diffuse IBL", &ibl.DiffuseEnabled());
    ImGui::Checkbox("Specular IBL", &ibl.SpecularEnabled());
    ImGui::SliderFloat("Diffuse Intensity", &ibl.DiffuseIntensity(), 0.0f, 4.0f, "%.2f");
    ImGui::SliderFloat("Specular Intensity", &ibl.SpecularIntensity(), 0.0f, 4.0f, "%.2f");
    ImGui::Combo("PBR View", &ibl.DebugMode(), views, IM_ARRAYSIZE(views));
    ImGui::TextDisabled("Albedo isolates texture; Normal/IBL reveal shading stripes.");
    ImGui::PopID();
}

void ImGuiLayer::DrawMenuBar()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Project"))
                Application::Get().NewProject();

            ImGui::Separator();

            if (ImGui::MenuItem("Open Model"))
                OpenModelFile();
            if (ImGui::MenuItem("Open GCode"))
                OpenGCodeFile();

            if (ImGui::MenuItem("Close All"))
                Application::Get().ClearObject3Ds();

            ImGui::Separator();

            if (ImGui::MenuItem("Exit"))
                Application::Get().Close();

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Mode"))
        {
            Application& app = Application::Get();
            bool isGame = (app.GetAppMode() == Application::AppMode::Game);
            if (ImGui::MenuItem("Editor", nullptr, !isGame))
                app.SetAppMode(Application::AppMode::Editor);
            if (ImGui::MenuItem("Game", "F11", isGame))
                app.SetAppMode(Application::AppMode::Game);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Environment"))
        {
            int& backgroundMode = Application::Get().GetBackgroundMode();
            if (ImGui::MenuItem("CubeMap", nullptr, backgroundMode == 1))
                backgroundMode = 1;
            if (ImGui::MenuItem("SH Map", nullptr, backgroundMode == 0))
                backgroundMode = 0;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Windows"))
        {
            ImGui::MenuItem("NodeEditor", nullptr, &m_ShowNodeEditor);
            bool showTanim = Application::Get().GetTimelineAnimation().IsVisible();
            if (ImGui::MenuItem("Tanim", nullptr, &showTanim))
            {
                Application::Get().GetTimelineAnimation().SetVisible(showTanim);
                if (showTanim)
                    Application::Get().GetCameraAnimation().SetVisible(false);
            }
            bool showCameraTimeline = Application::Get().GetCameraAnimation().IsVisible();
            if (ImGui::MenuItem("Camera Timeline", nullptr, &showCameraTimeline))
            {
                Application::Get().GetCameraAnimation().SetVisible(showCameraTimeline);
                if (showCameraTimeline)
                    Application::Get().GetTimelineAnimation().SetVisible(false);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("GameObject"))
        {
            if (ImGui::MenuItem("Cube"))
            {
                auto cube = Application::Get().GetScene().CreateCube();
                Application::Get().SetSelectedObjectIndex(Application::Get().GetScene().GetCount() - 1);
                TRACE("Created Cube: {}", cube ? "success" : "failed");
            }

            if (ImGui::MenuItem("Sphere"))
            {
                auto sphere = Application::Get().GetScene().CreateSphere();
                Application::Get().SetSelectedObjectIndex(Application::Get().GetScene().GetCount() - 1);
                TRACE("Created Sphere: {}", sphere ? "success" : "failed");
            }

            if (ImGui::MenuItem("Plane"))
            {
                auto plane = Application::Get().GetScene().CreatePlane();
                Application::Get().SetSelectedObjectIndex(Application::Get().GetScene().GetCount() - 1);
                TRACE("Created Plane: {}", plane ? "success" : "failed");
            }

            if (ImGui::MenuItem("Manix Volume"))
            {
                auto volume = Application::Get().LoadManixVolume();
                TRACE("Loaded Manix Volume: {}", volume ? "success" : "failed");
            }

            if (ImGui::MenuItem("CDLOD Hetch Terrain"))
            {
                auto terrain = Application::Get().LoadDefaultTerrainCDLOD();
                TRACE("Loaded CDLOD Hetch Terrain: {}", terrain ? "success" : "failed");
            }

            if (ImGui::MenuItem("Terrain HeightMap"))
            {
                auto terrain = Application::Get().LoadTerrainHeightMap();
                TRACE("Loaded Terrain HeightMap: {}", terrain ? "success" : "failed");
            }

            if (ImGui::MenuItem("Water Node"))
            {
                auto water = Application::Get().LoadWaterNode();
                TRACE("Loaded Water Node: {}", water ? "success" : "failed");
            }

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void ImGuiLayer::DrawProjectPanel()
{
    Application& app = Application::Get();
    const auto& entries = app.GetScene().GetObjects();

    if (entries.empty())
    {
        ImGui::TextDisabled("No objects in scene");
    }
    else
    {
        Scene& scene = app.GetScene();
        for (int i = 0; i < (int)entries.size(); i++)
        {
            const auto& entry = entries[i];

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

            bool selected = app.IsObjectSelected(i);
            if (selected)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
                flags |= ImGuiTreeNodeFlags_Selected;
            }

            // Visibility toggle
            Scene::Entry* e = scene.GetEntry(i);
            bool vis = e ? e->Visible : true;
            if (ImGui::Checkbox(("##vis" + std::to_string(i)).c_str(), &vis))
            {
                if (e) e->Visible = vis;
            }
            ImGui::SameLine();

            ImGui::TreeNodeEx((void*)(intptr_t)i, flags, "%s", entry.Name.c_str());
            if (ImGui::IsItemClicked())
            {
                if (ImGui::GetIO().KeyShift)
                    app.AddSelectedObjectIndex(i);
                else
                    app.SetSelectedObjectIndex(i);
            }

            if (selected)
                ImGui::PopStyleColor();
        }
    }
}

void ImGuiLayer::DrawPropertiesPanel()
{
    Application& app = Application::Get();
    Transform* targetTransform = app.GetGizmoTargetTransform();
    const int selectedCount = app.GetSelectedObjectCount();

    if (!targetTransform)
    {
        ImGui::TextDisabled("No object selected");
    }
    else
    {
        if (selectedCount > 1)
        {
            ImGui::TextDisabled("%d objects selected", selectedCount);
        }
        else
        {
            ImGui::SeparatorText("Transform");

            float translation[3] = { targetTransform->translation.x, targetTransform->translation.y, targetTransform->translation.z };
            if (ImGui::DragFloat3("Translation", translation, 0.1f))
            {
                targetTransform->translation = glm::vec3(translation[0], translation[1], translation[2]);
            }

            glm::vec3 euler = glm::degrees(quatToEulerSafe(targetTransform->rotation));
            float rotationDeg[3] = { euler.x, euler.y, euler.z };
            if (ImGui::InputFloat3("Rotation (deg)", rotationDeg, "%.2f"))
            {
                glm::vec3 rad = glm::radians(glm::vec3(rotationDeg[0], rotationDeg[1], rotationDeg[2]));
                targetTransform->rotation = glm::yawPitchRoll(rad.y, rad.x, rad.z);
            }

            float scale[3] = { targetTransform->scale.x, targetTransform->scale.y, targetTransform->scale.z };
            if (ImGui::DragFloat3("Scale", scale, 0.1f))
            {
                targetTransform->scale = glm::vec3(scale[0], scale[1], scale[2]);
            }

            Scene::Entry* selectedEntry = app.GetScene().GetSelectedEntry();
            if (selectedEntry && selectedEntry->Object)
            {
                ObjectInspectorRegistry::DrawInspector(*selectedEntry->Object);

                ImGui::SeparatorText("Keyframe Timeline");
                TimelineAnimation& timeline = app.GetTimelineAnimation();
                const int selectedIndex = app.GetSelectedObjectIndex();
                const bool hasTimeline = timeline.HasTimeline(*selectedEntry);
                if (!hasTimeline)
                {
                    if (ImGui::Button("Create Timeline"))
                        timeline.EnsureTimeline(*selectedEntry, selectedIndex);
                }
                else
                {
                    const std::string timelineName = timeline.GetTimelineName(*selectedEntry);
                    ImGui::TextDisabled("%s", timelineName.empty() ? "Timeline" : timelineName.c_str());

                    if (ImGui::Button("Edit Timeline"))
                        timeline.OpenEditor(*selectedEntry, selectedIndex);
                    ImGui::SameLine();
                    if (timeline.IsPlaying(*selectedEntry))
                    {
                        if (ImGui::Button("Pause"))
                            timeline.Pause(*selectedEntry);
                    }
                    else
                    {
                        if (ImGui::Button("Play"))
                            timeline.Play(*selectedEntry, selectedIndex);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Stop"))
                        timeline.Stop(*selectedEntry);

                    if (ImGui::Button("Load .tanim"))
                    {
                        auto files = pfd::open_file(
                            "Load Timeline",
                            "",
                            { "Tanim Timeline", "*.tanim", "All Files", "*" }).result();
                        if (!files.empty() && !timeline.Load(*selectedEntry, selectedIndex, std::filesystem::u8path(files[0])))
                            ::Log::GetCoreLogger()->error("Failed to load timeline: {}", files[0]);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Save .tanim"))
                    {
                        std::string defaultPath;
                        const std::filesystem::path currentPath = timeline.GetTimelinePath(*selectedEntry);
                        if (!currentPath.empty())
                            defaultPath = currentPath.u8string();
                        auto filepath = pfd::save_file(
                            "Save Timeline",
                            defaultPath,
                            { "Tanim Timeline", "*.tanim", "All Files", "*" }).result();
                        if (!filepath.empty())
                        {
                            std::filesystem::path path = std::filesystem::u8path(filepath);
                            if (path.extension().empty())
                                path += ".tanim";
                            if (!timeline.Save(*selectedEntry, path))
                                ::Log::GetCoreLogger()->error("Failed to save timeline: {}", path.u8string());
                        }
                    }
                }
            }
        }

        ImGui::SeparatorText("Gizmo");

        int& gizmoMode = app.GetGizmoMode();
        ImGui::Text("Mode:");
        ImGui::SameLine();
        ImGui::RadioButton("Move", &gizmoMode, 0); ImGui::SameLine();
        ImGui::RadioButton("Rotate", &gizmoMode, 1); ImGui::SameLine();
        ImGui::RadioButton("Scale", &gizmoMode, 2);

        bool local = app.GetGizmoLocal();
        if (ImGui::Checkbox("Local", &local))
            app.GetGizmoLocal() = local;
        ImGui::SameLine();
        bool view = app.GetGizmoView();
        if (ImGui::Checkbox("View", &view))
            app.GetGizmoView() = view;

        float gizmoSize = app.GetGizmoSize();
        if (ImGui::SliderFloat("Size", &gizmoSize, 0.1f, 5.0f))
            app.GetGizmoSize() = gizmoSize;

        float gizmoLineWidth = app.GetGizmoLineWidth();
        if (ImGui::SliderFloat("Line Width", &gizmoLineWidth, 0.5f, 10.0f))
            app.GetGizmoLineWidth() = gizmoLineWidth;

        bool active = IsGizmoActivate();
        ImGui::Text("Active: %s", active ? "Yes" : "No");

        if (selectedCount <= 1 && ImGui::Button("Reset"))
        {
            targetTransform->translation = glm::vec3(0.0f);
            targetTransform->rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            targetTransform->scale = glm::vec3(0.1f);
        }
        if (selectedCount <= 1)
        {
            ImGui::SameLine();
            if (ImGui::Button("Focus"))
            {
                if (m_Camera) m_Camera->setInputEnabled(true);
            }
        }
    }

    if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::TreeNodeEx("CSM", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawShadowDebugWindow();
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Probe GI", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawProbeGIDebugWindow();
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("SSAO", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawSSAODebugWindow();
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Anti-Aliasing", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawFXAADebugWindow();
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Denoising / SVGF", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawSVGFDenoiserWindow();
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("PBR / IBL", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawPBRIBLDebugWindow();
            ImGui::TreePop();
        }
    }
}

void ImGuiLayer::DrawContentBrowser()
{
    std::filesystem::path currentPath = std::filesystem::u8path(m_CurrentDir);
    std::error_code pathEc;
    const bool currentDriveRoot = IsDriveRootPath(currentPath);
    if ((currentDriveRoot && !IsDriveRootAvailable(currentPath)) ||
        (!currentDriveRoot && (!std::filesystem::exists(currentPath, pathEc) || !std::filesystem::is_directory(currentPath, pathEc))))
    {
        currentPath = std::filesystem::current_path();
        m_CurrentDir = currentPath.u8string();
        m_ContentBrowserNeedsRefresh = true;
        pathEc.clear();
    }
    if (m_ContentBrowserPath.empty())
        m_ContentBrowserPath = currentPath.u8string();

    auto refreshContentDirectory = [&]() {
        if (EnumerateContentDirectory(currentPath, m_ContentBrowserDirectories, m_ContentBrowserFiles))
        {
            m_ContentBrowserCachedDir = currentPath.u8string();
            m_ContentBrowserNeedsRefresh = false;
        }
        else
        {
            m_ContentBrowserCachedDir.clear();
            m_ContentBrowserDirectories.clear();
            m_ContentBrowserFiles.clear();
            m_ContentBrowserNeedsRefresh = false;
        }
    };

    if (m_ContentBrowserNeedsRefresh || m_ContentBrowserCachedDir != currentPath.u8string())
        refreshContentDirectory();

    auto setCurrentDirectory = [&](const std::filesystem::path& path) {
        std::error_code ec;
        const bool driveRoot = IsDriveRootPath(path);
        if (driveRoot && !IsDriveRootAvailable(path))
            return false;
        if (!driveRoot && (!std::filesystem::exists(path, ec) || !std::filesystem::is_directory(path, ec)))
            return false;

        std::filesystem::path displayPath = path;
        if (!driveRoot)
        {
            displayPath = std::filesystem::weakly_canonical(path, ec);
            if (ec)
            {
                ec.clear();
                displayPath = path;
            }
        }

        m_CurrentDir = displayPath.u8string();
        m_ContentBrowserPath = m_CurrentDir;
        currentPath = displayPath;
        m_ContentBrowserNeedsRefresh = true;
        return true;
    };

    auto navigateToTypedPath = [&]() {
        std::error_code ec;
        std::filesystem::path typedPath = std::filesystem::u8path(m_ContentBrowserPath);
        const bool driveRoot = IsDriveRootPath(typedPath);
        if (driveRoot && !IsDriveRootAvailable(typedPath))
            return false;
        if (!driveRoot && (!std::filesystem::exists(typedPath, ec) || !std::filesystem::is_directory(typedPath, ec)))
            return false;
        return setCurrentDirectory(typedPath);
    };

    auto toLowerExtension = [](const std::filesystem::path& path) {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension;
    };

    auto makeEllipsis = [](const std::string& text, float maxWidth) {
        if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
            return text;

        const char* ellipsis = "...";
        std::string clipped = text;
        while (!clipped.empty())
        {
            clipped.pop_back();
            std::string candidate = clipped + ellipsis;
            if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth)
                return candidate;
        }
        return std::string(ellipsis);
    };

    auto isTextFile = [&](const std::filesystem::path& path) {
        return toLowerExtension(path) == ".txt";
    };

    auto isModelIconFile = [&](const std::filesystem::path& path) {
        const std::string extension = toLowerExtension(path);
        return extension == ".obj" || extension == ".ply" || extension == ".mmd" ||
            extension == ".pmx" || extension == ".pmd" || extension == ".stl" ||
            extension == ".gltf" || extension == ".glb" || extension == ".step" ||
            extension == ".stp";
    };

    auto loadIconTexture = [](const char* name, const char* relativePath) -> Ref<Texture> {
        const std::string path = GetFilePath(relativePath);
        Ref<Texture> texture = TextureLibrary::GetTexture(path);
        if (!texture)
            WARN("Content Browser icon texture failed: {} -> {}", name, path);
        return texture;
    };

    static Ref<Texture> txtIcon = loadIconTexture("content_browser_icon_txt", "../data/images/content_browser/icon_txt.png");
    static Ref<Texture> modelIcon = loadIconTexture("content_browser_icon_model", "../data/images/content_browser/icon_model.png");
    static Ref<Texture> ncIcon = loadIconTexture("content_browser_icon_nc", "../data/images/content_browser/icon_nc.png");
    static Ref<Texture> unknownIcon = loadIconTexture("content_browser_icon_unknown", "../data/images/content_browser/icon_unknown.png");
    static Ref<Texture> driveCIcon = loadIconTexture("content_browser_icon_drive_c", "../data/images/content_browser/icon_drive_c.png");
    static Ref<Texture> driveDIcon = loadIconTexture("content_browser_icon_drive_d", "../data/images/content_browser/icon_drive_d.png");
    static Ref<Texture> driveEIcon = loadIconTexture("content_browser_icon_drive_e", "../data/images/content_browser/icon_drive_e.png");
    static Ref<Texture> driveFIcon = loadIconTexture("content_browser_icon_drive_f", "../data/images/content_browser/icon_drive_f.png");

    auto iconForFile = [&](const std::filesystem::path& path) -> Ref<Texture> {
        if (isTextFile(path) && txtIcon)
            return txtIcon;
        if (isModelIconFile(path) && modelIcon)
            return modelIcon;
        if (IsSupportedGCodeFile(path) && ncIcon)
            return ncIcon;
        return unknownIcon;
    };

    auto drawFallbackIcon = [](ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const char* label, ImU32 color) {
        drawList->AddRectFilled(min, max, IM_COL32(38, 43, 51, 255), 5.0f);
        drawList->AddRect(min, max, IM_COL32(220, 224, 232, 210), 5.0f);
        ImVec2 textSize = ImGui::CalcTextSize(label);
        drawList->AddText(ImVec2(min.x + (max.x - min.x - textSize.x) * 0.5f, min.y + (max.y - min.y - textSize.y) * 0.5f),
            color, label);
    };

    auto drawTextureIcon = [](ImDrawList* drawList, const Ref<Texture>& texture, const ImVec2& min, const ImVec2& max) {
        if (!texture)
            return;
        drawList->AddImage((ImTextureID)(uint64_t)texture->m_RendererID, min, max, ImVec2(0, 1), ImVec2(1, 0));
    };

    // Path navigation
    char pathBuf[512];
    std::string pathStr = m_ContentBrowserPath;
    strncpy_s(pathBuf, pathStr.c_str(), sizeof(pathBuf));
    ImGui::TextUnformatted("Path");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-34.0f);
    if (ImGui::InputText("##ContentBrowserPath", pathBuf, sizeof(pathBuf), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        m_ContentBrowserPath = pathBuf;
        navigateToTypedPath();
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
        m_ContentBrowserPath = pathBuf;
    ImGui::SameLine();
    if (ImGui::ArrowButton("##GoContentBrowserPath", ImGuiDir_Right))
        navigateToTypedPath();

    ImGui::BeginChild("ContentBrowserDrives", ImVec2(0.0f, 82.0f), false, ImGuiWindowFlags_NoScrollbar);
    struct DriveShortcut
    {
        char Letter;
        Ref<Texture> Icon;
    };
    const DriveShortcut drives[] = {
        { 'C', driveCIcon },
        { 'D', driveDIcon },
        { 'E', driveEIcon },
        { 'F', driveFIcon },
    };
    for (const DriveShortcut& drive : drives)
    {
        std::filesystem::path drivePath(std::string(1, drive.Letter) + ":\\");
        const bool driveAvailable = IsDriveRootAvailable(drivePath);
        ImGui::PushID(drive.Letter);
        if (!driveAvailable)
            ImGui::BeginDisabled();
        if (drive.Icon)
        {
            ImGui::ImageButton("##Drive", (ImTextureID)(uint64_t)drive.Icon->m_RendererID,
                ImVec2(64.0f, 64.0f), ImVec2(0, 1), ImVec2(1, 0));
        }
        else
        {
            ImGui::Button((std::string(1, drive.Letter) + ":").c_str(), ImVec2(64.0f, 64.0f));
        }
        if (ImGui::IsItemClicked() && driveAvailable)
            setCurrentDirectory(drivePath);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(driveAvailable ? "%c:\\" : "%c:\\ not available", drive.Letter);
        if (!driveAvailable)
            ImGui::EndDisabled();
        ImGui::PopID();
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::EndChild();

    try
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        const float leftWidth = glm::clamp(avail.x * 0.28f, 170.0f, 320.0f);

        ImGui::BeginChild("ContentBrowserDirectories", ImVec2(leftWidth, 0.0f), true);
        ImGui::TextDisabled("Directories");
        ImGui::Separator();
        if (ImGui::Selectable("\xE4\xB8\x8A\xE4\xB8\x80\xE7\xBA\xA7", false))
        {
            std::filesystem::path parentPath = currentPath.parent_path();
            if (!parentPath.empty() && parentPath != currentPath)
                setCurrentDirectory(parentPath);
        }
        for (const auto& entry : m_ContentBrowserDirectories)
        {
            const std::filesystem::path path = entry;
            const std::string filename = path.filename().u8string();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.86f, 0.78f, 0.36f, 1.0f));
            if (ImGui::Selectable(filename.c_str(), false))
                setCurrentDirectory(path);
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ContentBrowserFiles", ImVec2(0.0f, 0.0f), true);
        ImGui::TextDisabled("%zu files", m_ContentBrowserFiles.size());
        ImGui::Separator();

        const float tileWidth = 104.0f;
        const float tileHeight = 112.0f;
        const float iconSize = 64.0f;
        const float spacing = 12.0f;
        const float contentWidth = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, (int)((contentWidth + spacing) / (tileWidth + spacing)));

        for (int i = 0; i < (int)m_ContentBrowserFiles.size(); i++)
        {
            const std::filesystem::path path = m_ContentBrowserFiles[i];
            const std::string filename = path.filename().u8string();
            const std::string selectedKey = path.u8string();
            const bool selected = m_SelectedFile == selectedKey;
            const bool isModel = IsSupportedModelFile(path);
            const bool isGCode = IsSupportedGCodeFile(path);
            Ref<Texture> icon = iconForFile(path);

            if (i > 0 && (i % columns) != 0)
                ImGui::SameLine(0.0f, spacing);

            ImGui::PushID(selectedKey.c_str());
            ImVec2 tileMin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##FileTile", ImVec2(tileWidth, tileHeight), ImGuiButtonFlags_MouseButtonLeft);
            const bool hovered = ImGui::IsItemHovered();
            const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            const bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

            if (clicked)
                m_SelectedFile = selectedKey;
            if (doubleClicked && isModel)
            {
                if (!Application::Get().LoadObject3D(path.u8string()))
                    WARN("Failed to load: {}", path.u8string());
            }
            if (doubleClicked && isGCode)
            {
                if (!Application::Get().LoadGCode(path))
                    WARN("Failed to load GCode: {}", path.u8string());
            }

            ImVec2 tileMax(tileMin.x + tileWidth, tileMin.y + tileHeight);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (selected || hovered)
            {
                ImU32 bg = selected ? IM_COL32(58, 88, 130, 170) : IM_COL32(255, 255, 255, 36);
                drawList->AddRectFilled(tileMin, tileMax, bg, 6.0f);
                drawList->AddRect(tileMin, tileMax, selected ? IM_COL32(103, 154, 222, 210) : IM_COL32(255, 255, 255, 50), 6.0f);
            }

            ImVec2 iconMin(tileMin.x + (tileWidth - iconSize) * 0.5f, tileMin.y + 10.0f);
            ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
            if (icon)
                drawTextureIcon(drawList, icon, iconMin, iconMax);
            else if (isModel)
                drawFallbackIcon(drawList, iconMin, iconMax, "3D", IM_COL32(241, 164, 61, 255));
            else if (isGCode)
                drawFallbackIcon(drawList, iconMin, iconMax, "NC", IM_COL32(88, 220, 145, 255));
            else if (isTextFile(path))
                drawFallbackIcon(drawList, iconMin, iconMax, "TXT", IM_COL32(88, 166, 255, 255));
            else
                drawFallbackIcon(drawList, iconMin, iconMax, "FILE", IM_COL32(154, 164, 178, 255));

            std::string visibleName = makeEllipsis(filename, tileWidth - 10.0f);
            ImVec2 textSize = ImGui::CalcTextSize(visibleName.c_str());
            ImVec2 textPos(tileMin.x + (tileWidth - textSize.x) * 0.5f, tileMin.y + 80.0f);
            ImU32 textColor = isModel ? IM_COL32(150, 204, 255, 255) :
                (isGCode ? IM_COL32(128, 230, 150, 255) : IM_COL32(225, 228, 235, 255));
            drawList->AddText(textPos, textColor, visibleName.c_str());

            if (hovered)
                ImGui::SetTooltip("%s", path.u8string().c_str());

            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    catch (const std::exception&)
    {
        ImGui::TextDisabled("Unable to browse directory");
    }
}

void ImGuiLayer::DrawConsolePanel()
{
    if (ImGui::Button("Clear"))
    {
        s_ConsoleMessages.clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu messages", s_ConsoleMessages.size());

    ImGui::BeginChild("ConsoleScroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));
    for (const auto& msg : s_ConsoleMessages)
    {
        ImGui::TextColored(msg.Color, "%s", msg.Message.c_str());
    }

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);

    ImGui::PopStyleVar();
    ImGui::EndChild();
}

void ImGuiLayer::DrawViewportPanel()
{
    Application& app = Application::Get();

    bool editorMode = app.GetViewportRenderMode() == Application::ViewportRenderMode::Editor;
    if (ImGui::Selectable("Editor", editorMode, 0, ImVec2(86.0f, 0.0f)))
        app.SetViewportRenderMode(Application::ViewportRenderMode::Editor);
    ImGui::SameLine();
    if (ImGui::Selectable("Rendering", !editorMode, 0, ImVec2(96.0f, 0.0f)))
        app.SetViewportRenderMode(Application::ViewportRenderMode::Rendering);
    if (!editorMode)
    {
        ImGui::SameLine();
        PathTracer& tracer = app.GetPathTracer();
        ImGui::TextDisabled("SPP %u | Tris %u | %s", tracer.GetSampleCount(), tracer.GetTriangleCount(),
            tracer.GetStatus().c_str());
        ImGui::SameLine();
        SVGF& svgf = app.GetSVGF();
        bool svgfEnabled = svgf.Enabled();
        if (ImGui::Checkbox("SVGF", &svgfEnabled))
        {
            svgf.Enabled() = svgfEnabled;
            svgf.ResetHistory();
            tracer.ResetAccumulation();
        }
    }
    ImGui::Separator();

    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    app.SetViewportSize(glm::vec2(viewportSize.x, viewportSize.y));

    // Draw FBO texture as the 3D viewport
    auto fbo = app.GetViewportFBO();
    if (fbo && viewportSize.x > 0 && viewportSize.y > 0)
    {
        uint64_t textureID = app.GetViewportColorTextureID();
        
        // Debug: Check if textureID is valid
        if (textureID == 0)
        {
            WARN("Viewport FBO texture ID is 0 or null! FBO may not be properly initialized.");
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Viewport texture not available");
        }
        else
        {
            ImGui::Image((ImTextureID)textureID, viewportSize, ImVec2(0, 1), ImVec2(1, 0));

            const bool imageHovered = ImGui::IsItemHovered();
            ImVec2 itemMin = ImGui::GetItemRectMin();
            ImVec2 itemMax = ImGui::GetItemRectMax();
            const bool axisIndicatorHovered = DrawViewportAxisIndicator(app, itemMin, itemMax);
            const bool viewportHovered = imageHovered && !axisIndicatorHovered;
            app.SetViewportHovered(viewportHovered);

            if (viewportHovered)
            {
                app.SetViewportOrigin(glm::vec2(itemMin.x, itemMin.y));
                ImVec2 mousePos = ImGui::GetMousePos();
                app.GetViewportMousePos() = glm::vec2(mousePos.x - itemMin.x, mousePos.y - itemMin.y);
            }
        }
    }
    else
    {
        if (!fbo)
            WARN("Viewport FBO is null!");
        if (viewportSize.x <= 0 || viewportSize.y <= 0)
            WARN("Viewport size is invalid: {}x{}", viewportSize.x, viewportSize.y);
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Viewport not initialized");
    }
}

// pfd returns UTF-8 on Windows; keep as UTF-8, use u8path for filesystem API
// (no ANSI conversion needed - std::filesystem::u8path handles UTF-8 directly)

void ImGuiLayer::OpenModelFile()
{
    auto selectedFiles = pfd::open_file(
        "Open 3D Model",
        "",
        {
            "3D Model Files", "*.obj *.stl *.ply *.gltf *.glb *.pmx *.pmd *.step *.stp",
            "All Files", "*"
        }).result();

    if (selectedFiles.empty())
        return;

    // pfd returns UTF-8 on Windows; use u8path for filesystem operations
    const std::string& utf8path = selectedFiles[0];
    auto fspath = std::filesystem::u8path(utf8path);

    if (!IsSupportedModelFile(fspath))
    {
        WARN("Unsupported model file: {}", utf8path);
        return;
    }

    TRACE("Opening model file: {}", utf8path);
    if (!std::filesystem::exists(fspath))
    {
        ::Log::GetCoreLogger()->error("File does not exist: {}", utf8path);
        return;
    }

    if (!Application::Get().LoadObject3D(fspath))
        ::Log::GetCoreLogger()->error("Failed to load model file: {}", fspath.u8string());
}

void ImGuiLayer::OpenGCodeFile()
{
    auto selectedFiles = pfd::open_file(
        "Open GCode",
        "",
        {
            "GCode Files", "*.gcode *.nc *.cnc *.tap",
            "All Files", "*"
        }).result();

    if (selectedFiles.empty())
        return;

    const std::string& utf8path = selectedFiles[0];
    auto fspath = std::filesystem::u8path(utf8path);
    if (!IsSupportedGCodeFile(fspath))
    {
        WARN("Unsupported GCode file: {}", utf8path);
        return;
    }
    if (!std::filesystem::exists(fspath))
    {
        ::Log::GetCoreLogger()->error("File does not exist: {}", utf8path);
        return;
    }
    if (!Application::Get().LoadGCode(fspath))
        ::Log::GetCoreLogger()->error("Failed to load GCode file: {}", fspath.u8string());
}

bool ImGuiLayer::IsSupportedModelFile(const std::filesystem::path& filepath) const
{
    std::string extension = filepath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return extension == ".obj" || extension == ".stl" || extension == ".ply" ||
           extension == ".gltf" || extension == ".glb" ||
           extension == ".mmd" || extension == ".pmx" || extension == ".pmd" ||
           extension == ".step" || extension == ".stp";
}

bool ImGuiLayer::IsSupportedGCodeFile(const std::filesystem::path& filepath) const
{
    std::string extension = filepath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return extension == ".gcode" || extension == ".nc" || extension == ".cnc" || extension == ".tap";
}

void ImGuiLayer::OnEvent(Event& event)
{
    EventDispatcher dispatcher(event);
    dispatcher.Dispatch<MouseButtonPressedEvent>(BIND_EVENT_FN(OnMouseButtonDown));
    dispatcher.Dispatch<MouseButtonReleasedEvent>(BIND_EVENT_FN(OnMouseButtonUp));
    dispatcher.Dispatch<MouseMovedEvent>(BIND_EVENT_FN(OnMouseMove));
}

void ImGuiLayer::Begin()
{
#ifdef G_DX11
    ImGui_ImplDX11_NewFrame();
#else
    ImGui_ImplOpenGL3_NewFrame();
#endif
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::End()
{
    ImGuiIO& io = ImGui::GetIO();
    Application& app = Application::Get();
    io.DisplaySize = ImVec2((float)app.GetWindow().GetWidth(), (float)app.GetWindow().GetHeight());

    ImGui::Render();
#ifdef G_DX11
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#else
    auto shader = Application::Get().GetShaderLibrary()->Get("DefaultColor");
    shader->Bind();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
}


void ImGuiLayer::SetDarkThemeColors()
{
    auto& colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg] = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };

    colors[ImGuiCol_Header] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
    colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
    colors[ImGuiCol_HeaderActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

    colors[ImGuiCol_Button] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
    colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
    colors[ImGuiCol_ButtonActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

    colors[ImGuiCol_FrameBg] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
    colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
    colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

    colors[ImGuiCol_Tab] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_TabHovered] = ImVec4{ 0.38f, 0.3805f, 0.381f, 1.0f };
    colors[ImGuiCol_TabActive] = ImVec4{ 0.28f, 0.2805f, 0.281f, 1.0f };
    colors[ImGuiCol_TabUnfocused] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };

    colors[ImGuiCol_TitleBg] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
}


bool ImGuiLayer::OnMouseButtonDown(MouseButtonPressedEvent& e)
{
    Application& app = Application::Get();
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse && !app.IsViewportHovered())
        return false;

    if (e.GetMouseButton() == 1)
    {
        m_Camera->setInputEnabled(true);
        m_LeftDownCamera = true;
        app.GetLeftDownCamera() = true;
    }
    else if (e.GetMouseButton() == 0)
    {
        bool mouseOverGizmo = false;
        Transform* targetTransform = app.GetGizmoTargetTransform();
        if (app.IsViewportHovered() && targetTransform)
        {
            int gizmoFlags = 0;
            switch (app.GetGizmoMode())
            {
            case 0: gizmoFlags = GIZMO_TRANSLATE; break;
            case 1: gizmoFlags = GIZMO_ROTATE;    break;
            case 2: gizmoFlags = GIZMO_SCALE;     break;
            case 3: gizmoFlags = GIZMO_ALL;       break;
            default: gizmoFlags = GIZMO_TRANSLATE; break;
            }
            if (app.GetGizmoLocal()) gizmoFlags |= GIZMO_LOCAL;
            if (app.GetGizmoView())  gizmoFlags |= GIZMO_VIEW;
            SetGizmoSize(app.GetGizmoSize());
            SetGizmoLineWidth(app.GetGizmoLineWidth());
            SetGizmoViewportSize((int)app.GetViewportSize().x, (int)app.GetViewportSize().y);
            mouseOverGizmo = isMouseOverGizmo(m_Camera->GetViewMatrix(), m_Camera->GetProjectionMatrix(),
                false, app.GetViewportMousePos(), gizmoFlags, targetTransform);
        }

        if (app.IsViewportHovered() && !IsGizmoActivate() && !mouseOverGizmo)
        {
            const glm::vec2& viewportMouse = app.GetViewportMousePos();
            const glm::vec2& viewportSize = app.GetViewportSize();
            const int x = (int)viewportMouse.x;
            const int y = (int)(viewportSize.y - viewportMouse.y - 1.0f);
            const int objectID = app.ReadPickupPixel(x, y);
            const int objectIndex = objectID - 1;
            const bool hasPickedObject = objectIndex >= 0 && objectIndex < app.GetScene().GetCount();
            if (io.KeyShift)
            {
                if (hasPickedObject)
                    app.AddSelectedObjectIndex(objectIndex);
            }
            else
            {
                app.SetSelectedObjectIndex(hasPickedObject ? objectIndex : -1);
            }
        }
        m_LeftDownGizmo = true;
        app.GetLeftDownGizmo() = true;
    }

    return false;
};

bool ImGuiLayer::OnMouseButtonUp(MouseButtonReleasedEvent& e)
{
    Application& app = Application::Get();
    if (e.GetMouseButton() == 1)
    {
        m_Camera->setInputEnabled(false);
        m_LeftDownCamera = false;
        app.GetLeftDownCamera() = false;
    }
    else if (e.GetMouseButton() == 0)
    {
        m_LeftDownGizmo = false;
        app.GetLeftDownGizmo() = false;
    }
    return false;
};

bool ImGuiLayer::OnMouseMove(MouseMovedEvent& e)
{
    if (m_Camera->isInputEnabled())
        m_LeftDownGizmo = false;
    m_MousePos = { e.GetX(),e.GetY() };
    m_Camera->processMouseMovement(e.GetX(), e.GetY());

    // Update viewport-relative mouse position for gizmo tracking
    Application& app = Application::Get();
    const glm::vec2& origin = app.GetViewportOrigin();
    app.GetViewportMousePos() = glm::vec2(m_MousePos.x - origin.x, m_MousePos.y - origin.y);
    return false;
};
