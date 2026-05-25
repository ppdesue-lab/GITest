#include "stdsfx.h"
#include "Object3D.h"
#include <Renderer/RenderCommand.h>

#include <functional>
#include <fstream>
#include <cctype>

#include "Application.h"


void Mesh::Draw(const glm::mat4& view,const glm::mat4 proj)
{
	Mat->Bind();
	Mat->MatShader->SetMat4("u_View", view);
	Mat->MatShader->SetMat4("u_Projection", proj);
	Mat->MatShader->SetMat4("u_Model", Transfm.GetMatrix());
	{
		//phong stuff
		glm::vec3 viewpos = glm::vec3(glm::inverse(view)[3]);
		Mat->MatShader->SetFloat3("u_lightPos", glm::vec3(0,10,0));
		Mat->MatShader->SetFloat3("u_viewPos", viewpos);
		Mat->MatShader->SetFloat3("u_lightColor", glm::vec3(0, 1, 0));
		Mat->MatShader->SetFloat3("u_objectColor", glm::vec3(1, 1, 1));

		// CSM shadow uniforms (set when shader supports them)
		Application& app = Application::Get();
		CSM& csm = app.GetCSM();
		auto& lightViewProj = csm.GetLightViewProjMatrices();
		auto& cascadeDists = csm.GetCascadeDistances();
		uint32_t cascadeCount = csm.Enabled() ? csm.GetCascadeCount() : 0;
		auto lightDir = csm.GetLight().Direction;

		Mat->MatShader->SetFloat3("u_lightPos", -glm::normalize(lightDir) * 1000.0f);
		Mat->MatShader->SetFloat3("u_lightColor", csm.GetLight().Color * csm.GetLight().Intensity);
		Mat->MatShader->SetInt("u_cascadeCount", (int)cascadeCount);
		Mat->MatShader->SetFloat("u_shadowMapSize", (float)csm.GetShadowMapSize());
		Mat->MatShader->SetFloat3("u_lightDir", lightDir);
		Mat->MatShader->SetInt("u_debugCascadeView", Application::Get().GetDebugCascadeView() ? 1 : 0);
		for (uint32_t i = 0; i <= cascadeCount && i < 4; i++)
		{
			Mat->MatShader->SetFloat("u_cascadeDistances[" + std::to_string(i) + "]", cascadeDists[i]);
			if (i < cascadeCount)
				Mat->MatShader->SetMat4("u_lightViewProj[" + std::to_string(i) + "]", lightViewProj[i]);
		}

		csm.BindShadowTexture(2);
		Mat->MatShader->SetInt("u_shadowMap", 2);
		app.GetProbeGI().Bind(Mat->MatShader);
		app.GetPBRIBL().Bind(Mat->MatShader);
	}
    RenderCommand::DrawIndexed(VertexObject);
};

void Object3D::Draw(const glm::mat4& view,const glm::mat4 proj)
{
	for (auto& mesh : Meshes)
		mesh->Draw(view,proj);
}


#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

template<typename T>
bool Object3D::Load(const std::string& filepath) {
	return LoadFromPath<T>(std::filesystem::u8path(filepath));
}

template<typename T>
bool Object3D::LoadFromPath(const std::filesystem::path& filepath) { 

	// Read file into memory using std::filesystem::path (handles encoding on Windows)
	std::ifstream file(filepath, std::ios::binary);
	if (!file)
	{
		std::cerr << "Failed to open file: " << filepath << std::endl;
		return false;
	}
	std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	file.close();

	std::vector<T> vertices;
	std::vector<uint32_t> indices;
	auto processMesh = [&](aiMesh* mesh, const aiScene* scene) {

		uint32_t indices_offset = indices.size();
			//vector<Texture> textures;
			for (unsigned int i = 0; i < mesh->mNumVertices; i++)
			{
				// process vertex positions, normals and texture coordinates
				aiVector3D position = mesh->mVertices[i];
				T vertex;
				vertex.Position = glm::vec3(position.x, position.y, position.z);
				if constexpr (std::is_same_v<T, VertexColor>)
				{
					vertex.Color = glm::vec4(1, 0, 0, 1);
				}
				else if constexpr (std::is_same_v<T, VertexNormal>)
				{
					vertex.Normal = glm::vec3(1, 0, 0);

					if (mesh->mNormals)
					{
						vertex.Normal.x = mesh->mNormals[i].x;
						vertex.Normal.y = mesh->mNormals[i].y;
						vertex.Normal.z = mesh->mNormals[i].z;
					}
				}
				else if constexpr (std::is_same_v<T, VertexTexture>)
				{
					vertex.TexCoord = glm::vec2(0, 0);

					if (mesh->HasTextureCoords(0))
					{
						vertex.TexCoord.x = mesh->mTextureCoords[0][i].x;
						vertex.TexCoord.y = mesh->mTextureCoords[0][i].y;
					}
				}
				else if constexpr (std::is_same_v<T, VertexNormalTexture>)
				{
					vertex.Normal = glm::vec3(1, 0, 0);

					if (mesh->mNormals)
					{
						vertex.Normal.x = mesh->mNormals[i].x;
						vertex.Normal.y = mesh->mNormals[i].y;
						vertex.Normal.z = mesh->mNormals[i].z;
					}

					vertex.TexCoord = glm::vec2(0, 0);

					if (mesh->HasTextureCoords(0))
					{
						vertex.TexCoord.x = mesh->mTextureCoords[0][i].x;
						vertex.TexCoord.y = mesh->mTextureCoords[0][i].y;
					}
				}
				vertices.push_back(vertex);
			}
			// process indices
			for (uint32_t i = 0; i < mesh->mNumFaces; i++)
			{
				aiFace face = mesh->mFaces[i];
				for (uint32_t j = 0; j < face.mNumIndices; j++)
					indices.push_back(indices_offset + face.mIndices[j]);
			}
		};
	
	std::function<void(aiNode* node, const aiScene* scene)> processNode;
	processNode = [&](aiNode* node, const aiScene* scene)
	{
		// process all the node's meshes (if any)
		for (unsigned int i = 0; i < node->mNumMeshes; i++)
		{
			aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
			processMesh(mesh, scene);
		}
		// then do the same for each of its children
		for (unsigned int i = 0; i < node->mNumChildren; i++)
		{
			processNode(node->mChildren[i], scene);
		}
	};
	
	Assimp::Importer importer;
	std::string hint = filepath.extension().string();
	if (!hint.empty() && hint[0] == '.') hint = hint.substr(1);
	std::transform(hint.begin(), hint.end(), hint.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	unsigned int flags = aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs;
	const aiScene* scene = nullptr;

	// Try ReadFile with the ANSI path first.
	// Then try ReadFileFromMemory from the pre-loaded buffer.
	// For paths with non-ASCII chars, copy to a temp ASCII path first.

	// Check if the path is pure ASCII (no encoding issues with fopen)
	bool isPureAscii = true;
	auto u8str = filepath.u8string();
	for (char c : u8str) {
		if (static_cast<unsigned char>(c) > 127) { isPureAscii = false; break; }
	}

	if (isPureAscii) {
		scene = importer.ReadFile(filepath.string().c_str(), flags);
	} else {
		// Create a temp file with pure ASCII name
		auto tmpDir = std::filesystem::temp_directory_path();
		auto tmpPath = tmpDir / ("kengine_" + hint + ".tmp");
		bool copied = false;
		try {
			std::filesystem::copy_file(filepath, tmpPath, std::filesystem::copy_options::overwrite_existing);
			copied = true;
		} catch (...) {}
		if (copied) {
			scene = importer.ReadFile(tmpPath.string().c_str(), flags);
			std::error_code ec;
			std::filesystem::remove(tmpPath, ec);
		}
	}
	if (!scene) {
		scene = importer.ReadFileFromMemory(buffer.data(), buffer.size(), flags, hint.c_str());
	}
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
		std::cerr << "assimp error: " << importer.GetErrorString() << std::endl;
		std::cerr << "assimp Failed to load mesh: " << filepath.u8string() << std::endl;
		return false;
	}
	processNode(scene->mRootNode, scene);
	
	//process mesh
	
	//generate VertexArray from mesh
	Ref<Mesh> mesh = CreateRef<Mesh>();
	auto VertexObject = VertexArray::Create();
	auto vbuffer = VertexBuffer::Create((float*)vertices.data(), vertices.size() * sizeof(T));
	BufferLayout layout;
	Ref<Material> meshmat = nullptr;
	if constexpr (std::is_same_v<T, VertexColor>)
	{
		layout = {
			BufferElement(ShaderDataType::Float3,"a_Position",false),
			BufferElement(ShaderDataType::Float4,"a_Color",false),
		};

		meshmat = CreateRef<MaterialColor>();
	}
	else if constexpr (std::is_same_v<T, VertexNormal>)
	{
		layout = {
			BufferElement(ShaderDataType::Float3,"a_Position",false),
			BufferElement(ShaderDataType::Float3,"a_Normal",false),
		};
		meshmat = CreateRef<MaterialPBR>();
	}
	else if constexpr (std::is_same_v<T, VertexNormalTexture>)
	{
		layout = {
			BufferElement(ShaderDataType::Float3,"a_Position",false),
			BufferElement(ShaderDataType::Float3,"a_Normal",false),
			BufferElement(ShaderDataType::Float2,"a_TexCoord",false),
		};
		meshmat = CreateRef<MaterialPBR>(
			"E:/githubs/MapleEngine-main/Assets/textures/rusted_iron");
	}

	vbuffer->SetLayout(layout);
	auto ibuffer = IndexBuffer::Create(indices.data(), indices.size());
	VertexObject->AddVertexBuffer(vbuffer);
	VertexObject->SetIndexBuffer(ibuffer);
	VertexObject->Unbind();
	mesh->VertexObject = VertexObject;

	mesh->Mat = meshmat;
	Meshes.clear();
	Meshes.push_back(mesh);

	return true;

}
//template bool Object3D::Load<VertexBase>(const std::string& filepath);
template bool Object3D::Load<VertexColor>(const std::string& filepath);
template bool Object3D::Load<VertexNormal>(const std::string& filepath);
template bool Object3D::LoadFromPath<VertexColor>(const std::filesystem::path& filepath);
template bool Object3D::LoadFromPath<VertexNormal>(const std::filesystem::path& filepath);
template bool Object3D::LoadFromPath<VertexNormalTexture>(const std::filesystem::path& filepath);


//
//template<typename VertexBase>
//bool Object3D::Load<VertexBase>(const std::string& filepath)
//{
//	std::vector<VertexBase> vertices;
//	std::vector<uint32_t> indices;
//	auto processMesh = [&](aiMesh* mesh, const aiScene* scene) {
//			//vector<Texture> textures;
//			for (unsigned int i = 0; i < mesh->mNumVertices; i++)
//			{
//				// process vertex positions, normals and texture coordinates
//				aiVector3D position = mesh->mVertices[i];
//				VertexBase vertex;
//				vertex.Position = glm::vec3(position.x, position.y, position.z);
//				vertices.push_back(vertex);
//			}
//			// process indices
//			uint32_t indices_offset = indices.size();
//			for (uint32_t i = 0; i < mesh->mNumFaces; i++)
//			{
//				aiFace face = mesh->mFaces[i];
//				for (uint32_t j = 0; j < face.mNumIndices; j++)
//					indices.push_back(indices_offset + face.mIndices[j]);
//			}
//		};
//
//	auto processNode = [&](aiNode * node, const aiScene * scene)
//	{
//		// process all the node's meshes (if any)
//		for (unsigned int i = 0; i < node->mNumMeshes; i++)
//		{
//			aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
//			meshes.push_back(processMesh(mesh, scene));
//		}
//		// then do the same for each of its children
//		for (unsigned int i = 0; i < node->mNumChildren; i++)
//		{
//			processNode(node->mChildren[i], scene);
//		}
//	};
//
//	Assimp::Importer importer;
//	const aiScene* scene = importer.ReadFile(filepath, aiProcess_Triangulate | aiProcess_FlipUVs);
//	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
//		std::cerr << "Failed to load mesh: " << filepath << std::endl;
//		return false;
//	}
//	processNode(scene->mRootNode, scene);
//
//	//process mesh
//
//	//generate VertexArray from mesh
//	VertexObject = VertexArray::Create();
//	auto vbuffer = VertexBuffer::Create((float*)vertices.data(), vertices.size() * sizeof(VertexBase));
//	BufferLayout layout = {
//		BufferElement(ShaderDataType::Float3,"a_Position",false),
//	};
//	vbuffer->SetLayout(layout);
//	auto ibuffer = IndexBuffer::Create(indices.data(), indices.size());
//	VertexObject.AddVertexBuffer(vbuffer);
//	VertexObject.SetIndexBuffer(ibuffer);
//
//	return true;
//}
