#include "ImGuiLayer.h"
#include "Log.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <glm/gtx/euler_angles.hpp>

#include "Application.h"
#include <Primitive/Gizmo.h>
#include <Renderer/RenderCommand.h>

#ifdef ERROR
#undef ERROR
#endif
#include <thirdparty/ofd/portable-file-dialogs.h>
#ifdef ERROR
#undef ERROR
#endif

#include <cctype>
#include <filesystem>
#include <mutex>

#include <spdlog/sinks/base_sink.h>

#include <backends/imgui_impl_glfw.h>
#ifdef G_DX11
#include <backends/imgui_impl_dx11.h>
#include <Platform/DX11/DX11Context.h>
#else
#include <backends/imgui_impl_opengl3.h>
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

    float fontSize = 18.0f;
    io.Fonts->AddFontFromFileTTF(GetFilePath("../data/fonts/CangErYuYangTiW03-2.ttf").c_str(), fontSize, nullptr, io.Fonts->GetGlyphRangesChineseFull());
    io.Fonts->Build();

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
    m_CurrentDir = std::filesystem::current_path().string();
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
}

void ImGuiLayer::DrawEditorLayout(ImVec2 pos, ImVec2 size, float menuBarHeight)
{
    float leftW = size.x * 0.18f;
    float rightW = size.x * 0.22f;
    float centerW = size.x - leftW - rightW;
    float bottomH = (size.y - menuBarHeight) * 0.25f;
    float topH = (size.y - menuBarHeight) - bottomH;

    ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;

    // Project panel (left)
    ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y + menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(leftW, size.y - menuBarHeight));
    ImGui::Begin("Project", nullptr, panelFlags);
    DrawProjectPanel();
    ImGui::End();

    // Properties panel (right)
    ImGui::SetNextWindowPos(ImVec2(pos.x + leftW + centerW, pos.y + menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(rightW, size.y - menuBarHeight));
    ImGui::Begin("Properties", nullptr, panelFlags);
    DrawPropertiesPanel();
    ImGui::End();

    // Viewport panel (center top)
    ImGui::SetNextWindowPos(ImVec2(pos.x + leftW, pos.y + menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(centerW, topH));
    ImGui::Begin("Viewport", nullptr, panelFlags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar);
    DrawViewportPanel();
    ImGui::End();

    // Content Browser (bottom left)
    ImGui::SetNextWindowPos(ImVec2(pos.x + leftW, pos.y + menuBarHeight + topH));
    ImGui::SetNextWindowSize(ImVec2(centerW * 0.5f, bottomH));
    ImGui::Begin("Content Browser", nullptr, panelFlags);
    DrawContentBrowser();
    ImGui::End();

    // Console (bottom right)
    ImGui::SetNextWindowPos(ImVec2(pos.x + leftW + centerW * 0.5f, pos.y + menuBarHeight + topH));
    ImGui::SetNextWindowSize(ImVec2(centerW * 0.5f, bottomH));
    ImGui::Begin("Console", nullptr, panelFlags);
    DrawConsolePanel();
    ImGui::End();
}

void ImGuiLayer::DrawMenuBar()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open Model"))
                OpenModelFile();

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

        if (ImGui::BeginMenu("GameObject"))
        {
            if (ImGui::MenuItem("Cube"))
            {
                std::vector<VertexColor> verts = {
                    {{-1.0f, -1.0f, -1.0f}, {0.8f, 0.2f, 0.2f, 1.0f}},
                    {{ 1.0f, -1.0f, -1.0f}, {0.8f, 0.2f, 0.2f, 1.0f}},
                    {{ 1.0f,  1.0f, -1.0f}, {0.8f, 0.2f, 0.2f, 1.0f}},
                    {{-1.0f,  1.0f, -1.0f}, {0.8f, 0.2f, 0.2f, 1.0f}},
                    {{-1.0f, -1.0f,  1.0f}, {0.2f, 0.8f, 0.2f, 1.0f}},
                    {{ 1.0f, -1.0f,  1.0f}, {0.2f, 0.8f, 0.2f, 1.0f}},
                    {{ 1.0f,  1.0f,  1.0f}, {0.2f, 0.8f, 0.2f, 1.0f}},
                    {{-1.0f,  1.0f,  1.0f}, {0.2f, 0.8f, 0.2f, 1.0f}},
                };
                std::vector<uint32_t> idxs = {
                    0,1,2, 2,3,0, 4,5,6, 6,7,4,
                    0,1,5, 5,4,0, 2,3,7, 7,6,2,
                    0,3,7, 7,4,0, 1,2,6, 6,5,1
                };
                Application::Get().CreatePrimitive("Cube", verts, idxs);
            }

            if (ImGui::MenuItem("Sphere"))
            {
                std::vector<VertexColor> verts;
                std::vector<uint32_t> idxs;
                uint32_t sc = 24, st = 16;
                for (uint32_t i = 0; i <= st; ++i) {
                    float stackAngle = 3.14159f / 2.0f - i * 3.14159f / (float)st;
                    float xy = cosf(stackAngle);
                    float z = sinf(stackAngle);
                    for (uint32_t j = 0; j <= sc; ++j) {
                        float sectorAngle = j * 2.0f * 3.14159f / (float)sc;
                        float x = xy * cosf(sectorAngle);
                        float y = xy * sinf(sectorAngle);
                        verts.push_back({{x, y, z}, {0.3f, 0.5f, 0.9f, 1.0f}});
                    }
                }
                for (uint32_t i = 0; i < st; ++i) {
                    uint32_t k1 = i * (sc + 1);
                    uint32_t k2 = k1 + sc + 1;
                    for (uint32_t j = 0; j < sc; ++j, ++k1, ++k2) {
                        if (i != 0) { idxs.push_back(k1); idxs.push_back(k2); idxs.push_back(k1 + 1); }
                        if (i != st - 1) { idxs.push_back(k1 + 1); idxs.push_back(k2); idxs.push_back(k2 + 1); }
                    }
                }
                Application::Get().CreatePrimitive("Sphere", verts, idxs);
            }

            if (ImGui::MenuItem("Plane"))
            {
                std::vector<VertexColor> verts = {
                    {{-1.0f, 0.0f, -1.0f}, {0.7f, 0.7f, 0.7f, 0.8f}},
                    {{ 1.0f, 0.0f, -1.0f}, {0.7f, 0.7f, 0.7f, 0.8f}},
                    {{ 1.0f, 0.0f,  1.0f}, {0.7f, 0.7f, 0.7f, 0.8f}},
                    {{-1.0f, 0.0f,  1.0f}, {0.7f, 0.7f, 0.7f, 0.8f}},
                };
                std::vector<uint32_t> idxs = {0, 1, 2, 2, 3, 0};
                Application::Get().CreatePrimitive("Plane", verts, idxs);
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

            bool selected = (i == app.GetSelectedObjectIndex());
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

    if (!targetTransform)
    {
        ImGui::TextDisabled("No object selected");
        return;
    }

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

    if (ImGui::Button("Reset"))
    {
        targetTransform->translation = glm::vec3(0.0f);
        targetTransform->rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        targetTransform->scale = glm::vec3(0.1f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Focus"))
    {
        if (m_Camera) m_Camera->setInputEnabled(true);
    }
}

void ImGuiLayer::DrawContentBrowser()
{
    std::filesystem::path currentPath(m_CurrentDir);

    // Path navigation
    char pathBuf[512];
    std::string pathStr = currentPath.string();
    strncpy_s(pathBuf, pathStr.c_str(), sizeof(pathBuf));
    if (ImGui::InputText("Path", pathBuf, sizeof(pathBuf), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        std::filesystem::path newPath(pathBuf);
        if (std::filesystem::exists(newPath))
            m_CurrentDir = newPath.string();
    }

    ImGui::SameLine();
    if (ImGui::Button("Up"))
    {
        if (currentPath.has_parent_path())
            m_CurrentDir = currentPath.parent_path().string();
    }

    ImGui::BeginChild("ContentBrowserScroll");

    try
    {
        // List directories
        for (const auto& entry : std::filesystem::directory_iterator(currentPath))
        {
            const auto& path = entry.path();
            std::string filename = path.filename().string();

            if (entry.is_directory())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.2f, 1.0f));
                if (ImGui::Selectable(filename.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    if (ImGui::IsMouseDoubleClicked(0))
                    {
                        m_CurrentDir = path.string();
                    }
                }
                ImGui::PopStyleColor();
            }
        }

        // List files
        for (const auto& entry : std::filesystem::directory_iterator(currentPath))
        {
            const auto& path = entry.path();
            if (!entry.is_regular_file())
                continue;

            std::string filename = path.filename().string();
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            bool isModel = (extension == ".obj" || extension == ".stl" || extension == ".ply" ||
                           extension == ".gltf" || extension == ".glb" ||
                           extension == ".pmx" || extension == ".pmd");
            if (isModel)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.7f, 1.0f, 1.0f));

            if (ImGui::Selectable(filename.c_str(), m_SelectedFile == filename, ImGuiSelectableFlags_AllowDoubleClick))
            {
                m_SelectedFile = filename;
                if (ImGui::IsMouseDoubleClicked(0) && isModel)
                {
                    if (!Application::Get().LoadObject3D(path.u8string()))
                        WARN("Failed to load: {}", path.u8string());
                }
            }

            if (isModel)
                ImGui::PopStyleColor();
        }
    }
    catch (const std::exception&)
    {
        ImGui::TextDisabled("Unable to browse directory");
    }

    ImGui::EndChild();
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

    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    app.SetViewportSize(glm::vec2(viewportSize.x, viewportSize.y));

    // Draw FBO texture as the 3D viewport
    auto fbo = app.GetViewportFBO();
    if (fbo && viewportSize.x > 0 && viewportSize.y > 0)
    {
        uint64_t textureID = fbo->GetColorAttachmentRendererID(0);
        
        // Debug: Check if textureID is valid
        if (textureID == 0)
        {
            WARN("Viewport FBO texture ID is 0 or null! FBO may not be properly initialized.");
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Viewport texture not available");
        }
        else
        {
            ImGui::Image((ImTextureID)textureID, viewportSize, ImVec2(0, 1), ImVec2(1, 0));

            bool hovered = ImGui::IsItemHovered();
            app.SetViewportHovered(hovered);

            if (hovered)
            {
                ImVec2 itemMin = ImGui::GetItemRectMin();
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
            "3D Model Files", "*.obj *.stl *.ply *.gltf *.glb *.pmx *.pmd",
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

bool ImGuiLayer::IsSupportedModelFile(const std::filesystem::path& filepath) const
{
    std::string extension = filepath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return extension == ".obj" || extension == ".stl" || extension == ".ply" ||
           extension == ".gltf" || extension == ".glb" ||
           extension == ".pmx" || extension == ".pmd";
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
