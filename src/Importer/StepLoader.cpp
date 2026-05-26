#include "stdsfx.h"
#include "StepLoader.h"

#include <Log.h>

#include <BRep_Tool.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Trsf.hxx>

namespace
{
std::filesystem::path AsciiReadablePath(const std::filesystem::path& filepath, std::filesystem::path& temporaryPath)
{
    const std::string utf8 = filepath.u8string();
    const bool isAscii = std::all_of(utf8.begin(), utf8.end(), [](unsigned char c) { return c < 128; });
    if (isAscii)
        return filepath;

    temporaryPath = std::filesystem::temp_directory_path() / "kengine_step_import.step";
    std::error_code error;
    std::filesystem::copy_file(filepath, temporaryPath, std::filesystem::copy_options::overwrite_existing, error);
    return error ? std::filesystem::path() : temporaryPath;
}

glm::vec3 Position(const gp_Pnt& point)
{
    return glm::vec3(static_cast<float>(point.X()), static_cast<float>(point.Y()), static_cast<float>(point.Z()));
}

glm::vec3 Normal(const gp_Dir& normal)
{
    return glm::vec3(static_cast<float>(normal.X()), static_cast<float>(normal.Y()), static_cast<float>(normal.Z()));
}
}

bool StepLoader::Load(const std::filesystem::path& filepath, StepMeshData& mesh)
{
    mesh = {};

    std::filesystem::path temporaryPath;
    const std::filesystem::path readPath = AsciiReadablePath(filepath, temporaryPath);
    if (readPath.empty())
    {
        mesh.Error = "Unable to create an ASCII temporary file for OCCT";
        return false;
    }

    STEPControl_Reader reader;
    const IFSelect_ReturnStatus status = reader.ReadFile(readPath.string().c_str());
    if (!temporaryPath.empty())
    {
        std::error_code error;
        std::filesystem::remove(temporaryPath, error);
    }
    if (status != IFSelect_RetDone)
    {
        mesh.Error = "OCCT failed to read STEP file";
        return false;
    }
    if (reader.TransferRoots() <= 0)
    {
        mesh.Error = "OCCT did not transfer any STEP roots";
        return false;
    }

    const TopoDS_Shape shape = reader.OneShape();
    if (shape.IsNull())
    {
        mesh.Error = "OCCT returned an empty shape";
        return false;
    }

    // Absolute chordal deflection in model units and angular deflection in radians.
    BRepMesh_IncrementalMesh tessellator(shape, 0.1, false, 0.35, true);
    if (!tessellator.IsDone())
    {
        mesh.Error = "OCCT triangulation failed";
        return false;
    }

    TopTools_IndexedMapOfShape extractedEdges;
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next())
    {
        const TopoDS_Face face = TopoDS::Face(explorer.Current());
        TopLoc_Location location;
        Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull() || triangulation->NbTriangles() == 0)
            continue;

        if (!triangulation->HasNormals())
            triangulation->ComputeNormals();

        const gp_Trsf transform = location.Transformation();
        const bool reversed = face.Orientation() == TopAbs_REVERSED;
        const uint32_t baseVertex = static_cast<uint32_t>(mesh.Vertices.size());
        mesh.Vertices.reserve(mesh.Vertices.size() + triangulation->NbNodes());
        mesh.Indices.reserve(mesh.Indices.size() + triangulation->NbTriangles() * 3);

        for (Standard_Integer node = 1; node <= triangulation->NbNodes(); ++node)
        {
            const gp_Pnt position = triangulation->Node(node).Transformed(transform);
            gp_Dir normal = triangulation->Normal(node);
            normal.Transform(transform);
            if (reversed)
                normal.Reverse();

            glm::vec2 uv(0.0f);
            if (triangulation->HasUVNodes())
            {
                const gp_Pnt2d uvNode = triangulation->UVNode(node);
                uv = glm::vec2(static_cast<float>(uvNode.X()), static_cast<float>(uvNode.Y()));
            }
            mesh.Vertices.emplace_back(Position(position), Normal(normal), uv);
        }

        for (Standard_Integer triangle = 1; triangle <= triangulation->NbTriangles(); ++triangle)
        {
            Standard_Integer n1 = 0;
            Standard_Integer n2 = 0;
            Standard_Integer n3 = 0;
            triangulation->Triangle(triangle).Get(n1, n2, n3);
            if (reversed)
                std::swap(n2, n3);
            mesh.Indices.push_back(baseVertex + static_cast<uint32_t>(n1 - 1));
            mesh.Indices.push_back(baseVertex + static_cast<uint32_t>(n2 - 1));
            mesh.Indices.push_back(baseVertex + static_cast<uint32_t>(n3 - 1));
        }

        for (TopExp_Explorer edgeExplorer(face, TopAbs_EDGE); edgeExplorer.More(); edgeExplorer.Next())
        {
            const TopoDS_Edge edge = TopoDS::Edge(edgeExplorer.Current());
            if (extractedEdges.Contains(edge))
                continue;

            const Handle(Poly_PolygonOnTriangulation)& polygon =
                BRep_Tool::PolygonOnTriangulation(edge, triangulation, location);
            if (polygon.IsNull() || polygon->NbNodes() < 2)
                continue;

            extractedEdges.Add(edge);
            for (Standard_Integer node = 1; node < polygon->NbNodes(); ++node)
            {
                const gp_Pnt first = triangulation->Node(polygon->Node(node)).Transformed(transform);
                const gp_Pnt second = triangulation->Node(polygon->Node(node + 1)).Transformed(transform);
                mesh.EdgeVertices.push_back(Position(first));
                mesh.EdgeVertices.push_back(Position(second));
            }
        }
    }

    if (mesh.Indices.empty())
    {
        mesh.Error = "OCCT produced no triangulated faces";
        return false;
    }

    INFO("OCCT STEP import triangulated {} vertices, {} triangles and {} edge segments: {}",
        mesh.Vertices.size(), mesh.Indices.size() / 3, mesh.EdgeVertices.size() / 2, filepath.u8string());
    return true;
}
