#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace gcode
{
struct BoundingBox
{
    glm::vec3 Min{ std::numeric_limits<float>::max() };
    glm::vec3 Max{ std::numeric_limits<float>::lowest() };
    bool Valid = false;

    void Expand(const glm::vec3& point)
    {
        if (!Valid)
        {
            Min = point;
            Max = point;
            Valid = true;
            return;
        }
        Min = glm::min(Min, point);
        Max = glm::max(Max, point);
    }
};

struct ToolInfo
{
    int Number = 0;
    std::string Id;
    std::string Name;
    std::string Type;
    float HandleDiameter = 0.0f;
    float StickLength = 0.0f;
    float ShoulderLength = 0.0f;
    float FluteLength = 0.0f;
    float Diameter = 0.0f;
    float TipDiameter = 0.0f;
    float CornerRadius = 0.0f;
    float Angle = 0.0f;
    float HalfAngle = 0.0f;
    std::vector<float> Xs;
    std::vector<float> Ys;
};

struct Move
{
    std::string Code;
    glm::vec3 Position{ 0.0f };
    glm::vec3 MachinePosition{ 0.0f };
    glm::vec3 RotaryDegrees{ 0.0f };
    int ToolNumber = 0;
    size_t LineNo = 0;
    int ToolpathNumber = 0;
    bool IsLaserPath = false;
    bool IsMillingStart = false;
};

class CNCProgram
{
public:
    void Load(const std::string& filePath)
    {
        std::ifstream input(filePath);
        if (!input)
            throw std::runtime_error("failed to open GCode file: " + filePath);
        LoadFromStream(input, filePath);
    }

    void LoadContent(const std::string& content, const std::string& sourceName = "<content>")
    {
        std::istringstream input(content);
        LoadFromStream(input, sourceName);
    }

    const std::vector<Move>& GetMoves() const { return m_Moves; }
    const std::vector<ToolInfo>& GetTools() const { return m_Tools; }
    const BoundingBox& GetToolpathBounds() const { return m_ToolpathBounds; }
    const BoundingBox& GetDisplayBounds() const { return m_DisplayBounds; }
    bool Is4Axis() const { return m_Is4Axis; }

    const ToolInfo* FindTool(int toolNumber) const
    {
        const auto it = m_ToolIndexByNumber.find(toolNumber);
        if (it == m_ToolIndexByNumber.end())
            return nullptr;
        return &m_Tools[it->second];
    }

private:
    void LoadFromStream(std::istream& input, const std::string& sourceName)
    {
        m_SourceName = sourceName;
        m_Tools.clear();
        m_ToolIndexByNumber.clear();
        m_Moves.clear();
        m_ToolpathBounds = {};
        m_DisplayBounds = {};
        m_HasRotaryMotion = false;
        m_Is4Axis = false;

        glm::vec3 currentPosition(0.0f);
        glm::vec3 currentRotary(0.0f);
        std::string currentMotionCode;
        int currentTool = 0;
        int currentToolpath = 0;
        bool currentLaser = false;
        size_t lineNo = 0;

        std::string line;
        while (std::getline(input, line))
        {
            ++lineNo;
            TrimInPlace(line);
            if (line.empty())
                continue;

            if (line.rfind(";@MKR|TOOL|", 0) == 0)
            {
                ToolInfo tool = ParseToolLine(line);
                BuildToolProfile(tool);
                m_ToolIndexByNumber[tool.Number] = m_Tools.size();
                m_Tools.push_back(tool);
                continue;
            }

            if (line.rfind(";@MKR|TOOLPATH_START|", 0) == 0)
            {
                currentToolpath = ParseToolpathStartLine(line);
                continue;
            }

            if (line[0] == ';')
            {
                if (line.rfind(";(thumbnail_image_begin)", 0) == 0)
                    break;
                continue;
            }

            if (line[0] == 'T')
            {
                currentTool = ParseToolNumber(line);
                EnsureDefaultTool(currentTool);
                currentLaser = false;
            }

            if (ContainsCommand(line, "M321"))
            {
                EnsureLaserTool();
                currentTool = kLaserToolNumber;
                currentLaser = true;
            }

            const bool explicitMotion = IsMotionLine(line);
            const bool modalContinuation = !explicitMotion && IsModalMotionContinuationLine(line, currentMotionCode);
            if (!explicitMotion && !modalContinuation)
                continue;

            if (currentTool <= 0)
            {
                currentTool = 1;
                EnsureDefaultTool(currentTool);
            }
            currentPosition = ParseMotionLine(line, currentPosition);
            currentRotary = ParseRotaryMotion(line, currentRotary);
            if (explicitMotion)
                currentMotionCode = ExtractMotionCode(line);

            Move move;
            move.Code = currentMotionCode;
            move.MachinePosition = currentPosition;
            move.Position = ApplyRotaryAxes(currentPosition, currentRotary);
            move.RotaryDegrees = currentRotary;
            move.ToolNumber = currentTool;
            move.LineNo = lineNo;
            move.ToolpathNumber = currentToolpath;
            move.IsLaserPath = currentLaser;
            m_Moves.push_back(move);
            m_ToolpathBounds.Expand(move.Position);
        }

        if (m_Is4Axis)
            InterpolateRotaryMoves();
        else
            MarkMillingStartFlags();

        CenterDisplayPositions();
    }

    static void TrimInPlace(std::string& line)
    {
        const auto begin = std::find_if_not(line.begin(), line.end(),
            [](unsigned char c) { return std::isspace(c); });
        const auto end = std::find_if_not(line.rbegin(), line.rend(),
            [](unsigned char c) { return std::isspace(c); }).base();
        line = begin < end ? std::string(begin, end) : std::string();
    }

    static std::map<std::string, std::string> ParseMkrKeyValues(const std::string& line)
    {
        std::map<std::string, std::string> values;
        size_t tokenStart = line.find('|');
        if (tokenStart == std::string::npos)
            return values;
        tokenStart = line.find('|', tokenStart + 1);
        while (tokenStart != std::string::npos)
        {
            const size_t next = line.find('|', tokenStart + 1);
            const std::string token = line.substr(tokenStart + 1,
                next == std::string::npos ? std::string::npos : next - tokenStart - 1);
            const size_t equal = token.find('=');
            if (equal != std::string::npos)
                values[token.substr(0, equal)] = token.substr(equal + 1);
            tokenStart = next;
        }
        return values;
    }

    static float ParseFloat(const std::map<std::string, std::string>& values, const std::string& key, float fallback = 0.0f)
    {
        const auto it = values.find(key);
        return it == values.end() || it->second.empty() ? fallback : std::stof(it->second);
    }

    static int ParseInt(const std::map<std::string, std::string>& values, const std::string& key, int fallback = 0)
    {
        const auto it = values.find(key);
        return it == values.end() || it->second.empty() ? fallback : std::stoi(it->second);
    }

    static bool IsMotionLine(const std::string& line)
    {
        return line.rfind("G0", 0) == 0 || line.rfind("G1", 0) == 0;
    }

    static bool ContainsCommand(const std::string& line, const std::string& command)
    {
        size_t pos = line.find(command);
        while (pos != std::string::npos)
        {
            const bool startsWord = pos == 0 || std::isspace(static_cast<unsigned char>(line[pos - 1]));
            const size_t end = pos + command.size();
            const bool endsWord = end == line.size() ||
                std::isspace(static_cast<unsigned char>(line[end])) || line[end] == ';';
            if (startsWord && endsWord)
                return true;
            pos = line.find(command, pos + 1);
        }
        return false;
    }

    static bool HasCoordinateWords(const std::string& line)
    {
        float value = 0.0f;
        return TryParseAxisValue(line, 'X', value) ||
               TryParseAxisValue(line, 'Y', value) ||
               TryParseAxisValue(line, 'Z', value) ||
               TryParseAxisValue(line, 'A', value) ||
               TryParseAxisValue(line, 'B', value) ||
               TryParseAxisValue(line, 'C', value);
    }

    static bool IsModalMotionContinuationLine(const std::string& line, const std::string& currentMotionCode)
    {
        return !currentMotionCode.empty() && HasCoordinateWords(line);
    }

    static std::string ExtractMotionCode(const std::string& line)
    {
        if (line.size() >= 3 && std::isdigit(static_cast<unsigned char>(line[2])))
            return std::string() + line[0] + line[2];
        if (line.size() >= 2)
            return line.substr(0, 2);
        return line;
    }

    static int ParseToolNumber(const std::string& line)
    {
        size_t pos = 1;
        while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])))
            ++pos;
        return pos > 1 ? std::stoi(line.substr(1, pos - 1)) : 0;
    }

    static bool TryParseAxisValue(const std::string& line, char axis, float& value)
    {
        const size_t pos = line.find(axis);
        if (pos == std::string::npos || pos + 1 >= line.size())
            return false;
        const char first = line[pos + 1];
        if (!(std::isdigit(static_cast<unsigned char>(first)) || first == '-' || first == '+' || first == '.'))
            return false;

        size_t end = pos + 1;
        while (end < line.size())
        {
            const char ch = line[end];
            if (!(std::isdigit(static_cast<unsigned char>(ch)) || ch == '-' || ch == '+' || ch == '.'))
                break;
            ++end;
        }
        value = std::stof(line.substr(pos + 1, end - pos - 1));
        return true;
    }

    static glm::vec3 ParseMotionLine(const std::string& line, const glm::vec3& current)
    {
        glm::vec3 next = current;
        TryParseAxisValue(line, 'X', next.x);
        TryParseAxisValue(line, 'Y', next.y);
        TryParseAxisValue(line, 'Z', next.z);
        return next;
    }

    glm::vec3 ParseRotaryMotion(const std::string& line, const glm::vec3& current)
    {
        glm::vec3 next = current;
        bool changed = false;
        changed |= TryParseAxisValue(line, 'A', next.x);
        changed |= TryParseAxisValue(line, 'B', next.y);
        changed |= TryParseAxisValue(line, 'C', next.z);
        if (changed)
        {
            m_HasRotaryMotion = true;
            m_Is4Axis = true;
        }
        return next;
    }

    static glm::vec3 RotateAroundX(const glm::vec3& p, float degrees)
    {
        const float radians = glm::radians(degrees);
        const float c = std::cos(radians);
        const float s = -std::sin(radians);
        return glm::vec3(p.x, p.y * c - p.z * s, p.y * s + p.z * c);
    }

    static glm::vec3 RotateAroundY(const glm::vec3& p, float degrees)
    {
        const float radians = glm::radians(degrees);
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return glm::vec3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
    }

    static glm::vec3 RotateAroundZ(const glm::vec3& p, float degrees)
    {
        const float radians = glm::radians(degrees);
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return glm::vec3(p.x * c - p.y * s, p.x * s + p.y * c, p.z);
    }

    static glm::vec3 ApplyRotaryAxes(const glm::vec3& position, const glm::vec3& rotaryDegrees)
    {
        glm::vec3 rotated = RotateAroundX(position, rotaryDegrees.x);
        rotated = RotateAroundY(rotated, rotaryDegrees.y);
        return RotateAroundZ(rotated, rotaryDegrees.z);
    }

    static float RotaryRadiusAroundX(const glm::vec3& p)
    {
        return std::sqrt(p.y * p.y + p.z * p.z);
    }

    static bool IsMillingStartTransition(const Move& prev, const Move& curr)
    {
        if (curr.Code != "G1" || curr.ToolNumber <= 0)
            return false;
        return prev.ToolNumber != curr.ToolNumber || prev.Code != "G1";
    }

    static int ParseToolpathStartLine(const std::string& line)
    {
        const auto values = ParseMkrKeyValues(line);
        return ParseInt(values, "toolpath_number", 0);
    }

    static ToolInfo ParseToolLine(const std::string& line)
    {
        const auto values = ParseMkrKeyValues(line);
        ToolInfo tool;
        tool.Number = ParseInt(values, "number");
        tool.Id = values.count("id") ? values.at("id") : "";
        tool.Name = values.count("name") ? values.at("name") : "";
        tool.Type = values.count("type") ? values.at("type") : "";
        tool.HandleDiameter = ParseFloat(values, "handlediameter");
        tool.StickLength = ParseFloat(values, "sticklength");
        tool.ShoulderLength = ParseFloat(values, "shoulderlength");
        tool.FluteLength = ParseFloat(values, "flutelength");
        tool.Diameter = ParseFloat(values, "diameter");
        tool.TipDiameter = ParseFloat(values, "tipdiameter", tool.Diameter);
        tool.CornerRadius = ParseFloat(values, "cornerradius");
        tool.Angle = ParseFloat(values, "angle");
        tool.HalfAngle = ParseFloat(values, "halfAngle");
        return tool;
    }

    void EnsureLaserTool()
    {
        if (m_ToolIndexByNumber.find(kLaserToolNumber) != m_ToolIndexByNumber.end())
            return;

        ToolInfo tool;
        tool.Number = kLaserToolNumber;
        tool.Id = "M321";
        tool.Name = "Laser";
        tool.Type = "Laser";
        tool.Diameter = 2.0f * kLaserRadius;
        tool.TipDiameter = tool.Diameter;
        tool.FluteLength = kLaserHeight;
        tool.StickLength = kLaserHeight;
        BuildToolProfile(tool);
        m_ToolIndexByNumber[tool.Number] = m_Tools.size();
        m_Tools.push_back(tool);
    }

    void EnsureDefaultTool(int toolNumber)
    {
        if (toolNumber <= 0 || m_ToolIndexByNumber.find(toolNumber) != m_ToolIndexByNumber.end())
            return;

        ToolInfo tool;
        tool.Number = toolNumber;
        tool.Name = "Tool " + std::to_string(toolNumber);
        tool.Type = "Flat End";
        tool.Diameter = 3.175f;
        tool.TipDiameter = tool.Diameter;
        tool.FluteLength = 12.0f;
        tool.StickLength = 20.0f;
        BuildToolProfile(tool);
        m_ToolIndexByNumber[tool.Number] = m_Tools.size();
        m_Tools.push_back(tool);
    }

    static void BuildToolProfile(ToolInfo& tool)
    {
        const float radius = std::max(0.1f, 0.5f * tool.Diameter);
        const float tipRadius = 0.5f * (tool.TipDiameter > 0.0f ? tool.TipDiameter : tool.Diameter);
        const float fluteLength = std::max(tool.FluteLength, 5.0f);
        tool.Xs.clear();
        tool.Ys.clear();

        if (tool.Type == "Ball Nose")
        {
            const int samples = 12;
            for (int i = 0; i <= samples; ++i)
            {
                const float y = radius * (float)i / (float)samples;
                const float r = std::sqrt(std::max(0.0f, radius * radius - (radius - y) * (radius - y)));
                tool.Xs.push_back(r);
                tool.Ys.push_back(y);
            }
            if (fluteLength > radius)
            {
                tool.Xs.push_back(radius);
                tool.Ys.push_back(fluteLength);
            }
            return;
        }

        if (tool.Type == "Engraving" || tool.Type == "V-bit")
        {
            const float tangent = std::tan(glm::radians(tool.HalfAngle));
            const float coneHeight = tangent > 1e-6f ? (radius - tipRadius) / tangent : fluteLength;
            const float profileHeight = std::min(fluteLength, std::max(coneHeight, 0.1f));
            const int samples = 8;
            for (int i = 0; i <= samples; ++i)
            {
                const float y = profileHeight * (float)i / (float)samples;
                tool.Xs.push_back(std::min(radius, tipRadius + y * tangent));
                tool.Ys.push_back(y);
            }
            if (fluteLength > profileHeight)
            {
                tool.Xs.push_back(radius);
                tool.Ys.push_back(fluteLength);
            }
            return;
        }

        tool.Xs = { radius, radius };
        tool.Ys = { 0.0f, fluteLength };
    }

    void InterpolateRotaryMoves()
    {
        if (m_Moves.empty())
            return;

        std::vector<Move> interpolated;
        interpolated.reserve(m_Moves.size());
        for (size_t i = 0; i < m_Moves.size(); i++)
        {
            const Move& curr = m_Moves[i];
            if (i == 0)
            {
                interpolated.push_back(curr);
                continue;
            }

            const Move& prev = m_Moves[i - 1];
            const bool millingStart = IsMillingStartTransition(prev, curr);
            const glm::vec3 startMachine = RotateAroundX(prev.Position, -prev.RotaryDegrees.x);
            const glm::vec3 endMachine = RotateAroundX(curr.Position, -curr.RotaryDegrees.x);
            const glm::vec3 machineDelta = endMachine - startMachine;
            const float machineDistance = glm::length(machineDelta);
            const float angleDistance = std::abs(curr.RotaryDegrees.x - prev.RotaryDegrees.x);
            const float maxRadius = std::max(RotaryRadiusAroundX(startMachine), RotaryRadiusAroundX(endMachine));
            const float rotaryArcLength = maxRadius * angleDistance * 3.14159265358979323846f / 180.0f;
            const int stepCount = std::max(1, std::max(
                (int)std::ceil(machineDistance / kLinearInterpolationStep),
                std::max((int)std::ceil(angleDistance / kRotaryInterpolationDegrees),
                    (int)std::ceil(rotaryArcLength / kRotaryInterpolationArcStep))));

            for (int step = 0; step <= stepCount; step++)
            {
                const float t = (float)step / (float)stepCount;
                Move move = curr;
                move.MachinePosition = startMachine + machineDelta * t;
                move.RotaryDegrees.x = prev.RotaryDegrees.x + (curr.RotaryDegrees.x - prev.RotaryDegrees.x) * t;
                move.Position = RotateAroundX(move.MachinePosition, move.RotaryDegrees.x);
                move.IsMillingStart = millingStart && step == 0;
                interpolated.push_back(move);
            }
        }
        m_Moves = std::move(interpolated);
        RebuildToolpathBounds();
    }

    void MarkMillingStartFlags()
    {
        if (m_Moves.empty())
            return;
        m_Moves[0].IsMillingStart = m_Moves[0].Code == "G1" && m_Moves[0].ToolNumber > 0;
        for (size_t i = 1; i < m_Moves.size(); ++i)
            m_Moves[i].IsMillingStart = IsMillingStartTransition(m_Moves[i - 1], m_Moves[i]);
    }

    void RebuildToolpathBounds()
    {
        m_ToolpathBounds = {};
        for (const Move& move : m_Moves)
            m_ToolpathBounds.Expand(move.Position);
    }

    void CenterDisplayPositions()
    {
        if (!m_ToolpathBounds.Valid)
            return;
        const glm::vec3 center = (m_ToolpathBounds.Min + m_ToolpathBounds.Max) * 0.5f;
        m_DisplayBounds = {};
        for (Move& move : m_Moves)
        {
            move.Position -= center;
            m_DisplayBounds.Expand(move.Position);
        }
    }

    std::string m_SourceName;
    std::vector<ToolInfo> m_Tools;
    std::map<int, size_t> m_ToolIndexByNumber;
    std::vector<Move> m_Moves;
    BoundingBox m_ToolpathBounds;
    BoundingBox m_DisplayBounds;
    bool m_HasRotaryMotion = false;
    bool m_Is4Axis = false;

    static constexpr int kLaserToolNumber = 321;
    static constexpr float kLaserRadius = 0.2f;
    static constexpr float kLaserHeight = 10.0f;
    static constexpr float kLinearInterpolationStep = 1.0f;
    static constexpr float kRotaryInterpolationDegrees = 1.0f;
    static constexpr float kRotaryInterpolationArcStep = 0.15f;
};
}
