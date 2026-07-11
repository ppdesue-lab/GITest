#pragma once

#include <base.h>
#include "Layer.h"
#include "MouseEvent.h"
#include "KeyEvent.h"
#include <Camera/Camera.h>
#include <Transform.h>
#include <Import/Vector2D/DxfLoader.h>
#include <imgui.h>
#include <deque>
#include <string>
#include <filesystem>
#include <vector>

struct ConsoleMessage
{
    ImVec4 Color;
    std::string Message;
};

class ImGuiLayer : public Layer
{
public:
    ImGuiLayer();
    virtual ~ImGuiLayer() = default;

    virtual void OnAttach() override;
    virtual void OnDetach() override;
    virtual void OnUpdate() override;
    virtual void OnImGuiRender() override;
    virtual void OnEvent(Event& event) override;

    void SetDarkThemeColors();

    void Begin();
    void End();
    void QueueFileImport(const std::filesystem::path& filepath);
    void QueueDxfImport(const std::filesystem::path& filepath);

    static void AddConsoleMessage(const ImVec4& color, const std::string& message);

private:
    void DrawMenuBar();
    void DrawEditorLayout(ImVec2 pos, ImVec2 size, float menuBarHeight);

    void DrawProjectPanel();
    void DrawPropertiesPanel();
    void DrawContentBrowser();
    void DrawConsolePanel();
    void DrawViewportPanel();
    void DrawNodeEditorWindow();
    void DrawShadowDebugWindow();
    void DrawProbeGIDebugWindow();
    void DrawSSAODebugWindow();
    void DrawFXAADebugWindow();
    void DrawSVGFDenoiserWindow();
    void DrawPBRIBLDebugWindow();
    void DrawImportOptionsModal();
    void OpenModelFile();
    void OpenGCodeFile();
    bool IsSupportedModelFile(const std::filesystem::path& filepath) const;
    bool IsSupported2DFile(const std::filesystem::path& filepath) const;
    bool IsSupportedGCodeFile(const std::filesystem::path& filepath) const;
    bool IsSupportedImageFile(const std::filesystem::path& filepath) const;

    bool OnMouseButtonDown(MouseButtonPressedEvent& e);
    bool OnMouseButtonUp(MouseButtonReleasedEvent& e);
    bool OnKeyPressed(KeyPressedEvent& e);
    bool OnMouseMove(MouseMovedEvent& e);
    bool OnMouseScrolled(MouseScrolledEvent& e);
    void SelectObjectsInViewportRect(const glm::vec2& start, const glm::vec2& end, bool appendSelection);
    void Select2DSubElementsInViewportRect(const glm::vec2& start, const glm::vec2& end, bool appendSelection);

private:
    bool m_LeftDownCamera = false;
    bool m_LeftDownGizmo = false;
    bool m_MiddleDownViewport2D = false;
    bool m_ViewportSelectRectActive = false;
    double m_LastViewport2DLeftClickTime = -1.0;
    glm::vec2 m_LastViewport2DLeftClickPos = glm::vec2(0.0f);
    glm::vec2 m_ViewportSelectStart = glm::vec2(0.0f);
    glm::vec2 m_ViewportSelectEnd = glm::vec2(0.0f);
    bool m_ShowViewport2DProperties = false;
    ImVec2 m_Viewport2DPropertiesPos = ImVec2(0.0f, 0.0f);

    glm::vec2 m_MousePos;
	Transform g_DefaultTransform;
	Ref<Camera> m_Camera;
    float m_Time = 0.0f;
    bool m_UpdateGizmo = false;

    // Panel state
    std::string m_ContentBrowserPath;
    std::string m_SelectedFile;
    std::string m_CurrentDir;
    std::string m_ContentBrowserCachedDir;
    std::vector<std::filesystem::path> m_ContentBrowserDirectories;
    std::vector<std::filesystem::path> m_ContentBrowserFiles;
    bool m_ContentBrowserNeedsRefresh = true;
    bool m_ShowNodeEditor = false;
    bool m_ImportPopupRequested = false;
    std::deque<std::filesystem::path> m_PendingImportPaths;
    std::filesystem::path m_ActiveImportPath;
    DxfImportMode m_SelectedDxfImportMode = DxfImportMode::LinesWithArcFit;

    // Console
    static std::deque<ConsoleMessage> s_ConsoleMessages;
    static const size_t MAX_CONSOLE_MESSAGES = 500;
    char m_ConsoleFilter[128] = {};
};
